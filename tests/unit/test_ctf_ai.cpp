// Capture-the-flag AI director tests: exact role partition (carrier,
// interceptor, defender, attacker, escort), the COMMAND_GOTO / walk_to_point
// machinery it drives, the goal-aware pathfinder, behavioral matches in open
// arenas, and the classic-world no-op proof.
//
// Role boundaries under test (documented contract of ctf_run_ai_director):
//   - eligibility: alive Order::Living, team active, act_type != ACT_CONTROL,
//     user() == -1, stats present, frozen_delay <= 0; everyone else untouched.
//   - CARRIER: every member carrying an enemy flag. leader = own flag entity;
//     front command becomes COMMAND_GOTO(60) to the own flag HOME each cadence.
//   - INTERCEPTOR: only while the own flag is Carried; the 2 nearest
//     unassigned members by Manhattan distance to the enemy carrier (ties to
//     the earlier oblist slot). foe = enemy carrier; front COMMAND_SEARCH(120).
//   - RETRIEVER: only while the own flag is Dropped; the nearest unassigned
//     member gets front COMMAND_GOTO(45) to the flag's drop position (a
//     friendly touch returns the flag home instantly).
//   - DEFENDER: first ceil(remaining/3) members in oblist order; front
//     COMMAND_GOTO(45) to the own flag home iff more than 96px (Manhattan)
//     away from it, otherwise left alone.
//   - ATTACKER/ESCORT: the rest in oblist order, alternating from attacker;
//     odd slots escort while the team has a carrier (leader = first carrier,
//     foe cleared, front COMMAND_FOLLOW(100)). Attackers get COMMAND_GOTO(60)
//     to the nearest enemy flag's CURRENT position when their queue is idle,
//     and the order also preempts a front COMMAND_SEARCH (the act_random
//     self-issued wander-chase) or refreshes a stale front COMMAND_GOTO;
//     active combat commands are never preempted. Flags secured by the own
//     team (carried by a teammate) are not attack targets.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>

#include "test_game_world_fixture.h"

#include <cstdlib>
#include <format>
#include <string>
#include <vector>

namespace {

loader& ctf_ai_test_loader()
{
    static loader instance{EntityFactory{}};
    static const bool registered = [] {
        register_ctf_loader_entries(instance);
        return true;
    }();
    (void)registered;
    return instance;
}

// TestGameWorld with the CTF loader entries wired in, mirroring the fixture
// in test_ctf_core.cpp so flags and bots run the production factory path.
struct CtfAiWorld : TestGameWorld
{
    explicit CtfAiWorld(int level_id = 500)
        : TestGameWorld(level_id)
    {
        loader* game_loader = &ctf_ai_test_loader();
        world().entity_factory =
            [game_loader](Order order, std::int32_t family) {
                return game_loader->create_walker_owned(order, family);
            };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
                game_loader->set_walker(&entity, order, family);
                return game_loader->graphics_for(entity.query_order(),
                                                 entity.family());
            };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
        world().type = GameWorld::TYPE_CTF;
    }

    walker* spawn_flag(int team, int x, int y)
    {
        walker* flag = world().add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
        if (flag == nullptr)
            return nullptr;
        flag->setxy(x, y);
        flag->set_team_num(static_cast<unsigned char>(team));
        return flag;
    }

    walker* spawn_anchor(int team, int x, int y)
    {
        walker* marker = world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (marker == nullptr)
            return nullptr;
        marker->setxy(x, y);
        marker->set_team_num(static_cast<unsigned char>(team));
        return marker;
    }

    walker* spawn_living(int family, int team, int x, int y, int act_type)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(x, y);
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(static_cast<short>(act_type));
        return w;
    }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }
};

int manhattan(const walker* w, int x, int y)
{
    return std::abs(static_cast<int>(w->xpos()) - x) +
           std::abs(static_cast<int>(w->ypos()) - y);
}

bool front_command_is(const walker* w, std::int32_t type, std::int32_t com1,
                      std::int32_t com2)
{
    const statistics* s = w->stats();
    if (s == nullptr || s->commands.empty())
        return false;
    const command& front = s->commands.front();
    return front.commandtype == type && front.com1 == com1 &&
           front.com2 == com2;
}

bool queue_contains(const walker* w, std::int32_t type)
{
    if (w->stats() == nullptr)
        return false;
    for (const command& c : w->stats()->commands)
    {
        if (c.commandtype == type)
            return true;
    }
    return false;
}

// Full behavior digest: positions, targeting links, and command queues of
// every oblist walker plus the world RNG and the CTF score state. Two worlds
// fed identical scripts must produce identical digests.
std::string digest_ai_world(GameWorld& world)
{
    std::string digest = std::format("rng={} tick={} caps={},{},{},{}|",
                                     world.rng_.state_, world.tick_count_,
                                     world.ctf.captures[0],
                                     world.ctf.captures[1],
                                     world.ctf.captures[2],
                                     world.ctf.captures[3]);
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr)
            continue;
        digest += std::format("[id={} x={} y={} dead={} act={} lead={} foe={}",
                              w->entity_id(), w->xpos(), w->ypos(),
                              w->dead() ? 1 : 0,
                              static_cast<int>(w->act_type()), w->leader_id(),
                              w->foe_id());
        if (w->stats() != nullptr)
        {
            digest += std::format(" q={}", w->stats()->commands.size());
            for (const command& c : w->stats()->commands)
            {
                digest += std::format(" ({},{},{},{})", c.commandtype,
                                      c.commandcount, c.com1, c.com2);
            }
        }
        digest += "]";
    }
    return digest;
}

void build_wall_column(GameWorld& world, int grid_x, int grid_y_begin,
                       int grid_y_end)
{
    PixieData& grid = world.grid;
    for (int gy = grid_y_begin; gy <= grid_y_end; ++gy)
        grid.data[grid_x + grid.w * gy] = PIX_H_WALL1;
}

} // namespace

// --- walk_to_point / COMMAND_GOTO ------------------------------------------

// ACT_SIT puppets never self-issue commands or chase foes, so the only
// movement below is the forced COMMAND_GOTO under test.
TEST(CtfAi, walk_to_point_crosses_open_map_and_completes)
{
    CtfAiWorld fx;
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96, ACT_SIT);
    ASSERT_NE(nullptr, runner);
    fx.tick(1); // CTF init (no flags: stays inactive; the gate is irrelevant here)

    const short target_x = 416;
    const short target_y = 96;
    ASSERT_GE(manhattan(runner, target_x, target_y), 100)
        << "target must be far enough to engage the real pathfinder";
    runner->stats()->force_command(COMMAND_GOTO, 200, target_x, target_y);

    int arrival_tick = -1;
    for (int i = 0; i < 300; ++i)
    {
        fx.tick();
        if (!runner->stats()->has_commands())
        {
            arrival_tick = i;
            break;
        }
    }
    ASSERT_GE(arrival_tick, 0) << "COMMAND_GOTO must complete";
    ASSERT_LT(manhattan(runner, target_x, target_y), 30)
        << "completion only inside the arrival radius";
    ASSERT_GT(runner->xpos(), 300) << "the walker actually crossed the map";
}

TEST(CtfAi, walk_to_point_routes_around_wall)
{
    CtfAiWorld fx;
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 160, ACT_SIT);
    ASSERT_NE(nullptr, runner);
    // Wall column at grid x=12 (pixels 192..207) from the map top down to
    // grid y=14: the straight line from (96,160) to (304,160) is blocked and
    // the only way around is below y=240.
    build_wall_column(fx.world(), 12, 0, 14);
    fx.tick(1);

    const short target_x = 304;
    const short target_y = 160;
    runner->stats()->force_command(COMMAND_GOTO, 500, target_x, target_y);

    bool detoured = false;
    int arrival_tick = -1;
    for (int i = 0; i < 500; ++i)
    {
        fx.tick();
        if (runner->ypos() >= 240)
            detoured = true;
        if (!runner->stats()->has_commands())
        {
            arrival_tick = i;
            break;
        }
    }
    ASSERT_GE(arrival_tick, 0) << "blocked GOTO must still arrive";
    ASSERT_LT(manhattan(runner, target_x, target_y), 30);
    ASSERT_TRUE(detoured) << "the only open route passes below the wall";
}

TEST(CtfAi, find_path_to_point_solves_around_wall)
{
    CtfAiWorld fx;
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 160, ACT_SIT);
    ASSERT_NE(nullptr, runner);
    build_wall_column(fx.world(), 12, 0, 14);

    runner->find_path_to_point(304, 160);
    ASSERT_FALSE(runner->path_to_foe.empty()) << "a route around the wall exists";

    bool touches_wall = false;
    for (const MicroPatherState state : runner->path_to_foe)
    {
        const int gx = GET_STATE_X(state) / GRID_SIZE;
        const int gy = GET_STATE_Y(state) / GRID_SIZE;
        if (gx == 12 && gy <= 14)
            touches_wall = true;
    }
    ASSERT_FALSE(touches_wall) << "no path node may sit on a wall tile";

    const MicroPatherState last = runner->path_to_foe.back();
    ASSERT_EQ(304 / GRID_SIZE, GET_STATE_X(last) / GRID_SIZE);
    ASSERT_EQ(160 / GRID_SIZE, GET_STATE_Y(last) / GRID_SIZE);

    // The goal is cleared after every point solve: a foe solve right after
    // must still produce a path toward the foe (the classic chase shape).
    walker* foe = fx.spawn_living(FAMILY_SOLDIER, 1, 96, 480, ACT_SIT);
    ASSERT_NE(nullptr, foe);
    runner->set_foe(foe);
    runner->find_path_to_foe();
    ASSERT_FALSE(runner->path_to_foe.empty());
    const MicroPatherState foe_last = runner->path_to_foe.back();
    ASSERT_EQ(480 / GRID_SIZE, GET_STATE_Y(foe_last) / GRID_SIZE);
}

// --- Director role partition (scripted, director invoked directly) ---------

TEST(CtfAi, director_partitions_roles_exactly)
{
    CtfAiWorld fx;
    walker* flag0 = fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* flag2 = fx.spawn_flag(2, 544, 96);
    ASSERT_NE(nullptr, flag0);
    ASSERT_NE(nullptr, flag1);
    ASSERT_NE(nullptr, flag2);
    fx.world().ctf_requested_team_count = 3;
    // Oblist order fixes the partition: m0 carrier, m1/m2 defenders (ceil(5/3)
    // = 2 of the 5 remaining), then attacker m3, escort m4, attacker m5.
    walker* m0 = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_SIT);
    walker* m1 = fx.spawn_living(FAMILY_SOLDIER, 0, 256, 256, ACT_SIT);
    walker* m2 = fx.spawn_living(FAMILY_SOLDIER, 0, 128, 128, ACT_SIT);
    walker* m3 = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320, ACT_SIT);
    walker* m4 = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 320, ACT_SIT);
    walker* m5 = fx.spawn_living(FAMILY_SOLDIER, 0, 384, 320, ACT_SIT);
    walker* enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700, ACT_CONTROL);
    fx.spawn_living(FAMILY_SOLDIER, 2, 500, 150, ACT_CONTROL);
    ASSERT_NE(nullptr, enemy);

    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_EQ(3, fx.world().ctf.team_count);
    ASSERT_TRUE(flag2->eat_me(m0)) << "m0 picks up team 2's flag";
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[2].state);

    og::sim::ctf_run_ai_director(fx.world());

    // CARRIER: leader is the OWN flag entity, ordered home.
    ASSERT_EQ(flag0, m0->leader());
    ASSERT_TRUE(front_command_is(m0, COMMAND_GOTO, 96, 96));
    ASSERT_EQ(60, m0->stats()->commands.front().commandcount);

    // DEFENDERS: m1 is far from home (>96px Manhattan) so it is sent back;
    // m2 sits within the 96px leash and is left alone.
    ASSERT_GT(manhattan(m1, 96, 96), 96);
    ASSERT_TRUE(front_command_is(m1, COMMAND_GOTO, 96, 96));
    ASSERT_EQ(45, m1->stats()->commands.front().commandcount);
    ASSERT_LE(manhattan(m2, 96, 96), 96);
    ASSERT_FALSE(m2->stats()->has_commands());

    // ATTACKERS: idle queues, so the nearest enemy flag's CURRENT position
    // is issued. Team 2's flag is excluded — it is secured (carried by the
    // own team) — so both attackers head for team 1's flag at its home spot.
    ASSERT_TRUE(front_command_is(m3, COMMAND_GOTO, 544, 800));
    ASSERT_EQ(60, m3->stats()->commands.front().commandcount);
    ASSERT_TRUE(front_command_is(m5, COMMAND_GOTO, 544, 800));

    // ESCORT: the 2nd would-be attacker shadows the carrier.
    ASSERT_EQ(m0, m4->leader());
    ASSERT_EQ(nullptr, m4->foe());
    ASSERT_TRUE(front_command_is(m4, COMMAND_FOLLOW, 0, 0));
    ASSERT_EQ(100, m4->stats()->commands.front().commandcount);

    // The enemy player walker is never touched.
    ASSERT_FALSE(enemy->stats()->has_commands());

    // Re-running the director refreshes the same front commands in place
    // instead of growing the queues.
    og::sim::ctf_run_ai_director(fx.world());
    ASSERT_EQ(1u, m0->stats()->commands.size());
    ASSERT_EQ(1u, m1->stats()->commands.size());
    ASSERT_EQ(1u, m3->stats()->commands.size());
    ASSERT_EQ(1u, m4->stats()->commands.size());
}

TEST(CtfAi, director_targets_enemy_carried_flag_at_its_live_position)
{
    CtfAiWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    fx.spawn_flag(2, 544, 96);
    fx.world().ctf_requested_team_count = 3;
    // Two team-0 members: defender (first) and attacker (second).
    fx.spawn_living(FAMILY_SOLDIER, 0, 128, 128, ACT_SIT);
    walker* attacker = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700, ACT_CONTROL);
    walker* carrier = fx.spawn_living(FAMILY_SOLDIER, 2, 480, 300, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_EQ(3, fx.world().ctf.team_count);

    // Team 2 steals team 1's flag: the flag's current position is wherever
    // its carrier stands. From the attacker, that carried flag (340px) is
    // nearer than team 2's home flag (608px), so the attacker's goto
    // converges on the rival carrier — the emergent interception.
    ASSERT_TRUE(flag1->eat_me(carrier));
    fx.tick(1); // run_carried_flags pins the flag to the carrier's position
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[1].state);
    ASSERT_EQ(carrier->xpos(), fx.world().ctf.flags[1].x);

    og::sim::ctf_run_ai_director(fx.world());
    ASSERT_TRUE(front_command_is(attacker, COMMAND_GOTO,
                                 fx.world().ctf.flags[1].x,
                                 fx.world().ctf.flags[1].y));
}

TEST(CtfAi, director_sends_nearest_retriever_to_dropped_own_flag)
{
    CtfAiWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* thief = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_SIT);
    walker* far_member = fx.spawn_living(FAMILY_SOLDIER, 1, 544, 96, ACT_SIT);
    walker* near_member = fx.spawn_living(FAMILY_SOLDIER, 1, 416, 480, ACT_SIT);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);

    // Steal team 1's flag, then kill the thief mid-map: the flag drops there.
    ASSERT_TRUE(flag1->eat_me(thief));
    thief->set_dead(1);
    fx.tick(1);
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    const short drop_x = fx.world().ctf.flags[1].x;
    const short drop_y = fx.world().ctf.flags[1].y;

    og::sim::ctf_run_ai_director(fx.world());

    // The nearest member is tasked to touch it (instant return); the other
    // member keeps its defender duties.
    ASSERT_TRUE(front_command_is(near_member, COMMAND_GOTO, drop_x, drop_y));
    ASSERT_EQ(45, near_member->stats()->commands.front().commandcount);
    ASSERT_TRUE(front_command_is(far_member, COMMAND_GOTO, 544, 800))
        << "the remaining member defends the (empty) flag home";
}

TEST(CtfAi, director_sends_two_nearest_interceptors_after_enemy_carrier)
{
    CtfAiWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* thief = fx.spawn_living(FAMILY_SOLDIER, 0, 480, 736, ACT_SIT);
    // Team 1 members at increasing distance from the thief.
    walker* near1 = fx.spawn_living(FAMILY_SOLDIER, 1, 480, 800, ACT_SIT);
    walker* near2 = fx.spawn_living(FAMILY_SOLDIER, 1, 416, 800, ACT_SIT);
    walker* far1 = fx.spawn_living(FAMILY_SOLDIER, 1, 96, 800, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_TRUE(flag1->eat_me(thief));

    og::sim::ctf_run_ai_director(fx.world());

    // The 2 nearest by Manhattan distance chase the carrier via the classic
    // foe-search machinery.
    ASSERT_EQ(thief, near1->foe());
    ASSERT_TRUE(front_command_is(near1, COMMAND_SEARCH, 0, 0));
    ASSERT_EQ(120, near1->stats()->commands.front().commandcount);
    ASSERT_EQ(thief, near2->foe());
    ASSERT_TRUE(front_command_is(near2, COMMAND_SEARCH, 0, 0));

    // The remaining member is a defender; it is far from home so it is sent
    // back rather than joining the chase. (Its foe may legitimately point at
    // the thief via the tick-level find_far_foe backstop — the director's
    // contract is the command, not the foe.)
    ASSERT_TRUE(front_command_is(far1, COMMAND_GOTO, 544, 800));
    ASSERT_FALSE(queue_contains(far1, COMMAND_SEARCH));
}

TEST(CtfAi, director_never_touches_player_walkers)
{
    CtfAiWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 800);
    // Both team-0 walkers would otherwise be directed (far from home, idle
    // queues): one is ACT_CONTROL, the other has a bound user id. ACT_SIT
    // keeps their own act loops from issuing commands, so any command seen
    // here could only have come from the director.
    walker* control = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_CONTROL);
    walker* bound = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 480, ACT_SIT);
    bound->set_user(0);
    walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 384, 480, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700, ACT_CONTROL);

    // Direct director invocation before init is a hard no-op (inactive gate).
    og::sim::ctf_run_ai_director(fx.world());
    ASSERT_FALSE(bot->stats()->has_commands());

    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);

    og::sim::ctf_run_ai_director(fx.world());

    ASSERT_FALSE(control->stats()->has_commands());
    ASSERT_EQ(nullptr, control->leader());
    ASSERT_FALSE(bound->stats()->has_commands());
    ASSERT_EQ(nullptr, bound->leader());
    // The lone directable walker was assigned (sole member => defender,
    // beyond the leash => ordered home): the gate is per-walker, not global.
    ASSERT_TRUE(front_command_is(bot, COMMAND_GOTO, 96, 96));
}

TEST(CtfAi, director_skips_frozen_and_dead_members)
{
    CtfAiWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 800);
    walker* frozen = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_SIT);
    walker* dead = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 480, ACT_SIT);
    walker* live = fx.spawn_living(FAMILY_SOLDIER, 0, 384, 480, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700, ACT_CONTROL);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);

    frozen->stats()->set_frozen_delay(10);
    dead->set_dead(1);
    og::sim::ctf_run_ai_director(fx.world());

    ASSERT_FALSE(frozen->stats()->has_commands());
    ASSERT_FALSE(dead->stats()->has_commands());
    // The one eligible member is the whole roster: sole remaining => defender.
    ASSERT_TRUE(front_command_is(live, COMMAND_GOTO, 96, 96));
}

// --- Behavioral: carrier run, pickup, defense, escort ----------------------

TEST(CtfAi, carrier_runs_home_distance_shrinks_each_cadence_then_captures)
{
    CtfAiWorld fx;
    walker* flag0 = fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 480, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 480, 760, ACT_RANDOM);
    fx.spawn_living(FAMILY_SOLDIER, 1, 96, 832, ACT_CONTROL);
    fx.world().ctf_requested_capture_limit = 5;
    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_TRUE(flag1->eat_me(runner));

    // Sample the carrier's distance to its flag home every director cadence:
    // it must shrink strictly window over window until the capture lands.
    int last_distance = manhattan(runner, 96, 96);
    bool captured = false;
    for (int window = 0; window < 90 && !captured; ++window)
    {
        fx.tick(og::sim::kCtfAiCadenceTicks);
        if (fx.world().ctf.captures[0] > 0)
        {
            captured = true;
            break;
        }
        const int now = manhattan(runner, 96, 96);
        ASSERT_LT(now, last_distance)
            << "carrier must make progress toward home every cadence window";
        last_distance = now;
        ASSERT_EQ(flag0, runner->leader())
            << "carrier leader is the own flag entity (auto-foe suppressed)";
        ASSERT_TRUE(queue_contains(runner, COMMAND_GOTO));
        ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[1].state);
        ASSERT_EQ(runner->entity_id(),
                  fx.world().ctf.flags[1].carrier_entity_id);
    }
    ASSERT_TRUE(captured)
        << "the carrier touching its own home flag banks the capture";
    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[1].state);
}

TEST(CtfAi, attacker_reaches_and_picks_up_enemy_flag_through_collision)
{
    CtfAiWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 480, 160);
    fx.spawn_anchor(0, 128, 96);
    fx.spawn_anchor(1, 512, 96);
    // Two AI members: oblist order makes the first the defender and the
    // second the attacker.
    walker* defender = fx.spawn_living(FAMILY_SOLDIER, 0, 128, 160, ACT_RANDOM);
    walker* attacker = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160, ACT_RANDOM);
    // The enemy roster is a passive player stand-in next to its flag, so the
    // bots' chase and the director's flag goal converge on the same corner.
    fx.spawn_living(FAMILY_SOLDIER, 1, 480, 192, ACT_CONTROL);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);

    bool picked_up = false;
    std::uint32_t carrier_id = 0;
    for (int i = 0; i < 800 && !picked_up; ++i)
    {
        fx.tick();
        if (fx.world().ctf.flags[1].state == og::sim::CtfFlagState::Carried)
        {
            picked_up = true;
            carrier_id = fx.world().ctf.flags[1].carrier_entity_id;
        }
    }
    ASSERT_TRUE(picked_up)
        << "a team-0 bot must reach the enemy flag and take it via the "
           "obmap collision -> eat_me path";
    ASSERT_TRUE(carrier_id == attacker->entity_id() ||
                carrier_id == defender->entity_id());
    ASSERT_EQ(attacker->entity_id(), carrier_id)
        << "the attacker role (2nd member) is the one sent across the map";
}

TEST(CtfAi, defender_stays_leashed_while_attackers_leave)
{
    CtfAiWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 800);
    fx.spawn_anchor(0, 128, 96);
    fx.spawn_anchor(1, 512, 832);
    // Three AI members: ceil(3/3) = 1 defender (first), two attackers.
    walker* defender = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160, ACT_RANDOM);
    walker* atk1 = fx.spawn_living(FAMILY_SOLDIER, 0, 192, 160, ACT_RANDOM);
    walker* atk2 = fx.spawn_living(FAMILY_SOLDIER, 0, 224, 160, ACT_RANDOM);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 768, ACT_CONTROL);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);

    // The leash bound: 96px radius plus one cadence (15 ticks) of drift plus
    // pathing overshoot. Sampled at every cadence boundary after the first
    // few windows let everyone settle into role positions.
    int defender_max = 0;
    int attacker_max = 0;
    for (int window = 0; window < 24; ++window)
    {
        fx.tick(og::sim::kCtfAiCadenceTicks);
        if (window < 4)
            continue;
        if (!defender->dead())
            defender_max =
                std::max(defender_max, manhattan(defender, 96, 96));
        if (!atk1->dead())
            attacker_max = std::max(attacker_max, manhattan(atk1, 96, 96));
        if (!atk2->dead())
            attacker_max = std::max(attacker_max, manhattan(atk2, 96, 96));
    }
    ASSERT_LE(defender_max, 250)
        << "the defender must stay leashed near the flag home";
    ASSERT_GT(attacker_max, 400)
        << "attackers must leave the base toward the enemy flag";
}

// --- Classic no-op proof -----------------------------------------------------

// In a classic (non-CTF) world the director never runs and nothing ever
// issues COMMAND_GOTO, so the world RNG stream of a CTF-capable binary stays
// byte-identical between twin runs and the new code is provably inert.
namespace {

std::string run_classic_skirmish_checked(int ticks)
{
    CtfAiWorld fx(1);
    fx.world().type = 0;
    fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160, ACT_RANDOM);
    fx.spawn_living(FAMILY_SOLDIER, 0, 192, 160, ACT_RANDOM);
    fx.spawn_living(FAMILY_ORC, 1, 160, 320, ACT_RANDOM);
    fx.spawn_living(FAMILY_ORC, 1, 192, 320, ACT_RANDOM);
    for (int i = 0; i < ticks; ++i)
    {
        fx.tick();
        for (const auto& uptr : fx.world().oblist)
        {
            const walker* w = uptr.get();
            if (w != nullptr && queue_contains(w, COMMAND_GOTO))
            {
                ADD_FAILURE() << "COMMAND_GOTO issued in a classic world at "
                                 "tick " << i;
                return {};
            }
        }
    }
    EXPECT_FALSE(fx.world().ctf.active);
    EXPECT_FALSE(fx.world().ctf.init_attempted);
    return digest_ai_world(fx.world());
}

} // namespace

TEST(CtfAi, classic_world_never_sees_director_or_goto_and_rng_is_stable)
{
    const std::string first = run_classic_skirmish_checked(300);
    const std::string second = run_classic_skirmish_checked(300);
    ASSERT_FALSE(first.empty());
    ASSERT_EQ(first, second)
        << "twin classic runs must match byte for byte (rng stream included)";
}

// --- Determinism and the bot-match smoke ------------------------------------

namespace {

// Open 2-team arena with auto-spawned bot squads (no authored livings).
void build_bot_match(CtfAiWorld& fx)
{
    fx.spawn_flag(0, 160, 128);
    fx.spawn_flag(1, 160, 800);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(0, 224, 96);
    fx.spawn_anchor(1, 96, 832);
    fx.spawn_anchor(1, 224, 832);
    fx.world().ctf_requested_respawn_ticks = 30;
}

std::string run_bot_match_digest(int ticks)
{
    CtfAiWorld fx;
    build_bot_match(fx);
    fx.tick(ticks);
    return digest_ai_world(fx.world());
}

} // namespace

TEST(CtfAi, directed_bot_match_is_deterministic)
{
    const std::string first = run_bot_match_digest(300);
    const std::string second = run_bot_match_digest(300);
    ASSERT_EQ(first, second)
        << "same seed + same arena must reproduce identical roles, commands, "
           "and movement";
}

TEST(CtfAi, bot_match_produces_pickups_and_captures)
{
    CtfAiWorld fx;
    build_bot_match(fx);

    bool saw_pickup = false;
    for (int i = 0; i < 1000 && !fx.world().game_ended; ++i)
    {
        fx.tick();
        if (fx.world().ctf.flags[0].state == og::sim::CtfFlagState::Carried ||
            fx.world().ctf.flags[1].state == og::sim::CtfFlagState::Carried)
        {
            saw_pickup = true;
        }
    }

    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_TRUE(saw_pickup) << "flags must get picked up in a directed match";
    ASSERT_GE(fx.world().ctf.captures[0] + fx.world().ctf.captures[1], 1)
        << "a directed 5v5 bot match must produce at least one capture in "
           "1000 ticks";
}
