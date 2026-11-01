// The spawn-room gate (#423): does every living spawn of the scenario table
// stand on ground its own body may stand on?
//
// The table places every spawn with setxy, which asks no passability
// (scenario_runtime.cpp). A living put on ground it cannot occupy is given a
// tick of flight and loses 1 hp every act tick (living.cpp), so a caster in
// the lake flies out and drowns while the row says it casts, and a summon
// beside it has no room. The arena once hid this: a passability check that
// used the caster itself as the probe passed the water on the caster's own
// flight and never met the caster's own body.
//
// So the gate never probes with the caster. For every distinct level among
// kScenarios it loads the level once (run_scenario with no ticks and no
// spawns: the grid as the rows see it) and asks og::sim::body_ground_clear,
// a detached body of the spawn's own family (its own box and flags, no
// flight), for every living SpawnSpec of every row on that level. Bodies are
// not asked: bodies touching and overlapping by a pixel are arena design in
// dozens of rows.

#include "parity_runner.h"
#include "scenario_table.h"

#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/respawn/respawn_state.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using og::parity::kOrderLiving;
using og::parity::kScenarios;
using og::parity::ScenarioSpec;
using og::parity::SpawnSpec;

// Spawns that stand in terrain on purpose (#423): a living on impassable
// ground is given a tick of flight and loses 1 hp a tick (living.cpp), so a
// caster there flies out and drowns, and a summon beside it has no room. The
// gate probes every living spawn with a detached body of its own family,
// never the caster; a spawn it finds in terrain must be here with its
// reason, and a listed spawn that stands on ground fails the gate too.
struct TerrainSpawn
{
    std::string_view id;
    std::size_t      spawn_index;
    std::string_view reason;
};

constexpr TerrainSpawn kTerrainSpawns[] = {
    { "exit_trigger_scen9302", 1,
      "a hostile orc parked in the lake at (64, 64), away from the player whose walk the row measures; it flies out drowning 1 hp a tick, and the golden records that" },
    { "smoke_nonempty_scen99", 1,
      "a hostile orc parked in the lake at (64, 64), away from the player whose walk the row measures; it flies out drowning 1 hp a tick, and the golden records that" },
    { "smoke_nonempty_scen99_inputs", 1,
      "a hostile orc parked in the lake at (64, 64), away from the player whose walk the row measures; it flies out drowning 1 hp a tick, and the golden records that" },
    { "family_golem_scen99", 1,
      "a large far foe whose box reaches past the map's edge at (600, 680); it drowns 1 hp a tick and is dead by the final tick, which the golden records (the row counts the family's spawn and its events)" },
    { "family_giant_skeleton_scen99", 1,
      "a large far foe whose box reaches past the map's edge at (600, 680); it drowns 1 hp a tick and is dead by the final tick, which the golden records (the row counts the family's spawn and its events)" },
    { "weapon_tree_emission_scen99", 2,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "weapon_wave2_emission_scen99", 2,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "weapon_wave3_emission_scen99", 2,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "weapon_circle_protection_emission_scen99", 2,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "weapon_door_emission_scen99", 2,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "effect_explosion_emission_scen99", 4,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "effect_marker_emission_scen99", 1,
      "parked off the map at (2000, 2000) on purpose: it never enters the play" },
    { "effect_chain_emission_scen99", 3,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "event_notification_emission_scen99", 3,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "event_set_palette_emission_scen99", 3,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "event_request_redraw_emission_scen99", 3,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "event_end_game_emission_scen99", 3,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "event_set_end_emission_scen99", 3,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "speed_potion_movement_scen99", 2,
      "a hostile orc parked in the lake at (64, 64), away from the player whose walk the row measures; it flies out drowning 1 hp a tick, and the golden records that" },
    { "invulnerable_potion_scen99", 2,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "summon_lifetime_faerie_scen99", 1,
      "parked off the map at (2000, 2000) on purpose: it never enters the play" },
    { "summon_lifetime_decrement_faerie_scen99", 1,
      "parked off the map at (2000, 2000) on purpose: it never enters the play" },
    { "generator_saturation_scen99", 1,
      "parked off the map at (2000, 2000) on purpose: it never enters the play" },
    { "effect_heartburst_multitarget_scen99", 4,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "effect_protection_emit_scen99", 0,
      "the protected ally stands on the lake shore; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "treasure_invis_effect_scen99", 1,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "effect_cloud_poison_scen99", 0,
      "the stationary target TOWER1 parked on the lake shore under the cloud; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "effect_boomerang_drawcycle_cap_scen99", 0,
      "parked off the map at (2000, 2000) on purpose: it never enters the play" },
    { "weapon_circle_protection_follow_scen99", 0,
      "the protected ally stands on the lake shore; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "mage_freeze_time_offteam_scen99", 0,
      "no setting-on twin: the mage and its two allied orcs start in the lake and drown-fly out, and the bonus-round movement fact measures play that starts with that flight; moving them would be a recapture of a classic row" },
    { "mage_freeze_time_offteam_scen99", 1,
      "no setting-on twin: the mage and its two allied orcs start in the lake and drown-fly out, and the bonus-round movement fact measures play that starts with that flight; moving them would be a recapture of a classic row" },
    { "mage_freeze_time_offteam_scen99", 2,
      "no setting-on twin: the mage and its two allied orcs start in the lake and drown-fly out, and the bonus-round movement fact measures play that starts with that flight; moving them would be a recapture of a classic row" },
    { "elf_mega_rocks_volley_scen99", 1,
      "a hostile target standing on a tree bottom beside the caster; it is given a tick of flight and loses 1 hp a tick there, and the golden records that" },
    { "soldier_whirlwind_no_foe_scen99", 0,
      "the far bot soldier stands on a tree bottom and flies off drowning; the caster is the player at (120, 120), on grass" },
    { "soldier_whirlwind_no_foe_fixes_scen99", 0,
      "the far bot soldier stands on a tree bottom and flies off drowning; the caster is the player at (120, 120), on grass" },
    { "elf_volley_refund_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "elf_volley_refund_fixes_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "mage_bot_freeze_no_foe_scen99", 1,
      "classic twin, untouched by the PR #404 ruling: the mage casts from the lake, drowns 1 hp a tick and flies out; its golden records that (the setting-on twin stands at (160, 160))" },
    { "mage_bot_freeze_alone_scen99", 1,
      "classic twin, untouched by the PR #404 ruling: the mage stops time from the lake on tick 0, drowns 1 hp a tick and flies out; its golden records that (the setting-on twin stands at (160, 160))" },
    { "mage_bot_freeze_engaged_scen99", 1,
      "the stationary target TOWER1 parked on a tree bottom; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "mage_bot_freeze_engaged_fixes_scen99", 1,
      "the stationary target TOWER1 parked on a tree bottom; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "archmage_summon_no_room_scen99", 0,
      "one of the four hostile TOWER1s 100 px out that the HEARTBURST splits its pool over; it stands in terrain and loses 1 hp a tick there, and the row's facts were derived with that in" },
    { "archmage_summon_no_room_scen99", 2,
      "one of the four hostile TOWER1s 100 px out that the HEARTBURST splits its pool over; it stands in terrain and loses 1 hp a tick there, and the row's facts were derived with that in" },
    { "archmage_summon_no_room_scen99", 3,
      "one of the four hostile TOWER1s 100 px out that the HEARTBURST splits its pool over; it stands in terrain and loses 1 hp a tick there, and the row's facts were derived with that in" },
    { "archmage_summon_no_room_fixes_scen99", 0,
      "one of the four hostile TOWER1s 100 px out that the HEARTBURST splits its pool over; it stands in terrain and loses 1 hp a tick there, and the row's facts were derived with that in" },
    { "archmage_summon_no_room_fixes_scen99", 2,
      "one of the four hostile TOWER1s 100 px out that the HEARTBURST splits its pool over; it stands in terrain and loses 1 hp a tick there, and the row's facts were derived with that in" },
    { "archmage_summon_no_room_fixes_scen99", 3,
      "one of the four hostile TOWER1s 100 px out that the HEARTBURST splits its pool over; it stands in terrain and loses 1 hp a tick there, and the row's facts were derived with that in" },
    { "archmage_bot_summon_no_foe_scen99", 0,
      "classic twin: the archmage drowns in the lake and teleports out before its counted SUMMON IMAGE; the fixed twin stands at (160, 160)" },
    { "elf_bot_rocks_far_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "elf_bot_rocks_far_fixes_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "archer_bot_ring_one_foe_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "archer_bot_ring_one_foe_fixes_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "barbarian_bot_boulder_far_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "barbarian_bot_boulder_far_fixes_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "barbarian_bot_explode_lone_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "barbarian_bot_explode_lone_fixes_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "elemental_bot_starburst_one_foe_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "elemental_bot_starburst_one_foe_fixes_scen99", 0,
      "the stationary target TOWER1 parked in the lake; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "archmage_bot_image_one_foe_scen99", 0,
      "the stationary target TOWER1 parked on a tree bottom; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
    { "archmage_bot_image_one_foe_fixes_scen99", 0,
      "the stationary target TOWER1 parked on a tree bottom; it loses 1 hp a tick standing there, and the row's hp facts were derived with that in" },
};

std::string describe(const ScenarioSpec& spec, std::size_t index)
{
    const SpawnSpec& s = spec.spawns[index];
    std::ostringstream os;
    os << spec.id << " spawn " << index << " (family " << s.family
       << ", team " << static_cast<int>(s.team) << ", at " << s.x << ", "
       << s.y << ", floor " << s.floor << ")";
    return os.str();
}

// The grid a row's spawns are placed on: its level file and, for the
// multi-floor rows, the floors it builds before spawning.
using LevelKey = std::tuple<std::string_view, std::int32_t, const og::parity::FloorPaint*, std::size_t>;

LevelKey level_key(const ScenarioSpec& spec)
{
    return {spec.scenario_file, spec.floor_count, spec.floor_paints, spec.floor_paint_count};
}

struct SpawnRoomReport
{
    std::size_t              probed = 0;   // living spawns asked
    std::size_t              levels = 0;   // distinct grids loaded
    std::vector<std::string> unlisted;     // in terrain, not in kTerrainSpawns
    std::vector<std::string> rotted;       // in kTerrainSpawns, on ground
};

SpawnRoomReport probe_every_spawn()
{
    std::set<std::pair<std::string_view, std::size_t>> listed;
    for (const auto& t : kTerrainSpawns)
        listed.emplace(t.id, t.spawn_index);

    SpawnRoomReport report;
    std::set<LevelKey> done;
    for (const auto& first : kScenarios)
    {
        const LevelKey key = level_key(first);
        if (!done.insert(key).second)
            continue;
        ++report.levels;

        // The level as run_scenario builds it, with nothing spawned and no
        // tick run: the probe reads the grid only.
        ScenarioSpec bare = first;
        bare.spawn_count = 0;
        bare.tick_budget = 0;
        (void)og::parity::run_scenario(bare, [&](GameWorld& world) {
            for (const auto& spec : kScenarios)
            {
                if (level_key(spec) != key)
                    continue;
                for (std::size_t i = 0; i < spec.spawn_count; ++i)
                {
                    const SpawnSpec& s = spec.spawns[i];
                    if (s.order != kOrderLiving)
                        continue;
                    ++report.probed;
                    const bool ground = og::sim::body_ground_clear(
                        world, Order::Living, s.family, static_cast<short>(s.x),
                        static_cast<short>(s.y), s.floor);
                    const bool is_listed = listed.count({spec.id, i}) != 0;
                    if (!ground && !is_listed)
                        report.unlisted.push_back(describe(spec, i));
                    if (ground && is_listed)
                        report.rotted.push_back(describe(spec, i));
                }
            }
        });
    }
    return report;
}

std::string join(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& l : lines)
        out += "  " + l + "\n";
    return out;
}

} // namespace

TEST(Parity, coverage_gate_spawn_room)
{
    // Every allow-list entry names a row and a living spawn that exist, once,
    // with a reason: a stale entry would hide nothing and say something false.
    std::set<std::pair<std::string_view, std::size_t>> seen;
    std::vector<std::string> stale;
    for (const auto& t : kTerrainSpawns)
    {
        if (!seen.emplace(t.id, t.spawn_index).second)
            stale.push_back(std::string(t.id) + " spawn " + std::to_string(t.spawn_index) + ": listed twice");
        if (t.reason.empty())
            stale.push_back(std::string(t.id) + " spawn " + std::to_string(t.spawn_index) + ": no reason");
        const ScenarioSpec* row = nullptr;
        for (const auto& spec : kScenarios)
            if (spec.id == t.id)
                row = &spec;
        if (row == nullptr || t.spawn_index >= row->spawn_count
                || row->spawns[t.spawn_index].order != kOrderLiving)
            stale.push_back(std::string(t.id) + " spawn " + std::to_string(t.spawn_index) + ": no such living spawn");
    }
    EXPECT_EQ(std::string{}, join(stale)) << "kTerrainSpawns entries that name nothing";

    std::size_t living = 0;
    std::set<LevelKey> levels;
    for (const auto& spec : kScenarios)
    {
        levels.insert(level_key(spec));
        for (std::size_t i = 0; i < spec.spawn_count; ++i)
            if (spec.spawns[i].order == kOrderLiving)
                ++living;
    }

    const SpawnRoomReport report = probe_every_spawn();
    // Each grid is loaded once and every living spawn is asked exactly once.
    EXPECT_EQ(levels.size(), report.levels);
    EXPECT_EQ(living, report.probed);
    EXPECT_EQ(std::string{}, join(report.unlisted))
        << "living spawns standing in terrain (water, a tree, the map's edge) "
           "that kTerrainSpawns does not list: move the spawn onto ground, or "
           "list it with the reason it stands there";
    EXPECT_EQ(std::string{}, join(report.rotted))
        << "kTerrainSpawns lists these spawns, but they stand on ground: take "
           "them off the list";
}
