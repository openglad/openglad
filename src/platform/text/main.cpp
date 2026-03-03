/* openglad_text: Headless text-based simulation client for OpenGlad.
 *
 * Runs the full game simulation via GameWorld::tick() without any
 * rendering or audio. Communicates via line-based stdin/stdout protocol.
 *
 * Usage:
 *   openglad_text --campaign <name> --level <num> --team <fam1,fam2,...> [--seed <num>]
 *
 * Commands (stdin):
 *   tick [N]     - advance N ticks (default 1), print tick results
 *   state        - dump all entity positions/status as JSON
 *   events       - drain and print accumulated sim events
 *   input <player> <key> - inject a player input
 *   quit         - exit
 *
 * All output is JSON lines on stdout.
 *
 * Copyright (C) 1995-2002 FSGames. Ported by Sean Ford and Yan Shosh.
 * Licensed under GPL v2.
 */

#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/platform/game_context.h>
#include <openglad/interface/input_state.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/interface/ui/picker.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Globals required by entity/runtime code
// ---------------------------------------------------------------------------

// Entity code references session members for legacy reasons.  The headless
// session buffer below provides zero-initialized storage for myscreen_,
// theprefs_, current_difficulty_, etc.
#include <openglad/interface/session_state.h>
namespace og::runtime {
    static SessionState s_headless_session{};
    thread_local SessionState* current_session = &s_headless_session;
    std::atomic<SessionState*> primary_session{&s_headless_session};
}

// cfg is declared in <openglad/resources/gparser.h> (already included above).

// Platform lifecycle (headless implementation in platform_headless.cpp)
void io_init(int argc, char* argv[]);
void io_exit();
void emit_headless_unsupported_warnings_probe();

// popup_dialog: normally shows an SDL dialog, we just print to stderr
void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

// random(): used by ProductionRandom (entity code uses sim_rng instead)
std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 12345;
    if (x == 0) return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

// read_pixie_file: defined in og_file.cpp (SDL-free)
// Declared in pixie_data.h, linked automatically.

// Protocol session implementation moved to text_protocol.cpp

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

struct TextClientArgs {
    std::string campaign = "org.openglad.gladiator";
    int level = 1;
    std::vector<int> team_families; // family IDs
    std::uint32_t seed = 42;
    bool protocol_mode = false;
    bool probe_unsupported_warnings = false;
};

static bool parse_args(int argc, char* argv[], TextClientArgs& args)
{
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--campaign" && i + 1 < argc) {
            args.campaign = argv[++i];
        } else if (arg == "--level" && i + 1 < argc) {
            args.level = std::atoi(argv[++i]);
        } else if (arg == "--team" && i + 1 < argc) {
            std::string team_str = argv[++i];
            std::istringstream ss(team_str);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                args.team_families.push_back(std::atoi(tok.c_str()));
            }
        } else if (arg == "--seed" && i + 1 < argc) {
            args.seed = static_cast<std::uint32_t>(std::atol(argv[++i]));
        } else if (arg == "--protocol") {
            args.protocol_mode = true;
        } else if (arg == "--probe-unsupported-warnings") {
            args.probe_unsupported_warnings = true;
        } else if (arg == "--help" || arg == "-h") {
            std::fprintf(stderr,
                "Usage: openglad_text [options]\n"
                "  --campaign <name>   Campaign ID (default: org.openglad.gladiator)\n"
                "  --level <num>       Level number (default: 1)\n"
                "  --team <f1,f2,...>  Team family IDs, comma-separated (default: 0 = soldier)\n"
                "  --seed <num>        RNG seed (default: 42)\n"
                "  --protocol          Run JSON protocol mode directly (no picker)\n"
                "  --probe-unsupported-warnings  Emit one-time headless unsupported warnings and exit\n"
                "\nCommands (stdin):\n"
                "  tick [N]   Advance N simulation ticks\n"
                "  state      Dump entity state as JSON\n"
                "  events     Drain and print sim events\n"
                "  quit       Exit\n");
            return false;
        }
    }
    if (args.team_families.empty())
        args.team_families.push_back(FAMILY_SOLDIER);
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    TextClientArgs args;
    if (!parse_args(argc, argv, args))
        return 0;

    // Initialize filesystem (SDL-free)
    io_init(argc, argv);

    // Load configuration
    cfg.load_settings();
    init_all_registries();

    if (args.probe_unsupported_warnings) {
        emit_headless_unsupported_warnings_probe();
        io_exit();
        return 0;
    }

    og::ui::TextPickerConfig picker_config;
    picker_config.campaign = args.campaign;
    picker_config.level = args.level;
    picker_config.team_families = args.team_families;
    picker_config.seed = args.seed;

    int rc = 0;
    if (args.protocol_mode) {
        rc = og::ui::run_text_picker_protocol_session(picker_config);
        io_exit();
        return rc;
    }

    og::ui::TextPickerError picker_error;
    og::ui::run_text_picker(picker_config, &picker_error);
    if (picker_error.code != og::ui::TextPickerErrorCode::None) {
        std::fprintf(stderr, "picker error: %s\n", picker_error.detail.c_str());
        rc = 1;
    }

    io_exit();
    return rc;
}
