/* openglad_text: Headless text-based simulation client for OpenGlad.
 *
 * Runs the full game simulation via SimWorld::tick() without any
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

#include <openglad/sim/sim_world.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/sim/irandom.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gloader.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/runtime/game_context.h>
#include <openglad/input/input_state.h>

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

// Entity code references myscreen for legacy reasons (all commented out,
// but the symbol must exist at link time).
class screen;
screen* myscreen = nullptr;

// options type needed for GameContext destructor
class options;
options* theprefs = nullptr;

// The global config store
extern cfg_store cfg;

// Platform lifecycle (headless implementation in platform_headless.cpp)
void io_init(int argc, char* argv[]);
void io_exit();

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

// ---------------------------------------------------------------------------
// JSON output helpers
// ---------------------------------------------------------------------------

static void json_entity(std::ostream& os, const walker* w, int index)
{
    os << "{\"id\":" << index
       << ",\"order\":" << static_cast<int>(w->query_order())
       << ",\"family\":" << static_cast<int>(w->query_family())
       << ",\"team\":" << static_cast<int>(w->team_num)
       << ",\"x\":" << w->xpos
       << ",\"y\":" << w->ypos
       << ",\"hp\":" << (w->stats() ? w->stats()->hitpoints : 0)
       << ",\"max_hp\":" << (w->stats() ? w->stats()->max_hitpoints : 0)
       << ",\"dead\":" << (w->dead ? "true" : "false")
       << "}";
}

static void json_event(std::ostream& os, const og::sim::Event& ev)
{
    os << "{\"tick\":" << ev.tick
       << ",\"kind\":" << static_cast<int>(ev.kind)
       << ",\"a\":" << ev.a
       << ",\"b\":" << ev.b;
    if (!ev.text.empty()) {
        // Simple JSON string escape (no control chars expected)
        os << ",\"text\":\"";
        for (char c : ev.text) {
            if (c == '"') os << "\\\"";
            else if (c == '\\') os << "\\\\";
            else os << c;
        }
        os << "\"";
    }
    os << "}";
}

// ---------------------------------------------------------------------------
// Wire sim pointers on all entities in a level (using LevelData::wire_entity)
// ---------------------------------------------------------------------------
static void wire_all_entities(LevelData& level)
{
    for (auto& uptr : level.oblist) level.wire_entity(uptr.get());
    for (auto& uptr : level.fxlist) level.wire_entity(uptr.get());
    for (auto& uptr : level.weaplist) level.wire_entity(uptr.get());
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static void cmd_tick(og::sim::SimWorld& sim, LevelData& level, SaveData& save,
                     std::int32_t& enemy_freeze, char& end,
                     og::sim::SimEventLog& events, int count)
{
    std::cout << "{\"cmd\":\"tick\",\"count\":" << count << ",\"results\":[";
    for (int i = 0; i < count; i++) {
        if (i > 0) std::cout << ",";
        auto result = sim.tick(level, save, enemy_freeze, end, events);
        level.level_done = result.level_done;
        std::cout << "{\"tick\":" << sim.tick_count()
                  << ",\"level_done\":" << result.level_done
                  << ",\"game_ended\":" << (result.game_ended ? "true" : "false")
                  << ",\"next_level\":" << result.next_level
                  << ",\"ending\":" << result.ending
                  << "}";
        if (result.game_ended) {
            end = 1;
            break;
        }
    }
    std::cout << "]}\n";
    std::cout.flush();
}

static void cmd_state(const LevelData& level)
{
    std::ostringstream os;
    os << "{\"cmd\":\"state\",\"entities\":[";
    int idx = 0;
    for (auto& uptr : level.oblist) {
        if (idx > 0) os << ",";
        json_entity(os, uptr.get(), idx++);
    }
    os << "],\"weapons\":[";
    idx = 0;
    for (auto& uptr : level.weaplist) {
        if (idx > 0) os << ",";
        json_entity(os, uptr.get(), idx++);
    }
    os << "],\"fx\":[";
    idx = 0;
    for (auto& uptr : level.fxlist) {
        if (idx > 0) os << ",";
        json_entity(os, uptr.get(), idx++);
    }
    os << "]}";
    std::cout << os.str() << "\n";
    std::cout.flush();
}

static void cmd_events(og::sim::SimEventLog& events)
{
    auto drained = events.drain();
    std::ostringstream os;
    os << "{\"cmd\":\"events\",\"events\":[";
    for (size_t i = 0; i < drained.size(); i++) {
        if (i > 0) os << ",";
        json_event(os, drained[i]);
    }
    os << "]}";
    std::cout << os.str() << "\n";
    std::cout.flush();
}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

struct TextClientArgs {
    std::string campaign = "org.openglad.gladiator";
    int level = 1;
    std::vector<int> team_families; // family IDs
    std::uint32_t seed = 42;
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
        } else if (arg == "--help" || arg == "-h") {
            std::fprintf(stderr,
                "Usage: openglad_text [options]\n"
                "  --campaign <name>   Campaign ID (default: org.openglad.gladiator)\n"
                "  --level <num>       Level number (default: 1)\n"
                "  --team <f1,f2,...>  Team family IDs, comma-separated (default: 0 = soldier)\n"
                "  --seed <num>        RNG seed (default: 42)\n"
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

    // Set up sim infrastructure
    og::sim::SimEventLog events;
    og::sim::SimWorld sim(args.seed);

    // Set up a seeded RNG for entities
    class TextRandom : public IRandom {
        og::sim::SimRandom rng_;
    public:
        explicit TextRandom(std::uint32_t seed) : rng_(seed) {}
        std::uint32_t next(std::uint32_t max_exclusive) override {
            return rng_.next(max_exclusive);
        }
    };
    TextRandom entity_rng(args.seed);

    // Set up GameContext minimally (entities access ctx().sim_events, etc. via their sim_ pointers,
    // but gloader.cpp's active_config() helper calls ctx().config)
    GameContext text_ctx;
    text_ctx.config = &cfg;
    text_ctx.rng = &entity_rng;
    text_ctx.sim_events = std::make_unique<og::sim::SimEventLog>();
    set_global_context(&text_ctx);

    // Create level data and load (headless — no tile graphics)
    LevelData level(args.level, true); // headless=true
    SaveData save;
    save.current_campaign = args.campaign;
    save.scen_num = static_cast<short>(args.level);
    save.numplayers = 1;

    if (!level.load_headless()) {
        std::fprintf(stderr, "Failed to load level %d\n", args.level);
        return 1;
    }

    // Create team walkers and add to the level
    for (size_t i = 0; i < args.team_families.size(); i++) {
        int family = args.team_families[i];
        walker* w = level.add_ob(Order::Living, family);
        if (w) {
            w->team_num = 0; // player team
            w->real_team_num = 0;
            w->user = static_cast<signed char>(i < 4 ? i : -1);

            // Create a guy record for the player
            auto g = std::make_unique<guy>();
            g->family = static_cast<char>(family);
            g->name = std::format("Player{}", i + 1);
            w->set_owned_myguy(std::move(g));
        }
    }

    // Wire up sim pointers on all entities
    std::int32_t enemy_freeze = 0;
    char end = 0;

    // Store sim context on LevelData so entities created mid-tick are auto-wired.
    level.set_sim_context(&save, &enemy_freeze, &events, &entity_rng, &cfg);
    wire_all_entities(level);

    // Output ready message
    std::cout << "{\"status\":\"ready\""
              << ",\"level\":" << args.level
              << ",\"title\":\"" << level.title << "\""
              << ",\"num_entities\":" << level.oblist.size()
              << ",\"seed\":" << args.seed
              << "}\n";
    std::cout.flush();

    // Command loop
    std::string line;
    while (std::getline(std::cin, line)) {
        // Trim whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "tick") {
            int count = 1;
            iss >> count;
            if (count < 1) count = 1;
            cmd_tick(sim, level, save, enemy_freeze, end, events, count);
            // Re-wire sim pointers for newly created entities (from summoning, etc.)
            wire_all_entities(level);
        } else if (cmd == "state") {
            cmd_state(level);
        } else if (cmd == "events") {
            cmd_events(events);
        } else if (cmd == "quit") {
            std::cout << "{\"cmd\":\"quit\",\"status\":\"ok\"}\n";
            std::cout.flush();
            break;
        } else {
            std::cout << "{\"cmd\":\"error\",\"message\":\"unknown command: " << cmd << "\"}\n";
            std::cout.flush();
        }

        if (end) {
            std::cout << "{\"status\":\"game_over\"}\n";
            std::cout.flush();
        }
    }

    set_global_context(nullptr);
    io_exit();
    return 0;
}
