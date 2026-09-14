#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>
#include <physfs.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern cfg_store cfg;

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

class ScopedPhysfsWriteDir
{
public:
    explicit ScopedPhysfsWriteDir(const std::filesystem::path& next)
    {
        if (const char* old = PHYSFS_getWriteDir())
            old_ = old;
        active_ = PHYSFS_setWriteDir(next.string().c_str()) != 0;
    }

    ~ScopedPhysfsWriteDir()
    {
        if (!active_)
            return;
        if (old_.empty())
            (void)PHYSFS_setWriteDir(nullptr);
        else
            (void)PHYSFS_setWriteDir(old_.c_str());
    }

private:
    std::string old_;
    bool active_ = false;
};

// load_settings() reads "cfg/openglad.yaml" through og_open_read, which asks
// PhysFS FIRST and only then the cwd. Other suites in this binary leave
// their own cfg/openglad.yaml reachable through PhysFS, so a fixture written
// into the cwd is invisible half the time. Prepending a directory to the
// search path (PHYSFS_mount appendToPath = 0) is the only way to be the file
// load_settings actually parses.
class ScopedPrependedMount
{
public:
    explicit ScopedPrependedMount(const std::filesystem::path& dir)
        : dir_(dir.string())
    {
        mounted_ = PHYSFS_mount(dir_.c_str(), nullptr, 0) != 0;
    }

    ~ScopedPrependedMount()
    {
        if (mounted_)
            (void)PHYSFS_unmount(dir_.c_str());
    }

    bool mounted() const { return mounted_; }

private:
    std::string dir_;
    bool mounted_ = false;
};

std::filesystem::path make_isolated_gparser_dir(const char* suffix)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
        ("openglad_gparser_funcs_" + std::string(suffix) + "_" + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "cfg", ec);
    return dir;
}

std::string read_file_text(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

// ---------------------------------------------------------------------------
// cfg_store::apply_setting / get_setting
// ---------------------------------------------------------------------------

TEST(GparserFuncs, gparser_apply_get_setting)
{
    cfg.apply_setting("test_cat", "test_key", "test_val");
    std::string result = cfg.get_setting("test_cat", "test_key");
    ASSERT_TRUE(result == "test_val") << "get_setting returns applied value";
}


TEST(GparserFuncs, gparser_get_setting_missing)
{
    std::string result = cfg.get_setting("nonexistent_cat", "nonexistent_key");
    ASSERT_TRUE(result.empty()) << "missing setting returns empty";
}


TEST(GparserFuncs, gparser_is_on_true)
{
    cfg.apply_setting("test_cat2", "enabled", "on");
    ASSERT_TRUE(cfg.is_on("test_cat2", "enabled")) << "on setting returns true";
}


TEST(GparserFuncs, gparser_is_on_false)
{
    cfg.apply_setting("test_cat2", "disabled", "off");
    ASSERT_TRUE(!cfg.is_on("test_cat2", "disabled")) << "off setting returns false";
}


TEST(GparserFuncs, gparser_is_on_missing)
{
    ASSERT_TRUE(!cfg.is_on("missing_cat", "missing_key")) << "missing setting returns false";
}


TEST(GparserFuncs, gparser_overwrite_setting)
{
    cfg.apply_setting("test_cat3", "key1", "first");
    cfg.apply_setting("test_cat3", "key1", "second");
    std::string result = cfg.get_setting("test_cat3", "key1");
    ASSERT_TRUE(result == "second") << "overwritten setting has new value";
}


TEST(GparserFuncs, gparser_multiple_categories)
{
    cfg.apply_setting("catA", "key1", "valA");
    cfg.apply_setting("catB", "key1", "valB");
    ASSERT_TRUE(cfg.get_setting("catA", "key1") == "valA") << "catA value";
    ASSERT_TRUE(cfg.get_setting("catB", "key1") == "valB") << "catB value";
}


// ---------------------------------------------------------------------------
// cfg_store data direct access
// ---------------------------------------------------------------------------

TEST(GparserFuncs, gparser_data_access)
{
    cfg.apply_setting("direct_cat", "direct_key", "direct_val");
    ASSERT_TRUE(cfg.data.count("direct_cat") > 0) << "category exists in data";
    ASSERT_TRUE(cfg.data["direct_cat"]["direct_key"] == "direct_val") << "value matches";
}


TEST(GparserFuncs, gparser_commandline_switches)
{
    cfg.apply_setting("sound", "sound", "on");
    cfg.apply_setting("graphics", "render", "normal");
    cfg.apply_setting("graphics", "smoothing", "off");
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

    ASSERT_TRUE(cfg.get_setting("sound", "sound") == "off") << "-S should disable sound";
    ASSERT_TRUE(cfg.get_setting("graphics", "render") == "sai") << "-x should select sai render";
    ASSERT_EQ("sai", cfg.get_setting("graphics", "smoothing"));
    ASSERT_TRUE(cfg.get_setting("graphics", "fullscreen") == "on") << "-f should enable fullscreen";
}


TEST(GparserFuncs, gparser_save_settings_roundtrip)
{
    namespace fs = std::filesystem;
    const fs::path isolated_dir = make_isolated_gparser_dir("save_roundtrip");
    const fs::path saved_cfg = isolated_dir / "cfg" / "openglad.yaml";

    {
        ScopedCurrentPath cwd_guard(isolated_dir);
        ScopedPhysfsWriteDir physfs_guard(isolated_dir);

        cfg_store local_cfg;
        local_cfg.apply_setting("test_save", "alpha", "1");
        local_cfg.apply_setting("test_save", "beta", "2");

        const bool saved = local_cfg.save_settings();
        ASSERT_TRUE(saved) << "save_settings should succeed in test environment";
    }

    std::error_code ec;
    ASSERT_TRUE(fs::exists(saved_cfg, ec)) << "save_settings should write cfg/openglad.yaml";
    const std::string contents = read_file_text(saved_cfg);
    ASSERT_NE(std::string::npos, contents.find("test_save"));
    ASSERT_NE(std::string::npos, contents.find("alpha: 1"));
    ASSERT_NE(std::string::npos, contents.find("beta: 2"));
    fs::remove_all(isolated_dir, ec);
}


TEST(GparserFuncs, gparser_load_settings_reapplies_built_in_defaults_over_stale_values)
{
    cfg.data.clear();
    // A stale in-memory value that the mounted cfg/openglad.yaml does NOT
    // carry: the reload's built-in defaults block must overwrite it.
    cfg.apply_setting("graphics", "render", "sai");
    (void)cfg.load_settings();

    ASSERT_EQ("normal", cfg.get_setting("graphics", "render"))
        << "load_settings re-applies graphics/render=normal before parsing, and the "
           "mounted cfg carries no graphics/render key to override it";
    ASSERT_EQ("on", cfg.get_setting("graphics", "fullscreen"))
        << "load_settings must define graphics/fullscreen=on";
    ASSERT_EQ("on", cfg.get_setting("effects", "mini_hp_bar"))
        << "load_settings must define effects/mini_hp_bar=on";
    ASSERT_EQ("on", cfg.get_setting("effects", "gore"))
        << "load_settings must define effects/gore=on";
    ASSERT_EQ("6", cfg.get_setting("gameplay", "timer_wait"))
        << "load_settings must default the sim tick wait to DEFAULT_TIMER_WAIT";
}


TEST(GparserFuncs, gparser_commandline_additional_switches_and_unknown)
{
    cfg.apply_setting("sound", "sound", "off");
    cfg.apply_setting("graphics", "render", "double");
    cfg.apply_setting("graphics", "smoothing", "off");
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

    ASSERT_TRUE(cfg.get_setting("sound", "sound") == "on") << "-s should enable sound";
    ASSERT_TRUE(cfg.get_setting("graphics", "render") == "eagle") << "-e should set eagle render";
    ASSERT_EQ("eagle", cfg.get_setting("graphics", "smoothing"));
    ASSERT_TRUE(cfg.get_setting("graphics", "fullscreen") == "off") << "unknown switch should not alter fullscreen";
}


TEST(GparserFuncs, gparser_load_settings_sequence_and_alias_event_paths)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path fixture_dir = make_isolated_gparser_dir("seq_alias");
    const fs::path cfg_path = fixture_dir / "cfg" / "openglad.yaml";

    // render: eagle (never a built-in default, never in the shipped cfg) is
    // the proof that THIS file -- not the defaults block -- was parsed.
    const char* yaml =
        "top_scalar: yes\n"
        "defaults: &d\n"
        "  sound: on\n"
        "graphics:\n"
        "  render: eagle\n"
        "listcat:\n"
        "  - one\n"
        "  - two\n"
        "alias_use: *d\n";
    FILE* f = std::fopen(cfg_path.string().c_str(), "wb");
    ASSERT_NE(nullptr, f) << "should open the fixture cfg/openglad.yaml for write";
    ASSERT_EQ(std::strlen(yaml), std::fwrite(yaml, 1, std::strlen(yaml), f))
        << "the whole fixture document must reach disk";
    ASSERT_EQ(0, std::fclose(f));

    {
        ScopedPrependedMount fixture_mount(fixture_dir);
        ASSERT_TRUE(fixture_mount.mounted())
            << "the fixture directory must win the PhysFS search path";

        cfg.data.clear();
        (void)cfg.load_settings();
    }
    fs::remove_all(fixture_dir, ec);

    ASSERT_EQ("eagle", cfg.get_setting("graphics", "render"))
        << "a MAPPING value is recursed into as a category and overrides the default";
    ASSERT_EQ("yes", cfg.get_setting("", "top_scalar"))
        << "a scalar root pair lands in the empty-string category";
    ASSERT_EQ("on", cfg.get_setting("defaults", "sound"))
        << "the anchored mapping is applied under its own key";
    ASSERT_EQ("on", cfg.get_setting("alias_use", "sound"))
        << "libyaml resolves *d to the anchored mapping node, so the alias "
           "populates a second category with the same pairs";
    ASSERT_EQ(0u, cfg.data.count("listcat"))
        << "a SEQUENCE value is neither scalar nor mapping and must be ignored";
}


TEST(GparserFuncs, gparser_round6_load_settings_existing_file_reports_success)
{
    cfg_store existing_cfg;
    const bool loaded_existing = existing_cfg.load_settings();
    ASSERT_TRUE(loaded_existing) << "integration runner should provide a mounted config file";
    ASSERT_EQ("on", existing_cfg.get_setting("sound", "sound"));
    ASSERT_EQ("normal", existing_cfg.get_setting("graphics", "render"));
    // Merged from gparser_load_settings_populates_core_keys: the core keys
    // are pinned by VALUE, not by mere presence.
    ASSERT_EQ("on", existing_cfg.get_setting("effects", "gore"));
}


TEST(GparserFuncs, gparser_round6_save_settings_reports_success)
{
    namespace fs = std::filesystem;
    const fs::path isolated_dir = make_isolated_gparser_dir("round6_save");
    const fs::path saved_cfg = isolated_dir / "cfg" / "openglad.yaml";

    {
        ScopedCurrentPath cwd_guard(isolated_dir);
        ScopedPhysfsWriteDir physfs_guard(isolated_dir);

        cfg_store local_cfg;
        local_cfg.apply_setting("test_save", "sentinel", "round6");
        const bool saved = local_cfg.save_settings();
        ASSERT_TRUE(saved) << "save_settings should report successful writes through the configured write dir";
    }

    std::error_code ec;
    ASSERT_TRUE(fs::exists(saved_cfg, ec)) << "save_settings should create the configured YAML file";
    const std::string contents = read_file_text(saved_cfg);
    ASSERT_NE(std::string::npos, contents.find("sentinel: round6"));
    fs::remove_all(isolated_dir, ec);
}


TEST(GparserFuncs, gparser_round6_commandline_all_short_switches)
{
    cfg.apply_setting("sound", "sound", "off");
    cfg.apply_setting("graphics", "render", "normal");
    cfg.apply_setting("graphics", "smoothing", "off");
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
    ASSERT_TRUE(cfg.get_setting("sound", "sound") == "off") << "-S should leave sound disabled";
    ASSERT_TRUE(cfg.get_setting("graphics", "render") == "sai") << "-x should leave render in sai mode";
    ASSERT_EQ("sai", cfg.get_setting("graphics", "smoothing"));
    ASSERT_TRUE(cfg.get_setting("graphics", "fullscreen") == "on") << "-f should enable fullscreen";
}


TEST(GparserFuncs, gparser_round9_commandline_help_and_version_exit_paths)
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

    ASSERT_EQ(0, run_child("-h")) << "commandline -h should exit(0)";
    ASSERT_EQ(0, run_child("-v")) << "commandline -v should exit(0)";
}
