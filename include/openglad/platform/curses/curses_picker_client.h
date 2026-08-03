/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/interface/ui/picker.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/platform/curses/clock.h>
#include <openglad/platform/curses/terminal.h>
#include <openglad/resources/save_data.h>

#include <memory>
#include <string>

namespace og::data {
struct CompanyInfo;
}

namespace og::curses {

class CursesLobby;

// Tunables / launch configuration for the curses picker.
struct CursesPickerOptions {
    bool allow_unicode = true;
    bool allow_color = true;
    int difficulty = 1; // index into the difficulty table
    int host_port = 0;  // 0 => default WebSocket port
};

// Terminal front-end for the shared picker state machine. Implements
// og::ui::IPickerClient, rendering rich menus via ITerminal and reading keys
// from it, while reusing all shared menu definitions and business logic
// (menu_model, picker_common: HireSession/TrainSession/team ops). This is the
// curses analogue of TextPickerClient.
class CursesPickerClient final : public og::ui::IPickerClient
{
public:
    CursesPickerClient(ITerminal& term, IClock& clock,
                       og::ui::TextPickerConfig& config,
                       const CursesPickerOptions& options);

    // --- og::ui::IPickerClient ---
    const og::ui::PickerMenuItem* present_menu(og::ui::PickerMenuId menu_id) override;
    void handle_menu_item(og::ui::PickerMenuId menu_id,
                          const og::ui::PickerMenuItem& item) override;
    bool prepare_new_game() override;
    bool configure_networking() override;
    bool host_game() override;
    bool join_game() override;
    std::string show_campaign_select() override;
    void show_options() override;
    void show_help() override;
    void run_game() override;
    bool load_game() override;
    bool save_game() override;
    bool show_company_list() override;
    og::ui::PickerScreen screen_after_game() const override;

    // Accessors for tests.
    const SaveData& save_data() const { return save_data_; }
    SaveData& save_data() { return save_data_; }
    int difficulty() const { return options_.difficulty; }

private:
    ITerminal& term_;
    IClock& clock_;
    og::ui::TextPickerConfig& config_;
    CursesPickerOptions options_;
    SaveData save_data_;

    // [SAVE-R2] Re-points the process-wide active-company slot at this
    // client's configured slot (default "curses_quicksave" via CursesApp).
    void assert_company_slot_authority() const;

    // §2.4 Backups sub-view for one company (entered from the company list's
    // Backups chrome). Returns true when a restore rewound — and repointed
    // this client's slot to — the company, so the caller proceeds to team
    // build.
    bool show_company_backups(const og::data::CompanyInfo& company);

    // #155 cloud saves: the terminal open path for a freshly downloaded
    // company (repoint the slot authority + load; restore the previous slot
    // on failure — the show_company_list discipline).
    bool open_downloaded_company(const std::string& slot);

    // Drive a host/join lobby to a started game, then run the networked level.
    void run_network_lobby(std::unique_ptr<CursesLobby> lobby);
};

// Convenience entry point: construct a CursesPickerClient and run the shared
// picker state machine until the user quits.
void run_curses_picker(ITerminal& term, IClock& clock,
                       og::ui::TextPickerConfig& config,
                       const CursesPickerOptions& options);

} // namespace og::curses
