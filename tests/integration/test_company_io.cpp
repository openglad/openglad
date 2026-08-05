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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
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

    const std::filesystem::path& dir() const { return save_dir_; }

private:
    std::filesystem::path save_dir_;
    std::filesystem::path stash_dir_;
};

// RAII clock pin (mirrors the unit-test guard): a failing assertion must not
// leak a fixed wall clock into sibling tests under --gtest_shuffle.
struct ScopedCompanyClock
{
    explicit ScopedCompanyClock(std::int64_t fixed_now_s)
    {
        og::data::set_company_clock_for_tests(fixed_now_s);
    }
    ~ScopedCompanyClock()
    {
        og::data::set_company_clock_for_tests(std::nullopt);
    }
};

std::string read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

} // namespace

// §3.5: the header scan must never mount — even when the scanned company
// names a DIFFERENT campaign than the one the session has mounted. This is
// the Load-list requirement: listing 30 companies across 5 campaigns must
// not thrash the live mount.
TEST(CompanyIo, header_scan_never_disturbs_a_real_mounted_campaign)
{
    SaveDirSandbox sandbox;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    SaveData foreign;
    foreign.save_name = "Foreign Campaign Co";
    foreign.current_campaign = "org.openglad.ctf";
    foreign.scen_num = 2;
    ASSERT_TRUE(foreign.save("foreignco"));

    const auto info = og::data::read_company_header("foreignco");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->valid);
    // The stored header keeps the legacy reverse-DNS spelling; the scan
    // reports the normalized id (matching what SaveData::load would mount).
    EXPECT_EQ("ctf", info->campaign_id);
    EXPECT_EQ("gladiator", get_mounted_campaign())
        << "scanning a company on another campaign must not remount (§3.5)";

    (void)og::data::list_companies();
    EXPECT_EQ("gladiator", get_mounted_campaign())
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

// --- Backups (§3.7), under the real IO stack ------------------------------

// §3.7: a snapshot of a REAL writer-produced company file is byte-identical
// to it, and the Backups view header-scan reads the snapshot's identity.
TEST(CompanyIo, backup_snapshot_round_trips_writer_output)
{
    SaveDirSandbox sandbox;

    SaveData save;
    save.save_name = "Backup Round Trip Co";
    save.totalcash = 100;
    save.scen_num = 2;
    save.last_played_unix_s = 1111;
    ASSERT_TRUE(save.save("roundco"));

    ASSERT_TRUE(og::data::backup_company_now("roundco"));
    ASSERT_TRUE(user_file_exists("save/backups/roundco.001.gtl"));
    EXPECT_EQ(read_file_bytes(sandbox.dir() / "roundco.gtl"),
              read_file_bytes(sandbox.dir() / "backups" / "roundco.001.gtl"))
        << "a snapshot must be a byte copy of the company file (§3.7)";

    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("roundco");
    ASSERT_EQ(1u, backups.size());
    EXPECT_TRUE(backups[0].header.valid);
    EXPECT_EQ("Backup Round Trip Co", backups[0].header.display_name);
    EXPECT_EQ(100u, backups[0].header.totalcash);
    EXPECT_EQ(1111, backups[0].header.last_played_unix_s);
}

// Retention over real writer files: 22 snapshots leave exactly the 20
// newest, pruned from the lowest seqs up.
TEST(CompanyIo, backup_retention_caps_snapshots)
{
    SaveDirSandbox sandbox;

    SaveData save;
    save.save_name = "Retention Co";
    ASSERT_TRUE(save.save("retentionco"));
    for (int round = 0; round < og::data::kCompanyBackupRetention + 2; ++round)
    {
        ASSERT_TRUE(og::data::backup_company_now("retentionco"))
            << "round " << round;
    }

    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("retentionco");
    ASSERT_EQ(static_cast<std::size_t>(og::data::kCompanyBackupRetention),
              backups.size());
    EXPECT_EQ(og::data::kCompanyBackupRetention + 2, backups.front().seq);
    EXPECT_EQ(3, backups.back().seq);
    EXPECT_FALSE(user_file_exists("save/backups/retentionco.002.gtl"));
}

// [SAVE-R3] The restore success path: rewind-in-place with the pre-restore
// state preserved as the newest backup, the timestamp re-stamped through the
// pinned clock, and no seq ever reused after the rewind.
TEST(CompanyIo, validated_restore_rewinds_in_place_without_seq_reuse)
{
    SaveDirSandbox sandbox;
    ScopedCompanyClock clock(5000);

    SaveData save;
    save.save_name = "Rewind Co";
    save.totalcash = 100;
    save.scen_num = 1;
    ASSERT_TRUE(save.save("rewindco"));
    ASSERT_TRUE(og::data::backup_company_now("rewindco")); // seq 1: cash 100

    save.totalcash = 999;
    save.scen_num = 3;
    ASSERT_TRUE(save.save("rewindco"));
    const std::string pre_restore_bytes =
        read_file_bytes(sandbox.dir() / "rewindco.gtl");

    ASSERT_EQ(og::data::CompanyRestoreError::None,
              og::data::restore_company_backup(save, "rewindco", 1));

    // Memory rewound to the seq-1 state, timestamp re-stamped (step 4).
    EXPECT_EQ(100u, save.totalcash);
    EXPECT_EQ(1, save.scen_num);
    EXPECT_EQ(5000, save.last_played_unix_s)
        << "the restore must re-stamp last_played so Continue still points "
           "at this company";

    // Disk agrees with memory (header scan of the rewound slot).
    const auto info = og::data::read_company_header("rewindco");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->valid);
    EXPECT_EQ(100u, info->totalcash);
    EXPECT_EQ(5000, info->last_played_unix_s);

    // The pre-restore state became the newest backup (step 1), byte-exact.
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("rewindco");
    ASSERT_EQ(2u, backups.size());
    EXPECT_EQ(2, backups[0].seq);
    EXPECT_EQ(pre_restore_bytes,
              read_file_bytes(sandbox.dir() / "backups" / "rewindco.002.gtl"))
        << "the pre-restore state must be preserved before the rewind";
    EXPECT_EQ(999u, backups[0].header.totalcash);

    // Seq monotonicity after a rewind: the next snapshot continues past the
    // pre-restore backup, never reusing a seq.
    ASSERT_TRUE(og::data::backup_company_now("rewindco"));
    EXPECT_TRUE(user_file_exists("save/backups/rewindco.003.gtl"));
}

// Corner case: restoring the OLDEST backup at FULL retention. The step-1
// pre-restore snapshot triggers the prune, which reaps exactly the chosen
// seq — the staged copy must keep the rewind working anyway.
TEST(CompanyIo, restore_oldest_backup_at_full_retention_still_rewinds)
{
    SaveDirSandbox sandbox;
    ScopedCompanyClock clock(6000);

    SaveData save;
    save.save_name = "Full Retention Co";
    save.totalcash = 111;
    ASSERT_TRUE(save.save("fullco"));
    ASSERT_TRUE(og::data::backup_company_now("fullco")); // seq 1: cash 111

    save.totalcash = 222;
    ASSERT_TRUE(save.save("fullco"));
    for (int round = 1; round < og::data::kCompanyBackupRetention; ++round)
        ASSERT_TRUE(og::data::backup_company_now("fullco")); // seqs 2..20
    ASSERT_EQ(static_cast<std::size_t>(og::data::kCompanyBackupRetention),
              og::data::list_company_backups("fullco").size());

    save.totalcash = 333;
    ASSERT_TRUE(save.save("fullco")); // current state: cash 333

    ASSERT_EQ(og::data::CompanyRestoreError::None,
              og::data::restore_company_backup(save, "fullco", 1))
        << "the oldest snapshot must remain restorable at full retention";
    EXPECT_EQ(111u, save.totalcash) << "memory rewound to the seq-1 state";
    EXPECT_EQ(6000, save.last_played_unix_s);
    EXPECT_FALSE(user_file_exists("save/fullco.gtl.restoretmp"))
        << "no staging file may survive the restore";

    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("fullco");
    ASSERT_EQ(static_cast<std::size_t>(og::data::kCompanyBackupRetention),
              backups.size())
        << "retention holds through the restore";
    EXPECT_EQ(og::data::kCompanyBackupRetention + 1, backups.front().seq)
        << "the pre-restore state is the newest snapshot";
    EXPECT_EQ(333u, backups.front().header.totalcash);
    EXPECT_EQ(2, backups.back().seq)
        << "the chosen seq-1 file was pruned by its own restore (documented)";
}

// [SAVE-R3] The restore failure path: a backup whose HEADER validates but
// whose body is torn passes step 0, fails the step-3 full reload, and must
// roll back BOTH the on-disk slot and the in-memory SaveData to the
// pre-restore state (which step 1 preserved as the newest backup).
TEST(CompanyIo, restore_reload_failure_rolls_back_disk_and_memory)
{
    SaveDirSandbox sandbox;

    SaveData save;
    save.save_name = "Rollback Co";
    save.totalcash = 777;
    ASSERT_TRUE(save.save("rollbackco"));
    const std::string good_bytes =
        read_file_bytes(sandbox.dir() / "rollbackco.gtl");
    ASSERT_GE(good_bytes.size(), 164u);

    // Torn snapshot: exactly the 164-byte v14 header with listsize patched
    // to 2 — the ≤164-byte header scan (step 0) accepts it, the full reader
    // hits EOF reading the promised roster (step 3).
    std::string torn = good_bytes.substr(0, 164);
    const std::int16_t fake_listsize = 2;
    std::memcpy(torn.data() + 130, &fake_listsize, sizeof(fake_listsize));
    std::error_code ec;
    std::filesystem::create_directories(sandbox.dir() / "backups", ec);
    ASSERT_FALSE(ec);
    {
        std::ofstream out(sandbox.dir() / "backups" / "rollbackco.005.gtl",
                          std::ios::binary | std::ios::trunc);
        out.write(torn.data(), static_cast<std::streamsize>(torn.size()));
        ASSERT_TRUE(out.good());
    }

    SaveData memory;
    ASSERT_EQ(SaveDataIoError::None, memory.load_with_error("rollbackco"));
    ASSERT_EQ(777u, memory.totalcash);

    ASSERT_EQ(og::data::CompanyRestoreError::ReloadFailed,
              og::data::restore_company_backup(memory, "rollbackco", 5));

    EXPECT_EQ(good_bytes, read_file_bytes(sandbox.dir() / "rollbackco.gtl"))
        << "the rollback must re-copy the step-1 backup over the slot";
    EXPECT_EQ(777u, memory.totalcash)
        << "the rollback must reload the pre-restore state into memory";

    // The step-1 pre-restore snapshot took seq 6 (past the torn seq 5); the
    // torn snapshot itself stays for inspection; nothing reuses a seq.
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("rollbackco");
    ASSERT_EQ(2u, backups.size());
    EXPECT_EQ(6, backups[0].seq);
    EXPECT_TRUE(backups[0].header.valid);
    EXPECT_EQ(good_bytes,
              read_file_bytes(sandbox.dir() / "backups" / "rollbackco.006.gtl"));
    EXPECT_EQ(5, backups[1].seq);
    ASSERT_TRUE(og::data::backup_company_now("rollbackco"));
    EXPECT_TRUE(user_file_exists("save/backups/rollbackco.007.gtl"));
}

// Recovery shape: the company file itself is missing (lost/deleted) but a
// valid backup remains. Step 1 has nothing to preserve; the rewind simply
// recreates the slot file.
TEST(CompanyIo, restore_recreates_a_missing_company_file)
{
    SaveDirSandbox sandbox;
    ScopedCompanyClock clock(7000);

    SaveData save;
    save.save_name = "Lost Co";
    save.totalcash = 555;
    ASSERT_TRUE(save.save("lostco"));
    ASSERT_TRUE(og::data::backup_company_now("lostco")); // seq 1
    ASSERT_TRUE(remove_user_file("save/lostco.gtl"));

    SaveData memory;
    ASSERT_EQ(og::data::CompanyRestoreError::None,
              og::data::restore_company_backup(memory, "lostco", 1));
    EXPECT_EQ(555u, memory.totalcash);
    EXPECT_EQ(7000, memory.last_played_unix_s);
    EXPECT_TRUE(user_file_exists("save/lostco.gtl"))
        << "the rewind recreates the missing company file";
    EXPECT_EQ(1u, og::data::list_company_backups("lostco").size())
        << "no pre-restore snapshot is taken when there is nothing to "
           "preserve";
}

// §3.7 delete: the company file and ALL its backups are reaped together; the
// listing no longer knows the company.
TEST(CompanyIo, delete_company_reaps_file_and_backups)
{
    SaveDirSandbox sandbox;

    SaveData save;
    save.save_name = "Reap Co";
    ASSERT_TRUE(save.save("reapco"));
    ASSERT_TRUE(og::data::backup_company_now("reapco"));
    ASSERT_TRUE(og::data::backup_company_now("reapco"));

    ASSERT_EQ("save0", og::data::active_company_slot())
        << "integration fixture default — reapco is deletable";
    ASSERT_TRUE(og::data::delete_company("reapco"));
    EXPECT_FALSE(user_file_exists("save/reapco.gtl"));
    EXPECT_TRUE(og::data::list_company_backups("reapco").empty());
    EXPECT_TRUE(og::data::list_companies().empty());
}
