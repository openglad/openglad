/* Source tripwire: every guarded switch in tests/parity keeps its
 * no-`default:` discipline.
 *
 * The product rule lives in the comment above evaluate_one
 * (tests/parity/fact_predicate.cpp): a `switch` over an enum keeps NO
 * `default:` arm on purpose, because -Wswitch (a -Werror diagnostic on the
 * ci-test / ci-asan / ci-tsan lanes) only reports an unhandled enumerator
 * while the switch has no default. GCC and Clang both go silent the moment a
 * default exists, so a single well-meaning `default: break;` turns "a new
 * EventKind / FactKind / Order is a build failure here" into "a new
 * enumerator renders as kind_19 / Unknown / an unbagged family, quietly".
 * That is the exact defect W-I-3 found: DamageNumber had been hiding behind a
 * `default:` in state_dump.cpp since the enumerator was added.
 *
 * Seven switches carry the guard marker (composed at runtime below, never
 * spelled in this file -- see the self-reference note). The compiler cannot
 * see the rule at all once a default is back, so the oracle is the source
 * tree itself; precedent: tests/curses/test_curses_mount_guard.cpp and
 * tests/unit/test_company.cpp's "[section 3.10 grep tripwire]".
 * OG_PARITY_WORKSPACE_ROOT is a compile definition because og_test_parity's
 * ctest WORKING_DIRECTORY is the build directory, not the repo root
 * (cmake/OpenGladTests.cmake).
 *
 * SELF-REFERENCE: the census scan walks every .cpp/.h under tests/parity,
 * this file included. The marker token is therefore assembled from two
 * halves at runtime and never appears contiguously in this source -- write
 * it whole anywhere here (code, literal or comment) and the tripwire reports
 * this file as an eighth guarded switch.
 *
 * The matcher is hand-written rather than <regex>: scripts/check_no_std_regex.sh
 * keeps std::regex out of the test tree (libstdc++ under GCC 15 with the
 * injected -O2 plus ASan/UBSan trips -Werror=maybe-uninitialized inside
 * bits/std_function.h). matcher_finds_depth_one_defaults_only gives the
 * replacement its own teeth.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

// The marker, assembled so this file never spells it. See the header note.
std::string marker_token()
{
    return std::string("[SWITCH-") + "GUARD]";
}

bool is_ident_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

bool is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

// A whole token at `pos`, not the head or tail of a longer identifier.
bool word_at(const std::string& s, std::size_t pos, const std::string& word)
{
    if (s.compare(pos, word.size(), word) != 0) return false;
    if (pos > 0 && is_ident_char(s[pos - 1])) return false;
    const std::size_t after = pos + word.size();
    if (after < s.size() && is_ident_char(s[after])) return false;
    return true;
}

// Replace every comment / string-literal / char-literal character with a
// space, keeping newlines (and therefore every byte offset and line number)
// exactly where they were. Braces and the words `switch` / `default` that
// live inside a comment or a literal are invisible to the scan below.
std::string blank_noncode(const std::string& src)
{
    std::string out(src.size(), ' ');
    for (std::size_t i = 0; i < src.size(); ++i)
        if (src[i] == '\n') out[i] = '\n';

    std::size_t i = 0;
    while (i < src.size())
    {
        const char c = src[i];
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/')
        {
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < src.size() &&
                   !(src[i] == '*' && src[i + 1] == '/'))
                ++i;
            i = std::min(src.size(), i + 2);
            continue;
        }
        if (c == '"')
        {
            ++i;
            while (i < src.size() && src[i] != '"')
            {
                if (src[i] == '\\') ++i;
                ++i;
            }
            if (i < src.size()) ++i; // past the closing quote
            continue;
        }
        if (c == '\'')
        {
            // A char literal closes on the same line; a lone apostrophe
            // (prose that escaped a comment, say) is left as code.
            std::size_t j = i + 1;
            bool closed = false;
            while (j < src.size() && src[j] != '\n')
            {
                if (src[j] == '\\') { j += 2; continue; }
                if (src[j] == '\'') { closed = true; break; }
                ++j;
            }
            if (closed)
            {
                i = j + 1;
                continue;
            }
        }
        out[i] = c;
        ++i;
    }
    return out;
}

int line_of(const std::string& s, std::size_t pos)
{
    return 1 + static_cast<int>(
                   std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(pos), '\n'));
}

std::string line_text(const std::string& s, std::size_t pos)
{
    const std::size_t b = s.rfind('\n', pos);
    const std::size_t begin = (b == std::string::npos) ? 0 : b + 1;
    std::size_t end = s.find('\n', pos);
    if (end == std::string::npos) end = s.size();
    std::string t = s.substr(begin, end - begin);
    const std::size_t first = t.find_first_not_of(" \t");
    return first == std::string::npos ? t : t.substr(first);
}

struct DefaultHit
{
    int         line;
    std::string text;
};

struct GuardedSwitch
{
    int                     marker_line = 0;
    int                     switch_line = 0; // 0 = no switch follows the marker
    std::vector<DefaultHit> hits;
};

// For every marker in `text`: the next `switch (` token after it, and every
// `default:` arm that belongs to THAT switch -- brace depth 1 inside its
// body. A `default:` in a nested switch (depth >= 2), in a string, or in a
// comment is not an arm of the guarded switch and is not a hit.
std::vector<GuardedSwitch> scan(const std::string& text)
{
    const std::string marker = marker_token();
    const std::string code   = blank_noncode(text);
    const std::string kSwitch("switch");
    const std::string kDefault("default");

    std::vector<GuardedSwitch> out;
    for (std::size_t m = text.find(marker); m != std::string::npos;
         m = text.find(marker, m + marker.size()))
    {
        GuardedSwitch g;
        g.marker_line = line_of(text, m);

        std::size_t sw = std::string::npos;
        for (std::size_t p = code.find(kSwitch, m); p != std::string::npos;
             p = code.find(kSwitch, p + 1))
        {
            if (!word_at(code, p, kSwitch)) continue;
            std::size_t q = p + kSwitch.size();
            while (q < code.size() && is_space_char(code[q])) ++q;
            if (q < code.size() && code[q] == '(')
            {
                sw = p;
                break;
            }
        }
        if (sw == std::string::npos)
        {
            out.push_back(g);
            continue;
        }
        g.switch_line = line_of(text, sw);

        const std::size_t open = code.find('{', sw);
        if (open == std::string::npos)
        {
            out.push_back(g);
            continue;
        }

        int depth = 0;
        for (std::size_t i = open; i < code.size(); ++i)
        {
            const char c = code[i];
            if (c == '{')
            {
                ++depth;
                continue;
            }
            if (c == '}')
            {
                --depth;
                if (depth == 0) break;
                continue;
            }
            if (depth == 1 && c == 'd' && word_at(code, i, kDefault))
            {
                std::size_t q = i + kDefault.size();
                while (q < code.size() && is_space_char(code[q])) ++q;
                if (q < code.size() && code[q] == ':')
                    g.hits.push_back({line_of(text, i), line_text(text, i)});
            }
        }
        out.push_back(g);
    }
    return out;
}

std::string read_file(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in.good()) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The four files that carry the guard today, and how many switches each one
// guards. Update this table (and the guard comments) together -- that is the
// point of the tripwire.
const std::pair<const char*, int> kExpectedCensus[] = {
    {"fact_predicate.cpp", 2},          // evaluate_one, fact_kind_name
    {"parity_runner.cpp", 1},           // bag_walker
    {"scenario_facts_dump_main.cpp", 2},// arg0_order_for_symbol, predicate_expression
    {"state_dump.cpp", 2},              // family_symbol_by_order, event_kind_symbol
};

} // namespace

TEST(SwitchGuardTripwire, matcher_finds_depth_one_defaults_only)
{
    // Composed at runtime, never spelled in this file: see the header note.
    const std::string M = marker_token();
    const std::string D = std::string("default") + ":";

    struct Row
    {
        const char* what;
        std::string text;
        int         expected_hits;
    };

    const Row rows[] = {
        {"a default arm of the guarded switch",
         "// " + M + "\nswitch (k)\n{\n    case A: return 1;\n    " + D +
             " return 0;\n}\n",
         1},
        {"default only inside a nested switch in a braced case body",
         "// " + M + "\nswitch (k)\n{\n    case A:\n    {\n        switch (j)\n"
         "        {\n            case B: return 1;\n            " + D +
             " return 0;\n        }\n    }\n    case C: return 2;\n}\n",
         0},
        {"the word inside a string literal",
         "// " + M + "\nswitch (k)\n{\n    case A: return \"" + D +
             "\";\n}\n",
         0},
        {"the word commented out inside the switch",
         "// " + M + "\nswitch (k)\n{\n    case A: return 1;\n    // " + D +
             " return 0;\n}\n",
         0},
        {"a `defaults:` label is a different identifier",
         "// " + M + "\nswitch (k)\n{\n    case A: return 1;\n    defaults: "
         "return 0;\n}\n",
         0},
        {"braces inside char literals do not unbalance the body",
         "// " + M + "\nswitch (k)\n{\n    case A: push('{'); push('}'); "
         "break;\n    " + D + " break;\n}\n",
         1},
        {"a default AFTER the guarded switch closed is not its arm",
         "// " + M + "\nswitch (k)\n{\n    case A: return 1;\n}\nswitch (j)\n"
         "{\n    " + D + " return 0;\n}\n",
         0},
        {"a `switch` mentioned in the guard comment is not the switch",
         "// " + M + " no switch here, the real one is below\nswitch (k)\n"
         "{\n    " + D + " return 0;\n}\n",
         1},
        {"myswitch is not the switch keyword",
         "// " + M + "\nint myswitch(int k);\nswitch (k)\n{\n    " + D +
             " return 0;\n}\n",
         1},
    };

    for (const Row& row : rows)
    {
        const std::vector<GuardedSwitch> got = scan(row.text);
        ASSERT_EQ(static_cast<std::size_t>(1), got.size())
            << "one marker must yield one guarded switch record: " << row.what;
        EXPECT_EQ(row.expected_hits, static_cast<int>(got[0].hits.size()))
            << "matcher counted the wrong number of depth-1 `default` arms: "
            << row.what << "\n--- input ---\n" << row.text;
        EXPECT_NE(0, got[0].switch_line)
            << "matcher lost the switch that the marker guards: " << row.what;
    }

    // A marker with no switch under it is a broken guard, and the matcher
    // must say so rather than silently reporting "clean".
    const std::vector<GuardedSwitch> orphan = scan("// " + M + "\nint x = 0;\n");
    ASSERT_EQ(static_cast<std::size_t>(1), orphan.size());
    EXPECT_EQ(0, orphan[0].switch_line)
        << "a marker with no switch after it must report switch_line 0";

    // Two markers in one file are two independent records.
    const std::string two = "// " + M + "\nswitch (a)\n{\n    " + D +
                            " break;\n}\n// " + M + "\nswitch (b)\n{\n"
                            "    case Z: break;\n}\n";
    const std::vector<GuardedSwitch> pair = scan(two);
    ASSERT_EQ(static_cast<std::size_t>(2), pair.size());
    EXPECT_EQ(static_cast<std::size_t>(1), pair[0].hits.size())
        << "first guarded switch has the default arm";
    EXPECT_EQ(static_cast<std::size_t>(0), pair[1].hits.size())
        << "second guarded switch is clean";
    EXPECT_EQ(1, pair[0].marker_line) << "first marker is on line 1";
    EXPECT_EQ(6, pair[1].marker_line) << "second marker is on line 6";
}

TEST(SwitchGuardTripwire, every_guarded_parity_switch_has_no_default_arm)
{
    namespace fs = std::filesystem;

    const fs::path root = fs::path(OG_PARITY_WORKSPACE_ROOT) / "tests" / "parity";
    ASSERT_TRUE(fs::exists(root))
        << "tripwire needs the parity sources at " << root;

    std::vector<std::pair<std::string, int>> census;
    std::vector<std::string>                 violations;
    std::vector<std::string>                 orphans;
    std::vector<std::string>                 found;
    int scanned = 0;

    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file()) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        ++scanned;

        const std::string text = read_file(entry.path());
        ASSERT_FALSE(text.empty()) << "unreadable or empty " << entry.path();

        const std::vector<GuardedSwitch> guards = scan(text);
        if (guards.empty()) continue;

        const std::string name = entry.path().filename().string();
        census.emplace_back(name, static_cast<int>(guards.size()));
        for (const GuardedSwitch& g : guards)
        {
            found.push_back(name + ":" + std::to_string(g.marker_line) +
                            " guards the switch at line " +
                            std::to_string(g.switch_line));
            if (g.switch_line == 0)
                orphans.push_back(name + ":" + std::to_string(g.marker_line));
            for (const DefaultHit& h : g.hits)
                violations.push_back(name + ":" + std::to_string(h.line) +
                                     "  (guard marker at line " +
                                     std::to_string(g.marker_line) + ")  " +
                                     h.text);
        }
    }

    ASSERT_GT(scanned, 20) << "tripwire scanned suspiciously few parity sources";

    std::sort(census.begin(), census.end());
    std::vector<std::pair<std::string, int>> expected;
    for (const auto& row : kExpectedCensus)
        expected.emplace_back(row.first, row.second);
    std::sort(expected.begin(), expected.end());

    std::string report;
    for (const std::string& f : found) report += "\n  " + f;

    EXPECT_EQ(expected, census)
        << "the guard-marker census moved: a guarded switch lost its marker "
           "(and with it the tripwire), or a new one appeared that this table "
           "does not name. Markers found:" << report;

    std::string orphan_report;
    for (const std::string& o : orphans) orphan_report += "\n  " + o;
    EXPECT_TRUE(orphans.empty())
        << "a guard marker has no switch under it, so it guards nothing:"
        << orphan_report;

    std::string violation_report;
    for (const std::string& v : violations) violation_report += "\n  " + v;
    EXPECT_TRUE(violations.empty())
        << "a guarded switch grew a `default:` arm, which silences -Wswitch "
           "and lets a NEW enumerator compile into a silent wrong render "
           "(the DamageNumber defect W-I-3 found). Remove the default and "
           "spell the missing enumerators, keeping any fallback as a "
           "post-switch return:" << violation_report;
}
