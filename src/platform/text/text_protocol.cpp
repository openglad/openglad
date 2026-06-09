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
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/platform/game_context.h>

#include <cstdio>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include <openglad/resources/gparser.h> // cfg_store, ::cfg

namespace og::ui {
namespace {

static void json_entity(std::ostream& os, const walker* w, int index)
{
    os << "{\"id\":" << index
       << ",\"order\":" << static_cast<int>(w->query_order())
       << ",\"family\":" << static_cast<int>(w->family())
       << ",\"team\":" << static_cast<int>(w->team_num())
       << ",\"x\":" << w->xpos()
       << ",\"y\":" << w->ypos()
       << ",\"hp\":" << (w->stats() ? w->stats()->hitpoints() : 0)
       << ",\"max_hp\":" << (w->stats() ? w->stats()->max_hitpoints() : 0)
       << ",\"dead\":" << (w->dead() ? "true" : "false")
       << "}";
}

static void json_string(std::ostream& os, std::string_view value)
{
    static constexpr char kHex[] = "0123456789abcdef";

    os << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"':
            os << "\\\"";
            break;
        case '\\':
            os << "\\\\";
            break;
        case '\b':
            os << "\\b";
            break;
        case '\f':
            os << "\\f";
            break;
        case '\n':
            os << "\\n";
            break;
        case '\r':
            os << "\\r";
            break;
        case '\t':
            os << "\\t";
            break;
        default:
            if (c < 0x20) {
                os << "\\u00" << kHex[c >> 4] << kHex[c & 0x0f];
            } else {
                os << static_cast<char>(c);
            }
            break;
        }
    }
    os << '"';
}

static void json_event(std::ostream& os, const og::sim::Event& ev)
{
    os << "{\"tick\":" << ev.tick
       << ",\"kind\":" << static_cast<int>(ev.kind)
       << ",\"a\":" << ev.a
       << ",\"b\":" << ev.b;
    if (!ev.text.empty()) {
        os << ",\"text\":";
        json_string(os, ev.text);
    }
    os << "}";
}

static void cmd_tick(GameWorld& world, int count)
{
    std::cout << "{\"cmd\":\"tick\",\"count\":" << count << ",\"results\":[";
    for (int i = 0; i < count; i++) {
        if (i > 0) std::cout << ",";
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

static void cmd_state(const LevelRuntimeData& level)
{
    std::ostringstream os;
    os << "{\"cmd\":\"state\",\"entities\":[";
    int idx = 0;
    for (auto& uptr : level.world().oblist) {
        if (idx > 0) os << ",";
        json_entity(os, uptr.get(), idx++);
    }
    os << "],\"weapons\":[";
    idx = 0;
    for (auto& uptr : level.world().weaplist) {
        if (idx > 0) os << ",";
        json_entity(os, uptr.get(), idx++);
    }
    os << "],\"fx\":[";
    idx = 0;
    for (auto& uptr : level.world().fxlist) {
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

#ifdef TESTING
std::string text_protocol_testing_format_event_text(std::string_view text)
{
    og::sim::Event event;
    event.tick = 7;
    event.kind = og::sim::EventKind::Notification;
    event.a = 1;
    event.b = 2;
    event.text = text;

    std::ostringstream os;
    json_event(os, event);
    return os.str();
}
#endif

int run_text_protocol_session(const TextProtocolArgs& args)
{
    // Set up sim infrastructure
    og::sim::SimEventLog events;

    // Configure the active session context for this protocol run.
    GameContext& text_ctx = ctx();
    IRandom* prev_rng = text_ctx.rng;

    // Create level data and load (headless — no tile graphics)
    LevelRuntimeData level(args.level, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    world.rng_.state_ = args.seed;
    world.tick_count_ = 0;
    world.reset_level_progress();
    text_ctx.rng = &world.rng_;
    set_gameplay_rng_override(&text_ctx.rng);
    SaveData save;
    save.current_campaign = args.campaign;
    save.scen_num = static_cast<short>(args.level);
    save.numplayers = 1;

    level.set_sim_context(&save, &world.enemy_freeze, &events, &world.rng_, &cfg);

    if (!level.load()) {
        std::fprintf(stderr, "Failed to load level %d\n", args.level);
        text_ctx.rng = prev_rng;
        set_gameplay_rng_override(nullptr);
        return 1;
    }

    // Create team walkers and add to the level
    for (size_t i = 0; i < args.team_families.size(); i++) {
        int family = args.team_families[i];
        walker* w = level.add_ob(Order::Living, family);
        if (w) {
            w->set_team_num(0);
            w->set_real_team_num(0);
            w->set_user(static_cast<signed char>(i < 4 ? i : -1));

            auto g = std::make_unique<guy>();
            g->family = static_cast<char>(family);
            g->name = std::format("Player{}", i + 1);
            w->set_owned_myguy(std::move(g));
        }
    }

    // Wire up sim pointers on all entities
    world.enemy_freeze = 0;
    world.end = 0;

    GameplayContext text_game_ctx;
    text_game_ctx.world = &world;
    text_game_ctx.save = &save;
    text_game_ctx.sim_events = &events;
    text_game_ctx.config = &cfg;
    GameplayContext* prev_game = current_game;
    current_game = &text_game_ctx;

    // Output ready message
    std::cout << "{\"status\":\"ready\""
              << ",\"level\":" << args.level
              << ",\"title\":";
    json_string(std::cout, level.world().title);
    std::cout << ",\"num_entities\":" << level.world().oblist.size()
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
            cmd_tick(world, count);
        } else if (cmd == "state") {
            cmd_state(level);
        } else if (cmd == "events") {
            cmd_events(events);
        } else if (cmd == "quit") {
            std::cout << "{\"cmd\":\"quit\",\"status\":\"ok\"}\n";
            std::cout.flush();
            break;
        } else {
            std::cout << "{\"cmd\":\"error\",\"message\":";
            json_string(std::cout, std::string("unknown command: ") + cmd);
            std::cout << "}\n";
            std::cout.flush();
        }

        if (world.end) {
            std::cout << "{\"status\":\"game_over\"}\n";
            std::cout.flush();
        }
    }

    current_game = prev_game;
    text_ctx.rng = prev_rng;
    set_gameplay_rng_override(nullptr);
    return 0;
}

} // namespace og::ui
