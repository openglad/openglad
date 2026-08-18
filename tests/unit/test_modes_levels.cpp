// Shipped "Multiplayer Game Modes" campaign validation
// (builtin/modes.glad, authored by tools/modes_mapgen).
//
// The 39-scenario seven-mode campaign (TDM 300-305 absorbing the arenas
// grids, CTF 500-509 keeping the shipped CTF maps, Onslaught 800-803,
// Soccer 820-823, Basketball 824-828, Mutant 840-843, Free For All
// 850-855) is loaded through the production campaign-mount path and
// pinned against the authoring invariants the generator promises: every
// level SCEN_TYPE_SCRIPTED with no exit treasures, Gamesmaster briefings
// inside the 33-char budget with the exact sign-off, per-mode entity
// inventories (markers, flags, waypoints, per-team generators,
// treasures, doors), the migrated decor-cell pins (arenas + CTF values
// carried over from test_migrated_campaigns), the kept CTF
// door/key/capture-limit content, the §2.3 obmap ledger with its
// documented 303/305 A* waivers, closed
// soccer perimeters whose painted goal strips match the generated
// manifest, closed basketball perimeters whose 3x3 dunk carpets and jump
// tile match the generated manifest, footing + A*-reachability, and the
// committed og.use("mode_levels") module byte-matching the archive member
// and executing clean in the script sandbox.
//
// This test is the regression pin for the committed package: if it drifts
// from tools/modes_mapgen, regenerate the package (the tool self-checks
// the same tables) and update the pins here in the same change.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/campaign_ids.h>

// The modes pack's flag/waypoint treasure wire bytes (the pack families
// claim the retired core CTF slots).
inline constexpr int kModesFlagFamily = 13;
inline constexpr int kModesWaypointFamily = 14;
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/mapgen/builders.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/game_context.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Entity wiring: one shared loader (with the flag/waypoint entries the CTF
// and Onslaught levels author) for every level load.
// ---------------------------------------------------------------------------
loader& modes_levels_loader()
{
    static loader instance{EntityFactory{}};
    static const bool registered = [] {
        return true;
    }();
    (void)registered;
    return instance;
}

void wire_modes_world_entity_services(GameWorld* world, LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &modes_levels_loader();
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

const LevelDataHooks& modes_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_modes_world_entity_services;
        return h;
    }();
    return hooks;
}

// Mounts the shipped modes campaign for the duration of one test and
// EXACT-restores the previous mount in teardown (og_unit_data carries a
// PhysFS-roundtrip landmine: a fixture must collapse the tracked campaign
// mount back to the state it found — empty at the process root — or the
// next mount after the roundtrip fails its unmount-previous step).
class ModesCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("modes"))
            << "builtin/modes.glad should restore and mount";
    }

    void TearDown() override
    {
        const std::string now = get_mounted_campaign();
        if (!now.empty())
            (void)unmount_campaign_package_with_error(now);
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

// A campaign level loaded with full sim context, ready to tick and audit.
struct LoadedModesLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedModesLevel(int id, std::uint32_t seed = 0)
        : level(id, true, &modes_levels_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.world().rng_.state_ = seed;
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &level.world().rng_, &cfg);
        gc.rng = &level.world().rng_;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedModesLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

// A living-sized (16x16) ground probe for marker/goal tile checks.
std::unique_ptr<walker> make_tile_probe(GameWorld& world)
{
    std::unique_ptr<walker> probe =
        world.entity_factory(Order::Living, FAMILY_SOLDIER);
    if (probe != nullptr)
    {
        PixieData square;
        square.frames = 1;
        square.w = 16;
        square.h = 16;
        square.data = std::make_unique<unsigned char[]>(16 * 16);
        probe->set_data(square);
    }
    return probe;
}

bool tile_passable(GameWorld& world, walker* probe, int tx, int ty)
{
    return world.query_grid_passable(static_cast<float>(tx * GRID_SIZE),
                                     static_cast<float>(ty * GRID_SIZE),
                                     probe);
}

// ---------------------------------------------------------------------------
// The 39-row pin table (mirrors tools/modes_mapgen's ExpectedLevel rows —
// tool and test move in lockstep).
// ---------------------------------------------------------------------------
struct ShippedModeLevel
{
    int id;
    const char* mode; // manifest tag: tdm/ctf/onslaught/soccer/basketball/
                      // mutant/ffa
    const char* title;
    int par;
    int grid_w;
    int grid_h;
    int teams;
    int markers_per_team;
    int flags;
    int cps;
    std::array<int, 8> gens; // per team byte (7 = neutral)
    int livings;
    int treasures; // spice incl. keys/teleporters, excl. flags/CPs
    int doors;
    int other_weapons;
    int caps_total; // sum of the manifest's spawn caps (ledger input)
    bool a_star_waived;
    int decor_cells;
    // Respawning pickups: live respawnable treasures per family
    // (drumstick, magic, invis, speed — the manifest item_pads mirror) and
    // the row's item_interval (0 = the mode ships no item respawns).
    std::array<int, 4> item_pads;
    int item_interval;
    // Roster modes only (ffa, mutant): the manifest's `fighters` count.
    // 0 pins the field ABSENT, which is what keeps every other mode's
    // manifest row byte-stable across the field's arrival.
    int fighters = 0;
};

const std::vector<ShippedModeLevel>& shipped_levels()
{
    static const std::vector<ShippedModeLevel> levels = {
        {300, "tdm", "Team Deathmatch: THE CIRCLE", 10, 60, 60, 4, 20, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 70, 0, 0, 0, false, 1,
         {24, 0, 0, 4}, 300},
        {301, "tdm", "Team Deathmatch: BLOODGLADE", 12, 60, 60, 4, 20, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 110, 0, 0, 0, false, 1,
         {80, 0, 0, 0}, 300},
        {302, "tdm", "Team Deathmatch: ARCHIPELAGO", 14, 80, 80, 4, 20, 0, 0,
         {0, 0, 0, 0, 2, 0, 0, 0}, 0, 100, 0, 0, 8, false, 26,
         {40, 8, 0, 0}, 300},
        {303, "tdm", "Team Deathmatch: GATEKEEPERS", 14, 80, 80, 4, 20, 0, 0,
         {0, 0, 0, 0, 0, 1, 1, 0}, 0, 136, 15, 0, 8, true, 34,
         {60, 11, 13, 0}, 300},
        {304, "tdm", "Team Deathmatch: THE CASTLE", 15, 60, 60, 4, 20, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 2}, 0, 112, 0, 0, 8, false, 16,
         {49, 5, 6, 16}, 300},
        {305, "tdm", "Team Deathmatch: BULLSEYE", 15, 60, 60, 4, 20, 0, 0,
         {1, 1, 1, 1, 0, 0, 0, 0}, 0, 120, 0, 0, 16, true, 24,
         {66, 0, 17, 8}, 300},
        {500, "ctf", "CTF: FIRST BLOOD", 4, 40, 30, 2, 12, 2, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 7, 0, 0, 0, false, 8,
         {4, 0, 1, 2}, 300},
        {501, "ctf", "CTF: A BORDER FORT", 4, 30, 30, 2, 12, 2, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 5, 25, 5, 0, 0, false, 4,
         {8, 0, 0, 1}, 300},
        {502, "ctf", "CTF: CASTLE CORNER", 4, 30, 40, 2, 12, 2, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 16, 0, 0, 0, false, 10,
         {8, 0, 0, 1}, 300},
        {503, "ctf", "CTF: THE OUTPOST", 5, 40, 60, 2, 12, 2, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 52, 5, 0, 0, false, 10,
         {8, 2, 2, 0}, 300},
        {504, "ctf", "CTF: RIVER RUN", 5, 60, 40, 2, 12, 2, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 7, 0, 0, 0, false, 30,
         {4, 0, 1, 2}, 300},
        {505, "ctf", "CTF: TRIAD", 6, 51, 51, 3, 12, 3, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 7, 0, 0, 0, false, 0,
         {3, 0, 1, 3}, 300},
        {506, "ctf", "CTF: THE UNDERPASS", 5, 60, 20, 2, 12, 2, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 52, 3, 16, 0, false, 0,
         {12, 0, 0, 0}, 300},
        {507, "ctf", "CTF: DUNGEON OF STARS", 6, 70, 70, 4, 12, 4, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 57, 22, 0, 0, false, 12,
         {16, 0, 0, 0}, 300},
        {508, "ctf", "CTF: CENTWHEIT MANOR", 6, 50, 50, 3, 12, 3, 2,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 17, 0, 0, 0, false, 9,
         {5, 0, 1, 2}, 300},
        {509, "ctf", "CTF: CROSSFIRE", 6, 60, 60, 4, 12, 4, 1,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 9, 0, 0, 0, false, 48,
         {4, 0, 1, 4}, 300},
        {800, "onslaught", "Onslaught: FOUNDRY LINE", 8, 50, 35, 2, 12, 0, 1,
         {4, 4, 0, 0, 0, 0, 0, 0}, 0, 16, 4, 0, 48, false, 20,
         {0, 0, 0, 0}, 0},
        {801, "onslaught", "Onslaught: TWIN SPIRES", 10, 60, 60, 2, 12, 0, 2,
         {6, 6, 0, 0, 0, 0, 0, 0}, 4, 20, 0, 0, 40, false, 16,
         {0, 0, 0, 0}, 0},
        {802, "onslaught", "Onslaught: THE MARCHES", 12, 80, 60, 3, 12, 0, 1,
         {4, 4, 4, 0, 0, 0, 0, 0}, 0, 18, 6, 0, 42, false, 0,
         {0, 0, 0, 0}, 0},
        {803, "onslaught", "Onslaught: LAST BASTION", 12, 70, 70, 2, 12, 0, 2,
         {5, 5, 0, 0, 0, 0, 0, 2}, 0, 20, 8, 0, 44, false, 6,
         {0, 0, 0, 0}, 0},
        {820, "soccer", "Soccer: THE PITCH", 6, 44, 28, 2, 12, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 12, 0, 0, 0, false, 4,
         {12, 0, 0, 0}, 180},
        {821, "soccer", "Soccer: THE MUDBOWL", 8, 50, 30, 2, 12, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 14, 0, 0, 0, false, 4,
         {12, 0, 0, 0}, 180},
        {822, "soccer", "Soccer: FOURSQUARE", 8, 40, 40, 4, 12, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 12, 0, 0, 0, false, 8,
         {12, 0, 0, 0}, 180},
        {823, "soccer", "Soccer: BONEYARD CUP", 10, 46, 30, 2, 12, 0, 0,
         {1, 1, 0, 0, 0, 0, 0, 0}, 0, 12, 0, 0, 12, false, 8,
         {12, 0, 0, 0}, 180},
        {824, "basketball", "Basketball: CENTER COURT", 6, 45, 25, 2, 5, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 4,
         {10, 0, 0, 0}, 240},
        {825, "basketball", "Basketball: THE PLAYGROUND", 6, 31, 19, 2, 5, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 6, 0, 0, 0, false, 4,
         {6, 0, 0, 0}, 240},
        {826, "basketball", "Basketball: FOUR HOOPS", 8, 41, 41, 4, 5, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 12, 0, 0, 0, false, 8,
         {12, 0, 0, 0}, 240},
        {827, "basketball", "Basketball: THE BANKHOUSE", 8, 45, 27, 2, 5, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 4,
         {10, 0, 0, 0}, 240},
        {828, "basketball", "Basketball: BENCHWARMERS", 10, 47, 29, 2, 5, 0, 0,
         {1, 1, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 8, false, 6,
         {10, 0, 0, 0}, 240},
        {840, "mutant", "Mutant: THE PIT", 6, 30, 30, 4, 12, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 8, 0, 0, 0, false, 4,
         {6, 0, 0, 2}, 180, 4},
        {841, "mutant", "Mutant: CATACOMBS", 8, 50, 50, 4, 12, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 12, 0, 0, 0, false, 66,
         {10, 0, 0, 2}, 180, 4},
        {842, "mutant", "Mutant: MOONCOURT", 8, 40, 40, 4, 12, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 12, 0, 0, 0, false, 48,
         {8, 0, 0, 4}, 180, 4},
        {843, "mutant", "Mutant: BROKEN CROWN", 10, 45, 45, 4, 12, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 20,
         {8, 0, 0, 2}, 180, 4},
        {850, "ffa", "FFA: THE MELEE", 6, 34, 34, 4, 8, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 16,
         {8, 0, 0, 2}, 180, 8},
        {851, "ffa", "FFA: CROSSFIRE", 8, 40, 40, 4, 8, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 16,
         {8, 0, 0, 2}, 180, 10},
        {852, "ffa", "FFA: SHARDS", 8, 44, 44, 4, 8, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 36,
         {8, 0, 0, 2}, 180, 12},
        {853, "ffa", "FFA: THE ROSE", 10, 45, 45, 4, 8, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 4,
         {8, 0, 0, 2}, 180, 16},
        {854, "ffa", "FFA: SCRAMBLE", 10, 48, 48, 4, 8, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 16,
         {8, 0, 0, 2}, 180, 16},
        {855, "ffa", "FFA: NIGHTFALL", 12, 50, 50, 4, 8, 0, 0,
         {0, 0, 0, 0, 0, 0, 0, 0}, 0, 10, 0, 0, 0, false, 36,
         {8, 0, 0, 2}, 180, 16},
    };
    return levels;
}

// Soccer terrain contracts (tile coords; the manifest carries the same
// rects in pixels — goal strip = the painted PIX_CARPET_M rect exactly).
struct SoccerPins
{
    int id;
    struct Rect { int x0, y0, x1, y1; };
    std::vector<Rect> goals; // index = defending team
    int kickoff_tx, kickoff_ty;
};

const std::vector<SoccerPins>& soccer_pins()
{
    static const std::vector<SoccerPins> pins = {
        {820, {{1, 10, 2, 17}, {41, 10, 42, 17}}, 21, 13},
        {821, {{1, 11, 2, 18}, {47, 11, 48, 18}}, 24, 14},
        {822,
         {{16, 1, 23, 2}, {37, 16, 38, 23}, {16, 37, 23, 38}, {1, 16, 2, 23}},
         19, 19},
        {823, {{1, 11, 2, 18}, {43, 11, 44, 18}}, 22, 14},
    };
    return pins;
}

// Basketball court contracts (tile coords; the manifest carries the rim
// centers and the jump spot in pixels). A hoop tile is the PIX_CARPET_M2
// center of a 3x3 PIX_CARPET_M dunk carpet — the painted carpet IS the
// Chebyshev dunk box the mode reads, so paint and manifest must agree.
struct BasketballPins
{
    int id;
    std::vector<std::array<int, 2>> hoops; // index = defending team
    int arc_radius;                        // three-point release px
    int jump_tx, jump_ty;
};

const std::vector<BasketballPins>& basketball_pins()
{
    static const std::vector<BasketballPins> pins = {
        {824, {{3, 12}, {41, 12}}, 160, 22, 12},
        {825, {{3, 9}, {27, 9}}, 96, 15, 9},
        {826, {{20, 3}, {37, 20}, {20, 37}, {3, 20}}, 144, 20, 20},
        {827, {{3, 13}, {41, 13}}, 176, 22, 13},
        {828, {{3, 14}, {43, 14}}, 160, 23, 14},
    };
    return pins;
}

struct Census
{
    std::array<int, 8> markers{};
    std::array<int, 8> flags{};
    std::array<int, 8> gens{};
    int cps = 0;
    int exits = 0;
    int stains = 0;
    int teleporters = 0;
    int treasures = 0;
    int doors = 0;
    int other_weapons = 0;
    int livings = 0;
    int named = 0;
    int save_protected = 0;
    // Respawnable treasures (drumstick/magic/invis/speed): per-family
    // counts and the (family index, tx, ty) multiset the manifest
    // item_pads must mirror exactly.
    std::array<int, 4> items{};
    std::vector<std::array<int, 3>> item_tiles;
};

int respawnable_index(int family)
{
    switch (family)
    {
        case FAMILY_DRUMSTICK: return 0;
        case FAMILY_MAGIC_POTION: return 1;
        case FAMILY_INVIS_POTION: return 2;
        case FAMILY_SPEED_POTION: return 3;
        default: return -1;
    }
}

Census take_census(GameWorld& world)
{
    Census c;
    auto sweep = [&](auto& list) {
        for (const auto& uptr : list)
        {
            walker* ob = uptr.get();
            if (ob == nullptr)
                continue;
            const Order order = ob->query_order();
            const int family = ob->family();
            const int team = std::min<int>(ob->team_num(), 7);
            if (ob->stats() != nullptr && !ob->stats()->name.empty())
                ++c.named;
            if (ob->save_all_protected())
                ++c.save_protected;
            if (order == Order::Special && family == FAMILY_RESERVED_TEAM)
                ++c.markers[static_cast<std::size_t>(team)];
            else if (order == Order::Treasure)
            {
                if (family == kModesFlagFamily)
                    ++c.flags[static_cast<std::size_t>(team)];
                else if (family == kModesWaypointFamily)
                    ++c.cps;
                else if (family == FAMILY_EXIT)
                    ++c.exits;
                else if (family == FAMILY_STAIN)
                    ++c.stains;
                else
                {
                    ++c.treasures;
                    if (family == FAMILY_TELEPORTER)
                        ++c.teleporters;
                    const int idx = respawnable_index(family);
                    if (idx >= 0)
                    {
                        ++c.items[static_cast<std::size_t>(idx)];
                        c.item_tiles.push_back({idx, ob->xpos() / GRID_SIZE,
                                                ob->ypos() / GRID_SIZE});
                    }
                }
            }
            else if (order == Order::Weapon)
            {
                if (family == FAMILY_DOOR)
                    ++c.doors;
                else
                    ++c.other_weapons;
            }
            else if (order == Order::Living)
                ++c.livings;
            else if (order == Order::Generator)
                ++c.gens[static_cast<std::size_t>(team)];
        }
    };
    sweep(world.oblist);
    sweep(world.fxlist);
    sweep(world.weaplist);
    return c;
}

// ---------------------------------------------------------------------------

using ModesLevels = ModesCampaignTest;

TEST_F(ModesLevels, roster_structure_round_trips)
{
    const std::vector<int> listed = list_levels_v();
    EXPECT_EQ(39u, listed.size()) << "the package must ship 39 scenarios";
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id << " failed to load";
        GameWorld& world = loaded.world();
        EXPECT_EQ(SCEN_TYPE_SCRIPTED, world.type)
            << "scen" << pin.id
            << ": every mode level authors 0x20 ONLY (no CAN_EXIT, no "
               "SAVE_ALL, no CTF/tower bits)";
        EXPECT_TRUE(loaded.level.generated)
            << "scen" << pin.id
            << ": mapgen output carries the SCEN_TYPE_GENERATED provenance "
               "mark (metadata-side; world.type above stays clean)";
        EXPECT_EQ(pin.title, world.title) << "scen" << pin.id;
        EXPECT_LE(world.title.size(), 30u) << "scen" << pin.id;
        EXPECT_EQ(pin.par, world.par_value) << "scen" << pin.id;
        EXPECT_EQ(1, world.floor_count()) << "scen" << pin.id;
        EXPECT_EQ(pin.grid_w, world.grid.w) << "scen" << pin.id;
        EXPECT_EQ(pin.grid_h, world.grid.h) << "scen" << pin.id;
    }
}

TEST_F(ModesLevels, briefings_fit_budget_and_carry_the_signoff)
{
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        const auto& lines = loaded.level.description;
        ASSERT_FALSE(lines.empty()) << "scen" << pin.id;
        for (const std::string& line : lines)
            EXPECT_LE(line.size(), 33u)
                << "scen" << pin.id << ": briefing line '" << line << "'";
        EXPECT_EQ("-- THE GAMESMASTER", lines.back())
            << "scen" << pin.id
            << ": every briefing ends with the Gamesmaster sign-off";
    }
}

TEST_F(ModesLevels, entity_inventories_match)
{
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        const Census c = take_census(loaded.world());
        for (int team = 0; team < 8; ++team)
        {
            const int expect = (team < pin.teams) ? pin.markers_per_team : 0;
            EXPECT_EQ(expect, c.markers[static_cast<std::size_t>(team)])
                << "scen" << pin.id << " team " << team << " markers";
            EXPECT_EQ(pin.gens[static_cast<std::size_t>(team)],
                      c.gens[static_cast<std::size_t>(team)])
                << "scen" << pin.id << " team " << team << " generators";
        }
        int total_flags = 0;
        for (const int f : c.flags)
            total_flags += f;
        EXPECT_EQ(pin.flags, total_flags) << "scen" << pin.id;
        EXPECT_EQ(pin.cps, c.cps) << "scen" << pin.id << " waypoints";
        EXPECT_EQ(pin.treasures, c.treasures) << "scen" << pin.id;
        EXPECT_EQ(pin.doors, c.doors) << "scen" << pin.id;
        EXPECT_EQ(pin.other_weapons, c.other_weapons) << "scen" << pin.id;
        EXPECT_EQ(pin.livings, c.livings) << "scen" << pin.id;
    }
}

TEST_F(ModesLevels, no_exits_no_named_npcs_no_teleporters_on_mutant)
{
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        const Census c = take_census(loaded.world());
        EXPECT_EQ(0, c.exits)
            << "scen" << pin.id << ": MP levels ship no exits";
        EXPECT_EQ(0, c.stains) << "scen" << pin.id;
        EXPECT_EQ(0, c.named) << "scen" << pin.id << ": no named NPCs";
        EXPECT_EQ(0, c.save_protected)
            << "scen" << pin.id << ": no protected bits";
        if (std::string(pin.mode) == "mutant")
        {
            EXPECT_EQ(0, c.teleporters)
                << "scen" << pin.id
                << ": a pad ride would break the beacon hunt";
        }
    }
}

TEST_F(ModesLevels, decor_planes_well_formed_and_pinned)
{
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        GameWorld& world = loaded.world();
        const PixieData& dec = world.decor;
        int cells = 0;
        if (dec.valid())
        {
            ASSERT_EQ(world.grid.w, dec.w) << "scen" << pin.id;
            ASSERT_EQ(world.grid.h, dec.h) << "scen" << pin.id;
            for (int ty = 0; ty < dec.h; ++ty)
                for (int tx = 0; tx < dec.w; ++tx)
                {
                    const unsigned char d = dec.data[static_cast<std::size_t>(tx + ty * dec.w)];
                    if (d == DECOR_NONE)
                        continue;
                    ++cells;
                    EXPECT_LT(d, DECOR_MAX)
                        << "scen" << pin.id << " (" << tx << ", " << ty << ")";
                }
        }
        EXPECT_EQ(pin.decor_cells, cells) << "scen" << pin.id;
    }
}

TEST_F(ModesLevels, footing_and_reachability)
{
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        GameWorld& world = loaded.world();

        // Marker tiles carry deploying 16x16 livings; probe them with a
        // living-size box (the library audit's marker-sprite footprint is
        // an editor artifact, filtered below).
        std::unique_ptr<walker> probe = make_tile_probe(world);
        ASSERT_NE(nullptr, probe) << "scen" << pin.id;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->query_order() != Order::Special ||
                ob->family() != FAMILY_RESERVED_TEAM)
                continue;
            EXPECT_TRUE(tile_passable(world, probe.get(),
                                      ob->xpos() / GRID_SIZE,
                                      ob->ypos() / GRID_SIZE))
                << "scen" << pin.id << ": marker ("
                << ob->xpos() / GRID_SIZE << ", " << ob->ypos() / GRID_SIZE
                << ") impassable";
        }

        for (const std::string& err : og::mapgen::audit_footing(world))
        {
            if (err.find("order 5 family 0") != std::string::npos)
                continue; // marker sprite footprint: covered above
            ADD_FAILURE() << "scen" << pin.id << ": " << err;
        }
        for (const std::string& err : og::mapgen::audit_reachability(world))
        {
            // THE CASTLE's paired towers are teleporter-served by design
            // (the twelve kept pads are the map's identity).
            if (pin.id == 304 && err.find("(28, 28)") != std::string::npos)
                continue;
            ADD_FAILURE() << "scen" << pin.id << ": " << err;
        }
    }
}

TEST_F(ModesLevels, kept_ctf_doors_and_keys)
{
    // scen503: the kept source door seals the empty inner keep; key at
    // (18, 11). scen506: the kept door seals a dead-end treasure pocket;
    // keys at (30, 15). Tile coordinates are pinned content.
    {
        LoadedModesLevel outpost(503);
        ASSERT_TRUE(outpost.loaded);
        std::set<std::pair<int, int>> door_tiles;
        for (const auto& uptr : outpost.world().weaplist)
        {
            walker* ob = uptr.get();
            if (ob != nullptr && ob->family() == FAMILY_DOOR)
                door_tiles.insert({ob->xpos() / GRID_SIZE,
                                   ob->ypos() / GRID_SIZE});
        }
        EXPECT_TRUE(door_tiles.count({18, 19}) && door_tiles.count({19, 19}))
            << "scen503 kept door tiles (18-19, 19)";
        bool key_found = false;
        for (const auto& uptr : outpost.world().fxlist)
        {
            walker* fx = uptr.get();
            if (fx != nullptr && fx->family() == FAMILY_KEY &&
                fx->xpos() / GRID_SIZE == 18 && fx->ypos() / GRID_SIZE == 11)
                key_found = true;
        }
        EXPECT_TRUE(key_found) << "scen503 kept key at (18, 11)";
    }
    {
        LoadedModesLevel underpass(506);
        ASSERT_TRUE(underpass.loaded);
        std::set<std::pair<int, int>> door_tiles;
        for (const auto& uptr : underpass.world().weaplist)
        {
            walker* ob = uptr.get();
            if (ob != nullptr && ob->family() == FAMILY_DOOR)
                door_tiles.insert({ob->xpos() / GRID_SIZE,
                                   ob->ypos() / GRID_SIZE});
        }
        EXPECT_TRUE(door_tiles.count({43, 12}) &&
                    door_tiles.count({44, 12}) && door_tiles.count({45, 12}))
            << "scen506 kept door tiles (43-45, 12)";
        int keys_at_pocket = 0;
        for (const auto& uptr : underpass.world().fxlist)
        {
            walker* fx = uptr.get();
            if (fx != nullptr && fx->family() == FAMILY_KEY &&
                fx->xpos() / GRID_SIZE == 30 && fx->ypos() / GRID_SIZE == 15)
                ++keys_at_pocket;
        }
        EXPECT_EQ(2, keys_at_pocket) << "scen506 kept keys at (30, 15)";
    }
}

TEST_F(ModesLevels, crossfire_capture_limit_is_five)
{
    LoadedModesLevel crossfire(509);
    ASSERT_TRUE(crossfire.loaded);
    int flags_seen = 0;
    for (const auto& uptr : crossfire.world().fxlist)
    {
        walker* fx = uptr.get();
        if (fx == nullptr || fx->family() != kModesFlagFamily)
            continue;
        ++flags_seen;
        ASSERT_NE(nullptr, fx->stats());
        EXPECT_EQ(5, fx->stats()->level())
            << "CROSSFIRE plays to five captures (flag stat level 5)";
    }
    EXPECT_EQ(4, flags_seen);
}

TEST_F(ModesLevels, obmap_budget_ledger_holds)
{
    // §2.3 model: authored ground load + capped spawns + 16 heroes +
    // 20 corpse/stain transients + 25 projectiles (+ the mode's own fx
    // entities: soccer spawns 1 ball; basketball 2 — the ball plus its
    // ground shadow — plus one hoop sprite per authored hoop, the peak
    // activation, D29/D32) stays <= 190 so A* never short-circuits
    // mid-match. 303 and 305 are the documented arenas-heritage waivers.
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        int gens = 0;
        for (const int g : pin.gens)
            gens += g;
        const std::string mode(pin.mode);
        int ball = (mode == "soccer") ? 1 : 0;
        if (mode == "basketball")
        {
            // 2 (ball + shadow) + the row's authored hoops, taken from the
            // basketball_pins() hoop lists so the two pin tables cannot
            // drift apart (826 fields 4 rims, the two-hoop courts 2).
            int hoops = 0;
            for (const BasketballPins& bp : basketball_pins())
                if (bp.id == pin.id)
                    hoops = static_cast<int>(bp.hoops.size());
            ASSERT_GT(hoops, 0)
                << "scen" << pin.id
                << ": basketball row missing from basketball_pins()";
            ball = 2 + hoops;
        }
        int flags = pin.flags;
        const int ledger = gens + pin.treasures + flags + pin.cps +
                           pin.doors + pin.livings + pin.caps_total + 16 +
                           20 + 25 + ball;
        if (pin.a_star_waived)
            EXPECT_GT(ledger, 190)
                << "scen" << pin.id
                << ": the A* waiver documents a real overrun; drop it if "
                   "the ledger now fits";
        else
            EXPECT_LE(ledger, 190) << "scen" << pin.id;
        EXPECT_TRUE(pin.a_star_waived == (pin.id == 303 || pin.id == 305))
            << "scen" << pin.id
            << ": only the two arenas-heritage maps carry the waiver";
    }
}

TEST_F(ModesLevels, soccer_goals_and_perimeters_match_the_manifest)
{
    for (const SoccerPins& pin : soccer_pins())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        GameWorld& world = loaded.world();
        std::unique_ptr<walker> probe = make_tile_probe(world);
        ASSERT_NE(nullptr, probe);

        // Closed perimeter: the ball and the players can never leave.
        for (int tx = 0; tx < world.grid.w; ++tx)
        {
            EXPECT_FALSE(tile_passable(world, probe.get(), tx, 0))
                << "scen" << pin.id << " (" << tx << ", 0)";
            EXPECT_FALSE(
                tile_passable(world, probe.get(), tx, world.grid.h - 1))
                << "scen" << pin.id << " (" << tx << ", " << world.grid.h - 1
                << ")";
        }
        for (int ty = 0; ty < world.grid.h; ++ty)
        {
            EXPECT_FALSE(tile_passable(world, probe.get(), 0, ty))
                << "scen" << pin.id << " (0, " << ty << ")";
            EXPECT_FALSE(
                tile_passable(world, probe.get(), world.grid.w - 1, ty))
                << "scen" << pin.id << " (" << world.grid.w - 1 << ", " << ty
                << ")";
        }

        // Goal strips: every tile carries the goal carpet, and the strip
        // is exactly 8x2 tiles (128x32 px — the manifest's pixel rects).
        for (const SoccerPins::Rect& g : pin.goals)
        {
            const int w = g.x1 - g.x0 + 1;
            const int h = g.y1 - g.y0 + 1;
            EXPECT_EQ(16, w * h)
                << "scen" << pin.id << ": goal strips are 8x2 tiles";
            for (int ty = g.y0; ty <= g.y1; ++ty)
                for (int tx = g.x0; tx <= g.x1; ++tx)
                    EXPECT_EQ(PIX_CARPET_M,
                              world.grid.data[static_cast<std::size_t>(tx + ty * world.grid.w)])
                        << "scen" << pin.id << " goal tile (" << tx << ", "
                        << ty << ")";
        }
        EXPECT_TRUE(tile_passable(world, probe.get(), pin.kickoff_tx,
                                  pin.kickoff_ty))
            << "scen" << pin.id << " kickoff";
    }
}

TEST_F(ModesLevels, basketball_courts_match_the_manifest)
{
    const std::vector<std::uint8_t> member = og::resources::read_file(
        "packs/modes.core/lib/mode_levels.lua");
    ASSERT_FALSE(member.empty()) << "manifest member missing from the .glad";
    const std::string member_text(member.begin(), member.end());
    const std::string expr_prefix =
        "(function() local M = (function() " +
        member_text.substr(member_text.find("local M = {}")) +
        " end)() return ";
    og::script::ScriptHost host;

    for (const BasketballPins& pin : basketball_pins())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        GameWorld& world = loaded.world();
        std::unique_ptr<walker> probe = make_tile_probe(world);
        ASSERT_NE(nullptr, probe);
        auto tile_at = [&world](int tx, int ty) {
            return world.grid.data[static_cast<std::size_t>(
                tx + ty * world.grid.w)];
        };

        // Closed perimeter: neither the players nor the ball can leave.
        for (int tx = 0; tx < world.grid.w; ++tx)
        {
            EXPECT_FALSE(tile_passable(world, probe.get(), tx, 0))
                << "scen" << pin.id << " (" << tx << ", 0)";
            EXPECT_FALSE(
                tile_passable(world, probe.get(), tx, world.grid.h - 1))
                << "scen" << pin.id << " (" << tx << ", " << world.grid.h - 1
                << ")";
        }
        for (int ty = 0; ty < world.grid.h; ++ty)
        {
            EXPECT_FALSE(tile_passable(world, probe.get(), 0, ty))
                << "scen" << pin.id << " (0, " << ty << ")";
            EXPECT_FALSE(
                tile_passable(world, probe.get(), world.grid.w - 1, ty))
                << "scen" << pin.id << " (" << world.grid.w - 1 << ", " << ty
                << ")";
        }

        const auto mode = host.eval_string(
            expr_prefix + std::format("M.levels[{}].mode end)()", pin.id));
        ASSERT_TRUE(mode.has_value()) << "scen" << pin.id;
        EXPECT_EQ("basketball", *mode) << "scen" << pin.id;

        // Arc sanity: a real three-point line that still fits the court
        // (the mapgen self-check enforces the same bounds at generation).
        const auto arc = host.eval_integer(
            expr_prefix +
            std::format("M.levels[{}].arc_radius end)()", pin.id));
        ASSERT_TRUE(arc.has_value()) << "scen" << pin.id;
        EXPECT_EQ(pin.arc_radius, *arc) << "scen" << pin.id << " arc_radius";
        EXPECT_LE(32, *arc) << "scen" << pin.id;
        EXPECT_LT(*arc, std::min(world.grid.w, world.grid.h) * GRID_SIZE / 2)
            << "scen" << pin.id << ": the arc must fit inside the court";

        for (std::size_t t = 0; t < pin.hoops.size(); ++t)
        {
            const int hx = pin.hoops[t][0];
            const int hy = pin.hoops[t][1];

            // The 3x3 dunk carpet: PIX_CARPET_M2 rim center inside eight
            // PIX_CARPET_M tiles, all nine walkable (the dunk box the mode
            // reads is exactly this painted square).
            EXPECT_EQ(PIX_CARPET_M2, tile_at(hx, hy))
                << "scen" << pin.id << " hoop " << t << " (" << hx << ", "
                << hy << ") is not the rim tile";
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx != 0 || dy != 0)
                    {
                        EXPECT_EQ(PIX_CARPET_M, tile_at(hx + dx, hy + dy))
                            << "scen" << pin.id << " hoop " << t
                            << " dunk carpet (" << hx + dx << ", " << hy + dy
                            << ")";
                    }
                    EXPECT_TRUE(
                        tile_passable(world, probe.get(), hx + dx, hy + dy))
                        << "scen" << pin.id << " hoop " << t
                        << " dunk carpet (" << hx + dx << ", " << hy + dy
                        << ") impassable";
                }

            // The manifest carries the PIXEL center of that rim tile.
            const auto mx = host.eval_integer(
                expr_prefix +
                std::format("M.levels[{}].hoops[{}].x end)()", pin.id, t));
            const auto my = host.eval_integer(
                expr_prefix +
                std::format("M.levels[{}].hoops[{}].y end)()", pin.id, t));
            ASSERT_TRUE(mx.has_value() && my.has_value())
                << "scen" << pin.id << " hoop " << t
                << ": the manifest must bank a hoop per active team";
            EXPECT_EQ(hx * GRID_SIZE + GRID_SIZE / 2, *mx)
                << "scen" << pin.id << " hoop " << t;
            EXPECT_EQ(hy * GRID_SIZE + GRID_SIZE / 2, *my)
                << "scen" << pin.id << " hoop " << t;
        }

        // Hoop separation > 2 * (scatter_cap_total + rim_r + rim_lip) = 84
        // px, so a scattered shot can never land inside a rival rim.
        for (std::size_t a = 0; a + 1 < pin.hoops.size(); ++a)
            for (std::size_t b = a + 1; b < pin.hoops.size(); ++b)
            {
                const int l1 =
                    std::abs(pin.hoops[a][0] - pin.hoops[b][0]) * GRID_SIZE +
                    std::abs(pin.hoops[a][1] - pin.hoops[b][1]) * GRID_SIZE;
                EXPECT_GT(l1, 84)
                    << "scen" << pin.id << ": hoops " << a << " and " << b
                    << " are close enough for cross-rim landings";
            }

        // The jump spot: a walkable tile whose pixel center the manifest
        // carries as the neutral re-spot.
        EXPECT_TRUE(
            tile_passable(world, probe.get(), pin.jump_tx, pin.jump_ty))
            << "scen" << pin.id << " jump ball tile";
        const auto jx = host.eval_integer(
            expr_prefix +
            std::format("M.levels[{}].jump_ball.x end)()", pin.id));
        const auto jy = host.eval_integer(
            expr_prefix +
            std::format("M.levels[{}].jump_ball.y end)()", pin.id));
        ASSERT_TRUE(jx.has_value() && jy.has_value()) << "scen" << pin.id;
        EXPECT_EQ(pin.jump_tx * GRID_SIZE + GRID_SIZE / 2, *jx)
            << "scen" << pin.id << " jump_ball x";
        EXPECT_EQ(pin.jump_ty * GRID_SIZE + GRID_SIZE / 2, *jy)
            << "scen" << pin.id << " jump_ball y";
    }

    // Every court's drumsticks are respawning pads now (#225 reversed the
    // "no item_pads ever" ruling); the per-row pads and interval are pinned
    // in the shipped_levels table and checked against the loaded world by
    // item_pads_mirror_the_world_and_the_off_modes_stay_off.

    // Only the five authored courts are basketball rows: an unauthored id
    // in the band carries no manifest entry at all.
    const auto spare = host.eval_boolean(expr_prefix +
                                         "M.levels[829] == nil end)()");
    ASSERT_TRUE(spare.has_value());
    EXPECT_TRUE(*spare) << "829-839 is spare band, not a shipped court";
}

// NOTE: the pack-vs-archive byte comparison that lived here
// (every_pack_member_matches_its_committed_source) moved to
// tests/unit/test_builtin_archives.cpp and now covers ALL campaigns:
// the archive is COMPOSED from the campaign tree (pack included) by the build
// (og_builtin_campaigns), so producer-side drift became impossible and
// the meaningful residue is the staged-archive-vs-source contract.

// The SHIPPED registration scripts wire the hook set each mode's rules
// actually need. The behavior suites bind the 9xxx test levels through
// hook tables they hand-declare themselves, so a hook line dropped from
// scripts/mode_*.lua would leave every one of them green while the real
// campaign lost, say, its frag scoring. This reads the engine's own
// registration for the shipped level ids.
TEST_F(ModesLevels, shipped_registration_scripts_wire_the_expected_hooks)
{
    using og::script::hooks::level_hook_kinds_for;

    constexpr std::uint32_t kModeInit = 1u << 4;
    constexpr std::uint32_t kModeTick = 1u << 5;
    constexpr std::uint32_t kDamage = 1u << 6;
    constexpr std::uint32_t kRespawn = 1u << 7;
    constexpr std::uint32_t kModePlan = 1u << 8;
    constexpr std::uint32_t kEntityDeath = 1u << 2;
    constexpr std::uint32_t kEntitySpawn = 1u << 3;

    // Loading any level of the campaign builds the world VM and replays the
    // pack scripts, which is what performs the registrations.
    LoadedModesLevel probe(300);
    ASSERT_TRUE(probe.loaded) << "scen300 should load";

    struct ModeHooks
    {
        int level_id;
        const char* mode;
        std::uint32_t expected;
    };
    // Per modes.md: TDM scores frags off deaths; CTF and Soccer carry
    // neither a damage gate nor a death hook (their object rules ride the
    // flag/ball treasure families); Onslaught flips generators through
    // on_damage and scrubs/scores through on_entity_death; Mutant needs
    // both (the one-way matrix and the crown transfer); Basketball adds
    // on_damage (the carrier fumble) but no death hook — the carrier's
    // death is caught by the mode tick's liveness sweep. The two fighter-band
    // modes (FFA, and Mutant since its docs/ffa-design.md §8 conversion) also
    // watch on_entity_spawn for mid-join band adoption.
    // The five mask modes register the plan phase (on_mode_plan); the two
    // fighter-band modes (FFA, Mutant) deliberately do not — they have no
    // team mask to plan and their on_mode_init ignores the chained nil.
    const ModeHooks rows[] = {
        {300, "tdm",
         kModeInit | kModeTick | kEntityDeath | kRespawn | kModePlan},
        {305, "tdm",
         kModeInit | kModeTick | kEntityDeath | kRespawn | kModePlan},
        {500, "ctf", kModeInit | kModeTick | kRespawn | kModePlan},
        {509, "ctf", kModeInit | kModeTick | kRespawn | kModePlan},
        {800, "onslaught",
         kModeInit | kModeTick | kDamage | kRespawn | kEntityDeath |
             kModePlan},
        {803, "onslaught",
         kModeInit | kModeTick | kDamage | kRespawn | kEntityDeath |
             kModePlan},
        {820, "soccer", kModeInit | kModeTick | kRespawn | kModePlan},
        {823, "soccer", kModeInit | kModeTick | kRespawn | kModePlan},
        {824, "basketball",
         kModeInit | kModeTick | kRespawn | kDamage | kModePlan},
        {828, "basketball",
         kModeInit | kModeTick | kRespawn | kDamage | kModePlan},
        {840, "mutant",
         kModeInit | kModeTick | kDamage | kEntityDeath | kEntitySpawn |
             kRespawn},
        {843, "mutant",
         kModeInit | kModeTick | kDamage | kEntityDeath | kEntitySpawn |
             kRespawn},
        {850, "ffa",
         kModeInit | kModeTick | kEntityDeath | kEntitySpawn | kRespawn},
        {855, "ffa",
         kModeInit | kModeTick | kEntityDeath | kEntitySpawn | kRespawn},
    };

    for (const ModeHooks& row : rows)
    {
        EXPECT_EQ(row.expected, level_hook_kinds_for(row.level_id))
            << "scen" << row.level_id << " (" << row.mode
            << "): the shipped registration script's hook table drifted";
    }

    // A level id the campaign does not author registers nothing, so the
    // reader above cannot be answering a constant.
    EXPECT_EQ(0u, level_hook_kinds_for(299))
        << "an unauthored level id must register no hooks";
    EXPECT_EQ(0u, level_hook_kinds_for(700));
}

TEST_F(ModesLevels, manifest_module_matches_package_and_executes)
{
    // The committed og.use("mode_levels") module and the archive member
    // must be the same bytes (regenerate-and-diff discipline), and the
    // chunk must execute clean in the script sandbox.
    const std::vector<std::uint8_t> member = og::resources::read_file(
        "packs/modes.core/lib/mode_levels.lua");
    ASSERT_FALSE(member.empty()) << "manifest member missing from the .glad";
    std::ifstream committed_in(
        "campaigns/modes/packs/modes.core/lib/"
        "mode_levels.lua",
        std::ios::binary);
    ASSERT_TRUE(committed_in.good())
        << "committed manifest missing from the repo";
    std::ostringstream committed_buf;
    committed_buf << committed_in.rdbuf();
    const std::string committed = committed_buf.str();
    const std::string member_text(member.begin(), member.end());
    EXPECT_EQ(committed, member_text)
        << "regenerate the campaign: the committed manifest and the "
           "package member have drifted";

    og::script::ScriptHost host;
    EXPECT_TRUE(host.run_chunk("mode_levels.lua", member_text,
                               "modes.core"))
        << "the manifest chunk failed to execute";
    EXPECT_TRUE(host.errors().empty());

    // Spot-check the data through the sandbox (same env key: the module's
    // global M is not visible — re-run returning fields instead).
    og::script::ScriptHost probe_host;
    const std::string expr_prefix =
        "(function() local M = (function() " +
        member_text.substr(member_text.find("local M = {}")) +
        " end)() return ";
    const auto teams =
        probe_host.eval_integer(expr_prefix + "M.levels[822].teams end)()");
    ASSERT_TRUE(teams.has_value());
    EXPECT_EQ(4, *teams) << "FOURSQUARE is the four-team pitch";
    const auto goal_w = probe_host.eval_integer(
        expr_prefix + "M.levels[820].goal_rects[0].w end)()");
    ASSERT_TRUE(goal_w.has_value());
    EXPECT_EQ(32, *goal_w) << "side goals are 32px deep";
    const auto cap = probe_host.eval_integer(
        expr_prefix + "M.levels[800].spawn_caps[0] end)()");
    ASSERT_TRUE(cap.has_value());
    EXPECT_EQ(24, *cap) << "FOUNDRY LINE caps 24 live spawns per team";
    // The B3 pace cuts (and a healthy-map control).
    const auto gates_limit = probe_host.eval_integer(
        expr_prefix + "M.levels[303].score_limit end)()");
    ASSERT_TRUE(gates_limit.has_value());
    EXPECT_EQ(12, *gates_limit) << "GATEKEEPERS score_limit cut to 12 (B3)";
    const auto castle_limit = probe_host.eval_integer(
        expr_prefix + "M.levels[304].score_limit end)()");
    ASSERT_TRUE(castle_limit.has_value());
    EXPECT_EQ(10, *castle_limit) << "THE CASTLE score_limit cut to 10 (B3)";
    const auto circle_limit = probe_host.eval_integer(
        expr_prefix + "M.levels[300].score_limit end)()");
    ASSERT_TRUE(circle_limit.has_value());
    EXPECT_EQ(20, *circle_limit) << "healthy TDM maps keep 20";
}

// Respawning pickups: the manifest item_pads must mirror the world's live
// respawnable treasures EXACTLY (same multiset of family + tile — the
// same pin the mapgen self-check enforces at generation), the per-family
// counts must match the row table above, and the one remaining OFF mode
// (onslaught, whose spawn attrition IS the mode) must ship no pads at all.
// Soccer and basketball were OFF too until the #225 playtest.
TEST_F(ModesLevels, item_pads_mirror_the_world_and_the_off_modes_stay_off)
{
    const std::vector<std::uint8_t> member = og::resources::read_file(
        "packs/modes.core/lib/mode_levels.lua");
    ASSERT_FALSE(member.empty()) << "manifest member missing from the .glad";
    const std::string member_text(member.begin(), member.end());
    const std::string expr_prefix =
        "(function() local M = (function() " +
        member_text.substr(member_text.find("local M = {}")) +
        " end)() return ";
    og::script::ScriptHost host;
    const std::map<std::string, int> family_index = {
        {"drumstick", 0},
        {"magic_potion", 1},
        {"invis_potion", 2},
        {"speed_potion", 3},
    };

    for (const ShippedModeLevel& pin : shipped_levels())
    {
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        Census c = take_census(loaded.world());

        const auto interval = host.eval_integer(
            expr_prefix +
            std::format("M.levels[{}].item_interval or 0 end)()", pin.id));
        ASSERT_TRUE(interval.has_value()) << "scen" << pin.id;
        EXPECT_EQ(pin.item_interval, *interval) << "scen" << pin.id;

        if (pin.item_interval == 0)
        {
            // OFF modes: their worlds may author static food, but the
            // manifest must carry no pads (item_pads means MANIFEST pads).
            const auto absent = host.eval_boolean(
                expr_prefix +
                std::format("M.levels[{}].item_pads == nil end)()", pin.id));
            ASSERT_TRUE(absent.has_value()) << "scen" << pin.id;
            EXPECT_TRUE(*absent)
                << "scen" << pin.id << " (" << pin.mode
                << "): the OFF modes must ship no item pads";
            continue;
        }

        for (std::size_t i = 0; i < 4; ++i)
            EXPECT_EQ(pin.item_pads[i], c.items[i])
                << "scen" << pin.id << " respawnable family " << i;

        // Serialize the row's pads through the sandbox and compare the
        // (family, tile) multiset against the loaded world.
        const auto pads = host.eval_string(
            expr_prefix +
            std::format("(function() local s = \"\" "
                        "local pads = M.levels[{}].item_pads "
                        "for i = 1, #pads do "
                        "s = s .. pads[i].family .. \",\" .. pads[i].x .. "
                        "\",\" .. pads[i].y .. \";\" end "
                        "return s end)() end)()",
                        pin.id));
        ASSERT_TRUE(pads.has_value()) << "scen" << pin.id;
        std::vector<std::array<int, 3>> manifest_tiles;
        std::stringstream stream(*pads);
        std::string entry;
        while (std::getline(stream, entry, ';'))
        {
            if (entry.empty())
                continue;
            std::stringstream fields(entry);
            std::string family, x, y;
            ASSERT_TRUE(std::getline(fields, family, ','));
            ASSERT_TRUE(std::getline(fields, x, ','));
            ASSERT_TRUE(std::getline(fields, y, ','));
            const auto idx = family_index.find(family);
            ASSERT_NE(family_index.end(), idx)
                << "scen" << pin.id << ": pad family '" << family
                << "' is not respawnable (gold and friends never respawn)";
            manifest_tiles.push_back({idx->second,
                                      std::stoi(x) / GRID_SIZE,
                                      std::stoi(y) / GRID_SIZE});
        }
        std::sort(manifest_tiles.begin(), manifest_tiles.end());
        std::sort(c.item_tiles.begin(), c.item_tiles.end());
        EXPECT_EQ(c.item_tiles, manifest_tiles)
            << "scen" << pin.id
            << ": manifest item_pads drifted from the authored treasures";
    }
}

// The roster modes (ffa, mutant) post how many competitors the mode fills
// their arena to. Every other mode's row must carry no `fighters` key at
// all — the absence is what kept their manifest bytes unchanged when the
// field arrived, so it is pinned in both directions.
TEST_F(ModesLevels, manifest_fighters_covers_the_roster_modes_only)
{
    const std::vector<std::uint8_t> member = og::resources::read_file(
        "packs/modes.core/lib/mode_levels.lua");
    ASSERT_FALSE(member.empty()) << "manifest member missing from the .glad";
    const std::string member_text(member.begin(), member.end());
    const std::string expr_prefix =
        "(function() local M = (function() " +
        member_text.substr(member_text.find("local M = {}")) +
        " end)() return ";
    og::script::ScriptHost host;

    for (const ShippedModeLevel& pin : shipped_levels())
    {
        const auto fighters = host.eval_integer(
            expr_prefix +
            std::format("M.levels[{}].fighters or 0 end)()", pin.id));
        ASSERT_TRUE(fighters.has_value()) << "scen" << pin.id;
        EXPECT_EQ(pin.fighters, *fighters)
            << "scen" << pin.id << " (" << pin.mode << ") fighters";

        const auto absent = host.eval_boolean(
            expr_prefix +
            std::format("M.levels[{}].fighters == nil end)()", pin.id));
        ASSERT_TRUE(absent.has_value()) << "scen" << pin.id;
        EXPECT_EQ(pin.fighters == 0, *absent)
            << "scen" << pin.id << " (" << pin.mode
            << "): a non-roster row must omit `fighters` entirely";
    }
}

// The FFA arenas' own manifest tuning: a frag race to 15 inside a ten
// minute clock, and a hard zero spawn cap on all four pools (the mode
// retires authored generators, so nothing may ever add a body to a
// sixteen-way brawl).
TEST_F(ModesLevels, ffa_arena_rows_post_the_deathmatch_tuning)
{
    const std::vector<std::uint8_t> member = og::resources::read_file(
        "packs/modes.core/lib/mode_levels.lua");
    ASSERT_FALSE(member.empty()) << "manifest member missing from the .glad";
    const std::string member_text(member.begin(), member.end());
    const std::string expr_prefix =
        "(function() local M = (function() " +
        member_text.substr(member_text.find("local M = {}")) +
        " end)() return ";
    og::script::ScriptHost host;

    int ffa_rows = 0;
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        if (std::string(pin.mode) != "ffa")
            continue;
        ++ffa_rows;
        const auto mode = host.eval_string(
            expr_prefix + std::format("M.levels[{}].mode end)()", pin.id));
        ASSERT_TRUE(mode.has_value()) << "scen" << pin.id;
        EXPECT_EQ("ffa", *mode) << "scen" << pin.id;

        const auto score = host.eval_integer(
            expr_prefix +
            std::format("M.levels[{}].score_limit end)()", pin.id));
        ASSERT_TRUE(score.has_value()) << "scen" << pin.id;
        EXPECT_EQ(15, *score) << "scen" << pin.id << " score_limit";

        const auto clock = host.eval_integer(
            expr_prefix +
            std::format("M.levels[{}].time_limit end)()", pin.id));
        ASSERT_TRUE(clock.has_value()) << "scen" << pin.id;
        EXPECT_EQ(7200, *clock) << "scen" << pin.id << " time_limit";

        for (int team = 0; team < 4; ++team)
        {
            const auto cap = host.eval_integer(
                expr_prefix + std::format("M.levels[{}].spawn_caps[{}] end)()",
                                          pin.id, team));
            ASSERT_TRUE(cap.has_value())
                << "scen" << pin.id << ": no spawn cap for pool " << team;
            EXPECT_EQ(0, *cap) << "scen" << pin.id << " pool " << team;
        }
    }
    EXPECT_EQ(6, ffa_rows) << "the campaign ships six FFA arenas";
}

// FFA authoring contract (docs/ffa-design.md sections 6-7): the four start
// clusters are INTERLEAVED around the arena, not parked in four corners.
// The mode walks them as position pools rather than team homes, so every
// quadrant must offer a spot from every pool — otherwise the placement
// rotation drops fighters in clumps. Mutant's corner clusters (one lobby
// team per corner) are the contrast case that proves the probe bites.
TEST_F(ModesLevels, ffa_start_clusters_interleave_over_the_arena)
{
    auto quadrant_team_masks = [](GameWorld& world) {
        std::array<int, 4> masks{};
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->query_order() != Order::Special ||
                ob->family() != FAMILY_RESERVED_TEAM)
                continue;
            const int tx = ob->xpos() / GRID_SIZE;
            const int ty = ob->ypos() / GRID_SIZE;
            const int quadrant = (tx < world.grid.w / 2 ? 0 : 1) +
                                 (ty < world.grid.h / 2 ? 0 : 2);
            masks[static_cast<std::size_t>(quadrant)] |=
                1 << std::min<int>(ob->team_num(), 3);
        }
        return masks;
    };
    static constexpr const char* kQuadrant[4] = {"NW", "NE", "SW", "SE"};

    int ffa_arenas = 0;
    for (const ShippedModeLevel& pin : shipped_levels())
    {
        if (std::string(pin.mode) != "ffa")
            continue;
        ++ffa_arenas;
        LoadedModesLevel loaded(pin.id);
        ASSERT_TRUE(loaded.loaded) << "scen" << pin.id;
        const std::array<int, 4> masks = quadrant_team_masks(loaded.world());
        for (int q = 0; q < 4; ++q)
            EXPECT_EQ(0xF, masks[static_cast<std::size_t>(q)])
                << "scen" << pin.id << " quadrant " << kQuadrant[q]
                << ": every pool must reach every quadrant";
    }
    EXPECT_EQ(6, ffa_arenas);

    LoadedModesLevel pit(840);
    ASSERT_TRUE(pit.loaded);
    const std::array<int, 4> mutant_masks = quadrant_team_masks(pit.world());
    for (int q = 0; q < 4; ++q)
        EXPECT_EQ(1, std::popcount(static_cast<unsigned>(
                         mutant_masks[static_cast<std::size_t>(q)])))
            << "scen840 quadrant " << kQuadrant[q]
            << ": the Mutant maps park exactly one lobby team per corner, "
               "which is the layout the FFA probe above must reject";
}

TEST_F(ModesLevels, pack_sprites_load_with_pinned_shapes)
{
    const struct { const char* path; int w; int h; int frames; } sprites[] = {
        {"icon.png", 32, 32, 1},
        {"packs/modes.core/sprites/flag.png", 10, 14, 4},
        {"packs/modes.core/sprites/ctfpoint.png", 16, 16, 1},
        {"packs/modes.core/sprites/ball.png", 12, 12, 8},
        {"packs/modes.core/sprites/bball.png", 12, 12, 8},
        {"packs/modes.core/sprites/bshadow.png", 12, 12, 4},
        {"packs/modes.core/sprites/hoop.png", 24, 26, 6},
        {"packs/modes.core/sprites/aura.png", 16, 16, 4},
    };
    for (const auto& s : sprites)
    {
        const PixieData pix = read_pixie_file(s.path);
        ASSERT_TRUE(pix.valid()) << s.path;
        EXPECT_EQ(s.w, static_cast<int>(pix.w)) << s.path;
        EXPECT_EQ(s.h, static_cast<int>(pix.h)) << s.path;
        EXPECT_EQ(s.frames, static_cast<int>(pix.frames)) << s.path;
    }
}

TEST_F(ModesLevels, embedded_pack_rides_the_mount_cycle)
{
    // Mounted: the pack lib member resolves through the VFS.
    EXPECT_FALSE(og::resources::read_file(
                     "packs/modes.core/lib/mode_levels.lua")
                     .empty());
    // Unmounted: it leaves with its campaign.
    ASSERT_EQ(CampaignPackageIoError::None,
              unmount_campaign_package_with_error("modes"));
    EXPECT_TRUE(og::resources::read_file(
                    "packs/modes.core/lib/mode_levels.lua")
                    .empty());
    // Remount so teardown finds the state it expects.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
}

TEST_F(ModesLevels, scripted_levels_tick_clean_without_mode_lua)
{
    // One level per mode: a full sim context, 30 real ticks. With no mode
    // scripts landed yet the scripted fork must be a clean no-op — no
    // script errors, no spurious level end. (The per-mode dispatch smokes
    // arrive with the Lua-mode waves.)
    for (const int id : {300, 500, 800, 820, 824, 840, 850})
    {
        LoadedModesLevel loaded(id, 7u);
        ASSERT_TRUE(loaded.loaded) << "scen" << id;
        for (int i = 0; i < 30; ++i)
            loaded.world().tick();
        EXPECT_TRUE(loaded.world().scripts().host().errors().empty())
            << "scen" << id << " recorded script errors";
    }
}

} // namespace
