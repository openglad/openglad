// Test-only definition included by curses_terminal.cpp under TESTING. Keeping
// orchestration in tests/ prevents source coverage from counting harness code.

int curses_terminal_testing_delayed_handshake_poll_count()
{
    int input_pipe[2]{};
    int output_pipe[2]{};
    if (::pipe(input_pipe) != 0)
        return -1;
    ScopedFd input_read(input_pipe[0]);
    ScopedFd input_write(input_pipe[1]);
    if (::pipe(output_pipe) != 0)
        return -1;
    ScopedFd output_read(output_pipe[0]);
    ScopedFd output_write(output_pipe[1]);

    int poll_count = 0;
    bool poll_contract_valid = true;
    const bool supported = detect_kitty_support_with_poll(
        input_read.get(), output_write.get(),
        [&](pollfd* fds, nfds_t count, int timeout_ms) {
            ++poll_count;
            poll_contract_valid = poll_contract_valid && count == 1 &&
                fds[0].fd == input_read.get() &&
                (fds[0].events & POLLIN) != 0 && timeout_ms == 50;
            if (poll_count == 1)
                return 0; // No response during the first polling slice.
            if (poll_count == 2 &&
                !write_all(input_write.get(), "\x1b[?11u\x1b[?62;1c"))
                return -1;
            return ::poll(fds, count, timeout_ms);
        });

    output_write.reset();
    std::vector<char> query(kitty::kQuery.size());
    const ssize_t query_size =
        ::read(output_read.get(), query.data(), query.size());
    const bool query_matches =
        query_size == static_cast<ssize_t>(kitty::kQuery.size()) &&
        std::string_view(query.data(), query.size()) == kitty::kQuery;
    return supported && poll_contract_valid && query_matches ? poll_count : -1;
}

int curses_terminal_testing_fatal_signal_probe()
{
    sigset_t blocked{};
    if (::sigemptyset(&blocked) != 0 ||
        ::sigaddset(&blocked, SIGTERM) != 0 ||
        ::sigprocmask(SIG_BLOCK, &blocked, nullptr) != 0)
        return 21;

    g_needs_restore.store(true);
    on_fatal_signal(SIGTERM);

    sigset_t pending{};
    struct sigaction disposition{};
    const bool restored = !g_needs_restore.load();
    const bool term_pending = ::sigpending(&pending) == 0 &&
        ::sigismember(&pending, SIGTERM) == 1;
    const bool defaulted = ::sigaction(SIGTERM, nullptr, &disposition) == 0 &&
        disposition.sa_handler == SIG_DFL;
    return restored && term_pending && defaulted ? 0 : 22;
}

int curses_terminal_testing_exercise_internal_helpers()
{
    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
            failed_check = check_index;
    };
    restore_terminal();

    // Make the constructor's non-TTY rejection deterministic instead of
    // relying on however the test runner happened to launch this process.
    {
        int input_pipe[2]{};
        int output_pipe[2]{};
        const bool input_pipe_created = ::pipe(input_pipe) == 0;
        ScopedFd input_read(input_pipe_created ? input_pipe[0] : -1);
        ScopedFd input_write(input_pipe_created ? input_pipe[1] : -1);
        const bool output_pipe_created = ::pipe(output_pipe) == 0;
        ScopedFd output_read(output_pipe_created ? output_pipe[0] : -1);
        ScopedFd output_write(output_pipe_created ? output_pipe[1] : -1);
        const bool pipes_created = input_pipe_created && output_pipe_created;
        ScopedTerminalProcessState process_state;

        check(pipes_created);
        check(process_state.valid());
        const bool redirected = pipes_created && process_state.valid() &&
            process_state.redirect_standard_fds(input_read.get(),
                                                output_write.get());
        check(redirected);

        bool default_constructor_failed = false;
        if (redirected) {
            try {
                CursesTerminal terminal;
                (void)terminal;
            } catch (const std::runtime_error&) {
                default_constructor_failed = true;
            }
        }
        check(default_constructor_failed);
        check(process_state.restore());
    }

    // Drive the complete production constructor through a real pseudoterminal.
    // The queued capability reply is consumed after curses switches the slave
    // into cbreak mode, exactly like a terminal answering the Kitty query.
    {
        ScopedFd pty_master(::posix_openpt(O_RDWR | O_NOCTTY));
        ScopedFd pty_slave;
        bool pty_ready = pty_master.valid() &&
            ::grantpt(pty_master.get()) == 0 &&
            ::unlockpt(pty_master.get()) == 0;
        if (pty_ready) {
            const char* const slave_name = ::ptsname(pty_master.get());
            if (slave_name != nullptr)
                pty_slave.reset(::open(slave_name, O_RDWR | O_NOCTTY));
            pty_ready = pty_slave.valid();
        }
        check(pty_ready);

        ScopedTerminalProcessState process_state;
        check(process_state.valid());

        struct winsize size{};
        size.ws_row = 24;
        size.ws_col = 80;
        const bool sized = pty_ready &&
            ::ioctl(pty_slave.get(), TIOCSWINSZ, &size) == 0;
        check(sized);
        const bool redirected = sized && process_state.valid() &&
            process_state.redirect_standard_fds(pty_slave.get(), pty_slave.get());
        check(redirected);
        const bool term_set = redirected &&
            process_state.set_term_value("xterm-256color");
        check(term_set);
        const bool response_queued = term_set &&
            write_all(pty_master.get(), "\x1b[?11u\x1b[?62;1c");
        check(response_queued);

        bool real_terminal_started = false;
        if (response_queued) {
            try {
                auto terminal = std::make_unique<CursesTerminal>(
                    CursesTerminal::Options{.use_unicode = true,
                                            .use_color = true});
                real_terminal_started = terminal->rows() > 0 &&
                    terminal->cols() > 0 && terminal->supports_unicode();
                (void)terminal->supports_color();
                terminal->clear();
                terminal->put(0, 0, U'Z', Color::Red, Color::Default, true);
                terminal->put_str(1, 0, "real terminal", Color::Cyan,
                                  Color::Default, false);
                terminal->present();
                terminal->set_cursor_visible(false);
                terminal->beep();
                const bool signal_raised = std::raise(SIGWINCH) == 0;
                real_terminal_started = real_terminal_started &&
                    signal_raised &&
                    terminal->poll_key(false).code == KeyCode::Resize;
            } catch (const std::runtime_error&) {
                real_terminal_started = false;
            }
        }
        check(real_terminal_started);
        // This restores stdin/stdout, TERM, locale, SIGWINCH, SIGINT, and
        // SIGTERM after CursesTerminal's destructor has left curses mode.
        check(process_state.restore());
    }

    // A real TTY without Kitty keyboard support must unwind curses mode and
    // reject construction with the documented capability diagnostic.
    {
        ScopedFd pty_master(::posix_openpt(O_RDWR | O_NOCTTY));
        ScopedFd pty_slave;
        bool pty_ready = pty_master.valid() &&
            ::grantpt(pty_master.get()) == 0 &&
            ::unlockpt(pty_master.get()) == 0;
        if (pty_ready) {
            const char* const slave_name = ::ptsname(pty_master.get());
            if (slave_name != nullptr)
                pty_slave.reset(::open(slave_name, O_RDWR | O_NOCTTY));
            pty_ready = pty_slave.valid();
        }
        check(pty_ready);

        ScopedTerminalProcessState process_state;
        check(process_state.valid());

        struct winsize size{};
        size.ws_row = 24;
        size.ws_col = 80;
        const bool sized = pty_ready &&
            ::ioctl(pty_slave.get(), TIOCSWINSZ, &size) == 0;
        check(sized);
        const bool redirected = sized && process_state.valid() &&
            process_state.redirect_standard_fds(
                pty_slave.get(), pty_slave.get());
        check(redirected);
        const bool term_set = redirected &&
            process_state.set_term_value("xterm-256color");
        check(term_set);
        // Device attributes complete the probe, but the reply deliberately
        // omits the Kitty keyboard-flags response.
        const bool response_queued = term_set &&
            write_all(pty_master.get(), "\x1b[?62;1c");
        check(response_queued);

        bool rejected_without_kitty = false;
        if (response_queued) {
            try {
                CursesTerminal terminal;
                (void)terminal;
            } catch (const std::runtime_error& ex) {
                rejected_without_kitty =
                    std::string_view(ex.what()).find(
                        "does not support the Kitty keyboard protocol") !=
                    std::string_view::npos;
            }
        }
        check(rejected_without_kitty);
        check(process_state.restore());
    }

    check(exercise_fatal_signal_in_child());

    // Exercise the concrete ncurses renderer against an isolated virtual
    // terminal. This validates actual cells while avoiding a dependency on the
    // CI process owning a TTY.
    using ScopedFile = std::unique_ptr<std::FILE, FileCloser>;
    ScopedFile curses_input(std::tmpfile());
    ScopedFile curses_output(std::tmpfile());
    if (curses_input && curses_output) {
        ScopedTestScreen screen_scope;
        SCREEN* const screen = newterm("xterm-256color", curses_output.get(),
                                       curses_input.get());
        screen_scope.attach(screen);
        if (screen != nullptr) {
            CursesTerminal term(CursesTerminal::TestingTag{});
            term.impl_->unicode = false;
            term.impl_->color = false;
            check(term.rows() > 0 && term.cols() > 4);
            term.clear();
            std::vector<chtype> visible_row_before(
                static_cast<std::size_t>(term.cols()));
            for (int col = 0; col < term.cols(); ++col) {
                visible_row_before[static_cast<std::size_t>(col)] =
                    mvwinch(stdscr, 0, col);
            }
            term.put(-1, 0, U'X', Color::Default, Color::Default, false);
            term.put_str(-1, 0, "offscreen", Color::Default,
                         Color::Default, false);
            term.put_str(0, term.cols(), "offscreen", Color::Default,
                         Color::Default, false);
            bool visible_row_unchanged = true;
            for (int col = 0; col < term.cols(); ++col) {
                visible_row_unchanged =
                    mvwinch(stdscr, 0, col) ==
                        visible_row_before[
                            static_cast<std::size_t>(col)] &&
                    visible_row_unchanged;
            }
            check(visible_row_unchanged);
            term.put(0, 0, U'A', Color::Default, Color::Default, true);
            term.put(0, 1, U'\u03a9', Color::Default, Color::Default, false);
            term.put_str(0, 2, "hi", Color::Default, Color::Default, false);
            check((mvwinch(stdscr, 0, 0) & A_CHARTEXT) == 'A');
            check((mvwinch(stdscr, 0, 1) & A_CHARTEXT) == '?');
            check((mvwinch(stdscr, 0, 2) & A_CHARTEXT) == 'h');
            term.present();
            term.set_cursor_visible(false);
            term.set_cursor_visible(true);
            term.beep();
            screen_scope.close();
        } else {
            check(false);
        }
    } else {
        check(false);
    }

    {
        CursesTerminal term(CursesTerminal::TestingTag{});
        term.impl_->unicode = true;
        term.impl_->color = false;
        term.impl_->handle_resize();
        check(term.supports_unicode());
        check(!term.supports_color());
        (void)term.rows();
        (void)term.cols();

        int in_pipe[2]{};
        if (::pipe(in_pipe) == 0) {
            term.impl_->in_fd = in_pipe[0];
            (void)write_all(in_pipe[1], "\x1b[113u");
            const Key key = term.poll_key(false);
            check(key.is_char(U'q'));
            ::close(in_pipe[1]);
            ::close(in_pipe[0]);
            term.impl_->in_fd = STDIN_FILENO;
        } else {
            check(false);
        }

        int empty_pipe[2]{};
        if (::pipe(empty_pipe) == 0) {
            ::close(empty_pipe[1]);
            term.impl_->in_fd = empty_pipe[0];
            check(term.poll_key(false).is_none());
            ::close(empty_pipe[0]);
            term.impl_->in_fd = STDIN_FILENO;
        } else {
            check(false);
        }

        // poll() reports a closed descriptor as ready-with-POLLNVAL; the
        // subsequent read fails with EBADF and must still return Key::none.
        int invalid_pipe[2]{};
        if (::pipe(invalid_pipe) == 0) {
            const int invalid_fd = invalid_pipe[0];
            ::close(invalid_pipe[0]);
            ::close(invalid_pipe[1]);
            term.impl_->in_fd = invalid_fd;
            check(term.poll_key(false).is_none());
            term.impl_->in_fd = STDIN_FILENO;
        } else {
            check(false);
        }
    }

    check(to_ncurses_color(Color::Black) == COLOR_BLACK);
    check(to_ncurses_color(Color::Red) == COLOR_RED);
    check(to_ncurses_color(Color::Green) == COLOR_GREEN);
    check(to_ncurses_color(Color::Yellow) == COLOR_YELLOW);
    check(to_ncurses_color(Color::Blue) == COLOR_BLUE);
    check(to_ncurses_color(Color::Magenta) == COLOR_MAGENTA);
    check(to_ncurses_color(Color::Cyan) == COLOR_CYAN);
    check(to_ncurses_color(Color::White) == COLOR_WHITE);
    check(to_ncurses_color(Color::Default) == -1);
    check(pair_id(Color::Red, Color::Blue) != pair_id(Color::Blue, Color::Red));

    int write_pipe[2]{};
    if (::pipe(write_pipe) == 0) {
        (void)write_all(write_pipe[1], "terminal-write-test");
        ::close(write_pipe[1]);
        char buf[64]{};
        const ssize_t n = ::read(write_pipe[0], buf, sizeof(buf));
        check(n == 19 && std::string(buf, static_cast<std::size_t>(n)) == "terminal-write-test");
        ::close(write_pipe[0]);
    } else {
        check(false);
    }

    auto probe = [&](std::string_view reply, bool expected) {
        int in_pipe[2]{};
        int out_pipe[2]{};
        if (::pipe(in_pipe) != 0) {
            check(false);
            return;
        }
        if (::pipe(out_pipe) != 0) {
            ::close(in_pipe[0]);
            ::close(in_pipe[1]);
            check(false);
            return;
        }
        (void)write_all(in_pipe[1], reply);
        ::close(in_pipe[1]);
        const bool supported = detect_kitty_support(in_pipe[0], out_pipe[1]);
        ::close(in_pipe[0]);
        ::close(out_pipe[1]);
        std::vector<char> written(kitty::kQuery.size());
        const ssize_t n = ::read(out_pipe[0], written.data(), written.size());
        ::close(out_pipe[0]);
        check(supported == expected &&
            n == static_cast<ssize_t>(kitty::kQuery.size()) &&
            std::string_view(written.data(), written.size()) == kitty::kQuery);
    };
    probe("\x1b[?11u\x1b[?62;1c", true);
    probe("\x1b[?62;1c", false);

    return failed_check == 0 ? 0 : -failed_check;
}
