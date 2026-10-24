/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The CHANGELOG! page (docs/ability-select-design.md F) in real SDL: shown
// from picker_main while cfg ui/changelog_seen is behind the newest entry,
// closed by OK (or Escape) into the main menu, and never again once the
// marker is stamped -- because of the MARKER, not a process latch (clear it
// and the page is back). A demo session never shows it, and a test that
// does not force it gets a trace and nothing else.
//
// The page's picture is drawn through a native plane at the display's own
// resolution, so the 320x200 capture cannot see it: the flows read the
// PHYSICAL frame back (capture_physical_frame) and check the picture is in
// its band and clear of the text. The three window sizes are written as
// PPMs when OG_CHANGELOG_SHOT_DIR names a directory.
//
// Injector flows on tier 2 of scripts/check_injector_settles.sh from birth:
// every settle is a completed engine frame or a named element.

#include <gtest/gtest.h>

#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/changelog_screen.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/sai2x.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>

#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_escape_tail.h"
#include "test_frame_capture.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

namespace
{

inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[static_cast<std::size_t>(i)].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

const char* shot_dir()
{
    return std::getenv("OG_CHANGELOG_SHOT_DIR");
}

// The page's own door first, then the main menu's way out (QUIT returns
// picker_main under TESTING).
constexpr EscapeDoor kChangelogEscapeDoors[] = {
    {"changelog_ok", "changelog_ok"},
    {"quit", "quit"},
};

std::string yaml_path()
{
    return get_user_path() + "/cfg/openglad.yaml";
}

std::string read_text(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

void erase_marker()
{
    auto category = cfg.data.find("ui");
    if (category != cfg.data.end())
        category->second.erase("changelog_seen");
}

// Every test leaves cfg (in memory and on disk) the way it found it, and
// the force flag down (the harness reset is the backstop).
struct MarkerRestore
{
    std::optional<std::string> saved;
    MarkerRestore()
    {
        auto category = cfg.data.find("ui");
        if (category != cfg.data.end()) {
            auto entry = category->second.find("changelog_seen");
            if (entry != category->second.end())
                saved = entry->second;
        }
        erase_marker();
    }
    ~MarkerRestore()
    {
        picker_testing_set_force_changelog(false);
        erase_marker();
        if (saved)
            cfg.apply_setting("ui", "changelog_seen", *saved);
        (void)cfg.save_settings();
    }
    MarkerRestore(const MarkerRestore&) = delete;
    MarkerRestore& operator=(const MarkerRestore&) = delete;
};

// One picker_main with the injector `body`, through the shared exit rule.
struct Flow
{
    std::atomic<bool> test_finished{false};
    int (*body)(Flow&) = nullptr;
    // The physical capture's file name under OG_CHANGELOG_SHOT_DIR.
    const char* shot_name = "changelog";
    // What the injector saw.
    bool page_seen = false;
    bool main_menu_seen = false;
    bool page_seen_on_return = false;
    bool main_menu_stayed = false;
    int marker_on_menu_thread = -1;
    CapturedFrame menu_frame;
    PhysicalFrame physical;
};

int run_flow_body(void* data)
{
    Flow& flow = *static_cast<Flow*>(data);
    return flow.body(flow);
}

int run_picker_flow(Flow& flow)
{
    SDL_Thread* thread = SDL_CreateThread(run_flow_body, "changelog", &flow);
    if (thread == nullptr)
        return -1;
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    flow.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    return thread_result;
}

int leave(Flow& flow, int leg, const char* why)
{
    return escape_to_the_main_thread(flow.test_finished, leg, why,
                                     kChangelogEscapeDoors);
}

// The page: frozen at 320x200 and read back at the display's resolution,
// then OK, then the main menu reads the marker and QUIT.
int page_then_ok(Flow& flow)
{
    if (!wait_for_interactable("changelog_ok", 15000))
        return leave(flow, 1, "the CHANGELOG! page never came up");
    flow.page_seen = true;
    if (!wait_for_menu_frames(2))
        return leave(flow, 2, "the page completed no frames");
    capture_presented_frame("changelog_menu", nullptr);
    {
        std::vector<CapturedFrame> frames = take_captured_frames();
        if (!frames.empty())
            flow.menu_frame = std::move(frames.back());
    }
    flow.physical = capture_physical_frame(flow.shot_name, shot_dir());
    if (!interact("changelog_ok"))
        return leave(flow, 3, "OK was not on the page");
    if (!wait_for_interactable("quit", 15000))
        return leave(flow, 4, "OK did not lead to the main menu");
    flow.main_menu_seen = true;
    if (!wait_for_menu_frames(2))
        return leave(flow, 5, "the main menu completed no frames");
    (void)run_on_main_thread([&flow] {
        flow.marker_on_menu_thread = og::ui::changelog_seen_marker();
    });
    return leave(flow, 0, "");
}

// The main menu with no page in front of it: two settled frames and no OK.
int main_menu_only(Flow& flow)
{
    if (!wait_for_interactable("quit", 15000))
        return leave(flow, 1, "the main menu never came up");
    flow.main_menu_seen = true;
    if (!wait_for_menu_frames(2))
        return leave(flow, 2, "the main menu completed no frames");
    flow.page_seen_on_return = has_interactable("changelog_ok");
    return leave(flow, 0, "");
}

// The page closed by Escape, as a key-press EVENT (a pushed key never moves
// SDL's keystate, test_input_helpers.h; the page honours the event as well
// as the held key); the main menu must not take the same press as its own
// QUIT.
int page_then_escape(Flow& flow)
{
    if (!wait_for_interactable("changelog_ok", 15000))
        return leave(flow, 1, "the CHANGELOG! page never came up");
    flow.page_seen = true;
    if (!wait_for_menu_frames(2))
        return leave(flow, 2, "the page completed no frames");
    inject_key_press(SDLK_ESCAPE);
    if (!wait_for_interactable("quit", 15000))
        return leave(flow, 3, "Escape did not close the page");
    flow.main_menu_seen = true;
    if (!wait_for_menu_frames(3))
        return leave(flow, 4, "the main menu completed no frames");
    flow.main_menu_stayed =
        has_interactable("quit") && !has_interactable("changelog_ok");
    return leave(flow, 0, "");
}

// Return, Escape and Space held down from before the page opened (the
// intro's skip key, mashed or held): the page must stay up until they have
// been seen up, and only a press after that closes it.
struct HeldKeys
{
    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    const bool* saved = nullptr;
    bool stayed_up_while_held = false;
    bool closed_by_a_new_press = false;
};
HeldKeys* g_held = nullptr;

// The page reads the fake keystate on the menu thread, so the injector
// flips it there too (a plain write from this thread is a data race).
bool set_held_keys_on_main_thread(HeldKeys& held, bool return_escape,
                                  bool space)
{
    return run_on_main_thread([&held, return_escape, space] {
        held.keys[static_cast<std::size_t>(KEYSTATE_RETURN)] = return_escape;
        held.keys[static_cast<std::size_t>(KEYSTATE_ESCAPE)] = return_escape;
        held.keys[static_cast<std::size_t>(KEYSTATE_SPACE)] = space;
    });
}

bool release_held_keys(HeldKeys& held)
{
    return set_held_keys_on_main_thread(held, false, false);
}

int page_with_held_keys(Flow& flow)
{
    HeldKeys& held = *g_held;
    // Every way out lets go first: a page that closed under the held keys
    // waits in its drain for them to come up.
    const auto give_up = [&](int leg, const char* why) {
        release_held_keys(held);
        return leave(flow, leg, why);
    };
    if (!wait_for_interactable("changelog_ok", 15000))
        return give_up(1, "the CHANGELOG! page never came up");
    flow.page_seen = true;
    if (!wait_for_menu_frames(4, 3000))
        return give_up(2, "the page completed no frames under the held keys");
    held.stayed_up_while_held = has_interactable("changelog_ok") &&
                                !trace_contains("changelog", "CHANGELOG!: closed");
    // Let go, give the page frames to see it, then press Space again.
    if (!release_held_keys(held))
        return give_up(3, "the menu thread never ran the key release");
    if (!wait_for_menu_frames(2))
        return give_up(3, "the page completed no frames after the release");
    if (!set_held_keys_on_main_thread(held, false, true))
        return give_up(4, "the menu thread never ran the Space press");
    held.closed_by_a_new_press =
        wait_for_trace("changelog", "CHANGELOG!: closed id=1", 15000);
    if (!release_held_keys(held))
        return give_up(4, "the menu thread never ran the Space release");
    if (!held.closed_by_a_new_press)
        return give_up(4, "a fresh Space did not close the page");
    if (!wait_for_interactable("quit", 15000))
        return give_up(5, "the page did not lead to the main menu");
    flow.main_menu_seen = true;
    return leave(flow, 0, "");
}

// For the tests that call picker_show_pending_changelog() expecting it to
// return at once: should the page come up after all, the shared escape tail
// clicks it shut instead of hanging the binary (the tests then fail on the
// "CHANGELOG!: showing" trace and the stamped marker).
constexpr EscapeDoor kPageOnlyDoor[] = {{"changelog_ok", "changelog_ok"}};

struct PageCloser
{
    std::atomic<bool> stop{false};
    SDL_Thread* thread = nullptr;
    PageCloser()
    {
        thread = SDL_CreateThread(
            [](void* data) -> int {
                auto& self = *static_cast<PageCloser*>(data);
                return escape_to_the_main_thread(self.stop, 0, "",
                                                 kPageOnlyDoor);
            },
            "changelog_closer", this);
    }
    ~PageCloser()
    {
        stop.store(true);
        SDL_WaitThread(thread, nullptr);
    }
    PageCloser(const PageCloser&) = delete;
    PageCloser& operator=(const PageCloser&) = delete;
};

// Count the band's key-cap pixels: the four saturated player colours (red
// 229,72,77; green 48,164,108; yellow 245,180,0; blue 62,142,247). The page's
// own ink is DARK_BLUE (0,0,228) on a grey panel: saturated too, but its red
// and green are equal, and so are those of every blend of it with the grey
// that a scaled-up menu canvas draws. Every cap colour has red and green at
// least 40 apart.
bool is_key_cap_colour(const std::array<Uint8, 3>& c)
{
    const int hi = std::max({c[0], c[1], c[2]});
    const int lo = std::min({c[0], c[1], c[2]});
    const int red_green = c[0] > c[1] ? c[0] - c[1] : c[1] - c[0];
    return hi >= 140 && hi - lo >= 90 && red_green >= 40;
}

struct BandCheck
{
    long band_pixels = 0;
    long band_caps = 0;
    long text_caps = 0;
    // The picture's extent in menu pixels: the bounding box of its dark
    // ground (every channel under 60). Nothing else on the page is that
    // dark: the panel is grey, its frame lines 100 and 196, the ink blue
    // and red. -1 when no dark pixel was found.
    double pic_x0 = -1, pic_y0 = -1, pic_x1 = -1, pic_y1 = -1;
};

// The menu canvas is aspect-fitted into the window; (x, y, w, h) in menu
// pixels -> the physical rectangle.
BandCheck check_band(const PhysicalFrame& frame)
{
    using og::ui::kChangelogBandH;
    using og::ui::kChangelogBandW;
    using og::ui::kChangelogBandX;
    using og::ui::kChangelogBandY;
    using og::ui::kChangelogLinesTopY;
    BandCheck check;
    const double scale = std::min(frame.w / 320.0, frame.h / 200.0);
    const double ox = (frame.w - 320.0 * scale) / 2.0;
    const double oy = (frame.h - 200.0 * scale) / 2.0;
    const auto px = [&](double ux) { return static_cast<int>(ox + ux * scale); };
    const auto py = [&](double uy) { return static_cast<int>(oy + uy * scale); };
    for (int y = py(kChangelogBandY) + 2;
         y < py(kChangelogBandY + kChangelogBandH) - 2; y += 2)
        for (int x = px(kChangelogBandX) + 2;
             x < px(kChangelogBandX + kChangelogBandW) - 2; x += 2) {
            ++check.band_pixels;
            if (is_key_cap_colour(frame.at(x, y)))
                ++check.band_caps;
        }
    // The lines (menu y kChangelogLinesTopY up to the band) never show the
    // picture. (The title above them is the big font's own red, a cap
    // colour by this rule; the lines sit between it and the band, so the
    // picture cannot reach it without crossing them.)
    for (int y = py(kChangelogLinesTopY); y < py(kChangelogBandY) - 1; ++y)
        for (int x = px(4); x < px(316); ++x)
            if (is_key_cap_colour(frame.at(x, y)))
                ++check.text_caps;
    // The picture's ground, over the whole menu canvas (never the
    // letterbox bars, which are black too).
    int x0 = frame.w, y0 = frame.h, x1 = -1, y1 = -1;
    for (int y = py(0); y < py(200); ++y)
        for (int x = px(0); x < px(320); ++x) {
            const std::array<Uint8, 3> c = frame.at(x, y);
            if (std::max({c[0], c[1], c[2]}) >= 60)
                continue;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    if (x1 >= 0) {
        check.pic_x0 = (x0 - ox) / scale;
        check.pic_y0 = (y0 - oy) / scale;
        check.pic_x1 = (x1 + 1 - ox) / scale;
        check.pic_y1 = (y1 + 1 - oy) / scale;
    }
    return check;
}

// The picture sits inside the panel's frame lines (x 4 and 315), with bare
// panel between it and the last text line and between it and OK, and is
// still nearly as wide as the panel (legibility).
void expect_picture_framed(const BandCheck& band)
{
    using og::ui::kChangelogFrameLeftX;
    using og::ui::kChangelogFrameRightX;
    using og::ui::kChangelogLinesBottomY;
    using og::ui::kChangelogOkY;
    ASSERT_GE(band.pic_x1, 0.0) << "no picture on the page";
    EXPECT_GE(band.pic_x0, kChangelogFrameLeftX + 1 + 2.0)
        << "the picture runs over the panel's left frame line (x 4)";
    EXPECT_LE(band.pic_x1, kChangelogFrameRightX - 2.0)
        << "the picture runs over the panel's right frame line (x 315)";
    EXPECT_GE(band.pic_y0, kChangelogLinesBottomY + 2.0)
        << "no gap between the last text line (ends y "
        << kChangelogLinesBottomY << ") and the picture (starts y "
        << band.pic_y0 << ")";
    EXPECT_LE(band.pic_y1, kChangelogOkY - 2.0)
        << "no gap between the picture (ends y " << band.pic_y1
        << ") and OK (y " << kChangelogOkY << ")";
    EXPECT_GE(band.pic_x1 - band.pic_x0, 280.0)
        << "the picture shrank below what its labels need";
}

// The output size for the three captures, put back after.
struct OutputSize
{
    int win_w = 0;
    int win_h = 0;
    float old_window_w;
    float old_window_h;
    OutputSize()
        : old_window_w(og::runtime::current_session->window_w_),
          old_window_h(og::runtime::current_session->window_h_)
    {
        SDL_GetWindowSize(E_Screen->window, &win_w, &win_h);
    }
    ~OutputSize() { apply(win_w, win_h, old_window_w, old_window_h); }
    OutputSize(const OutputSize&) = delete;
    OutputSize& operator=(const OutputSize&) = delete;

    static void apply(int w, int h, float session_w, float session_h)
    {
        (void)SDL_SetWindowSize(E_Screen->window, w, h);
        (void)SDL_SyncWindow(E_Screen->window);
        og::runtime::current_session->window_w_ = session_w;
        og::runtime::current_session->window_h_ = session_h;
        update_overscan_setting();
        E_Screen->set_world_zoom(og::kZoomStepsMax,
                                 og::WorldScaleMode::Integer, w, h);
        E_Screen->set_active_canvas(CanvasTarget::UI);
    }
    static void set(int w, int h)
    {
        apply(w, h, static_cast<float>(w), static_cast<float>(h));
    }
};

} // namespace

TEST(ChangelogFlow, forced_page_shows_once_and_the_marker_holds_it)
{
    trace_clear();
    (void)take_captured_frames();
    MarkerRestore restore;
    picker_testing_set_force_changelog(true);
    ASSERT_EQ(0, og::ui::changelog_seen_marker());

    Flow first;
    first.body = page_then_ok;
    const int first_result = run_picker_flow(first);
    EXPECT_EQ(0, first_result) << "the injector gave up at leg "
                               << first_result;
    ASSERT_TRUE(first.page_seen);
    ASSERT_TRUE(first.main_menu_seen);
    EXPECT_TRUE(trace_contains("changelog", "CHANGELOG!: showing id=1 picture=1"));

    // The 320x200 frame: the title in the big font at y 8, the six lines
    // below it, and bare panel under the last one.
    ASSERT_TRUE(first.menu_frame.captured);
    EXPECT_GE(first.menu_frame.nonblack, kCapturedFrameMinNonblackPixels);
    // Rows holding ink inside the bevel: `red` for the title (the big font
    // draws in its own red), dark blue for the five lines.
    const auto ink_rows = [&](int y0, int y1, bool red) {
        int rows = 0;
        for (int y = y0; y < y1; ++y) {
            bool ink = false;
            for (int x = 8; x < 312 && !ink; ++x) {
                const std::size_t i =
                    (static_cast<std::size_t>(y) * 320u + static_cast<std::size_t>(x)) * 3u;
                const int r = first.menu_frame.rgb[i];
                const int g = first.menu_frame.rgb[i + 1];
                const int b = first.menu_frame.rgb[i + 2];
                ink = red ? (r > g + 60 && r > b + 60)
                          : (b > r + 40 && b > g + 20);
            }
            rows += ink ? 1 : 0;
        }
        return rows;
    };
    EXPECT_GE(ink_rows(8, 21, true), 6) << "the title is not at y 8";
    EXPECT_EQ(0, ink_rows(0, 8, true)) << "the title starts above y 8";
    EXPECT_GE(ink_rows(og::ui::kChangelogLinesTopY,
                       og::ui::kChangelogLinesBottomY, false),
              6 * 4)
        << "the six lines are not under it";
    EXPECT_EQ(0, ink_rows(og::ui::kChangelogLinesBottomY,
                          og::ui::kChangelogBandY, false))
        << "text runs into the gap above the picture";

    // The physical frame: the keyboard picture fills its band and stays out
    // of the text.
    ASSERT_TRUE(first.physical.captured);
    const BandCheck band = check_band(first.physical);
    EXPECT_GT(band.band_caps * 5, band.band_pixels)
        << "the picture band shows no keyboard (" << band.band_caps << " of "
        << band.band_pixels << " samples are key caps)";
    EXPECT_EQ(0, band.text_caps) << "the picture spills into the text";
    expect_picture_framed(band);

    // OK stamped the marker: on the menu thread, in cfg, and on disk.
    EXPECT_EQ(og::ui::kChangelogLatestId, first.marker_on_menu_thread);
    EXPECT_EQ(og::ui::kChangelogLatestId, og::ui::changelog_seen_marker());
    EXPECT_NE(std::string::npos, read_text(yaml_path()).find("changelog_seen"))
        << "the marker was not saved to " << yaml_path();

    // A second picker_main in the same process: no page, only the trace.
    trace_clear();
    Flow second;
    second.body = main_menu_only;
    const int second_result = run_picker_flow(second);
    EXPECT_EQ(0, second_result) << "the injector gave up at leg "
                                << second_result;
    ASSERT_TRUE(second.main_menu_seen);
    EXPECT_FALSE(second.page_seen_on_return)
        << "the page came back with the marker stamped";
    EXPECT_TRUE(trace_contains("changelog", "CHANGELOG!: pending=0 seen=1"));
    EXPECT_FALSE(trace_contains("changelog", "CHANGELOG!: showing"));
}

TEST(ChangelogFlow, marker_cleared_shows_it_again)
{
    // The marker is the only latch: a process that already showed the page
    // shows it again once the marker is gone. Escape closes it this time,
    // and the main menu (whose QUIT is Escape) must not take that press.
    trace_clear();
    MarkerRestore restore;
    picker_testing_set_force_changelog(true);
    cfg.apply_setting("ui", "changelog_seen", "1");

    Flow stamped;
    stamped.body = main_menu_only;
    EXPECT_EQ(0, run_picker_flow(stamped));
    EXPECT_FALSE(stamped.page_seen_on_return);

    erase_marker();
    trace_clear();
    Flow cleared;
    cleared.body = page_then_escape;
    const int result = run_picker_flow(cleared);
    EXPECT_EQ(0, result) << "the injector gave up at leg " << result;
    EXPECT_TRUE(cleared.page_seen) << "a cleared marker shows the page again";
    EXPECT_TRUE(cleared.main_menu_stayed)
        << "the Escape that closed the page reached the main menu";
    EXPECT_EQ(og::ui::kChangelogLatestId, og::ui::changelog_seen_marker());
}

TEST(ChangelogFlow, keys_held_from_the_intro_do_not_close_the_page)
{
    trace_clear();
    MarkerRestore restore;
    picker_testing_set_force_changelog(true);
    HeldKeys held;
    held.keys[static_cast<std::size_t>(KEYSTATE_RETURN)] = true;
    held.keys[static_cast<std::size_t>(KEYSTATE_ESCAPE)] = true;
    held.keys[static_cast<std::size_t>(KEYSTATE_SPACE)] = true;
    held.saved = og::runtime::current_session->keystates_;
    og::runtime::current_session->keystates_ = held.keys.data();
    g_held = &held;

    Flow flow;
    flow.body = page_with_held_keys;
    const int result = run_picker_flow(flow);
    og::runtime::current_session->keystates_ = held.saved;
    g_held = nullptr;
    EXPECT_EQ(0, result) << "the injector gave up at leg " << result;
    EXPECT_TRUE(flow.page_seen);
    EXPECT_TRUE(held.stayed_up_while_held)
        << "keys held since before the page opened closed it unseen";
    EXPECT_TRUE(held.closed_by_a_new_press);
    EXPECT_TRUE(flow.main_menu_seen);
    EXPECT_EQ(og::ui::kChangelogLatestId, og::ui::changelog_seen_marker());
}

TEST(ChangelogFlow, a_marker_that_is_not_a_number_counts_as_unseen)
{
    MarkerRestore restore;
    cfg.apply_setting("ui", "changelog_seen", "yes");
    EXPECT_EQ(0, og::ui::changelog_seen_marker());
    cfg.apply_setting("ui", "changelog_seen", "7");
    EXPECT_EQ(7, og::ui::changelog_seen_marker());
}

TEST(ChangelogFlow, demo_session_never_shows_it)
{
    trace_clear();
    MarkerRestore restore;
    picker_testing_set_force_changelog(true);
    auto* const session = og::runtime::current_session;
    ASSERT_NE(nullptr, session);
    const bool was = session->non_interactive_;
    session->non_interactive_ = true;
    {
        PageCloser closer;
        picker_show_pending_changelog();
    }
    session->non_interactive_ = was;
    EXPECT_FALSE(trace_contains("changelog", "CHANGELOG!"))
        << "a demo session must not even consider the page";
    EXPECT_EQ(0, og::ui::changelog_seen_marker())
        << "a demo session must not stamp the marker";
}

TEST(ChangelogFlow, testing_default_is_trace_only)
{
    trace_clear();
    MarkerRestore restore;
    ASSERT_FALSE(picker_testing_changelog_forced());
    {
        PageCloser closer;
        picker_show_pending_changelog();
    }
    EXPECT_TRUE(trace_contains("changelog",
                               "CHANGELOG!: pending=1 seen=0 forced=0"));
    EXPECT_FALSE(trace_contains("changelog", "CHANGELOG!: showing"));
    EXPECT_EQ(0, og::ui::changelog_seen_marker())
        << "a page nobody saw must not be stamped as seen";
}

TEST(ChangelogFlow, page_at_three_window_sizes_keeps_the_picture_in_its_band)
{
    // The same page at the windowed default and two larger outputs. The
    // picture is drawn at each output's own pixels (the band is 296 x 104
    // menu pixels: 1184 x 416 at 1280 x 800, 1598 x 562 at 1920 x 1080),
    // inside the panel's frame lines with a gap above and below.
    ASSERT_TRUE(E_Screen);
    MarkerRestore restore;
    OutputSize output;
    picker_testing_set_force_changelog(true);
    struct Size { int w, h; const char* name; };
    constexpr Size kSizes[] = {
        {640, 400, "changelog_640x400"},
        {1280, 800, "changelog_1280x800"},
        {1920, 1080, "changelog_1920x1080"},
    };
    for (const Size& size : kSizes) {
        SCOPED_TRACE(size.name);
        erase_marker();
        OutputSize::set(size.w, size.h);
        Flow flow;
        flow.body = page_then_ok;
        flow.shot_name = size.name;
        const int result = run_picker_flow(flow);
        EXPECT_EQ(0, result) << "the injector gave up at leg " << result;
        ASSERT_TRUE(flow.physical.captured);
        EXPECT_EQ(size.w, flow.physical.w);
        EXPECT_EQ(size.h, flow.physical.h);
        const BandCheck band = check_band(flow.physical);
        EXPECT_GT(band.band_caps * 5, band.band_pixels)
            << "the picture band shows no keyboard";
        EXPECT_EQ(0, band.text_caps) << "the picture spills into the text";
        expect_picture_framed(band);
    }
}
