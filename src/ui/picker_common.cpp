/* Shared picker business logic implementation.
 *
 * Extracted from picker.cpp, picker_team_build.cpp, and text_picker.cpp
 * so both SDL and text clients call the same code.
 */

#include <openglad/ui/picker_common.h>
#include <openglad/data/save_data.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/guy.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

// Defined in entities/guy.cpp
std::uint32_t calculate_exp(std::int32_t level);

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
    switch (family) {
    case FAMILY_ARCHER:       return "ARCHER";
    case FAMILY_CLERIC:       return "CLERIC";
    case FAMILY_DRUID:        return "DRUID";
    case FAMILY_ELF:          return "ELF";
    case FAMILY_MAGE:         return "MAGE";
    case FAMILY_SOLDIER:      return "SOLDIER";
    case FAMILY_THIEF:        return "THIEF";
    case FAMILY_ARCHMAGE:     return "ARCHMAGE";
    case FAMILY_ORC:          return "ORC";
    case FAMILY_BIG_ORC:      return "ORC CAP.";
    case FAMILY_BARBARIAN:    return "BARBAR.";
    case FAMILY_FIREELEMENTAL:return "ELEMENT.";
    case FAMILY_SKELETON:     return "SKELTON";
    case FAMILY_SMALL_SLIME:  return "SLIME";
    case FAMILY_FAERIE:       return "FAERIE";
    case FAMILY_GHOST:        return "GHOST";
    default:                  return "BEAST";
    }
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
    for (int i = 0; i < save.team_size; i++) {
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
}

} // namespace og::ui
