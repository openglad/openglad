#include <openglad/interface/ui/campaign_picker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/level_picker.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/io.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string_view>
#include <string>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

// level_picker.cpp helpers
bool isDir(const std::string& filename);
bool sort_scen(const std::string& first, const std::string& second);
// campaign_picker.cpp helper
int toInt(const std::string& s);
int campaign_picker_testing_exercise_entry_draw_paths();
void campaign_picker_testing_input_reset();
void campaign_picker_testing_abort();
std::uint64_t campaign_picker_testing_entered_count();
std::uint64_t campaign_picker_testing_action_count();
// results_screen.cpp helper
void show_ending_popup(int ending, int nextlevel);
// Deterministic dialog answers used by the level picker.
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
int picker_testing_yes_or_no_queue_remaining();
void level_picker_testing_input_reset();
void level_picker_testing_click(int x, int y);
std::uint64_t level_picker_testing_entered_count();
std::uint64_t level_picker_testing_action_count();

namespace
{
void cleanup_leftover_test_campaigns()
{
    // Prior failed/aborted runs can leave behind test campaign packages in
    // ~/.openglad/campaigns. Some of those are intentionally malformed and can
    // cause picker flows to hang while enumerating/loading campaigns.
    //
    // Keep this narrow: only delete known-hazard prefixes created by tests.
    for (const auto& id : list_campaigns())
    {
        if (id.rfind("test.invalid_yaml.", 0) == 0 ||
            id.rfind("test.missing_yaml.", 0) == 0)
        {
            delete_campaign(id);
        }
    }
}

std::string unique_test_campaign_id(std::string_view stem)
{
    static std::atomic<std::uint64_t> sequence{0};
    const auto nonce = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "test." + std::string(stem) + "." +
        std::to_string(nonce) + "." +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

// Release handshake for hold_q_key_for_picker: the test flips this after
// pick_campaign returns, so the injector holds 'q' across every input-loop
// poll instead of guessing a wall-clock window.
std::atomic<bool> g_picker_q_release{false};

// Injector coordinates come from the pure layouts (the :539 precedent), so
// a geometry change moves the clicks with it instead of silently missing.
int rect_center_x(const og::ui::PickerRect& rect) { return rect.x + rect.w / 2; }
int rect_center_y(const og::ui::PickerRect& rect) { return rect.y + rect.h / 2; }

// A point inside level-preview row `row`'s radar even for the narrowest map.
void level_row_click_point(int row, int& x, int& y)
{
    const og::ui::LevelPickerLayout layout = og::ui::level_picker_layout();
    x = layout.row_x + 14;
    y = og::ui::level_picker_row_y(row) + layout.radar_dy + 10;
}
} // namespace

// Defined below; declared here so the injector can handshake on them.
bool wait_for_campaign_picker_counter(
    std::uint64_t (*counter)(), std::uint64_t baseline);
bool wait_for_campaign_picker_event_consumed(Uint32 event_type);

// Holds 'q' down for pick_campaign's input loop. The picker's entry setup
// (campaign enumeration — every entry mount reinstalls the class packs)
// takes arbitrarily long under instrumented builds, so a fixed-width hold
// started at thread creation can be fully released before the loop's first
// keystate poll, leaving the loop waiting forever (the ASan/coverage
// og_test_menu_ui 420 s timeouts). Handshake on both edges instead:
//
//   PRESS only once the loop marks itself entered (counter handshake).
//   RELEASE only once the press is provably consumed: two marker events
//   pushed one-at-a-time through the queue the picker pumps. Marker 1 is
//   drained by an input pump that the press write happens-before (queue
//   handoff), so that pump's iteration reads 'q' held; marker 2 is only
//   pushed after marker 1 is gone, so its drain proves a LATER pump — one
//   the loop can only reach after that q-read. Releasing any earlier races
//   the read (the original bug); releasing only after pick_campaign returns
//   instead burns wait_for_key_release's whole 5 s poll limit, because the
//   picker's quit path polls this same shared array for the release.
//
// The re-press backstop covers the one unprovable interleaving (a marker
// swallowed by the tail of an in-flight pump): if pick_campaign has not
// returned shortly after the release, the loop must still be polling, so a
// second held press is consumed on its next iteration and the picker's own
// release-wait poll limit bounds the exit.
static int hold_q_key_for_picker(void* data)
{
    og::runtime::ensure_thread_session();
    const auto* entered_baseline = static_cast<const std::uint64_t*>(data);
    if (!wait_for_campaign_picker_counter(
            campaign_picker_testing_entered_count, *entered_baseline))
        return 1; // waiter timed out and already aborted the picker
    int numkeys = 0;
    bool* keys = const_cast<bool*>(SDL_GetKeyboardState(&numkeys));
    SDL_Scancode sc = SDL_GetScancodeFromKey(SDLK_Q, nullptr);
    if (!(sc >= 0 && sc < numkeys))
    {
        campaign_picker_testing_abort();
        return 2;
    }
    keys[sc] = true;
    for (int marker = 0; marker < 2; ++marker)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_USER;
        if (!SDL_PushEvent(&event))
        {
            // Queue rejected the marker: abort so pick_campaign still
            // returns and the test reds with a diagnosis, never a hang.
            campaign_picker_testing_abort();
            keys[sc] = false;
            return 3;
        }
        if (!wait_for_campaign_picker_event_consumed(SDL_EVENT_USER))
        {
            // No pump drained the marker for 5 s: the picker is wedged. The
            // consumed-waiter already aborted it; fail with a diagnosis.
            keys[sc] = false;
            return 3;
        }
    }
    keys[sc] = false;

    const Uint64 released_at = SDL_GetTicks();
    bool re_pressed = false;
    while (!g_picker_q_release.load(std::memory_order_acquire))
    {
        const Uint64 waited = SDL_GetTicks() - released_at;
        if (!re_pressed && waited >= 2000)
        {
            keys[sc] = true; // backstop: press again and hold
            re_pressed = true;
        }
        if (waited >= 10000)
        {
            campaign_picker_testing_abort();
            keys[sc] = false;
            return 4;
        }
        SDL_Delay(1);
    }
    if (re_pressed)
        keys[sc] = false;
    return 0;
}

static void repeated_click(int x, int y, int attempts = 8)
{
    for (int i = 0; i < attempts; ++i) {
        inject_click(x, y, 100);
        SDL_Delay(150);
    }
}

bool wait_for_campaign_picker_counter(
    std::uint64_t (*counter)(), std::uint64_t baseline)
{
    constexpr Uint64 kHandshakeTimeoutMs = 5000;
    const Uint64 started_at = SDL_GetTicks();
    while (counter() <= baseline)
    {
        if (SDL_GetTicks() - started_at >= kHandshakeTimeoutMs)
        {
            campaign_picker_testing_abort();
            return false;
        }
        SDL_Delay(1);
    }
    return true;
}

bool wait_for_campaign_picker_ready()
{
    return wait_for_campaign_picker_counter(
        campaign_picker_testing_entered_count, 0);
}

bool wait_for_campaign_picker_event_consumed(Uint32 event_type)
{
    constexpr Uint64 kHandshakeTimeoutMs = 5000;
    const Uint64 started_at = SDL_GetTicks();
    while (SDL_HasEvent(event_type))
    {
        if (SDL_GetTicks() - started_at >= kHandshakeTimeoutMs)
        {
            campaign_picker_testing_abort();
            return false;
        }
        SDL_Delay(1);
    }
    return true;
}

bool push_campaign_picker_mouse_event(Uint32 event_type, int x, int y)
{
    SDL_Event event{};
    event.type = event_type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = event_type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.clicks = 1;
    event.button.x = static_cast<float>(x);
    event.button.y = static_cast<float>(y);
    return SDL_PushEvent(&event);
}

bool click_campaign_picker_action(int x, int y)
{
    const std::uint64_t action_before =
        campaign_picker_testing_action_count();
    if (!push_campaign_picker_mouse_event(
            SDL_EVENT_MOUSE_BUTTON_DOWN, x, y))
    {
        campaign_picker_testing_abort();
        return false;
    }

    const bool acknowledged = wait_for_campaign_picker_counter(
        campaign_picker_testing_action_count, action_before);
    if (!push_campaign_picker_mouse_event(
            SDL_EVENT_MOUSE_BUTTON_UP, x, y))
    {
        campaign_picker_testing_abort();
        return false;
    }
    const bool released =
        wait_for_campaign_picker_event_consumed(
            SDL_EVENT_MOUSE_BUTTON_UP);
    return acknowledged && released;
}

struct CampaignPickerInputGuard
{
    CampaignPickerInputGuard()
    {
        campaign_picker_testing_input_reset();
        if (SDL_HasEvents(
                SDL_EVENT_MOUSE_BUTTON_DOWN,
                SDL_EVENT_MOUSE_BUTTON_UP))
        {
            ADD_FAILURE()
                << "campaign picker inherited stale mouse-button events";
        }
    }

    ~CampaignPickerInputGuard()
    {
        campaign_picker_testing_input_reset();
        SDL_FlushEvents(
            SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP);
    }

    void reset()
    {
        if (SDL_HasEvents(
                SDL_EVENT_MOUSE_BUTTON_DOWN,
                SDL_EVENT_MOUSE_BUTTON_UP))
        {
            ADD_FAILURE()
                << "previous campaign picker left mouse-button events queued";
        }
        campaign_picker_testing_input_reset();
    }
};

struct ViewportGuard
{
    float ow, oh, ovw, ovh, ox, oy;
    ViewportGuard()
    {
        ow = og::runtime::current_session->window_w_;
        oh = og::runtime::current_session->window_h_;
        ovw = og::runtime::current_session->viewport_w_;
        ovh = og::runtime::current_session->viewport_h_;
        ox = og::runtime::current_session->viewport_offset_x_;
        oy = og::runtime::current_session->viewport_offset_y_;
    }
    ~ViewportGuard()
    {
        og::runtime::current_session->window_w_ = ow;
        og::runtime::current_session->window_h_ = oh;
        og::runtime::current_session->viewport_w_ = ovw;
        og::runtime::current_session->viewport_h_ = ovh;
        og::runtime::current_session->viewport_offset_x_ = ox;
        og::runtime::current_session->viewport_offset_y_ = oy;
    }
};

struct WorldEndGuard
{
    char& end;
    char saved;

    explicit WorldEndGuard(char& end_) : end(end_), saved(end_) {}
    ~WorldEndGuard() { end = saved; }
};

struct CampaignMountGuard
{
    std::string original_mount = get_mounted_campaign();

    ~CampaignMountGuard()
    {
        const std::string mounted = get_mounted_campaign();
        if (mounted == original_mount)
            return;

        const CampaignPackageIoError error =
            original_mount.empty()
                ? unmount_campaign_package_with_error(mounted)
                : mount_campaign_package_with_error(original_mount);
        if (error != CampaignPackageIoError::None)
        {
            ADD_FAILURE()
                << "failed to restore campaign mount to " << original_mount;
        }
    }
};

class ScopedSdlThread
{
public:
    explicit ScopedSdlThread(SDL_Thread* thread)
        : thread_(thread)
    {
    }

    ~ScopedSdlThread()
    {
        if (thread_ != nullptr)
            SDL_WaitThread(thread_, nullptr);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return thread_ != nullptr;
    }

    int join()
    {
        int result = 1;
        if (thread_ != nullptr)
        {
            SDL_WaitThread(thread_, &result);
            thread_ = nullptr;
        }
        return result;
    }

    ScopedSdlThread(const ScopedSdlThread&) = delete;
    ScopedSdlThread& operator=(const ScopedSdlThread&) = delete;

private:
    SDL_Thread* thread_ = nullptr;
};

struct TemporaryCampaignGuard
{
    std::string original_mount;
    std::string temporary_id;

    ~TemporaryCampaignGuard()
    {
        const std::string mounted = get_mounted_campaign();
        CampaignPackageIoError restore_error =
            CampaignPackageIoError::None;
        if (mounted != original_mount)
        {
            if (original_mount.empty())
                restore_error =
                    unmount_campaign_package_with_error(mounted);
            else
                restore_error =
                    mount_campaign_package_with_error(original_mount);
        }
        if (restore_error != CampaignPackageIoError::None)
        {
            ADD_FAILURE()
                << "failed to restore campaign mount to "
                << original_mount;
            return;
        }
        delete_campaign(temporary_id);
    }
};

struct PromptQueueGuard
{
    PromptQueueGuard() { level_editor_testing_prompt_queue_clear(); }
    ~PromptQueueGuard() { level_editor_testing_prompt_queue_clear(); }

    void push(const char* value)
    {
        level_editor_testing_prompt_queue_push(value);
    }
};

struct YesNoQueueGuard
{
    YesNoQueueGuard() { picker_testing_yes_or_no_queue_clear(); }
    ~YesNoQueueGuard() { picker_testing_yes_or_no_queue_clear(); }

    void push(bool value)
    {
        picker_testing_yes_or_no_queue_push(value);
    }
};

bool wait_for_level_picker_counter(std::uint64_t (*counter)(),
                                   std::uint64_t baseline)
{
    constexpr Uint64 kHandshakeTimeoutMs = 5000;
    const Uint64 started_at = SDL_GetTicks();
    while (counter() <= baseline)
    {
        if (SDL_GetTicks() - started_at >= kHandshakeTimeoutMs)
        {
            // Negative coordinates are the harness's fail-safe abort request.
            // They never count as a picker action, so the test still fails.
            level_picker_testing_click(-1, -1);
            return false;
        }
        SDL_Delay(1);
    }
    return true;
}

bool wait_for_level_picker_ready()
{
    return wait_for_level_picker_counter(
        level_picker_testing_entered_count, 0);
}

bool click_level_picker_action(int x, int y)
{
    const std::uint64_t action_before =
        level_picker_testing_action_count();
    level_picker_testing_click(x, y);
    return wait_for_level_picker_counter(
        level_picker_testing_action_count, action_before);
}

static int picker_choose_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::PickerRect ok_rect = og::ui::campaign_picker_layout().choose;
    const bool ready = wait_for_campaign_picker_ready();
    return ready && click_campaign_picker_action(rect_center_x(ok_rect),
                                                 rect_center_y(ok_rect))
        ? 0
        : 1; // OK
}

static int picker_more_then_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    bool ok = wait_for_campaign_picker_ready();
    // MORE is deterministically visible while gladiator is highlighted: its
    // shipped description overflows the detail box. Under TESTING the
    // full-description scroller returns immediately (help.cpp force guard),
    // so the click exercises the control without blocking.
    if (ok)
        ok = click_campaign_picker_action(rect_center_x(layout.more_button),
                                          rect_center_y(layout.more_button));
    if (ok)
        ok = click_campaign_picker_action(rect_center_x(layout.cancel),
                                          rect_center_y(layout.cancel));
    return ok ? 0 : 1;
}

static int campaign_delete_then_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    bool ok = wait_for_campaign_picker_ready();
    if (ok)
        ok = click_campaign_picker_action(rect_center_x(layout.delete_button),
                                          rect_center_y(layout.delete_button));
    if (ok)
        ok = click_campaign_picker_action(rect_center_x(layout.cancel),
                                          rect_center_y(layout.cancel));
    return ok ? 0 : 1;
}

static int campaign_reset_then_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    bool ok = wait_for_campaign_picker_ready();
    if (ok)
        ok = click_campaign_picker_action(rect_center_x(layout.reset_button),
                                          rect_center_y(layout.reset_button));
    if (ok)
        ok = click_campaign_picker_action(rect_center_x(layout.cancel),
                                          rect_center_y(layout.cancel));
    return ok ? 0 : 1;
}

struct CampaignNavigationInjectorContext
{
    std::size_t page_steps = 0;  // PREV/NEXT page clicks to reach the far page
    int row = 0;                 // visible row to click once there
    bool next = true;
};

static int campaign_next_prev_choose_injector(void* data)
{
    og::runtime::ensure_thread_session();
    const auto* const context =
        static_cast<const CampaignNavigationInjectorContext*>(data);
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    bool ok = context != nullptr && wait_for_campaign_picker_ready();
    for (std::size_t i = 0; ok && i < context->page_steps; ++i)
    {
        const og::ui::PickerRect pager =
            context->next ? layout.next : layout.prev;
        ok = click_campaign_picker_action(rect_center_x(pager),
                                          rect_center_y(pager));
    }
    if (ok)
    {
        const og::ui::PickerRect row_rect =
            og::ui::campaign_picker_row_rect(context->row);
        ok = click_campaign_picker_action(rect_center_x(row_rect),
                                          rect_center_y(row_rect));
    }
    if (ok)
        ok = click_campaign_picker_action(rect_center_x(layout.choose),
                                          rect_center_y(layout.choose)); // OK
    return ok ? 0 : 1;
}

static int campaign_enter_id_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const bool ready = wait_for_campaign_picker_ready();
    // ENTER ID lives under DELETE/RESET now; click its computed center so the
    // injector follows og::ui::campaign_picker_layout() instead of a literal.
    const og::ui::PickerRect id =
        og::ui::campaign_picker_layout().id_button;
    return ready && click_campaign_picker_action(id.x + id.w / 2,
                                                 id.y + id.h / 2)
        ? 0
        : 1;
}

struct CampaignDeleteInjectorContext
{
    std::filesystem::path package;
};

static int campaign_confirmed_delete_injector(void* opaque)
{
    og::runtime::ensure_thread_session();
    auto* const context =
        static_cast<CampaignDeleteInjectorContext*>(opaque);
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();

    // One destructive click only. The cancel press cannot be consumed until
    // deletion and row reload finish, so its event handshake also proves the
    // destructive action committed before this thread inspects the package.
    bool clicked_delete = wait_for_campaign_picker_ready();
    if (clicked_delete)
        clicked_delete =
            click_campaign_picker_action(rect_center_x(layout.delete_button),
                                         rect_center_y(layout.delete_button));
    bool canceled = false;
    if (clicked_delete)
        canceled = click_campaign_picker_action(rect_center_x(layout.cancel),
                                                rect_center_y(layout.cancel));
    const bool removed = !std::filesystem::exists(context->package);
    return clicked_delete && removed && canceled ? 0 : 1;
}

static int campaign_confirmed_reset_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    bool reset = wait_for_campaign_picker_ready();
    if (reset)
        reset = click_campaign_picker_action(rect_center_x(layout.reset_button),
                                             rect_center_y(layout.reset_button));
    bool canceled = false;
    if (reset)
        canceled = click_campaign_picker_action(rect_center_x(layout.cancel),
                                                rect_center_y(layout.cancel));
    return reset && canceled ? 0 : 1;
}

static int level_picker_choose_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::LevelPickerLayout layout = og::ui::level_picker_layout();
    int row_x = 0;
    int row_y = 0;
    level_row_click_point(0, row_x, row_y);
    SDL_Delay(500);
    for (int i = 0; i < 8; ++i) {
        inject_click(row_x, row_y, 100);   // Select entry 1
        SDL_Delay(150);
        inject_click(rect_center_x(layout.choose),
                     rect_center_y(layout.choose), 100); // OK
        SDL_Delay(150);
    }
    return 0;
}

static int level_picker_delete_then_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::LevelPickerLayout layout = og::ui::level_picker_layout();
    int row_x = 0;
    int row_y = 0;
    level_row_click_point(0, row_x, row_y);
    SDL_Delay(500);
    for (int i = 0; i < 4; ++i) {
        inject_click(row_x, row_y, 100);  // Select entry 1
        SDL_Delay(150);
        inject_click(rect_center_x(layout.delete_button),
                     rect_center_y(layout.delete_button), 100); // DELETE
        SDL_Delay(150);
    }
    repeated_click(rect_center_x(layout.cancel),
                   rect_center_y(layout.cancel), 8); // CANCEL
    return 0;
}

static int level_picker_scroll_then_choose_first_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::LevelPickerLayout layout = og::ui::level_picker_layout();
    int row_x = 0;
    int row_y = 0;
    level_row_click_point(0, row_x, row_y);
    bool ok = wait_for_level_picker_ready();
    if (ok)
        ok = click_level_picker_action(
            rect_center_x(layout.next),
            rect_center_y(layout.next)); // NEXT: shift previews up
    if (ok)
        ok = click_level_picker_action(
            row_x, row_y); // Select the entry shifted into the first row
    if (ok)
        ok = click_level_picker_action(rect_center_x(layout.choose),
                                       rect_center_y(layout.choose)); // OK
    return ok ? 0 : 1;
}

static int level_picker_scroll_back_then_choose_first_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::LevelPickerLayout layout = og::ui::level_picker_layout();
    int row_x = 0;
    int row_y = 0;
    level_row_click_point(0, row_x, row_y);
    bool ok = wait_for_level_picker_ready();
    if (ok)
        ok = click_level_picker_action(rect_center_x(layout.next),
                                       rect_center_y(layout.next)); // NEXT
    if (ok)
        ok = click_level_picker_action(rect_center_x(layout.prev),
                                       rect_center_y(layout.prev)); // PREV
    if (ok)
        ok = click_level_picker_action(
            row_x, row_y); // First entry after scrolling back
    if (ok)
        ok = click_level_picker_action(rect_center_x(layout.choose),
                                       rect_center_y(layout.choose)); // OK
    return ok ? 0 : 1;
}

static int level_picker_enter_id_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::PickerRect id_button = og::ui::level_picker_layout().id_button;
    bool ok = wait_for_level_picker_ready();
    if (ok)
        ok = click_level_picker_action(rect_center_x(id_button),
                                       rect_center_y(id_button)); // ENTER ID
    return ok ? 0 : 1;
}

static int level_picker_delete_once_then_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    const og::ui::LevelPickerLayout layout = og::ui::level_picker_layout();
    int row_x = 0;
    int row_y = 0;
    level_row_click_point(0, row_x, row_y);
    bool ok = wait_for_level_picker_ready();
    if (ok)
        ok = click_level_picker_action(row_x, row_y); // Select first entry
    if (ok)
        ok = click_level_picker_action(rect_center_x(layout.delete_button),
                                       rect_center_y(layout.delete_button));
    if (ok)
        ok = click_level_picker_action(rect_center_x(layout.cancel),
                                       rect_center_y(layout.cancel)); // CANCEL
    return ok ? 0 : 1;
}

TEST(CampaignAndLevelPicker, campaign_picker_cancel_esc_does_not_crash)
{
    cleanup_leftover_test_campaigns();

    ASSERT_TRUE(isDir(".")) << "isDir should report current directory as directory";
    ASSERT_TRUE(!isDir("./definitely_missing_openglad_path")) << "isDir should report missing path as not directory";

    ASSERT_TRUE(sort_scen("level2", "level10")) << "sort_scen should order numeric suffixes";
    ASSERT_TRUE(!sort_scen("abc9", "abc2")) << "sort_scen should not invert numeric suffix ordering";
    ASSERT_TRUE(!sort_scen("levelx", "level2"))
        << "a malformed numeric suffix should use deterministic lexical ordering";
    ASSERT_TRUE(sort_scen("alpha1", "beta1"))
        << "different prefixes should use deterministic lexical ordering";
    ASSERT_EQ(42, toInt("42")) << "toInt should parse decimal text";
    ASSERT_EQ(0, toInt("not-an-integer"))
        << "toInt should reject malformed input instead of accepting a prefix";

    std::map<std::string, int> current_levels;
    const std::string mounted = get_mounted_campaign();
    current_levels[mounted] = 7;
    ASSERT_EQ(7, load_campaign(mounted, current_levels, 1)) << "load_campaign should use tracked current level";
    current_levels.clear();
    ASSERT_EQ(4, load_campaign(mounted, current_levels, 4)) << "load_campaign should fall back to first level";

    show_ending_popup(1, -1);
    show_ending_popup(1, 3);
    show_ending_popup(SCEN_TYPE_SAVE_ALL, 2);
    show_ending_popup(0, 2);

    // Keep picker exit deterministic in headless CI while still exercising setup paths.
    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;
    CampaignResult canceled = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    og::runtime::current_session->myscreen_->world().end = old_end;
    ASSERT_TRUE(canceled.id.empty()) << "campaign picker early-exit should return empty campaign id";
}

TEST(CampaignAndLevelPicker, campaign_entry_draws_real_metadata_states)
{
    EXPECT_EQ(0, campaign_picker_testing_exercise_entry_draw_paths());
}


TEST(CampaignAndLevelPicker, campaign_picker_draw_loop_exits_on_q)
{
    cleanup_leftover_test_campaigns();

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    g_picker_q_release.store(false, std::memory_order_release);
    std::uint64_t entered_baseline = campaign_picker_testing_entered_count();
    ScopedSdlThread thread(SDL_CreateThread(
        hold_q_key_for_picker, "picker_q_hold", &entered_baseline));
    ASSERT_TRUE(thread.valid()) << "failed to create picker q-hold thread";

    CampaignResult out = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    g_picker_q_release.store(true, std::memory_order_release);

    const int thread_result = thread.join();

    ASSERT_EQ(0, thread_result);
    ASSERT_TRUE(out.id.empty()) << "q exit path should not select a campaign";
}


TEST(CampaignAndLevelPicker, campaign_picker_mouse_choose_and_cancel_paths)
{
    cleanup_leftover_test_campaigns();

    CampaignPickerInputGuard input_guard;
    CampaignMountGuard mount_guard;
    const std::string old_campaign = get_mounted_campaign();

    ViewportGuard guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    ScopedSdlThread choose_thread(
        SDL_CreateThread(picker_choose_injector, "picker_choose", nullptr));
    ASSERT_TRUE(choose_thread.valid()) << "failed to create choose injector";
    CampaignResult chosen = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    const int choose_rc = choose_thread.join();
    ASSERT_EQ(0, choose_rc);
    ASSERT_TRUE(!chosen.id.empty()) << "choose path should return a selected campaign id";
    // Ensure later tests run against the baseline default campaign that has scenarios.
    ASSERT_TRUE(mount_campaign_package_with_error(old_campaign) == CampaignPackageIoError::None) << "failed to restore mounted campaign after choose path";

    input_guard.reset();
    // Highlight gladiator deterministically so MORE is visible (see the
    // injector), then open the full description and cancel out.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    ScopedSdlThread cancel_thread(SDL_CreateThread(
        picker_more_then_cancel_injector, "picker_more_cancel", nullptr));
    ASSERT_TRUE(cancel_thread.valid()) << "failed to create cancel injector";
    CampaignResult canceled = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    const int cancel_rc = cancel_thread.join();
    ASSERT_EQ(0, cancel_rc);
    ASSERT_TRUE(canceled.id.empty()) << "cancel path should not return a campaign id";
    ASSERT_TRUE(mount_campaign_package_with_error(old_campaign) == CampaignPackageIoError::None) << "failed to restore mounted campaign after cancel path";

}


TEST(CampaignAndLevelPicker, campaign_picker_delete_and_reset_prompt_paths)
{
    cleanup_leftover_test_campaigns();

    CampaignPickerInputGuard input_guard;
    ViewportGuard guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    ScopedSdlThread delete_thread(SDL_CreateThread(
        campaign_delete_then_cancel_injector,
        "picker_delete_cancel", nullptr));
    ASSERT_TRUE(delete_thread.valid())
        << "failed to create campaign delete injector";
    CampaignResult after_delete_prompt = pick_campaign(&og::runtime::current_session->myscreen_->save_data, true);
    const int delete_rc = delete_thread.join();
    ASSERT_EQ(0, delete_rc);
    ASSERT_TRUE(after_delete_prompt.id.empty()) << "delete+cancel path should return empty campaign id";

    input_guard.reset();
    ScopedSdlThread reset_thread(SDL_CreateThread(
        campaign_reset_then_cancel_injector,
        "picker_reset_cancel", nullptr));
    ASSERT_TRUE(reset_thread.valid())
        << "failed to create campaign reset injector";
    CampaignResult after_reset_prompt = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    const int reset_rc = reset_thread.join();
    ASSERT_EQ(0, reset_rc);
    ASSERT_TRUE(after_reset_prompt.id.empty()) << "reset+cancel path should return empty campaign id";

}

TEST(CampaignAndLevelPicker, campaign_picker_next_and_prev_reach_ordered_boundaries)
{
    cleanup_leftover_test_campaigns();
    std::list<std::string> ordered = list_campaigns();
    og::ui::order_campaigns_for_select(ordered);
    ASSERT_GT(ordered.size(), 1u);
    const std::string first = ordered.front();
    const std::string last = ordered.back();
    CampaignPickerInputGuard input_guard;
    CampaignMountGuard mount_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(first));

    // Page math mirrors campaign_list_page_step: from the first page,
    // page_hops NEXT clicks land on the last page (offset = total - rows);
    // from the last page the same count of PREV clicks lands on offset 0.
    // A hidden pager can't acknowledge a click, so the counts must be exact.
    const int total = static_cast<int>(ordered.size());
    const int rows = og::ui::campaign_picker_layout().list_rows;
    ASSERT_GT(total, rows)
        << "the shipped shelf must span 2+ pages for this boundary walk";
    const int last_offset = total - rows;
    const std::size_t page_hops =
        static_cast<std::size_t>((last_offset + rows - 1) / rows);
    CampaignNavigationInjectorContext next_context{
        page_hops, rows - 1, true};

    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    ScopedSdlThread next_thread(SDL_CreateThread(
        campaign_next_prev_choose_injector,
        "campaign_next_choose", &next_context));
    ASSERT_TRUE(next_thread.valid());
    const CampaignResult next_result = pick_campaign(
        &og::runtime::current_session->myscreen_->save_data, false);
    const int next_thread_result = next_thread.join();

    ASSERT_EQ(0, next_thread_result);
    ASSERT_EQ(last, next_result.id)
        << "paging NEXT then choosing the last row must reach the last ordered id";

    input_guard.reset();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(last));
    CampaignNavigationInjectorContext prev_context{
        page_hops, 0, false};
    ScopedSdlThread prev_thread(SDL_CreateThread(
        campaign_next_prev_choose_injector,
        "campaign_prev_choose", &prev_context));
    ASSERT_TRUE(prev_thread.valid());
    const CampaignResult prev_result = pick_campaign(
        &og::runtime::current_session->myscreen_->save_data, false);
    const int prev_thread_result = prev_thread.join();

    EXPECT_EQ(0, prev_thread_result);
    EXPECT_EQ(first, prev_result.id)
        << "paging PREV then choosing row 0 must return to the first ordered id";
}

TEST(CampaignAndLevelPicker, campaign_picker_enter_id_returns_exact_prompt_text)
{
    CampaignPickerInputGuard input_guard;
    CampaignMountGuard mount_guard;
    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    PromptQueueGuard prompt_queue;
    prompt_queue.push("manual-selection");
    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    ScopedSdlThread thread(SDL_CreateThread(
        campaign_enter_id_injector, "campaign_enter_id", nullptr));
    ASSERT_TRUE(thread.valid());
    const CampaignResult result = pick_campaign(
        &og::runtime::current_session->myscreen_->save_data, false);
    const int thread_result = thread.join();

    EXPECT_EQ(0, thread_result);
    EXPECT_EQ("manual-selection", result.id);
    EXPECT_EQ(1, result.first_level)
        << "manual IDs retain the CampaignResult default first level";
}

TEST(CampaignAndLevelPicker, campaign_picker_confirmed_delete_removes_only_selected_package)
{
    const std::string temporary_id = unique_test_campaign_id(
        "cpdel");
    ASSERT_TRUE(is_safe_campaign_id(temporary_id));
    const std::filesystem::path temporary_package =
        std::filesystem::path(get_user_path()) / "campaigns" /
        (temporary_id + ".glad");
    ASSERT_FALSE(std::filesystem::exists(temporary_package))
        << "the generated campaign ID must be test-owned";
    CampaignPickerInputGuard input_guard;
    TemporaryCampaignGuard campaign_guard{
        get_mounted_campaign(), temporary_id};

    CampaignData source("gladiator");
    ASSERT_TRUE(source.load());
    source.title = "Delete Only This Campaign";
    ASSERT_TRUE(source.save_as(temporary_id));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(temporary_id));

    const std::list<std::string> campaigns_before = list_campaigns();
    ASSERT_TRUE(std::find(campaigns_before.begin(), campaigns_before.end(),
                          temporary_id) != campaigns_before.end());
    CampaignDeleteInjectorContext injector_context{
        temporary_package};
    ASSERT_TRUE(std::filesystem::exists(injector_context.package));

    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;
    YesNoQueueGuard yes_no_queue;
    yes_no_queue.push(true);
    yes_no_queue.push(true);

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    ScopedSdlThread thread(SDL_CreateThread(
        campaign_confirmed_delete_injector,
        "campaign_confirmed_delete", &injector_context));
    ASSERT_TRUE(thread.valid());
    const CampaignResult result = pick_campaign(
        &og::runtime::current_session->myscreen_->save_data, true);
    const int thread_result = thread.join();

    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(0, picker_testing_yes_or_no_queue_remaining())
        << "both destructive confirmations must be consumed";
    EXPECT_TRUE(result.id.empty());
    EXPECT_FALSE(std::filesystem::exists(injector_context.package));
    const std::list<std::string> campaigns_after = list_campaigns();
    for (const std::string& id : campaigns_before)
    {
        if (id != temporary_id)
        {
            EXPECT_TRUE(std::find(campaigns_after.begin(),
                                  campaigns_after.end(), id) !=
                        campaigns_after.end())
                << "deleting one selected campaign must preserve " << id;
        }
    }
}

TEST(CampaignAndLevelPicker, campaign_picker_confirmed_reset_clears_selected_progress_only)
{
    const std::string temporary_id = unique_test_campaign_id(
        "cprst");
    ASSERT_TRUE(is_safe_campaign_id(temporary_id));
    const std::filesystem::path temporary_package =
        std::filesystem::path(get_user_path()) / "campaigns" /
        (temporary_id + ".glad");
    ASSERT_FALSE(std::filesystem::exists(temporary_package))
        << "the generated campaign ID must be test-owned";
    CampaignPickerInputGuard input_guard;
    TemporaryCampaignGuard campaign_guard{
        get_mounted_campaign(), temporary_id};

    CampaignData source("gladiator");
    ASSERT_TRUE(source.load());
    source.title = "Reset Only This Campaign";
    ASSERT_TRUE(source.save_as(temporary_id));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(temporary_id));

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const auto saved_completed_levels = save.completed_levels;
    struct CompletedLevelsRestore
    {
        SaveData& save;
        std::map<std::string, std::set<int>> completed;
        ~CompletedLevelsRestore()
        {
            save.completed_levels = std::move(completed);
        }
    } completed_restore{save, saved_completed_levels};
    save.add_level_completed(temporary_id, 1);
    save.add_level_completed(temporary_id, 3);
    save.add_level_completed("gladiator", 77);
    ASSERT_EQ(2, save.get_num_levels_completed(temporary_id));
    ASSERT_EQ(1u,
              save.completed_levels.at("gladiator").count(77));

    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;
    YesNoQueueGuard yes_no_queue;
    yes_no_queue.push(true);
    yes_no_queue.push(true);

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    ScopedSdlThread thread(SDL_CreateThread(
        campaign_confirmed_reset_injector,
        "campaign_confirmed_reset", nullptr));
    ASSERT_TRUE(thread.valid());
    const CampaignResult result = pick_campaign(&save, false);
    const int thread_result = thread.join();

    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(0, picker_testing_yes_or_no_queue_remaining())
        << "both destructive confirmations must be consumed";
    EXPECT_TRUE(result.id.empty());
    EXPECT_EQ(0, save.get_num_levels_completed(temporary_id));
    EXPECT_EQ(1u,
              save.completed_levels.at("gladiator").count(77))
        << "resetting the selected campaign must preserve other progress";
}


TEST(CampaignAndLevelPicker, load_campaign_invalid_id_reports_error)
{
    std::map<std::string, int> current_levels;
    const std::string old_campaign = get_mounted_campaign();

    int rv = load_campaign("this_campaign_should_not_exist", current_levels, 1);
    ASSERT_EQ(-2, rv) << "load_campaign should return -2 when mount fails";

    // Restore environment for tests that expect a mounted campaign.
    ASSERT_TRUE(mount_campaign_package_with_error(old_campaign) == CampaignPackageIoError::None) << "failed to remount original campaign";
}


TEST(CampaignAndLevelPicker, load_campaign_with_error_typed_result_paths)
{
    std::map<std::string, int> current_levels;
    current_levels["gladiator"] = 7;

    const std::string old_campaign = get_mounted_campaign();
    CampaignLoadResult typed = load_campaign_with_error("gladiator", current_levels, 1);
    ASSERT_EQ(static_cast<int>(CampaignLoadError::None), static_cast<int>(typed.error)) << "typed load_campaign should succeed for mounted campaign";
    ASSERT_EQ(7, typed.current_level) << "typed load_campaign should return mapped current level";

    typed = load_campaign_with_error("this_campaign_should_not_exist", current_levels, 1);
    ASSERT_EQ(static_cast<int>(CampaignLoadError::MountFailed), static_cast<int>(typed.error)) << "typed load_campaign should report MountFailed for invalid campaign";

    // Restore environment for tests that expect a mounted campaign.
    ASSERT_TRUE(mount_campaign_package_with_error(old_campaign) == CampaignPackageIoError::None) << "failed to remount original campaign";
}


TEST(CampaignAndLevelPicker, level_picker_cancel_esc_returns_default)
{
    LevelRuntimeData ld(1);
    ld.create_new_grid();
    walker* e1 = ld.add_ob(Order::Living, FAMILY_ORC);
    walker* e2 = ld.add_ob(Order::Living, FAMILY_BIG_ORC);
    walker* ally = ld.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(e1 && e2 && ally) << "level test walkers should be created";
    if (e1 && e2 && ally) {
        e1->set_team_num(1);
        e2->set_team_num(1);
        ally->set_team_num(0);
        e1->stats()->set_level(4);
        e2->stats()->set_level(2);
        ally->stats()->set_level(3);
    }
    walker* x1 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    walker* x2 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    walker* x3 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    ASSERT_TRUE(x1 && x2 && x3) << "exit markers should be created";
    if (x1 && x2 && x3) {
        x1->stats()->set_level(9);
        x2->stats()->set_level(5);
        x3->stats()->set_level(9);
    }

    int max_enemy = 0;
    float avg_enemy = 0.0f;
    int num_enemy = 0;
    float difficulty = 0.0f;
    std::list<int> exits;
    getLevelStats(ld, &max_enemy, &avg_enemy, &num_enemy, &difficulty, exits);
    ASSERT_EQ(2, num_enemy) << "getLevelStats should count enemy team members";
    ASSERT_EQ(4, max_enemy) << "getLevelStats should report max enemy level";
    ASSERT_TRUE(avg_enemy > 2.9f && avg_enemy < 3.1f) << "getLevelStats should report average enemy level";
    ASSERT_TRUE(difficulty > 8.9f && difficulty < 9.1f) << "getLevelStats should subtract ally difficulty";
    ASSERT_EQ(2, (int)exits.size()) << "getLevelStats should sort and uniquify exits";
    ASSERT_EQ(5, exits.front()) << "getLevelStats exits should be sorted";
    ASSERT_EQ(9, exits.back()) << "getLevelStats exits should include highest exit level";
    ld.delete_objects();

    LevelRuntimeData empty_level(1);
    empty_level.create_new_grid();
    avg_enemy = -1.0f;
    exits = {99};
    getLevelStats(empty_level, nullptr, &avg_enemy, nullptr, nullptr, exits);
    ASSERT_EQ(0.0f, avg_enemy)
        << "an enemy-free level should have a defined zero average";
    ASSERT_TRUE(exits.empty())
        << "getLevelStats should replace, rather than append to, exit data";

    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;
    int canceled = pick_level(og::runtime::current_session->myscreen_, 1, false);
    og::runtime::current_session->myscreen_->world().end = old_end;
    ASSERT_EQ(1, canceled) << "level cancel should return default level";
}


TEST(CampaignAndLevelPicker, level_picker_choose_and_delete_prompt_paths)
{
    ViewportGuard guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    SDL_Thread* choose_thread = SDL_CreateThread(level_picker_choose_injector, "level_picker_choose", nullptr);
    ASSERT_TRUE(choose_thread != nullptr) << "failed to create level picker choose injector";
    int chosen = pick_level(og::runtime::current_session->myscreen_, 1, false);
    int choose_rc = 0;
    SDL_WaitThread(choose_thread, &choose_rc);
    ASSERT_TRUE(chosen > 0) << "choose path should return a valid level id";

    SDL_Thread* delete_thread = SDL_CreateThread(level_picker_delete_then_cancel_injector, "level_picker_delete_cancel", nullptr);
    ASSERT_TRUE(delete_thread != nullptr) << "failed to create level picker delete injector";
    int canceled_after_delete_prompt = pick_level(og::runtime::current_session->myscreen_, 1, true);
    int delete_rc = 0;
    SDL_WaitThread(delete_thread, &delete_rc);
    ASSERT_EQ(1, canceled_after_delete_prompt) << "delete prompt + cancel should keep default level";

    og::runtime::current_session->myscreen_->world().end = old_end;
}

TEST(CampaignAndLevelPicker, level_picker_scroll_repositions_preview_entries)
{
    ViewportGuard guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    const std::vector<int> levels = list_levels_v();
    ASSERT_GT(levels.size(), 3u)
        << "the picker needs another full preview window to exercise NEXT";
    const int starting_level = levels.front();
    const int expected_level_after_scroll = levels[1];

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    level_picker_testing_input_reset();
    SDL_Thread* thread = SDL_CreateThread(
        level_picker_scroll_then_choose_first_injector,
        "level_picker_scroll_choose", nullptr);
    ASSERT_TRUE(thread != nullptr);
    const int chosen = pick_level(
        og::runtime::current_session->myscreen_, starting_level, false);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(expected_level_after_scroll, chosen)
        << "NEXT must move the second level into the first preview's clickable row";
}

TEST(CampaignAndLevelPicker, level_picker_previous_restores_prior_preview_entries)
{
    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    const std::vector<int> levels = list_levels_v();
    ASSERT_GT(levels.size(), 3u)
        << "PREV needs a full preview window after one NEXT step";
    const int starting_level = levels.front();

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    level_picker_testing_input_reset();
    SDL_Thread* thread = SDL_CreateThread(
        level_picker_scroll_back_then_choose_first_injector,
        "level_picker_scroll_back_choose", nullptr);
    ASSERT_TRUE(thread != nullptr);
    const int chosen = pick_level(
        og::runtime::current_session->myscreen_, starting_level, false);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(starting_level, chosen)
        << "PREV must restore the original first preview and its level id";
}

TEST(CampaignAndLevelPicker, level_picker_enter_id_returns_valid_prompt_value)
{
    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    PromptQueueGuard prompt_queue;
    prompt_queue.push("42");

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    level_picker_testing_input_reset();
    SDL_Thread* thread = SDL_CreateThread(
        level_picker_enter_id_injector, "level_picker_enter_id", nullptr);
    ASSERT_TRUE(thread != nullptr);
    const int chosen = pick_level(
        og::runtime::current_session->myscreen_, 1, false);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(42, chosen)
        << "ENTER ID must accept a positive integer even when it is not listed";
}

TEST(CampaignAndLevelPicker, level_picker_delete_removes_only_selected_level)
{
    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    const std::string temporary_id =
        "test.level_picker_delete";
    TemporaryCampaignGuard campaign_guard{
        get_mounted_campaign(), temporary_id};
    delete_campaign(temporary_id);

    CampaignData campaign("gladiator");
    ASSERT_TRUE(campaign.load());
    ASSERT_TRUE(campaign.save_as(temporary_id));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(temporary_id));

    const std::vector<int> before = list_levels_v();
    ASSERT_GT(before.size(), 3u);
    const int deleted_level = before.front();

    YesNoQueueGuard yes_no_queue;
    yes_no_queue.push(true);

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    level_picker_testing_input_reset();
    SDL_Thread* thread = SDL_CreateThread(
        level_picker_delete_once_then_cancel_injector,
        "level_picker_delete_once", nullptr);
    ASSERT_TRUE(thread != nullptr);
    const int chosen = pick_level(
        og::runtime::current_session->myscreen_, deleted_level, true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    const std::vector<int> after = list_levels_v();
    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(deleted_level, chosen)
        << "canceling after deletion keeps the caller's default selection";
    ASSERT_EQ(before.size() - 1u, after.size());
    EXPECT_TRUE(std::find(after.begin(), after.end(), deleted_level) ==
                after.end())
        << "DELETE must remove exactly the selected level from the copy";
    for (const int level : after)
    {
        EXPECT_TRUE(std::find(before.begin(), before.end(), level) !=
                    before.end())
            << "DELETE must not synthesize or replace unrelated levels";
    }
}

// picker_main_menu.cpp: the new-game flow under test below.
bool picker_prepare_new_game_setup();

TEST(CampaignAndLevelPicker, sync_campaign_mount_follows_save_and_restores_on_failure)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_mounted = get_mounted_campaign();
    const std::string old_save_campaign = save.current_campaign;

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    save.current_campaign = "modes";
    ASSERT_TRUE(og::ui::sync_campaign_mount_to_save(save))
        << "the ctf package ships with the game and should mount";
    ASSERT_EQ(std::string("modes"), get_mounted_campaign());

    // Same campaign again: success without touching the mount.
    ASSERT_TRUE(og::ui::sync_campaign_mount_to_save(save));
    ASSERT_EQ(std::string("modes"), get_mounted_campaign());

    // Missing package: report failure and restore the previous mount.
    save.current_campaign = "this_campaign_should_not_exist";
    ASSERT_FALSE(og::ui::sync_campaign_mount_to_save(save));
    ASSERT_EQ(std::string("modes"), get_mounted_campaign())
        << "failed sync must put the previous mount back";

    save.current_campaign = old_save_campaign;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(old_mounted))
        << "failed to remount original campaign";
}

namespace
{
int new_game_name_accepter(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    // §2.2: picker_prepare_new_game_setup() opens the name-entry screen
    // first; accept the generated company name. The campaign intro no
    // longer runs here — it moved behind the campaign select (issue #186:
    // the intro belongs to the campaign the player picks), so after the
    // accept the flow under test runs to completion without input.
    accept_generated_company_name(10000);
    return 0;
}
} // namespace

TEST(CampaignAndLevelPicker, new_game_resets_campaign_and_mount_to_default)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_mounted = get_mounted_campaign();

    // Simulate "the last session played CTF": the save selects it and its
    // package is the one mounted (SaveData::load() would have done both).
    save.current_campaign = "modes";
    save.scen_num = 505;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // team_size == 0 keeps the flow from raising the "restart?" prompt.
    for (int i = 0; i < MAX_TEAM_SIZE; i++)
        save.team_list[static_cast<std::size_t>(i)].reset();
    save.team_size = 0;

    SDL_Thread* thread =
        SDL_CreateThread(new_game_name_accepter, "name_accept", nullptr);
    ASSERT_TRUE(thread != nullptr) << "failed to create name accepter thread";
    const bool ok = picker_prepare_new_game_setup();
    SDL_WaitThread(thread, nullptr);

    ASSERT_TRUE(ok) << "new game setup should not abort";
    ASSERT_EQ(std::string("gladiator"), save.current_campaign)
        << "a new game must reset to the default campaign";
    ASSERT_EQ(std::string("gladiator"), get_mounted_campaign())
        << "a new game must remount the default campaign package";
    ASSERT_EQ(1, save.scen_num) << "a new game must rewind the level cursor";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(old_mounted));
}

// --- Issue #186: committing a selection must survive a failed load ---------

// button.cpp handler under test (SET CAMPAIGN mid-game path).
Sint32 do_pick_campaign(Sint32 arg1);
// handle_menu_nav's TESTING hook (the options-menu capture precedent).
extern int g_test_menu_nav_key;

TEST(CampaignAndLevelPicker, apply_campaign_selection_commits_or_rolls_back)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_mounted = get_mounted_campaign();
    const std::string old_campaign = save.current_campaign;
    const short old_scen = save.scen_num;

    save.current_campaign = "gladiator";
    save.scen_num = 1;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    // Success: the save follows the selection and the cursor comes from
    // load_campaign (first_level when the campaign has no tracked cursor).
    save.current_levels.erase("modes");
    ASSERT_TRUE(og::ui::apply_campaign_selection(save, "modes", 300));
    EXPECT_EQ(std::string("modes"), save.current_campaign);
    EXPECT_EQ(300, save.scen_num);
    EXPECT_EQ(std::string("modes"), get_mounted_campaign());

    // Failure: the save keeps the previous selection AND the previous
    // package is remounted — no negative scen_num can reach the save.
    ASSERT_FALSE(og::ui::apply_campaign_selection(
        save, "this_campaign_should_not_exist", 1));
    EXPECT_EQ(std::string("modes"), save.current_campaign)
        << "a failed load must not adopt the bad campaign id";
    EXPECT_EQ(300, save.scen_num)
        << "a failed load must not clobber the level cursor";
    EXPECT_EQ(std::string("modes"), get_mounted_campaign())
        << "a failed load must remount the previous package";

    save.current_campaign = old_campaign;
    save.scen_num = old_scen;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(old_mounted));
}

TEST(CampaignAndLevelPicker, set_campaign_bad_enter_id_pops_and_keeps_save)
{
    trace_clear();
    CampaignPickerInputGuard input_guard;
    CampaignMountGuard mount_guard;
    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_campaign = save.current_campaign;
    const short old_scen = save.scen_num;
    // Deterministic starting point: save and mount agree on gladiator.
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    PromptQueueGuard prompt_queue;
    prompt_queue.push("this_campaign_should_not_exist");
    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    ScopedSdlThread thread(SDL_CreateThread(
        campaign_enter_id_injector, "campaign_bad_enter_id", nullptr));
    ASSERT_TRUE(thread.valid());
    const Sint32 redraw = do_pick_campaign(0);
    const int thread_result = thread.join();

    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(2, static_cast<int>(redraw)) << "handler must return MENU_REDRAW";
    EXPECT_TRUE(trace_contains("popup", "Could not load campaign"))
        << "the failed load must surface a popup";
    EXPECT_EQ(std::string("gladiator"), save.current_campaign)
        << "the save must keep its previous campaign";
    EXPECT_EQ(1, save.scen_num)
        << "no negative load_campaign return may reach scen_num";
    EXPECT_EQ(std::string("gladiator"), get_mounted_campaign())
        << "the previous package must be remounted";

    save.current_campaign = old_campaign;
    save.scen_num = old_scen;
}

// Arrow keys drive the list: walking the highlight up from CANCEL lands on
// the last visible row, and the detail cursor follows it, so OK chooses
// that row's campaign.
TEST(CampaignAndLevelPicker, campaign_picker_keyboard_row_highlight_moves_cursor)
{
    cleanup_leftover_test_campaigns();
    std::list<std::string> ordered = list_campaigns();
    og::ui::order_campaigns_for_select(ordered);
    const int rows = og::ui::campaign_picker_layout().list_rows;
    ASSERT_GT(static_cast<int>(ordered.size()), rows)
        << "the shipped shelf must overflow one page so NEXT is the up-exit";
    // Page 1 shows rows [0, rows); the expected pick is its last row.
    const std::string expected =
        *std::next(ordered.begin(), static_cast<std::ptrdiff_t>(rows - 1));

    CampaignPickerInputGuard input_guard;
    CampaignMountGuard mount_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(ordered.front()));

    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    char& end = og::runtime::current_session->myscreen_->world().end;
    WorldEndGuard end_guard(end);
    end = 0;

    struct KeyboardWalkInjector
    {
        static int run(void*)
        {
            og::runtime::ensure_thread_session();
            if (!wait_for_campaign_picker_ready())
                return 1;
            // CANCEL.up -> NEXT (page 1 of 2+), NEXT.up -> last visible row.
            // The nav hook is consumed once per ~10ms loop pass; the delays
            // give each press its own pass (options-menu capture precedent).
            SDL_Delay(300);
            g_test_menu_nav_key = KEY_UP;
            SDL_Delay(500);
            g_test_menu_nav_key = KEY_UP;
            SDL_Delay(500);
            const og::ui::PickerRect ok_rect =
                og::ui::campaign_picker_layout().choose;
            return click_campaign_picker_action(rect_center_x(ok_rect),
                                                rect_center_y(ok_rect))
                ? 0
                : 2;
        }
    };

    ScopedSdlThread thread(SDL_CreateThread(
        KeyboardWalkInjector::run, "campaign_keyboard_walk", nullptr));
    ASSERT_TRUE(thread.valid());
    const CampaignResult result = pick_campaign(
        &og::runtime::current_session->myscreen_->save_data, false);
    const int thread_result = thread.join();

    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(expected, result.id)
        << "the arrowed-to row must be the one OK chooses";
}
