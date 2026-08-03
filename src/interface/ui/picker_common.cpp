/* Shared picker business logic implementation.
 *
 * Extracted from picker.cpp, picker_team_build.cpp, and text_picker.cpp
 * so both SDL and text clients call the same code.
 */

#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/util.h>
#include <openglad/core/scale_mode.h>
#include <optional>
#include <openglad/core/tower_constants.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/irandom.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <set>

// Defined in entities/guy.cpp
std::uint32_t calculate_exp(std::int32_t level);

// Legacy global used by entity code (living.cpp, walker.cpp) via
// extern std::int32_t difficulty_level[].  Centralised here so both
// the SDL and headless clients share a single definition.
// Difficulty settings .. in percent, so 100 == normal
extern const std::int32_t difficulty_level[DIFFICULTY_SETTINGS] = {50, 100, 200};

namespace og::ui {

// --- Constants ---

const char* const kDifficultyNames[DIFFICULTY_SETTINGS] = {
    "Skirmish",
    "Battle",
    "Slaughter",
};

// --- Family display helpers ---

const char* family_display_name(int family)
{
    const auto* fd = get_family_descriptor(family);
    if (fd)
        return fd->name;
    return "BEAST";
}

const char* family_short_name(short family)
{
    const auto* fd = get_family_descriptor(family);
    if (fd)
        return fd->short_name ? fd->short_name : fd->name;
    return "BEAST";
}

// --- Cost calculations ---

int family_hiring_base_cost(int family)
{
    const FamilyDescriptor* fd = get_family_descriptor(family);
    return fd ? static_cast<int>(fd->hiring_cost) : 0;
}

std::uint32_t calculate_hire_cost(const guy& recruit)
{
    const auto* fd = get_family_descriptor(recruit.family);
    if (!fd)
        return 0;

    std::int32_t temp = fd->hiring_cost;

    // Stat upgrade costs above base (clamped to 0 if below base)
    auto stat_delta = [](short stat, std::int32_t base) -> std::int32_t {
        std::int32_t d = static_cast<std::int32_t>(stat) - base;
        return d > 0 ? d : 0;
    };

    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.strength, fd->base_stats[StatAxis::Strength]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Strength]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.dexterity, fd->base_stats[StatAxis::Dexterity]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Dexterity]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.constitution, fd->base_stats[StatAxis::Constitution]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Constitution]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.intelligence, fd->base_stats[StatAxis::Intelligence]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Intelligence]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.armor, fd->base_stats[StatAxis::Armor]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Armor]);

    // Level cost
    short effective_level = std::max(recruit.level, static_cast<short>(fd->base_stats[StatAxis::Level]));
    std::int32_t exp_cost = static_cast<std::int32_t>(calculate_exp(effective_level));
    if (exp_cost < 0) // overflow
        return 0;
    temp += exp_cost;

    if (temp < 0)
        return 0;

    return static_cast<std::uint32_t>(temp);
}

std::uint32_t calculate_train_cost(const guy& current, const guy& original)
{
    const auto* fd = get_family_descriptor(current.family);
    if (!fd)
        return 0;

    std::int32_t temp = 0;

    // Use effective stats: current stats clamped to not go below original
    auto effective = [](short cur, short orig) -> short {
        return cur > orig ? cur : orig;
    };

    short eff_str = effective(current.strength, original.strength);
    short eff_dex = effective(current.dexterity, original.dexterity);
    short eff_con = effective(current.constitution, original.constitution);
    short eff_int = effective(current.intelligence, original.intelligence);
    short eff_arm = effective(current.armor, original.armor);
    short eff_lvl = std::max(current.level, original.level);

    // Level XP cost
    if (calculate_exp(eff_lvl) > original.exp)
        temp += static_cast<std::int32_t>(calculate_exp(eff_lvl) - original.exp);

    // Stat costs only if not upgrading level
    if (eff_lvl <= original.level) {
        auto stat_cost = [&](short stat, short old_stat, int idx) -> std::int32_t {
            std::int32_t base = fd->base_stats[idx];
            std::int32_t new_delta = std::max(static_cast<std::int32_t>(stat) - base,
                                              std::int32_t{0});
            std::int32_t old_delta = std::max(static_cast<std::int32_t>(old_stat) - base,
                                              std::int32_t{0});
            std::int32_t new_cost = static_cast<std::int32_t>(
                std::pow(new_delta, kStatCostExponent) * fd->stat_costs[idx]);
            std::int32_t old_cost = static_cast<std::int32_t>(
                std::pow(old_delta, kStatCostExponent) * fd->stat_costs[idx]);
            return new_cost - old_cost;
        };

        temp += stat_cost(eff_str, original.strength, 0);
        temp += stat_cost(eff_dex, original.dexterity, 1);
        temp += stat_cost(eff_con, original.constitution, 2);
        temp += stat_cost(eff_int, original.intelligence, 3);
        temp += stat_cost(eff_arm, original.armor, 4);
    }

    if (temp < 0)
        return 0;

    return static_cast<std::uint32_t>(temp);
}

std::uint32_t calculate_sell_value(const guy& member)
{
    // query_heart_value() is the death-drop valuation. It is historically
    // non-const, so query a copy while keeping this helper pure.
    guy valued(member);
    const std::int32_t heart_value = valued.query_heart_value();
    if (heart_value <= 0)
        return 0;

    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(heart_value) * 3u / 4u);
}

// --- Name generation ---

namespace {

bool has_name_in_save(const char* name, const SaveData& save)
{
    for (int i = 0; i < save.team_size && i < MAX_TEAM_SIZE; i++) {
        if (save.team_list[i] && save.team_list[i]->name == name)
            return true;
    }
    return false;
}

} // anonymous namespace

// Recruit names come from the family descriptor's pool (`names` in the
// declaration), so a class pack's family names its own recruits with no
// engine change. Families that deliberately share a pool — archmage with
// mage, the orc captain with the orcs, all three slimes — do so by shipping
// the same list, which is how the switch this replaced behaved.
//
// A family with no pool of its own borrows the soldier pool, the fallback the
// old `default:` branch used; the core pack leaves the three unhireable
// families (golem, giant skeleton, tower) poolless on purpose.
const char* get_random_name(unsigned char family)
{
    const FamilyDescriptor* fd = get_family_descriptor(family);
    if (fd == nullptr || fd->name_pool == nullptr || fd->name_pool_size <= 0)
        fd = get_family_descriptor(FAMILY_SOLDIER);
    if (fd == nullptr || fd->name_pool == nullptr || fd->name_pool_size <= 0)
        return "Nameless";  // no pool at all: not reachable with a core pack
    return fd->name_pool[std::rand() % fd->name_pool_size];
}

std::string get_unique_name(unsigned char family, const SaveData& save)
{
    const char* result = get_random_name(family);

    // Try a few times to get a unique name
    int i = 0;
    while (has_name_in_save(result, save) && i < 10) {
        result = get_random_name(family);
        i++;
    }

    // Still a duplicate? Append a number.
    if (has_name_in_save(result, save)) {
        i = 2;
        std::string numbered;
        do {
            numbered = std::format("{}{}", result, i);
            i++;
        } while (has_name_in_save(numbered.c_str(), save));
        return numbered;
    }

    return std::string(result);
}

// --- Company name generation (design §2.2) ---

namespace {

// Word banks for "<ADJ> <NOUN> <GROUP>". Budget contract (asserted by the
// unit pins over company_name_banks()): adjectives <= 5 chars, nouns <= 6,
// groups <= 5, so the longest combination is 5 + 1 + 6 + 1 + 5 = 18
// = kCompanyNameMaxLen — the generator can never overflow and never
// truncates (the cap is load-bearing: §2.2 promises the name fits every
// later surface untrimmed).
constexpr const char* kCompanyAdjectives[] = {
    "IRON", "GREY", "BLACK", "WHITE", "ASHEN", "AMBER", "IVORY", "STONE",
    "THORN", "EMBER", "RIVER", "NORTH", "SABLE", "FROST", "STORM", "GOLD",
};

constexpr const char* kCompanyNouns[] = {
    "KETTLE", "RAVEN", "FALCON", "WOLF", "LION", "DRAGON", "HAMMER",
    "SHIELD", "DAGGER", "BANNER", "VIPER", "BADGER", "WYVERN", "LANCE",
    "CROW", "ANVIL",
};

constexpr const char* kCompanyGroups[] = {
    "BAND", "CREW", "HOST", "GUILD", "ORDER", "LODGE", "TROOP", "WATCH",
    "PACK", "CADRE",
};

const char* pick_bank_word(IRandom& rng, std::span<const char* const> bank)
{
    return bank[rng.next(static_cast<std::uint32_t>(bank.size()))];
}

} // namespace

CompanyNameBanks company_name_banks()
{
    return CompanyNameBanks{
        std::span<const char* const>(kCompanyAdjectives),
        std::span<const char* const>(kCompanyNouns),
        std::span<const char* const>(kCompanyGroups),
    };
}

std::string generate_company_name(IRandom& rng)
{
    // Exactly three draws, adjective->noun->group, so the output for a given
    // rng state is pinned (determinism is part of the API contract).
    const char* adjective = pick_bank_word(rng, company_name_banks().adjectives);
    const char* noun = pick_bank_word(rng, company_name_banks().nouns);
    const char* group = pick_bank_word(rng, company_name_banks().groups);
    return std::format("{} {} {}", adjective, noun, group);
}

// --- Team queries ---

int count_family_members(int family, const SaveData& save)
{
    int counter = 0;
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (save.team_list[i] && save.team_list[i]->family == family)
            counter++;
    }
    return counter;
}

// --- Team operations ---

int add_recruit_to_team(SaveData& save, std::unique_ptr<guy> recruit, int team_num)
{
    if (save.team_size >= MAX_TEAM_SIZE)
        return -1;

    recruit->teamnum = static_cast<short>(team_num);

    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        // found an empty slot
        if (!save.team_list[i]) {
            save.team_list[i] = std::move(recruit);
            save.team_size++;
            return i;
        }
    }

    return -1;
}

std::unique_ptr<guy> create_recruit(int family, int team_num, const SaveData& save)
{
    auto recruit = std::make_unique<guy>(family);
    recruit->teamnum = static_cast<short>(team_num);
    recruit->name = get_unique_name(static_cast<unsigned char>(family), save);
    return recruit;
}

void reset_for_new_game(SaveData& save)
{
    save.reset();
    save.totalcash = kNewGameStartingGold;
}

void ensure_team_populated(SaveData& save, const std::vector<int>& families, int team_num)
{
    if (save.team_size > 0)
        return;

    for (size_t i = 0; i < families.size() && save.team_size < MAX_TEAM_SIZE; ++i) {
        auto recruit = create_recruit(families[i], team_num, save);
        add_recruit_to_team(save, std::move(recruit), team_num);
    }

    // Fallback: if families was empty or all failed, add a soldier.
    if (save.team_size == 0) {
        auto recruit = create_recruit(FAMILY_SOLDIER, team_num, save);
        add_recruit_to_team(save, std::move(recruit), team_num);
    }
}

// --- Derived stats ---

DerivedStats compute_derived_stats(const guy& g,
    float base_hp, float base_damage, float base_stepsize, float base_fire_freq)
{
    DerivedStats ds;
    ds.hp = std::ceil(base_hp + g.get_hp_bonus());
    ds.mp = std::ceil(g.get_mp_bonus());
    ds.atk = base_damage + g.get_damage_bonus();
    ds.def = g.get_armor_bonus();
    ds.spd = base_stepsize + g.get_speed_bonus();
    // The 10.0f/fire_frequency is somewhat arbitrary, but it makes for good comparison info.
    float freq = base_fire_freq - g.get_fire_frequency_bonus();
    if (freq < 1.0f)
        freq = 1.0f;
    ds.atk_spd = 10.0f / freq;
    return ds;
}

// --- Difficulty ---

int cycle_difficulty(int current)
{
    return (current + 1) % DIFFICULTY_SETTINGS;
}

int difficulty_percent(int difficulty)
{
    const int normalized =
        ((difficulty % DIFFICULTY_SETTINGS) + DIFFICULTY_SETTINGS) % DIFFICULTY_SETTINGS;
    return static_cast<int>(difficulty_level[normalized]);
}

// --- Allied mode ---

void toggle_allied_mode(SaveData& save)
{
    save.allied_mode = static_cast<short>((save.allied_mode + 1) % 2);
}

bool is_allied_mode(const SaveData& save)
{
    return save.allied_mode != 0;
}

// --- CTF match settings ---

void cycle_ctf_team_count(SaveData& save)
{
    // Auto (0 = every team the map authors) -> 2 -> 3 -> 4 -> Auto.
    if (save.ctf_team_count <= 0)
        save.ctf_team_count = 2;
    else if (save.ctf_team_count >= 4)
        save.ctf_team_count = 0;
    else
        save.ctf_team_count = static_cast<short>(save.ctf_team_count + 1);
}

void cycle_ctf_capture_limit(SaveData& save)
{
    switch (save.ctf_capture_limit)
    {
        case 0: save.ctf_capture_limit = 1; break;
        case 1: save.ctf_capture_limit = 3; break;
        case 3: save.ctf_capture_limit = 5; break;
        case 5: save.ctf_capture_limit = 10; break;
        default: save.ctf_capture_limit = 0; break;
    }
}

bool is_ctf_campaign(const SaveData& save)
{
    return save.current_campaign == og::kCtfCampaignId;
}

std::uint8_t ctf_authored_team_mask_for_loaded_level(
    const SaveData& save,
    const GameWorld& world,
    std::string_view mounted_campaign)
{
    if (!is_ctf_campaign(save) ||
        mounted_campaign != save.current_campaign ||
        world.id != save.scen_num ||
        (world.type & GameWorld::TYPE_CTF) == 0)
    {
        return 0;
    }
    return og::sim::ctf_authored_flag_team_mask(world);
}

void toggle_ctf_scenario_troops(SaveData& save)
{
    save.ctf_strip_scenario_troops =
        static_cast<short>(save.ctf_strip_scenario_troops != 0 ? 0 : 1);
}

// --- Difficulty submenu match rules ---

void cycle_respawn_mode(SaveData& save)
{
    switch (save.respawn_mode)
    {
        case og::sim::kRespawnModeOff:
            save.respawn_mode = og::sim::kRespawnModeHeroes;
            break;
        case og::sim::kRespawnModeHeroes:
            save.respawn_mode = og::sim::kRespawnModeEveryone;
            break;
        case og::sim::kRespawnModeEveryone:
            save.respawn_mode = og::sim::kRespawnModeTeamOneHeroes;
            break;
        case og::sim::kRespawnModeTeamOneHeroes:
        default:
            save.respawn_mode = og::sim::kRespawnModeOff;
            break;
    }
}

void cycle_respawn_delay(SaveData& save)
{
    switch (save.ctf_respawn_ticks)
    {
        case 0: save.ctf_respawn_ticks = 60; break;
        case 60: save.ctf_respawn_ticks = 360; break;
        default: save.ctf_respawn_ticks = 0; break;
    }
}

void toggle_permadeath(SaveData& save)
{
    save.keep_fallen_heroes =
        static_cast<short>(save.keep_fallen_heroes != 0 ? 0 : 1);
}

void cycle_generator_rate(SaveData& save)
{
    switch (save.generator_rate)
    {
        case 0: save.generator_rate = 50; break;
        case 50: save.generator_rate = 200; break;
        default: save.generator_rate = 0; break;
    }
}

// --- GRAPHICS FX depth selector (cfg effects/depth_fx) ---

// Out-of-set values (including the empty string an absent key reads as)
// count as the default, fog — the same rule depth_fx_mode_from_setting
// applies in the renderer, so the label always names what is drawn.
static std::string normalize_depth_fx_value(const std::string& value)
{
    if (value == "off" || value == "tint" || value == "haze" || value == "mist")
        return value;
    return "fog";
}

std::string cycle_depth_fx(const std::string& current)
{
    const std::string value = normalize_depth_fx_value(current);
    if (value == "fog")
        return "haze";
    if (value == "haze")
        return "mist";
    if (value == "mist")
        return "tint";
    if (value == "tint")
        return "off";
    return "fog"; // off wraps back to the default
}

std::string format_depth_fx_label(const std::string& value)
{
    const std::string v = normalize_depth_fx_value(value);
    if (v == "off")
        return "Depth: Off";
    if (v == "tint")
        return "Depth: Tint";
    if (v == "haze")
        return "Depth: Haze";
    if (v == "mist")
        return "Depth: Mist";
    return "Depth: Fog";
}

bool depth_fx_is_active(const std::string& value)
{
    return normalize_depth_fx_value(value) != "off";
}

// --- DISPLAY zoom and smoothing selectors ---

// Zoom selector (cfg graphics/zoom): each click zooms out one step and wraps
// at the deepest step that fits the current window's resource budget.
std::string cycle_zoom(const std::string& current, int minimum_steps)
{
    const int steps = og::parse_zoom_steps(current);
    const int minimum = std::clamp(minimum_steps, 1, og::kZoomStepsMax);
    const int next = steps <= minimum ? og::kZoomStepsMax : steps - 1;
    return next == og::kZoomStepsMax ? std::string("1.0")
                                     : "0." + std::to_string(next);
}

std::string format_zoom_label(const std::string& value)
{
    const int steps = og::parse_zoom_steps(value);
    if (steps == og::kZoomStepsMax)
        return "Zoom: 1.0x";
    return "Zoom: 0." + std::to_string(steps) + "x";
}

// Smoothing selector (cfg graphics/smoothing): the world-canvas-only present
// filter. off -> sai -> eagle -> off.
std::string effective_smoothing_setting(const std::string& value,
                                        const std::string& legacy_render)
{
    if (value.empty() && (legacy_render == "sai" || legacy_render == "eagle"))
        return legacy_render;
    return value;
}

std::string cycle_smoothing(const std::string& current)
{
    const og::WorldScaleMode mode = og::parse_smoothing_setting(current);
    if (mode == og::WorldScaleMode::Sai)
        return "eagle";
    if (mode == og::WorldScaleMode::Eagle)
        return "off";
    return "sai";
}

std::string format_smoothing_label(const std::string& value, bool supported)
{
    switch (og::parse_smoothing_setting(value))
    {
    case og::WorldScaleMode::Sai:
        return supported ? "Smooth: SAI" : "Smooth: SAI N/A";
    case og::WorldScaleMode::Eagle:
        return supported ? "Smooth: Eagle" : "Smooth: Eagle N/A";
    default:
        return "Smooth: Off";
    }
}

DisplayMode parse_display_mode(const std::string& value)
{
    // Legacy boolean cfg: "on" meant the borderless desktop fullscreen that
    // SDL_WINDOW_FULLSCREEN(_DESKTOP) provided.
    if (value == "on" || value == "borderless")
        return DisplayMode::Borderless;
    if (value == "exclusive" || value == "fullscreen")
        return DisplayMode::Exclusive;
    return DisplayMode::Windowed; // "off", absent, or anything unrecognized
}

std::string display_mode_cfg_value(DisplayMode mode)
{
    switch (mode)
    {
    case DisplayMode::Borderless:
        return "borderless";
    case DisplayMode::Exclusive:
        return "exclusive";
    case DisplayMode::Windowed:
    default:
        return "off";
    }
}

DisplayMode next_display_mode(DisplayMode mode)
{
    switch (mode)
    {
    case DisplayMode::Windowed:
        return DisplayMode::Borderless;
    case DisplayMode::Borderless:
        return DisplayMode::Exclusive;
    case DisplayMode::Exclusive:
    default:
        return DisplayMode::Windowed;
    }
}

std::string format_display_mode_label(const std::string& value)
{
    switch (parse_display_mode(value))
    {
    case DisplayMode::Borderless:
        return "Mode: Borderless";
    case DisplayMode::Exclusive:
        return "Mode: Fullscreen";
    case DisplayMode::Windowed:
    default:
        return "Mode: Windowed";
    }
}

std::vector<std::pair<int, int>> fallback_resolutions(std::pair<int, int> desktop)
{
    if (desktop.first < 640 || desktop.second < 400)
    {
        // No usable desktop information: the 16:10 multiples of the classic
        // 320x200 canvas.
        return {{640, 400}, {960, 600}, {1280, 800}, {1600, 1000}, {1920, 1200}};
    }
    // The desktop itself plus aspect-preserving fractions of it, so every
    // offered window size matches the user's own display shape.
    std::vector<std::pair<int, int>> out;
    for (const int num : {4, 3, 2})
    {
        // 4/4 (native), 3/4, 2/4 of the desktop, rounded to even.
        const int w = (desktop.first * num / 4) & ~1;
        const int h = (desktop.second * num / 4) & ~1;
        if (w >= 640 && h >= 400)
            out.emplace_back(w, h);
    }
    if (out.empty())
        out.emplace_back(desktop);
    return out;
}

std::vector<std::pair<int, int>> build_resolution_choices(
    const std::vector<std::pair<int, int>>& display_modes,
    std::pair<int, int> desktop,
    std::pair<int, int> current,
    DisplayMode mode)
{
    std::vector<std::pair<int, int>> out = display_modes;
    if (mode != DisplayMode::Exclusive)
    {
        const bool desktop_is_usable =
            desktop.first >= 640 && desktop.second >= 400;
        if (desktop_is_usable &&
            std::find(out.begin(), out.end(), desktop) == out.end())
        {
            out.push_back(desktop);
        }

        if (out.size() < 2)
            out = fallback_resolutions(desktop);

        // A hand-edited/windowed size is useful on the window-size lap.
        if (std::find(out.begin(), out.end(), current) == out.end())
            out.push_back(current);
    }

    std::sort(out.begin(), out.end(), std::greater<>());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::pair<int, int> preferred_exclusive_resolution(
    const std::vector<std::pair<int, int>>& display_modes,
    std::pair<int, int> desktop)
{
    const auto choices = build_resolution_choices(
        display_modes, desktop, {0, 0}, DisplayMode::Exclusive);
    const auto desktop_mode = std::find(choices.begin(), choices.end(), desktop);
    if (desktop.first >= 640 && desktop.second >= 400 &&
        desktop_mode != choices.end())
    {
        return *desktop_mode;
    }
    return choices.empty() ? std::pair<int, int>{0, 0} : choices.front();
}

std::pair<int, int> parse_resolution(const std::string& width, const std::string& height)
{
    const std::optional<int> w = parse_int_strict(width);
    const std::optional<int> h = parse_int_strict(height);
    if (!w || !h || *w < 320 || *h < 200)
        return {640, 400}; // the video_init boot default
    return {*w, *h};
}

std::pair<int, int> next_resolution(const std::vector<std::pair<int, int>>& list,
                                    const std::string& width, const std::string& height)
{
    if (list.empty())
        return parse_resolution(width, height);
    const std::pair<int, int> current = parse_resolution(width, height);
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (list[i] == current)
            return list[(i + 1) % list.size()];
    }
    return list.front(); // hand-edited size re-enters the list
}

std::string format_resolution_label(const std::string& width, const std::string& height)
{
    const std::pair<int, int> r = parse_resolution(width, height);
    return "Res: " + std::to_string(r.first) + "x" + std::to_string(r.second);
}

// --- Team choice helpers (local seats) ---

bool team_has_members(const SaveData& save, short team)
{
    for (const auto& member : save.team_list)
    {
        if (member && member->teamnum == team)
            return true;
    }
    return false;
}

bool set_preferred_team(SaveData& save, short team)
{
    if (team < 0 || team >= MAX_PLAYERS)
        return false;
    if (!team_has_members(save, team))
        return false;
    save.my_team = team;
    return true;
}

short cycle_guy_team(SaveData& save, int slot_index, int dir)
{
    if (slot_index < 0 || slot_index >= MAX_TEAM_SIZE)
        return -1;
    guy* member = save.team_list[static_cast<std::size_t>(slot_index)].get();
    if (member == nullptr)
        return -1;
    const int next = ((member->teamnum + dir) % 4 + 4) % 4;
    member->teamnum = static_cast<short>(next);
    return static_cast<short>(next);
}

std::vector<short> derive_local_seat_teams(const SaveData& save)
{
    std::vector<short> teams;
    teams.reserve(MAX_PLAYERS);
    for (const auto& member : save.team_list)
    {
        if (!member || !member->deployed)
            continue;
        const short team = member->teamnum;
        if (team <= 0 || team >= MAX_PLAYERS)
            continue;
        if (std::find(teams.begin(), teams.end(), team) == teams.end())
            teams.push_back(team);
    }

    const short preferred = save.my_team;
    const bool preferred_has_deployed = std::any_of(
        save.team_list.begin(), save.team_list.end(),
        [preferred](const std::unique_ptr<guy>& member) {
            return member != nullptr && member->deployed &&
                member->teamnum == preferred;
        });
    if (preferred >= 0 && preferred < MAX_PLAYERS && preferred_has_deployed)
    {
        teams.erase(std::remove(teams.begin(), teams.end(), preferred),
                    teams.end());
        teams.insert(teams.begin(), preferred);
    }
    return teams;
}

std::vector<short> derive_local_gameplay_seat_teams(const SaveData& save)
{
    const int required_players = std::clamp<int>(save.numplayers, 0, MAX_PLAYERS);
    if (required_players == 0)
        return {};

    std::vector<short> teams = derive_local_seat_teams(save);
    if (static_cast<int>(teams.size()) > required_players)
        teams.resize(static_cast<std::size_t>(required_players));

    for (short candidate = 0;
         static_cast<int>(teams.size()) < required_players &&
             candidate < MAX_PLAYERS;
         ++candidate)
    {
        if (std::find(teams.begin(), teams.end(), candidate) == teams.end())
            teams.push_back(candidate);
    }

    if (teams.empty())
        teams.push_back(0);
    if (save.allied_mode != 0)
        teams.assign(static_cast<std::size_t>(required_players), teams.front());
    return teams;
}

bool local_seat_teams_have_controls(const SaveData& save,
                                    std::span<const short> seat_teams)
{
    std::array<int, MAX_PLAYERS> available{};
    for (const auto& member : save.team_list)
    {
        if (member == nullptr || !member->deployed || member->teamnum < 0 ||
            member->teamnum >= MAX_PLAYERS)
        {
            continue;
        }
        ++available[static_cast<std::size_t>(member->teamnum)];
    }

    for (const short team : seat_teams)
    {
        if (team < 0 || team >= MAX_PLAYERS)
            return false;
        int& remaining = available[static_cast<std::size_t>(team)];
        if (remaining <= 0)
            return false;
        --remaining;
    }
    return true;
}

og::data::CompanyAutosaveContext company_autosave_context(
    const SaveData& save, bool networked_lobby_active)
{
    og::data::CompanyAutosaveContext context;
    context.networked_lobby = networked_lobby_active;
    if (!networked_lobby_active)
        return context;

    // Owned wallets = every m_totalcash index this machine's mutations spend
    // from. In a networked lobby the in-memory roster remains private to this
    // machine and each in-range teamnum remains a combat/wallet color;
    // my_team covers hiring into an empty roster. Team 0 is a real wallet
    // index here — unlike derive_local_seat_teams, which is about SEATS.
    if (save.my_team >= 0 && save.my_team < SCORE_TEAM_COUNT)
        context.owned_teams[static_cast<std::size_t>(save.my_team)] = true;
    for (const auto& member : save.team_list)
    {
        if (!member)
            continue;
        const short team = member->teamnum;
        if (team >= 0 && team < SCORE_TEAM_COUNT)
            context.owned_teams[static_cast<std::size_t>(team)] = true;
    }
    return context;
}

SaveDataIoError company_autosave_after_mutation(SaveData& save,
                                                bool networked_lobby_active,
                                                int additional_owned_team)
{
    og::data::CompanyAutosaveContext context =
        company_autosave_context(save, networked_lobby_active);
    if (networked_lobby_active && additional_owned_team >= 0 &&
        additional_owned_team < SCORE_TEAM_COUNT)
    {
        context.owned_teams[static_cast<std::size_t>(
            additional_owned_team)] = true;
    }
    return og::data::company_autosave(
        save,
        og::data::CompanyAutosaveKind::BaseCampMutation,
        context);
}

std::vector<int> collect_base_camp_slots(const SaveData& save)
{
    std::vector<int> slots;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (save.team_list[i])
            slots.push_back(i);
    }
    return slots;
}

int count_deployed_members(const SaveData& save)
{
    int deployed = 0;
    for (const auto& member : save.team_list) {
        if (member && member->deployed)
            ++deployed;
    }
    return deployed;
}

bool toggle_deploy_slot(SaveData& save, int slot)
{
    if (slot < 0 || slot >= MAX_TEAM_SIZE || !save.team_list[slot])
        return false;
    guy& member = *save.team_list[slot];
    member.deployed = !member.deployed;
    return member.deployed;
}

int move_team_member_up(SaveData& save, int slot)
{
    if (slot <= 0 || slot >= MAX_TEAM_SIZE || !save.team_list[slot])
        return -1;

    for (int previous = slot - 1; previous >= 0; --previous) {
        if (!save.team_list[previous])
            continue;
        std::swap(save.team_list[previous], save.team_list[slot]);
        return previous;
    }
    return -1;
}

namespace {

std::string clip_chars(std::string value, std::size_t max_chars)
{
    if (value.size() > max_chars)
        value.resize(max_chars);
    return value;
}

} // namespace

BaseCampRowText format_base_camp_row(const guy& member)
{
    BaseCampRowText row;
    row.name = clip_chars(member.name, 12);
    std::string cls = family_display_name(member.family);
    for (char& c : cls)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    row.cls = clip_chars(std::move(cls), 9);
    // §9.9 graft (b): fixed-width left-padded numerics so the digit columns
    // right-align down the page (the clip budgets are unchanged).
    row.level = clip_chars(std::format("{:>2}", member.level), 3);
    row.exp = clip_chars(std::format("{:>6}", member.exp), 6);
    return row;
}

unsigned char base_camp_family_ramp_start(short family)
{
    // The exact View Team formula from master. Each result begins the
    // family's 16-entry palette ramp.
    return static_cast<unsigned char>(((family + 1) << 4) & 255);
}

std::string format_base_camp_gold_label(const SaveData& save)
{
    // The lobby may remap my_team to a shared seat/control team without
    // changing this company's combat colors or wallets. Prefer the first
    // wallet actually represented by the private
    // roster; only an empty company falls back to the seat team.
    int team = MAX_PLAYERS;
    for (const auto& member : save.team_list)
    {
        if (member != nullptr && member->teamnum >= 0 &&
            member->teamnum < MAX_PLAYERS)
        {
            team = std::min(team, static_cast<int>(member->teamnum));
        }
    }
    if (team == MAX_PLAYERS)
    {
        team = (save.my_team >= 0 && save.my_team < MAX_PLAYERS)
            ? save.my_team
            : 0;
    }
    return clip_chars(
        std::format("GOLD {}",
                    static_cast<unsigned>(
                        save.m_totalcash[static_cast<std::size_t>(team)])),
        11);
}

std::string format_base_camp_scen_line(const SaveData& save,
                                       std::string_view level_title)
{
    const std::string dep_part =
        std::format("  DEP {}/{}", count_deployed_members(save),
                    static_cast<int>(collect_base_camp_slots(save).size()));
    std::string scen_part =
        std::format("SCEN {}: {}", save.scen_num, level_title);
    const std::size_t budget = 34;
    const std::size_t scen_budget =
        dep_part.size() < budget ? budget - dep_part.size() : 0;
    scen_part = clip_chars(std::move(scen_part), scen_budget);
    return scen_part + dep_part;
}

std::vector<BaseCampDisplaySlot> collect_base_camp_display_slots(
    const SaveData& save,
    const std::vector<og::sim::LobbyPlayer>& players,
    const std::vector<std::uint8_t>& local_player_indices,
    bool networked)
{
    std::vector<BaseCampDisplaySlot> slots;

    // Own rows first (§2.5 MP roster rule), read from the PRIVATE save so
    // toggles/train edits show the same frame. Solo: this is the whole list.
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (!save.team_list[i])
            continue;
        BaseCampDisplaySlot slot;
        slot.owned = true;
        slot.save_slot = i;
        slot.deployed = save.team_list[i]->deployed;
        if (networked)
            slot.company = save.save_name;
        slots.push_back(std::move(slot));
    }
    if (!networked)
        return slots;

    // Foreign rows: every other machine's replicated slots, ordered by
    // owner player index then declared slot order (stable across polls).
    std::vector<const og::sim::LobbyPlayer*> foreign;
    for (const og::sim::LobbyPlayer& player : players) {
        if (std::find(local_player_indices.begin(), local_player_indices.end(),
                      player.player_index) != local_player_indices.end())
        {
            continue;
        }
        foreign.push_back(&player);
    }
    std::stable_sort(foreign.begin(), foreign.end(),
                     [](const og::sim::LobbyPlayer* a,
                        const og::sim::LobbyPlayer* b) {
                         return a->player_index < b->player_index;
                     });
    for (const og::sim::LobbyPlayer* player : foreign) {
        for (const og::sim::LobbyCharacterSlot& wire_slot :
             player->character_slots)
        {
            BaseCampDisplaySlot slot;
            slot.owned = false;
            slot.owner_player_index = player->player_index;
            slot.deployed = wire_slot.deployed;
            slot.company = player->company;
            slot.character = wire_slot.character;
            slots.push_back(std::move(slot));
        }
    }
    return slots;
}

BaseCampDeployCounts count_base_camp_display_deploys(
    const std::vector<BaseCampDisplaySlot>& slots, const SaveData& save)
{
    BaseCampDeployCounts counts;
    for (const BaseCampDisplaySlot& slot : slots) {
        ++counts.total;
        bool deployed = slot.deployed;
        if (slot.owned && slot.save_slot >= 0 &&
            slot.save_slot < MAX_TEAM_SIZE &&
            save.team_list[slot.save_slot])
        {
            deployed = save.team_list[slot.save_slot]->deployed;
        }
        if (deployed)
            ++counts.deployed;
    }
    return counts;
}

namespace {

std::uint64_t lobby_machine_group_key(
    const og::sim::LobbyPlayer& player,
    std::size_t ordinal) noexcept
{
    if (player.machine_id != og::sim::kInvalidLobbyMachineId)
        return player.machine_id;
    // A missing authority grant must never make unrelated seats look like
    // one machine. This path exists for defensive/test-shaped states only;
    // v9 servers stamp every accepted seat.
    return (std::uint64_t{1} << 32) | ordinal;
}

} // namespace

BaseCampReadyCounts count_base_camp_ready_machines(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    struct MachineState {
        bool is_host = false;
        bool all_ready = true;
    };
    std::map<std::uint64_t, MachineState> machines;
    for (std::size_t index = 0; index < players.size(); ++index) {
        const og::sim::LobbyPlayer& player = players[index];
        MachineState& machine =
            machines[lobby_machine_group_key(player, index)];
        machine.is_host = machine.is_host || player.is_host;
        machine.all_ready = machine.all_ready && player.ready;
    }
    BaseCampReadyCounts counts;
    for (const auto& [key, machine] : machines) {
        (void)key;
        if (machine.is_host)
            continue;
        ++counts.machines;
        if (machine.all_ready)
            ++counts.ready;
    }
    return counts;
}

BaseCampSessionCensus count_base_camp_session_census(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    std::set<std::uint64_t> machines;
    for (std::size_t index = 0; index < players.size(); ++index)
        machines.insert(lobby_machine_group_key(players[index], index));
    BaseCampSessionCensus census;
    census.machines = static_cast<int>(machines.size());
    census.players = static_cast<int>(players.size());
    return census;
}

std::string base_camp_host_display_name(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    for (const og::sim::LobbyPlayer& player : players) {
        if (!player.is_host)
            continue;
        if (!player.company.empty())
            return clip_chars(player.company, 16);
        return clip_chars(player.name, 16);
    }
    return {};
}

std::string format_base_camp_session_status(
    bool is_host,
    std::string_view room_code,
    const std::vector<og::sim::LobbyPlayer>& players)
{
    // Room codes display-clip at 12 (relay codes are 9-char "GLAD-XXXX";
    // the NETWORKING screen keeps the authoritative full form).
    const std::string room = clip_chars(std::string(room_code), 12);
    std::string status;
    if (is_host) {
        // §9.12 budget note: "MACH / PLYR" is the recorded "shape like"
        // latitude call — the spelled-out census overruns the 42-char band
        // at double-digit counts ("HOSTING GLAD-XXXX - 16 MACHINES / 16
        // PLAYERS" = 44); the abbreviated worst case is 37.
        const BaseCampSessionCensus census =
            count_base_camp_session_census(players);
        const std::string census_part = std::format(
            "{} MACH / {} PLYR", census.machines, census.players);
        status = room.empty()
            ? std::format("HOSTING {}", census_part)
            : std::format("HOSTING {} - {}", room, census_part);
    } else {
        const std::string host_name = base_camp_host_display_name(players);
        const std::string room_part =
            room.empty() ? std::string("JOINED")
                         : std::format("IN {}", room);
        status = host_name.empty()
            ? room_part
            : std::format("{} - HOST: {}", room_part, host_name);
    }
    return clip_chars(std::move(status), 42);
}

BaseCampLineB compose_base_camp_line_b(
    const std::optional<std::string>& alert,
    bool is_host,
    std::string_view room_code,
    const std::vector<og::sim::LobbyPlayer>& players)
{
    // Degraded links outrank the healthy session status: the alert takes
    // the §2.5 line-B slot (and the ORANGE color) until the link heals.
    if (alert.has_value())
        return {.text = *alert, .alert = true};
    return {.text = format_base_camp_session_status(is_host, room_code,
                                                    players),
            .alert = false};
}

ReadyGoPresentation format_ready_go_button(bool networked,
                                           bool is_host,
                                           bool my_ready,
                                           bool all_other_machines_ready,
                                           int global_deployed,
                                           int own_deployed,
                                           bool cross_control,
                                           bool spectator)
{
    ReadyGoPresentation p;
    if (!networked) {
        // States 1-2: solo/local multi never consults ready (the local
        // LobbyServer is exempt) and keeps the plain grey face byte-identical.
        p.state = global_deployed > 0 ? ReadyGoState::LocalGo
                                      : ReadyGoState::LocalGoNoDeploy;
        p.label = "GO";
        p.face_color = kReadyGoFaceGrey;
        if (p.state == ReadyGoState::LocalGoNoDeploy)
            p.caption = "DEPLOY AT LEAST ONE";
        return p;
    }
    if (is_host) {
        // States 3-4. Rule 3 outranks rule 4 (the server's start_allowed
        // order): report unready machines before the global-deploy gate.
        p.label = "GO";
        if (!all_other_machines_ready) {
            p.state = ReadyGoState::HostGated;
            p.face_color = kReadyGoFaceGated;
            p.caption = "WAITING FOR OTHERS";
        } else if (global_deployed <= 0) {
            p.state = ReadyGoState::HostGated;
            p.face_color = kReadyGoFaceGated;
            p.caption = "NO ONE IS DEPLOYED";
        } else {
            p.state = ReadyGoState::HostGo;
            p.face_color = kReadyGoFaceGo;
        }
        return p;
    }
    // States 5-6 (joiner). Label = the action, color = the state.
    if (my_ready) {
        p.state = ReadyGoState::ClientReady;
        p.label = "UNREADY";
        p.face_color = kReadyGoFaceGo;
        return p;
    }
    p.state = ReadyGoState::ClientUnready;
    p.label = "READY";
    p.face_color = kReadyGoFaceUnready;
    // Client ready gate: cross-control OFF + own roster brought characters
    // + none deployed => the click popups instead of readying. The spectator
    // formatter shape and active seats with empty rosters have no deploy
    // minimum [NET-R9]; Base Camp separately hides READY for a true zero-seat
    // client, which is exempt from the server gate. Cross-control ON also
    // removes the minimum (bring 0, play a friend's characters).
    if (!cross_control && !spectator && own_deployed <= 0)
        p.caption = "DEPLOY AT LEAST ONE";
    return p;
}

std::string format_go_blockers(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    struct MachineState {
        bool is_host = false;
        bool all_ready = true;
        std::string company;
        std::string display_name;
    };
    // Insertion order keeps the popup stable across identical lobby states.
    std::vector<std::pair<std::uint64_t, MachineState>> machines;
    for (std::size_t index = 0; index < players.size(); ++index) {
        const og::sim::LobbyPlayer& player = players[index];
        const std::uint64_t key = lobby_machine_group_key(player, index);
        auto it = std::find_if(machines.begin(), machines.end(),
                               [&key](const auto& entry) {
                                   return entry.first == key;
                               });
        if (it == machines.end())
            it = machines.insert(machines.end(), {key, MachineState{}});
        it->second.is_host = it->second.is_host || player.is_host;
        it->second.all_ready = it->second.all_ready && player.ready;
        if (it->second.company.empty())
            it->second.company = player.company;
        if (it->second.display_name.empty())
            it->second.display_name = player.name;
    }
    std::vector<std::string> blockers;
    for (const auto& [key, machine] : machines) {
        (void)key;
        if (machine.is_host || machine.all_ready)
            continue;
        blockers.push_back(clip_chars(
            machine.company.empty() ? machine.display_name : machine.company,
            26));
    }
    std::string body;
    constexpr std::size_t kMaxLines = 4;
    for (std::size_t i = 0; i < blockers.size() && i < kMaxLines; ++i) {
        if (!body.empty())
            body += '\n';
        body += blockers[i];
    }
    if (blockers.size() > kMaxLines)
        body += std::format("\nAND {} MORE", blockers.size() - kMaxLines);
    return body;
}

std::string format_cross_control_label(bool cross_control_enabled)
{
    return cross_control_enabled ? "CTRL: ALL" : "CTRL: OWN";
}

BaseCampNetRowText format_base_camp_net_row(std::string_view name,
                                            std::string_view company,
                                            int level)
{
    BaseCampNetRowText row;
    row.name = clip_chars(std::string(name), 10);
    row.company = clip_chars(std::string(company), 16);
    // §9.9 graft (b): the level left-pads to 2 like the solo shape.
    row.level = clip_chars(std::format("{:>2}", level), 3);
    return row;
}

std::unique_ptr<guy> make_base_camp_display_guy(
    const og::sim::LobbyCharacterData& character)
{
    auto result = std::make_unique<guy>(character.family);
    result->name = character.name;
    result->family = static_cast<char>(character.family);
    result->strength = character.strength;
    result->dexterity = character.dexterity;
    result->constitution = character.constitution;
    result->intelligence = character.intelligence;
    result->armor = character.armor;
    result->exp = character.exp;
    result->level = character.level;
    result->teamnum = character.teamnum;
    return result;
}

std::string format_team_row_label(short team,
                                  int hero_count,
                                  bool is_ctf,
                                  bool authored,
                                  bool has_humans,
                                  std::string_view seat_tag)
{
    std::string label = std::format("{} TEAM", og::sim::ctf_team_color_name(team));
    if (!seat_tag.empty())
    {
        label += ' ';
        label += seat_tag;
    }
    label += ' ';
    if (is_ctf && !authored)
        label += "NOT ON MAP";
    else if (is_ctf && authored && !has_humans && hero_count == 0)
        label += "BOTS";
    else
        label += std::format("{} HEROES", hero_count);
    return label;
}

std::vector<std::string> paginate_team_detail_pages(
    const std::vector<std::string>& items, int max_chars)
{
    std::vector<std::string> pages;
    if (max_chars < 1)
        max_chars = 1;

    std::string current;
    for (const std::string& raw_item : items)
    {
        std::string item = raw_item;
        if (static_cast<int>(item.size()) > max_chars)
        {
            // Keep the truncation visible even when the result is a single
            // page (no '>' pager, no p/N indicator): clip inside the budget
            // and mark the cut with '..'. Degenerate budgets (<= 2 chars)
            // have no room for the marker and just clip.
            if (max_chars > 2)
            {
                item.resize(static_cast<std::size_t>(max_chars - 2));
                item += "..";
            }
            else
            {
                item.resize(static_cast<std::size_t>(max_chars));
            }
        }

        if (current.empty())
        {
            current = item;
            continue;
        }
        const std::size_t joined_size = current.size() + 2 + item.size();
        if (static_cast<int>(joined_size) <= max_chars)
        {
            current += ", ";
            current += item;
        }
        else
        {
            pages.push_back(std::move(current));
            current = item;
        }
    }
    if (!current.empty() || pages.empty())
        pages.push_back(std::move(current));
    return pages;
}

// --- Campaign ordering ---

void order_campaigns_for_select(std::list<std::string>& campaign_ids)
{
    // The shipped shelf order: the classics lead (gladiator, then tryxian),
    // the two original story campaigns follow, then the multiplayer
    // packages, then the tower (single-player mode package, owner-placed
    // after the MP block), with the concept playground trailing. Campaigns
    // not on the shelf (user-made packages) keep their incoming enumeration
    // order and follow every shelved id.
    static constexpr std::string_view kShelf[] = {
        og::kDefaultCampaignId, // org.openglad.gladiator
        "org.openglad.tryxian",
        "org.openglad.westlands",
        "org.openglad.longseason",
        og::kCtfCampaignId,     // org.openglad.ctf (multiplayer)
        "org.openglad.arenas",  // multiplayer arenas
        og::kTowerCampaignId,   // org.openglad.tower (The Endless Tower)
        "org.openglad.concept",
    };
    auto anchor = campaign_ids.begin();
    for (const std::string_view id : kShelf)
    {
        const auto it = std::find(anchor, campaign_ids.end(), id);
        if (it == campaign_ids.end())
            continue;
        if (it == anchor)
        {
            ++anchor;
            continue;
        }
        campaign_ids.splice(anchor, campaign_ids, it);
    }
}

void filter_campaigns_for_networked_lobby(std::list<std::string>& campaign_ids,
                                          bool networked_session)
{
    if (!networked_session)
        return; // local shelves keep every campaign (tower is local-only)
    // v1 keys on the tower id directly (the kCtfCampaignId precedent); the
    // documented upgrade is a yaml-driven campaign_mode(id) accessor once a
    // second mode campaign exists. The prepare_launch veto and the
    // LobbyServer sanitize backstop enforce the same rule below the UI.
    campaign_ids.remove_if([](const std::string& id)
                           { return id == og::kTowerCampaignId; });
}

std::vector<std::string> format_campaign_select_labels(
    const std::vector<std::string>& campaign_ids)
{
    std::map<std::string, int> title_counts;
    for (const std::string& id : campaign_ids)
        ++title_counts[og::data::campaign_display_title(id)];

    std::vector<std::string> labels;
    labels.reserve(campaign_ids.size());
    for (const std::string& id : campaign_ids)
    {
        std::string label = og::data::campaign_display_title(id);
        // A fallback label already IS the id — appending it again would
        // just stutter.
        if (title_counts[label] > 1 && label != id)
            label += " [" + id + "]";
        labels.push_back(std::move(label));
    }
    return labels;
}

bool sync_campaign_mount_to_save(const SaveData& save)
{
    const std::string mounted = get_mounted_campaign();
    if (mounted == save.current_campaign)
        return true;
    if (mount_campaign_package_with_error(save.current_campaign) ==
        CampaignPackageIoError::None)
        return true;
    // Missing/corrupt package (a joiner can legitimately lack the host's
    // campaign): a failed mount leaves NOTHING mounted, so put the previous
    // package back to keep some level data loadable.
    (void)mount_campaign_package_with_error(mounted);
    return false;
}

// --- Player count ---

void set_player_count(SaveData& save, int count)
{
    save.numplayers = static_cast<unsigned char>(count);
}

bool is_spectator_mode(const SaveData& save)
{
    return save.numplayers == 0;
}

// --- Label formatting ---

std::string format_difficulty_label(int difficulty)
{
    const int normalized =
        ((difficulty % DIFFICULTY_SETTINGS) + DIFFICULTY_SETTINGS) % DIFFICULTY_SETTINGS;
    return std::format("Difficulty: {}", kDifficultyNames[normalized]);
}

std::string format_allied_mode_label(const SaveData& save)
{
    return is_allied_mode(save) ? "SEATS: TOGETHER" : "SEATS: SPLIT";
}

std::string format_ctf_teams_label(const SaveData& save)
{
    if (save.ctf_team_count <= 0)
        return "Teams: Auto";
    return std::format("Teams: {}", save.ctf_team_count);
}

std::string format_ctf_caps_label(const SaveData& save)
{
    if (save.ctf_capture_limit <= 0)
        return "Limit: Map";
    return std::format("Limit: {}", save.ctf_capture_limit);
}

std::string format_ctf_troops_label(const SaveData& save)
{
    return save.ctf_strip_scenario_troops != 0 ? "Troops: Own" : "Troops: Scen";
}

std::string format_respawn_mode_label(const SaveData& save)
{
    if (save.respawn_mode <= og::sim::kRespawnModeOff)
        return "Respawns: Off";
    if (save.respawn_mode == og::sim::kRespawnModeHeroes)
        return "Respawns: Heroes";
    if (save.respawn_mode == og::sim::kRespawnModeTeamOneHeroes)
        return "Respawns: Team 1 Heroes";
    return "Respawns: Everyone";
}

std::string format_respawn_delay_label(const SaveData& save)
{
    // Anything outside the cycle set (including the 0 map-default sentinel)
    // reads as the default delay.
    if (save.ctf_respawn_ticks == 60)
        return "Spawn Delay: Fast";
    if (save.ctf_respawn_ticks == 360)
        return "Spawn Delay: Slow";
    return "Spawn Delay: Normal";
}

std::string format_permadeath_label(const SaveData& save)
{
    return save.keep_fallen_heroes != 0 ? "Permadeath: Off" : "Permadeath: On";
}

std::string format_generator_rate_label(const SaveData& save)
{
    if (save.generator_rate == 50)
        return "Generators: Calm";
    if (save.generator_rate == 200)
        return "Generators: Frenzy";
    return "Generators: Normal";
}

// --- Company screens: label formatters (design §2.2/§2.3) ---

namespace {

std::string clip_to(std::string text, std::size_t max_len)
{
    if (text.size() > max_len)
        text.resize(max_len);
    return text;
}

// "YYYY-MM-DD" in UTC (deterministic — never the machine's timezone), or ""
// for never-played (<= 0) and out-of-calendar values (a 4-digit year keeps
// the §2.3 10-char date column budget honest).
std::string format_played_date_utc(std::int64_t unix_s)
{
    // 9999-12-31T23:59:59Z. Values beyond this would overflow the int day
    // count inside year_month_day (wrapping to a bogus in-range date), so
    // bound the input BEFORE converting.
    constexpr std::int64_t kMaxFourDigitYearUnixS = 253402300799;
    if (unix_s <= 0 || unix_s > kMaxFourDigitYearUnixS)
        return {};
    const std::chrono::sys_seconds when{std::chrono::seconds{unix_s}};
    const std::chrono::year_month_day ymd{
        std::chrono::floor<std::chrono::days>(when)};
    if (!ymd.ok())
        return {};
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(ymd.year()),
                       static_cast<unsigned>(ymd.month()),
                       static_cast<unsigned>(ymd.day()));
}

} // namespace

std::string format_company_list_title(int count)
{
    return std::format("COMPANIES ({})", count);
}

CompanyRowText format_company_row(const og::data::CompanyInfo& info)
{
    CompanyRowText row;
    row.corrupt = !info.valid;
    // Corrupt headers may still have parsed a display name before failing;
    // fall back to the slot so the row always identifies its file.
    row.name = clip_to(
        !info.display_name.empty() ? info.display_name : info.slot,
        kCompanyNameMaxLen);
    if (row.corrupt)
    {
        row.roster = "--";
        row.played = "CORRUPT";
        return row;
    }
    row.roster = std::format("{:2}", std::clamp(info.roster_size, 0, 99));
    row.played = format_played_date_utc(info.last_played_unix_s);
    return row;
}

std::string format_company_row_line(const og::data::CompanyInfo& info,
                                    bool active)
{
    const CompanyRowText row = format_company_row(info);
    std::string line = std::format("{} {:<18} {:>2}", active ? '*' : ' ',
                                   row.name, row.roster);
    if (!row.played.empty()) {
        line += ' ';
        line += row.played;
    }
    return line;
}

// --- Backups sub-view: label formatters (design §2.4) ---

namespace {

// "MM-DD HH:MM" in UTC (deterministic — never the machine's timezone; the
// §2.3 date-column precedent), or "" for unstamped (<= 0) and
// out-of-calendar values.
std::string format_saved_datetime_utc(std::int64_t unix_s)
{
    // 9999-12-31T23:59:59Z, the same bound the §2.3 date formatter applies
    // BEFORE converting (larger values overflow year_month_day's day count).
    constexpr std::int64_t kMaxFourDigitYearUnixS = 253402300799;
    if (unix_s <= 0 || unix_s > kMaxFourDigitYearUnixS)
        return {};
    const std::chrono::sys_seconds when{std::chrono::seconds{unix_s}};
    const auto day = std::chrono::floor<std::chrono::days>(when);
    const std::chrono::year_month_day ymd{day};
    if (!ymd.ok())
        return {};
    const std::chrono::hh_mm_ss<std::chrono::seconds> tod{when - day};
    return std::format("{:02}-{:02} {:02}:{:02}",
                       static_cast<unsigned>(ymd.month()),
                       static_cast<unsigned>(ymd.day()),
                       tod.hours().count(), tod.minutes().count());
}

} // namespace

std::string format_backup_list_title(const std::string& company_name,
                                     int count)
{
    return std::format("BACKUPS: {} ({}/{})",
                       clip_to(company_name, kCompanyNameMaxLen), count,
                       og::data::kCompanyBackupRetention);
}

BackupRowText format_backup_row(const og::data::CompanyBackupInfo& info)
{
    BackupRowText row;
    row.corrupt = !info.header.valid;
    if (row.corrupt)
    {
        // §2.4 corrupt-backup marking: the row stays listed (and clickable —
        // the restore API's step-0 validation is the guard), but identifies
        // itself as damage instead of level context it never parsed.
        row.level = "CORRUPT";
        row.saved = "--";
        return row;
    }
    const int scen = info.header.scen_num;
    row.level = std::format("L{}", scen);
    // Level title off the MOUNTED package only (the level_display_guarded
    // mount-match rule): a mismatched mount would caption the row with
    // another campaign's title.
    if (get_mounted_campaign() == info.header.campaign_id)
    {
        std::string title = og::data::scenario_display_name(scen);
        const std::string prefix = std::format("{}. ", scen);
        if (title.starts_with(prefix))
            title.erase(0, prefix.size());
        // The missing-scenario fallback title is "Level N" — pure noise
        // next to "L<nn>", so the bare tag stands alone instead.
        if (title != std::format("Level {}", scen))
        {
            row.level += ' ';
            row.level += clip_to(std::move(title), 14);
        }
    }
    row.saved = format_saved_datetime_utc(info.header.last_played_unix_s);
    return row;
}

std::string format_backup_row_line(const og::data::CompanyBackupInfo& info)
{
    const BackupRowText row = format_backup_row(info);
    if (row.saved.empty())
        return row.level;
    return std::format("{:<20} {}", row.level, row.saved);
}

const char* company_restore_error_string(og::data::CompanyRestoreError error)
{
    switch (error)
    {
    case og::data::CompanyRestoreError::None:
        return "";
    case og::data::CompanyRestoreError::InvalidBackup:
        return "BACKUP FILE DAMAGED";
    case og::data::CompanyRestoreError::PreRestoreBackupFailed:
        return "COULD NOT BACK UP CURRENT STATE";
    case og::data::CompanyRestoreError::CopyFailed:
        return "COPY FAILED - COMPANY UNCHANGED";
    case og::data::CompanyRestoreError::ReloadFailed:
        return "RELOAD FAILED - REWIND UNDONE";
    case og::data::CompanyRestoreError::RestampFailed:
        return "REWOUND, BUT THE TIMESTAMP WRITE FAILED";
    }
    return "";
}

ContinueResult open_company_slot(SaveData& save, const std::string& slot,
                                 SaveDataIoError* io_error)
{
    // Never SILENTLY switch to a corrupt company (§2.1/§2.3): validate the
    // header first and keep the currently loaded save on damage — only an
    // explicit restore-from-backup or delete acts on a corrupt file.
    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header(slot);
    if (!header || !header->valid)
        return ContinueResult::Corrupt;

    const std::string previous = og::data::active_company_slot();
    (void)og::data::set_active_company_slot(slot);
    const SaveDataIoError io = save.load_with_error(slot);
    if (io != SaveDataIoError::None) {
        if (io_error != nullptr)
            *io_error = io;
        // The header validated but the body failed (torn file, missing
        // campaign package). Restore the previous slot and best-effort
        // reload it so autosaves keep targeting the company that is really
        // open instead of writing the old save into the broken slot.
        (void)og::data::set_active_company_slot(previous);
        (void)save.load_with_error(previous);
        return ContinueResult::LoadFailed;
    }
    return ContinueResult::Opened;
}

ContinueResult open_most_recent_company(SaveData& save,
                                        SaveDataIoError* io_error)
{
    // §2.1: CONTINUE opens the most-recent company (WP2 startup selection).
    const std::string slot = og::data::select_startup_company();
    if (slot.empty())
        return ContinueResult::NoCompany;  // CONTINUE is gated hidden anyway.
    return open_company_slot(save, slot, io_error);
}

// --- Team family extraction ---

std::vector<int> collect_team_families(const SaveData& save)
{
    std::vector<int> families;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (save.team_list[i])
            families.push_back(static_cast<int>(save.team_list[i]->family));
    }
    return families;
}

// --- Team initialization ---

void initialize_starting_team(SaveData& save, const std::vector<int>& families, int team_num)
{
    if (save.team_size > 0)
        return;

    save.m_totalcash[team_num] = kNewGameStartingGold;
    save.totalcash = kNewGameStartingGold;

    ensure_team_populated(save, families, team_num);
}

// --- Save/Load error strings ---

const char* save_error_string(SaveDataIoError error)
{
    switch (error) {
    case SaveDataIoError::None: return "none";
    case SaveDataIoError::OpenReadFailed: return "open_read_failed";
    case SaveDataIoError::OpenWriteFailed: return "open_write_failed";
    case SaveDataIoError::ReadFailed: return "read_failed";
    case SaveDataIoError::WriteFailed: return "write_failed";
    case SaveDataIoError::InvalidHeader: return "invalid_header";
    case SaveDataIoError::UnsupportedVersion: return "unsupported_version";
    case SaveDataIoError::CampaignLoadFailed: return "campaign_load_failed";
    }
    return "unknown";
}

// --- Stats copy utility ---

void statscopy(guy* dest, const guy* source)
{
    dest->family = source->family;
    dest->strength = source->strength;
    dest->dexterity = source->dexterity;
    dest->constitution = source->constitution;
    dest->intelligence = source->intelligence;
    dest->level = source->level;
    dest->armor = source->armor;
    dest->exp = source->exp;
    dest->kills = source->kills;
    dest->level_kills = source->level_kills;
    dest->total_damage = source->total_damage;
    dest->total_hits = source->total_hits;
    dest->total_shots = source->total_shots;
    dest->teamnum = source->teamnum;

    dest->scen_damage = source->scen_damage;
    dest->scen_kills = source->scen_kills;
    dest->scen_damage_taken = source->scen_damage_taken;
    dest->scen_min_hp = source->scen_min_hp;
    dest->scen_shots = source->scen_shots;
    dest->scen_hits = source->scen_hits;

    dest->name = source->name;
}

// --- HireSession ---

HireSession::HireSession(SaveData& save, int team_num)
    : save_(save), team_num_(team_num)
{
    make_recruit();
}

void HireSession::next_family()
{
    current_type_ = (current_type_ + 1) % static_cast<int>(kAllowableGuys.size());
    make_recruit();
}

void HireSession::prev_family()
{
    current_type_ = (current_type_ - 1 + static_cast<int>(kAllowableGuys.size()))
                    % static_cast<int>(kAllowableGuys.size());
    make_recruit();
}

int HireSession::hire()
{
    if (!recruit_ || team_full())
        return -1;

    // team_num_ comes from a save-loaded guy::teamnum and is not clamped on the
    // SDL hire path; clamp at the index sites so a corrupt save cannot drive an
    // out-of-bounds read/write of the 4-element m_totalcash array.
    const int cash_team = std::clamp(team_num_, 0, static_cast<int>(SCORE_TEAM_COUNT) - 1);

    std::uint32_t cost = current_cost();
    if (cost == 0 || cost > save_.m_totalcash[cash_team])
        return -1;

    save_.m_totalcash[cash_team] -= cost;

    int newfamily = recruit_->family;
    recruit_->teamnum = static_cast<short>(team_num_);
    // Ensure we have the right exp for our level
    recruit_->exp = calculate_exp(recruit_->level);

    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (!save_.team_list[i]) {
            // Save a copy of the hired recruit's stats for the next recruit
            auto next = std::make_unique<guy>(newfamily);
            statscopy(next.get(), recruit_.get());
            next->name = get_unique_name(static_cast<unsigned char>(newfamily), save_);

            save_.team_list[i] = std::move(recruit_);
            save_.team_size++;

            // Next recruit starts with same stats as the one just hired
            recruit_ = std::move(next);

            return i;
        }
    }

    return -1;
}

void HireSession::rename_hired(int slot, const std::string& name)
{
    if (slot >= 0 && slot < MAX_TEAM_SIZE && save_.team_list[slot])
        save_.team_list[slot]->name = name;
}

const guy* HireSession::current_recruit() const
{
    return recruit_.get();
}

std::uint32_t HireSession::current_cost() const
{
    if (!recruit_)
        return 0;
    return calculate_hire_cost(*recruit_);
}

int HireSession::family_index() const
{
    return current_type_;
}

int HireSession::team_num() const
{
    return team_num_;
}

bool HireSession::team_full() const
{
    return save_.team_size >= MAX_TEAM_SIZE;
}

void HireSession::make_recruit()
{
    int family = kAllowableGuys[current_type_];
    recruit_ = create_recruit(family, team_num_, save_);

    // Clamp stats up to family base values
    const auto* fd = get_family_descriptor(family);
    if (fd) {
        if (recruit_->strength < fd->base_stats[StatAxis::Strength])
            recruit_->strength = static_cast<short>(fd->base_stats[StatAxis::Strength]);
        if (recruit_->dexterity < fd->base_stats[StatAxis::Dexterity])
            recruit_->dexterity = static_cast<short>(fd->base_stats[StatAxis::Dexterity]);
        if (recruit_->constitution < fd->base_stats[StatAxis::Constitution])
            recruit_->constitution = static_cast<short>(fd->base_stats[StatAxis::Constitution]);
        if (recruit_->intelligence < fd->base_stats[StatAxis::Intelligence])
            recruit_->intelligence = static_cast<short>(fd->base_stats[StatAxis::Intelligence]);
        if (recruit_->armor < fd->base_stats[StatAxis::Armor])
            recruit_->armor = static_cast<short>(fd->base_stats[StatAxis::Armor]);
        if (recruit_->level < fd->base_stats[StatAxis::Level])
            recruit_->upgrade_to_level(static_cast<short>(fd->base_stats[StatAxis::Level]));
    }
}

// --- TrainSession ---

TrainSession::TrainSession(SaveData& save)
    : save_(save)
{
    if (save_.team_size < 1)
        return;

    // Find first editable team slot. Network lobbies render remote players in
    // the shared save view, but only the local player's slots may be trained.
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (save_.team_list[i] && picker_lobby_save_slot_editable(i)) {
            edit_slot_ = i;
            break;
        }
    }

    select_current_slot();
}

bool TrainSession::empty() const
{
    return save_.team_size < 1 || !working_ || original_member() == nullptr;
}

void TrainSession::next_member()
{
    if (empty())
        return;

    int start = edit_slot_;
    do {
        edit_slot_++;
        if (edit_slot_ >= MAX_TEAM_SIZE)
            edit_slot_ = 0;
    } while ((!save_.team_list[edit_slot_] ||
              !picker_lobby_save_slot_editable(edit_slot_)) &&
             edit_slot_ != start);

    select_current_slot();
}

bool TrainSession::seek_slot(int slot)
{
    if (slot < 0 || slot >= MAX_TEAM_SIZE || !save_.team_list[slot] ||
        !picker_lobby_save_slot_editable(slot))
    {
        return false;
    }
    edit_slot_ = slot;
    select_current_slot();
    return true;
}

void TrainSession::prev_member()
{
    if (empty())
        return;

    int start = edit_slot_;
    do {
        edit_slot_--;
        if (edit_slot_ < 0)
            edit_slot_ = MAX_TEAM_SIZE - 1;
    } while ((!save_.team_list[edit_slot_] ||
              !picker_lobby_save_slot_editable(edit_slot_)) &&
             edit_slot_ != start);

    select_current_slot();
}

void TrainSession::increase_stat(Stat stat, int amount)
{
    if (!working_)
        return;

    // Self-heal before editing: if the real member was promoted (family
    // changed) since the working copy was snapshotted, edit the fresh
    // post-promotion stats. Editing the stale copy would clamp against
    // cross-family values and "put the old stats back" (issue #133).
    resync_if_promoted();

    const short delta = static_cast<short>(amount);

    if (stat == Stat::Level) {
        if (!stats_increased()) {
            short newlevel = static_cast<short>(static_cast<int>(working_->level) + delta);
            working_->upgrade_to_level(newlevel);
        }
    } else {
        if (!level_increased()) {
            switch (stat) {
            case Stat::Strength:
                working_->strength = static_cast<short>(static_cast<int>(working_->strength) + delta);
                break;
            case Stat::Dexterity:
                working_->dexterity = static_cast<short>(static_cast<int>(working_->dexterity) + delta);
                break;
            case Stat::Constitution:
                working_->constitution = static_cast<short>(static_cast<int>(working_->constitution) + delta);
                break;
            case Stat::Intelligence:
                working_->intelligence = static_cast<short>(static_cast<int>(working_->intelligence) + delta);
                break;
            case Stat::Armor:
                working_->armor = static_cast<short>(static_cast<int>(working_->armor) + delta);
                break;
            default:
                break;
            }
        }
    }

    clamp_working_stats();
}

void TrainSession::decrease_stat(Stat stat, int amount)
{
    if (!working_)
        return;

    // See increase_stat: never edit a working copy whose family went stale.
    resync_if_promoted();

    const short delta = static_cast<short>(amount);

    if (stat == Stat::Level) {
        if (!stats_increased()) {
            short newlevel = static_cast<short>(static_cast<int>(working_->level) - delta);
            const guy* const original = original_member();
            if (original && newlevel > 0 && newlevel >= original->level) {
                working_->upgrade_to_level(newlevel);
                if (working_->level == original->level)
                    working_->exp = original->exp;
            }
        }
    } else {
        if (!level_increased()) {
            switch (stat) {
            case Stat::Strength:
                working_->strength = static_cast<short>(static_cast<int>(working_->strength) - delta);
                break;
            case Stat::Dexterity:
                working_->dexterity = static_cast<short>(static_cast<int>(working_->dexterity) - delta);
                break;
            case Stat::Constitution:
                working_->constitution = static_cast<short>(static_cast<int>(working_->constitution) - delta);
                break;
            case Stat::Intelligence:
                working_->intelligence = static_cast<short>(static_cast<int>(working_->intelligence) - delta);
                break;
            case Stat::Armor:
                working_->armor = static_cast<short>(static_cast<int>(working_->armor) - delta);
                break;
            default:
                break;
            }
        }
    }

    clamp_working_stats();
}

void TrainSession::set_team(int team_num)
{
    if (!working_)
        return;

    working_->teamnum =
        static_cast<short>(std::clamp(team_num, 0, static_cast<int>(SCORE_TEAM_COUNT) - 1));
}

bool TrainSession::accept(bool force)
{
    // A family mismatch always means an external promotion (training never
    // edits family). Never statscopy stale cross-family stats over the
    // promoted member (bug A9 / issue #133) — resync first, which discards
    // pending edits and turns this into a no-op accept of the promotion.
    resync_if_promoted();

    guy* const original = original_member();
    if (!working_ || !original)
        return false;

    std::uint32_t cost = current_cost();

    if (!force) {
        // If cost is 0 but stats changed, that's a cost overflow — reject and revert
        if (cost == 0 && (working_->strength != original->strength ||
                          working_->dexterity != original->dexterity ||
                          working_->constitution != original->constitution ||
                          working_->intelligence != original->intelligence ||
                          working_->armor != original->armor ||
                          working_->level != original->level)) {
            statscopy(working_.get(), original);
            return false;
        }

        // working_->teamnum is copied verbatim from a save-loaded guy and is not
        // clamped on the accept() path; clamp at the index sites so a corrupt
        // save cannot drive an out-of-bounds read/write of m_totalcash[4].
        const int cash_team = std::clamp(static_cast<int>(working_->teamnum), 0,
                                         static_cast<int>(SCORE_TEAM_COUNT) - 1);
        if (cost > save_.m_totalcash[cash_team])
            return false;

        save_.m_totalcash[cash_team] -= cost;
    }

    if (original->level != working_->level)
        working_->upgrade_to_level(working_->level);

    statscopy(original, working_.get());

    return true;
}

TrainSession::SellResult TrainSession::sell_current(
    const std::function<bool()>& checkpoint)
{
    const guy* const member = original_member();
    if (!working_ || !member)
        return SellResult::NoMember;

    const int sold_slot = edit_slot_;
    const int cash_team = std::clamp(
        static_cast<int>(member->teamnum), 0,
        static_cast<int>(SCORE_TEAM_COUNT) - 1);
    const std::uint32_t payout = calculate_sell_value(*member);

    // The callback is intentionally the final fallible operation before the
    // wallet/roster mutation. A sale never proceeds without a durable way
    // back to the exact pre-sale company.
    if (!checkpoint || !checkpoint())
        return SellResult::CheckpointFailed;

    const std::uint64_t cash_after =
        static_cast<std::uint64_t>(save_.m_totalcash[cash_team]) + payout;
    save_.m_totalcash[cash_team] = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(
            cash_after, std::numeric_limits<std::uint32_t>::max()));

    // SaveData serialization requires a dense [0, team_size) roster. Remove
    // the selected member and stable-compact every survivor, including
    // defensive recovery from a sparse in-memory roster.
    int write_slot = 0;
    int preferred_slot = 0;
    for (int read_slot = 0; read_slot < MAX_TEAM_SIZE; ++read_slot)
    {
        if (!save_.team_list[read_slot])
            continue;
        if (read_slot == sold_slot)
        {
            preferred_slot = write_slot;
            continue;
        }
        if (write_slot != read_slot)
            save_.team_list[write_slot] =
                std::move(save_.team_list[read_slot]);
        ++write_slot;
    }
    for (int slot = write_slot; slot < MAX_TEAM_SIZE; ++slot)
        save_.team_list[slot].reset();
    save_.team_size = static_cast<unsigned char>(write_slot);

    if (write_slot == 0)
    {
        edit_slot_ = 0;
        working_.reset();
        return SellResult::Sold;
    }

    // Prefer the character that followed the sold member; when the last
    // member was sold, stay on the new last member. Skip any foreign network
    // rows exactly as PREV/NEXT do.
    const int first_candidate = std::min(preferred_slot, write_slot - 1);
    for (int offset = 0; offset < write_slot; ++offset)
    {
        const int candidate = (first_candidate + offset) % write_slot;
        if (save_.team_list[candidate] &&
            picker_lobby_save_slot_editable(candidate))
        {
            edit_slot_ = candidate;
            select_current_slot();
            return SellResult::Sold;
        }
    }

    edit_slot_ = 0;
    working_.reset();
    return SellResult::Sold;
}

bool TrainSession::resync_if_promoted()
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return false;
    if (working_->family == original->family)
        return false;

    // The real team member changed family underneath us (DETAILS promote
    // button). Discard the stale working copy — pending unaccepted stat
    // edits are meaningless across a class change anyway (bug A9).
    select_current_slot();
    return true;
}

const guy& TrainSession::working_copy() const
{
    return *working_;
}

const guy& TrainSession::original() const
{
    return *original_member();
}

std::uint32_t TrainSession::current_cost() const
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return 0;
    return calculate_train_cost(*working_, *original);
}

std::uint32_t TrainSession::current_sell_value() const
{
    const guy* const member = original_member();
    return member != nullptr ? calculate_sell_value(*member) : 0;
}

bool TrainSession::level_increased() const
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return false;
    return original->level < working_->level;
}

bool TrainSession::stats_increased() const
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return false;
    if (level_increased())
        return false;
    return (original->strength < working_->strength
         || original->dexterity < working_->dexterity
         || original->constitution < working_->constitution
         || original->intelligence < working_->intelligence
         || original->armor < working_->armor);
}

int TrainSession::current_slot() const
{
    return edit_slot_;
}

void TrainSession::select_current_slot()
{
    const guy* const original = original_member();
    if (!original) {
        working_.reset();
        return;
    }

    working_ = std::make_unique<guy>(original->family);
    statscopy(working_.get(), original);
}

guy* TrainSession::original_member()
{
    if (edit_slot_ < 0 || edit_slot_ >= MAX_TEAM_SIZE)
        return nullptr;
    if (!picker_lobby_save_slot_editable(edit_slot_))
        return nullptr;
    return save_.team_list[edit_slot_].get();
}

const guy* TrainSession::original_member() const
{
    if (edit_slot_ < 0 || edit_slot_ >= MAX_TEAM_SIZE)
        return nullptr;
    if (!picker_lobby_save_slot_editable(edit_slot_))
        return nullptr;
    return save_.team_list[edit_slot_].get();
}

void TrainSession::clamp_working_stats()
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return;

    if (working_->strength < original->strength)
        working_->strength = original->strength;
    if (working_->dexterity < original->dexterity)
        working_->dexterity = original->dexterity;
    if (working_->constitution < original->constitution)
        working_->constitution = original->constitution;
    if (working_->intelligence < original->intelligence)
        working_->intelligence = original->intelligence;
    if (working_->armor < original->armor)
        working_->armor = original->armor;
    if (working_->level < original->level)
        working_->upgrade_to_level(original->level);
}

// --- Scenario roster report (View Level) ---

namespace {

// Defensive cap for pathological custom levels (pagination handles real maps).
constexpr std::size_t kMaxScenarioReportRows = 200;

bool is_score_team_index(int team)
{
    return team >= 0 && team < 4;
}

std::vector<short> roster_teams_for_strip(const SaveData& save)
{
    std::vector<short> teams;
    for (const auto& member : save.team_list)
    {
        if (!member)
            continue;
        const short team = member->teamnum;
        if (team < 0 || team >= MAX_PLAYERS)
            continue;
        if (std::find(teams.begin(), teams.end(), team) == teams.end())
            teams.push_back(team);
    }
    return teams;
}

std::string clip_line(std::string line)
{
    constexpr std::size_t kMaxLineLength = 48;
    if (line.size() > kMaxLineLength)
        line.resize(kMaxLineLength);
    return line;
}

const char* strip_suffix(ScenarioStripReason reason)
{
    switch (reason)
    {
        case ScenarioStripReason::TroopsOff: return "*";
        case ScenarioStripReason::InactiveTeam: return "+";
        default: return "";
    }
}

} // namespace

ScenarioRosterReport build_scenario_roster_report(const GameWorld& world,
                                                  const SaveData& save)
{
    ScenarioRosterReport report;
    report.is_ctf = (world.type & GameWorld::TYPE_CTF) != 0;
    report.your_team = save.my_team;

    int map_capture_limit = 0;
    if (report.is_ctf)
    {
        // Mirror the init scan: first live flag per team counts; the first
        // flag with a stats level above 1 sets the per-map capture limit.
        for (const auto& uptr : world.fxlist)
        {
            walker* fx = uptr.get();
            if (fx == nullptr || fx->dead() ||
                fx->query_order() != Order::Treasure)
            {
                continue;
            }
            if (fx->family() == og::FAMILY_FLAG)
            {
                const int team = fx->team_num();
                if (!is_score_team_index(team) || report.team_has_flag[team])
                    continue;
                report.team_has_flag[team] = true;
                if (map_capture_limit == 0 && fx->stats() != nullptr &&
                    fx->stats()->level() > 1)
                {
                    map_capture_limit = fx->stats()->level();
                }
            }
            else if (fx->family() == og::FAMILY_CTF_POINT)
            {
                if (report.cp_count < og::sim::kCtfMaxControlPoints)
                    report.cp_count++;
            }
        }

        // Respawn anchors (dead markers included, matching the init scan).
        for (const auto& uptr : world.oblist)
        {
            walker* w = uptr.get();
            if (w == nullptr || w->query_order() != Order::Special ||
                w->family() != FAMILY_RESERVED_TEAM)
            {
                continue;
            }
            const int team = w->team_num();
            if (is_score_team_index(team) &&
                report.team_anchor_count[team] <
                    og::sim::kCtfMaxAnchorsPerTeam)
            {
                report.team_anchor_count[team]++;
            }
        }

        // Active teams: the requested count intersected with authored flag
        // teams, in team index order (the init clamp).
        const int requested = save.ctf_team_count;
        const int max_active =
            (requested <= 0) ? 4 : std::clamp(requested, 2, 4);
        int active_count = 0;
        for (int t = 0; t < 4; ++t)
        {
            if (report.team_has_flag[t] && active_count < max_active)
            {
                report.team_active[t] = true;
                active_count++;
            }
        }
        report.ctf_will_activate = active_count >= 2;
        if (!report.ctf_will_activate)
        {
            for (bool& active : report.team_active)
                active = false;
        }

        const int requested_limit = save.ctf_capture_limit;
        int limit = og::sim::kCtfDefaultCaptureLimit;
        if (requested_limit > 0)
            limit = requested_limit;
        else if (map_capture_limit > 0)
            limit = map_capture_limit;
        report.capture_limit = std::clamp(limit, 1, 255);
    }

    // Strip-annotation predicates (save-side mirror of the sim rules).
    // The sim consumes ctf_strip_scenario_troops on ANY TYPE_CTF map, so the
    // preview must not add a campaign gate the sim does not have.
    const std::vector<short> roster_teams = roster_teams_for_strip(save);
    const bool troops_strip_on = report.is_ctf && report.ctf_will_activate &&
        save.ctf_strip_scenario_troops != 0;
    auto strip_reason_for_team = [&](int team) {
        if (!report.is_ctf || !report.ctf_will_activate)
            return ScenarioStripReason::None;
        // The sim's inactive-team strip removes every living/generator whose
        // team is outside the score range or inactive on an activating map.
        if (!is_score_team_index(team) || !report.team_active[team])
            return ScenarioStripReason::InactiveTeam;
        if (troops_strip_on &&
            std::find(roster_teams.begin(), roster_teams.end(),
                      static_cast<short>(team)) != roster_teams.end())
        {
            return ScenarioStripReason::TroopsOff;
        }
        return ScenarioStripReason::None;
    };

    // Livings: named NPCs individually, unnamed grouped by (team, family,
    // level). Generators aggregate to one row per team. List order.
    std::vector<ScenarioRosterRow> generator_rows;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->dead())
            continue;
        const Order order = w->query_order();
        if (order == Order::Living)
        {
            ScenarioRosterRow row;
            row.team = static_cast<short>(w->team_num());
            row.family = static_cast<short>(w->family());
            row.level = w->stats() != nullptr ? w->stats()->level() : 1;
            if (w->stats() != nullptr && !w->stats()->name.empty())
            {
                row.named = true;
                row.name = w->stats()->name;
                row.strip_reason = strip_reason_for_team(row.team);
                report.rows.push_back(std::move(row));
            }
            else
            {
                auto match = std::find_if(
                    report.rows.begin(), report.rows.end(),
                    [&row](const ScenarioRosterRow& existing) {
                        return !existing.named && !existing.is_generator &&
                            existing.team == row.team &&
                            existing.family == row.family &&
                            existing.level == row.level;
                    });
                if (match != report.rows.end())
                {
                    match->count++;
                }
                else
                {
                    row.strip_reason = strip_reason_for_team(row.team);
                    report.rows.push_back(std::move(row));
                }
            }
        }
        else if (order == Order::Generator)
        {
            const short team = static_cast<short>(w->team_num());
            auto match = std::find_if(
                generator_rows.begin(), generator_rows.end(),
                [team](const ScenarioRosterRow& existing) {
                    return existing.team == team;
                });
            if (match != generator_rows.end())
            {
                match->count++;
            }
            else
            {
                ScenarioRosterRow row;
                row.team = team;
                row.is_generator = true;
                row.family = static_cast<short>(w->family());
                row.strip_reason = strip_reason_for_team(team);
                generator_rows.push_back(std::move(row));
            }
        }
        if (report.rows.size() + generator_rows.size() >=
            kMaxScenarioReportRows)
        {
            break;
        }
    }
    report.rows.insert(report.rows.end(),
                       std::make_move_iterator(generator_rows.begin()),
                       std::make_move_iterator(generator_rows.end()));

    // Team-major presentation order (stable within a team = list order).
    std::stable_sort(report.rows.begin(), report.rows.end(),
                     [](const ScenarioRosterRow& a, const ScenarioRosterRow& b) {
                         return a.team < b.team;
                     });

    for (const ScenarioRosterRow& row : report.rows)
    {
        if (row.strip_reason == ScenarioStripReason::TroopsOff)
            report.any_troops_off = true;
        else if (row.strip_reason == ScenarioStripReason::InactiveTeam)
            report.any_inactive = true;
    }
    return report;
}

std::vector<std::string> format_scenario_report_lines(
    const ScenarioRosterReport& report)
{
    std::vector<std::string> lines;

    if (report.is_ctf)
    {
        int flag_teams = 0;
        for (const bool present : report.team_has_flag)
            flag_teams += present ? 1 : 0;
        lines.push_back(clip_line(std::format(
            "CTF: {} FLAG TEAMS, {} CONTROL POINTS",
            flag_teams, report.cp_count)));
        if (report.ctf_will_activate)
        {
            lines.push_back(clip_line(
                std::format("CAPTURE LIMIT: {}", report.capture_limit)));
            for (int t = 0; t < 4; ++t)
            {
                if (!report.team_has_flag[t])
                    continue;
                lines.push_back(clip_line(std::format(
                    "  {} FLAG  ANCHORS: {}  {}",
                    og::sim::ctf_team_color_name(t),
                    report.team_anchor_count[t],
                    report.team_active[t] ? "ACTIVE" : "INACTIVE")));
            }
        }
        else
        {
            lines.push_back(
                clip_line("CTF INACTIVE: FEWER THAN 2 FLAG TEAMS"));
        }
    }

    short current_team = -1;
    bool first_team = !report.is_ctf;
    for (const ScenarioRosterRow& row : report.rows)
    {
        if (row.team != current_team)
        {
            current_team = row.team;
            if (!first_team || !lines.empty())
                lines.emplace_back();
            first_team = false;
            // Score teams get their color name (matching the MATCHUP screen
            // and the CTF flag lines); anything beyond keeps the raw index.
            std::string header = (current_team >= 0 && current_team < 4)
                ? std::format("{} TEAM",
                              og::sim::ctf_team_color_name(current_team))
                : std::format("TEAM {}", current_team);
            if (current_team == report.your_team)
                header += " (YOURS)";
            lines.push_back(clip_line(std::move(header)));
        }

        std::string text;
        if (row.is_generator)
        {
            text = std::format("  {}x GENERATOR", row.count);
        }
        else if (row.named)
        {
            text = std::format("  {} - {} Lv {}", row.name,
                               family_display_name(row.family), row.level);
        }
        else
        {
            text = std::format("  {}x {} Lv {}", row.count,
                               family_display_name(row.family), row.level);
        }
        text += strip_suffix(row.strip_reason);
        lines.push_back(clip_line(std::move(text)));
    }

    if (report.any_troops_off || report.any_inactive)
    {
        lines.emplace_back();
        if (report.any_troops_off)
            lines.push_back(clip_line("* REMOVED: TROOPS OFF"));
        if (report.any_inactive)
            lines.push_back(clip_line("+ REMOVED: INACTIVE TEAM"));
    }
    return lines;
}

} // namespace og::ui
