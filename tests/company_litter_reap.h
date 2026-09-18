#ifndef _TEST_COMPANY_LITTER_REAP_H__
#define _TEST_COMPANY_LITTER_REAP_H__

// [SAVE-R9] the one implementation of the litter rule; both harnesses call it.
//
// tests/integration/integration_main.cpp (reset_integration_ui_state) and
// tests/curses/curses_test_main.cpp (CursesSessionListener::OnTestEnd) each
// run this between every pair of tests with their own process baseline. It is
// a header because the two harnesses share no translation unit, and it is
// SDL-free (list_files / remove_user_file come from og_resources) because the
// curses binary links no SDL at all.
//
// The rule it enforces: a company file a test writes is NOT inert once that
// test returns. CONTINUE opens the MOST RECENT company (design §2.1), so a
// leftover slot with a fresh (or pinned-future) last_played_unix_s re-targets
// the next test's whole session, and a leftover save/backups/<slot>.NNN.gtl
// re-targets a positional backup-row click.
//
// Pinned by the a/b pairs in tests/integration/test_company_litter_guard.cpp
// (og_test_basecamp) and tests/curses/test_curses_company_litter_guard.cpp
// (og_test_curses): identical bodies, so whichever runs second is the oracle
// under every --gtest_shuffle order.

#include <openglad/resources/io_common.h>

#include <cstddef>
#include <set>
#include <string>

namespace og_test {

// Deletes every company artifact and company backup in the user save
// directory whose slot is not in `baseline`. save0 IS reaped: neither harness
// writes one, so in a fresh per-PID config dir any save0 present at a test end
// is that test's litter. Only "netsession" is exempt by slot ([SAVE-R8]'s slot
// setter already isolates it structurally and no leak of it is on file).
inline void reap_companies_outside_baseline(const std::set<std::string>& baseline)
{
    for (const std::string& name : list_files("save"))
    {
        // Every artifact name in this directory carries ".gtl" SOMEWHERE:
        // "<slot>.gtl" plus the four staging names company.cpp writes around
        // an atomic save or a backup restore ("<slot>.tmp.gtl",
        // "<slot>.gtl.tmp", "<slot>.gtl.restoretmp",
        // "<slot>.gtl.restoretmp.tmp"). A suffix test would reap the first two
        // and leave the restore-staging pair behind for the next test.
        if (name.find(".gtl") == std::string::npos)
            continue;
        // Slot is the basename up to the FIRST dot, which folds all five
        // staging names onto their own slot.
        const std::string slot = name.substr(0, name.find('.'));
        if (slot == "netsession")
            continue;
        if (baseline.count(slot) != 0)
            continue;
        (void)remove_user_file("save/" + name);
    }
    for (const std::string& name : list_files("save/backups"))
    {
        if (!name.ends_with(".gtl"))
            continue;
        // Backup grammar (src/resources/company.cpp, parse_backup_seq):
        // "<slot>.<SEQ>.gtl", so the slot is the name minus the last two
        // dot-tokens and may itself contain dots.
        const std::size_t gtl_dot = name.rfind('.');
        if (gtl_dot == std::string::npos || gtl_dot == 0)
            continue;
        const std::size_t seq_dot = name.rfind('.', gtl_dot - 1);
        if (seq_dot == std::string::npos)
            continue;
        const std::string slot = name.substr(0, seq_dot);
        if (baseline.count(slot) != 0)
            continue;
        (void)remove_user_file("save/backups/" + name);
    }
}

} // namespace og_test

#endif // _TEST_COMPANY_LITTER_REAP_H__
