/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// CHANGELOG!: see changelog_screen.h.

#include <openglad/interface/ui/changelog_screen.h>

#include <openglad/core/test_trace.h>
#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/video.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/web_control_defaults.h>
#include <openglad/resources/filesystem_sync.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/og_file.h>

#include "picker_sdl_defs.h"

#include <array>
#include <charconv>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace og::ui {

namespace {

// The OK row at its place on the finished page (centred under the picture).
constexpr MenuButtonSpec kChangelogRows[] = {
    // No hotkey: a button hotkey fires on a HELD key, and Return, Escape
    // and Space all close the page through changelog_frame_tick instead,
    // which ignores a key that was already down when the page opened.
    {.id = "changelog_ok", .label = "OK",
     .x = 135, .y = kChangelogOkY, .w = 50, .h = 16,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT},
};

// The page's geometry is in changelog_screen.h (the title, the text lines,
// and the picture band between the text and OK). Nothing is drawn on the
// menu canvas inside the band: the picture's native plane covers it.
constexpr int kTitleY = kChangelogTitleY;
constexpr int kLinesTopY = kChangelogLinesTopY;
constexpr int kLinePitch = kChangelogLinePitch;
constexpr int kBandX = kChangelogBandX;
constexpr int kBandY = kChangelogBandY;
constexpr int kBandW = kChangelogBandW;
constexpr int kBandH = kChangelogBandH;
// The last line ends kChangelogBandGap rows above the band, which ends as
// far above OK.
static_assert(kChangelogLinesBottomY + kChangelogBandGap <= kBandY,
              "the text lines must end above the picture band, with a gap");
static_assert(kBandY + kBandH + kChangelogBandGap <= kChangelogRows[0].y,
              "the picture band must end above OK, with a gap");
static_assert(kTitleY + 12 <= kLinesTopY,
              "the big-font title (12 px) ends above the first line");

std::vector<button>& changelog_buttons()
{
    static std::vector<button> buttons;
    return buttons;
}

// What one showing of the page reads every frame.
struct ChangelogScreenState {
    const ChangelogEntry* entry = nullptr;
    std::optional<og::io::RgbaImage> picture;
    // Set once Return, Escape and Space have all been seen up: until then a
    // closing key is the one that skipped the intro, still held (or its
    // autorepeat), and must not close a page nobody has seen yet.
    bool keys_seen_up = false;
};

screen* page_screen()
{
    return og::runtime::current_session->myscreen_;
}

// The aspect-fitted rectangle of a w x h picture inside a box_w x box_h
// plane, centred.
struct FitRect {
    int x, y, w, h;
};
FitRect fit_picture(int w, int h, int box_w, int box_h)
{
    // Compare w/h with box_w/box_h without division.
    if (static_cast<long long>(w) * box_h >= static_cast<long long>(box_w) * h) {
        const int fit_h = static_cast<int>(static_cast<long long>(h) * box_w / w);
        return {0, (box_h - fit_h) / 2, box_w, fit_h};
    }
    const int fit_w = static_cast<int>(static_cast<long long>(w) * box_h / h);
    return {(box_w - fit_w) / 2, 0, fit_w, box_h};
}

// The picture through a native plane: its pixels are the display's own, so
// the keyboard's labels stay sharp at any window size the band reaches.
bool draw_picture(const og::io::RgbaImage& picture)
{
    screen* const scr = page_screen();
    const std::array<NativeWorldViewDestination, 1> destinations = {
        NativeWorldViewDestination{CanvasTarget::UI, kBandX, kBandY, kBandW,
                                   kBandH}};
    const NativeWorldViewSource source =
        scr->begin_native_world_view(destinations);
    if (!source)
        return false;
    const FitRect fit = fit_picture(picture.w, picture.h, source.w, source.h);
    if (!scr->draw_rgba_scaled(picture, fit.x, fit.y, fit.w, fit.h)) {
        scr->cancel_native_world_view();
        return false;
    }
    return scr->end_native_world_view();
}

void changelog_draw_background(void* screen_state)
{
    options_panel_draw_background(nullptr);
    const auto* const state = static_cast<ChangelogScreenState*>(screen_state);
    if (state != nullptr && state->picture && draw_picture(*state->picture))
        return;
    // A missing picture, or no plane (a tiny window, a lost device): the
    // band says where the keys are instead.
    TRACE("changelog", "CHANGELOG!: picture unavailable");
    page_screen()->text_normal.write_xy_center(
        160, kBandY + kBandH / 2 - 3, DARK_BLUE, "%s",
        "Keyboard picture: see HELP > Controls");
}

void changelog_draw_content(void* screen_state)
{
    const auto* const state = static_cast<ChangelogScreenState*>(screen_state);
    if (state == nullptr || state->entry == nullptr)
        return;
    screen* const scr = page_screen();
    scr->text_big.write_xy_center(160, kTitleY, DARK_BLUE, "%s",
                                  state->entry->title);
    int y = kLinesTopY;
    for (const char* line : state->entry->lines) {
        scr->text_normal.write_xy_center(160, y, DARK_BLUE, "%s", line);
        y += kLinePitch;
    }
}

bool closing_key_held()
{
    const auto& keys = og::runtime::current_session->keystates_;
    return keys[KEYSTATE_RETURN] || keys[KEYSTATE_ESCAPE] ||
           keys[KEYSTATE_SPACE];
}

// A key-down event for a closing key (Backspace arrives as Escape on the
// web build, web_back_key.h).
bool closing_key_pressed()
{
    if (!query_key_press_event())
        return false;
    const int key = query_key();
    return key == KEYCODE_RETURN || key == KEYCODE_ESCAPE ||
           key == KEYCODE_SPACE;
}

// OK, Return, Escape (Backspace on the web build) and Space close the page.
// The pause menu's rule (pause_menu.cpp, esc_seen_up): a key that was down
// when the page opened -- the intro's skip key, mashed or held -- must be
// seen up first. Until then the frame's key events are dropped too, so an
// autorepeat of the held key cannot close the page either. After that, the
// key held down or its press event closes it.
bool changelog_frame_tick(void* screen_state, int /*frame*/)
{
    auto* const state = static_cast<ChangelogScreenState*>(screen_state);
    if (!state->keys_seen_up) {
        state->keys_seen_up = !closing_key_held();
        clear_keyboard();
        return true;
    }
    return !closing_key_held() && !closing_key_pressed();
}

// The key that closed the page must not reach the main menu, whose QUIT
// row carries Escape.
void drain_closing_keys()
{
    while (closing_key_held()) {
#ifdef __EMSCRIPTEN__
        emscripten_sleep(1);
#else
        og::input_native::sleep_ms(1);
#endif
        get_input_events(POLL);
    }
    clear_keyboard();
}

#ifdef TESTING
bool g_force_changelog = false;
#endif

#if defined(__EMSCRIPTEN__) && !defined(TESTING)
// The Playwright specs boot without the page (tests/e2e/wasm_helpers.js
// installChangelogSkip, and every spec that skips the intro); the changelog
// spec boots without either flag.
EM_JS(int, should_skip_changelog_js, (), {
    return (window.__opengladSkipChangelogForTests ||
            window.__opengladSkipIntroForTests) ? 1 : 0;
});

// What the page did on this boot, for tests/e2e/wasm-changelog.spec.js:
// "none" (nothing pending), "open", then "seen" once the marker is stamped
// and flushed.
EM_JS(void, publish_changelog_state_js, (const char* state), {
    window.__opengladChangelogState = UTF8ToString(state);
});
#endif

void publish_changelog_state([[maybe_unused]] const char* state)
{
#if defined(__EMSCRIPTEN__) && !defined(TESTING)
    publish_changelog_state_js(state);
#endif
}

void show_entry(const ChangelogEntry& entry)
{
    ChangelogScreenState state;
    state.entry = &entry;
    if (const char* image =
            changelog_entry_image(entry, og::input::kWebControlDefaults))
        state.picture = og::io::read_rgba_png(image);
    TRACE("changelog", "CHANGELOG!: showing id=%d picture=%d", entry.id,
          state.picture ? 1 : 0);
    publish_changelog_state("open");
    (void)run_menu_screen(changelog_menu_screen_spec(), &state);
    TRACE("changelog", "CHANGELOG!: closed id=%d", entry.id);
    drain_closing_keys();
}

}  // namespace

const MenuScreenSpec& changelog_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "changelog",
        .rows = kChangelogRows,
        .row_count = static_cast<int>(std::size(kChangelogRows)),
        .buttons_accessor = &picker_changelog_buttons,
        .count_accessor = &picker_changelog_button_count,
        // No lobby exists yet when the page shows, so there is no remote
        // start to follow (the engine test's allowlist names it).
        .remote_start = RemoteStartScope::None,
        .draw_background = &changelog_draw_background,
        .draw_content = &changelog_draw_content,
        .frame_tick = &changelog_frame_tick,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

int changelog_seen_marker()
{
    const std::string text = cfg.get_setting("ui", "changelog_seen");
    int seen = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), seen);
    if (error != std::errc{} || end != text.data() + text.size())
        return 0;
    return seen;
}

const char* changelog_entry_image(const ChangelogEntry& entry, bool web)
{
    return web && entry.web_image != nullptr ? entry.web_image : entry.image;
}

}  // namespace og::ui

void picker_show_pending_changelog()
{
    // The demo (and anything else that plays itself) never stops for it.
    if (og::runtime::current_session == nullptr ||
        og::runtime::current_session->non_interactive_)
        return;
    const int seen = og::ui::changelog_seen_marker();
    const bool pending = seen < og::ui::kChangelogLatestId;
#ifdef TESTING
    TRACE("changelog", "CHANGELOG!: pending=%d seen=%d forced=%d", pending ? 1 : 0,
          seen, og::ui::g_force_changelog ? 1 : 0);
    if (!og::ui::g_force_changelog)
        return;
#endif
    if (!pending) {
        og::ui::publish_changelog_state("none");
        return;
    }
#if defined(__EMSCRIPTEN__) && !defined(TESTING)
    if (og::ui::should_skip_changelog_js())
        return;
#endif
    // Whatever key skipped the intro is still down or queued; the page must
    // not take it as OK.
    clear_keyboard();
    clear_key_press_event();
    for (const og::ui::ChangelogEntry& entry : og::ui::kChangelog)
        if (entry.id > seen)
            og::ui::show_entry(entry);
    // The marker is in memory from here on even if the save fails; the sync
    // is what carries it to IndexedDB on the web build (save_settings alone
    // never does).
    cfg.apply_setting("ui", "changelog_seen",
                      std::to_string(og::ui::kChangelogLatestId));
    (void)cfg.save_settings();
    sync_filesystem();
    og::ui::publish_changelog_state("seen");
    TRACE("changelog", "CHANGELOG!: seen=%d", og::ui::kChangelogLatestId);
}

#ifdef TESTING
void picker_testing_set_force_changelog(bool force)
{
    og::ui::g_force_changelog = force;
}

bool picker_testing_changelog_forced()
{
    return og::ui::g_force_changelog;
}
#endif

button* picker_changelog_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::changelog_menu_screen_spec(),
                                     og::ui::changelog_buttons());
    return og::ui::changelog_buttons().data();
}

int picker_changelog_button_count()
{
    return static_cast<int>(og::ui::changelog_buttons().size());
}
