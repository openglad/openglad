/* Shared picker business logic.
 *
 * Cost calculations, name generation, team management, and constants
 * used by both the SDL and text picker clients.
 */
#pragma once

#include <openglad/core/constants.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

class guy;
class SaveData;

namespace og::ui {

// --- Constants ---

inline constexpr float kStatCostExponent = 1.85f;

inline constexpr std::array<int, 14> kAllowableGuys = {
    FAMILY_SOLDIER,
    FAMILY_BARBARIAN,
    FAMILY_ELF,
    FAMILY_ARCHER,
    FAMILY_MAGE,
    FAMILY_CLERIC,
    FAMILY_THIEF,
    FAMILY_DRUID,
    FAMILY_ORC,
    FAMILY_SKELETON,
    FAMILY_FIREELEMENTAL,
    FAMILY_SMALL_SLIME,
    FAMILY_FAERIE,
    FAMILY_GHOST,
};

inline constexpr int kNewGameStartingGold = 5000;

extern const char* const kDifficultyNames[DIFFICULTY_SETTINGS];

// --- Family display helpers ---

// Full display name from FamilyDescriptor (e.g. "SOLDIER", "ORC CAPTAIN").
// Returns "BEAST" for unknown families.
const char* family_display_name(int family);

// Short label for picker UI (e.g. "SOLDIER", "BARBAR.", "ORC CAP.").
// Returns "BEAST" for unlisted families.
const char* family_short_name(short family);

// --- Cost calculations ---

// Base hiring cost for a family (from FamilyDescriptor::hiring_cost).
int family_hiring_base_cost(int family);

// Total hire cost including stat upgrades above base and level XP.
// Pure function: does not mutate the guy.
std::uint32_t calculate_hire_cost(const guy& recruit);

// Training cost: difference between current stats and original stats.
// Pure function: does not mutate either guy.
std::uint32_t calculate_train_cost(const guy& current, const guy& original);

// --- Name generation ---

// Return a random name appropriate for the given family.
const char* get_random_name(unsigned char family);

// Return a unique name not already in the save's team list.
std::string get_unique_name(unsigned char family, const SaveData& save);

// --- Team queries ---

// Count how many team members of the given family exist in save.
int count_family_members(int family, const SaveData& save);

// --- Team operations ---

// Add a recruit to save's team list. Returns slot index or -1 if full.
int add_recruit_to_team(SaveData& save, std::unique_ptr<guy> recruit, int team_num);

// Create a fresh recruit of the given family with a unique name.
std::unique_ptr<guy> create_recruit(int family, int team_num, const SaveData& save);

// Reset save data for a new game: clear team, reset gold.
void reset_for_new_game(SaveData& save);

} // namespace og::ui
