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

// --- [SAVE-R9] structural company-litter reap ------------------------------
//
// Defined in tests/integration/integration_main.cpp as a thin wrapper around
// the shared rule in tests/company_litter_reap.h (the curses harness runs the
// same rule). Declared here so tests can drive it directly
// (tests/integration/test_company_litter_guard.cpp is the pin) rather than
// only observe it.

// Seeds comma-separated stray company slots into save/ AND into the process
// baseline, the [SAVE-R5](c) diagnostic's own entry point.
void seed_stray_company_slots(const std::string& csv);

// Deletes every save/ artifact whose name carries ".gtl" — "<slot>.gtl" and
// the four atomic-save / backup-restore staging names ("<slot>.tmp.gtl",
// "<slot>.gtl.tmp", "<slot>.gtl.restoretmp", "<slot>.gtl.restoretmp.tmp") —
// and every save/backups/<slot>.NNN.gtl whose slot is not in the baseline.
// The "netsession" slot is the one exemption.
void reap_non_baseline_companies();

// The slots that existed before the first test ran, plus every stray seeded
// through seed_stray_company_slots(). Never reaped.
const std::set<std::string>& integration_company_baseline();

// [SAVE-R9] The per-test half of this rule is STRUCTURAL now and lives in
// tests/integration/integration_main.cpp: reset_integration_ui_state() runs
// between every pair of tests and deletes every save/ company artifact and
// save/backups/*.gtl whose slot is not in the process baseline (the
// [SAVE-R5](c) stray-slot seeds), save0 included. Nothing a test writes
// survives into the next test, so no test needs a teardown reaper.
//
// The rule it enforces, for context: a company file a test creates is NOT
// inert once the test returns. CONTINUE opens the MOST RECENT company (§2.1),
// and a company written through the game's own paths carries a wall-clock
// last_played_unix_s — so it outranks any company a later test seeds with a
// bare SaveData::save(), which never stamps. The later test's whole session
// then runs on the earlier test's company, and the failure surfaces far away
// as a missing roster row or a team_size that is one short.
//
// This RAII is what is LEFT of the convention: the tool for a scope that ends
// BEFORE the test does — a test that founds a company mid-body and must see
// the list without it a few lines later. Exactly one user is left,
// CampaignAndLevelPicker.new_game_flow_leaves_no_company_behind in
// tests/integration/test_campaign_and_level_picker.cpp, whose oracle reads
// the list after the guard's scope closes and before the test returns. At the
// top of a test body it is a twin of [SAVE-R9] and buys nothing, which is why
// the nineteen test-top declarations this suite used to carry are gone.
//
// It snapshots the company list on entry and removes everything that is new
// on exit; save0 is treated as pre-existing and is never touched by it.
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
