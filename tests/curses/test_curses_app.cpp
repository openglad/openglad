#include <openglad/platform/curses/clock.h>
#include <openglad/platform/curses/curses_app.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef OPENGLAD_CURSES_TEST_EXECUTABLE
#define OPENGLAD_CURSES_TEST_EXECUTABLE "openglad_curses"
#endif

void popup_dialog(const char* title, const char* message);
std::uint32_t random(std::uint32_t x);

namespace
{
using namespace std::chrono_literals;

std::vector<char*> argv_from(std::vector<std::string>& args)
{
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());
    return argv;
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern =
            (std::filesystem::temp_directory_path() /
             "openglad-curses-process-XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        if (char* created = ::mkdtemp(writable.data()))
            path_ = created;
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        if (!path_.empty())
            std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct CursesProcessResult {
    bool launched = false;
    bool timed_out = false;
    bool saw_query = false;
    bool saw_enable = false;
    bool sent_input = false;
    int wait_status = -1;
    std::string output;
};

bool write_all_fd(int fd, std::string_view bytes)
{
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written =
            ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd output_ready{fd, POLLOUT, 0};
            if (::poll(&output_ready, 1, 100) >= 0)
                continue;
        }
        return false;
    }
    return true;
}

void drain_pty_output(int master_fd, std::string& output)
{
    for (;;) {
        char buffer[4096];
        const ssize_t count = ::read(master_fd, buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        break;
    }
}

CursesProcessResult run_curses_process(
    const std::vector<std::string>& arguments,
    std::string_view input_after_enable)
{
    CursesProcessResult result;
    TemporaryDirectory config_directory;
    if (config_directory.path().empty())
        return result;

    const std::filesystem::path executable = OPENGLAD_CURSES_TEST_EXECUTABLE;
    int master_fd = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master_fd < 0)
        return result;
    if (::grantpt(master_fd) != 0 || ::unlockpt(master_fd) != 0) {
        (void)::close(master_fd);
        return result;
    }

    char slave_name[256]{};
    if (::ptsname_r(master_fd, slave_name, sizeof(slave_name)) != 0) {
        (void)::close(master_fd);
        return result;
    }
    const int slave_fd = ::open(slave_name, O_RDWR | O_NOCTTY);
    if (slave_fd < 0) {
        (void)::close(master_fd);
        return result;
    }

    winsize dimensions{};
    dimensions.ws_row = 40;
    dimensions.ws_col = 100;
    (void)::ioctl(slave_fd, TIOCSWINSZ, &dimensions);

    std::vector<std::string> owned_argv;
    owned_argv.reserve(arguments.size() + 1);
    owned_argv.push_back(executable.string());
    owned_argv.insert(owned_argv.end(), arguments.begin(), arguments.end());
    std::vector<char*> child_argv = argv_from(owned_argv);
    child_argv.push_back(nullptr);

    const pid_t child = ::fork();
    if (child < 0) {
        (void)::close(slave_fd);
        (void)::close(master_fd);
        return result;
    }
    if (child == 0) {
        (void)::close(master_fd);
        if (::setsid() < 0 ||
            ::ioctl(slave_fd, TIOCSCTTY, 0) < 0 ||
            ::dup2(slave_fd, STDIN_FILENO) < 0 ||
            ::dup2(slave_fd, STDOUT_FILENO) < 0 ||
            ::dup2(slave_fd, STDERR_FILENO) < 0) {
            _exit(126);
        }
        if (slave_fd > STDERR_FILENO)
            (void)::close(slave_fd);

        (void)::setenv("TERM", "xterm-256color", 1);
        (void)::setenv("OPENGLAD_CONFIG_DIR",
                       config_directory.path().c_str(), 1);
        if (!executable.parent_path().empty() &&
            ::chdir(executable.parent_path().c_str()) != 0)
            _exit(126);
        ::execv(executable.c_str(), child_argv.data());
        _exit(127);
    }

    result.launched = true;
    (void)::close(slave_fd);

    constexpr std::string_view kitty_query = "\x1b[?u\x1b[c";
    constexpr std::string_view kitty_reply = "\x1b[?11u\x1b[?62;1c";
    constexpr std::string_view kitty_enable = "\x1b[>11u";
    bool sent_reply = false;
    const auto deadline = std::chrono::steady_clock::now() + 15s;

    while (std::chrono::steady_clock::now() < deadline) {
        drain_pty_output(master_fd, result.output);
        result.saw_query =
            result.output.find(kitty_query) != std::string::npos;
        if (result.saw_query && !sent_reply) {
            sent_reply = write_all_fd(master_fd, kitty_reply);
        }
        result.saw_enable =
            result.output.find(kitty_enable) != std::string::npos;
        if (result.saw_enable && !result.sent_input &&
            !input_after_enable.empty()) {
            result.sent_input = write_all_fd(master_fd, input_after_enable);
        }

        const pid_t waited = ::waitpid(child, &result.wait_status, WNOHANG);
        if (waited == child)
            break;
        if (waited < 0 && errno != EINTR)
            break;

        pollfd input_ready{master_fd, POLLIN, 0};
        (void)::poll(&input_ready, 1, 10);
    }

    if (result.wait_status == -1) {
        result.timed_out = true;
        (void)::kill(child, SIGKILL);
        while (::waitpid(child, &result.wait_status, 0) < 0 && errno == EINTR) {
        }
    }
    drain_pty_output(master_fd, result.output);
    (void)::close(master_fd);
    return result;
}

std::optional<int> dynamically_free_tcp_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return std::nullopt;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0) {
        (void)::close(fd);
        return std::nullopt;
    }

    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        (void)::close(fd);
        return std::nullopt;
    }
    const int port = ntohs(address.sin_port);
    (void)::close(fd);
    return port > 0 ? std::optional<int>(port) : std::nullopt;
}

void expect_clean_curses_process(const CursesProcessResult& result)
{
    ASSERT_TRUE(result.launched);
    ASSERT_FALSE(result.timed_out) << result.output;
    ASSERT_TRUE(result.saw_query) << result.output;
    ASSERT_TRUE(result.saw_enable) << result.output;
    ASSERT_TRUE(result.sent_input) << result.output;
    ASSERT_TRUE(WIFEXITED(result.wait_status)) << result.output;
    EXPECT_EQ(0, WEXITSTATUS(result.wait_status)) << result.output;
}
} // namespace

TEST(CursesPlatformGlobals, popup_dialog_writes_headless_diagnostic)
{
    testing::internal::CaptureStderr();
    popup_dialog("Network", "Connection lost");
    EXPECT_EQ("[Network] Connection lost\n",
              testing::internal::GetCapturedStderr());
}

TEST(CursesPlatformGlobals, random_obeys_zero_and_exclusive_upper_bound)
{
    EXPECT_EQ(0u, random(0));
    EXPECT_EQ(0u, random(1));
    for (int sample = 0; sample < 128; ++sample)
        EXPECT_LT(random(17), 17u);
}

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

TEST(CursesAppProcess, picker_runs_in_a_real_terminal_and_quits_cleanly)
{
    const CursesProcessResult result =
        run_curses_process({"--no-unicode", "--no-color"}, "\x1b[27u");

    expect_clean_curses_process(result);
    EXPECT_NE(std::string::npos, result.output.find("OpenGlad"));
    EXPECT_NE(std::string::npos, result.output.find("\x1b[<u"));
}

TEST(CursesAppProcess, host_shortcut_enters_the_real_lobby_and_can_cancel)
{
    const std::optional<int> port = dynamically_free_tcp_port();
    ASSERT_TRUE(port.has_value());
    const CursesProcessResult result = run_curses_process(
        {"--host", "--port", std::to_string(*port),
         "--no-unicode", "--no-color"},
        "\x1b[113u");

    expect_clean_curses_process(result);
    EXPECT_NE(std::string::npos, result.output.find("Hosting Game"));
}

TEST(CursesAppProcess, join_shortcut_enters_the_real_lobby_and_can_cancel)
{
    const CursesProcessResult result = run_curses_process(
        {"--join", "ws://127.0.0.1:1", "--no-unicode", "--no-color"},
        "\x1b[113u");

    expect_clean_curses_process(result);
    EXPECT_NE(std::string::npos, result.output.find("Joining Game"));
}

TEST(CursesAppProcess, host_uses_direct_transport_when_optional_relay_is_invalid)
{
    const std::optional<int> port = dynamically_free_tcp_port();
    ASSERT_TRUE(port.has_value());
    const CursesProcessResult result = run_curses_process(
        {"--host", "--port", std::to_string(*port),
         "--relay", "   ", "--no-unicode", "--no-color"},
        "\x1b[113u");

    expect_clean_curses_process(result);
    EXPECT_NE(std::string::npos, result.output.find("Hosting Game"));
}

TEST(CursesAppProcess, invalid_relay_join_reports_failure_and_restores_terminal)
{
    const CursesProcessResult result = run_curses_process(
        {"--join", "   ", "--relay", "relay-enabled",
         "--no-unicode", "--no-color"},
        {});

    ASSERT_TRUE(result.launched);
    ASSERT_FALSE(result.timed_out) << result.output;
    EXPECT_TRUE(result.saw_query) << result.output;
    EXPECT_TRUE(result.saw_enable) << result.output;
    EXPECT_FALSE(result.sent_input);
    ASSERT_TRUE(WIFEXITED(result.wait_status)) << result.output;
    EXPECT_EQ(1, WEXITSTATUS(result.wait_status)) << result.output;
    EXPECT_NE(std::string::npos,
              result.output.find("RelayWebSocketTransport URL must not be empty"));
    EXPECT_NE(std::string::npos, result.output.find("\x1b[<u"));
}
