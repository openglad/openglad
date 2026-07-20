/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Company-layer integration tests (docs/company-basecamp-design.md §3.5/§3.6)
// under the REAL io_init environment: PhysFS mounted, campaigns installed,
// OPENGLAD_CONFIG_DIR isolation (the cfg-clobber rule). The pure-logic
// scanner/slug/slot coverage lives in tests/unit/test_company.cpp; this file
// pins what only the full IO stack can prove — mount neutrality against a
// real mounted campaign, startup selection over writer-produced files, and
// the atomic write through PhysFS.

#include <gtest/gtest.h>

#include <openglad/resources/company.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Moves the whole save/ dir aside so these tests see exactly the files they
// create, restoring everything afterwards — order-independent under
// --gtest_shuffle even though the og_test_io binary shares one user dir.
class SaveDirSandbox
{
public:
    SaveDirSandbox()
        : save_dir_(std::filesystem::path(get_user_path()) / "save")
        , stash_dir_(std::filesystem::path(get_user_path()) /
                     "save_sandbox_stash_io")
    {
        // Order-independence hardening (mirrors the unit sandbox): a sibling
        // test that redirected the PhysFS write dir or dropped the user-dir
        // mount must not break the PhysFS-routed writes below.
        if (og::resources::is_initialized())
        {
            const std::string user_path = get_user_path();
            (void)og::resources::set_write_dir(user_path);
            (void)og::resources::mount(user_path.c_str(), nullptr, 1);
        }

        std::error_code ec;
        std::filesystem::remove_all(stash_dir_, ec);
        if (std::filesystem::exists(save_dir_, ec))
            std::filesystem::rename(save_dir_, stash_dir_, ec);
        std::filesystem::create_directories(save_dir_, ec);
    }

    ~SaveDirSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(save_dir_, ec);
        if (std::filesystem::exists(stash_dir_, ec))
            std::filesystem::rename(stash_dir_, save_dir_, ec);
        else
            std::filesystem::create_directories(save_dir_, ec);
    }

private:
    std::filesystem::path save_dir_;
    std::filesystem::path stash_dir_;
};

} // namespace

// §3.5: the header scan must never mount — even when the scanned company
// names a DIFFERENT campaign than the one the session has mounted. This is
// the Load-list requirement: listing 30 companies across 5 campaigns must
// not thrash the live mount.
TEST(CompanyIo, header_scan_never_disturbs_a_real_mounted_campaign)
{
    SaveDirSandbox sandbox;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));

    SaveData foreign;
    foreign.save_name = "Foreign Campaign Co";
    foreign.current_campaign = "org.openglad.ctf";
    foreign.scen_num = 2;
    ASSERT_TRUE(foreign.save("foreignco"));

    const auto info = og::data::read_company_header("foreignco");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->valid);
    EXPECT_EQ("org.openglad.ctf", info->campaign_id);
    EXPECT_EQ("org.openglad.gladiator", get_mounted_campaign())
        << "scanning a company on another campaign must not remount (§3.5)";

    (void)og::data::list_companies();
    EXPECT_EQ("org.openglad.gladiator", get_mounted_campaign())
        << "listing companies must not remount either";
}

// Startup selection over REAL writer-produced files (not raw fixtures), with
// the timestamp pinned through the company clock seam.
TEST(CompanyIo, startup_selection_prefers_most_recent_writer_output)
{
    SaveDirSandbox sandbox;

    SaveData older;
    older.save_name = "Old Guard";
    older.last_played_unix_s = 1000;
    ASSERT_TRUE(older.save("save0"));

    SaveData newer;
    newer.save_name = "Fresh Company";
    newer.last_played_unix_s = 2000;
    ASSERT_TRUE(newer.save("freshco"));

    EXPECT_EQ("freshco", og::data::select_startup_company());

    const std::vector<og::data::CompanyInfo> companies =
        og::data::list_companies();
    ASSERT_EQ(2u, companies.size());
    EXPECT_EQ("freshco", companies[0].slot);
    EXPECT_EQ("Fresh Company", companies[0].display_name);
    EXPECT_EQ("save0", companies[1].slot);
    EXPECT_EQ("Old Guard", companies[1].display_name);
}

// [SAVE-R5] The stray-slot behavior change, pinned under real IO: save0
// absent, other slots present — startup selection names the newest stray.
TEST(CompanyIo, startup_selection_no_save0_with_strays)
{
    SaveDirSandbox sandbox;

    SaveData stray_a;
    stray_a.save_name = "Stray A";
    stray_a.last_played_unix_s = 10;
    ASSERT_TRUE(stray_a.save("straya"));

    SaveData stray_b;
    stray_b.save_name = "Stray B";
    stray_b.last_played_unix_s = 20;
    ASSERT_TRUE(stray_b.save("strayb"));

    ASSERT_FALSE(user_file_exists("save/save0.gtl"));
    EXPECT_EQ("strayb", og::data::select_startup_company());
}

// §3.6: the atomic write goes through PhysFS (write dir = user path), the
// staging file is renamed away, and the result loads back with the full
// reader byte-for-byte (roundtrip through save_with_error/load_with_error).
TEST(CompanyIo, atomic_company_save_roundtrips_under_physfs)
{
    SaveDirSandbox sandbox;

    SaveData save;
    save.save_name = "Atomic IO Company";
    save.scen_num = 1;
    save.totalcash = 1717;
    save.last_played_unix_s = 313131;
    ASSERT_EQ(SaveDataIoError::None,
              og::data::atomic_company_save(save, "atomicio"));

    EXPECT_TRUE(user_file_exists("save/atomicio.gtl"));
    EXPECT_FALSE(user_file_exists("save/atomicio.tmp.gtl"))
        << "no staging file may survive a successful atomic write";

    SaveData loaded;
    ASSERT_EQ(SaveDataIoError::None, loaded.load_with_error("atomicio"));
    EXPECT_EQ("Atomic IO Company", loaded.save_name);
    EXPECT_EQ(1717u, loaded.totalcash);
    EXPECT_EQ(313131, loaded.last_played_unix_s);

    // The listing sees it as a company, and the tmp exclusion holds even for
    // a deliberately planted stale staging file.
    {
        std::ofstream stale(std::filesystem::path(get_user_path()) / "save" /
                                "atomicio.tmp.gtl",
                            std::ios::binary | std::ios::trunc);
        stale << "stale staging bytes";
    }
    const std::vector<og::data::CompanyInfo> companies =
        og::data::list_companies();
    ASSERT_EQ(1u, companies.size());
    EXPECT_EQ("atomicio", companies[0].slot);
    EXPECT_TRUE(companies[0].valid);
}
