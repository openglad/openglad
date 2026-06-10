// Capture-the-flag core engine tests: lazy init/activation, team stripping,
// the flag state machine, control points, win conditions, and the classic
// no-op + double-run determinism proofs.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>

#include "test_game_world_fixture.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

loader& ctf_test_loader()
{
    static loader instance{EntityFactory{}};
    static const bool registered = [] {
        register_ctf_loader_entries(instance);
        return true;
    }();
    (void)registered;
    return instance;
}

// TestGameWorld wired to a loader that carries the CTF treasure entries, so
// flag/control-point spawns run the production entity factory path.
struct CtfWorld : TestGameWorld
{
    explicit CtfWorld(int level_id = 500)
        : TestGameWorld(level_id)
    {
        loader* game_loader = &ctf_test_loader();
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

    walker* spawn_flag(int team, int x, int y, int level = 0)
    {
        walker* flag = world().add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
        if (flag == nullptr)
            return nullptr;
        flag->setxy(x, y);
        flag->set_team_num(static_cast<unsigned char>(team));
        if (level > 0 && flag->stats() != nullptr)
            flag->stats()->set_level(level);
        return flag;
    }

    walker* spawn_point(int x, int y)
    {
        walker* point = world().add_fx_ob(Order::Treasure, og::FAMILY_CTF_POINT);
        if (point == nullptr)
            return nullptr;
        point->setxy(x, y);
        return point;
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

    // A map teleporter pad. Pads pair with the next live same-level pad in
    // fxlist order (treasure::find_teleport_target), wrapping at the end.
    walker* spawn_teleporter(int x, int y, int pad_level = 1)
    {
        walker* pad = world().add_fx_ob(Order::Treasure, FAMILY_TELEPORTER);
        if (pad == nullptr)
            return nullptr;
        pad->setxy(x, y);
        if (pad->stats() != nullptr)
            pad->stats()->set_level(pad_level);
        return pad;
    }

    // Stages a self-teleport for the upcoming world tick. The production
    // paths (walker::teleport / teleport_ranged) stamp the marker inside
    // the walker's act, after tick() has already incremented tick_count_;
    // a between-ticks script therefore stamps tick_count_ + 1 before
    // warping the walker with setxy.
    void stage_self_teleport(walker* w)
    {
        w->set_last_self_teleport_tick(world().tick_count_ + 1);
    }

    // ACT_CONTROL livings stand still without player input, which keeps the
    // flag/control-point geometry of these tests deterministic.
    walker* spawn_living(int family, int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(x, y);
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(ACT_CONTROL);
        return w;
    }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }
};

int count_notifications(const og::sim::SimEventLog& log, const std::string& needle)
{
    int count = 0;
    for (const auto& ev : log.events())
    {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
        {
            count++;
        }
    }
    return count;
}

bool has_notification(const og::sim::SimEventLog& log, const std::string& needle)
{
    return count_notifications(log, needle) > 0;
}

bool has_score_change(const og::sim::SimEventLog& log, std::uint32_t team,
                      std::uint32_t points)
{
    for (const auto& ev : log.events())
    {
        if (ev.kind == og::sim::EventKind::ScoreChange && ev.a == team &&
            ev.b == points)
        {
            return true;
        }
    }
    return false;
}

// Builds a classic (non-CTF) skirmish and returns a behavior digest after
// `ticks` ticks. Worlds are constructed and run sequentially so each run owns
// the gameplay context for its full lifetime.
struct WorldDigest
{
    std::uint32_t rng_state = 0;
    std::uint32_t tick_count = 0;
    int living_alive = 0;
    long long position_sum = 0;
    bool ctf_active = false;
    bool ctf_init_attempted = false;
    std::size_t respawn_queue_size = 0;
    std::uint16_t captures[4] = {};

    bool operator==(const WorldDigest& o) const = default;
};

WorldDigest digest_world(GameWorld& world)
{
    WorldDigest d;
    d.rng_state = world.rng_.state_;
    d.tick_count = world.tick_count_;
    d.ctf_active = world.ctf.active;
    d.ctf_init_attempted = world.ctf.init_attempted;
    d.respawn_queue_size = world.ctf.respawn_queue.size();
    for (int t = 0; t < 4; ++t)
        d.captures[t] = world.ctf.captures[t];
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr)
            continue;
        d.position_sum += w->xpos() * 31 + w->ypos();
        if (!w->dead() && w->query_order() == Order::Living)
            d.living_alive++;
    }
    return d;
}

WorldDigest run_classic_skirmish(int ticks)
{
    CtfWorld fx(1);
    fx.world().type = 0; // classic level: the CTF gate must stay cold
    fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160)->set_act_type(ACT_RANDOM);
    fx.spawn_living(FAMILY_SOLDIER, 0, 192, 160)->set_act_type(ACT_RANDOM);
    fx.spawn_living(FAMILY_ORC, 1, 160, 320)->set_act_type(ACT_RANDOM);
    fx.spawn_living(FAMILY_ORC, 1, 192, 320)->set_act_type(ACT_RANDOM);
    fx.tick(ticks);
    return digest_world(fx.world());
}

WorldDigest run_ctf_bot_match(int ticks)
{
    CtfWorld fx(500);
    fx.spawn_flag(0, 160, 160);
    fx.spawn_flag(1, 480, 800);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(0, 192, 128);
    fx.spawn_anchor(1, 448, 832);
    fx.spawn_anchor(1, 512, 832);
    fx.spawn_point(320, 480);
    fx.world().ctf_requested_respawn_ticks = 30;
    fx.tick(ticks);
    return digest_world(fx.world());
}

} // namespace

// --- Classic no-op -----------------------------------------------------

TEST(CtfCore, classic_world_is_untouched_and_deterministic)
{
    const WorldDigest first = run_classic_skirmish(200);
    const WorldDigest second = run_classic_skirmish(200);

    ASSERT_EQ(first, second) << "identical classic runs must stay byte-stable";
    ASSERT_FALSE(first.ctf_active);
    ASSERT_FALSE(first.ctf_init_attempted) << "CTF init must never run off-path";
    ASSERT_EQ(0u, first.respawn_queue_size);
    for (int t = 0; t < 4; ++t)
        ASSERT_EQ(0, first.captures[t]);
}

TEST(CtfCore, classic_world_emits_no_ctf_events)
{
    CtfWorld fx(1);
    fx.world().type = 0;
    fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    fx.spawn_living(FAMILY_ORC, 1, 480, 800);
    fx.tick(50);
    ASSERT_FALSE(has_notification(fx.events, "CAPTURE THE FLAG"));
    ASSERT_FALSE(has_notification(fx.events, "FLAG"));
}

// --- Lazy init / activation --------------------------------------------

TEST(CtfCore, lazy_init_activates_two_team_map)
{
    CtfWorld fx;
    walker* flag0 = fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    ASSERT_NE(nullptr, flag0);
    ASSERT_NE(nullptr, flag1);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(1, 512, 832);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);

    ASSERT_FALSE(fx.world().ctf.active);
    fx.tick();

    const og::sim::CtfState& ctf = fx.world().ctf;
    ASSERT_TRUE(ctf.active);
    ASSERT_TRUE(ctf.init_attempted);
    ASSERT_EQ(2, ctf.team_count);
    ASSERT_TRUE(ctf.team_active[0]);
    ASSERT_TRUE(ctf.team_active[1]);
    ASSERT_FALSE(ctf.team_active[2]);
    ASSERT_TRUE(ctf.flags[0].present);
    ASSERT_TRUE(ctf.flags[1].present);
    ASSERT_EQ(96, ctf.flags[0].home_x);
    ASSERT_EQ(96, ctf.flags[0].home_y);
    ASSERT_EQ(flag0->entity_id(), ctf.flags[0].flag_entity_id);
    ASSERT_EQ(flag1->entity_id(), ctf.flags[1].flag_entity_id);
    ASSERT_EQ(og::sim::kCtfDefaultCaptureLimit, ctf.capture_limit);
    ASSERT_EQ(1, ctf.anchor_count[0]);
    ASSERT_EQ(1, ctf.anchor_count[1]);
    ASSERT_TRUE(has_notification(fx.events, "CAPTURE THE FLAG! TO 3"));
}

TEST(CtfCore, init_strips_teams_beyond_requested_count)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 96);
    walker* flag2 = fx.spawn_flag(2, 96, 800);
    walker* flag3 = fx.spawn_flag(3, 544, 800);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(1, 512, 128);
    fx.spawn_anchor(2, 128, 832);
    fx.spawn_anchor(3, 512, 832);
    walker* stripped_living = fx.spawn_living(FAMILY_ORC, 2, 200, 760);
    fx.world().ctf_requested_team_count = 2;

    fx.tick();

    const og::sim::CtfState& ctf = fx.world().ctf;
    ASSERT_TRUE(ctf.active);
    ASSERT_EQ(2, ctf.team_count);
    ASSERT_TRUE(ctf.team_active[0]);
    ASSERT_TRUE(ctf.team_active[1]);
    ASSERT_FALSE(ctf.team_active[2]);
    ASSERT_FALSE(ctf.team_active[3]);
    ASSERT_FALSE(ctf.flags[2].present);
    ASSERT_FALSE(ctf.flags[3].present);
    ASSERT_TRUE(flag2->dead());
    ASSERT_TRUE(flag3->dead());
    ASSERT_TRUE(stripped_living->dead());

    // Active teams had no livings: each gets a five-bot squad.
    int alive[4] = {};
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->team_num() < 4)
            alive[w->team_num()]++;
    }
    ASSERT_EQ(5, alive[0]);
    ASSERT_EQ(5, alive[1]);
    ASSERT_EQ(0, alive[2]);
}

TEST(CtfCore, init_demotes_to_inactive_below_two_flag_teams)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* survivor = fx.spawn_living(FAMILY_ORC, 1, 400, 700);

    fx.tick();

    ASSERT_FALSE(fx.world().ctf.active);
    ASSERT_TRUE(fx.world().ctf.init_attempted);
    ASSERT_FALSE(survivor->dead()) << "demoted init must leave the map untouched";
    ASSERT_FALSE(has_notification(fx.events, "CAPTURE THE FLAG"));

    // Later ticks stay inert: the latch prevents re-initialization.
    fx.tick(5);
    ASSERT_FALSE(fx.world().ctf.active);
}

TEST(CtfCore, map_capture_limit_comes_from_flag_level_and_request_wins)
{
    {
        CtfWorld fx;
        fx.spawn_flag(0, 96, 96, /*level=*/5);
        fx.spawn_flag(1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.tick();
        ASSERT_EQ(5, fx.world().ctf.capture_limit);
    }
    {
        CtfWorld fx;
        fx.spawn_flag(0, 96, 96, /*level=*/5);
        fx.spawn_flag(1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.world().ctf_requested_capture_limit = 9;
        fx.tick();
        ASSERT_EQ(9, fx.world().ctf.capture_limit);
    }
}

// --- Flag state machine -------------------------------------------------

TEST(CtfCore, enemy_touch_picks_up_and_carry_visual_follows)
{
    CtfWorld fx;
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    fx.spawn_flag(0, 96, 96);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);

    ASSERT_TRUE(flag1->eat_me(runner));
    og::sim::CtfFlag& f1 = fx.world().ctf.flags[1];
    ASSERT_EQ(og::sim::CtfFlagState::Carried, f1.state);
    ASSERT_EQ(runner->entity_id(), f1.carrier_entity_id);
    ASSERT_EQ(1, flag1->ignore());
    ASSERT_TRUE(has_notification(fx.events, "GREEN FLAG TAKEN!"));

    // A scripted 60/40 px warp: far beyond any walking speed, but with no
    // self-teleport marker stamped it must NOT drop — position warps from
    // snapshots and tests are not teleports.
    runner->setxy(260, 240);
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Carried, f1.state)
        << "an unmarked position warp must never drop the flag";
    ASSERT_EQ(260, flag1->xpos());
    ASSERT_EQ(232, flag1->ypos()) << "carry visual rides 8px above the carrier";
    ASSERT_EQ(260, f1.x);
    ASSERT_EQ(240, f1.y);
}

TEST(CtfCore, pickup_fires_through_obmap_collision)
{
    CtfWorld fx;
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    fx.spawn_flag(0, 96, 96);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick();

    // Probing the flag's tile through the obmap runs the production
    // collision -> eat_me -> on_eat dispatch.
    runner->setxy(544, 780);
    (void)fx.world().query_passable(544.0f, 796.0f, runner);
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[1].state);
    ASSERT_EQ(runner->entity_id(), fx.world().ctf.flags[1].carrier_entity_id);
}

TEST(CtfCore, own_team_touch_returns_dropped_flag)
{
    CtfWorld fx;
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    fx.spawn_flag(0, 96, 96);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    walker* enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();

    ASSERT_TRUE(flag1->eat_me(runner));
    runner->set_dead(1);
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    ASSERT_TRUE(has_notification(fx.events, "GREEN FLAG DROPPED!"));

    ASSERT_TRUE(flag1->eat_me(enemy));
    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[1].state);
    ASSERT_EQ(544, flag1->xpos());
    ASSERT_EQ(800, flag1->ypos());
    ASSERT_EQ(0, flag1->ignore());
    ASSERT_TRUE(has_notification(fx.events, "GREEN FLAG RETURNED!"));
}

// A flag announces TAKEN only when it leaves home; regrabs of the dropped
// flag in a melee stay silent (no notification ping-pong over one flag).
TEST(CtfCore, regrab_of_dropped_flag_emits_no_second_taken)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    walker* backup = fx.spawn_living(FAMILY_SOLDIER, 0, 232, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);

    // Home -> Carried announces.
    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"));

    // The carrier dies and a second enemy regrabs the dropped flag: the
    // pickup works but stays silent.
    runner->set_dead(1);
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    ASSERT_TRUE(flag1->eat_me(backup));
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[1].state);
    ASSERT_EQ(backup->entity_id(), fx.world().ctf.flags[1].carrier_entity_id);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"))
        << "a regrab of a dropped flag must not re-announce TAKEN";
}

// Every CTF announcement stays within a 25-char budget (<=150px of 6px
// glyphs, inside even the 152px 2-player panes), including the worst case:
// YELLOW, the longest team color name.
TEST(CtfCore, notifications_fit_25_char_budget)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 96);
    fx.spawn_flag(2, 96, 800);
    walker* flag3 = fx.spawn_flag(3, 544, 800);
    fx.spawn_point(320, 320);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 60);
    fx.spawn_living(FAMILY_SOLDIER, 2, 60, 800);
    fx.spawn_living(FAMILY_SOLDIER, 3, 352, 320);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_TRUE(has_notification(fx.events, "CAPTURE THE FLAG! TO 3"));
    fx.world().ctf.flag_return_ticks = 4;

    // Team 3 holds the waypoint alone until it flips.
    fx.tick(og::sim::kCtfCpCaptureTicks + 1);
    ASSERT_TRUE(has_notification(fx.events, "YELLOW TAKES WAYPOINT!"));

    // Take, drop, and auto-return team 3's flag.
    ASSERT_TRUE(flag3->eat_me(runner));
    ASSERT_TRUE(has_notification(fx.events, "YELLOW FLAG TAKEN!"));
    runner->set_dead(1);
    fx.tick();
    ASSERT_TRUE(has_notification(fx.events, "YELLOW FLAG DROPPED!"));
    fx.tick(5);
    ASSERT_TRUE(has_notification(fx.events, "YELLOW FLAG RETURNED!"));

    for (const auto& ev : fx.events.events())
    {
        if (ev.kind == og::sim::EventKind::Notification)
        {
            EXPECT_LE(ev.text.size(), 25u) << "over budget: " << ev.text;
        }
    }
}

TEST(CtfCore, capture_requires_own_flag_home_and_awards_score)
{
    CtfWorld fx;
    walker* flag0 = fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    walker* enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();

    // Runner grabs the enemy flag; the enemy grabs the runner's flag.
    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag0->eat_me(enemy));
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[0].state);

    // Own flag away: touching it (it rides the enemy) cannot capture.
    ASSERT_TRUE(flag0->eat_me(runner));
    ASSERT_EQ(0, fx.world().ctf.captures[0]);

    // Kill the enemy: the runner's flag drops, a friendly touch returns it.
    enemy->set_dead(1);
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[0].state);
    ASSERT_TRUE(flag0->eat_me(runner));
    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[0].state);

    // Own flag home: the touch banks the carried enemy flag.
    const std::uint32_t score_before = fx.world().m_score[0];
    ASSERT_TRUE(flag0->eat_me(runner));
    ASSERT_EQ(1, fx.world().ctf.captures[0]);
    ASSERT_EQ(score_before + og::sim::kCtfCaptureScore, fx.world().m_score[0]);
    ASSERT_TRUE(has_score_change(fx.events, 0, og::sim::kCtfCaptureScore));
    ASSERT_TRUE(has_notification(fx.events, "TEAM 1 SCORES! 1/3"));
    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[1].state);
    ASSERT_EQ(544, flag1->xpos());
    ASSERT_EQ(800, flag1->ypos());
    ASSERT_EQ(0, flag1->ignore());
}

TEST(CtfCore, multi_carry_capture_awards_all_flags)
{
    CtfWorld fx;
    walker* flag0 = fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 96);
    walker* flag2 = fx.spawn_flag(2, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 500, 150);
    fx.spawn_living(FAMILY_SOLDIER, 2, 500, 760);
    fx.world().ctf_requested_team_count = 3;
    fx.tick();
    ASSERT_EQ(3, fx.world().ctf.team_count);

    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag2->eat_me(runner));
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[1].state);
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[2].state);

    ASSERT_TRUE(flag0->eat_me(runner));
    ASSERT_EQ(2, fx.world().ctf.captures[0]);
    ASSERT_EQ(2u * og::sim::kCtfCaptureScore, fx.world().m_score[0]);
    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[1].state);
    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[2].state);
}

TEST(CtfCore, charm_flipped_carrier_drops_flag)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick();

    ASSERT_TRUE(flag1->eat_me(runner));
    runner->set_team_num(1); // charm flip onto the flag's own team
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    ASSERT_EQ(0u, fx.world().ctf.flags[1].carrier_entity_id);
}

TEST(CtfCore, dropped_flag_auto_returns_after_countdown)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();
    fx.world().ctf.flag_return_ticks = 6;

    ASSERT_TRUE(flag1->eat_me(runner));
    runner->set_dead(1);
    fx.tick(); // drop (phase 3) + first countdown step (phase 4)
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    ASSERT_EQ(5, fx.world().ctf.flags[1].return_ticks);

    fx.tick(4);
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[1].state);
    ASSERT_EQ(544, flag1->xpos());
    ASSERT_EQ(800, flag1->ypos());
}

TEST(CtfCore, drop_on_impassable_tile_returns_home_instantly)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();

    ASSERT_TRUE(flag1->eat_me(runner));

    // Wall in the carrier's tile neighborhood, then kill it: the corpse tile
    // fails the terrain probe and the flag must go home instead of dropping.
    PixieData& grid = fx.world().grid;
    for (int gy = 20; gy <= 21; ++gy)
        for (int gx = 20; gx <= 21; ++gx)
            grid.data[gx + grid.w * gy] = PIX_H_WALL1;
    runner->set_dead(1);
    fx.tick();

    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[1].state);
    ASSERT_EQ(544, flag1->xpos());
    ASSERT_EQ(800, flag1->ypos());
    ASSERT_EQ(0, flag1->ignore());
}

// --- Self-teleport flag drop (UT rule) -------------------------------------

namespace {

// Two-team scaffold for the teleport rule: flags for teams 0/1, an
// ACT_CONTROL runner on team 0, an enemy to keep both teams fielded, and a
// long respawn so scripted deaths stay out of the way.
struct TeleportWorld : CtfWorld
{
    walker* flag1 = nullptr;
    walker* runner = nullptr;

    TeleportWorld()
    {
        spawn_flag(0, 96, 96);
        flag1 = spawn_flag(1, 544, 800);
        runner = spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        world().ctf_requested_respawn_ticks = 5000;
    }

    og::sim::CtfFlag& f1() { return world().ctf.flags[1]; }
};

} // namespace

// The core drop bookkeeping for a staged blink: Dropped state at the
// departure point (the carried flag's replicated x/y from the previous
// tick), carrier cleared, flag entity grounded and touchable, return timer
// armed, one announcement.
TEST(CtfCore, staged_blink_drops_flag_at_departure_point)
{
    TeleportWorld fx;
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.f1().state);

    fx.stage_self_teleport(fx.runner);
    fx.runner->setxy(440, 488); // the blink
    fx.tick();

    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.f1().state);
    ASSERT_EQ(0u, fx.f1().carrier_entity_id);
    ASSERT_EQ(200, fx.f1().x) << "the flag stays at the departure point";
    ASSERT_EQ(200, fx.f1().y);
    ASSERT_EQ(200, fx.flag1->xpos());
    ASSERT_EQ(200, fx.flag1->ypos());
    ASSERT_EQ(0, fx.flag1->ignore());
    ASSERT_GT(fx.f1().return_ticks, 0);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

// Drives the production special: mage arms ANI_TELE_OUT via walker::special()
// and walker::animate fires handle_teleport -> walker::teleport(), which
// stamps the self-teleport marker and center_on()'s the owned marker beacon
// (deterministic, no RNG draw).
TEST(CtfCore, real_mage_marker_teleport_special_drops_flag)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* mage = fx.spawn_living(FAMILY_MAGE, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);

    walker* marker = fx.world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->set_owner(mage);
    marker->setxy(416, 416);
    marker->set_lifetime(5);
    marker->set_ani_type(ANI_SPIN); // production marker animation:
    // effect::act kills any FX left on the default ANI_WALK

    ASSERT_TRUE(flag1->eat_me(mage));
    fx.tick();
    const short depart_x = mage->xpos();
    const short depart_y = mage->ypos();

    ASSERT_NE(nullptr, mage->stats());
    mage->stats()->set_max_magicpoints(500);
    mage->stats()->set_magicpoints(500);
    mage->set_current_special(1);
    ASSERT_TRUE(mage->special()) << "mage teleport special must arm TELE_OUT";
    fx.tick(20); // TELE_OUT completes -> handle_teleport -> marker blink

    ASSERT_GT(std::abs(mage->xpos() - depart_x) +
                  std::abs(mage->ypos() - depart_y),
              64)
        << "the mage must have blinked to the marker";
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    ASSERT_EQ(depart_x, fx.world().ctf.flags[1].x);
    ASSERT_EQ(depart_y, fx.world().ctf.flags[1].y);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

// The skeleton's blink is teleport_ranged(level * 18): at level 1 the hop
// spans at most 18 px per axis -- well inside legitimate walking speed,
// which is exactly why displacement inference could never catch it.
// Explicit source marking drops the flag no matter how short the hop.
// Drives the production special: skeleton_do_special arms ANI_TELE_OUT and
// walker::animate fires handle_teleport -> teleport_ranged (destination
// from the world RNG, deterministic under the fixture seed).
TEST(CtfCore, real_level1_skeleton_blink_drops_flag)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* skel = fx.spawn_living(FAMILY_SKELETON, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(10); // activation; ANI_SKEL_GROW completes
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_NE(nullptr, skel->stats());
    ASSERT_EQ(1u, skel->stats()->level()) << "the short-hop case needs level 1";

    ASSERT_TRUE(flag1->eat_me(skel));
    fx.tick();
    const short depart_x = skel->xpos();
    const short depart_y = skel->ypos();

    skel->stats()->set_max_magicpoints(100);
    skel->stats()->set_magicpoints(100);
    skel->set_current_special(1); // TUNNEL
    ASSERT_TRUE(skel->special()) << "skeleton tunnel must arm TELE_OUT";
    fx.tick(20); // TELE_OUT completes -> handle_teleport -> teleport_ranged

    ASSERT_LE(std::abs(skel->xpos() - depart_x), 18)
        << "a level-1 blink stays within 18 px per axis";
    ASSERT_LE(std::abs(skel->ypos() - depart_y), 18);
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state)
        << "even the shortest blink drops the flag";
    ASSERT_EQ(depart_x, fx.world().ctf.flags[1].x);
    ASSERT_EQ(depart_y, fx.world().ctf.flags[1].y);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

// COMMAND_RUSH (the fighter's charge special) executes THREE walksteps per
// do_command round, so a max-stepsize fighter legitimately covers 36 px in
// one tick -- the false positive that sank displacement inference. Source
// marking keeps the flag: a charge is legs, not a teleport.
TEST(CtfCore, fighter_rush_charge_at_max_stepsize_keeps_flag)
{
    TeleportWorld fx;
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.f1().state);

    // Max non-weapon stepsize (guy::update_derived_stats caps at 12),
    // pre-faced so living::act's turn gate doesn't spend ticks rotating.
    fx.runner->set_normal_stepsize(12.0f);
    fx.runner->set_stepsize(12.0f);
    fx.runner->set_curdir(static_cast<signed char>(FACE_RIGHT));
    fx.runner->set_enddir(static_cast<char>(FACE_RIGHT));
    ASSERT_NE(nullptr, fx.runner->stats());
    fx.runner->stats()->add_command(COMMAND_RUSH, 3, 1, 0);

    int max_tick_dx = 0;
    for (int step = 0; step < 3; ++step)
    {
        const short before_x = fx.runner->xpos();
        fx.tick();
        const int dx = static_cast<int>(fx.runner->xpos()) - before_x;
        if (dx > max_tick_dx)
            max_tick_dx = dx;
        ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.f1().state)
            << "step " << step << ": a rush charge must never drop the flag";
        ASSERT_EQ(fx.runner->xpos(), fx.f1().x);
        ASSERT_EQ(fx.runner->ypos(), fx.f1().y);
    }
    ASSERT_EQ(36, max_tick_dx)
        << "the charge must actually cover 3x stepsize in a single tick";
    ASSERT_EQ(0, count_notifications(fx.events, "DROPPED"));
}

// A real pad ride: obmap probe -> collision -> eat_me -> teleporter_on_eat
// (production distance gate, find_teleport_target pairing, center_on).
// Pads never stamp the self-teleport marker, so the flag rides through.
TEST(CtfCore, real_teleporter_pad_ride_keeps_flag_carried)
{
    TeleportWorld fx;
    walker* padA = fx.spawn_teleporter(320, 320);
    walker* padB = fx.spawn_teleporter(480, 640);
    ASSERT_NE(nullptr, padA);
    ASSERT_NE(nullptr, padB);
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);

    fx.runner->center_on(padA);
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick(); // f.x/f.y sync on the departure pad

    (void)fx.world().query_passable(static_cast<float>(padA->xpos()),
                                    static_cast<float>(padA->ypos()),
                                    fx.runner);
    ASSERT_LE(std::abs((fx.runner->xpos() + fx.runner->sizex() / 2) -
                       (padB->xpos() + padB->sizex() / 2)),
              1)
        << "the eat must have center_on'd the rider onto the partner pad";

    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.f1().state)
        << "a map teleporter ride carries the flag through";
    ASSERT_EQ(fx.runner->entity_id(), fx.f1().carrier_entity_id);
    ASSERT_EQ(fx.runner->xpos(), fx.f1().x)
        << "the carried flag resyncs at the arrival pad";
    ASSERT_EQ(fx.runner->ypos(), fx.f1().y);
    ASSERT_EQ(fx.runner->xpos(), fx.flag1->xpos());
    ASSERT_EQ(static_cast<short>(fx.runner->ypos() - 8), fx.flag1->ypos());
    ASSERT_EQ(0, count_notifications(fx.events, "DROPPED"));
}

// The displacement era exempted pad-shaped jumps by geometry, which a blink
// from beside a pad to beside its partner could spoof. Source marking does
// not care about pad geometry: a self-teleport drops no matter where it
// starts or lands.
TEST(CtfCore, blink_from_beside_pad_to_beside_partner_drops)
{
    TeleportWorld fx;
    walker* padA = fx.spawn_teleporter(320, 320);
    walker* padB = fx.spawn_teleporter(480, 640);
    fx.tick();

    // 20 px off pad A: close enough to look like a ride to any proximity
    // heuristic, too far for the real eat gate to ever fire.
    fx.runner->setxy(static_cast<short>(padA->xpos() + 20),
                     static_cast<short>(padA->ypos() + 20));
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick();
    const short depart_x = fx.runner->xpos();
    const short depart_y = fx.runner->ypos();

    fx.stage_self_teleport(fx.runner);
    fx.runner->setxy(static_cast<short>(padB->xpos() + 20),
                     static_cast<short>(padB->ypos() + 20));
    fx.tick();

    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.f1().state);
    ASSERT_EQ(depart_x, fx.f1().x);
    ASSERT_EQ(depart_y, fx.f1().y);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

// A marker beacon pre-placed on the caster's own flag stand turns the blink
// destination probe inside walker::teleport into an own-flag touch while
// the caster still stands across the map. The touch gate refuses eats fired
// mid-teleport: no capture is banked, and the same tick's carried-flag
// phase drops the stolen flag at the departure point instead.
TEST(CtfCore, capture_during_blink_is_refused)
{
    CtfWorld fx;
    walker* flag0 = fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* mage = fx.spawn_living(FAMILY_MAGE, 0, 400, 400);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);

    walker* marker = fx.world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->set_owner(mage);
    marker->setxy(flag0->xpos(), flag0->ypos()); // overlapping the own stand
    marker->set_lifetime(5);
    marker->set_ani_type(ANI_SPIN); // production marker animation:
    // effect::act kills any FX left on the default ANI_WALK

    ASSERT_TRUE(flag1->eat_me(mage));
    fx.tick();
    const short depart_x = mage->xpos();
    const short depart_y = mage->ypos();

    ASSERT_NE(nullptr, mage->stats());
    mage->stats()->set_max_magicpoints(500);
    mage->stats()->set_magicpoints(500);
    mage->set_current_special(1);
    ASSERT_TRUE(mage->special());
    fx.tick(20); // the blink lands the mage on its own stand

    ASSERT_LE(std::abs(mage->xpos() - flag0->xpos()), 8)
        << "the mage must have arrived at the marker on the stand";
    ASSERT_EQ(0, fx.world().ctf.captures[0])
        << "a capture banked by the blink's destination probe is an exploit";
    ASSERT_FALSE(has_notification(fx.events, "SCORES"));
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state)
        << "the carried flag drops at the departure point instead";
    ASSERT_EQ(depart_x, fx.world().ctf.flags[1].x);
    ASSERT_EQ(depart_y, fx.world().ctf.flags[1].y);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

// The mirror exploit: a marker overlapping an AtHome ENEMY flag would let
// the destination probe "pick up" the flag while the caster still stands at
// the departure point -- and the same tick's drop rule would then deposit
// it there, relocating the flag across the map without it ever being
// carried. The touch gate refuses the probe-eat: the flag never moves, the
// mage arrives empty-handed, and a real touch on a later tick picks it up
// normally.
TEST(CtfCore, pickup_during_blink_is_refused_and_flag_stays_home)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* mage = fx.spawn_living(FAMILY_MAGE, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);

    walker* marker = fx.world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->set_owner(mage);
    marker->setxy(flag1->xpos(), flag1->ypos()); // overlapping the enemy flag
    marker->set_lifetime(5);
    marker->set_ani_type(ANI_SPIN); // production marker animation:
    // effect::act kills any FX left on the default ANI_WALK

    ASSERT_NE(nullptr, mage->stats());
    mage->stats()->set_max_magicpoints(500);
    mage->stats()->set_magicpoints(500);
    mage->set_current_special(1);
    ASSERT_TRUE(mage->special());
    fx.tick(20); // the blink lands the mage on the enemy flag

    ASSERT_LE(std::abs(mage->xpos() - 544), 8)
        << "the mage must have arrived at the marker on the enemy flag";
    ASSERT_LE(std::abs(mage->ypos() - 800), 8);
    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.world().ctf.flags[1].state)
        << "the blink's destination probe must not pick the flag up";
    ASSERT_EQ(0u, fx.world().ctf.flags[1].carrier_entity_id);
    ASSERT_EQ(544, flag1->xpos()) << "and the flag must not relocate";
    ASSERT_EQ(800, flag1->ypos());
    ASSERT_EQ(0, count_notifications(fx.events, "GREEN FLAG TAKEN!"));
    ASSERT_EQ(0, count_notifications(fx.events, "DROPPED"));

    // The marker is stale on later ticks: a real touch picks it up.
    (void)fx.world().query_passable(static_cast<float>(flag1->xpos()),
                                    static_cast<float>(flag1->ypos()), mage);
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[1].state);
    ASSERT_EQ(mage->entity_id(), fx.world().ctf.flags[1].carrier_entity_id);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"));
}

TEST(CtfCore, teleport_drop_on_impassable_departure_returns_home)
{
    TeleportWorld fx;
    fx.tick();
    fx.runner->setxy(320, 320);
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick();

    // Wall the departure tile after the fact, then blink away: the drop
    // probe fails and the stranding rule sends the flag home.
    PixieData& grid = fx.world().grid;
    for (int gy = 20; gy <= 21; ++gy)
        for (int gx = 20; gx <= 21; ++gx)
            grid.data[gx + grid.w * gy] = PIX_H_WALL1;
    fx.stage_self_teleport(fx.runner);
    fx.runner->setxy(96, 700);
    fx.tick();

    ASSERT_EQ(og::sim::CtfFlagState::AtHome, fx.f1().state);
    ASSERT_EQ(544, fx.flag1->xpos());
    ASSERT_EQ(800, fx.flag1->ypos());
    ASSERT_EQ(0, fx.flag1->ignore());
    ASSERT_TRUE(has_notification(fx.events, "GREEN FLAG RETURNED!"));
    ASSERT_EQ(0, count_notifications(fx.events, "DROPPED"));
}

TEST(CtfCore, one_blink_drops_every_carried_flag)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 96);
    walker* flag2 = fx.spawn_flag(2, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 500, 150);
    fx.spawn_living(FAMILY_SOLDIER, 2, 500, 760);
    fx.world().ctf_requested_team_count = 3;
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick();
    ASSERT_EQ(3, fx.world().ctf.team_count);

    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag2->eat_me(runner));
    fx.tick();

    fx.stage_self_teleport(runner);
    runner->setxy(200, 500);
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[2].state);
    ASSERT_EQ(200, fx.world().ctf.flags[1].x);
    ASSERT_EQ(200, fx.world().ctf.flags[1].y);
    ASSERT_EQ(200, fx.world().ctf.flags[2].x);
    ASSERT_EQ(200, fx.world().ctf.flags[2].y);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
    ASSERT_EQ(1, count_notifications(fx.events, "BLUE FLAG DROPPED!"));
}

// A teleport drop behaves like any other drop for the announcement rule:
// the regrab of the dropped flag stays silent (TAKEN announces once).
TEST(CtfCore, regrab_after_teleport_drop_stays_silent)
{
    TeleportWorld fx;
    walker* backup = fx.spawn_living(FAMILY_SOLDIER, 0, 232, 200);
    fx.tick();

    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"));
    fx.tick();

    fx.stage_self_teleport(fx.runner);
    fx.runner->setxy(440, 488);
    fx.tick();
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.f1().state);

    ASSERT_TRUE(fx.flag1->eat_me(backup));
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.f1().state);
    ASSERT_EQ(backup->entity_id(), fx.f1().carrier_entity_id);
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"))
        << "a regrab after a teleport drop must not re-announce TAKEN";
}

// --- Control points -----------------------------------------------------

TEST(CtfCore, control_point_capture_and_contender_reset)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 800);
    walker* point = fx.spawn_point(320, 320);
    walker* holder = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 320);
    walker* rival = fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700);
    fx.tick();
    ASSERT_EQ(1, fx.world().ctf.cp_count);

    // Single-team presence accumulates.
    fx.tick(10);
    const og::sim::CtfControlPoint& cp = fx.world().ctf.cps[0];
    ASSERT_EQ(0, cp.progress_team);
    ASSERT_GT(cp.progress, 0);
    ASSERT_EQ(-1, cp.owner);

    // Contender change resets the meter.
    const std::int16_t before_swap = cp.progress;
    holder->setxy(96, 700);
    rival->setxy(352, 320);
    fx.tick();
    ASSERT_EQ(1, cp.progress_team);
    ASSERT_LE(cp.progress, before_swap);

    // Sole presence to the capture threshold flips owner + entity team.
    fx.tick(og::sim::kCtfCpCaptureTicks);
    ASSERT_EQ(1, cp.owner);
    ASSERT_EQ(1, point->team_num());
    ASSERT_EQ(og::sim::kCtfCpCaptureScore, fx.world().m_score[1]);
    ASSERT_TRUE(has_score_change(fx.events, 1, og::sim::kCtfCpCaptureScore));
    ASSERT_TRUE(has_notification(fx.events, "GREEN TAKES WAYPOINT!"));
}

// --- Waypoint retake dynamics (majority contender + symmetric decay) ------

namespace {

// Enemy-owned waypoint scaffold: flags for teams 0/1, one control point at
// (320,320) pre-owned by team 1 (owner + pad entity team stamped after the
// lazy init), and ACT_CONTROL livings the test positions explicitly.
struct RetakeWorld : CtfWorld
{
    walker* point = nullptr;
    walker* attacker = nullptr;
    walker* owner_bot = nullptr;

    RetakeWorld()
    {
        spawn_flag(0, 96, 96);
        spawn_flag(1, 544, 800);
        point = spawn_point(320, 320);
        attacker = spawn_living(FAMILY_SOLDIER, 0, 96, 700);
        owner_bot = spawn_living(FAMILY_SOLDIER, 1, 544, 700);
        world().ctf_requested_respawn_ticks = 5000;
        tick();
        world().ctf.cps[0].owner = 1;
        if (point != nullptr)
            point->set_team_num(1);
    }

    og::sim::CtfControlPoint& cp() { return world().ctf.cps[0]; }
};

} // namespace

TEST(CtfCore, retake_of_enemy_owned_point_flips_at_36_sole_ticks)
{
    RetakeWorld fx;
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_EQ(1, fx.world().ctf.cp_count);

    fx.attacker->setxy(336, 320); // 16px from the pad: inside the 48px disc
    fx.tick(og::sim::kCtfCpCaptureTicks - 1);
    ASSERT_EQ(1, fx.cp().owner) << "35 sole-occupancy ticks must not flip";
    ASSERT_EQ(0, fx.cp().progress_team);
    ASSERT_EQ(og::sim::kCtfCpCaptureTicks - 1, fx.cp().progress);

    fx.tick();
    ASSERT_EQ(0, fx.cp().owner)
        << "the 36th sole-occupancy tick retakes the waypoint";
    ASSERT_EQ(0, fx.point->team_num()) << "pad entity recolors to the taker";
    ASSERT_EQ(0, fx.cp().progress);
    ASSERT_EQ(-1, fx.cp().progress_team);
    ASSERT_TRUE(has_notification(fx.events, "RED TAKES WAYPOINT!"));
}

TEST(CtfCore, corpses_inside_disc_do_not_contest_retake)
{
    RetakeWorld fx;
    // Dead myguy corpses persist in oblist (the sweep keeps them); park one
    // enemy and one friendly corpse inside the disc before the retake.
    walker* enemy_corpse = fx.spawn_living(FAMILY_SOLDIER, 1, 320, 352);
    walker* friendly_corpse = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 288);
    ASSERT_NE(nullptr, enemy_corpse);
    ASSERT_NE(nullptr, friendly_corpse);
    enemy_corpse->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    friendly_corpse->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    enemy_corpse->myguy->id = 21;
    friendly_corpse->myguy->id = 22;
    enemy_corpse->set_dead(1);
    friendly_corpse->set_dead(1);

    fx.attacker->setxy(336, 320);
    fx.tick(og::sim::kCtfCpCaptureTicks);
    ASSERT_TRUE(enemy_corpse->dead());
    ASSERT_TRUE(friendly_corpse->dead());
    ASSERT_EQ(0, fx.cp().owner) << "corpses inside the disc must not contest";
}

TEST(CtfCore, majority_retake_accrues_while_outnumbered_owner_contests)
{
    RetakeWorld fx;
    walker* attacker_b = fx.spawn_living(FAMILY_SOLDIER, 0, 304, 320);
    ASSERT_NE(nullptr, attacker_b);

    fx.attacker->setxy(336, 320);
    fx.owner_bot->setxy(320, 352); // owner contests inside the disc, but 2v1
    fx.tick(og::sim::kCtfCpCaptureTicks - 1);
    ASSERT_EQ(1, fx.cp().owner);
    ASSERT_EQ(0, fx.cp().progress_team)
        << "a 2v1 strict majority accrues for the attackers";
    ASSERT_EQ(og::sim::kCtfCpCaptureTicks - 1, fx.cp().progress);

    fx.tick();
    ASSERT_EQ(0, fx.cp().owner)
        << "a 2v1 majority retakes through the contesting owner";
}

TEST(CtfCore, equal_presence_holds_meter_without_reset)
{
    RetakeWorld fx;
    fx.cp().progress = 10;
    fx.cp().progress_team = 0;

    fx.attacker->setxy(336, 320);
    fx.owner_bot->setxy(320, 352); // 1v1: no strict majority
    fx.tick(20);
    ASSERT_EQ(1, fx.cp().owner);
    ASSERT_EQ(10, fx.cp().progress) << "an even contest freezes the meter";
    ASSERT_EQ(0, fx.cp().progress_team)
        << "an even contest must not reset the contender";
}

TEST(CtfCore, owner_dominance_decays_progress_stepwise)
{
    RetakeWorld fx;
    fx.cp().progress = 30;
    fx.cp().progress_team = 0;

    fx.owner_bot->setxy(336, 320); // owner alone in the disc
    fx.tick(5);
    ASSERT_EQ(25, fx.cp().progress)
        << "owner-alone ticks decay progress one step per tick";
    ASSERT_EQ(0, fx.cp().progress_team)
        << "decay keeps the contending team until the meter empties";

    fx.tick(25);
    ASSERT_EQ(0, fx.cp().progress);
    ASSERT_EQ(-1, fx.cp().progress_team)
        << "draining to zero clears the contender";

    fx.tick(5);
    ASSERT_EQ(0, fx.cp().progress) << "an empty meter stays empty";
    ASSERT_EQ(1, fx.cp().owner);
}

TEST(CtfCore, radius_edge_geometry_contests_at_exactly_48px)
{
    RetakeWorld fx;
    fx.attacker->setxy(336, 320); // on the pad
    // radius = 3 tiles * 16px = 48px, compared top-left to top-left.
    fx.owner_bot->setxy(320 + 48, 320);
    fx.tick(10);
    ASSERT_EQ(0, fx.cp().progress)
        << "an enemy at exactly 48px is inside the disc and contests (1v1)";

    fx.owner_bot->setxy(320 + 49, 320);
    fx.tick(10);
    ASSERT_EQ(10, fx.cp().progress)
        << "one px past the radius no longer contests";
    ASSERT_EQ(0, fx.cp().progress_team);
    ASSERT_EQ(1, fx.cp().owner);
}

TEST(CtfCore, control_point_pulse_is_localized_to_owner_team)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 800);
    fx.spawn_point(320, 320);
    walker* near_member = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 320);
    walker* far_member = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 800);
    walker* enemy_near = fx.spawn_living(FAMILY_SOLDIER, 1, 320, 250);
    fx.tick();

    // Enemy walks out of the radius so team 0 holds the point alone.
    enemy_near->setxy(544, 700);
    fx.tick(og::sim::kCtfCpCaptureTicks + 2);
    ASSERT_EQ(0, fx.world().ctf.cps[0].owner);

    // The pulse fires on the capture tick: near owner-team livings only.
    ASSERT_EQ(1.0f, near_member->speed_bonus());
    ASSERT_GT(near_member->speed_bonus_left(), 0);
    ASSERT_EQ(0.0f, far_member->speed_bonus());
    ASSERT_EQ(0, enemy_near->speed_bonus_left());
}

// --- Win conditions ------------------------------------------------------

TEST(CtfCore, capture_limit_win_sets_match_end_shape)
{
    CtfWorld fx(500);
    walker* flag0 = fx.spawn_flag(0, 96, 96);
    walker* flag1 = fx.spawn_flag(1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 11;
    fx.world().ctf_requested_capture_limit = 1;
    fx.tick();
    ASSERT_EQ(1, fx.world().ctf.capture_limit);

    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag0->eat_me(runner));
    ASSERT_EQ(1, fx.world().ctf.captures[0]);

    fx.tick();
    ASSERT_TRUE(fx.world().game_ended);
    ASSERT_EQ(0, fx.world().ending);
    ASSERT_EQ(0, fx.world().ctf.winner_team);
    ASSERT_TRUE(fx.world().ctf.winner_is_player);
    ASSERT_EQ(501, fx.world().next_level) << "human win advances the campaign";
    ASSERT_TRUE(has_notification(fx.events, "RED TEAM WINS!"))
        << "match-end notify uses the team color name, not a bare number";
}

TEST(CtfCore, bot_win_keeps_same_map_cursor)
{
    CtfWorld fx(500);
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 800);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick();

    fx.world().ctf.captures[1] = fx.world().ctf.capture_limit;
    fx.tick();
    ASSERT_TRUE(fx.world().game_ended);
    ASSERT_EQ(1, fx.world().ctf.winner_team);
    ASSERT_FALSE(fx.world().ctf.winner_is_player);
    ASSERT_EQ(500, fx.world().next_level) << "bot win replays the same map";
}

TEST(CtfCore, time_limit_win_picks_leader_with_tiebreakers)
{
    // Capture lead wins.
    {
        CtfWorld fx;
        fx.spawn_flag(0, 96, 96);
        fx.spawn_flag(1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.tick();
        fx.world().ctf.captures[1] = 2;
        fx.world().ctf.captures[0] = 1;
        fx.world().ctf.time_limit_ticks = fx.world().level_tick_count() + 2;
        fx.tick(2);
        ASSERT_TRUE(fx.world().game_ended);
        ASSERT_EQ(1, fx.world().ctf.winner_team);
    }
    // Captures tied: larger m_score wins.
    {
        CtfWorld fx;
        fx.spawn_flag(0, 96, 96);
        fx.spawn_flag(1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.tick();
        fx.world().m_score[1] = 700;
        fx.world().ctf.time_limit_ticks = fx.world().level_tick_count() + 2;
        fx.tick(2);
        ASSERT_TRUE(fx.world().game_ended);
        ASSERT_EQ(1, fx.world().ctf.winner_team);
    }
    // Full tie: smaller team index wins.
    {
        CtfWorld fx;
        fx.spawn_flag(0, 96, 96);
        fx.spawn_flag(1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.tick();
        fx.world().ctf.time_limit_ticks = fx.world().level_tick_count() + 2;
        fx.tick(2);
        ASSERT_TRUE(fx.world().game_ended);
        ASSERT_EQ(0, fx.world().ctf.winner_team);
    }
}

// --- Determinism ----------------------------------------------------------

TEST(CtfCore, ctf_bot_match_is_deterministic_across_runs)
{
    const WorldDigest first = run_ctf_bot_match(300);
    const WorldDigest second = run_ctf_bot_match(300);

    ASSERT_TRUE(first.ctf_active);
    ASSERT_TRUE(second.ctf_active);
    ASSERT_EQ(first, second)
        << "same seed + same scripted CTF scenario must replay byte-identically";
}

namespace {

// Scripted teleport-rule workout: a real pad ride (carries through), a
// staged blink (drop), and a bot match running underneath (squad mages and
// skeletons may genuinely blink on the world RNG, exercising the marker in
// anger). Returns the digest plus the drop announcements.
struct TeleportRunResult
{
    WorldDigest digest;
    int dropped_notifications = 0;

    bool operator==(const TeleportRunResult& o) const = default;
};

TeleportRunResult run_ctf_teleport_script(int ticks)
{
    CtfWorld fx(500);
    fx.spawn_flag(0, 160, 160);
    walker* flag1 = fx.spawn_flag(1, 480, 800);
    walker* padA = fx.spawn_teleporter(320, 320);
    fx.spawn_teleporter(480, 640);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(1, 512, 832);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.world().ctf_requested_respawn_ticks = 30;
    fx.tick(); // team 1 fields a five-bot squad (mage and skeleton included)

    runner->center_on(padA);
    flag1->eat_me(runner);
    fx.tick();
    (void)fx.world().query_passable(static_cast<float>(padA->xpos()),
                                    static_cast<float>(padA->ypos()),
                                    runner);
    fx.tick(5); // pad ride carried the flag through; settle
    fx.stage_self_teleport(runner);
    runner->setxy(112, 200); // staged blink: drop
    fx.tick(ticks);

    TeleportRunResult result;
    result.digest = digest_world(fx.world());
    result.dropped_notifications = count_notifications(fx.events, "DROPPED");
    return result;
}

} // namespace

TEST(CtfCore, teleport_rule_is_deterministic_across_runs)
{
    const TeleportRunResult first = run_ctf_teleport_script(100);
    const TeleportRunResult second = run_ctf_teleport_script(100);

    ASSERT_TRUE(first.digest.ctf_active);
    ASSERT_GE(first.dropped_notifications, 1)
        << "the scripted blink must have dropped the flag";
    ASSERT_EQ(first, second)
        << "the teleport rule reads only deterministic position/entity state";
}
