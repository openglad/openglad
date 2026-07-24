// Test-only process-state guards included inside curses_terminal.cpp's private
// namespace. They live under tests/ so coverage classifies them as test code.

class ScopedFd
{
public:
    explicit ScopedFd(int fd = -1) : fd_(fd) {}
    ~ScopedFd() { reset(); }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ScopedFd(ScopedFd&& other) noexcept
        : fd_(std::exchange(other.fd_, -1))
    {
    }

    ScopedFd& operator=(ScopedFd&& other) noexcept
    {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }

    void reset(int fd = -1)
    {
        if (fd_ >= 0)
            (void)::close(fd_);
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

struct FileCloser
{
    void operator()(std::FILE* file) const
    {
        if (file != nullptr)
            (void)std::fclose(file);
    }
};

// A production CursesTerminal constructor intentionally mutates process-wide
// state. Tests that redirect the standard descriptors or construct against a
// PTY must put every bit of that state back, including on an exception.
class ScopedTerminalProcessState
{
public:
    ScopedTerminalProcessState()
        : saved_stdin_(::dup(STDIN_FILENO)),
          saved_stdout_(::dup(STDOUT_FILENO)),
          resize_pending_(g_resize_pending),
          needs_restore_(g_needs_restore.load())
    {
        if (const char* term = std::getenv("TERM"))
            term_ = std::string(term);
        if (const char* locale = std::setlocale(LC_ALL, nullptr))
            locale_ = std::string(locale);

        have_sigwinch_ = ::sigaction(SIGWINCH, nullptr, &sigwinch_) == 0;
        have_sigint_ = ::sigaction(SIGINT, nullptr, &sigint_) == 0;
        have_sigterm_ = ::sigaction(SIGTERM, nullptr, &sigterm_) == 0;
    }

    ~ScopedTerminalProcessState()
    {
        if (active_)
            (void)restore();
    }

    ScopedTerminalProcessState(const ScopedTerminalProcessState&) = delete;
    ScopedTerminalProcessState& operator=(const ScopedTerminalProcessState&) = delete;

    bool valid() const
    {
        return saved_stdin_.valid() && saved_stdout_.valid() && locale_.has_value() &&
            have_sigwinch_ && have_sigint_ && have_sigterm_;
    }

    bool redirect_standard_fds(int input_fd, int output_fd)
    {
        (void)std::fflush(stdout);
        bool ok = true;
        if (::dup2(input_fd, STDIN_FILENO) < 0) {
            ok = false;
        } else {
            stdin_redirected_ = true;
        }
        if (::dup2(output_fd, STDOUT_FILENO) < 0) {
            ok = false;
        } else {
            stdout_redirected_ = true;
        }
        return ok;
    }

    bool set_term_value(const char* value)
    {
        if (::setenv("TERM", value, 1) != 0)
            return false;
        term_changed_ = true;
        return true;
    }

    bool restore()
    {
        if (!active_)
            return restore_succeeded_;

        bool ok = std::fflush(stdout) == 0;
        if (stdin_redirected_ &&
            ::dup2(saved_stdin_.get(), STDIN_FILENO) < 0)
            ok = false;
        if (stdout_redirected_ &&
            ::dup2(saved_stdout_.get(), STDOUT_FILENO) < 0)
            ok = false;

        if (term_changed_) {
            const int result = term_.has_value()
                ? ::setenv("TERM", term_->c_str(), 1)
                : ::unsetenv("TERM");
            if (result != 0)
                ok = false;
        }
        if (locale_.has_value() &&
            std::setlocale(LC_ALL, locale_->c_str()) == nullptr)
            ok = false;

        if (have_sigwinch_ && ::sigaction(SIGWINCH, &sigwinch_, nullptr) != 0)
            ok = false;
        if (have_sigint_ && ::sigaction(SIGINT, &sigint_, nullptr) != 0)
            ok = false;
        if (have_sigterm_ && ::sigaction(SIGTERM, &sigterm_, nullptr) != 0)
            ok = false;

        g_resize_pending = resize_pending_;
        g_needs_restore.store(needs_restore_);
        restore_succeeded_ = ok;
        active_ = false;
        return ok;
    }

private:
    ScopedFd saved_stdin_;
    ScopedFd saved_stdout_;
    std::optional<std::string> term_;
    std::optional<std::string> locale_;
    struct sigaction sigwinch_{};
    struct sigaction sigint_{};
    struct sigaction sigterm_{};
    std::sig_atomic_t resize_pending_ = 0;
    bool needs_restore_ = false;
    bool have_sigwinch_ = false;
    bool have_sigint_ = false;
    bool have_sigterm_ = false;
    bool stdin_redirected_ = false;
    bool stdout_redirected_ = false;
    bool term_changed_ = false;
    bool restore_succeeded_ = true;
    bool active_ = true;
};

// newterm() changes ncurses' process-global current SCREEN. Detach and retain
// the old one before creating an isolated screen, then restore it before the
// isolated screen is deleted.
class ScopedTestScreen
{
public:
    ScopedTestScreen() : previous_(set_term(nullptr)) {}
    ~ScopedTestScreen() { close(); }

    ScopedTestScreen(const ScopedTestScreen&) = delete;
    ScopedTestScreen& operator=(const ScopedTestScreen&) = delete;

    void attach(SCREEN* screen) { screen_ = screen; }

    void close()
    {
        if (!active_)
            return;
        if (screen_ != nullptr) {
            (void)set_term(screen_);
            (void)endwin();
            (void)set_term(previous_);
            delscreen(screen_);
            screen_ = nullptr;
        } else {
            (void)set_term(previous_);
        }
        active_ = false;
    }

private:
    SCREEN* previous_ = nullptr;
    SCREEN* screen_ = nullptr;
    bool active_ = true;
};

// Exercise fatal restoration in an isolated, freshly exec'd test process. The
// fork branch never runs inherited atexit handlers: it either becomes the new
// image or calls _Exit. The new image owns its cleanup/coverage runtime.
bool exercise_fatal_signal_in_child()
{
    const char* const executable =
        std::getenv("OPENGLAD_CURSES_TEST_EXECUTABLE");
    if (executable == nullptr || *executable == '\0')
        return false;

    int output_pipe[2]{};
    if (::pipe(output_pipe) != 0)
        return false;
    ScopedFd output_read(output_pipe[0]);
    ScopedFd output_write(output_pipe[1]);

    (void)std::fflush(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        output_read.reset();
        if (::dup2(output_write.get(), STDOUT_FILENO) < 0)
            std::_Exit(20);
        output_write.reset();
        if (::setenv("OPENGLAD_CURSES_FATAL_SIGNAL_PROBE", "1", 1) != 0)
            std::_Exit(21);
        ::execl(executable, executable, static_cast<char*>(nullptr));
        std::_Exit(22);
    }

    output_write.reset();
    std::string output;
    std::array<char, 256> buffer{};
    for (;;) {
        const ssize_t n = ::read(output_read.get(), buffer.data(), buffer.size());
        if (n > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    return waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
        output.find(kitty::kDisableFocus) != std::string::npos &&
        output.find(kitty::kDisable) != std::string::npos;
}
