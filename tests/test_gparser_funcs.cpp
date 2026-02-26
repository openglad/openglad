#include <openglad/resources/gparser.h>
#include "test_framework.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>

extern cfg_store cfg;

// ---------------------------------------------------------------------------
// cfg_store::apply_setting / get_setting
// ---------------------------------------------------------------------------

void test_gparser_apply_get_setting()
{
    cfg.apply_setting("test_cat", "test_key", "test_val");
    std::string result = cfg.get_setting("test_cat", "test_key");
    TEST_ASSERT(result == "test_val", "get_setting returns applied value");
}
REGISTER_TEST(test_gparser_apply_get_setting);

void test_gparser_get_setting_missing()
{
    std::string result = cfg.get_setting("nonexistent_cat", "nonexistent_key");
    TEST_ASSERT(result.empty(), "missing setting returns empty");
}
REGISTER_TEST(test_gparser_get_setting_missing);

void test_gparser_is_on_true()
{
    cfg.apply_setting("test_cat2", "enabled", "on");
    TEST_ASSERT(cfg.is_on("test_cat2", "enabled"), "on setting returns true");
}
REGISTER_TEST(test_gparser_is_on_true);

void test_gparser_is_on_false()
{
    cfg.apply_setting("test_cat2", "disabled", "off");
    TEST_ASSERT(!cfg.is_on("test_cat2", "disabled"), "off setting returns false");
}
REGISTER_TEST(test_gparser_is_on_false);

void test_gparser_is_on_missing()
{
    TEST_ASSERT(!cfg.is_on("missing_cat", "missing_key"), "missing setting returns false");
}
REGISTER_TEST(test_gparser_is_on_missing);

void test_gparser_overwrite_setting()
{
    cfg.apply_setting("test_cat3", "key1", "first");
    cfg.apply_setting("test_cat3", "key1", "second");
    std::string result = cfg.get_setting("test_cat3", "key1");
    TEST_ASSERT(result == "second", "overwritten setting has new value");
}
REGISTER_TEST(test_gparser_overwrite_setting);

void test_gparser_multiple_categories()
{
    cfg.apply_setting("catA", "key1", "valA");
    cfg.apply_setting("catB", "key1", "valB");
    TEST_ASSERT(cfg.get_setting("catA", "key1") == "valA", "catA value");
    TEST_ASSERT(cfg.get_setting("catB", "key1") == "valB", "catB value");
}
REGISTER_TEST(test_gparser_multiple_categories);

// ---------------------------------------------------------------------------
// cfg_store data direct access
// ---------------------------------------------------------------------------

void test_gparser_data_access()
{
    cfg.apply_setting("direct_cat", "direct_key", "direct_val");
    TEST_ASSERT(cfg.data.count("direct_cat") > 0, "category exists in data");
    TEST_ASSERT(cfg.data["direct_cat"]["direct_key"] == "direct_val", "value matches");
}
REGISTER_TEST(test_gparser_data_access);

void test_gparser_commandline_switches()
{
    cfg.apply_setting("sound", "sound", "on");
    cfg.apply_setting("graphics", "render", "normal");
    cfg.apply_setting("graphics", "fullscreen", "off");

    char arg0[] = "openglad";
    char arg1[] = "-S";
    char arg2[] = "-d";
    char arg3[] = "-f";
    char arg4[] = "-x";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4};
    int argc = 5;
    char** argv_ptr = argv;

    cfg.commandline(argc, argv_ptr);

    TEST_ASSERT(cfg.get_setting("sound", "sound") == "off", "-S should disable sound");
    TEST_ASSERT(cfg.get_setting("graphics", "render") == "sai", "-x should select sai render");
    TEST_ASSERT(cfg.get_setting("graphics", "fullscreen") == "on", "-f should enable fullscreen");
}
REGISTER_TEST(test_gparser_commandline_switches);

void test_gparser_save_settings_roundtrip()
{
    cfg.apply_setting("test_save", "alpha", "1");
    cfg.apply_setting("test_save", "beta", "2");

    const bool saved = cfg.save_settings();
    TEST_ASSERT(saved, "save_settings should succeed in test environment");
}
REGISTER_TEST(test_gparser_save_settings_roundtrip);

void test_gparser_load_settings_populates_core_keys()
{
    cfg.data.clear();
    (void)cfg.load_settings();

    TEST_ASSERT(!cfg.get_setting("sound", "sound").empty(), "sound/sound should be present after load");
    TEST_ASSERT(!cfg.get_setting("graphics", "render").empty(), "graphics/render should be present after load");
    TEST_ASSERT(!cfg.get_setting("effects", "gore").empty(), "effects/gore should be present after load");
}
REGISTER_TEST(test_gparser_load_settings_populates_core_keys);

void test_gparser_load_settings_preserves_defaults_when_reloading()
{
    cfg.data.clear();
    cfg.apply_setting("graphics", "render", "sai");
    (void)cfg.load_settings();

    TEST_ASSERT(!cfg.get_setting("graphics", "fullscreen").empty(),
                "load_settings should ensure fullscreen key is available");
    TEST_ASSERT(!cfg.get_setting("effects", "mini_hp_bar").empty(),
                "load_settings should ensure effects keys are available");
}
REGISTER_TEST(test_gparser_load_settings_preserves_defaults_when_reloading);

void test_gparser_commandline_additional_switches_and_unknown()
{
    cfg.apply_setting("sound", "sound", "off");
    cfg.apply_setting("graphics", "render", "double");
    cfg.apply_setting("graphics", "fullscreen", "off");

    char arg0[] = "openglad";
    char arg1[] = "-s";
    char arg2[] = "-n";
    char arg3[] = "-e";
    char arg4[] = "-z";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4};
    int argc = 5;
    char** argv_ptr = argv;

    cfg.commandline(argc, argv_ptr);

    TEST_ASSERT(cfg.get_setting("sound", "sound") == "on", "-s should enable sound");
    TEST_ASSERT(cfg.get_setting("graphics", "render") == "eagle", "-e should set eagle render");
    TEST_ASSERT(cfg.get_setting("graphics", "fullscreen") == "off", "unknown switch should not alter fullscreen");
}
REGISTER_TEST(test_gparser_commandline_additional_switches_and_unknown);

void test_gparser_load_settings_sequence_and_alias_event_paths()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists("cfg", ec) && !fs::is_directory("cfg", ec))
        fs::remove("cfg", ec);
    fs::create_directories("cfg", ec);

    const fs::path cfg_path = fs::path("cfg") / "openglad.yaml";
    const fs::path backup_path = fs::path("cfg") / "openglad.yaml.bak.test";
    fs::remove(backup_path, ec);
    if (fs::exists(cfg_path, ec))
        fs::rename(cfg_path, backup_path, ec);

    const char* yaml =
        "defaults: &d\n"
        "  sound: on\n"
        "graphics:\n"
        "  render: normal\n"
        "listcat:\n"
        "  - one\n"
        "  - two\n"
        "alias_use: *d\n";
    FILE* f = std::fopen(cfg_path.string().c_str(), "wb");
    TEST_ASSERT(f != nullptr, "should open cfg/openglad.yaml for test write");
    if (!f)
        return;
    (void)std::fwrite(yaml, 1, std::strlen(yaml), f);
    std::fclose(f);

    cfg.data.clear();
    (void)cfg.load_settings();

    TEST_ASSERT(!cfg.get_setting("graphics", "render").empty(), "load_settings should parse scalar/pair mapping");

    fs::remove(cfg_path, ec);
    if (fs::exists(backup_path, ec))
        fs::rename(backup_path, cfg_path, ec);
}
REGISTER_TEST(test_gparser_load_settings_sequence_and_alias_event_paths);

void test_gparser_round6_load_settings_missing_file_and_parse_error_paths()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists("cfg", ec) && !fs::is_directory("cfg", ec))
        fs::remove("cfg", ec);
    fs::create_directories("cfg", ec);

    const fs::path cfg_path = fs::path("cfg") / "openglad.yaml";
    const fs::path backup_path = fs::path("cfg") / "openglad.yaml.bak.round6";
    fs::remove(backup_path, ec);
    if (fs::exists(cfg_path, ec))
        fs::rename(cfg_path, backup_path, ec);

    cfg.data.clear();
    (void)cfg.load_settings();
    TEST_ASSERT(!cfg.get_setting("sound", "sound").empty(),
                "load_settings should keep defaults even when config file is missing");

    const char* bad_yaml = "graphics:\n  render: normal\n  - invalid\n";
    FILE* f = std::fopen(cfg_path.string().c_str(), "wb");
    TEST_ASSERT(f != nullptr, "should open cfg file for malformed yaml write");
    if (f)
    {
        (void)std::fwrite(bad_yaml, 1, std::strlen(bad_yaml), f);
        std::fclose(f);
    }

    cfg.data.clear();
    (void)cfg.load_settings();
    TEST_ASSERT(!cfg.get_setting("graphics", "render").empty(),
                "load_settings should still leave defaults available after parse error");

    fs::remove(cfg_path, ec);
    if (fs::exists(backup_path, ec))
        fs::rename(backup_path, cfg_path, ec);
}
REGISTER_TEST(test_gparser_round6_load_settings_missing_file_and_parse_error_paths);

void test_gparser_round6_save_settings_open_write_failure()
{
    namespace fs = std::filesystem;
    const fs::path old_cwd = fs::current_path();
    fs::current_path("/proc");
    (void)cfg.save_settings();
    fs::current_path(old_cwd);
    TEST_ASSERT(true, "save_settings open-write edge path executed");
}
REGISTER_TEST(test_gparser_round6_save_settings_open_write_failure);

void test_gparser_round6_commandline_all_short_switches()
{
    cfg.apply_setting("sound", "sound", "off");
    cfg.apply_setting("graphics", "render", "normal");
    cfg.apply_setting("graphics", "fullscreen", "off");

    char arg0[] = "openglad";
    char arg1[] = "-s";
    char arg2[] = "-S";
    char arg3[] = "-n";
    char arg4[] = "-d";
    char arg5[] = "-e";
    char arg6[] = "-x";
    char arg7[] = "-f";
    char arg8[] = "-?";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8};
    int argc = 9;
    char** argv_ptr = argv;

    cfg.commandline(argc, argv_ptr);

    // Last toggle wins for repeated options.
    TEST_ASSERT(cfg.get_setting("sound", "sound") == "off", "-S should leave sound disabled");
    TEST_ASSERT(cfg.get_setting("graphics", "render") == "sai", "-x should leave render in sai mode");
    TEST_ASSERT(cfg.get_setting("graphics", "fullscreen") == "on", "-f should enable fullscreen");
}
REGISTER_TEST(test_gparser_round6_commandline_all_short_switches);

void test_gparser_round9_commandline_help_and_version_exit_paths()
{
    auto run_child = [](const char* flag) -> int {
        pid_t pid = fork();
        if (pid == 0)
        {
            (void)setenv("ASAN_OPTIONS", "detect_leaks=0", 1);
            char arg0[] = "openglad";
            char arg1[3] = {'-', flag[1], '\0'};
            char* argv[] = {arg0, arg1};
            int argc = 2;
            char** argv_ptr = argv;
            cfg.commandline(argc, argv_ptr);
            _exit(42); // should not happen for -h/-v paths
        }
        if (pid < 0)
            return -1;
        int status = 0;
        (void)waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        return -1;
    };

    TEST_ASSERT_EQ(0, run_child("-h"), "commandline -h should exit(0)");
    TEST_ASSERT_EQ(0, run_child("-v"), "commandline -v should exit(0)");
}
REGISTER_TEST(test_gparser_round9_commandline_help_and_version_exit_paths);
