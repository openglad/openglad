// [SAVE-R9] The pin for the structural company-litter reap.
//
// The harness rule lives in tests/integration/integration_main.cpp
// (reset_integration_ui_state): between every pair of tests it deletes every
// save/*.gtl and every save/backups/<slot>.NNN.gtl whose slot is not in the
// process baseline, un-pins the company clock, and zeroes the live save's
// last_played_unix_s. Before it existed the leak was invisible until some
// unrelated positional flow clicked a re-targeted row six tests later.
//
// Two of these tests are a deliberate a/b PAIR with identical bodies: each
// asserts a clean slate at entry and then dirties the exact state the rule
// cleans. Whichever of them runs second is the oracle, so the pair pins the
// rule under EVERY --gtest_shuffle order, not just the declared one. The
// third test drives the reap directly to pin the baseline exemption that
// keeps the [SAVE-R5](c) stray-slot diagnostic alive.

#include <gtest/gtest.h>

#include <openglad/core/test_trace.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <cstdint>
#include <optional>
#include <string>

#include "test_company_cleanup.h"

namespace {

// 2100-01-01 UTC: later than any real-now stamp, so a leaked company written
// with it would outrank every other company in the binary — the worst case
// the rule has to clean up.
constexpr std::int64_t kFarFutureStamp = 4102444800LL;

SaveData& live_save()
{
    return og::runtime::current_session->myscreen_->save_data;
}

// Every entry precondition the [SAVE-R9] reset is responsible for. Written as
// a helper with fatal assertions so the a/b pair shares one oracle; the caller
// must check ::testing::Test::HasFatalFailure() semantics by returning, which
// ASSERT_NO_FATAL_FAILURE does for us at the call site.
void assert_reset_left_a_clean_slate()
{
    ASSERT_NE(nullptr, og::runtime::current_session)
        << "[SAVE-R9] pin needs the integration session";
    ASSERT_NE(nullptr, og::runtime::current_session->myscreen_)
        << "[SAVE-R9] pin needs the session's screen";

    ASSERT_FALSE(user_file_exists("save/save0.gtl"))
        << "[SAVE-R9]: save0 written by an earlier test must be reaped — it is "
           "litter, not a shared fixture, in a fresh per-PID config dir";
    ASSERT_FALSE(user_file_exists("save/r9litter-a.gtl"))
        << "[SAVE-R9]: a non-baseline company file must not survive into the "
           "next test";
    ASSERT_FALSE(user_file_exists("save/r9litter-b.gtl"))
        << "[SAVE-R9]: a non-baseline company file must not survive into the "
           "next test";

    ASSERT_EQ(0u, og::data::list_company_backups("r9litter-a").size())
        << "[SAVE-R9]: save/backups/<slot>.NNN.gtl is litter too — a stale "
           "backup re-targets the next positional backup-row click";
    ASSERT_EQ(0u, og::data::list_company_backups("r9litter-b").size())
        << "[SAVE-R9]: save/backups/<slot>.NNN.gtl is litter too — a stale "
           "backup re-targets the next positional backup-row click";

    ASSERT_EQ(0, live_save().last_played_unix_s)
        << "[SAVE-R9]: the live save's last_played_unix_s must return to zero "
           "— SaveData::reset() does not clear it, so a stamped in-memory save "
           "hands the next company write that stamp";
    ASSERT_LT(og::data::company_clock_now_s(), kFarFutureStamp)
        << "[SAVE-R9]: the company clock must be un-pinned — a test that pinned "
           "a future clock would stamp every later company with it";
}

// Writes `slot` with a bare SaveData::save_with_error (no autosave choke
// point, so the stamp is exactly what we put in it) and snapshots it.
void litter_company_slot(const std::string& slot)
{
    SaveData sd;
    sd.reset();
    sd.save_name = "R9 LITTER";
    sd.current_campaign = "gladiator";
    sd.last_played_unix_s = kFarFutureStamp;
    ASSERT_EQ(SaveDataIoError::None, sd.save_with_error(slot))
        << "could not write the litter company save/" << slot << ".gtl";
    ASSERT_TRUE(user_file_exists("save/" + slot + ".gtl"))
        << "litter company save/" << slot << ".gtl was not created";
    ASSERT_TRUE(og::data::backup_company_now(slot))
        << "could not snapshot the litter company " << slot;
    ASSERT_EQ(1u, og::data::list_company_backups(slot).size())
        << "the litter backup save/backups/" << slot << ".001.gtl is the "
           "second half of what the reap must remove";
}

// The shared body of the a/b pair: prove the slate is clean, then leave
// exactly the mess the rule exists to clean.
void clean_slate_then_litter(const std::string& slot)
{
    ASSERT_NO_FATAL_FAILURE(assert_reset_left_a_clean_slate());

    ASSERT_NO_FATAL_FAILURE(litter_company_slot("save0"));
    ASSERT_NO_FATAL_FAILURE(litter_company_slot(slot));

    live_save().last_played_unix_s = kFarFutureStamp;
    og::data::set_company_clock_for_tests(kFarFutureStamp);
}

} // namespace

TEST(CompanyLitter, harness_reaps_files_between_tests_a)
{
    trace_clear();
    ASSERT_NO_FATAL_FAILURE(clean_slate_then_litter("r9litter-a"));
}

TEST(CompanyLitter, harness_reaps_files_between_tests_b)
{
    trace_clear();
    ASSERT_NO_FATAL_FAILURE(clean_slate_then_litter("r9litter-b"));
}

// The baseline exemption. seed_stray_company_slots() is the real
// [SAVE-R5](c) entry point (OPENGLAD_TEST_SEED_STRAY_SLOTS calls exactly
// this), and its slots must survive every reap: a diagnostic that deletes
// itself after the first test measures nothing.
TEST(CompanyLitter, reap_spares_the_baseline_strays)
{
    trace_clear();
    seed_stray_company_slots("r9probe");
    ASSERT_EQ(1u, integration_company_baseline().count("r9probe"))
        << "[SAVE-R5](c): a seeded stray slot must join the [SAVE-R9] baseline";
    ASSERT_TRUE(user_file_exists("save/r9probe.gtl"))
        << "[SAVE-R5](c): the seeder must write a real company file";

    SaveData junk;
    junk.reset();
    junk.save_name = "R9 JUNK";
    junk.current_campaign = "gladiator";
    ASSERT_EQ(SaveDataIoError::None, junk.save_with_error("r9junk"))
        << "could not write the control company save/r9junk.gtl";
    ASSERT_TRUE(user_file_exists("save/r9junk.gtl"))
        << "control company save/r9junk.gtl was not created";

    reap_non_baseline_companies();

    const std::optional<og::data::CompanyInfo> probe =
        og::data::read_company_header("r9probe");
    ASSERT_TRUE(probe.has_value())
        << "[SAVE-R9]: the reap must SPARE a baseline stray — the stray-slot "
           "sweep depends on its slots staying in the list all run";
    ASSERT_TRUE(probe->valid)
        << "[SAVE-R9]: the spared stray must still be a loadable v14 company";
    ASSERT_EQ("STRAY r9probe", probe->display_name)
        << "[SAVE-R9]: the spared file must be the seeder's company, untouched";

    ASSERT_FALSE(user_file_exists("save/r9junk.gtl"))
        << "[SAVE-R9]: a non-baseline company written in the same directory "
           "must be reaped by the same call";

    // r9probe is permanently in this process's baseline, so nothing else will
    // ever remove its file: this test owns it.
    ASSERT_TRUE(remove_user_file("save/r9probe.gtl"))
        << "the baseline stray this test seeded must be removable by hand";
}
