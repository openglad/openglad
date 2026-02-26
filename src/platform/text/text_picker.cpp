/* Text-based picker implementation for the headless client.
 *
 * Provides a line-oriented stdin/stdout menu interface that drives
 * the shared picker state machine. Uses SaveData as the backing store
 * and shared business logic from picker_common.h.
 */

#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/platform/io_common.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/interface/ui/text_protocol.h>

#include <cstdio>
#include <format>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <vector>


// Access current_difficulty through the headless session storage (defined in main.cpp).
#include <openglad/platform/game_session.h>

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
        reset_for_new_game(save_data_);
        ensure_team_populated(save_data_);

        sync_config_from_save();
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
        save_data_.current_campaign = config_.campaign;
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
        sync_config_from_save();

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
        const SaveDataIoError io = save_data_.load_with_error(config_.save_name);
        if (io != SaveDataIoError::None) {
            set_error(TextPickerErrorCode::LoadIoError,
                std::string("load failed: ") + save_error_string(io));
            std::printf("Load failed for '%s' (%s).\n",
                config_.save_name.c_str(), save_error_string(io));
            return false;
        }

        config_.campaign = save_data_.current_campaign;
        config_.level = save_data_.scen_num > 0 ? save_data_.scen_num : 1;

        ensure_team_populated(save_data_);

        sync_config_from_save();

        std::printf("Loaded '%s' (campaign=%s level=%d team=%d gold=%u).\n",
            config_.save_name.c_str(), config_.campaign.c_str(),
            config_.level, static_cast<int>(save_data_.team_size),
            static_cast<unsigned>(save_data_.m_totalcash[0]));
        clear_error();
        return true;
    }

    bool save_game() override
    {
        ensure_team_initialized();
        save_data_.current_campaign = config_.campaign;
        save_data_.scen_num = static_cast<short>(config_.level);
        save_data_.numplayers = 1;

        const SaveDataIoError io = save_data_.save_with_error(config_.save_name);
        if (io != SaveDataIoError::None) {
            set_error(TextPickerErrorCode::SaveIoError,
                std::string("save failed: ") + save_error_string(io));
            std::printf("Save failed for '%s' (%s).\n",
                config_.save_name.c_str(), save_error_string(io));
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
        if (save_data_.team_size == 0) {
            std::printf("(empty)\n");
        } else {
            bool first = true;
            for_each_team_member(save_data_, [&](int /*slot*/, const guy& member) {
                if (!first)
                    std::printf(", ");
                std::printf("%s (%s)", member.name.c_str(),
                    family_display_name(member.family));
                first = false;
            });
            std::printf("\n");
        }

        std::printf("Gold: %u\n", static_cast<unsigned>(save_data_.m_totalcash[0]));
        if (start_team_build_in_hire_mode_) {
            std::printf("[New game: hire and train your team before GO!]\n");
            start_team_build_in_hire_mode_ = false;
        }
    }

    std::string menu_item_label(const PickerMenuItem& item) const
    {
        if (item.command == PickerMenuCommand::SetDifficulty)
            return format_difficulty_label(og::runtime::current_session->current_difficulty_);
        if (item.command == PickerMenuCommand::SetLevel)
            return std::format("{} ({})", item.label, config_.level);
        if (item.command == PickerMenuCommand::SetCampaign)
            return std::format("{} ({})", item.label, config_.campaign);
        if (item.command == PickerMenuCommand::ToggleAlliedMode)
            return format_allied_mode_label(save_data_);
        return std::string(item.label);
    }

    void handle_main_menu_item(const PickerMenuItem& item)
    {
        switch (item.command) {
        case PickerMenuCommand::SetDifficulty:
            og::runtime::current_session->current_difficulty_ = cycle_difficulty(og::runtime::current_session->current_difficulty_);
            std::printf("Difficulty set to %s.\n",
                kDifficultyNames[og::runtime::current_session->current_difficulty_]);
            break;
        case PickerMenuCommand::SetPlayerMode:
            set_player_count(save_data_, item.arg);
            std::printf("Player mode set to %d.\n", item.arg);
            break;
        case PickerMenuCommand::ToggleAlliedMode:
            toggle_allied_mode(save_data_);
            std::printf("PVP mode set to %s.\n",
                is_allied_mode(save_data_) ? "Allied" : "Enemy");
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
        if (config_.team_families.empty())
            config_.team_families.push_back(FAMILY_SOLDIER);
        initialize_starting_team(save_data_, config_.team_families);
    }

    void sync_config_from_save()
    {
        config_.team_families = collect_team_families(save_data_);
    }

    void view_team_roster()
    {
        std::printf("\n--- Team Roster ---\n");
        if (save_data_.team_size == 0) {
            std::printf("(empty)\n");
            wait_for_enter();
            return;
        }

        int idx = 1;
        for_each_team_member(save_data_, [&](int /*slot*/, const guy& member) {
            std::printf("%2d. %-14s Family=%-14s L=%d STR=%d DEX=%d CON=%d INT=%d ARM=%d\n",
                idx++,
                member.name.c_str(),
                family_display_name(member.family),
                member.level,
                member.strength,
                member.dexterity,
                member.constitution,
                member.intelligence,
                member.armor);
        });

        wait_for_enter();
    }

    void train_team()
    {
        TrainSession session(save_data_);
        if (session.empty()) {
            std::printf("No team members available to train.\n");
            return;
        }

        using S = TrainSession::Stat;
        std::string line;

        for (;;) {
            const guy& w = session.working_copy();
            const guy& o = session.original();
            std::printf("\n--- Train: %s (%s) ---\n",
                w.name.c_str(), family_display_name(w.family));
            std::printf("        Current  Original\n");
            std::printf("  1.STR:  %5d    %5d%s\n", w.strength, o.strength,
                session.level_increased() ? " [locked]" : "");
            std::printf("  2.DEX:  %5d    %5d%s\n", w.dexterity, o.dexterity,
                session.level_increased() ? " [locked]" : "");
            std::printf("  3.CON:  %5d    %5d%s\n", w.constitution, o.constitution,
                session.level_increased() ? " [locked]" : "");
            std::printf("  4.INT:  %5d    %5d%s\n", w.intelligence, o.intelligence,
                session.level_increased() ? " [locked]" : "");
            std::printf("  5.ARM:  %5d    %5d%s\n", w.armor, o.armor,
                session.level_increased() ? " [locked]" : "");
            std::printf("  6.LVL:  %5d    %5d%s\n", w.level, o.level,
                session.stats_increased() ? " [locked]" : "");
            std::printf("Cost: %u  |  Gold: %u\n",
                static_cast<unsigned>(session.current_cost()),
                static_cast<unsigned>(save_data_.m_totalcash[0]));
            std::printf("[+1..6] increase  [-1..-6] decrease\n");
            std::printf("[A]ccept  [N]ext  [P]rev  [B]ack: ");
            std::fflush(stdout);

            if (!read_line(line) || line.empty())
                continue;

            char c = line[0];
            if (c == 'b' || c == 'B')
                return;
            if (c == 'n' || c == 'N') { session.next_member(); continue; }
            if (c == 'p' || c == 'P') { session.prev_member(); continue; }
            if (c == 'a' || c == 'A') {
                if (session.accept())
                    std::printf("Training accepted.\n");
                else
                    std::printf("Can't afford training.\n");
                continue;
            }

            const auto val = parse_int_strict(line);
            if (val) {
                S stats[] = {S::Strength, S::Dexterity, S::Constitution,
                             S::Intelligence, S::Armor, S::Level};
                int idx = std::abs(*val) - 1;
                if (idx >= 0 && idx < 6) {
                    if (*val > 0) session.increase_stat(stats[idx]);
                    else session.decrease_stat(stats[idx]);
                }
            }
        }
    }

    void hire_troops()
    {
        HireSession session(save_data_, 0);
        if (session.team_full()) {
            std::printf("Team is already at max size (%d).\n", MAX_TEAM_SIZE);
            return;
        }

        std::string line;

        for (;;) {
            const guy* r = session.current_recruit();
            if (!r)
                break;
            std::printf("\n--- Hire: %s (%d/%d) ---\n",
                family_display_name(r->family),
                session.family_index() + 1,
                static_cast<int>(kAllowableGuys.size()));
            std::printf("Name: %s\n", r->name.c_str());
            std::printf("STR: %d  DEX: %d  CON: %d  INT: %d  ARM: %d  LVL: %d\n",
                r->strength, r->dexterity, r->constitution,
                r->intelligence, r->armor, r->level);
            std::printf("Cost: %u  |  Gold: %u\n",
                static_cast<unsigned>(session.current_cost()),
                static_cast<unsigned>(save_data_.m_totalcash[0]));
            std::printf("[N]ext  [P]rev  [H]ire  [B]ack: ");
            std::fflush(stdout);

            if (!read_line(line) || line.empty())
                continue;

            char c = line[0];
            if (c == 'b' || c == 'B')
                return;
            if (c == 'n' || c == 'N') { session.next_family(); continue; }
            if (c == 'p' || c == 'P') { session.prev_family(); continue; }
            if (c == 'h' || c == 'H') {
                int slot = session.hire();
                if (slot < 0) {
                    std::printf("Can't hire (not enough gold or team full).\n");
                    continue;
                }
                std::printf("Hired %s!\n", save_data_.team_list[slot]->name.c_str());
                sync_config_from_save();
                if (session.team_full()) {
                    std::printf("Team is now full.\n");
                    return;
                }
            }
        }
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
        save_data_.scen_num = static_cast<short>(*level);
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
    SaveData save_data_;
    bool start_team_build_in_hire_mode_ = false;
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
