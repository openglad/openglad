/* Text-based picker implementation for the headless client.
 *
 * Provides a line-oriented stdin/stdout menu interface that drives
 * the shared picker state machine. Uses SaveData as the backing store
 * and shared business logic from picker_common.h.
 */

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/interface/ui/terminal_menu_model.h>
#include <openglad/interface/ui/text_protocol.h>

#include <algorithm>
#include <array>
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
    protocol_args.team_level = config.team_level;
    return run_text_protocol_session(protocol_args);
}

class TextPickerClient final : public IPickerClient
{
public:
    explicit TextPickerClient(TextPickerConfig& config, TextPickerError* error)
        : config_(config), error_(error)
    {
        ensure_team_initialized();
        // Terminal slot authority ([SAVE-R2]): company-level writes must
        // target this client's chosen slot (default "text_quicksave"), never
        // save0. An unsafe name is rejected by the setter and simply leaves
        // the previous active slot in place (the save itself would fail the
        // same validation).
        assert_company_slot_authority();
    }

    const PickerMenuItem* present_menu(PickerMenuId menu_id) override
    {
        for (;;) {
            ensure_team_initialized();
            const TerminalMenuModel menu =
                build_terminal_menu_model(menu_id, label_context());
            print_menu_context(menu);

            std::printf("\n=== %s ===\n", menu.title.c_str());
            for (size_t i = 0; i < menu.entries.size(); ++i) {
                std::printf("  %2zu. %s\n", i + 1, menu.entries[i].label.c_str());
            }
            std::printf("Choice: ");
            std::fflush(stdout);

            std::string line;
            if (!read_line(line))
                return menu.cancel_item;

            const auto choice = parse_int_strict(line);
            if (!choice || *choice < 1 || static_cast<size_t>(*choice) > menu.entries.size()) {
                std::printf("Invalid choice.\n");
                continue;
            }

            return menu.entries[static_cast<size_t>(*choice - 1)].item;
        }
    }

    void handle_menu_item(PickerMenuId menu_id, const PickerMenuItem& item) override
    {
        if (menu_id == PickerMenuId::Main) {
            handle_main_menu_item(item);
            return;
        }
        if (menu_id == PickerMenuId::Difficulty) {
            handle_difficulty_menu_item(item);
            return;
        }
        handle_team_build_item(item);
    }

    // §2.2 terminal name entry: the read_line projection of the SDL screen.
    // Prints the generated suggestion + slug preview, then accepts a blank
    // line (keep the suggestion), "reroll" (a fresh suggestion), or any typed
    // text (the new name, clamped to the display cap). Exhausted input (EOF)
    // keeps the suggestion — the headless internal-path checks call
    // prepare_new_game with no stdin and still expect a founded company.
    std::string prompt_new_company_name()
    {
        SeededRandom rng(
            static_cast<std::uint32_t>(og::data::company_clock_now_s()));
        std::string name = og::ui::generate_company_name(rng);
        for (;;) {
            std::printf("\n=== Found Your Company ===\n");
            std::printf("Name: %s\n", name.c_str());
            std::printf("%s\n",
                og::ui::format_company_file_preview(name).c_str());
            std::printf("Enter a name, blank to accept, "
                        "or 'reroll' for another suggestion: ");
            std::fflush(stdout);
            std::string line;
            if (!read_line(line) || line.empty())
                return name;
            if (line == "reroll") {
                name = og::ui::generate_company_name(rng);
                continue;
            }
            if (line.size() > og::ui::kCompanyNameMaxLen)
                line.resize(og::ui::kCompanyNameMaxLen);
            return line;
        }
    }

    bool prepare_new_game() override
    {
        // §2.2: found the company FIRST (generated name, reroll, editable).
        std::string company_name = prompt_new_company_name();

        reset_for_new_game(save_data_);
        ensure_team_populated(save_data_);
        // The display name lives in the 40-byte save_name; the filename stays
        // this terminal client's own slot (config_.save_name, [SAVE-R2]).
        save_data_.save_name = company_name;
        assert_company_slot_authority(); // [SAVE-R2]

        // A new game always starts on the default campaign: pull the session
        // config and the mounted package back from whatever campaign a
        // previously loaded save selected.
        config_.campaign = save_data_.current_campaign;
        (void)sync_campaign_mount_to_save(save_data_);

        sync_config_from_save();
        show_new_game_team_build_notice_ = true;
        return true;
    }

    bool configure_networking() override
    {
        std::printf("Networking setup is not available in the headless text client.\n");
        return false;
    }

    std::string show_campaign_select() override
    {
        std::list<std::string> campaigns = list_campaigns();
        order_campaigns_for_select(campaigns);
        // Kept in lockstep with the SDL/curses shelves: networked lobbies
        // must not offer the (local-only) tower. The text client cannot
        // currently enter a networked session — configure_networking is a
        // stub — so the flag is always false here today.
        filter_campaigns_for_networked_lobby(campaigns,
                                             /*networked_session=*/false);
        std::vector<std::string> entries(campaigns.begin(), campaigns.end());

        if (entries.empty()) {
            set_error(TextPickerErrorCode::CampaignIoError, "no campaigns found");
            // Title when known; falls back to the raw id when the kept
            // campaign's package is itself missing.
            std::printf("No campaigns found; keeping '%s'.\n",
                og::data::campaign_display_title(config_.campaign).c_str());
            return config_.campaign;
        }

        std::printf("\n--- Campaign Select ---\n");
        // Human titles, disambiguated with the raw id when two packages
        // share a title; selection stays by index over the raw-id list.
        const std::vector<std::string> labels =
            format_campaign_select_labels(entries);
        for (size_t i = 0; i < labels.size(); ++i)
            std::printf("  %zu. %s\n", i + 1, labels[i].c_str());
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
        // GO loads levels straight from the mounted package; selecting a
        // campaign without mounting it would silently play the old one.
        (void)sync_campaign_mount_to_save(save_data_);
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
        {
            config_.save_name = line;
            assert_company_slot_authority(); // [SAVE-R2] slot command
        }

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

#ifdef TESTING
    // GCOVR_EXCL_START -- test-only accessor in src/, not shipped code.
    const SaveData& testing_save_data() const
    {
        return save_data_;
    }
    // GCOVR_EXCL_STOP
#endif

    bool load_game() override
    {
        assert_company_slot_authority(); // [SAVE-R2]
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

        std::printf("Loaded '%s' (campaign=%s level=%s team=%d gold=%u).\n",
            config_.save_name.c_str(),
            og::data::campaign_display_title(config_.campaign).c_str(),
            level_display(config_.level).c_str(),
            static_cast<int>(save_data_.team_size),
            static_cast<unsigned>(save_data_.m_totalcash[0]));
        clear_error();
        return true;
    }

    // §2.3 Company List, terminal projection: the numbered company rows
    // (shared row formatter — byte-identical with curses) print above the
    // LoadCompany chrome menu (open / backups / delete / back); open and
    // delete then prompt for a row number. Opening repoints this client's
    // slot ([SAVE-R2]: config_.save_name IS the terminal's file authority)
    // and returns true so the state machine proceeds to team build.
    bool show_company_list() override
    {
        for (;;) {
            const std::vector<og::data::CompanyInfo> companies =
                og::data::list_companies();
            if (companies.empty()) {
                std::printf("No companies yet.\n");
                return false;
            }
            std::printf("\n--- %s ---\n",
                format_company_list_title(
                    static_cast<int>(companies.size())).c_str());
            for (std::size_t i = 0; i < companies.size(); ++i) {
                std::printf("  %2zu. %s\n", i + 1,
                    format_company_row_line(companies[i],
                        companies[i].slot ==
                            og::data::active_company_slot()).c_str());
            }

            const PickerMenuItem* item =
                present_menu(PickerMenuId::LoadCompany);
            if (!item || item->command == PickerMenuCommand::Back)
                return false;

            switch (item->command) {
            case PickerMenuCommand::OpenCompany: {
                const int idx = prompt_company_index(companies.size(), "Open");
                if (idx < 0)
                    break;
                const og::data::CompanyInfo& info =
                    companies[static_cast<std::size_t>(idx)];
                if (!info.valid) {
                    // Never silently switch to a corrupt company ([SAVE-R6]).
                    std::printf("Company file '%s' is damaged; restore a "
                                "backup or delete it.\n", info.slot.c_str());
                    break;
                }
                const std::string previous_slot = config_.save_name;
                config_.save_name = info.slot;
                if (load_game())
                    return true; // -> team build (base camp)
                // load_game printed the error; restore the slot authority.
                config_.save_name = previous_slot;
                assert_company_slot_authority(); // [SAVE-R2]
                break;
            }
            case PickerMenuCommand::OpenCompanyBackups: {
                // §2.4 Backups sub-view for one company (corrupt rows keep
                // the door — restore-from-backup IS their recovery path).
                const int idx =
                    prompt_company_index(companies.size(), "Backups for");
                if (idx < 0)
                    break;
                if (show_company_backups(
                        companies[static_cast<std::size_t>(idx)]))
                    return true; // restored -> team build (base camp)
                break;
            }
            case PickerMenuCommand::DeleteCompany: {
                const int idx =
                    prompt_company_index(companies.size(), "Delete");
                if (idx < 0)
                    break;
                const og::data::CompanyInfo& info =
                    companies[static_cast<std::size_t>(idx)];
                if (info.slot == og::data::active_company_slot()) {
                    // §3.7: delete_company refuses the active slot.
                    std::printf("Cannot delete the open company; "
                                "switch first.\n");
                    break;
                }
                const CompanyRowText row = format_company_row(info);
                // NO-first confirm (U3): only an explicit yes deletes.
                std::printf("Delete company '%s'? Backups are deleted too. "
                            "[y/N]: ", row.name.c_str());
                std::fflush(stdout);
                std::string line;
                if (!read_line(line) || (line != "y" && line != "yes")) {
                    std::printf("Not deleted.\n");
                    break;
                }
                if (og::data::delete_company(info.slot))
                    std::printf("Deleted '%s'.\n", info.slot.c_str());
                else
                    std::printf("Delete failed for '%s'.\n",
                                info.slot.c_str());
                break;
            }
            default:
                break;
            }
        }
    }

    bool save_game() override
    {
        assert_company_slot_authority(); // [SAVE-R2]
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
    // [SAVE-R2] Re-points the process-wide active-company slot at this
    // client's configured slot so shared company-level code (autosave and the
    // slot-authoritative save paths) always writes the user's chosen file.
    void assert_company_slot_authority() const
    {
        (void)og::data::set_active_company_slot(config_.save_name);
    }

    // §2.3: prompt for a 1-based company row number; -1 on cancel/invalid.
    int prompt_company_index(std::size_t count, const char* verb)
    {
        std::printf("%s which company [1-%zu]: ", verb, count);
        std::fflush(stdout);
        std::string line;
        if (!read_line(line))
            return -1;
        const auto choice = parse_int_strict(line);
        if (!choice || *choice < 1
            || static_cast<std::size_t>(*choice) > count) {
            std::printf("Invalid company selection.\n");
            return -1;
        }
        return *choice - 1;
    }

    // §2.4: prompt for a 1-based backup row number; -1 on cancel/invalid.
    int prompt_backup_index(std::size_t count, const char* verb)
    {
        std::printf("%s which backup [1-%zu]: ", verb, count);
        std::fflush(stdout);
        std::string line;
        if (!read_line(line))
            return -1;
        const auto choice = parse_int_strict(line);
        if (!choice || *choice < 1
            || static_cast<std::size_t>(*choice) > count) {
            std::printf("Invalid backup selection.\n");
            return -1;
        }
        return *choice - 1;
    }

    // §2.4 Backups sub-view, terminal projection: the numbered snapshot rows
    // (shared row formatter — byte-identical with curses) print above the
    // Backups chrome menu (restore / delete / back); restore and delete then
    // prompt for a row number. A successful restore repoints this client's
    // slot ([SAVE-R2]) and reloads the rewound company, so the caller
    // proceeds to team build (base camp). An empty snapshot list backs out
    // (backups are level-win products, §3.7).
    bool show_company_backups(const og::data::CompanyInfo& company)
    {
        const CompanyRowText company_row = format_company_row(company);
        for (;;) {
            const std::vector<og::data::CompanyBackupInfo> backups =
                og::data::list_company_backups(company.slot);
            std::printf("\n--- %s ---\n",
                format_backup_list_title(company_row.name,
                    static_cast<int>(backups.size())).c_str());
            if (backups.empty()) {
                std::printf("No backups yet - backups snapshot on "
                            "level wins.\n");
                return false;
            }
            for (std::size_t i = 0; i < backups.size(); ++i) {
                std::printf("  %2zu. %s\n", i + 1,
                    format_backup_row_line(backups[i]).c_str());
            }

            const PickerMenuItem* item = present_menu(PickerMenuId::Backups);
            if (!item || item->command == PickerMenuCommand::Back)
                return false;

            switch (item->command) {
            case PickerMenuCommand::RestoreBackup: {
                const int idx =
                    prompt_backup_index(backups.size(), "Restore");
                if (idx < 0)
                    break;
                const og::data::CompanyBackupInfo& backup =
                    backups[static_cast<std::size_t>(idx)];
                if (!backup.header.valid) {
                    // The §3.7 step-0 validation is the real guard; refuse
                    // up front to spare a confirm that can only fail.
                    std::printf("Backup file '%s' is damaged.\n",
                                backup.filename.c_str());
                    break;
                }
                // NO-first confirm (U3): only an explicit yes rewinds.
                std::printf("Rewind to this backup? The current state is "
                            "backed up first. [y/N]: ");
                std::fflush(stdout);
                std::string line;
                if (!read_line(line) || (line != "y" && line != "yes")) {
                    std::printf("Not restored.\n");
                    break;
                }
                const std::string previous_slot = config_.save_name;
                config_.save_name = company.slot;
                assert_company_slot_authority(); // [SAVE-R2]
                const og::data::CompanyRestoreError error =
                    og::data::restore_company_backup(save_data_,
                                                     company.slot,
                                                     backup.seq);
                // RestampFailed included: the rewind itself finished (the
                // next autosave re-stamps).
                if (error == og::data::CompanyRestoreError::None
                    || error == og::data::CompanyRestoreError::RestampFailed) {
                    std::printf("Restored backup %d of '%s'.\n", backup.seq,
                                company.slot.c_str());
                    if (load_game())
                        return true; // -> team build (base camp)
                } else {
                    std::printf("Restore failed (%s).\n",
                                company_restore_error_string(error));
                }
                // Not rewound: restore the slot authority (the open-flow
                // discipline).
                config_.save_name = previous_slot;
                assert_company_slot_authority(); // [SAVE-R2]
                break;
            }
            case PickerMenuCommand::DeleteBackup: {
                const int idx = prompt_backup_index(backups.size(), "Delete");
                if (idx < 0)
                    break;
                const og::data::CompanyBackupInfo& backup =
                    backups[static_cast<std::size_t>(idx)];
                // NO-first confirm (U3): only an explicit yes deletes.
                std::printf("Delete backup %d of '%s'? [y/N]: ", backup.seq,
                            company.slot.c_str());
                std::fflush(stdout);
                std::string line;
                if (!read_line(line) || (line != "y" && line != "yes")) {
                    std::printf("Not deleted.\n");
                    break;
                }
                if (og::data::delete_company_backup(company.slot, backup.seq))
                    std::printf("Deleted backup %d.\n", backup.seq);
                else
                    std::printf("Delete failed for backup %d.\n", backup.seq);
                break;
            }
            default:
                break;
            }
        }
    }

    // Borrow bundle for the shared label/gate layer (menu_binding.h).
    MenuLabelContext label_context() const
    {
        MenuLabelContext context;
        context.save = &save_data_;
        context.session_difficulty =
            og::runtime::current_session->current_difficulty_;
        context.spectator = is_spectator_mode(save_data_);
        context.campaign = config_.campaign;
        context.level = config_.level;
        return context;
    }

    void print_menu_context(const TerminalMenuModel& menu)
    {
        if (menu.context_lines.empty())
            return;

        std::printf("\n");
        for (const std::string& line : menu.context_lines)
            std::printf("%s\n", line.c_str());
        if (show_new_game_team_build_notice_) {
            std::printf("[New game: build your team before GO!]\n");
            show_new_game_team_build_notice_ = false;
        }
    }

    void handle_main_menu_item(const PickerMenuItem& item)
    {
        switch (item.command) {
        case PickerMenuCommand::OpenDifficultyMenu:
            // The DIFFICULTY submenu: the shared nested presentation loop
            // (show_submenu in picker_state) until Back.
            show_submenu(PickerMenuId::Difficulty);
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
        // Gated items (the CTF match settings outside the CTF campaign)
        // print their shared guard message instead of acting.
        const std::string_view guard =
            terminal_gate_message(item, label_context());
        if (!guard.empty()) {
            std::printf("%s\n", std::string(guard).c_str());
            return;
        }

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
            std::printf("Current campaign progress: campaign=%s level=%s.\n",
                og::data::campaign_display_title(config_.campaign).c_str(),
                level_display(config_.level).c_str());
            break;
        case PickerMenuCommand::Networking:
            (void)configure_networking();
            break;
        case PickerMenuCommand::SetLevel:
            set_level();
            break;
        case PickerMenuCommand::SetCampaign:
            (void)show_campaign_select();
            break;
        case PickerMenuCommand::CycleCtfTeamCount:
            cycle_ctf_team_count(save_data_);
            std::printf("%s\n", format_ctf_teams_label(save_data_).c_str());
            break;
        case PickerMenuCommand::CycleCtfCaptureLimit:
            cycle_ctf_capture_limit(save_data_);
            std::printf("%s\n", format_ctf_caps_label(save_data_).c_str());
            break;
        case PickerMenuCommand::ToggleCtfScenarioTroops:
            toggle_ctf_scenario_troops(save_data_);
            std::printf("%s\n", format_ctf_troops_label(save_data_).c_str());
            break;
        case PickerMenuCommand::ViewScenario:
            view_scenario();
            break;
        case PickerMenuCommand::Teams:
            teams_screen();
            break;
        default:
            break;
        }
    }

    void handle_difficulty_menu_item(const PickerMenuItem& item)
    {
        switch (item.command) {
        case PickerMenuCommand::SetDifficulty:
            og::runtime::current_session->current_difficulty_ = cycle_difficulty(og::runtime::current_session->current_difficulty_);
            if (og::runtime::current_session->game_.world != nullptr) {
                og::runtime::current_session->game_.world->difficulty =
                    static_cast<short>(difficulty_percent(og::runtime::current_session->current_difficulty_));
            }
            std::printf("Difficulty set to %s.\n",
                kDifficultyNames[og::runtime::current_session->current_difficulty_]);
            break;
        case PickerMenuCommand::CycleRespawnMode:
            cycle_respawn_mode(save_data_);
            std::printf("%s\n", format_respawn_mode_label(save_data_).c_str());
            break;
        case PickerMenuCommand::CycleRespawnDelay:
            cycle_respawn_delay(save_data_);
            std::printf("%s\n", format_respawn_delay_label(save_data_).c_str());
            break;
        case PickerMenuCommand::TogglePermadeath:
            toggle_permadeath(save_data_);
            std::printf("%s\n", format_permadeath_label(save_data_).c_str());
            break;
        case PickerMenuCommand::CycleGeneratorRate:
            cycle_generator_rate(save_data_);
            std::printf("%s\n", format_generator_rate_label(save_data_).c_str());
            break;
        default:
            break;
        }
    }

    // Read-only roster report of the current level from a scratch headless
    // load (the same shared report the SDL VIEW LEVEL screen renders).
    void view_scenario()
    {
        if (get_mounted_campaign() != save_data_.current_campaign) {
            std::printf("Campaign '%s' is not mounted.\n",
                save_data_.current_campaign.c_str());
            return;
        }

        LevelRuntimeData scenario(save_data_.scen_num, false,
            &headless_level_data_hooks());
        if (!scenario.load()) {
            std::printf("Could not load level %d.\n",
                static_cast<int>(save_data_.scen_num));
            return;
        }

        const ScenarioRosterReport report =
            build_scenario_roster_report(scenario.world(), save_data_);
        std::printf("\n--- SCEN %d: %s ---\n",
            static_cast<int>(save_data_.scen_num),
            scenario.world().title.c_str());
        for (const std::string& line : format_scenario_report_lines(report))
            std::printf("%s\n", line.c_str());
        wait_for_enter();
    }

    // Roster grouped by team plus a sub-prompt: "play N" re-seats P1
    // (my_team), "move S N" moves roster slot S to team N. Teams are 1-based
    // in the prompt, matching the train menu's "Playing on Team N" wording.
    void teams_screen()
    {
        for (;;) {
            print_team_rows();
            std::printf("Teams: 'play N' | 'move SLOT N' | blank line exits: ");
            std::fflush(stdout);

            std::string line;
            if (!read_line(line) || line.empty())
                return;

            int value = 0;
            int slot = 0;
            if (std::sscanf(line.c_str(), "play %d", &value) == 1) {
                if (value < 1 || value > 4 ||
                    !set_preferred_team(save_data_,
                        static_cast<short>(value - 1))) {
                    std::printf("No heroes on team %d.\n", value);
                } else {
                    std::printf("Playing on %s.\n",
                        og::sim::ctf_team_color_name(value - 1));
                }
                continue;
            }
            if (std::sscanf(line.c_str(), "move %d %d", &slot, &value) == 2) {
                if (slot < 1 || slot > MAX_TEAM_SIZE || value < 1 || value > 4
                    || !save_data_.team_list[slot - 1]) {
                    std::printf("Invalid slot or team.\n");
                    continue;
                }
                const short current =
                    save_data_.team_list[slot - 1]->teamnum;
                const short moved = cycle_guy_team(save_data_, slot - 1,
                    (value - 1) - static_cast<int>(current));
                if (moved < 0)
                    std::printf("Invalid slot or team.\n");
                else
                    std::printf("Moved slot %d to %s.\n", slot,
                        og::sim::ctf_team_color_name(value - 1));
                continue;
            }
            std::printf("Unrecognized command.\n");
        }
    }

    void print_team_rows()
    {
        const std::vector<short> seats = derive_local_seat_teams(save_data_);
        std::printf("\n--- Teams ---\n");
        // Only seats below numplayers materialize in-game (game.cpp
        // truncates the derivation at numviews); never tag the rest.
        const std::size_t seat_limit = std::min<std::size_t>(
            seats.size(), static_cast<std::size_t>(save_data_.numplayers));
        for (short t = 0; t < 4; ++t) {
            std::string seat_tag;
            for (std::size_t seat = 0; seat < seat_limit; ++seat) {
                if (seats[seat] == t)
                    seat_tag = std::format("(P{})", seat + 1);
            }

            int hero_count = 0;
            std::string members;
            for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot) {
                const auto& member = save_data_.team_list[slot];
                if (!member || member->teamnum != t)
                    continue;
                ++hero_count;
                members += std::format("  {}. {} ({})\n", slot + 1,
                    member->name, family_display_name(member->family));
            }

            // No world is loaded in the text picker, so no map tags
            // (authored/BOTS) — is_ctf=false keeps the label honest.
            std::printf("%s\n",
                format_team_row_label(t, hero_count, false, false, false,
                    seat_tag).c_str());
            std::printf("%s", members.c_str());
        }
        if (is_ctf_campaign(save_data_))
            std::printf("[%s]\n", format_ctf_teams_label(save_data_).c_str());
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

    // Mount-guarded level display over the session campaign (the shared
    // level_display_guarded helper carries the guard rule).
    std::string level_display(int level) const
    {
        return level_display_guarded(config_.campaign, level);
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
                const std::array<S, 6> stats = {S::Strength, S::Dexterity,
                                                S::Constitution, S::Intelligence,
                                                S::Armor, S::Level};
                int idx = std::abs(*val) - 1;
                if (idx >= 0 && static_cast<std::size_t>(idx) < stats.size()) {
                    if (*val > 0) session.increase_stat(stats[static_cast<std::size_t>(idx)]);
                    else session.decrease_stat(stats[static_cast<std::size_t>(idx)]);
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
        std::printf("Set level (current %s): ",
            level_display(config_.level).c_str());
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
    bool show_new_game_team_build_notice_ = false;
};

#ifdef TESTING
// GCOVR_EXCL_START -- test-only coverage harness in src/, not shipped code.
class ScopedCinRedirect
{
public:
    explicit ScopedCinRedirect(std::string input)
        : input_(std::move(input))
        , saved_(std::cin.rdbuf(input_.rdbuf()))
    {
    }

    ~ScopedCinRedirect()
    {
        std::cin.rdbuf(saved_);
    }

    ScopedCinRedirect(const ScopedCinRedirect&) = delete;
    ScopedCinRedirect& operator=(const ScopedCinRedirect&) = delete;

private:
    std::istringstream input_;
    std::streambuf* saved_;
};

int text_picker_testing_exercise_internal_paths()
{
    TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    config.save_name = "missing-text-picker-save";
    TextPickerError error;
    TextPickerClient client(config, &error);
    const SaveData& save = client.testing_save_data();

    int checks = 0;
    int check_index = 0;
    int failed_check = 0;
    bool failed = false;
    const auto check = [&checks, &check_index, &failed_check, &failed](bool condition) {
        ++check_index;
        if (condition)
            ++checks;
        else {
            failed = true;
            if (failed_check == 0)
                failed_check = check_index;
        }
    };

    // [SAVE-R2] Client construction asserts terminal slot authority: the
    // process-wide active company must already be this client's slot.
    check(og::data::active_company_slot() == "missing-text-picker-save");

    client.show_help();
    const bool networking = client.configure_networking();
    const bool loaded = client.load_game();
    check(!networking);
    check(!loaded);
    check(error.code == TextPickerErrorCode::LoadIoError &&
          !error.detail.empty());

    {
        ScopedCinRedirect input("bad\n1\n");
        const PickerMenuItem* item = client.present_menu(PickerMenuId::Main);
        check(item != nullptr &&
              item->command == PickerMenuCommand::BeginNewGame);
    }
    {
        ScopedCinRedirect input("");
        const PickerMenuItem* item = client.present_menu(PickerMenuId::TeamBuild);
        check(item != nullptr && item->command == PickerMenuCommand::Back);
    }
    {
        ScopedCinRedirect input("");
        const PickerMenuItem* item = client.present_menu(PickerMenuId::Main);
        check(item != nullptr && item->command == PickerMenuCommand::Quit);
    }

    check(client.prepare_new_game() &&
          save.team_size == 1 &&
          save.team_list[0] != nullptr &&
          save.team_list[0]->family == FAMILY_SOLDIER);
    check(client.screen_after_game() == PickerScreen::TeamBuild);

    // The difficulty cycle lives in the DIFFICULTY submenu now.
    if (const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::Difficulty, PickerMenuCommand::SetDifficulty)) {
        const int previous = og::runtime::current_session->current_difficulty_;
        client.handle_menu_item(PickerMenuId::Difficulty, *item);
        check(og::runtime::current_session->current_difficulty_ ==
              cycle_difficulty(previous));
    } else {
        check(false);
    }
    // The match-rule cyclers restore their SaveData field after a full
    // cycle (three steps, or two for the permadeath toggle).
    const auto check_difficulty_rule = [&](PickerMenuCommand command, int period,
                                           const auto& current) {
        const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::Difficulty, command);
        if (item == nullptr) {
            check(false);
            return;
        }
        const short previous = current();
        client.handle_menu_item(PickerMenuId::Difficulty, *item);
        const bool changed = current() != previous;
        for (int step = 1; step < period; ++step)
            client.handle_menu_item(PickerMenuId::Difficulty, *item);
        check(changed && current() == previous);
    };
    check_difficulty_rule(PickerMenuCommand::CycleRespawnMode, 3,
        [&] { return save.respawn_mode; });
    check_difficulty_rule(PickerMenuCommand::CycleRespawnDelay, 3,
        [&] { return save.ctf_respawn_ticks; });
    check_difficulty_rule(PickerMenuCommand::TogglePermadeath, 2,
        [&] { return save.keep_fallen_heroes; });
    check_difficulty_rule(PickerMenuCommand::CycleGeneratorRate, 3,
        [&] { return save.generator_rate; });
    // The main-menu DIFFICULTY door runs the nested submenu loop: toggle
    // permadeath twice, then back — a round trip that leaves save untouched.
    if (const PickerMenuItem* door =
            find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::OpenDifficultyMenu)) {
        const short previous_permadeath = save.keep_fallen_heroes;
        ScopedCinRedirect input("4\n4\n6\n");
        client.handle_menu_item(PickerMenuId::Main, *door);
        check(save.keep_fallen_heroes == previous_permadeath);
    } else {
        check(false);
    }
    if (const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 2)) {
        client.handle_menu_item(PickerMenuId::Main, *item);
        check(save.numplayers == 2);
    } else {
        check(false);
    }
    if (const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::ToggleAlliedMode)) {
        const bool previous = is_allied_mode(save);
        client.handle_menu_item(PickerMenuId::Main, *item);
        check(is_allied_mode(save) != previous);
    } else {
        check(false);
    }
    if (const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::LevelEdit)) {
        const int previous_level = config.level;
        const TextPickerErrorCode previous_error = error.code;
        client.handle_menu_item(PickerMenuId::Main, *item);
        check(config.level == previous_level && error.code == previous_error);
    } else {
        check(false);
    }
    const PickerMenuItem default_main{"helper-default", "Helper Default",
                                      PickerMenuCommand::BeginNewGame, 0};
    const unsigned char previous_players = save.numplayers;
    client.handle_menu_item(PickerMenuId::Main, default_main);
    check(save.numplayers == previous_players);
    const PickerMenuItem default_team{"helper-default", "Helper Default",
                                      PickerMenuCommand::Back, 0};
    int previous_level = config.level;
    client.handle_menu_item(PickerMenuId::TeamBuild, default_team);
    check(config.level == previous_level);

    auto handle_team_item = [&](PickerMenuCommand command,
                                std::string input_text,
                                const auto& predicate) {
        // The scenario-shaped commands live in the SCENARIO submenu now;
        // both menus dispatch through the same team-build handler.
        const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::TeamBuild, command);
        PickerMenuId menu_id = PickerMenuId::TeamBuild;
        if (item == nullptr) {
            item = find_picker_menu_item(PickerMenuId::Scenario, command);
            menu_id = PickerMenuId::Scenario;
        }
        if (item != nullptr)
        {
            ScopedCinRedirect input(std::move(input_text));
            client.handle_menu_item(menu_id, *item);
            check(predicate());
        } else {
            check(false);
        }
    };

    unsigned char previous_team_size = save.team_size;
    handle_team_item(PickerMenuCommand::ViewTeam, "\n", [&] {
        return save.team_size == previous_team_size;
    });
    previous_team_size = save.team_size;
    handle_team_item(PickerMenuCommand::TrainTeam, "n\np\n1\n-1\n6\na\nb\n", [&] {
        return save.team_size == previous_team_size &&
               !config.team_families.empty();
    });
    previous_team_size = save.team_size;
    handle_team_item(PickerMenuCommand::HireTroops, "n\np\nh\nb\n", [&] {
        return save.team_size == static_cast<unsigned char>(previous_team_size + 1) &&
               config.team_families.size() == save.team_size;
    });
    previous_level = config.level;
    handle_team_item(PickerMenuCommand::ShowProgress, "", [&] {
        return config.level == previous_level;
    });
    const TextPickerErrorCode error_before_networking = error.code;
    handle_team_item(PickerMenuCommand::Networking, "", [&] {
        return error.code == error_before_networking;
    });
    previous_level = config.level;
    handle_team_item(PickerMenuCommand::SetLevel, "0\n", [&] {
        return config.level == previous_level &&
               save.scen_num == previous_level;
    });
    handle_team_item(PickerMenuCommand::SetLevel, "5\n", [&] {
        return config.level == 5 && save.scen_num == 5;
    });
    const std::string previous_campaign = config.campaign;
    handle_team_item(PickerMenuCommand::SetCampaign, "999\n", [&] {
        return config.campaign == previous_campaign &&
               save.current_campaign == previous_campaign;
    });
    handle_team_item(PickerMenuCommand::SetCampaign, "\n", [&] {
        return config.campaign == previous_campaign &&
               save.current_campaign == previous_campaign;
    });

    const PickerMenuItem zero_players{"helper-zero", "Zero Players",
                                      PickerMenuCommand::SetPlayerMode, 0};
    client.handle_menu_item(PickerMenuId::Main, zero_players);
    check(save.numplayers == 0);
    previous_team_size = save.team_size;
    handle_team_item(PickerMenuCommand::ViewTeam, "\n", [&] {
        return save.team_size == previous_team_size;
    });
    handle_team_item(PickerMenuCommand::TrainTeam, "b\n", [&] {
        return save.team_size == previous_team_size &&
               save.numplayers == 0;
    });
    // [SAVE-R2] A stale process-wide slot must not survive a new game: the
    // client re-asserts its own slot in prepare_new_game.
    (void)og::data::set_active_company_slot("save0");
    check(client.prepare_new_game() &&
          save.team_size >= 1 &&
          !config.team_families.empty());
    check(og::data::active_company_slot() == "missing-text-picker-save");

    {
        const std::uint32_t previous_seed = config.seed;
        ScopedCinRedirect input("helper-slot\nbad-seed\n");
        client.show_options();
        check(config.save_name == "helper-slot" &&
              config.seed == previous_seed);
        // [SAVE-R2] The slot command repoints the active company too.
        check(og::data::active_company_slot() == "helper-slot");
    }
    {
        ScopedCinRedirect input("\n123\n");
        client.show_options();
        check(config.save_name == "helper-slot" && config.seed == 123u);
    }
    check(client.save_game() && error.code == TextPickerErrorCode::None);
    check(og::data::active_company_slot() == "helper-slot");
    check(client.load_game() &&
          error.code == TextPickerErrorCode::None &&
          !config.team_families.empty());

    return failed ? -failed_check : 0;
}
// GCOVR_EXCL_STOP
#endif

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
