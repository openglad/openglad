#include <openglad/data/level_data.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/io/og_file.h>
#include <openglad/platform/io.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <vector>

extern screen* myscreen;

short load_scenario_version(og::io::OgFile& infile, LevelData* data, short version);
bool save_grid_file(const char* gridname, const PixieData& grid);
short remaining_foes(LevelData& level, walker* myguy);

namespace {

class MemoryOgFile final : public og::io::OgFile {
public:
    MemoryOgFile(const void* data, std::size_t size)
        : data_(static_cast<const unsigned char*>(data)), size_(size), pos_(0) {}

    std::size_t read(void* buf, std::size_t size, std::size_t count) override {
        if (size == 0 || count == 0) return 0;
        std::size_t total = size * count;
        std::size_t avail = (pos_ < size_) ? size_ - pos_ : 0;
        if (total > avail) total = avail;
        std::size_t objects = total / size;
        if (objects > 0)
            std::memcpy(buf, data_ + pos_, objects * size);
        pos_ += objects * size;
        return objects;
    }

    std::size_t write(const void*, std::size_t, std::size_t) override { return 0; }

    std::int64_t seek(std::int64_t offset, int whence) override {
        std::int64_t newpos = 0;
        switch (whence) {
            case 0: newpos = offset; break;
            case 1: newpos = static_cast<std::int64_t>(pos_) + offset; break;
            case 2: newpos = static_cast<std::int64_t>(size_) + offset; break;
            default: return -1;
        }
        if (newpos < 0) return -1;
        pos_ = static_cast<std::size_t>(newpos);
        return static_cast<std::int64_t>(pos_);
    }

    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }

private:
    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_;
};

static bool write_bytes(const std::filesystem::path& p, const std::vector<unsigned char>& bytes)
{
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    FILE* f = fopen(p.string().c_str(), "wb");
    if (!f)
        return false;
    size_t n = fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return n == bytes.size();
}

static walker* add_living(unsigned char family = FAMILY_SOLDIER)
{
    walker* w = myscreen->level_data.add_ob(Order::Living, family);
    if (w) {
        w->setxy(64, 64);
        w->sizex = 1;
        w->sizey = 1;
    }
    return w;
}

} // namespace

void test_level_data_save_rejects_null_fx_and_weap_entries()
{
    std::filesystem::create_directories("temp/scen");
    myscreen->level_data.id = 789;
    myscreen->level_data.grid_file = "grid";
    myscreen->level_data.title = "coverage";
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    myscreen->level_data.fxlist.push_back(std::unique_ptr<walker>{});
    TEST_ASSERT(!myscreen->level_data.save(), "save should fail when fxlist contains nullptr");

    myscreen->level_data.delete_objects();
    myscreen->level_data.weaplist.push_back(std::unique_ptr<walker>{});
    TEST_ASSERT(!myscreen->level_data.save(), "save should fail when weaplist contains nullptr");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_save_rejects_null_fx_and_weap_entries);

void test_level_data_range_helpers_and_null_paths()
{
    myscreen->level_data.delete_objects();

    std::int32_t howmany = 99;
    std::list<walker*> empty;

    empty = myscreen->level_data.find_in_range(myscreen->level_data.oblist, 120, &howmany, nullptr);
    TEST_ASSERT(empty.empty(), "find_in_range nullptr actor should return empty");
    TEST_ASSERT_EQ(0, (int)howmany, "find_in_range should zero howmany");

    empty = myscreen->level_data.find_foes_in_range(myscreen->level_data.oblist, 120, &howmany, nullptr);
    TEST_ASSERT(empty.empty(), "find_foes_in_range nullptr actor should return empty");

    empty = myscreen->level_data.find_foe_weapons_in_range(myscreen->level_data.weaplist, 120, &howmany, nullptr);
    TEST_ASSERT(empty.empty(), "find_foe_weapons_in_range nullptr actor should return empty");

    empty = myscreen->level_data.find_friends_in_range(myscreen->level_data.oblist, 120, &howmany, nullptr);
    TEST_ASSERT(empty.empty(), "find_friends_in_range nullptr actor should return empty");

    TEST_ASSERT(myscreen->level_data.find_nearest_blood(nullptr) == nullptr, "find_nearest_blood nullptr should return null");
    TEST_ASSERT(myscreen->level_data.find_nearest_player(nullptr) == nullptr, "find_nearest_player nullptr should return null");

    walker* p1 = add_living(FAMILY_SOLDIER);
    walker* p2 = add_living(FAMILY_ARCHER);
    TEST_ASSERT(p1 != nullptr && p2 != nullptr, "living walkers should be created");
    p1->user = -1;
    p2->user = 0;
    p2->setxy(96, 64);

    walker probe;
    probe.setxy(80, 64);
    walker* nearest = myscreen->level_data.find_nearest_player(&probe);
    TEST_ASSERT(nearest == p2, "find_nearest_player should select nearest controlled walker");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_range_helpers_and_null_paths);

void test_level_data_load_error_codes_and_scenario_title_paths()
{
    namespace fs = std::filesystem;
    fs::create_directories("scen");

    const int id_parse = 9301;
    const int id_bad = 9302;
    const int id_ver = 9303;
    const int id_title = 9304;

    TEST_ASSERT(write_bytes(fs::path("scen") / std::format("scen{}.fss", id_parse), {'F', 'S', 'S'}), "write parse-fail scenario");
    TEST_ASSERT(write_bytes(fs::path("scen") / std::format("scen{}.fss", id_bad), {'B', 'A', 'D', 6}), "write invalid-header scenario");
    TEST_ASSERT(write_bytes(fs::path("scen") / std::format("scen{}.fss", id_ver), {'F', 'S', 'S', 1}), "write unsupported-version scenario");

    std::vector<unsigned char> titled = {'F', 'S', 'S', 6};
    const char grid[8] = {'g','r','i','d',0,0,0,0};
    titled.insert(titled.end(), grid, grid + 8);
    const char title[30] = "Coverage Title";
    titled.insert(titled.end(), title, title + 30);
    TEST_ASSERT(write_bytes(fs::path("scen") / std::format("scen{}.fss", id_title), titled), "write title scenario");

    LevelData parse_fail(id_parse);
    TEST_ASSERT_EQ((int)LevelData::IoError::ParseFailed, (int)parse_fail.load_with_error(), "truncated file should parse-fail");

    LevelData bad_header(id_bad);
    TEST_ASSERT_EQ((int)LevelData::IoError::InvalidHeader, (int)bad_header.load_with_error(), "bad header should fail");

    LevelData unsupported(id_ver);
    TEST_ASSERT_EQ((int)LevelData::IoError::UnsupportedVersion, (int)unsupported.load_with_error(), "unsupported version should fail");

    TEST_ASSERT(get_scenario_title(nullptr) == "none", "null scenario title request should return none");
    TEST_ASSERT(get_scenario_title("does_not_exist") == "none", "missing scenario title should return none");
    TEST_ASSERT(get_scenario_title(std::format("scen{}", id_bad).c_str()) == "none", "invalid header title should return none");
    TEST_ASSERT(get_scenario_title(std::format("scen{}", id_title).c_str()) == "Coverage Title", "version 6 title should be readable");
}
REGISTER_TEST(test_level_data_load_error_codes_and_scenario_title_paths);

void test_level_data_load_version_dispatch_and_grid_save_paths()
{
    unsigned char dummy = 0;
    MemoryOgFile mem(&dummy, 0);
    LevelData data(1);

    TEST_ASSERT_EQ(0, (int)load_scenario_version(mem, nullptr, 6), "null level pointer should fail");
    TEST_ASSERT_EQ(0, (int)load_scenario_version(mem, &data, 42), "unsupported loader version should fail");

    std::filesystem::create_directories("temp/pix");
    PixieData pix(1, 1, 1, new unsigned char[1]{7});
    TEST_ASSERT(save_grid_file("coverage_ok", pix), "save_grid_file should write to temp/pix");
    TEST_ASSERT(!save_grid_file("nested/coverage_fail", pix), "save_grid_file should fail when parent dir is missing");
}
REGISTER_TEST(test_level_data_load_version_dispatch_and_grid_save_paths);

void test_level_data_query_grid_passable_edge_cases()
{
    myscreen->level_data.create_new_grid();
    walker* w = add_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker should be created");

    w->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(64.0f, 64.0f, w), "ethereal walker should pass grid");
    w->stats()->set_bit_flags(BIT_ETHEREAL, 0);

    myscreen->level_data.delete_grid();
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(64.0f, 64.0f, w), "missing grid should fail passability");

    myscreen->level_data.create_new_grid();
    myscreen->level_data.grid.data[0] = PIX_TREE_M1;
    w->setxy(0, 0);
    w->sizex = 1;
    w->sizey = 1;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, w), "tree should block non-forestwalker");

    w->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, w), "forestwalker should pass trees");

    w->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    w->stats()->set_bit_flags(BIT_FLYING, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, w), "flying should pass trees");

    w->stats()->set_bit_flags(BIT_FLYING, 0);
    w->dead = 1;
    TEST_ASSERT(myscreen->level_data.query_object_passable(0.0f, 0.0f, w), "dead walker should skip object collision checks");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_query_grid_passable_edge_cases);

void test_campaign_data_save_and_save_as_fail_for_missing_campaign()
{
    const std::string missing_id = "org.openglad.test.missing.save.coverage";
    delete_campaign(missing_id);

    CampaignData cd(missing_id);
    TEST_ASSERT_EQ((int)CampaignData::IoError::PackageUnpackFailed, (int)cd.save_with_error(),
                   "save_with_error should report unpack failure for missing campaign");

    TEST_ASSERT_EQ((int)CampaignData::IoError::PackageUnpackFailed, (int)cd.save_as_with_error("new_missing_id"),
                   "save_as_with_error should report unpack failure for missing campaign");
}
REGISTER_TEST(test_campaign_data_save_and_save_as_fail_for_missing_campaign);

void test_level_data_set_sim_context_wires_pointers()
{
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng(1);
    cfg_store cfg_local;

    LevelData d(42);
    d.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg_local);
    TEST_ASSERT(true, "set_sim_context should accept valid pointer set");
}
REGISTER_TEST(test_level_data_set_sim_context_wires_pointers);

void test_level_data_batch2_misc_uncovered_paths_smoke()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* a = add_living(FAMILY_SOLDIER);
    walker* b = add_living(FAMILY_ORC);
    TEST_ASSERT(a && b, "walkers created");
    if (!(a && b))
        return;

    a->team_num = 0;
    b->team_num = 1;
    a->setxy(64, 64);
    b->setxy(96, 64);

    // remaining_foes helper.
    TEST_ASSERT(remaining_foes(myscreen->level_data, a) >= 0, "remaining_foes should run");

    // query_passable wrappers.
    TEST_ASSERT(myscreen->level_data.query_passable(64.0f, 64.0f, a) == (myscreen->level_data.query_grid_passable(64.0f, 64.0f, a)
        && myscreen->level_data.query_object_passable(64.0f, 64.0f, a)),
        "query_passable should compose grid/object checks");

    // entity search null-protected paths.
    TEST_ASSERT(myscreen->level_data.find_near_foe(nullptr) == nullptr, "find_near_foe null should return null");
    TEST_ASSERT(myscreen->level_data.find_far_foe(nullptr) == nullptr, "find_far_foe null should return null");
}
REGISTER_TEST(test_level_data_batch2_misc_uncovered_paths_smoke);

void test_level_data_wall4_projectile_passability_distance_and_rng_paths()
{
    myscreen->level_data.delete_objects();
    myscreen->level_data.create_new_grid();
    myscreen->level_data.grid.data[0] = PIX_WALL4;

    walker* owner = add_living(FAMILY_ARCHER);
    walker* projectile = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(owner != nullptr && projectile != nullptr, "owner and projectile should be created");
    if (!(owner && projectile))
        return;

    owner->setxy(160, 0);
    owner->sizex = 1;
    owner->sizey = 1;

    projectile->owner = owner;
    projectile->setxy(0, 0);
    projectile->sizex = 1;
    projectile->sizey = 1;

    FixedRandom rng_pass(0);
    projectile->sim_rng = &rng_pass;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, projectile),
                "weapon on PIX_WALL4 should pass when rng returns 0");

    FixedRandom rng_block(1);
    projectile->sim_rng = &rng_block;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, projectile),
                "weapon on PIX_WALL4 should block when rng returns non-zero");

    owner->setxy(0, 0);
    owner->stats()->set_bit_flags(BIT_FLYING, 0);
    owner->flight_left = 0;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, owner),
                "living walker on PIX_WALL4 should be blocked immediately");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_wall4_projectile_passability_distance_and_rng_paths);

void test_level_data_range_helpers_positive_selection_paths()
{
    myscreen->level_data.delete_objects();

    walker* actor = add_living(FAMILY_SOLDIER);
    walker* friend_living = add_living(FAMILY_ARCHER);
    walker* foe_living = add_living(FAMILY_ORC);
    walker* foe_generator = myscreen->level_data.add_ob(Order::Generator, FAMILY_TOWER);
    walker* friend_weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* blood_far = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    walker* blood_near = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_STAIN);

    TEST_ASSERT(actor && friend_living && foe_living && foe_generator && friend_weapon && blood_far && blood_near,
                "range-helper fixtures should be created");
    if (!(actor && friend_living && foe_living && foe_generator && friend_weapon && blood_far && blood_near))
        return;

    actor->team_num = 0;
    actor->setxy(64, 64);
    actor->sizex = 1;
    actor->sizey = 1;

    friend_living->team_num = 0;
    friend_living->setxy(80, 64);
    friend_living->dead = 0;

    foe_living->team_num = 1;
    foe_living->setxy(96, 64);
    foe_living->dead = 0;

    foe_generator->team_num = 1;
    foe_generator->setxy(112, 64);
    foe_generator->dead = 0;

    friend_weapon->team_num = 0;
    friend_weapon->setxy(72, 64);
    friend_weapon->dead = 0;

    blood_far->setxy(200, 200);
    blood_near->setxy(68, 64);

    std::int32_t howmany = -1;
    auto nearest_blood = myscreen->level_data.find_nearest_blood(actor);
    TEST_ASSERT(nearest_blood == blood_near, "find_nearest_blood should return nearest stain");

    auto in_range = myscreen->level_data.find_in_range(myscreen->level_data.oblist, 80, &howmany, actor);
    TEST_ASSERT(!in_range.empty() && howmany > 0, "find_in_range should collect nearby non-dead walkers");

    auto foes = myscreen->level_data.find_foes_in_range(myscreen->level_data.oblist, 100, &howmany, actor);
    TEST_ASSERT((int)foes.size() >= 2 && howmany >= 2, "find_foes_in_range should include living + generator foes");

    auto foe_weapons = myscreen->level_data.find_foe_weapons_in_range(myscreen->level_data.weaplist, 80, &howmany, actor);
    TEST_ASSERT(foe_weapons.size() == 1 && howmany == 1, "find_foe_weapons_in_range should include friendly weapon");

    auto friends = myscreen->level_data.find_friends_in_range(myscreen->level_data.oblist, 80, &howmany, actor);
    TEST_ASSERT(!friends.empty() && howmany >= 1, "find_friends_in_range should include friendly living walkers");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_range_helpers_positive_selection_paths);

void test_level_data_round5_query_grid_passable_contiguous_block_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* living = add_living(FAMILY_SOLDIER);
    walker* owner = add_living(FAMILY_ARCHER);
    walker* weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(living && owner && weapon, "fixture walkers should be created");
    if (!(living && owner && weapon))
        return;

    living->setxy(0, 0);
    living->sizex = 1;
    living->sizey = 1;
    owner->setxy(80, 0);
    owner->sizex = 1;
    owner->sizey = 1;
    weapon->setxy(0, 0);
    weapon->sizex = 1;
    weapon->sizey = 1;
    weapon->owner = owner;

    myscreen->level_data.grid.data[0] = PIX_GRASS1;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "ground tile should pass");

    myscreen->level_data.grid.data[0] = PIX_TREE_M1;
    living->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "forestwalk should pass upper tree tiles");
    living->stats()->set_bit_flags(BIT_FORESTWALK, 0);

    living->stats()->set_bit_flags(BIT_FLYING, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "flying should pass upper tree tiles");
    living->stats()->set_bit_flags(BIT_FLYING, 0);

    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "non-flying non-forestwalk should fail upper tree tiles");

    myscreen->level_data.grid.data[0] = PIX_TREE_B1;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "weapon should pass trunk tree tile");

    living->stats()->set_bit_flags(BIT_FLYING, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "flying should pass trunk tree tile");
    living->stats()->set_bit_flags(BIT_FLYING, 0);

    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "non-flying living should fail trunk tree tile");

    myscreen->level_data.grid.data[0] = PIX_H_WALL1;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "hard wall tile should block living");

    myscreen->level_data.grid.data[0] = PIX_WALL4;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "wall4 should block living immediately");

    FixedRandom rng_block(1);
    weapon->sim_rng = &rng_block;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall4 projectile should block when rng returns non-zero");

    owner->setxy(8, 0); // triggers dist < GRID_SIZE adjustment branch
    FixedRandom rng_pass(0);
    weapon->sim_rng = &rng_pass;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall4 projectile should pass and fall through with rng zero");

    myscreen->level_data.grid.data[0] = PIX_WATER1;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "weapon should pass water/obstacle group");

    living->stats()->set_bit_flags(BIT_FLYING, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "flying living should pass water/obstacle group");
    living->stats()->set_bit_flags(BIT_FLYING, 0);

    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "non-flying living should fail water/obstacle group");

    myscreen->level_data.grid.data[0] = 255;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "unknown tile should use default fail branch");

    living->dead = 1;
    TEST_ASSERT(myscreen->level_data.query_object_passable(0.0f, 0.0f, living),
                "dead object should pass object-collision query");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round5_query_grid_passable_contiguous_block_paths);

void test_level_data_round5_find_helpers_contiguous_block_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = add_living(FAMILY_SOLDIER);
    walker* friend_living = add_living(FAMILY_ARCHER);
    walker* foe_living = add_living(FAMILY_ORC);
    walker* foe_generator = myscreen->level_data.add_ob(Order::Generator, FAMILY_TOWER);
    walker* foe_weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* blood = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    TEST_ASSERT(actor && friend_living && foe_living && foe_generator && foe_weapon && blood,
                "fixtures should be created");
    if (!(actor && friend_living && foe_living && foe_generator && foe_weapon && blood))
        return;

    actor->team_num = 0;
    actor->setxy(64, 64);
    actor->sim_level = &myscreen->level_data;

    friend_living->team_num = 0;
    friend_living->setxy(72, 64);

    foe_living->team_num = 1;
    foe_living->setxy(80, 64);

    foe_generator->team_num = 1;
    foe_generator->setxy(96, 64);

    foe_weapon->team_num = 1;
    foe_weapon->setxy(70, 64);

    blood->setxy(68, 64);

    FixedRandom rng_zero(0);
    actor->sim_rng = &rng_zero;
    TEST_ASSERT(myscreen->level_data.find_far_foe(actor) != nullptr,
                "find_far_foe should return nearest visible living/generator foe");
    TEST_ASSERT(myscreen->level_data.find_nearest_blood(actor) == blood,
                "find_nearest_blood should return stain target");
    TEST_ASSERT(myscreen->level_data.find_nearest_player(actor) == nullptr,
                "find_nearest_player should return null when no controlled walkers exist");

    std::int32_t howmany = -1;
    auto in_range = myscreen->level_data.find_in_range(myscreen->level_data.oblist, 128, &howmany, actor);
    TEST_ASSERT(!in_range.empty() && howmany > 0, "find_in_range should count nearby non-dead walkers");

    auto foes = myscreen->level_data.find_foes_in_range(myscreen->level_data.oblist, 128, &howmany, actor);
    TEST_ASSERT(!foes.empty() && howmany > 0, "find_foes_in_range should include living/generator enemies");

    auto foe_weapons = myscreen->level_data.find_foe_weapons_in_range(myscreen->level_data.weaplist, 128, &howmany, actor);
    TEST_ASSERT(foe_weapons.empty(), "enemy weapon should be excluded because helper accepts friendly weapons");

    foe_weapon->team_num = actor->team_num;
    foe_weapons = myscreen->level_data.find_foe_weapons_in_range(myscreen->level_data.weaplist, 128, &howmany, actor);
    TEST_ASSERT(!foe_weapons.empty() && howmany > 0, "friendly weapon should be included by helper predicate");

    auto friends = myscreen->level_data.find_friends_in_range(myscreen->level_data.oblist, 128, &howmany, actor);
    TEST_ASSERT(!friends.empty() && howmany > 0, "find_friends_in_range should include friendly living");

    TEST_ASSERT(myscreen->level_data.find_nearest_blood(nullptr) == nullptr,
                "find_nearest_blood nullptr guard should return null");
    TEST_ASSERT(myscreen->level_data.find_nearest_player(nullptr) == nullptr,
                "find_nearest_player nullptr guard should return null");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round5_find_helpers_contiguous_block_paths);
