#include <openglad/resources/io.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <list>
#include <string>
#include <vector>

namespace {
class ScopedEnvVar {
public:
    explicit ScopedEnvVar(const char* name) : name_(name) {
        const char* current = std::getenv(name_);
        if (current) {
            had_value_ = true;
            old_value_ = current;
        }
    }

    ~ScopedEnvVar() {
        if (had_value_) {
#ifdef _WIN32
            _putenv_s(name_, old_value_.c_str());
#else
            setenv(name_, old_value_.c_str(), 1);
#endif
        } else {
#ifdef _WIN32
            _putenv_s(name_, "");
#else
            unsetenv(name_);
#endif
        }
    }

private:
    const char* name_;
    bool had_value_ = false;
    std::string old_value_;
};
} // namespace

// ---------------------------------------------------------------------------
// explode() - string splitting utility
// ---------------------------------------------------------------------------

TEST(IoFuncs, io_explode_basic)
{
    auto result = explode("hello,world,foo", ',');
    ASSERT_EQ(3, (int)result.size()) << "3 parts";
    auto it = result.begin();
    ASSERT_TRUE(*it == "hello") << "first part";
    ++it;
    ASSERT_TRUE(*it == "world") << "second part";
    ++it;
    ASSERT_TRUE(*it == "foo") << "third part";
}


TEST(IoFuncs, io_explode_no_delimiter)
{
    auto result = explode("nodots", '.');
    ASSERT_EQ(1, (int)result.size()) << "no delimiter = 1 part";
    ASSERT_TRUE(result.front() == "nodots") << "whole string";
}


// explode() pushes the final substring unconditionally, so the empty input is
// one empty token, never a zero-length list: callers such as
// campaign.description = explode(metadata.description, '\n') index front().
TEST(IoFuncs, io_explode_empty_yields_one_empty_token)
{
    const std::list<std::string> result = explode("", ',');
    ASSERT_EQ(1u, result.size()) << "explode always yields the final (possibly empty) token";
    ASSERT_EQ("", result.front()) << "that token is the empty string";
}


TEST(IoFuncs, io_explode_trailing_delimiter)
{
    const std::list<std::string> result = explode("a,b,", ',');
    ASSERT_EQ(3u, result.size()) << "the trailing delimiter mints an empty final token";
    auto it = result.begin();
    ASSERT_EQ("a", *it++) << "first token";
    ASSERT_EQ("b", *it++) << "second token";
    ASSERT_EQ("", *it) << "the token after the last delimiter is empty";
}


// ---------------------------------------------------------------------------
// get_user_path / get_asset_path
// ---------------------------------------------------------------------------

// Every caller concatenates a relative name straight onto get_user_path()
// (platform_io_common.cpp: std::format("{}temp/scen/scen{}.fss", get_user_path(),
// id)), so the returned directory must always end in exactly one separator --
// "./" when neither OPENGLAD_CONFIG_DIR nor HOME is set.
TEST(IoFuncs, io_get_user_path_ends_in_exactly_one_separator)
{
    const std::string path = get_user_path();
    ASSERT_FALSE(path.empty()) << "user path is never empty";
    ASSERT_EQ('/', path.back()) << "get_user_path is normalized to a trailing slash";
    ASSERT_TRUE(path.size() < 2 || path[path.size() - 2] != '/')
        << "get_user_path is normalized to exactly ONE trailing slash, got: " << path;

    // ... and it names the directory the user's writes actually land in.
    namespace fs = std::filesystem;
    const fs::path probe = fs::path(path) / "io_funcs_user_path_probe.bin";
    std::error_code ec;
    fs::remove(probe, ec);
    SDL_IOStream* out = open_write_file("io_funcs_user_path_probe.bin");
    ASSERT_NE(nullptr, out) << "a bare filename should be writable";
    SDL_CloseIO(out);
    ASSERT_TRUE(fs::exists(probe))
        << "get_user_path names the directory user-dir writes land in, expected: "
        << probe.string();
    fs::remove(probe, ec);
}


TEST(IoFuncs, io_get_user_path_uses_openglad_config_dir_when_set)
{
    ScopedEnvVar scoped("OPENGLAD_CONFIG_DIR");
#ifdef _WIN32
    _putenv_s("OPENGLAD_CONFIG_DIR", "C:/tmp/openglad_test_cfg");
    const char* expected = "C:/tmp/openglad_test_cfg/";
#else
    setenv("OPENGLAD_CONFIG_DIR", "/tmp/openglad_test_cfg", 1);
    const char* expected = "/tmp/openglad_test_cfg/";
#endif

    std::string path = get_user_path();
    ASSERT_STREQ(expected, path.c_str()) << "OPENGLAD_CONFIG_DIR should override default user path";
}


TEST(IoFuncs, io_get_user_path_ignores_empty_openglad_config_dir)
{
    ScopedEnvVar scoped_cfg("OPENGLAD_CONFIG_DIR");
#ifdef _WIN32
    _putenv_s("OPENGLAD_CONFIG_DIR", "");
    std::string path = get_user_path();
    ASSERT_TRUE(!path.empty()) << "empty OPENGLAD_CONFIG_DIR should still produce a non-empty default path";
#else
    ScopedEnvVar scoped_home("HOME");
    setenv("OPENGLAD_CONFIG_DIR", "", 1);
    setenv("HOME", "/tmp/openglad_test_home", 1);
    std::string path = get_user_path();
    ASSERT_STREQ("/tmp/openglad_test_home/.openglad/", path.c_str()) << "empty OPENGLAD_CONFIG_DIR should fall back to HOME/.openglad";
#endif
}


TEST(IoFuncs, io_get_user_path_normalizes_trailing_slashes)
{
    ScopedEnvVar scoped("OPENGLAD_CONFIG_DIR");
#ifdef _WIN32
    _putenv_s("OPENGLAD_CONFIG_DIR", "C:/tmp/openglad_cfg///");
    const char* expected = "C:/tmp/openglad_cfg/";
#else
    setenv("OPENGLAD_CONFIG_DIR", "/tmp/openglad_cfg///", 1);
    const char* expected = "/tmp/openglad_cfg/";
#endif

    std::string path = get_user_path();
    ASSERT_STREQ(expected, path.c_str()) << "OPENGLAD_CONFIG_DIR should normalize repeated trailing slashes";
}


// ---------------------------------------------------------------------------
// list_files
// ---------------------------------------------------------------------------

TEST(IoFuncs, io_list_files_enumerates_bare_names_under_a_mounted_dir)
{
    const std::list<std::string> files = list_files("cfg/");
    ASSERT_FALSE(files.empty()) << "the mounted asset tree ships a cfg/ directory";
    ASSERT_NE(files.end(), std::find(files.begin(), files.end(), std::string("openglad.yaml")))
        << "list_files enumerates bare filenames under the mounted cfg/ dir";

    // Negative control: a directory PhysFS cannot see lists nothing.
    ASSERT_TRUE(list_files("definitely_missing_dir_io_funcs/").empty())
        << "an unmounted directory enumerates nothing";
}


// ---------------------------------------------------------------------------
// get_mounted_campaign
// ---------------------------------------------------------------------------

TEST(IoFuncs, io_get_mounted_campaign_reports_the_id_a_successful_mount_recorded)
{
    const std::string prev = get_mounted_campaign();
#ifdef TESTING
    // Start from a known state: a stale bogus id left by a sibling would make
    // the mount below fail in prev's unmount instead of exercising the record.
    set_mounted_campaign_for_testing("");
#endif
    ASSERT_EQ(CampaignPackageIoError::None, mount_campaign_package_with_error("gladiator"))
        << "the default campaign package should mount";
    ASSERT_EQ("gladiator", get_mounted_campaign())
        << "a successful mount records the mounted id";
#ifdef TESTING
    set_mounted_campaign_for_testing(prev);
#else
    (void)prev;
#endif
}


// list_campaigns()/list_levels()/list_levels_v() are pinned with real fixtures
// and orderings by IoFilesystem.io_list_campaigns_and_levels and
// IoPlatformCoverage.platform_io_batch3_mount_switch_and_listing_filters (both
// in this binary); the bodies that only called them here pinned nothing.

