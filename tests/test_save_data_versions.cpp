#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <openglad/legacy/base.h> // Order + family constants used for test data
#include <openglad/platform/io.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include "test_framework.h"

static void rw_write(SDL_RWops* out, const void* data, size_t len)
{
    SDL_RWwrite(out, data, 1, len);
}

template <typename T>
static void rw_write_val(SDL_RWops* out, const T& v)
{
    SDL_RWwrite(out, &v, sizeof(T), 1);
}

static void rw_write_padded(SDL_RWops* out, const std::string& s, size_t len)
{
    std::string tmp = s;
    tmp.resize(len, '\0');
    rw_write(out, tmp.data(), len);
}

struct GuyRecord
{
    unsigned char order = static_cast<unsigned char>(Order::Living);
    char family = FAMILY_SOLDIER;
    std::string name = "TESTGUY";
    short str = 10, dex = 11, con = 12, intel = 13, armor = 14, level = 3;
    uint32_t exp = 1234;
    short kills = 2;
    int32_t level_kills = 7;
    int32_t total_damage = 50;
    int32_t total_hits = 6;
    int32_t total_shots = 9;
    short teamnum = 1;
};

static void write_guy(SDL_RWops* out, const GuyRecord& g)
{
    rw_write_val(out, g.order);
    rw_write_val(out, g.family);
    char namebuf[12];
    std::memset(namebuf, 0, sizeof(namebuf));
    std::snprintf(namebuf, sizeof(namebuf), "%s", g.name.c_str());
    rw_write(out, namebuf, sizeof(namebuf));

    rw_write_val(out, g.str);
    rw_write_val(out, g.dex);
    rw_write_val(out, g.con);
    rw_write_val(out, g.intel);
    rw_write_val(out, g.armor);
    rw_write_val(out, g.level);
    rw_write_val(out, g.exp);

    // The loader reads these fields unconditionally, then conditionally uses them.
    rw_write_val(out, g.kills);
    rw_write_val(out, g.level_kills);
    rw_write_val(out, g.total_damage);
    rw_write_val(out, g.total_hits);
    rw_write_val(out, g.total_shots);
    rw_write_val(out, g.teamnum);

    char filler8[8] = {0};
    rw_write(out, filler8, sizeof(filler8));
}

static void write_save_file(const std::string& filename_no_ext,
                            char version,
                            const std::string& campaign_id,
                            short scen_num,
                            uint32_t cash,
                            uint32_t score,
                            short allied_mode,
                            unsigned char numplayers,
                            const GuyRecord* guys,
                            short listsize,
                            bool use_v8plus_campaigns,
                            bool v5plus_levelstatus,
                            const std::array<uint8_t, 500>* levelstatus_500,
                            const std::array<uint8_t, 200>* levelstatus_200)
{
    std::string fname = filename_no_ext + ".gtl";
    SDL_RWops* out = open_write_file("save/", fname.c_str());
    TEST_ASSERT(out != nullptr, "open_write_file for save");

    // Header + version
    rw_write(out, "GTL", 3);
    rw_write_val(out, version);

    // Version 7+ registered mark (unused by loader except to read)
    if (version >= 7) {
        short registered = 0;
        rw_write_val(out, registered);
    }

    // Version 2+ save name (40 bytes)
    if (version >= 2) {
        rw_write_padded(out, "TESTSAVE", 40);
    }

    // Version 8+ current campaign id stored in header (40 bytes)
    if (version >= 8) {
        rw_write_padded(out, campaign_id, 40);
    }

    // Scenario, cash, score
    rw_write_val(out, scen_num);
    rw_write_val(out, cash);
    rw_write_val(out, score);

    // Version 6+ per-team cash/score
    if (version >= 6) {
        for (int i = 0; i < 4; i++) {
            uint32_t c = cash + (uint32_t)i;
            uint32_t s = score + (uint32_t)(i * 10);
            rw_write_val(out, c);
            rw_write_val(out, s);
        }
    }

    // Version 7+ allied
    if (version >= 7) {
        rw_write_val(out, allied_mode);
    }

    // Listsize, numplayers, reserved
    rw_write_val(out, listsize);
    rw_write_val(out, numplayers);
    char filler31[31] = {0};
    rw_write(out, filler31, sizeof(filler31));

    for (int i = 0; i < listsize; i++) {
        write_guy(out, guys[i]);
    }

    if (!use_v8plus_campaigns) {
        // Version < 8: raw levelstatus list.
        if (v5plus_levelstatus) {
            std::array<uint8_t, 500> zeros{};
            const auto& data = levelstatus_500 ? *levelstatus_500 : zeros;
            rw_write(out, data.data(), data.size());
        } else {
            std::array<uint8_t, 200> zeros{};
            const auto& data = levelstatus_200 ? *levelstatus_200 : zeros;
            rw_write(out, data.data(), data.size());
        }
    } else {
        // Version 8+: campaign list.
        short num_campaigns = 2;
        rw_write_val(out, num_campaigns);

        // Campaign 1: default campaign
        rw_write_padded(out, campaign_id, 40);
        short cur_level = scen_num;
        rw_write_val(out, cur_level);
        short num_levels = 2;
        rw_write_val(out, num_levels);
        short cleared1 = 1;
        short cleared2 = 3;
        rw_write_val(out, cleared1);
        rw_write_val(out, cleared2);

        // Campaign 2: another id (may not exist on disk; loader tolerates it)
        rw_write_padded(out, "org.openglad.nonexistent", 40);
        short cur_level2 = 1;
        rw_write_val(out, cur_level2);
        short num_levels2 = 1;
        rw_write_val(out, num_levels2);
        short cleared3 = 2;
        rw_write_val(out, cleared3);
    }

    SDL_RWclose(out);
}

void test_save_data_load_rejects_bad_header()
{
    SDL_RWops* out = open_write_file("save/", "bad_header.gtl");
    TEST_ASSERT(out != nullptr, "open_write_file bad header");
    rw_write(out, "BAD", 3);
    char v = 9;
    rw_write_val(out, v);
    SDL_RWclose(out);

    SaveData tmp;
    TEST_ASSERT(!tmp.load("bad_header"), "bad header should fail load");
}
REGISTER_TEST(test_save_data_load_rejects_bad_header);

void test_save_data_load_v4_uses_200_levelstatus()
{
    GuyRecord g{};
    g.family = FAMILY_SOLDIER;
    g.name = "V4GUY";
    g.level = 2;

    std::array<uint8_t, 200> status{};
    status[3] = 1;

    write_save_file("ver4_200",
                    /*version=*/4,
                    /*campaign_id=*/"org.openglad.gladiator",
                    /*scen_num=*/1,
                    /*cash=*/111,
                    /*score=*/222,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    &g,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/false,
                    /*v5plus_levelstatus=*/false,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/&status);

    SaveData tmp;
    TEST_ASSERT(tmp.load("ver4_200"), "v4 load should succeed");
    TEST_ASSERT_EQ(1, tmp.team_size, "v4 should load 1 guy");
    TEST_ASSERT(tmp.is_level_completed(3), "v4 should mark level completed from status array");
}
REGISTER_TEST(test_save_data_load_v4_uses_200_levelstatus);

void test_save_data_load_v7_reads_allied_and_scores()
{
    GuyRecord g{};
    g.family = FAMILY_ARCHER;
    g.name = "V7ARCH";
    g.teamnum = 2;

    std::array<uint8_t, 500> status{};
    status[10] = 1;

    write_save_file("ver7_allied",
                    /*version=*/7,
                    /*campaign_id=*/"org.openglad.gladiator",
                    /*scen_num=*/2,
                    /*cash=*/333,
                    /*score=*/444,
                    /*allied_mode=*/0,
                    /*numplayers=*/2,
                    &g,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/false,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/&status,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    TEST_ASSERT(tmp.load("ver7_allied"), "v7 load should succeed");
    TEST_ASSERT_EQ(2, (int)tmp.numplayers, "v7 should load numplayers");
    TEST_ASSERT_EQ(0, (int)tmp.allied_mode, "v7 should load allied_mode");
    TEST_ASSERT(tmp.is_level_completed(10), "v7 should mark cleared levels from 500-byte status array");
}
REGISTER_TEST(test_save_data_load_v7_reads_allied_and_scores);

void test_save_data_load_v9_uses_campaign_list()
{
    GuyRecord g{};
    g.family = FAMILY_MAGE;
    g.name = "V9MAGE";
    g.level = 5;

    write_save_file("ver9_campaigns",
                    /*version=*/9,
                    /*campaign_id=*/"org.openglad.gladiator",
                    /*scen_num=*/1,
                    /*cash=*/555,
                    /*score=*/666,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    &g,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    TEST_ASSERT(tmp.load("ver9_campaigns"), "v9 load should succeed");
    TEST_ASSERT_EQ(1, tmp.team_size, "v9 should load 1 guy");
    TEST_ASSERT(tmp.current_levels.count("org.openglad.gladiator") > 0, "v9 should populate current_levels");
    TEST_ASSERT(tmp.completed_levels.count("org.openglad.gladiator") > 0, "v9 should populate completed_levels");
}
REGISTER_TEST(test_save_data_load_v9_uses_campaign_list);

void test_save_data_load_non_nul_terminated_save_name_is_bounded()
{
    SDL_RWops* out = open_write_file("save/", "ver9_non_nul_name.gtl");
    TEST_ASSERT(out != nullptr, "open_write_file non-nul save-name");
    if (!out)
        return;

    rw_write(out, "GTL", 3);
    unsigned char version = 9;
    rw_write_val(out, version);

    short registered = 0;
    rw_write_val(out, registered);

    const std::string non_nul_name(40, 'N');
    rw_write(out, non_nul_name.data(), non_nul_name.size());
    rw_write_padded(out, "org.openglad.gladiator", 40);

    short scen_num = 1;
    uint32_t cash = 123;
    uint32_t score = 456;
    rw_write_val(out, scen_num);
    rw_write_val(out, cash);
    rw_write_val(out, score);

    for (int i = 0; i < 4; i++) {
        uint32_t c = cash + (uint32_t)i;
        uint32_t s = score + (uint32_t)(i * 10);
        rw_write_val(out, c);
        rw_write_val(out, s);
    }

    short allied_mode = 1;
    rw_write_val(out, allied_mode);

    short listsize = 0;
    unsigned char numplayers = 1;
    rw_write_val(out, listsize);
    rw_write_val(out, numplayers);
    char filler31[31] = {0};
    rw_write(out, filler31, sizeof(filler31));

    short num_campaigns = 1;
    rw_write_val(out, num_campaigns);
    rw_write_padded(out, "org.openglad.gladiator", 40);
    short cur_level = 1;
    short num_levels = 0;
    rw_write_val(out, cur_level);
    rw_write_val(out, num_levels);

    SDL_RWclose(out);

    SaveData tmp;
    TEST_ASSERT(tmp.load("ver9_non_nul_name"), "v9 load with non-nul save-name should succeed");
    TEST_ASSERT_EQ(40, (int)tmp.save_name.size(), "save_name should remain bounded to 40 bytes");
    TEST_ASSERT(tmp.save_name == std::string(40, 'N'),
                "save_name content should be exactly the 40-byte field");
}
REGISTER_TEST(test_save_data_load_non_nul_terminated_save_name_is_bounded);

void test_save_data_load_with_error_truncated_file_reports_read_failed()
{
    SDL_RWops* out = open_write_file("save/", "truncated_read_fail.gtl");
    TEST_ASSERT(out != nullptr, "open_write_file truncated save");
    // Valid header + version, then truncate before required fields.
    rw_write(out, "GTL", 3);
    char version = 9;
    rw_write_val(out, version);
    SDL_RWclose(out);

    SaveData tmp;
    SaveDataIoError err = tmp.load_with_error("truncated_read_fail");
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::ReadFailed), static_cast<int>(err),
        "truncated save should report ReadFailed");
}
REGISTER_TEST(test_save_data_load_with_error_truncated_file_reports_read_failed);

void test_save_data_load_with_error_campaign_mount_failure()
{
    GuyRecord g{};
    g.family = FAMILY_SOLDIER;
    g.name = "BADCMP";

    // current_campaign is set to an invalid package id in the save header.
    write_save_file("ver9_bad_campaign",
                    /*version=*/9,
                    /*campaign_id=*/"org.openglad.this_campaign_should_not_exist",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    &g,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    SaveDataIoError err = tmp.load_with_error("ver9_bad_campaign");
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::CampaignLoadFailed), static_cast<int>(err),
        "invalid current campaign should report CampaignLoadFailed");

    // Restore expected mounted campaign for other tests.
    TEST_ASSERT(mount_campaign_package_with_error("org.openglad.gladiator") == CampaignPackageIoError::None,
        "should remount default campaign after CampaignLoadFailed test");
}
REGISTER_TEST(test_save_data_load_with_error_campaign_mount_failure);

void test_save_data_load_rejects_negative_team_size()
{
    write_save_file("ver9_negative_team_size",
                    /*version=*/9,
                    /*campaign_id=*/"org.openglad.gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/-1,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    TEST_ASSERT(!tmp.load("ver9_negative_team_size"),
                "load should fail when team listsize is negative");
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::ReadFailed),
                   static_cast<int>(tmp.last_io_error()),
                   "negative team listsize should report ReadFailed");
}
REGISTER_TEST(test_save_data_load_rejects_negative_team_size);

void test_save_data_load_rejects_unbounded_numplayers()
{
    write_save_file("ver9_unbounded_numplayers",
                    /*version=*/9,
                    /*campaign_id=*/"org.openglad.gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/255,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    TEST_ASSERT(!tmp.load("ver9_unbounded_numplayers"),
                "load should fail when numplayers exceeds fixed score/view arrays");
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::ReadFailed),
                   static_cast<int>(tmp.last_io_error()),
                   "invalid numplayers should report ReadFailed");
}
REGISTER_TEST(test_save_data_load_rejects_unbounded_numplayers);

void test_save_data_save_with_error_open_write_failed_for_missing_directory()
{
    const std::string bad_subdir = "save/typed_save_missing_dir";
    std::error_code ec;
    std::filesystem::remove_all(bad_subdir, ec);

    SaveData tmp;
    tmp.current_campaign = "org.openglad.gladiator";
    SaveDataIoError err = tmp.save_with_error("typed_save_missing_dir/slot1");
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::OpenWriteFailed), static_cast<int>(err),
        "save_with_error should report OpenWriteFailed for missing nested directory");
}
REGISTER_TEST(test_save_data_save_with_error_open_write_failed_for_missing_directory);

void test_save_data_v9_roundtrip_preserves_campaign_progress_maps()
{
    SaveData src;
    src.current_campaign = "org.openglad.gladiator";
    src.current_levels.clear();
    src.completed_levels.clear();
    src.current_levels["org.openglad.gladiator"] = 1;
    src.current_levels["org.openglad.other"] = 2;
    src.completed_levels["org.openglad.gladiator"].insert(1);
    src.completed_levels["org.openglad.gladiator"].insert(3);
    src.completed_levels["org.openglad.other"].insert(2);

    SaveDataIoError save_err = src.save_with_error("typed_save_roundtrip_v9");
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::None), static_cast<int>(save_err),
        "save_with_error should succeed for roundtrip save");

    SaveData loaded;
    SaveDataIoError load_err = loaded.load_with_error("typed_save_roundtrip_v9");
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::None), static_cast<int>(load_err),
        "load_with_error should succeed for roundtrip save");

    TEST_ASSERT(loaded.current_levels.count("org.openglad.gladiator") > 0,
        "current_levels should include default campaign");
    TEST_ASSERT_EQ(1, loaded.current_levels["org.openglad.gladiator"],
        "default campaign current level should remain valid after reload");
    TEST_ASSERT(loaded.current_levels.count("org.openglad.other") > 0,
        "current_levels should include secondary campaign");
    TEST_ASSERT_EQ(2, loaded.current_levels["org.openglad.other"],
        "secondary campaign current level should roundtrip");
    TEST_ASSERT(loaded.completed_levels.count("org.openglad.gladiator") > 0,
        "completed_levels should include default campaign");
    TEST_ASSERT(loaded.completed_levels["org.openglad.gladiator"].count(1) > 0,
        "completed level 1 should roundtrip");
    TEST_ASSERT(loaded.completed_levels["org.openglad.gladiator"].count(3) > 0,
        "completed level 3 should roundtrip");
    TEST_ASSERT(loaded.completed_levels.count("org.openglad.other") > 0,
        "completed_levels should include secondary campaign");
    TEST_ASSERT(loaded.completed_levels["org.openglad.other"].count(2) > 0,
        "secondary campaign completed level should roundtrip");
}
REGISTER_TEST(test_save_data_v9_roundtrip_preserves_campaign_progress_maps);

void test_save_data_update_guys_copies_only_live_entries_with_myguy()
{
    std::list<std::unique_ptr<walker>> oblist;

    auto live_with_guy = std::make_unique<walker>();
    live_with_guy->dead = 0;
    live_with_guy->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    live_with_guy->myguy->exp = 50;

    auto dead_with_guy = std::make_unique<walker>();
    dead_with_guy->dead = 1;
    dead_with_guy->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHER));

    auto live_no_guy = std::make_unique<walker>();
    live_no_guy->dead = 0;
    live_no_guy->clear_myguy();

    oblist.push_back(std::move(live_with_guy));
    oblist.push_back(std::move(dead_with_guy));
    oblist.push_back(std::move(live_no_guy));

    std::vector<const guy*> guys;
    guys.reserve(oblist.size());
    for (const auto& uptr : oblist)
    {
        const walker* w = uptr.get();
        if (w && !w->dead && w->myguy)
            guys.push_back(w->myguy);
    }

    SaveData data;
    data.update_guys(guys);

    TEST_ASSERT_EQ(1, (int)data.team_size, "update_guys should copy only live walkers with myguy");
    TEST_ASSERT(data.team_list[0] != nullptr, "copied guy should exist");
    if (data.team_list[0])
        TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)data.team_list[0]->family, "copied guy should match source family");
}
REGISTER_TEST(test_save_data_update_guys_copies_only_live_entries_with_myguy);

void test_save_data_unsupported_version_and_campaign_helper_branches()
{
    SDL_RWops* out = open_write_file("save/", "unsupported_ver0.gtl");
    TEST_ASSERT(out != nullptr, "open_write_file unsupported version");
    if (!out)
        return;
    rw_write(out, "GTL", 3);
    unsigned char version0 = 0;
    rw_write_val(out, version0);
    SDL_RWclose(out);

    SaveData tmp;
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::UnsupportedVersion),
                   static_cast<int>(tmp.load_with_error("unsupported_ver0")),
                   "version 0 save should report UnsupportedVersion");

    tmp.completed_levels.clear();
    tmp.add_level_completed("test.campaign", 2);
    tmp.add_level_completed("test.campaign", 5);
    TEST_ASSERT_EQ(2, tmp.get_num_levels_completed("test.campaign"),
                   "helper should count completed levels for present campaign");
    TEST_ASSERT_EQ(0, tmp.get_num_levels_completed("missing.campaign"),
                   "helper should return 0 for missing campaign");
    tmp.reset_campaign("test.campaign");
    TEST_ASSERT_EQ(0, tmp.get_num_levels_completed("test.campaign"),
                   "reset_campaign should clear existing campaign progress");
}
REGISTER_TEST(test_save_data_unsupported_version_and_campaign_helper_branches);

void test_save_data_save_with_team_entry_and_wrapper_none_path()
{
    SaveData tmp;
    tmp.current_campaign = "org.openglad.gladiator";
    tmp.scen_num = 3;
    tmp.team_size = 1;
    tmp.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    tmp.team_list[0]->name = "B6TEAM";
    tmp.team_list[0]->strength = 14;
    tmp.team_list[0]->dexterity = 13;
    tmp.team_list[0]->constitution = 12;
    tmp.team_list[0]->intelligence = 11;
    tmp.team_list[0]->armor = 10;
    tmp.team_list[0]->level = 4;
    tmp.team_list[0]->exp = 777;
    tmp.team_list[0]->kills = 5;
    tmp.team_list[0]->level_kills = 6;
    tmp.team_list[0]->total_damage = 7;
    tmp.team_list[0]->total_hits = 8;
    tmp.team_list[0]->total_shots = 9;
    tmp.team_list[0]->teamnum = 0;

    tmp.current_levels.clear();      // exercise insert branch for current campaign on save
    tmp.completed_levels.clear();
    tmp.completed_levels["org.openglad.gladiator"].insert(1);
    tmp.completed_levels["org.openglad.gladiator"].insert(2);

    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
                   static_cast<int>(tmp.save_with_error("typed_save_with_team")),
                   "save_with_error should succeed for one-team-entry save");

    SaveData loaded;
    TEST_ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
                   static_cast<int>(loaded.load_with_error("typed_save_with_team")),
                   "load_with_error wrapper should return None for saved file");
    TEST_ASSERT_EQ(1, (int)loaded.team_size, "loaded team should contain one entry");
}
REGISTER_TEST(test_save_data_save_with_team_entry_and_wrapper_none_path);

void test_save_data_round8_open_write_failure_and_is_level_completed_paths()
{
    SaveData data;
    data.current_campaign = "round8.campaign";
    data.completed_levels.clear();

    TEST_ASSERT(!data.is_level_completed(3),
                "is_level_completed should be false when current campaign is absent");

    data.add_level_completed("round8.campaign", 3);
    TEST_ASSERT(data.is_level_completed(3),
                "is_level_completed should be true after adding the level to current campaign");
    TEST_ASSERT(!data.is_level_completed(99),
                "is_level_completed should be false for a non-completed level index");

    data.reset_campaign("round8.campaign");
    TEST_ASSERT(!data.is_level_completed(3),
                "is_level_completed should become false after reset_campaign");

    const SaveDataIoError err = data.save_with_error("round8/missing_parent_path");
    TEST_ASSERT_EQ((int)SaveDataIoError::OpenWriteFailed, (int)err,
                   "save_with_error should report OpenWriteFailed when parent path is missing");
}
REGISTER_TEST(test_save_data_round8_open_write_failure_and_is_level_completed_paths);

void test_save_data_round9_reset_campaign_missing_entry_is_noop()
{
    SaveData data;
    data.completed_levels.clear();
    data.current_campaign = "round9.none";

    // Missing campaign path should be a no-op.
    data.reset_campaign("round9.none");
    TEST_ASSERT_EQ(0, data.get_num_levels_completed("round9.none"),
                   "reset_campaign should keep missing campaign at zero levels");

    // Existing campaign path should clear only that campaign's levels.
    data.add_level_completed("round9.a", 1);
    data.add_level_completed("round9.a", 2);
    data.add_level_completed("round9.b", 3);
    data.reset_campaign("round9.a");

    TEST_ASSERT_EQ(0, data.get_num_levels_completed("round9.a"),
                   "reset_campaign should clear target campaign progress");
    TEST_ASSERT_EQ(1, data.get_num_levels_completed("round9.b"),
                   "reset_campaign should not clear other campaign progress");
}
REGISTER_TEST(test_save_data_round9_reset_campaign_missing_entry_is_noop);
