#include <openglad/platform/curses/clock.h>
#include <openglad/platform/curses/curses_app.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::vector<char*> argv_from(std::vector<std::string>& args)
{
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());
    return argv;
}
} // namespace

TEST(CursesAppOptions, parses_every_supported_option)
{
    std::vector<std::string> args{
        "openglad_curses",
        "--campaign", "org.openglad.test",
        "--level", "4",
        "--save", "slot7",
        "--seed", "123456",
        "--difficulty", "2",
        "--host",
        "--port", "34567",
        "--join", "ws://127.0.0.1:34568",
        "--relay", "http://127.0.0.1:8787",
        "--no-unicode",
        "--no-color",
    };
    std::vector<char*> argv = argv_from(args);

    og::curses::AppOptions options;
    bool should_exit = true;
    ASSERT_TRUE(og::curses::parse_app_options(
        static_cast<int>(argv.size()), argv.data(), options, &should_exit));
    ASSERT_FALSE(should_exit);
    ASSERT_EQ("org.openglad.test", options.campaign);
    ASSERT_EQ(4, options.level);
    ASSERT_EQ("slot7", options.save_name);
    ASSERT_EQ(static_cast<std::uint32_t>(123456), options.seed);
    ASSERT_EQ(2, options.difficulty);
    ASSERT_TRUE(options.host);
    ASSERT_EQ(34567, options.host_port);
    ASSERT_EQ("ws://127.0.0.1:34568", options.join_url);
    ASSERT_EQ("http://127.0.0.1:8787", options.relay_url);
    ASSERT_FALSE(options.allow_unicode);
    ASSERT_FALSE(options.allow_color);
}

TEST(CursesAppOptions, reports_help_and_missing_values)
{
    {
        std::vector<std::string> args{"openglad_curses", "--help"};
        std::vector<char*> argv = argv_from(args);
        og::curses::AppOptions options;
        bool should_exit = false;
        ASSERT_FALSE(og::curses::parse_app_options(
            static_cast<int>(argv.size()), argv.data(), options, &should_exit));
        ASSERT_TRUE(should_exit);
    }

    const std::vector<std::string> missing_value_options{
        "--campaign", "--level", "--save", "--seed", "--difficulty",
        "--port", "--join", "--relay",
    };
    for (const std::string& option : missing_value_options) {
        std::vector<std::string> args{"openglad_curses", option};
        std::vector<char*> argv = argv_from(args);
        og::curses::AppOptions options;
        bool should_exit = true;
        ASSERT_FALSE(og::curses::parse_app_options(
            static_cast<int>(argv.size()), argv.data(), options, &should_exit))
            << option;
        ASSERT_FALSE(should_exit) << option;
    }
}

TEST(CursesAppOptions, reports_unknown_option)
{
    std::vector<std::string> args{"openglad_curses", "--bogus"};
    std::vector<char*> argv = argv_from(args);

    og::curses::AppOptions options;
    bool should_exit = true;
    ASSERT_FALSE(og::curses::parse_app_options(
        static_cast<int>(argv.size()), argv.data(), options, &should_exit));
    ASSERT_FALSE(should_exit);
}

TEST(CursesClock, now_and_sleep_are_monotonic)
{
    og::curses::SteadyClock clock;
    const std::uint64_t before = clock.now_ms();
    clock.sleep_ms(1);
    const std::uint64_t after = clock.now_ms();
    ASSERT_GE(after, before);
}
