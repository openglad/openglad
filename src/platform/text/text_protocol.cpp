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
#include <openglad/resources/level_io.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/resources/save_io.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/stats.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>

#include <cstdio>
#include <cstddef>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <openglad/resources/gparser.h> // cfg_store, ::cfg

namespace og::ui {
namespace {

constexpr std::size_t kMaxProtocolLineBytes = 64 * 1024;

enum class ReadLineStatus {
    Ok,
    Eof,
    TooLong,
};

ReadLineStatus read_line_capped(std::istream& in, std::string& out, std::size_t max_bytes)
{
    out.clear();
    bool overflow = false;
    for (;;) {
        int next = in.get();
        if (next == EOF) {
            if (in.bad())
                return ReadLineStatus::Eof;
            return (out.empty() && !overflow) ? ReadLineStatus::Eof : ReadLineStatus::Ok;
        }

        char ch = static_cast<char>(next);
        if (ch == '\n')
            break;
        if (ch == '\r')
            continue;

        if (!overflow) {
            if (out.size() >= max_bytes) {
                overflow = true;
            } else {
                out.push_back(ch);
            }
        }
    }

    return overflow ? ReadLineStatus::TooLong : ReadLineStatus::Ok;
}

std::string json_escape_string(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 8);
    static constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : input)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7f)
                {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0f];
                    out += hex[c & 0x0f];
                }
                else
                {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

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
        os << ",\"text\":\"" << json_escape_string(ev.text) << "\"";
    }
    os << "}";
}

static void cmd_tick(og::gameplay::GameWorld& world, SaveData& save,
                     og::sim::SimEventLog& events,
                     std::vector<og::sim::Event>& pending_events, int count)
{
    constexpr std::size_t kMaxPendingEvents = 10000;
    std::cout << "{\"cmd\":\"tick\",\"count\":" << count << ",\"results\":[";
    for (int i = 0; i < count; i++) {
        if (i > 0) std::cout << ",";
        world.my_team = save.my_team;
        world.tick();
        // Mirror SDL/runtime behavior: SimEventLog must be drained each tick.
        auto drained_events = events.drain();
        pending_events.insert(pending_events.end(),
                              drained_events.begin(),
                              drained_events.end());
        if (pending_events.size() > kMaxPendingEvents) {
            const std::size_t drop_count = pending_events.size() - kMaxPendingEvents;
            pending_events.erase(pending_events.begin(),
                                 pending_events.begin() + static_cast<std::ptrdiff_t>(drop_count));
        }
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

static void cmd_events(std::vector<og::sim::Event>& pending_events)
{
    std::ostringstream os;
    os << "{\"cmd\":\"events\",\"events\":[";
    for (size_t i = 0; i < pending_events.size(); i++) {
        if (i > 0) os << ",";
        json_event(os, pending_events[i]);
    }
    os << "]}";
    std::cout << os.str() << "\n";
    std::cout.flush();
    pending_events.clear();
}

} // namespace

int run_text_protocol_session(const TextProtocolArgs& args)
{
    og::runtime::ensure_thread_game();
    auto* thread_session = og::runtime::current_session;
    if (!thread_session) {
        std::fprintf(stderr, "Missing thread session for text protocol\n");
        return 1;
    }
    og::runtime::install_thread_session(thread_session);

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
    // Text protocol is a lightweight client (no constructed GameSession ctx_); it
    // installs a scoped ctx() override for this session.
    set_global_context(&text_ctx);
    og::gameplay::GameWorld* prev_world = thread_session->gameplay_.world;
    og::sim::SimEventLog* prev_events = thread_session->gameplay_.sim_events;
    og::gameplay::PathfindingState* prev_pathfinding = thread_session->gameplay_.pathfinding;

    // Create GameWorld and load (headless — no tile graphics)
    og::gameplay::GameWorld world;
    world.myobmap = std::make_unique<obmap>();
    world.id = args.level;
    loader ldr;
    wire_loader_to_world(world, ldr, true);
    // Wire pre-delete hook for headless (no view controls to clear)
    auto& bridge = og::interface::platform_bridge();
    if (bridge.clear_stale_view_controls)
        world.on_pre_delete_objects = [&bridge](og::gameplay::GameWorld* w) { bridge.clear_stale_view_controls(w); };

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
            thread_session->gameplay_.world = prev_world;
            thread_session->gameplay_.sim_events = prev_events;
            thread_session->gameplay_.pathfinding = prev_pathfinding;
            og::runtime::install_thread_session(thread_session);
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
    thread_session->gameplay_.world = &world;
    thread_session->gameplay_.sim_events = &events;
    thread_session->gameplay_.pathfinding = world.pathfinding.get();
    og::runtime::install_thread_session(thread_session);

    world.set_sim_context(&save, &world.enemy_freeze, &events, &entity_rng, &cfg);

    // Output ready message
    std::cout << "{\"status\":\"ready\""
              << ",\"level\":" << args.level
              << ",\"title\":\"" << json_escape_string(world.title) << "\""
              << ",\"num_entities\":" << world.oblist.size()
              << ",\"seed\":" << args.seed
              << "}\n";
    std::cout.flush();

    std::vector<og::sim::Event> pending_events;

    // Command loop
    std::string line;
    for (;;) {
        const ReadLineStatus line_status = read_line_capped(std::cin, line, kMaxProtocolLineBytes);
        if (line_status == ReadLineStatus::Eof)
            break;
        if (line_status == ReadLineStatus::TooLong) {
            std::cout << "{\"cmd\":\"error\",\"message\":\"input line too long (max 65536 bytes)\"}\n";
            std::cout.flush();
            continue;
        }
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "tick") {
            int count = 1;
            iss >> count;
            if (count < 1) count = 1;
            if (count > 10000) count = 10000;
            cmd_tick(world, save, events, pending_events, count);
        } else if (cmd == "state") {
            cmd_state(world);
        } else if (cmd == "events") {
            cmd_events(pending_events);
        } else if (cmd == "quit") {
            std::cout << "{\"cmd\":\"quit\",\"status\":\"ok\"}\n";
            std::cout.flush();
            break;
        } else {
            std::cout << "{\"cmd\":\"error\",\"message\":\"unknown command: "
                      << json_escape_string(cmd) << "\"}\n";
            std::cout.flush();
        }

        if (world.end) {
            std::cout << "{\"status\":\"game_over\"}\n";
            std::cout.flush();
        }
    }

    set_global_context(nullptr);
    thread_session->gameplay_.world = prev_world;
    thread_session->gameplay_.sim_events = prev_events;
    thread_session->gameplay_.pathfinding = prev_pathfinding;
    og::runtime::install_thread_session(thread_session);
    return 0;
}

} // namespace og::ui
