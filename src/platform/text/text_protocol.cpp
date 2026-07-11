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
#include <openglad/resources/io_common.h>
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
       << ",\"floor\":" << w->floor()
       << ",\"hp\":" << (w->stats() ? w->stats()->hitpoints() : 0)
       << ",\"max_hp\":" << (w->stats() ? w->stats()->max_hitpoints() : 0)
       << ",\"dead\":" << (w->dead() ? "true" : "false")
       << "}";
}

static void json_string(std::ostream& os, std::string_view value)
{
    static constexpr std::string_view kHex = "0123456789abcdef";

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
                os << "\\u00" << kHex[static_cast<std::size_t>(c >> 4)]
                   << kHex[static_cast<std::size_t>(c & 0x0f)];
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

static void json_ctf(std::ostream& os, const og::sim::CtfState& ctf)
{
    os << "{\"active\":" << (ctf.active ? "true" : "false")
       << ",\"team_count\":" << static_cast<int>(ctf.team_count)
       << ",\"capture_limit\":" << static_cast<int>(ctf.capture_limit)
       << ",\"winner_team\":" << static_cast<int>(ctf.winner_team)
       << ",\"captures\":[";
    for (int team = 0; team < 4; team++) {
        if (team > 0) os << ",";
        os << static_cast<int>(ctf.captures[team]);
    }
    os << "],\"flags\":[";
    bool first = true;
    for (int team = 0; team < 4; team++) {
        const og::sim::CtfFlag& flag = ctf.flags[team];
        if (!flag.present)
            continue;
        if (!first) os << ",";
        first = false;
        os << "{\"team\":" << team
           << ",\"state\":" << static_cast<int>(flag.state)
           << ",\"carrier\":" << flag.carrier_entity_id
           << ",\"x\":" << flag.x
           << ",\"y\":" << flag.y
           << "}";
    }
    os << "],\"cps\":[";
    for (int index = 0; index < static_cast<int>(ctf.cp_count); index++) {
        const og::sim::CtfControlPoint& cp = ctf.cps[index];
        if (index > 0) os << ",";
        os << "{\"owner\":" << static_cast<int>(cp.owner)
           << ",\"x\":" << cp.x
           << ",\"y\":" << cp.y
           << "}";
    }
    os << "]}";
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
    os << "],\"ctf\":";
    json_ctf(os, level.world().ctf);
    os << "}";
    std::cout << os.str() << "\n";
    std::cout.flush();
}

static void json_census_named(std::ostream& os, const walker* w, bool& first)
{
    if (w == nullptr || w->query_order() != Order::Living)
        return;
    if (w->stats() == nullptr || w->stats()->name.empty())
        return;
    if (!first) os << ",";
    first = false;
    os << "{\"name\":";
    json_string(os, w->stats()->name);
    os << ",\"team\":" << static_cast<int>(w->team_num())
       << ",\"hp\":" << w->stats()->hitpoints()
       << ",\"dead\":" << (w->dead() ? "true" : "false")
       << "}";
}

// Balance-playtest snapshot: per-team alive/dormant Living counts, every
// named Living (placed heroes -- the legacy SAVE_ALL watch set; with
// npc_flags bit2 "protected" present, SAVE_ALL narrows to flagged walkers
// only, but the census still reports all named heroes -- including dead
// ones swept to dead_list), and the spawned crew (myguy-bearing walkers,
// whose corpses persist in oblist).
static void cmd_census(GameWorld& world)
{
    int alive[8] = {};
    int dormant[8] = {};
    for (const auto& uptr : world.oblist) {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        const int team = w->team_num() & 7;
        if (w->dormant())
            dormant[team]++;
        else
            alive[team]++;
    }

    std::ostringstream os;
    os << "{\"cmd\":\"census\",\"tick\":" << world.tick_count_
       << ",\"team_counts\":[";
    for (int team = 0; team < 8; team++) {
        if (team > 0) os << ",";
        os << alive[team];
    }
    os << "],\"dormant_counts\":[";
    for (int team = 0; team < 8; team++) {
        if (team > 0) os << ",";
        os << dormant[team];
    }
    os << "],\"named\":[";
    bool first = true;
    for (const auto& uptr : world.oblist)
        json_census_named(os, uptr.get(), first);
    for (const auto& uptr : world.dead_list)
        json_census_named(os, uptr.get(), first);
    os << "],\"crew\":[";
    first = true;
    for (const auto& uptr : world.oblist) {
        const walker* w = uptr.get();
        if (w == nullptr || w->myguy == nullptr)
            continue;
        if (!first) os << ",";
        first = false;
        os << "{\"name\":";
        json_string(os, w->myguy->name);
        os << ",\"family\":" << static_cast<int>(w->family())
           << ",\"level\":" << static_cast<int>(w->myguy->level)
           << ",\"hp\":" << (w->stats() ? w->stats()->hitpoints() : 0)
           << ",\"max_hp\":" << (w->stats() ? w->stats()->max_hitpoints() : 0)
           << ",\"dead\":" << (w->dead() ? "true" : "false")
           << "}";
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

    // io_init mounts the default campaign; --campaign was only recorded in
    // the save, so non-default campaigns silently loaded the wrong package.
    if (get_mounted_campaign() != args.campaign &&
        mount_campaign_package_with_error(args.campaign) !=
            CampaignPackageIoError::None) {
        std::fprintf(stderr, "Failed to mount campaign %s\n",
            args.campaign.c_str());
        text_ctx.rng = prev_rng;
        set_gameplay_rng_override(nullptr);
        return 1;
    }

    level.set_sim_context(&save, &world.enemy_freeze, &events, &world.rng_, &cfg);

    // Install the gameplay context BEFORE level.load(), not merely before the
    // crew spawns. walker::setxy registers a walker in the collision obmap
    // only while a gameplay context is active (walker_movement.cpp), so
    // loading without one leaves every placed walker OUT of the obmap.
    // Movers self-heal on their first step, but ACT_GUARD posts that never
    // move stayed collision-ghosts for the whole run: weapons flew through
    // them (fire_check ray and real projectiles alike) and other walkers
    // walked into them, forming the interpenetrating "frozen pair" mode.
    // This was the text-harness-only divergence from the production loaders
    // (game.cpp, server_main.cpp GameplayContextGuard-before-load, the curses
    // runtime's explicit "activate the server context for the level load")
    // and from the og_unit_sim fixtures — the same seed/level/crew simulated
    // macroscopically differently under openglad_text.
    GameplayContext text_game_ctx;
    text_game_ctx.world = &world;
    text_game_ctx.save = &save;
    text_game_ctx.sim_events = &events;
    text_game_ctx.config = &cfg;
    GameplayContext* prev_game = current_game;
    current_game = &text_game_ctx;

    if (!level.load()) {
        std::fprintf(stderr, "Failed to load level %d\n", args.level);
        current_game = prev_game;
        text_ctx.rng = prev_rng;
        set_gameplay_rng_override(nullptr);
        return 1;
    }

    // Derive placed-walker stats from their authored levels, exactly like
    // the production loaders (game.cpp load_saved_game and
    // load_headless_level_from_save). Without this pass every placed Living
    // keeps base-family hitpoints while generator SPAWNS get fully leveled
    // stats via create_weapon — which silently skews balance playtests.
    // Runs before the crew spawns so crew walkers (leveled through
    // guy::update_derived_stats) are never touched.
    for (auto& uptr : world.oblist) {
        walker* w = uptr.get();
        if (w != nullptr)
            w->set_difficulty(static_cast<std::uint32_t>(w->stats()->level()));
    }

    // Create team walkers and add to the level
    std::vector<walker*> crew;
    for (size_t i = 0; i < args.team_families.size(); i++) {
        int family = args.team_families[i];
        walker* w = level.add_ob(Order::Living, family);
        if (w) {
            w->set_team_num(0);
            w->set_real_team_num(0);
            w->set_user(static_cast<signed char>(i < 4 ? i : -1));

            // Playtest crews (--team-level > 0) use the family ctor so the
            // guy carries the family baseline attributes a hired character
            // gets; the legacy default-guy path is kept byte-identical.
            auto g = args.team_level > 0
                ? std::make_unique<guy>(family)
                : std::make_unique<guy>();
            g->family = static_cast<char>(family);
            g->name = std::format("Player{}", i + 1);
            w->set_owned_myguy(std::move(g));
            if (args.team_level > 0) {
                // Level the guy up and recompute the walker's derived
                // stats (same sequence as the headless server's
                // create_team_walker).
                w->myguy->upgrade_to_level(
                    static_cast<short>(args.team_level));
                if (w->stats() != nullptr)
                    w->stats()->set_level(w->myguy->level);
                w->myguy->update_derived_stats(w);
            }
            crew.push_back(w);
        }
    }

    // Deploy the crew onto the authored team start markers, the way the
    // production spawn path (spawn_team_from_save) does. Without this the
    // crew stacks at the add_ob default position, which makes balance runs
    // meaningless. Falls back to a random teleport when markers run out.
    auto find_team_marker = [&world](int team) -> walker* {
        for (const auto& entity : world.oblist) {
            walker* const m = entity.get();
            if (m == nullptr || m->dead())
                continue;
            if (m->query_order() != Order::Special ||
                m->family() != FAMILY_RESERVED_TEAM)
                continue;
            if (team != -1 && m->team_num() != team)
                continue;
            return m;
        }
        return nullptr;
    };
    auto next_team_marker = [&find_team_marker]() -> walker* {
        if (walker* m = find_team_marker(0))
            return m;
        return find_team_marker(-1);
    };
    for (walker* w : crew) {
        if (walker* marker = next_team_marker()) {
            // set_floor MUST precede setxy (setxy re-buckets the
            // floor-keyed obmap).
            w->set_floor(marker->floor());
            w->setxy(marker->xpos(), marker->ypos());
            marker->set_dead(1);
        } else {
            w->teleport();
        }
    }
    while (walker* leftover = next_team_marker())
        leftover->set_dead(1);

    // Wire up sim pointers on all entities
    world.enemy_freeze = 0;
    world.end = 0;

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
        } else if (cmd == "census") {
            cmd_census(world);
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
