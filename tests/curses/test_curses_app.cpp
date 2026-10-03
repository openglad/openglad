#include <openglad/platform/curses/clock.h>
#include <openglad/platform/curses/curses_app.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <optional>
#include <set>
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

#if defined(__linux__) && defined(__x86_64__)
#include <sys/ptrace.h>
#include <sys/user.h>
#endif

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

// Runs once, after the child enabled the keyboard protocol and before
// `input_after_enable` is written: it may drive the child further through the
// pty (resize it, read its transcript) while the child waits for a key. The
// transcript is the harness's own buffer; the callback appends to it with
// drain_pty_output so nothing the child writes is lost to the final asserts.
using AfterEnableStep =
    std::function<void(pid_t child, int master_fd, std::string& transcript)>;

CursesProcessResult run_curses_process(
    const std::vector<std::string>& arguments,
    std::string_view input_after_enable,
    bool report_flags_from_loop = true,
    const AfterEnableStep& after_enable = {})
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
    auto deadline = std::chrono::steady_clock::now() + 15s;
    bool ran_after_enable = false;

    while (std::chrono::steady_clock::now() < deadline) {
        drain_pty_output(master_fd, result.output);
        // The in-loop scan drives the handshake: the child only emits the
        // enable once we answer its query, and only accepts typing once it
        // has enabled. Reporting those flags out of the loop is a separate
        // step, and the one the regression test suppresses — that leaves the
        // loop in the state a loaded runner reaches by accident, with the
        // tail of the transcript arriving after the waitpid that ends it.
        const bool query_seen =
            result.output.find(kitty_query) != std::string::npos;
        if (query_seen && !sent_reply) {
            sent_reply = write_all_fd(master_fd, kitty_reply);
        }
        const bool enable_seen =
            result.output.find(kitty_enable) != std::string::npos;
        if (enable_seen && !ran_after_enable && after_enable) {
            ran_after_enable = true;
            after_enable(child, master_fd, result.output);
            // The step bounds its own waits; the child's exit after the
            // input below keeps the same 15 s budget the handshake had.
            deadline = std::chrono::steady_clock::now() + 15s;
        }
        if (enable_seen && !result.sent_input && !input_after_enable.empty()) {
            result.sent_input = write_all_fd(master_fd, input_after_enable);
        }
        if (report_flags_from_loop) {
            result.saw_query = query_seen;
            result.saw_enable = enable_seen;
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
    // The flags the loop published are a snapshot of whatever had reached us
    // by its last poll. The child writes the enable and the whole ncurses
    // teardown in one buffered flush, and a short child (an invalid --join
    // fails before it ever waits on a key) can flush and exit between our
    // last drain and the waitpid that breaks the loop — that tail then
    // arrives only in the drain above. Re-derive the flags from the COMPLETE
    // transcript, so an assertion about what the terminal saw never depends
    // on how the pty reads happened to be chopped up.
    result.saw_query = result.output.find(kitty_query) != std::string::npos;
    result.saw_enable = result.output.find(kitty_enable) != std::string::npos;
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

TEST(CursesPlatformGlobals, random_stays_in_range_and_advances_its_state)
{
    EXPECT_EQ(0u, random(0)) << "random(0) has no value to pick and returns 0";
    EXPECT_EQ(0u, random(1)) << "the only value in [0, 1) is 0";
    // The static LCG must actually advance: a scratch RNG that returns a
    // constant is in range for every sample and still worthless.
    std::set<std::uint32_t> seen;
    for (int sample = 0; sample < 256; ++sample) {
        const std::uint32_t value = random(17);
        EXPECT_LT(value, 17u) << "random(x) must stay inside [0, x)";
        seen.insert(value);
    }
    EXPECT_GT(seen.size(), 4u)
        << "the scratch LCG must advance its state across calls, not return a "
           "constant (saw " << seen.size() << " distinct values in 256 draws)";
}

TEST(CursesAppOptions, parses_every_supported_option)
{
    std::vector<std::string> args{
        "openglad_curses",
        "--campaign", "test",
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
    ASSERT_EQ("test", options.campaign);
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

TEST(CursesClock, now_tracks_steady_clock_and_sleep_ms_blocks)
{
    og::curses::SteadyClock clock;

    // sleep_ms(0) is the documented no-sleep case (clock.cpp's early return).
    // The ceiling is deliberately loose: sleep_for(0ms) returns at once too,
    // so no tooth lives here — only a guard against a catastrophic "sleep_ms(0)
    // parks the frame" regression. A tight wall-clock bound would buy nothing
    // and flake on a loaded CI box.
    const std::uint64_t zero_before = clock.now_ms();
    clock.sleep_ms(0);
    EXPECT_LT(clock.now_ms() - zero_before, 500u)
        << "sleep_ms(0) must return without parking the frame";

    // sleep_ms(n) must really block: this is the pacing the level loop and the
    // lobby spend their frame budget on.
    const std::uint64_t before = clock.now_ms();
    clock.sleep_ms(25);
    const std::uint64_t after = clock.now_ms();
    EXPECT_GE(after - before, 24u)
        << "sleep_ms(25) must block for at least 25ms (ms truncation loses <1)";
    EXPECT_LT(after - before, 5000u) << "sleep_ms(25) must not hang";

    // now_ms() is steady_clock's own epoch in milliseconds, not a counter of
    // its own: a constant (or invented-epoch) now_ms() fails both bounds.
    const std::uint64_t reference = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    EXPECT_GE(reference, after) << "now_ms() must not run ahead of steady_clock";
    EXPECT_LT(reference - after, 1000u)
        << "now_ms() must report steady_clock's millisecond epoch";
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

// Regression for a harness flake, not a product one: this same short-lived
// child (an invalid --join fails before it ever waits on a key) used to be
// reported as "never enabled the kitty protocol" whenever the poll loop
// happened to reap the child before draining its final ncurses flush. The
// transcript held the enable either way; only the flags — published from
// inside the loop and never re-derived — disagreed with it. Suppressing that
// publication reproduces the same end state on purpose, every run.
TEST(CursesAppProcess, terminal_flags_survive_a_drain_that_lags_the_child)
{
    const CursesProcessResult result = run_curses_process(
        {"--join", "   ", "--relay", "relay-enabled",
         "--no-unicode", "--no-color"},
        {}, /*report_flags_from_loop=*/false);

    ASSERT_TRUE(result.launched);
    ASSERT_FALSE(result.timed_out) << result.output;
    ASSERT_NE(std::string::npos, result.output.find("\x1b[>11u"))
        << "the child did enable the kitty protocol";
    EXPECT_TRUE(result.saw_query)
        << "the reported flags must match the transcript, not the read sizes";
    EXPECT_TRUE(result.saw_enable)
        << "the reported flags must match the transcript, not the read sizes";
}

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
namespace
{
// The main menu's footer hint (curses_picker_client.cpp, the generic
// TerminalMenuModel choose call); draw_list puts it on the LAST row.
constexpr std::string_view kMainMenuHint =
    "Up/Down or j/k move | Enter select | digits jump | Esc/q back";
// Where ncurses first paints that footer on the 40-row pty: a carriage
// return, then VPA to row 40. Measured once (2026-10-02, ncurses from the
// nix shell): the first frame writes "\r\x1b[40dUp/Down or ...", and after
// the resize to 30 rows the redraw writes only "\r" + the hint, because
// resize_term clamps ncurses' idea of the cursor (row 40) onto the new last
// row, 30, so no vertical move is needed. ncurses refreshes damaged cells
// only: at an unchanged size a redraw of the same menu writes nothing, so a
// hint in the post-resize output means the footer MOVED to the new last row.
const std::string kRow40Hint =
    std::string("\x1b[40d") + std::string(kMainMenuHint);

// /proc/<pid>/syscall: "<nr> <arg1> ... <arg6> <sp> <pc>" while the task is
// blocked in a syscall, "running" or "-1 <sp> <pc>" otherwise.
std::vector<std::string> read_proc_syscall(pid_t child, int* open_errno)
{
    std::vector<std::string> fields;
    const std::string path = "/proc/" + std::to_string(child) + "/syscall";
    errno = 0;
    std::ifstream in(path);
    if (!in) {
        if (open_errno != nullptr)
            *open_errno = errno != 0 ? errno : EIO;
        return fields;
    }
    std::string line;
    std::getline(in, line);
    std::istringstream words(line);
    for (std::string word; words >> word;)
        fields.push_back(word);
    if (open_errno != nullptr)
        *open_errno = fields.empty() ? EACCES : 0;
    return fields;
}

// The child is INSIDE poll_key's blocking poll(..., -1): the only state in
// which a SIGWINCH is guaranteed to interrupt the poll (EINTR) instead of
// landing between the resize-flag check and the poll (the self-pipe arm,
// pinned by resize_landing_before_the_blocking_poll_still_redraws; this test
// pins the EINTR arm). x86_64 glibc calls poll (7) and the third
// argument is the -1 timeout; aarch64 has no poll syscall and glibc routes
// it to ppoll (73) with a NULL timespec, so only the number is checked there.
// The timeout is an int (-1) handed to the kernel in a 64-bit register, and
// which upper half the register carries depends on how the C library's poll
// wrapper loaded it: the dev box's toolchain sign-extends it (the field
// reads 0xffffffffffffffff), GitHub's ubuntu runner zero-extends it
// (0xffffffff). The kernel reads the low 32 bits either way, so the oracle
// does too. (PR #345: the first CI run failed both resize pins on exactly
// this difference while the child sat in the right poll.)
bool poll_timeout_is_minus_one(const std::string& field)
{
    if (field.size() < 3 || field[0] != '0' || (field[1] != 'x' && field[1] != 'X'))
        return false;
    unsigned long long value = 0;
    try {
        value = std::stoull(field.substr(2), nullptr, 16);
    } catch (const std::exception&) {
        return false;
    }
    return (value & 0xffffffffULL) == 0xffffffffULL;
}

bool in_blocking_poll(const std::vector<std::string>& fields)
{
#if defined(__x86_64__)
    return fields.size() >= 4 && fields[0] == "7" &&
           poll_timeout_is_minus_one(fields[3]);
#else
    return !fields.empty() && fields[0] == "73";
#endif
}

bool child_still_running(pid_t child)
{
    siginfo_t info{};
    // WNOWAIT: look without reaping, so the harness's own waitpid still
    // collects the exit status.
    if (::waitid(P_PID, static_cast<id_t>(child), &info,
                 WEXITED | WNOHANG | WNOWAIT) != 0)
        return false;
    return info.si_pid == 0;
}

std::string escape_for_message(std::string_view bytes)
{
    std::string out;
    for (const char c : bytes) {
        if (c == '\x1b')
            out += "\\e";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\n')
            out += "\\n";
        else
            out += c;
    }
    return out;
}
} // namespace

// #340: a terminal resize while the picker waits for a key. The SIGWINCH
// interrupts poll_key's blocking poll; the EINTR arm loops, sees the pending
// resize and hands the menu a Resize key, and the menu redraws at the new
// size and keeps waiting. The child must still be alive (a resize is not a
// cancel) and must draw its footer on the NEW last row, 30.
TEST(CursesAppProcess, resize_during_a_blocking_poll_redraws_and_keeps_waiting)
{
    bool reached_blocking_poll = false;
    bool redrew_at_new_size = false;
    bool child_alive_when_esc_sent = false;
    std::string after_resize;

    const AfterEnableStep resize_while_blocked =
        [&](pid_t child, int master_fd, std::string& transcript) {
            // Precondition, loud and immediate: the parent can read the
            // child's syscall state (Yama ptrace_scope <= 1 and
            // CONFIG_HAVE_ARCH_TRACEHOOK). Without it the wait below could
            // only time out, so fail with the errno instead.
            int open_errno = 0;
            const std::vector<std::string> probe =
                read_proc_syscall(child, &open_errno);
            ASSERT_EQ(0, open_errno)
                << "cannot read /proc/" << child << "/syscall: "
                << std::strerror(open_errno)
                << " (needs Yama ptrace_scope <= 1 and "
                   "CONFIG_HAVE_ARCH_TRACEHOOK)";
            ASSERT_FALSE(probe.empty());

            // Wait until the main menu is on screen AND the child sits in
            // the blocking poll. Both waits end on a condition; the 60 s
            // ceilings only bound a dead child. Measured on ci-test, the
            // whole test (launch, reach, resize, redraw, exit) takes
            // 11-22 ms; 60 s is far above an instrumented child's start,
            // which the harness's own 15 s handshake budget already covers.
            const auto poll_deadline =
                std::chrono::steady_clock::now() + 60s;
            while (std::chrono::steady_clock::now() < poll_deadline) {
                drain_pty_output(master_fd, transcript);
                if (transcript.find(kMainMenuHint) != std::string::npos &&
                    in_blocking_poll(read_proc_syscall(child, nullptr))) {
                    reached_blocking_poll = true;
                    break;
                }
                if (!child_still_running(child))
                    break;
                pollfd ready{master_fd, POLLIN, 0};
                (void)::poll(&ready, 1, 10);
            }
            ASSERT_TRUE(reached_blocking_poll)
                << "the child never reached the main menu's blocking poll; "
                   "last syscall state: "
                << [&] {
                       std::string joined;
                       for (const std::string& f :
                            read_proc_syscall(child, nullptr))
                           joined += f + ' ';
                       return joined;
                   }()
                << "\n" << escape_for_message(transcript);

            // Positive control: before the resize the footer sat on row 40.
            ASSERT_NE(std::string::npos, transcript.find(kRow40Hint))
                << escape_for_message(transcript);
            const std::size_t resize_offset = transcript.size();
            winsize smaller{};
            smaller.ws_row = 30;
            smaller.ws_col = 80;
            ASSERT_EQ(0, ::ioctl(master_fd, TIOCSWINSZ, &smaller))
                << std::strerror(errno);

            const auto redraw_deadline =
                std::chrono::steady_clock::now() + 60s;
            while (std::chrono::steady_clock::now() < redraw_deadline) {
                drain_pty_output(master_fd, transcript);
                if (transcript.find(kMainMenuHint, resize_offset) !=
                    std::string::npos) {
                    redrew_at_new_size = true;
                    break;
                }
                if (!child_still_running(child))
                    break;
                pollfd ready{master_fd, POLLIN, 0};
                (void)::poll(&ready, 1, 10);
            }
            after_resize = transcript.substr(resize_offset);
            EXPECT_EQ(std::string::npos, after_resize.find("\x1b[40d"))
                << "after the resize nothing may be drawn on the old row 40";
            child_alive_when_esc_sent = child_still_running(child);
        };

    const CursesProcessResult result = run_curses_process(
        {"--no-unicode", "--no-color"}, "\x1b[27u",
        /*report_flags_from_loop=*/true, resize_while_blocked);

    EXPECT_TRUE(reached_blocking_poll);
    EXPECT_TRUE(redrew_at_new_size)
        << "the menu must redraw its footer on the new last row after the "
           "resize; "
           "after the resize the child wrote:\n"
        << escape_for_message(after_resize);
    EXPECT_TRUE(child_alive_when_esc_sent)
        << "a resize must not end the menu; the child exited before Esc";
    expect_clean_curses_process(result);
}

#if defined(__x86_64__)
// A resize whose SIGWINCH handler runs AFTER poll_key's pending-resize check
// and BEFORE its blocking poll is entered (the lost-wakeup window #340 avoids
// on purpose). The flag is set, nothing is pending on the terminal, and the
// poll that follows must still return at once so the menu redraws before any
// key arrives.
//
// The test reaches that window deterministically through the real kernel
// signal path, without a seam in the product: it parks the child in the main
// menu's blocking poll exactly as #340 does, stops it with PTRACE_INTERRUPT
// (the interrupted poll(2) returns -ERESTART_RESTARTBLOCK, -516 -- the poll
// syscall always arms a restart block -- and the task stops before the
// restart fix-up), rewrites that return value to -ERESTARTNOINTR (-513),
// resizes the pty while the child is still stopped (the kernel queues
// SIGWINCH on it) and detaches. On resume the kernel delivers SIGWINCH, runs
// on_sigwinch (the flag is set) and, because -513 re-executes the original
// syscall even after a handled signal, re-enters poll(..., -1) once the
// handler has returned: the poll starts after the handler ran and after the
// flag check, with no signal left to interrupt it. Left at -516 the kernel
// would turn the return into EINTR for a handled signal, and the EINTR arm
// would catch the resize -- that is the #340 path, which is why the rewrite
// is the whole trick.
//
// Rubric 5: every state the child sees (the handler ran between the check and
// the poll; the poll was entered with nothing pending) is one the race reaches
// on its own; the register rewrite only chooses WHEN the handler runs.
// x86_64 only: aarch64 could do the same through PTRACE_SETREGSET (x0 holds
// the return value, pc -= 4 on restart) but its user_pt_regs has no orig_x0
// to assert the parked syscall number on, so the precondition below could not
// be checked there.
TEST(CursesAppProcess, resize_landing_before_the_blocking_poll_still_redraws)
{
    bool reached_blocking_poll = false;
    bool restarted_after_handler = false;
    bool redrew_at_new_size = false;
    std::string last_syscall;
    std::string after_resize;

    const AfterEnableStep resize_before_the_poll =
        [&](pid_t child, int master_fd, std::string& transcript) {
            int open_errno = 0;
            const std::vector<std::string> probe =
                read_proc_syscall(child, &open_errno);
            ASSERT_EQ(0, open_errno)
                << "cannot read /proc/" << child << "/syscall: "
                << std::strerror(open_errno)
                << " (needs Yama ptrace_scope <= 1 and "
                   "CONFIG_HAVE_ARCH_TRACEHOOK)";
            ASSERT_FALSE(probe.empty());

            // Reach the main menu's blocking poll exactly as #340 does. The
            // 60 s ceilings only bound a dead child.
            const auto poll_deadline =
                std::chrono::steady_clock::now() + 60s;
            while (std::chrono::steady_clock::now() < poll_deadline) {
                drain_pty_output(master_fd, transcript);
                if (transcript.find(kMainMenuHint) != std::string::npos &&
                    in_blocking_poll(read_proc_syscall(child, nullptr))) {
                    reached_blocking_poll = true;
                    break;
                }
                if (!child_still_running(child))
                    break;
                pollfd ready{master_fd, POLLIN, 0};
                (void)::poll(&ready, 1, 10);
            }
            ASSERT_TRUE(reached_blocking_poll)
                << "the child never reached the main menu's blocking poll\n"
                << escape_for_message(transcript);
            // Positive control: before the resize the footer sat on row 40.
            ASSERT_NE(std::string::npos, transcript.find(kRow40Hint))
                << escape_for_message(transcript);

            // Every precondition from here on detaches before it fails, so a
            // broken precondition never leaves a stopped child behind.
            const auto detach = [child] {
                (void)::ptrace(PTRACE_DETACH, child, nullptr, nullptr);
            };

            errno = 0;
            const long seized = ::ptrace(PTRACE_SEIZE, child, nullptr, nullptr);
            ASSERT_EQ(0L, seized)
                << "PTRACE_SEIZE of our own child failed: "
                << std::strerror(errno) << " (needs Yama ptrace_scope <= 1)";
            errno = 0;
            const long interrupted =
                ::ptrace(PTRACE_INTERRUPT, child, nullptr, nullptr);
            const int interrupt_errno = errno;
            if (interrupted != 0)
                detach();
            ASSERT_EQ(0L, interrupted)
                << "PTRACE_INTERRUPT failed: " << std::strerror(interrupt_errno);

            int stop_status = 0;
            pid_t stopped = -1;
            do {
                stopped = ::waitpid(child, &stop_status, __WALL);
            } while (stopped < 0 && errno == EINTR);
            const bool event_stop = stopped == child && WIFSTOPPED(stop_status) &&
                                    (stop_status >> 16) == PTRACE_EVENT_STOP;
            if (!event_stop)
                detach();
            ASSERT_TRUE(event_stop)
                << "expected a PTRACE_EVENT_STOP, waitpid returned " << stopped
                << " with status 0x" << std::hex << stop_status;

            user_regs_struct regs{};
            errno = 0;
            const long got = ::ptrace(PTRACE_GETREGS, child, nullptr, &regs);
            const int getregs_errno = errno;
            // Parked on the interrupted blocking poll: syscall 7 (poll), its
            // return still -ERESTART_RESTARTBLOCK, its timeout argument -1.
            const bool parked_in_poll =
                got == 0 && regs.orig_rax == 7 &&
                static_cast<long long>(regs.rax) == -516 &&
                static_cast<long long>(regs.rdx) == -1;
            if (!parked_in_poll)
                detach();
            ASSERT_TRUE(parked_in_poll)
                << "the child is not parked on the interrupted blocking poll: "
                << "PTRACE_GETREGS " << got << " ("
                << std::strerror(getregs_errno) << "), orig_rax "
                << regs.orig_rax << ", rax "
                << static_cast<long long>(regs.rax) << ", rdx "
                << static_cast<long long>(regs.rdx);

            regs.rax = static_cast<unsigned long long>(-513LL);
            errno = 0;
            const long set = ::ptrace(PTRACE_SETREGS, child, nullptr, &regs);
            const int setregs_errno = errno;
            if (set != 0)
                detach();
            ASSERT_EQ(0L, set)
                << "PTRACE_SETREGS failed: " << std::strerror(setregs_errno);

            // The real resize, while the child is still stopped: the kernel
            // queues SIGWINCH on it and delivers it on resume.
            const std::size_t resize_offset = transcript.size();
            winsize smaller{};
            smaller.ws_row = 30;
            smaller.ws_col = 80;
            errno = 0;
            const int resized = ::ioctl(master_fd, TIOCSWINSZ, &smaller);
            const int resize_errno = errno;
            errno = 0;
            const long detached =
                ::ptrace(PTRACE_DETACH, child, nullptr, nullptr);
            const int detach_errno = errno;
            ASSERT_EQ(0, resized) << std::strerror(resize_errno);
            ASSERT_EQ(0L, detached)
                << "PTRACE_DETACH failed: " << std::strerror(detach_errno);
            restarted_after_handler = true;

            const auto redraw_deadline =
                std::chrono::steady_clock::now() + 60s;
            while (std::chrono::steady_clock::now() < redraw_deadline) {
                drain_pty_output(master_fd, transcript);
                if (transcript.find(kMainMenuHint, resize_offset) !=
                    std::string::npos) {
                    redrew_at_new_size = true;
                    break;
                }
                if (!child_still_running(child))
                    break;
                pollfd ready{master_fd, POLLIN, 0};
                (void)::poll(&ready, 1, 10);
            }
            for (const std::string& f : read_proc_syscall(child, nullptr))
                last_syscall += f + ' ';
            after_resize = transcript.substr(resize_offset);
        };

    const CursesProcessResult result = run_curses_process(
        {"--no-unicode", "--no-color"}, "\x1b[27u",
        /*report_flags_from_loop=*/true, resize_before_the_poll);

    EXPECT_TRUE(reached_blocking_poll);
    EXPECT_TRUE(restarted_after_handler);
    EXPECT_TRUE(redrew_at_new_size)
        << "a resize landing before the blocking poll must still redraw "
           "before any key; the child's last syscall state: "
        << last_syscall << "\nafter the resize the child wrote:\n"
        << escape_for_message(after_resize);
    expect_clean_curses_process(result);
}
#endif
#endif
