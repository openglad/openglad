/* Text protocol session implementation for openglad_text.
 *
 * Runs a headless JSON protocol mode for a single game session.
 */

#include <openglad/ui/text_protocol.h>

#include <openglad/gameplay/game_world.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/sim/irandom.h>
#include <openglad/data/level_data.h>
#include <openglad/data/level_data_hooks.h>
#include <openglad/data/save_data.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/game_context.h>

#include <cstdio>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <openglad/data/gparser.h> // cfg_store, ::cfg

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

static void wire_all_entities(LevelData& level)
{
    for (auto& uptr : level.oblist) level.wire_entity(uptr.get());
    for (auto& uptr : level.fxlist) level.wire_entity(uptr.get());
    for (auto& uptr : level.weaplist) level.wire_entity(uptr.get());
}

static void cmd_tick(GameWorld& world, LevelData& level, SaveData& save,
                     std::int32_t& enemy_freeze, char& end,
                     og::sim::SimEventLog& events, int count)
{
    std::cout << "{\"cmd\":\"tick\",\"count\":" << count << ",\"results\":[";
    for (int i = 0; i < count; i++) {
        if (i > 0) std::cout << ",";
        auto result = world.tick(save, enemy_freeze, end, events);
        level.level_done = result.level_done;
        std::cout << "{\"tick\":" << world.tick_count_
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

    // Create level data and load (headless — no tile graphics)
    LevelData level(args.level, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    world.rng_.state_ = args.seed;
    world.tick_count_ = 0;
    world.reset_level_progress();
    SaveData save;
    save.current_campaign = args.campaign;
    save.scen_num = static_cast<short>(args.level);
    save.numplayers = 1;

    if (!level.load()) {
        std::fprintf(stderr, "Failed to load level %d\n", args.level);
        set_global_context(nullptr);
        return 1;
    }

    // Create team walkers and add to the level
    for (size_t i = 0; i < args.team_families.size(); i++) {
        int family = args.team_families[i];
        walker* w = level.add_ob(Order::Living, family);
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
    std::int32_t enemy_freeze = 0;
    char end = 0;

    level.set_sim_context(&save, &enemy_freeze, &events, &entity_rng, &cfg);
    wire_all_entities(level);

    // Output ready message
    std::cout << "{\"status\":\"ready\""
              << ",\"level\":" << args.level
              << ",\"title\":\"" << level.world().title << "\""
              << ",\"num_entities\":" << level.oblist.size()
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
            cmd_tick(world, level, save, enemy_freeze, end, events, count);
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
    return 0;
}

} // namespace og::ui
