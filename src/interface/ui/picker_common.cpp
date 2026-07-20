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
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>

// Defined in entities/guy.cpp
std::uint32_t calculate_exp(std::int32_t level);

// Legacy global used by entity code (living.cpp, walker.cpp) via
// extern std::int32_t difficulty_level[].  Centralised here so both
// the SDL and headless clients share a single definition.
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
        std::pow(stat_delta(recruit.strength, fd->base_stats[0]), kStatCostExponent)
        * fd->stat_costs[0]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.dexterity, fd->base_stats[1]), kStatCostExponent)
        * fd->stat_costs[1]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.constitution, fd->base_stats[2]), kStatCostExponent)
        * fd->stat_costs[2]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.intelligence, fd->base_stats[3]), kStatCostExponent)
        * fd->stat_costs[3]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.armor, fd->base_stats[4]), kStatCostExponent)
        * fd->stat_costs[4]);

    // Level cost
    short effective_level = std::max(recruit.level, static_cast<short>(fd->base_stats[5]));
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

// --- Name generation ---

namespace {

#define GET_RAND_ELEM(array) (array[std::rand() % (sizeof(array) / sizeof(array[0]))])

const char* archer_names[] = {
    "Robin", "Green Arrow",
    "Legolas",
    "Yeoman", "Strider", "Longshot", "Bowyer", "Hunter", "Archy"
};

const char* cleric_names[] = {
    "Tuck",
    "Brother", "Pater", "Drake", "Friar", "Francis", "John Paul", "Medic"
};

const char* druid_names[] = {
    "Roland",
    "Merlin",
    "Hippy", "Green Thumb", "Treefall", "Rain"
};

const char* elf_names[] = {
    "Legolas", "Took", "Elrond",
    "Tanis",
    "Acorn", "Lightfoot", "Treewee"
};

const char* mage_names[] = {
    "Gandalf", "Saruman", "Radagast", "Alatar", "Pallando",
    "Raistlin", "Fizban", "Mordenkainen",
    "Merlin",
    "Harry",
    "Manannan", "Mordack",
    "Jace"
};

const char* soldier_names[] = {
    "Lothar",
    "Arthur", "Uther",
    "Achilles", "Lu Bu", "Wallace", "Leonidas", "Attila", "Alexander", "Ajax", "Nestor", "Priam", "Hector",
    "Tom", "Bigfoot"
};

const char* thief_names[] = {
    "Shinobi",
    "Dismas",
    "Shadow", "Stabby", "Swiftstrike", "Scourge", "Rogue"
};

const char* orc_names[] = {
    "Grom",
    "Thrull",
    "Vernix", "Lanugo",
    "Grok", "Horde", "Grog", "Krosh"
};

const char* barbarian_names[] = {
    "Thor",
    "Conan",
    "Beowulf", "Cronus", "Pallas", "Atlas", "Prometheus",
    "Titan"
};

const char* elemental_names[] = {
    "Furnace", "Molten", "Burns", "Fire Eli", "Fireball", "Sunny", "Lava", "Heatwave", "Torch", "Scorch"
};

const char* skeleton_names[] = {
    "Drybones",
    "Blackbeard",
    "Boney", "Femur", "Patella", "Humerus", "Scapula"
};

const char* slime_names[] = {
    "Grimer",
    "Goop", "Slurp", "Glopp", "Sludge", "Blob"
};

const char* faerie_names[] = {
    "Tink",
    "Gem", "Glitter", "Jewel", "Blossom", "Ruby", "Muffin", "Flutter", "Sparkle", "Sprint", "Sprite", "Eve", "Twinkle", "Violet", "Daisy", "Lily"
};

const char* ghost_names[] = {
    "Casper",
    "Slimer",
    "Reaper", "Ecto", "Pepper", "Boo", "Banshee", "Nyx"
};

bool has_name_in_save(const char* name, const SaveData& save)
{
    for (int i = 0; i < save.team_size && i < MAX_TEAM_SIZE; i++) {
        if (save.team_list[i] && save.team_list[i]->name == name)
            return true;
    }
    return false;
}

} // anonymous namespace

const char* get_random_name(unsigned char family)
{
    switch (family) {
    case FAMILY_ARCHER:        return GET_RAND_ELEM(archer_names);
    case FAMILY_CLERIC:        return GET_RAND_ELEM(cleric_names);
    case FAMILY_DRUID:         return GET_RAND_ELEM(druid_names);
    case FAMILY_ELF:           return GET_RAND_ELEM(elf_names);
    case FAMILY_MAGE:          return GET_RAND_ELEM(mage_names);
    case FAMILY_SOLDIER:       return GET_RAND_ELEM(soldier_names);
    case FAMILY_THIEF:         return GET_RAND_ELEM(thief_names);
    case FAMILY_ARCHMAGE:      return GET_RAND_ELEM(mage_names);
    case FAMILY_ORC:           return GET_RAND_ELEM(orc_names);
    case FAMILY_BIG_ORC:       return GET_RAND_ELEM(orc_names);
    case FAMILY_BARBARIAN:     return GET_RAND_ELEM(barbarian_names);
    case FAMILY_FIREELEMENTAL: return GET_RAND_ELEM(elemental_names);
    case FAMILY_SKELETON:      return GET_RAND_ELEM(skeleton_names);
    case FAMILY_SLIME:
    case FAMILY_MEDIUM_SLIME:
    case FAMILY_SMALL_SLIME:   return GET_RAND_ELEM(slime_names);
    case FAMILY_FAERIE:        return GET_RAND_ELEM(faerie_names);
    case FAMILY_GHOST:         return GET_RAND_ELEM(ghost_names);
    default:                   return GET_RAND_ELEM(soldier_names);
    }
}

#undef GET_RAND_ELEM

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
        case 0: save.respawn_mode = 1; break;
        case 1: save.respawn_mode = 2; break;
        default: save.respawn_mode = 0; break;
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
        if (!member)
            continue;
        const short team = member->teamnum;
        if (team <= 0 || team >= MAX_PLAYERS)
            continue;
        if (std::find(teams.begin(), teams.end(), team) == teams.end())
            teams.push_back(team);
    }

    const short preferred = save.my_team;
    if (preferred >= 0 && preferred < MAX_PLAYERS &&
        team_has_members(save, preferred))
    {
        teams.erase(std::remove(teams.begin(), teams.end(), preferred),
                    teams.end());
        teams.insert(teams.begin(), preferred);
    }
    return teams;
}

og::data::CompanyAutosaveContext company_autosave_context(
    const SaveData& save, bool networked_lobby_active)
{
    og::data::CompanyAutosaveContext context;
    context.networked_lobby = networked_lobby_active;
    if (!networked_lobby_active)
        return context;

    // Owned wallets = every m_totalcash index this machine's mutations spend
    // from. In a networked lobby the in-memory roster is private to this
    // machine (apply_lobby_state_to_save stamps session teams onto LOCAL
    // members only), so its in-range teamnums are the machine's seats;
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
                                                bool networked_lobby_active)
{
    return og::data::company_autosave(
        save,
        og::data::CompanyAutosaveKind::BaseCampMutation,
        company_autosave_context(save, networked_lobby_active));
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
    return is_allied_mode(save) ? "PVP: Ally" : "PVP: Enemy";
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
    if (save.respawn_mode <= 0)
        return "Respawns: Off";
    if (save.respawn_mode == 1)
        return "Respawns: Heroes";
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

std::string format_company_file_preview(const std::string& display_name)
{
    return std::format("file: {}.gtl",
                       og::data::derive_company_slot(display_name));
}

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

ContinueResult open_most_recent_company(SaveData& save)
{
    // §2.1: CONTINUE opens the most-recent company (WP2 startup selection).
    const std::string slot = og::data::select_startup_company();
    if (slot.empty())
        return ContinueResult::NoCompany;  // CONTINUE is gated hidden anyway.
    return open_company_slot(save, slot);
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
        if (recruit_->strength < fd->base_stats[0])
            recruit_->strength = static_cast<short>(fd->base_stats[0]);
        if (recruit_->dexterity < fd->base_stats[1])
            recruit_->dexterity = static_cast<short>(fd->base_stats[1]);
        if (recruit_->constitution < fd->base_stats[2])
            recruit_->constitution = static_cast<short>(fd->base_stats[2]);
        if (recruit_->intelligence < fd->base_stats[3])
            recruit_->intelligence = static_cast<short>(fd->base_stats[3]);
        if (recruit_->armor < fd->base_stats[4])
            recruit_->armor = static_cast<short>(fd->base_stats[4]);
        if (recruit_->level < fd->base_stats[5])
            recruit_->upgrade_to_level(static_cast<short>(fd->base_stats[5]));
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
    if (is_allied_mode(save))
        return {0};
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
    report.your_team = is_allied_mode(save) ? 0 : save.my_team;

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
            // Score teams get their color name (matching the TEAMS screen
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
