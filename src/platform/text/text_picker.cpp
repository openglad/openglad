/* Text-based picker implementation for the headless client.
 *
 * Provides a line-oriented stdin/stdout menu interface that drives
 * the shared picker state machine. Uses SaveData as the backing store
 * and shared business logic from picker_common.h.
 */

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/util.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/campaign_state_providers.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/cloud_save_client.h>
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
#include <optional>
#ifdef TESTING
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#endif
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
    protocol_args.campaign_state = config.campaign_state;
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
        // Campaign scripting (#206): point the process-global og.campaign_*
        // providers at THIS client's SaveData — the same object the picker
        // mutates — so campaign hooks always read/write the live save (the
        // design doc's install-site list: the text picker's init, beside
        // the [SAVE-R2] slot repoint).
        og::script::hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_data_));
    }

    ~TextPickerClient() override
    {
        // #206: the providers borrow save_data_ — clear before it dies.
        og::script::hooks::clear_campaign_providers();
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
        if (menu_id == PickerMenuId::CloudSave) {
            handle_cloud_menu_item(item);
            return;
        }
        handle_team_build_item(item);
    }

    // §2.2 terminal name entry: the read_line projection of the SDL screen.
    // Prints the generated suggestion, then accepts a blank line (keep the
    // suggestion), "reroll" (a fresh suggestion), or any typed text (the new
    // name, clamped to the display cap). Exhausted input (EOF) keeps the
    // suggestion — the headless internal-path checks call prepare_new_game
    // with no stdin and still expect a founded company. Deliberately NO
    // "file: <slug>.gtl" preview here (§9.3/F2 removed it on every client;
    // for terminals it was also simply false: they persist in place to their
    // own slot (config_.save_name, [SAVE-R2]), so the SDL slug preview would
    // name a file that is never created).
    std::string prompt_new_company_name()
    {
        SeededRandom rng(
            static_cast<std::uint32_t>(og::data::company_clock_now_s()));
        std::string name = og::ui::generate_company_name(rng);
        for (;;) {
            std::printf("\n=== Found Your Company ===\n");
            std::printf("Name: %s\n", name.c_str());
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
        // #162: between prompts in a printf/read_line client — no pixies.
        headless_entity_loader()->reload_graphics_if_stale();

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
        // #162: follow the mount with the sprite set (campaign entity art,
        // pack families) before GO builds the level.
        headless_entity_loader()->reload_graphics_if_stale();
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
    const SaveData& testing_save_data() const;
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

        std::printf("Loaded '%s' (campaign=%s level=%s team=%d gold=%s).\n",
            config_.save_name.c_str(),
            og::data::campaign_display_title(config_.campaign).c_str(),
            level_display(config_.level).c_str(),
            static_cast<int>(save_data_.team_size),
            format_wallet_amount(save_data_, 0).c_str());
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
            if (item.arg > 1) {
                set_player_count(save_data_, 1);
                std::printf("The text simulator has no player controls; "
                            "player mode remains 1.\n");
            } else {
                set_player_count(save_data_, item.arg);
                std::printf("Player mode set to %d.\n", item.arg);
            }
            break;
        case PickerMenuCommand::ToggleAlliedMode:
            // Compatibility-only command: no current menu exposes it. Match
            // the curses client and direct old callers to the replacement
            // surface without changing the legacy persisted bit.
            std::printf("Choose this player's team from Matchup.\n");
            break;
        case PickerMenuCommand::LevelEdit:
            std::printf("Level Editor is not available in the headless text client.\n");
            break;
        case PickerMenuCommand::OpenCloudMenu:
            // #155: the CLOUD submenu — the shared nested presentation loop
            // until Back.
            show_submenu(PickerMenuId::CloudSave);
            break;
        default:
            break;
        }
    }

    // #155 cloud saves, text projection: passphrase on stdin (echoed, like
    // company names), upload/download through the shared pure flows with
    // stdin y/N confirms. The headless bridge installs no HTTP callbacks, so
    // the flows degrade with the D8 unavailable line.
    void handle_cloud_menu_item(const PickerMenuItem& item)
    {
        switch (item.command) {
        case PickerMenuCommand::CloudSetPassphrase: {
            std::printf("Cloud passphrase (8-64 chars, blank cancels): ");
            std::fflush(stdout);
            std::string line;
            if (!read_line(line) || line.empty()) {
                std::printf("Passphrase unchanged.\n");
                break;
            }
            const std::string key =
                og::ui::cloud::derive_cloud_save_key(line);
            if (key.empty()) {
                std::printf("Passphrase must be 8-64 characters.\n");
                break;
            }
            og::ui::cloud::store_cloud_key(key);
            std::printf("Passphrase set.\n");
            break;
        }
        case PickerMenuCommand::CloudUpload: {
            bool notified = false;
            const std::string status = og::ui::cloud::run_cloud_upload(
                {}, text_cloud_hooks(notified));
            if (!notified)
                std::printf("%s\n", status.c_str());
            break;
        }
        case PickerMenuCommand::CloudDownload: {
            bool notified = false;
            const std::string status = og::ui::cloud::run_cloud_download(
                {}, text_cloud_hooks(notified),
                [this](const std::string& slot) {
                    return open_downloaded_company(slot);
                });
            if (!notified)
                std::printf("%s\n", status.c_str());
            break;
        }
        default:
            break;
        }
    }

    // Hooks for the shared cloud flows: bridge HTTP (absent in the headless
    // bridge -> the flows report unavailability), stdin y/N confirm, printf
    // notify. `notified` lets the caller print the returned status only for
    // silent paths (the explicit-No cancels).
    og::ui::cloud::CloudHooks text_cloud_hooks(bool& notified)
    {
        og::ui::cloud::CloudHooks hooks;
        const PlatformBridge& bridge = platform_bridge();
        if (bridge.cloud_http_get)
            hooks.http_get = bridge.cloud_http_get;
        if (bridge.cloud_http_post)
            hooks.http_post = bridge.cloud_http_post;
        hooks.confirm = [](const std::string& title,
                           const std::string& message) {
            std::string flat = message;
            std::replace(flat.begin(), flat.end(), '\n', ' ');
            // NO-first (U3): only an explicit yes proceeds.
            std::printf("%s %s [y/N]: ", title.c_str(), flat.c_str());
            std::fflush(stdout);
            std::string line;
            if (!read_line(line))
                return false;
            return line == "y" || line == "yes";
        };
        hooks.notify = [&notified](const std::string& title,
                                   const std::string& message) {
            std::string flat = message;
            std::replace(flat.begin(), flat.end(), '\n', ' ');
            std::printf("%s: %s\n", title.c_str(), flat.c_str());
            notified = true;
        };
        return hooks;
    }

    // #155 download open path: the terminal §2.3 open sequence — repoint
    // this client's slot authority at the installed company and load it;
    // restore the previous slot on failure (the show_company_list
    // discipline).
    bool open_downloaded_company(const std::string& slot)
    {
        const std::string previous_slot = config_.save_name;
        config_.save_name = slot;
        if (load_game())
            return true;
        config_.save_name = previous_slot;
        assert_company_slot_authority(); // [SAVE-R2]
        return false;
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
        case PickerMenuCommand::ToggleDeploy:
            deploy_prompt();
            break;
        // ToggleReady is gated NetworkedOnly: the guard above already
        // printed its message (the text picker holds no networked lobby).
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
            autosave_company_after_mutation(); // §3.8 settings tail
            break;
        case PickerMenuCommand::CycleCtfCaptureLimit:
            cycle_ctf_capture_limit(save_data_);
            std::printf("%s\n", format_ctf_caps_label(save_data_).c_str());
            autosave_company_after_mutation(); // §3.8 settings tail
            break;
        case PickerMenuCommand::ToggleCtfScenarioTroops:
            toggle_ctf_scenario_troops(save_data_);
            std::printf("%s\n", format_ctf_troops_label(save_data_).c_str());
            autosave_company_after_mutation(); // §3.8 settings tail
            break;
        case PickerMenuCommand::ViewScenario:
            view_scenario();
            break;
        case PickerMenuCommand::Teams:
            teams_screen();
            break;
        case PickerMenuCommand::CampaignCamp:
            // #206: the Base Camp gameplay zone (guard + flow live in the
            // shared terminal driver).
            campaign_camp_flow();
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
            autosave_company_after_mutation(); // §3.8 settings tail
            break;
        case PickerMenuCommand::CycleRespawnMode:
            cycle_respawn_mode(save_data_);
            std::printf("%s\n", format_respawn_mode_label(save_data_).c_str());
            autosave_company_after_mutation(); // §3.8 settings tail
            break;
        case PickerMenuCommand::CycleRespawnDelay:
            cycle_respawn_delay(save_data_);
            std::printf("%s\n", format_respawn_delay_label(save_data_).c_str());
            autosave_company_after_mutation(); // §3.8 settings tail
            break;
        case PickerMenuCommand::TogglePermadeath:
            toggle_permadeath(save_data_);
            std::printf("%s\n", format_permadeath_label(save_data_).c_str());
            autosave_company_after_mutation(); // §3.8 settings tail
            break;
        case PickerMenuCommand::CycleGeneratorRate:
            cycle_generator_rate(save_data_);
            std::printf("%s\n", format_generator_rate_label(save_data_).c_str());
            autosave_company_after_mutation(); // §3.8 settings tail
            break;
        case PickerMenuCommand::ToggleInfiniteGold:
            // SESSION-ONLY (never in the GTL file), so unlike every other row
            // here this one deliberately skips the settings autosave tail.
            toggle_infinite_gold(save_data_);
            std::printf("%s\n", format_infinite_gold_label(save_data_).c_str());
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

    // MATCHUP groups the roster by team. The historical "play N" spelling is
    // retained as preferred-team metadata only: the headless text protocol
    // has no playable seats or input routing. "move S N" moves roster slot S
    // to team N. Teams are 1-based in the prompt, matching the train menu's
    // "Playing on Team N" wording.
    void teams_screen()
    {
        for (;;) {
            print_team_rows();
            std::printf("Matchup (simulation has no player controls): "
                        "'play TEAM' (preferred metadata) | "
                        "'move SLOT TEAM' | blank line exits: ");
            std::fflush(stdout);

            std::string line;
            if (!read_line(line) || line.empty())
                return;

            int value = 0;
            int slot = 0;
            int seat = 0;
            if (std::sscanf(line.c_str(), "play %d %d", &seat, &value) == 2) {
                std::printf("Player-seat assignments are unavailable in "
                            "the text simulator.\n");
                continue;
            }
            char extra = '\0';
            if (std::sscanf(line.c_str(), "play %d %c", &value, &extra) == 1) {
                if (value < 1 || value > 4 ||
                    !set_preferred_team(save_data_,
                        static_cast<short>(value - 1))) {
                    std::printf("No heroes on team %d.\n", value);
                } else {
                    std::printf("Preferred-team metadata is now %s; "
                                "the text simulator has no player controls.\n",
                        og::sim::team_color_name(value - 1));
                }
                continue;
            }
            if (std::sscanf(line.c_str(), "move %d %d", &slot, &value) == 2) {
                if (slot < 1 || slot > MAX_TEAM_SIZE || value < 1 || value > 4
                    || !save_data_.team_list[static_cast<std::size_t>(slot - 1)]) {
                    std::printf("Invalid slot or team.\n");
                    continue;
                }
                const short current =
                    save_data_.team_list[static_cast<std::size_t>(slot - 1)]->teamnum;
                const short moved = cycle_guy_team(save_data_, slot - 1,
                    (value - 1) - static_cast<int>(current));
                if (moved < 0) {
                    std::printf("Invalid slot or team.\n");
                } else {
                    std::printf("Moved slot %d to %s.\n", slot,
                        og::sim::team_color_name(value - 1));
                    autosave_company_after_mutation();  // §3.8 team cycle
                }
                continue;
            }
            std::printf("Unrecognized command.\n");
        }
    }

    void print_team_rows()
    {
        std::printf("\n--- Matchup ---\n");
        for (short t = 0; t < 4; ++t) {
            int hero_count = 0;
            std::string members;
            for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot) {
                const auto& member = save_data_.team_list[static_cast<std::size_t>(slot)];
                if (!member || member->teamnum != t)
                    continue;
                ++hero_count;
                members += std::format("  {}. {} ({})\n", slot + 1,
                    member->name, family_display_name(member->family));
            }

            // No world is loaded in the text picker, so no map tags
            // (authored/BOTS) — is_ctf=false keeps the label honest.
            std::printf("%s\n",
                format_team_row_label(
                    t, hero_count, false, false, false, "").c_str());
            std::printf("%s", members.c_str());
        }
        if (is_versus_campaign(save_data_))
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

    // §2.5 command roster (text shape): the deploy flags lead each row;
    // 'deploy N' toggles, 'train N' opens the train flow directly on that
    // character, a blank line exits (so legacy "1 then Enter" drives still
    // work).
    void view_team_roster()
    {
        for (;;) {
            std::printf("\n--- Team Roster ---\n");
            const std::vector<int> slots = collect_base_camp_slots(save_data_);
            if (slots.empty()) {
                std::printf("(empty)\n");
                wait_for_enter();
                return;
            }

            for (std::size_t i = 0; i < slots.size(); ++i) {
                const guy& member =
                    *save_data_.team_list[static_cast<std::size_t>(slots[i])];
                std::printf("%2zu. [%c] %-14s Family=%-14s L=%2d STR=%d DEX=%d CON=%d INT=%d ARM=%d\n",
                    i + 1,
                    member.deployed ? 'X' : ' ',
                    member.name.c_str(),
                    family_display_name(member.family),
                    member.level,
                    member.strength,
                    member.dexterity,
                    member.constitution,
                    member.intelligence,
                    member.armor);
            }
            std::printf("DEP %d/%d\n", count_deployed_members(save_data_),
                static_cast<int>(slots.size()));
            std::printf("Roster: 'deploy N' | 'train N' | blank line exits: ");
            std::fflush(stdout);

            std::string line;
            if (!read_line(line) || line.empty())
                return;

            int value = 0;
            if (std::sscanf(line.c_str(), "deploy %d", &value) == 1) {
                toggle_deploy_display_row(slots, value);
                continue;
            }
            if (std::sscanf(line.c_str(), "train %d", &value) == 1) {
                if (value < 1 || static_cast<std::size_t>(value) > slots.size())
                    std::printf("Invalid roster row.\n");
                else
                    train_team(slots[static_cast<std::size_t>(value - 1)]);
                continue;
            }
            std::printf("Unrecognized command.\n");
        }
    }

    // Toggle deploy for 1-based display row `value` of `slots`; §3.8
    // autosave rides every flip (the §4.3 ready-clear is a no-op here — the
    // text picker holds no networked lobby).
    void toggle_deploy_display_row(const std::vector<int>& slots, int value)
    {
        if (value < 1 || static_cast<std::size_t>(value) > slots.size()) {
            std::printf("Invalid roster row.\n");
            return;
        }
        const int slot = slots[static_cast<std::size_t>(value - 1)];
        // The camp's deploy rules bind this row too: the Camp screen shows
        // the padlock and its reason, and a client that then deploys the
        // locked hero anyway is a different campaign from the SDL one.
        if (const std::optional<std::string> refused = terminal_roster_refusal(
                save_data_, TerminalRosterCommand::Deploy, slot)) {
            std::printf("%s\n", refused->c_str());
            return;
        }
        const bool deployed = toggle_deploy_slot(save_data_, slot);
        std::printf("%s %s.\n", save_data_.team_list[static_cast<std::size_t>(slot)]->name.c_str(),
            deployed ? "deployed" : "benched");
        autosave_company_after_mutation();
    }

    // The §2.5 'deploy' item (was Load Team): prompt for a roster row and
    // flip its mission-deploy flag.
    void deploy_prompt()
    {
        const std::vector<int> slots = collect_base_camp_slots(save_data_);
        if (slots.empty()) {
            std::printf("(empty roster - hire someone first)\n");
            return;
        }
        std::printf("Toggle deploy for roster row [1-%d]: ",
            static_cast<int>(slots.size()));
        std::fflush(stdout);
        std::string line;
        if (!read_line(line) || line.empty())
            return;
        const auto value = parse_int_strict(line);
        if (!value) {
            std::printf("Invalid roster row.\n");
            return;
        }
        toggle_deploy_display_row(slots, *value);
    }

    // §3.8: every base-camp mutation autosaves the active company slot
    // ([SAVE-R2]: the text client's slot authority points it at the user's
    // chosen quicksave slot, never save0).
    void autosave_company_after_mutation()
    {
        (void)company_autosave_after_mutation(save_data_, false);
    }

    void train_team(int seed_slot = -1)
    {
        // A camp that retired training refuses in words: the SDL panel just
        // has no train affordance to click, but a prompt cannot hide.
        if (const std::optional<std::string> refused = terminal_roster_refusal(
                save_data_, TerminalRosterCommand::Train, -1)) {
            std::printf("%s\n", refused->c_str());
            return;
        }
        TrainSession session(save_data_);
        if (session.empty()) {
            std::printf("No team members available to train.\n");
            return;
        }
        if (seed_slot >= 0)
            (void)session.seek_slot(seed_slot);  // §2.5 'train N' direct-open

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
            std::printf("Cost: %u  |  Gold: %s\n",
                static_cast<unsigned>(session.current_cost()),
                format_wallet_amount(save_data_, 0).c_str());
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
                if (session.accept()) {
                    std::printf("Training accepted.\n");
                    autosave_company_after_mutation();  // §3.8
                } else {
                    std::printf("Can't afford training.\n");
                }
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
        // The camp's can_hire capability — the flag that hides HIRE on the
        // SDL panel — answers this row too.
        if (const std::optional<std::string> refused = terminal_roster_refusal(
                save_data_, TerminalRosterCommand::Hire, -1)) {
            std::printf("%s\n", refused->c_str());
            return;
        }
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
            std::printf("Cost: %u  |  Gold: %s\n",
                static_cast<unsigned>(session.current_cost()),
                format_wallet_amount(save_data_, 0).c_str());
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
                std::printf("Hired %s!\n", save_data_.team_list[static_cast<std::size_t>(slot)]->name.c_str());
                sync_config_from_save();
                autosave_company_after_mutation();  // §3.8
                if (session.team_full()) {
                    std::printf("Team is now full.\n");
                    return;
                }
            }
        }
    }

    // #206 CAMP, text projection: the shared terminal driver over this
    // client's save — the printf banner and the read_line prompt are the
    // only client-specific parts (every camp line, docket ordinal and roster
    // row is composed by the driver, byte-identical with curses).
    void campaign_camp_flow()
    {
        TerminalCampaignPickerIo io;
        io.prompt = [this](const std::string& title,
                           const std::vector<std::string>& lines,
                           const std::string& label)
            -> std::optional<std::string> {
            std::printf("\n--- %s ---\n", title.c_str());
            for (const std::string& line : lines)
                std::printf("%s\n", line.c_str());
            std::printf("%s", label.c_str());
            std::fflush(stdout);
            std::string line;
            if (!read_line(line))
                return std::nullopt;
            return line;
        };
        io.notice = [](const std::string& line) {
            std::printf("%s\n", line.c_str());
        };
        // The SET LEVEL host predicate: the text picker holds no networked
        // lobby (configure_networking is a stub), so the shared context
        // always answers host here.
        io.is_host = [this] { return label_context().is_host; };
        io.apply_level = [this](int level) {
            // The text "Set level" tail: session config + save cursor.
            config_.level = level;
            save_data_.scen_num = static_cast<short>(level);
        };
        run_terminal_campaign_camp(save_data_, io);
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
#include "../../../tests/coverage_internal/text_picker_internal.inc"
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
