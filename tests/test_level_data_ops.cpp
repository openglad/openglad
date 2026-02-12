#include "graph.h"
#include "entities/guy.h"
#include "data/gloader.h"
#include "platform/io.h"
#include "test_framework.h"

#include <cstdint>
#include <filesystem>
#include <unistd.h>
#include <vector>

extern screen* myscreen;
short load_scenario_version(SDL_RWops* infile, LevelData* data, short version);

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
        (void)mount_campaign_package(g_editor_campaign_fixture.old_mounted_campaign);
}
} // namespace

// ---------------------------------------------------------------------------
// LevelData::clear
// ---------------------------------------------------------------------------

void test_level_data_clear()
{
    myscreen->level_data.title = "Modified";
    myscreen->level_data.type = 5;
    myscreen->level_data.par_value = 99;
    myscreen->level_data.time_bonus_limit = 9999;
    myscreen->level_data.topx = 50;
    myscreen->level_data.topy = 50;

    myscreen->level_data.clear();

    TEST_ASSERT(myscreen->level_data.title == "New Level", "title reset");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.type, "type reset");
    TEST_ASSERT_EQ(1, (int)myscreen->level_data.par_value, "par_value reset");
    TEST_ASSERT_EQ(4000, (int)myscreen->level_data.time_bonus_limit, "time_bonus_limit reset");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.topx, "topx reset");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.topy, "topy reset");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.numobs, "numobs reset");
    TEST_ASSERT(myscreen->level_data.oblist.empty(), "oblist reset");
    TEST_ASSERT(myscreen->level_data.fxlist.empty(), "fxlist reset");
    TEST_ASSERT(myscreen->level_data.weaplist.empty(), "weaplist reset");

    // Restore grid for other tests
    myscreen->level_data.create_new_grid();
}
REGISTER_TEST(test_level_data_clear);

// ---------------------------------------------------------------------------
// LevelData::create_new_grid
// ---------------------------------------------------------------------------

void test_level_data_create_new_grid()
{
    myscreen->level_data.create_new_grid();
    TEST_ASSERT(myscreen->level_data.grid.valid(), "grid should be valid");
    TEST_ASSERT_EQ(40, (int)myscreen->level_data.grid.w, "grid width 40");
    TEST_ASSERT_EQ(60, (int)myscreen->level_data.grid.h, "grid height 60");
    TEST_ASSERT(myscreen->level_data.pixmaxx > 0, "pixmaxx positive");
    TEST_ASSERT(myscreen->level_data.pixmaxy > 0, "pixmaxy positive");
    TEST_ASSERT(myscreen->level_data.grid.data != nullptr, "grid data allocated");

    // Grass tile generation should stay in expected range.
    for (int i = 0; i < 25; i++)
    {
        unsigned char t = myscreen->level_data.grid.data[i];
        TEST_ASSERT(t >= PIX_GRASS1 && t <= PIX_GRASS4, "new grid tiles should be grass variants");
    }
}
REGISTER_TEST(test_level_data_create_new_grid);

// ---------------------------------------------------------------------------
// LevelData::resize_grid
// ---------------------------------------------------------------------------

void test_level_data_resize_grid_grow()
{
    myscreen->level_data.create_new_grid();
    int old_w = myscreen->level_data.grid.w;
    int old_h = myscreen->level_data.grid.h;
    unsigned char old00 = myscreen->level_data.grid.data[0];

    myscreen->level_data.resize_grid(50, 70);
    TEST_ASSERT_EQ(50, (int)myscreen->level_data.grid.w, "resized width");
    TEST_ASSERT_EQ(70, (int)myscreen->level_data.grid.h, "resized height");
    TEST_ASSERT_EQ((int)old00, (int)myscreen->level_data.grid.data[0], "existing cells should be preserved");
    (void)old_w;
    (void)old_h;

    // Restore
    myscreen->level_data.resize_grid(40, 60);
}
REGISTER_TEST(test_level_data_resize_grid_grow);

void test_level_data_resize_grid_shrink()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.resize_grid(20, 30);
    TEST_ASSERT_EQ(20, (int)myscreen->level_data.grid.w, "shrunk width");
    TEST_ASSERT_EQ(30, (int)myscreen->level_data.grid.h, "shrunk height");

    // Restore
    myscreen->level_data.resize_grid(40, 60);
}
REGISTER_TEST(test_level_data_resize_grid_shrink);

void test_level_data_resize_grid_invalid()
{
    myscreen->level_data.create_new_grid();
    int w_before = myscreen->level_data.grid.w;

    myscreen->level_data.resize_grid(2, 2); // too small
    TEST_ASSERT_EQ(w_before, (int)myscreen->level_data.grid.w, "invalid resize should be no-op");

    myscreen->level_data.resize_grid(256, 256); // too large
    TEST_ASSERT_EQ(w_before, (int)myscreen->level_data.grid.w, "oversized resize should be no-op");
}
REGISTER_TEST(test_level_data_resize_grid_invalid);

// ---------------------------------------------------------------------------
// LevelData::add_ob / remove_ob
// ---------------------------------------------------------------------------

void test_level_data_add_ob_living()
{
    int obs_before = myscreen->level_data.numobs;
    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "add_ob living should succeed");
    TEST_ASSERT_EQ(obs_before + 1, myscreen->level_data.numobs, "numobs incremented");
    myscreen->level_data.remove_ob(w);
    TEST_ASSERT_EQ(obs_before, myscreen->level_data.numobs, "numobs decremented");
}
REGISTER_TEST(test_level_data_add_ob_living);

void test_level_data_add_ob_weapon()
{
    int obs_before = myscreen->level_data.numobs;
    walker* w = myscreen->level_data.add_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(w != nullptr, "add_ob weapon should succeed");
    TEST_ASSERT_EQ(obs_before, myscreen->level_data.numobs, "weapon should not increment numobs");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_level_data_add_ob_weapon);

void test_level_data_add_fx_ob()
{
    int obs_before = myscreen->level_data.numobs;
    walker* w = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    TEST_ASSERT(w != nullptr, "add_fx_ob should succeed");
    TEST_ASSERT_EQ(obs_before, myscreen->level_data.numobs, "fx should not increment numobs");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_level_data_add_fx_ob);

void test_level_data_add_weap_ob()
{
    walker* w = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(w != nullptr, "add_weap_ob should succeed");
    short result = myscreen->level_data.remove_ob(w);
    TEST_ASSERT_EQ(1, (int)result, "remove_ob should find weapon");
}
REGISTER_TEST(test_level_data_add_weap_ob);

void test_level_data_remove_ob_from_each_list()
{
    // Add to oblist (living)
    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(living != nullptr, "living created");

    // Add to fxlist
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    TEST_ASSERT(fx != nullptr, "fx created");

    // Add to weaplist
    walker* weap = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(weap != nullptr, "weap created");

    // Remove from each
    short r1 = myscreen->level_data.remove_ob(weap);
    TEST_ASSERT_EQ(1, (int)r1, "removed from weaplist");

    short r2 = myscreen->level_data.remove_ob(fx);
    TEST_ASSERT_EQ(1, (int)r2, "removed from fxlist");

    short r3 = myscreen->level_data.remove_ob(living);
    TEST_ASSERT_EQ(1, (int)r3, "removed from oblist");

    walker* non_member = myscreen->level_data.myloader->create_walker(Order::Living, FAMILY_SOLDIER, myscreen);
    TEST_ASSERT(non_member != nullptr, "non-member walker created");
    short r4 = myscreen->level_data.remove_ob(non_member);
    TEST_ASSERT_EQ(0, (int)r4, "removing non-member object should fail");
    delete non_member;
    short r5 = myscreen->level_data.remove_ob(nullptr);
    TEST_ASSERT_EQ(0, (int)r5, "removing null should fail");
}
REGISTER_TEST(test_level_data_remove_ob_from_each_list);

// ---------------------------------------------------------------------------
// LevelData::delete_objects
// ---------------------------------------------------------------------------

void test_level_data_delete_objects()
{
    // Add some objects
    walker* living1 = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* living2 = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    walker* weap = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);

    TEST_ASSERT(living1 != nullptr, "living1 created");
    TEST_ASSERT(living2 != nullptr, "living2 created");
    TEST_ASSERT(fx != nullptr, "fx created");
    TEST_ASSERT(weap != nullptr, "weap created");

    // Populate the spatial index so delete_objects() must clean it up.
    living1->setxy(10, 10);
    living2->setxy(30, 30);
    fx->setxy(50, 50);
    weap->setxy(70, 70);

    auto dead = std::unique_ptr<walker>(myscreen->level_data.myloader->create_walker(Order::Living, FAMILY_ORC, myscreen));
    TEST_ASSERT(dead != nullptr, "dead_list walker created");
    dead->myobmap = myscreen->level_data.myobmap.get();
    dead->setxy(90, 90);
    myscreen->level_data.dead_list.push_back(std::move(dead));

    TEST_ASSERT(myscreen->level_data.myobmap != nullptr, "myobmap exists");
    TEST_ASSERT(myscreen->level_data.myobmap->size() > 0, "obmap has entries before delete_objects()");

    myscreen->level_data.delete_objects();

    TEST_ASSERT(myscreen->level_data.oblist.empty(), "oblist empty");
    TEST_ASSERT(myscreen->level_data.fxlist.empty(), "fxlist empty");
    TEST_ASSERT(myscreen->level_data.weaplist.empty(), "weaplist empty");
    TEST_ASSERT(myscreen->level_data.dead_list.empty(), "dead_list empty");
    TEST_ASSERT_EQ(0, myscreen->level_data.numobs, "numobs 0");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.myobmap->size(), "obmap has no walkers after delete_objects()");
    TEST_ASSERT(myscreen->level_data.myobmap->pos_to_walker.empty(), "obmap pos_to_walker empty");
    TEST_ASSERT(myscreen->level_data.myobmap->walker_to_pos.empty(), "obmap walker_to_pos empty");
}
REGISTER_TEST(test_level_data_delete_objects);

// ---------------------------------------------------------------------------
// LevelData::set_draw_pos / add_draw_pos
// ---------------------------------------------------------------------------

void test_level_data_set_draw_pos()
{
    myscreen->level_data.set_draw_pos(100, 200);
    TEST_ASSERT_EQ(100, (int)myscreen->level_data.topx, "topx set");
    TEST_ASSERT_EQ(200, (int)myscreen->level_data.topy, "topy set");

    myscreen->level_data.set_draw_pos(0, 0);
}
REGISTER_TEST(test_level_data_set_draw_pos);

void test_level_data_add_draw_pos()
{
    myscreen->level_data.set_draw_pos(100, 200);
    myscreen->level_data.add_draw_pos(10, 20);
    TEST_ASSERT_EQ(110, (int)myscreen->level_data.topx, "topx added");
    TEST_ASSERT_EQ(220, (int)myscreen->level_data.topy, "topy added");

    myscreen->level_data.add_draw_pos(-5, -10);
    TEST_ASSERT_EQ(105, (int)myscreen->level_data.topx, "topx supports negative deltas");
    TEST_ASSERT_EQ(210, (int)myscreen->level_data.topy, "topy supports negative deltas");

    myscreen->level_data.set_draw_pos(0, 0);
}
REGISTER_TEST(test_level_data_add_draw_pos);

// ---------------------------------------------------------------------------
// LevelData::get_description_line
// ---------------------------------------------------------------------------

void test_level_data_get_description_line()
{
    myscreen->level_data.description.clear();
    myscreen->level_data.description.push_back("Line 1");
    myscreen->level_data.description.push_back("Line 2");
    myscreen->level_data.description.push_back("Line 3");

    TEST_ASSERT(myscreen->level_data.get_description_line(0) == "Line 1", "line 0");
    TEST_ASSERT(myscreen->level_data.get_description_line(1) == "Line 2", "line 1");
    TEST_ASSERT(myscreen->level_data.get_description_line(2) == "Line 3", "line 2");
    TEST_ASSERT(myscreen->level_data.get_description_line(10) == "", "out of bounds returns empty");
    TEST_ASSERT(myscreen->level_data.get_description_line(-1) == "Line 1", "negative index returns first line");

    myscreen->level_data.description.clear();

    CampaignData c("org.openglad.tests");
    c.description.clear();
    c.description.push_back("Campaign 1");
    c.description.push_back("Campaign 2");
    TEST_ASSERT(c.getDescriptionLine(0) == "Campaign 1", "campaign line 0");
    TEST_ASSERT(c.getDescriptionLine(1) == "Campaign 2", "campaign line 1");
    TEST_ASSERT(c.getDescriptionLine(10) == "", "campaign out of range");

    // CampaignData load/save/save_as roundtrip on a temporary campaign.
    const std::string src_id = "org.openglad.gladiator";
    const std::string tmp_id =
        std::string("org.openglad.test.coverage.") + std::to_string(::getpid());

    delete_campaign(tmp_id);

    CampaignData src(src_id);
    TEST_ASSERT(src.load(), "source campaign should load");
    src.title = "Coverage Campaign";
    src.version = "9.9";
    src.authors = "Test Author";
    src.contributors = "Test Contributor";
    src.suggested_power = 42;
    src.first_level = 2;
    src.description.clear();
    src.description.push_back("line a");
    src.description.push_back("line b");
    TEST_ASSERT(src.save_as(tmp_id), "save_as should create target campaign");

    CampaignData loaded(tmp_id);
    TEST_ASSERT(loaded.load(), "saved-as campaign should load");
    TEST_ASSERT(loaded.title == "Coverage Campaign", "title should persist after save_as");
    TEST_ASSERT(loaded.getDescriptionLine(0) == "line a", "description first line should persist");
    TEST_ASSERT(loaded.getDescriptionLine(1) == "line b", "description second line should persist");

    loaded.title = "Coverage Campaign Updated";
    loaded.description.clear();
    loaded.description.push_back("line c");
    TEST_ASSERT(loaded.save(), "save should update existing campaign");

    CampaignData updated(tmp_id);
    TEST_ASSERT(updated.load(), "updated campaign should load");
    TEST_ASSERT(updated.title == "Coverage Campaign Updated", "title should persist after save");
    TEST_ASSERT(updated.getDescriptionLine(0) == "line c", "updated description should persist");

    // Directly exercise load_scenario_version branches 3/4/5 and unknown version.
    {
        std::vector<uint8_t> blob3 = make_scenario_blob_with_one_object(false, false);
        SDL_RWops* rw3 = SDL_RWFromConstMem(blob3.data(), static_cast<int>(blob3.size()));
        TEST_ASSERT(rw3 != nullptr, "SDL_RWFromConstMem for version 3 should succeed");
        myscreen->level_data.delete_objects();
        myscreen->level_data.description.clear();
        short r3 = load_scenario_version(rw3, &myscreen->level_data, 3);
        SDL_RWclose(rw3);
        TEST_ASSERT_EQ(1, (int)r3, "load_scenario_version v3 should succeed");
        TEST_ASSERT(!myscreen->level_data.oblist.empty(), "v3 should load at least one object");
        TEST_ASSERT(!myscreen->level_data.description.empty(), "v3 should load description lines");
    }
    {
        std::vector<uint8_t> blob4 = make_scenario_blob_with_one_object(false, true);
        SDL_RWops* rw4 = SDL_RWFromConstMem(blob4.data(), static_cast<int>(blob4.size()));
        TEST_ASSERT(rw4 != nullptr, "SDL_RWFromConstMem for version 4 should succeed");
        myscreen->level_data.delete_objects();
        myscreen->level_data.description.clear();
        short r4 = load_scenario_version(rw4, &myscreen->level_data, 4);
        SDL_RWclose(rw4);
        TEST_ASSERT_EQ(1, (int)r4, "load_scenario_version v4 should succeed");
        TEST_ASSERT(!myscreen->level_data.oblist.empty(), "v4 should load at least one object");
    }
    {
        std::vector<uint8_t> blob5 = make_scenario_blob_with_one_object(true, true);
        SDL_RWops* rw5 = SDL_RWFromConstMem(blob5.data(), static_cast<int>(blob5.size()));
        TEST_ASSERT(rw5 != nullptr, "SDL_RWFromConstMem for version 5 should succeed");
        myscreen->level_data.delete_objects();
        myscreen->level_data.description.clear();
        short r5 = load_scenario_version(rw5, &myscreen->level_data, 5);
        SDL_RWclose(rw5);
        TEST_ASSERT_EQ(1, (int)r5, "load_scenario_version v5 should succeed");
        TEST_ASSERT_EQ(2, (int)myscreen->level_data.type, "v5 should load scenario type");
        TEST_ASSERT(!myscreen->level_data.oblist.empty(), "v5 should load at least one object");
    }
    {
        // Unknown version should hit default branch and report failure.
        std::vector<uint8_t> tiny = {0};
        SDL_RWops* rw_bad = SDL_RWFromConstMem(tiny.data(), static_cast<int>(tiny.size()));
        TEST_ASSERT(rw_bad != nullptr, "SDL_RWFromConstMem for unknown version should succeed");
        short bad = load_scenario_version(rw_bad, &myscreen->level_data, 42);
        SDL_RWclose(rw_bad);
        TEST_ASSERT_EQ(0, (int)bad, "unknown scenario version should fail");
    }

    // Save with populated ob/fx/weap/description to cover all object-list loops.
    myscreen->level_data.id = 99;
    myscreen->level_data.grid_file = "grid";
    myscreen->level_data.title = "Coverage Level";
    myscreen->level_data.type = 3;
    myscreen->level_data.par_value = 7;
    myscreen->level_data.time_bonus_limit = 1234;
    myscreen->level_data.description.clear();
    myscreen->level_data.description.push_back("desc-a");
    myscreen->level_data.description.push_back("desc-b");
    myscreen->level_data.delete_objects();
    std::filesystem::create_directories("temp/scen");

    walker* ob = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* wp = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(ob && fx && wp, "save-loop objects should be created");
    if (ob) ob->stats()->name = "OB";
    if (fx) fx->stats()->name = "FX";
    if (wp) wp->stats()->name = "WP";
    TEST_ASSERT(myscreen->level_data.save(), "save should succeed with populated lists");
    myscreen->level_data.delete_objects();

    delete_campaign(tmp_id);
}
REGISTER_TEST(test_level_data_get_description_line);

// ---------------------------------------------------------------------------
// LevelData::resize_grid with objects - tests off-map cleanup
// ---------------------------------------------------------------------------

void test_level_data_resize_grid_removes_offmap()
{
    myscreen->level_data.create_new_grid();
    // Add an object far out
    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    if (w) {
        w->setxy(500, 500); // way beyond 40*GRID_SIZE
        size_t before = myscreen->level_data.oblist.size();
        myscreen->level_data.resize_grid(10, 10);
        // Object at (500,500) should be removed from 10*GRID_SIZE grid
        TEST_ASSERT(myscreen->level_data.oblist.size() < before, "off-map objects removed");
    }
    // Restore
    myscreen->level_data.resize_grid(40, 60);
}
REGISTER_TEST(test_level_data_resize_grid_removes_offmap);

void test_campaign_editor_save_load_and_remount_with_fixture()
{
    CampaignData src("org.openglad.gladiator");
    TEST_ASSERT_EQ(static_cast<int>(CampaignData::IoError::None), static_cast<int>(src.load_with_error()),
        "source campaign load_with_error should succeed");

    src.title = "Editor Fixture Campaign";
    src.description.clear();
    src.description.push_back("editor fixture line");
    TEST_ASSERT_EQ(static_cast<int>(CampaignData::IoError::None),
        static_cast<int>(src.save_as_with_error(g_editor_campaign_fixture.tmp_id)),
        "save_as_with_error should create fixture campaign");

    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None),
        static_cast<int>(mount_campaign_package_with_error(g_editor_campaign_fixture.tmp_id)),
        "fixture campaign mount should succeed");
    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None),
        static_cast<int>(remount_campaign_package_with_error()),
        "fixture campaign remount should succeed");

    CampaignData loaded(g_editor_campaign_fixture.tmp_id);
    TEST_ASSERT_EQ(static_cast<int>(CampaignData::IoError::None), static_cast<int>(loaded.load_with_error()),
        "fixture campaign load_with_error should succeed");
    TEST_ASSERT(loaded.title == "Editor Fixture Campaign", "fixture campaign title should persist");
    TEST_ASSERT(loaded.getDescriptionLine(0) == "editor fixture line",
        "fixture campaign description should persist");
}
REGISTER_TEST_WITH_FIXTURE(
    test_campaign_editor_save_load_and_remount_with_fixture,
    setup_editor_campaign_fixture,
    teardown_editor_campaign_fixture);
