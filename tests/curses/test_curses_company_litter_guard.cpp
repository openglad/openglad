/* [SAVE-R9] The curses harness's pin for the structural company-litter reap.
 *
 * The rule lives in ONE place, tests/company_litter_reap.h, and both harnesses
 * call it: tests/integration/integration_main.cpp between every pair of SDL
 * integration tests, and tests/curses/curses_test_main.cpp
 * (CursesSessionListener::OnTestEnd) between every pair of tests in THIS
 * binary. Until it was wired here the curses suite had no reap at all: every
 * test that founded a company cleaned up by its own per-test convention, and
 * the leaks that convention missed re-target the next positional company-row
 * click (CONTINUE opens the MOST RECENT company, design §2.1).
 *
 * The two tests below are a deliberate a/b PAIR with identical bodies: each
 * asserts a clean slate at entry and then leaves exactly the mess the rule
 * exists to clean. Whichever runs second is the oracle, so the pair pins the
 * rule under EVERY --gtest_shuffle order, not just the declared one. The
 * sibling pin for the other harness is
 * tests/integration/test_company_litter_guard.cpp (og_test_basecamp); this one
 * drops its clock and live-save legs (the curses suite pins no company clock
 * and holds no SDL session save) and adds the staging-artifact leg, which is
 * why the shared reap matches ".gtl" anywhere in the name instead of only at
 * the end.
 */
#include <gtest/gtest.h>

#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// 2100-01-01 UTC: later than any real-now stamp, so a leaked company written
// with it would outrank every other company in the binary — the worst case
// the rule has to clean up.
constexpr std::int64_t kFarFutureStamp = 4102444800LL;

// The interrupted-restore staging name src/resources/company.cpp:640 writes
// ("<slot>.gtl" + ".restoretmp"). It is a company artifact that does NOT end
// in ".gtl", which is the whole reason the shared reap searches for ".gtl"
// rather than testing the suffix.
std::string staging_relative_path(const std::string& slot)
{
    return "save/" + slot + ".gtl.restoretmp";
}

// Every entry precondition the [SAVE-R9] reap is responsible for in this
// binary. Fatal assertions, so the a/b pair shares one oracle; callers wrap it
// in ASSERT_NO_FATAL_FAILURE.
void assert_reap_left_a_clean_slate()
{
    ASSERT_FALSE(user_file_exists("save/save0.gtl"))
        << "[SAVE-R9]: save0 written by an earlier curses test must be reaped "
           "— it is litter, not a shared fixture, in a fresh per-PID config "
           "dir";

    for (const std::string& slot :
         {std::string("save0"), std::string("r9c-litter-a"),
          std::string("r9c-litter-b")}) {
        ASSERT_FALSE(user_file_exists("save/" + slot + ".gtl"))
            << "[SAVE-R9]: the non-baseline company save/" << slot
            << ".gtl must not survive into the next curses test";
        ASSERT_FALSE(user_file_exists(staging_relative_path(slot)))
            << "[SAVE-R9]: the restore-staging artifact "
            << staging_relative_path(slot)
            << " must be reaped too — a suffix match on \".gtl\" leaves it "
               "behind and the next restore of that slot adopts it";
        ASSERT_TRUE(og::data::list_company_backups(slot).empty())
            << "[SAVE-R9]: save/backups/" << slot
            << ".NNN.gtl is litter too — a stale backup re-targets the next "
               "positional backup-row click";
    }
}

// Leaves one company slot's full artifact set behind: the company file with a
// far-future stamp, its backup, and an interrupted-restore staging file.
void litter_company_slot(const std::string& slot)
{
    SaveData sd;
    sd.reset();
    sd.save_name = "R9C LITTER";
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

    const std::filesystem::path staging =
        std::filesystem::path(get_user_path()) / staging_relative_path(slot);
    {
        std::ofstream out(staging, std::ios::binary);
        ASSERT_TRUE(out.good())
            << "could not open the staging artifact " << staging;
        out << "staging";
    }
    ASSERT_TRUE(user_file_exists(staging_relative_path(slot)))
        << "staging artifact " << staging << " was not created";
}

// The shared body of the a/b pair: prove the slate is clean, then leave
// exactly the mess the rule exists to clean.
void clean_slate_then_litter(const std::string& slot)
{
    ASSERT_NO_FATAL_FAILURE(assert_reap_left_a_clean_slate());

    ASSERT_NO_FATAL_FAILURE(litter_company_slot("save0"));
    ASSERT_NO_FATAL_FAILURE(litter_company_slot(slot));
}

} // namespace

TEST(CursesCompanyLitter, harness_reaps_files_between_tests_a)
{
    ASSERT_NO_FATAL_FAILURE(clean_slate_then_litter("r9c-litter-a"));
}

TEST(CursesCompanyLitter, harness_reaps_files_between_tests_b)
{
    ASSERT_NO_FATAL_FAILURE(clean_slate_then_litter("r9c-litter-b"));
}
