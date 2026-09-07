// Shipped "Gladiator" campaign level pins (builtin/gladiator.glad).
//
// The Gladiator campaign is the hand-authored 2002 original: it has no
// generator, so there is no self-checking mapgen behind it and its level
// data is only ever pinned here. This file holds the balance invariants
// that a hand edit to a scenN.fss must keep honoring.
//
// Issue #266 ("There is a CRAZY powerful thief in Nurtham"): scen17,
// "THE CITY OF NUTHRAM", shipped an unnamed level-8 thief on team 3 at
// world (1168,1152). Two things were wrong with it:
//
//   1. Nothing on screen said he was the boss. The 12-byte name field in
//      his object record was all zeros, so BIT_NAMED never got set, he
//      drew without the OUTLINE_NAMED highlight, and his taunt/kill text
//      read as the generic family name.
//   2. He was effectively invulnerable. living::set_difficulty scales
//      armor QUADRATICALLY (armor += 2*level^2) while
//      compute_damage_reduction subtracts a LINEAR armor/2 and floors the
//      result at "always do at least 1 damage". A level-8 thief carries
//      128 armor, so the 64-point reduction exceeds the 45 base melee of
//      a level-12 player soldier and every single hit lands for exactly 1
//      against 779 hitpoints plus fast regeneration.
//
// The two tests below pin both halves: the boss carries a name, and no
// placed enemy in the level sits on the wrong side of the 1-damage clamp
// against a mid-campaign party.

#include <gtest/gtest.h>

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <cstdint>
#include <memory>
#include <string>

namespace {

// The Nuthram guild master, anchored by his AUTHORED placement so the pin
// survives any rebalance of his level or name.
constexpr int kNuthramLevel = 17;
constexpr int kMasterTeam = 3;
constexpr int kMasterX = 1168;
constexpr int kMasterY = 1152;
constexpr const char* kMasterName = "Saffron";

// The reference party the nerf is measured against: a level-12 soldier,
// roughly where a crew stands when the campaign reaches Nuthram.
constexpr short kReferencePartyLevel = 12;

// ---------------------------------------------------------------------------
// Entity wiring: one shared loader for every level load (mirrors the
// production headless wiring; the campaign uses only stock families).
// ---------------------------------------------------------------------------
loader& gladiator_levels_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_gladiator_world_entity_services(GameWorld* world,
                                          LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &gladiator_levels_loader();
    world->entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    world->entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family());
    };
    world->entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

const LevelDataHooks& gladiator_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_gladiator_world_entity_services;
        return h;
    }();
    return hooks;
}

// Mounts the shipped Gladiator campaign for the duration of one test and
// restores the previous mount in teardown.
class GladiatorCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("gladiator"))
            << "builtin/gladiator.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error("gladiator");
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

struct LoadedGladiatorLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedGladiatorLevel(int id, std::uint32_t seed = 0)
        : level(id, true, &gladiator_levels_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.world().rng_.state_ = seed;
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &level.world().rng_, &cfg);
        gc.rng = &level.world().rng_;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedGladiatorLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

walker* find_placed_living(GameWorld& world, std::int32_t family, int team,
                           int x, int y)
{
    for (auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->query_order() != Order::Living)
            continue;
        if (w->family() == family && w->team_num() == team &&
            w->xpos() == x && w->ypos() == y)
            return w;
    }
    return nullptr;
}

// The production post-load pass: game.cpp load_saved_game, the text
// protocol's level bootstrap and the headless server runtime all derive
// placed-walker stats from their authored levels this way.
void apply_authored_levels(GameWorld& world)
{
    for (auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr)
            w->set_difficulty(static_cast<std::uint32_t>(w->stats()->level()));
    }
}

TEST_F(GladiatorCampaignTest, nuthram_guild_master_is_named)
{
    LoadedGladiatorLevel fx(kNuthramLevel);
    ASSERT_TRUE(fx.loaded) << "gladiator scenario 17 should load";

    walker* master = find_placed_living(fx.world(), FAMILY_THIEF, kMasterTeam,
                                        kMasterX, kMasterY);
    ASSERT_NE(nullptr, master)
        << "the team-3 thief authored at (1168,1152) in THE CITY OF NUTHRAM";

    EXPECT_TRUE(master->stats()->query_bit_flags(BIT_NAMED))
        << "the Nuthram guild master must carry a name so the player can "
           "tell him from a guild mook (#266)";
    EXPECT_EQ(kMasterName, master->stats()->name);
}

TEST_F(GladiatorCampaignTest, nuthram_has_no_enemy_immune_to_a_mid_campaign_party)
{
    LoadedGladiatorLevel fx(kNuthramLevel);
    ASSERT_TRUE(fx.loaded) << "gladiator scenario 17 should load";
    GameWorld& world = fx.world();

    apply_authored_levels(world);

    // The reference attacker, built the way the text protocol builds a
    // --team-level playtest crew.
    walker* reference = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, reference);
    reference->set_team_num(0);
    reference->set_real_team_num(0);
    auto g = std::make_unique<guy>(FAMILY_SOLDIER);
    g->family = static_cast<char>(FAMILY_SOLDIER);
    g->name = "Reference";
    reference->set_owned_myguy(std::move(g));
    reference->myguy->upgrade_to_level(kReferencePartyLevel);
    reference->stats()->set_level(reference->myguy->level);
    reference->myguy->update_derived_stats(reference);

    const float melee = reference->damage();
    EXPECT_FLOAT_EQ(45.0f, melee)
        << "level-12 soldier melee is 20 + strength/4; re-pin this and the "
           "boss level together if the soldier curve moves";

    for (auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w == reference)
            continue;
        if (w->query_order() != Order::Living || w->team_num() == 0)
            continue;

        // compute_damage_reduction floors damage at 1: once the reduction
        // clamps to (melee - 1) the target takes exactly one point per hit
        // no matter how hard it is struck.
        EXPECT_LT(compute_damage_reduction(melee, w->stats()->armor()),
                  melee - 1.0f)
            << "family " << static_cast<int>(w->family()) << " team "
            << static_cast<int>(w->team_num())
            << " level " << w->stats()->level() << " at (" << w->xpos() << ","
            << w->ypos() << ") armor " << w->stats()->armor()
            << ": every hit from a level-" << kReferencePartyLevel
            << " party lands for exactly 1 (#266)";
    }

    // The boss is located by his AUTHORED placement rather than by "whoever
    // has the most hitpoints": he ties on max_hitpoints with Lord Jakarta's
    // own level-6 master of assassins at (1504,160), and a max-accumulator
    // would resolve that tie purely on oblist insertion order (add_to_list
    // appends, so it follows record order) — a false red the day a record is
    // reordered. The invariant that actually matters is order-independent:
    // no other enemy placed in the level may outrank the named boss.
    walker* master = find_placed_living(world, FAMILY_THIEF, kMasterTeam,
                                        kMasterX, kMasterY);
    ASSERT_NE(nullptr, master)
        << "the team-3 thief authored at (1168,1152) in THE CITY OF NUTHRAM";
    EXPECT_TRUE(master->stats()->query_bit_flags(BIT_NAMED))
        << "Nuthram's toughest unit must be the named boss (#266)";

    for (auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w == reference || w == master)
            continue;
        if (w->query_order() != Order::Living || w->team_num() == 0)
            continue;

        EXPECT_LE(w->stats()->max_hitpoints(), master->stats()->max_hitpoints())
            << "family " << static_cast<int>(w->family()) << " team "
            << static_cast<int>(w->team_num())
            << " level " << w->stats()->level() << " at (" << w->xpos() << ","
            << w->ypos() << ") carries " << w->stats()->max_hitpoints()
            << " hp: this enemy outranks the level's named boss ("
            << master->stats()->max_hitpoints() << " hp) (#266)";
    }
}

} // namespace
