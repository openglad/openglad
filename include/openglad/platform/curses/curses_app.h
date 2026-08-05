/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>
#include <string>

namespace og::curses {

// Parsed command-line options for the curses client.
struct AppOptions {
    std::string campaign = "gladiator";
    int level = 1;
    std::string save_name = "curses_quicksave";
    std::uint32_t seed = 42;
    int difficulty = 1;

    bool allow_unicode = true;
    bool allow_color = true;

    // Networking launch shortcuts (skip straight into host/join).
    bool host = false;
    int host_port = 0;
    std::string join_url;
    std::string relay_url;
};

// Parse argv into AppOptions. Returns false and prints usage when --help is
// given or arguments are malformed (sets *should_exit accordingly).
bool parse_app_options(int argc, char* argv[], AppOptions& out, bool* should_exit);

// Run the full client: init filesystem/config/registries, open a real terminal,
// and drive the picker (or jump straight into host/join). Returns a process exit
// code. This is what main() calls; argc/argv are forwarded to filesystem init.
int run_curses_app(const AppOptions& options, int argc, char* argv[]);

} // namespace og::curses
