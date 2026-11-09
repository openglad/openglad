namespace {
std::atomic<std::uint64_t> g_menu_screen_completed_frames{0};
// Mirror of the live loop's `highlighted_button`, which is a local of
// run_menu_screen with no accessor of its own. -1 whenever no engine screen
// is running, so a waiter can tell "no highlight yet" from index 0.
std::atomic<int> g_menu_screen_highlighted_button{-1};
// Draw-time publication check. A gate pass (apply_row_states) resets every
// Always row to visible; a Rewire screen's nav program then hides the rows
// it owns (Base Camp's empty deploy boxes). True between the two, so a frame
// drawn while it is set shows rows the screen never meant to show. Main
// thread only, like the passes it brackets.
bool g_row_states_awaiting_nav = false;
std::atomic<std::uint64_t> g_menu_screen_draws_before_nav{0};

// Clears the mirror on EVERY exit path of run_menu_screen (there are six,
// including two nested-door propagations), so an injector that reads it after
// a screen closed sees -1 rather than the last screen's index. It is defined
// out here rather than inside run_menu_screen because a struct defined inside
// a function hides that function from the site-key scanner in
// scripts/check_fadeblack_sites.sh: with the definition nested, the three
// classified fades below it read as unclassified calls from
// `~HighlightMirrorScope` and the check (a build dependency) fails.
struct HighlightMirrorScope {
    ~HighlightMirrorScope()
    {
        g_menu_screen_highlighted_button.store(-1, std::memory_order_release);
    }
};
}

std::uint64_t menu_screen_testing_completed_frames()
{
    return g_menu_screen_completed_frames.load(std::memory_order_acquire);
}

int menu_screen_testing_highlighted_button()
{
    return g_menu_screen_highlighted_button.load(std::memory_order_acquire);
}

std::uint64_t menu_screen_testing_draws_before_nav()
{
    return g_menu_screen_draws_before_nav.load(std::memory_order_acquire);
}
