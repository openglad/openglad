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
 *
 * The scan reads THIS file too, so the matcher's own self-tests compose
 * their inputs at runtime: never write the keyword and the class name
 * adjacent in this file (code, string literal or comment) or the tripwire
 * reports the guard itself as a second declaring file.
 *
 * The matcher is hand-written rather than the standard regex header:
 * libstdc++'s under GCC 15 with the nix cc-wrapper's injected -O2 plus
 * ASan/UBSan trips -Werror=maybe-uninitialized in bits/std_function.h,
 * which makes this file unbuildable on the local ci-asan preset.
 * scripts/check_no_std_regex.sh keeps it out, and
 * declaration_matcher_counts_declarations_not_uses gives the replacement
 * its own teeth.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

bool is_identifier_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// A declaration, not a use: a class-key keyword (its own token — not the tail
// of `subclass` or `my_struct`), then at least one space or tab, then an
// identifier whose last characters spell the guard's name. `Foo` + the name
// counts (a renamed twin is still a twin); the name plus a suffix does not.
bool declares_mount_restore(const std::string& line)
{
    static const char* const kKeywords[] = {"class", "struct"};
    static const std::string kName = "MountRestore";

    for (const char* const kw : kKeywords)
    {
        const std::string keyword(kw);
        for (std::size_t p = line.find(keyword); p != std::string::npos;
             p = line.find(keyword, p + 1))
        {
            if (p > 0 && is_identifier_char(line[p - 1]))
                continue; // the tail of a longer identifier
            std::size_t i = p + keyword.size();
            const std::size_t gap = i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            if (i == gap)
                continue; // no separator: a longer word, or end of line
            const std::size_t name_start = i;
            while (i < line.size() && is_identifier_char(line[i]))
                ++i;
            const std::string word = line.substr(name_start, i - name_start);
            if (word.size() >= kName.size() &&
                word.compare(word.size() - kName.size(), kName.size(),
                             kName) == 0)
                return true;
        }
    }
    return false;
}

} // namespace

TEST(CursesMountGuard, declaration_matcher_counts_declarations_not_uses)
{
    // Composed at runtime, never spelled adjacently: see the file header.
    const std::string kw_c = "class";
    const std::string kw_s = "struct";
    const std::string name = "MountRestore";

    struct Row
    {
        std::string line;
        bool declares;
    };
    const Row rows[] = {
        {kw_c + " " + name, true},
        {kw_s + "\t" + "Foo" + name + " {", true},
        {"  " + kw_c + "  " + name, true},
        {"friend " + kw_c + " " + name + ";", true},
        {name + " mount_restore;", false},   // a use
        {"~" + name + "()", false},          // the destructor line
        {kw_c + " " + name + "X", false},    // a different name
        {kw_c + name, false},                // no separator
        {"sub" + kw_c + " " + name, false},  // keyword is a word tail
        {kw_c + " Mount", false},
        {"my_" + kw_s + " " + name, false},
        {kw_c, false},
        {"", false},
    };
    for (const Row& row : rows)
    {
        EXPECT_EQ(row.declares, declares_mount_restore(row.line))
            << "declares_mount_restore(\"" << row.line << "\")";
    }
}

TEST(CursesMountGuard, the_suite_declares_exactly_one_mount_restore_class)
{
    namespace fs = std::filesystem;

    const fs::path root(OG_CURSES_TESTS_SOURCE_DIR);
    ASSERT_TRUE(fs::exists(root))
        << "tripwire needs the curses test sources at " << root;

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
            if (!declares_mount_restore(line))
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
