#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/legacy/base.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/gameplay/guy.h>
#include <openglad/resources/save_data.h>
#include <gtest/gtest.h>

#include <map>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace {

// Drives one EndGame game-flow event exactly as the networked transport does:
// the server forwards a GameFlowEventBatch{EndGame, a=ending, b=next_level} and
// the display client routes it through screen::dispatch_game_flow_events ->
// screen::endgame -> results_screen (a no-op returning retry=false in TESTING).
void dispatch_endgame_event(int ending, int next_level)
{
    screen* const s = og::runtime::current_session->myscreen_;
    s->world().end = 0;       // endgame() early-returns if end is already set
    s->world().retry = false;
    s->world().current_scenario = s->save_data.scen_num;

    og::sim::GameFlowEventBatch batch;
    batch.sequence = 1;
    og::sim::Event ev;
    ev.kind = og::sim::EventKind::EndGame;
    ev.a = static_cast<std::uint32_t>(ending);
    ev.b = static_cast<std::uint32_t>(static_cast<std::int32_t>(next_level));
    batch.events.push_back(ev);
    s->dispatch_game_flow_events(batch);
}

void seed_save0_single_member(const char* name, short scen_num)
{
    SaveData& sd = og::runtime::current_session->myscreen_->save_data;
    sd.current_campaign = "org.openglad.gladiator";
    sd.scen_num = scen_num;
    sd.current_levels.clear();
    sd.completed_levels.clear();
    for (auto& slot : sd.team_list)
        slot.reset();
    sd.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    sd.team_list[0]->name = name;
    sd.team_list[0]->id = 1;
    sd.team_size = 1;
    ASSERT_TRUE(sd.save("save0"));
}

} // namespace

TEST(ResultsScreenBranches, results_screen_ending_branches_smoke)
{
    // Defeat generic.
    ASSERT_FALSE(results_screen(1, -1));
    // Defeat retreat.
    ASSERT_FALSE(results_screen(1, 2));

    // Victory, completed vs not completed.
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    // Make sure completion state is deterministic: clear and set once.
    og::runtime::current_session->myscreen_->save_data.current_levels.clear();
    og::runtime::current_session->myscreen_->save_data.completed_levels.clear();
    ASSERT_FALSE(results_screen(0, 2)); // not completed -> "Victory!"

    og::runtime::current_session->myscreen_->save_data.completed_levels[og::runtime::current_session->myscreen_->save_data.current_campaign].insert(
        og::runtime::current_session->myscreen_->save_data.scen_num);
    ASSERT_FALSE(results_screen(0, 2)); // completed -> "Traveling on..."

    results_screen_testing_set_force_full(true);
    ASSERT_FALSE(results_screen(0, 2)); // force the non-bypass TESTING overload path.
    results_screen_testing_set_force_full(false);

    // Special defeat type.
    ASSERT_FALSE(results_screen(SCEN_TYPE_SAVE_ALL, -1));
}


TEST(ResultsScreenBranches, results_screen_overload_calls_smoke)
{
    std::map<int, guy*> before;
    std::map<int, walker*> after;
    ASSERT_FALSE(results_screen(0, 2, before, after));
}

// Gap 4 ("hit play for next level") + Bug B regression: in a networked game the
// win is delivered as an EndGame game-flow event that the display routes to
// screen::endgame. Networked play now returns to the team-build menu between
// levels (single-player parity), so the win ENDS the display session
// (world.end=1) — the same as single-player — instead of auto-advancing
// in-session. It advances scen_num in memory but must NOT autosave the COMBINED
// roster to save0 — that would re-clobber this player's save0 with every
// player's gladiators. The networked per-player save path owns save0.
TEST(MpEndgameFlow, networked_win_ends_display_to_menu_but_does_not_clobber_save0)
{
    SaveData& sd = og::runtime::current_session->myscreen_->save_data;
    const auto saved_end = og::runtime::current_session->myscreen_->world().end;
    const bool saved_net = og::runtime::current_session->networked_session_;

    seed_save0_single_member("ONLY_MINE", 1);

    og::runtime::current_session->networked_session_ = true;
    dispatch_endgame_event(0, 2); // networked WIN -> next level 2

    EXPECT_EQ(1, static_cast<int>(
                     og::runtime::current_session->myscreen_->world().end))
        << "a networked win ends the display session (world.end=1 -> loop exits "
           "-> back to the team-build menu, where the next level is started)";
    EXPECT_EQ(2, static_cast<int>(sd.scen_num))
        << "the display advances scen_num in memory after a win";
    EXPECT_TRUE(sd.is_level_completed(1));

    SaveData disk;
    ASSERT_EQ(SaveDataIoError::None, disk.load_with_error("save0"));
    EXPECT_EQ(1, static_cast<int>(disk.scen_num))
        << "networked display win must NOT persist to save0 (Bug B): that would "
           "overwrite this player's save with the combined roster";
    ASSERT_NE(nullptr, disk.team_list[0]);
    EXPECT_EQ("ONLY_MINE", disk.team_list[0]->name)
        << "save0 must still hold only this player's own roster";

    og::runtime::current_session->networked_session_ = saved_net;
    og::runtime::current_session->myscreen_->world().end = saved_end;
}

// Guard the other side of the Bug B fix: a SINGLE-PLAYER win must still autosave
// (advance) save0 exactly as before.
TEST(MpEndgameFlow, single_player_win_still_autosaves_save0_advance)
{
    SaveData& sd = og::runtime::current_session->myscreen_->save_data;
    const auto saved_end = og::runtime::current_session->myscreen_->world().end;
    const bool saved_net = og::runtime::current_session->networked_session_;

    seed_save0_single_member("SOLO", 1);

    og::runtime::current_session->networked_session_ = false; // single-player
    dispatch_endgame_event(0, 2);

    EXPECT_EQ(1, static_cast<int>(
                     og::runtime::current_session->myscreen_->world().end));
    EXPECT_EQ(2, static_cast<int>(sd.scen_num));

    SaveData disk;
    ASSERT_EQ(SaveDataIoError::None, disk.load_with_error("save0"));
    EXPECT_EQ(2, static_cast<int>(disk.scen_num))
        << "single-player win must still autosave the level advance to save0";
    EXPECT_TRUE(disk.is_level_completed(1));

    og::runtime::current_session->networked_session_ = saved_net;
    og::runtime::current_session->myscreen_->world().end = saved_end;
}

// Playtest bug C regression: a CTF LOSS arrives in the classic WIN shape
// (ending=0, next_level == this level), but the popup must never claim
// victory, never claim travel, and the level must NOT be marked completed —
// on the FIRST loss and on every loss after it (the old code marked the
// level completed on loss #1, so loss #2 printed "Moving on to Level N").
TEST(ResultsScreenBranches, ctf_loss_popup_is_defeat_and_never_completes_level)
{
    screen* const s = og::runtime::current_session->myscreen_;
    SaveData& sd = s->save_data;
    const auto saved_end = s->world().end;
    const char saved_type = s->world().type;
    walker* const saved_control = s->viewob[0]->control;

    sd.current_campaign = "org.openglad.gladiator";
    sd.scen_num = 506;
    sd.current_levels.clear();
    sd.completed_levels.clear();
    s->world().completed_levels.clear(); // endgame syncs save from world

    s->world().type |= GameWorld::TYPE_CTF;
    s->world().ctf = og::sim::CtfState{};
    s->world().ctf.active = true;
    s->world().ctf.winner_team = 1;       // GREEN bots won
    s->world().ctf.winner_is_player = false;
    s->world().ctf.team_active[0] = true;
    s->world().ctf.team_active[1] = true;

    // A live local control on the LOSING team drives the DEFEAT! title.
    walker* const local = s->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, local);
    local->set_team_num(0);
    s->viewob[0]->control = local;

    // First loss: the CTF loss/rematch shape (ending=0, next == current).
    trace_clear();
    dispatch_endgame_event(0, 506);
    EXPECT_TRUE(trace_contains("popup", "DEFEAT!"))
        << "a local control on the losing team must see DEFEAT!";
    EXPECT_TRUE(trace_contains("popup", "GREEN TEAM WINS!"));
    EXPECT_TRUE(trace_contains("popup", "rematch"));
    EXPECT_FALSE(trace_contains("popup", "Victory!"))
        << "a CTF loss must never show the classic victory popup";
    EXPECT_FALSE(trace_contains("popup", "Moving on"));
    EXPECT_FALSE(sd.is_level_completed(506))
        << "a CTF loss must not mark the level completed";
    EXPECT_EQ(506, static_cast<int>(sd.scen_num))
        << "the rematch shape stays on the same level";

    // Second loss: byte-identical messaging — the old bug printed
    // "Traveling on... Moving on to Level 506" here.
    trace_clear();
    dispatch_endgame_event(0, 506);
    EXPECT_TRUE(trace_contains("popup", "DEFEAT!"));
    EXPECT_TRUE(trace_contains("popup", "rematch"));
    EXPECT_FALSE(trace_contains("popup", "Victory!"));
    EXPECT_FALSE(trace_contains("popup", "Moving on"));
    EXPECT_FALSE(sd.is_level_completed(506));

    // A CTF WIN (next level advances) still completes the level and shows
    // VICTORY! for the winning local control.
    trace_clear();
    s->world().ctf.winner_team = 0;
    s->world().ctf.winner_is_player = true;
    dispatch_endgame_event(0, 507);
    EXPECT_TRUE(trace_contains("popup", "VICTORY!"));
    EXPECT_TRUE(trace_contains("popup", "RED TEAM WINS!"));
    EXPECT_TRUE(trace_contains("popup", "Moving on to Level 507"));
    EXPECT_TRUE(sd.is_level_completed(506))
        << "a real CTF win must still mark the level completed";

    s->viewob[0]->control = saved_control;
    local->set_dead(1);
    s->world().ctf = og::sim::CtfState{};
    s->world().type = saved_type;
    s->world().end = saved_end;
}

// Neutral fallback: with no resolvable local control the CTF popup must not
// guess — MATCH OVER, never winner_is_player-derived VICTORY.
TEST(ResultsScreenBranches, ctf_loss_popup_neutral_fallback_without_control)
{
    screen* const s = og::runtime::current_session->myscreen_;
    SaveData& sd = s->save_data;
    const auto saved_end = s->world().end;
    const char saved_type = s->world().type;
    walker* const saved_control = s->viewob[0]->control;

    sd.current_campaign = "org.openglad.gladiator";
    sd.scen_num = 506;
    sd.current_levels.clear();
    sd.completed_levels.clear();

    s->world().type |= GameWorld::TYPE_CTF;
    s->world().ctf = og::sim::CtfState{};
    s->world().ctf.active = true;
    s->world().ctf.winner_team = 1;
    // winner_is_player=true must NOT produce VICTORY! — it is global (any
    // human on the winning team), not this client's outcome.
    s->world().ctf.winner_is_player = true;
    s->viewob[0]->control = nullptr;

    trace_clear();
    dispatch_endgame_event(0, 506);
    EXPECT_TRUE(trace_contains("popup", "MATCH OVER"))
        << "no resolvable local control: neutral title";
    EXPECT_FALSE(trace_contains("popup", "VICTORY!"));
    EXPECT_FALSE(trace_contains("popup", "Victory!"));

    s->viewob[0]->control = saved_control;
    s->world().ctf = og::sim::CtfState{};
    s->world().type = saved_type;
    s->world().end = saved_end;
}

// Gap 2 ("back to menu as intended"): a terminal defeat (no next level) sets
// world.end=1, which makes run_native_game_loop exit and control return to the
// picker/menu. It must not advance the level.
TEST(MpEndgameFlow, networked_terminal_defeat_ends_session_back_to_menu)
{
    SaveData& sd = og::runtime::current_session->myscreen_->save_data;
    const auto saved_end = og::runtime::current_session->myscreen_->world().end;
    const bool saved_net = og::runtime::current_session->networked_session_;

    seed_save0_single_member("MINE", 3);

    og::runtime::current_session->networked_session_ = true;
    dispatch_endgame_event(1, -1); // terminal defeat, no next level

    EXPECT_EQ(1, static_cast<int>(
                     og::runtime::current_session->myscreen_->world().end))
        << "terminal defeat ends the session (world.end=1 -> loop exits -> menu)";
    EXPECT_EQ(3, static_cast<int>(sd.scen_num))
        << "a defeat must not advance the level";

    og::runtime::current_session->networked_session_ = saved_net;
    og::runtime::current_session->myscreen_->world().end = saved_end;
}
