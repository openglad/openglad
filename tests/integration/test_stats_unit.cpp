#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#include <openglad/legacy/base.h>
#include <memory>
#include <gtest/gtest.h>
#include <openglad/interface/game_context.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <openglad/gameplay/guy.h>
#include "test_gameplay_context_scope.h"

namespace {
// Paint one tile of a fixture's level grid (GRID_SIZE px per tile). The
// blocked/passable probes in these tests read the world through this grid, so
// a single wall tile is how a test blocks one specific probe cell.
template <class Fixture>
void set_grid_tile(Fixture& fx, int tx, int ty, unsigned char tile)
{
    PixieData& g = fx.level.world().grid;
    ASSERT_TRUE(g.valid()) << "the fixture grid exists";
    ASSERT_TRUE(tx >= 0 && ty >= 0 && tx < g.w && ty < g.h) << "tile is on the map";
    g.data[static_cast<std::size_t>(ty * g.w + tx)] = tile;
}
} // namespace

// --- From test_stats_coverage_push.cpp ---
namespace detail_stats_coverage_push {
namespace {

struct StatsFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    StatsFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &events, &cfg);
    }
};

walker* add_living(StatsFixture& fx, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(64, 64);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(StatsUnit, stats_commands_and_clamps_paths)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    ASSERT_TRUE(w != nullptr);

    w->stats()->add_command(COMMAND_DIE, 1, 0, 0);
    ASSERT_TRUE(w->stats()->delete_me() == 1);

    w->stats()->force_command(COMMAND_WALK, 1, 0, 0);
    ASSERT_TRUE(!w->stats()->commands.empty());
    ASSERT_TRUE(w->stats()->commands.front().com1 == 1);
    ASSERT_TRUE(w->stats()->commands.front().com2 == 1);

    w->stats()->commands.clear();
    ASSERT_TRUE(w->stats()->do_command() == 0);
}

TEST(StatsUnit, stats_attack_and_follow_step_toward_their_target_and_blocked_probes_read_their_own_cells)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_NE(nullptr, w) << "actor created";
    ASSERT_NE(nullptr, foe) << "foe created";

    // walk() only MOVES when the requested facing already matches curdir, so
    // every "walks east" expectation below starts out facing east.
    w->setxy(64, 64);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    w->set_foe(foe);
    foe->setxy(320, 64);

    // COMMAND_ATTACK on a foe far out of weapon reach: fire_check denies with
    // OutOfRange, so the arm walks one step toward the foe and keeps its count.
    w->stats()->force_command(COMMAND_ATTACK, 2, 0, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "ATTACK on a live foe reports success";
    EXPECT_EQ(65, w->xpos()) << "ATTACK steps one stepsize east toward the foe";
    EXPECT_EQ(64, w->ypos()) << "the 3:1 dominance snap zeroes the y delta";
    ASSERT_EQ(1u, w->stats()->commands.size()) << "the ATTACK command stays queued";
    EXPECT_EQ(1, w->stats()->commands.front().commandcount) << "one round consumed";

    // COMMAND_FOLLOW with no foe: step toward the leader, and release the
    // leader on the LAST round (commandcount < 2).
    w->stats()->commands.clear();
    w->set_foe(nullptr);
    w->set_leader(foe);
    w->stats()->force_command(COMMAND_FOLLOW, 2, 0, 0);
    ASSERT_EQ(1, w->stats()->do_command()) << "FOLLOW reports success";
    EXPECT_EQ(66, w->xpos()) << "FOLLOW steps one stepsize toward the leader";
    EXPECT_EQ(foe, w->leader()) << "a distant leader is kept while the count is >= 2";
    ASSERT_EQ(1, w->stats()->do_command()) << "last round of FOLLOW";
    EXPECT_EQ(67, w->xpos()) << "and steps again";
    EXPECT_EQ(nullptr, w->leader()) << "the final round releases the leader";
    EXPECT_FALSE(w->stats()->has_commands()) << "the exhausted command is popped";

    // The four blocked probes each read ONE cell, one pixel off our own
    // position, chosen by curdir. Blocking a single grid tile to the east must
    // therefore light up exactly the probes whose x offset is +1, and a tile to
    // the south exactly those whose y offset is +1.
    struct Probe { int dx; int dy; };
    struct Row { char dir; Probe right; Probe right_forward; Probe right_back; Probe forward; };
    const Row rows[] = {
        {FACE_UP,         { 1,  0}, { 1, -1}, { 1,  1}, { 0, -1}},
        {FACE_UP_RIGHT,   { 1,  1}, { 1,  0}, { 0,  1}, { 1, -1}},
        {FACE_RIGHT,      { 0,  1}, { 1,  1}, {-1,  1}, { 1,  0}},
        {FACE_DOWN_RIGHT, {-1,  1}, { 0,  1}, {-1,  0}, { 1,  1}},
        {FACE_DOWN,       {-1,  0}, {-1,  1}, {-1, -1}, { 0,  1}},
        {FACE_DOWN_LEFT,  {-1, -1}, {-1,  0}, { 0, -1}, {-1,  1}},
        {FACE_LEFT,       { 0, -1}, {-1, -1}, { 1, -1}, {-1,  0}},
        {FACE_UP_LEFT,    { 1, -1}, { 0, -1}, { 1,  0}, {-1, -1}},
        {static_cast<char>(99), {0, 0}, {0, 0}, {0, 0}, {0, 0}},  // default arm probes nothing
    };

    w->setxy(64, 64);       // tile (4,4); a 16px box on a 16px grid
    foe->setxy(320, 320);   // far enough that the obmap never answers a probe

    for (int phase = 0; phase < 3; ++phase)
    {
        // phase 0: open ground, phase 1: wall east, phase 2: wall south.
        set_grid_tile(fx, 5, 4, phase == 1 ? PIX_H_WALL1 : PIX_GRASS1);
        set_grid_tile(fx, 4, 5, phase == 2 ? PIX_H_WALL1 : PIX_GRASS1);
        for (const Row& r : rows)
        {
            SCOPED_TRACE(phase * 100 + static_cast<int>(r.dir));
            w->set_curdir(r.dir);
            const auto blocked = [phase](const Probe& p) {
                return phase == 1 ? p.dx == 1 : (phase == 2 ? p.dy == 1 : false);
            };
            EXPECT_EQ(blocked(r.right), w->stats()->right_blocked())
                << "right_blocked probes its own cell for this facing";
            EXPECT_EQ(blocked(r.right_forward), w->stats()->right_forward_blocked())
                << "right_forward_blocked probes its own cell for this facing";
            EXPECT_EQ(blocked(r.right_back), w->stats()->right_back_blocked())
                << "right_back_blocked probes its own cell for this facing";
            EXPECT_EQ(blocked(r.forward), w->stats()->forward_blocked())
                << "forward_blocked probes its own cell for this facing";
        }
    }
    set_grid_tile(fx, 5, 4, PIX_GRASS1);
    set_grid_tile(fx, 4, 5, PIX_GRASS1);
}

TEST(StatsUnit, stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_NE(nullptr, w) << "actor created";
    ASSERT_NE(nullptr, foe) << "foe created";

    // direct_walk: a straight step toward a foe that is far out of weapon reach
    // (fire_check denies with OutOfRange, so no attack is queued).
    w->setxy(64, 64);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    w->set_foe(foe);
    foe->setxy(320, 64);
    ASSERT_TRUE(w->stats()->direct_walk()) << "the straight step east is open";
    EXPECT_EQ(65, w->xpos()) << "direct_walk advances one stepsize toward the foe";
    EXPECT_EQ(64, w->ypos()) << "the dominant-axis snap zeroes the y delta";
    EXPECT_FLOAT_EQ(1.0f, w->lastx()) << "walkstep records the normalized step vector";
    EXPECT_FLOAT_EQ(0.0f, w->lasty()) << "walkstep records the normalized step vector";
    EXPECT_FALSE(w->stats()->has_commands()) << "an out-of-reach foe queues no attack";

    // hit_response from an attacker we were NOT already fighting: it becomes
    // our foe, we become its foe, and both distance trackers reset to 32000.
    w->set_foe(nullptr);
    w->stats()->set_last_distance(17);
    w->stats()->set_current_distance(17);
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(100.0f); // healthy: above the 5/16 flee threshold
    w->set_yo_delay(0);
    w->stats()->hit_response(foe);
    EXPECT_EQ(foe, w->foe()) << "hit_response retargets us at our attacker";
    EXPECT_EQ(w, foe->foe()) << "and points the attacker back at us";
    EXPECT_EQ(32000u, w->stats()->last_distance()) << "the new chase resets last_distance";
    EXPECT_EQ(32000, w->stats()->current_distance()) << "the new chase resets current_distance";
    EXPECT_EQ(0, w->yo_delay()) << "a healthy walker never yells";

    // Below the threshold the same call yells: +80 yo_delay and a forced
    // 16-round walk directly AWAY from the attacker.
    w->set_foe(nullptr);
    foe->set_foe(nullptr);
    w->stats()->set_hitpoints(1.0f);
    w->stats()->commands.clear();
    w->stats()->hit_response(foe);
    EXPECT_EQ(80, w->yo_delay()) << "the low-hitpoint branch yells exactly once (+80)";

    // yell_for_help on its own: another 80, and the flee command it forces.
    w->stats()->commands.clear();
    w->stats()->yell_for_help(foe);
    EXPECT_EQ(160, w->yo_delay()) << "each yell adds exactly 80";
    ASSERT_EQ(1u, w->stats()->commands.size()) << "yell_for_help forces one flee command";
    const command& flee = w->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, static_cast<int>(flee.commandtype)) << "the flee command is a walk";
    EXPECT_EQ(16, static_cast<int>(flee.commandcount)) << "16 rounds of running away";
    EXPECT_EQ(-1, static_cast<int>(flee.com1)) << "west, away from a foe to the east";
    EXPECT_EQ(0, static_cast<int>(flee.com2)) << "no vertical component: the foe is due east";
}
} // namespace detail_stats_coverage_push

// --- From test_stats_r11.cpp ---
namespace detail_stats_r11 {
namespace {

struct StatsFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    StatsFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &events, &cfg);
    }
};

walker* add_living(StatsFixture& fx, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(96, 96);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

int deterministic_path_check_counter_roll(std::uint32_t seed)
{
    StatsFixture fx;
    walker* actor = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    if (actor == nullptr || foe == nullptr)
        return 0;

    actor->setxy(96, 96);
    foe->setxy(112, 96);
    actor->set_foe(foe);
    actor->set_path_check_counter(0);
    fx.level.world().rng_.state_ = seed;
    (void)actor->stats()->walk_to_foe();
    return actor->path_check_counter();
}

} // namespace

TEST(StatsUnit, stats_r11_clear_command_and_blocked_direction_defaults)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* leader = add_living(fx, 0);
    ASSERT_TRUE(w != nullptr);
    ASSERT_TRUE(leader != nullptr);

    w->set_leader(leader);
    w->set_team_num(1);
    w->set_real_team_num(0);
    w->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    w->stats()->clear_command();
    ASSERT_TRUE(w->team_num() == 0);
    ASSERT_TRUE(w->real_team_num() == 255);
    ASSERT_TRUE(w->leader() == nullptr);

    w->set_curdir(127);
    ASSERT_TRUE(!w->stats()->right_forward_blocked());
    ASSERT_TRUE(!w->stats()->right_back_blocked());
}

TEST(StatsUnit, stats_r11_right_walk_branch_matrix)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    ASSERT_NE(nullptr, w) << "actor created";
    w->set_foe(nullptr);

    // The actor is a 16px box at (96,96), i.e. exactly on grid tile (6,6), so a
    // single wall tile east/north/south of it blocks exactly the one-pixel
    // probes that reach into that tile.
    const auto reset_ground = [&fx]() {
        set_grid_tile(fx, 7, 6, PIX_GRASS1);
        set_grid_tile(fx, 6, 5, PIX_GRASS1);
        set_grid_tile(fx, 6, 7, PIX_GRASS1);
    };

    // 1. Right blocked, forward open: walk forward along the normalized
    //    lastx/lasty vector (NOT along curdir).
    reset_ground();
    set_grid_tile(fx, 7, 6, PIX_H_WALL1);
    w->setxy(96, 96);
    w->set_curdir(FACE_UP);
    w->set_enddir(FACE_UP);
    w->set_lastx(0.0f);
    w->set_lasty(-2.0f);
    ASSERT_TRUE(w->stats()->right_walk()) << "the forward step is open";
    EXPECT_EQ(96, w->xpos()) << "the blocked east side is not entered";
    EXPECT_EQ(95, w->ypos()) << "right blocked + forward open walks one step forward";
    EXPECT_FLOAT_EQ(0.0f, w->lastx()) << "the step vector is normalized to the unit square";
    EXPECT_FLOAT_EQ(-1.0f, w->lasty()) << "the step vector is normalized to the unit square";
    EXPECT_EQ(FACE_UP, static_cast<int>(w->enddir())) << "no turn while we can walk forward";

    // 2. Right blocked AND forward blocked: turn left (+6 mod 8), stand still.
    reset_ground();
    set_grid_tile(fx, 7, 6, PIX_H_WALL1);
    set_grid_tile(fx, 6, 5, PIX_H_WALL1);
    w->setxy(96, 96);
    w->set_curdir(FACE_UP);
    w->set_enddir(FACE_UP);
    ASSERT_TRUE(w->stats()->right_walk()) << "right_walk reports the turn";
    EXPECT_EQ((FACE_UP + 6) % 8, static_cast<int>(w->enddir())) << "boxed in on the right: turn left";
    EXPECT_EQ(96, w->xpos()) << "a turn does not move us";
    EXPECT_EQ(96, w->ypos()) << "a turn does not move us";

    // 3. Right side open but forward blocked: the same left turn, other arm.
    //    Facing up-right, only the forward probe reaches tile (6,5).
    reset_ground();
    set_grid_tile(fx, 6, 5, PIX_H_WALL1);
    w->setxy(96, 96);
    w->set_curdir(FACE_UP_RIGHT);
    w->set_enddir(FACE_UP_RIGHT);
    ASSERT_FALSE(w->stats()->right_blocked()) << "the right side is open in this setup";
    ASSERT_FALSE(w->stats()->right_forward_blocked()) << "the right-forward is open in this setup";
    ASSERT_TRUE(w->stats()->forward_blocked()) << "only the forward probe is blocked";
    ASSERT_TRUE(w->stats()->right_walk()) << "right_walk reports the turn";
    EXPECT_EQ((FACE_UP_RIGHT + 6) % 8, static_cast<int>(w->enddir())) << "forward blocked: turn left";
    EXPECT_EQ(96, w->xpos()) << "a turn does not move us";
    EXPECT_EQ(96, w->ypos()) << "a turn does not move us";

    // 4. Only the right-BACK is blocked: turn right (+2 mod 8) and queue one
    //    COMMAND_WALK along the new enddir.
    reset_ground();
    set_grid_tile(fx, 6, 7, PIX_H_WALL1);
    w->setxy(96, 96);
    w->set_curdir(FACE_UP);
    ASSERT_TRUE(w->stats()->right_back_blocked()) << "only the right-back probe is blocked";
    ASSERT_FALSE(w->stats()->right_blocked());
    ASSERT_FALSE(w->stats()->right_forward_blocked());
    ASSERT_FALSE(w->stats()->forward_blocked());
    const int step_table[8][2] = {
        { 0, -1}, { 1, -1}, { 1,  0}, { 1,  1},
        { 0,  1}, {-1,  1}, {-1,  0}, {-1, -1},
    };
    for (int dir = 0; dir < 8; ++dir)
    {
        SCOPED_TRACE(dir);
        w->setxy(96, 96);
        w->set_curdir(FACE_UP);
        w->set_enddir(static_cast<char>(dir));
        w->stats()->commands.clear();
        ASSERT_TRUE(w->stats()->right_walk()) << "right_walk reports the turn";
        const int turned = (dir + 2) % 8;
        ASSERT_EQ(turned, static_cast<int>(w->enddir())) << "right-back blocked: turn right";
        ASSERT_EQ(1u, w->stats()->commands.size()) << "exactly one walk command is queued";
        const command& c = w->stats()->commands.front();
        EXPECT_EQ(COMMAND_WALK, static_cast<int>(c.commandtype)) << "the queued command is a walk";
        EXPECT_EQ(1, static_cast<int>(c.commandcount)) << "for a single round";
        EXPECT_EQ(step_table[turned][0], static_cast<int>(c.com1)) << "x step of the NEW enddir";
        EXPECT_EQ(step_table[turned][1], static_cast<int>(c.com2)) << "y step of the NEW enddir";
    }

    // 5. Nothing blocked and no foe: direct_walk() fails, so right_walk falls
    //    back to a single step along curdir.
    reset_ground();
    w->stats()->commands.clear();
    w->set_foe(nullptr);
    for (int dir = 0; dir < 8; ++dir)
    {
        SCOPED_TRACE(dir);
        w->setxy(96, 96);
        w->set_curdir(static_cast<char>(dir));
        w->set_enddir(static_cast<char>(dir));
        ASSERT_TRUE(w->stats()->right_walk()) << "the open-ground step succeeds";
        EXPECT_EQ(96 + step_table[dir][0], w->xpos()) << "fallback step follows curdir";
        EXPECT_EQ(96 + step_table[dir][1], w->ypos()) << "fallback step follows curdir";
        EXPECT_EQ(dir, static_cast<int>(w->enddir())) << "the fallback step never turns";
    }
}

TEST(StatsUnit, stats_r11_direct_walk_and_walk_to_foe_tail_branches)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_NE(nullptr, w) << "actor created";
    ASSERT_NE(nullptr, foe) << "foe created";

    // direct_walk with no foe at all walks nowhere and reports failure.
    w->setxy(96, 96);
    w->set_foe(nullptr);
    ASSERT_FALSE(w->stats()->direct_walk()) << "no foe: direct_walk is a no-op";
    EXPECT_EQ(96, w->xpos()) << "no foe: direct_walk must not move us";

    // Foe dead and no other foe inside PATHING_MIN_DISTANCE: walk_to_foe zeroes
    // the front command's count but leaves the entry in place for do_command to
    // pop. The foe is 50px away: inside the 100px short-circuit radius, outside
    // the 30px "really close" tail, so only the no-nearby-foe branch can fire.
    w->set_foe(foe);
    foe->setxy(146, 96);
    foe->set_dead(1);
    w->stats()->force_command(COMMAND_WALK, 5, 1, 0);
    w->set_path_check_counter(0);   // force the pathing check this round
    fx.level.world().rng_.state_ = 1;
    ASSERT_TRUE(w->stats()->walk_to_foe()) << "walk_to_foe ran";
    ASSERT_EQ(1u, w->stats()->commands.size()) << "the command is zeroed, not popped";
    EXPECT_EQ(COMMAND_WALK, static_cast<int>(w->stats()->commands.front().commandtype))
        << "and it is still the walk we queued";
    EXPECT_EQ(0, static_cast<int>(w->stats()->commands.front().commandcount))
        << "no live foe nearby: the front command's count is zeroed";

    // A LIVE foe inside PATHING_MIN_DISTANCE takes the melee short-circuit: the
    // queue is cleared and replaced by a single attack command.
    foe->set_dead(0);
    foe->setxy(100, 96);
    w->set_foe(foe);
    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_SEARCH, 5, 0, 0);
    w->set_path_check_counter(0);
    fx.level.world().rng_.state_ = 1;
    ASSERT_TRUE(w->stats()->walk_to_foe()) << "walk_to_foe ran";
    ASSERT_EQ(1u, w->stats()->commands.size()) << "the old queue is replaced, not appended to";
    EXPECT_EQ(COMMAND_ATTACK, static_cast<int>(w->stats()->commands.front().commandtype))
        << "a live foe within 100px is attacked instead of walked to";
    EXPECT_EQ(foe, w->foe()) << "and it stays our foe";
}

TEST(StatsUnit, stats_r11_path_check_counter_roll_is_seed_deterministic)
{
    const int first = deterministic_path_check_counter_roll(1u);
    const int second = deterministic_path_check_counter_roll(1u);
    const int third = deterministic_path_check_counter_roll(2u);

    ASSERT_EQ(11, first);
    ASSERT_EQ(first, second);
    ASSERT_EQ(12, third);
    ASSERT_NE(first, third);
}
} // namespace detail_stats_r11

// --- From test_stats_r12.cpp ---
namespace detail_stats_r12 {
namespace {

struct StatsR12Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    StatsR12Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &events, &cfg);
    }
};

walker* add_living(StatsR12Fixture& fx, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(96, 96);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(StatsUnit, stats_r12_command_and_hit_response_branches)
{
    StatsR12Fixture fx;
    walker* self = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_NE(nullptr, self) << "actor created";
    ASSERT_NE(nullptr, foe) << "foe created";
    self->setxy(96, 96);
    foe->setxy(400, 96);

    // add_command appends and force_command prepends, both clamping a walk
    // delta into the unit square and marking the forced entry.
    self->stats()->add_command(COMMAND_WALK, 1, 9, -9);
    ASSERT_EQ(1u, self->stats()->commands.size()) << "add_command appended";
    EXPECT_EQ(1, static_cast<int>(self->stats()->commands.back().com1)) << "+9 clamps to +1";
    EXPECT_EQ(-1, static_cast<int>(self->stats()->commands.back().com2)) << "-9 clamps to -1";
    self->stats()->force_command(COMMAND_WALK, 1, -9, 9);
    ASSERT_EQ(2u, self->stats()->commands.size()) << "force_command prepended";
    EXPECT_EQ(-1, static_cast<int>(self->stats()->commands.front().com1)) << "-9 clamps to -1";
    EXPECT_EQ(1, static_cast<int>(self->stats()->commands.front().com2)) << "+9 clamps to +1";
    EXPECT_TRUE(self->stats()->commands.front().forced) << "force_command marks the entry forced";
    EXPECT_FALSE(self->stats()->commands.back().forced) << "add_command does not";

    // COMMAND_FIRE with no foe: fire_check denies, the count is zeroed and
    // do_command reports failure.
    self->stats()->commands.clear();
    self->set_foe(nullptr);
    self->stats()->force_command(COMMAND_FIRE, 5, 1, 0);
    ASSERT_EQ(0, self->stats()->do_command()) << "FIRE without a foe fails";
    EXPECT_FALSE(self->stats()->has_commands()) << "the zeroed FIRE command is popped";

    // COMMAND_RIGHT_WALK with a foe 150px away (the 120..240 band) hands the
    // round to right_walk, whose boxed-in signature is a left turn on the spot.
    set_grid_tile(fx, 7, 6, PIX_H_WALL1);   // east of tile (6,6)
    set_grid_tile(fx, 6, 5, PIX_H_WALL1);   // north of tile (6,6)
    self->setxy(96, 96);
    self->set_curdir(FACE_UP);
    self->set_enddir(FACE_UP);
    self->set_foe(foe);
    foe->setxy(246, 96);
    self->stats()->force_command(COMMAND_RIGHT_WALK, 2, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "RIGHT_WALK reports success";
    EXPECT_EQ((FACE_UP + 6) % 8, static_cast<int>(self->enddir()))
        << "right and forward blocked: right_walk turns left";
    EXPECT_EQ(96, self->xpos()) << "the turn does not move us";
    set_grid_tile(fx, 7, 6, PIX_GRASS1);
    set_grid_tile(fx, 6, 5, PIX_GRASS1);

    // COMMAND_SEARCH with a live foe inside the 100px pathing radius walks to
    // the foe, which replaces the queue with an attack command.
    foe->setxy(150, 96);
    self->stats()->commands.clear();
    self->stats()->force_command(COMMAND_SEARCH, 2, 0, 0);
    self->set_path_check_counter(0);
    fx.level.world().rng_.state_ = 1;   // rng(300) != 0: walk_to_foe does not bail
    ASSERT_EQ(1, self->stats()->do_command()) << "SEARCH reports success";
    ASSERT_EQ(1u, self->stats()->commands.size()) << "the SEARCH command was replaced";
    EXPECT_EQ(COMMAND_ATTACK, static_cast<int>(self->stats()->commands.front().commandtype))
        << "SEARCH with a live foe hands off to walk_to_foe";

    // COMMAND_SEARCH with a DEAD foe stops trying: the count is zeroed and the
    // command is popped.
    foe->set_dead(1);
    self->stats()->commands.clear();
    self->stats()->force_command(COMMAND_SEARCH, 5, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "SEARCH still reports success";
    EXPECT_FALSE(self->stats()->has_commands()) << "a dead foe zeroes the SEARCH count";
    foe->set_dead(0);

    // hit_response below 5/16 hitpoints yells: +80 yo_delay and a forced
    // 16-round run away from the attacker.
    self->set_foe(foe);
    foe->set_foe(self);
    self->stats()->set_max_hitpoints(100.0f);
    self->stats()->set_hitpoints(1.0f);
    self->set_yo_delay(0);
    self->stats()->commands.clear();
    self->stats()->hit_response(foe);
    EXPECT_EQ(80, self->yo_delay()) << "hit_response at 1/100 hp yells exactly once";
    ASSERT_EQ(1u, self->stats()->commands.size()) << "the yell forces one flee command";
    EXPECT_EQ(COMMAND_WALK, static_cast<int>(self->stats()->commands.front().commandtype));
    EXPECT_EQ(16, static_cast<int>(self->stats()->commands.front().commandcount))
        << "16 rounds of fleeing";
    EXPECT_EQ(-1, static_cast<int>(self->stats()->commands.front().com1))
        << "away from a foe to the east";

    // Bit flags are a mask: setting one leaves the others clear.
    self->stats()->clear_bit_flags();
    EXPECT_EQ(0, self->stats()->query_bit_flags(BIT_FLYING)) << "cleared";
    self->stats()->set_bit_flags(BIT_FLYING, 1);
    EXPECT_EQ(BIT_FLYING, self->stats()->query_bit_flags(BIT_FLYING)) << "flying set";
    EXPECT_EQ(0, self->stats()->query_bit_flags(BIT_NO_RANGED)) << "only flying was set";
    self->stats()->set_bit_flags(BIT_FLYING, 0);
    EXPECT_EQ(0, self->stats()->query_bit_flags(BIT_FLYING)) << "flying cleared again";
}

TEST(StatsUnit, special_cost_out_of_range_is_safe)
{
    // special_cost_/set_special_cost index a fixed NUM_SPECIALS array. Out-of-
    // range indices (e.g. from corrupt snapshot/save data) must not read or
    // write past the array; the getter returns 0 and the setter is a no-op.
    statistics s(nullptr);

    s.set_special_cost(0, 11);
    s.set_special_cost(NUM_SPECIALS - 1, 22);
    EXPECT_EQ(11, (int)s.special_cost(0)) << "in-range get/set still works";
    EXPECT_EQ(22, (int)s.special_cost(NUM_SPECIALS - 1)) << "last valid index";

    EXPECT_EQ(0, (int)s.special_cost(-1)) << "negative index returns 0";
    EXPECT_EQ(0, (int)s.special_cost(NUM_SPECIALS)) << "one past end returns 0";
    EXPECT_EQ(0, (int)s.special_cost(100000)) << "far out of range returns 0";

    // Out-of-range setters are no-ops and must not corrupt valid entries.
    s.set_special_cost(-1, 99);
    s.set_special_cost(NUM_SPECIALS, 99);
    s.set_special_cost(100000, 99);
    EXPECT_EQ(11, (int)s.special_cost(0)) << "valid entry untouched by OOB set";
    EXPECT_EQ(22, (int)s.special_cost(NUM_SPECIALS - 1)) << "valid entry untouched by OOB set";
}

TEST(StatsUnit, stats_r12_extra_command_switch_and_null_controller_paths)
{
    StatsR12Fixture fx;
    walker* self = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_NE(nullptr, self) << "actor created";
    ASSERT_NE(nullptr, foe) << "foe created";

    // Null-controller constructor + do_command early return.
    statistics null_stats(nullptr);
    ASSERT_EQ(0, null_stats.do_command()) << "a controller-less statistics does nothing";

    self->set_default_weapon(FAMILY_KNIFE);
    self->set_current_weapon(FAMILY_ARROW);

    // COMMAND_SET_WEAPON / COMMAND_RESET_WEAPON.
    self->stats()->force_command(COMMAND_SET_WEAPON, 1, FAMILY_FIREBALL, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "SET_WEAPON succeeds";
    EXPECT_EQ(FAMILY_FIREBALL, self->current_weapon()) << "com1 becomes the current weapon";
    EXPECT_FALSE(self->stats()->has_commands()) << "the one-round command is popped";

    self->stats()->force_command(COMMAND_RESET_WEAPON, 1, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "RESET_WEAPON succeeds";
    EXPECT_EQ(self->default_weapon(), self->current_weapon()) << "the default weapon is restored";

    // COMMAND_DIE marks us for deletion on its last round.
    self->set_dead(0);
    self->stats()->set_delete_me(0);
    self->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "DIE succeeds";
    EXPECT_EQ(1, self->stats()->delete_me()) << "the last DIE round marks us deleted";

    // COMMAND_FOLLOW while we have a foe: the follow is abandoned this round —
    // the leader is dropped and the command reports failure.
    self->set_foe(foe);
    self->set_leader(foe);
    self->stats()->force_command(COMMAND_FOLLOW, 3, 0, 0);
    ASSERT_EQ(0, self->stats()->do_command()) << "FOLLOW with a foe fails";
    EXPECT_EQ(nullptr, self->leader()) << "and releases the leader";
    EXPECT_FALSE(self->stats()->has_commands()) << "the zeroed command is popped";

    // COMMAND_FOLLOW with nobody to follow (headless find_follow_leader is null).
    self->set_foe(nullptr);
    self->set_leader(nullptr);
    self->stats()->force_command(COMMAND_FOLLOW, 3, 0, 0);
    ASSERT_EQ(0, self->stats()->do_command()) << "FOLLOW without a leader fails";
    EXPECT_EQ(nullptr, self->leader()) << "no leader was found";
    EXPECT_FALSE(self->stats()->has_commands()) << "the zeroed command is popped";

    // COMMAND_UNCHARM and an unknown command type both fall through the switch
    // and are consumed by the count/pop tail.
    self->stats()->force_command(COMMAND_UNCHARM, 1, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "UNCHARM is a no-op that succeeds";
    EXPECT_FALSE(self->stats()->has_commands()) << "and is consumed";
    self->stats()->force_command(9999, 1, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "an unknown command type still succeeds";
    EXPECT_FALSE(self->stats()->has_commands()) << "and is consumed";
}
} // namespace detail_stats_r12

// --- From test_stats_r14.cpp ---
namespace detail_stats_r14 {
namespace {

struct StatsR14Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;
    GameContext gc;

    StatsR14Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &events, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
    }

    ~StatsR14Fixture()
    {
        pop_test_context();
    }
};

walker* add_living(StatsR14Fixture& fx, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(x, y);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_weapon(StatsR14Fixture& fx, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Weapon, FAMILY_ARROW);
    w->set_sizex(8);
    w->set_sizey(8);
    w->set_stepsize(1.0f);
    w->setxy(x, y);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().weaplist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(StatsUnit, stats_r14_lines_122_133_135_155_161_add_force_walk_clamps)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 96, 96);
    ASSERT_TRUE(self != nullptr);

    self->stats()->add_command(COMMAND_FOLLOW, 1, 0, 0);
    self->stats()->add_command(COMMAND_WALK, 1, -9, 9);
    ASSERT_TRUE(!self->stats()->commands.empty());
    auto& back = self->stats()->commands.back();
    ASSERT_TRUE(back.com1 == -1);
    ASSERT_TRUE(back.com2 == 1);

    self->stats()->force_command(COMMAND_WALK, 1, 9, -9);
    ASSERT_TRUE(!self->stats()->commands.empty());
    auto& front = self->stats()->commands.front();
    ASSERT_TRUE(front.com1 == 1);
    ASSERT_TRUE(front.com2 == -1);
}

TEST(StatsUnit, stats_r14_command_switch_arms_each_move_their_own_state)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 96, 96);
    walker* foe = add_living(fx, 1, 220, 96);
    walker* lead = add_living(fx, 0, 300, 96);
    ASSERT_NE(nullptr, self) << "actor created";
    ASSERT_NE(nullptr, foe) << "foe created";
    ASSERT_NE(nullptr, lead) << "leader created";

    // walk() only moves along the current facing, so face east once up front.
    self->set_curdir(FACE_RIGHT);
    self->set_enddir(FACE_RIGHT);
    self->set_lastx(1.0f);
    self->set_lasty(0.0f);
    self->set_foe(nullptr);

    // COMMAND_WALK walksteps by its own (com1,com2).
    self->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "WALK succeeds";
    EXPECT_EQ(97, self->xpos()) << "WALK steps one stepsize east";
    EXPECT_FALSE(self->stats()->has_commands()) << "the one-round command is popped";

    // COMMAND_FIRE from a NON-living order bails out of the switch without
    // firing, but still reports success and consumes the command.
    self->set_order_family(Order::Weapon, FAMILY_ARROW);
    self->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "a non-living FIRE breaks with success";
    EXPECT_EQ(97, self->xpos()) << "and nothing moved";
    EXPECT_FALSE(self->stats()->has_commands()) << "the command is consumed";
    self->set_order_family(Order::Living, FAMILY_SOLDIER);

    // COMMAND_FOLLOW steps toward a distant leader and, on its last round,
    // releases the leader.
    self->set_leader(lead);
    self->set_foe(nullptr);
    self->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "FOLLOW succeeds";
    EXPECT_EQ(98, self->xpos()) << "FOLLOW steps one stepsize toward the leader";
    EXPECT_EQ(nullptr, self->leader()) << "the last round releases the leader";

    // COMMAND_ATTACK on a foe far out of reach walks toward it.
    self->set_foe(foe);
    self->stats()->force_command(COMMAND_ATTACK, 1, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "ATTACK succeeds";
    EXPECT_EQ(99, self->xpos()) << "an out-of-reach foe is walked toward";

    // COMMAND_QUICK_FIRE walks first, then fires.
    self->stats()->force_command(COMMAND_QUICK_FIRE, 1, 1, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "QUICK_FIRE succeeds";
    EXPECT_EQ(100, self->xpos()) << "QUICK_FIRE walksteps by its own (com1,com2) first";

    // COMMAND_SEARCH with no foe at all stops searching: count zeroed, popped.
    self->set_foe(nullptr);
    self->stats()->force_command(COMMAND_SEARCH, 5, 0, 0);
    ASSERT_EQ(1, self->stats()->do_command()) << "SEARCH still reports success";
    EXPECT_FALSE(self->stats()->has_commands()) << "no foe zeroes the SEARCH count";
}

TEST(StatsUnit, stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 96, 96);
    walker* foe = add_living(fx, 1, 200, 96);
    walker* owner = add_living(fx, 1, 300, 96);
    walker* proj = add_weapon(fx, 1, 250, 96);
    ASSERT_NE(nullptr, self) << "actor created";
    ASSERT_NE(nullptr, foe) << "foe created";
    ASSERT_NE(nullptr, owner) << "weapon owner created";
    ASSERT_NE(nullptr, proj) << "projectile created";

    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    self->myguy->name = "R14";
    self->stats()->set_max_hitpoints(100.0f);
    self->stats()->set_hitpoints(100.0f);   // healthy: the yell branch stays shut
    self->set_yo_delay(0);

    // A player-controlled walker never answers a hit: hit_response returns
    // before it can retarget or yell.
    self->set_foe(nullptr);
    self->set_act_type(ACT_CONTROL);
    self->stats()->hit_response(foe);
    EXPECT_EQ(nullptr, self->foe()) << "an ACT_CONTROL walker does not retarget itself";
    EXPECT_EQ(0, self->yo_delay()) << "and does not yell";

    // Hit by a WEAPON, we retaliate against its owner, not the projectile.
    self->set_act_type(ACT_RANDOM);
    proj->set_owner(owner);
    self->stats()->set_last_distance(17);
    self->stats()->set_current_distance(17);
    self->stats()->hit_response(proj);
    EXPECT_EQ(owner, self->foe()) << "the weapon's owner becomes our foe, not the projectile";
    EXPECT_EQ(self, owner->foe()) << "and we become its foe";
    EXPECT_EQ(32000u, self->stats()->last_distance()) << "the new chase resets last_distance";
    EXPECT_EQ(32000, self->stats()->current_distance()) << "the new chase resets current_distance";
    EXPECT_EQ(0, self->yo_delay()) << "a healthy walker takes the hit without yelling";

    // The same hit below 5/16 hitpoints yells instead: +80 yo_delay and a
    // forced run away from the owner. (The attacker is already our foe, so
    // only the yell can fire on this call.)
    self->stats()->set_hitpoints(1.0f);
    self->stats()->commands.clear();
    self->stats()->hit_response(proj);
    EXPECT_EQ(80, self->yo_delay()) << "at 1/100 hitpoints the hit makes us yell (+80)";
    ASSERT_EQ(1u, self->stats()->commands.size()) << "the yell forces one flee command";
    EXPECT_EQ(COMMAND_WALK, static_cast<int>(self->stats()->commands.front().commandtype))
        << "the flee command is a walk";
    EXPECT_EQ(16, static_cast<int>(self->stats()->commands.front().commandcount))
        << "16 rounds of running away";
    EXPECT_EQ(-1, static_cast<int>(self->stats()->commands.front().com1))
        << "west, away from an attacker to the east";

    // An out-of-range facing byte selects the default arm of every probe
    // switch, which reads our own cell and is therefore never blocked.
    self->set_curdir(127);
    self->set_enddir(127);
    EXPECT_FALSE(self->stats()->right_blocked()) << "default facing probes nothing";
    EXPECT_FALSE(self->stats()->right_forward_blocked()) << "default facing probes nothing";
    EXPECT_FALSE(self->stats()->right_back_blocked()) << "default facing probes nothing";
    EXPECT_FALSE(self->stats()->forward_blocked()) << "default facing probes nothing";

    // A foe standing exactly on top of us has no direction to walk in.
    self->set_foe(foe);
    self->setxy(96, 96);
    foe->setxy(96, 96);
    ASSERT_FALSE(self->stats()->direct_walk()) << "a coincident foe yields no step";
    EXPECT_EQ(96, self->xpos()) << "and no movement";

    // A distant foe does: one stepsize along the straight line to it.
    self->set_curdir(FACE_RIGHT);
    self->set_enddir(FACE_RIGHT);
    foe->setxy(400, 96);
    ASSERT_TRUE(self->stats()->direct_walk()) << "the straight step east is open";
    EXPECT_EQ(97, self->xpos()) << "direct_walk steps toward the foe";

    // walk_to_foe with the pathing check still on cooldown skips straight to
    // the right-hand fallback, which walks us one more step.
    self->stats()->set_last_distance(10);
    self->stats()->set_current_distance(10);
    self->set_path_check_counter(5);
    fx.level.world().rng_.state_ = 1;   // rng(300) != 0: walk_to_foe does not bail
    ASSERT_TRUE(self->stats()->walk_to_foe()) << "walk_to_foe ran";
    EXPECT_EQ(4, self->path_check_counter()) << "the throttle counter ticks down by one";
    EXPECT_EQ(98, self->xpos()) << "the fallback still closes on the foe";
}
} // namespace detail_stats_r14
