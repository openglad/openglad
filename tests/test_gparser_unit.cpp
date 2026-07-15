#include <openglad/resources/gparser.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

std::string get_asset_path();

namespace
{
class ScopedCurrentPath
{
public:
    explicit ScopedCurrentPath(const std::filesystem::path& next)
        : old_(std::filesystem::current_path())
    {
        std::filesystem::current_path(next);
    }

    ~ScopedCurrentPath()
    {
        std::error_code ec;
        std::filesystem::current_path(old_, ec);
    }

private:
    std::filesystem::path old_;
};

class ScopedPathMove
{
public:
    ScopedPathMove(std::filesystem::path source, std::filesystem::path backup)
        : source_(std::move(source)), backup_(std::move(backup)), moved_(false)
    {
        std::error_code ec;
        if (std::filesystem::exists(source_, ec))
        {
            std::filesystem::remove(backup_, ec);
            std::filesystem::rename(source_, backup_, ec);
            moved_ = !ec;
        }
    }

    ~ScopedPathMove()
    {
        if (!moved_)
            return;
        std::error_code ec;
        std::filesystem::rename(backup_, source_, ec);
    }

private:
    std::filesystem::path source_;
    std::filesystem::path backup_;
    bool moved_;
};

std::filesystem::path unit_config_dir()
{
    const char* dir = std::getenv("OPENGLAD_CONFIG_DIR");
    return dir != nullptr ? std::filesystem::path(dir) : std::filesystem::path();
}

std::filesystem::path unit_config_file()
{
    return unit_config_dir() / "cfg" / "openglad.yaml";
}

// Another suite in this binary (test_physfs_wrappers) deliberately leaves
// PhysFS sabotaged (mounts destroyed, write dir redirected) to simulate a
// fatal-assert bail. cfg load/save resolves "cfg/openglad.yaml" through
// PhysFS first and falls back to the cwd (the REPO checkout under ctest) —
// so a sabotaged predecessor makes these tests read/clobber the repo's
// cfg/openglad.yaml under --gtest_shuffle. Re-establish the unit_main
// contract before any test that loads or saves settings.
void heal_unit_filesystem()
{
    const std::string user_path = get_user_path();
    ASSERT_TRUE(og::resources::set_write_dir(user_path));
    // Fails harmlessly when the user dir is still mounted.
    (void)og::resources::mount(user_path.c_str(), nullptr, 1);
}

void write_unit_config(const char* text)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(unit_config_file().parent_path(), ec);

    FILE* file = std::fopen(unit_config_file().string().c_str(), "wb");
    ASSERT_TRUE(file != nullptr) << "unit config file should be writable";
    ASSERT_EQ(std::strlen(text), std::fwrite(text, 1, std::strlen(text), file));
    std::fclose(file);
}
} // namespace

TEST(GparserUnit, gparser_apply_get_and_is_on_paths)
{
    cfg_store local_cfg;
    local_cfg.apply_setting("sound", "sound", "on");
    local_cfg.apply_setting("graphics", "fullscreen", "off");

    ASSERT_TRUE(local_cfg.get_setting("sound", "sound") == "on");
    ASSERT_TRUE(local_cfg.get_setting("graphics", "fullscreen") == "off");
    ASSERT_TRUE(local_cfg.get_setting("graphics", "missing_key").empty());
    ASSERT_TRUE(local_cfg.get_setting("missing_cat", "missing_key").empty());
    ASSERT_TRUE(local_cfg.is_on("sound", "sound"));
    ASSERT_TRUE(!local_cfg.is_on("graphics", "fullscreen"));
}

TEST(GparserUnit, gparser_commandline_switches_and_unknown_arg)
{
    cfg_store local_cfg;

    std::vector<std::string> arg_storage = {
        "openglad",
        "-s",
        "-S",
        "-n",
        "-d",
        "-e",
        "-x",
        "-f",
        "-q"
    };
    std::vector<char*> argv_buf;
    argv_buf.reserve(arg_storage.size());
    for (std::string& s : arg_storage)
        argv_buf.push_back(s.data());

    int argc = static_cast<int>(argv_buf.size());
    char** argv = argv_buf.data();
    local_cfg.commandline(argc, argv);

    ASSERT_TRUE(local_cfg.get_setting("sound", "sound") == "off");
    ASSERT_TRUE(local_cfg.get_setting("graphics", "render") == "sai");
    ASSERT_EQ("sai", local_cfg.get_setting("graphics", "smoothing"));
    ASSERT_TRUE(local_cfg.get_setting("graphics", "fullscreen") == "on");
}

TEST(GparserUnit, gparser_show_fps_flag)
{
    cfg_store local_cfg;

    std::vector<std::string> arg_storage = {
        "openglad",
        "--show-fps"
    };
    std::vector<char*> argv_buf;
    argv_buf.reserve(arg_storage.size());
    for (std::string& s : arg_storage)
        argv_buf.push_back(s.data());

    int argc = static_cast<int>(argv_buf.size());
    char** argv = argv_buf.data();
    local_cfg.commandline(argc, argv);

    // The flag is honored at runtime...
    ASSERT_TRUE(local_cfg.is_on("graphics", "show_fps"));

    // ...but must NOT leak into the persisted config. save_settings() serializes
    // exactly `data`, so "not persisted" == "absent from data". Regression:
    // --show-fps used to write data["graphics"]["show_fps"], which then got
    // baked into the user's openglad.yaml on the next settings save.
    auto cat = local_cfg.data.find("graphics");
    const bool show_fps_in_data =
        cat != local_cfg.data.end() && cat->second.count("show_fps") > 0;
    ASSERT_FALSE(show_fps_in_data)
        << "--show-fps must be an ephemeral override, not a persisted setting";

    // It lives in the transient overrides map instead.
    auto ocat = local_cfg.overrides.find("graphics");
    ASSERT_TRUE(ocat != local_cfg.overrides.end()
                && ocat->second.count("show_fps") > 0)
        << "--show-fps should be recorded as a transient override";
}

TEST(GparserUnit, gparser_override_precedence_and_non_persistence)
{
    cfg_store local_cfg;

    // A persisted value...
    local_cfg.apply_setting("graphics", "show_fps", "off");
    ASSERT_FALSE(local_cfg.is_on("graphics", "show_fps"));

    // ...is shadowed by a transient override for reads...
    local_cfg.apply_override("graphics", "show_fps", "on");
    ASSERT_TRUE(local_cfg.is_on("graphics", "show_fps"));
    ASSERT_TRUE(local_cfg.get_setting("graphics", "show_fps") == "on");

    // ...without mutating the persisted value that save_settings() would write.
    ASSERT_TRUE(local_cfg.data["graphics"]["show_fps"] == "off");
}

TEST(GparserUnit, gparser_load_settings_reports_missing_config_and_keeps_defaults)
{
    namespace fs = std::filesystem;
    ASSERT_FALSE(unit_config_dir().empty()) << "unit runner should set OPENGLAD_CONFIG_DIR";

    std::error_code ec;
    fs::remove(unit_config_file(), ec);

    const fs::path isolated_cwd = fs::temp_directory_path() /
        ("openglad_gparser_missing_" + std::to_string(getpid()));
    fs::create_directories(isolated_cwd, ec);
    ScopedCurrentPath cwd_guard(isolated_cwd);
    const fs::path asset_cfg = fs::path(get_asset_path()) / "cfg" / "openglad.yaml";
    ScopedPathMove asset_guard(asset_cfg, asset_cfg.parent_path() / "openglad.yaml.gparser-unit-bak");

    cfg_store local_cfg;
    ASSERT_FALSE(local_cfg.load_settings()) << "missing config should report fallback to defaults";
    ASSERT_EQ("on", local_cfg.get_setting("sound", "sound"));
    ASSERT_EQ("normal", local_cfg.get_setting("graphics", "render"));
    ASSERT_EQ("on", local_cfg.get_setting("effects", "gore"));

    fs::remove_all(isolated_cwd, ec);
    fs::create_directories(unit_config_file().parent_path(), ec);
}

TEST(GparserUnit, gparser_load_settings_parses_mapping_sequence_and_alias)
{
    heal_unit_filesystem();
    const char* valid_yaml =
        "sound:\n"
        "  sound: off\n"
        "graphics:\n"
        "  render: eagle\n"
        "  fullscreen: off\n"
        "defaults: &defaults\n"
        "  gore: off\n"
        "listcat:\n"
        "  - one\n"
        "  - two\n"
        "alias_use: *defaults\n";
    write_unit_config(valid_yaml);

    cfg_store local_cfg;
    ASSERT_TRUE(local_cfg.load_settings());
    ASSERT_EQ("off", local_cfg.get_setting("sound", "sound"));
    ASSERT_EQ("eagle", local_cfg.get_setting("graphics", "render"));
    ASSERT_EQ("off", local_cfg.get_setting("graphics", "fullscreen"));
}

TEST(GparserUnit, gparser_load_settings_tolerates_stale_floor_ghost_key)
{
    heal_unit_filesystem();
    // graphics/floor_ghost was a real setting once (the ghost view is now
    // hold-look-up only, with no cfg toggle). A user cfg saved by an older
    // build still carries the key; loading must succeed and simply hold the
    // stale key as inert data nothing in the engine reads anymore.
    const char* stale_yaml =
        "graphics:\n"
        "  render: normal\n"
        "  floor_ghost: on\n"
        "sound:\n"
        "  sound: off\n";
    write_unit_config(stale_yaml);

    cfg_store local_cfg;
    ASSERT_TRUE(local_cfg.load_settings())
        << "a cfg with the retired graphics/floor_ghost key must still load";
    ASSERT_EQ("normal", local_cfg.get_setting("graphics", "render"));
    ASSERT_EQ("off", local_cfg.get_setting("sound", "sound"));
    ASSERT_EQ("on", local_cfg.get_setting("graphics", "floor_ghost"))
        << "stale keys are carried as inert data, not rejected";
}

// effects/depth_fx migration shim (load_settings -> migrate_depth_fx): a
// config that predates the selector derives it from the retired boolean
// effects/depth_tint — on carries the intent to the new default treatment
// (fog), off stays off.
TEST(GparserUnit, gparser_depth_fx_migrates_legacy_depth_tint_on_to_fog)
{
    heal_unit_filesystem();
    write_unit_config(
        "effects:\n"
        "  depth_tint: on\n");

    cfg_store local_cfg;
    ASSERT_TRUE(local_cfg.load_settings());
    ASSERT_EQ("fog", local_cfg.get_setting("effects", "depth_fx"))
        << "legacy depth_tint on must migrate to the new default, fog";
    ASSERT_EQ("on", local_cfg.get_setting("effects", "depth_tint"))
        << "the legacy key stays as inert data (stale-key tolerance)";
}

TEST(GparserUnit, gparser_depth_fx_migrates_legacy_depth_tint_off_to_off)
{
    heal_unit_filesystem();
    write_unit_config(
        "effects:\n"
        "  depth_tint: off\n");

    cfg_store local_cfg;
    ASSERT_TRUE(local_cfg.load_settings());
    ASSERT_EQ("off", local_cfg.get_setting("effects", "depth_fx"))
        << "a user who turned the old tint off must stay depth-effect-free";
}

TEST(GparserUnit, gparser_depth_fx_defaults_to_fog_when_neither_key_present)
{
    heal_unit_filesystem();
    write_unit_config(
        "effects:\n"
        "  gore: on\n");

    cfg_store local_cfg;
    ASSERT_TRUE(local_cfg.load_settings());
    ASSERT_EQ("fog", local_cfg.get_setting("effects", "depth_fx"))
        << "a config that knew neither key gets the plain default";

    // The same default applies on the missing-config path: load_settings
    // runs the migration on every exit.
    cfg_store defaults_cfg;
    defaults_cfg.migrate_depth_fx();
    ASSERT_EQ("fog", defaults_cfg.get_setting("effects", "depth_fx"));
}

TEST(GparserUnit, gparser_depth_fx_present_key_wins_and_legacy_key_is_inert)
{
    heal_unit_filesystem();
    // A config written by the new build can still carry the stale legacy
    // key: depth_fx always wins, and a REPEAT migration never rewrites it.
    write_unit_config(
        "effects:\n"
        "  depth_fx: mist\n"
        "  depth_tint: on\n");

    cfg_store local_cfg;
    ASSERT_TRUE(local_cfg.load_settings());
    ASSERT_EQ("mist", local_cfg.get_setting("effects", "depth_fx"))
        << "an explicit depth_fx must never be clobbered by the legacy key";
    local_cfg.migrate_depth_fx();
    ASSERT_EQ("mist", local_cfg.get_setting("effects", "depth_fx"))
        << "migration must be idempotent once the key exists";
    ASSERT_EQ("on", local_cfg.get_setting("effects", "depth_tint"))
        << "the stale legacy key is carried as inert data, not rejected";
}

TEST(GparserUnit, gparser_save_settings_writes_only_persisted_data_and_reports_open_failure)
{
    heal_unit_filesystem();
    namespace fs = std::filesystem;
    ASSERT_FALSE(unit_config_dir().empty()) << "unit runner should set OPENGLAD_CONFIG_DIR";

    std::error_code ec;
    fs::create_directories(unit_config_file().parent_path(), ec);
    fs::remove(unit_config_file(), ec);

    cfg_store local_cfg;
    local_cfg.apply_setting("graphics", "render", "sai");
    local_cfg.apply_override("graphics", "show_fps", "on");
    ASSERT_TRUE(local_cfg.save_settings());

    std::ifstream saved(unit_config_file());
    ASSERT_TRUE(saved.good()) << "save_settings should write cfg/openglad.yaml";
    const std::string contents((std::istreambuf_iterator<char>(saved)),
                               std::istreambuf_iterator<char>());
    ASSERT_NE(std::string::npos, contents.find("render"));
    ASSERT_NE(std::string::npos, contents.find("sai"));
    ASSERT_EQ(std::string::npos, contents.find("show_fps"))
        << "transient overrides must not be persisted";

    const fs::path cfg_dir = unit_config_file().parent_path();
    fs::remove(unit_config_file(), ec);
    fs::remove_all(cfg_dir, ec);

    const fs::path isolated_cwd = fs::temp_directory_path() /
        ("openglad_gparser_save_fail_" + std::to_string(getpid()));
    fs::create_directories(isolated_cwd, ec);
    ScopedCurrentPath cwd_guard(isolated_cwd);

    cfg_store failing_cfg;
    failing_cfg.apply_setting("graphics", "render", "normal");
    ASSERT_FALSE(failing_cfg.save_settings())
        << "missing cfg directory should report write failure";

    fs::remove_all(isolated_cwd, ec);
    fs::create_directories(cfg_dir, ec);
}

TEST(GparserUnit, gparser_commandline_help_and_version_exit_paths)
{
    auto run_child = [](const char* flag) -> int {
        pid_t pid = fork();
        if (pid == 0)
        {
            cfg_store local_cfg;
            std::vector<std::string> args = {"openglad", flag};
            std::vector<char*> argv_buf;
            argv_buf.reserve(args.size());
            for (std::string& s : args)
                argv_buf.push_back(s.data());
            int argc = static_cast<int>(argv_buf.size());
            char** argv = argv_buf.data();
            local_cfg.commandline(argc, argv); // expected to call exit(0)
            _exit(7);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return status;
    };

    const int help_status = run_child("-h");
    ASSERT_TRUE(WIFEXITED(help_status));
    ASSERT_TRUE(WEXITSTATUS(help_status) == 0);

    const int version_status = run_child("-v");
    ASSERT_TRUE(WIFEXITED(version_status));
    ASSERT_TRUE(WEXITSTATUS(version_status) == 0);
}
