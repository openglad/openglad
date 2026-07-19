// Company layer unit tests (docs/company-basecamp-design.md §3.2 / §3.10):
// the wall-clock seam that stamps GTL v14 `last_played_unix_s`, and the
// grep tripwire proving the timestamp can never reach the deterministic sim.

#include <openglad/resources/company.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

// RAII guard so a failing assertion can't leak a pinned clock into other
// tests (the cfg-clobber lesson: convention-only cleanup breaks under
// --gtest_shuffle).
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

} // namespace

TEST(CompanyClock, override_pins_the_clock_and_reset_restores_wall_time)
{
    {
        ScopedCompanyClock pin(1234567890);
        ASSERT_EQ(1234567890, og::data::company_clock_now_s())
            << "pinned clock should return the fixed value";

        og::data::set_company_clock_for_tests(42);
        ASSERT_EQ(42, og::data::company_clock_now_s())
            << "re-pinning should take effect immediately";
    }

    // Guard destroyed -> real wall clock. Any plausible current time is far
    // past 2020-01-01 (1577836800) and the seam must be monotone enough to
    // never return the stale pinned values.
    const std::int64_t now_s = og::data::company_clock_now_s();
    ASSERT_GT(now_s, 1577836800) << "unpinned clock should read real wall time";
}

TEST(CompanyClock, zero_is_a_valid_pinned_value)
{
    // 0 is the v13-payload default for last_played_unix_s; the seam must be
    // able to produce it (optional-engaged, not "falsy = unpinned").
    ScopedCompanyClock pin(0);
    ASSERT_EQ(0, og::data::company_clock_now_s())
        << "a pinned value of 0 must not fall through to wall time";
}

// [§3.10 grep tripwire] The gameplay component must never reference the
// company layer: no gameplay implementation file may mention the
// last-played timestamp or include resources/company.h. This is what keeps
// the wall clock structurally unreachable from the deterministic sim (the
// parity harness does zero save IO; og_gameplay cannot link og_resources'
// company seam). Runs from the repo root (ctest WORKING_DIRECTORY).
TEST(CompanyClock, gameplay_sources_never_reference_company_or_last_played)
{
    namespace fs = std::filesystem;
    const std::vector<fs::path> roots = {
        fs::path("src") / "gameplay",
        fs::path("include") / "openglad" / "gameplay",
    };

    const std::vector<std::string> forbidden = {
        "last_played",
        "resources/company.h",
        "company_clock",
    };

    int scanned = 0;
    for (const auto& root : roots)
    {
        ASSERT_TRUE(fs::exists(root))
            << "tripwire must run from the repo root; missing " << root;
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;
            const auto ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".hpp")
                continue;
            ++scanned;

            std::ifstream in(entry.path());
            ASSERT_TRUE(in.good()) << "unreadable " << entry.path();
            std::stringstream buffer;
            buffer << in.rdbuf();
            const std::string contents = buffer.str();

            for (const auto& token : forbidden)
            {
                EXPECT_EQ(std::string::npos, contents.find(token))
                    << entry.path()
                    << " references forbidden company-layer token '" << token
                    << "' — the wall-clock/timestamp seam must never reach "
                       "the deterministic sim (design §3.2)";
            }
        }
    }
    ASSERT_GT(scanned, 50) << "tripwire scanned suspiciously few files";
}
