// Respawn engine tests (retargeted off the retired CTF engine): player
// revive-in-place, AI respawns, stain/gem scrubbing, win-latch revive-all,
// queue capping, spawn probes, and the team-wipe / input gates — all through
// the scripted-mode frame (Lua owns eligibility via og.respawn_schedule;
// respawn_schedule_corpse is that binding's backend).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/campaign_ids.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>

#include "test_game_world_fixture.h"

#include <algorithm>
#include <memory>

namespace {

loader& respawn_test_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

struct ScriptedWorld : TestGameWorld
{
    explicit ScriptedWorld(int level_id = 500)
        : TestGameWorld(level_id)
    {
        loader* game_loader = &respawn_test_loader();
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
        world().type = GameWorld::TYPE_SCRIPTED;
        // Hand-arm the mode (a mounted pack's on_mode_init would do this on
        // the first scripted tick).
        world().mode.active = true;
        world().mode.init_attempted = true;
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

// Standard respawn arena: anchors for both teams (scanned, then consumed the
// way the level bootstrap consumes markers), one stationary living per team,
// short respawn timer.
struct RespawnArena
{
    ScriptedWorld fx;
    walker* runner = nullptr; // team 0
    walker* enemy = nullptr;  // team 1

    RespawnArena()
    {
        walker* a0 = fx.spawn_anchor(0, 128, 128);
        walker* a1 = fx.spawn_anchor(0, 192, 128);
        walker* a2 = fx.spawn_anchor(1, 512, 832);
        og::sim::respawn_scan_anchors(fx.world());
        for (walker* marker : {a0, a1, a2})
        {
            if (marker != nullptr)
                marker->set_dead(1);
        }
        runner = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
        enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 480, 760);
        fx.world().respawn.respawn_ticks = 6;
        fx.tick();
        EXPECT_TRUE(fx.world().mode.active);
    }

    GameWorld& world() { return fx.world(); }

    void kill(walker* w)
    {
        w->set_dead(1);
        w->death();
    }

    // The mode Lua's on_entity_death eligibility call.
    bool schedule(walker* w, int ticks_override = 0)
    {
        return og::sim::respawn_schedule_corpse(fx.world(), w,
                                                ticks_override);
    }
};

bool any_alive_with(GameWorld& world, int family, int team,
                    std::uint32_t excluding_id = 0)
{
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->family() == family &&
            w->team_num() == static_cast<unsigned char>(team) &&
            w->entity_id() != excluding_id)
        {
            return true;
        }
    }
    return false;
}

walker* find_alive_with(GameWorld& world, int family, int team,
                        std::uint32_t excluding_id = 0)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->family() == family &&
            w->team_num() == static_cast<unsigned char>(team) &&
            w->entity_id() != excluding_id)
        {
            return w;
        }
    }
    return nullptr;
}

int live_stains_for_team(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.fxlist)
    {
        const walker* fxw = uptr.get();
        if (fxw != nullptr && !fxw->dead() &&
            fxw->query_order() == Order::Treasure &&
            fxw->family() == FAMILY_STAIN &&
            fxw->team_num() == static_cast<unsigned char>(team))
        {
            count++;
        }
    }
    return count;
}

} // namespace

TEST(RespawnEngine, player_corpse_revives_in_place_with_control_preserved)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;
    runner->set_user(0);
    runner->set_act_type(ACT_CONTROL);
    const std::uint32_t corpse_id = runner->entity_id();

    arena.kill(runner);
    ASSERT_TRUE(arena.schedule(runner));
    ASSERT_EQ(1u, arena.world().respawn.respawn_queue.size());
    const og::sim::RespawnEntry& entry = arena.world().respawn.respawn_queue[0];
    ASSERT_EQ(0, entry.kind);
    ASSERT_EQ(corpse_id, entry.walker_entity_id);
    ASSERT_EQ(6, entry.ticks_left);

    arena.fx.tick(5);
    ASSERT_TRUE(runner->dead()) << "timer must run out before the revive";

    arena.fx.tick();
    ASSERT_FALSE(runner->dead());
    ASSERT_EQ(corpse_id, runner->entity_id()) << "same walker, same entity id";
    ASSERT_EQ(runner->stats()->max_hitpoints(), runner->stats()->hitpoints());
    ASSERT_EQ(nullptr, runner->foe());
    ASSERT_EQ(nullptr, runner->leader());
    ASSERT_TRUE(runner->stats()->commands.empty());
    ASSERT_EQ(0, runner->user()) << "control binding survives the revive";
    ASSERT_EQ(ACT_CONTROL, runner->act_type());
    ASSERT_EQ(320, runner->xpos()) << "engine default: revive in place";
    ASSERT_EQ(320, runner->ypos());
    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
}

TEST(RespawnEngine, user_conflict_demotes_revived_walker_to_ai)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;
    runner->set_user(0);
    runner->set_act_type(ACT_CONTROL);

    // The player switches bodies while dead: another walker takes user 0.
    walker* replacement = arena.fx.spawn_living(FAMILY_SOLDIER, 0, 256, 320);
    replacement->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    replacement->myguy->id = 42;
    replacement->set_user(0);
    replacement->set_act_type(ACT_CONTROL);

    arena.kill(runner);
    ASSERT_TRUE(arena.schedule(runner));
    arena.fx.tick(7);

    ASSERT_FALSE(runner->dead());
    ASSERT_EQ(-1, runner->user()) << "conflicting binding joins the AI pool";
    ASSERT_EQ(ACT_RANDOM, runner->act_type());
    ASSERT_EQ(0, replacement->user());
    ASSERT_EQ(ACT_CONTROL, replacement->act_type());
}

TEST(RespawnEngine, ai_respawn_spawns_fresh_walker_of_same_family_level_team)
{
    RespawnArena arena;
    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    bot->stats()->set_level(2);
    const std::uint32_t old_id = bot->entity_id();

    arena.kill(bot);
    ASSERT_TRUE(arena.schedule(bot));
    ASSERT_EQ(1u, arena.world().respawn.respawn_queue.size());
    ASSERT_EQ(1, arena.world().respawn.respawn_queue[0].kind);
    ASSERT_EQ(FAMILY_ARCHER, arena.world().respawn.respawn_queue[0].family);
    ASSERT_EQ(2, arena.world().respawn.respawn_queue[0].level);
    ASSERT_EQ(1, arena.world().respawn.respawn_queue[0].team);
    ASSERT_FALSE(any_alive_with(arena.world(), FAMILY_ARCHER, 1));

    arena.fx.tick(7);
    const walker* fresh = find_alive_with(arena.world(), FAMILY_ARCHER, 1);
    ASSERT_NE(nullptr, fresh);
    ASSERT_NE(old_id, fresh->entity_id()) << "AI respawn is a fresh walker";
    ASSERT_EQ(2, fresh->stats()->level());
    ASSERT_EQ(255, fresh->real_team_num());
    ASSERT_EQ(480, fresh->xpos()) << "the entry carries the corpse spot";
    ASSERT_EQ(700, fresh->ypos());
    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
}

TEST(RespawnEngine, scheduling_scrubs_the_fresh_corpse_stain)
{
    RespawnArena arena;
    walker* bot = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 480, 700);

    arena.kill(bot);
    ASSERT_EQ(1, live_stains_for_team(arena.world(), 1))
        << "death must leave a bloodstain before the scrub";

    ASSERT_TRUE(arena.schedule(bot));
    ASSERT_EQ(0, live_stains_for_team(arena.world(), 1))
        << "scheduling a respawn must kill the corpse stain";
}

TEST(RespawnEngine, live_duplicate_character_cancels_the_revive)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 7;

    // A live walker already bound to the same character (cleric resurrect).
    walker* duplicate = arena.fx.spawn_living(FAMILY_SOLDIER, 0, 256, 320);
    duplicate->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    duplicate->myguy->id = 7;

    arena.kill(runner);
    EXPECT_FALSE(og::sim::respawn_retains_player_control(
        arena.world(), runner))
        << "a cleric-resurrected copy must be claimable immediately";
    EXPECT_FALSE(arena.schedule(runner))
        << "the schedule dedupe must refuse a live duplicate";
    arena.fx.tick(7);

    ASSERT_TRUE(runner->dead()) << "duplicate character must cancel the revive";
    ASSERT_FALSE(duplicate->dead());
    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
}

TEST(RespawnEngine, retains_player_control_through_pending_scripted_entry)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 8;

    arena.kill(runner);
    EXPECT_FALSE(og::sim::respawn_retains_player_control(arena.world(),
                                                         runner))
        << "no queued entry yet: the seat is claimable";
    ASSERT_TRUE(arena.schedule(runner));
    EXPECT_TRUE(og::sim::respawn_retains_player_control(arena.world(),
                                                        runner))
        << "queued entry + undecided match keeps the seat";

    og::sim::mode_declare_winner(arena.world(), 1);
    EXPECT_FALSE(og::sim::respawn_retains_player_control(arena.world(),
                                                         runner))
        << "a decided match releases the seat keep-alive";
}

TEST(RespawnEngine, win_latch_flush_revives_all_player_corpses)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;

    arena.kill(runner);
    ASSERT_TRUE(arena.schedule(runner));
    ASSERT_EQ(1u, arena.world().respawn.respawn_queue.size());
    ASSERT_TRUE(runner->dead());

    // og.declare_winner: first arming flushes the respawn engine (D2)
    // before winner math, and the engine re-asserts the win every tick.
    og::sim::mode_declare_winner(arena.world(), 1);
    arena.fx.tick();

    ASSERT_TRUE(arena.world().game_ended);
    ASSERT_EQ(1, arena.world().mode.winner_team);
    ASSERT_FALSE(runner->dead())
        << "match end must revive pending player corpses before the merge";
    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
}

TEST(RespawnEngine, queue_cap_evicts_oldest_ai_entry_for_a_player)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;

    auto& queue = arena.world().respawn.respawn_queue;
    for (int i = 0; i < og::sim::kRespawnMaxQueueEntries; ++i)
    {
        og::sim::RespawnEntry filler;
        filler.kind = 1;
        filler.team = 1;
        filler.family = FAMILY_SOLDIER;
        filler.level = 1;
        filler.ticks_left = 5000;
        filler.walker_entity_id = 900000u + static_cast<std::uint32_t>(i);
        queue.push_back(filler);
    }

    arena.kill(runner);
    ASSERT_TRUE(arena.schedule(runner));

    ASSERT_EQ(static_cast<std::size_t>(og::sim::kRespawnMaxQueueEntries),
              queue.size());
    const bool player_queued = std::any_of(
        queue.begin(), queue.end(), [&](const og::sim::RespawnEntry& e) {
            return e.kind == 0 && e.walker_entity_id == runner->entity_id();
        });
    ASSERT_TRUE(player_queued);
    const bool oldest_ai_evicted = std::none_of(
        queue.begin(), queue.end(), [](const og::sim::RespawnEntry& e) {
            return e.walker_entity_id == 900000u;
        });
    ASSERT_TRUE(oldest_ai_evicted);
}

TEST(RespawnEngine, queue_full_of_players_drops_incoming_ai_entry)
{
    RespawnArena arena;
    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);

    auto& queue = arena.world().respawn.respawn_queue;
    for (int i = 0; i < og::sim::kRespawnMaxQueueEntries; ++i)
    {
        og::sim::RespawnEntry filler;
        filler.kind = 0;
        filler.team = 0;
        filler.ticks_left = 5000;
        filler.walker_entity_id = 800000u + static_cast<std::uint32_t>(i);
        queue.push_back(filler);
    }

    bot->set_dead(1);
    (void)arena.schedule(bot);

    ASSERT_EQ(static_cast<std::size_t>(og::sim::kRespawnMaxQueueEntries),
              queue.size());
    const bool any_ai = std::any_of(
        queue.begin(), queue.end(),
        [](const og::sim::RespawnEntry& e) { return e.kind == 1; });
    ASSERT_FALSE(any_ai) << "player entries are never evicted for AI";
}

TEST(RespawnEngine, team_wipe_suppression_tracks_match_lifecycle)
{
    // Classic world: no suppression (respawn_mode 0).
    {
        ScriptedWorld fx;
        fx.world().type = 0;
        fx.world().mode = og::sim::ModeState{};
        ASSERT_FALSE(og::sim::respawn_suppress_team_wipe_endgame(fx.world()));
    }
    // Scripted world whose mode failed activation: no suppression.
    {
        ScriptedWorld fx;
        fx.world().mode.active = false;
        ASSERT_FALSE(og::sim::respawn_suppress_team_wipe_endgame(fx.world()));
    }
    // Undecided scripted match: suppressed. After the win latch: released.
    {
        RespawnArena arena;
        ASSERT_TRUE(og::sim::respawn_suppress_team_wipe_endgame(arena.world()));
        og::sim::mode_declare_winner(arena.world(), 0);
        arena.fx.tick();
        ASSERT_TRUE(arena.world().game_ended);
        ASSERT_FALSE(
            og::sim::respawn_suppress_team_wipe_endgame(arena.world()));
    }
}

TEST(RespawnEngine, charmed_corpse_respawns_on_its_true_team)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;

    // Charm flips team_num and parks the true team in real_team_num.
    runner->set_real_team_num(runner->team_num());
    runner->set_team_num(1);

    arena.kill(runner);
    ASSERT_TRUE(arena.schedule(runner));
    arena.fx.tick(7);

    ASSERT_FALSE(runner->dead()) << "charmed corpse must still respawn";
    ASSERT_EQ(0, runner->team_num()) << "respawn breaks the charm";
    ASSERT_EQ(255, runner->real_team_num());
}

// Guy ids are only unique per owning player (each networked client numbers
// its roster from its own counter): a same-id walker owned by ANOTHER player
// is a different character and must not block the respawn.
TEST(RespawnEngine, same_guy_id_under_a_different_owner_does_not_block_revive)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 7;
    runner->myguy->owner_player_index = 0;
    runner->myguy->owner_save_slot = 0;

    walker* other_players_guy = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 480, 720);
    other_players_guy->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    other_players_guy->myguy->id = 7; // same id, different owner
    other_players_guy->myguy->owner_player_index = 1;
    other_players_guy->myguy->owner_save_slot = 0;

    arena.kill(runner);
    ASSERT_TRUE(arena.schedule(runner));
    arena.fx.tick(7);

    ASSERT_FALSE(runner->dead())
        << "another player's same-id character must not block the revive";
    ASSERT_FALSE(other_players_guy->dead());
}

// Player corpses drop a LIFE_GEM centered on the body; respawn scheduling
// must scrub it (and the stain) or respawn cycles farm tiebreaker score.
TEST(RespawnEngine, scheduling_scrubs_the_corpse_life_gem)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 31;

    const auto count_gems = [&arena] {
        int gems = 0;
        for (const auto& uptr : arena.world().fxlist)
        {
            const walker* w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->query_order() == Order::Treasure &&
                w->family() == FAMILY_LIFE_GEM)
            {
                ++gems;
            }
        }
        for (const auto& uptr : arena.world().oblist)
        {
            const walker* w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->query_order() == Order::Treasure &&
                w->family() == FAMILY_LIFE_GEM)
            {
                ++gems;
            }
        }
        return gems;
    };

    arena.kill(runner);
    ASSERT_GE(count_gems(), 1) << "a player corpse must drop its heart first";

    ASSERT_TRUE(arena.schedule(runner));
    ASSERT_EQ(0, count_gems())
        << "scheduling the respawn must scrub the fresh gem";
}

// A corpse that died charmed revives on its true team at MATCH END too (the
// win-path revive must not freeze the charm onto the charmer's team).
TEST(RespawnEngine, win_latch_flush_breaks_charm_onto_true_team)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 53;

    runner->set_real_team_num(0); // charmed off team 0...
    runner->set_team_num(1);      // ...onto team 1
    arena.kill(runner);
    ASSERT_TRUE(arena.schedule(runner)); // scheduled, still pending

    og::sim::mode_declare_winner(arena.world(), 1);
    arena.fx.tick(); // the win latch re-asserts; the flush already revived

    ASSERT_TRUE(arena.world().game_ended);
    ASSERT_FALSE(runner->dead()) << "match end must revive pending corpses";
    ASSERT_EQ(0, runner->team_num()) << "revive breaks the charm";
    ASSERT_EQ(255, runner->real_team_num());
    ASSERT_EQ(0, runner->charm_left());
}

// Spawn probes must see blockers that straddle obmap buckets and blocking
// scenery (doors), and must never run treasure pickup side effects. The
// probe backs og.spawn_spot_clear — a mode's on_respawn placement runs on
// exactly these answers.
TEST(RespawnEngine, spawn_probe_rejects_doors_and_cross_bucket_blockers)
{
    ScriptedWorld fx;
    // Spot 1: covered by a door. Spot 2: unaligned (150,128) so its 16px
    // spawn bbox spans two 32px obmap buckets; the lurker lives only in the
    // second bucket. Spot 3: clear.
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, runner);

    walker* door = fx.world().add_weap_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_NE(nullptr, door);
    door->setxy(128, 96);
    door->set_team_num(7);
    walker* lurker = fx.spawn_living(FAMILY_SOLDIER, 1, 162, 128);
    ASSERT_NE(nullptr, lurker);
    fx.tick();

    EXPECT_FALSE(og::sim::respawn_spot_clear(fx.world(), runner, 128, 96, -1))
        << "a door must veto the spot";
    EXPECT_FALSE(og::sim::respawn_spot_clear(fx.world(), runner, 150, 128, -1))
        << "a cross-bucket living must veto the spot";
    EXPECT_TRUE(og::sim::respawn_spot_clear(fx.world(), runner, 224, 128, -1))
        << "a clear spot must pass the probe";
}
