#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/sai2x.h>
#include <openglad/resources/company.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include "test_interact.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

// picker.cpp globals
#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


// picker_input.cpp global keyboard state observer

// picker.cpp menu entry
Sint32 create_detail_menu(guy* arg1);
Sint32 create_train_menu(Sint32 arg1);
const char* family_name_copy(short family);
void picker_lobby_shutdown();
void picker_lobby_initialize_from_save();
void picker_lobby_sync_settings_from_save();
void picker_lobby_sync_roster_from_save();

namespace
{
struct PickerStateGuard
{
    std::unique_ptr<guy> saved_current;
    guy* saved_old = nullptr;
    Sint32 saved_editguy = 0;
    unsigned char saved_team_size = 0;

    PickerStateGuard()
    {
        saved_current = std::move(og::runtime::current_session->current_guy_);
        saved_old = pks().old_guy;
        saved_editguy = og::runtime::current_session->editguy_;
        saved_team_size = og::runtime::current_session->myscreen_->save_data.team_size;
    }

    ~PickerStateGuard()
    {
        og::runtime::current_session->current_guy_ = std::move(saved_current);
        pks().old_guy = saved_old;
        og::runtime::current_session->editguy_ = saved_editguy;
        og::runtime::current_session->myscreen_->save_data.team_size = saved_team_size;
    }
};

struct TeamSlotGuard
{
    int slot = 0;
    guy* saved = nullptr;
    explicit TeamSlotGuard(int slot_) : slot(slot_), saved(og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(slot_)].release()) {}
    ~TeamSlotGuard() { og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(slot)].reset(saved); }
};

struct KeyStateGuard
{
    const bool* saved = nullptr;
    std::array<bool, MAXKEYS> fake{};

    KeyStateGuard()
    {
        saved = og::runtime::current_session->keystates_;
        fake.fill(false);
        og::runtime::current_session->keystates_ = fake.data();
    }

    ~KeyStateGuard()
    {
        og::runtime::current_session->keystates_ = saved;
    }

    void pulse(SDL_Scancode sc, int down_ms = 25, int up_ms = 10)
    {
        fake[sc] = true;
        SDL_Delay(static_cast<Uint32>(down_ms));
        fake[sc] = false;
        SDL_Delay(static_cast<Uint32>(up_ms));
    }
};

struct InjectorArgs
{
    KeyStateGuard* ks = nullptr;
    bool go_to_promote = false;
    std::atomic<bool>* done = nullptr;
};

static int injector_thread_exit_detail_menu(void* data)
{
    og::runtime::ensure_thread_session();
    InjectorArgs* a = static_cast<InjectorArgs*>(data);
    // Wait until init_buttons has created vbuttons for this menu. If we pulse too
    // early, handle_menu_nav/leftmouse won't observe the press.
    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (SDL_GetTicks() < deadline)
    {
        // Through the locking accessor: a raw allbuttons_[] read races
        // init_buttons' publication (#257). BACK is the detail menu's
        // first row, so its arrival means the menu is up.
        if (has_interactable("back"))
            break;
        SDL_Delay(5);
    }

    const int logical_x = a->go_to_promote ? 200 : 20;
    const int logical_y = a->go_to_promote ? 30 : 180;
    // UI-canvas-pinned map — raw viewport math mismaps in non-16:10
    // windows (see test_interact.h).
    const auto [mapped_x, mapped_y] =
        ui_canvas_to_window(static_cast<float>(logical_x),
                            static_cast<float>(logical_y));
    const int click_x = static_cast<int>(std::lround(mapped_x));
    const int click_y = static_cast<int>(std::lround(mapped_y));
    do
    {
        og::input_native::push_mouse_button_event(true, og::input_native::kMouseButtonLeft, click_x, click_y);
        SDL_Delay(5);
    } while (a->done && !a->done->load(std::memory_order_relaxed));
    og::input_native::push_mouse_button_event(false, og::input_native::kMouseButtonLeft, click_x, click_y);
    return 0;
}

void prepare_detail_menu_mouse_click()
{
    clear_events();
    auto& input_hw = input_hardware_state();
    input_hw.mouse.left = 0;
    input_hw.mouse.right = 0;
    input_hw.picker_was_left_down = false;
    input_hw.picker_was_right_down = false;
}

// Both the train menu's ACCEPT path and (since issue #133) the promote
// button itself call picker_lobby_sync_roster_from_save(), which lazily
// creates the STANDALONE local lobby client and seeds its cached roster.
// Any later picker menu's picker_lobby_poll() then rewrites save.team_list
// from that stale cache (replacing the guys other tests just planted —
// observed as a use-after-free wedge of the promote tests). RAII so an
// early ASSERT can't skip the cleanup; every test that can promote or
// accept needs one.
struct PickerLobbyShutdownGuard
{
    ~PickerLobbyShutdownGuard() { picker_lobby_shutdown(); }
};

struct CanvasRoutingGuard
{
    CanvasTarget target = E_Screen->active_canvas();
    ~CanvasRoutingGuard() { E_Screen->set_active_canvas(target); }
};

// Palette-index ink counter. The picker paints indexed colours: the header
// font (pix/textbig.png) is a single-index pixie whose lit pixels are exactly
// RED (40), and the small font shades a glyph across its colour's five-entry
// ramp, so DARK_BLUE body text lands in 72..76.
std::size_t count_palette(int x1, int x2, int y1, int y2, int lo, int hi)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    std::size_t hits = 0;
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
        {
            int index = 0;
            scr->get_pixel(x, y, &index);
            if (index >= lo && index <= hi)
                ++hits;
        }
    return hits;
}

// create_detail_menu's abilities panel: draw_dialog(5, 68, 315, 167,
// "Character Special Abilities") plus render_family_abilities' left column
// (DETAIL_LM = 11, detail_line_y(n) = 90 + 6n).
constexpr int kAbilityHeaderX1 = 9;
constexpr int kAbilityHeaderX2 = 311;
constexpr int kAbilityHeaderY1 = 72;   // draw_dialog header field: y1+4
constexpr int kAbilityHeaderY2 = 86;   //                          y1+18
constexpr int kAbilityTextX1 = 11;
constexpr int kAbilityTextX2 = 155;
constexpr int kAbilityTextY1 = 88;
constexpr int kAbilityTextY2 = 163;
} // namespace

// create_detail_menu returns MENU_REDRAW from FOUR places (the two early-outs
// for an unseated/empty slot, the promote branch, and the BACK tail), so the
// return code alone proves nothing. Pin the painted frame: the loop must have
// run, drawn the abilities dialog, and rendered the family's OWN ability text
// (a family with no ability table paints the panel and no text at all).
TEST(PickerDetailMenuDriven, picker_detail_menu_paints_the_seated_family_abilities_then_exits_on_back)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    PickerLobbyShutdownGuard lobby_guard;
    CanvasRoutingGuard canvas_guard;

    struct Case
    {
        int family;
        const char* what;
        bool has_ability_table;   // get_family_detail() knows this family
    };
    const Case cases[] = {
        { FAMILY_SOLDIER,  "soldier",  true  },
        { FAMILY_THIEF,    "thief",    true  },
        { FAMILY_SKELETON, "skeleton", false },
    };

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    std::size_t soldier_ability_ink = 0;
    std::size_t thief_ability_ink = 0;

    for (const Case& c : cases)
    {
        og::runtime::current_session->editguy_ = 0;
        save.team_size = 1;
        save.team_list[0].reset(new guy(c.family));
        save.team_list[0]->name = "TEAM_GUY";
        save.team_list[0]->level = 10;
        og::runtime::current_session->current_guy_ =
            std::make_unique<guy>(*save.team_list[0]);

        E_Screen->set_active_canvas(CanvasTarget::UI);
        SDL_FillSurfaceRect(E_Screen->render, nullptr, 0);

        KeyStateGuard ks;
        std::atomic<bool> done{false};
        prepare_detail_menu_mouse_click();
        InjectorArgs args{&ks, false, &done};
        SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu,
                                          "picker_detail_exit", &args);
        ASSERT_TRUE(th != nullptr) << "injector thread started for " << c.what;

        Sint32 r = create_detail_menu(save.team_list[0].get());
        done.store(true, std::memory_order_relaxed);
        int code = 0;
        SDL_WaitThread(th, &code);
        clear_events();

        // Exclude the two silent early-outs: the slot stayed seated and held
        // a guy for the whole loop, so MENU_REDRAW came from the BACK click.
        ASSERT_EQ(0, og::runtime::current_session->editguy_)
            << "the parameter seats slot 0 for " << c.what;
        ASSERT_TRUE(save.team_list[0] != nullptr)
            << "slot 0 stayed seated for " << c.what;
        ASSERT_EQ(2, (int)r) << "detail menu returns REDRAW on back for " << c.what;

        const std::size_t header_red =
            count_palette(kAbilityHeaderX1, kAbilityHeaderX2,
                          kAbilityHeaderY1, kAbilityHeaderY2, RED, RED);
        const std::size_t ability_ink =
            count_palette(kAbilityTextX1, kAbilityTextX2,
                          kAbilityTextY1, kAbilityTextY2,
                          DARK_BLUE, DARK_BLUE + 4);
        EXPECT_GT(header_red, 40u)
            << "the detail loop must paint its 'Character Special Abilities' "
               "header for " << c.what;
        if (c.has_ability_table)
        {
            EXPECT_GT(ability_ink, 40u)
                << "render_family_abilities must write the class line and "
                   "ability text for " << c.what;
        }
        else
        {
            EXPECT_EQ(0u, ability_ink)
                << "a family with no ability table paints the panel and no "
                   "ability text (" << c.what << ")";
        }

        if (c.family == FAMILY_SOLDIER)
            soldier_ability_ink = ability_ink;
        if (c.family == FAMILY_THIEF)
            thief_ability_ink = ability_ink;
    }

    EXPECT_NE(soldier_ability_ink, thief_ability_ink)
        << "the panel text is derived from the seated family, not a fixed "
           "block: a soldier and a thief do not read the same";

    save.team_list[0].reset();
    save.team_size = 0;
}


TEST(PickerDetailMenuDriven, picker_detail_menu_promote_mage_to_archmage_branch)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    PickerLobbyShutdownGuard lobby_guard; // promote lazily creates the client

    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_MAGE));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "TEAM_MAGE";
    og::runtime::current_session->myscreen_->save_data.team_list[0]->level = 6;

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(*og::runtime::current_session->myscreen_->save_data.team_list[0]);

    KeyStateGuard ks;
    std::atomic<bool> done{false};
    prepare_detail_menu_mouse_click();
    InjectorArgs args{&ks, true, &done};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_promote_mage", &args);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
    done.store(true, std::memory_order_relaxed);
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);
    clear_events();

    ASSERT_EQ(2, (int)r) << "mage promote should request redraw";
    ASSERT_EQ(FAMILY_ARCHMAGE, og::runtime::current_session->myscreen_->save_data.team_list[0]->family);

    // §3.8: PROMOTE is a roster mutation — it must run the shared mutation
    // tail, so the promotion round-trips from the ACTIVE company file with
    // no manual save (with SAVE retired, a bare lobby sync would lose a
    // promote-then-quit).
    SaveData reloaded;
    ASSERT_TRUE(reloaded.load(og::data::active_company_slot()))
        << "the promote autosave must have written the active company slot";
    ASSERT_EQ(1, (int)reloaded.team_size);
    ASSERT_TRUE(reloaded.team_list[0] != nullptr);
    EXPECT_EQ(FAMILY_ARCHMAGE, (int)reloaded.team_list[0]->family)
        << "the promotion must be on disk via the §3.8 autosave";
}


TEST(PickerDetailMenuDriven, picker_detail_menu_promote_orc_to_captain_branch)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    PickerLobbyShutdownGuard lobby_guard; // promote lazily creates the client

    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_ORC));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "TEAM_ORC";
    og::runtime::current_session->myscreen_->save_data.team_list[0]->level = 5;

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(*og::runtime::current_session->myscreen_->save_data.team_list[0]);

    KeyStateGuard ks;
    std::atomic<bool> done{false};
    prepare_detail_menu_mouse_click();
    InjectorArgs args{&ks, true, &done};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_promote_orc", &args);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
    done.store(true, std::memory_order_relaxed);
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);
    clear_events();

    ASSERT_EQ(2, (int)r) << "orc promote should request redraw";
    ASSERT_EQ(FAMILY_BIG_ORC, og::runtime::current_session->myscreen_->save_data.team_list[0]->family);
}


namespace
{
static bool reset_pointer_on_menu_thread()
{
    return run_on_main_thread([] { reset_mouse_click_tracking(); });
}

// The settle for the DETAIL menu, which is a legacy loop: it is not
// run_menu_screen-hosted, so no engine frame can ever complete inside it
// (wait_for_menu_frames would time out) and it never drains the main-thread
// task queue (run_on_main_thread would burn its whole ceiling). What the next
// click actually needs is the pointer EDGE: leftmouse() mints a click only on
// an unpressed->pressed transition (src/interface/ui/picker_input.cpp), so the
// press that opened this screen must have been sampled as RELEASED before a
// fresh press can be seen at all. That is a state, and this waits for the
// state — bounded, and normally satisfied on the first read, where the flat
// SDL_Delay(300) it replaces spent 300 ms proving nothing.
static bool wait_for_released_pointer_edge(int timeout_ms = 2000)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    while (SDL_GetTicks() < deadline)
    {
        const auto& hw = input_hardware_state();
        if (!hw.picker_was_left_down && hw.mouse.left == 0)
            return true;
        SDL_Delay(2);
    }
    fprintf(stderr,
            "  [test] the pointer never returned to a released edge within "
            "%d ms\n",
            timeout_ms);
    return false;
}

template <typename Predicate>
static bool wait_for_menu_thread_condition(Predicate&& predicate,
                                           int timeout_ms = 10000)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        bool matched = false;
        if (!run_on_main_thread(
                [&] {
                    matched = predicate();
                    if (matched)
                        reset_mouse_click_tracking();
                },
                timeout_ms - elapsed))
            return false;
        if (matched)
            return true;
        SDL_Delay(50);
        elapsed += 50;
    }
    return false;
}

struct TrainPromoteFlowState
{
    std::atomic<bool> finished{false};
    bool saw_train_menu = false;
    bool saw_promote = false;
    bool back_in_train_menu = false;
    bool pointer_edges_acknowledged = true;
    // Every settle in the flow was a CONDITION that came true (a completed
    // engine frame, a released pointer edge), not a clock that ran out.
    bool settles_observed = true;
};

// Drives the REAL nesting: train menu -> DETAILS -> promote -> back in the
// train menu -> ACCEPT -> BACK. This is the flow bug A9 broke: the promotion
// mutated the real team member, but the train menu's stale TrainSession
// working copy hid it on screen and ACCEPT statscopy()d the old family back.
static int train_menu_promote_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<TrainPromoteFlowState*>(data);

    if (!wait_for_interactable("details", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->saw_train_menu = true;
    // No settle before this: reset_pointer_on_menu_thread() IS one. It posts
    // to the menu thread's task queue, which run_menu_screen drains at the top
    // of a frame, so its return proves a frame ran — strictly more than a
    // flat delay proved.
    state->pointer_edges_acknowledged &= reset_pointer_on_menu_thread();
    interact("details");

    if (!wait_for_interactable("promote", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->saw_promote = true;
    state->settles_observed &= wait_for_released_pointer_edge();
    interact("promote");

    // The promotion returns MENU_REDRAW straight into the train menu;
    // "accept" only exists there.
    if (!wait_for_interactable("accept", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->back_in_train_menu = true;

    short strength_before = 0;
    state->pointer_edges_acknowledged &= run_on_main_thread([&] {
        reset_mouse_click_tracking();
        if (og::runtime::current_session->current_guy_ != nullptr)
            strength_before =
                og::runtime::current_session->current_guy_->strength;
    });
    interact("inc_str");
    state->pointer_edges_acknowledged &= wait_for_menu_thread_condition(
        [strength_before] {
            const auto& current =
                og::runtime::current_session->current_guy_;
            return current != nullptr && current->strength != strength_before;
        });

    state->pointer_edges_acknowledged &= reset_pointer_on_menu_thread();
    interact("accept"); // must NOT revert the promotion (bug A9)
    state->pointer_edges_acknowledged &= wait_for_menu_thread_condition([] {
        const auto& current = og::runtime::current_session->current_guy_;
        const auto& saved = og::runtime::current_session->myscreen_
                                ->save_data.team_list[0];
        return current != nullptr && saved != nullptr &&
            saved->strength == current->strength;
    });

    state->pointer_edges_acknowledged &= reset_pointer_on_menu_thread();
    interact("back");
    state->finished.store(true, std::memory_order_relaxed);
    return 0;
}
} // namespace

namespace
{
struct DetailPromoteFlowState
{
    std::atomic<bool> finished{false};
    bool saw_promote = false;
    bool clicked_promote = false;
    // See TrainPromoteFlowState::settles_observed.
    bool settles_observed = true;
};

// Gated on the affordance, never on a flat delay: on the unfixed tree the
// freed guy's family byte is garbage, the promote row never un-hides, and
// this fails BY NAME ("[interact] TIMEOUT waiting for 'promote'") instead of
// eating the group's whole CTest budget.
static int detail_menu_promote_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<DetailPromoteFlowState*>(data);

    if (wait_for_interactable("promote", 10000)) {
        state->saw_promote = true;
        // No run_on_main_thread() settle here: create_detail_menu is a legacy
        // loop, not a run_menu_screen spec, so it never pumps the injector
        // task queue and the post would burn its whole 15 s ceiling. The
        // released-pointer edge is the condition this click needs.
        state->settles_observed = wait_for_released_pointer_edge();
        state->clicked_promote = interact("promote");
    }
    if (!state->clicked_promote) {
        // Do not wedge the body in its menu loop when the promote never came.
        if (wait_for_interactable("back", 5000))
            (void)interact("back");
    }
    state->finished.store(true, std::memory_order_relaxed);
    return 0;
}
} // namespace

// The detail menu is opened on a BORROWED guy* into save.team_list, and the
// first statement of its loop is picker_lobby_poll() — which rebuilds every
// team_list slot at a NEW address. The member is not lost here (the lobby's
// cached roster already holds it), so this pins the dangling borrow alone:
// the menu must promote the member that is in the slot NOW.
TEST(PickerDetailMenuDriven, detail_menu_promotes_after_a_lobby_poll_rebuilds_the_roster)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    PickerLobbyShutdownGuard lobby_guard;

    auto& save = og::runtime::current_session->myscreen_->save_data;
    og::runtime::current_session->editguy_ = 0;
    save.team_size = 1;
    save.current_campaign = "gladiator";
    save.team_list[0].reset(new guy(FAMILY_MAGE));
    save.team_list[0]->name = "POLLED_MAGE";
    save.team_list[0]->level = 6;

    // Seed the standalone lobby client from a save that ALREADY holds the
    // member: the poll below then re-creates slot 0 at a new address rather
    // than dropping it.
    picker_lobby_shutdown();
    picker_lobby_sync_settings_from_save();
    picker_lobby_sync_roster_from_save();

    og::runtime::current_session->current_guy_ =
        std::make_unique<guy>(*save.team_list[0]);

    prepare_detail_menu_mouse_click();
    DetailPromoteFlowState state;
    SDL_Thread* th = SDL_CreateThread(
        detail_menu_promote_injector, "detail_promote_after_poll", &state);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    Sint32 r = create_detail_menu(save.team_list[0].get());

    int code = 0;
    SDL_WaitThread(th, &code);
    clear_events();

    ASSERT_TRUE(state.saw_promote)
        << "the promote affordance must survive the roster rebuild";
    ASSERT_TRUE(state.settles_observed)
        << "the injector's settle must be a condition that came true";
    ASSERT_TRUE(state.clicked_promote);
    ASSERT_EQ(2, (int)r) << "promote returns REDRAW";
    ASSERT_TRUE(save.team_list[0] != nullptr)
        << "the roster rebuild must not lose the member";
    ASSERT_EQ(FAMILY_ARCHMAGE, (int)save.team_list[0]->family)
        << "the promotion must land on the guy the slot holds now, not on "
           "the freed one the caller borrowed";
}


TEST(PickerDetailMenuDriven, train_menu_details_promote_survives_redraw_and_accept)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    PickerLobbyShutdownGuard lobby_guard;

    auto& save = og::runtime::current_session->myscreen_->save_data;
    const std::uint32_t saved_cash = save.m_totalcash[0];

    og::runtime::current_session->editguy_ = 0;
    save.team_size = 1;
    save.team_list[0].reset(new guy(FAMILY_MAGE));
    save.team_list[0]->name = "PROMO_MAGE";
    save.team_list[0]->level = 6;
    save.m_totalcash[0] = 999999;

    og::runtime::current_session->current_guy_ =
        std::make_unique<guy>(*save.team_list[0]);

    prepare_detail_menu_mouse_click();
    TrainPromoteFlowState state;
    SDL_Thread* th = SDL_CreateThread(
        train_menu_promote_injector, "train_promote_flow", &state);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    Sint32 r = create_train_menu(0);

    int code = 0;
    SDL_WaitThread(th, &code);
    clear_events();

    ASSERT_TRUE(state.finished.load(std::memory_order_relaxed));
    ASSERT_TRUE(state.saw_train_menu) << "train menu should have opened";
    ASSERT_TRUE(state.saw_promote) << "details menu should offer promote";
    ASSERT_TRUE(state.back_in_train_menu)
        << "promotion should return to the train menu";
    ASSERT_TRUE(state.pointer_edges_acknowledged);
    ASSERT_TRUE(state.settles_observed)
        << "every settle in the flow must be a condition that came true";
    ASSERT_EQ(2, (int)r) << "train menu BACK should return REDRAW";

    // The real team member is an Archmage and ACCEPT did not revert it.
    ASSERT_EQ(FAMILY_ARCHMAGE, (int)save.team_list[0]->family);
    // The train screen's displayed guy resynced to the promotion (before
    // the fix it still showed the stale Mage working copy).
    ASSERT_TRUE(og::runtime::current_session->current_guy_ != nullptr);
    ASSERT_EQ(FAMILY_ARCHMAGE,
              (int)og::runtime::current_session->current_guy_->family);

    save.m_totalcash[0] = saved_cash;
}

namespace
{
struct TrainPromoteScriptState
{
    std::atomic<bool> finished{false};
    bool saw_train_menu = false;
    bool saw_promote = false;
    bool back_in_train_menu = false;
    bool pointer_edges_acknowledged = true;
    // See TrainPromoteFlowState::settles_observed.
    bool settles_observed = true;
    // Optional extra steps performed back in the train menu after the
    // promotion, before BACK.
    bool do_stat_edit = false;
    bool do_accept = false;
};

// Drives: train menu -> DETAILS -> promote -> back in the train menu ->
// [inc_str] -> [ACCEPT] -> BACK. The waits give the train menu loop plenty
// of picker_lobby_poll() iterations after the promotion — the issue #133
// clobber window (each poll rewrites save.team_list from the lobby's cached
// roster, which pre-fix still held the un-promoted mage).
static int train_menu_promote_script_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<TrainPromoteScriptState*>(data);

    if (!wait_for_interactable("details", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->saw_train_menu = true;
    // No settle before this: reset_pointer_on_menu_thread() IS one. It posts
    // to the menu thread's task queue, which run_menu_screen drains at the top
    // of a frame, so its return proves a frame ran — strictly more than a
    // flat delay proved.
    state->pointer_edges_acknowledged &= reset_pointer_on_menu_thread();
    interact("details");

    if (!wait_for_interactable("promote", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->saw_promote = true;
    // The legacy detail loop runs synchronously inside the train menu's
    // button callback, so it cannot drain the menu-screen task queue. Wait on
    // the pointer edge it leaves behind instead.
    state->settles_observed &= wait_for_released_pointer_edge();
    interact("promote");

    // "accept" only exists in the train menu, so this waits out the return
    // from the details submenu.
    if (!wait_for_interactable("accept", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    // The pre-fix revert window is a number of picker_lobby_poll()s, and the
    // train screen polls the lobby once per engine frame
    // (train_menu_screen_spec, .polls_lobby = true). Five COMPLETED frames is
    // therefore five real polls; the flat 500 ms it replaces was a guess that
    // any ran at all.
    state->back_in_train_menu = true;
    state->settles_observed &= wait_for_menu_frames(5);

    if (state->do_stat_edit) {
        short strength_before = 0;
        state->pointer_edges_acknowledged &= run_on_main_thread([&] {
            reset_mouse_click_tracking();
            if (og::runtime::current_session->current_guy_ != nullptr)
                strength_before =
                    og::runtime::current_session->current_guy_->strength;
        });
        interact("inc_str");
        state->pointer_edges_acknowledged &= wait_for_menu_thread_condition(
            [strength_before] {
                const auto& current =
                    og::runtime::current_session->current_guy_;
                return current != nullptr &&
                    current->strength != strength_before;
            });
    }
    if (state->do_accept) {
        state->pointer_edges_acknowledged &= reset_pointer_on_menu_thread();
        interact("accept");
        state->pointer_edges_acknowledged &= wait_for_menu_thread_condition([] {
            const auto& current =
                og::runtime::current_session->current_guy_;
            const auto& saved = og::runtime::current_session->myscreen_
                                    ->save_data.team_list[0];
            return current != nullptr && saved != nullptr &&
                saved->strength == current->strength;
        });
    }
    state->pointer_edges_acknowledged &= reset_pointer_on_menu_thread();
    interact("back");
    state->finished.store(true, std::memory_order_relaxed);
    return 0;
}

// Exits the train menu as soon as it is up (used for the re-enter check).
static int train_menu_exit_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<TrainPromoteScriptState*>(data);
    if (wait_for_interactable("details", 10000)) {
        state->saw_train_menu = true;
        state->pointer_edges_acknowledged &= reset_pointer_on_menu_thread();
        interact("back");
    }
    state->finished.store(true, std::memory_order_relaxed);
    return 0;
}

// Shared setup: a level-6 mage in slot 0 plus the lobby client initialized
// from that save, so its cached roster holds the UN-promoted mage — that is
// the stale state issue #133's polls copied back over the promotion.
static guy setup_promotable_mage_with_lobby()
{
    auto& save = og::runtime::current_session->myscreen_->save_data;
    og::runtime::current_session->editguy_ = 0;
    save.numplayers = 1;
    save.team_size = 1;
    save.team_list[0].reset(new guy(FAMILY_MAGE));
    save.team_list[0]->name = "PROMO_MAGE";
    save.team_list[0]->upgrade_to_level(6);
    save.m_totalcash[0] = 999999;

    og::runtime::current_session->current_guy_ =
        std::make_unique<guy>(*save.team_list[0]);

    // What create_detail_menu's promote produces: upgrade_to_level(new_level)
    // runs BEFORE the family flip.
    guy expected(*save.team_list[0]);
    expected.upgrade_to_level(1);
    expected.family = FAMILY_ARCHMAGE;

    picker_lobby_initialize_from_save();
    return expected;
}
} // namespace

// Issue #133: the promotion ALONE (no accept, no stat edit) must persist when
// the player BACKs out of the train menu and must still be there on re-entry.
TEST(PickerDetailMenuDriven, train_menu_promote_alone_persists_on_exit_and_reenter)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    PickerLobbyShutdownGuard lobby_guard;

    auto& save = og::runtime::current_session->myscreen_->save_data;
    const std::uint32_t saved_cash = save.m_totalcash[0];
    const guy expected = setup_promotable_mage_with_lobby();

    prepare_detail_menu_mouse_click();
    TrainPromoteScriptState state;
    SDL_Thread* th = SDL_CreateThread(
        train_menu_promote_script_injector, "train_promote_exit", &state);
    ASSERT_TRUE(th != nullptr) << "injector thread started";
    create_train_menu(0);
    SDL_WaitThread(th, nullptr);
    clear_events();

    ASSERT_TRUE(state.finished.load(std::memory_order_relaxed));
    ASSERT_TRUE(state.saw_promote) << "details menu should offer promote";
    ASSERT_TRUE(state.back_in_train_menu);
    ASSERT_TRUE(state.pointer_edges_acknowledged);
    ASSERT_TRUE(state.settles_observed)
        << "every settle in the flow must be a condition that came true";

    // Family AND the promotion's stats survived the exit.
    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_EQ(FAMILY_ARCHMAGE, (int)save.team_list[0]->family)
        << "promotion must persist without a stat edit (issue #133)";
    ASSERT_EQ((int)expected.level, (int)save.team_list[0]->level);
    ASSERT_EQ((int)expected.strength, (int)save.team_list[0]->strength);
    ASSERT_EQ((int)expected.intelligence, (int)save.team_list[0]->intelligence);

    // Re-entering the train menu shows the archmage, not a reverted mage.
    prepare_detail_menu_mouse_click();
    TrainPromoteScriptState reenter_state;
    SDL_Thread* th2 = SDL_CreateThread(
        train_menu_exit_injector, "train_reenter", &reenter_state);
    ASSERT_TRUE(th2 != nullptr);
    create_train_menu(0);
    SDL_WaitThread(th2, nullptr);
    clear_events();

    ASSERT_TRUE(reenter_state.saw_train_menu);
    ASSERT_TRUE(reenter_state.pointer_edges_acknowledged);
    ASSERT_TRUE(reenter_state.settles_observed)
        << "every settle in the flow must be a condition that came true";
    ASSERT_TRUE(og::runtime::current_session->current_guy_ != nullptr);
    ASSERT_EQ(FAMILY_ARCHMAGE,
              (int)og::runtime::current_session->current_guy_->family);
    ASSERT_EQ((int)expected.strength,
              (int)og::runtime::current_session->current_guy_->strength);

    save.m_totalcash[0] = saved_cash;
}

// Issue #133: a stat edit AFTER the promotion must compose on the fresh
// archmage stats (pre-fix it clamped the stale mage stats back over them).
TEST(PickerDetailMenuDriven, train_menu_promote_then_stat_edit_keeps_both)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    PickerLobbyShutdownGuard lobby_guard;

    auto& save = og::runtime::current_session->myscreen_->save_data;
    const std::uint32_t saved_cash = save.m_totalcash[0];
    const guy expected = setup_promotable_mage_with_lobby();

    prepare_detail_menu_mouse_click();
    TrainPromoteScriptState state;
    state.do_stat_edit = true;
    state.do_accept = true;
    SDL_Thread* th = SDL_CreateThread(
        train_menu_promote_script_injector, "train_promote_edit", &state);
    ASSERT_TRUE(th != nullptr) << "injector thread started";
    create_train_menu(0);
    SDL_WaitThread(th, nullptr);
    clear_events();

    ASSERT_TRUE(state.finished.load(std::memory_order_relaxed));
    ASSERT_TRUE(state.saw_promote);
    ASSERT_TRUE(state.back_in_train_menu);
    ASSERT_TRUE(state.pointer_edges_acknowledged);
    ASSERT_TRUE(state.settles_observed)
        << "every settle in the flow must be a condition that came true";

    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_EQ(FAMILY_ARCHMAGE, (int)save.team_list[0]->family)
        << "promotion must survive the stat edit";
    ASSERT_EQ((int)expected.level, (int)save.team_list[0]->level);
    ASSERT_EQ((int)expected.strength + 1, (int)save.team_list[0]->strength)
        << "the +1 STR must compose on the archmage stats, not the old mage's";
    ASSERT_EQ((int)expected.intelligence, (int)save.team_list[0]->intelligence);

    save.m_totalcash[0] = saved_cash;
}

// Issue #133 counterpart: BACK without ACCEPT still cancels a pending stat
// edit — but the promotion itself (an instant, irreversible action per the
// game's own "CANNOT be undone" dialog) stays.
TEST(PickerDetailMenuDriven, train_menu_promote_then_cancel_discards_pending_edit)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);
    PickerLobbyShutdownGuard lobby_guard;

    auto& save = og::runtime::current_session->myscreen_->save_data;
    const std::uint32_t saved_cash = save.m_totalcash[0];
    const guy expected = setup_promotable_mage_with_lobby();

    prepare_detail_menu_mouse_click();
    TrainPromoteScriptState state;
    state.do_stat_edit = true; // +1 STR, but never accepted
    SDL_Thread* th = SDL_CreateThread(
        train_menu_promote_script_injector, "train_promote_cancel", &state);
    ASSERT_TRUE(th != nullptr) << "injector thread started";
    create_train_menu(0);
    SDL_WaitThread(th, nullptr);
    clear_events();

    ASSERT_TRUE(state.finished.load(std::memory_order_relaxed));
    ASSERT_TRUE(state.saw_promote);
    ASSERT_TRUE(state.back_in_train_menu);
    ASSERT_TRUE(state.pointer_edges_acknowledged);
    ASSERT_TRUE(state.settles_observed)
        << "every settle in the flow must be a condition that came true";

    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_EQ(FAMILY_ARCHMAGE, (int)save.team_list[0]->family)
        << "the irreversible promotion persists through cancel";
    ASSERT_EQ((int)expected.level, (int)save.team_list[0]->level);
    ASSERT_EQ((int)expected.strength, (int)save.team_list[0]->strength)
        << "the un-accepted stat edit must be discarded on BACK";
    ASSERT_EQ(999999u, save.m_totalcash[0])
        << "no gold may be spent on a cancelled edit";

    save.m_totalcash[0] = saved_cash;
}

// family_name_copy is picker.cpp's wrapper over og::ui::family_short_name,
// which never returns nullptr -- so a null check pins nothing. Pin the label
// each of its three branches produces.
TEST(PickerDetailMenuDriven, picker_family_name_copy_labels_each_short_name_branch)
{
    // packs/core/families/living-17-archmage.lua: short_name = og.NIL, so the
    // display name is the label.
    EXPECT_STREQ("ARCHMAGE", family_name_copy(FAMILY_ARCHMAGE))
        << "no pack short_name -> the descriptor's display name is the label";
    // living-15-orc_captain.lua: name "ORC CAPTAIN", short_name "ORC CAP."
    EXPECT_STREQ("ORC CAP.", family_name_copy(FAMILY_BIG_ORC))
        << "a pack short_name wins over the display name";
    EXPECT_STREQ("BEAST", family_name_copy(static_cast<short>(99)))
        << "no descriptor at all -> BEAST";
}
