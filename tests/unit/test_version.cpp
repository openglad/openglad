/* The build stamp's shape.
 *
 * og::version is the only accessor for the generated numbers, and three
 * player-facing surfaces are composed from it (the main-menu stamp, the
 * help footer, `-v`). This pins the FORMAT — nothing here can pin the
 * value, which changes with every commit — plus the composition rule the
 * surfaces rely on. The one value check is opt-in: the CI test lane sets
 * OG_EXPECT_VERSION_MINOR from `git rev-list --count HEAD`, so a build
 * that stamped 2.0 because the checkout was shallow fails there instead of
 * shipping a lying number.
 */
#include <gtest/gtest.h>

#include <openglad/core/version.h>

#include <cstdlib>
#include <format>
#include <regex>
#include <string>

TEST(Version, major_is_the_current_generation)
{
    EXPECT_EQ(2, og::version::major());
}

TEST(Version, string_is_major_dot_minor)
{
    const std::string s(og::version::string());
    EXPECT_TRUE(std::regex_match(s, std::regex(R"(^2\.[0-9]+$)")))
        << "version string " << s << " is not 2.<count>";
    EXPECT_EQ(std::format("{}.{}", og::version::major(), og::version::minor()),
              s);
    EXPECT_GE(og::version::minor(), 0);
}

TEST(Version, git_hash_is_a_short_sha_or_nogit)
{
    const std::string h(og::version::git_hash());
    EXPECT_TRUE(std::regex_match(h, std::regex(R"(^([0-9a-f]{8}\+?|nogit)$)")))
        << "git hash " << h << " is neither a short sha nor nogit";
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
