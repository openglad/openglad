/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Real ncurses backend for ITerminal. ncurses is used only for RENDERING (screen
 * buffer, color, refresh); input goes through the Kitty keyboard protocol instead
 * of getch(), so we get key-up/down, real modifiers, and standalone Ctrl/Alt. The
 * protocol is mandatory: if the terminal does not support it, construction throws
 * and the client aborts (there is no legacy fallback). Not exercised in CI (needs
 * a TTY); the parsing it depends on is tested via kitty_keys / HeadlessTerminal.
 */
#include <openglad/platform/curses/curses_terminal.h>

#include <openglad/platform/curses/kitty_keys.h>
#include <openglad/platform/curses/text_util.h>

#include <atomic>
#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include <csignal>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

// ncursesw: wide-character ncurses for UTF-8 glyphs. Force the wide prototypes
// and prefer the ncursesw-specific header when it is installed in its own dir.
#define NCURSES_WIDECHAR 1
#if __has_include(<ncursesw/ncurses.h>)
#  include <ncursesw/ncurses.h>
#else
#  include <ncurses.h>
#endif

namespace og::curses {

namespace {

// Map an abstract Color to an ncurses COLOR_* value; -1 == terminal default.
int to_ncurses_color(Color c)
{
    switch (c) {
    case Color::Black:   return COLOR_BLACK;
    case Color::Red:     return COLOR_RED;
    case Color::Green:   return COLOR_GREEN;
    case Color::Yellow:  return COLOR_YELLOW;
    case Color::Blue:    return COLOR_BLUE;
    case Color::Magenta: return COLOR_MAGENTA;
    case Color::Cyan:    return COLOR_CYAN;
    case Color::White:   return COLOR_WHITE;
    case Color::Default:
    default:             return -1;
    }
}

constexpr int kColorCount = 9; // Default + 8 named colors

int pair_id(Color fg, Color bg)
{
    // Pair 0 is reserved by ncurses for the default fg/bg.
    return static_cast<int>(fg) * kColorCount + static_cast<int>(bg) + 1;
}

// --- terminal restoration (must survive Ctrl-C / kill so the keyboard protocol
// and alternate screen are never left enabled in the user's shell) ---

std::atomic<bool> g_needs_restore{false};

void write_all(int fd, std::string_view s)
{
    std::size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::write(fd, s.data() + off, s.size() - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
        off += static_cast<std::size_t>(n);
    }
}

void restore_terminal()
{
    if (!g_needs_restore.exchange(false))
        return;
    // Pop the keyboard-mode flags and focus reporting before leaving curses.
    write_all(STDOUT_FILENO, kitty::kDisableFocus);
    write_all(STDOUT_FILENO, kitty::kDisable);
    endwin();
}

volatile std::sig_atomic_t g_resize_pending = 0;
void on_sigwinch(int) { g_resize_pending = 1; }

void on_fatal_signal(int sig)
{
    restore_terminal();
    // Re-raise with the default disposition so the process really terminates.
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

// Run the capability handshake on `in_fd`/`out_fd`. Returns true if the terminal
// advertises Kitty keyboard support. Consumes (and discards) the handshake reply
// so it never leaks into the key stream.
bool detect_kitty_support(int in_fd, int out_fd)
{
    write_all(out_fd, kitty::kQuery);

    std::string reply;
    bool done = false;
    bool supported = false;
    // ~1s budget (20 * 50ms) — local terminals answer in well under that.
    for (int tries = 0; tries < 20 && !done; ++tries) {
        struct pollfd pfd{};
        pfd.fd = in_fd;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, 50);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pr == 0)
            break; // no (more) data within this slice
        char tmp[256];
        const ssize_t n = ::read(in_fd, tmp, sizeof(tmp));
        if (n <= 0)
            break;
        reply.append(tmp, static_cast<std::size_t>(n));
        supported = kitty::response_indicates_support(reply, done);
    }
    return supported;
}

} // namespace

struct CursesTerminal::Impl {
    bool color = false;
    bool unicode = false;
    bool started = false;
    int in_fd = STDIN_FILENO;
    int out_fd = STDOUT_FILENO;
    kitty::Decoder decoder;

    ~Impl()
    {
        if (started) {
            restore_terminal();
            started = false;
        }
    }

    void handle_resize()
    {
        struct winsize ws{};
        if (::ioctl(out_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0)
            resize_term(ws.ws_row, ws.ws_col);
    }
};

CursesTerminal::CursesTerminal() : CursesTerminal(Options{}) {}

#ifdef TESTING
CursesTerminal::CursesTerminal(TestingTag) : impl_(std::make_unique<Impl>()) {}
#endif

CursesTerminal::CursesTerminal(Options opt) : impl_(std::make_unique<Impl>())
{
    if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO))
        throw std::runtime_error(
            "openglad_curses: standard input/output is not a terminal");

    if (opt.use_unicode)
        std::setlocale(LC_ALL, "");

    if (initscr() == nullptr)
        throw std::runtime_error("openglad_curses: failed to initialize terminal (initscr)");
    impl_->started = true;
    g_needs_restore.store(true);
    std::atexit(restore_terminal);

    cbreak();   // leave ISIG on, so Ctrl-C can still interrupt a wedged client
    noecho();
    nonl();
    keypad(stdscr, FALSE); // we parse input ourselves; don't let ncurses translate
    curs_set(0);

    // Probe for the Kitty keyboard protocol. It is mandatory — abort if absent.
    if (!detect_kitty_support(impl_->in_fd, impl_->out_fd)) {
        restore_terminal();
        impl_->started = false;
        throw std::runtime_error(
            "openglad_curses: this terminal does not support the Kitty keyboard "
            "protocol (needed for Ctrl/Alt and key-release events). Try kitty, "
            "foot, WezTerm, Ghostty, or a recent Alacritty/Konsole/iTerm2.");
    }
    write_all(impl_->out_fd, kitty::kEnable);
    write_all(impl_->out_fd, kitty::kEnableFocus);

    // Signal-driven terminal restoration + resize.
    struct sigaction sa{};
    sa.sa_handler = on_sigwinch;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, nullptr);

    struct sigaction fa{};
    fa.sa_handler = on_fatal_signal;
    sigemptyset(&fa.sa_mask);
    sigaction(SIGINT, &fa, nullptr);
    sigaction(SIGTERM, &fa, nullptr);

    impl_->unicode = opt.use_unicode;
    impl_->color = opt.use_color && has_colors();

    if (impl_->color) {
        start_color();
        use_default_colors();
        for (int fg = 0; fg < kColorCount; ++fg)
            for (int bg = 0; bg < kColorCount; ++bg) {
                const int id = static_cast<int>(fg) * kColorCount + bg + 1;
                if (id < COLOR_PAIRS)
                    init_pair(static_cast<short>(id),
                              static_cast<short>(to_ncurses_color(static_cast<Color>(fg))),
                              static_cast<short>(to_ncurses_color(static_cast<Color>(bg))));
            }
    }
}

CursesTerminal::~CursesTerminal() = default;

int CursesTerminal::rows() const { return LINES; }
int CursesTerminal::cols() const { return COLS; }
bool CursesTerminal::supports_unicode() const { return impl_->unicode; }
bool CursesTerminal::supports_color() const { return impl_->color; }

void CursesTerminal::clear() { erase(); }

void CursesTerminal::put(int row, int col, char32_t ch, Color fg, Color bg, bool bold)
{
    if (row < 0 || row >= LINES || col < 0 || col >= COLS)
        return;
    attr_t attr = bold ? A_BOLD : A_NORMAL;
    // Only apply a pair that was actually init_pair'd; on terminals with few
    // COLOR_PAIRS a high id would otherwise select an uninitialized pair.
    if (impl_->color && pair_id(fg, bg) < COLOR_PAIRS)
        attr |= static_cast<attr_t>(COLOR_PAIR(pair_id(fg, bg)));
    const std::string utf8 = impl_->unicode ? utf8_encode(ch)
                                            : std::string(1, ch < 0x80 ? static_cast<char>(ch) : '?');
    attron(attr);
    mvaddstr(row, col, utf8.c_str());
    attroff(attr);
}

void CursesTerminal::put_str(int row, int col, std::string_view utf8,
                             Color fg, Color bg, bool bold)
{
    if (row < 0 || row >= LINES || col < 0 || col >= COLS)
        return;
    attr_t attr = bold ? A_BOLD : A_NORMAL;
    if (impl_->color && pair_id(fg, bg) < COLOR_PAIRS)
        attr |= static_cast<attr_t>(COLOR_PAIR(pair_id(fg, bg)));
    attron(attr);
    // Clip to the screen width; pass a NUL-terminated copy to ncurses.
    const std::string text(utf8);
    mvaddnstr(row, col, text.c_str(), COLS - col);
    attroff(attr);
}

void CursesTerminal::present() { refresh(); }

Key CursesTerminal::poll_key(bool block)
{
    for (;;) {
        // A pending resize takes priority so the renderer re-lays-out promptly.
        if (g_resize_pending) {
            g_resize_pending = 0;
            impl_->handle_resize();
            return Key::special(KeyCode::Resize);
        }
        // Drain any already-buffered events before reading more bytes.
        Key key;
        if (impl_->decoder.next(key))
            return key;

        struct pollfd pfd{};
        pfd.fd = impl_->in_fd;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, block ? -1 : 0);
        if (pr < 0) {
            if (errno == EINTR)
                continue; // interrupted (likely SIGWINCH): loop and re-check
            return Key::none();
        }
        if (pr == 0) {
            if (!block)
                return Key::none(); // nothing ready and not blocking
            continue;               // blocking: keep waiting
        }
        char tmp[256];
        const ssize_t n = ::read(impl_->in_fd, tmp, sizeof(tmp));
        if (n > 0) {
            impl_->decoder.feed(std::string_view(tmp, static_cast<std::size_t>(n)));
            continue; // try to decode what we just read
        }
        if (n == 0)
            return Key::none(); // EOF on the terminal
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!block)
                return Key::none();
            continue;
        }
        return Key::none();
    }
}

void CursesTerminal::set_cursor_visible(bool visible)
{
    curs_set(visible ? 1 : 0);
}

void CursesTerminal::beep() { ::beep(); }

#ifdef TESTING
// GCOVR_EXCL_START -- test-only coverage harness in src/, not shipped code.
int curses_terminal_testing_exercise_internal_helpers()
{
    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
            failed_check = check_index;
    };
    bool default_constructor_failed = false;
    try {
        CursesTerminal terminal(CursesTerminal::Options{});
        (void)terminal;
    } catch (const std::runtime_error&) {
        default_constructor_failed = true;
    }
    check(default_constructor_failed);

    restore_terminal();
    on_sigwinch(SIGWINCH);
    check(g_resize_pending);
    if (g_resize_pending) {
        g_resize_pending = 0;
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
            write_all(in_pipe[1], "\x1b[113u");
            const Key key = term.poll_key(false);
            check(key.is_char(U'q'));
            ::close(in_pipe[1]);
            ::close(in_pipe[0]);
            term.impl_->in_fd = STDIN_FILENO;
        }

        int empty_pipe[2]{};
        if (::pipe(empty_pipe) == 0) {
            ::close(empty_pipe[1]);
            term.impl_->in_fd = empty_pipe[0];
            check(term.poll_key(false).is_none());
            ::close(empty_pipe[0]);
            term.impl_->in_fd = STDIN_FILENO;
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
        write_all(write_pipe[1], "terminal-write-test");
        ::close(write_pipe[1]);
        char buf[64]{};
        const ssize_t n = ::read(write_pipe[0], buf, sizeof(buf));
        check(n == 19 && std::string(buf, static_cast<std::size_t>(n)) == "terminal-write-test");
        ::close(write_pipe[0]);
    }

    auto probe = [&](std::string_view reply, bool expected) {
        int in_pipe[2]{};
        int out_pipe[2]{};
        if (::pipe(in_pipe) != 0)
            return;
        if (::pipe(out_pipe) != 0) {
            ::close(in_pipe[0]);
            ::close(in_pipe[1]);
            return;
        }
        write_all(in_pipe[1], reply);
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
// GCOVR_EXCL_STOP
#endif

} // namespace og::curses
