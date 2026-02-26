/* Text protocol session implementation for openglad_text.
 *
 * Runs a headless JSON protocol mode for a single game session.
 */

#include <openglad/interface/ui/text_protocol.h>

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/stats.h>
#include <openglad/platform/game_context.h>

#include <cstdio>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <openglad/resources/gparser.h> // cfg_store, ::cfg

namespace og::ui {
namespace {

static void json_entity(std::ostream& os, const walker* w, int index)
{
    os << "{\"id\":" << index
       << ",\"order\":" << static_cast<int>(w->query_order())
       << ",\"family\":" << static_cast<int>(w->family)
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

static void cmd_tick(og::gameplay::GameWorld& world, SaveData& save,
                     og::sim::SimEventLog& events, int count)
{
    std::cout << "{\"cmd\":\"tick\",\"count\":" << count << ",\"results\":[";
    for (int i = 0; i < count; i++) {
        if (i > 0) std::cout << ",";
        world.my_team = save.my_team;
        world.tick();
        std::cout << "{\"tick\":" << world.tick_count_
                  << ",\"level_done\":" << world.level_done
                  << ",\"game_ended\":" << (world.game_ended ? "true" : "false")
                  << ",\"next_level\":" << world.next_level
                  << ",\"ending\":" << world.ending
                  << "}";
        if (world.game_ended) {
            world.end = 1;
            break;
        }
    }
    std::cout << "]}\n";
    std::cout.flush();
}

static void cmd_state(const og::gameplay::GameWorld& world)
{
    std::ostringstream os;
    os << "{\"cmd\":\"state\",\"entities\":[";
    int idx = 0;
    for (auto& uptr : world.oblist) {
        if (idx > 0) os << ",";
        json_entity(os, uptr.get(), idx++);
    }
    os << "],\"weapons\":[";
    idx = 0;
    for (auto& uptr : world.weaplist) {
        if (idx > 0) os << ",";
        json_entity(os, uptr.get(), idx++);
    }
    os << "],\"fx\":[";
    idx = 0;
    for (auto& uptr : world.fxlist) {
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

} // namespace

int run_text_protocol_session(const TextProtocolArgs& args)
{
    // Set up sim infrastructure
    og::sim::SimEventLog events;

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

    // Set up GameContext minimally
    GameContext text_ctx;
    text_ctx.rng = &entity_rng;
    text_ctx.sim_events = std::make_unique<og::sim::SimEventLog>();
    set_global_context(&text_ctx);
    og::gameplay::GameplayContext gameplay_ctx;

    // Create GameWorld and load (headless — no tile graphics)
    og::gameplay::GameWorld world;
    world.myobmap = std::make_unique<obmap>();
    world.id = args.level;
    loader ldr;
    wire_loader_to_world(world, ldr, true);
    // Wire pre-delete hook for headless (no view controls to clear)
    const LevelDataHooks& hooks = headless_level_data_hooks();
    if (hooks.clear_stale_view_controls)
        world.on_pre_delete_objects = [&hooks](og::gameplay::GameWorld* w) { hooks.clear_stale_view_controls(w); };

    SaveData save;
    save.current_campaign = args.campaign;
    save.scen_num = static_cast<short>(args.level);
    save.numplayers = 1;

    {
        LevelVisuals dummy_visuals;
        og::data::LevelFileMetadata meta;
        std::string thefile = std::format("scen{}.fss", args.level);
        if (!og::data::load_level(thefile, world, dummy_visuals, meta)) {
            std::fprintf(stderr, "Failed to load level %d\n", args.level);
            set_global_context(nullptr);
            return 1;
        }
    }

    // Create team walkers and add to the level
    for (size_t i = 0; i < args.team_families.size(); i++) {
        int family = args.team_families[i];
        walker* w = world.add_ob(Order::Living, family);
        if (w) {
            w->team_num = 0;
            w->real_team_num = 0;
            w->user = static_cast<signed char>(i < 4 ? i : -1);

            auto g = std::make_unique<guy>();
            g->family = static_cast<char>(family);
            g->name = std::format("Player{}", i + 1);
            w->set_owned_myguy(std::move(g));
        }
    }

    // Wire up sim pointers on all entities
    world.rng_.state_ = args.seed;
    gameplay_ctx.world = &world;
    gameplay_ctx.sim_events = &events;
    og::gameplay::GameplayContext* prev_game = og::gameplay::current_game;
    og::gameplay::current_game = &gameplay_ctx;

    world.set_sim_context(&save, &world.enemy_freeze, &events, &entity_rng, &cfg);

    // Output ready message
    std::cout << "{\"status\":\"ready\""
              << ",\"level\":" << args.level
              << ",\"title\":\"" << world.title << "\""
              << ",\"num_entities\":" << world.oblist.size()
              << ",\"seed\":" << args.seed
              << "}\n";
    std::cout.flush();

    // Command loop
    std::string line;
    while (std::getline(std::cin, line)) {
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
            cmd_tick(world, save, events, count);
        } else if (cmd == "state") {
            cmd_state(world);
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

        if (world.end) {
            std::cout << "{\"status\":\"game_over\"}\n";
            std::cout.flush();
        }
    }

    set_global_context(nullptr);
    og::gameplay::current_game = prev_game;
    return 0;
}

} // namespace og::ui
