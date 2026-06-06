#include <openglad/resources/gparser.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <gtest/gtest.h>

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
