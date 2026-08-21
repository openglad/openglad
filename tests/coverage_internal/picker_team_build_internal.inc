#include <openglad/platform/sai2x.h>

#include <cstdio>
#include <filesystem>

void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
int picker_testing_yes_or_no_queue_remaining();

int picker_team_build_testing_exercise_internal_paths()
{
    struct TestLobbyClient final : og::ui::IPickerLobbyClient
    {
        bool host_visible = false;
        bool has_start_config = true;
        bool editable = true;
        bool requested = false;
        bool networked = false;
        std::vector<og::sim::LobbyPlayer> players;
        std::vector<std::uint8_t> local_indices;

        void initialize_from_save() override {}
        void shutdown() override {}
        void sync_from_save() override {}
        void sync_roster_from_save() override {}
        void sync_settings_from_save() override {}
        void poll_and_apply() override {}
        void set_player_mode(int) override {}
        bool request_start_game() override
        {
            requested = true;
            return true;
        }
        std::optional<og::ui::PickerLobbyGameStartConfig>
        build_game_start_config() const override
        {
            return has_start_config
                ? std::optional<og::ui::PickerLobbyGameStartConfig>{og::ui::PickerLobbyGameStartConfig{}}
                : std::nullopt;
        }
        std::optional<og::ui::PickerLobbyGameStartConfig>
        consume_game_start_config() override
        {
            return build_game_start_config();
        }
        bool start_request_pending() const noexcept override
        {
            return false;
        }
        bool has_game_start_config() const noexcept override
        {
            return has_start_config;
        }
        bool host_controls_visible() const noexcept override
        {
            return host_visible;
        }
        bool is_save_slot_editable(std::size_t) const noexcept override
        {
            return editable;
        }
        bool is_networked_session() const noexcept override
        {
            return networked;
        }
        std::vector<og::sim::LobbyPlayer> lobby_players() const override
        {
            return players;
        }
        std::vector<std::uint8_t> local_player_indices() const override
        {
            return local_indices;
        }
    };

    struct LobbyGuard
    {
        og::ui::IPickerLobbyClient* saved = og::ui::active_picker_lobby_client();
        ~LobbyGuard()
        {
            og::ui::install_active_picker_lobby_client(saved);
        }
    } lobby_guard;

    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
            failed_check = check_index;
    };
    TestLobbyClient client;
    og::ui::install_active_picker_lobby_client(&client);
    client.initialize_from_save();
    client.sync_from_save();
    client.sync_roster_from_save();
    client.sync_settings_from_save();
    client.poll_and_apply();
    client.set_player_mode(2);
    check(client.request_start_game() && client.requested);
    check(client.build_game_start_config().has_value());
    check(client.consume_game_start_config().has_value());
    check(!client.start_request_pending());
    client.shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    struct SharedStateGuard
    {
        SaveData& save;
        og::runtime::SessionState& session;
        PickerState& picker;
        unsigned char team_size = save.team_size;
        unsigned char numplayers = save.numplayers;
        short my_team = save.my_team;
        short scen_num = save.scen_num;
        std::int16_t cross_control = save.cross_control;
        std::string save_name = save.save_name;
        std::string current_campaign = save.current_campaign;
        std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team;
        std::unique_ptr<guy> current_guy;
        std::int32_t current_type = session.current_type_;
        short current_team_num = session.current_team_num_;
        std::int32_t editguy = session.editguy_;
        guy* old_guy = picker.old_guy;
        const og::ui::PickerMenuItem* selected_menu_item =
            picker.selected_menu_item;
        og::ui::HireSession* hire_session = picker.hire_session;
        og::ui::TrainSession* train_session = picker.train_session;
        std::array<int, 4> teams_menu_team_page =
            picker.teams_menu_team_page;
        int view_scenario_page_step = picker.view_scenario_page_step;
        bool start_game_requested = g_start_game_requested;

        SharedStateGuard(SaveData& save_, og::runtime::SessionState& session_,
                         PickerState& picker_)
            : save(save_), session(session_), picker(picker_),
              current_guy(std::move(session.current_guy_))
        {
            for (int i = 0; i < MAX_TEAM_SIZE; ++i)
                team[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
        }

        ~SharedStateGuard()
        {
            for (int i = 0; i < MAX_TEAM_SIZE; ++i)
                save.team_list[static_cast<std::size_t>(i)] = std::move(team[static_cast<std::size_t>(i)]);
            save.team_size = team_size;
            save.numplayers = numplayers;
            save.my_team = my_team;
            save.scen_num = scen_num;
            save.cross_control = cross_control;
            save.save_name = save_name;
            save.current_campaign = current_campaign;
            session.current_guy_ = std::move(current_guy);
            session.current_type_ = current_type;
            session.current_team_num_ = current_team_num;
            session.editguy_ = editguy;
            picker.old_guy = old_guy;
            picker.selected_menu_item = selected_menu_item;
            picker.hire_session = hire_session;
            picker.train_session = train_session;
            picker.teams_menu_team_page = teams_menu_team_page;
            picker.view_scenario_page_step = view_scenario_page_step;
            g_start_game_requested = start_game_requested;
        }
    } shared_state{save, *og::runtime::current_session, pks()};

    struct RenderSurfaceRestore
    {
        SDL_Surface* surface = nullptr;
        std::vector<unsigned char> bytes;

        RenderSurfaceRestore()
        {
            surface = E_Screen ? E_Screen->render : nullptr;
            if (surface == nullptr || surface->pixels == nullptr)
                return;
            const std::size_t byte_count =
                static_cast<std::size_t>(surface->pitch) *
                static_cast<std::size_t>(surface->h);
            const auto* const first =
                static_cast<const unsigned char*>(surface->pixels);
            bytes.assign(first, first + byte_count);
        }

        ~RenderSurfaceRestore()
        {
            if (surface == nullptr || surface->pixels == nullptr ||
                E_Screen == nullptr || E_Screen->render != surface)
            {
                return;
            }
            const std::size_t byte_count =
                static_cast<std::size_t>(surface->pitch) *
                static_cast<std::size_t>(surface->h);
            if (byte_count == bytes.size())
                std::memcpy(surface->pixels, bytes.data(), bytes.size());
        }
    } render_surface_restore;

    check(!save_has_trainable_team_member(save));
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    client.editable = false;
    check(!save_has_trainable_team_member(save));
    client.editable = true;
    check(save_has_trainable_team_member(save));

    // MATCHUP's frame model is derived from lobby seats and roster colors,
    // which are intentionally independent domains. Exercise both the local
    // compatibility fallback and the real replicated-lobby shape.
    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "LOCAL HERO";
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;
    save.numplayers = 1;
    save.my_team = 0;
    save.save_name = "Local Company";
    save.current_campaign = get_mounted_campaign();
    client.networked = false;
    client.players.clear();
    client.local_indices.clear();
    pks().teams_menu_team_page = {0, 0, 0, 0};
    TeamsMenuFrameState local_matchup = compute_teams_menu_state();
    check(!local_matchup.wiring.networked &&
          local_matchup.seat_count[0] == 1 &&
          local_matchup.hero_count[0] == 1 &&
          local_matchup.summary == "CO-OP" &&
          local_matchup.campaign_mounted);

    auto make_player = [](std::uint8_t index, short team,
                          std::string company, bool ready) {
        og::sim::LobbyPlayer player;
        player.player_index = index;
        player.name = std::format("Player {}", static_cast<int>(index) + 1);
        player.company = std::move(company);
        player.team = team;
        player.ready = ready;
        return player;
    };
    client.networked = true;
    client.local_indices = {0};
    client.players = {
        make_player(3, 1, "Delta Guild", false),
        make_player(0, 0, "Local Company", true),
        make_player(2, 1, "Crimson Order", true),
        make_player(1, 0, "Blue Company", false),
    };
    for (int slot = 0; slot < 10; ++slot)
    {
        og::sim::LobbyCharacterSlot character_slot;
        character_slot.slot_index = static_cast<std::uint8_t>(slot);
        character_slot.character.name =
            std::format("LONG HERO NAME {}", slot);
        character_slot.character.teamnum = 0;
        character_slot.deployed = true;
        client.players[0].character_slots.push_back(character_slot);
    }
    pks().teams_menu_team_page[0] = -1;
    TeamsMenuFrameState network_matchup = compute_teams_menu_state();
    check(network_matchup.wiring.networked &&
          network_matchup.summary == "2 VS 2" &&
          network_matchup.seat_count[0] == 2 &&
          network_matchup.seat_count[1] == 2 &&
          network_matchup.hero_count[0] == 10 &&
          network_matchup.wiring.pager_visible[0] &&
          network_matchup.detail_pages[0].size() > 1 &&
          network_matchup.detail_page[0] >= 0);
    draw_teams_menu_row_bars();
    network_matchup.campaign_mounted = false;
    draw_teams_menu_content(
        network_matchup,
        og::runtime::current_session->myscreen_->text_normal);
    network_matchup.campaign_mounted = true;
    draw_teams_menu_content(
        network_matchup,
        og::runtime::current_session->myscreen_->text_normal);

    // The seat vocabulary lives in og::ui now (#218 seat block); MATCHUP
    // consumes the shared helpers, so the pins ride the shared names.
    check(og::ui::company_abbreviation("  --  ") == "NET");
    check(og::ui::company_abbreviation("blue-company") == "BLU");
    check(og::ui::seat_identity_label(client.players[1],
                                      client.local_indices) ==
          "P1 YOU [RDY]");
    check(og::ui::seat_identity_label(client.players[0],
                                      client.local_indices) ==
          "P4 DEL");
    check(og::ui::format_seat_summary({}) == "NO PLAYER SEATS");
    check(og::ui::format_seat_summary({make_player(0, 0, "A", false),
                                       make_player(1, 1, "B", false)}) ==
          "FREE-FOR-ALL");
    check(og::ui::format_seat_summary({make_player(0, 0, "A", false),
                                       make_player(1, 0, "B", false),
                                       make_player(2, 1, "C", false)}) ==
          "MIXED TEAMS");
    check(clip_to_width("abcdefgh", 4) == "abcd");
    check(clip_to_width("abc", 4) == "abc");

    TeamsMenuWiring no_wiring;
    picker_wire_teams_menu_nav(nullptr, 0, no_wiring);
    int ignored_highlight = 0;
    sync_teams_menu_visibility(nullptr, 0, ignored_highlight,
                               network_matchup);
    check(picker_teams_menu_engine_on_spec_row(-1, nullptr) == 0);
    client.host_visible = false;
    check(picker_teams_menu_engine_on_spec_row(
              kTeamsMenuCrossControlIndex, nullptr) == MENU_OK);

    // Engine VIEW LEVEL hooks: cover the no-screen shape and a real multi-page
    // model without entering a blocking menu or touching the framebuffer.
    ViewScenarioEngineState* const saved_view_state =
        g_view_scenario_engine_state;
    g_view_scenario_engine_state = nullptr;
    og::ui::MenuLabelContext label_context;
    check(picker_view_scenario_engine_pager_row_state(label_context) ==
          og::ui::RowState::Hidden);
    picker_view_scenario_engine_rewire(nullptr, 0, ignored_highlight);
    check(picker_view_scenario_engine_consume_click(77, nullptr) == 77);
    picker_view_scenario_engine_draw_content(nullptr);

    ViewScenarioEngineState view_state;
    view_state.lines.resize(35, "report line");
    view_state.pager = og::ui::PageModel::make(
        static_cast<int>(view_state.lines.size()), kViewScenarioRowsPerPage);
    g_view_scenario_engine_state = &view_state;
    button view_buttons[] = {
        button("prev", "PREV", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0,
               MenuNav{}),
        button("next", "NEXT", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0,
               MenuNav{}),
        button("back", "BACK", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0,
               MenuNav{}),
    };
    check(picker_view_scenario_engine_pager_row_state(label_context) ==
          og::ui::RowState::Visible);
    picker_view_scenario_engine_rewire(
        view_buttons, static_cast<int>(std::size(view_buttons)),
        ignored_highlight);
    check(view_buttons[kViewScenarioBackIndex].nav.right ==
          kViewScenarioPrevIndex);
    check(view_scenario_page_flip(1) == MENU_OK);
    check(picker_view_scenario_engine_consume_click(MENU_OK, nullptr) == 0 &&
          view_state.pager.page == 1 &&
          pks().view_scenario_page_step == 0);
    check(view_scenario_page_flip(-1) == MENU_OK);
    check(picker_view_scenario_engine_consume_click(MENU_OK, nullptr) == 0 &&
          view_state.pager.page == 0);
    picker_view_scenario_engine_draw_content(nullptr);
    g_view_scenario_engine_state = saved_view_state;

    ProgressEngineState progress_state;
    progress_state.levels = {
        LevelProgress{.id = 1, .title = "One", .num_enemies = 2,
                      .is_cleared = false, .is_current = true},
        LevelProgress{.id = 2, .title = "Two", .num_enemies = 0,
                      .is_cleared = true, .is_current = false},
    };
    for (int id = 3; id <= 12; ++id)
    {
        progress_state.levels.push_back(LevelProgress{
            .id = id,
            .title = std::format("Level {}", id),
            .num_enemies = id,
            .is_cleared = false,
            .is_current = false,
        });
    }
    progress_state.num_cleared = 1;
    check(picker_progress_menu_engine_frame_tick(nullptr, 0));
    check(picker_progress_menu_engine_frame_tick(&progress_state, 0));
    picker_progress_menu_engine_draw_content(nullptr);
    picker_progress_menu_engine_draw_background(nullptr);
    picker_progress_menu_engine_draw_content(&progress_state);
    // #207 row-action hit-test: uncleared rows keep the legacy GO rect;
    // cleared rows answer through TWO rects — VISIT (x 230..266, the plain
    // write) and REPLAY (x 270..310, the arm). Row 0 = id 1
    // uncleared/current at y 36..49, row 1 = id 2 cleared at y 49..62.
    // The rects belong to the host: the teams-menu arm above left this
    // client a joiner, and a joiner's report draws no buttons at all.
    client.host_visible = true;
    check(progress_rows_actionable());
    check(progress_row_action_hit(progress_state, 300, 40).id == 1 &&
          progress_row_action_hit(progress_state, 300, 40).action ==
              ProgressRowAction::Go);
    check(progress_row_action_hit(progress_state, 300, 55).id == 2 &&
          progress_row_action_hit(progress_state, 300, 55).action ==
              ProgressRowAction::Replay);
    check(progress_row_action_hit(progress_state, 271, 55).id == 2 &&
          progress_row_action_hit(progress_state, 271, 55).action ==
              ProgressRowAction::Replay);
    check(progress_row_action_hit(progress_state, 240, 55).id == 2 &&
          progress_row_action_hit(progress_state, 240, 55).action ==
              ProgressRowAction::Visit);
    check(progress_row_action_hit(progress_state, 268, 55).id == -1);
    check(progress_row_action_hit(progress_state, 270, 40).id == -1);
    check(progress_row_action_hit(progress_state, 240, 40).id == -1);
    check(progress_row_action_hit(progress_state, 5, 40).id == -1);
    // A joiner's report draws "(HOST)" where the buttons were, so the same
    // coordinates answer nothing at all: no click, no refusal popup, no
    // event loop parked in front of a joiner waiting on the host's GO.
    client.host_visible = false;
    check(!progress_rows_actionable());
    check(progress_row_action_hit(progress_state, 300, 40).id == -1);
    check(progress_row_action_hit(progress_state, 271, 55).id == -1);
    check(progress_row_action_hit(progress_state, 240, 55).id == -1);
    picker_progress_menu_engine_draw_content(&progress_state);
    check(picker_progress_menu_engine_frame_tick(&progress_state, 0));

    // #207 visual pin, read back from the drawn frame (a green geometry
    // table can pin a crooked layout; ink-in-rect relations cannot): the
    // cleared row wears BOTH buttons with a clean gutter between them, the
    // uncleared row wears only GO, and the Foes number moved to the new
    // column (the old x=258 spot is bare canvas on the row band).
    {
        client.host_visible = true;
        auto ink_in = [](int x0, int y0, int x1, int y1) {
            int count = 0;
            screen* const scr = og::runtime::current_session->myscreen_;
            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    Uint8 r = 0, g = 0, b = 0;
                    scr->get_pixel(x, y, &r, &g, &b);
                    if (r != 0 || g != 0 || b != 0)
                        ++count;
                }
            }
            return count;
        };
        picker_progress_menu_engine_draw_background(&progress_state);
        picker_progress_menu_engine_draw_content(&progress_state);
        // Row 1 (id 2, cleared, band y 50..59): VISIT + REPLAY, gutter bare.
        check(ink_in(230, 50, 266, 59) > 0);   // VISIT face
        check(ink_in(270, 50, 310, 59) > 0);   // REPLAY face
        check(ink_in(267, 50, 269, 59) == 0);  // the 3px gutter between them
        // Row 0 (id 1, uncleared, band y 37..46): GO only.
        check(ink_in(292, 37, 310, 46) > 0);   // GO face
        check(ink_in(230, 37, 268, 46) == 0);  // no VISIT/REPLAY on GO rows
        // The Foes number: new column inked, the old x=258 spot bare.
        check(ink_in(kProgressFoesNumberX, 37, kProgressFoesNumberX + 18, 46) > 0);
        check(ink_in(250, 37, 280, 46) == 0);  // the pre-#207 number column
        // Read-back artifact for human review (gitignored build/media).
        std::filesystem::create_directories("build/media");
        FILE* const shot = fopen("build/media/progress_replay_rows.ppm", "wb");
        if (shot != nullptr)
        {
            fprintf(shot, "P6\n320 200\n255\n");
            screen* const scr = og::runtime::current_session->myscreen_;
            for (int y = 0; y < 200; ++y)
            {
                for (int x = 0; x < 320; ++x)
                {
                    Uint8 r = 0, g = 0, b = 0;
                    scr->get_pixel(x, y, &r, &g, &b);
                    fputc(r, shot);
                    fputc(g, shot);
                    fputc(b, shot);
                }
            }
            fclose(shot);
        }
        client.host_visible = false;
    }
    // ... and the arm leaves the client as it found it.

    button hire_buttons[] = {
        button("prev", "PREV", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0,
               MenuNav{}),
        button("next", "NEXT", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0,
               MenuNav{}),
        button("team", "TEAM", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0,
               MenuNav{}),
        button("hire", "HIRE", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0,
               MenuNav{.right = 2}),
    };
    picker_hire_menu_engine_prepare_buttons(hire_buttons, 1, nullptr);
    picker_hire_menu_engine_prepare_buttons(
        hire_buttons, static_cast<int>(std::size(hire_buttons)), nullptr);
    check(hire_buttons[0].x != 0 && hire_buttons[1].x != 0);
    hire_buttons[2].hidden = true;
    picker_hire_menu_engine_rewire(
        hire_buttons, static_cast<int>(std::size(hire_buttons)),
        ignored_highlight);
    check(hire_buttons[3].nav.right == -1);
    hire_buttons[2].hidden = false;
    picker_hire_menu_engine_rewire(
        hire_buttons, static_cast<int>(std::size(hire_buttons)),
        ignored_highlight);
    check(hire_buttons[3].nav.right == 2);
    HireEngineState hire_state;
    check(picker_hire_menu_engine_frame_tick(nullptr, 0));
    check(picker_hire_menu_engine_frame_tick(&hire_state, 1));
    picker_hire_menu_engine_draw_content(nullptr);
    picker_train_menu_engine_draw_content(nullptr);

    // Leave the remainder of this helper in its original local-session shape.
    client.networked = false;
    client.players.clear();
    client.local_indices.clear();
    client.host_visible = true;
    save.numplayers = 1;
    save.my_team = 0;
    save.current_campaign = get_mounted_campaign();
    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;

    button buttons[] = {
        button("b0", "0", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b1", "1", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b2", "2", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b3", "3", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b4", "4", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b5", "5", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b6", "6", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b7", "7", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b8", "8", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b9", "9", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b10", "10", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
    };
    int highlighted = 5;
    sync_button_hidden_state(nullptr, 0);
    sync_button_hidden_state(buttons, -1);
    ensure_highlighted_button_visible(nullptr, 0, highlighted);
    buttons[0].hidden = true;
    buttons[1].hidden = false;
    highlighted = 0;
    ensure_highlighted_button_visible(buttons, 2, highlighted);
    check(highlighted == 1);
    buttons[1].hidden = true;
    ensure_highlighted_button_visible(buttons, 2, highlighted);
    check(highlighted == 0);

    highlighted = 5;
    // (The base camp's full-graph rewire — GO gating included — lives on the
    // spec in menu_screen_specs.cpp and is exercised by the engine tests.)
    client.host_visible = false;
    sync_scenario_menu_host_control_visibility(buttons, 11, highlighted);
    check(buttons[kScenarioMenuSetCampaignIndex].hidden &&
        buttons[kScenarioMenuSetLevelIndex].hidden &&
        buttons[kScenarioMenuViewScenarioIndex].nav.up == -1 &&
        buttons[kScenarioMenuSpareIndex].nav.up == -1 &&
        buttons[kScenarioMenuSpareIndex].hidden &&
        buttons[kScenarioMenuProgressIndex].nav.up == -1);
    client.host_visible = true;
    sync_scenario_menu_host_control_visibility(buttons, 11, highlighted);
    check(!buttons[kScenarioMenuSetCampaignIndex].hidden &&
        !buttons[kScenarioMenuSetLevelIndex].hidden &&
        buttons[kScenarioMenuViewScenarioIndex].nav.up ==
            kScenarioMenuSetLevelIndex);

    // DIFFICULTY subscreen gating: every settings row hides for a non-host;
    // BACK's vertical cycle is rewired and the highlight is pulled onto BACK.
    for (int i = 0; i < 11; ++i)
        buttons[i].hidden = false;
    buttons[kDifficultyMenuBackIndex].nav.up = kDifficultyMenuInfiniteGoldIndex;
    buttons[kDifficultyMenuBackIndex].nav.down = kDifficultyMenuDifficultyIndex;
    int diff_highlight = kDifficultyMenuGeneratorRateIndex;
    client.host_visible = false;
    sync_difficulty_menu_visibility(buttons, kDifficultyMenuButtonCount,
                                    diff_highlight);
    check(buttons[kDifficultyMenuDifficultyIndex].hidden &&
        buttons[kDifficultyMenuRespawnModeIndex].hidden &&
        buttons[kDifficultyMenuRespawnDelayIndex].hidden &&
        buttons[kDifficultyMenuPermadeathIndex].hidden &&
        buttons[kDifficultyMenuGeneratorRateIndex].hidden &&
        buttons[kDifficultyMenuInfiniteGoldIndex].hidden &&
        buttons[kDifficultyMenuBackIndex].nav.up == -1 &&
        buttons[kDifficultyMenuBackIndex].nav.down == -1 &&
        diff_highlight == kDifficultyMenuBackIndex);
    client.host_visible = true;
    sync_difficulty_menu_visibility(buttons, kDifficultyMenuButtonCount,
                                    diff_highlight);
    check(!buttons[kDifficultyMenuDifficultyIndex].hidden &&
        !buttons[kDifficultyMenuGeneratorRateIndex].hidden &&
        !buttons[kDifficultyMenuInfiniteGoldIndex].hidden &&
        buttons[kDifficultyMenuBackIndex].nav.up ==
            kDifficultyMenuInfiniteGoldIndex &&
        buttons[kDifficultyMenuBackIndex].nav.down ==
            kDifficultyMenuDifficultyIndex);

    g_start_game_requested = false;
    Sint32 retvalue = 0;
    check(!team_build_remote_start_requested(retvalue));
    g_start_game_requested = true;
    client.has_start_config = false;
    check(!team_build_remote_start_requested(retvalue));
    client.has_start_config = true;
    check(team_build_remote_start_requested(retvalue) && (retvalue & MENU_EXIT));
    check(team_build_start_selected());
    pks().selected_menu_item = nullptr;
    check(!team_build_start_selected());
    g_start_game_requested = false;

    // Main-menu-scope remote start (mainmenu + the DIFFICULTY subscreen):
    // exits with the Main CONTINUE item selected so the state machine
    // re-enters team build, which consumes the start.
    retvalue = 0;
    check(!picker_main_scope_remote_start_requested(retvalue));
    g_start_game_requested = true;
    client.has_start_config = false;
    check(!picker_main_scope_remote_start_requested(retvalue));
    client.has_start_config = true;
    check(picker_main_scope_remote_start_requested(retvalue) &&
        (retvalue & MENU_EXIT));
    check(pks().selected_menu_item != nullptr &&
        pks().selected_menu_item->command ==
            og::ui::PickerMenuCommand::ContinueGame);
    pks().selected_menu_item = nullptr;
    g_start_game_requested = false;
    picker_prepare_async_team_build_start_request();
    check(client.requested);
#ifdef __EMSCRIPTEN__
    check(g_start_game_requested);
#else
    check(!g_start_game_requested);
#endif

    check(!get_class_description(FAMILY_SOLDIER).empty());
    check(get_class_description(250).empty());
    for (int family : og::ui::kAllowableGuys)
    {
        const FamilyDescriptor* const descriptor =
            get_family_descriptor(family);
        check(descriptor != nullptr);
        if (descriptor == nullptr)
            continue;
        for (int stat = 0; stat < 5; ++stat)
        {
            const int cost = descriptor->stat_costs[stat];
            const int stars = cost == 0 ? 0 : ((55 / cost) * 5) / 11;
            const std::string expected =
                stars >= 1 && stars <= 5
                    ? std::string(static_cast<std::size_t>(stars), '*')
                    : std::string();
            check(get_training_cost_rating(
                      static_cast<unsigned char>(family), stat) == expected);
        }
        check(std::strlen(get_training_cost_rating(
                  static_cast<unsigned char>(family), -1)) == 0);
        check(std::strlen(get_training_cost_rating(
                  static_cast<unsigned char>(family), 6)) == 0);
    }
    check(std::strlen(get_training_cost_rating(250, 0)) == 0);

    save.team_list[MAX_TEAM_SIZE - 1] = std::make_unique<guy>(FAMILY_ELF);
    save.team_size = 2;
    og::ui::TrainSession train_session(save);
    pks().train_session = nullptr;
    check(increase_stat(BUT_STR, 1) == MENU_OK &&
        decrease_stat(BUT_STR, 1) == MENU_OK &&
        cycle_team_guy(1) == -1 &&
        edit_guy(0) == -1);

    pks().train_session = &train_session;
    const auto stat_value = [](const guy& member, Sint32 stat) {
        switch (stat)
        {
        case BUT_DEX: return member.dexterity;
        case BUT_CON: return member.constitution;
        case BUT_INT: return member.intelligence;
        case BUT_ARMOR: return member.armor;
        case BUT_LEVEL: return member.level;
        case BUT_STR:
        default: return member.strength;
        }
    };
    for (Sint32 stat : {BUT_STR, BUT_DEX, BUT_CON, BUT_INT, BUT_ARMOR, BUT_LEVEL, Sint32{-999}})
    {
        const short before = stat_value(train_session.working_copy(), stat);
        const short saved_before = stat_value(*save.team_list[0], stat);
        check(increase_stat(stat, 1) == MENU_OK);
        check(stat_value(train_session.working_copy(), stat) == before + 1);
        check(stat_value(*save.team_list[0], stat) == saved_before);
        check(decrease_stat(stat, 1) == MENU_OK);
        check(stat_value(train_session.working_copy(), stat) == before);
        check(stat_value(*save.team_list[0], stat) == saved_before);
    }
    check(train_session.current_slot() == 0 &&
          train_session.working_copy().family == FAMILY_SOLDIER);
    check(cycle_team_guy(-1) == MENU_OK &&
          train_session.current_slot() == MAX_TEAM_SIZE - 1 &&
          train_session.working_copy().family == FAMILY_ELF);
    check(cycle_team_guy(1) == MENU_OK &&
          train_session.current_slot() == 0 &&
          train_session.working_copy().family == FAMILY_SOLDIER);
    sync_current_guy_from_train();
    check(og::runtime::current_session->current_guy_ != nullptr &&
          og::runtime::current_session->current_guy_->family == FAMILY_SOLDIER &&
          og::runtime::current_session->editguy_ == 0);
    pks().train_session = nullptr;

    og::ui::HireSession hire_session(save, 0);
    pks().hire_session = nullptr;
    check(add_guy(0) == -1);
    og::runtime::current_session->current_type_ = 0;
    check(cycle_guy(0) == MENU_OK &&
          og::runtime::current_session->current_type_ == 0 &&
          og::runtime::current_session->current_guy_ != nullptr &&
          og::runtime::current_session->current_guy_->family ==
              og::ui::kAllowableGuys[0]);
    pks().hire_session = &hire_session;
    check(hire_session.family_index() == 0 &&
          hire_session.current_recruit() != nullptr &&
          hire_session.current_recruit()->family == og::ui::kAllowableGuys[0]);
    check(cycle_guy(-1) == MENU_OK &&
          hire_session.family_index() ==
              static_cast<int>(og::ui::kAllowableGuys.size()) - 1 &&
          hire_session.current_recruit() != nullptr &&
          hire_session.current_recruit()->family ==
              og::ui::kAllowableGuys.back());
    check(cycle_guy(1) == MENU_OK &&
          hire_session.family_index() == 0 &&
          hire_session.current_recruit() != nullptr &&
          hire_session.current_recruit()->family ==
              og::ui::kAllowableGuys.front());
    check(cycle_guy(1) == MENU_OK &&
          hire_session.family_index() == 1 &&
          hire_session.current_recruit() != nullptr &&
          hire_session.current_recruit()->family ==
              og::ui::kAllowableGuys[1]);
    sync_current_guy_from_hire();
    check(og::runtime::current_session->current_guy_ != nullptr &&
          og::runtime::current_session->current_guy_->family ==
              og::ui::kAllowableGuys[1]);
    pks().hire_session = nullptr;

    og::runtime::current_session->current_guy_.reset();
    check(name_guy(0) == MENU_REDRAW);
    client.editable = false;
    og::runtime::current_session->editguy_ = 0;
    check(name_guy(1) == MENU_REDRAW);
    client.editable = true;

    check(how_many(FAMILY_SOLDIER) == 1);
    check(how_many(FAMILY_ELF) == 1);
    check(how_many(FAMILY_MAGE) == 0);
    // Restore the dense-roster invariant after the deliberately sparse
    // slot-23 wrap probe above; delete_all historically walks team_size slots.
    save.team_list[MAX_TEAM_SIZE - 1].reset();
    save.team_size = 1;
    check(delete_all() == 1);
    check(save.team_size == 0 &&
          std::all_of(save.team_list.begin(), save.team_list.end(),
              [](const std::unique_ptr<guy>& member) {
                  return member == nullptr;
              }));
    check(go_menu(0) == MENU_REDRAW);
    save.team_list[0] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;
    client.requested = false;
    g_start_game_requested = false;
    check(go_menu(0) == MENU_REDRAW && client.requested);

    // §4.3 rule 4 / [NET-R9] go_menu guard: a hired-but-benched LOCAL roster
    // blocks the launch (deploy popup, no start request)...
    save.team_list[0]->deployed = false;
    client.requested = false;
    g_start_game_requested = false;
    check(go_menu(0) == MENU_REDRAW && !client.requested);
    // ...and the §2.6 presentation resolves to state 2 (grey face — the
    // solo byte-identity face) with the DEPLOY caption.
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        check(p.state == og::ui::ReadyGoState::LocalGoNoDeploy &&
              p.face_color == og::ui::kReadyGoFaceGrey &&
              p.caption == "DEPLOY AT LEAST ONE");
    }
    save.team_list[0]->deployed = true;
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        check(p.state == og::ui::ReadyGoState::LocalGo &&
              p.face_color == og::ui::kReadyGoFaceGrey && p.label == "GO");
    }
    // ...and the NETWORKED guard is GLOBAL any-deployed, not per-machine:
    // an all-benched lobby blocks, one deployed slot ANYWHERE (even on a
    // remote machine) launches.
    client.networked = true;
    {
        og::sim::LobbyPlayer remote_machine;
        remote_machine.name = "Remote";
        // [NET-R2]: the earlier difficulty arm left host_visible=true, so
        // the §2.6 rule-3 pre-check runs here — the remote machine must be
        // ready for these arms to isolate the rule-4 deploy gate.
        remote_machine.ready = true;
        remote_machine.character_slots.push_back(og::sim::LobbyCharacterSlot{
            .slot_index = 0,
            .character = {},
            .deployed = false,
        });
        client.players = {remote_machine};
    }
    client.requested = false;
    check(go_menu(0) == MENU_REDRAW && !client.requested);
    client.players[0].character_slots[0].deployed = true;
    client.requested = false;
    g_start_game_requested = false;
    check(go_menu(0) == MENU_REDRAW && client.requested);

    // §2.6 state 3: a networked HOST with an unready machine popups the
    // blockers (rule 3 outranks rule 4) and sends NOTHING; readying that
    // machine flips the presentation yellow -> green and unblocks the GO.
    client.host_visible = true;
    client.players[0].name = "Remote";
    client.players[0].company = "REMOTE BAND";
    client.players[0].ready = false;
    client.requested = false;
    g_start_game_requested = false;
    check(go_menu(0) == MENU_REDRAW && !client.requested);
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        check(p.state == og::ui::ReadyGoState::HostGated &&
              p.face_color == og::ui::kReadyGoFaceGated &&
              p.caption == "WAITING FOR OTHERS");
    }
    client.players[0].ready = true;
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        check(p.state == og::ui::ReadyGoState::HostGo &&
              p.face_color == og::ui::kReadyGoFaceGo && p.label == "GO");
    }
    client.requested = false;
    check(go_menu(0) == MENU_REDRAW && client.requested);
    // Joiner shape (states 5-6): READY/UNREADY labels off the lobby flag.
    client.host_visible = false;
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        check(p.state == og::ui::ReadyGoState::ClientUnready &&
              p.face_color == og::ui::kReadyGoFaceUnready &&
              p.label == "READY");
    }
    client.networked = false;
    client.players.clear();
    client.requested = false;
    g_start_game_requested = false;

    guy source(FAMILY_ELF);
    guy destination(FAMILY_SOLDIER);
    source.name = "Copied";
    source.level = 3;
    source.exp = calculate_exp(source.level);
    source.kills = 4;
    statscopy(&destination, &source);
    check(destination.family == source.family &&
        destination.name == source.name &&
        destination.exp == source.exp);

    // --- SELL row handler + its inline backup-checkpoint lambda -----------
    // Drives picker_train_menu_engine_on_spec_row through every observable
    // shape: the guard legs, the declined prompt, the CheckpointFailed leg
    // (which EXECUTES the []{ backup_company_now(active_company_slot()) }
    // lambda against a slot with no company file — sell_current invokes the
    // checkpoint BEFORE deciding failure), and both Sold shapes against a
    // real seeded company file.
    {
        picker_testing_yes_or_no_queue_clear();
        const std::string saved_slot = og::data::active_company_slot();
        std::array<std::uint32_t, 4> saved_cash;
        for (int i = 0; i < 4; ++i)
            saved_cash[static_cast<std::size_t>(i)] = save.m_totalcash[i];
        const std::int64_t saved_stamp = save.last_played_unix_s;

        // Guard: empty session (team_size < 1 leaves no working copy).
        for (auto& member : save.team_list)
            member.reset();
        save.team_size = 0;
        og::ui::TrainSession empty_sell_session(save);
        pks().train_session = &empty_sell_session;
        check(picker_train_menu_engine_on_spec_row(
                  kTrainMenuSellIndex, nullptr) == MENU_OK);

        save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[0]->name = "SELLA";
        save.team_list[0]->teamnum = 0;
        save.team_list[1] = std::make_unique<guy>(FAMILY_ELF);
        save.team_list[1]->name = "SELLB";
        save.team_list[1]->teamnum = 0;
        save.team_size = 2;
        save.m_totalcash[0] = 500;
        og::ui::TrainSession sell_session(save);

        // Guard: no session at all.
        pks().train_session = nullptr;
        check(picker_train_menu_engine_on_spec_row(
                  kTrainMenuSellIndex, nullptr) == MENU_OK);
        pks().train_session = &sell_session;
        // Guard: wrong row.
        check(picker_train_menu_engine_on_spec_row(
                  kTrainMenuSellIndex - 1, nullptr) == MENU_OK);
        // Guard: non-editable slot (empty() applies the same predicate via
        // original_member(), so the first guard absorbs this shape).
        client.editable = false;
        check(picker_train_menu_engine_on_spec_row(
                  kTrainMenuSellIndex, nullptr) == MENU_OK);
        client.editable = true;

        // Declined prompt: an empty override queue falls back to the
        // handler's default_value=false — nothing is touched.
        trace_clear();
        check(picker_train_menu_engine_on_spec_row(
                  kTrainMenuSellIndex, nullptr) == MENU_REDRAW);
        check(trace_contains("confirm", "SELL SELLA FOR"));
        check(save.team_size == 2 && save.m_totalcash[0] == 500);

        // CheckpointFailed: the inline backup lambda runs against an active
        // slot whose company file does not exist, so backup_company_now
        // returns false and the sale is refused before any mutation.
        check(!user_file_exists("save/sellmiss.gtl"));
        check(og::data::set_active_company_slot("sellmiss"));
        picker_testing_yes_or_no_queue_push(true);
        trace_clear();
        check(picker_train_menu_engine_on_spec_row(
                  kTrainMenuSellIndex, nullptr) == MENU_REDRAW);
        check(trace_contains("popup", "BACKUP FAILED"));
        check(save.team_size == 2 && save.m_totalcash[0] == 500);

        // Sold with a survivor: seed a real company file so the checkpoint
        // succeeds; the wallet is credited and the session moves to the
        // surviving member.
        check(og::data::set_active_company_slot("sellcov"));
        {
            SaveData seed;
            seed.save_name = "SELL COVERAGE";
            check(seed.save("sellcov"));
        }
        const std::uint32_t first_payout = sell_session.current_sell_value();
        picker_testing_yes_or_no_queue_push(true);
        check(picker_train_menu_engine_on_spec_row(
                  kTrainMenuSellIndex, nullptr) == MENU_REDRAW);
        check(save.team_size == 1 &&
              save.m_totalcash[0] == 500 + first_payout);
        check(!sell_session.empty() &&
              sell_session.working_copy().name == "SELLB");
        check(og::runtime::current_session->current_guy_ != nullptr &&
              og::runtime::current_session->current_guy_->name == "SELLB" &&
              og::runtime::current_session->editguy_ == 0);

        // Sold to empty: the last member leaves and TRAIN exits.
        const std::uint32_t second_payout = sell_session.current_sell_value();
        picker_testing_yes_or_no_queue_push(true);
        check(picker_train_menu_engine_on_spec_row(
                  kTrainMenuSellIndex, nullptr) == MENU_EXIT);
        check(save.team_size == 0 && sell_session.empty());
        check(save.m_totalcash[0] == 500 + first_payout + second_payout);
        check(og::runtime::current_session->current_guy_ == nullptr &&
              pks().old_guy == nullptr &&
              og::runtime::current_session->editguy_ == -1);

        // Restore everything the SharedStateGuard does not cover: the sell
        // legs' company files/backups, the active slot, the wallet, and the
        // autosave timestamp.
        check(picker_testing_yes_or_no_queue_remaining() == 0);
        picker_testing_yes_or_no_queue_clear();
        pks().train_session = nullptr;  // before the local sessions die
        for (const og::data::CompanyBackupInfo& backup :
             og::data::list_company_backups("sellcov"))
        {
            (void)remove_user_file("save/backups/" + backup.filename);
        }
        (void)remove_user_file("save/sellcov.gtl");
        check(og::data::set_active_company_slot(saved_slot));
        for (int i = 0; i < 4; ++i)
            save.m_totalcash[i] = saved_cash[static_cast<std::size_t>(i)];
        save.last_played_unix_s = saved_stamp;
    }

    g_start_game_requested = false;
    pks().selected_menu_item = nullptr;
    pks().train_session = nullptr;
    pks().hire_session = nullptr;
    return failed_check == 0 ? 0 : -failed_check;
}
