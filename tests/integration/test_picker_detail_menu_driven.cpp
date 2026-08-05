#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
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
        if (og::runtime::current_session->allbuttons_[0] != nullptr) // "back" is index 0 in details_buttons
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
} // namespace

TEST(PickerDetailMenuDriven, picker_detail_menu_back_exercises_many_family_descriptions)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);

    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_SOLDIER));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "TEAM_SOLDIER";
    og::runtime::current_session->myscreen_->save_data.team_list[0]->level = 10;

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(*og::runtime::current_session->myscreen_->save_data.team_list[0]);

    KeyStateGuard ks;
    std::atomic<bool> done{false};
    prepare_detail_menu_mouse_click();
    InjectorArgs args{&ks, false, &done};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_exit", &args);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
    done.store(true, std::memory_order_relaxed);
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);
    clear_events();

    // create_detail_menu exits back to the edit menu and always returns REDRAW.
    ASSERT_EQ(2, (int)r) << "detail menu should return REDRAW on back";
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
struct TrainPromoteFlowState
{
    std::atomic<bool> finished{false};
    bool saw_train_menu = false;
    bool saw_promote = false;
    bool back_in_train_menu = false;
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
    SDL_Delay(300);
    interact("details");

    if (!wait_for_interactable("promote", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->saw_promote = true;
    SDL_Delay(300);
    interact("promote");

    // The promotion returns MENU_REDRAW straight into the train menu;
    // "accept" only exists there.
    if (!wait_for_interactable("accept", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->back_in_train_menu = true;
    SDL_Delay(300);
    interact("accept"); // must NOT revert the promotion (bug A9)
    SDL_Delay(300);
    interact("back");
    state->finished.store(true, std::memory_order_relaxed);
    return 0;
}
} // namespace

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
    SDL_Delay(300);
    interact("details");

    if (!wait_for_interactable("promote", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->saw_promote = true;
    SDL_Delay(300);
    interact("promote");

    // "accept" only exists in the train menu, so this waits out the return
    // from the details submenu.
    if (!wait_for_interactable("accept", 10000)) {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->back_in_train_menu = true;
    SDL_Delay(500); // several poll iterations — the pre-fix revert window

    if (state->do_stat_edit) {
        interact("inc_str");
        SDL_Delay(300);
    }
    if (state->do_accept) {
        interact("accept");
        SDL_Delay(300);
    }
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
        SDL_Delay(300);
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

TEST(PickerDetailMenuDriven, picker_family_name_copy_includes_archmage)
{
    const char* a = family_name_copy(FAMILY_ARCHMAGE);
    ASSERT_TRUE(a != nullptr) << "family_name_copy should return a string";
}
