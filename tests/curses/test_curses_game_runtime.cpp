/* Integration tests for the local single-player game runtime.
 *
 * These load real level 1 of the default campaign (the test main initializes the
 * headless filesystem + registries), build an in-process server+client session,
 * and drive the loop headlessly with a HeadlessTerminal + FakeClock. No SDL, no
 * TTY.
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/curses_game_runtime.h>
#include <openglad/platform/curses/curses_input.h>
#include <openglad/platform/curses/curses_renderer.h>
#include <openglad/platform/curses/headless_terminal.h>
#include <openglad/platform/curses/clock.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>

#include <memory>

using namespace og::curses;

namespace {

// Fill `save` with a one-soldier team on level 1 of the default campaign.
// (SaveData is move-only, so callers pass a local by reference.)
void init_test_save(SaveData& save)
{
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.numplayers = 1;
    save.my_team = 0;
    og::ui::initialize_starting_team(save, {FAMILY_SOLDIER});
}

int held(InputAction a) { return static_cast<int>(a); }

// A minimal CursesGameSession that presents one exit prompt, so run_level_loop's
// prompt handling can be tested without reaching a real level exit.
class FakeExitSession : public CursesGameSession
{
public:
    GameWorld world_{0};
    bool pending_ = true;
    bool responded_ = false;
    bool last_response_ = false;

    void send_input(const InputState&) override {}
    void advance() override {}
    GameWorld& mirror_world() override { return world_; }
    std::uint32_t followed_entity_id() const override { return 0; }
    std::uint32_t next_input_tick() const override { return 0; }
    bool ended() const override { return false; }
    int ending() const override { return 0; }
    int next_level() const override { return -1; }
    void request_abort() override {}
    std::vector<std::string> drain_messages() override { return {}; }

    bool exit_prompt_pending() const override { return pending_; }
    std::string exit_prompt_text() const override { return "Exit to Level 2?"; }
    void respond_to_exit_prompt(bool accept) override
    {
        responded_ = true;
        last_response_ = accept;
        pending_ = false;
    }
};

} // namespace

TEST(CursesGameRuntimeLocal, session_loads_level_and_populates_mirror)
{
    SaveData save;
    init_test_save(save);
    std::string err;
    auto session = make_local_session(save, /*difficulty=*/1, &err);
    ASSERT_NE(session, nullptr) << "make_local_session failed: " << err;

    // After the initial keyframe exchange the mirror world has entities.
    GameWorld& world = session->mirror_world();
    EXPECT_GT(world.oblist.size(), 0u) << "mirror world should have entities";
    EXPECT_GT(world.grid.w, 0) << "mirror world should have a tile grid";
}

TEST(CursesGameRuntimeLocal, followed_avatar_resolves_to_player)
{
    SaveData save;
    init_test_save(save);
    std::string err;
    auto session = make_local_session(save, 1, &err);
    ASSERT_NE(session, nullptr) << err;

    const std::uint32_t id = session->followed_entity_id();
    ASSERT_NE(id, 0u) << "the player's avatar id should resolve";

    const walker* avatar = session->mirror_world().find_by_id(id);
    ASSERT_NE(avatar, nullptr);
    EXPECT_GE(avatar->user(), 0) << "followed avatar is a human-controlled walker";
}

TEST(CursesGameRuntimeLocal, advancing_progresses_the_simulation)
{
    SaveData save;
    init_test_save(save);
    std::string err;
    auto session = make_local_session(save, 1, &err);
    ASSERT_NE(session, nullptr) << err;

    const std::uint32_t start_tick = session->next_input_tick();
    for (int i = 0; i < 10; ++i)
        session->advance();
    EXPECT_GT(session->next_input_tick(), start_tick) << "server tick should advance";
}

TEST(CursesGameRuntimeLocal, movement_input_moves_the_avatar)
{
    SaveData save;
    init_test_save(save);
    std::string err;
    auto session = make_local_session(save, 1, &err);
    ASSERT_NE(session, nullptr) << err;

    const std::uint32_t id = session->followed_entity_id();
    ASSERT_NE(id, 0u);
    auto pos = [&]() {
        const walker* w = session->mirror_world().find_by_id(id);
        return w ? std::pair<int, int>{w->xpos(), w->ypos()} : std::pair<int, int>{-1, -1};
    };

    // Try each cardinal direction for a stretch of frames; the avatar should be
    // able to move somewhere (open ground exists around the spawn).
    const InputAction dirs[] = {InputAction::MoveRight, InputAction::MoveDown,
                                InputAction::MoveLeft, InputAction::MoveUp};
    bool moved = false;
    for (InputAction dir : dirs) {
        const std::pair<int, int> dir_start = pos();
        for (int i = 0; i < 40 && !moved; ++i) {
            InputState st;
            st.players[0].held[held(dir)] = true;
            st.players[0].pressed[held(dir)] = (i == 0);
            session->send_input(st);
            session->advance();
            if (pos() != dir_start)
                moved = true;
        }
        if (moved)
            break;
    }
    EXPECT_TRUE(moved) << "movement input should change the avatar's position";
}

TEST(CursesGameRuntimeLocal, run_level_loop_renders_avatar_and_respects_frame_cap)
{
    SaveData save;
    init_test_save(save);
    std::string err;
    auto session = make_local_session(save, 1, &err);
    ASSERT_NE(session, nullptr) << err;

    HeadlessTerminal term(30, 80);
    FakeClock clock;
    CursesInput input;
    CursesRenderer renderer;

    LevelLoopOptions opt;
    opt.max_frames = 20;
    opt.no_pacing = true;
    opt.render = true;

    GameRunResult result = run_level_loop(*session, term, clock, input, renderer, opt);

    // The frame cap stopped us before the level naturally ended.
    EXPECT_FALSE(result.ended);
    // The player's avatar ('@') was drawn at least once.
    EXPECT_GT(term.count_char(U'@'), 0) << "the followed avatar should render as '@'";
    EXPECT_GE(term.present_count(), 1);
}

// Regression lock at the loop level: 'q' is a player key, NOT a quit/withdraw —
// only Esc withdraws. This is the bug the user hit (q was hardcoded to quit); 'q'
// must never short-circuit the mission again.
TEST(CursesGameRuntimeLocal, non_meta_key_does_not_withdraw)
{
    SaveData save;
    init_test_save(save);
    std::string err;
    auto session = make_local_session(save, 1, &err);
    ASSERT_NE(session, nullptr) << err;

    HeadlessTerminal term(30, 80);
    FakeClock clock;
    CursesInput input;
    CursesRenderer renderer;

    term.push_char(U'q'); // a player key, NOT a meta key

    LevelLoopOptions opt;
    opt.max_frames = 10;
    opt.no_pacing = true;
    opt.render = false;

    GameRunResult result = run_level_loop(*session, term, clock, input, renderer, opt);
    EXPECT_FALSE(result.withdrew) << "'q' is a player key, not a withdraw";
    EXPECT_FALSE(result.ended) << "the frame cap stopped us, not a level end";
}

// Esc is the one meta key the loop intercepts: it opens the in-game menu, which
// the level loop treats as a quit-to-menu / withdraw.
TEST(CursesGameRuntimeLocal, escape_key_withdraws_from_the_mission)
{
    SaveData save;
    init_test_save(save);
    std::string err;
    auto session = make_local_session(save, 1, &err);
    ASSERT_NE(session, nullptr) << err;

    HeadlessTerminal term(30, 80);
    FakeClock clock;
    CursesInput input;
    CursesRenderer renderer;

    term.push_special(KeyCode::Escape); // open in-game menu -> withdraw

    LevelLoopOptions opt;
    opt.max_frames = 50;
    opt.no_pacing = true;
    opt.render = false;

    GameRunResult result = run_level_loop(*session, term, clock, input, renderer, opt);
    EXPECT_TRUE(result.withdrew) << "pressing Esc should withdraw from the mission";
}

// The exit is NEVER auto-accepted: the loop asks the player, and 'n' declines so
// they stay in the level. (This is the user's complaint — they could not refuse.)
TEST(CursesGameRuntimeLocal, exit_prompt_can_be_declined)
{
    FakeExitSession session;
    HeadlessTerminal term(24, 60);
    FakeClock clock;
    CursesInput input;
    CursesRenderer renderer;

    term.push_char(U'n'); // decline

    LevelLoopOptions opt;
    opt.max_frames = 3;
    opt.no_pacing = true;
    opt.render = false;

    run_level_loop(session, term, clock, input, renderer, opt);

    EXPECT_TRUE(session.responded_) << "the player must be asked, not auto-accepted";
    EXPECT_FALSE(session.last_response_) << "'n' declines the exit (stay in the level)";
}

// And 'y' accepts.
TEST(CursesGameRuntimeLocal, exit_prompt_accepts_on_y)
{
    FakeExitSession session;
    HeadlessTerminal term(24, 60);
    FakeClock clock;
    CursesInput input;
    CursesRenderer renderer;

    term.push_char(U'y'); // accept

    LevelLoopOptions opt;
    opt.max_frames = 3;
    opt.no_pacing = true;
    opt.render = false;

    run_level_loop(session, term, clock, input, renderer, opt);

    EXPECT_TRUE(session.responded_);
    EXPECT_TRUE(session.last_response_) << "'y' accepts the exit";
}

// [BLOCKER lock-in] A real, server-forwarded level end must (a) terminate the
// client's end-detection AND (b) leave the caller's roster untouched on a non-win.
//
// We drive a genuine, server-authoritative loss: the mission-tick safety-net
// timeout. When GameWorld::tick() hits its level-tick limit it sets
// game_ended=true / ending=1 (a retreat) on the SERVER world; the broadcast layer
// then synthesizes and forwards the terminal SetEnd + EndGame game-flow events.
// The client's apply_game_flow_batch is what interprets those events into
// mirror.end / mirror.ending — exactly the path that previously never fired
// (the snapshot's end/ending alone did not carry the verdict). Because the
// outcome is a loss (ending != 0), commit_result_to_save() must be a no-op:
// persisting a defeat would wipe the team (update_guys drops everyone) or
// otherwise corrupt the roster the player entered the level with.
//
// (A user-initiated withdraw via request_abort() exercises the same
// forward-EndGame plumbing, but in return-to-lobby mode the server never sets its
// own world.end, so each following snapshot would re-clear the mirror's end; the
// withdraw is instead observed via GameRunResult::withdrew — see the 'q'/Esc
// tests. The timeout gives a durable server-side end that makes ended() stable
// and assertable here.)
TEST(CursesGameRuntimeLocal, server_forwarded_end_terminates_and_preserves_roster)
{
    SaveData save;
    init_test_save(save);
    const int team_before = static_cast<int>(save.team_size);
    ASSERT_EQ(team_before, 1) << "the test starts with a 1-soldier team";
    const std::string member_before = save.team_list[0]->name;

    std::string err;
    auto session = make_local_session(save, /*difficulty=*/1, &err);
    ASSERT_NE(session, nullptr) << err;
    ASSERT_FALSE(session->ended()) << "fresh session is mid-level";

    // Force the very next server tick to hit the mission-timeout safety net.
    og::sim::g_test_level_tick_limit_override = 1;
    struct OverrideGuard {
        ~OverrideGuard() { og::sim::g_test_level_tick_limit_override = 0; }
    } override_guard;

    for (int i = 0; i < 15; ++i) {
        InputState st; // input keeps the server world ticking each frame
        session->send_input(st);
        session->advance();
    }

    EXPECT_TRUE(session->ended())
        << "the server-forwarded EndGame must terminate end-detection (the bug "
           "that previously never fired)";
    EXPECT_NE(session->ending(), 0)
        << "a timed-out mission is a loss/retreat (ending != 0), never a win";

    // A non-win must not persist: the caller's team is unchanged.
    session->commit_result_to_save();
    EXPECT_EQ(static_cast<int>(save.team_size), team_before)
        << "a non-win must not wipe or grow the caller's roster";
    ASSERT_GE(static_cast<int>(save.team_size), 1);
    ASSERT_NE(save.team_list[0], nullptr);
    EXPECT_EQ(save.team_list[0]->name, member_before)
        << "the surviving member is exactly the one we entered with";
}

// A server-forwarded EndGame (here via request_abort -> party withdraw) must
// DURABLY end the session: the latched end state survives the delta snapshots
// that keep arriving afterward (which re-serialize the server world's end=0 in
// return-to-lobby mode). This is the snapshot-clobbering hazard that a
// world-stored end flag would hit; the session latches it instead.
TEST(CursesGameRuntimeLocal, server_forwarded_endgame_event_durably_ends_session)
{
    SaveData save;
    init_test_save(save);
    std::string err;
    auto session = make_local_session(save, 1, &err);
    ASSERT_NE(session, nullptr) << err;

    session->request_abort(); // party-wide withdraw -> server forwards EndGame(ending=1)

    bool ended_once = false;
    for (int i = 0; i < 25; ++i) {
        session->advance();
        if (session->ended())
            ended_once = true;
        // Once ended, it must stay ended despite further snapshots clobbering the
        // mirror world's end field back to 0.
        if (ended_once) {
            EXPECT_TRUE(session->ended()) << "end state must persist (frame " << i << ")";
        }
    }
    EXPECT_TRUE(ended_once) << "the forwarded EndGame event must end the session";
    EXPECT_NE(session->ending(), 0) << "a withdraw is reported as a non-win";
}

// [win persistence] Unit-test advance_save_after_win directly. We feed it a world
// holding one surviving team-0 Living walker with a myguy so update_guys keeps the
// roster, then assert the campaign/gold/score/completion mutations.
//
// Critical ordering detail: advance_save_after_win first calls
// sync_headless_server_save_data_from_world(), which OVERWRITES save.m_score[] and
// save.completed_levels[] from the world and sets save.scen_num from
// world.current_scenario. So the score that becomes gold, and the level that gets
// marked completed, both come from the WORLD, not from pre-set save fields.
TEST(CursesGameRuntimeWin, advance_save_after_win_advances_campaign_and_persists_team)
{
    SaveData save;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.numplayers = 1;
    save.my_team = 0;
    og::ui::initialize_starting_team(save, {FAMILY_SOLDIER});
    const std::uint32_t cash_before = save.m_totalcash[0];

    // A world we control: a custom factory so add_ob() yields bare walkers.
    GameWorld world(0);
    world.entity_factory = [](Order, std::int32_t) { return std::make_unique<walker>(); };
    world.my_team = 0;
    world.current_scenario = 1;   // sync -> save.scen_num = 1 (the level we just won)
    world.m_score[0] = 100;       // sync -> save.m_score[0] = 100; gold gain = 2*100

    // One surviving, controlled team-0 hero carrying a guy, so update_guys keeps it.
    walker* hero = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(hero, nullptr);
    hero->set_order(Order::Living);
    hero->set_family(static_cast<char>(FAMILY_SOLDIER));
    hero->set_team_num(0);
    hero->set_user(0);
    {
        auto g = std::make_unique<guy>(FAMILY_SOLDIER);
        g->name = "Veteran";
        g->exp = 0;
        hero->set_owned_myguy(std::move(g));
    }

    advance_save_after_win(save, world, /*next_level=*/2);

    EXPECT_EQ(static_cast<int>(save.scen_num), 2) << "scen_num advances to next_level";
    EXPECT_TRUE(save.is_level_completed(1))
        << "the just-finished level (world.current_scenario) is marked completed";
    EXPECT_EQ(save.m_totalcash[0], cash_before + 2u * 100u)
        << "gold awarded is 2x the team score from the world";
    EXPECT_EQ(save.m_score[0], 0u) << "the per-level score is consumed";
    EXPECT_EQ(static_cast<int>(save.team_size), 1)
        << "update_guys persisted the surviving controlled hero";
    ASSERT_NE(save.team_list[0], nullptr);
    EXPECT_EQ(save.team_list[0]->name, "Veteran")
        << "the persisted member is the one that survived";
}

// next_level < 0 means "advance to the next sequential level": scen_num becomes
// old+1. Empty world is fine here — update_guys on an empty oblist just clears the
// team, which is irrelevant to the campaign-cursor assertion.
TEST(CursesGameRuntimeWin, advance_save_after_win_negative_next_level_advances_by_one)
{
    SaveData save;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 4;
    save.my_team = 0;
    og::ui::initialize_starting_team(save, {FAMILY_SOLDIER});

    GameWorld world(0);
    world.entity_factory = [](Order, std::int32_t) { return std::make_unique<walker>(); };
    world.my_team = 0;
    world.current_scenario = 4; // sync -> save.scen_num = 4 before the +1 advance

    advance_save_after_win(save, world, /*next_level=*/-1);

    EXPECT_EQ(static_cast<int>(save.scen_num), 5)
        << "a negative next_level advances scen_num to old+1";
    EXPECT_TRUE(save.is_level_completed(4))
        << "the finished level is still marked completed";
    // update_guys ran on an empty oblist, so the team is now empty (no survivors).
    EXPECT_EQ(static_cast<int>(save.team_size), 0)
        << "an empty finished world leaves no surviving team members";
}

// [difficulty affects sim] Loading the same level at a harder difficulty scales
// enemy stats. The spawn COUNT is unchanged (difficulty only re-scales the
// already-spawned enemies, never their number), so the observable signal is total
// enemy HP: difficulty 2 (200%) gives enemies far more max-HP than difficulty 0
// (50%). We read it straight from the mirror world the renderer sees.
TEST(CursesGameRuntimeLocal, harder_difficulty_scales_enemy_hp_in_the_mirror)
{
    // Sum max-HP of every non-player-team (team != 0) Living enemy in the mirror.
    auto enemy_hp_and_count = [](CursesGameSession& session) {
        float total_hp = 0.0f;
        int count = 0;
        for (const auto& up : session.mirror_world().oblist) {
            const walker* w = up.get();
            if (w && !w->dead() && w->query_order() == Order::Living &&
                w->team_num() != 0 && w->stats()) {
                total_hp += w->stats()->max_hitpoints();
                ++count;
            }
        }
        return std::pair<float, int>{total_hp, count};
    };

    SaveData easy_save;
    init_test_save(easy_save);
    std::string err;
    auto easy = make_local_session(easy_save, /*difficulty=*/0, &err);
    ASSERT_NE(easy, nullptr) << err;

    SaveData hard_save;
    init_test_save(hard_save);
    auto hard = make_local_session(hard_save, /*difficulty=*/2, &err);
    ASSERT_NE(hard, nullptr) << err;

    const auto [easy_hp, easy_count] = enemy_hp_and_count(*easy);
    const auto [hard_hp, hard_count] = enemy_hp_and_count(*hard);

    ASSERT_GT(easy_count, 0) << "level 1 should spawn enemies";
    // Difficulty rescales existing enemies, so the headcount is identical.
    EXPECT_EQ(easy_count, hard_count)
        << "difficulty changes enemy stats, not the number of enemies";
    EXPECT_GT(hard_hp, easy_hp)
        << "difficulty 2 (200%) enemies have more max-HP than difficulty 0 (50%); "
           "easy=" << easy_hp << " hard=" << hard_hp;
}

