#include "parity_dump_state.h"
#include "parity_event_log.h"
#include "parity_scenario_table.h"

#include "gparser.h"
#include "input.h"
#include "io.h"
#include "screen.h"
#include "stats.h"
#include "view.h"
#include "walker.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

extern cfg_store cfg;
extern options* theprefs;

namespace {

constexpr std::uint32_t K_UP             = 1u << 0;
constexpr std::uint32_t K_UP_RIGHT       = 1u << 1;
constexpr std::uint32_t K_RIGHT          = 1u << 2;
constexpr std::uint32_t K_DOWN_RIGHT     = 1u << 3;
constexpr std::uint32_t K_DOWN           = 1u << 4;
constexpr std::uint32_t K_DOWN_LEFT      = 1u << 5;
constexpr std::uint32_t K_LEFT           = 1u << 6;
constexpr std::uint32_t K_UP_LEFT        = 1u << 7;
constexpr std::uint32_t K_FIRE           = 1u << 8;
constexpr std::uint32_t K_SPECIAL        = 1u << 9;
constexpr std::uint32_t K_SWITCH         = 1u << 10;
constexpr std::uint32_t K_SPECIAL_SWITCH = 1u << 11;
constexpr std::uint32_t K_SHIFT          = 1u << 13;

struct InputDriver
{
    walker*       control   = nullptr;
    std::uint32_t held_mask = 0;
    std::uint32_t prev_mask = 0;
};

constexpr unsigned char kParityScen99Fixture[] = {
    0x46, 0x53, 0x53, 0x09, 0x67, 0x72, 0x69, 0x64, 0x00, 0x00, 0x00, 0x00,
    0x43, 0x6f, 0x76, 0x65, 0x72, 0x61, 0x67, 0x65, 0x20, 0x4c, 0x65, 0x76,
    0x65, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x07, 0x00, 0xd2, 0x04, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x4f, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4d, 0x53, 0x54, 0x52, 0x4d, 0x53, 0x54, 0x52, 0x4d, 0x53, 0x04, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x46, 0x58, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4d, 0x53, 0x54,
    0x52, 0x4d, 0x53, 0x54, 0x52, 0x4d, 0x53, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x01, 0x01, 0x00, 0x57, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4d, 0x53, 0x54, 0x52, 0x4d, 0x53,
    0x54, 0x52, 0x4d, 0x53, 0x02, 0x06, 0x64, 0x65, 0x73, 0x63, 0x2d, 0x61,
    0x06, 0x64, 0x65, 0x73, 0x63, 0x2d, 0x62
};

void print_usage(std::FILE* out)
{
    std::fprintf(out,
        "usage: parity_dump_master --scenario <id> --out <path>\n"
        "       parity_dump_master <id> [--out <path>]\n"
        "       parity_dump_master --list\n");
}

int list_scenarios()
{
    for (const auto& s : og::parity::kScenarios)
    {
        if (s.is_branch_internal) continue;
        std::printf("%.*s\n", static_cast<int>(s.id.size()), s.id.data());
    }
    return 0;
}

const og::parity::ScenarioSpec* find_scenario(std::string_view id)
{
    for (const auto& s : og::parity::kScenarios)
    {
        if (s.id == id) return &s;
    }
    return nullptr;
}

int scenario_level_id(std::string_view scenario_file)
{
    const std::size_t slash = scenario_file.find_last_of('/');
    std::string_view base = (slash == std::string_view::npos)
                                ? scenario_file
                                : scenario_file.substr(slash + 1);

    constexpr std::string_view prefix = "scen";
    constexpr std::string_view suffix = ".fss";
    if (base.size() <= prefix.size() + suffix.size()) return -1;
    if (base.substr(0, prefix.size()) != prefix) return -1;
    if (base.substr(base.size() - suffix.size()) != suffix) return -1;

    int value = 0;
    for (char c : base.substr(prefix.size(),
                              base.size() - prefix.size() - suffix.size()))
    {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

bool read_binary_file(const std::filesystem::path& path,
                      std::vector<unsigned char>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    return !out.empty();
}

bool try_load_external_scen99(std::vector<unsigned char>& out)
{
    if (const char* fixture = std::getenv("OG_PARITY_SCEN99_FSS");
        fixture != nullptr && fixture[0] != '\0')
    {
        if (read_binary_file(fixture, out)) return true;
        std::fprintf(stderr,
                     "parity_dump_master: could not read OG_PARITY_SCEN99_FSS=%s; using embedded fixture\n",
                     fixture);
    }

    if (const char* root = std::getenv("OG_PARITY_WORKSPACE_ROOT");
        root != nullptr && root[0] != '\0')
    {
        const auto p = std::filesystem::path(root) / "temp/scen/scen99.fss";
        if (read_binary_file(p, out)) return true;
    }

    std::error_code ec;
    const auto cwd_fixture =
        std::filesystem::current_path(ec) / "temp/scen/scen99.fss";
    if (!ec && read_binary_file(cwd_fixture, out)) return true;

    return false;
}

bool install_parity_scen99_fixture(const std::string& home)
{
    std::vector<unsigned char> bytes;
    if (!try_load_external_scen99(bytes))
        bytes.assign(std::begin(kParityScen99Fixture),
                     std::end(kParityScen99Fixture));

    const auto scen_dir = std::filesystem::path(home) / ".openglad/scen";
    std::error_code ec;
    std::filesystem::create_directories(scen_dir, ec);
    if (ec)
    {
        std::fprintf(stderr, "parity_dump_master: could not create %s: %s\n",
                     scen_dir.string().c_str(), ec.message().c_str());
        return false;
    }

    const auto target = scen_dir / "scen99.fss";
    std::ofstream f(target, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        std::fprintf(stderr, "parity_dump_master: could not write %s\n",
                     target.string().c_str());
        return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

bool copy_rwops(SDL_RWops* in, SDL_RWops* out)
{
    char buffer[4096];
    for (;;)
    {
        const std::size_t got = SDL_RWread(in, buffer, 1, sizeof(buffer));
        if (got == 0) break;
        if (SDL_RWwrite(out, buffer, 1, got) != got) return false;
    }
    return true;
}

bool install_parity_grid_fixture()
{
    create_dir(get_user_path() + "pix");

    SDL_RWops* in = open_read_file("pix/", "scen0001.pix");
    if (in == nullptr)
        in = open_read_file("pix/", "scen1.pix");
    if (in == nullptr)
    {
        std::fprintf(stderr,
                     "parity_dump_master: could not find scen0001.pix for grid fixture\n");
        return false;
    }

    SDL_RWops* out = open_write_file("pix/", "grid.pix");
    if (out == nullptr)
    {
        SDL_RWclose(in);
        std::fprintf(stderr,
                     "parity_dump_master: could not write pix/grid.pix fixture\n");
        return false;
    }

    const bool ok = copy_rwops(in, out);
    SDL_RWclose(out);
    SDL_RWclose(in);
    if (!ok)
    {
        std::fprintf(stderr,
                     "parity_dump_master: failed while writing pix/grid.pix fixture\n");
    }
    return ok;
}

void move_to_front(std::list<walker*>& list, walker* w)
{
    if (w == nullptr) return;
    list.remove(w);
    list.push_front(w);
}

walker* add_spawn(screen& game, const og::parity::SpawnSpec& s)
{
    walker* w = game.level_data.add_ob(static_cast<char>(s.order),
                                       static_cast<char>(s.family),
                                       /*atstart=*/true);
    if (w == nullptr) return nullptr;

    if (s.order == ORDER_WEAPON)
        move_to_front(game.level_data.weaplist, w);
    else
        move_to_front(game.level_data.oblist, w);

    w->setxy(static_cast<short>(s.x), static_cast<short>(s.y));
    w->team_num      = s.team;
    w->real_team_num = s.team;
    if (s.default_weapon != 0) w->default_weapon = s.default_weapon;
    if (s.current_weapon != 0) w->current_weapon = s.current_weapon;
    if (w->stats != nullptr)
    {
        if (s.stats_level != 0)
            w->stats->level = static_cast<unsigned short>(s.stats_level);
        if (s.magicpoints != 0)
            w->stats->magicpoints = static_cast<float>(s.magicpoints);
    }
    if (s.precompleted_level != 0)
        game.save_data.add_level_completed(game.save_data.current_campaign,
                                           s.precompleted_level);
    return w;
}

void apply_post_load_spawns(screen& game, const og::parity::ScenarioSpec& spec)
{
    for (std::size_t i = 0; i < spec.spawn_count; ++i)
        add_spawn(game, spec.spawns[i]);
}

walker* find_player_walker(screen& game, std::uint8_t player_team)
{
    for (walker* w : game.level_data.oblist)
    {
        if (w && !w->dead && w->query_order() == ORDER_LIVING &&
            w->team_num == player_team)
            return w;
    }
    return nullptr;
}

void claim_control(screen& game, InputDriver& driver,
                   const og::parity::ScenarioSpec& spec)
{
    if (driver.control == nullptr || driver.control->dead)
        driver.control = find_player_walker(game, spec.player_team);
    if (driver.control == nullptr) return;
    if (driver.control->user == -1)
    {
        driver.control->set_act_type(ACT_CONTROL);
        driver.control->user = 0;
        if (driver.control->stats != nullptr)
            driver.control->stats->clear_command();
    }
    if (game.viewob[0] != nullptr)
    {
        game.viewob[0]->control = driver.control;
        game.viewob[0]->my_team = spec.player_team;
    }
    game.save_data.my_team = spec.player_team;
    game.control_hp = driver.control->stats ? driver.control->stats->hitpoints : 0;
}

void cycle_next_character(screen& game, InputDriver& driver,
                          const og::parity::ScenarioSpec& spec)
{
    walker* old = driver.control;
    if (old == nullptr) return;
    if (old->user == 0)
    {
        old->restore_act_type();
        old->user = -1;
    }

    auto& list = game.level_data.oblist;
    auto mine = std::find(list.begin(), list.end(), old);
    walker* next = nullptr;
    auto accept = [&](walker* w) {
        return w && w->query_order() == ORDER_LIVING &&
               w->is_friendly(old) &&
               w->team_num == spec.player_team &&
               w->real_team_num == 255 &&
               w->user == -1;
    };

    if (mine != list.end())
    {
        for (auto it = std::next(mine); it != list.end(); ++it)
        {
            if (accept(*it)) { next = *it; break; }
        }
        if (next == nullptr)
        {
            for (auto it = list.begin(); it != mine; ++it)
            {
                if (accept(*it)) { next = *it; break; }
            }
        }
    }
    if (next == nullptr) next = old;
    driver.control = next;
    claim_control(game, driver, spec);
}

void cycle_special(screen& game, walker* control)
{
    if (control == nullptr || control->stats == nullptr) return;
    control->current_special++;
    if (control->current_special > (NUM_SPECIALS - 1) ||
        std::strcmp(game.special_name[static_cast<int>(control->query_family())]
                                     [static_cast<int>(control->current_special)],
                    "NONE") == 0 ||
        (((control->current_special - 1) * 3 + 1) > control->stats->level))
        control->current_special = 1;
}

bool held(std::uint32_t mask, std::uint32_t bit)
{
    return (mask & bit) != 0;
}

void apply_inputs_at_tick(screen& game,
                          const og::parity::ScenarioSpec& spec,
                          std::uint32_t tick,
                          InputDriver& driver)
{
    if (spec.inputs != nullptr)
    {
        for (std::size_t i = 0; i < spec.input_count; ++i)
        {
            if (spec.inputs[i].tick == tick && spec.inputs[i].player_id == 0)
                driver.held_mask = spec.inputs[i].key_mask;
        }
    }

    claim_control(game, driver, spec);
    walker* control = driver.control;
    if (control == nullptr || control->dead)
    {
        driver.prev_mask = driver.held_mask;
        return;
    }

    const std::uint32_t pressed = driver.held_mask & ~driver.prev_mask;
    if (held(pressed, K_SWITCH))
    {
        cycle_next_character(game, driver, spec);
        control = driver.control;
    }
    if (control == nullptr || control->dead)
    {
        driver.prev_mask = driver.held_mask;
        return;
    }

    control->shifter_down = held(driver.held_mask, K_SHIFT) ? 1 : 0;

    if (held(pressed, K_SPECIAL_SWITCH))
        cycle_special(game, control);

    if (control->stats != nullptr && control->stats->commands.empty())
    {
        if (held(driver.held_mask, K_SPECIAL))
            control->special();

        int walkx = 0;
        int walky = 0;
        if (held(driver.held_mask, K_UP) || held(driver.held_mask, K_UP_LEFT) ||
            held(driver.held_mask, K_UP_RIGHT))
            walky = -1;
        else if (held(driver.held_mask, K_DOWN) ||
                 held(driver.held_mask, K_DOWN_LEFT) ||
                 held(driver.held_mask, K_DOWN_RIGHT))
            walky = 1;

        if (held(driver.held_mask, K_LEFT) || held(driver.held_mask, K_UP_LEFT) ||
            held(driver.held_mask, K_DOWN_LEFT))
            walkx = -1;
        else if (held(driver.held_mask, K_RIGHT) ||
                 held(driver.held_mask, K_DOWN_RIGHT) ||
                 held(driver.held_mask, K_UP_RIGHT))
            walkx = 1;

        if (walkx != 0 || walky != 0)
            control->walkstep(static_cast<float>(walkx), static_cast<float>(walky));

        if (held(driver.held_mask, K_FIRE))
            control->init_fire();
    }

    driver.prev_mask = driver.held_mask;
}

void record_score_changes(const Uint32 before[4], const Uint32 after[4])
{
    for (std::uint32_t team = 0; team < 4; ++team)
    {
        if (before[team] == after[team]) continue;
        const std::uint32_t delta = after[team] - before[team];
        og::parity::record_event(og::parity::kEventScoreChange,
                                 team, delta, {});
    }
}

void ensure_view_team(screen& game, const og::parity::ScenarioSpec& spec)
{
    game.save_data.my_team = spec.player_team;
    if (game.viewob[0] != nullptr)
        game.viewob[0]->my_team = spec.player_team;
}

int run_scenario(const og::parity::ScenarioSpec& spec,
                 int level_id,
                 const std::string& out_path)
{
    og::parity::seed_rng_observable(spec.rng_seed);
    std::srand(spec.rng_seed);

    std::vector<og::parity::WeaponTrackSample> tracks;
    std::string json;

    {
        screen game(1);
        game.save_data.reset();
        game.save_data.current_campaign = "org.openglad.gladiator";
        game.save_data.scen_num = level_id;
        game.save_data.numplayers = 1;
        game.save_data.my_team = spec.player_team;
        game.level_data.id = level_id;
        if (!game.level_data.load())
        {
            std::fprintf(stderr, "parity_dump_master: failed to load level %d\n",
                         level_id);
            return 1;
        }
        game.ready_for_battle(1);
        ensure_view_team(game, spec);

        if (spec.fresh_arena)
            game.level_data.delete_objects();

        apply_post_load_spawns(game, spec);
        ensure_view_team(game, spec);

        std::srand(spec.rng_seed);
        og::parity::reset_event_log();
        og::parity::seed_rng_observable(spec.rng_seed);

        std::map<std::int32_t, std::int32_t> next_seq_per_family;
        std::map<std::int32_t, std::int32_t> fx_next_seq_per_family;
        std::unordered_map<const walker*, std::int32_t> seq_by_ptr;
        std::unordered_map<const walker*, std::uint32_t> last_tick_by_ptr;
        InputDriver input_driver;

        auto sample_weapon_tracks = [&](std::uint32_t cur_tick) {
            auto sample_one = [&](walker* w,
                                  std::map<std::int32_t, std::int32_t>& next_seq,
                                  std::int32_t order) {
                if (w == nullptr || w->dead != 0) return;
                const auto fam = static_cast<std::int32_t>(w->query_family());
                auto it = seq_by_ptr.find(w);
                std::int32_t seq = 0;
                const bool contiguous =
                    it != seq_by_ptr.end() && last_tick_by_ptr[w] + 1 == cur_tick;
                if (!contiguous)
                {
                    seq = next_seq[fam]++;
                    seq_by_ptr[w] = seq;
                }
                else
                {
                    seq = it->second;
                }
                last_tick_by_ptr[w] = cur_tick;
                og::parity::WeaponTrackSample s;
                s.tick = cur_tick;
                s.family = og::parity::family_symbol_by_order(order, fam);
                s.seq = seq;
                s.xpos = static_cast<std::int32_t>(w->xpos);
                s.ypos = static_cast<std::int32_t>(w->ypos);
                s.lifetime = static_cast<std::int32_t>(w->lifetime);
                tracks.push_back(std::move(s));
            };

            for (walker* w : game.level_data.weaplist)
            {
                if (w && w->query_order() == ORDER_WEAPON)
                    sample_one(w, next_seq_per_family, ORDER_WEAPON);
            }
            for (walker* w : game.level_data.oblist)
            {
                if (w && w->query_order() == ORDER_FX)
                    sample_one(w, fx_next_seq_per_family, ORDER_FX);
            }
        };

        for (std::uint32_t t = 0; t < spec.tick_budget; ++t)
        {
            og::parity::set_event_tick(t);
            apply_inputs_at_tick(game, spec, t, input_driver);

            Uint32 before[4] = {
                game.save_data.m_score[0], game.save_data.m_score[1],
                game.save_data.m_score[2], game.save_data.m_score[3],
            };
            game.act();
            Uint32 after[4] = {
                game.save_data.m_score[0], game.save_data.m_score[1],
                game.save_data.m_score[2], game.save_data.m_score[3],
            };
            record_score_changes(before, after);
            sample_weapon_tracks(t + 1);
        }

        auto dump = og::parity::capture_state_dump(game, spec.tick_budget,
                                                   std::move(tracks));
        json = og::parity::canonical_serialize(dump);
    }

    if (out_path == "-")
    {
        std::fwrite(json.data(), 1, json.size(), stdout);
    }
    else
    {
        std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            std::fprintf(stderr, "parity_dump_master: cannot open output: %s\n",
                         out_path.c_str());
            return 2;
        }
        f.write(json.data(), static_cast<std::streamsize>(json.size()));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::string scenario_id;
    std::string out_path = "-";

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--list")
            return list_scenarios();
        if (arg == "--scenario")
        {
            if (i + 1 >= argc)
            {
                print_usage(stderr);
                return 2;
            }
            scenario_id = argv[++i];
        }
        else if (arg == "--out")
        {
            if (i + 1 >= argc)
            {
                print_usage(stderr);
                return 2;
            }
            out_path = argv[++i];
        }
        else if (arg == "-h" || arg == "--help")
        {
            print_usage(stdout);
            return 0;
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            print_usage(stderr);
            return 2;
        }
        else
        {
            scenario_id = arg;
        }
    }

    if (scenario_id.empty())
    {
        print_usage(stderr);
        return 2;
    }

    const og::parity::ScenarioSpec* spec = find_scenario(scenario_id);
    if (spec == nullptr)
    {
        std::fprintf(stderr, "parity_dump_master: unknown scenario: %s\n",
                     scenario_id.c_str());
        return 2;
    }

    const int level_id = scenario_level_id(spec->scenario_file);
    if (level_id <= 0)
    {
        std::fprintf(stderr, "parity_dump_master: bad scenario file: %.*s\n",
                     static_cast<int>(spec->scenario_file.size()),
                     spec->scenario_file.data());
        return 2;
    }

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    char* base_path_raw = SDL_GetBasePath();
    std::string home = std::string(base_path_raw ? base_path_raw : "") +
                       "parity-home";
    SDL_free(base_path_raw);
    std::filesystem::create_directories(home);
    SDL_setenv("HOME", home.c_str(), 1);
    if (!install_parity_scen99_fixture(home))
        return 2;

    init_logging();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0)
    {
        std::fprintf(stderr, "parity_dump_master: SDL_Init failed: %s\n",
                     SDL_GetError());
        return 2;
    }

    io_init(argc, argv);
    if (!install_parity_grid_fixture())
    {
        io_exit();
        SDL_Quit();
        return 2;
    }
    cfg.load_settings();
    cfg.apply_setting("sound", "sound", "off");
    cfg.apply_setting("graphics", "fullscreen", "off");
    cfg.apply_setting("graphics", "render", "normal");
    cfg.commandline(argc, argv);
    theprefs = new options;
    init_input();

    const int rc = run_scenario(*spec, level_id, out_path);

    delete theprefs;
    theprefs = nullptr;
    io_exit();
    SDL_Quit();
    return rc;
}
