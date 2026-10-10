#include <openglad/platform/sai2x.h>
#include <openglad/core/tower_constants.h>
#include <openglad/resources/io_common.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <source_location>

void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
int picker_testing_yes_or_no_queue_remaining();

int picker_team_build_testing_exercise_internal_paths(int* checks_run)
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
        // PROGRESS held-click spin (picker_progress_menu_engine_frame_tick):
        // the hook latches the press and spins on picker_lobby_poll() +
        // get_input_events until the button comes up. A release queued
        // BEFORE the call would be drained by the hook's own query_mouse()
        // together with the press (clicked=false), so the release is
        // delivered from here: the first poll inside the spin queues it and
        // the spin's next get_input_events consumes it.
        bool release_on_poll = false;
        int release_x = 0;
        int release_y = 0;
        int releases_sent = 0;
        void poll_and_apply() override
        {
            if (!release_on_poll)
                return;
            release_on_poll = false;
            ++releases_sent;
            og::input_native::push_mouse_button_event(false, 1, release_x,
                                                      release_y);
        }
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
        // A staged setup/keyframe pair, as the lobby wire hands it to the
        // VIEW LEVEL pane (off unless a check arms it).
        bool has_staged_pair = false;
        std::uint32_t staged_generation = 0;
        std::vector<std::uint8_t> staged_setup;
        std::vector<std::uint8_t> staged_keyframe;
        std::uint32_t stage_generation() const override
        {
            return has_staged_pair ? staged_generation : 0;
        }
        bool staged_keyframe_bytes(
            std::uint32_t& generation,
            const std::vector<std::uint8_t>*& setup_bytes,
            const std::vector<std::uint8_t>*& keyframe_bytes) const override
        {
            if (!has_staged_pair)
                return false;
            generation = staged_generation;
            setup_bytes = &staged_setup;
            keyframe_bytes = &staged_keyframe;
            return true;
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
    // Every failing check names its line (the returned index is the first):
    // the index alone sends a reader counting check() calls through loops.
    const auto check = [&check_index, &failed_check](
                           bool condition,
                           std::source_location where =
                               std::source_location::current()) {
        ++check_index;
        if (condition)
            return;
        if (failed_check == 0)
            failed_check = check_index;
        std::fprintf(stderr, "  [internal paths] check %d failed at line %u\n",
                     check_index, static_cast<unsigned>(where.line()));
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

    // Engine VIEW LEVEL hooks: cover the no-screen shape and a real multi-page
    // model without entering a blocking menu or touching the framebuffer.
    ViewScenarioEngineState* const saved_view_state =
        g_view_scenario_engine_state;
    g_view_scenario_engine_state = nullptr;
    og::ui::MenuLabelContext label_context;
    int ignored_highlight = 0;
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

    // PROGRESS click handling through the real input path: a pressed button
    // at (x, y) enters the held-click spin, the lobby poll inside it releases
    // the press, and the hook then scrolls or dispatches the row. Rows are
    // the host's (host_visible); ids 1..12, ten visible rows.
    {
        client.host_visible = true;
        const std::string saved_campaign = save.current_campaign;
        const short saved_scen = save.scen_num;
        save.current_campaign = get_mounted_campaign();
        // The report is a UI-canvas screen: its coordinates reach the input
        // layer as WINDOW points through the fixed UI-canvas map.
        ScopedUiCanvas ui_canvas(*og::runtime::current_session->myscreen_);
        const auto held_click = [&](int x, int y) {
            get_input_events(POLL);  // nothing stale in the queue
            const auto [wx, wy] = ui_canvas_to_window(
                static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            client.release_on_poll = true;
            client.release_x = static_cast<int>(wx);
            client.release_y = static_cast<int>(wy);
            og::input_native::push_mouse_button_event(
                true, 1, static_cast<int>(wx), static_cast<int>(wy));
            return picker_progress_menu_engine_frame_tick(&progress_state, 0);
        };
        // NEXT scrolls one row, PREV scrolls back; both stay on the report.
        progress_state.scroll_offset = 0;
        client.releases_sent = 0;
        check(held_click(100, 180) && progress_state.scroll_offset == 1 &&
              client.releases_sent == 1);
        check(held_click(50, 180) && progress_state.scroll_offset == 0 &&
              client.releases_sent == 2);
        // A PREV press at the top is inert (the button is disabled there).
        check(held_click(50, 180) && progress_state.scroll_offset == 0);

        // GO on an uncleared, reachable row (id 1, the campaign's entry
        // level) writes the cursor and leaves the report (false).
        trace_clear();
        save.scen_num = 3;
        check(!held_click(300, 40) &&
              trace_contains("picker", "progress_row_go level=1") &&
              save.scen_num == 1);

        // GO on a row whose road is closed refuses and stays (true): no
        // cursor write, no dispatch trace, the refusal popup instead.
        const int saved_id = progress_state.levels[2].id;
        progress_state.levels[2].id = 99;  // row 2, band y 62..75
        trace_clear();
        save.scen_num = 3;
        check(held_click(300, 66) &&
              !trace_contains("picker", "progress_row_go") &&
              trace_contains("picker", "progress_row_denied_gate 99") &&
              save.scen_num == 3);
        progress_state.levels[2].id = saved_id;

        // A host GO arriving while the press is held ends the spin and the
        // hook stays up (true) for the loop-top check to fire the start --
        // the row under the press is NOT dispatched.
        trace_clear();
        g_start_game_requested = true;
        client.has_start_config = true;
        check(held_click(300, 40) && !trace_contains("picker", "progress_row_go") &&
              save.scen_num == 3);
        g_start_game_requested = false;
        pks().selected_menu_item = nullptr;
        client.release_on_poll = false;
        get_input_events(POLL);  // the release the early return left queued
        reset_mouse_click_tracking();

        save.scen_num = saved_scen;
        save.current_campaign = saved_campaign;
        progress_state.scroll_offset = 0;
        client.host_visible = false;
    }

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
        buttons[kScenarioMenuLineupIndex].nav.up == -1 &&
        !buttons[kScenarioMenuLineupIndex].hidden &&
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

    check(!get_class_description(FAMILY_SOLDIER, 1).empty());
    check(get_class_description(250, 0).empty());
    // The HIRE/TRAIN star faces of every hireable family, spelled out from
    // the core pack's train prices (STR, DEX, CON, INT, ARMOR) rather than
    // recomputed from the product's own ((55 / cost) * 5) / 11 -- a test that
    // shares the formula shares its bugs (the cost 1-3 blank-stars bug hid
    // behind exactly that twin).
    struct FamilyStars
    {
        int family;
        std::array<const char*, 5> stars;
    };
    static constexpr std::array<FamilyStars, 14> kHireableStars = {{
        {FAMILY_SOLDIER, {"****", "**", "****", "", ""}},         // 6 10 6 25 50
        {FAMILY_BARBARIAN, {"*****", "", "*****", "", ""}},       // 5 35 5 35 50
        {FAMILY_ELF, {"", "****", "*", "**", ""}},                // 25 6 12 8 50
        {FAMILY_ARCHER, {"*", "****", "**", "**", ""}},           // 15 6 9 10 50
        {FAMILY_MAGE, {"", "*", "*", "****", ""}},                // 20 15 16 6 50
        {FAMILY_CLERIC, {"*", "*", "**", "****", ""}},            // 15 15 9 6 50
        {FAMILY_THIEF, {"*", "****", "**", "**", ""}},            // 15 6 9 10 50
        {FAMILY_DRUID, {"*", "*", "***", "****", ""}},            // 15 15 7 6 50
        {FAMILY_ORC, {"****", "*", "*****", "", ""}},             // 6 15 5 40 50
        {FAMILY_SKELETON, {"*", "****", "*", "", ""}},            // 15 6 16 25 50
        {FAMILY_FIREELEMENTAL, {"***", "**", "*", "*", ""}},      // 7 10 14 12 50
        {FAMILY_SMALL_SLIME, {"", "", "**", "*", ""}},            // 20 20 8 14 50
        {FAMILY_FAERIE, {"", "****", "*", "**", ""}},             // 25 6 12 8 50
        {FAMILY_GHOST, {"*", "*", "*", "*", ""}},                 // 16 16 16 16 45
    }};
    static_assert(kHireableStars.size() == og::ui::kAllowableGuys.size());
    for (const FamilyStars& row : kHireableStars)
    {
        const int family = row.family;
        check(get_family_descriptor(family) != nullptr);
        for (int stat = 0; stat < 5; ++stat)
        {
            check(std::string(get_training_cost_rating(
                      static_cast<unsigned char>(family), stat)) ==
                  row.stars[static_cast<std::size_t>(stat)]);
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

    // HIRE content pixels, read back from the drawn frame.
    {
        screen* const scr = og::runtime::current_session->myscreen_;
        const auto region = [scr](int x0, int y0, int x1, int y1) {
            std::vector<std::uint32_t> px;
            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    Uint8 r = 0, g = 0, b = 0;
                    scr->get_pixel(x, y, &r, &g, &b);
                    px.push_back((std::uint32_t{r} << 16) |
                                 (std::uint32_t{g} << 8) | b);
                }
            }
            return px;
        };
        HireEngineState px_state;
        px_state.last_family = static_cast<unsigned char>(
            og::runtime::current_session->current_guy_->family);
        px_state.family_name = get_family_string(px_state.last_family);
        // (1) The description box holds at most ten 8px lines: a twelve-
        // line description draws EXACTLY the frame a ten-line one does --
        // the runaway lines never reach the box's bottom bevel or below.
        px_state.desc.assign(10, "DESCRIPTION LINE");
        scr->clearbuffer();
        picker_hire_menu_engine_draw_content(&px_state);
        const std::vector<std::uint32_t> ten_lines = region(0, 0, 319, 199);
        px_state.desc.assign(12, "DESCRIPTION LINE");
        scr->clearbuffer();
        picker_hire_menu_engine_draw_content(&px_state);
        check(region(0, 0, 319, 199) == ten_lines);

        // (2) The cost number is inked in STAT_CHANGED when the wallet
        // cannot cover it and in STAT_COLOR when it can: the two renders of
        // the cost digits differ, and only in colour.
        const std::uint32_t saved_cash = save.m_totalcash[0];
        const short saved_team = og::runtime::current_session->current_team_num_;
        og::runtime::current_session->current_team_num_ = 0;
        const Uint32 cost = hire_session.current_cost();
        save.m_totalcash[0] = cost;  // exactly affordable
        scr->clearbuffer();
        picker_hire_menu_engine_draw_content(&px_state);
        const std::vector<std::uint32_t> affordable = region(240, 148, 300, 157);
        save.m_totalcash[0] = 0;
        scr->clearbuffer();
        picker_hire_menu_engine_draw_content(&px_state);
        const std::vector<std::uint32_t> broke = region(240, 148, 300, 157);
        // Only the cost digits' ink changes (the font shades each glyph
        // from its base colour, so a colour is a small ramp): every changed
        // pixel moves from the affordable ramp to a DISJOINT broke ramp of
        // the same size.
        std::set<std::uint32_t> affordable_ink;
        std::set<std::uint32_t> broke_ink;
        for (std::size_t i = 0; i < affordable.size(); ++i)
        {
            if (affordable[i] != broke[i])
            {
                affordable_ink.insert(affordable[i]);
                broke_ink.insert(broke[i]);
            }
        }
        const bool disjoint = std::none_of(
            affordable_ink.begin(), affordable_ink.end(),
            [&broke_ink](std::uint32_t c) { return broke_ink.count(c) != 0; });
        check(!affordable_ink.empty() &&
              affordable_ink.size() == broke_ink.size() && disjoint);
        save.m_totalcash[0] = saved_cash;
        og::runtime::current_session->current_team_num_ = saved_team;
    }
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
    // The same ready, deployed networked host on TOWER CLIMB: the mode's
    // prepare_launch vetoes a networked session, and the GO says why in the
    // session's words ("local-only", not the local floor-write failure) and
    // sends nothing.
    {
        const std::string saved_mount = get_mounted_campaign();
        const std::string saved_campaign = save.current_campaign;
        const short saved_scen = save.scen_num;
        (void)unmount_campaign_package_with_error(saved_mount);
        const bool tower_mounted =
            mount_campaign_package_with_error(std::string(og::kTowerCampaignId)) ==
            CampaignPackageIoError::None;
        check(tower_mounted);
        if (tower_mounted)
        {
            save.current_campaign = std::string(og::kTowerCampaignId);
            save.scen_num = og::kTowerGateLevel;
            trace_clear();
            client.requested = false;
            check(go_menu(0) == MENU_REDRAW && !client.requested &&
                  trace_contains("popup",
                                 "TOWER CLIMB: TOWER CLIMB is local-only") &&
                  !trace_contains("popup", "Could not prepare"));
        }
        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        check(mount_campaign_package_with_error(saved_mount) ==
              CampaignPackageIoError::None);
        save.current_campaign = saved_campaign;
        save.scen_num = saved_scen;
    }
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

    // picker_compute_guy_derived_stats: guy::family comes verbatim from the
    // .gtl with no range check, so a byte the loader cannot address must
    // price on the SOLDIER row instead of indexing the stat arrays out of
    // bounds.
    {
        // guy::family is a signed char, so a .gtl byte of 0x80..0xff arrives
        // negative — the one value loader::slot_for refuses outright.
        guy stranger(FAMILY_MAGE);
        stranger.family = static_cast<char>(-1);
        guy stranger_as_soldier(stranger);
        stranger_as_soldier.family = FAMILY_SOLDIER;
        check(loader::slot_for(Order::Living, stranger.family) < 0);
        const og::ui::DerivedStats fallback =
            picker_compute_guy_derived_stats(stranger);
        const og::ui::DerivedStats as_soldier =
            picker_compute_guy_derived_stats(stranger_as_soldier);
        check(fallback.hp == as_soldier.hp && fallback.mp == as_soldier.mp &&
              fallback.atk == as_soldier.atk &&
              fallback.def == as_soldier.def &&
              fallback.spd == as_soldier.spd &&
              fallback.atk_spd == as_soldier.atk_spd);
        // Control: a family the loader DOES address reads its own row, so
        // the fallback is not "everybody is a soldier".
        guy mage(FAMILY_MAGE);
        guy mage_as_soldier(mage);
        mage_as_soldier.family = FAMILY_SOLDIER;
        check(loader::slot_for(Order::Living, FAMILY_MAGE) >= 0);
        check(picker_compute_guy_derived_stats(mage).hp !=
              picker_compute_guy_derived_stats(mage_as_soldier).hp);
    }

    // view_scenario_seat_context: a session whose lobby lists no seats at
    // all (solo, before a client has any) still shows the player their own
    // seats — the shared synthesis from the save, every one of them local.
    {
        client.networked = false;
        client.players.clear();
        client.local_indices.clear();
        save.numplayers = 2;
        save.my_team = 0;
        const og::ui::ScenarioSeatContext synthesized =
            view_scenario_seat_context(save);
        check(synthesized.players.size() == 2);
        check(synthesized.players[0].player_index == 0 &&
              synthesized.players[1].player_index == 1);
        check(synthesized.local_player_indices ==
              std::vector<std::uint8_t>{0, 1});
        // Control: a lobby that DOES list seats is taken verbatim.
        og::sim::LobbyPlayer listed;
        listed.player_index = 3;
        listed.team = 2;
        client.players = {listed};
        const og::ui::ScenarioSeatContext from_lobby =
            view_scenario_seat_context(save);
        check(from_lobby.players.size() == 1 &&
              from_lobby.players[0].player_index == 3 &&
              from_lobby.players[0].team == 2);
        check(from_lobby.local_player_indices ==
              std::vector<std::uint8_t>{3});
        client.players.clear();
    }

    // VIEW LEVEL parked under a restage (the per-frame refresh over a real
    // loaded level; pure calls, no menu loop). Rules:
    //  - an authored level title longer than the frame is clipped to 48
    //    characters ("SCEN n: <title>") when a lobby knob restages;
    //  - if the campaign is unmounted while the screen is parked, the
    //    report becomes the single line "CAMPAIGN NOT MOUNTED" with a bare
    //    "SCEN n:" title and the render copy unhealed -- never a stale
    //    report of the previous campaign.
    {
        client.networked = false;
        client.players.clear();
        client.local_indices.clear();
        save.current_campaign = get_mounted_campaign();
        save.scen_num = 1;
        const short saved_time_limit = save.time_limit;
        ViewScenarioEngineState parked;
        parked.scenario = std::make_unique<LevelRuntimeData>(
            save.scen_num, false, &sdl_level_data_hooks());
        const bool loaded = parked.scenario->load();
        check(loaded);
        if (loaded)
        {
            parked.key = view_scenario_current_key(save);
            ViewScenarioEngineState* const saved_state =
                g_view_scenario_engine_state;
            g_view_scenario_engine_state = &parked;

            parked.scenario->world().title = std::string(60, 'T');
            save.time_limit = static_cast<short>(saved_time_limit + 1);
            check(picker_view_scenario_engine_frame_tick(nullptr, 0));
            const std::string prefix =
                std::format("SCEN {}: ", save.scen_num);
            check(parked.title ==
                  prefix + std::string(48 - prefix.size(), 'T'));
            check(!parked.lines.empty() &&
                  parked.lines.front() != "CAMPAIGN NOT MOUNTED");

            parked.render_healed = true;
            save.current_campaign = "org.openglad.cov97.not-mounted";
            check(picker_view_scenario_engine_frame_tick(nullptr, 0));
            check(parked.lines ==
                  std::vector<std::string>{"CAMPAIGN NOT MOUNTED"});
            check(parked.title == std::format("SCEN {}:", save.scen_num));
            check(!parked.render_healed && parked.pager.page_count() == 1);

            g_view_scenario_engine_state = saved_state;
        }
        save.time_limit = saved_time_limit;
        save.current_campaign = get_mounted_campaign();
    }

    // VIEW LEVEL render-copy heal from the lobby's staged pair: a keyframe
    // that does not deserialize leaves the copy UNHEALED at that generation
    // (the band shows degradation text, never a stale world claiming to be
    // the stage); a real pair heals it and traces the pane generation.
    {
        save.current_campaign = get_mounted_campaign();
        save.scen_num = 1;
        ViewScenarioEngineState heal;
        heal.scenario = std::make_unique<LevelRuntimeData>(
            save.scen_num, false, &sdl_level_data_hooks());
        const bool loaded = heal.scenario->load();
        check(loaded);
        if (loaded)
        {
            GameWorld& staged = heal.scenario->world();
            og::sim::InitialSetupMessage setup;
            setup.level_id = staged.id;
            setup.level_title = staged.title;
            setup.pixmaxx = staged.pixmaxx;
            setup.pixmaxy = staged.pixmaxy;
            setup.my_team = staged.my_team;
            client.staged_setup = og::sim::serialize_initial_setup_message(setup);
            client.staged_keyframe = {0xDE, 0xAD, 0xBE, 0xEF};
            client.staged_generation = 7;
            client.has_staged_pair = true;
            trace_clear();
            view_scenario_heal_render_copy(heal);
            check(!heal.render_healed && heal.healed_generation == 7 &&
                  !trace_contains("picker", "view_scenario pane gen="));

            client.staged_keyframe = og::sim::serialize_snapshot(
                og::sim::capture_keyframe_snapshot(staged));
            client.staged_generation = 8;
            view_scenario_heal_render_copy(heal);
            check(heal.render_healed && heal.healed_generation == 8 &&
                  trace_contains("picker", "view_scenario pane gen=8"));
        }
        client.has_staged_pair = false;
        client.staged_generation = 0;
        client.staged_setup.clear();
        client.staged_keyframe.clear();
    }

    // The VIEW LEVEL band with nothing staged names WHO it is waiting on: a
    // networked joiner waits for the host's preview (24 characters of ink),
    // the host itself has no preview to show (27). Text is centred in the
    // band, so the ink span pins which string drew.
    {
        ViewScenarioEngineState band_state;
        ViewScenarioEngineState* const saved_band_state =
            g_view_scenario_engine_state;
        g_view_scenario_engine_state = &band_state;
        screen* const scr = og::runtime::current_session->myscreen_;
        const auto band_ink_span = [scr]() {
            const int y0 = kViewScenarioPreviewBandY +
                           kViewScenarioPreviewBandH / 2 - 3;
            int left = 10000;
            int right = -1;
            // Ink = any pixel on the text rows that differs from the band's
            // own ground at its left edge (the text never reaches x+2).
            for (int y = y0; y < y0 + 8; ++y)
            {
                Uint8 gr = 0, gg = 0, gb = 0;
                scr->get_pixel(kViewScenarioPreviewBandX + 2, y, &gr, &gg, &gb);
                for (int x = kViewScenarioPreviewBandX + 2;
                     x < kViewScenarioPreviewBandX + kViewScenarioPreviewBandW - 2;
                     ++x)
                {
                    Uint8 r = 0, g = 0, b = 0;
                    scr->get_pixel(x, y, &r, &g, &b);
                    if (r != gr || g != gg || b != gb)
                    {
                        left = std::min(left, x);
                        right = std::max(right, x);
                    }
                }
            }
            return right < left ? 0 : right - left + 1;
        };
        const auto spans_chars = [](int span, int chars) {
            return span > 6 * (chars - 1) && span <= 6 * chars;
        };
        client.networked = true;
        client.host_visible = false;
        picker_view_scenario_staged_draw_background(nullptr);
        const int joiner_span = band_ink_span();
        client.host_visible = true;
        picker_view_scenario_staged_draw_background(nullptr);
        const int host_span = band_ink_span();
        if (!spans_chars(joiner_span, 24) || !spans_chars(host_span, 27))
            std::fprintf(stderr, "  [view band] joiner span %d, host span %d\n",
                         joiner_span, host_span);
        check(spans_chars(joiner_span, 24));
        check(spans_chars(host_span, 27));
        client.networked = false;
        client.host_visible = false;
        g_view_scenario_engine_state = saved_band_state;
    }

    // Remote-start preemption: a host GO launches a peer parked in VIEW
    // LEVEL, PROGRESS or TRAIN -- each subscreen returns the remote
    // MENU_EXIT instead of its ordinary MENU_REDRAW. HANG TRAP: the
    // screens block in their menu loop unless the preemption is armed, so
    // the arm is asserted first and the screens run only when it holds.
    {
        client.networked = false;
        client.host_visible = true;
        client.editable = true;
        client.has_start_config = true;
        save.current_campaign = get_mounted_campaign();
        save.scen_num = 1;
        save.numplayers = 1;
        save.my_team = 0;
        for (auto& member : save.team_list)
            member.reset();
        save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[0]->teamnum = 0;
        save.team_size = 1;
        g_start_game_requested = true;
        Sint32 armed_value = 0;
        const bool armed = team_build_remote_start_requested(armed_value);
        check(armed && armed_value == MENU_EXIT);
        if (armed)
        {
            check(create_view_scenario_menu(0) == MENU_EXIT);
            check(create_progress_menu(0) == MENU_EXIT);
            check(create_train_menu(0) == MENU_EXIT);
        }
        g_start_game_requested = false;
        pks().selected_menu_item = nullptr;
        pks().train_session = nullptr;
    }

    // TRAIN content pass colours and accuracy line (one member, real train
    // rows materialized through the product accessor, as the engine does).
    // Rules:
    //  - with nothing staged every stat value and LEVEL draw in STAT_COLOR;
    //  - after a staged level-up every stat value draws STAT_LEVELED and the
    //    LEVEL value STAT_CHANGED;
    //  - the info box reads "Accuracy: <hits*100/shots>%" once the fighter
    //    has landed a hit, "Accuracy: N/A" before.
    // Oracles: the palette indices of the inked pixels in a value cell (the
    // lowest-index rule get_pixel applies), and "re-rendering the expected
    // accuracy line over the drawn one changes no pixel".
    {
        screen* const scr = og::runtime::current_session->myscreen_;
        const auto canonical = [](unsigned char color) {
            int wr = 0, wg = 0, wb = 0;
            query_palette_reg(color, &wr, &wg, &wb);
            for (int i = 0; i < 256; ++i)
            {
                int r = 0, g = 0, b = 0;
                query_palette_reg(static_cast<unsigned char>(i), &r, &g, &b);
                if (r == wr && g == wg && b == wb)
                    return i;
            }
            return static_cast<int>(color);
        };
        // The 6px font shades each glyph row one palette step: a glyph in
        // `color` inks color+0 .. color+4 top to bottom.
        const auto shaded = [&canonical](unsigned char color) {
            std::set<int> inks;
            for (int step = 0; step < 5; ++step)
                inks.insert(canonical(static_cast<unsigned char>(color + step)));
            return inks;
        };
        // Inked indices of the value cell at row y (x 88..111, 7 rows),
        // against the text bar's own index sampled at its right edge.
        const auto value_ink = [scr](int y) {
            int background = -1;
            scr->get_pixel(114, y + 3, &background);
            std::set<int> inks;
            for (int yy = y; yy < y + 7; ++yy)
            {
                for (int xx = 88; xx < 112; ++xx)
                {
                    int index = -1;
                    scr->get_pixel(xx, yy, &index);
                    if (index != background)
                        inks.insert(index);
                }
            }
            return inks;
        };
        const auto grab = [scr](int x0, int y0, int x1, int y1) {
            std::vector<int> pixels;
            for (int yy = y0; yy <= y1; ++yy)
                for (int xx = x0; xx <= x1; ++xx)
                {
                    int index = -1;
                    scr->get_pixel(xx, yy, &index);
                    pixels.push_back(index);
                }
            return pixels;
        };
        // Accuracy is info line 1: x 180, y = 34 + 4 + 10.
        constexpr int kAccuracyY = 48;
        const auto accuracy_line_reads = [&](const char* expected) {
            const std::vector<int> drawn = grab(176, kAccuracyY, 303,
                                                kAccuracyY + 7);
            scr->text_normal.write_xy(180, kAccuracyY, expected,
                                      static_cast<unsigned char>(DARK_BLUE),
                                      1);
            return grab(176, kAccuracyY, 303, kAccuracyY + 7) == drawn;
        };

        client.networked = false;
        client.editable = true;
        for (auto& member : save.team_list)
            member.reset();
        save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[0]->teamnum = 0;
        save.team_list[0]->total_hits = 0;
        save.team_list[0]->total_shots = 0;
        save.team_size = 1;
        og::runtime::current_session->localbuttons_ = init_buttons(
            picker_trainmenu_buttons(), picker_trainmenu_button_count());
        TrainEngineState colour_state;

        // Nothing staged; the fighter has never hit anything.
        {
            og::ui::TrainSession plain_session(save);
            pks().train_session = &plain_session;
            sync_current_guy_from_train();
            picker_train_menu_engine_draw_content(&colour_state);
            check(value_ink(DOWN(0)) ==
                      shaded(STAT_COLOR) &&
                  value_ink(DOWN(5)) ==
                      shaded(STAT_COLOR));
            check(!accuracy_line_reads("   Accuracy: 50% ") &&
                  accuracy_line_reads("   Accuracy: N/A "));
            pks().train_session = nullptr;
        }

        // A staged level-up; the fighter landed 1 of 2 shots.
        save.team_list[0]->total_hits = 1;
        save.team_list[0]->total_shots = 2;
        {
            og::ui::TrainSession levelled_session(save);
            pks().train_session = &levelled_session;
            sync_current_guy_from_train();
            check(increase_stat(BUT_LEVEL, 1) == MENU_OK &&
                  levelled_session.level_increased());
            picker_train_menu_engine_draw_content(&colour_state);
            check(value_ink(DOWN(0)) ==
                      shaded(STAT_LEVELED) &&
                  value_ink(DOWN(4)) ==
                      shaded(STAT_LEVELED));
            check(value_ink(DOWN(5)) ==
                  shaded(STAT_CHANGED));
            check(accuracy_line_reads("   Accuracy: 50% "));
            pks().train_session = nullptr;
        }
        og::runtime::current_session->current_guy_.reset();
    }

    g_start_game_requested = false;
    pks().selected_menu_item = nullptr;
    pks().train_session = nullptr;
    pks().hire_session = nullptr;
    // The caller pins this: "no check failed" (0) is indistinguishable from
    // "no check ran" without it.
    if (checks_run)
        *checks_run = check_index;
    return failed_check == 0 ? 0 : -failed_check;
}
