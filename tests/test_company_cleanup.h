#ifndef _TEST_COMPANY_CLEANUP_H__
#define _TEST_COMPANY_CLEANUP_H__

#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>

// A company file this test creates is NOT inert once the test returns.
//
// CONTINUE opens the MOST RECENT company (§2.1), and a company written
// through the game's own paths carries a wall-clock last_played_unix_s — so
// it outranks any company a later test seeds with a bare SaveData::save(),
// which never stamps. The later test's whole session then runs on this
// test's company, and the failure surfaces far away as a missing roster row
// or a team_size that is one short. Two independent instances of exactly
// that cost this suite a shuffle-order red each (SaveLoadTeam's save5/save6
// against the hire autosave; the new-game flows' founded companies against
// TrainTeam).
//
// Declare this at the top of any test that writes or founds a company: it
// snapshots the company list on entry and removes everything that is new on
// exit. save0 is the shared fixture, never this test's litter, and is never
// touched.
struct ScopedCompanyFileCleanup
{
    std::set<std::string> before;

    ScopedCompanyFileCleanup()
    {
        for (const og::data::CompanyInfo& info : og::data::list_companies())
            before.insert(info.slot);
    }

    ScopedCompanyFileCleanup(const ScopedCompanyFileCleanup&) = delete;
    ScopedCompanyFileCleanup& operator=(const ScopedCompanyFileCleanup&) = delete;

    ~ScopedCompanyFileCleanup()
    {
        for (const og::data::CompanyInfo& info : og::data::list_companies()) {
            if (info.slot == "save0" || before.count(info.slot) != 0)
                continue;
            for (const og::data::CompanyBackupInfo& backup :
                 og::data::list_company_backups(info.slot))
                (void)og::data::delete_company_backup(info.slot, backup.seq);
            (void)remove_user_file("save/" + info.slot + ".gtl");
        }
    }
};

// --- Seeding the company the flow under test must open ---------------------
//
// The other half of the same rule. A test that only cleans up after itself
// still has to make sure CONTINUE opens ITS company and not somebody else's,
// and a bare SaveData::save() cannot say so: it serializes whatever
// last_played_unix_s the in-memory save happens to hold (zero, in a fresh
// process), while select_startup_company() sorts the list by that stamp.
// These three were written for the hire flows and live here because six more
// menu_ui flows need exactly the same three lines — one implementation of
// the rule, not seven (the maintainer principle from PR #245).

// Restores whatever fixed clock (if any) the suite had installed.
struct CompanyClockRestore {
    ~CompanyClockRestore() { og::data::set_company_clock_for_tests(std::nullopt); }
};

// Later than ANY company already on disk, not merely later than "now".
// Another test in this binary can found a company under a FIXED future clock
// (test_new_game.cpp pins 2100-01-01 for its player-count flow, and the flow
// stamps save0 on the way through), so a now-relative stamp loses to it and
// CONTINUE opens that company instead of the one under test.
inline std::int64_t newest_company_stamp()
{
    std::int64_t newest = og::data::company_clock_now_s();
    for (const og::data::CompanyInfo& info : og::data::list_companies())
        newest = std::max(newest, info.last_played_unix_s);
    return newest;
}

// Write the in-memory save to `slot` the way the GAME writes a company: through
// the autosave choke point, which stamps last_played_unix_s. A bare
// SaveData::save() leaves the stamp at zero, which is what made these flows
// depend on nothing else in the binary having a fresher company — the CONTINUE
// door opens the most recent one, not the one the test happened to write.
inline bool seed_open_company(SaveData& save, const std::string& slot,
                              std::int64_t stamp_s)
{
    og::data::set_company_clock_for_tests(stamp_s);
    const bool ok = og::data::set_active_company_slot(slot) &&
                    og::data::company_autosave(
                        save, og::data::CompanyAutosaveKind::BaseCampMutation) ==
                        SaveDataIoError::None;
    og::data::set_company_clock_for_tests(std::nullopt);
    return ok;
}

#endif // _TEST_COMPANY_CLEANUP_H__
