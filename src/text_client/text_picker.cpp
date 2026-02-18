/* Text-based picker implementation for the headless client.
 *
 * Provides a line-oriented stdin/stdout menu interface that drives
 * the shared picker state machine.
 */

#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/data/save_data.h>
#include <openglad/entities/guy.h>
#include <openglad/platform/io_common.h>
#include <openglad/ui/picker.h>
#include <openglad/ui/picker_state.h>

#include <cstdio>
#include <iostream>
#include <list>
#include <string>
#include <vector>

namespace og::ui {
namespace {

bool read_line(std::string& out)
{
    if (!std::getline(std::cin, out))
        return false;
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
        out.pop_back();
    return true;
}

void print_team(const TextPickerConfig& config)
{
    std::printf("Team families: ");
    if (config.team_families.empty()) {
        std::printf("(empty)");
    } else {
        for (size_t i = 0; i < config.team_families.size(); ++i) {
            if (i > 0)
                std::printf(",");
            std::printf("%d", config.team_families[i]);
        }
    }
    std::printf("\n");
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

} // namespace

class TextPickerClient final : public IPickerClient
{
public:
    explicit TextPickerClient(TextPickerConfig& config, TextPickerError* error)
        : config_(config), error_(error) {}

    bool play_requested() const { return play_requested_; }

    MainMenuAction show_main_menu() override
    {
        if (play_requested_)
            return MainMenuAction::Quit;

        for (;;) {
            std::printf("\n=== OpenGlad Text Picker ===\n");
            std::printf("Campaign: %s\n", config_.campaign.c_str());
            std::printf("Level: %d\n", config_.level);
            std::printf("Seed: %u\n", static_cast<unsigned>(config_.seed));
            std::printf("Save slot: %s\n", config_.save_name.c_str());
            print_team(config_);
            std::printf("  1. Play\n");
            std::printf("  2. New game setup\n");
            std::printf("  3. Load team\n");
            std::printf("  4. Save team\n");
            std::printf("  5. Team setup\n");
            std::printf("  6. Options\n");
            std::printf("  7. Help\n");
            std::printf("  8. Quit\n");
            std::printf("Choice: ");
            std::fflush(stdout);

            std::string line;
            if (!read_line(line))
                return MainMenuAction::Quit;

            const auto choice = parse_int_strict(line);
            if (!choice) {
                set_error(TextPickerErrorCode::ParseError, "main_menu choice must be an integer");
                std::printf("Invalid choice.\n");
                continue;
            }

            switch (*choice) {
            case 1: return MainMenuAction::ContinueGame;
            case 2: return MainMenuAction::NewGame;
            case 3: return MainMenuAction::LoadGame;
            case 4: return MainMenuAction::SaveGame;
            case 5: return MainMenuAction::ViewTeam;
            case 6: return MainMenuAction::Options;
            case 7: return MainMenuAction::Help;
            case 8: return MainMenuAction::Quit;
            default:
                set_error(TextPickerErrorCode::InvalidSelection, "main_menu choice out of range");
                std::printf("Invalid choice.\n");
                break;
            }
        }
    }

    void show_team_build() override
    {
        for (;;) {
            std::printf("\n--- Team Setup ---\n");
            print_team(config_);
            std::printf("  1. Add family id\n");
            std::printf("  2. Remove last\n");
            std::printf("  3. Reset to default\n");
            std::printf("  4. Back\n");
            std::printf("Choice: ");
            std::fflush(stdout);

            std::string line;
            if (!read_line(line))
                return;
            const auto choice = parse_int_strict(line);
            if (!choice) {
                set_error(TextPickerErrorCode::ParseError, "team menu choice must be an integer");
                std::printf("Invalid choice.\n");
                continue;
            }

            if (*choice == 1) {
                std::printf("Family id to add: ");
                std::fflush(stdout);
                if (!read_line(line))
                    return;
                const auto family = parse_int_strict(line);
                if (!family) {
                    set_error(TextPickerErrorCode::ParseError, "family id must be an integer");
                    std::printf("Invalid family id.\n");
                    continue;
                }
                config_.team_families.push_back(*family);
            } else if (*choice == 2) {
                if (!config_.team_families.empty())
                    config_.team_families.pop_back();
            } else if (*choice == 3) {
                config_.team_families.clear();
                config_.team_families.push_back(FAMILY_SOLDIER);
            } else if (*choice == 4) {
                if (config_.team_families.empty())
                    config_.team_families.push_back(FAMILY_SOLDIER);
                return;
            } else {
                set_error(TextPickerErrorCode::InvalidSelection, "team menu choice out of range");
                std::printf("Invalid choice.\n");
            }
        }
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
            set_error(TextPickerErrorCode::InvalidSelection, "campaign selection out of range");
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
        std::printf("Current level: %d. New level (blank keeps current): ", config_.level);
        std::fflush(stdout);
        if (!read_line(line))
            return;
        if (!line.empty()) {
            const auto value = parse_int_strict(line);
            if (!value || *value < 1) {
                set_error(TextPickerErrorCode::ParseError, "level must be a positive integer");
                std::printf("Invalid level.\n");
            } else {
                config_.level = *value;
            }
        }

        std::printf("Current seed: %u. New seed (blank keeps current): ",
            static_cast<unsigned>(config_.seed));
        std::fflush(stdout);
        if (!read_line(line))
            return;
        if (!line.empty()) {
            const auto value = parse_int_strict(line);
            if (!value || *value < 0) {
                set_error(TextPickerErrorCode::ParseError, "seed must be a non-negative integer");
                std::printf("Invalid seed.\n");
            } else {
                config_.seed = static_cast<std::uint32_t>(*value);
            }
        }

        std::printf("Current save slot: %s. New slot (blank keeps current): ", config_.save_name.c_str());
        std::fflush(stdout);
        if (!read_line(line))
            return;
        if (!line.empty())
            config_.save_name = line;
    }

    void show_help() override
    {
        std::printf("\n--- Help ---\n");
        std::printf("Use this picker to configure campaign, team, and save slot.\n");
        std::printf("Choose Play to launch the text gameplay loop.\n");
        std::printf("Use --protocol for machine-driven JSON protocol mode.\n");
    }

    void run_game() override
    {
        if (config_.team_families.empty())
            config_.team_families.push_back(FAMILY_SOLDIER);
        play_requested_ = true;
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
        config_.team_families.clear();
        for (size_t i = 0; i < loaded.team_size; ++i) {
            if (loaded.team_list[i])
                config_.team_families.push_back(static_cast<int>(loaded.team_list[i]->family));
        }
        if (config_.team_families.empty())
            config_.team_families.push_back(FAMILY_SOLDIER);

        std::printf("Loaded '%s' (campaign=%s level=%d team=%zu).\n",
            config_.save_name.c_str(), config_.campaign.c_str(),
            config_.level, config_.team_families.size());
        clear_error();
        return true;
    }

    bool save_game() override
    {
        SaveData save;
        save.current_campaign = config_.campaign;
        save.scen_num = static_cast<short>(config_.level);
        save.numplayers = 1;
        save.team_size = 0;
        for (size_t i = 0; i < config_.team_families.size() && i < MAX_TEAM_SIZE; ++i) {
            auto g = std::make_unique<guy>();
            g->family = static_cast<char>(config_.team_families[i]);
            g->name = std::format("P{}", i + 1);
            save.team_list[i] = std::move(g);
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
    bool play_requested_ = false;
};

bool run_text_picker(TextPickerConfig& config, TextPickerError* error)
{
    if (config.team_families.empty())
        config.team_families.push_back(FAMILY_SOLDIER);
    if (error)
        *error = {};

    TextPickerClient client(config, error);
    run_picker(client);
    return client.play_requested();
}

} // namespace og::ui
