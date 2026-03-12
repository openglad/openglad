#include <openglad/resources/io.h>
#include "test_framework.h"

#include <cstdlib>
#include <string>

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


TEST(IoFuncs, io_explode_empty)
{
    auto result = explode("", ',');
    // Should return 1 empty string or empty list
    (void)result;
}


TEST(IoFuncs, io_explode_trailing_delimiter)
{
    auto result = explode("a,b,", ',');
    ASSERT_TRUE(result.size() >= 2) << "at least 2 parts";
}


// ---------------------------------------------------------------------------
// get_user_path / get_asset_path
// ---------------------------------------------------------------------------

TEST(IoFuncs, io_get_user_path)
{
    std::string path = get_user_path();
    ASSERT_TRUE(!path.empty()) << "user path not empty";
}


TEST(IoFuncs, io_get_user_path_nonempty)
{
    std::string path = get_user_path();
    // Should have some characters
    ASSERT_TRUE(path.size() > 1) << "user path has content";
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

TEST(IoFuncs, io_list_files)
{
    auto files = list_files("cfg/");
    // May or may not have files depending on mount state
    (void)files;
}


// ---------------------------------------------------------------------------
// get_mounted_campaign
// ---------------------------------------------------------------------------

TEST(IoFuncs, io_get_mounted_campaign)
{
    std::string campaign = get_mounted_campaign();
    // May be empty or have a value
    (void)campaign;
}


// ---------------------------------------------------------------------------
// list_campaigns
// ---------------------------------------------------------------------------

TEST(IoFuncs, io_list_campaigns)
{
    auto campaigns = list_campaigns();
    (void)campaigns;
}


// ---------------------------------------------------------------------------
// list_levels
// ---------------------------------------------------------------------------

TEST(IoFuncs, io_list_levels)
{
    auto levels = list_levels();
    (void)levels;
}


TEST(IoFuncs, io_list_levels_v)
{
    auto levels = list_levels_v();
    (void)levels;
}

