/* Text-based picker implementation for the headless client.
 *
 * Provides a line-oriented stdin/stdout menu interface that drives
 * the shared picker state machine.
 */

#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/data/save_data.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/guy.h>
#include <openglad/platform/io_common.h>
#include <openglad/ui/menu_model.h>
#include <openglad/ui/picker.h>
#include <openglad/ui/picker_state.h>
#include <openglad/ui/text_protocol.h>

#include <array>
#include <cstdio>
#include <format>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <vector>

extern std::int32_t current_difficulty;

namespace og::ui {
namespace {

constexpr std::array<const char*, DIFFICULTY_SETTINGS> kDifficultyNames = {
    "Skirmish",
    "Battle",
    "Slaughter",
};

constexpr std::array<int, 14> kAllowableGuys = {
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

bool read_line(std::string& out)
{
    if (!std::getline(std::cin, out))
        return false;
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
        out.pop_back();
    return true;
}

const char* family_display_name(int family)
{
    switch (family) {
    case FAMILY_SOLDIER: return "Soldier";
    case FAMILY_BARBARIAN: return "Barbarian";
    case FAMILY_ELF: return "Elf";
    case FAMILY_ARCHER: return "Archer";
    case FAMILY_MAGE: return "Mage";
    case FAMILY_CLERIC: return "Cleric";
    case FAMILY_THIEF: return "Thief";
    case FAMILY_DRUID: return "Druid";
    case FAMILY_ORC: return "Orc";
    case FAMILY_SKELETON: return "Skeleton";
    case FAMILY_FIREELEMENTAL: return "Fire Elemental";
    case FAMILY_SMALL_SLIME: return "Slime";
    case FAMILY_FAERIE: return "Faerie";
    case FAMILY_GHOST: return "Ghost";
    default: {
        const FamilyDescriptor* fd = get_family_descriptor(family);
        return fd ? fd->name : "Unknown";
    }
    }
}

std::string save_error_string(SaveDataIoError error)
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

int family_hiring_cost(int family)
{
    const FamilyDescriptor* fd = get_family_descriptor(family);
    return fd ? static_cast<int>(fd->hiring_cost) : 0;
}

void wait_for_enter()
{
    std::printf("Press Enter to continue...");
    std::fflush(stdout);
    std::string line;
    (void)read_line(line);
}

} // namespace

int run_text_picker_protocol_session(const TextPickerConfig& config)
{
    TextProtocolArgs protocol_args;
    protocol_args.campaign = config.campaign;
    protocol_args.level = config.level;
    protocol_args.team_families = config.team_families;
    protocol_args.seed = config.seed;
    return run_text_protocol_session(protocol_args);
}

class TextPickerClient final : public IPickerClient
{
public:
    explicit TextPickerClient(TextPickerConfig& config, TextPickerError* error)
        : config_(config), error_(error)
    {
        ensure_team_initialized();
    }

    const PickerMenuItem* present_menu(PickerMenuId menu_id) override
    {
        const PickerMenuDefinition& menu = picker_menu_definition(menu_id);
        for (;;) {
            ensure_team_initialized();
            print_menu_context(menu_id);

            std::printf("\n=== %s ===\n", std::string(menu.title).c_str());
            for (size_t i = 0; i < menu.items.size(); ++i) {
                std::printf("  %2zu. %s\n", i + 1, menu_item_label(menu.items[i]).c_str());
            }
            std::printf("Choice: ");
            std::fflush(stdout);

            std::string line;
            if (!read_line(line)) {
                if (menu_id == PickerMenuId::Main)
                    return find_picker_menu_item(menu_id, PickerMenuCommand::Quit);
                return find_picker_menu_item(menu_id, PickerMenuCommand::Back);
            }

            const auto choice = parse_int_strict(line);
            if (!choice || *choice < 1 || static_cast<size_t>(*choice) > menu.items.size()) {
                std::printf("Invalid choice.\n");
                continue;
            }

            return &menu.items[static_cast<size_t>(*choice - 1)];
        }
    }

    void handle_menu_item(PickerMenuId menu_id, const PickerMenuItem& item) override
    {
        if (menu_id == PickerMenuId::Main) {
            handle_main_menu_item(item);
            return;
        }
        handle_team_build_item(item);
    }

    bool prepare_new_game() override
    {
        team_roster_.clear();
        team_gold_ = 5000;

        guy starter(FAMILY_SOLDIER);
        starter.name = "Soldier1";
        team_roster_.push_back(starter);

        sync_team_families_from_roster();
        start_team_build_in_hire_mode_ = true;
        return true;
    }

    std::string show_campaign_select() override
    {
        std::list<std::string> campaigns = list_campaigns();
        std::vector<std::string> entries(campaigns.begin(), campaigns.end());

        if (entries.empty()) {
            set_error(TextPickerErrorCode::CampaignIoError, "no campaigns found");
            std::printf("No campaigns found; keeping '%s'.\n", config_.campaign.c_str());
            return config_.campaign;
        }

        std::printf("\n--- Campaign Select ---\n");
        for (size_t i = 0; i < entries.size(); ++i)
            std::printf("  %zu. %s\n", i + 1, entries[i].c_str());
        std::printf("Select campaign [1-%zu] (blank keeps current): ", entries.size());
        std::fflush(stdout);

        std::string line;
        if (!read_line(line))
            return config_.campaign;
        if (line.empty())
            return config_.campaign;

        const auto choice = parse_int_strict(line);
        if (!choice || *choice < 1 || static_cast<size_t>(*choice) > entries.size()) {
            std::printf("Invalid campaign selection.\n");
            return config_.campaign;
        }

        config_.campaign = entries[static_cast<size_t>(*choice - 1)];
        return config_.campaign;
    }

    void show_options() override
    {
        std::string line;
        std::printf("\n--- Options ---\n");

        std::printf("Current save slot: %s. New slot (blank keeps current): ", config_.save_name.c_str());
        std::fflush(stdout);
        if (!read_line(line))
            return;
        if (!line.empty())
            config_.save_name = line;

        std::printf("Current seed: %u. New seed (blank keeps current): ",
            static_cast<unsigned>(config_.seed));
        std::fflush(stdout);
        if (!read_line(line))
            return;
        if (!line.empty()) {
            const auto value = parse_int_strict(line);
            if (!value || *value < 0) {
                std::printf("Invalid seed.\n");
            } else {
                config_.seed = static_cast<std::uint32_t>(*value);
            }
        }
    }

    void show_help() override
    {
        std::printf("\n--- Help ---\n");
        std::printf("Begin new game resets your team and enters Team Build.\n");
        std::printf("Continue game opens Team Build. Use GO! there to start playing.\n");
        std::printf("Use --protocol for machine-driven JSON protocol mode.\n");
    }

    void run_game() override
    {
        ensure_team_initialized();
        sync_team_families_from_roster();

        const int result = run_text_picker_protocol_session(config_);
        if (result != 0) {
            set_error(TextPickerErrorCode::Unsupported,
                std::string("protocol session failed with code ") + std::to_string(result));
        }
    }

    PickerScreen screen_after_game() const override
    {
        return PickerScreen::TeamBuild;
    }

    bool load_game() override
    {
        SaveData loaded;
        const SaveDataIoError io = loaded.load_with_error(config_.save_name);
        if (io != SaveDataIoError::None) {
            set_error(TextPickerErrorCode::LoadIoError,
                std::string("load failed: ") + save_error_string(io));
            std::printf("Load failed for '%s' (%s).\n",
                config_.save_name.c_str(), save_error_string(io).c_str());
            return false;
        }

        config_.campaign = loaded.current_campaign;
        config_.level = loaded.scen_num > 0 ? loaded.scen_num : 1;

        team_gold_ = static_cast<int>(loaded.m_totalcash[0]);
        team_roster_.clear();
        for (size_t i = 0; i < loaded.team_size; ++i) {
            if (loaded.team_list[i])
                team_roster_.push_back(*loaded.team_list[i]);
        }

        if (team_roster_.empty()) {
            guy starter(FAMILY_SOLDIER);
            starter.name = "Soldier1";
            team_roster_.push_back(starter);
        }

        sync_team_families_from_roster();

        std::printf("Loaded '%s' (campaign=%s level=%d team=%zu gold=%d).\n",
            config_.save_name.c_str(), config_.campaign.c_str(),
            config_.level, team_roster_.size(), team_gold_);
        clear_error();
        return true;
    }

    bool save_game() override
    {
        ensure_team_initialized();
        sync_team_families_from_roster();

        SaveData save;
        save.current_campaign = config_.campaign;
        save.scen_num = static_cast<short>(config_.level);
        save.numplayers = 1;
        save.totalcash = static_cast<std::uint32_t>(team_gold_);
        save.m_totalcash[0] = static_cast<std::uint32_t>(team_gold_);

        save.team_size = 0;
        for (size_t i = 0; i < team_roster_.size() && i < MAX_TEAM_SIZE; ++i) {
            save.team_list[i] = std::make_unique<guy>(team_roster_[i]);
            ++save.team_size;
        }

        const SaveDataIoError io = save.save_with_error(config_.save_name);
        if (io != SaveDataIoError::None) {
            set_error(TextPickerErrorCode::SaveIoError,
                std::string("save failed: ") + save_error_string(io));
            std::printf("Save failed for '%s' (%s).\n",
                config_.save_name.c_str(), save_error_string(io).c_str());
            return false;
        }

        std::printf("Saved '%s'.\n", config_.save_name.c_str());
        clear_error();
        return true;
    }

private:
    void print_menu_context(PickerMenuId menu_id)
    {
        if (menu_id != PickerMenuId::TeamBuild)
            return;

        std::printf("\nTeam: ");
        if (team_roster_.empty()) {
            std::printf("(empty)\n");
        } else {
            for (size_t i = 0; i < team_roster_.size(); ++i) {
                if (i > 0)
                    std::printf(", ");
                std::printf("%s (%s)", team_roster_[i].name.c_str(),
                    family_display_name(team_roster_[i].family));
            }
            std::printf("\n");
        }

        std::printf("Gold: %d\n", team_gold_);
        if (start_team_build_in_hire_mode_) {
            std::printf("[New game: hire and train your team before GO!]\n");
            start_team_build_in_hire_mode_ = false;
        }
    }

    std::string menu_item_label(const PickerMenuItem& item) const
    {
        if (item.command == PickerMenuCommand::SetDifficulty) {
            const int difficulty_idx = current_difficulty >= 0
                ? (current_difficulty % DIFFICULTY_SETTINGS)
                : 0;
            return std::format("{}: {}", item.label, kDifficultyNames[static_cast<size_t>(difficulty_idx)]);
        }
        if (item.command == PickerMenuCommand::SetLevel)
            return std::format("{} ({})", item.label, config_.level);
        if (item.command == PickerMenuCommand::SetCampaign)
            return std::format("{} ({})", item.label, config_.campaign);
        if (item.command == PickerMenuCommand::ToggleAlliedMode)
            return std::format("{}: {}", item.label, allied_mode_ ? "Allied" : "Enemy");
        return std::string(item.label);
    }

    void handle_main_menu_item(const PickerMenuItem& item)
    {
        switch (item.command) {
        case PickerMenuCommand::SetDifficulty: {
            const int difficulty_idx = current_difficulty >= 0
                ? (current_difficulty % DIFFICULTY_SETTINGS)
                : 0;
            current_difficulty = (difficulty_idx + 1) % DIFFICULTY_SETTINGS;
            std::printf("Difficulty set to %s.\n",
                kDifficultyNames[static_cast<size_t>(current_difficulty)]);
            break;
        }
        case PickerMenuCommand::SetPlayerMode:
            player_mode_ = item.arg;
            std::printf("Player mode set to %d.\n", player_mode_);
            break;
        case PickerMenuCommand::ToggleAlliedMode:
            allied_mode_ = !allied_mode_;
            std::printf("PVP mode set to %s.\n", allied_mode_ ? "Allied" : "Enemy");
            break;
        case PickerMenuCommand::LevelEdit:
            std::printf("Level Edit is not available in the headless text client.\n");
            break;
        default:
            break;
        }
    }

    void handle_team_build_item(const PickerMenuItem& item)
    {
        switch (item.command) {
        case PickerMenuCommand::ViewTeam:
            view_team_roster();
            break;
        case PickerMenuCommand::TrainTeam:
            train_team();
            break;
        case PickerMenuCommand::HireTroops:
            hire_troops();
            break;
        case PickerMenuCommand::LoadTeam:
            (void)load_game();
            break;
        case PickerMenuCommand::SaveTeam:
            (void)save_game();
            break;
        case PickerMenuCommand::ShowProgress:
            std::printf("Current campaign progress: campaign=%s level=%d.\n",
                config_.campaign.c_str(), config_.level);
            break;
        case PickerMenuCommand::SetLevel:
            set_level();
            break;
        case PickerMenuCommand::SetCampaign:
            (void)show_campaign_select();
            break;
        default:
            break;
        }
    }

    void ensure_team_initialized()
    {
        if (!team_roster_.empty())
            return;

        if (config_.team_families.empty())
            config_.team_families.push_back(FAMILY_SOLDIER);

        for (size_t i = 0; i < config_.team_families.size(); ++i) {
            const int family = config_.team_families[i];
            guy g(family);
            if (g.name.empty()) {
                g.name = std::format("{}{}", family_display_name(family), i + 1);
            } else {
                g.name = std::format("{}{}", family_display_name(family), i + 1);
            }
            team_roster_.push_back(g);
        }

        if (team_roster_.empty()) {
            guy starter(FAMILY_SOLDIER);
            starter.name = "Soldier1";
            team_roster_.push_back(starter);
        }

        sync_team_families_from_roster();
    }

    void sync_team_families_from_roster()
    {
        config_.team_families.clear();
        for (const guy& member : team_roster_)
            config_.team_families.push_back(static_cast<int>(member.family));
    }

    size_t family_count(int family) const
    {
        size_t count = 0;
        for (const guy& member : team_roster_) {
            if (member.family == family)
                ++count;
        }
        return count;
    }

    void view_team_roster()
    {
        std::printf("\n--- Team Roster ---\n");
        if (team_roster_.empty()) {
            std::printf("(empty)\n");
            wait_for_enter();
            return;
        }

        for (size_t i = 0; i < team_roster_.size(); ++i) {
            const guy& member = team_roster_[i];
            std::printf("%2zu. %-14s Family=%-14s L=%d STR=%d DEX=%d CON=%d INT=%d ARM=%d\n",
                i + 1,
                member.name.c_str(),
                family_display_name(member.family),
                member.level,
                member.strength,
                member.dexterity,
                member.constitution,
                member.intelligence,
                member.armor);
        }

        wait_for_enter();
    }

    void train_team()
    {
        if (team_roster_.empty()) {
            std::printf("No team members available to train.\n");
            return;
        }

        std::printf("\n--- Train Team ---\n");
        for (size_t i = 0; i < team_roster_.size(); ++i) {
            const guy& member = team_roster_[i];
            std::printf("  %zu. %s (%s, L%d)\n",
                i + 1,
                member.name.c_str(),
                family_display_name(member.family),
                member.level);
        }
        std::printf("Choose member [1-%zu] (blank cancels): ", team_roster_.size());
        std::fflush(stdout);

        std::string line;
        if (!read_line(line) || line.empty())
            return;

        const auto pick = parse_int_strict(line);
        if (!pick || *pick < 1 || static_cast<size_t>(*pick) > team_roster_.size()) {
            std::printf("Invalid member selection.\n");
            return;
        }

        guy& member = team_roster_[static_cast<size_t>(*pick - 1)];
        const FamilyDescriptor* fd = get_family_descriptor(member.family);
        if (!fd) {
            std::printf("Unable to train this family.\n");
            return;
        }

        for (;;) {
            std::printf("\nTraining %s (%s) | Gold: %d\n",
                member.name.c_str(), family_display_name(member.family), team_gold_);
            std::printf("  1. Strength     (%d)  +1 cost %d\n", member.strength, static_cast<int>(fd->stat_costs[0]));
            std::printf("  2. Dexterity    (%d)  +1 cost %d\n", member.dexterity, static_cast<int>(fd->stat_costs[1]));
            std::printf("  3. Constitution (%d)  +1 cost %d\n", member.constitution, static_cast<int>(fd->stat_costs[2]));
            std::printf("  4. Intelligence (%d)  +1 cost %d\n", member.intelligence, static_cast<int>(fd->stat_costs[3]));
            std::printf("  5. Armor        (%d)  +1 cost %d\n", member.armor, static_cast<int>(fd->stat_costs[4]));
            std::printf("  6. Back\n");
            std::printf("Choice: ");
            std::fflush(stdout);

            if (!read_line(line))
                return;

            const auto choice = parse_int_strict(line);
            if (!choice) {
                std::printf("Invalid choice.\n");
                continue;
            }
            if (*choice == 6)
                return;

            short* target_stat = nullptr;
            int cost = 0;
            switch (*choice) {
            case 1:
                target_stat = &member.strength;
                cost = static_cast<int>(fd->stat_costs[0]);
                break;
            case 2:
                target_stat = &member.dexterity;
                cost = static_cast<int>(fd->stat_costs[1]);
                break;
            case 3:
                target_stat = &member.constitution;
                cost = static_cast<int>(fd->stat_costs[2]);
                break;
            case 4:
                target_stat = &member.intelligence;
                cost = static_cast<int>(fd->stat_costs[3]);
                break;
            case 5:
                target_stat = &member.armor;
                cost = static_cast<int>(fd->stat_costs[4]);
                break;
            default:
                std::printf("Invalid choice.\n");
                break;
            }

            if (!target_stat)
                continue;
            if (cost <= 0 || team_gold_ < cost) {
                std::printf("Not enough gold.\n");
                continue;
            }

            team_gold_ -= cost;
            ++(*target_stat);
            std::printf("Upgraded successfully.\n");
        }
    }

    void hire_troops()
    {
        if (team_roster_.size() >= MAX_TEAM_SIZE) {
            std::printf("Team is already at max size (%d).\n", MAX_TEAM_SIZE);
            return;
        }

        std::printf("\n--- Hire Troops ---\n");
        for (size_t i = 0; i < kAllowableGuys.size(); ++i) {
            const int family = kAllowableGuys[i];
            std::printf("  %2zu. %-14s Cost %d\n",
                i + 1,
                family_display_name(family),
                family_hiring_cost(family));
        }
        std::printf("Gold: %d\n", team_gold_);
        std::printf("Choose troop [1-%zu] (blank cancels): ", kAllowableGuys.size());
        std::fflush(stdout);

        std::string line;
        if (!read_line(line) || line.empty())
            return;

        const auto choice = parse_int_strict(line);
        if (!choice || *choice < 1 || static_cast<size_t>(*choice) > kAllowableGuys.size()) {
            std::printf("Invalid troop selection.\n");
            return;
        }

        const int family = kAllowableGuys[static_cast<size_t>(*choice - 1)];
        const int cost = family_hiring_cost(family);
        if (cost <= 0) {
            std::printf("That troop cannot be hired.\n");
            return;
        }
        if (team_gold_ < cost) {
            std::printf("Not enough gold.\n");
            return;
        }

        team_gold_ -= cost;
        guy recruit(family);
        recruit.name = std::format("{}{}", family_display_name(family), family_count(family) + 1);
        team_roster_.push_back(recruit);
        sync_team_families_from_roster();

        std::printf("Hired %s for %d gold.\n", recruit.name.c_str(), cost);
    }

    void set_level()
    {
        std::printf("Set level (current %d): ", config_.level);
        std::fflush(stdout);

        std::string line;
        if (!read_line(line) || line.empty())
            return;

        const auto level = parse_int_strict(line);
        if (!level || *level < 1) {
            std::printf("Invalid level.\n");
            return;
        }

        config_.level = *level;
    }

    void clear_error()
    {
        if (error_)
            *error_ = {};
    }

    void set_error(TextPickerErrorCode code, std::string detail)
    {
        if (!error_)
            return;
        error_->code = code;
        error_->detail = std::move(detail);
    }

    TextPickerConfig& config_;
    TextPickerError* error_ = nullptr;
    std::vector<guy> team_roster_;
    int team_gold_ = 5000;
    bool start_team_build_in_hire_mode_ = false;
    int player_mode_ = 1;
    bool allied_mode_ = false;
};

void run_text_picker(TextPickerConfig& config, TextPickerError* error)
{
    if (config.team_families.empty())
        config.team_families.push_back(FAMILY_SOLDIER);
    if (error)
        *error = {};

    TextPickerClient client(config, error);
    run_picker(client);
}

} // namespace og::ui
