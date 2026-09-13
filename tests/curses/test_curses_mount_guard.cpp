/* Source tripwire: the ncurses suite keeps exactly ONE campaign-mount guard.
 *
 * The rule being enforced is "no rule twins" — a restore rule that exists in
 * two places drifts, and the copies here had already drifted apart (one
 * exposed an idempotent restore() and the campaign it captured, one did not),
 * which is why a mechanical rename could not merge them. curses_test_main.cpp
 * already tells a leaking test which guard to declare; this test makes sure
 * that sentence keeps naming the only implementation there is.
 *
 * No behavioural assertion can see a duplicate class, so the oracle is the
 * source tree itself (precedent: tests/unit/test_company.cpp's
 * "[§3.10 grep tripwire]"). OG_CURSES_TESTS_SOURCE_DIR is a compile
 * definition because og_test_curses' ctest WORKING_DIRECTORY is the build
 * directory, not the repo root.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

TEST(CursesMountGuard, the_suite_declares_exactly_one_mount_restore_class)
{
    namespace fs = std::filesystem;

    const fs::path root(OG_CURSES_TESTS_SOURCE_DIR);
    ASSERT_TRUE(fs::exists(root))
        << "tripwire needs the curses test sources at " << root;

    // A declaration, not a use: `MountRestore mount_restore;` and the
    // destructor line must not count.
    const std::regex declaration(R"((class|struct)[ \t]+[A-Za-z0-9_]*MountRestore\b)");

    std::vector<std::string> declaring_files;
    std::vector<std::string> hits;
    int scanned = 0;

    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".inc")
            continue;
        ++scanned;

        std::ifstream in(entry.path());
        ASSERT_TRUE(in.good()) << "unreadable " << entry.path();
        std::string line;
        int number = 0;
        while (std::getline(in, line))
        {
            ++number;
            if (!std::regex_search(line, declaration))
                continue;
            const std::string name = entry.path().filename().string();
            hits.push_back(name + ":" + std::to_string(number) + ":" + line);
            if (std::find(declaring_files.begin(), declaring_files.end(), name) ==
                declaring_files.end())
                declaring_files.push_back(name);
        }
    }

    ASSERT_GT(scanned, 8) << "tripwire scanned suspiciously few files";

    std::sort(declaring_files.begin(), declaring_files.end());
    std::string report;
    for (const std::string& hit : hits)
        report += "\n  " + hit;

    const std::vector<std::string> expected = {"curses_mount_restore.h"};
    EXPECT_EQ(expected, declaring_files)
        << "the campaign-mount guard is declared in more than one place; the "
           "shared one in tests/curses/curses_mount_restore.h is the rule, "
           "every other copy is a twin that will drift:" << report;
}
