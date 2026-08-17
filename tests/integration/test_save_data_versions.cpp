#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <openglad/legacy/base.h> // Order + family constants used for test data
#include <openglad/resources/company.h>
#include <openglad/resources/io.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include "test_save_state_guard.h"

static void rw_write(SDL_IOStream* out, const void* data, size_t len)
{
    SDL_WriteIO(out, data, len);
}

template <typename T>
static void rw_write_val(SDL_IOStream* out, const T& v)
{
    SDL_WriteIO(out, &v, sizeof(T));
}

static void rw_write_padded(SDL_IOStream* out, const std::string& s, size_t len)
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
    unsigned char deployed = 1; // v14+: 0 = held back (guy offset +50)
    unsigned char campaign_tag = 0; // v16+: assignment channel (guy offset +51)
};

static void write_guy(SDL_IOStream* out, const GuyRecord& g, char version = 13)
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

    if (version >= 16) {
        // v16+ reinterprets the second reserved byte as the campaign tag.
        rw_write_val(out, g.deployed);
        rw_write_val(out, g.campaign_tag);
        char filler6[6] = {0};
        rw_write(out, filler6, sizeof(filler6));
    } else if (version >= 14) {
        // v14+ reinterprets the first reserved byte as the deploy flag.
        rw_write_val(out, g.deployed);
        char filler7[7] = {0};
        rw_write(out, filler7, sizeof(filler7));
    } else {
        char filler8[8] = {0};
        rw_write(out, filler8, sizeof(filler8));
    }
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
                            const std::array<uint8_t, 200>* levelstatus_200,
                            short ctf_team_count = 2,
                            short ctf_capture_limit = 0,
                            short ctf_respawn_ticks = 0,
                            short ctf_strip_scenario_troops = 0,
                            short respawn_mode = 0,
                            short generator_rate = 0,
                            short keep_fallen_heroes = 0,
                            short tower_best_floor = 0,
                            uint32_t tower_run_seed = 0,
                            int64_t last_played_unix_s = 0)
{
    std::string fname = filename_no_ext + ".gtl";
    SDL_IOStream* out = open_write_file("save/", fname.c_str());
    ASSERT_TRUE(out != nullptr) << "open_write_file for save";

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

    // Listsize, numplayers, reserved (v14+ reinterprets the first 8 reserved
    // bytes at offset 133 as the last-played unix timestamp)
    rw_write_val(out, listsize);
    rw_write_val(out, numplayers);
    if (version >= 14) {
        rw_write_val(out, last_played_unix_s);
        char filler23[23] = {0};
        rw_write(out, filler23, sizeof(filler23));
    } else {
        char filler31[31] = {0};
        rw_write(out, filler31, sizeof(filler31));
    }

    for (int i = 0; i < listsize; i++) {
        write_guy(out, guys[i], version);
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
        rw_write_padded(out, "nonexistent", 40);
        short cur_level2 = 1;
        rw_write_val(out, cur_level2);
        short num_levels2 = 1;
        rw_write_val(out, num_levels2);
        short cleared3 = 2;
        rw_write_val(out, cleared3);
    }

    // Version 10+ appends the CTF match settings.
    if (version >= 10) {
        rw_write_val(out, ctf_team_count);
        rw_write_val(out, ctf_capture_limit);
        rw_write_val(out, ctf_respawn_ticks);
    }

    // Version 11+ appends the CTF scenario-troops strip flag.
    if (version >= 11) {
        rw_write_val(out, ctf_strip_scenario_troops);
    }

    // Version 12+ appends the difficulty submenu settings.
    if (version >= 12) {
        rw_write_val(out, respawn_mode);
        rw_write_val(out, generator_rate);
        rw_write_val(out, keep_fallen_heroes);
    }

    // Version 13+ appends the Tower Climb fields (seed as 2 x int16 lo/hi).
    if (version >= 13) {
        rw_write_val(out, tower_best_floor);
        const short seed_lo =
            static_cast<short>(static_cast<uint16_t>(tower_run_seed & 0xFFFFu));
        const short seed_hi =
            static_cast<short>(static_cast<uint16_t>(tower_run_seed >> 16));
        rw_write_val(out, seed_lo);
        rw_write_val(out, seed_hi);
    }

    // Version 15+ appends the campaign scripted-state block (empty here;
    // the state-carrying fixtures round-trip through SaveData itself).
    if (version >= 15) {
        short num_state_campaigns = 0;
        rw_write_val(out, num_state_campaigns);
    }

    SDL_CloseIO(out);
}

TEST(SaveDataVersions, save_data_load_rejects_bad_header)
{
    SDL_IOStream* out = open_write_file("save/", "bad_header.gtl");
    ASSERT_TRUE(out != nullptr) << "open_write_file bad header";
    rw_write(out, "BAD", 3);
    char v = 9;
    rw_write_val(out, v);
    SDL_CloseIO(out);

    SaveData tmp;
    ASSERT_TRUE(!tmp.load("bad_header")) << "bad header should fail load";
}


TEST(SaveDataVersions, save_data_load_v4_uses_200_levelstatus)
{
    GuyRecord g{};
    g.family = FAMILY_SOLDIER;
    g.name = "V4GUY";
    g.level = 2;

    std::array<uint8_t, 200> status{};
    status[3] = 1;

    write_save_file("ver4_200",
                    /*version=*/4,
                    /*campaign_id=*/"gladiator",
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
    ASSERT_TRUE(tmp.load("ver4_200")) << "v4 load should succeed";
    ASSERT_EQ(1, tmp.team_size) << "v4 should load 1 guy";
    ASSERT_TRUE(tmp.is_level_completed(3)) << "v4 should mark level completed from status array";
}


TEST(SaveDataVersions, save_data_load_v7_reads_allied_and_scores)
{
    GuyRecord g{};
    g.family = FAMILY_ARCHER;
    g.name = "V7ARCH";
    g.teamnum = 2;

    std::array<uint8_t, 500> status{};
    status[10] = 1;

    write_save_file("ver7_allied",
                    /*version=*/7,
                    /*campaign_id=*/"gladiator",
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
    tmp.numplayers = 3;
    ASSERT_TRUE(tmp.load("ver7_allied")) << "v7 load should succeed";
    ASSERT_EQ(3, (int)tmp.numplayers)
        << "the legacy byte is consumed, but player count remains live "
           "session state";
    ASSERT_EQ(0, (int)tmp.allied_mode) << "v7 should load allied_mode";
    ASSERT_TRUE(tmp.is_level_completed(10)) << "v7 should mark cleared levels from 500-byte status array";
}


TEST(SaveDataVersions, save_data_load_v9_uses_campaign_list)
{
    GuyRecord g{};
    g.family = FAMILY_MAGE;
    g.name = "V9MAGE";
    g.level = 5;

    write_save_file("ver9_campaigns",
                    /*version=*/9,
                    /*campaign_id=*/"gladiator",
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
    ASSERT_TRUE(tmp.load("ver9_campaigns")) << "v9 load should succeed";
    ASSERT_EQ(1, tmp.team_size) << "v9 should load 1 guy";
    ASSERT_TRUE(tmp.current_levels.count("gladiator") > 0) << "v9 should populate current_levels";
    ASSERT_TRUE(tmp.completed_levels.count("gladiator") > 0) << "v9 should populate completed_levels";
}


TEST(SaveDataVersions, save_data_load_v10_reads_ctf_settings)
{
    write_save_file("ver10_ctf",
                    /*version=*/10,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/3,
                    /*ctf_capture_limit=*/5,
                    /*ctf_respawn_ticks=*/240);

    SaveData tmp;
    ASSERT_TRUE(tmp.load("ver10_ctf")) << "v10 load should succeed";
    ASSERT_EQ(3, (int)tmp.ctf_team_count) << "v10 should read ctf_team_count";
    ASSERT_EQ(5, (int)tmp.ctf_capture_limit) << "v10 should read ctf_capture_limit";
    ASSERT_EQ(240, (int)tmp.ctf_respawn_ticks) << "v10 should read ctf_respawn_ticks";
}


TEST(SaveDataVersions, save_data_load_v9_payload_defaults_ctf_settings)
{
    write_save_file("ver9_no_ctf",
                    /*version=*/9,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    // Poison the in-memory fields: a v9 payload must restore classic defaults.
    tmp.ctf_team_count = 4;
    tmp.ctf_capture_limit = 7;
    tmp.ctf_respawn_ticks = 999;
    ASSERT_TRUE(tmp.load("ver9_no_ctf")) << "v9 load should succeed";
    ASSERT_EQ(0, (int)tmp.ctf_team_count) << "v9 saves default ctf_team_count to Auto";
    ASSERT_EQ(0, (int)tmp.ctf_capture_limit) << "v9 saves default ctf_capture_limit to 0";
    ASSERT_EQ(0, (int)tmp.ctf_respawn_ticks) << "v9 saves default ctf_respawn_ticks to 0";
}


TEST(SaveDataVersions, save_data_v10_roundtrip_preserves_ctf_settings)
{
    SaveData src;
    src.current_campaign = "gladiator";
    src.ctf_team_count = 4;
    src.ctf_capture_limit = 10;
    src.ctf_respawn_ticks = 120;

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_ctf_roundtrip")))
        << "v10 writer should succeed";

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_ctf_roundtrip")))
        << "v10 reader should succeed";
    ASSERT_EQ(4, (int)loaded.ctf_team_count) << "ctf_team_count should roundtrip";
    ASSERT_EQ(10, (int)loaded.ctf_capture_limit) << "ctf_capture_limit should roundtrip";
    ASSERT_EQ(120, (int)loaded.ctf_respawn_ticks) << "ctf_respawn_ticks should roundtrip";
}


TEST(SaveDataVersions, save_data_load_v10_payload_defaults_strip_flag)
{
    write_save_file("ver10_no_strip",
                    /*version=*/10,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/3,
                    /*ctf_capture_limit=*/5,
                    /*ctf_respawn_ticks=*/240);

    SaveData tmp;
    // Poison the in-memory field: a v10 payload must restore the classic default.
    tmp.ctf_strip_scenario_troops = 1;
    ASSERT_TRUE(tmp.load("ver10_no_strip")) << "v10 load should succeed";
    ASSERT_EQ(0, (int)tmp.ctf_strip_scenario_troops)
        << "v10 saves default ctf_strip_scenario_troops to 0 (keep troops)";
    ASSERT_EQ(3, (int)tmp.ctf_team_count) << "v10 fields still read";
}


TEST(SaveDataVersions, save_data_load_v11_reads_strip_flag)
{
    write_save_file("ver11_strip",
                    /*version=*/11,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/2,
                    /*ctf_capture_limit=*/0,
                    /*ctf_respawn_ticks=*/0,
                    /*ctf_strip_scenario_troops=*/1);

    SaveData tmp;
    ASSERT_TRUE(tmp.load("ver11_strip")) << "v11 load should succeed";
    ASSERT_EQ(1, (int)tmp.ctf_strip_scenario_troops)
        << "v11 should read ctf_strip_scenario_troops";
}

// The knob is 0 (keep the authored cast), 2 (strip it), or 3 (strip plus
// census-matched squads, TROOPS: FAIR — matched-teams D27) on the SAME
// int16 record field, so v11 carries any of them without a format bump.
// State 1 is the retired middle state: it still round-trips verbatim, and
// every strip rule reads it as 2 — pinned by the OWN-semantics arm below.
TEST(SaveDataVersions, save_data_round_trips_strip_all_without_a_format_bump)
{
    for (const short state : {short{0}, short{2}, short{1}, short{3}})
    {
        write_save_file("ver11_strip_all",
                        /*version=*/11,
                        /*campaign_id=*/"gladiator",
                        /*scen_num=*/1,
                        /*cash=*/100,
                        /*score=*/200,
                        /*allied_mode=*/1,
                        /*numplayers=*/1,
                        /*guys=*/nullptr,
                        /*listsize=*/0,
                        /*use_v8plus_campaigns=*/true,
                        /*v5plus_levelstatus=*/true,
                        /*levelstatus_500=*/nullptr,
                        /*levelstatus_200=*/nullptr,
                        /*ctf_team_count=*/2,
                        /*ctf_capture_limit=*/0,
                        /*ctf_respawn_ticks=*/0,
                        /*ctf_strip_scenario_troops=*/state);

        SaveData read_back;
        ASSERT_TRUE(read_back.load("ver11_strip_all")) << "state " << state;
        ASSERT_EQ(state, read_back.ctf_strip_scenario_troops) << "state " << state;

        // ...and survives a save() the game itself writes.
        ASSERT_TRUE(read_back.save("ver11_strip_all_rt")) << "state " << state;
        SaveData twice;
        ASSERT_TRUE(twice.load("ver11_strip_all_rt")) << "state " << state;
        ASSERT_EQ(state, twice.ctf_strip_scenario_troops)
            << "state " << state << " must survive the game's own writer";

        // What the stored value MEANS: 0 keeps, everything above it strips,
        // and exactly 3 additionally sizes the generated squads (FAIR).
        const char* expected_label = "TROOPS: OWN";
        if (state == 0)
            expected_label = "TROOPS: ALL";
        else if (state == og::sim::kTroopsMatched)
            expected_label = "TROOPS: FAIR";
        EXPECT_EQ(expected_label, og::ui::format_ctf_troops_label(twice))
            << "state " << state;
    }
}


TEST(SaveDataVersions, save_data_v11_roundtrip_preserves_strip_flag_and_version_byte)
{
    SaveData src;
    src.current_campaign = "gladiator";
    src.ctf_strip_scenario_troops = 1;

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_strip_roundtrip")))
        << "v11 writer should succeed";

    // The writer must stamp version 16 in the GTL header.
    SDL_IOStream* in = open_read_file("save/", "typed_save_strip_roundtrip.gtl");
    ASSERT_TRUE(in != nullptr) << "saved file should be readable";
    char header[3] = {};
    unsigned char version_byte = 0;
    SDL_ReadIO(in, header, 3);
    SDL_ReadIO(in, &version_byte, 1);
    SDL_CloseIO(in);
    ASSERT_EQ(0, std::memcmp(header, "GTL", 3)) << "GTL header expected";
    ASSERT_EQ(16, (int)version_byte) << "writer should stamp version 16";

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_strip_roundtrip")))
        << "v11 reader should succeed";
    ASSERT_EQ(1, (int)loaded.ctf_strip_scenario_troops)
        << "ctf_strip_scenario_troops should roundtrip";
}


TEST(SaveDataVersions, save_data_load_v12_reads_difficulty_submenu_settings)
{
    write_save_file("ver12_difficulty",
                    /*version=*/12,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/2,
                    /*ctf_capture_limit=*/0,
                    /*ctf_respawn_ticks=*/0,
                    /*ctf_strip_scenario_troops=*/0,
                    /*respawn_mode=*/2,
                    /*generator_rate=*/200,
                    /*keep_fallen_heroes=*/1);

    SaveData tmp;
    ASSERT_TRUE(tmp.load("ver12_difficulty")) << "v12 load should succeed";
    ASSERT_EQ(2, (int)tmp.respawn_mode) << "v12 should read respawn_mode";
    ASSERT_EQ(200, (int)tmp.generator_rate) << "v12 should read generator_rate";
    ASSERT_EQ(1, (int)tmp.keep_fallen_heroes) << "v12 should read keep_fallen_heroes";
}


TEST(SaveDataVersions, save_data_load_v11_payload_defaults_difficulty_submenu_settings)
{
    write_save_file("ver11_no_difficulty",
                    /*version=*/11,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/2,
                    /*ctf_capture_limit=*/0,
                    /*ctf_respawn_ticks=*/0,
                    /*ctf_strip_scenario_troops=*/1);

    SaveData tmp;
    // Poison the in-memory fields: a v11 payload must restore classic defaults.
    tmp.respawn_mode = 2;
    tmp.generator_rate = 200;
    tmp.keep_fallen_heroes = 1;
    ASSERT_TRUE(tmp.load("ver11_no_difficulty")) << "v11 load should succeed";
    ASSERT_EQ(0, (int)tmp.respawn_mode) << "v11 saves default respawn_mode to off";
    ASSERT_EQ(0, (int)tmp.generator_rate) << "v11 saves default generator_rate to 0";
    ASSERT_EQ(0, (int)tmp.keep_fallen_heroes)
        << "v11 saves default keep_fallen_heroes to permadeath";
    ASSERT_EQ(1, (int)tmp.ctf_strip_scenario_troops) << "v11 fields still read";
}


TEST(SaveDataVersions, save_data_v12_roundtrip_preserves_difficulty_submenu_settings)
{
    SaveData src;
    src.current_campaign = "gladiator";
    src.respawn_mode = 3;
    src.generator_rate = 50;
    src.keep_fallen_heroes = 1;

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_difficulty_roundtrip")))
        << "v12 writer should succeed";

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_difficulty_roundtrip")))
        << "v12 reader should succeed";
    ASSERT_EQ(3, (int)loaded.respawn_mode)
        << "Team 1 Heroes respawn_mode should roundtrip";
    ASSERT_EQ(50, (int)loaded.generator_rate) << "generator_rate should roundtrip";
    ASSERT_EQ(1, (int)loaded.keep_fallen_heroes)
        << "keep_fallen_heroes should roundtrip";
}


TEST(SaveDataVersions, save_data_load_v13_reads_tower_fields)
{
    write_save_file("ver13_tower",
                    /*version=*/13,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/2,
                    /*ctf_capture_limit=*/0,
                    /*ctf_respawn_ticks=*/0,
                    /*ctf_strip_scenario_troops=*/0,
                    /*respawn_mode=*/0,
                    /*generator_rate=*/0,
                    /*keep_fallen_heroes=*/0,
                    /*tower_best_floor=*/23,
                    /*tower_run_seed=*/0xDEADBEEFu);

    SaveData tmp;
    ASSERT_TRUE(tmp.load("ver13_tower")) << "v13 load should succeed";
    ASSERT_EQ(23, (int)tmp.tower_best_floor) << "v13 should read tower_best_floor";
    ASSERT_EQ(0xDEADBEEFu, tmp.tower_run_seed)
        << "v13 should reassemble tower_run_seed from the 2 int16 halves";
}


TEST(SaveDataVersions, save_data_load_v12_payload_defaults_tower_fields)
{
    write_save_file("ver12_no_tower",
                    /*version=*/12,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/2,
                    /*ctf_capture_limit=*/0,
                    /*ctf_respawn_ticks=*/0,
                    /*ctf_strip_scenario_troops=*/0,
                    /*respawn_mode=*/1,
                    /*generator_rate=*/50,
                    /*keep_fallen_heroes=*/1);

    SaveData tmp;
    // Poison the in-memory fields: a v12 payload must restore the defaults.
    tmp.tower_best_floor = 42;
    tmp.tower_run_seed = 0x12345678u;
    ASSERT_TRUE(tmp.load("ver12_no_tower")) << "v12 load should succeed";
    ASSERT_EQ(0, (int)tmp.tower_best_floor)
        << "v12 saves default tower_best_floor to 0 (no climb recorded)";
    ASSERT_EQ(0u, tmp.tower_run_seed)
        << "v12 saves default tower_run_seed to 0 (no run in progress)";
    ASSERT_EQ(1, (int)tmp.respawn_mode) << "v12 fields still read";
    ASSERT_EQ(50, (int)tmp.generator_rate) << "v12 fields still read";
}


TEST(SaveDataVersions, save_data_v13_roundtrip_preserves_tower_fields)
{
    SaveData src;
    src.current_campaign = "gladiator";
    src.tower_best_floor = 17;
    // Both int16 halves nontrivial AND the high bit of each half set, so the
    // lo/hi reassembly and the unsigned carriage are both exercised.
    src.tower_run_seed = 0xCAFEBABEu;

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_tower_roundtrip")))
        << "v13 writer should succeed";

    // The writer must stamp version 16 in the GTL header.
    SDL_IOStream* in = open_read_file("save/", "typed_save_tower_roundtrip.gtl");
    ASSERT_TRUE(in != nullptr) << "saved file should be readable";
    char header[3] = {};
    unsigned char version_byte = 0;
    SDL_ReadIO(in, header, 3);
    SDL_ReadIO(in, &version_byte, 1);
    SDL_CloseIO(in);
    ASSERT_EQ(0, std::memcmp(header, "GTL", 3)) << "GTL header expected";
    ASSERT_EQ(16, (int)version_byte) << "writer should stamp version 16";

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_tower_roundtrip")))
        << "v13 reader should succeed";
    ASSERT_EQ(17, (int)loaded.tower_best_floor)
        << "tower_best_floor should roundtrip";
    ASSERT_EQ(0xCAFEBABEu, loaded.tower_run_seed)
        << "tower_run_seed should roundtrip through the 2 int16 halves";
}


TEST(SaveDataVersions, save_data_load_non_nul_terminated_save_name_is_bounded)
{
    SDL_IOStream* out = open_write_file("save/", "ver9_non_nul_name.gtl");
    ASSERT_TRUE(out != nullptr) << "open_write_file non-nul save-name";
    if (!out)
        return;

    rw_write(out, "GTL", 3);
    unsigned char version = 9;
    rw_write_val(out, version);

    short registered = 0;
    rw_write_val(out, registered);

    const std::string non_nul_name(40, 'N');
    rw_write(out, non_nul_name.data(), non_nul_name.size());
    rw_write_padded(out, "gladiator", 40);

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
    rw_write_padded(out, "gladiator", 40);
    short cur_level = 1;
    short num_levels = 0;
    rw_write_val(out, cur_level);
    rw_write_val(out, num_levels);

    SDL_CloseIO(out);

    SaveData tmp;
    ASSERT_TRUE(tmp.load("ver9_non_nul_name")) << "v9 load with non-nul save-name should succeed";
    ASSERT_EQ(40, (int)tmp.save_name.size()) << "save_name should remain bounded to 40 bytes";
    ASSERT_TRUE(tmp.save_name == std::string(40, 'N')) << "save_name content should be exactly the 40-byte field";
}


TEST(SaveDataVersions, save_data_load_with_error_truncated_file_reports_read_failed)
{
    SDL_IOStream* out = open_write_file("save/", "truncated_read_fail.gtl");
    ASSERT_TRUE(out != nullptr) << "open_write_file truncated save";
    // Valid header + version, then truncate before required fields.
    rw_write(out, "GTL", 3);
    char version = 9;
    rw_write_val(out, version);
    SDL_CloseIO(out);

    SaveData tmp;
    SaveDataIoError err = tmp.load_with_error("truncated_read_fail");
    ASSERT_EQ(static_cast<int>(SaveDataIoError::ReadFailed), static_cast<int>(err)) << "truncated save should report ReadFailed";
}


TEST(SaveDataVersions, save_data_rejects_unsafe_save_slot_names)
{
    SaveData tmp;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::OpenReadFailed),
              static_cast<int>(tmp.load_with_error("../save0")))
        << "load should reject traversal save slot names before path construction";
    ASSERT_EQ(static_cast<int>(SaveDataIoError::OpenWriteFailed),
              static_cast<int>(tmp.save_with_error("../save0")))
        << "save should reject traversal save slot names before path construction";
    ASSERT_EQ(static_cast<int>(SaveDataIoError::OpenWriteFailed),
              static_cast<int>(tmp.save_with_error("nested/slot1")))
        << "save should reject nested save slot names before path construction";
}


TEST(SaveDataVersions, save_data_load_with_error_campaign_mount_failure)
{
    GuyRecord g{};
    g.family = FAMILY_SOLDIER;
    g.name = "BADCMP";

    // current_campaign is set to an invalid package id in the save header.
    write_save_file("ver9_bad_campaign",
                    /*version=*/9,
                    /*campaign_id=*/"this_campaign_should_not_exist",
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
    ASSERT_EQ(static_cast<int>(SaveDataIoError::CampaignLoadFailed), static_cast<int>(err)) << "invalid current campaign should report CampaignLoadFailed";

    // Restore expected mounted campaign for other tests.
    ASSERT_TRUE(mount_campaign_package_with_error("gladiator") == CampaignPackageIoError::None) << "should remount default campaign after CampaignLoadFailed test";
}


TEST(SaveDataVersions, save_data_load_rejects_negative_team_size)
{
    write_save_file("ver9_negative_team_size",
                    /*version=*/9,
                    /*campaign_id=*/"gladiator",
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
    ASSERT_TRUE(!tmp.load("ver9_negative_team_size")) << "load should fail when team listsize is negative";
    ASSERT_EQ(static_cast<int>(SaveDataIoError::ReadFailed), static_cast<int>(tmp.last_io_error())) << "negative team listsize should report ReadFailed";
}


TEST(SaveDataVersions, save_data_load_rejects_invalid_legacy_player_byte)
{
    write_save_file("ver9_unbounded_numplayers",
                    /*version=*/9,
                    /*campaign_id=*/"gladiator",
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
    ASSERT_TRUE(!tmp.load("ver9_unbounded_numplayers"))
        << "load should reject an out-of-range retired compatibility byte";
    ASSERT_EQ(static_cast<int>(SaveDataIoError::ReadFailed),
              static_cast<int>(tmp.last_io_error()))
        << "an invalid legacy player byte should report ReadFailed";
}


TEST(SaveDataVersions, save_data_v2_load_defaults_stats_added_by_later_versions)
{
    GuyRecord legacy{};
    legacy.name = "V2LEGACY";
    legacy.kills = 91;
    legacy.level_kills = 92;
    legacy.total_damage = 93;
    legacy.total_hits = 94;
    legacy.total_shots = 95;

    write_save_file("ver2_legacy_stat_defaults",
                    /*version=*/2,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/4,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/0,
                    /*numplayers=*/1,
                    /*guys=*/&legacy,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/false,
                    /*v5plus_levelstatus=*/false,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData loaded;
    ASSERT_EQ(SaveDataIoError::None,
              loaded.load_with_error("ver2_legacy_stat_defaults"));
    ASSERT_EQ(1, static_cast<int>(loaded.team_size));
    ASSERT_TRUE(loaded.team_list[0] != nullptr);
    EXPECT_EQ("V2LEGACY", loaded.team_list[0]->name);
    EXPECT_EQ(0, loaded.team_list[0]->kills)
        << "v2 predates persisted kill counts";
    EXPECT_EQ(0, loaded.team_list[0]->level_kills)
        << "v2 predates persisted per-level kills";
    EXPECT_EQ(0, loaded.team_list[0]->total_damage)
        << "v2 predates persisted damage totals";
    EXPECT_EQ(0, loaded.team_list[0]->total_hits)
        << "v2 predates persisted hit totals";
    EXPECT_EQ(0, loaded.team_list[0]->total_shots)
        << "v2 predates persisted shot totals";
}


TEST(SaveDataVersions, save_data_save_with_error_open_write_failed_for_missing_directory)
{
    const std::string bad_subdir = "save/typed_save_missing_dir";
    std::error_code ec;
    std::filesystem::remove_all(bad_subdir, ec);

    SaveData tmp;
    tmp.current_campaign = "gladiator";
    SaveDataIoError err = tmp.save_with_error("typed_save_missing_dir/slot1");
    ASSERT_EQ(static_cast<int>(SaveDataIoError::OpenWriteFailed), static_cast<int>(err)) << "save_with_error should report OpenWriteFailed for missing nested directory";
}


TEST(SaveDataVersions, save_data_v9_roundtrip_preserves_campaign_progress_maps)
{
    SaveData src;
    src.current_campaign = "gladiator";
    src.current_levels.clear();
    src.completed_levels.clear();
    src.current_levels["gladiator"] = 1;
    src.current_levels["other"] = 2;
    src.completed_levels["gladiator"].insert(1);
    src.completed_levels["gladiator"].insert(3);
    src.completed_levels["other"].insert(2);

    SaveDataIoError save_err = src.save_with_error("typed_save_roundtrip_v9");
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None), static_cast<int>(save_err)) << "save_with_error should succeed for roundtrip save";

    SaveData loaded;
    SaveDataIoError load_err = loaded.load_with_error("typed_save_roundtrip_v9");
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None), static_cast<int>(load_err)) << "load_with_error should succeed for roundtrip save";

    ASSERT_TRUE(loaded.current_levels.count("gladiator") > 0) << "current_levels should include default campaign";
    ASSERT_EQ(1, loaded.current_levels["gladiator"]) << "default campaign current level should remain valid after reload";
    ASSERT_TRUE(loaded.current_levels.count("other") > 0) << "current_levels should include secondary campaign";
    ASSERT_EQ(2, loaded.current_levels["other"]) << "secondary campaign current level should roundtrip";
    ASSERT_TRUE(loaded.completed_levels.count("gladiator") > 0) << "completed_levels should include default campaign";
    ASSERT_TRUE(loaded.completed_levels["gladiator"].count(1) > 0) << "completed level 1 should roundtrip";
    ASSERT_TRUE(loaded.completed_levels["gladiator"].count(3) > 0) << "completed level 3 should roundtrip";
    ASSERT_TRUE(loaded.completed_levels.count("other") > 0) << "completed_levels should include secondary campaign";
    ASSERT_TRUE(loaded.completed_levels["other"].count(2) > 0) << "secondary campaign completed level should roundtrip";
}


// A company written before the reverse-DNS purge: the 40-byte header
// campaign and the progress-list keys all carry "org.openglad.<name>".
// Loading folds every stored id onto its plain spelling; the legacy keys
// never surface in the maps.
TEST(SaveDataVersions, save_data_load_normalizes_legacy_reverse_dns_ids)
{
    GuyRecord g{};
    g.family = FAMILY_SOLDIER;
    g.name = "LEGACY";
    g.level = 1;

    write_save_file("legacy_rdns",
                    /*version=*/14,
                    /*campaign_id=*/"org.openglad.gladiator",
                    /*scen_num=*/2,
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
    ASSERT_TRUE(tmp.load("legacy_rdns")) << "pre-rename saves must keep loading";
    EXPECT_EQ("gladiator", tmp.current_campaign)
        << "the header id folds onto the plain spelling";
    EXPECT_EQ(0u, tmp.current_levels.count("org.openglad.gladiator"))
        << "legacy keys must not survive the load";
    EXPECT_EQ(0u, tmp.completed_levels.count("org.openglad.gladiator"));
    ASSERT_TRUE(tmp.current_levels.count("gladiator") > 0);
    EXPECT_EQ(2, tmp.current_levels["gladiator"])
        << "the legacy entry's cursor lands on the plain key";
    EXPECT_TRUE(tmp.completed_levels["gladiator"].count(1) > 0)
        << "completed levels stored under the legacy key land on the plain key";
    EXPECT_TRUE(tmp.completed_levels["gladiator"].count(3) > 0);
}

// A file carrying BOTH spellings of one campaign (a pre-rename company later
// touched in memory by a post-rename build) merges conservatively onto the
// plain key: completed sets union, the cursor keeps the furthest level.
TEST(SaveDataVersions, save_data_load_merges_legacy_and_plain_progress_keys)
{
    SaveData src;
    src.current_campaign = "gladiator";
    src.scen_num = 2;
    src.current_levels.clear();
    src.completed_levels.clear();
    src.current_levels["org.openglad.gladiator"] = 7;
    src.current_levels["gladiator"] = 2;
    src.completed_levels["org.openglad.gladiator"] = {2, 5};
    src.completed_levels["gladiator"] = {1};
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("legacy_merge")));

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("legacy_merge")));
    EXPECT_EQ(0u, loaded.current_levels.count("org.openglad.gladiator"));
    EXPECT_EQ(0u, loaded.completed_levels.count("org.openglad.gladiator"));
    ASSERT_TRUE(loaded.current_levels.count("gladiator") > 0);
    EXPECT_EQ(7, loaded.current_levels["gladiator"])
        << "the merged cursor keeps the furthest level of the two spellings";
    const std::set<int>& done = loaded.completed_levels["gladiator"];
    EXPECT_TRUE(done.count(1) > 0) << "plain-key completions survive the merge";
    EXPECT_TRUE(done.count(2) > 0) << "legacy-key completions union in";
    EXPECT_TRUE(done.count(5) > 0);
}

// Re-saving a loaded legacy company writes plain ids only: the retired
// prefix never reaches disk again.
TEST(SaveDataVersions, save_data_resave_of_legacy_ids_writes_plain_bytes)
{
    write_save_file("legacy_rdns_resave",
                    /*version=*/14,
                    /*campaign_id=*/"org.openglad.gladiator",
                    /*scen_num=*/2,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    ASSERT_TRUE(tmp.load("legacy_rdns_resave"));
    ASSERT_TRUE(tmp.save("legacy_rdns_resave"));

    namespace fs = std::filesystem;
    const fs::path path =
        fs::path(get_user_path()) / "save" / "legacy_rdns_resave.gtl";
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    const std::string bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    EXPECT_EQ(std::string::npos, bytes.find("org.openglad"))
        << "writes always emit plain ids";
    EXPECT_NE(std::string::npos, bytes.find("gladiator"));
}

TEST(SaveDataVersions, save_data_update_guys_copies_only_live_entries_with_myguy)
{
    std::list<std::unique_ptr<walker>> oblist;

    auto live_with_guy = std::make_unique<walker>();
    live_with_guy->set_dead(0);
    live_with_guy->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    live_with_guy->myguy->exp = 50;

    auto dead_with_guy = std::make_unique<walker>();
    dead_with_guy->set_dead(1);
    dead_with_guy->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHER));

    auto live_no_guy = std::make_unique<walker>();
    live_no_guy->set_dead(0);
    live_no_guy->clear_myguy();

    oblist.push_back(std::move(live_with_guy));
    oblist.push_back(std::move(dead_with_guy));
    oblist.push_back(std::move(live_no_guy));

    SaveData data;
    data.update_guys(oblist);

    ASSERT_EQ(1, (int)data.team_size) << "update_guys should copy only live walkers with myguy";
    ASSERT_TRUE(data.team_list[0] != nullptr) << "copied guy should exist";
    if (data.team_list[0])
    {
        ASSERT_EQ((int)FAMILY_SOLDIER, (int)data.team_list[0]->family) << "copied guy should match source family";
    }
}


TEST(SaveDataVersions, save_data_unsupported_version_and_campaign_helper_branches)
{
    SDL_IOStream* out = open_write_file("save/", "unsupported_ver0.gtl");
    ASSERT_TRUE(out != nullptr) << "open_write_file unsupported version";
    if (!out)
        return;
    rw_write(out, "GTL", 3);
    unsigned char version0 = 0;
    rw_write_val(out, version0);
    SDL_CloseIO(out);

    SaveData tmp;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::UnsupportedVersion), static_cast<int>(tmp.load_with_error("unsupported_ver0"))) << "version 0 save should report UnsupportedVersion";

    tmp.completed_levels.clear();
    tmp.add_level_completed("test.campaign", 2);
    tmp.add_level_completed("test.campaign", 5);
    ASSERT_EQ(2, tmp.get_num_levels_completed("test.campaign")) << "helper should count completed levels for present campaign";
    ASSERT_EQ(0, tmp.get_num_levels_completed("missing.campaign")) << "helper should return 0 for missing campaign";
    tmp.reset_campaign("test.campaign");
    ASSERT_EQ(0, tmp.get_num_levels_completed("test.campaign")) << "reset_campaign should clear existing campaign progress";
}


TEST(SaveDataVersions, save_data_save_with_team_entry_and_wrapper_none_path)
{
    SaveData tmp;
    tmp.current_campaign = "gladiator";
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
    tmp.completed_levels["gladiator"].insert(1);
    tmp.completed_levels["gladiator"].insert(2);

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None), static_cast<int>(tmp.save_with_error("typed_save_with_team"))) << "save_with_error should succeed for one-team-entry save";

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None), static_cast<int>(loaded.load_with_error("typed_save_with_team"))) << "load_with_error wrapper should return None for saved file";
    ASSERT_EQ(1, (int)loaded.team_size) << "loaded team should contain one entry";
}


TEST(SaveDataVersions, save_data_round8_open_write_failure_and_is_level_completed_paths)
{
    SaveData data;
    data.current_campaign = "round8.campaign";
    data.completed_levels.clear();

    ASSERT_TRUE(!data.is_level_completed(3)) << "is_level_completed should be false when current campaign is absent";

    data.add_level_completed("round8.campaign", 3);
    ASSERT_TRUE(data.is_level_completed(3)) << "is_level_completed should be true after adding the level to current campaign";
    ASSERT_TRUE(!data.is_level_completed(99)) << "is_level_completed should be false for a non-completed level index";

    data.reset_campaign("round8.campaign");
    ASSERT_TRUE(!data.is_level_completed(3)) << "is_level_completed should become false after reset_campaign";

    const SaveDataIoError err = data.save_with_error("round8/missing_parent_path");
    ASSERT_EQ((int)SaveDataIoError::OpenWriteFailed, (int)err) << "save_with_error should report OpenWriteFailed when parent path is missing";
}


TEST(SaveDataVersions, save_data_round9_reset_campaign_missing_entry_is_noop)
{
    SaveData data;
    data.completed_levels.clear();
    data.current_campaign = "round9.none";

    // Missing campaign path should be a no-op.
    data.reset_campaign("round9.none");
    ASSERT_EQ(0, data.get_num_levels_completed("round9.none")) << "reset_campaign should keep missing campaign at zero levels";

    // Existing campaign path should clear only that campaign's levels.
    data.add_level_completed("round9.a", 1);
    data.add_level_completed("round9.a", 2);
    data.add_level_completed("round9.b", 3);
    data.reset_campaign("round9.a");

    ASSERT_EQ(0, data.get_num_levels_completed("round9.a")) << "reset_campaign should clear target campaign progress";
    ASSERT_EQ(1, data.get_num_levels_completed("round9.b")) << "reset_campaign should not clear other campaign progress";
}


// The earned-roads gate keys the frontier off the cursor, so a RESET must
// rewind it: a company parked past its wiped completions would find every
// road closed. The current campaign rewinds to its declared first_level;
// any other campaign just forgets its stored cursor (the next switch falls
// back to first_level through load_campaign).
TEST(SaveDataVersions, save_data_reset_campaign_rewinds_the_cursor)
{
    restore_default_campaigns();
    const std::string previous_mount = get_mounted_campaign();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    SaveData data;
    data.reset();
    data.current_campaign = "modes";
    data.scen_num = 815;
    data.current_levels["modes"] = 815;
    data.current_levels["gladiator"] = 12;
    data.add_level_completed("modes", 815);
    data.add_level_completed("gladiator", 12);

    // Resetting the CURRENT campaign rewinds to its campaign.yaml
    // first_level (modes declares 300).
    data.reset_campaign("modes");
    ASSERT_EQ(0, data.get_num_levels_completed("modes"));
    ASSERT_EQ(300, (int)data.scen_num)
        << "resetting the current campaign must rewind the live cursor";
    ASSERT_EQ(300, data.current_levels.at("modes"));

    // Resetting another campaign leaves the live cursor alone and erases
    // only that campaign's stored cursor.
    data.reset_campaign("gladiator");
    ASSERT_EQ(300, (int)data.scen_num)
        << "resetting another campaign must not move the live cursor";
    ASSERT_EQ(0u, data.current_levels.count("gladiator"))
        << "the stored cursor is forgotten so the next switch re-enters at "
           "first_level";

    (void)unmount_campaign_package_with_error("modes");
    if (!previous_mount.empty() && previous_mount != "modes")
        (void)mount_campaign_package_with_error(previous_mount);
}


// --- GTL v14: reserved-block embedding (docs/company-basecamp-design.md §3.1) ---

// Reads an entire "save/<name>" file into a byte vector for raw offset checks.
static std::vector<uint8_t> read_save_bytes(const char* fname)
{
    std::vector<uint8_t> bytes;
    SDL_IOStream* in = open_read_file("save/", fname);
    if (in == nullptr)
        return bytes;
    const Sint64 size = SDL_GetIOSize(in);
    if (size > 0) {
        bytes.resize(static_cast<size_t>(size));
        if (SDL_ReadIO(in, bytes.data(), bytes.size()) != bytes.size())
            bytes.clear();
    }
    SDL_CloseIO(in);
    return bytes;
}

static void rewrite_save_bytes(const char* fname,
                               const std::vector<uint8_t>& bytes)
{
    SDL_IOStream* out = open_write_file("save/", fname);
    ASSERT_TRUE(out != nullptr) << fname;
    ASSERT_EQ(bytes.size(), SDL_WriteIO(out, bytes.data(), bytes.size()))
        << fname;
    ASSERT_TRUE(SDL_CloseIO(out)) << fname;
}

TEST(SaveDataVersions,
     save_data_v14_rejects_invalid_campaign_collection_boundaries)
{
    constexpr std::size_t kCampaignCountOffset = 164;
    constexpr std::size_t kFirstCampaignOffset = 166;
    constexpr std::size_t kFirstLevelCountOffset = 208;

    SaveData seed;
    seed.save_name = "BOUNDARY SEED";
    seed.current_campaign = "gladiator";
    seed.team_size = 0;
    seed.completed_levels.clear();
    seed.current_levels.clear();
    seed.completed_levels["gladiator"] = {1, 3};
    seed.current_levels["gladiator"] = 2;
    ASSERT_EQ(SaveDataIoError::None,
              seed.save_with_error("typed_save_v14_boundary_seed"));

    const std::vector<uint8_t> canonical =
        read_save_bytes("typed_save_v14_boundary_seed.gtl");
    ASSERT_GT(canonical.size(), kFirstLevelCountOffset + sizeof(std::int16_t));
    ASSERT_EQ(16, static_cast<int>(canonical[3]));

    const auto expect_read_failure =
        [&](const char* slot, std::vector<uint8_t> bytes) {
            rewrite_save_bytes((std::string(slot) + ".gtl").c_str(), bytes);
            SaveData loaded;
            EXPECT_EQ(SaveDataIoError::ReadFailed,
                      loaded.load_with_error(slot))
                << slot;
            EXPECT_EQ(SaveDataIoError::ReadFailed, loaded.last_io_error())
                << slot;
        };

    {
        std::vector<uint8_t> bytes = canonical;
        const std::int16_t invalid_campaign_count = 129;
        std::memcpy(bytes.data() + kCampaignCountOffset,
                    &invalid_campaign_count, sizeof(invalid_campaign_count));
        expect_read_failure("typed_save_v14_too_many_campaigns",
                            std::move(bytes));
    }

    {
        std::vector<uint8_t> bytes = canonical;
        std::fill_n(bytes.begin() +
                        static_cast<std::ptrdiff_t>(kFirstCampaignOffset),
                    40, uint8_t{0});
        constexpr std::string_view unsafe_id = "../outside";
        std::copy(unsafe_id.begin(), unsafe_id.end(),
                  bytes.begin() +
                      static_cast<std::ptrdiff_t>(kFirstCampaignOffset));
        expect_read_failure("typed_save_v14_unsafe_stored_campaign",
                            std::move(bytes));
    }

    {
        std::vector<uint8_t> bytes = canonical;
        const std::int16_t invalid_level_count = 1001;
        std::memcpy(bytes.data() + kFirstLevelCountOffset,
                    &invalid_level_count, sizeof(invalid_level_count));
        expect_read_failure("typed_save_v14_too_many_levels",
                            std::move(bytes));
    }
}

TEST(SaveDataVersions,
     save_data_v14_unsafe_header_campaign_falls_back_to_default)
{
    constexpr std::size_t kHeaderCampaignOffset = 46;
    constexpr std::size_t kHeaderCampaignWidth = 40;

    SaveData seed;
    seed.save_name = "HEADER FALLBACK";
    seed.current_campaign = "gladiator";
    seed.scen_num = 2;
    seed.team_size = 0;
    seed.completed_levels.clear();
    seed.current_levels.clear();
    seed.completed_levels["gladiator"] = {2};
    seed.current_levels["gladiator"] = 2;
    ASSERT_EQ(SaveDataIoError::None,
              seed.save_with_error("typed_save_v14_header_fallback"));

    std::vector<uint8_t> bytes =
        read_save_bytes("typed_save_v14_header_fallback.gtl");
    ASSERT_GE(bytes.size(),
              kHeaderCampaignOffset + kHeaderCampaignWidth);
    std::fill_n(bytes.begin() +
                    static_cast<std::ptrdiff_t>(kHeaderCampaignOffset),
                kHeaderCampaignWidth, uint8_t{0});
    constexpr std::string_view unsafe_header_id = "../outside";
    std::copy(unsafe_header_id.begin(), unsafe_header_id.end(),
              bytes.begin() +
                  static_cast<std::ptrdiff_t>(kHeaderCampaignOffset));
    rewrite_save_bytes("typed_save_v14_header_fallback.gtl", bytes);

    SaveData loaded;
    ASSERT_EQ(SaveDataIoError::None,
              loaded.load_with_error("typed_save_v14_header_fallback"));
    EXPECT_EQ("gladiator", loaded.current_campaign);
    EXPECT_EQ(2, loaded.current_levels.at("gladiator"));
    EXPECT_TRUE(
        loaded.completed_levels.at("gladiator").contains(2));
}

TEST(SaveDataVersions, save_data_v14_writer_rejects_unsafe_campaign_ids)
{
    SaveData unsafe_current;
    unsafe_current.current_campaign = "../outside";
    EXPECT_EQ(SaveDataIoError::WriteFailed,
              unsafe_current.save_with_error(
                  "typed_save_v14_unsafe_current_campaign"));
    EXPECT_EQ(SaveDataIoError::WriteFailed, unsafe_current.last_io_error());

    SaveData unsafe_progress;
    unsafe_progress.current_campaign = "gladiator";
    unsafe_progress.completed_levels.clear();
    unsafe_progress.current_levels.clear();
    unsafe_progress.completed_levels["../outside"] = {1};
    EXPECT_EQ(SaveDataIoError::WriteFailed,
              unsafe_progress.save_with_error(
                  "typed_save_v14_unsafe_progress_campaign"));
    EXPECT_EQ(SaveDataIoError::WriteFailed, unsafe_progress.last_io_error());
}

TEST(SaveDataVersions,
     save_data_v14_writer_rejects_oversized_collections_before_count_fields)
{
    namespace fs = std::filesystem;
    constexpr std::size_t kCampaignCountOffset = 164;
    constexpr std::size_t kFirstLevelCountOffset = 208;

    const auto expect_partial_header =
        [](const fs::path& path, std::size_t expected_size) {
            std::error_code ec;
            ASSERT_TRUE(fs::is_regular_file(path, ec));
            ASSERT_FALSE(ec);
            ASSERT_EQ(expected_size, fs::file_size(path, ec));
            ASSERT_FALSE(ec);

            std::ifstream in(path, std::ios::binary);
            ASSERT_TRUE(in.good());
            std::array<unsigned char, 4> header{};
            in.read(reinterpret_cast<char*>(header.data()),
                    static_cast<std::streamsize>(header.size()));
            ASSERT_TRUE(in.good());
            EXPECT_EQ((std::array<unsigned char, 4>{'G', 'T', 'L', 16}),
                      header);
        };

    {
        constexpr const char* slot = "typed_save_too_many_campaigns";
        const fs::path path =
            fs::path(get_user_path()) / "save" / (std::string(slot) + ".gtl");
        og::test::ScopedPhysicalFileState file_state(path);
        ASSERT_TRUE(file_state.ready()) << file_state.error().message();

        SaveData data;
        data.current_campaign = "gladiator";
        data.completed_levels.clear();
        data.current_levels.clear();
        for (int i = 0; i < 32768; ++i)
        {
            data.completed_levels.emplace(
                "campaign." + std::to_string(i), std::set<int>{});
        }

        EXPECT_EQ(SaveDataIoError::WriteFailed, data.save_with_error(slot));
        EXPECT_EQ(SaveDataIoError::WriteFailed, data.last_io_error());
        expect_partial_header(path, kCampaignCountOffset);
    }

    {
        constexpr const char* slot = "typed_save_too_many_levels";
        const fs::path path =
            fs::path(get_user_path()) / "save" / (std::string(slot) + ".gtl");
        og::test::ScopedPhysicalFileState file_state(path);
        ASSERT_TRUE(file_state.ready()) << file_state.error().message();

        SaveData data;
        data.current_campaign = "gladiator";
        data.completed_levels.clear();
        data.current_levels.clear();
        std::set<int>& levels =
            data.completed_levels["gladiator"];
        for (int i = 0; i < 32768; ++i)
            levels.insert(i);
        data.current_levels["gladiator"] = 1;

        EXPECT_EQ(SaveDataIoError::WriteFailed, data.save_with_error(slot));
        EXPECT_EQ(SaveDataIoError::WriteFailed, data.last_io_error());
        expect_partial_header(path, kFirstLevelCountOffset);
    }
}

TEST(SaveDataVersions, save_data_v14_writer_retires_company_player_count)
{
    SaveData src;
    src.current_campaign = "gladiator";
    src.save_name = "COUNT-INDEPENDENT";
    src.last_played_unix_s = 123456789;

    std::vector<uint8_t> canonical_bytes;
    for (const unsigned char live_count :
         std::array<unsigned char, 5>{0, 1, 2, 3, 4}) {
        SCOPED_TRACE("live player count " + std::to_string(live_count));
        src.numplayers = live_count;
        const std::string slot =
            "typed_save_retired_player_count_" + std::to_string(live_count);

        ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
                  static_cast<int>(src.save_with_error(slot)));
        const std::vector<uint8_t> bytes =
            read_save_bytes((slot + ".gtl").c_str());
        ASSERT_GT(bytes.size(), 132u) << "complete GTL header expected";
        EXPECT_EQ(16, static_cast<int>(bytes[3]))
            << "retiring the field does not change the GTL layout version";
        EXPECT_EQ(1, static_cast<int>(bytes[132]))
            << "old readers still need the canonical one-player marker";

        if (canonical_bytes.empty())
            canonical_bytes = bytes;
        else
            EXPECT_EQ(canonical_bytes, bytes)
                << "transient player count must not leak anywhere in a "
                   "company file";
    }
}

TEST(SaveDataVersions, save_data_load_does_not_adopt_legacy_player_count)
{
    // Every value old writers could legally place at offset 132 remains
    // readable, but none becomes the live count. Normal builds preserve the
    // receiver; touch-only builds force their sole supported player count.
    for (const unsigned char legacy_count :
         std::array<unsigned char, 5>{0, 1, 2, 3, 4}) {
        SCOPED_TRACE("legacy player count " +
                     std::to_string(legacy_count));
        const std::string slot =
            "ver14_legacy_player_count_" + std::to_string(legacy_count);
        write_save_file(slot,
                        /*version=*/14,
                        /*campaign_id=*/"gladiator",
                        /*scen_num=*/1,
                        /*cash=*/100,
                        /*score=*/200,
                        /*allied_mode=*/1,
                        /*numplayers=*/legacy_count,
                        /*guys=*/nullptr,
                        /*listsize=*/0,
                        /*use_v8plus_campaigns=*/true,
                        /*v5plus_levelstatus=*/true,
                        /*levelstatus_500=*/nullptr,
                        /*levelstatus_200=*/nullptr);

        SaveData loaded;
        const unsigned char live_count =
            static_cast<unsigned char>((legacy_count + 2) % 5);
        loaded.numplayers = live_count;
        ASSERT_TRUE(loaded.load(slot));
#ifdef USE_TOUCH_INPUT
        EXPECT_EQ(1, static_cast<int>(loaded.numplayers))
            << "touch-only builds force their single supported live player";
#else
        EXPECT_EQ(static_cast<int>(live_count),
                  static_cast<int>(loaded.numplayers))
            << "company loading must preserve the runtime seat projection";
#endif
    }
}

TEST(SaveDataVersions, save_data_v14_roundtrip_preserves_timestamp_and_deploy_flags)
{
    SaveData src;
    src.current_campaign = "gladiator";
    // Distinct byte pattern in every one of the 8 timestamp bytes.
    src.last_played_unix_s = 0x1122334455667788LL;
    src.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    src.team_list[0]->name = "BROUGHT";
    src.team_list[0]->exp = 1000;
    src.team_list[0]->deployed = true;
    src.team_list[1] = std::make_unique<guy>(FAMILY_ELF);
    src.team_list[1]->name = "HELDBACK";
    src.team_list[1]->exp = 2000;
    src.team_list[1]->deployed = false;
    src.team_size = 2;

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_v14_roundtrip")))
        << "v14 writer should succeed";

    // RAW offset spot-checks (§3.1 fixed offsets):
    //   3        version byte = 16
    //   133..140 last_played_unix_s (host-endian i64)
    //   141..163 reserved, zero-filled
    //   164      first guy record; guy+50 deployed flag, guy+51 campaign
    //            tag (v16), guy+52..57 zero
    const std::vector<uint8_t> bytes = read_save_bytes("typed_save_v14_roundtrip.gtl");
    constexpr size_t kHeaderSize = 164;
    constexpr size_t kGuySize = 58;
    ASSERT_GE(bytes.size(), kHeaderSize + 2 * kGuySize) << "file too small";
    ASSERT_EQ(16, (int)bytes[3]) << "version byte at offset 3";

    std::int64_t raw_ts = 0;
    std::memcpy(&raw_ts, bytes.data() + 133, 8);
    ASSERT_EQ(0x1122334455667788LL, raw_ts)
        << "timestamp embedded at header offset 133";
    for (size_t i = 141; i < kHeaderSize; ++i)
        ASSERT_EQ(0, (int)bytes[i]) << "header reserved byte " << i
                                    << " must be zero-filled";

    const size_t guy0 = kHeaderSize;
    const size_t guy1 = kHeaderSize + kGuySize;
    ASSERT_EQ(1, (int)bytes[guy0 + 50]) << "deployed flag at guy+50";
    ASSERT_EQ(0, (int)bytes[guy1 + 50]) << "held-back flag at guy+50";
    // Unassigned characters write tag 0 at guy+51 (GTL v16), so the whole
    // former reserved range still reads zero here.
    for (size_t i = 51; i < kGuySize; ++i) {
        ASSERT_EQ(0, (int)bytes[guy0 + i]) << "guy0 reserved byte " << i;
        ASSERT_EQ(0, (int)bytes[guy1 + i]) << "guy1 reserved byte " << i;
    }

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_v14_roundtrip")))
        << "v14 reader should succeed";
    ASSERT_EQ(0x1122334455667788LL, loaded.last_played_unix_s)
        << "last_played_unix_s should roundtrip";
    ASSERT_EQ(2, (int)loaded.team_size);
    ASSERT_TRUE(loaded.team_list[0]->deployed) << "deployed=true roundtrips";
    ASSERT_FALSE(loaded.team_list[1]->deployed) << "deployed=false roundtrips";
    ASSERT_TRUE(loaded.team_list[1]->name == "HELDBACK");
}

TEST(SaveDataVersions, save_data_load_v14_reads_timestamp_and_deployed)
{
    GuyRecord guys[2];
    guys[0].name = "SENT";
    guys[0].deployed = 1;
    guys[1].name = "HELD";
    guys[1].deployed = 0;
    write_save_file("ver14_company",
                    /*version=*/14,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/guys,
                    /*listsize=*/2,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/2,
                    /*ctf_capture_limit=*/0,
                    /*ctf_respawn_ticks=*/0,
                    /*ctf_strip_scenario_troops=*/0,
                    /*respawn_mode=*/0,
                    /*generator_rate=*/0,
                    /*keep_fallen_heroes=*/0,
                    /*tower_best_floor=*/0,
                    /*tower_run_seed=*/0,
                    /*last_played_unix_s=*/987654321LL);

    SaveData tmp;
    ASSERT_TRUE(tmp.load("ver14_company")) << "v14 load should succeed";
    ASSERT_EQ(static_cast<std::int64_t>(987654321LL), tmp.last_played_unix_s)
        << "v14 should read last_played_unix_s from offset 133";
    ASSERT_EQ(2, (int)tmp.team_size);
    ASSERT_TRUE(tmp.team_list[0]->deployed) << "v14 should read deployed=1";
    ASSERT_FALSE(tmp.team_list[1]->deployed) << "v14 should read deployed=0";
}

TEST(SaveDataVersions, save_data_load_v13_payload_defaults_v14_fields)
{
    GuyRecord guys[1];
    guys[0].name = "OLDGUY";
    // v13 files carry 'GTL' filler / arbitrary bytes in the reserved ranges;
    // the deployed member of GuyRecord is IGNORED for version < 14.
    guys[0].deployed = 0;
    write_save_file("ver13_no_company",
                    /*version=*/13,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/guys,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    // Poison the in-memory fields: a v13 payload must restore the defaults.
    tmp.last_played_unix_s = 999;
    ASSERT_TRUE(tmp.load("ver13_no_company")) << "v13 load should succeed";
    ASSERT_EQ(static_cast<std::int64_t>(0), tmp.last_played_unix_s)
        << "v13 saves default last_played_unix_s to 0 (never sniffed)";
    ASSERT_EQ(1, (int)tmp.team_size);
    ASSERT_TRUE(tmp.team_list[0]->deployed)
        << "v13 saves default deployed to true (everyone was always brought)";
}

TEST(SaveDataVersions, save_data_v13_shaped_reader_tolerance_proxy)
{
    // §3.9 v13-binary-reads-v14 proxy: a v14 file re-labeled as v13 must load
    // perfectly with the reserved ranges skipped (same byte counts consumed,
    // no new tail), exactly as a v13 binary reads a v14 file. The v14-only
    // fields fall back to their defaults.
    SaveData src;
    src.current_campaign = "gladiator";
    src.totalcash = 4321;
    src.last_played_unix_s = 0x0102030405060708LL;
    src.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    src.team_list[0]->name = "PROXY";
    src.team_list[0]->exp = 555;
    src.team_list[0]->deployed = false;
    src.team_size = 1;

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_v14_as_v13")))
        << "v14 writer should succeed";

    std::vector<uint8_t> bytes = read_save_bytes("typed_save_v14_as_v13.gtl");
    ASSERT_GT(bytes.size(), (size_t)164) << "readable v14 file expected";
    ASSERT_EQ(16, (int)bytes[3]);
    bytes[3] = 13; // re-label: v13-shaped reader sees reserved filler

    SDL_IOStream* out = open_write_file("save/", "typed_save_v14_as_v13.gtl");
    ASSERT_TRUE(out != nullptr) << "rewrite of re-labeled file";
    ASSERT_EQ(bytes.size(), SDL_WriteIO(out, bytes.data(), bytes.size()));
    SDL_CloseIO(out);

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_v14_as_v13")))
        << "v13-shaped read of a v14 payload must succeed (tolerant reader)";
    ASSERT_EQ(4321u, loaded.totalcash) << "non-reserved fields intact";
    ASSERT_EQ(1, (int)loaded.team_size);
    ASSERT_TRUE(loaded.team_list[0]->name == "PROXY");
    ASSERT_EQ(555u, loaded.team_list[0]->exp);
    ASSERT_EQ(static_cast<std::int64_t>(0), loaded.last_played_unix_s)
        << "a v13-shaped read drops the timestamp (default restored)";
    ASSERT_TRUE(loaded.team_list[0]->deployed)
        << "a v13-shaped read drops the deploy flag (default true)";
}

TEST(SaveDataVersions, save_data_load_v7_ladder_defaults_v14_fields)
{
    // [SAVE-R1] the v<8 prefix ladder has no campaign id and uses the flat
    // levelstatus tail; the v14 fields must still default cleanly and the
    // version-gated sequence must keep every older field at its correct
    // position (allied mode + per-team scores prove the ladder held).
    GuyRecord guys[1];
    guys[0].name = "VETERAN";
    write_save_file("ver7_ladder_company",
                    /*version=*/7,
                    /*campaign_id=*/"gladiator", // unused < v8
                    /*scen_num=*/5,
                    /*cash=*/300,
                    /*score=*/400,
                    /*allied_mode=*/1,
                    /*numplayers=*/2,
                    /*guys=*/guys,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/false,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData tmp;
    tmp.numplayers = 4;
    tmp.last_played_unix_s = 777; // must be reset by the load
    ASSERT_TRUE(tmp.load("ver7_ladder_company")) << "v7 load should succeed";
    ASSERT_EQ(5, (int)tmp.scen_num) << "scen_num at the v7 ladder position";
    ASSERT_EQ(300u, tmp.totalcash);
    ASSERT_EQ(1, (int)tmp.allied_mode) << "v7 allied field read";
    ASSERT_EQ(4, (int)tmp.numplayers)
        << "the historical player-count byte no longer owns session state";
    ASSERT_EQ(static_cast<std::int64_t>(0), tmp.last_played_unix_s)
        << "v7 saves default last_played_unix_s to 0";
    ASSERT_EQ(1, (int)tmp.team_size);
    ASSERT_TRUE(tmp.team_list[0]->deployed)
        << "v7 saves default deployed to true";
}

TEST(SaveDataVersions, save_data_v13_gtl_filler_first_autosave_upgrades_in_place)
{
    // §3.9 direction 1 (v14 binary reads v13): a REAL v13 file carries
    // nonzero 'GTLGTL…' filler in BOTH reserved ranges (the master writer's
    // 50-byte filler array); the v14 fields are hard-gated on the version
    // byte, so that filler must never be sniffed. The first company_autosave
    // then upgrades the file to v14 in place (the silent-upgrade precedent).
    GuyRecord guys[1];
    guys[0].name = "UPGRADED";
    guys[0].exp = 4242;
    write_save_file("ver13_upgrade_company",
                    /*version=*/13,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/2,
                    /*cash=*/500,
                    /*score=*/600,
                    /*allied_mode=*/0,
                    /*numplayers=*/1,
                    /*guys=*/guys,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    // Patch both reserved ranges with the real master-writer filler bytes.
    std::vector<uint8_t> bytes = read_save_bytes("ver13_upgrade_company.gtl");
    constexpr size_t kHeaderSize = 164;
    constexpr size_t kGuySize = 58;
    ASSERT_GE(bytes.size(), kHeaderSize + kGuySize) << "readable v13 file";
    ASSERT_EQ(13, (int)bytes[3]);
    static const char kGtl[3] = {'G', 'T', 'L'};
    for (size_t i = 133; i < kHeaderSize; ++i)
        bytes[i] = static_cast<uint8_t>(kGtl[(i - 133) % 3]);
    for (size_t i = 50; i < kGuySize; ++i)
        bytes[kHeaderSize + i] = static_cast<uint8_t>(kGtl[(i - 50) % 3]);
    SDL_IOStream* out = open_write_file("save/", "ver13_upgrade_company.gtl");
    ASSERT_TRUE(out != nullptr) << "rewrite of GTL-filler v13 file";
    ASSERT_EQ(bytes.size(), SDL_WriteIO(out, bytes.data(), bytes.size()));
    SDL_CloseIO(out);

    SaveData loaded;
    loaded.last_played_unix_s = 424242; // must be defaulted, never sniffed
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("ver13_upgrade_company")))
        << "a v13 file with real GTL filler must load clean";
    ASSERT_EQ(static_cast<std::int64_t>(0), loaded.last_played_unix_s)
        << "the 'GTL' bytes at offset 133 must never be read as a timestamp";
    ASSERT_EQ(1, (int)loaded.team_size);
    ASSERT_TRUE(loaded.team_list[0]->deployed)
        << "the 'G' byte at guy+50 must never be read as a deploy flag";
    ASSERT_EQ(500u, loaded.totalcash);

    // First autosave upgrades in place: same slot, now v14 with a stamped
    // timestamp — and NO backup snapshot for a non-LevelWin kind.
    {
        og::data::ScopedActiveCompany scope("ver13_upgrade_company");
        og::data::set_company_clock_for_tests(1700000123LL);
        const SaveDataIoError io = og::data::company_autosave(
            loaded, og::data::CompanyAutosaveKind::BaseCampMutation);
        og::data::set_company_clock_for_tests(std::nullopt);
        ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
                  static_cast<int>(io)) << "upgrade autosave should succeed";
    }

    const std::vector<uint8_t> upgraded =
        read_save_bytes("ver13_upgrade_company.gtl");
    ASSERT_GE(upgraded.size(), kHeaderSize + kGuySize);
    ASSERT_EQ(16, (int)upgraded[3]) << "in-place upgrade rewrites version 16";
    std::int64_t raw_ts = 0;
    std::memcpy(&raw_ts, upgraded.data() + 133, 8);
    ASSERT_EQ(1700000123LL, raw_ts) << "upgrade stamps the pinned clock";
    for (size_t i = 141; i < kHeaderSize; ++i)
        ASSERT_EQ(0, (int)upgraded[i])
            << "upgrade zero-fills header reserved byte " << i;
    ASSERT_EQ(1, (int)upgraded[kHeaderSize + 50])
        << "default deployed=true serialized at guy+50 after the upgrade";
    for (size_t i = 51; i < kGuySize; ++i)
        ASSERT_EQ(0, (int)upgraded[kHeaderSize + i])
            << "upgrade zero-fills guy reserved byte " << i;

    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header("ver13_upgrade_company");
    ASSERT_TRUE(header.has_value() && header->valid)
        << "upgraded file must header-scan clean";
    ASSERT_EQ(16, (int)header->version);
    ASSERT_EQ(1700000123LL, header->last_played_unix_s);
    ASSERT_TRUE(og::data::list_company_backups("ver13_upgrade_company").empty())
        << "a mutation autosave must not snapshot a backup";
}

TEST(SaveDataVersions, save_data_v13_resave_of_v14_drops_company_fields)
{
    // §3.9 direction 2, second half: a v13 binary reads a v14 file
    // tolerantly, then re-SAVES it — the v13 writer has no timestamp/deploy
    // members, so the round trip drops both; the next v14 read restores the
    // defaults while every other field survives.
    SaveData src;
    src.current_campaign = "gladiator";
    src.totalcash = 7777;
    src.last_played_unix_s = 55555555LL;
    src.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    src.team_list[0]->name = "ROUNDTRIP";
    src.team_list[0]->exp = 9999;
    src.team_list[0]->deployed = false; // held back before the detour
    src.team_size = 1;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_v13_detour")))
        << "v14 writer should succeed";

    // The v13 binary's tolerant view of the file: same logical content, no
    // company members. Its re-save writes the v13 shape over the same slot.
    SaveData detour;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(detour.load_with_error("typed_save_v13_detour")));
    GuyRecord guys[1];
    guys[0].name = "ROUNDTRIP";
    guys[0].exp = 9999;
    guys[0].deployed = 0; // ignored below v14: the v13 writer can't know it
    write_save_file("typed_save_v13_detour",
                    /*version=*/13,
                    /*campaign_id=*/detour.current_campaign,
                    /*scen_num=*/detour.scen_num,
                    /*cash=*/detour.totalcash,
                    /*score=*/detour.totalscore,
                    /*allied_mode=*/detour.allied_mode,
                    /*numplayers=*/detour.numplayers,
                    /*guys=*/guys,
                    /*listsize=*/1,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr);

    SaveData rounded;
    rounded.last_played_unix_s = 121212; // must be defaulted by the load
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(rounded.load_with_error("typed_save_v13_detour")))
        << "v14 reader should accept the v13 re-save";
    ASSERT_EQ(static_cast<std::int64_t>(0), rounded.last_played_unix_s)
        << "the v13 re-save dropped the timestamp (default 0 restored)";
    ASSERT_EQ(1, (int)rounded.team_size);
    ASSERT_TRUE(rounded.team_list[0]->deployed)
        << "the v13 re-save dropped the deploy flag (default true restored)";
    ASSERT_TRUE(rounded.team_list[0]->name == "ROUNDTRIP");
    ASSERT_EQ(9999u, rounded.team_list[0]->exp);
    ASSERT_EQ(7777u, rounded.totalcash) << "non-company fields intact";
}


// --- GTL v15: campaign scripted state (docs/campaign-scripting-design.md,
// "Persistent campaign state") ---

TEST(SaveDataVersions, save_data_v15_roundtrip_preserves_campaign_state)
{
    SaveData src;
    src.current_campaign = "gladiator";
    // Insert in deliberately unsorted order: the entry lists must come out
    // sorted by key (the deterministic-serialization invariant).
    ASSERT_TRUE(src.campaign_state_set("gladiator", "watch_paid", 3));
    ASSERT_TRUE(src.campaign_state_set("gladiator", "advance_debt", -60));
    ASSERT_TRUE(src.campaign_state_set("gladiator", "delve_counted", 0));
    ASSERT_TRUE(src.campaign_state_set("other_campaign", "zz_key", 7));

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_v15_roundtrip")))
        << "v15 writer should succeed";

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_v15_roundtrip")))
        << "v15 reader should succeed";
    ASSERT_EQ(3, loaded.campaign_state_get("gladiator", "watch_paid"))
        << "campaign state value should roundtrip";
    ASSERT_EQ(-60, loaded.campaign_state_get("gladiator", "advance_debt"))
        << "negative values should roundtrip";
    ASSERT_EQ(0, loaded.campaign_state_get("gladiator", "delve_counted"));
    ASSERT_EQ(7, loaded.campaign_state_get("other_campaign", "zz_key"))
        << "a second campaign's state should roundtrip";
    ASSERT_EQ(0, loaded.campaign_state_get("gladiator", "missing_key"))
        << "absent keys read 0";

    // The explicitly-set-to-0 entry KEEPS its slot through the roundtrip
    // (presence never depends on value history).
    const auto& entries = loaded.campaign_state.at("gladiator");
    ASSERT_EQ(3u, entries.size()) << "all three entries persisted";
    ASSERT_EQ("advance_debt", entries[0].first) << "entries sorted by key";
    ASSERT_EQ("delve_counted", entries[1].first) << "entries sorted by key";
    ASSERT_EQ("watch_paid", entries[2].first) << "entries sorted by key";

    // Deterministic files: a load-then-resave is byte-identical.
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.save_with_error("typed_save_v15_resave")));
    const std::vector<uint8_t> first =
        read_save_bytes("typed_save_v15_roundtrip.gtl");
    const std::vector<uint8_t> second =
        read_save_bytes("typed_save_v15_resave.gtl");
    ASSERT_FALSE(first.empty());
    ASSERT_EQ(16, (int)first[3]) << "version byte at offset 3";
    ASSERT_EQ(first, second)
        << "sorted-by-key storage must make a re-save byte-identical";
}

TEST(SaveDataVersions, save_data_load_v14_payload_defaults_campaign_state)
{
    write_save_file("ver14_no_campaign_state",
                    /*version=*/14,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/nullptr,
                    /*listsize=*/0,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/2,
                    /*ctf_capture_limit=*/0,
                    /*ctf_respawn_ticks=*/0,
                    /*ctf_strip_scenario_troops=*/0,
                    /*respawn_mode=*/0,
                    /*generator_rate=*/0,
                    /*keep_fallen_heroes=*/0,
                    /*tower_best_floor=*/0,
                    /*tower_run_seed=*/0,
                    /*last_played_unix_s=*/42LL);

    SaveData tmp;
    // Poison the in-memory field: a v14 payload must leave it empty.
    ASSERT_TRUE(tmp.campaign_state_set("gladiator", "stale_key", 9));
    ASSERT_TRUE(tmp.load("ver14_no_campaign_state")) << "v14 load should succeed";
    ASSERT_TRUE(tmp.campaign_state.empty())
        << "a v14 file (no campaign-state block) loads with empty state";
    ASSERT_EQ(0, tmp.campaign_state_get("gladiator", "stale_key"));
}

// Byte-level bounds rejections on the v15 tail. The state block is the
// LAST block in the file, so its offsets are computed from the file end:
// seed state = one campaign ("gladiator") with entries "alpha"(1) and
// "beta"(2) -> block = 2 (count) + 40 (id) + 2 (entries) + (1+5+4) +
// (1+4+4) = 63 bytes.
TEST(SaveDataVersions, save_data_v15_rejects_invalid_campaign_state_boundaries)
{
    SaveData seed;
    seed.save_name = "STATE BOUNDARY SEED";
    seed.current_campaign = "gladiator";
    ASSERT_TRUE(seed.campaign_state_set("gladiator", "alpha", 1));
    ASSERT_TRUE(seed.campaign_state_set("gladiator", "beta", 2));
    ASSERT_EQ(SaveDataIoError::None,
              seed.save_with_error("typed_save_v15_boundary_seed"));

    const std::vector<uint8_t> canonical =
        read_save_bytes("typed_save_v15_boundary_seed.gtl");
    constexpr std::size_t kStateBlockSize = 2 + 40 + 2 + (1 + 5 + 4) + (1 + 4 + 4);
    ASSERT_GT(canonical.size(), kStateBlockSize);
    ASSERT_EQ(16, static_cast<int>(canonical[3]));
    const std::size_t state_start = canonical.size() - kStateBlockSize;

    // Verify the computed offset really is the block start (campaign count
    // == 1) before mutating around it.
    std::int16_t stored_count = 0;
    std::memcpy(&stored_count, canonical.data() + state_start, 2);
    ASSERT_EQ(1, stored_count) << "state block offset arithmetic";

    const auto expect_read_failure =
        [&](const char* slot, std::vector<uint8_t> bytes) {
            rewrite_save_bytes((std::string(slot) + ".gtl").c_str(), bytes);
            SaveData loaded;
            EXPECT_EQ(SaveDataIoError::ReadFailed,
                      loaded.load_with_error(slot))
                << slot;
            EXPECT_EQ(SaveDataIoError::ReadFailed, loaded.last_io_error())
                << slot;
        };

    {
        // >128 campaigns with state.
        std::vector<uint8_t> bytes = canonical;
        const std::int16_t invalid_count = 129;
        std::memcpy(bytes.data() + state_start, &invalid_count, 2);
        expect_read_failure("typed_save_v15_too_many_state_campaigns",
                            std::move(bytes));
    }

    {
        // Unsafe campaign id inside the state block.
        std::vector<uint8_t> bytes = canonical;
        std::fill_n(bytes.begin() +
                        static_cast<std::ptrdiff_t>(state_start + 2),
                    40, uint8_t{0});
        constexpr std::string_view unsafe_id = "../outside";
        std::copy(unsafe_id.begin(), unsafe_id.end(),
                  bytes.begin() +
                      static_cast<std::ptrdiff_t>(state_start + 2));
        expect_read_failure("typed_save_v15_unsafe_state_campaign",
                            std::move(bytes));
    }

    {
        // >128 entries in one campaign.
        std::vector<uint8_t> bytes = canonical;
        const std::int16_t invalid_entries = 129;
        std::memcpy(bytes.data() + state_start + 42, &invalid_entries, 2);
        expect_read_failure("typed_save_v15_too_many_state_entries",
                            std::move(bytes));
    }

    {
        // Oversized key length (33 > kCampaignVarNameMax).
        std::vector<uint8_t> bytes = canonical;
        bytes[state_start + 44] = 33;
        expect_read_failure("typed_save_v15_oversized_state_key",
                            std::move(bytes));
    }

    {
        // Key charset violation ('A' is outside [a-z0-9_]).
        std::vector<uint8_t> bytes = canonical;
        bytes[state_start + 45] = static_cast<uint8_t>('A');
        expect_read_failure("typed_save_v15_bad_charset_state_key",
                            std::move(bytes));
    }
}

TEST(SaveDataVersions, save_data_campaign_state_set_refuses_without_mutating)
{
    SaveData data;

    // Bad charset (uppercase, dash, space) and empty keys.
    ASSERT_FALSE(data.campaign_state_set("gladiator", "BadKey", 1));
    ASSERT_FALSE(data.campaign_state_set("gladiator", "with-dash", 1));
    ASSERT_FALSE(data.campaign_state_set("gladiator", "with space", 1));
    ASSERT_FALSE(data.campaign_state_set("gladiator", "", 1));
    ASSERT_TRUE(data.campaign_state.empty())
        << "a refused set must not create the campaign's entry list";

    // A campaign id longer than the serialized 40-byte field would silently
    // truncate on disk and detach the state from its campaign on reload —
    // the write choke refuses it with no mutation. The 40-char boundary id
    // is accepted (on a scratch SaveData so the cap arithmetic below stays
    // exact).
    ASSERT_FALSE(data.campaign_state_set(std::string(41, 'c'), "seed", 1));
    ASSERT_TRUE(data.campaign_state.empty())
        << "a refused campaign id must not create an entry list";
    {
        SaveData boundary;
        const std::string campaign40(40, 'c');
        ASSERT_TRUE(boundary.campaign_state_set(campaign40, "seed", 1));
        ASSERT_EQ(1, boundary.campaign_state_get(campaign40, "seed"));
    }

    // 33-char key refused, 32-char key accepted.
    const std::string key32(32, 'k');
    const std::string key33(33, 'k');
    ASSERT_FALSE(data.campaign_state_set("gladiator", key33, 1));
    ASSERT_TRUE(data.campaign_state.empty());
    ASSERT_TRUE(data.campaign_state_set("gladiator", key32, 5));
    ASSERT_EQ(5, data.campaign_state_get("gladiator", key32));

    // Fill to the 128-entry cap; the 129th NEW key is refused with no
    // mutation, while existing keys stay writable — including a write of 0,
    // which KEEPS the entry.
    for (int i = 1; i < 128; ++i)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "k%03d", i);
        ASSERT_TRUE(data.campaign_state_set("gladiator", name, i)) << name;
    }
    ASSERT_EQ(128u, data.campaign_state.at("gladiator").size());
    ASSERT_FALSE(data.campaign_state_set("gladiator", "one_too_many", 1))
        << "129th entry refused";
    ASSERT_EQ(128u, data.campaign_state.at("gladiator").size())
        << "refusal must not mutate";
    ASSERT_TRUE(data.campaign_state_set("gladiator", "k001", 0))
        << "existing keys stay writable at the cap";
    ASSERT_EQ(128u, data.campaign_state.at("gladiator").size())
        << "setting an existing key to 0 keeps the entry";
    ASSERT_EQ(0, data.campaign_state_get("gladiator", "k001"));

    // Fill to the 128-campaign cap; state for a 129th campaign is refused.
    for (int i = 1; i < 128; ++i)
    {
        const std::string campaign = "camp" + std::to_string(i);
        ASSERT_TRUE(data.campaign_state_set(campaign, "seed", i)) << campaign;
    }
    ASSERT_EQ(128u, data.campaign_state.size());
    ASSERT_FALSE(data.campaign_state_set("camp129", "seed", 1))
        << "129th campaign refused";
    ASSERT_EQ(128u, data.campaign_state.size()) << "refusal must not mutate";
    ASSERT_TRUE(data.campaign_state_set("camp1", "seed2", 2))
        << "existing campaigns stay writable at the cap";
}

// The writer re-checks every campaign-state bound the choke enforces:
// direct field manipulation (the only way past campaign_state_set) must
// refuse to WRITE rather than emit a file the reader rejects.
TEST(SaveDataVersions, save_data_writer_refuses_direct_campaign_state_abuse)
{
    const auto expect_write_refused = [](SaveData& data, const char* slot) {
        namespace fs = std::filesystem;
        const fs::path path =
            fs::path(get_user_path()) / "save" / (std::string(slot) + ".gtl");
        og::test::ScopedPhysicalFileState file_state(path);
        ASSERT_TRUE(file_state.ready()) << file_state.error().message();
        EXPECT_EQ(SaveDataIoError::WriteFailed, data.save_with_error(slot))
            << slot;
        EXPECT_EQ(SaveDataIoError::WriteFailed, data.last_io_error()) << slot;
    };

    {
        SaveData data;
        data.current_campaign = "gladiator";
        for (int i = 0; i < SaveData::kCampaignStateMaxCampaigns + 1; ++i)
            data.campaign_state["camp" + std::to_string(i)] = {{"seed", 1}};
        expect_write_refused(data, "typed_state_too_many_campaigns");
    }
    {
        SaveData data;
        data.current_campaign = "gladiator";
        data.campaign_state["../escape"] = {{"seed", 1}};
        expect_write_refused(data, "typed_state_unsafe_campaign_id");
    }
    {
        SaveData data;
        data.current_campaign = "gladiator";
        auto& entries = data.campaign_state["gladiator"];
        for (int i = 0; i < SaveData::kCampaignStateMaxEntries + 1; ++i)
            entries.emplace_back("k" + std::to_string(1000 + i), i);
        expect_write_refused(data, "typed_state_too_many_entries");
    }
    {
        SaveData data;
        data.current_campaign = "gladiator";
        data.campaign_state["gladiator"] = {{"Bad Key", 1}};
        expect_write_refused(data, "typed_state_invalid_key");
    }
}

TEST(SaveDataVersions, save_data_reset_and_reset_campaign_clear_campaign_state)
{
    SaveData data;
    ASSERT_TRUE(data.campaign_state_set("camp_a", "key_a", 1));
    ASSERT_TRUE(data.campaign_state_set("camp_b", "key_b", 2));

    // reset_campaign erases exactly that campaign's decision book.
    data.reset_campaign("camp_a");
    ASSERT_EQ(0u, data.campaign_state.count("camp_a"))
        << "reset_campaign erases the campaign's state";
    ASSERT_EQ(2, data.campaign_state_get("camp_b", "key_b"))
        << "other campaigns keep their state";

    // reset clears everything.
    ASSERT_TRUE(data.campaign_state_set("camp_a", "key_a", 3));
    data.reset();
    ASSERT_TRUE(data.campaign_state.empty())
        << "reset() clears campaign_state beside completed_levels";
}


// --- GTL v16: per-hero campaign tag (docs/basecamp-zones-design.md,
// "Per-hero identity") ---

TEST(SaveDataVersions, save_data_v16_roundtrip_preserves_campaign_tags)
{
    SaveData src;
    src.current_campaign = "gladiator";
    src.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    src.team_list[0]->name = "SWORN";
    src.team_list[0]->campaign_tag = 1;
    src.team_list[1] = std::make_unique<guy>(FAMILY_ELF);
    src.team_list[1]->name = "BURDENED";
    src.team_list[1]->campaign_tag = 255;
    src.team_list[1]->deployed = false;
    src.team_list[2] = std::make_unique<guy>(FAMILY_MAGE);
    src.team_list[2]->name = "UNSWORN";
    src.team_size = 3;

    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_v16_tags")))
        << "v16 writer should succeed";

    // RAW offsets: version byte 16; guy+50 deployed, guy+51 campaign tag,
    // guy+52..57 zero-filled reserved.
    const std::vector<uint8_t> bytes = read_save_bytes("typed_save_v16_tags.gtl");
    constexpr size_t kHeaderSize = 164;
    constexpr size_t kGuySize = 58;
    ASSERT_GE(bytes.size(), kHeaderSize + 3 * kGuySize) << "file too small";
    ASSERT_EQ(16, (int)bytes[3]) << "version byte at offset 3";
    const size_t guy0 = kHeaderSize;
    const size_t guy1 = kHeaderSize + kGuySize;
    const size_t guy2 = kHeaderSize + 2 * kGuySize;
    ASSERT_EQ(1, (int)bytes[guy0 + 50]) << "deployed flag at guy+50";
    ASSERT_EQ(1, (int)bytes[guy0 + 51]) << "campaign tag at guy+51";
    ASSERT_EQ(0, (int)bytes[guy1 + 50]) << "held-back flag rides beside tag";
    ASSERT_EQ(255, (int)bytes[guy1 + 51]) << "the full byte range persists";
    ASSERT_EQ(0, (int)bytes[guy2 + 51]) << "unassigned writes tag 0";
    for (size_t i = 52; i < kGuySize; ++i) {
        ASSERT_EQ(0, (int)bytes[guy0 + i]) << "guy0 reserved byte " << i;
        ASSERT_EQ(0, (int)bytes[guy1 + i]) << "guy1 reserved byte " << i;
    }

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_v16_tags")))
        << "v16 reader should succeed";
    ASSERT_EQ(3, (int)loaded.team_size);
    ASSERT_EQ(1, (int)loaded.team_list[0]->campaign_tag);
    ASSERT_EQ(255, (int)loaded.team_list[1]->campaign_tag);
    ASSERT_FALSE(loaded.team_list[1]->deployed)
        << "the deploy flag still roundtrips beside the tag";
    ASSERT_EQ(0, (int)loaded.team_list[2]->campaign_tag);
}

TEST(SaveDataVersions, save_data_load_v15_payload_defaults_campaign_tags)
{
    // A v15 fixture file (deployed flag + 7 filler bytes per guy, empty
    // campaign-state tail): the reader must honor the deploy flag and
    // default every tag to 0 — the GuyRecord campaign_tag member is
    // IGNORED for version < 16.
    GuyRecord guys[2];
    guys[0].name = "SENT";
    guys[0].deployed = 1;
    guys[0].campaign_tag = 9; // must not be written by a v15-shaped fixture
    guys[1].name = "HELD";
    guys[1].deployed = 0;
    write_save_file("ver15_no_tags",
                    /*version=*/15,
                    /*campaign_id=*/"gladiator",
                    /*scen_num=*/1,
                    /*cash=*/100,
                    /*score=*/200,
                    /*allied_mode=*/1,
                    /*numplayers=*/1,
                    /*guys=*/guys,
                    /*listsize=*/2,
                    /*use_v8plus_campaigns=*/true,
                    /*v5plus_levelstatus=*/true,
                    /*levelstatus_500=*/nullptr,
                    /*levelstatus_200=*/nullptr,
                    /*ctf_team_count=*/2,
                    /*ctf_capture_limit=*/0,
                    /*ctf_respawn_ticks=*/0,
                    /*ctf_strip_scenario_troops=*/0,
                    /*respawn_mode=*/0,
                    /*generator_rate=*/0,
                    /*keep_fallen_heroes=*/0,
                    /*tower_best_floor=*/0,
                    /*tower_run_seed=*/0,
                    /*last_played_unix_s=*/42LL);

    SaveData tmp;
    ASSERT_TRUE(tmp.load("ver15_no_tags")) << "v15 load should succeed";
    ASSERT_EQ(2, (int)tmp.team_size);
    ASSERT_EQ(0, (int)tmp.team_list[0]->campaign_tag)
        << "a v15 file loads with zero tags";
    ASSERT_EQ(0, (int)tmp.team_list[1]->campaign_tag);
    ASSERT_TRUE(tmp.team_list[0]->deployed);
    ASSERT_FALSE(tmp.team_list[1]->deployed)
        << "the v14 deploy flag still reads under a v15 label";
}

TEST(SaveDataVersions, save_data_v15_shaped_reader_never_sniffs_the_tag_byte)
{
    // Write a REAL v16 file with a non-zero tag at guy+51, then re-label
    // it v15: the tag byte must read as filler (tag = 0), never sniffed
    // from content. The v15 tail (campaign-state block) parses unchanged.
    SaveData src;
    src.current_campaign = "gladiator";
    ASSERT_TRUE(src.campaign_state_set("gladiator", "watch_paid", 3));
    src.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    src.team_list[0]->name = "TAGGED";
    src.team_list[0]->campaign_tag = 7;
    src.team_size = 1;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(src.save_with_error("typed_save_v16_as_v15")));

    std::vector<uint8_t> bytes = read_save_bytes("typed_save_v16_as_v15.gtl");
    constexpr size_t kHeaderSize = 164;
    ASSERT_GT(bytes.size(), kHeaderSize + 58u);
    ASSERT_EQ(16, (int)bytes[3]);
    ASSERT_EQ(7, (int)bytes[kHeaderSize + 51]) << "tag really on disk";
    bytes[3] = 15; // re-label: v15-shaped reader sees reserved filler

    SDL_IOStream* out = open_write_file("save/", "typed_save_v16_as_v15.gtl");
    ASSERT_TRUE(out != nullptr);
    rw_write(out, bytes.data(), bytes.size());
    SDL_CloseIO(out);

    SaveData loaded;
    ASSERT_EQ(static_cast<int>(SaveDataIoError::None),
              static_cast<int>(loaded.load_with_error("typed_save_v16_as_v15")))
        << "a v15-labeled v16 file must load cleanly";
    ASSERT_EQ(1, (int)loaded.team_size);
    ASSERT_EQ(0, (int)loaded.team_list[0]->campaign_tag)
        << "the tag byte is hard-gated on version >= 16, never sniffed";
    ASSERT_EQ(3, loaded.campaign_state_get("gladiator", "watch_paid"))
        << "the v15 state tail still parses after the re-label";
}
