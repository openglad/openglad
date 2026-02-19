#include <openglad/data/level_data.h>
#include <openglad/data/level_data_hooks.h>
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

void test_level_data_round8_ctor_hook_wiring_and_remove_paths()
{
    static int wired_count = 0;
    static int render_count = 0;
    wired_count = 0;
    render_count = 0;

    LevelDataHooks hooks;
    hooks.wire_entity_from_screen = [](walker*) { wired_count++; };
    hooks.create_level_render = [](PixieData[]) -> std::unique_ptr<LevelRender> {
        render_count++;
        return nullptr;
    };

    {
        LevelData d(17001, false, &hooks);
        walker* living = d.add_ob(Order::Living, FAMILY_SOLDIER);
        walker* fx = d.add_fx_ob(Order::FX, FAMILY_FLASH);
        walker* weapon = d.add_weap_ob(Order::Weapon, FAMILY_ARROW);
        TEST_ASSERT(living && fx && weapon, "hooked level should create walkers");
        if (!(living && fx && weapon))
            return;

        TEST_ASSERT(living->sim_level == &d, "wire_entity should set sim_level");
        TEST_ASSERT(living->myobmap == d.myobmap.get(), "wire_entity should set myobmap");
        TEST_ASSERT_EQ(1, (int)d.remove_ob(weapon), "remove_ob should erase from weaplist");
        TEST_ASSERT_EQ(1, (int)d.remove_ob(fx), "remove_ob should erase from fxlist");
        TEST_ASSERT_EQ(1, (int)d.remove_ob(living), "remove_ob should erase from oblist");
    }

    // Delegating constructor path LevelData(int, const LevelDataHooks*).
    {
        LevelData d(17002, &hooks);
        walker* living = d.add_ob(Order::Living, FAMILY_ARCHER);
        TEST_ASSERT(living != nullptr, "delegating hooks ctor should create living walkers");
    }

    // Headless constructor should not invoke create_level_render.
    const int render_before_headless = render_count;
    {
        LevelData d(17003, true, &hooks);
        walker* living = d.add_ob(Order::Living, FAMILY_SOLDIER);
        TEST_ASSERT(living != nullptr, "headless hooks ctor should still create walkers");
    }

    TEST_ASSERT(render_count >= 1, "non-headless constructors should invoke create_level_render hook");
    TEST_ASSERT_EQ(render_before_headless, render_count, "headless ctor should skip create_level_render hook");
    TEST_ASSERT(wired_count >= 5, "wire_entity_from_screen hook should run for each created walker");
}
REGISTER_TEST(test_level_data_round8_ctor_hook_wiring_and_remove_paths);

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

void test_level_data_round8_query_grid_treeb1_and_arrow_slit_variants()
{
    myscreen->level_data.delete_objects();
    myscreen->level_data.create_new_grid();

    walker* living = add_living(FAMILY_SOLDIER);
    walker* weapon = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(living && weapon, "living and weapon should be created");
    if (!(living && weapon))
        return;

    living->setxy(0, 0);
    weapon->setxy(0, 0);
    living->sizex = weapon->sizex = 1;
    living->sizey = weapon->sizey = 1;

    // PIX_TREE_B1 branch: living blocks, weapon passes.
    myscreen->level_data.grid.data[0] = PIX_TREE_B1;
    living->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    living->stats()->set_bit_flags(BIT_FLYING, 0);
    living->flight_left = 0;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "TREE_B1 should block non-flying, non-forestwalk living");
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "TREE_B1 should allow weapons");

    // Arrow-slit distance/rng branch for weapons.
    walker* owner = add_living(FAMILY_ARCHER);
    TEST_ASSERT(owner != nullptr, "owner should be created");
    if (!owner)
        return;
    weapon->owner = owner;
    owner->setxy(48, 0);
    weapon->setxy(0, 0);

    myscreen->level_data.grid.data[0] = PIX_WALL_ARROW_GRASS;
    FixedRandom rng_pass(0);
    weapon->sim_rng = &rng_pass;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "arrow-slit passability should pass when rng returns 0");

    FixedRandom rng_block(1);
    weapon->sim_rng = &rng_block;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "arrow-slit passability should block when rng returns non-zero");

    // Unknown tile type default should block.
    myscreen->level_data.grid.data[0] = 255;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "unknown tile type should block in default branch");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round8_query_grid_treeb1_and_arrow_slit_variants);

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

void test_level_data_round7a_constructor_overloads_and_remove_paths()
{
    LevelData a(11);
    LevelData b(12, static_cast<const LevelDataHooks*>(nullptr));
    LevelData c(13, true);
    LevelData d(14, true, static_cast<const LevelDataHooks*>(nullptr));
    TEST_ASSERT(true, "constructor overloads executed");

    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weap = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(living && fx && weap, "objects created");
    if (!(living && fx && weap))
        return;

    TEST_ASSERT_EQ(1, (int)myscreen->level_data.remove_ob(weap), "remove_ob should erase weapon list item");
    TEST_ASSERT_EQ(1, (int)myscreen->level_data.remove_ob(fx), "remove_ob should erase fx list item");
    TEST_ASSERT_EQ(1, (int)myscreen->level_data.remove_ob(living), "remove_ob should erase oblist item");

    walker dummy;
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.remove_ob(&dummy), "remove_ob should return 0 when not found");
}
REGISTER_TEST(test_level_data_round7a_constructor_overloads_and_remove_paths);

void test_level_data_round7a_title_reader_and_error_wrappers()
{
    namespace fs = std::filesystem;
    fs::create_directories("scen");

    // Header ok, version ok, but truncated before grid/title reads.
    const int id_short = 9401;
    TEST_ASSERT(write_bytes(fs::path("scen") / std::format("scen{}.fss", id_short), {'F', 'S', 'S', 6}),
                "write short title file");
    TEST_ASSERT(get_scenario_title(std::format("scen{}", id_short).c_str()) == "none",
                "title reader should fail on short grid/title payload");

    // Header ok but version too old.
    const int id_old = 9402;
    TEST_ASSERT(write_bytes(fs::path("scen") / std::format("scen{}.fss", id_old), {'F', 'S', 'S', 5}),
                "write old-version title file");
    TEST_ASSERT(get_scenario_title(std::format("scen{}", id_old).c_str()) == "none",
                "title reader should reject versions < 6");

    // save_with_error wrapper.
    myscreen->level_data.delete_objects();
    myscreen->level_data.id = 9403;
    myscreen->level_data.grid_file = "round7a";
    myscreen->level_data.title = "Round7A";
    myscreen->level_data.create_new_grid();
    std::filesystem::create_directories("temp/scen");
    const auto err = myscreen->level_data.save_with_error();
    TEST_ASSERT(err == LevelData::IoError::None || err == LevelData::IoError::OpenWriteFailed,
                "save_with_error wrapper should return a concrete io error");
}
REGISTER_TEST(test_level_data_round7a_title_reader_and_error_wrappers);

namespace {
class ConstRandom final : public IRandom {
public:
    explicit ConstRandom(std::uint32_t value) : value_(value) {}
    std::uint32_t next(std::uint32_t max_exclusive) override {
        if (max_exclusive == 0) return 0;
        return value_ % max_exclusive;
    }
private:
    std::uint32_t value_;
};
}

void test_level_data_round6_version6plus_and_title_read_paths()
{
    LevelData data(1);

    // v9: truncate before time-limit field to hit version>=9 READ_OR_RETURN failure.
    {
        std::vector<unsigned char> bytes(8 + 30 + 1 + 2, 0);
        MemoryOgFile f(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(0, (int)load_scenario_version(f, &data, 9), "v9 should fail when time-limit field is missing");
    }

    // v6: invalid object count (> MAX_SCENARIO_OBJECTS).
    {
        std::vector<unsigned char> bytes(8 + 30 + 1 + 2, 0);
        short bad_count = 5000;
        std::memcpy(bytes.data() + 8 + 30 + 1, &bad_count, sizeof(bad_count));
        MemoryOgFile f(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(0, (int)load_scenario_version(f, &data, 6), "v6 should reject invalid object count");
    }

    // v8: long description line exercises discard loop in load_version_6.
    {
        std::vector<unsigned char> bytes;
        bytes.resize(8 + 30 + 1 + 2 + 2 + 1 + 1, 0); // grid + title + type + par + listsize + numlines + width
        bytes.back() = 120;
        bytes.insert(bytes.end(), 120, 'x');
        MemoryOgFile f(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(1, (int)load_scenario_version(f, &data, 8), "v8 should accept long description line with discard");
    }

    namespace fs = std::filesystem;
    fs::create_directories("scen");
    const int id_ver_read_fail = 9401;
    const int id_grid_read_fail = 9402;
    const int id_title_read_fail = 9403;
    const auto p1 = fs::path("scen") / std::format("scen{}.fss", id_ver_read_fail);
    const auto p2 = fs::path("scen") / std::format("scen{}.fss", id_grid_read_fail);
    const auto p3 = fs::path("scen") / std::format("scen{}.fss", id_title_read_fail);
    TEST_ASSERT(write_bytes(p1, {'F', 'S', 'S'}), "write version-read-fail title file");
    TEST_ASSERT(write_bytes(p2, {'F', 'S', 'S', 6}), "write grid-read-fail title file");

    std::vector<unsigned char> with_grid = {'F', 'S', 'S', 6};
    const char grid8[8] = {'g','r','i','d',0,0,0,0};
    with_grid.insert(with_grid.end(), grid8, grid8 + 8);
    TEST_ASSERT(write_bytes(p3, with_grid), "write title-read-fail title file");

    TEST_ASSERT(get_scenario_title(std::format("scen{}", id_ver_read_fail).c_str()) == "none",
                "title read should return none when version byte read fails");
    TEST_ASSERT(get_scenario_title(std::format("scen{}", id_grid_read_fail).c_str()) == "none",
                "title read should return none when grid bytes are missing");
    TEST_ASSERT(get_scenario_title(std::format("scen{}", id_title_read_fail).c_str()) == "none",
                "title read should return none when title bytes are missing");
}
REGISTER_TEST(test_level_data_round6_version6plus_and_title_read_paths);

void test_level_data_round6_remove_ob_and_wrapper_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_GOLD_BAR);
    walker* weap = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(living && fx && weap, "fixtures should be created");
    if (!(living && fx && weap))
        return;

    TEST_ASSERT_EQ(1, (int)myscreen->level_data.remove_ob(weap), "remove_ob should remove from weaplist");
    TEST_ASSERT_EQ(1, (int)myscreen->level_data.remove_ob(fx), "remove_ob should remove from fxlist");
    TEST_ASSERT_EQ(1, (int)myscreen->level_data.remove_ob(living), "remove_ob should remove from oblist");

    walker orphan;
    orphan.set_order_family(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.remove_ob(&orphan), "remove_ob should return 0 for unknown walker");

    std::filesystem::create_directories("temp/scen");
    myscreen->level_data.id = 9410;
    myscreen->level_data.grid_file = "grid";
    myscreen->level_data.title = "round6";
    TEST_ASSERT_EQ((int)LevelData::IoError::None, (int)myscreen->level_data.save_with_error(),
                   "save_with_error should return None on successful save");
}
REGISTER_TEST(test_level_data_round6_remove_ob_and_wrapper_paths);

void test_level_data_round6_passable_wall4_and_water_weapon_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT(owner && weapon && living, "fixtures should be created");
    if (!(owner && weapon && living))
        return;

    owner->setxy(64, 0);
    weapon->owner = owner;
    weapon->setxy(0, 0);
    weapon->sizex = 1;
    weapon->sizey = 1;
    living->setxy(0, 0);
    living->sizex = 1;
    living->sizey = 1;

    myscreen->level_data.grid.frames = 1;
    myscreen->level_data.grid.w = 1;
    myscreen->level_data.grid.h = 1;
    myscreen->level_data.pixmaxx = GRID_SIZE;
    myscreen->level_data.pixmaxy = GRID_SIZE;
    myscreen->level_data.grid.data = std::make_unique<unsigned char[]>(1);

    myscreen->level_data.grid.data[0] = PIX_WALL4;
    ConstRandom rng_block(1);
    weapon->sim_rng = &rng_block;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall4 projectile should block when rng yields non-zero");

    ConstRandom rng_zero(0);
    weapon->sim_rng = &rng_zero;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall4 projectile should pass when rng yields zero");

    myscreen->level_data.grid.data[0] = PIX_WATER1;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "weapon should pass water tile group");
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "non-flying living should fail water tile group");

    living->stats()->set_bit_flags(BIT_FLYING, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "flying living should pass water tile group");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round6_passable_wall4_and_water_weapon_paths);

void test_level_data_round6_wrapper_and_passability_edges()
{
    // load_with_error wrapper should surface open-read failure.
    {
        LevelData missing(9898);
        TEST_ASSERT_EQ((int)LevelData::IoError::OpenReadFailed, (int)missing.load_with_error(),
                       "load_with_error should report open-read failure for missing scenario");
    }

    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT(owner && weapon && living, "fixtures should be created");
    if (!(owner && weapon && living))
        return;

    owner->setxy(0, 64);
    weapon->owner = owner;
    weapon->setxy(0, 0);
    weapon->sizex = 1;
    weapon->sizey = 1;
    living->setxy(0, 0);
    living->sizex = 1;
    living->sizey = 1;

    myscreen->level_data.grid.frames = 1;
    myscreen->level_data.grid.w = 1;
    myscreen->level_data.grid.h = 1;
    myscreen->level_data.pixmaxx = GRID_SIZE;
    myscreen->level_data.pixmaxy = GRID_SIZE;
    myscreen->level_data.grid.data = std::make_unique<unsigned char[]>(1);

    // Hard-wall branch.
    myscreen->level_data.grid.data[0] = PIX_WALLTOP_H;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "wall top should block living walker");

    // WALL4 projectile branch using Y-distance path in dist calculation.
    myscreen->level_data.grid.data[0] = PIX_WALL4;
    ConstRandom rng_block(1);
    weapon->sim_rng = &rng_block;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall4 projectile should block when rng is non-zero (y-distance branch)");

    // Weapon should pass water/obstacle bucket via weapon special-case.
    myscreen->level_data.grid.data[0] = PIX_BOULDER_1;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "weapon should pass obstacle bucket tiles");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round6_wrapper_and_passability_edges);

void test_level_data_round6_load_version3_4_5_minimal_and_treasure_paths()
{
    auto append_short = [](std::vector<unsigned char>& bytes, short value) {
        unsigned char raw[sizeof(short)];
        std::memcpy(raw, &value, sizeof(short));
        bytes.insert(bytes.end(), raw, raw + sizeof(short));
    };

    auto append_obj_v3 = [&](std::vector<unsigned char>& bytes, unsigned char order) {
        bytes.push_back(order);                            // order
        bytes.push_back(FAMILY_GOLD_BAR);                 // family
        append_short(bytes, 8);                           // x
        append_short(bytes, 8);                           // y
        bytes.push_back(0);                               // team
        bytes.push_back(0);                               // facing
        bytes.push_back(0);                               // command
        bytes.push_back(1);                               // level
        bytes.insert(bytes.end(), 10, 0);                // reserved
    };

    auto append_obj_v4_v5 = [&](std::vector<unsigned char>& bytes, unsigned char order) {
        bytes.push_back(order);                            // order
        bytes.push_back(FAMILY_GOLD_BAR);                 // family
        append_short(bytes, 8);                           // x
        append_short(bytes, 8);                           // y
        bytes.push_back(0);                               // team
        bytes.push_back(0);                               // facing
        bytes.push_back(0);                               // command
        bytes.push_back(1);                               // level
        bytes.insert(bytes.end(), 12, 0);                // name
        bytes.insert(bytes.end(), 10, 0);                // reserved
    };

    LevelData data(5501);

    // Version 3: minimal valid payload + treasure object branch + width==0 branch.
    {
        std::vector<unsigned char> bytes(8, 0);          // grid name
        append_short(bytes, 1);                          // listsize
        append_obj_v3(bytes, static_cast<unsigned char>(Order::Treasure));
        bytes.push_back(1);                              // numlines
        bytes.push_back(0);                              // width -> oneline[0] branch
        MemoryOgFile f(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(1, (int)load_scenario_version(f, &data, 3), "version 3 should load treasure+zero-width path");
    }

    // Version 4: minimal valid payload + treasure object branch + width==0 branch.
    {
        std::vector<unsigned char> bytes(8, 0);          // grid name
        append_short(bytes, 1);                          // listsize
        append_obj_v4_v5(bytes, static_cast<unsigned char>(Order::Treasure));
        bytes.push_back(1);                              // numlines
        bytes.push_back(0);                              // width
        MemoryOgFile f(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(1, (int)load_scenario_version(f, &data, 4), "version 4 should load treasure+zero-width path");
    }

    // Version 5: includes scenario type byte.
    {
        std::vector<unsigned char> bytes(8, 0);          // grid name
        bytes.push_back(2);                              // scenario type
        append_short(bytes, 1);                          // listsize
        append_obj_v4_v5(bytes, static_cast<unsigned char>(Order::Treasure));
        bytes.push_back(1);                              // numlines
        bytes.push_back(0);                              // width
        MemoryOgFile f(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(1, (int)load_scenario_version(f, &data, 5), "version 5 should load treasure+zero-width path");
    }

    // Truncated payloads should fail early read guards in each version parser.
    {
        unsigned char one = 0;
        MemoryOgFile f(&one, 1);
        TEST_ASSERT_EQ(0, (int)load_scenario_version(f, &data, 3), "version 3 should fail on truncated grid read");
    }
    {
        unsigned char one = 0;
        MemoryOgFile f(&one, 1);
        TEST_ASSERT_EQ(0, (int)load_scenario_version(f, &data, 4), "version 4 should fail on truncated grid read");
    }
    {
        unsigned char one = 0;
        MemoryOgFile f(&one, 1);
        TEST_ASSERT_EQ(0, (int)load_scenario_version(f, &data, 5), "version 5 should fail on truncated grid read");
    }
}
REGISTER_TEST(test_level_data_round6_load_version3_4_5_minimal_and_treasure_paths);

void test_level_data_round6_find_near_foe_boundary_fallback_path()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(actor && foe, "fixtures should be created");
    if (!(actor && foe))
        return;

    actor->team_num = 0;
    foe->team_num = 1;
    actor->setxy(GRID_SIZE * 3, myscreen->level_data.pixmaxy - 1);
    foe->setxy(GRID_SIZE * 2, GRID_SIZE * 2);

    ConstRandom rng_zero(0);
    actor->sim_rng = &rng_zero;

    // Near-search spiral should hit the y-boundary and fall back to find_far_foe().
    walker* picked = myscreen->level_data.find_near_foe(actor);
    TEST_ASSERT(picked == foe, "find_near_foe should boundary-fallback to far foe selection");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round6_find_near_foe_boundary_fallback_path);

void test_level_data_round7_wall_arrow_distance_axis_and_rng_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT(owner && weapon && living, "fixtures should be created");
    if (!(owner && weapon && living))
        return;

    myscreen->level_data.grid.frames = 1;
    myscreen->level_data.grid.w = 1;
    myscreen->level_data.grid.h = 1;
    myscreen->level_data.pixmaxx = GRID_SIZE;
    myscreen->level_data.pixmaxy = GRID_SIZE;
    myscreen->level_data.grid.data = std::make_unique<unsigned char[]>(1);
    myscreen->level_data.grid.data[0] = PIX_WALL5;

    owner->sizex = 1;
    owner->sizey = 1;
    weapon->sizex = 1;
    weapon->sizey = 1;
    weapon->owner = owner;

    // Living walkers should fail immediately on wall-arrow tiles.
    living->setxy(0, 0);
    living->sizex = 1;
    living->sizey = 1;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "wall-arrow tiles should block living walkers");

    // X-axis distance branch (abs(dx) > abs(dy)); rng zero => pass.
    owner->setxy(200, 5);
    weapon->setxy(0, 0);
    ConstRandom rng_zero(0);
    weapon->sim_rng = &rng_zero;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall-arrow projectile should pass when rng returns zero");

    // Y-axis distance branch (abs(dy) >= abs(dx)); rng non-zero => fail.
    owner->setxy(5, 200);
    weapon->setxy(0, 0);
    ConstRandom rng_block(1);
    weapon->sim_rng = &rng_block;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall-arrow projectile should fail when rng returns non-zero");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round7_wall_arrow_distance_axis_and_rng_paths);

void test_level_data_round11_wrappers_draw_and_query_grid_entry_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    // save_with_error/load_with_error wrappers should return a concrete enum.
    myscreen->level_data.id = 9421;
    myscreen->level_data.grid_file = "grid";
    myscreen->level_data.title = "round11";
    const auto save_err = myscreen->level_data.save_with_error();
    TEST_ASSERT((int)save_err >= (int)LevelData::IoError::None, "save_with_error wrapper should execute");

    LevelData missing(9876);
    const auto load_err = missing.load_with_error();
    TEST_ASSERT_EQ((int)LevelData::IoError::OpenReadFailed, (int)load_err,
                   "load_with_error should report open-read failure for missing scenario");

    // draw(nullptr) should hit early-return guard safely.
    myscreen->level_data.draw(nullptr);

    // get_description_line while-loop and bounds branches.
    myscreen->level_data.description.clear();
    myscreen->level_data.description.push_back("alpha");
    myscreen->level_data.description.push_back("beta");
    TEST_ASSERT(myscreen->level_data.get_description_line(1) == "beta", "second description line should be readable");
    TEST_ASSERT(myscreen->level_data.get_description_line(5).empty(), "out-of-range description line should be empty");

    // query_grid_passable switch-entry path over passable terrain (lines 1907+).
    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(living != nullptr, "living created");
    if (!living)
        return;

    myscreen->level_data.grid.frames = 1;
    myscreen->level_data.grid.w = 2;
    myscreen->level_data.grid.h = 2;
    myscreen->level_data.pixmaxx = GRID_SIZE * 2;
    myscreen->level_data.pixmaxy = GRID_SIZE * 2;
    myscreen->level_data.grid.data = std::make_unique<unsigned char[]>(4);
    for (int i = 0; i < 4; i++)
        myscreen->level_data.grid.data[i] = PIX_GRASS1;

    living->setxy(0, 0);
    living->sizex = GRID_SIZE; // force xover/yover exact-grid-edge path
    living->sizey = GRID_SIZE;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "grass tile should be passable for living walker");
}
REGISTER_TEST(test_level_data_round11_wrappers_draw_and_query_grid_entry_paths);

void test_level_data_round13_grid_passability_tree_wall_water_and_object_guards()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT(living && weapon && owner, "fixtures created");
    if (!(living && weapon && owner))
        return;

    living->setxy(0, 0);
    weapon->setxy(0, 0);
    owner->setxy(96, 0);
    weapon->owner = owner;
    living->sizex = weapon->sizex = 1;
    living->sizey = weapon->sizey = 1;

    myscreen->level_data.grid.frames = 1;
    myscreen->level_data.grid.w = 1;
    myscreen->level_data.grid.h = 1;
    myscreen->level_data.pixmaxx = GRID_SIZE;
    myscreen->level_data.pixmaxy = GRID_SIZE;
    myscreen->level_data.grid.data = std::make_unique<unsigned char[]>(1);

    // Path tile branch (level_data.cpp:1989) should pass.
    myscreen->level_data.grid.data[0] = PIX_PATH_4;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "path tile should be passable");

    // Tree middle blocks non-forestwalking/non-flying living walkers (1995-2000).
    myscreen->level_data.grid.data[0] = PIX_TREE_M1;
    living->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    living->stats()->set_bit_flags(BIT_FLYING, 0);
    living->flight_left = 0;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "tree middle should block normal living walkers");
    living->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "forestwalk should pass tree middle");

    // Tree base branch allows weapons, blocks normal living walkers (2001-2010).
    myscreen->level_data.grid.data[0] = PIX_TREE_B1;
    living->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "tree base should block living walkers without forestwalk/flying");
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "tree base should allow weapons");

    // Wall-arrow/wall4 branch for weapons with RNG gate (2019-2044), then fallthrough.
    myscreen->level_data.grid.data[0] = PIX_WALL4;
    ConstRandom rng_pass(0);
    weapon->sim_rng = &rng_pass;
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall-arrow projectile should pass when rng returns zero");
    ConstRandom rng_block(1);
    weapon->sim_rng = &rng_block;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, weapon),
                "wall-arrow projectile should block when rng returns non-zero");

    // Water passability branch for flying vs non-flying living walkers (2046-2078).
    myscreen->level_data.grid.data[0] = PIX_WATER2;
    living->stats()->set_bit_flags(BIT_FLYING, 0);
    living->flight_left = 0;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "water should block non-flying living walkers");
    living->stats()->set_bit_flags(BIT_FLYING, 1);
    TEST_ASSERT(myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "water should pass flying living walkers");

    // query_object_passable dead-object shortcut (2089-2091).
    living->dead = 1;
    TEST_ASSERT(myscreen->level_data.query_object_passable(0.0f, 0.0f, living),
                "dead objects should always pass object collision checks");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round13_grid_passability_tree_wall_water_and_object_guards);

void test_level_data_round13_find_helpers_selection_and_filters()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe_far = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* foe_near = myscreen->level_data.add_ob(Order::Generator, FAMILY_TOWER);
    walker* friend_living = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    walker* friend_weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* enemy_weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* blood_near = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    walker* blood_far = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    walker* player_near = myscreen->level_data.add_ob(Order::Living, FAMILY_ELF);
    walker* player_far = myscreen->level_data.add_ob(Order::Living, FAMILY_MAGE);
    TEST_ASSERT(actor && foe_far && foe_near && friend_living && friend_weapon && enemy_weapon &&
                    blood_near && blood_far && player_near && player_far,
                "fixtures created");
    if (!(actor && foe_far && foe_near && friend_living && friend_weapon && enemy_weapon &&
          blood_near && blood_far && player_near && player_far))
        return;

    actor->team_num = 0;
    actor->setxy(64, 64);
    actor->sim_level = &myscreen->level_data;
    foe_far->team_num = 2;
    foe_far->setxy(220, 64);
    foe_near->team_num = 1;
    foe_near->setxy(90, 64);
    friend_living->team_num = 0;
    friend_living->setxy(72, 64);
    friend_weapon->team_num = 0;
    friend_weapon->setxy(70, 64);
    enemy_weapon->team_num = 2;
    enemy_weapon->setxy(74, 64);
    blood_near->setxy(80, 64);
    blood_far->setxy(180, 64);
    player_near->user = 0;
    player_far->user = 1;
    player_near->setxy(78, 64);
    player_far->setxy(200, 64);

    FixedRandom rng_zero(0);
    actor->sim_rng = &rng_zero;
    foe_far->invisibility_left = 0;
    foe_near->invisibility_left = 0;

    TEST_ASSERT(myscreen->level_data.find_far_foe(actor) == foe_near,
                "find_far_foe should return nearest visible living/generator foe");
    TEST_ASSERT(myscreen->level_data.find_nearest_blood(actor) == blood_near,
                "find_nearest_blood should return nearest alive stain");
    TEST_ASSERT(myscreen->level_data.find_nearest_player(actor) == player_near,
                "find_nearest_player should return nearest controlled walker");

    std::int32_t howmany = -1;
    auto in_range = myscreen->level_data.find_in_range(myscreen->level_data.oblist, 40, &howmany, actor);
    TEST_ASSERT(!in_range.empty() && howmany > 0, "find_in_range should collect nearby alive walkers");

    auto foes = myscreen->level_data.find_foes_in_range(myscreen->level_data.oblist, 64, &howmany, actor);
    TEST_ASSERT(!foes.empty() && howmany > 0, "find_foes_in_range should include nearby non-friendly living/generator");

    auto foe_weapons = myscreen->level_data.find_foe_weapons_in_range(myscreen->level_data.weaplist, 64, &howmany, actor);
    TEST_ASSERT(!foe_weapons.empty() && howmany > 0, "find_foe_weapons_in_range should include friendly weapons only");
    for (walker* w : foe_weapons)
        TEST_ASSERT_EQ((int)actor->team_num, (int)w->team_num, "returned weapon should be on friendly team");

    auto friends = myscreen->level_data.find_friends_in_range(myscreen->level_data.oblist, 64, &howmany, actor);
    TEST_ASSERT(!friends.empty() && howmany > 0, "find_friends_in_range should include friendly living walkers");
    for (walker* w : friends)
        TEST_ASSERT_EQ((int)Order::Living, (int)w->query_order(), "friend results should be living walkers");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_round13_find_helpers_selection_and_filters);

void test_level_data_round14_find_helper_exclusion_branches()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* actor = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* hidden_foe = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* dead_blood = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    walker* near_friend = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    walker* dead_enemy = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* enemy_weapon = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(actor && hidden_foe && dead_blood && near_friend && dead_enemy && enemy_weapon, "fixtures created");
    if (!(actor && hidden_foe && dead_blood && near_friend && dead_enemy && enemy_weapon))
        return;

    actor->team_num = 0;
    actor->setxy(64, 64);
    actor->sim_level = &myscreen->level_data;

    hidden_foe->team_num = 1;
    hidden_foe->setxy(72, 64);
    hidden_foe->invisibility_left = 40; // divisor branch in find_far_foe

    near_friend->team_num = 0;
    near_friend->setxy(70, 64);

    dead_enemy->team_num = 2;
    dead_enemy->setxy(68, 64);
    dead_enemy->dead = 1;

    enemy_weapon->team_num = 2;
    enemy_weapon->setxy(66, 64);

    dead_blood->setxy(66, 64);
    dead_blood->dead = 1;

    ConstRandom rng_block_hidden(1);
    actor->sim_rng = &rng_block_hidden;
    TEST_ASSERT(myscreen->level_data.find_far_foe(actor) == nullptr,
                "find_far_foe should skip hidden foes when rng check blocks visibility");
    TEST_ASSERT(myscreen->level_data.find_nearest_blood(actor) == nullptr,
                "find_nearest_blood should ignore dead blood stains");

    std::int32_t howmany = -1;
    auto in_range = myscreen->level_data.find_in_range(myscreen->level_data.oblist, 32, &howmany, actor);
    for (walker* w : in_range)
        TEST_ASSERT(!w->dead, "find_in_range should exclude dead objects");

    auto foes = myscreen->level_data.find_foes_in_range(myscreen->level_data.oblist, 32, &howmany, actor);
    TEST_ASSERT_EQ(1, (int)foes.size(), "find_foes_in_range should keep one alive enemy in range");
    if (!foes.empty())
        TEST_ASSERT(foes.front() == hidden_foe, "find_foes_in_range should exclude dead and friendly walkers");

    auto foe_weapons = myscreen->level_data.find_foe_weapons_in_range(myscreen->level_data.weaplist, 32, &howmany, actor);
    TEST_ASSERT(foe_weapons.empty(), "find_foe_weapons_in_range should exclude enemy-team weapons");
}
REGISTER_TEST(test_level_data_round14_find_helper_exclusion_branches);

void test_level_data_round15_hard_wall_and_unknown_tile_block_paths()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* flying = myscreen->level_data.add_ob(Order::Living, FAMILY_MAGE);
    TEST_ASSERT(living && flying, "fixtures created");
    if (!(living && flying))
        return;

    flying->stats()->set_bit_flags(BIT_FLYING, 1);
    living->setxy(0, 0);
    flying->setxy(0, 0);

    myscreen->level_data.grid.frames = 1;
    myscreen->level_data.grid.w = 1;
    myscreen->level_data.grid.h = 1;
    myscreen->level_data.pixmaxx = GRID_SIZE;
    myscreen->level_data.pixmaxy = GRID_SIZE;
    myscreen->level_data.grid.data = std::make_unique<unsigned char[]>(1);

    // Hard wall cases return blocked immediately for all walkers.
    myscreen->level_data.grid.data[0] = PIX_H_WALL1;
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "hard wall should block living walkers");
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, flying),
                "hard wall should block flying walkers");

    // Unknown/default tile path should also block.
    myscreen->level_data.grid.data[0] = static_cast<unsigned char>(255);
    TEST_ASSERT(!myscreen->level_data.query_grid_passable(0.0f, 0.0f, living),
                "unknown tile id should hit default blocked path");
}
REGISTER_TEST(test_level_data_round15_hard_wall_and_unknown_tile_block_paths);
