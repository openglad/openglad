#include <openglad/data/level_data.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/game_context.h>
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
