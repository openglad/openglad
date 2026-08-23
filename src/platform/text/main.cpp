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
 *   census       - per-team alive counts + named-hero and crew status
 *   events       - drain and print accumulated sim events
 *   quit         - exit
 *
 * All output is JSON lines on stdout.
 *
 * Copyright (C) 1995-2002 FSGames. Ported by Sean Ford and Yan Shosh.
 * Licensed under GPL v2.
 */

#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/core/irandom.h>
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
#include <openglad/core/weather.h>
#include <openglad/interface/game_context.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/interface/ui/picker.h>

#include <chrono>
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

// read_pixie_file: defined in og_file.cpp (SDL-free)
// Declared in pixie_data.h, linked automatically.

// Protocol session implementation moved to text_protocol.cpp

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

struct TextClientArgs {
    std::string campaign = "gladiator";
    int level = 1;
    std::vector<int> team_families; // family IDs
    std::uint32_t seed = 42;
    int team_level = 0; // 0 = loader-default stats (legacy)
    std::vector<std::pair<std::string, std::int32_t>> campaign_state;
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
        } else if (arg == "--team-level" && i + 1 < argc) {
            args.team_level = std::atoi(argv[++i]);
        } else if (arg == "--campaign-state" && i + 1 < argc) {
            std::string state_str = argv[++i];
            std::istringstream ss(state_str);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                size_t eq = tok.find('=');
                if (eq == std::string::npos || eq == 0) {
                    std::fprintf(stderr,
                        "--campaign-state expects key=value[,key=value...]\n");
                    return false;
                }
                args.campaign_state.emplace_back(tok.substr(0, eq),
                    static_cast<std::int32_t>(
                        std::atol(tok.c_str() + eq + 1)));
            }
        } else if (arg == "--protocol") {
            args.protocol_mode = true;
        } else if (arg == "--probe-unsupported-warnings") {
            args.probe_unsupported_warnings = true;
        } else if (arg == "--help" || arg == "-h") {
            std::fprintf(stderr,
                "Usage: openglad_text [options]\n"
                "  --campaign <name>   Campaign ID (default: gladiator)\n"
                "  --level <num>       Level number (default: 1)\n"
                "  --team <f1,f2,...>  Team family IDs, comma-separated (default: 0 = soldier)\n"
                "  --seed <num>        RNG seed (default: 42)\n"
                "  --team-level <n>    Upgrade each spawned team guy to level n (requires --protocol; default: 0 = family defaults)\n"
                "  --campaign-state <k=v,...>  Pre-seed campaign decision state (requires --protocol)\n"
                "  --protocol          Run JSON protocol mode directly (no picker)\n"
                "  --probe-unsupported-warnings  Emit one-time headless unsupported warnings and exit\n"
                "\nCommands (stdin):\n"
                "  tick [N]   Advance N simulation ticks\n"
                "  state      Dump entity state as JSON\n"
                "  census     Per-team alive counts + named-hero and crew status\n"
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
    // One-shot clock sample for the per-level weather roll nonce (test
    // builds bypass this main and keep the deterministic default 0).
    og::set_weather_roll_nonce(static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    TextClientArgs args;
    if (!parse_args(argc, argv, args))
        return 0;

    // #247: --team-level and --campaign-state are inputs to the CLI session
    // assembler in run_text_protocol_session, which only --protocol reaches.
    // The picker's GO stages its world from the picker's own live save, and
    // neither flag ever touches that save — so accepting them here would
    // drop them silently, the exact dishonesty class #247 closes. Refuse
    // loudly instead.
    if (!args.protocol_mode &&
        (args.team_level > 0 || !args.campaign_state.empty())) {
        std::fprintf(stderr,
            "--team-level and --campaign-state require --protocol: the "
            "picker's GO stages from its own save and cannot apply them.\n");
        return 1;
    }

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
    picker_config.team_level = args.team_level;
    picker_config.campaign_state = args.campaign_state;

    int rc = 0;
    if (args.protocol_mode) {
        // --protocol has no picker and therefore no save to stage from: the
        // CLI shape, with the config scalars and the CLI crew assembler.
        rc = og::ui::run_text_picker_protocol_session(picker_config,
            /*session_save=*/nullptr, /*difficulty=*/1);
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
