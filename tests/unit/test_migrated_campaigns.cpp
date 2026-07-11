// Shipped-package validation for the BASE + DECOR tile layering migration
// (.fss v11, core/decordefs.h). Covers all five decor-bearing stock
// packages: the three hand-authored ones rewritten by tools/grid_migrate
// (gladiator, tryxian, arenas) and the two decor-authoring generators
// (westlands, ctf). concept ships no decor (regenerates byte-identical) and
// is pinned by test_concept_levels.cpp.
//
// What is pinned here (design: tile-layering, test plan item 4):
//   - every level loads through the production campaign-mount path;
//   - decor planes are well-formed: grid-matching dims, known ids, never
//     over air / Z-stair / void bases;
//   - per-level decor-cell count pins (exact, kDemoLevels-style) so a
//     silent regen drift cannot slip through;
//   - the composition invariant: for every decor cell, the registry-composed
//     passability (base AND decor, probed through the real
//     query_grid_passable) equals the legacy semantics of the reverse-mapped
//     combined tile — BlocksGround decor reproduces the water/torch arm
//     (weapons and flyers pass, ground walkers blocked), passive decor
//     (pebbles) sits on plain-walkable ground. Table-driven; no pre-migration
//     package needed.
//   - the three grid_migrate packages carry ZERO leftover combined-tile
//     bytes in any REFERENCED grid (the tool reverted nothing; unreferenced
//     legacy duplicate grid members are copied through untouched and are
//     not visible through a level load).
//
// The migration equivalence proof itself (per-cell old-vs-new passability /
// concealment / damage / door genre / entity streams / footing) lives in
// tools/grid_migrate and runs at migration time; og_test_parity pins the
// migrated gladiator package against the untouched goldens.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Entity wiring (mirrors test_westlands_levels.cpp).
// ---------------------------------------------------------------------------
loader& migrated_levels_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_migrated_world_entity_services(GameWorld* world,
                                         LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &migrated_levels_loader();
    world->entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    world->entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(),
                                         entity.family());
    };
    world->entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

const LevelDataHooks& migrated_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_migrated_world_entity_services;
        return h;
    }();
    return hooks;
}

// ---------------------------------------------------------------------------
// Pins: per-level decor-cell counts (nonzero bytes summed over the level's
// decor planes). Regenerating a package legitimately (new content, mapping
// change) means re-baselining these numbers in the same commit.
// ---------------------------------------------------------------------------
struct DecorLevelPin
{
    int id;
    int decor_cells;
};

struct PackagePins
{
    const char* campaign;
    // grid_migrate packages: proven cell-for-cell equivalent, no leftover
    // combined-tile bytes in any referenced grid.
    bool fully_migrated;
    std::vector<DecorLevelPin> levels;
};

const std::vector<PackagePins>& package_pins()
{
    static const std::vector<PackagePins> pins = {
        {"org.openglad.gladiator", true,
         {{1, 20},  {2, 6},   {3, 5},   {4, 8},   {5, 8},   {6, 11},
          {7, 5},   {8, 13},  {9, 10},  {10, 9},  {11, 29}, {12, 7},
          {13, 36}, {14, 0},  {15, 0},  {16, 4},  {17, 13}, {18, 35},
          {19, 9},  {20, 10}, {21, 9},  {22, 14}, {23, 12}, {24, 10},
          {25, 1059}, {30, 59}, {35, 9}, {36, 0}, {37, 8},  {38, 10},
          {39, 65}, {40, 9},  {41, 11}, {42, 4},  {43, 7},  {44, 7},
          {45, 19}}},
        {"org.openglad.tryxian", true,
         {{103, 5}, {104, 8}, {105, 10}, {106, 0}, {107, 0}, {108, 0},
          {109, 9}, {110, 23}, {111, 0}, {112, 1}, {113, 0}, {114, 6},
          {115, 266}}},
        {"org.openglad.arenas", true,
         {{300, 1}, {301, 1}, {302, 26}, {303, 34}, {304, 16}, {305, 24}}},
        // (scen3 75 -> 74 and scen4 49 -> 46: the 2026-07 content batch
        // scaled the Ford/Refuge waves and added the Refuge muster — the
        // ambience scatters skip cells near entities, so the bigger armies
        // suppress a few dressing cells. Re-pinned from the regen.)
        // (scen24 273 -> 270: the terrace-circuit scree rule — no boulder
        // on or within one cell of the shoulder's lava — removed the three
        // ring-walk rocks that pinched the circuit against the lava sheet.
        // See levels_finale.cpp; re-pinned from the regen.)
        {"org.openglad.westlands", false,
         {{1, 65},  {2, 44},  {3, 74},  {4, 46},  {5, 135}, {6, 174},
          {7, 96},  {8, 277}, {9, 270}, {10, 92}, {11, 105}, {12, 143},
          {13, 71}, {14, 70}, {15, 147}, {16, 77}, {17, 187}, {19, 397},
          {20, 114}, {21, 91}, {22, 145}, {23, 417}, {24, 270}, {25, 88},
          {26, 38}}},
        {"org.openglad.ctf", false,
         {{500, 8}, {501, 4}, {502, 10}, {503, 10}, {504, 30}, {505, 0},
          {506, 0}, {507, 12}, {508, 9}, {509, 48}}},
    };
    return pins;
}

// The migration byte set (legacy combined tiles) — must be absent from every
// referenced grid of a fully migrated package.
bool is_legacy_combined_tile(unsigned char t)
{
    switch (t)
    {
        case PIX_TORCH1:
        case PIX_TORCH2:
        case PIX_TORCH3:
        case PIX_BRAZIER1:
        case PIX_BOULDER_1:
        case PIX_BOULDER_2:
        case PIX_BOULDER_3:
        case PIX_BOULDER_4:
        case PIX_GRASS_RUBBLE:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Passability probes: the five mover archetypes, owner-less 16x16 one-tile
// walkers never added to any list (mirrors tools/grid_migrate). Owner-less
// probes cannot reach the arrow-wall RNG gamble, so probing consumes no RNG.
// ---------------------------------------------------------------------------
std::unique_ptr<walker> make_probe(GameWorld& world, Order order, int family)
{
    if (!world.entity_factory)
        return nullptr;
    std::unique_ptr<walker> probe = world.entity_factory(order, family);
    if (probe == nullptr)
        return nullptr;
    PixieData square;
    square.frames = 1;
    square.w = 16;
    square.h = 16;
    square.data = std::make_unique<unsigned char[]>(16 * 16);
    probe->set_data(square);
    probe->stats()->set_bit_flags(BIT_FLYING, 0);
    probe->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    probe->stats()->set_bit_flags(BIT_ETHEREAL, 0);
    return probe;
}

struct ProbeKit
{
    std::unique_ptr<walker> ground;
    std::unique_ptr<walker> weapon;
    std::unique_ptr<walker> flyer;
    std::unique_ptr<walker> forest;
    std::unique_ptr<walker> special;

    bool make(GameWorld& world)
    {
        ground = make_probe(world, Order::Living, FAMILY_SOLDIER);
        weapon = make_probe(world, Order::Weapon, FAMILY_KNIFE);
        flyer = make_probe(world, Order::Living, FAMILY_SOLDIER);
        forest = make_probe(world, Order::Living, FAMILY_SOLDIER);
        special = make_probe(world, Order::Special, FAMILY_RESERVED_TEAM);
        if (!ground || !weapon || !flyer || !forest || !special)
            return false;
        flyer->stats()->set_bit_flags(BIT_FLYING, 1);
        forest->stats()->set_bit_flags(BIT_FORESTWALK, 1);
        return true;
    }

    std::array<bool, 5> at(GameWorld& world, int tx, int ty, int floor) const
    {
        const std::array<walker*, 5> probes = {ground.get(), weapon.get(),
                                               flyer.get(), forest.get(),
                                               special.get()};
        std::array<bool, 5> out{};
        for (std::size_t i = 0; i < probes.size(); ++i)
        {
            out[i] = world.query_grid_passable(
                static_cast<float>(tx * GRID_SIZE),
                static_cast<float>(ty * GRID_SIZE), probes[i], floor);
        }
        return out;
    }
};

// Legacy semantics of the reverse-mapped combined tile, per decor id:
//   BlocksGround (torches, brazier, boulders, columns) — exactly the
//   water/torch arm: {ground F, weapon T, flyer T, forestwalk F, special F}.
//   PEBBLES / BONES (None, reverse-mapped to plain-walkable rubble) — all
//   pass. SHRUB (conceal-only new content) ships in no stock package yet.
constexpr std::array<bool, 5> kBlockedVector = {false, true, true, false,
                                                false};
constexpr std::array<bool, 5> kOpenVector = {true, true, true, true, true};

class MigratedCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
    }

    void TearDown() override
    {
        const std::string current = get_mounted_campaign();
        if (!current.empty())
            (void)unmount_campaign_package_with_error(current);
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

int count_decor_cells(const GameWorld& world)
{
    int total = 0;
    for (int f = 0; f < world.floor_count(); ++f)
    {
        const PixieData& dec = world.decor_for_floor(f);
        if (!dec.valid())
            continue;
        const std::size_t cells =
            static_cast<std::size_t>(dec.w) * static_cast<std::size_t>(dec.h);
        for (std::size_t c = 0; c < cells; ++c)
            total += dec.data[c] != DECOR_NONE ? 1 : 0;
    }
    return total;
}

TEST_F(MigratedCampaignTest, decor_planes_are_wellformed_and_pinned)
{
    for (const PackagePins& pkg : package_pins())
    {
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(pkg.campaign))
            << pkg.campaign << " should mount";

        for (const DecorLevelPin& pin : pkg.levels)
        {
            LevelRuntimeData level(pin.id, true, &migrated_levels_hooks());
            ASSERT_TRUE(level.load())
                << pkg.campaign << " scen" << pin.id << " should load";
            GameWorld& world = level.world();

            for (int f = 0; f < world.floor_count(); ++f)
            {
                const PixieData& dec = world.decor_for_floor(f);
                if (!dec.valid())
                    continue;
                const PixieData& g = world.grid_for_floor(f);
                ASSERT_TRUE(g.valid());
                ASSERT_TRUE(dec.w == g.w && dec.h == g.h)
                    << pkg.campaign << " scen" << pin.id << " floor " << f
                    << ": decor plane dims mismatch";
                for (int ty = 0; ty < dec.h; ++ty)
                {
                    for (int tx = 0; tx < dec.w; ++tx)
                    {
                        const unsigned char d = dec.data[tx + ty * dec.w];
                        ASSERT_LT(d, DECOR_MAX)
                            << pkg.campaign << " scen" << pin.id
                            << ": decor id out of range at (" << tx << ", "
                            << ty << ")";
                        const unsigned char base = g.data[tx + ty * g.w];
                        if (d != DECOR_NONE)
                        {
                            ASSERT_TRUE(base != PIX_AIR &&
                                        base != PIX_ZSTAIR_UP &&
                                        base != PIX_ZSTAIR_DOWN &&
                                        base != PIX_VOID1)
                                << pkg.campaign << " scen" << pin.id
                                << ": decor over air/stair/void at (" << tx
                                << ", " << ty << ") floor " << f;
                        }
                    }
                }
            }

            EXPECT_EQ(pin.decor_cells, count_decor_cells(world))
                << pkg.campaign << " scen" << pin.id
                << ": decor-cell count drifted from the regen baseline";

            if (pkg.fully_migrated)
            {
                for (int f = 0; f < world.floor_count(); ++f)
                {
                    const PixieData& g = world.grid_for_floor(f);
                    ASSERT_TRUE(g.valid());
                    const std::size_t cells = static_cast<std::size_t>(g.w) *
                                              static_cast<std::size_t>(g.h);
                    int leftovers = 0;
                    for (std::size_t c = 0; c < cells; ++c)
                        leftovers += is_legacy_combined_tile(g.data[c]) ? 1
                                                                        : 0;
                    EXPECT_EQ(0, leftovers)
                        << pkg.campaign << " scen" << pin.id << " floor "
                        << f
                        << ": grid_migrate left combined-tile bytes behind";
                }
            }
        }

        ASSERT_EQ(CampaignPackageIoError::None,
                  unmount_campaign_package_with_error(pkg.campaign));
    }
}

TEST_F(MigratedCampaignTest, decor_composition_reproduces_legacy_semantics)
{
    for (const PackagePins& pkg : package_pins())
    {
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(pkg.campaign))
            << pkg.campaign << " should mount";

        for (const DecorLevelPin& pin : pkg.levels)
        {
            if (pin.decor_cells == 0)
                continue;
            LevelRuntimeData level(pin.id, true, &migrated_levels_hooks());
            ASSERT_TRUE(level.load())
                << pkg.campaign << " scen" << pin.id << " should load";
            GameWorld& world = level.world();

            ProbeKit kit;
            ASSERT_TRUE(kit.make(world)) << "probe kit";

            for (int f = 0; f < world.floor_count(); ++f)
            {
                const PixieData& dec = world.decor_for_floor(f);
                if (!dec.valid())
                    continue;
                for (int ty = 0; ty < dec.h; ++ty)
                {
                    for (int tx = 0; tx < dec.w; ++tx)
                    {
                        const unsigned char d = dec.data[tx + ty * dec.w];
                        if (d == DECOR_NONE)
                            continue;
                        // The one-tile probe footprint at the far edge
                        // clips the pixmax bound and reads all-blocked for
                        // every archetype; skip boundary cells.
                        if (tx + 1 >= dec.w || ty + 1 >= dec.h)
                            continue;
                        SCOPED_TRACE(std::string(pkg.campaign) + " scen" +
                                     std::to_string(pin.id) + " floor " +
                                     std::to_string(f) + " cell (" +
                                     std::to_string(tx) + ", " +
                                     std::to_string(ty) + ") decor " +
                                     std::to_string(d));
                        ASSERT_LT(d, DECOR_MAX);
                        const DecorDef& def = kDecorRegistry[d];
                        const std::array<bool, 5> got =
                            kit.at(world, tx, ty, f);
                        if (def.pass == DecorPassability::BlocksGround)
                        {
                            EXPECT_EQ(kBlockedVector, got)
                                << "BlocksGround decor must reproduce the "
                                   "legacy combined tile exactly (weapons "
                                   "and flyers pass, everyone else blocked)";
                        }
                        else if (def.pass == DecorPassability::None)
                        {
                            EXPECT_EQ(kOpenVector, got)
                                << "passive decor (pebbles) must sit on "
                                   "plain-walkable ground like the legacy "
                                   "rubble tile";
                        }
                    }
                }
            }
        }

        ASSERT_EQ(CampaignPackageIoError::None,
                  unmount_campaign_package_with_error(pkg.campaign));
    }
}

} // namespace
