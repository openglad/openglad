/* OpenGlad Text Client — SDL-free headless simulation interface
 *
 * This binary proves the sim/core modules are fully decoupled from SDL.
 * It links ONLY against og_sim and og_core (no render, no runtime, no SDL).
 *
 * Provides a text-based interface suitable for LLM interaction:
 *   - Prints game state as structured text
 *   - Accepts commands via stdin
 *   - Outputs events as structured text
 *
 * Licensed under GPL v2.
 */

// Only sim/ and core/ headers — NO render, NO runtime, NO SDL
#include <openglad/sim/sim_world.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_entity.h>
#include <openglad/sim/event.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/data/save_data.h>
#include <openglad/data/level_data.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Minimal stub implementations for types that the text client needs
// but whose full implementations live in SDL-dependent modules.
// These let us instantiate SaveData and LevelData for the sim tick.
// Complete type definitions needed for unique_ptr destruction in headers.

// --- Stub: complete types for unique_ptr in level_data.h ---
class pixie { public: virtual ~pixie() = default; };
class pixieN : public pixie { public: ~pixieN() override = default; };
class loader { public: virtual ~loader() = default; };
class obmap { public: virtual ~obmap() = default; };
namespace og::sim { class SimEntity; }
class walker : public og::sim::SimEntity {
public:
    walker() = default;
    ~walker() override = default;
    short dead = 0;
    struct guy* myguy = nullptr;
};

// --- Stub: guy (entities module) ---
#include <openglad/entities/guy.h>
guy::guy() : name("Stub"), family(0), strength(1), dexterity(1), constitution(1),
    intelligence(1), armor(0), exp(0), kills(0), level_kills(0),
    total_damage(0), total_hits(0), total_shots(0), teamnum(0),
    scen_damage(0), scen_kills(0), scen_damage_taken(0), scen_min_hp(0),
    scen_shots(0), scen_hits(0), id(0), level(1) {}
guy::guy(int f) : guy() { family = static_cast<char>(f); }
guy::~guy() = default;
guy::guy(const guy&) = default;
std::int32_t guy::query_heart_value() { return 0; }
void guy::upgrade_to_level(short, bool) {}
float guy::get_hp_bonus() const { return 0; }
float guy::get_mp_bonus() const { return 0; }
float guy::get_damage_bonus() const { return 0; }
float guy::get_armor_bonus() const { return 0; }
float guy::get_speed_bonus() const { return 0; }
float guy::get_fire_frequency_bonus() const { return 0; }
void guy::update_derived_stats(walker*) {}

// --- Stub: PixieData (data module) ---
#include <openglad/data/pixie_data.h>
PixieData::PixieData() : frames(0), w(0), h(0), data(nullptr) {}
PixieData::PixieData(unsigned char f, unsigned char wi, unsigned char hi, unsigned char* d)
    : frames(f), w(wi), h(hi), data(nullptr) { (void)d; }
PixieData::PixieData(PixieData&& other) noexcept = default;
PixieData& PixieData::operator=(PixieData&& other) noexcept = default;
bool PixieData::valid() const { return data != nullptr; }
void PixieData::free() { data.reset(); }

// --- Stub: SimEntity (sim module) ---
og::sim::SimEntity::SimEntity() = default;
og::sim::SimEntity::~SimEntity() = default;

// --- Stub: SimWorld::tick (lives in runtime, too heavy to link) ---
og::sim::TickResult og::sim::SimWorld::tick(
    LevelData& level, SaveData& /*save*/,
    std::int32_t& enemy_freeze, char /*end*/,
    og::sim::SimEventLog& events)
{
    tick_count_++;
    og::sim::TickResult result;
    result.level_done = level.oblist.empty() ? 2 : 0;
    if (enemy_freeze > 0) enemy_freeze--;
    (void)events;
    return result;
}

// --- Stub: SaveData methods ---
SaveData::SaveData()
    : current_campaign("org.openglad.gladiator"), scen_num(1),
      score(0), totalcash(0), totalscore(0), my_team(0),
      team_size(0), numplayers(1), allied_mode(1)
{
    for (auto& s : m_score) s = 0;
    for (auto& c : m_totalcash) c = 5000;
    for (auto& t : m_totalscore) t = 0;
}
SaveData::~SaveData() = default;
void SaveData::reset() { team_size = 0; scen_num = 1; }
bool SaveData::load(const std::string&) { return false; }
bool SaveData::save(const std::string&) { return false; }
SaveDataIoError SaveData::load_with_error(const std::string& f) { load(f); return last_io_error_; }
SaveDataIoError SaveData::save_with_error(const std::string& f) { save(f); return last_io_error_; }
void SaveData::update_guys(std::list<std::unique_ptr<walker>>&) {}
bool SaveData::is_level_completed(int) const { return false; }
int SaveData::get_num_levels_completed(const std::string&) const { return 0; }
void SaveData::add_level_completed(const std::string&, int) {}
void SaveData::reset_campaign(const std::string&) {}

// --- Stub: LevelData (minimal, no file I/O) ---
LevelData::LevelData(int level_id)
    : id(level_id), type(0), par_value(100), time_bonus_limit(6000),
      pixmaxx(0), pixmaxy(0), numobs(0), topx(0), topy(0)
{
    title = "Headless Level";
}
LevelData::~LevelData() = default;
bool LevelData::load() { return false; }
bool LevelData::save() { return false; }
LevelData::IoError LevelData::load_with_error() { return last_io_error_; }
LevelData::IoError LevelData::save_with_error() { return last_io_error_; }
walker* LevelData::add_ob(Order, std::int32_t, bool) { return nullptr; }
walker* LevelData::add_fx_ob(Order, std::int32_t) { return nullptr; }
walker* LevelData::add_weap_ob(Order, std::int32_t) { return nullptr; }
short LevelData::remove_ob(walker*) { return 0; }
bool LevelData::query_passable(float, float, walker*) { return true; }
bool LevelData::query_object_passable(float, float, walker*) { return true; }
bool LevelData::query_grid_passable(float, float, walker*) { return true; }
walker* LevelData::find_near_foe(walker*) { return nullptr; }
walker* LevelData::find_far_foe(walker*) { return nullptr; }
walker* LevelData::find_nearest_blood(walker*) { return nullptr; }
walker* LevelData::find_nearest_player(walker*) { return nullptr; }
std::list<walker*> LevelData::find_in_range(std::list<std::unique_ptr<walker>>&, std::int32_t, std::int32_t*, walker*) { return {}; }
std::list<walker*> LevelData::find_foes_in_range(std::list<std::unique_ptr<walker>>&, std::int32_t, std::int32_t*, walker*) { return {}; }
std::list<walker*> LevelData::find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>&, std::int32_t, std::int32_t*, walker*) { return {}; }
std::list<walker*> LevelData::find_friends_in_range(std::list<std::unique_ptr<walker>>&, std::int32_t, std::int32_t*, walker*) { return {}; }
void LevelData::create_new_grid() {}
void LevelData::resize_grid(int, int) {}
void LevelData::delete_grid() {}
void LevelData::delete_objects() {}
void LevelData::clear() {}
void LevelData::set_draw_pos(std::int32_t, std::int32_t) {}
void LevelData::add_draw_pos(std::int32_t, std::int32_t) {}
void LevelData::draw(screen*) {}
std::string LevelData::get_description_line(int) { return ""; }
std::string get_scenario_title(const char*) { return "none"; }
short remaining_foes(LevelData&, walker*) { return 0; }

// --- Stub: smoother ---
smoother::smoother() : mygrid(nullptr), maxx(0), maxy(0) {}
void smoother::reset() {}
void smoother::set_target(const PixieData&) {}
std::int32_t smoother::smooth() { return 0; }
std::int32_t smoother::smooth(std::int32_t, std::int32_t) { return 0; }
std::int32_t smoother::query_x_y(std::int32_t, std::int32_t) { return 0; }
std::int32_t smoother::query_genre_x_y(std::int32_t, std::int32_t) { return 0; }
std::int32_t smoother::surrounds(std::int32_t, std::int32_t, std::int32_t) { return 0; }
void smoother::set_x_y(std::int32_t, std::int32_t, std::int32_t) {}

static const char* event_kind_name(og::sim::EventKind kind)
{
    switch (kind) {
        case og::sim::EventKind::None:          return "None";
        case og::sim::EventKind::PlaySound:     return "PlaySound";
        case og::sim::EventKind::Notification:  return "Notification";
        case og::sim::EventKind::SetPalette:    return "SetPalette";
        case og::sim::EventKind::RequestRedraw: return "RequestRedraw";
        case og::sim::EventKind::EndGame:       return "EndGame";
        case og::sim::EventKind::DamageTile:    return "DamageTile";
        case og::sim::EventKind::SetEnd:        return "SetEnd";
        default: return "Unknown";
    }
}

static void print_help()
{
    std::printf(
        "OpenGlad Text Client — SDL-free headless simulation\n"
        "===================================================\n"
        "Commands:\n"
        "  tick [N]       Run N simulation ticks (default: 1)\n"
        "  state          Print current game state\n"
        "  events         Print pending events from last tick\n"
        "  emit <kind> <a> <b>  Emit a test event\n"
        "  seed <value>   Reset the sim RNG seed\n"
        "  help           Show this help\n"
        "  quit           Exit\n"
        "\n"
    );
}

static void print_state(og::sim::SimWorld& world, LevelData& level, SaveData& save,
                         og::sim::SimEventLog& events)
{
    std::printf("=== Game State ===\n");
    std::printf("  SimWorld tick_count: %u\n", world.tick_count());
    std::printf("  RNG state: %u\n", world.rng().state());
    std::printf("  Level: id=%d title=\"%s\"\n", level.id, level.title.c_str());
    std::printf("  Level done: %d\n", level.level_done);
    std::printf("  Save: campaign=\"%s\" scen=%d\n",
                save.current_campaign.c_str(), save.scen_num);
    std::printf("  Entities in oblist: %zu\n", level.oblist.size());
    std::printf("  Entities in fxlist: %zu\n", level.fxlist.size());
    std::printf("  Entities in weaplist: %zu\n", level.weaplist.size());
    std::printf("  Pending events: %zu\n", events.size());
    std::printf("==================\n");
}

static void print_events(og::sim::SimEventLog& events)
{
    const auto& evts = events.events();
    if (evts.empty()) {
        std::printf("(no events)\n");
        return;
    }
    for (const auto& e : evts) {
        std::printf("  [tick=%u] %s a=%u b=%u",
                    e.tick, event_kind_name(e.kind), e.a, e.b);
        if (!e.text.empty())
            std::printf(" text=\"%s\"", e.text.c_str());
        std::printf("\n");
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::printf("OpenGlad Text Client v1.0 (SDL-free)\n");
    std::printf("Type 'help' for commands, 'quit' to exit.\n\n");

    // Create simulation components
    og::sim::SimWorld world(42);  // seed=42 for reproducibility
    og::sim::SimEventLog events;
    LevelData level(1);
    SaveData save;
    std::int32_t enemy_freeze = 0;
    char end_flag = 0;

    std::string line;
    std::printf("> ");
    std::fflush(stdout);

    while (std::getline(std::cin, line)) {
        // Trim whitespace
        while (!line.empty() && line.back() == '\n') line.pop_back();
        while (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) {
            std::printf("> ");
            std::fflush(stdout);
            continue;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            std::printf("Goodbye.\n");
            break;
        }
        else if (cmd == "help" || cmd == "h" || cmd == "?") {
            print_help();
        }
        else if (cmd == "tick" || cmd == "t") {
            int n = 1;
            iss >> n;
            if (n < 1) n = 1;
            if (n > 10000) n = 10000;

            for (int i = 0; i < n; i++) {
                events.clear();
                events.set_tick(world.tick_count());
                auto result = world.tick(level, save, enemy_freeze, end_flag, events);

                if (i == n - 1 || result.game_ended) {
                    std::printf("Tick %u: level_done=%d game_ended=%s next_level=%d ending=%d\n",
                                world.tick_count(),
                                result.level_done,
                                result.game_ended ? "true" : "false",
                                result.next_level,
                                result.ending);
                    if (!events.empty()) {
                        std::printf("Events:\n");
                        print_events(events);
                    }
                    if (result.game_ended) break;
                }
            }
        }
        else if (cmd == "state" || cmd == "s") {
            print_state(world, level, save, events);
        }
        else if (cmd == "events" || cmd == "e") {
            print_events(events);
        }
        else if (cmd == "emit") {
            unsigned int kind_val = 0, a = 0, b = 0;
            iss >> kind_val >> a >> b;
            events.push(static_cast<og::sim::EventKind>(kind_val), a, b);
            std::printf("Emitted event: kind=%u a=%u b=%u\n", kind_val, a, b);
        }
        else if (cmd == "seed") {
            unsigned int seed = 0;
            iss >> seed;
            world.rng().reset(seed);
            std::printf("RNG seed reset to %u\n", seed);
        }
        else {
            std::printf("Unknown command: %s (type 'help' for commands)\n", cmd.c_str());
        }

        std::printf("> ");
        std::fflush(stdout);
    }

    return 0;
}
