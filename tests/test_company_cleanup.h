#ifndef _TEST_COMPANY_CLEANUP_H__
#define _TEST_COMPANY_CLEANUP_H__

#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>

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

#endif // _TEST_COMPANY_CLEANUP_H__
