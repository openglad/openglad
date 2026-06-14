#include "SDL.h"
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/io.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include <openglad/resources/og_file.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)
short load_scenario_version(og::io::OgFile& infile, LevelRuntimeData* data, short version);

// Memory-backed OgFile for testing (replaces SDL_RWFromConstMem)
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
        if (objects == 0 || buf == nullptr) return 0;
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

namespace
{
static void push_u8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }
static void push_i16(std::vector<uint8_t>& v, int16_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xff));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xff));
}
static void push_bytes(std::vector<uint8_t>& v, const char* s, size_t n)
{
    for (size_t i = 0; i < n; ++i) v.push_back(static_cast<uint8_t>(s[i]));
}

static std::vector<uint8_t> make_scenario_blob_with_one_object(bool include_type_byte, bool include_name)
{
    std::vector<uint8_t> b;
    // 8-byte grid name, read as lowercase then ".pix" is appended.
    push_bytes(b, "16grass1", 8);
    if (include_type_byte)
        push_u8(b, 2); // scenario type for v5+

    // listsize (short) = 1
    push_i16(b, 1);

    // Object payload.
    push_u8(b, static_cast<uint8_t>(Order::Living)); // order
    push_u8(b, static_cast<uint8_t>(FAMILY_SOLDIER)); // family
    push_i16(b, 48); // x
    push_i16(b, 64); // y
    push_u8(b, 1); // team
    push_u8(b, 0); // facing
    push_u8(b, static_cast<uint8_t>(ACT_RANDOM)); // command
    push_u8(b, 4); // level (char in v3/4, later cast to short in loader)
    if (include_name)
        push_bytes(b, "COVNAME\0\0\0\0\0", 12);
    push_bytes(b, "0123456789", 10); // reserved

    // numlines + one description line
    push_u8(b, 1);
    push_u8(b, 6);
    push_bytes(b, "hello!", 6);
    return b;
}

static bool read_file_bytes(const std::string& path, std::vector<uint8_t>* out)
{
    if(out == nullptr)
        return false;

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if(f == nullptr)
        return false;

    if(std::fseek(f, 0, SEEK_END) != 0)
    {
        std::fclose(f);
        return false;
    }
    const long len = std::ftell(f);
    if(len < 0 || std::fseek(f, 0, SEEK_SET) != 0)
    {
        std::fclose(f);
        return false;
    }

    out->resize(static_cast<size_t>(len));
    if(!out->empty())
    {
        const size_t got = std::fread(out->data(), 1, out->size(), f);
        if(got != out->size())
        {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}
} // namespace

namespace
{
struct EditorCampaignFixture
{
    std::string tmp_id;
    std::string old_mounted_campaign;
} g_editor_campaign_fixture;

void setup_editor_campaign_fixture()
{
    g_editor_campaign_fixture.tmp_id =
        std::string("org.openglad.test.editorfixture.") + std::to_string(::getpid());
    g_editor_campaign_fixture.old_mounted_campaign = get_mounted_campaign();
    delete_campaign(g_editor_campaign_fixture.tmp_id);
}

void teardown_editor_campaign_fixture()
{
    delete_campaign(g_editor_campaign_fixture.tmp_id);
    if(!g_editor_campaign_fixture.old_mounted_campaign.empty())
        (void)mount_campaign_package_with_error(g_editor_campaign_fixture.old_mounted_campaign);
}
} // namespace

// ---------------------------------------------------------------------------
// LevelRuntimeData::clear
// ---------------------------------------------------------------------------

TEST(LevelDataOps, level_data_clear)
{
    og::runtime::current_session->myscreen_->world().title = "Modified";
    og::runtime::current_session->myscreen_->world().type = 5;
    og::runtime::current_session->myscreen_->world().par_value = 99;
    og::runtime::current_session->myscreen_->world().time_bonus_limit = 9999;
    og::runtime::current_session->myscreen_->level_visuals_.topx = 50;
    og::runtime::current_session->myscreen_->level_visuals_.topy = 50;

    og::runtime::current_session->myscreen_->level_runtime_data().clear();

    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().title == "New Level") << "title reset";
    ASSERT_EQ(0, (int)og::runtime::current_session->myscreen_->world().type) << "type reset";
    ASSERT_EQ(1, (int)og::runtime::current_session->myscreen_->world().par_value) << "par_value reset";
    ASSERT_EQ(4000, (int)og::runtime::current_session->myscreen_->world().time_bonus_limit) << "time_bonus_limit reset";
    ASSERT_EQ(0, (int)og::runtime::current_session->myscreen_->level_visuals_.topx) << "topx reset";
    ASSERT_EQ(0, (int)og::runtime::current_session->myscreen_->level_visuals_.topy) << "topy reset";
    ASSERT_EQ(0, (int)og::runtime::current_session->myscreen_->world().living_count) << "numobs reset";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().oblist.empty()) << "oblist reset";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().fxlist.empty()) << "fxlist reset";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().weaplist.empty()) << "weaplist reset";

    // Restore grid for other tests
    og::runtime::current_session->myscreen_->world().create_new_grid();
}


// ---------------------------------------------------------------------------
// LevelRuntimeData::create_new_grid
// ---------------------------------------------------------------------------

TEST(LevelDataOps, level_data_create_new_grid)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().grid.valid()) << "grid should be valid";
    ASSERT_EQ(40, (int)og::runtime::current_session->myscreen_->world().grid.w) << "grid width 40";
    ASSERT_EQ(60, (int)og::runtime::current_session->myscreen_->world().grid.h) << "grid height 60";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().pixmaxx > 0) << "pixmaxx positive";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().pixmaxy > 0) << "pixmaxy positive";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().grid.data != nullptr) << "grid data allocated";

    // Grass tile generation should stay in expected range.
    for (int i = 0; i < 25; i++)
    {
        unsigned char t = og::runtime::current_session->myscreen_->world().grid.data[i];
        ASSERT_TRUE(t >= PIX_GRASS1 && t <= PIX_GRASS4) << "new grid tiles should be grass variants";
    }
}


// ---------------------------------------------------------------------------
// LevelRuntimeData::resize_grid
// ---------------------------------------------------------------------------

TEST(LevelDataOps, level_data_resize_grid_grow)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    int old_w = og::runtime::current_session->myscreen_->world().grid.w;
    int old_h = og::runtime::current_session->myscreen_->world().grid.h;
    unsigned char old00 = og::runtime::current_session->myscreen_->world().grid.data[0];

    og::runtime::current_session->myscreen_->world().resize_grid(50, 70);
    ASSERT_EQ(50, (int)og::runtime::current_session->myscreen_->world().grid.w) << "resized width";
    ASSERT_EQ(70, (int)og::runtime::current_session->myscreen_->world().grid.h) << "resized height";
    ASSERT_EQ((int)old00, (int)og::runtime::current_session->myscreen_->world().grid.data[0]) << "existing cells should be preserved";
    (void)old_w;
    (void)old_h;

    // Restore
    og::runtime::current_session->myscreen_->world().resize_grid(40, 60);
}


TEST(LevelDataOps, level_data_resize_grid_shrink)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().resize_grid(20, 30);
    ASSERT_EQ(20, (int)og::runtime::current_session->myscreen_->world().grid.w) << "shrunk width";
    ASSERT_EQ(30, (int)og::runtime::current_session->myscreen_->world().grid.h) << "shrunk height";

    // Restore
    og::runtime::current_session->myscreen_->world().resize_grid(40, 60);
}


TEST(LevelDataOps, level_data_resize_grid_invalid)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    int w_before = og::runtime::current_session->myscreen_->world().grid.w;

    og::runtime::current_session->myscreen_->world().resize_grid(2, 2); // too small
    ASSERT_EQ(w_before, (int)og::runtime::current_session->myscreen_->world().grid.w) << "invalid resize should be no-op";

    og::runtime::current_session->myscreen_->world().resize_grid(256, 256); // too large
    ASSERT_EQ(w_before, (int)og::runtime::current_session->myscreen_->world().grid.w) << "oversized resize should be no-op";
}


// ---------------------------------------------------------------------------
// LevelRuntimeData::add_ob / remove_ob
// ---------------------------------------------------------------------------

TEST(LevelDataOps, level_data_add_ob_living)
{
    int obs_before = og::runtime::current_session->myscreen_->world().living_count;
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "add_ob living should succeed";
    ASSERT_EQ(obs_before + 1, og::runtime::current_session->myscreen_->world().living_count) << "numobs incremented";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
    ASSERT_EQ(obs_before, og::runtime::current_session->myscreen_->world().living_count) << "numobs decremented";
}


TEST(LevelDataOps, level_data_add_ob_weapon)
{
    int obs_before = og::runtime::current_session->myscreen_->world().living_count;
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(w != nullptr) << "add_ob weapon should succeed";
    ASSERT_EQ(obs_before, og::runtime::current_session->myscreen_->world().living_count) << "weapon should not increment numobs";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(LevelDataOps, level_data_add_fx_ob)
{
    int obs_before = og::runtime::current_session->myscreen_->world().living_count;
    walker* w = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(w != nullptr) << "add_fx_ob should succeed";
    ASSERT_EQ(obs_before, og::runtime::current_session->myscreen_->world().living_count) << "fx should not increment numobs";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(LevelDataOps, level_data_add_weap_ob)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(w != nullptr) << "add_weap_ob should succeed";
    short result = og::runtime::current_session->myscreen_->world().remove_ob(w);
    ASSERT_EQ(1, (int)result) << "remove_ob should find weapon";
}


TEST(LevelDataOps, level_data_remove_ob_from_each_list)
{
    // Add to oblist (living)
    walker* living = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(living != nullptr) << "living created";

    // Add to fxlist
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(fx != nullptr) << "fx created";

    // Add to weaplist
    walker* weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weap != nullptr) << "weap created";

    // Remove from each
    short r1 = og::runtime::current_session->myscreen_->world().remove_ob(weap);
    ASSERT_EQ(1, (int)r1) << "removed from weaplist";

    short r2 = og::runtime::current_session->myscreen_->world().remove_ob(fx);
    ASSERT_EQ(1, (int)r2) << "removed from fxlist";

    short r3 = og::runtime::current_session->myscreen_->world().remove_ob(living);
    ASSERT_EQ(1, (int)r3) << "removed from oblist";

    auto non_member = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(non_member != nullptr) << "non-member walker created";
    short r4 = og::runtime::current_session->myscreen_->world().remove_ob(non_member.get());
    ASSERT_EQ(0, (int)r4) << "removing non-member object should fail";
    short r5 = og::runtime::current_session->myscreen_->world().remove_ob(nullptr);
    ASSERT_EQ(0, (int)r5) << "removing null should fail";
}


// ---------------------------------------------------------------------------
// LevelRuntimeData::delete_objects
// ---------------------------------------------------------------------------

TEST(LevelDataOps, level_data_delete_objects)
{
    // Add some objects
    walker* living1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* living2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    walker* weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);

    ASSERT_TRUE(living1 != nullptr) << "living1 created";
    ASSERT_TRUE(living2 != nullptr) << "living2 created";
    ASSERT_TRUE(fx != nullptr) << "fx created";
    ASSERT_TRUE(weap != nullptr) << "weap created";

    // Populate the spatial index so delete_objects() must clean it up.
    living1->setxy(10, 10);
    living2->setxy(30, 30);
    fx->setxy(50, 50);
    weap->setxy(70, 70);

    auto dead = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(dead != nullptr) << "dead_list walker created";
    dead->setxy(90, 90);
    og::runtime::current_session->myscreen_->world().dead_list.push_back(std::move(dead));

    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().myobmap != nullptr) << "myobmap exists";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().myobmap->size() > 0) << "obmap has entries before delete_objects()";

    og::runtime::current_session->myscreen_->world().delete_objects();

    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().oblist.empty()) << "oblist empty";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().fxlist.empty()) << "fxlist empty";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().weaplist.empty()) << "weaplist empty";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().dead_list.empty()) << "dead_list empty";
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->world().living_count) << "numobs 0";
    ASSERT_EQ(0, (int)og::runtime::current_session->myscreen_->world().myobmap->size()) << "obmap has no walkers after delete_objects()";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().myobmap->pos_to_walker.empty()) << "obmap pos_to_walker empty";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().myobmap->walker_to_pos.empty()) << "obmap walker_to_pos empty";
}


// ---------------------------------------------------------------------------
// LevelRuntimeData::set_draw_pos / add_draw_pos
// ---------------------------------------------------------------------------

TEST(LevelDataOps, level_data_set_draw_pos)
{
    og::runtime::current_session->myscreen_->set_level_draw_pos(100, 200);
    ASSERT_EQ(100, (int)og::runtime::current_session->myscreen_->level_visuals_.topx) << "topx set";
    ASSERT_EQ(200, (int)og::runtime::current_session->myscreen_->level_visuals_.topy) << "topy set";

    og::runtime::current_session->myscreen_->set_level_draw_pos(0, 0);
}


TEST(LevelDataOps, level_data_add_draw_pos)
{
    og::runtime::current_session->myscreen_->set_level_draw_pos(100, 200);
    og::runtime::current_session->myscreen_->add_level_draw_pos(10, 20);
    ASSERT_EQ(110, (int)og::runtime::current_session->myscreen_->level_visuals_.topx) << "topx added";
    ASSERT_EQ(220, (int)og::runtime::current_session->myscreen_->level_visuals_.topy) << "topy added";

    og::runtime::current_session->myscreen_->add_level_draw_pos(-5, -10);
    ASSERT_EQ(105, (int)og::runtime::current_session->myscreen_->level_visuals_.topx) << "topx supports negative deltas";
    ASSERT_EQ(210, (int)og::runtime::current_session->myscreen_->level_visuals_.topy) << "topy supports negative deltas";

    og::runtime::current_session->myscreen_->set_level_draw_pos(0, 0);
}


// ---------------------------------------------------------------------------
// LevelRuntimeData::get_description_line
// ---------------------------------------------------------------------------

TEST(LevelDataOps, level_data_get_description_line)
{
    og::runtime::current_session->myscreen_->level_description().clear();
    og::runtime::current_session->myscreen_->level_description().push_back("Line 1");
    og::runtime::current_session->myscreen_->level_description().push_back("Line 2");
    og::runtime::current_session->myscreen_->level_description().push_back("Line 3");

    ASSERT_TRUE(og::runtime::current_session->myscreen_->get_level_description_line(0) == "Line 1") << "line 0";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->get_level_description_line(1) == "Line 2") << "line 1";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->get_level_description_line(2) == "Line 3") << "line 2";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->get_level_description_line(10) == "") << "out of bounds returns empty";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->get_level_description_line(-1) == "Line 1") << "negative index returns first line";

    og::runtime::current_session->myscreen_->level_description().clear();

    // Empty description must not dereference end() for any index, including
    // negative ones (regression: get_description_line(-1) on an empty list
    // previously bypassed the size guard and dereferenced end()).
    ASSERT_TRUE(og::runtime::current_session->myscreen_->get_level_description_line(0) == "") << "empty list line 0";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->get_level_description_line(-1) == "") << "empty list negative index";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->get_level_description_line(5) == "") << "empty list out of range";

    CampaignData c("org.openglad.tests");
    c.description.clear();
    c.description.push_back("Campaign 1");
    c.description.push_back("Campaign 2");
    ASSERT_TRUE(c.getDescriptionLine(0) == "Campaign 1") << "campaign line 0";
    ASSERT_TRUE(c.getDescriptionLine(1) == "Campaign 2") << "campaign line 1";
    ASSERT_TRUE(c.getDescriptionLine(10) == "") << "campaign out of range";

    // CampaignData load/save/save_as roundtrip on a temporary campaign.
    const std::string src_id = "org.openglad.gladiator";
    const std::string tmp_id =
        std::string("org.openglad.test.coverage.") + std::to_string(::getpid());

    delete_campaign(tmp_id);

    CampaignData src(src_id);
    ASSERT_TRUE(src.load()) << "source campaign should load";
    src.title = "Coverage Campaign";
    src.version = "9.9";
    src.authors = "Test Author";
    src.contributors = "Test Contributor";
    src.suggested_power = 42;
    src.first_level = 2;
    src.description.clear();
    src.description.push_back("line a");
    src.description.push_back("line b");
    ASSERT_TRUE(src.save_as(tmp_id)) << "save_as should create target campaign";

    CampaignData loaded(tmp_id);
    ASSERT_TRUE(loaded.load()) << "saved-as campaign should load";
    ASSERT_TRUE(loaded.title == "Coverage Campaign") << "title should persist after save_as";
    ASSERT_TRUE(loaded.getDescriptionLine(0) == "line a") << "description first line should persist";
    ASSERT_TRUE(loaded.getDescriptionLine(1) == "line b") << "description second line should persist";

    loaded.title = "Coverage Campaign Updated";
    loaded.description.clear();
    loaded.description.push_back("line c");
    ASSERT_TRUE(loaded.save()) << "save should update existing campaign";

    CampaignData updated(tmp_id);
    ASSERT_TRUE(updated.load()) << "updated campaign should load";
    ASSERT_TRUE(updated.title == "Coverage Campaign Updated") << "title should persist after save";
    ASSERT_TRUE(updated.getDescriptionLine(0) == "line c") << "updated description should persist";

    // Directly exercise load_scenario_version branches 3/4/5 and unknown version.
    {
        std::vector<uint8_t> blob3 = make_scenario_blob_with_one_object(false, false);
        MemoryOgFile rw3(blob3.data(), blob3.size());
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->level_description().clear();
        short r3 = load_scenario_version(rw3, &og::runtime::current_session->myscreen_->level_runtime_data(), 3);
        ASSERT_EQ(1, (int)r3) << "load_scenario_version v3 should succeed";
        ASSERT_TRUE(!og::runtime::current_session->myscreen_->world().oblist.empty()) << "v3 should load at least one object";
        ASSERT_TRUE(!og::runtime::current_session->myscreen_->level_description().empty()) << "v3 should load description lines";
    }
    {
        std::vector<uint8_t> blob4 = make_scenario_blob_with_one_object(false, true);
        MemoryOgFile rw4(blob4.data(), blob4.size());
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->level_description().clear();
        short r4 = load_scenario_version(rw4, &og::runtime::current_session->myscreen_->level_runtime_data(), 4);
        ASSERT_EQ(1, (int)r4) << "load_scenario_version v4 should succeed";
        ASSERT_TRUE(!og::runtime::current_session->myscreen_->world().oblist.empty()) << "v4 should load at least one object";
    }
    {
        std::vector<uint8_t> blob5 = make_scenario_blob_with_one_object(true, true);
        MemoryOgFile rw5(blob5.data(), blob5.size());
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->level_description().clear();
        short r5 = load_scenario_version(rw5, &og::runtime::current_session->myscreen_->level_runtime_data(), 5);
        ASSERT_EQ(1, (int)r5) << "load_scenario_version v5 should succeed";
        ASSERT_EQ(2, (int)og::runtime::current_session->myscreen_->world().type) << "v5 should load scenario type";
        ASSERT_TRUE(!og::runtime::current_session->myscreen_->world().oblist.empty()) << "v5 should load at least one object";
    }
    {
        // Unknown version should hit default branch and report failure.
        std::vector<uint8_t> tiny = {0};
        MemoryOgFile rw_bad(tiny.data(), tiny.size());
        short bad = load_scenario_version(rw_bad, &og::runtime::current_session->myscreen_->level_runtime_data(), 42);
        ASSERT_EQ(0, (int)bad) << "unknown scenario version should fail";
    }

    // Save with populated ob/fx/weap/description to cover all object-list loops.
    og::runtime::current_session->myscreen_->world().id = 99;
    og::runtime::current_session->myscreen_->level_grid_file() = "grid";
    og::runtime::current_session->myscreen_->world().title = "Coverage Level";
    og::runtime::current_session->myscreen_->world().type = 3;
    og::runtime::current_session->myscreen_->world().par_value = 7;
    og::runtime::current_session->myscreen_->world().time_bonus_limit = 1234;
    og::runtime::current_session->myscreen_->level_description().clear();
    og::runtime::current_session->myscreen_->level_description().push_back("desc-a");
    og::runtime::current_session->myscreen_->level_description().push_back("desc-b");
    og::runtime::current_session->myscreen_->world().delete_objects();
    std::filesystem::create_directories("temp/scen");
    std::filesystem::create_directories("temp/pix");

    walker* ob = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* wp = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(ob && fx && wp) << "save-loop objects should be created";
    if (ob) ob->stats()->name = "OB";
    if (fx) fx->stats()->name = "FX";
    if (wp) wp->stats()->name = "WP";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_level()) << "save should succeed with populated lists";
    og::runtime::current_session->myscreen_->world().delete_objects();

    delete_campaign(tmp_id);
}


TEST(LevelDataOps, level_data_save_description_serialization_bounds)
{
    constexpr int kScenarioId = 950;
    const std::string empty_line;
    const std::string boundary_line(79, 'B');
    const std::string long_line(400, 'L');

    og::runtime::current_session->myscreen_->world().id = kScenarioId;
    og::runtime::current_session->myscreen_->level_grid_file() = "grid";
    og::runtime::current_session->myscreen_->world().title = "Save Desc Regression";
    og::runtime::current_session->myscreen_->world().type = 1;
    og::runtime::current_session->myscreen_->world().par_value = 2;
    og::runtime::current_session->myscreen_->world().time_bonus_limit = 3000;
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->level_description().clear();
    og::runtime::current_session->myscreen_->level_description().push_back(empty_line);
    og::runtime::current_session->myscreen_->level_description().push_back(boundary_line);
    og::runtime::current_session->myscreen_->level_description().push_back(long_line);
    std::filesystem::create_directories("temp/scen");

    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_level()) << "save should succeed for description bounds regression";

    const std::string scen_path = "temp/scen/scen" + std::to_string(kScenarioId) + ".fss";
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(read_file_bytes(scen_path, &bytes)) << "saved scenario should be readable";
    ASSERT_TRUE(bytes.size() >= 49) << "saved scenario should include fixed header";

    size_t pos = 0;
    ASSERT_TRUE(bytes[pos++] == 'F' && bytes[pos++] == 'S' && bytes[pos++] == 'S') << "header should start with FSS";
    pos += 1;  // version
    pos += 8;  // grid name
    pos += 30; // title
    pos += 1;  // type
    pos += 2;  // par
    pos += 2;  // time limit
    ASSERT_TRUE(bytes.size() >= pos + 2) << "saved scenario should include object count";
    const uint16_t object_count = static_cast<uint16_t>(bytes[pos])
        | (static_cast<uint16_t>(bytes[pos + 1]) << 8);
    ASSERT_EQ(0, (int)object_count) << "test fixture should serialize zero objects";
    pos += 2;

    ASSERT_TRUE(bytes.size() > pos) << "saved scenario should include description line count";
    const uint8_t num_lines = bytes[pos++];
    ASSERT_EQ(3, (int)num_lines) << "expected three serialized description lines";

    ASSERT_TRUE(bytes.size() > pos) << "line 1 width should be present";
    const uint8_t width0 = bytes[pos++];
    ASSERT_EQ(0, (int)width0) << "empty description should serialize with width 0";

    ASSERT_TRUE(bytes.size() > pos) << "line 2 width should be present";
    const uint8_t width1 = bytes[pos++];
    ASSERT_EQ(79, (int)width1) << "79-char description should preserve exact width";
    ASSERT_TRUE(bytes.size() >= pos + width1) << "line 2 payload should be present";
    for(size_t i = 0; i < width1; ++i)
        ASSERT_TRUE(bytes[pos + i] == static_cast<uint8_t>('B')) << "line 2 payload should match source text";
    pos += width1;

    ASSERT_TRUE(bytes.size() > pos) << "line 3 width should be present";
    const uint8_t width2 = bytes[pos++];
    ASSERT_EQ(255, (int)width2) << "long description should clamp width to uint8_t max";
    ASSERT_TRUE(bytes.size() >= pos + width2) << "line 3 payload should be present";
    for(size_t i = 0; i < width2; ++i)
        ASSERT_TRUE(bytes[pos + i] == static_cast<uint8_t>('L')) << "line 3 payload should be copied from source text";
}


TEST(LevelDataOps, level_data_load_version4_5_name_field_without_nul_is_bounded)
{
    auto make_blob = [](bool include_type_byte) {
        std::vector<uint8_t> b;
        push_bytes(b, "16grass1", 8); // grid
        if (include_type_byte)
            push_u8(b, 2); // scenario type

        push_i16(b, 1); // listsize

        push_u8(b, static_cast<uint8_t>(Order::Living));
        push_u8(b, static_cast<uint8_t>(FAMILY_SOLDIER));
        push_i16(b, 48);
        push_i16(b, 64);
        push_u8(b, 1); // team
        push_u8(b, 0); // facing
        push_u8(b, static_cast<uint8_t>(ACT_RANDOM)); // command
        push_u8(b, 4); // level
        push_bytes(b, "ABCDEFGHIJKL", 12); // no NUL terminator in fixed 12-byte field
        push_bytes(b, "RRRRRRRRRR", 10);   // no NUL in reserved field either

        push_u8(b, 1); // numlines
        push_u8(b, 6); // width
        push_bytes(b, "hello!", 6);
        return b;
    };

    const std::string expected_name = "ABCDEFGHIJKL";

    {
        std::vector<uint8_t> blob4 = make_blob(false);
        MemoryOgFile rw4(blob4.data(), blob4.size());
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->level_description().clear();
        short r4 = load_scenario_version(rw4, &og::runtime::current_session->myscreen_->level_runtime_data(), 4);
        ASSERT_EQ(1, (int)r4) << "v4 should load with full 12-byte non-NUL name";
        ASSERT_TRUE(!og::runtime::current_session->myscreen_->world().oblist.empty()) << "v4 should create an object";
        ASSERT_TRUE(og::runtime::current_session->myscreen_->world().oblist.front()->stats()->name == expected_name) << "v4 name should be bounded to 12 bytes";
    }

    {
        std::vector<uint8_t> blob5 = make_blob(true);
        MemoryOgFile rw5(blob5.data(), blob5.size());
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->level_description().clear();
        short r5 = load_scenario_version(rw5, &og::runtime::current_session->myscreen_->level_runtime_data(), 5);
        ASSERT_EQ(1, (int)r5) << "v5 should load with full 12-byte non-NUL name";
        ASSERT_TRUE(!og::runtime::current_session->myscreen_->world().oblist.empty()) << "v5 should create an object";
        ASSERT_TRUE(og::runtime::current_session->myscreen_->world().oblist.front()->stats()->name == expected_name) << "v5 name should be bounded to 12 bytes";
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
}


// ---------------------------------------------------------------------------
// LevelRuntimeData::resize_grid with objects - tests off-map cleanup
// ---------------------------------------------------------------------------

TEST(LevelDataOps, level_data_resize_grid_removes_offmap)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    // Add an object far out
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    if (w) {
        w->setxy(500, 500); // way beyond 40*GRID_SIZE
        size_t before = og::runtime::current_session->myscreen_->world().oblist.size();
        og::runtime::current_session->myscreen_->world().resize_grid(10, 10);
        // Object at (500,500) should be removed from 10*GRID_SIZE grid
        ASSERT_TRUE(og::runtime::current_session->myscreen_->world().oblist.size() < before) << "off-map objects removed";
    }
    // Restore
    og::runtime::current_session->myscreen_->world().resize_grid(40, 60);
}


class LevelDataOpsFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        setup_editor_campaign_fixture();
    }

    void TearDown() override
    {
        teardown_editor_campaign_fixture();
    }
};

TEST_F(LevelDataOpsFixture, campaign_editor_save_load_and_remount_with_fixture)
{
    CampaignData src("org.openglad.gladiator");
    ASSERT_EQ(static_cast<int>(CampaignData::IoError::None), static_cast<int>(src.load_with_error())) << "source campaign load_with_error should succeed";

    src.title = "Editor Fixture Campaign";
    src.description.clear();
    src.description.push_back("editor fixture line");
    ASSERT_EQ(static_cast<int>(CampaignData::IoError::None), static_cast<int>(src.save_as_with_error(g_editor_campaign_fixture.tmp_id))) << "save_as_with_error should create fixture campaign";

    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None), static_cast<int>(mount_campaign_package_with_error(g_editor_campaign_fixture.tmp_id))) << "fixture campaign mount should succeed";
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None), static_cast<int>(remount_campaign_package_with_error())) << "fixture campaign remount should succeed";

    CampaignData loaded(g_editor_campaign_fixture.tmp_id);
    ASSERT_EQ(static_cast<int>(CampaignData::IoError::None), static_cast<int>(loaded.load_with_error())) << "fixture campaign load_with_error should succeed";
    ASSERT_TRUE(loaded.title == "Editor Fixture Campaign") << "fixture campaign title should persist";
    ASSERT_TRUE(loaded.getDescriptionLine(0) == "editor fixture line") << "fixture campaign description should persist";
}


TEST(LevelDataOps, level_data_find_foe_helpers_return_null_without_valid_targets)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr) << "actor should be created";
    if (!actor)
        return;
    actor->set_team_num(0);
    actor->setxy(64, 64);

    // Friendly and dead enemies should be ignored by foe selection helpers.
    walker* friendly = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    walker* dead_enemy = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(friendly && dead_enemy) << "fixtures should be created";
    if (!(friendly && dead_enemy))
        return;
    friendly->set_team_num(0);
    friendly->setxy(96, 64);
    dead_enemy->set_team_num(1);
    dead_enemy->set_dead(1);
    dead_enemy->setxy(128, 64);

    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().find_far_foe(actor) == nullptr) << "find_far_foe should return null when no valid foes exist";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().find_near_foe(actor) == nullptr) << "find_near_foe should return null when no valid foes exist";

    og::runtime::current_session->myscreen_->world().delete_objects();
}
