#include <openglad/data/gparser.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "unit/unit.h"

OG_UNIT_TEST(test_gparser_apply_get_and_is_on_paths)
{
    cfg_store local_cfg;
    local_cfg.apply_setting("sound", "sound", "on");
    local_cfg.apply_setting("graphics", "fullscreen", "off");

    OG_ASSERT(local_cfg.get_setting("sound", "sound") == "on");
    OG_ASSERT(local_cfg.get_setting("graphics", "fullscreen") == "off");
    OG_ASSERT(local_cfg.get_setting("graphics", "missing_key").empty());
    OG_ASSERT(local_cfg.get_setting("missing_cat", "missing_key").empty());
    OG_ASSERT(local_cfg.is_on("sound", "sound"));
    OG_ASSERT(!local_cfg.is_on("graphics", "fullscreen"));
}

OG_UNIT_TEST(test_gparser_commandline_switches_and_unknown_arg)
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

    OG_ASSERT(local_cfg.get_setting("sound", "sound") == "off");
    OG_ASSERT(local_cfg.get_setting("graphics", "render") == "sai");
    OG_ASSERT(local_cfg.get_setting("graphics", "fullscreen") == "on");
}

OG_UNIT_TEST(test_gparser_commandline_help_and_version_exit_paths)
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
    OG_ASSERT(WIFEXITED(help_status));
    OG_ASSERT(WEXITSTATUS(help_status) == 0);

    const int version_status = run_child("-v");
    OG_ASSERT(WIFEXITED(version_status));
    OG_ASSERT(WEXITSTATUS(version_status) == 0);
}
