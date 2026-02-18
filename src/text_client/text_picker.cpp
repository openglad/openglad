/* Text-based picker implementation for the headless client.
 *
 * Provides a simple stdin/stdout menu interface that drives the same
 * picker state machine as the SDL client (og::ui::IPickerClient).
 *
 * Copyright (C) 1995-2002 FSGames. Ported by Sean Ford and Yan Shosh.
 * Licensed under GPL v2.
 */

#include <openglad/core/constants.h>
#include <openglad/ui/picker.h>
#include <openglad/ui/picker_state.h>

#include <cstdio>
#include <iostream>
#include <string>

namespace og::ui {

class TextPickerClient final : public IPickerClient
{
public:
    explicit TextPickerClient(TextPickerConfig& config)
        : config_(config) {}

    bool play_requested() const { return play_requested_; }

    MainMenuAction show_main_menu() override
    {
        std::printf("\n=== OpenGlad (Text Mode) ===\n");
        std::printf("  1. Continue Game\n");
        std::printf("  2. New Game\n");
        std::printf("  3. Load Game\n");
        std::printf("  4. Save Game\n");
        std::printf("  5. View Team\n");
        std::printf("  6. Options\n");
        std::printf("  7. Help\n");
        std::printf("  8. Quit\n");
        std::printf("Choice: ");
        std::fflush(stdout);

        int choice = 0;
        if (std::scanf("%d", &choice) != 1)
            return MainMenuAction::Quit; // EOF

        switch (choice) {
        case 1:
            play_requested_ = true;
            return MainMenuAction::Quit;
        case 2:
            play_requested_ = true;
            return MainMenuAction::Quit;
        case 3: return MainMenuAction::LoadGame;
        case 4: return MainMenuAction::SaveGame;
        case 5: return MainMenuAction::ViewTeam;
        case 6: return MainMenuAction::Options;
        case 7: return MainMenuAction::Help;
        case 8: return MainMenuAction::Quit;
        default:
            std::printf("Invalid choice.\n");
            return MainMenuAction::Quit;
        }
    }

    void show_team_build() override
    {
        std::printf("\n--- Team Build ---\n");
        std::printf("(Team building not available in text mode.)\n");
        std::printf("Use --team <fam1,fam2,...> on the command line.\n");
        if (config_.team_families.empty())
            config_.team_families.push_back(FAMILY_SOLDIER);
    }

    std::string show_campaign_select() override
    {
        std::printf("\n--- Campaign Select ---\n");
        std::printf("Enter campaign ID (or empty for default): ");
        std::fflush(stdout);
        std::string campaign;
        std::getline(std::cin, campaign);
        if (campaign.empty())
            campaign = "org.openglad.gladiator";
        config_.campaign = campaign;
        return campaign;
    }

    void show_options() override
    {
        std::printf("\n--- Options ---\n");
        std::printf("(Options not available in text mode.)\n");
    }

    void show_help() override
    {
        std::printf("\n--- Help ---\n");
        std::printf("OpenGlad headless text client.\n");
        std::printf("Use 'tick N' to advance simulation, 'state' to view entities.\n");
    }

    void run_game() override
    {
        std::printf("\n--- Game ---\n");
        std::printf("(Use the non-interactive mode: openglad_text --level N --seed S)\n");
    }

    bool load_game() override
    {
        std::printf("(Load not available in text mode.)\n");
        return false;
    }

    bool save_game() override
    {
        std::printf("(Save not available in text mode.)\n");
        return false;
    }

private:
    TextPickerConfig& config_;
    bool play_requested_ = false;
};

bool run_text_picker(TextPickerConfig& config, TextPickerError* error)
{
    if (error)
        *error = {};
    if (config.team_families.empty())
        config.team_families.push_back(FAMILY_SOLDIER);

    TextPickerClient client(config);
    run_picker(client);
    return client.play_requested();
}

} // namespace og::ui
