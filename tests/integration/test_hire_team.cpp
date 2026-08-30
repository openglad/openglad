#include <memory>
#include <array>
#include <functional>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace
{
int remaining_menu_wait_ms(Uint64 deadline)
{
    const Uint64 now = SDL_GetTicks();
    return now >= deadline ? 0 : static_cast<int>(deadline - now);
}

bool wait_for_menu_surface(const char* description,
                           const std::function<bool()>& visible,
                           int timeout_ms)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    while (remaining_menu_wait_ms(deadline) > 0)
    {
        if (visible())
        {
            const int remaining = remaining_menu_wait_ms(deadline);
            if (remaining > 0 && run_on_main_thread([] {}, remaining))
                return true;
            break;
        }
        SDL_Delay(50);
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for live %s (%d ms)\n",
            description, timeout_ms);
    return false;
}

bool interact_on_menu_thread(const char* id, int timeout_ms)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    while (remaining_menu_wait_ms(deadline) > 0)
    {
        if (has_interactable(id))
        {
            const int remaining = remaining_menu_wait_ms(deadline);
            if (remaining > 0
                && run_on_main_thread([id] { interact(id); }, remaining))
            {
                return true;
            }
            break;
        }
        SDL_Delay(50);
    }
    fprintf(stderr, "  [interact] TIMEOUT dispatching live '%s' (%d ms)\n",
            id, timeout_ms);
    return false;
}

bool wait_for_menu_condition(const char* description,
                             const std::function<bool()>& condition,
                             int timeout_ms = 10000)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    while (remaining_menu_wait_ms(deadline) > 0)
    {
        bool matched = false;
        const int remaining = remaining_menu_wait_ms(deadline);
        if (remaining <= 0
            || !run_on_main_thread([&] { matched = condition(); }, remaining))
        {
            break;
        }
        if (matched)
            return true;
        SDL_Delay(10);
    }
    fprintf(stderr,
            "  [interact] TIMEOUT waiting for menu condition '%s' (%d ms)\n",
            description, timeout_ms);
    return false;
}

bool wait_for_base_camp(int timeout_ms = 10000)
{
    return wait_for_menu_surface(
        "Base Camp surface",
        [] {
            return has_interactable("hire_troops")
                && has_interactable("networking");
        },
        timeout_ms);
}

// On a failed condition, unwind whichever engine-hosted menu is still open
// so the owning test reports its assertion instead of timing out in
// picker_main. These BACK clicks are cleanup, not retries of the tested path.
void unwind_picker_menu_stack()
{
    for (int depth = 0; depth < 3; ++depth)
    {
        if (!interact_on_menu_thread("back", 2000))
            return;
    }
}
} // namespace


static void cleanup_picker_state()
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

// Test: Navigate to hire troops, browse characters with NEXT/PREV, then exit.
//
// Note: We can't click HIRE ME because add_guy() calls prompt_for_string()
// to name the character, which blocks on text input. Instead we test that
// the hire menu loads, character cycling works, and we can exit cleanly.
//
// Flow: Main Menu -> Begin New Game -> (accept company name) ->
//       Team Build -> Hire Troops -> NEXT -> NEXT -> PREV -> Back -> Back

struct HireState {
    bool started;
    bool finished;
    bool saw_team_menu;
    bool saw_hire_menu;
    int cycles_completed;
};

static int hire_injector(void* data)
{
    og::runtime::ensure_thread_session();
    HireState* state = static_cast<HireState*>(data);
    state->started = true;

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    if (!interact_on_menu_thread("begin_new_game", 5000)) {
        unwind_picker_menu_stack();
        return 0;
    }

    // §2.2: accept the generated company name at the name-entry screen.
    fprintf(stderr, "  [test] accepting generated company name\n");
    if (!interact_on_menu_thread("company_name_accept", 5000)) {
        unwind_picker_menu_stack();
        return 0;
    }

    // No campaign intro here anymore (issue #186: it moved behind the
    // campaign select, skipped under TESTING) — an Escape here would BACK
    // out of the team-build screen instead.

    // New games now land on team build first, then enter hire explicitly.
    if (!wait_for_base_camp()) {
        unwind_picker_menu_stack();
        return 0;
    }
    state->saw_team_menu = true;
    fprintf(stderr, "  [test] clicking hire_troops\n");
    if (!interact_on_menu_thread("hire_troops", 10000)) {
        unwind_picker_menu_stack();
        return 0;
    }

    if (!wait_for_menu_surface(
            "hire surface",
            [] {
                return has_interactable("hire_me")
                    && has_interactable("next")
                    && has_interactable("prev");
            },
            10000))
    {
        unwind_picker_menu_stack();
        return 0;
    }
    state->saw_hire_menu = true;

    int initial_family = -1;
    if (!wait_for_menu_condition("initial hire family", [&] {
            const guy* const current =
                og::runtime::current_session->current_guy_.get();
            if (current == nullptr)
                return false;
            initial_family = static_cast<unsigned char>(current->family);
            return true;
        }))
    {
        unwind_picker_menu_stack();
        return 0;
    }

    // Cycle through candidates, acknowledging each click by the exact family
    // transition instead of assuming it landed after a flat delay.
    fprintf(stderr, "  [test] clicking next\n");
    if (!interact_on_menu_thread("next", 10000)) {
        unwind_picker_menu_stack();
        return 0;
    }
    int first_next_family = -1;
    if (!wait_for_menu_condition("first NEXT family", [&] {
            const guy* const current =
                og::runtime::current_session->current_guy_.get();
            if (current == nullptr)
                return false;
            const int family = static_cast<unsigned char>(current->family);
            if (family == initial_family)
                return false;
            first_next_family = family;
            return true;
        }))
    {
        unwind_picker_menu_stack();
        return 0;
    }
    ++state->cycles_completed;

    fprintf(stderr, "  [test] clicking next again\n");
    if (!interact_on_menu_thread("next", 10000)) {
        unwind_picker_menu_stack();
        return 0;
    }
    int second_next_family = -1;
    if (!wait_for_menu_condition("second NEXT family", [&] {
            const guy* const current =
                og::runtime::current_session->current_guy_.get();
            if (current == nullptr)
                return false;
            const int family = static_cast<unsigned char>(current->family);
            if (family == first_next_family)
                return false;
            second_next_family = family;
            return true;
        }))
    {
        unwind_picker_menu_stack();
        return 0;
    }
    ++state->cycles_completed;

    fprintf(stderr, "  [test] clicking prev\n");
    if (!interact_on_menu_thread("prev", 10000)
        || !wait_for_menu_condition("PREV restores first NEXT family", [&] {
            const guy* const current =
                og::runtime::current_session->current_guy_.get();
            return current != nullptr
                && static_cast<unsigned char>(current->family)
                    == first_next_family;
        }))
    {
        unwind_picker_menu_stack();
        return 0;
    }
    ++state->cycles_completed;
    (void)second_next_family;

    // Go back to team menu
    fprintf(stderr, "  [test] clicking back from hire menu\n");
    if (!interact_on_menu_thread("back", 10000)
        || !wait_for_base_camp())
    {
        unwind_picker_menu_stack();
        return 0;
    }

    // Back to main menu
    fprintf(stderr, "  [test] clicking back from team menu\n");
    if (!interact_on_menu_thread("back", 10000)) {
        unwind_picker_menu_stack();
        return 0;
    }

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_menu_browsing) {
    trace_clear();

    // Start with empty team
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    HireState state = { false, false, false, false, 0 };
    SDL_Thread* thread = SDL_CreateThread(hire_injector, "hire_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_team_menu)
        << "company-name ACCEPT should land on Base Camp";
    ASSERT_TRUE(state.saw_hire_menu) << "should have seen the hire menu";
    ASSERT_EQ(3, state.cycles_completed)
        << "NEXT, NEXT, PREV must each change the displayed family once";
}

// §2.9 flow 7 + §3.8: HIRE re-enters from the base-camp command strip, a
// successful hire lands as a roster row that is DEPLOYED BY DEFAULT, and the
// hire mutation autosaves the company (the new member is on disk without any
// manual save). Under TESTING the hire name prompt accepts the generated
// name without blocking.
//
// Flow: Main Menu -> Continue -> base camp -> HIRE -> hire_me -> Back ->
//       base camp shows the new row -> Back

struct HireDeployState {
    bool started;
    bool finished;
    bool saw_base_camp;
    bool saw_hire_menu;
    bool hired;
    bool saw_new_row;
};

static int hire_deployed_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<HireDeployState*>(data);
    state->started = true;

    if (!interact_on_menu_thread("continue_game", 5000)) {
        unwind_picker_menu_stack();
        return 0;
    }

    if (!wait_for_base_camp()) {
        unwind_picker_menu_stack();
        return 0;
    }
    state->saw_base_camp = true;
    if (!interact_on_menu_thread("hire_troops", 10000)) {
        unwind_picker_menu_stack();
        return 0;
    }

    if (!wait_for_menu_surface(
            "hire surface",
            [] {
                return has_interactable("hire_me")
                    && has_interactable("next")
                    && has_interactable("back");
            },
            10000))
    {
        unwind_picker_menu_stack();
        return 0;
    }
    state->saw_hire_menu = true;
    fprintf(stderr, "  [test] clicking hire_me\n");
    if (!interact_on_menu_thread("hire_me", 10000)
        || !wait_for_menu_condition("new hire in roster", [] {
            const SaveData& save =
                og::runtime::current_session->myscreen_->save_data;
            return save.team_size == 2 && save.team_list[1] != nullptr
                && save.team_list[1]->deployed;
        }))
    {
        unwind_picker_menu_stack();
        return 0;
    }
    state->hired = true;

    fprintf(stderr, "  [test] clicking back from hire menu\n");
    if (!interact_on_menu_thread("back", 10000)) {
        unwind_picker_menu_stack();
        return 0;
    }

    // Re-entry: the base camp shows the hired member as roster row 1.
    if (!wait_for_menu_surface(
            "Base Camp hired roster row",
            [] {
                return has_interactable("hire_troops")
                    && has_interactable("roster_dep_1");
            },
            10000))
    {
        unwind_picker_menu_stack();
        return 0;
    }
    state->saw_new_row = true;
    fprintf(stderr, "  [test] clicking back from base camp\n");
    if (!interact_on_menu_thread("back", 10000)) {
        unwind_picker_menu_stack();
        return 0;
    }

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_from_base_camp_lands_deployed_and_autosaves) {
    trace_clear();

    // One existing member + gold to hire with; CONTINUE needs the file on
    // disk (it also serves as the pre-hire disk baseline).
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    {
        auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
        soldier->name = "VETERAN";
        og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
        og::runtime::current_session->myscreen_->save_data.team_size = 1;
        og::runtime::current_session->myscreen_->save_data.m_totalcash[0] = 100000;
        og::runtime::current_session->myscreen_->save_data.totalcash = 100000;
    }
    og::runtime::current_session->myscreen_->save_data.save("save0");

    HireDeployState state = { false, false, false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(hire_deployed_injector, "hire_deploy_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_base_camp)
        << "CONTINUE should land on the Base Camp surface";
    ASSERT_TRUE(state.saw_hire_menu) << "HIRE strip button should open the hire screen";
    ASSERT_TRUE(state.hired) << "hire_me should be clickable";
    ASSERT_TRUE(state.saw_new_row)
        << "the hired member must appear as a base-camp roster row on re-entry";

    // In memory: the hire landed and defaults to deployed (§2.5).
    ASSERT_EQ(2, static_cast<int>(og::runtime::current_session->myscreen_->save_data.team_size));
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[1] != nullptr);
    EXPECT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[1]->deployed)
        << "new hires default to deployed=true";

    // §3.8: the hire mutation AUTOSAVED — the pre-hire disk baseline had one
    // member; the file must now hold the hired member, deployed.
    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    ASSERT_EQ(2, static_cast<int>(reloaded.team_size));
    ASSERT_TRUE(reloaded.team_list[1] != nullptr);
    EXPECT_TRUE(reloaded.team_list[1]->deployed)
        << "the hired member must persist deployed via the mutation autosave";
    EXPECT_EQ("VETERAN", reloaded.team_list[0]->name) << "slot 0 untouched";
}
