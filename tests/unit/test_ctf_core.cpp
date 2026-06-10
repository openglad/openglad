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
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>

#include "test_game_world_fixture.h"

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

    runner->setxy(260, 240);
    fx.tick();
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
