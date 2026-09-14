#include <openglad/interface/button.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
#include <openglad/interface/screen.h>
#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <openglad/interface/native_input.h>
#include "test_input_helpers.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <array>
#include <memory>
#include <set>
#include <openglad/resources/io_common.h>

// myscreen is now a macro defined in base.h (via game_session.h)
#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


Sint32 name_guy(Sint32 arg);
Sint32 do_pick_campaign(Sint32 arg1);
Sint32 do_set_scen_level(Sint32 arg1);
Sint32 change_teamnum(Sint32 arg);
Sint32 change_hire_teamnum(Sint32 arg);
void picker_request_start_game();
void view_team(short left, short top, short right, short bottom);
int picker_team_build_testing_exercise_internal_paths(int* checks_run = nullptr);
extern bool g_start_game_requested;
// The exact number of check() calls in tests/coverage_internal/
// picker_team_build_internal.inc. Bump it deliberately when you add one.
inline constexpr int kPickerTeamBuildInternalCheckCount = 282;

namespace
{
struct PickerStateGuard
{
    std::unique_ptr<guy> saved_current;
    guy* saved_old = nullptr;
	Sint32 saved_editguy = 0;
	char saved_end = 0;
	unsigned char saved_team_size = 0;
	short saved_scen_num = 0;

    PickerStateGuard()
    {
        saved_current = std::move(og::runtime::current_session->current_guy_);
        saved_old = pks().old_guy;
        saved_editguy = og::runtime::current_session->editguy_;
        saved_end = og::runtime::current_session->myscreen_->world().end;
        saved_team_size = og::runtime::current_session->myscreen_->save_data.team_size;
        saved_scen_num = og::runtime::current_session->myscreen_->save_data.scen_num;
    }

    ~PickerStateGuard()
    {
        og::runtime::current_session->current_guy_ = std::move(saved_current);
        pks().old_guy = saved_old;
        og::runtime::current_session->editguy_ = saved_editguy;
        og::runtime::current_session->myscreen_->world().end = saved_end;
        og::runtime::current_session->myscreen_->save_data.team_size = saved_team_size;
        og::runtime::current_session->myscreen_->save_data.scen_num = saved_scen_num;
    }
};

struct TeamSlotGuard
{
	int slot;
	guy* saved;
	TeamSlotGuard(int slot_) : slot(slot_), saved(og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(slot_)].release()) {}
	~TeamSlotGuard() { og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(slot)].reset(saved); }
};

struct SaveRosterGuard
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team;
    unsigned char team_size = save.team_size;
    unsigned char numplayers = save.numplayers;
    short my_team = save.my_team;

    SaveRosterGuard()
    {
        for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
            team[static_cast<std::size_t>(slot)] = std::move(save.team_list[static_cast<std::size_t>(slot)]);
    }

    ~SaveRosterGuard()
    {
        picker_lobby_shutdown();
        for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
            save.team_list[static_cast<std::size_t>(slot)] = std::move(team[static_cast<std::size_t>(slot)]);
        save.team_size = team_size;
        save.numplayers = numplayers;
        save.my_team = my_team;
    }
};

struct ButtonSlotGuard
{
	int slot;
	vbutton* saved;
	ButtonSlotGuard(int slot_) : slot(slot_), saved(og::runtime::current_session->allbuttons_[static_cast<std::size_t>(slot_)]) {}
	~ButtonSlotGuard() { og::runtime::current_session->allbuttons_[static_cast<std::size_t>(slot)] = saved; }
};

struct OwnedButtonReplacementGuard
{
    int slot;
    vbutton* saved;

    OwnedButtonReplacementGuard(int slot_, const char* name)
        : slot(slot_), saved(og::runtime::current_session->allbuttons_[static_cast<std::size_t>(slot_)])
    {
        og::runtime::current_session->allbuttons_[static_cast<std::size_t>(slot)] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, name, KEYSTATE_UNKNOWN);
    }

    ~OwnedButtonReplacementGuard()
    {
        delete og::runtime::current_session->allbuttons_[static_cast<std::size_t>(slot)];
        og::runtime::current_session->allbuttons_[static_cast<std::size_t>(slot)] = saved;
    }
};

// One listed roster row inside the view_team box: view_team starts at
// top+3+6 = 19 for left/top = 10/10 and advances 6 px per listed member.
std::set<int> view_team_row_colors(screen& scr, int row_top)
{
    std::set<int> colors;
    for (int y = row_top; y < row_top + 6; ++y)
    {
        for (int x = 15; x < 300; ++x)
        {
            int index = 0;
            scr.get_pixel(x, y, &index);
            colors.insert(index);
        }
    }
    return colors;
}

struct NameGuyInjectorArgs
{
    const char* text = nullptr;
    std::atomic<bool> saw_editor{false};
};

// The rename modal is NOT engine-hosted (input_string_ex blocks in
// get_input_events(WAIT)), so wait_for_menu_frames can never be satisfied
// inside it. og::input_native::text_input_is_active() is the only observable
// that the editor reached its loop and cleared the input state -- anything
// injected before that is discarded. Bounded, never a flat delay.
int name_guy_injector(void* data)
{
    og::runtime::ensure_thread_session();
    NameGuyInjectorArgs* a = static_cast<NameGuyInjectorArgs*>(data);
    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (SDL_GetTicks() < deadline)
    {
        if (og::input_native::text_input_is_active())
        {
            a->saw_editor.store(true, std::memory_order_relaxed);
            break;
        }
        SDL_Delay(5);
    }
    inject_text_input(a->text);
    SDL_Delay(30);
    inject_key_press(SDLK_RETURN, 10);
    return 0;
}
} // namespace

TEST(PickerUncovered, picker_name_guy_writes_accepted_text_and_only_a_roster_rename_saves)
{
    PickerStateGuard guard;
    // §3.8: a rename ACCEPT (has_value) runs the base-camp mutation tail,
    // which lazily creates the local lobby client and seeds its cached roster
    // with THIS test's guys. Any later picker loop's picker_lobby_poll() would
    // rewrite save.team_list from that stale cache (the documented promote-test
    // wedge) — shut the client down on every exit path.
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;

    std::unique_ptr<guy> original_current = std::move(og::runtime::current_session->current_guy_);
    og::runtime::current_session->current_guy_ = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->current_guy_->name = "CURSORIG";

    // arg == 0 names the hire screen's not-yet-hired recruit: nothing on the
    // roster changed, so the §3.8 tail must NOT run. Start from a clean slate
    // so the lazily created standalone client is the evidence.
    picker_lobby_shutdown();
    ASSERT_FALSE(picker_lobby_testing_standalone_client_alive())
        << "setup: no standalone lobby client before the recruit rename";

    NameGuyInjectorArgs recruit_args{"RENAMED0", {}};
    SDL_Thread* rename_current_thread = SDL_CreateThread(name_guy_injector, "picker_name_current", &recruit_args);
    ASSERT_TRUE(rename_current_thread != nullptr) << "rename-current injector should be created";
    ASSERT_EQ(2, (int)name_guy(0)) << "name_guy(0) should return REDRAW";
    int thread_result = 0;
    SDL_WaitThread(rename_current_thread, &thread_result);
    ASSERT_TRUE(recruit_args.saw_editor.load(std::memory_order_relaxed))
        << "the recruit rename must actually open the text editor";
    EXPECT_EQ("RENAMED0", og::runtime::current_session->current_guy_->name)
        << "an accepted rename writes the typed text onto the guy on screen";
    EXPECT_FALSE(picker_lobby_testing_standalone_client_alive())
        << "renaming the hire screen's recruit is not a roster mutation: no "
           "lobby sync, no autosave";

    TeamSlotGuard slot_guard(0);
    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_MAGE));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "TEAMORIG";

    NameGuyInjectorArgs roster_args{"RENAMED1", {}};
    SDL_Thread* rename_team_thread = SDL_CreateThread(name_guy_injector, "picker_name_team", &roster_args);
    ASSERT_TRUE(rename_team_thread != nullptr) << "rename-team injector should be created";
    ASSERT_EQ(2, (int)name_guy(1)) << "name_guy(1) should return REDRAW";
    SDL_WaitThread(rename_team_thread, &thread_result);
    ASSERT_TRUE(roster_args.saw_editor.load(std::memory_order_relaxed))
        << "the roster rename must actually open the text editor";
    EXPECT_EQ("RENAMED1", og::runtime::current_session->myscreen_->save_data.team_list[0]->name)
        << "an accepted rename writes the typed text onto the roster member";
    EXPECT_TRUE(picker_lobby_testing_standalone_client_alive())
        << "§3.8: a ROSTER rename runs picker_base_camp_after_roster_mutation, "
           "whose lobby roster push creates the local client";

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset();
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(nullptr);
    og::runtime::current_session->current_guy_ = std::move(original_current);
}


TEST(PickerUncovered, picker_train_accept_debits_the_purse_and_writes_the_roster)
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);

    // Empty session path: no team members to train.
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(nullptr);
    og::ui::TrainSession empty_session(og::runtime::current_session->myscreen_->save_data);
    ASSERT_TRUE(empty_session.empty()) << "session should be empty with no team";

    // Successful accept path.
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_SOLDIER));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->teamnum = 0;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.m_totalcash[0] = 100000;

    og::ui::TrainSession session(og::runtime::current_session->myscreen_->save_data);
    ASSERT_TRUE(!session.empty()) << "session should not be empty";

    // The soldier sits at its family base, so +1 STR is priced from delta 0
    // to delta 1: pow(1, kStatCostExponent) * stat_costs[Strength] = 6 gold.
    const FamilyDescriptor* const soldier = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier) << "SOLDIER must have a descriptor";
    const int str_before = og::runtime::current_session->myscreen_->save_data.team_list[0]->strength;
    const std::uint32_t gold_before = og::runtime::current_session->myscreen_->save_data.m_totalcash[0];

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);
    const std::uint32_t cost = session.current_cost();
    ASSERT_EQ(static_cast<std::uint32_t>(soldier->stat_costs[StatAxis::Strength]), cost)
        << "one point above the family base costs exactly one stat_costs unit";
    ASSERT_TRUE(session.accept()) << "accept should succeed with enough gold";

    // accept() debits the wallet and statscopy's the working guy back onto the
    // roster member; an accept that does neither used to pass this test.
    EXPECT_EQ(str_before + 1,
              (int)og::runtime::current_session->myscreen_->save_data.team_list[0]->strength)
        << "accept writes the trained stat onto the roster member";
    EXPECT_EQ(gold_before - cost,
              og::runtime::current_session->myscreen_->save_data.m_totalcash[0])
        << "accept charges the training price to the member's team purse";

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(nullptr);
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
}


TEST(PickerUncovered, picker_campaign_and_level_wrappers_cancel_fast)
{
    PickerStateGuard guard;

    og::runtime::current_session->myscreen_->world().end = 1;
    int scen_before = og::runtime::current_session->myscreen_->save_data.scen_num;

    ASSERT_EQ(2, (int)do_pick_campaign(0)) << "do_pick_campaign should return REDRAW";
    ASSERT_EQ(2, (int)do_set_scen_level(0)) << "do_set_scen_level should return REDRAW";
    ASSERT_EQ(scen_before, (int)og::runtime::current_session->myscreen_->save_data.scen_num) << "cancel paths should preserve selected scenario";
}


TEST(PickerUncovered, picker_team_wraps_on_negative_step)
{
    PickerStateGuard guard;
    OwnedButtonReplacementGuard button2_guard(2, "b2");
    OwnedButtonReplacementGuard button18_guard(kTrainMenuChangeTeamIndex, "b18");
    const short saved_team_num = og::runtime::current_session->current_team_num_;

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->current_guy_->teamnum = static_cast<short>(0);
    og::runtime::current_session->current_team_num_ = static_cast<short>(0);

    ASSERT_EQ(4, (int)change_teamnum(-1)) << "change_teamnum should return OK";
    ASSERT_EQ(3, (int)og::runtime::current_session->current_guy_->teamnum) << "change_teamnum should wrap 0 -> 3 for arg -1";
    ASSERT_STREQ("Team 4", og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]->label.c_str()) << "team label should wrap to Team 4";

    og::runtime::current_session->current_guy_->teamnum = static_cast<short>(0);
    og::runtime::current_session->current_team_num_ = static_cast<short>(0);
    ASSERT_EQ(4, (int)change_hire_teamnum(-1)) << "change_hire_teamnum should return OK";
    ASSERT_EQ(3, (int)og::runtime::current_session->current_team_num_) << "change_hire_teamnum should wrap 0 -> 3 for arg -1";
    ASSERT_EQ(3, (int)og::runtime::current_session->current_guy_->teamnum) << "change_hire_teamnum should mirror to current_guy";
    ASSERT_STREQ("Hiring for Team 4", og::runtime::current_session->allbuttons_[2]->label.c_str()) << "hire label should wrap to Team 4";

    og::runtime::current_session->current_team_num_ = saved_team_num;
}

// The exerciser reports "no check failed" as 0 -- which is also what it
// returns after running ZERO checks. Pin the count too, so an early return or
// a guard that skips part of the ladder cannot silently turn ~140 product
// assertions into none.
TEST(PickerUncovered, picker_team_build_internal_paths)
{
    int checks = -1;
    ASSERT_EQ(0, picker_team_build_testing_exercise_internal_paths(&checks))
        << "every internal check must pass (the value is the 1-based index of "
           "the first failure, negated)";
    ASSERT_EQ(kPickerTeamBuildInternalCheckCount, checks)
        << "the exerciser must run its whole check ladder";
}

TEST(PickerUncovered, start_game_request_and_team_view_use_lobby_state)
{
    SaveRosterGuard guard;
    SaveData& save = guard.save;
    for (auto& member : save.team_list)
        member.reset();

    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Red";
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->deployed = true;
    save.team_list[3] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[3]->name = "Blue";
    save.team_list[3]->teamnum = 2;
    save.team_list[3]->deployed = true;
    save.team_size = 2;
    save.numplayers = 1;
    save.my_team = 0;

    picker_lobby_shutdown();
    picker_lobby_initialize_from_save();

    trace_clear();
    g_start_game_requested = false;
    picker_request_start_game();
    EXPECT_TRUE(g_start_game_requested);
    EXPECT_TRUE(picker_lobby_has_game_start_config());

    screen* const scr = og::runtime::current_session->myscreen_;
    scr->redrawme = 0;
    view_team(10, 10, 310, 100);
    EXPECT_EQ(1, scr->redrawme);

    // view_team lists every non-null slot, one 6 px row at a time, writing the
    // name in the family-derived namecolor ((family+1)<<4)&255 and the stat
    // line in BLACK. Two members are seated, so rows 0 and 1 carry ink and row
    // 2 is bare box face. (The small font shades a glyph over its colour's
    // five-entry ramp.)
    const std::set<int> row0 = view_team_row_colors(*scr, 19);
    const std::set<int> row1 = view_team_row_colors(*scr, 25);
    const std::set<int> row2 = view_team_row_colors(*scr, 31);

    const int soldier_ramp = ((FAMILY_SOLDIER + 1) << 4) & 255;   // 16
    const int mage_ramp = ((FAMILY_MAGE + 1) << 4) & 255;         // 64
    auto shades_in = [](const std::set<int>& row, int base) {
        std::size_t n = 0;
        for (int shade = 0; shade < 5; ++shade)
            n += row.count(base + shade);
        return n;
    };

    // Each listed member is named in its OWN family ramp and its stat line is
    // written in BLACK; the rows do not share a colour, so a loop that drew
    // one member twice, drew nothing, or used a fixed colour fails here.
    EXPECT_EQ(3u, shades_in(row0, soldier_ramp))
        << "slot 0 (a SOLDIER) is named in the family-derived ramp 16..20";
    EXPECT_EQ(0u, shades_in(row0, mage_ramp))
        << "and not in the mage's";
    EXPECT_EQ(5u, shades_in(row1, mage_ramp))
        << "slot 3 (a MAGE) is named in ITS ramp 64..68, one row below";
    EXPECT_EQ(0u, shades_in(row1, soldier_ramp))
        << "and not in the soldier's";
    EXPECT_EQ(1u, row0.count(static_cast<int>(BLACK)))
        << "the STR/DEX/CON/INT/ARM line is written in BLACK";
    EXPECT_EQ(1u, row1.count(static_cast<int>(BLACK)))
        << "for every listed member";

    // Exact ink, so a dropped column or an extra one is visible too.
    const std::set<int> expected_row0{0, 3, 13, 17, 18, 19, 160, 161, 162, 163, 164};
    const std::set<int> expected_row1{13, 64, 65, 66, 67, 68, 160, 161, 162, 163, 164};
    EXPECT_EQ(expected_row0, row0)
        << "row 0 = box face 13 + the soldier name ramp + the BLACK stat and "
           "level glyphs";
    EXPECT_EQ(expected_row1, row1)
        << "row 1 = box face 13 + the mage name ramp + the BLACK stat and "
           "level glyphs";
    EXPECT_EQ(std::set<int>{13}, row2)
        << "only the two seated members are listed: the third row is bare box "
           "face, with no phantom row for an empty slot";

    g_start_game_requested = false;
}


// --- WP5: roster-verb refusals and the empty-roster banner ---------------

namespace {

// The colours painted into the "*** YOU HAVE NO TEAM! ***" band inside the
// view_team box (the banner sits at left+80, y=60, 25 glyphs wide).
std::set<int> view_team_banner_colors(screen& scr)
{
    std::set<int> colors;
    for (int y = 58; y < 68; ++y)
    {
        for (int x = 88; x < 244; ++x)
        {
            int index = 0;
            scr.get_pixel(x, y, &index);
            colors.insert(index);
        }
    }
    return colors;
}

struct HireSessionSlotGuard
{
    og::ui::HireSession* saved = pks().hire_session;
    explicit HireSessionSlotGuard(og::ui::HireSession* session)
    {
        pks().hire_session = session;
    }
    ~HireSessionSlotGuard() { pks().hire_session = saved; }
};

} // namespace

// The roster box tells a player with nobody hired that they have nobody
// hired; with a roster listed that line must not appear at all.
TEST(PickerUncovered, view_team_banners_only_an_empty_roster)
{
    SaveRosterGuard guard;
    SaveData& save = guard.save;
    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Red";
    save.team_size = 1;

    screen* const scr = og::runtime::current_session->myscreen_;
    view_team(10, 10, 310, 100);
    const std::set<int> listed = view_team_banner_colors(*scr);
    ASSERT_EQ(1u, listed.size())
        << "the banner band is bare box face while a roster is listed";
    const int box_face = *listed.begin();

    save.team_list[0].reset();
    save.team_size = 0;
    view_team(10, 10, 310, 100);
    // The small font shades a glyph across its colour's five-entry palette
    // ramp, so the empty band holds the box face plus ORANGE_START..+4 and
    // nothing else.
    std::set<int> expected{box_face};
    for (int shade = 0; shade < 5; ++shade)
        expected.insert(static_cast<int>(ORANGE_START) + shade);
    EXPECT_EQ(expected, view_team_banner_colors(*scr))
        << "an empty roster paints the orange banner over the box face";
}

// TEAM on the TRAIN screen edits the guy the screen is showing. With no guy
// on screen the click must be inert — not a write through a null.
TEST(PickerUncovered, change_teamnum_without_a_current_guy_is_inert)
{
    PickerStateGuard guard;
    OwnedButtonReplacementGuard team_button(kTrainMenuChangeTeamIndex, "team");
    const short saved_team_num = og::runtime::current_session->current_team_num_;

    og::runtime::current_session->current_guy_.reset();
    og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]->label =
        "UNTOUCHED";
    EXPECT_EQ(0, static_cast<int>(change_teamnum(1)))
        << "no guy, no menu result";
    EXPECT_STREQ(
        "UNTOUCHED",
        og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]
            ->label.c_str())
        << "the TEAM face is not relabelled for a guy that is not there";

    // Paired control: with a guy on screen the same click cycles and
    // relabels.
    og::runtime::current_session->current_guy_ =
        std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->current_guy_->teamnum = 0;
    og::runtime::current_session->current_team_num_ = 0;
    EXPECT_EQ(4, static_cast<int>(change_teamnum(1)));
    EXPECT_EQ(1, static_cast<int>(
                     og::runtime::current_session->current_guy_->teamnum));
    EXPECT_STREQ(
        "Team 2",
        og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]
            ->label.c_str());

    og::runtime::current_session->current_team_num_ = saved_team_num;
}

// HIRE refuses for two different reasons and the caller has to tell them
// apart: a full roster closes the screen (-1), an unaffordable recruit keeps
// it open (MENU_OK). Neither may touch the wallet or the roster.
TEST(PickerUncovered, add_guy_distinguishes_a_full_team_from_an_empty_purse)
{
    SaveRosterGuard guard;
    PickerStateGuard state_guard;
    SaveData& save = guard.save;
    for (auto& member : save.team_list)
        member.reset();

    // Broke, with room to hire: the screen stays open.
    save.team_size = 0;
    save.m_totalcash[0] = 0;
    const FamilyDescriptor* const soldier =
        get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    og::ui::HireSession broke(save, 0);
    ASSERT_TRUE(broke.current_recruit() != nullptr);
    ASSERT_EQ(static_cast<std::uint32_t>(soldier->hiring_cost),
              broke.current_cost())
        << "a base soldier costs its family's hiring price, and the purse "
           "holds nothing";
    {
        HireSessionSlotGuard installed(&broke);
        EXPECT_EQ(4, static_cast<int>(add_guy(0)))
            << "an unaffordable hire returns MENU_OK, keeping HIRE open";
    }
    EXPECT_EQ(0, static_cast<int>(save.team_size));
    EXPECT_EQ(nullptr, save.team_list[0]);
    EXPECT_EQ(0u, save.m_totalcash[0]) << "nothing was charged";

    // Rich, with a full roster: the screen closes.
    for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
    {
        save.team_list[static_cast<std::size_t>(slot)] =
            std::make_unique<guy>(FAMILY_SOLDIER);
    }
    save.team_size = MAX_TEAM_SIZE;
    save.m_totalcash[0] = 900000;
    og::ui::HireSession full(save, 0);
    {
        HireSessionSlotGuard installed(&full);
        EXPECT_EQ(-1, static_cast<int>(add_guy(0)))
            << "a full roster returns -1, closing HIRE";
    }
    EXPECT_EQ(MAX_TEAM_SIZE, static_cast<int>(save.team_size));
    EXPECT_EQ(900000u, save.m_totalcash[0]) << "nothing was charged";
}

// A joiner parked on a host's campaign it does not have mounted cannot read
// that campaign's level: VIEW LEVEL says so and opens nothing.
TEST(PickerUncovered, view_scenario_refuses_an_unmounted_campaign)
{
    PickerStateGuard guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string saved_campaign = save.current_campaign;
    const short saved_scen = save.scen_num;

    trace_clear();
    save.current_campaign = "org.example.not-mounted";
    // Belt: even if the mount guard were removed, this level cannot load, so
    // the viewer's blocking loop is unreachable from this test.
    save.scen_num = 9999;
    ASSERT_NE(get_mounted_campaign(), save.current_campaign);

    EXPECT_EQ(2, static_cast<int>(create_view_scenario_menu(0)))
        << "the refusal returns MENU_REDRAW";
    EXPECT_TRUE(trace_contains("popup", "VIEW LEVEL: CAMPAIGN NOT"))
        << "the popup names the reason";
    EXPECT_FALSE(trace_contains("popup", "COULD NOT"))
        << "the mount guard fires BEFORE any level load is attempted";
    EXPECT_EQ("org.example.not-mounted", save.current_campaign)
        << "the viewer never mounts anything";
    EXPECT_EQ(9999, static_cast<int>(save.scen_num));

    save.current_campaign = saved_campaign;
    save.scen_num = saved_scen;
}
