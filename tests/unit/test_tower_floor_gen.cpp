/* Headless unit tests for the Tower Climb floor generator (WP-5).
 *
 * Covers: floor_seed purity, the §5.6 ramp knob table, the (run_seed, N) ->
 * byte-identical .fss + PNG determinism contract (generate-twice compare —
 * this contract IS the future MP door), the audit sweep over floors 1-60 x
 * 3 run seeds (the recipe's coverage engine), the T0 fallback path, the
 * headless reload self-check (westlands pattern), the open-stairs briefing
 * consistency, and the D7 .glad member-list invariant (the shipped package
 * carries the Gate ONLY — ids >= 701 would shadow and freeze a run).
 * All byte/tick driven — no wall-clock timing.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/tower_constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mapgen/builders.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/mapgen/tower_floor_gen.h>

#include "westlands_sim_fixture.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using og::tower::build_tower_floor;
using og::tower::floor_seed;
using og::tower::generate_tower_floor_to_user_dir;
using og::tower::TowerFloorReport;

// Remove every possibly-left tower floor file (tests generate sparse ids,
// so the contiguous run-start prune shape is not enough here).
void prune_all_floors()
{
    for (int id = og::kTowerFirstFloorLevel; id <= 760; ++id)
        (void)og::data::delete_tower_floor_files(id);
}

std::vector<unsigned char> file_bytes(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
}

// Snapshot every artifact of floor N: the .fss plus all grid/decor PNGs.
std::map<std::string, std::vector<unsigned char>> floor_artifacts(int id)
{
    std::map<std::string, std::vector<unsigned char>> out;
    const fs::path user(get_user_path());
    const fs::path fss = user / "scen" / std::format("scen{}.fss", id);
    if (fs::exists(fss))
        out[fss.filename().string()] = file_bytes(fss);
    const std::string grid = std::format("scen{:04d}", id);
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(user / "pix", ec))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind(grid, 0) == 0)
            out[name] = file_bytes(entry.path());
    }
    return out;
}

class TowerFloorGenTest : public ::testing::Test
{
protected:
    void SetUp() override { prune_all_floors(); }
    void TearDown() override { prune_all_floors(); }
};

// --- floor_seed + ramp knobs. -------------------------------------------------

TEST(TowerSeed, floor_seed_pure_and_distinct)
{
    EXPECT_EQ(floor_seed(42u, 3), floor_seed(42u, 3));
    EXPECT_NE(floor_seed(42u, 3), floor_seed(42u, 4));
    EXPECT_NE(floor_seed(42u, 3), floor_seed(43u, 3));
    // Distinct across a whole lap (no accidental collisions in the small n).
    std::vector<std::uint32_t> seen;
    for (int n = 1; n <= 60; ++n)
        seen.push_back(floor_seed(0xA11CEu, n));
    std::sort(seen.begin(), seen.end());
    EXPECT_TRUE(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
}

TEST(TowerRamp, knob_table)
{
    using namespace og::tower;
    // L(f) = 1 + (f-1)/3, soft cap 50.
    EXPECT_EQ(1, foe_level_for_floor(1));
    EXPECT_EQ(1, foe_level_for_floor(3));
    EXPECT_EQ(2, foe_level_for_floor(4));
    EXPECT_EQ(7, foe_level_for_floor(20));
    EXPECT_EQ(50, foe_level_for_floor(148));
    EXPECT_EQ(50, foe_level_for_floor(500));
    // N(f) = min(7 + f, 30).
    EXPECT_EQ(8, foe_count_for_floor(1));
    EXPECT_EQ(29, foe_count_for_floor(22));
    EXPECT_EQ(30, foe_count_for_floor(23));
    EXPECT_EQ(30, foe_count_for_floor(90));
    // Boss floors: every 5th.
    EXPECT_FALSE(is_boss_floor(1));
    EXPECT_TRUE(is_boss_floor(5));
    EXPECT_TRUE(is_boss_floor(30));
    EXPECT_FALSE(is_boss_floor(31));
    // Bands cycle every 30 floors; the ramp (lap) never resets.
    EXPECT_EQ(0, band_index_for_floor(1));
    EXPECT_EQ(0, band_index_for_floor(5));
    EXPECT_EQ(1, band_index_for_floor(6));
    EXPECT_EQ(3, band_index_for_floor(16));
    EXPECT_EQ(5, band_index_for_floor(26));
    EXPECT_EQ(5, band_index_for_floor(30));
    EXPECT_EQ(0, band_index_for_floor(31));
    EXPECT_EQ(0, lap_for_floor(30));
    EXPECT_EQ(1, lap_for_floor(31));
    EXPECT_EQ(2, lap_for_floor(61));
    // Elites: f/5 on lap 0; +10%-of-N share per lap.
    EXPECT_EQ(1, elite_slots_for_floor(5));
    EXPECT_EQ(4, elite_slots_for_floor(20));
    EXPECT_EQ(7 + 3, elite_slots_for_floor(35)); // 35/5 + 30*10%/lap1
}

// --- Determinism (§5.4): the generate-twice byte-compare. ---------------------

TEST_F(TowerFloorGenTest, generate_twice_byte_identical)
{
    constexpr std::uint32_t kRunSeed = 0xA11CEu;
    // One floor per interesting shape: Bailey, Barracks, Spires (the air
    // template), a boss/vault floor.
    for (const int floor_number : {1, 7, 16, 20})
    {
        SCOPED_TRACE("floor " + std::to_string(floor_number));
        const int id = og::kTowerGateLevel + floor_number;

        const TowerFloorReport first =
            generate_tower_floor_to_user_dir(kRunSeed, floor_number);
        ASSERT_TRUE(first.written);
        ASSERT_TRUE(og::data::tower_floor_files_exist(id));
        const auto bytes_a = floor_artifacts(id);
        ASSERT_TRUE(bytes_a.count(std::format("scen{}.fss", id)) == 1);
        ASSERT_GE(bytes_a.size(), 2u) << "expected the .fss plus >= 1 PNG";

        ASSERT_TRUE(og::data::delete_tower_floor_files(id));
        ASSERT_FALSE(og::data::tower_floor_files_exist(id));

        const TowerFloorReport second =
            generate_tower_floor_to_user_dir(kRunSeed, floor_number);
        ASSERT_TRUE(second.written);
        EXPECT_EQ(first.attempts, second.attempts);
        const auto bytes_b = floor_artifacts(id);

        ASSERT_EQ(bytes_a.size(), bytes_b.size());
        for (const auto& [name, bytes] : bytes_a)
        {
            ASSERT_TRUE(bytes_b.count(name) == 1) << name << " missing";
            EXPECT_EQ(bytes, bytes_b.at(name))
                << name << " is not byte-identical across regeneration";
        }
    }
}

TEST_F(TowerFloorGenTest, different_run_seed_differs)
{
    const int id = og::kTowerGateLevel + 1;
    ASSERT_TRUE(generate_tower_floor_to_user_dir(1u, 1).written);
    const auto bytes_a = floor_artifacts(id);
    prune_all_floors();
    ASSERT_TRUE(generate_tower_floor_to_user_dir(2u, 1).written);
    const auto bytes_b = floor_artifacts(id);
    EXPECT_NE(bytes_a, bytes_b)
        << "different run seeds produced byte-identical floors";
}

// --- The audit sweep: floors 1-60 x 3 run seeds (the coverage engine). --------

TEST(TowerAuditSweep, floors_1_to_60_x3_seeds_build_clean)
{
    int fallback_builds = 0;
    int reroll_builds = 0;
    for (const std::uint32_t run_seed : {42u, 1337u, 2025u})
        for (int f = 1; f <= 60; ++f)
        {
            SCOPED_TRACE("run_seed " + std::to_string(run_seed) + " floor " +
                         std::to_string(f));
            bool clean = false;
            for (int attempt = 0; attempt < 4 && !clean; ++attempt)
            {
                GameWorld world(0);
                std::list<std::string> description;
                const std::vector<std::string> failures =
                    build_tower_floor(world, description, run_seed, f,
                                      attempt);
                if (failures.empty())
                {
                    clean = true;
                    if (attempt > 0)
                        ++reroll_builds;
                    if (attempt == 3)
                        ++fallback_builds;
                    break;
                }
                if (attempt == 3)
                {
                    std::string joined;
                    for (const std::string& e : failures)
                        joined += e + "\n";
                    ADD_FAILURE()
                        << "even the T0 fallback failed audits:\n" << joined;
                }
            }
            EXPECT_TRUE(clean);
        }
    // The recipe should rarely need its safety nets; a drift in these
    // numbers is worth eyes even while the sweep stays green.
    std::printf("tower audit sweep: %d reroll builds, %d fallback builds "
                "(180 floors)\n",
                reroll_builds, fallback_builds);
    EXPECT_LE(fallback_builds, 9) << "T0 fallback rate exploded";
}

// --- The T0 fallback is audit-clean by construction. ---------------------------

TEST(TowerFallback, t0_fallback_is_audit_clean)
{
    for (const int f : {2, 16, 30})
    {
        SCOPED_TRACE("floor " + std::to_string(f));
        GameWorld world(0);
        std::list<std::string> description;
        const std::vector<std::string> failures =
            build_tower_floor(world, description, 0xBADF00Du, f,
                              /*attempt=*/3);
        std::string joined;
        for (const std::string& e : failures)
            joined += e + "\n";
        EXPECT_TRUE(failures.empty()) << joined;
        EXPECT_LE(world.floor_count(), 2) << "fallback keeps it simple";

        ASSERT_FALSE(world.oblist.empty());
        walker* const entity = world.oblist.front().get();
        ASSERT_NE(nullptr, entity);
        const Order order = entity->query_order();
        const int family = entity->family();
        const PixieData* const graphics =
            world.configure_existing_entity(*entity, order, family);
        ASSERT_NE(nullptr, graphics);
        EXPECT_EQ(order, entity->query_order());
        EXPECT_EQ(family, entity->family());
        entity->set_stepsize(-101.0f);
        entity->set_normal_stepsize(-102.0f);
        entity->set_lineofsight(-103);
        entity->set_damage(-104.0f);
        entity->set_fire_frequency(-105.0f);
        world.set_entity_derived_stats(
            entity, entity->query_order(), entity->family());
        EXPECT_NE(-101.0f, entity->stepsize());
        EXPECT_FLOAT_EQ(entity->stepsize(), entity->normal_stepsize());
        EXPECT_NE(-103, entity->lineofsight());
        EXPECT_NE(-104.0f, entity->damage());
        EXPECT_NE(-105.0f, entity->fire_frequency());
    }
}

// --- Reload self-check (westlands pattern: LevelRuntimeData + headless hooks).

TEST_F(TowerFloorGenTest, generated_floor_reloads_headlessly)
{
    constexpr std::uint32_t kRunSeed = 20260713u;
    ASSERT_TRUE(generate_tower_floor_to_user_dir(kRunSeed, 1).written);

    westlands_fixture::LoadedWestlandsLevel fx(og::kTowerGateLevel + 1, 42u);
    ASSERT_TRUE(fx.loaded) << "generated floor must load from the user path";
    GameWorld& world = fx.world();

    EXPECT_EQ("Floor 1", world.title);
    EXPECT_NE(0, world.type & GameWorld::TYPE_TOWER);
    EXPECT_GE(world.floor_count(), 1);
    for (int f = 0; f < world.floor_count(); ++f)
        EXPECT_TRUE(world.grid_for_floor(f).valid()) << "floor " << f;

    ASSERT_FALSE(fx.level.description.empty());
    for (const std::string& line : fx.level.description)
        EXPECT_LE(line.size(), 33u) << line;

    int starts = 0;
    int livings = 0;
    int generators = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
            ++starts;
        else if (ob->query_order() == Order::Living)
        {
            ++livings;
            EXPECT_TRUE(ob->team_num() == 2 || ob->team_num() == 3)
                << "foes climb on teams 2/3 (never the hold-post teams 0/1)";
        }
        else if (ob->query_order() == Order::Generator)
            ++generators;
    }
    EXPECT_EQ(10, starts) << "the crew deploys onto 10 markers";
    EXPECT_GT(livings, 0);
    EXPECT_LE(livings + 8 * generators, 120) << "MAXOBS Frenzy budget";

    int exits = 0;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT)
        {
            ++exits;
            EXPECT_EQ(og::kTowerGateLevel + 2, ob->stats()->level())
                << "the exit chains to the NEXT floor id";
        }
    }
    EXPECT_EQ(1, exits);

    // Round-trip footing: the audit passes on the RELOADED world too.
    const auto footing = og::mapgen::audit_footing(world);
    std::string joined;
    for (const std::string& e : footing)
        joined += e + "\n";
    EXPECT_TRUE(footing.empty()) << joined;
}

// --- Open-stairs floors and their briefing line agree. -------------------------

TEST(TowerOpenStairs, type_bit_and_briefing_agree_and_boss_floors_never_open)
{
    int open_floors = 0;
    for (int f = 1; f <= 20; ++f)
    {
        SCOPED_TRACE("floor " + std::to_string(f));
        GameWorld world(0);
        std::list<std::string> description;
        (void)build_tower_floor(world, description, 42u, f, /*attempt=*/0);
        const bool open = (world.type & SCEN_TYPE_CAN_EXIT) != 0;
        bool announced = false;
        for (const std::string& line : description)
            if (line == "The stairs stand open.")
                announced = true;
        EXPECT_EQ(open, announced);
        EXPECT_NE(0, world.type & GameWorld::TYPE_TOWER);
        if (og::tower::is_boss_floor(f))
        {
            EXPECT_FALSE(open) << "boss floors never leave the stairs open";
        }
        if (open)
            ++open_floors;
    }
    EXPECT_GT(open_floors, 0) << "~1 in 4 non-boss floors should be open";
    EXPECT_LT(open_floors, 10);
}

// --- D7: the shipped package holds the Gate ONLY (unit-test enforcement). ------

TEST(TowerPackage, glad_member_list_has_no_floor_ids)
{
    const fs::path package =
        fs::absolute("builtin/org.openglad.tower.glad");
    ASSERT_TRUE(fs::exists(package))
        << package << " missing — run scripts/generate_tower_campaign.sh";

    const std::string mountpoint = "tower_pkg_check";
    ASSERT_TRUE(og::resources::mount(package.string().c_str(),
                                     mountpoint.c_str(), 1))
        << og::resources::filesystem_last_error();

    const std::list<std::string> root =
        og::resources::list_files(mountpoint.c_str());
    EXPECT_TRUE(std::find(root.begin(), root.end(), "campaign.yaml") !=
                root.end());
    EXPECT_TRUE(std::find(root.begin(), root.end(), "icon.png") != root.end());

    const std::list<std::string> scens =
        og::resources::list_files((mountpoint + "/scen").c_str());
    EXPECT_EQ(1u, scens.size());
    for (const std::string& name : scens)
        EXPECT_EQ("scen700.fss", name)
            << "a floor id >= 701 in the package would shadow (and freeze) "
               "every generated run";

    const std::list<std::string> pix =
        og::resources::list_files((mountpoint + "/pix").c_str());
    for (const std::string& name : pix)
        EXPECT_EQ("scen0700.png", name);

    EXPECT_TRUE(og::resources::unmount(package.string().c_str()));
}

} // namespace
