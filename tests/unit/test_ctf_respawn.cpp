// Capture-the-flag respawn tests: player revive-in-place, AI respawns,
// anchor rotation, stain scrubbing, match-end revive, queue capping, and
// the team-wipe / input gates.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>

#include "test_game_world_fixture.h"

#include <algorithm>
#include <memory>

namespace {

loader& ctf_test_loader()
{
    static loader instance{EntityFactory{}};
    static const bool registered = [] {
        return true;
    }();
    (void)registered;
    return instance;
}

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

// Standard respawn arena: two flag teams, anchors for team 0, one stationary
// living per team, short respawn timer.
struct RespawnArena
{
    CtfWorld fx;
    walker* runner = nullptr; // team 0
    walker* enemy = nullptr;  // team 1

    RespawnArena()
    {
        fx.spawn_flag(0, 96, 96);
        fx.spawn_flag(1, 544, 800);
        fx.spawn_anchor(0, 128, 128);
        fx.spawn_anchor(0, 192, 128);
        fx.spawn_anchor(1, 512, 832);
        runner = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
        enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 480, 760);
        fx.world().ctf_requested_respawn_ticks = 6;
        fx.tick();
        EXPECT_TRUE(fx.world().ctf.active);
    }

    GameWorld& world() { return fx.world(); }

    void kill(walker* w)
    {
        w->set_dead(1);
        w->death();
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

TEST(CtfRespawn, player_corpse_revives_in_place_with_control_preserved)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;
    runner->set_user(0);
    runner->set_act_type(ACT_CONTROL);
    const std::uint32_t corpse_id = runner->entity_id();

    arena.kill(runner);
    arena.fx.tick();
    ASSERT_EQ(1u, arena.world().respawn.respawn_queue.size());
    const og::sim::CtfRespawnEntry& entry = arena.world().respawn.respawn_queue[0];
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
    ASSERT_EQ(128, runner->xpos());
    ASSERT_EQ(128, runner->ypos());
    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
}

TEST(CtfRespawn, user_conflict_demotes_revived_walker_to_ai)
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
    arena.fx.tick(7);

    ASSERT_FALSE(runner->dead());
    ASSERT_EQ(-1, runner->user()) << "conflicting binding joins the AI pool";
    ASSERT_EQ(ACT_RANDOM, runner->act_type());
    ASSERT_EQ(0, replacement->user());
    ASSERT_EQ(ACT_CONTROL, replacement->act_type());
}

TEST(CtfRespawn, ai_respawn_spawns_fresh_walker_of_same_family_level_team)
{
    RespawnArena arena;
    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    bot->stats()->set_level(2);
    const std::uint32_t old_id = bot->entity_id();

    arena.kill(bot);
    arena.fx.tick();
    ASSERT_EQ(1u, arena.world().respawn.respawn_queue.size());
    ASSERT_EQ(1, arena.world().respawn.respawn_queue[0].kind);
    ASSERT_EQ(FAMILY_ARCHER, arena.world().respawn.respawn_queue[0].family);
    ASSERT_EQ(2, arena.world().respawn.respawn_queue[0].level);
    ASSERT_EQ(1, arena.world().respawn.respawn_queue[0].team);
    ASSERT_FALSE(any_alive_with(arena.world(), FAMILY_ARCHER, 1));

    arena.fx.tick(6);
    const walker* fresh = find_alive_with(arena.world(), FAMILY_ARCHER, 1);
    ASSERT_NE(nullptr, fresh);
    ASSERT_NE(old_id, fresh->entity_id()) << "AI respawn is a fresh walker";
    ASSERT_EQ(2, fresh->stats()->level());
    ASSERT_EQ(255, fresh->real_team_num());
    ASSERT_EQ(512, fresh->xpos());
    ASSERT_EQ(832, fresh->ypos());
    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
}

TEST(CtfRespawn, anchors_rotate_and_impassable_anchors_are_skipped)
{
    RespawnArena arena;
    walker* bot_a = arena.fx.spawn_living(FAMILY_ARCHER, 0, 320, 200);
    walker* bot_b = arena.fx.spawn_living(FAMILY_THIEF, 0, 352, 200);

    // First respawn rotates to the first team-0 anchor (128, 128).
    arena.kill(bot_a);
    arena.fx.tick(7);
    walker* first = find_alive_with(arena.world(), FAMILY_ARCHER, 0);
    ASSERT_NE(nullptr, first);
    ASSERT_EQ(128, first->xpos());
    ASSERT_EQ(128, first->ypos());

    // Wall off the second anchor (192, 128): the rotation must skip it and
    // wrap back to the first anchor. Clear the first anchor of the previous
    // respawn so the wrap target is open.
    first->setxy(320, 240);
    first->set_act_type(ACT_CONTROL);
    PixieData& grid = arena.world().grid;
    for (int gy = 7; gy <= 9; ++gy)
        for (int gx = 11; gx <= 13; ++gx)
            grid.data[gx + grid.w * gy] = PIX_H_WALL1;

    arena.kill(bot_b);
    arena.fx.tick(7);
    const walker* second = find_alive_with(arena.world(), FAMILY_THIEF, 0);
    ASSERT_NE(nullptr, second);
    ASSERT_EQ(128, second->xpos()) << "impassable anchor must be skipped";
    ASSERT_EQ(128, second->ypos());
}

TEST(CtfRespawn, scheduling_scrubs_the_fresh_corpse_stain)
{
    RespawnArena arena;
    walker* bot = arena.fx.spawn_living(FAMILY_SOLDIER, 1, 480, 700);

    arena.kill(bot);
    ASSERT_EQ(1, live_stains_for_team(arena.world(), 1))
        << "death must leave a bloodstain before the scrub";

    arena.fx.tick();
    ASSERT_EQ(0, live_stains_for_team(arena.world(), 1))
        << "scheduling a respawn must kill the corpse stain";
}

TEST(CtfRespawn, live_duplicate_character_cancels_the_revive)
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
    arena.fx.tick(7);

    ASSERT_TRUE(runner->dead()) << "duplicate character must cancel the revive";
    ASSERT_FALSE(duplicate->dead());
    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
}

TEST(CtfRespawn, owned_walkers_are_left_to_their_generator)
{
    RespawnArena arena;
    walker* summon = arena.fx.spawn_living(FAMILY_SKELETON, 1, 480, 700);
    summon->set_owner(arena.enemy);

    summon->set_dead(1);
    arena.fx.tick();

    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty())
        << "walkers with an owner are not CTF-respawned";
}

TEST(CtfRespawn, match_end_revive_restores_all_player_corpses)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;

    arena.kill(runner);
    arena.fx.tick();
    ASSERT_EQ(1u, arena.world().respawn.respawn_queue.size());
    ASSERT_TRUE(runner->dead());

    arena.world().ctf.captures[1] = arena.world().ctf.capture_limit;
    arena.fx.tick();

    ASSERT_TRUE(arena.world().game_ended);
    ASSERT_EQ(1, arena.world().ctf.winner_team);
    ASSERT_FALSE(runner->dead())
        << "match end must revive pending player corpses before the merge";
    ASSERT_TRUE(arena.world().respawn.respawn_queue.empty());
}

TEST(CtfRespawn, queue_cap_evicts_oldest_ai_entry_for_a_player)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;

    auto& queue = arena.world().respawn.respawn_queue;
    for (int i = 0; i < og::sim::kCtfMaxRespawnEntries; ++i)
    {
        og::sim::CtfRespawnEntry filler;
        filler.kind = 1;
        filler.team = 1;
        filler.family = FAMILY_SOLDIER;
        filler.level = 1;
        filler.ticks_left = 5000;
        filler.walker_entity_id = 900000u + static_cast<std::uint32_t>(i);
        queue.push_back(filler);
    }

    arena.kill(runner);
    arena.fx.tick();

    ASSERT_EQ(static_cast<std::size_t>(og::sim::kCtfMaxRespawnEntries),
              queue.size());
    const bool player_queued = std::any_of(
        queue.begin(), queue.end(), [&](const og::sim::CtfRespawnEntry& e) {
            return e.kind == 0 && e.walker_entity_id == runner->entity_id();
        });
    ASSERT_TRUE(player_queued);
    const bool oldest_ai_evicted = std::none_of(
        queue.begin(), queue.end(), [](const og::sim::CtfRespawnEntry& e) {
            return e.walker_entity_id == 900000u;
        });
    ASSERT_TRUE(oldest_ai_evicted);
}

TEST(CtfRespawn, queue_full_of_players_drops_incoming_ai_entry)
{
    RespawnArena arena;
    walker* bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);

    auto& queue = arena.world().respawn.respawn_queue;
    for (int i = 0; i < og::sim::kCtfMaxRespawnEntries; ++i)
    {
        og::sim::CtfRespawnEntry filler;
        filler.kind = 0;
        filler.team = 0;
        filler.ticks_left = 5000;
        filler.walker_entity_id = 800000u + static_cast<std::uint32_t>(i);
        queue.push_back(filler);
    }

    bot->set_dead(1);
    arena.fx.tick();

    ASSERT_EQ(static_cast<std::size_t>(og::sim::kCtfMaxRespawnEntries),
              queue.size());
    const bool any_ai = std::any_of(
        queue.begin(), queue.end(),
        [](const og::sim::CtfRespawnEntry& e) { return e.kind == 1; });
    ASSERT_FALSE(any_ai) << "player entries are never evicted for AI";
}

TEST(CtfRespawn, owning_a_control_point_doubles_the_respawn_decrement)
{
    RespawnArena arena;
    arena.world().ctf.cp_count = 1;
    arena.world().ctf.cps[0].owner = 0;
    arena.world().ctf.cps[0].x = 600;
    arena.world().ctf.cps[0].y = 920;
    arena.world().ctf.cps[0].next_pulse_tick = 0xffffffffu;

    walker* friendly_bot = arena.fx.spawn_living(FAMILY_ARCHER, 0, 320, 200);
    walker* enemy_bot = arena.fx.spawn_living(FAMILY_ARCHER, 1, 480, 700);
    friendly_bot->set_dead(1);
    enemy_bot->set_dead(1);
    arena.fx.tick();
    ASSERT_EQ(2u, arena.world().respawn.respawn_queue.size());
    ASSERT_EQ(6, arena.world().respawn.respawn_queue[0].ticks_left);
    ASSERT_EQ(6, arena.world().respawn.respawn_queue[1].ticks_left);

    arena.fx.tick();
    const auto& queue = arena.world().respawn.respawn_queue;
    ASSERT_EQ(2u, queue.size());
    int team0_left = -1;
    int team1_left = -1;
    for (const auto& entry : queue)
    {
        if (entry.team == 0)
            team0_left = entry.ticks_left;
        else if (entry.team == 1)
            team1_left = entry.ticks_left;
    }
    ASSERT_EQ(4, team0_left) << "CP owner ticks down twice as fast";
    ASSERT_EQ(5, team1_left);
}

TEST(CtfRespawn, team_wipe_suppression_tracks_match_lifecycle)
{
    // Classic world: no suppression.
    {
        CtfWorld fx;
        fx.world().type = 0;
        ASSERT_FALSE(og::sim::ctf_suppress_team_wipe_endgame(fx.world()));
    }
    // CTF world before init: inactive, no suppression.
    {
        CtfWorld fx;
        ASSERT_FALSE(og::sim::ctf_suppress_team_wipe_endgame(fx.world()));
    }
    // Active match: suppressed. After the win: released.
    {
        RespawnArena arena;
        ASSERT_TRUE(og::sim::ctf_suppress_team_wipe_endgame(arena.world()));
        arena.world().ctf.captures[0] = arena.world().ctf.capture_limit;
        arena.fx.tick();
        ASSERT_TRUE(arena.world().game_ended);
        ASSERT_FALSE(og::sim::ctf_suppress_team_wipe_endgame(arena.world()));
    }
}

TEST(CtfRespawn, charmed_corpse_respawns_on_its_true_team)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 41;

    // Charm flips team_num and parks the true team in real_team_num.
    runner->set_real_team_num(runner->team_num());
    runner->set_team_num(1);

    arena.kill(runner);
    arena.fx.tick(7);

    ASSERT_FALSE(runner->dead()) << "charmed corpse must still respawn";
    ASSERT_EQ(0, runner->team_num()) << "respawn breaks the charm";
    ASSERT_EQ(255, runner->real_team_num());
}

// Guy ids are only unique per owning player (each networked client numbers
// its roster from its own counter): a same-id walker owned by ANOTHER player
// is a different character and must not block the respawn.
TEST(CtfRespawn, same_guy_id_under_a_different_owner_does_not_block_revive)
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
    arena.fx.tick(7);

    ASSERT_FALSE(runner->dead())
        << "another player's same-id character must not block the revive";
    ASSERT_FALSE(other_players_guy->dead());
}

// Player corpses drop a LIFE_GEM centered on the body; respawn scheduling
// must scrub it (and the stain) or respawn cycles farm tiebreaker score.
TEST(CtfRespawn, scheduling_scrubs_the_corpse_life_gem)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 31;

    arena.kill(runner);
    int gems = 0;
    for (const auto& uptr : arena.world().fxlist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Treasure &&
            w->family() == FAMILY_LIFE_GEM)
        {
            ++gems;
        }
    }
    for (const auto& uptr : arena.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Treasure &&
            w->family() == FAMILY_LIFE_GEM)
        {
            ++gems;
        }
    }
    ASSERT_GE(gems, 1) << "a player corpse must drop its heart first";

    arena.fx.tick();
    gems = 0;
    for (const auto& uptr : arena.world().fxlist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Treasure &&
            w->family() == FAMILY_LIFE_GEM)
        {
            ++gems;
        }
    }
    for (const auto& uptr : arena.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Treasure &&
            w->family() == FAMILY_LIFE_GEM)
        {
            ++gems;
        }
    }
    ASSERT_EQ(0, gems) << "scheduling the respawn must scrub the fresh gem";
}

// A corpse that died charmed revives on its true team at MATCH END too (the
// win-path revive must not freeze the charm onto the charmer's team).
TEST(CtfRespawn, match_end_revive_breaks_charm_onto_true_team)
{
    RespawnArena arena;
    walker* runner = arena.runner;
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 53;

    runner->set_real_team_num(0); // charmed off team 0...
    runner->set_team_num(1);      // ...onto team 1
    arena.kill(runner);
    arena.fx.tick(); // scheduled, still pending

    arena.world().ctf.captures[1] = arena.world().ctf.capture_limit;
    arena.fx.tick(); // win check fires: match-end revive

    ASSERT_TRUE(arena.world().game_ended);
    ASSERT_FALSE(runner->dead()) << "match end must revive pending corpses";
    ASSERT_EQ(0, runner->team_num()) << "revive breaks the charm";
    ASSERT_EQ(255, runner->real_team_num());
    ASSERT_EQ(0, runner->charm_left());
}

// Spawn probes must see blockers that straddle obmap buckets and blocking
// scenery (doors), and must never run treasure pickup side effects.
TEST(CtfRespawn, anchor_probe_rejects_doors_and_cross_bucket_blockers)
{
    CtfWorld fx;
    fx.spawn_flag(0, 96, 96);
    fx.spawn_flag(1, 544, 800);
    // Anchor 1: covered by a door. Anchor 2: unaligned (150,128) so its
    // 16px spawn bbox spans two 32px obmap buckets; the lurker lives only
    // in the second bucket. Anchor 3: clear.
    fx.spawn_anchor(0, 128, 96);
    fx.spawn_anchor(0, 150, 128);
    fx.spawn_anchor(0, 224, 128);
    fx.spawn_anchor(1, 512, 832);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, runner);
    runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    runner->myguy->id = 61;
    fx.world().ctf_requested_respawn_ticks = 6;

    walker* door = fx.world().add_weap_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_NE(nullptr, door);
    door->setxy(128, 96);
    door->set_team_num(7);
    walker* lurker = fx.spawn_living(FAMILY_SOLDIER, 1, 162, 128);
    ASSERT_NE(nullptr, lurker);

    fx.tick();
    ASSERT_TRUE(fx.world().ctf.active);

    runner->set_dead(1);
    runner->death();
    fx.tick(7);

    ASSERT_FALSE(runner->dead());
    EXPECT_EQ(224, runner->xpos()) << "door and cross-bucket living must "
                                      "both veto their anchors";
    EXPECT_EQ(128, runner->ypos());
}
