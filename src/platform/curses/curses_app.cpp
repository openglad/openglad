/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/curses/curses_app.h>

#include <openglad/platform/curses/clock.h>
#include <openglad/platform/curses/curses_network.h>
#include <openglad/platform/curses/curses_picker_client.h>
#include <openglad/platform/curses/curses_terminal.h>

#include <openglad/core/irandom.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/input.h> // load_player_control_settings_from_cfg, set_player_control_mode
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

void io_init(int argc, char* argv[]);
void io_exit();

namespace og::curses {

bool parse_app_options(int argc, char* argv[], AppOptions& out, bool* should_exit)
{
    if (should_exit)
        *should_exit = false;

    auto need_value = [&](int& i) -> const char* {
        if (i + 1 >= argc)
            return nullptr;
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--campaign") {
            const char* v = need_value(i);
            if (!v) return false;
            out.campaign = v;
        } else if (arg == "--level") {
            const char* v = need_value(i);
            if (!v) return false;
            out.level = std::atoi(v);
        } else if (arg == "--save") {
            const char* v = need_value(i);
            if (!v) return false;
            out.save_name = v;
        } else if (arg == "--seed") {
            const char* v = need_value(i);
            if (!v) return false;
            out.seed = static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--difficulty") {
            const char* v = need_value(i);
            if (!v) return false;
            out.difficulty = std::atoi(v);
        } else if (arg == "--host") {
            out.host = true;
        } else if (arg == "--port") {
            const char* v = need_value(i);
            if (!v) return false;
            out.host_port = std::atoi(v);
        } else if (arg == "--join") {
            const char* v = need_value(i);
            if (!v) return false;
            out.join_url = v;
        } else if (arg == "--relay") {
            const char* v = need_value(i);
            if (!v) return false;
            out.relay_url = v;
        } else if (arg == "--no-unicode") {
            out.allow_unicode = false;
        } else if (arg == "--no-color") {
            out.allow_color = false;
        } else if (arg == "--help" || arg == "-h") {
            std::printf(
                "Usage: openglad_curses [options]\n"
                "  --campaign <id>   Campaign ID (default: org.openglad.gladiator)\n"
                "  --level <n>       Starting level (default: 1)\n"
                "  --save <name>     Save slot name (default: curses_quicksave)\n"
                "  --seed <num>      RNG seed (default: 42)\n"
                "  --difficulty <n>  Difficulty index 0..2 (default: 1)\n"
                "  --host [--port p] Host a networked game\n"
                "  --join <url>      Join a networked game (ws:// or wss:// URL)\n"
                "  --relay <url>     Use a relay server for host/join\n"
                "  --no-unicode      ASCII-only glyphs\n"
                "  --no-color        Monochrome output\n");
            if (should_exit)
                *should_exit = true;
            return false;
        } else {
            std::fprintf(stderr, "openglad_curses: unknown option '%s' (try --help)\n", arg.c_str());
            return false;
        }
    }
    return true;
}

namespace {

// Install a minimal gameplay context on the headless session so menu code that
// expects current_game/ctx() to be valid behaves exactly as it does under test
// (the per-level game runtime installs its own contexts while playing).
void install_app_session_context()
{
    static ProductionRandom app_rng;
    static GameWorld app_fallback_world(0);
    static SaveData app_fallback_save;

    og::runtime::SessionState& session = *og::runtime::current_session;
    session.ctx_.rng = &app_rng;
    session.game_.world = &app_fallback_world;
    session.game_.save = &app_fallback_save;
    session.game_.sim_events = session.ctx_.sim_events.get();
    session.game_.config = &cfg;
    session.game_.session_rng_ref = &session.ctx_.rng;
    session.game_.gameplay_active_ref = &session.gameplay_active_;
    current_game = &session.game_;

    // Load the player's real keybindings AND their 4/8-direction mode from the
    // config (the very same loader the SDL client uses). CursesInput resolves
    // terminal keys against current_session->player_keys_[0], so this config-loaded
    // table is the single source of truth for which key does what — the curses
    // client hardcodes no movement/action keys of its own, and honors the chosen
    // direction mode (the Kitty protocol's real key-up/down makes 4-direction work
    // exactly as well as 8-direction).
    if (!session.input_hw_)
        session.input_hw_ = std::make_unique<InputHardwareState>();
    load_player_control_settings_from_cfg(cfg);
}

} // namespace

int run_curses_app(const AppOptions& options, int argc, char* argv[])
{
    init_logging();
    io_init(argc, argv);
    cfg.load_settings();
    init_all_registries();
    install_app_session_context();

    CursesTerminal::Options term_options;
    term_options.use_unicode = options.allow_unicode;
    term_options.use_color = options.allow_color;

    std::unique_ptr<CursesTerminal> terminal;
    try {
        terminal = std::make_unique<CursesTerminal>(term_options);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "openglad_curses: %s\n", e.what());
        io_exit();
        return 1;
    }

    SteadyClock clock;

    og::ui::TextPickerConfig config;
    config.campaign = options.campaign;
    config.level = options.level;
    config.save_name = options.save_name;
    config.seed = options.seed;

    CursesPickerOptions picker_options;
    picker_options.allow_unicode = options.allow_unicode;
    picker_options.allow_color = options.allow_color;
    picker_options.difficulty = options.difficulty;
    picker_options.host_port = options.host_port;

    if (options.host || !options.join_url.empty()) {
        // Command-line shortcut straight into a networked game with a default
        // starting team. (The in-menu Networking submenu offers the same with a
        // hand-built team.)
        SaveData save;
        save.current_campaign = options.campaign;
        save.scen_num = static_cast<short>(options.level);
        save.numplayers = 1;
        save.my_team = 0;
        og::ui::initialize_starting_team(save, {FAMILY_SOLDIER});

        std::string error;
        std::unique_ptr<CursesLobby> lobby;
        if (options.host) {
            HostOptions host_options;
            host_options.port = options.host_port;
            host_options.relay_url = options.relay_url;
            host_options.difficulty = options.difficulty;
            lobby = make_host_lobby(save, host_options, &error);
        } else {
            JoinOptions join_options;
            join_options.url = options.join_url;
            join_options.via_relay = !options.relay_url.empty();
            lobby = make_join_lobby(save, join_options, &error);
        }

        if (!lobby) {
            terminal.reset();
            std::fprintf(stderr, "openglad_curses: %s\n", error.c_str());
            io_exit();
            return 1;
        }
        run_curses_lobby(*lobby, *terminal, clock);
    } else {
        run_curses_picker(*terminal, clock, config, picker_options);
    }

    // Tear the terminal down (endwin) before shutting the filesystem.
    terminal.reset();
    io_exit();
    return 0;
}

} // namespace og::curses
