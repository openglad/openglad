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

SaveData& TextPickerClient::testing_save_data_mutable()
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

    bool exhausted() const
    {
        return input_.rdbuf()->in_avail() == 0;
    }

    bool eof_observed() const
    {
        return std::cin.eof();
    }

private:
    std::istringstream input_;
    std::streambuf* saved_;
};

// TextPickerClient's protocol is deliberately C stdio based. Capture stdout
// around a single action so state-preserving error/cancel paths still have a
// behavioral oracle: the exact prompt or diagnostic must have been emitted.
class ScopedTextPickerStdoutCapture
{
public:
    ScopedTextPickerStdoutCapture()
    {
        if (std::fflush(stdout) != 0)
            return;
        capture_ = std::tmpfile();
        if (capture_ == nullptr)
            return;
        saved_fd_ = ::dup(::fileno(stdout));
        if (saved_fd_ < 0)
            return;
        if (::dup2(::fileno(capture_), ::fileno(stdout)) < 0)
            return;
        active_ = true;
        ready_ = true;
    }

    ~ScopedTextPickerStdoutCapture()
    {
        if (active_)
            (void)restore_stdout();
        if (saved_fd_ >= 0)
            (void)::close(saved_fd_);
        if (capture_ != nullptr)
            (void)std::fclose(capture_);
    }

    ScopedTextPickerStdoutCapture(
        const ScopedTextPickerStdoutCapture&) = delete;
    ScopedTextPickerStdoutCapture& operator=(
        const ScopedTextPickerStdoutCapture&) = delete;

    bool ready() const { return ready_; }

    std::string finish()
    {
        if (finished_)
            return output_;
        finished_ = true;
        if (!ready_)
            return {};

        bool ok = std::fflush(stdout) == 0;
        const bool restored = restore_stdout();
        ok = restored && ok;
        if (!restored)
            return {};
        if (saved_fd_ >= 0) {
            ok = ::close(saved_fd_) == 0 && ok;
            saved_fd_ = -1;
        }
        if (std::fseek(capture_, 0, SEEK_END) != 0)
            ok = false;
        const long size = ok ? std::ftell(capture_) : -1;
        if (size < 0)
            ok = false;
        if (ok && std::fseek(capture_, 0, SEEK_SET) != 0)
            ok = false;
        if (ok) {
            output_.resize(static_cast<std::size_t>(size));
            const std::size_t read =
                std::fread(output_.data(), 1, output_.size(), capture_);
            ok = read == output_.size();
        }
        finish_ok_ = ok;
        return output_;
    }

    bool finish_ok() const
    {
        return finished_ && finish_ok_;
    }

private:
    bool restore_stdout()
    {
        if (!active_)
            return true;
        if (saved_fd_ < 0 ||
            ::dup2(saved_fd_, ::fileno(stdout)) < 0)
            return false;
        active_ = false;
        return true;
    }

    std::FILE* capture_ = nullptr;
    int saved_fd_ = -1;
    bool active_ = false;
    bool ready_ = false;
    bool finished_ = false;
    bool finish_ok_ = false;
    std::string output_;
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
        capture_file("save/textcov-active.gtl");
        capture_file("save/textcov-active.tmp.gtl");
        capture_file("save/textcov-list.gtl");
        capture_file("save/textcov-list.tmp.gtl");
        capture_file("save/textcov-list.gtl.tmp");
        capture_file("save/textcov-list.gtl.restoretmp.tmp");
        const std::filesystem::path backup_directory =
            std::filesystem::path(get_user_path()) / "save" / "backups";
        std::error_code directory_error;
        backup_directory_existed_ =
            std::filesystem::exists(backup_directory, directory_error);
        if (directory_error)
            snapshot_ok_ = false;
        capture_textcov_list_backups(backup_directory);
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

    [[nodiscard]] bool clear_textcov_list_backups() noexcept
    {
        const std::filesystem::path backup_directory =
            std::filesystem::path(get_user_path()) /
            "save" / "backups";
        return remove_textcov_list_backups(backup_directory);
    }

    [[nodiscard]] bool restore() noexcept
    {
        if (restored_)
            return restore_ok_;

        restore_ok_ = snapshot_ok_;
        restore_ok_ =
            clear_textcov_list_backups() && restore_ok_;
        for (const FileSnapshot& file : files_)
            restore_ok_ = restore_file(file) && restore_ok_;
        if (!backup_directory_existed_) {
            std::error_code ec;
            const std::filesystem::path backup_directory =
                std::filesystem::path(get_user_path()) /
                "save" / "backups";
            (void)std::filesystem::remove(backup_directory, ec);
            std::error_code exists_error;
            const bool directory_remains =
                std::filesystem::exists(
                    backup_directory, exists_error);
            restore_ok_ = !ec && !exists_error &&
                !directory_remains && restore_ok_;
        }

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

    static bool is_textcov_list_backup(
        const std::filesystem::path& path)
    {
        return path.filename().string().starts_with(
            "textcov-list.");
    }

    void capture_textcov_list_backups(
        const std::filesystem::path& backup_directory)
    {
        if (!backup_directory_existed_)
            return;

        std::error_code ec;
        std::filesystem::directory_iterator entries(
            backup_directory, ec);
        if (ec)
        {
            snapshot_ok_ = false;
            return;
        }
        for (const std::filesystem::directory_entry& entry :
             entries)
        {
            std::error_code entry_error;
            const bool regular =
                entry.is_regular_file(entry_error);
            if (entry_error)
            {
                snapshot_ok_ = false;
                continue;
            }
            if (regular &&
                is_textcov_list_backup(entry.path()))
                capture_absolute_file(entry.path());
        }
    }

    bool remove_textcov_list_backups(
        const std::filesystem::path& backup_directory) noexcept
    {
        std::error_code exists_error;
        if (!std::filesystem::exists(
                backup_directory, exists_error))
            return !exists_error;

        std::error_code iteration_error;
        std::filesystem::directory_iterator entries(
            backup_directory, iteration_error);
        if (iteration_error)
            return false;

        std::vector<std::filesystem::path> matches;
        for (const std::filesystem::directory_entry& entry :
             entries)
        {
            std::error_code entry_error;
            const bool regular =
                entry.is_regular_file(entry_error);
            if (entry_error)
                return false;
            if (regular &&
                is_textcov_list_backup(entry.path()))
                matches.push_back(entry.path());
        }

        bool ok = true;
        for (const std::filesystem::path& path : matches)
        {
            std::error_code remove_error;
            (void)std::filesystem::remove(path, remove_error);
            ok = !remove_error && ok;
        }
        return ok;
    }

    void capture_file(const std::string& relative_path)
    {
        capture_absolute_file(
            std::filesystem::path(get_user_path()) /
            relative_path);
    }

    void capture_absolute_file(
        const std::filesystem::path& path)
    {
        FileSnapshot snapshot;
        snapshot.path = path;

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
    bool backup_directory_existed_ = false;
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
    check_difficulty_rule(PickerMenuCommand::CycleRespawnMode, 4,
        [&] { return save.respawn_mode; });
    check_difficulty_rule(PickerMenuCommand::CycleRespawnDelay, 3,
        [&] { return save.ctf_respawn_ticks; });
    check_difficulty_rule(PickerMenuCommand::TogglePermadeath, 2,
        [&] { return save.keep_fallen_heroes; });
    check_difficulty_rule(PickerMenuCommand::CycleGeneratorRate, 3,
        [&] { return save.generator_rate; });
    check_difficulty_rule(PickerMenuCommand::ToggleInfiniteGold, 2,
        [&] { return save.infinite_gold; });
    // The team-build DIFFICULTY door runs the nested submenu loop: toggle
    // permadeath twice, then back — a round trip that leaves save untouched.
    // (The door left the main menu for Base Camp —
    // docs/camp-controls-design.md.)
    if (const PickerMenuItem* door =
            find_picker_menu_item(PickerMenuId::TeamBuild,
                                  PickerMenuCommand::OpenDifficultyMenu)) {
        const short previous_permadeath = save.keep_fallen_heroes;
        ScopedCinRedirect input("4\n4\n7\n");
        client.handle_menu_item(PickerMenuId::TeamBuild, *door);
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
    // The earned-roads gate: an unearned forward id refuses and moves
    // nothing; clearing the level opens it to the same prompt.
    handle_team_item(PickerMenuCommand::SetLevel, "5\n", [&] {
        return config.level == previous_level &&
               save.scen_num == previous_level;
    });
    client.testing_save_data_mutable().add_level_completed(
        save.current_campaign, 5);
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
        const std::string slot_before = config.save_name;
        const std::uint32_t seed_before = config.seed;
        ScopedTextPickerStdoutCapture output;
        check(output.ready());
        ScopedCinRedirect input("");
        client.show_options();
        const bool input_reached_first_eof =
            input.exhausted() && input.eof_observed();
        const std::string text = output.finish();
        check(config.save_name == slot_before &&
              config.seed == seed_before &&
              input_reached_first_eof &&
              output.finish_ok() &&
              text.find("Current save slot:") != std::string::npos &&
              text.find("Current seed:") == std::string::npos);
    }
    {
        const std::string slot_before = config.save_name;
        const std::uint32_t seed_before = config.seed;
        ScopedTextPickerStdoutCapture output;
        check(output.ready());
        ScopedCinRedirect input("\n");
        client.show_options();
        const bool input_reached_second_eof =
            input.exhausted() && input.eof_observed();
        const std::string text = output.finish();
        check(config.save_name == slot_before &&
              config.seed == seed_before &&
              input_reached_second_eof &&
              output.finish_ok() &&
              text.find("Current save slot:") != std::string::npos &&
              text.find("Current seed:") != std::string::npos);
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
    // The prompt's existence check now refuses an id no campaign ships,
    // even a "completed" one; the ViewScenario failure branch is reached by
    // parking the cursor directly (the load-failure fallback's own shape).
    client.testing_save_data_mutable().add_level_completed(
        save.current_campaign, 32000);
    handle_team_item(PickerMenuCommand::SetLevel, "32000\n", [&] {
        return config.level == 1 && save.scen_num == 1;
    });
    config.level = 32000;
    client.testing_save_data_mutable().scen_num = 32000;
    {
        ScopedTextPickerStdoutCapture output;
        check(output.ready());
        handle_team_item(PickerMenuCommand::ViewScenario, "", [&] {
            return config.level == 32000 && save.scen_num == 32000;
        });
        const std::string text = output.finish();
        check(output.finish_ok() &&
            text.find("Could not load level 32000.") !=
                std::string::npos);
    }
    handle_team_item(PickerMenuCommand::SetLevel, "1\n", [&] {
        return config.level == 1 && save.scen_num == 1;
    });

    // A client without an error sink must still fail an unsafe load cleanly;
    // set_error's null branch is part of the public nullptr contract.
    {
        TextPickerConfig null_error_config;
        null_error_config.team_families = {FAMILY_SOLDIER};
        null_error_config.save_name = "../unsafe-text-picker-load";
        TextPickerClient null_error_client(null_error_config, nullptr);
        check(!null_error_client.load_game());
    }

    // Unknown commands are ignored by the Difficulty dispatcher, preserving
    // both the session difficulty and every match-rule field.
    {
        const int difficulty_before =
            og::runtime::current_session->current_difficulty_;
        const short respawn_before = save.respawn_mode;
        const PickerMenuItem unknown_difficulty{
            "unknown-difficulty", "Unknown",
            PickerMenuCommand::BeginNewGame, 0};
        client.handle_menu_item(
            PickerMenuId::Difficulty, unknown_difficulty);
        check(og::runtime::current_session->current_difficulty_ ==
                  difficulty_before &&
              save.respawn_mode == respawn_before);
    }

    // Drive the cancellation/validation branches of the company and backup
    // row prompts over a real, valid save+snapshot. Each flow must leave both
    // artifacts intact and keep this client on its original slot.
    (void)remove_user_file("save/textcov-active.gtl");
    (void)remove_user_file("save/textcov-active.tmp.gtl");
    (void)remove_user_file("save/textcov-list.gtl");
    (void)remove_user_file("save/textcov-list.tmp.gtl");
    (void)remove_user_file("save/textcov-list.gtl.tmp");
    (void)remove_user_file("save/textcov-list.gtl.restoretmp.tmp");
    check(state_restore.clear_textcov_list_backups());
    SaveData listed_company;
    listed_company.reset();
    listed_company.save_name = "TEXT COVERAGE COMPANY";
    listed_company.current_campaign = "gladiator";
    listed_company.last_played_unix_s = 9000000000000000LL;
    check(listed_company.save_with_error("textcov-list") ==
          SaveDataIoError::None);
    check(og::data::backup_company_now("textcov-list"));

    const auto check_cancelled_company_flow =
        [&check](std::string input_text,
                 std::string_view expected_output,
                 bool expected_eof) {
            TextPickerConfig list_config;
            list_config.team_families = {FAMILY_SOLDIER};
            list_config.save_name = "textcov-active";
            TextPickerError list_error;
            TextPickerClient list_client(list_config, &list_error);
            ScopedTextPickerStdoutCapture output;
            check(output.ready());
            ScopedCinRedirect input(std::move(input_text));
            const bool opened = list_client.show_company_list();
            const bool input_oracle =
                input.exhausted() &&
                input.eof_observed() == expected_eof;
            const std::string text = output.finish();
            check(!opened &&
                list_config.save_name == "textcov-active" &&
                input_oracle &&
                output.finish_ok() &&
                text.find(expected_output) != std::string::npos &&
                user_file_exists("save/textcov-list.gtl") &&
                og::data::list_company_backups(
                    "textcov-list").size() == 1);
        };
    check_cancelled_company_flow(
        "1\n", "Open which company", true);
    check_cancelled_company_flow(
        "1\n99\n4\n", "Invalid company selection.", false);
    check_cancelled_company_flow(
        "2\n", "Backups for which company", true);
    check_cancelled_company_flow(
        "2\n1\n1\n", "Restore which backup", true);
    check_cancelled_company_flow(
        "2\n1\n1\n99\n3\n4\n",
        "Invalid backup selection.", false);
    check_cancelled_company_flow(
        "3\n", "Delete which company", true);
    check_cancelled_company_flow(
        "2\n1\n2\n", "Delete which backup", true);

    config.team_families.clear();
    {
        ScopedCinRedirect input("");
        const PickerMenuItem* item = client.present_menu(PickerMenuId::TeamBuild);
        check(item != nullptr && item->command == PickerMenuCommand::Back &&
              config.team_families.size() == 1 &&
              config.team_families.front() == FAMILY_SOLDIER);
    }

    // The scenario-troops row retired with its knob (amendment B5); teams
    // and target score moved to the camp's MATCH SETUP page
    // (docs/camp-controls-design.md). Neither is dispatched here any more.
    config.campaign = "modes";
    check(client.save_game() &&
          save.current_campaign == "modes");
    const short previous_ctf_teams = save.ctf_team_count;
    // Amendment A1/A3: the Matchup dump carries NO teams label any more.
    // TEAMS is not a control, and a readout of a setting nothing writes is
    // just a lie waiting to happen — the team rows themselves are the whole
    // screen now. The dump still only reads.
    {
        ScopedTextPickerStdoutCapture output;
        check(output.ready());
        handle_team_item(PickerMenuCommand::Teams, "\n", [&] {
            return save.ctf_team_count == previous_ctf_teams;
        });
        const std::string text = output.finish();
        check(output.finish_ok() &&
            text.find("--- Matchup ---") != std::string::npos &&
            text.find("Teams:") == std::string::npos);
    }
    {
        ScopedTextPickerStdoutCapture output;
        check(output.ready());
        handle_team_item(PickerMenuCommand::ViewScenario, "", [&] {
            return config.campaign == "modes";
        });
        const std::string text = output.finish();
        check(output.finish_ok() &&
            text.find(
                "Campaign 'modes' is not mounted.") !=
                std::string::npos);
    }

    check(state_restore.restore());

    return failed ? -failed_check : 0;
}

// Staged VIEW LEVEL (#218, C10): the text census is a locally STAGED world
// (one-shot MatchStage over this save, seeded by config.seed), so it lists
// the real assembled match and the same seed prints the identical census
// twice. Returns 0, or the negated 1-based index of the first failed check.
int text_picker_testing_staged_view_scenario()
{
    ScopedTextPickerInternalState state_restore;
    if (!state_restore.ready())
        return -1;
    if (mount_campaign_package_with_error("modes") !=
        CampaignPackageIoError::None)
        return -2;

    TextPickerConfig config;
    config.campaign = "modes";
    config.level = 820;
    config.seed = 777u;
    config.team_families = {FAMILY_SOLDIER};
    config.save_name = "helper-slot";
    TextPickerError error;
    TextPickerClient client(config, &error);
    client.testing_save_data_mutable().current_campaign = "modes";
    client.testing_save_data_mutable().scen_num = 820;

    int check_index = 0;
    int failed_check = 0;
    bool failed = false;
    const auto check = [&check_index, &failed_check, &failed](bool condition) {
        ++check_index;
        if (!condition) {
            failed = true;
            if (failed_check == 0)
                failed_check = check_index;
        }
    };

    const auto run_view_scenario = [&]() -> std::string {
        const PickerMenuItem* item = find_picker_menu_item(
            PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
        if (item == nullptr)
            return {};
        ScopedTextPickerStdoutCapture output;
        if (!output.ready())
            return {};
        ScopedCinRedirect input("\n");
        client.handle_menu_item(PickerMenuId::Scenario, *item);
        std::string text = output.finish();
        if (!output.finish_ok())
            return {};
        return text;
    };

    const std::string first = run_view_scenario();
    const std::string second = run_view_scenario();
    // The staged census header and the soccer census over the 820 pitch:
    // one deployed soldier = COMPANY on the roster team, a FILL squad
    // backfilling the other. That squad is MATCHED BOTS (1) FAIR, not the
    // BOT SQUAD (5) this arm pinned before Amendment 2: the deployed soldier
    // IS human power, so B3's reference exists and the default FILL: FAIR
    // solves one bot against it. BOT SQUAD is the legacy-formula label, and
    // W5-A left it reachable only where no human power exists at all.
    check(first.find("--- SCEN 820:") != std::string::npos);
    check(first.find("MATCH: SOCCER - 2 TEAMS ACTIVE") != std::string::npos);
    check(first.find("ACTIVE - COMPANY (1)") != std::string::npos);
    check(first.find("ACTIVE - MATCHED BOTS (1) FAIR") != std::string::npos);
    // Seat block (#218): the text client stages locally, so the seat lines
    // are the save-derived synthesis — one all-local seat, directly after
    // the match block.
    check(first.find("SEATS: CO-OP") != std::string::npos);
    check(first.find("  P1 YOU - RED TEAM") != std::string::npos);
    check(first.find("MATCH: SOCCER - 2 TEAMS ACTIVE") <
          first.find("SEATS: CO-OP"));
    // Determinism: the session-latched seed makes the census a pure
    // function of (save, knobs, seed) — reopening the viewer never
    // flickers a single byte.
    check(!first.empty());
    check(first == second);

    // Degradation (#218): an oversize completed-levels ledger refuses the
    // stage at the wire cap, so VIEW LEVEL degrades to the fallback census
    // over the scratch load, led by the honest STAGING FAILED line — no
    // mode census, never a stale staged world. Shrinking the ledger heals
    // the viewer back to the byte-identical staged census.
    std::set<int>& ledger =
        client.testing_save_data_mutable().completed_levels["modes"];
    for (int level = 100000; level < 117000; ++level)
        ledger.insert(level);
    const std::string degraded = run_view_scenario();
    check(degraded.find("STAGING FAILED") != std::string::npos);
    check(degraded.find("--- SCEN 820:") != std::string::npos);
    check(degraded.find("MATCH: SOCCER") == std::string::npos);
    ledger.clear();
    const std::string healed = run_view_scenario();
    check(healed == first);

    check(state_restore.restore());
    return failed ? -failed_check : 0;
}

// One seed, both consumers (#218 §1.3): VIEW LEVEL stages its preview on
// TextPickerConfig::seed and the GO path runs the protocol session on the
// SAME latch, so the text preview is never a differently seeded world than
// the text launch. The protocol session reports the seed it ran with in its
// ready line — the observable that splits the moment either side starts
// drawing its own. Returns 0, or the negated 1-based index of the first
// failed check.
int text_picker_testing_launch_seed_matches_the_preview()
{
    ScopedTextPickerInternalState state_restore;
    if (!state_restore.ready())
        return -1;
    if (mount_campaign_package_with_error("modes") !=
        CampaignPackageIoError::None)
        return -2;

    int check_index = 0;
    int failed_check = 0;
    bool failed = false;
    const auto check = [&check_index, &failed_check, &failed](bool condition) {
        ++check_index;
        if (!condition) {
            failed = true;
            if (failed_check == 0)
                failed_check = check_index;
        }
    };

    // Both halves of one session at a given latch: the previewed census and
    // the launch's ready line.
    const auto preview_and_launch =
        [](std::uint32_t seed) -> std::pair<std::string, std::string> {
        TextPickerConfig config;
        config.campaign = "modes";
        config.level = 820;
        config.seed = seed;
        config.team_families = {FAMILY_SOLDIER};
        config.save_name = "helper-slot";
        TextPickerError error;
        TextPickerClient client(config, &error);
        client.testing_save_data_mutable().current_campaign = "modes";
        client.testing_save_data_mutable().scen_num = 820;

        const PickerMenuItem* const item = find_picker_menu_item(
            PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
        if (item == nullptr)
            return {};

        std::string preview;
        {
            ScopedTextPickerStdoutCapture output;
            if (!output.ready())
                return {};
            ScopedCinRedirect input("\n");
            client.handle_menu_item(PickerMenuId::Scenario, *item);
            preview = output.finish();
            if (!output.finish_ok())
                return {};
        }

        std::string launch;
        {
            ScopedTextPickerStdoutCapture output;
            if (!output.ready())
                return {};
            // EOF stdin: the protocol session prints its ready line (seed
            // included) and returns without entering the command loop.
            ScopedCinRedirect input("");
            // #247: the GO path's own call — this client's save and
            // difficulty, so the launch stages the world the preview above
            // just staged instead of building its own.
            (void)run_text_picker_protocol_session(config,
                &client.testing_save_data(),
                og::runtime::current_session != nullptr
                    ? og::runtime::current_session->current_difficulty_
                    : 1);
            launch = output.finish();
            if (!output.finish_ok())
                return {};
        }
        return {preview, launch};
    };

    const auto [preview_777, launch_777] = preview_and_launch(777u);
    const auto [preview_4242, launch_4242] = preview_and_launch(4242u);

    check(launch_777.find("\"seed\":777") != std::string::npos);
    check(launch_4242.find("\"seed\":4242") != std::string::npos);
    // The previews are seed-sensitive (staged squads are drawn at stage
    // time), so a preview that ignored the latch would print the same census
    // for both.
    check(!preview_777.empty());
    check(preview_777.find("--- SCEN 820:") != std::string::npos);
    check(preview_4242.find("--- SCEN 820:") != std::string::npos);
    check(preview_777 != preview_4242);

    check(state_restore.restore());
    return failed ? -failed_check : 0;
}

// #247: GO launches the world VIEW LEVEL promised. The text launch used to
// copy six scalars out of the session config and build its own world from a
// fresh default SaveData, so every match knob the picker had written into
// the save died on the way to the launch: the preview showed MATCHED BOTS
// (1) per team and the launch fielded the legacy five-strong squads. Both
// halves now run the one staged pipeline on the one session seed, so the
// previewed per-team census IS the launched world's headcount.
//
// The levers are Amendment 2's two band controls. (a) the FILL wheel on the
// four-team soccer pitch — matched squads in the preview, matched squads in
// the world, and one team's NONE drops its bot from both. (b) the MAP UNITS
// box on the onslaught map, which authors a real cast — the launch keeps the
// authored troops the preview lists, and unchecking one team's box strips
// exactly that team's pair from BOTH halves.
// Returns 0, or the negated 1-based index of the first failed check.
int text_picker_testing_launch_census_matches_the_preview()
{
    ScopedTextPickerInternalState state_restore;
    if (!state_restore.ready())
        return -1;
    if (mount_campaign_package_with_error("modes") !=
        CampaignPackageIoError::None)
        return -2;

    int check_index = 0;
    int failed_check = 0;
    bool failed = false;
    const auto check = [&check_index, &failed_check, &failed](bool condition) {
        ++check_index;
        if (!condition) {
            failed = true;
            if (failed_check == 0)
                failed_check = check_index;
        }
    };

    // Digits of the first unsigned integer at or after `from`.
    const auto scan_int = [](const std::string& text, std::size_t from) {
        int value = 0;
        while (from < text.size() && (text[from] < '0' || text[from] > '9'))
            ++from;
        while (from < text.size() && text[from] >= '0' && text[from] <= '9')
            value = value * 10 + (text[from++] - '0');
        return value;
    };

    // How many times `needle` appears in `text` (non-overlapping).
    const auto count_occurrences = [](const std::string& text,
                                      const std::string& needle) {
        int count = 0;
        for (std::size_t pos = text.find(needle); pos != std::string::npos;
             pos = text.find(needle, pos + needle.size()))
            ++count;
        return count;
    };

    // The previewed per-team headcounts in team order: the parenthesised
    // count on each "<COLOR> TEAM  ACTIVE - <LABEL> (N)" line.
    const auto previewed_counts =
        [&scan_int](const std::string& text) -> std::vector<int> {
        std::vector<int> counts;
        for (std::size_t pos = text.find(" TEAM  ACTIVE - ");
             pos != std::string::npos;
             pos = text.find(" TEAM  ACTIVE - ", pos + 1)) {
            const std::size_t open = text.find('(', pos);
            if (open == std::string::npos)
                return {};
            counts.push_back(scan_int(text, open));
        }
        return counts;
    };

    // The launched world's per-team headcounts: the census command's
    // team_counts array, trimmed of its trailing empty teams.
    const auto launched_counts =
        [&scan_int](const std::string& text) -> std::vector<int> {
        const std::string key = "\"team_counts\":[";
        const std::size_t open = text.find(key);
        if (open == std::string::npos)
            return {};
        const std::size_t close = text.find(']', open);
        if (close == std::string::npos)
            return {};
        std::vector<int> counts;
        for (std::size_t pos = open + key.size(); pos < close;) {
            counts.push_back(scan_int(text, pos));
            const std::size_t comma = text.find(',', pos);
            if (comma == std::string::npos || comma > close)
                break;
            pos = comma + 1;
        }
        while (!counts.empty() && counts.back() == 0)
            counts.pop_back();
        return counts;
    };

    struct Halves {
        std::string preview;
        std::vector<int> previewed;
        std::vector<int> launched;
    };

    // One session at one knob setting: what VIEW LEVEL promised, and what
    // GO built.
    using TeamKnob = std::array<std::int16_t, SCORE_TEAM_COUNT>;
    const auto preview_and_launch =
        [&](int level, const TeamKnob& fill,
            const TeamKnob& map_units) -> Halves {
        TextPickerConfig config;
        config.campaign = "modes";
        config.level = level;
        config.seed = 1234u;
        config.team_families = {FAMILY_SOLDIER};
        config.save_name = "helper-slot";
        TextPickerError error;
        TextPickerClient client(config, &error);
        client.testing_save_data_mutable().current_campaign = "modes";
        client.testing_save_data_mutable().scen_num =
            static_cast<short>(level);
        client.testing_save_data_mutable().fill = fill;
        client.testing_save_data_mutable().map_units = map_units;
        const int difficulty = og::runtime::current_session != nullptr
            ? og::runtime::current_session->current_difficulty_
            : 1;

        const PickerMenuItem* const item = find_picker_menu_item(
            PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
        if (item == nullptr)
            return {};

        std::string preview;
        {
            ScopedTextPickerStdoutCapture output;
            if (!output.ready())
                return {};
            ScopedCinRedirect input("\n");
            client.handle_menu_item(PickerMenuId::Scenario, *item);
            preview = output.finish();
            if (!output.finish_ok())
                return {};
        }

        std::string launch;
        {
            ScopedTextPickerStdoutCapture output;
            if (!output.ready())
                return {};
            ScopedCinRedirect input("census\nquit\n");
            (void)run_text_picker_protocol_session(config,
                &client.testing_save_data(), difficulty);
            launch = output.finish();
            if (!output.finish_ok())
                return {};
        }
        return {preview, previewed_counts(preview), launched_counts(launch)};
    };

    // (a) The four-team soccer pitch at rest. Every knob is at its default —
    // FILL: FAIR on all four, MAP UNITS on — and B3's matched world is what
    // that default now means: the company on RED, one solved bot on each of
    // the other three. Before the caller split the launch fielded [1,5,5,5]
    // here.
    const Halves fair = preview_and_launch(822, TeamKnob{}, TeamKnob{});
    check(fair.preview.find("MATCHED BOTS (1)") != std::string::npos);
    check(fair.previewed == std::vector<int>({1, 1, 1, 1}));
    // A harness failure returns two empty censuses, and empty == empty would
    // read as agreement — pin both halves non-empty before comparing them.
    check(!fair.previewed.empty() && !fair.launched.empty());
    check(fair.launched == fair.previewed);

    // The knob is doing the work, not a pitch that happens to field ones:
    // one team's wheel to FILL: NONE and its bot leaves BOTH halves, so the
    // pitch previews and launches three-a-side.
    //
    // Intent history: this arm used to turn TROOPS off and pin the legacy
    // five-strong squads on both sides. Amendment 2's B5 retired TROOPS and
    // W5-A made the matched world the DEFAULT world, so there is no longer a
    // "legacy squads" state to compare against — FILL is the live wheel, and
    // NONE is its only value that moves a headcount (B2 sizes every other
    // value by the same matched-headcount rule and varies only the level).
    const Halves none_on_yellow = preview_and_launch(
        822, TeamKnob{0, 0, 0, og::sim::kFillNone}, TeamKnob{});
    check(none_on_yellow.preview.find("MATCHED BOTS (1)") !=
          std::string::npos);
    check(none_on_yellow.preview.find("YELLOW TEAM") == std::string::npos);
    check(none_on_yellow.previewed == std::vector<int>({1, 1, 1}));
    check(!none_on_yellow.previewed.empty() &&
          !none_on_yellow.launched.empty());
    check(none_on_yellow.launched == none_on_yellow.previewed);

    // (b) The MAP UNITS box, per team. The onslaught map is the one mode
    // level that ships real map troops (the arenas are empty), so it is the
    // only place the box's two states are distinguishable at all. With every
    // box on, the two authored fighters the preview lists are alive in the
    // launched world (RED carries the company plus the map's own two
    // allies).
    //
    // The headline census and the launch disagree on RED by exactly those
    // two: RED's ACTIVE line counts the COMPANY, while the roster pane below
    // it lists the map's pair. So the pane, not the line, is the preview's
    // honest half here, and both are pinned.
    const Halves authored = preview_and_launch(801, TeamKnob{}, TeamKnob{});
    check(authored.preview.find("MAP TROOPS (2)") != std::string::npos);
    check(authored.previewed == std::vector<int>({1, 2}));
    check(authored.launched == std::vector<int>({3, 2}));
    check(count_occurrences(authored.preview, "2x SOLDIER Lv 3") == 2);

    // ...and unchecking RED's box removes exactly them from both halves: the
    // roster pane loses RED's pair (one listing left, GREEN's) and the
    // launched world fields the company alone on RED. GREEN, whose box is
    // untouched, keeps its two — the box is per team, and the match still
    // stands.
    const Halves red_units_off = preview_and_launch(
        801, TeamKnob{}, TeamKnob{og::sim::kMapUnitsOff, 0, 0, 0});
    check(count_occurrences(red_units_off.preview, "2x SOLDIER Lv 3") == 1);
    check(red_units_off.preview.find("GREEN TEAM  ACTIVE - MAP TROOPS (2)") !=
          std::string::npos);
    check(red_units_off.launched == std::vector<int>({1, 2}));

    check(state_restore.restore());
    return failed ? -failed_check : 0;
}
