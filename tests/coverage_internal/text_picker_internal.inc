/* Test-only same-translation-unit harness for TextPickerClient.
 *
 * This file is included by text_picker.cpp under TESTING so it can exercise
 * the private concrete client without adding test hooks to production APIs.
 * Keeping the orchestration in tests/ also keeps coverage attribution honest.
 */

const SaveData& TextPickerClient::testing_save_data() const
{
    return save_data_;
}

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

class ScopedTextPickerInternalState
{
public:
    ScopedTextPickerInternalState()
        : active_company_before_(og::data::active_company_slot())
        , mounted_campaign_before_(get_mounted_campaign())
        , session_(og::runtime::current_session)
        , difficulty_before_(session_ != nullptr
              ? session_->current_difficulty_
              : 0)
        , world_(session_ != nullptr ? session_->game_.world : nullptr)
        , world_difficulty_before_(world_ != nullptr ? world_->difficulty : 0)
    {
        capture_file("save/missing-text-picker-save.gtl");
        capture_file("save/missing-text-picker-save.tmp.gtl");
        capture_file("save/helper-slot.gtl");
        capture_file("save/helper-slot.tmp.gtl");
    }

    ~ScopedTextPickerInternalState()
    {
        (void)restore();
    }

    ScopedTextPickerInternalState(const ScopedTextPickerInternalState&) = delete;
    ScopedTextPickerInternalState& operator=(
        const ScopedTextPickerInternalState&) = delete;

    [[nodiscard]] bool ready() const noexcept
    {
        return snapshot_ok_;
    }

    [[nodiscard]] bool restore() noexcept
    {
        if (restored_)
            return restore_ok_;

        restore_ok_ = snapshot_ok_;
        for (const FileSnapshot& file : files_)
            restore_ok_ = restore_file(file) && restore_ok_;

        const std::string mounted_after = get_mounted_campaign();
        if (mounted_after != mounted_campaign_before_)
        {
            const CampaignPackageIoError mount_result =
                mounted_campaign_before_.empty()
                    ? unmount_campaign_package_with_error(mounted_after)
                    : mount_campaign_package_with_error(
                          mounted_campaign_before_);
            restore_ok_ =
                mount_result == CampaignPackageIoError::None && restore_ok_;
        }

        if (session_ != nullptr)
            session_->current_difficulty_ = difficulty_before_;
        if (world_ != nullptr)
            world_->difficulty = world_difficulty_before_;
        restore_ok_ =
            og::data::set_active_company_slot(active_company_before_) &&
            restore_ok_;

        restored_ = true;
        return restore_ok_;
    }

private:
    struct FileSnapshot {
        std::filesystem::path path;
        bool existed = false;
        std::vector<char> contents;
    };

    void capture_file(const std::string& relative_path)
    {
        FileSnapshot snapshot;
        snapshot.path =
            std::filesystem::path(get_user_path()) / relative_path;

        std::error_code ec;
        snapshot.existed = std::filesystem::exists(snapshot.path, ec);
        if (ec)
        {
            snapshot_ok_ = false;
            files_.push_back(std::move(snapshot));
            return;
        }

        if (snapshot.existed)
        {
            std::ifstream input(snapshot.path, std::ios::binary);
            if (!input)
            {
                snapshot_ok_ = false;
            }
            else
            {
                snapshot.contents.assign(
                    std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
                snapshot_ok_ = !input.bad() && snapshot_ok_;
            }
        }
        files_.push_back(std::move(snapshot));
    }

    bool restore_file(const FileSnapshot& snapshot) noexcept
    {
        std::error_code ec;
        if (!snapshot.existed)
        {
            (void)std::filesystem::remove(snapshot.path, ec);
            return !ec;
        }

        std::filesystem::create_directories(snapshot.path.parent_path(), ec);
        if (ec)
            return false;

        std::filesystem::path temporary = snapshot.path;
        temporary += std::format(
            ".text-picker-restore-{}",
            reinterpret_cast<std::uintptr_t>(this));
        {
            std::ofstream output(
                temporary,
                std::ios::binary | std::ios::trunc);
            if (!output)
                return false;
            if (!snapshot.contents.empty())
            {
                output.write(
                    snapshot.contents.data(),
                    static_cast<std::streamsize>(snapshot.contents.size()));
            }
            output.close();
            if (!output)
            {
                std::error_code cleanup_error;
                (void)std::filesystem::remove(temporary, cleanup_error);
                return false;
            }
        }

        std::filesystem::rename(temporary, snapshot.path, ec);
        if (!ec)
            return true;

        ec.clear();
        std::filesystem::copy_file(
            temporary,
            snapshot.path,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        std::error_code cleanup_error;
        (void)std::filesystem::remove(temporary, cleanup_error);
        return !ec;
    }

    std::string active_company_before_;
    std::string mounted_campaign_before_;
    og::runtime::SessionState* session_ = nullptr;
    int difficulty_before_ = 0;
    GameWorld* world_ = nullptr;
    short world_difficulty_before_ = 0;
    std::vector<FileSnapshot> files_;
    bool snapshot_ok_ = true;
    bool restored_ = false;
    bool restore_ok_ = true;
};

int text_picker_testing_exercise_internal_paths()
{
    ScopedTextPickerInternalState state_restore;
    if (!state_restore.ready())
        return -1;

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
    {
        const std::string submitted_name = "A COMPANY NAME THAT IS TOO LONG";
        ScopedCinRedirect input("reroll\n" + submitted_name + "\n");
        check(client.prepare_new_game() &&
              save.save_name ==
                  submitted_name.substr(0, og::ui::kCompanyNameMaxLen));
    }
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
    // Seat lifecycle moved to Base Camp. Keep legacy dispatch branches
    // covered for old callers, but assert that the terminal menu no longer
    // exposes either operation as a selectable main-menu row.
    check(find_picker_menu_item(
              PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 2) == nullptr);
    const PickerMenuItem legacy_player_mode{
        "legacy-player-mode", "Legacy Player Mode",
        PickerMenuCommand::SetPlayerMode, 2};
    client.handle_menu_item(PickerMenuId::Main, legacy_player_mode);
    check(save.numplayers == 1);
    check(find_picker_menu_item(
              PickerMenuId::Main, PickerMenuCommand::ToggleAlliedMode) == nullptr);
    const bool previous_allied_mode = is_allied_mode(save);
    const PickerMenuItem legacy_allied_mode{
        "legacy-allied-mode", "Legacy Allied Mode",
        PickerMenuCommand::ToggleAlliedMode, 0};
    client.handle_menu_item(PickerMenuId::Main, legacy_allied_mode);
    check(is_allied_mode(save) == previous_allied_mode);
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
    handle_team_item(PickerMenuCommand::HireTroops, "\nn\np\nh\nb\n", [&] {
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
    config.save_name = "../unsafe-helper-slot";
    check(!client.save_game() &&
          error.code == TextPickerErrorCode::SaveIoError &&
          !error.detail.empty());
    config.save_name = "helper-slot";
    check(client.save_game() && error.code == TextPickerErrorCode::None);
    check(og::data::active_company_slot() == "helper-slot");
    check(client.load_game() &&
          error.code == TextPickerErrorCode::None &&
          !config.team_families.empty());

    handle_team_item(PickerMenuCommand::SetLevel, "1\n", [&] {
        return config.level == 1 && save.scen_num == 1;
    });
    const int scenario_level = config.level;
    handle_team_item(PickerMenuCommand::ViewScenario, "\n", [&] {
        return config.level == scenario_level;
    });

    config.team_families.clear();
    {
        ScopedCinRedirect input("");
        const PickerMenuItem* item = client.present_menu(PickerMenuId::TeamBuild);
        check(item != nullptr && item->command == PickerMenuCommand::Back &&
              config.team_families.size() == 1 &&
              config.team_families.front() == FAMILY_SOLDIER);
    }

    // Exercise the CTF-only settings through the same terminal gate and
    // dispatch path users reach from the final three Base Camp rows.
    config.campaign = "org.openglad.ctf";
    check(client.save_game() &&
          save.current_campaign == "org.openglad.ctf");
    const short previous_ctf_teams = save.ctf_team_count;
    handle_team_item(PickerMenuCommand::CycleCtfTeamCount, "", [&] {
        return save.ctf_team_count != previous_ctf_teams;
    });
    const short previous_ctf_limit = save.ctf_capture_limit;
    handle_team_item(PickerMenuCommand::CycleCtfCaptureLimit, "", [&] {
        return save.ctf_capture_limit != previous_ctf_limit;
    });
    const short previous_ctf_troops = save.ctf_strip_scenario_troops;
    handle_team_item(PickerMenuCommand::ToggleCtfScenarioTroops, "", [&] {
        return save.ctf_strip_scenario_troops != previous_ctf_troops;
    });
    handle_team_item(PickerMenuCommand::Teams, "\n", [&] {
        return save.ctf_team_count != previous_ctf_teams;
    });
    handle_team_item(PickerMenuCommand::ViewScenario, "", [&] {
        return config.campaign == "org.openglad.ctf";
    });

    check(state_restore.restore());

    return failed ? -failed_check : 0;
}
