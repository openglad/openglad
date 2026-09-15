/* The build stamp's shape.
 *
 * og::version is the only accessor for the generated numbers, and three
 * player-facing surfaces are composed from it (the main-menu stamp, the
 * help footer, `-v`). Nothing here can pin the VALUE, which changes with
 * every commit; what is pinned is the exact composition each surface is
 * built from — `std::format("{}.{}", major(), minor())` for the version
 * string, "v<string> <hash>" for the stamp, "openglad version <string>
 * (<hash>)" for `-v` — plus the shape of the generated git hash. The one
 * value check is opt-in: the CI test lane sets OG_EXPECT_VERSION_MINOR from
 * `git rev-list --count HEAD`, so a build that stamped 2.0 because the
 * checkout was shallow fails there instead of shipping a lying number.
 *
 * The hash shape is checked by two hand-written predicates rather than
 * <regex>: libstdc++'s <regex> under GCC 15 with the nix cc-wrapper's
 * injected -O2 plus ASan/UBSan trips -Werror=maybe-uninitialized inside
 * bits/std_function.h, which makes this file (and two others) unbuildable
 * on the local ci-asan preset. scripts/check_no_std_regex.sh keeps it out.
 * The predicates have their own table test below, so they have teeth too.
 */
#include <gtest/gtest.h>

#include <openglad/core/version.h>

#include <cstddef>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

namespace
{

bool is_lowercase_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// The shape cmake/OpenGladVersion.cmake stamps: `git rev-parse --short=8
// HEAD`, with a single '+' appended when the tree was dirty. Nothing else.
bool is_short_sha(std::string_view h)
{
    if (h.size() != 8 && h.size() != 9)
        return false;
    if (h.size() == 9 && h.back() != '+')
        return false;
    for (std::size_t i = 0; i < 8; ++i)
    {
        if (!is_lowercase_hex(h[i]))
            return false;
    }
    return true;
}

// "nogit" is the legitimate stamp for a tree with no history (a source
// tarball); it is the ONLY non-sha spelling the generator may produce.
bool is_short_sha_or_nogit(std::string_view h)
{
    return is_short_sha(h) || h == "nogit";
}

} // namespace

TEST(Version, major_is_the_current_generation)
{
    EXPECT_EQ(2, og::version::major());
}

TEST(Version, string_is_major_dot_minor)
{
    const std::string s(og::version::string());
    EXPECT_EQ(std::format("{}.{}", og::version::major(), og::version::minor()),
              s);
    EXPECT_GE(og::version::minor(), 0);
}

TEST(Version, short_sha_predicate_accepts_only_eight_lowercase_hex_with_optional_plus)
{
    struct Row
    {
        const char* text;
        bool sha;      // is_short_sha
        bool sha_or_nogit;
    };
    const Row rows[] = {
        {"7e7f4079", true, true},
        {"7e7f4079+", true, true},
        {"00000000", true, true},
        {"abcdef01", true, true},
        {"nogit", false, true},   // legal stamp, never a sha
        {"", false, false},
        {"7e7f407", false, false},        // seven digits
        {"7e7f40799", false, false},      // nine digits, no '+'
        {"7E7F4079", false, false},       // uppercase
        {"7e7f4079++", false, false},     // two dirty markers
        {"+7e7f4079", false, false},      // marker on the wrong end
        {"7e7f407g", false, false},       // 'g' is not hex
        {"nogit+", false, false},         // "nogit" takes no dirty marker
        {"NOGIT", false, false},
    };
    for (const Row& row : rows)
    {
        EXPECT_EQ(row.sha, is_short_sha(row.text))
            << "is_short_sha(\"" << row.text << "\")";
        EXPECT_EQ(row.sha_or_nogit, is_short_sha_or_nogit(row.text))
            << "is_short_sha_or_nogit(\"" << row.text << "\")";
    }
}

TEST(Version, git_hash_is_a_short_sha_or_nogit)
{
    const std::string h(og::version::git_hash());
    EXPECT_TRUE(is_short_sha_or_nogit(h))
        << "git hash " << h << " is neither a short sha nor nogit";
}

// "nogit" is the legitimate stamp for a build with no history (a source
// tarball), but on a git checkout it is a LYING stamp — and the format test
// above accepts it, so a broken hash probe (wrong working directory, a
// quoting regression, git missing from the image) would otherwise ship
// "v2.N nogit" unnoticed. Opt in on the same signal minor_matches_history
// uses: the CI test lane exports OG_EXPECT_VERSION_MINOR from
// `git rev-list --count HEAD`, which only a tree WITH git history can do.
TEST(Version, git_hash_is_a_real_sha_when_the_tree_has_history)
{
    const char* has_history = std::getenv("OG_EXPECT_VERSION_MINOR");
    if (has_history == nullptr || *has_history == '\0')
    {
        GTEST_SKIP() << "OG_EXPECT_VERSION_MINOR unset; nothing proves this "
                     << "build tree has git history, so \"nogit\" is legal";
    }
    const std::string h(og::version::git_hash());
    EXPECT_TRUE(is_short_sha(h))
        << "the tree has git history (OG_EXPECT_VERSION_MINOR=" << has_history
        << ") but the build stamped '" << h
        << "': a git checkout must never stamp nogit";
}

TEST(Version, stamp_is_the_menu_line)
{
    EXPECT_EQ("v" + std::string(og::version::string()) + " " +
                  std::string(og::version::git_hash()),
              og::version::stamp());
}

TEST(Version, cli_line_is_the_dash_v_line)
{
    EXPECT_EQ("openglad version " + std::string(og::version::string()) + " (" +
                  std::string(og::version::git_hash()) + ")",
              og::version::cli_line());
}

TEST(Version, minor_matches_history)
{
    const char* expected = std::getenv("OG_EXPECT_VERSION_MINOR");
    if (expected == nullptr || *expected == '\0')
    {
        GTEST_SKIP() << "OG_EXPECT_VERSION_MINOR unset; the format tests "
                     << "above still ran";
    }
    EXPECT_EQ(std::string(expected), std::to_string(og::version::minor()))
        << "the build stamped 2." << og::version::minor()
        << " but the checkout's history says " << expected
        << " (shallow clone, or a stale build tree)";
}
