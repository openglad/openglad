#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/legacy/base.h>
#include <memory>
#include <string>
#include <gtest/gtest.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <openglad/resources/og_file.h>
#include <openglad/resources/zip_api.h>
#include <openglad/resources/io_common.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/interface/level_render.h>
#include <openglad/core/constants.h>
#include "test_gameplay_context_scope.h"

// --- From test_level_data_coverage_push.cpp ---
namespace detail_level_data_coverage_push {
namespace {

struct LevelFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{1};
    ScopedGameplayContext gameplay;

    LevelFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_to_list(LevelFixture& fx, std::list<std::unique_ptr<walker>>& ls,
                    Order o, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(o, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    ls.push_back(std::move(w));
    if (o == Order::Living)
        fx.level.numobs++;
    return out;
}

} // namespace

TEST(LevelDataUnit, level_data_grid_and_description_paths)
{
    LevelFixture fx;
    fx.level.resize_grid(2, 2); // invalid no-op
    ASSERT_TRUE(fx.level.world().grid.w == 40);
    ASSERT_TRUE(fx.level.world().grid.h == 60);

    fx.level.resize_grid(50, 50);
    ASSERT_TRUE(fx.level.world().grid.w == 50);
    ASSERT_TRUE(fx.level.world().grid.h == 50);

    fx.level.description.clear();
    fx.level.description.push_back("line-1");
    ASSERT_TRUE(fx.level.get_description_line(0) == "line-1");
    ASSERT_TRUE(fx.level.get_description_line(3).empty());

    fx.level.set_draw_pos(3, 4);
    fx.level.add_draw_pos(2, -1);
    ASSERT_TRUE(fx.level.level_visuals().topx == 5);
    ASSERT_TRUE(fx.level.level_visuals().topy == 3);
}

TEST(LevelDataUnit, level_data_passable_and_range_queries)
{
    LevelFixture fx;
    walker* self = add_to_list(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_to_list(fx, fx.level.world().oblist, Order::Living, FAMILY_ORC, 1, 80, 64);
    walker* player = add_to_list(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 96, 64);
    walker* weapon = add_to_list(fx, fx.level.world().weaplist, Order::Weapon, FAMILY_KNIFE, 1, 72, 64);
    walker* stain = add_to_list(fx, fx.level.world().fxlist, Order::Treasure, FAMILY_STAIN, 0, 70, 64);
    ASSERT_TRUE(self && foe && player && weapon && stain);

    player->user = 0;
    self->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    ASSERT_TRUE(fx.level.query_grid_passable(10, 10, self));
    self->stats()->set_bit_flags(BIT_ETHEREAL, 0);
    ASSERT_TRUE(!fx.level.query_grid_passable(-1, 10, self));

    const int tile = 4 + 4 * fx.level.world().grid.w;
    fx.level.world().grid.data[tile] = PIX_H_WALL1;
    self->setxy(4 * GRID_SIZE, 4 * GRID_SIZE);
    ASSERT_TRUE(!fx.level.query_grid_passable(self->xpos, self->ypos, self));

    self->dead = 1;
    ASSERT_TRUE(fx.level.query_object_passable(self->xpos, self->ypos, self));
    self->dead = 0;
    (void)fx.level.query_passable(self->xpos, self->ypos, self);

    ASSERT_TRUE(fx.level.find_near_foe(self) != nullptr);
    ASSERT_TRUE(fx.level.find_far_foe(self) != nullptr);
    ASSERT_TRUE(fx.level.find_nearest_blood(self) == stain);
    ASSERT_TRUE(fx.level.find_nearest_player(self) == player);

    std::int32_t count = 0;
    (void)fx.level.find_in_range(fx.level.world().oblist, 200, &count, self);
    ASSERT_TRUE(count >= 2);
    (void)fx.level.find_foes_in_range(fx.level.world().oblist, 200, &count, self);
    ASSERT_TRUE(count >= 1);
    (void)fx.level.find_foe_weapons_in_range(fx.level.world().weaplist, 200, &count, self);
    (void)fx.level.find_friends_in_range(fx.level.world().oblist, 200, &count, self);
}

TEST(LevelDataUnit, level_data_remove_and_remaining_foes_paths)
{
    LevelFixture fx;
    walker* self = add_to_list(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_to_list(fx, fx.level.world().oblist, Order::Living, FAMILY_ORC, 1, 80, 64);
    ASSERT_TRUE(self && foe);
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));

    const short foes_before = remaining_foes(fx.level, self);
    ASSERT_TRUE(foes_before >= 1);

    ASSERT_TRUE(fx.level.remove_ob(foe) == 1);
    const short foes_after = remaining_foes(fx.level, self);
    ASSERT_TRUE(foes_after <= foes_before);

    fx.level.delete_objects();
    ASSERT_TRUE(fx.level.world().oblist.empty());
    ASSERT_TRUE(fx.level.world().fxlist.empty());
    ASSERT_TRUE(fx.level.world().weaplist.empty());
}
} // namespace detail_level_data_coverage_push

// --- From test_level_data_r11.cpp ---
namespace detail_level_data_r11 {
namespace {

struct LevelR11Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{1};
    ScopedGameplayContext gameplay;

    LevelR11Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_to(LevelR11Fixture& fx, std::list<std::unique_ptr<walker>>& ls,
               Order o, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(o, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    ls.push_back(std::move(w));
    if (o == Order::Living)
        fx.level.numobs++;
    return out;
}

} // namespace

TEST(LevelDataUnit, level_data_r11_basic_construction_remove_and_helpers)
{
    LevelR11Fixture fx;

    // get_description_line out of range and in range
    fx.level.description.clear();
    fx.level.description.push_back("a");
    ASSERT_TRUE(fx.level.get_description_line(0) == "a");
    ASSERT_TRUE(fx.level.get_description_line(9).empty());

    // remove_ob miss path
    walker dummy;
    ASSERT_TRUE(fx.level.remove_ob(&dummy) == 0);

    // null guards for searches
    std::int32_t hm = 0;
    auto none1 = fx.level.find_in_range(fx.level.world().oblist, 10, &hm, nullptr);
    auto none2 = fx.level.find_foes_in_range(fx.level.world().oblist, 10, &hm, nullptr);
    auto none3 = fx.level.find_foe_weapons_in_range(fx.level.world().weaplist, 10, &hm, nullptr);
    auto none4 = fx.level.find_friends_in_range(fx.level.world().oblist, 10, &hm, nullptr);
    ASSERT_TRUE(none1.empty() && none2.empty() && none3.empty() && none4.empty());

    ASSERT_TRUE(fx.level.find_near_foe(nullptr) == nullptr);
    ASSERT_TRUE(fx.level.find_far_foe(nullptr) == nullptr);
    ASSERT_TRUE(fx.level.find_nearest_blood(nullptr) == nullptr);
    ASSERT_TRUE(fx.level.find_nearest_player(nullptr) == nullptr);
}

TEST(LevelDataUnit, level_data_r11_query_grid_passable_terrain_branches)
{
    LevelR11Fixture fx;
    walker* ob = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    ASSERT_TRUE(ob != nullptr);

    // grid invalid branch
    fx.level.delete_grid();
    ASSERT_TRUE(!fx.level.query_grid_passable(10, 10, ob));
    fx.level.create_new_grid();

    // out-of-bounds branch
    ASSERT_TRUE(!fx.level.query_grid_passable(-1, 0, ob));

    // ethereal branch
    ob->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    ASSERT_TRUE(fx.level.query_grid_passable(64, 64, ob));
    ob->stats()->set_bit_flags(BIT_ETHEREAL, 0);

    const int gx = 4;
    const int gy = 4;
    ob->setxy(static_cast<short>(gx * GRID_SIZE), static_cast<short>(gy * GRID_SIZE));

    // tree branch blocked/unblocked by forestwalk/flying
    fx.level.world().grid.data[gx + gy * fx.level.world().grid.w] = PIX_TREE_M1;
    ASSERT_TRUE(!fx.level.query_grid_passable(ob->xpos, ob->ypos, ob));
    ob->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    ASSERT_TRUE(fx.level.query_grid_passable(ob->xpos, ob->ypos, ob));
    ob->stats()->set_bit_flags(BIT_FORESTWALK, 0);

    // tree_b branch with weapon
    walker* weap = add_to(fx, fx.level.world().weaplist, Order::Weapon, FAMILY_ARROW, 1, ob->xpos, ob->ypos);
    fx.level.world().grid.data[gx + gy * fx.level.world().grid.w] = PIX_TREE_B1;
    ASSERT_TRUE(fx.level.query_grid_passable(weap->xpos, weap->ypos, weap));

    // hard wall path
    fx.level.world().grid.data[gx + gy * fx.level.world().grid.w] = PIX_H_WALL1;
    ASSERT_TRUE(!fx.level.query_grid_passable(ob->xpos, ob->ypos, ob));

    // arrow wall + weapon owner branch
    fx.level.world().grid.data[gx + gy * fx.level.world().grid.w] = PIX_WALL4;
    weap->owner = ob;
    ASSERT_TRUE(!fx.level.query_grid_passable(weap->xpos, weap->ypos, weap) || fx.level.query_grid_passable(weap->xpos, weap->ypos, weap));
}

TEST(LevelDataUnit, level_data_r11_object_passability_and_search_sets)
{
    LevelR11Fixture fx;
    walker* self = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_ORC, 1, 80, 64);
    walker* ally = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 96, 64);
    walker* weapon = add_to(fx, fx.level.world().weaplist, Order::Weapon, FAMILY_ARROW, 1, 70, 64);
    walker* stain = add_to(fx, fx.level.world().fxlist, Order::Treasure, FAMILY_STAIN, 0, 68, 64);
    ASSERT_TRUE(self && foe && ally && weapon && stain);

    ally->user = 0;
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    fx.level.world().allied_mode = 0;

    // query_object_passable dead-ob fast path
    self->dead = 1;
    ASSERT_TRUE(fx.level.query_object_passable(self->xpos, self->ypos, self));
    self->dead = 0;

    (void)fx.level.query_passable(self->xpos, self->ypos, self);

    ASSERT_TRUE(fx.level.find_near_foe(self) != nullptr);
    ASSERT_TRUE(fx.level.find_far_foe(self) != nullptr);
    ASSERT_TRUE(fx.level.find_nearest_blood(self) == stain);
    ASSERT_TRUE(fx.level.find_nearest_player(self) == ally);

    std::int32_t c = 0;
    auto inr = fx.level.find_in_range(fx.level.world().oblist, 200, &c, self);
    ASSERT_TRUE(c >= 1 && !inr.empty());
    auto foes = fx.level.find_foes_in_range(fx.level.world().oblist, 200, &c, self);
    ASSERT_TRUE(c >= 1 && !foes.empty());
    (void)fx.level.find_foe_weapons_in_range(fx.level.world().weaplist, 200, &c, self);
    auto friends = fx.level.find_friends_in_range(fx.level.world().oblist, 200, &c, self);
    ASSERT_TRUE(!friends.empty());

    // helper functions
    (void)get_scenario_title("nonexistent_file");
    ASSERT_TRUE(remaining_foes(fx.level, self) >= 0);
}
} // namespace detail_level_data_r11

// --- From test_level_data_r12.cpp ---
short load_scenario_version(og::io::OgFile& infile, LevelRuntimeData* data, short version);
bool save_grid_file(const char* gridname, const PixieData& grid);

namespace detail_level_data_r12 {
namespace {

class MemoryOgFile final : public og::io::OgFile {
public:
    MemoryOgFile(const void* data, std::size_t size)
        : data_(static_cast<const unsigned char*>(data)), size_(size), pos_(0) {}

    std::size_t read(void* buf, std::size_t size, std::size_t count) override
    {
        if (size == 0 || count == 0)
            return 0;
        std::size_t total = size * count;
        std::size_t avail = (pos_ < size_) ? size_ - pos_ : 0;
        if (total > avail)
            total = avail;
        const std::size_t objects = total / size;
        if (objects > 0)
            std::memcpy(buf, data_ + pos_, objects * size);
        pos_ += objects * size;
        return objects;
    }

    std::size_t write(const void*, std::size_t, std::size_t) override { return 0; }

    std::int64_t seek(std::int64_t offset, int whence) override
    {
        std::int64_t newpos = 0;
        switch (whence)
        {
            case 0: newpos = offset; break;
            case 1: newpos = static_cast<std::int64_t>(pos_) + offset; break;
            case 2: newpos = static_cast<std::int64_t>(size_) + offset; break;
            default: return -1;
        }
        if (newpos < 0)
            return -1;
        pos_ = static_cast<std::size_t>(newpos);
        return static_cast<std::int64_t>(pos_);
    }

    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }

private:
    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_;
};

struct MaxRandom : IRandom {
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive <= 1)
            return 0;
        return max_exclusive - 1;
    }
};

bool write_bytes(const std::filesystem::path& p, const std::vector<unsigned char>& bytes)
{
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    FILE* f = fopen(p.string().c_str(), "wb");
    if (!f)
        return false;
    const size_t n = fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return n == bytes.size();
}

struct ScopedFileRemover {
    std::vector<std::filesystem::path> files;

    ~ScopedFileRemover()
    {
        std::error_code ec;
        for (const auto& file : files)
            std::filesystem::remove(file, ec);
    }
};

struct LevelR12Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    MaxRandom rng;
    ScopedGameplayContext gameplay;

    LevelR12Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_to(LevelR12Fixture& fx, std::list<std::unique_ptr<walker>>& ls,
               Order o, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(o, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    ls.push_back(std::move(w));
    if (o == Order::Living)
        fx.level.numobs++;
    return out;
}

} // namespace

TEST(LevelDataUnit, level_data_r12_load_title_dispatch_and_save_grid_paths)
{
    std::filesystem::create_directories("scen");
    std::filesystem::create_directories("temp/pix");

    const int id_parse = 9411;
    const int id_bad = 9412;
    const int id_ver = 9413;
    const int id_title = 9414;

    const auto parse_path = std::filesystem::path("scen") / ("scen" + std::to_string(id_parse) + ".fss");
    const auto bad_path = std::filesystem::path("scen") / ("scen" + std::to_string(id_bad) + ".fss");
    const auto version_path = std::filesystem::path("scen") / ("scen" + std::to_string(id_ver) + ".fss");
    const auto title_path = std::filesystem::path("scen") / ("scen" + std::to_string(id_title) + ".fss");
    ScopedFileRemover cleanup;
    cleanup.files = {parse_path, bad_path, version_path, title_path};

    ASSERT_TRUE(write_bytes(parse_path, {'F', 'S', 'S'}));
    ASSERT_TRUE(write_bytes(bad_path, {'B', 'A', 'D', 6}));
    ASSERT_TRUE(write_bytes(version_path, {'F', 'S', 'S', 1}));

    std::vector<unsigned char> titled = {'F', 'S', 'S', 6};
    const char grid[8] = {'g','r','i','d',0,0,0,0};
    titled.insert(titled.end(), grid, grid + 8);
    const char title[30] = "Coverage R12";
    titled.insert(titled.end(), title, title + 30);
    ASSERT_TRUE(write_bytes(title_path, titled));

    LevelRuntimeData parse_fail(id_parse, true);
    ASSERT_TRUE(parse_fail.load_with_error() == LevelRuntimeData::IoError::ParseFailed);
    LevelRuntimeData bad_header(id_bad, true);
    ASSERT_TRUE(bad_header.load_with_error() == LevelRuntimeData::IoError::InvalidHeader);
    LevelRuntimeData unsupported(id_ver, true);
    ASSERT_TRUE(unsupported.load_with_error() == LevelRuntimeData::IoError::UnsupportedVersion);

    ASSERT_TRUE(get_scenario_title(nullptr) == "none");
    ASSERT_TRUE(get_scenario_title("does_not_exist") == "none");
    ASSERT_TRUE(get_scenario_title("scen9412") == "none");
    ASSERT_TRUE(get_scenario_title("scen9414") == "Coverage R12");

    unsigned char dummy = 0;
    MemoryOgFile mem(&dummy, 0);
    LevelRuntimeData data(1, true);
    ASSERT_TRUE(load_scenario_version(mem, nullptr, 6) == 0);
    ASSERT_TRUE(load_scenario_version(mem, &data, 42) == 0);

    PixieData pix(1, 1, 1, new unsigned char[1]{7});
    ASSERT_TRUE(save_grid_file("coverage_r12_ok", pix));
    ASSERT_TRUE(!save_grid_file("nested/coverage_r12_fail", pix));
}

TEST(LevelDataUnit, level_data_r12_save_null_entries_and_query_passable_branches)
{
    LevelR12Fixture fx;
    std::filesystem::create_directories("temp/scen");
    std::filesystem::create_directories("temp/pix");

    fx.level.world().id = 9450;
    fx.level.grid_file = "grid";
    fx.level.world().title = "r12";

    fx.level.world().fxlist.push_back(std::unique_ptr<walker>{});
    ASSERT_TRUE(!fx.level.save());
    fx.level.delete_objects();

    fx.level.world().weaplist.push_back(std::unique_ptr<walker>{});
    ASSERT_TRUE(!fx.level.save());
    fx.level.delete_objects();

    walker* living = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 0, 0);
    walker* owner = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* weapon = add_to(fx, fx.level.world().weaplist, Order::Weapon, FAMILY_ARROW, 0, 0, 0);
    ASSERT_TRUE(living && owner && weapon);
    living->sizex = 1;
    living->sizey = 1;
    weapon->sizex = 1;
    weapon->sizey = 1;
    weapon->owner = owner;

    fx.level.world().grid.frames = 1;
    fx.level.world().grid.w = 1;
    fx.level.world().grid.h = 1;
    fx.level.world().pixmaxx = GRID_SIZE;
    fx.level.world().pixmaxy = GRID_SIZE;
    fx.level.world().grid.data = std::make_unique<unsigned char[]>(1);

    fx.level.world().grid.data[0] = PIX_TREE_M1;
    ASSERT_TRUE(!fx.level.query_grid_passable(0, 0, living));
    living->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    ASSERT_TRUE(fx.level.query_grid_passable(0, 0, living));
    living->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    living->stats()->set_bit_flags(BIT_FLYING, 1);
    ASSERT_TRUE(fx.level.query_grid_passable(0, 0, living));

    living->stats()->set_bit_flags(BIT_FLYING, 0);
    fx.level.world().grid.data[0] = PIX_TREE_B1;
    ASSERT_TRUE(!fx.level.query_grid_passable(0, 0, living));
    ASSERT_TRUE(fx.level.query_grid_passable(0, 0, weapon));

    fx.level.world().grid.data[0] = PIX_WALL4;
    ASSERT_TRUE(!fx.level.query_grid_passable(0, 0, living));
    weapon->setxy(200, 0);
    owner->setxy(0, 0);
    fx.level.world().rng_.state_ = 1;
    ASSERT_TRUE(!fx.level.query_grid_passable(0, 0, weapon));

    fx.level.world().grid.data[0] = PIX_WATER1;
    ASSERT_TRUE(fx.level.query_grid_passable(0, 0, weapon));
    ASSERT_TRUE(!fx.level.query_grid_passable(0, 0, living));
    living->flight_left = 1;
    ASSERT_TRUE(fx.level.query_grid_passable(0, 0, living));

    living->flight_left = 0;
    fx.level.world().grid.data[0] = 255;
    ASSERT_TRUE(!fx.level.query_grid_passable(0, 0, living));

    living->dead = 1;
    ASSERT_TRUE(fx.level.query_object_passable(0, 0, living));
}

TEST(LevelDataUnit, level_data_r12_find_helpers_null_and_ranges)
{
    LevelR12Fixture fx;

    walker* self = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_ORC, 1, 80, 64);
    walker* ally = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_ARCHER, 0, 96, 64);
    walker* enemy_weapon = add_to(fx, fx.level.world().weaplist, Order::Weapon, FAMILY_ARROW, 0, 70, 64);
    walker* blood = add_to(fx, fx.level.world().fxlist, Order::Treasure, FAMILY_STAIN, 0, 68, 64);
    ASSERT_TRUE(self && foe && ally && enemy_weapon && blood);

    ally->user = 0;

    std::int32_t howmany = 0;
    ASSERT_TRUE(fx.level.find_in_range(fx.level.world().oblist, 50, &howmany, nullptr).empty());
    ASSERT_TRUE(fx.level.find_foes_in_range(fx.level.world().oblist, 50, &howmany, nullptr).empty());
    ASSERT_TRUE(fx.level.find_foe_weapons_in_range(fx.level.world().weaplist, 50, &howmany, nullptr).empty());
    ASSERT_TRUE(fx.level.find_friends_in_range(fx.level.world().oblist, 50, &howmany, nullptr).empty());

    ASSERT_TRUE(fx.level.find_nearest_blood(nullptr) == nullptr);
    ASSERT_TRUE(fx.level.find_nearest_player(nullptr) == nullptr);

    ASSERT_TRUE(fx.level.find_near_foe(self) != nullptr);
    ASSERT_TRUE(fx.level.find_far_foe(self) != nullptr);
    ASSERT_TRUE(fx.level.find_nearest_blood(self) == blood);
    ASSERT_TRUE(fx.level.find_nearest_player(self) == ally);

    auto inr = fx.level.find_in_range(fx.level.world().oblist, 200, &howmany, self);
    ASSERT_TRUE(!inr.empty() && howmany >= 1);
    auto foes = fx.level.find_foes_in_range(fx.level.world().oblist, 200, &howmany, self);
    ASSERT_TRUE(!foes.empty());
    auto foe_weapons = fx.level.find_foe_weapons_in_range(fx.level.world().weaplist, 200, &howmany, self);
    ASSERT_TRUE(!foe_weapons.empty());
    auto friends = fx.level.find_friends_in_range(fx.level.world().oblist, 200, &howmany, self);
    ASSERT_TRUE(!friends.empty());
}

TEST(LevelDataUnit, level_data_r12_remove_ob_paths_and_zip_api_paths)
{
    LevelR12Fixture fx;
    walker* living = add_to(fx, fx.level.world().oblist, Order::Living, FAMILY_SOLDIER, 0, 40, 40);
    walker* weap = add_to(fx, fx.level.world().weaplist, Order::Weapon, FAMILY_ARROW, 0, 44, 40);
    walker* fxob = add_to(fx, fx.level.world().fxlist, Order::FX, FAMILY_EXPLOSION, 0, 48, 40);
    ASSERT_TRUE(living && weap && fxob);

    const std::int32_t before = fx.level.numobs;
    ASSERT_TRUE(fx.level.remove_ob(weap) == 1);
    ASSERT_TRUE(fx.level.remove_ob(fxob) == 1);
    ASSERT_TRUE(fx.level.remove_ob(living) == 1);
    ASSERT_TRUE(fx.level.numobs == before - 1);
    ASSERT_TRUE(fx.level.remove_ob(nullptr) == 0);

    ASSERT_TRUE(og::io::unzip_into_with_error("temp/r12_zip/not_there.zip", "temp/r12_zip/out2") == ArchiveIoError::OpenArchiveFailed);
    (void)og::io::zip_contents_with_error("temp/r12_zip/in", "temp/r12_zip/missing_parent/archive.zip");
}
} // namespace detail_level_data_r12

// --- From test_level_data_r14.cpp ---
namespace detail_level_data_r14 {
namespace {

class MemoryOgFile final : public og::io::OgFile {
public:
    MemoryOgFile(const std::vector<unsigned char>& data)
        : data_(data), pos_(0) {}

    std::size_t read(void* buf, std::size_t size, std::size_t count) override
    {
        if (size == 0 || count == 0)
            return 0;
        std::size_t total = size * count;
        std::size_t avail = (pos_ < data_.size()) ? data_.size() - pos_ : 0;
        if (total > avail)
            total = avail;
        const std::size_t objects = total / size;
        if (objects > 0)
            std::memcpy(buf, data_.data() + pos_, objects * size);
        pos_ += objects * size;
        return objects;
    }

    std::size_t write(const void*, std::size_t, std::size_t) override { return 0; }

    std::int64_t seek(std::int64_t offset, int whence) override
    {
        std::int64_t newpos = 0;
        switch (whence)
        {
            case 0: newpos = offset; break;
            case 1: newpos = static_cast<std::int64_t>(pos_) + offset; break;
            case 2: newpos = static_cast<std::int64_t>(data_.size()) + offset; break;
            default: return -1;
        }
        if (newpos < 0)
            return -1;
        pos_ = static_cast<std::size_t>(newpos);
        return static_cast<std::int64_t>(pos_);
    }

    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }

private:
    const std::vector<unsigned char>& data_;
    std::size_t pos_;
};

void append_bytes(std::vector<unsigned char>& out, const void* p, std::size_t n)
{
    const auto* b = static_cast<const unsigned char*>(p);
    out.insert(out.end(), b, b + n);
}

template <typename T>
void append_pod(std::vector<unsigned char>& out, const T& v)
{
    append_bytes(out, &v, sizeof(T));
}

void append_fixed_string(std::vector<unsigned char>& out, const std::string& s, std::size_t n)
{
    std::string t = s;
    t.resize(n, '\0');
    append_bytes(out, t.data(), n);
}

std::vector<unsigned char> make_payload_v2(const std::string& grid8)
{
    std::vector<unsigned char> v;
    append_fixed_string(v, grid8, 8);
    short listsize = 0;
    append_pod(v, listsize);
    return v;
}

std::vector<unsigned char> make_payload_v3(const std::string& grid8, unsigned char line_width)
{
    std::vector<unsigned char> v;
    append_fixed_string(v, grid8, 8);
    short listsize = 0;
    append_pod(v, listsize);
    char numlines = 1;
    append_pod(v, numlines);
    char width = static_cast<char>(line_width);
    append_pod(v, width);
    for (unsigned int i = 0; i < line_width; ++i)
        v.push_back(static_cast<unsigned char>('A' + (i % 26)));
    return v;
}

std::vector<unsigned char> make_payload_v5(const std::string& grid8, char scen_type)
{
    std::vector<unsigned char> v;
    append_fixed_string(v, grid8, 8);
    append_pod(v, scen_type);
    short listsize = 0;
    append_pod(v, listsize);
    char numlines = 0;
    append_pod(v, numlines);
    return v;
}

std::vector<unsigned char> make_payload_v9(const std::string& grid8, const std::string& title,
                                           char scen_type, short par, short time_limit,
                                           unsigned char line_width)
{
    std::vector<unsigned char> v;
    append_fixed_string(v, grid8, 8);
    append_fixed_string(v, title, 30);
    append_pod(v, scen_type);
    append_pod(v, par);
    append_pod(v, time_limit);
    short listsize = 0;
    append_pod(v, listsize);
    char numlines = 1;
    append_pod(v, numlines);
    char width = static_cast<char>(line_width);
    append_pod(v, width);
    for (unsigned int i = 0; i < line_width; ++i)
        v.push_back(static_cast<unsigned char>('a' + (i % 26)));
    return v;
}

} // namespace

TEST(LevelDataUnit, level_data_r14_lines_705_770_786_912_1069_1206_load_versions_success_matrix)
{
    // v2, grid without .pix extension path.
    {
        std::vector<unsigned char> bytes = make_payload_v2("grid");
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(1, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 2) == 1);
        ASSERT_TRUE(data.grid_file == "grid");
    }

    // v3, grid with .pix extension path + long line width skip/discard path.
    {
        std::vector<unsigned char> bytes = make_payload_v3("ab.pix", 95);
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(2, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 3) == 1);
        ASSERT_TRUE(data.grid_file == "ab.pix");
        ASSERT_TRUE(!data.description.empty());
    }

    // v5 scenario type path.
    {
        std::vector<unsigned char> bytes = make_payload_v5("gr5", static_cast<char>(7));
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(3, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 5) == 1);
        ASSERT_TRUE(data.world().type == 7);
    }

    // v9 title/par/time-limit + long line width skip path.
    {
        std::vector<unsigned char> bytes = make_payload_v9("gr9", "R14 Title", static_cast<char>(5),
                                                           static_cast<short>(77), static_cast<short>(1234),
                                                           110);
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(4, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 9) == 1);
        ASSERT_TRUE(data.world().title == "R14 Title");
        ASSERT_TRUE(data.world().par_value == 77);
        ASSERT_TRUE(data.world().time_bonus_limit == 1234);
        ASSERT_TRUE(data.world().type == 5);
    }
}

TEST(LevelDataUnit, level_data_r14_lines_725_733_735_746_873_1018_1112_1315_loader_failures_and_bounds)
{
    // Truncated v2 after grid field.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(11, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 2) == 0);
    }

    // Invalid object count v2.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        short bad_listsize = static_cast<short>(-1);
        append_pod(bytes, bad_listsize);
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(12, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 2) == 0);
    }

    // Truncated v3 while reading text lines.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        short listsize = 0;
        append_pod(bytes, listsize);
        char numlines = 1;
        append_pod(bytes, numlines);
        char width = 10;
        append_pod(bytes, width);
        bytes.push_back('x');
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(13, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 3) == 0);
    }

    // Truncated v5 before scenario type read.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(14, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 5) == 0);
    }

    // Invalid object count v8.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        append_fixed_string(bytes, "Bad Count", 30);
        char scen_type = 0;
        append_pod(bytes, scen_type);
        short par = 1;
        append_pod(bytes, par);
        short bad_listsize = 5000;
        append_pod(bytes, bad_listsize);
        MemoryOgFile mem(bytes);
        LevelRuntimeData data(15, true);
        ASSERT_TRUE(load_scenario_version(mem, &data, 8) == 0);
    }
}

TEST(LevelDataUnit, level_data_r14_lines_95_99_353_371_378_campaign_description_accessors)
{
    CampaignData c("r14_campaign");
    ASSERT_TRUE(c.getDescriptionLine(0) == "No description.");
    ASSERT_TRUE(c.get_description_line(0) == "No description.");
    ASSERT_TRUE(c.get_description_line(5).empty());

    // Out-of-range from load/save wrappers should remain deterministic without I/O setup.
    ASSERT_TRUE(c.load_with_error() == c.last_io_error());
    ASSERT_TRUE(c.save_with_error() == c.last_io_error());
}
} // namespace detail_level_data_r14

// --- From test_level_data_r15.cpp ---
namespace detail_level_data_r15 {
namespace {

int g_render_calls = 0;

std::unique_ptr<LevelRender> make_render(PixieData[])
{
    g_render_calls++;
    return std::make_unique<LevelRender>();
}

} // namespace

TEST(LevelDataUnit, level_data_r15_campaign_wrappers_and_description_iteration)
{
    CampaignData c("missing_campaign_r15");
    c.description.clear();
    c.description.push_back("line0");
    c.description.push_back("line1");
    c.description.push_back("line2");

    ASSERT_TRUE(c.get_description_line(2) == "line2");
    ASSERT_TRUE(c.getDescriptionLine(1) == "line1");
    ASSERT_TRUE(c.get_description_line(-1).empty());
    ASSERT_TRUE(c.get_description_line(9).empty());

    (void)c.load_with_error();
    (void)c.save_with_error();
    (void)c.save_as_with_error("missing_campaign_r15_copy");
}

TEST(LevelDataUnit, level_data_r15_ctor_hooks_add_paths_and_clear)
{
    g_render_calls = 0;

    LevelDataHooks hooks;
    hooks.create_level_render = make_render;

    LevelRuntimeData level_non_headless(9415, &hooks);
    LevelRuntimeData level_headless(9416, true, &hooks);
    ASSERT_TRUE(g_render_calls >= 1);

    SaveData save;
    std::int32_t freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    level_non_headless.set_sim_context(&save, &freeze, &events, &rng, &cfg);

    walker* living = level_non_headless.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fxob = level_non_headless.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    walker* weap = level_non_headless.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(living && fxob && weap);
    ASSERT_TRUE(level_non_headless.numobs >= 1);

    level_non_headless.world().title = "Mutated";
    level_non_headless.world().type = 7;
    level_non_headless.world().par_value = 9;
    level_non_headless.world().time_bonus_limit = 123;
    level_non_headless.level_visuals().topx = 44;
    level_non_headless.level_visuals().topy = 55;
    level_non_headless.clear();

    ASSERT_TRUE(level_non_headless.world().title == "New Level");
    ASSERT_TRUE(level_non_headless.world().type == 0);
    ASSERT_TRUE(level_non_headless.world().par_value == 1);
    ASSERT_TRUE(level_non_headless.world().time_bonus_limit == 4000);
    ASSERT_TRUE(level_non_headless.level_visuals().topx == 0);
    ASSERT_TRUE(level_non_headless.level_visuals().topy == 0);

    // Exercise delegating constructor overloads.
    LevelRuntimeData plain_ctor(9417);
    LevelRuntimeData hooks_ctor(9418, &hooks);
    ASSERT_TRUE(plain_ctor.world().id == 9417);
    ASSERT_TRUE(hooks_ctor.world().id == 9418);

    (void)level_headless;
}
} // namespace detail_level_data_r15

// --- From test_level_data_r16.cpp ---
namespace detail_level_data_r16 {
TEST(LevelDataUnit, level_data_r16_external_world_teardown_detaches_level)
{
    LevelRuntimeData level(9510, true);
    SaveData save;
    std::int32_t freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    level.create_new_grid();
    level.set_sim_context(&save, &freeze, &events, &rng, &cfg);

    {
        GameWorld external_world;
        level.attach_world(&external_world);
        ASSERT_TRUE(&level.world() == &external_world);
        ASSERT_TRUE(static_cast<bool>(external_world.entity_factory));

        walker* spawned = level.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(spawned != nullptr);
        ASSERT_TRUE(level.numobs == 1);
        ASSERT_TRUE(level.world().oblist.size() == 1);
    }

    ASSERT_TRUE(static_cast<bool>(level.world().entity_factory));
    ASSERT_TRUE(level.numobs == 1);
    ASSERT_TRUE(level.world().oblist.size() == 1);

    walker* spawned2 = level.add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_TRUE(spawned2 != nullptr);
    ASSERT_TRUE(level.numobs == 2);
    ASSERT_TRUE(level.world().oblist.size() == 2);
}
} // namespace detail_level_data_r16
