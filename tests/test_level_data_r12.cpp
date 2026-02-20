#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/io/og_file.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include "unit/unit.h"

short load_scenario_version(og::io::OgFile& infile, LevelData* data, short version);
bool save_grid_file(const char* gridname, const PixieData& grid);

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

struct LevelR12Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    MaxRandom rng;

    LevelR12Fixture()
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
    fx.level.wire_entity(w.get());
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

OG_UNIT_TEST(test_level_data_r12_load_title_dispatch_and_save_grid_paths)
{
    std::filesystem::create_directories("scen");
    std::filesystem::create_directories("temp/pix");

    const int id_parse = 9411;
    const int id_bad = 9412;
    const int id_ver = 9413;
    const int id_title = 9414;

    OG_ASSERT(write_bytes(std::filesystem::path("scen") / "scen9411.fss", {'F', 'S', 'S'}));
    OG_ASSERT(write_bytes(std::filesystem::path("scen") / "scen9412.fss", {'B', 'A', 'D', 6}));
    OG_ASSERT(write_bytes(std::filesystem::path("scen") / "scen9413.fss", {'F', 'S', 'S', 1}));

    std::vector<unsigned char> titled = {'F', 'S', 'S', 6};
    const char grid[8] = {'g','r','i','d',0,0,0,0};
    titled.insert(titled.end(), grid, grid + 8);
    const char title[30] = "Coverage R12";
    titled.insert(titled.end(), title, title + 30);
    OG_ASSERT(write_bytes(std::filesystem::path("scen") / "scen9414.fss", titled));

    LevelData parse_fail(id_parse, true);
    OG_ASSERT(parse_fail.load_with_error() == LevelData::IoError::ParseFailed);
    LevelData bad_header(id_bad, true);
    OG_ASSERT(bad_header.load_with_error() == LevelData::IoError::InvalidHeader);
    LevelData unsupported(id_ver, true);
    OG_ASSERT(unsupported.load_with_error() == LevelData::IoError::UnsupportedVersion);

    OG_ASSERT(get_scenario_title(nullptr) == "none");
    OG_ASSERT(get_scenario_title("does_not_exist") == "none");
    OG_ASSERT(get_scenario_title("scen9412") == "none");
    OG_ASSERT(get_scenario_title("scen9414") == "Coverage R12");

    unsigned char dummy = 0;
    MemoryOgFile mem(&dummy, 0);
    LevelData data(1, true);
    OG_ASSERT(load_scenario_version(mem, nullptr, 6) == 0);
    OG_ASSERT(load_scenario_version(mem, &data, 42) == 0);

    PixieData pix(1, 1, 1, new unsigned char[1]{7});
    OG_ASSERT(save_grid_file("coverage_r12_ok", pix));
    OG_ASSERT(!save_grid_file("nested/coverage_r12_fail", pix));
}

OG_UNIT_TEST(test_level_data_r12_save_null_entries_and_query_passable_branches)
{
    LevelR12Fixture fx;
    std::filesystem::create_directories("temp/scen");
    std::filesystem::create_directories("temp/pix");

    fx.level.id = 9450;
    fx.level.grid_file = "grid";
    fx.level.title = "r12";

    fx.level.fxlist.push_back(std::unique_ptr<walker>{});
    OG_ASSERT(!fx.level.save());
    fx.level.delete_objects();

    fx.level.weaplist.push_back(std::unique_ptr<walker>{});
    OG_ASSERT(!fx.level.save());
    fx.level.delete_objects();

    walker* living = add_to(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 0, 0);
    walker* owner = add_to(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* weapon = add_to(fx, fx.level.weaplist, Order::Weapon, FAMILY_ARROW, 0, 0, 0);
    OG_ASSERT(living && owner && weapon);
    living->sizex = 1;
    living->sizey = 1;
    weapon->sizex = 1;
    weapon->sizey = 1;
    weapon->owner = owner;

    fx.level.grid.frames = 1;
    fx.level.grid.w = 1;
    fx.level.grid.h = 1;
    fx.level.pixmaxx = GRID_SIZE;
    fx.level.pixmaxy = GRID_SIZE;
    fx.level.grid.data = std::make_unique<unsigned char[]>(1);

    fx.level.grid.data[0] = PIX_TREE_M1;
    OG_ASSERT(!fx.level.query_grid_passable(0, 0, living));
    living->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    OG_ASSERT(fx.level.query_grid_passable(0, 0, living));
    living->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    living->stats()->set_bit_flags(BIT_FLYING, 1);
    OG_ASSERT(fx.level.query_grid_passable(0, 0, living));

    living->stats()->set_bit_flags(BIT_FLYING, 0);
    fx.level.grid.data[0] = PIX_TREE_B1;
    OG_ASSERT(!fx.level.query_grid_passable(0, 0, living));
    OG_ASSERT(fx.level.query_grid_passable(0, 0, weapon));

    fx.level.grid.data[0] = PIX_WALL4;
    OG_ASSERT(!fx.level.query_grid_passable(0, 0, living));
    weapon->setxy(200, 0);
    owner->setxy(0, 0);
    OG_ASSERT(!fx.level.query_grid_passable(0, 0, weapon));

    fx.level.grid.data[0] = PIX_WATER1;
    OG_ASSERT(fx.level.query_grid_passable(0, 0, weapon));
    OG_ASSERT(!fx.level.query_grid_passable(0, 0, living));
    living->flight_left = 1;
    OG_ASSERT(fx.level.query_grid_passable(0, 0, living));

    living->flight_left = 0;
    fx.level.grid.data[0] = 255;
    OG_ASSERT(!fx.level.query_grid_passable(0, 0, living));

    living->dead = 1;
    OG_ASSERT(fx.level.query_object_passable(0, 0, living));
}

OG_UNIT_TEST(test_level_data_r12_find_helpers_null_and_ranges)
{
    LevelR12Fixture fx;

    walker* self = add_to(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_to(fx, fx.level.oblist, Order::Living, FAMILY_ORC, 1, 80, 64);
    walker* ally = add_to(fx, fx.level.oblist, Order::Living, FAMILY_ARCHER, 0, 96, 64);
    walker* enemy_weapon = add_to(fx, fx.level.weaplist, Order::Weapon, FAMILY_ARROW, 0, 70, 64);
    walker* blood = add_to(fx, fx.level.fxlist, Order::Treasure, FAMILY_STAIN, 0, 68, 64);
    OG_ASSERT(self && foe && ally && enemy_weapon && blood);

    ally->user = 0;

    std::int32_t howmany = 0;
    OG_ASSERT(fx.level.find_in_range(fx.level.oblist, 50, &howmany, nullptr).empty());
    OG_ASSERT(fx.level.find_foes_in_range(fx.level.oblist, 50, &howmany, nullptr).empty());
    OG_ASSERT(fx.level.find_foe_weapons_in_range(fx.level.weaplist, 50, &howmany, nullptr).empty());
    OG_ASSERT(fx.level.find_friends_in_range(fx.level.oblist, 50, &howmany, nullptr).empty());

    OG_ASSERT(fx.level.find_nearest_blood(nullptr) == nullptr);
    OG_ASSERT(fx.level.find_nearest_player(nullptr) == nullptr);

    OG_ASSERT(fx.level.find_near_foe(self) != nullptr);
    OG_ASSERT(fx.level.find_far_foe(self) != nullptr);
    OG_ASSERT(fx.level.find_nearest_blood(self) == blood);
    OG_ASSERT(fx.level.find_nearest_player(self) == ally);

    auto inr = fx.level.find_in_range(fx.level.oblist, 200, &howmany, self);
    OG_ASSERT(!inr.empty() && howmany >= 1);
    auto foes = fx.level.find_foes_in_range(fx.level.oblist, 200, &howmany, self);
    OG_ASSERT(!foes.empty());
    auto foe_weapons = fx.level.find_foe_weapons_in_range(fx.level.weaplist, 200, &howmany, self);
    OG_ASSERT(!foe_weapons.empty());
    auto friends = fx.level.find_friends_in_range(fx.level.oblist, 200, &howmany, self);
    OG_ASSERT(!friends.empty());
}
