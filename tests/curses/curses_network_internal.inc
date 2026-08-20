// Test-only definition included by curses_network.cpp under TESTING. Keeping
// this orchestration in tests/ prevents coverage tools from treating it as
// shipped curses networking logic while preserving translation-unit access.

bool curses_network_testing_inject_mode(CursesGameSession& session,
                                       short requested_respawn_ticks);
int curses_network_testing_force_server_win(CursesGameSession& session,
                                             std::uint32_t pinned_team0_score);
int curses_network_testing_clear_server_team(CursesGameSession& session,
                                              short team);

class CursesNetworkTestingMountRestore
{
public:
    CursesNetworkTestingMountRestore()
        : mounted_before_(get_mounted_campaign())
    {
    }

    ~CursesNetworkTestingMountRestore()
    {
        (void)restore();
    }

    bool restore()
    {
        if (restored_)
            return restore_ok_;

        const std::string mounted_after =
            get_mounted_campaign();
        if (mounted_after != mounted_before_) {
            const CampaignPackageIoError result =
                mounted_before_.empty()
                    ? unmount_campaign_package_with_error(
                          mounted_after)
                    : mount_campaign_package_with_error(
                          mounted_before_);
            restore_ok_ =
                result == CampaignPackageIoError::None;
        }
        restore_ok_ =
            get_mounted_campaign() == mounted_before_ &&
            restore_ok_;
        restored_ = true;
        return restore_ok_;
    }

    const std::string& mounted_before() const
    {
        return mounted_before_;
    }

private:
    std::string mounted_before_;
    bool restored_ = false;
    bool restore_ok_ = true;
};

int curses_network_testing_exercise_internal_helpers()
{
    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
            failed_check = check_index;
    };

    const std::string name_a = make_network_player_name();
    const std::string name_b = make_network_player_name();
    check(!name_a.empty() && name_a != name_b);

    og::sim::SimEventBatch batch;
    batch.events.push_back(og::sim::Event{
        .kind = og::sim::EventKind::Notification,
        .text = "network helper notification",
    });
    batch.events.push_back(og::sim::Event{
        .kind = og::sim::EventKind::EndGame,
        .a = 1,
        .b = 2,
        .text = "",
    });
    batch.events.push_back(og::sim::Event{
        .kind = og::sim::EventKind::SetEnd,
        .text = "",
    });
    std::vector<std::string> notifications;
    collect_notifications(batch, notifications);
    PendingEnd pending;
    apply_game_flow_batch(batch, pending, notifications);
    check(pending.ended && pending.ending == 1 && pending.next_level == 2 &&
        notifications.size() >= 2);

    SaveData save;
    save.current_campaign.clear();
    save.scen_num = 3;
    save.my_team = MAX_PLAYERS;
    save.allied_mode = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    soldier->id = 77;
    soldier->name = "Curses Soldier";
    soldier->teamnum = 2;
    soldier->level = 4;
    save.team_list[3] = std::move(soldier);
    check(resolve_initial_local_team(save) == 2);
    SaveData no_team_save;
    check(resolve_initial_local_team(no_team_save) == 0);

    og::sim::LobbyPlayer player =
        build_local_lobby_player(save, "local-player", 2);
    // "Curses Soldier" (14 chars) exceeds the 11-char wire width on purpose:
    // the builder must pre-clamp it, or the VALIDATE_SERIALIZATION round-trip
    // (reader clamps, writer didn't) rejects every message this roster rides.
    check(player.name == "local-player" && player.team == 2 &&
        player.character_slots.size() == 1 &&
        player.character_slots.front().character.name == "Curses Sold");
    // Downstream helper probes need a valid slot even when the assertion above
    // catches a build_local_lobby_player regression.  Construct it from the
    // known test-owned save instead of dereferencing a possibly empty vector.
    const og::sim::LobbyCharacterSlot fixture_slot{
        .slot_index = 3,
        .character = make_lobby_character_data(*save.team_list[3]),
        .deployed = save.team_list[3]->deployed,
    };

    og::sim::LobbyMessage join_message =
        make_join_message(save, "local-player", 2);
    og::sim::LobbyMessage settings_message = make_settings_message(save, 5);
    check(join_message.kind() == og::sim::LobbyMessageKind::Join &&
        settings_message.kind() == og::sim::LobbyMessageKind::SettingsChange);

    // The settings message carries the difficulty-submenu match rules from
    // the save: respawn mode/delay, permadeath, and the generator rate.
    save.ctf_respawn_ticks = 60;
    save.respawn_mode = 2;
    save.generator_rate = 50;
    save.keep_fallen_heroes = 1;
    save.cross_control = 1;
    save.infinite_gold = 1;
    const og::sim::LobbyMessage rules_message = make_settings_message(save, 5);
    const auto* rules = std::get_if<og::sim::LobbySettingsChangeMessage>(
        &rules_message.payload);
    check(rules != nullptr &&
        rules->settings.difficulty == 5 &&
        rules->settings.ctf_respawn_ticks == 60 &&
        rules->settings.respawn_mode == 2 &&
        rules->settings.generator_rate == 50 &&
        rules->settings.keep_fallen_heroes == 1 &&
        rules->settings.cross_control == 1 &&
        rules->settings.infinite_gold == 1);
    save.ctf_respawn_ticks = 0;
    save.respawn_mode = 0;
    save.generator_rate = 0;
    save.keep_fallen_heroes = 0;
    save.cross_control = 0;
    save.infinite_gold = 0;

    og::sim::LobbyState state;
    state.settings.campaign_id = "";
    state.settings.scenario_id = 3;
    state.host_player_id = 0;
    player.player_index = 0;
    player.seat_id = 701;
    player.is_host = true;
    state.players.push_back(player);
    state.local_seat_ids = {player.seat_id};
    check(find_local_player(state) != nullptr &&
        find_local_player(state)->seat_id == player.seat_id);
    og::sim::LobbyPlayer spoof = player;
    spoof.player_index = 1;
    spoof.seat_id = 702;
    spoof.is_host = false;
    state.players.insert(state.players.begin(), spoof);
    check(find_local_player(state) != nullptr &&
        find_local_player(state)->seat_id == player.seat_id);

    // Preserve same-player/source-slot ordering, including the final
    // slot-order comparator tie-break. This is the deterministic ordering the
    // host and joiner must share before canonicalizing private guy ids.
    og::sim::LobbyState colliding_slots_state;
    colliding_slots_state.settings.campaign_id =
        "gladiator";
    colliding_slots_state.settings.scenario_id = 1;
    og::sim::LobbyPlayer colliding_player = player;
    colliding_player.player_index = 3;
    colliding_player.character_slots.clear();
    for (const std::string_view character_name : {"first", "second"}) {
        og::sim::LobbyCharacterSlot slot = fixture_slot;
        slot.slot_index = 7;
        slot.deployed = true;
        slot.character.name = std::string(character_name);
        colliding_player.character_slots.push_back(std::move(slot));
    }
    colliding_slots_state.players.push_back(std::move(colliding_player));
    const og::sim::LobbySaveDataEquivalent colliding_equivalent =
        build_join_save_equivalent_from_state(colliding_slots_state);
    check(colliding_equivalent.team_list.size() == 2 &&
        colliding_equivalent.team_list[0].character.name == "first" &&
        colliding_equivalent.team_list[1].character.name == "second" &&
        colliding_equivalent.team_list[0].owner_player_index == 3 &&
        colliding_equivalent.team_list[0].owner_save_slot == 7);

    // The wire roster is bounded by SaveData's real 24-slot capacity. Verify
    // that an oversized, otherwise valid roster is rejected rather than
    // silently truncated or compacted.
    og::sim::LobbyState oversized_state;
    oversized_state.settings.campaign_id = "gladiator";
    oversized_state.settings.scenario_id = 1;
    og::sim::LobbyPlayer oversized_player = player;
    oversized_player.character_slots.clear();
    for (std::size_t i = 0; i <= MAX_TEAM_SIZE; ++i) {
        og::sim::LobbyCharacterSlot slot = fixture_slot;
        slot.slot_index = static_cast<std::uint8_t>(i);
        slot.deployed = true;
        slot.character.name = std::format("oversized-{}", i);
        oversized_player.character_slots.push_back(std::move(slot));
    }
    oversized_state.players.push_back(std::move(oversized_player));
    bool oversized_rejected = false;
    try {
        (void)build_join_save_equivalent_from_state(oversized_state);
    } catch (const std::runtime_error& ex) {
        oversized_rejected =
            std::string_view(ex.what()).find("24-slot team limit") !=
            std::string_view::npos;
    }
    check(oversized_rejected);

    class RawTransport final : public og::sim::ITransport {
    public:
        explicit RawTransport(std::vector<og::sim::ReceivedMessage> messages)
            : messages_(std::move(messages))
        {
        }

        void send(og::sim::PeerId peer_id,
                  const std::uint8_t* data,
                  std::size_t len) override
        {
            sent_peer_id_ = peer_id;
            sent_payload_.assign(data, data + len);
        }
        std::vector<og::sim::ReceivedMessage> poll() override
        {
            return std::exchange(messages_, {});
        }
        void accept_connections() override { accepted_ = true; }
        void disconnect(og::sim::PeerId peer_id) override
        {
            disconnected_peer_id_ = peer_id;
        }
        std::vector<og::sim::PeerId> connected_peers() const override { return {1}; }
        bool accepted() const { return accepted_; }
        og::sim::PeerId disconnected_peer_id() const
        {
            return disconnected_peer_id_;
        }
        og::sim::PeerId sent_peer_id() const { return sent_peer_id_; }
        const std::vector<std::uint8_t>& sent_payload() const
        {
            return sent_payload_;
        }

    private:
        std::vector<og::sim::ReceivedMessage> messages_;
        bool accepted_ = false;
        og::sim::PeerId disconnected_peer_id_ = 0;
        og::sim::PeerId sent_peer_id_ = 0;
        std::vector<std::uint8_t> sent_payload_;
    };

    std::vector<std::uint8_t> unknown;
    og::sim::append_transport_header(unknown, og::sim::kHeartbeatMessageType, 0);
    const auto malformed_payload = [](std::uint8_t message_type) {
        std::vector<std::uint8_t> bytes;
        og::sim::append_transport_header(bytes, message_type, 1);
        bytes.push_back(0);
        return bytes;
    };
    RawTransport raw({
        og::sim::ReceivedMessage{.peer_id = 1, .data = {0xff}},
        og::sim::ReceivedMessage{
            .peer_id = 1,
            .data = malformed_payload(og::sim::kLobbyMessageType),
        },
        og::sim::ReceivedMessage{
            .peer_id = 1,
            .data = malformed_payload(og::sim::kLobbyStateMessageType),
        },
        og::sim::ReceivedMessage{.peer_id = 1, .data = og::sim::serialize_lobby_message(join_message)},
        og::sim::ReceivedMessage{.peer_id = 1, .data = og::sim::serialize_lobby_state_message(state)},
        og::sim::ReceivedMessage{.peer_id = 1, .data = unknown},
    });
    og::sim::ITransport& raw_transport = raw;
    raw_transport.accept_connections();
    raw_transport.disconnect(1);
    raw_transport.send(1, unknown.data(), unknown.size());
    check(raw.accepted() &&
        raw.disconnected_peer_id() == 1 &&
        raw.sent_peer_id() == 1 &&
        raw.sent_payload() == unknown &&
        raw_transport.connected_peers() == std::vector<og::sim::PeerId>{1});
    const std::vector<og::sim::TypedReceivedMessage> decoded =
        poll_lobby_transport_messages(raw);
    check(decoded.size() == 2 &&
        decoded[0].kind == og::sim::TypedReceivedMessageKind::LobbyMessage &&
        decoded[1].kind == og::sim::TypedReceivedMessageKind::LobbyState);

    // Staged lobby (#218, protocol v13) over a RAW transport: the joiner's
    // envelope pump must decode the staged pair and drop undecodable staged
    // frames — the in-process typed path never exercises this decode.
    const og::sim::StagedMatchSetupMessage staged_setup{
        .stage_generation = 5u, .setup_bytes = {0x01, 0x02, 0x03}};
    const og::sim::StagedMatchKeyframeMessage staged_keyframe{
        .stage_generation = 5u, .snapshot_bytes = {0x04, 0x05}};
    RawTransport staged_raw({
        og::sim::ReceivedMessage{
            .peer_id = 1,
            .data = malformed_payload(og::sim::kStagedMatchSetupMessageType),
        },
        og::sim::ReceivedMessage{
            .peer_id = 1,
            .data = og::sim::serialize_staged_match_setup_message(staged_setup),
        },
        og::sim::ReceivedMessage{
            .peer_id = 1,
            .data =
                malformed_payload(og::sim::kStagedMatchKeyframeMessageType),
        },
        og::sim::ReceivedMessage{
            .peer_id = 1,
            .data = og::sim::serialize_staged_match_keyframe_message(
                staged_keyframe),
        },
    });
    const std::vector<og::sim::TypedReceivedMessage> staged_decoded =
        poll_lobby_transport_messages(staged_raw);
    check(staged_decoded.size() == 2 &&
        staged_decoded[0].kind ==
            og::sim::TypedReceivedMessageKind::StagedMatchSetup &&
        staged_decoded[0].staged_match_setup != nullptr &&
        *staged_decoded[0].staged_match_setup == staged_setup &&
        staged_decoded[1].kind ==
            og::sim::TypedReceivedMessageKind::StagedMatchKeyframe &&
        staged_decoded[1].staged_match_keyframe != nullptr &&
        *staged_decoded[1].staged_match_keyframe == staged_keyframe);

    // A lobby before any transport/state attachment must expose a coherent
    // empty public state, reject mutations, and render/cancel safely.
    class ScriptedTerminal final : public ITerminal {
    public:
        explicit ScriptedTerminal(std::vector<Key> keys = {})
            : keys_(std::move(keys))
        {
        }
        int rows() const override { return 2; }
        int cols() const override { return 80; }
        bool supports_unicode() const override { return false; }
        bool supports_color() const override { return false; }
        void clear() override { ++clears_; }
        void put(int, int, char32_t, Color, Color, bool) override {}
        void put_str(int, int, std::string_view text, Color, Color,
                     bool) override
        {
            rendered_.push_back(std::string(text));
        }
        void present() override { ++presents_; }
        Key poll_key(bool) override
        {
            ++poll_count_;
            if (next_key_ >= keys_.size())
                return Key::none();
            return keys_[next_key_++];
        }
        void set_cursor_visible(bool) override {}
        void beep() override {}

        int clears() const { return clears_; }
        int presents() const { return presents_; }
        std::size_t consumed_key_count() const { return next_key_; }
        std::size_t poll_count() const { return poll_count_; }
        bool input_exhausted() const
        {
            return next_key_ == keys_.size();
        }
        const std::vector<std::string>& rendered() const { return rendered_; }

    private:
        std::vector<Key> keys_;
        std::size_t next_key_ = 0;
        std::size_t poll_count_ = 0;
        int clears_ = 0;
        int presents_ = 0;
        std::vector<std::string> rendered_;
    };

    SaveData unattached_save;
    CursesLobbyImpl unattached_host(
        LobbyRole::Host, unattached_save, 1);
    check(unattached_host.is_host() &&
        unattached_host.local_player_indices().empty() &&
        !unattached_host.local_ready() &&
        unattached_host.players().empty() &&
        !unattached_host.request_team_change(1) &&
        !unattached_host.request_seat_team_change(0, 1) &&
        !unattached_host.request_seat_team_change(
            0, static_cast<og::sim::LobbySeatId>(123), 1) &&
        !unattached_host.set_ready(true));
    unattached_host.request_start();
    ScriptedTerminal unattached_terminal({
        Key::character(U'x', KeyEvent::Release),
        Key::special(KeyCode::Left),
        Key::special(KeyCode::Right),
        Key::character(U't'),
        Key::character(U'c'),
        Key::character(U's'),
    });
    FakeClock unattached_clock;
    const bool unattached_started =
        unattached_host.poll(unattached_terminal, unattached_clock);
    const std::vector<std::string> unattached_status =
        unattached_host.status_lines();
    check(!unattached_started &&
        unattached_terminal.clears() == 1 &&
        unattached_terminal.presents() == 1 &&
        unattached_terminal.input_exhausted() &&
        unattached_terminal.consumed_key_count() == 6 &&
        unattached_terminal.poll_count() == 7 &&
        !unattached_host.state().has_value() &&
        !unattached_host.start_negotiated() &&
        unattached_status ==
            std::vector<std::string>{
                "Mode: HOST",
                "Waiting for players...",
                "Host controls cross-control"} &&
        std::find(unattached_terminal.rendered().begin(),
                  unattached_terminal.rendered().end(),
                  "Hosting Game") != unattached_terminal.rendered().end());
    unattached_host.cancel();
    check(unattached_host.cancelled());

    CursesLobbyImpl unattached_join(
        LobbyRole::Join, unattached_save, 1);
    check(!unattached_join.is_host() &&
        !unattached_join.set_ready(true) &&
        unattached_join.local_player_indices().empty());

    // run_curses_lobby has two early terminal outcomes: explicit cancellation
    // and a negotiated start whose session construction failed.
    class EarlyExitLobby final : public CursesLobby {
    public:
        EarlyExitLobby(bool starts, bool is_cancelled)
            : starts_(starts), cancelled_(is_cancelled)
        {
        }
        bool poll(ITerminal&, IClock&) override
        {
            ++poll_count_;
            return starts_;
        }
        bool is_host() const override { return true; }
        void request_start() override {}
        std::unique_ptr<CursesGameSession> take_session() override
        {
            ++take_count_;
            return nullptr;
        }
        std::vector<std::string> status_lines() const override { return {}; }
        void cancel() override { cancelled_ = true; }
        bool cancelled() const override { return cancelled_; }
        bool request_team_change(short) override { return false; }
        bool request_seat_team_change(std::uint8_t, short) override
        {
            return false;
        }
        bool request_seat_team_change(
            std::uint8_t, og::sim::LobbySeatId, short) override
        {
            return false;
        }
        std::vector<std::uint8_t> local_player_indices() const override
        {
            return {};
        }
        bool set_ready(bool) override { return false; }
        bool local_ready() const override { return false; }
        std::vector<og::sim::LobbyPlayer> players() const override
        {
            return {};
        }
        int poll_count() const { return poll_count_; }
        int take_count() const { return take_count_; }

    private:
        bool starts_ = false;
        bool cancelled_ = false;
        int poll_count_ = 0;
        int take_count_ = 0;
    };

    ScriptedTerminal unused_terminal;
    FakeClock lobby_clock;
    EarlyExitLobby cancelled_lobby(false, true);
    const GameRunResult cancelled_result =
        run_curses_lobby(cancelled_lobby, unused_terminal, lobby_clock);
    check(!cancelled_result.ended &&
        cancelled_lobby.poll_count() == 1 &&
        cancelled_lobby.take_count() == 0);
    EarlyExitLobby failed_start_lobby(true, false);
    const GameRunResult failed_start_result =
        run_curses_lobby(failed_start_lobby, unused_terminal, lobby_clock);
    check(!failed_start_result.ended &&
        failed_start_lobby.poll_count() == 1 &&
        failed_start_lobby.take_count() == 1);

    // Exercise follow maintenance with no controlled or fallback entity, then
    // add a valid anonymous spectator target before the second frame. The
    // engaged null target must be replaced with that entity's nonzero id.
    auto follow_server = og::sim::InProcessTransport::create_server();
    follow_server->accept_connections();
    auto follow_client_link = follow_server->create_client_transport();
    GameWorld empty_follow_world;
    og::sim::GameClient empty_follow_client(
        *follow_client_link, follow_client_link->local_peer_id(),
        &empty_follow_world);
    CursesFollowState empty_follow;
    maintain_curses_follow(
        empty_follow, empty_follow_client, empty_follow_world, 0);
    check(empty_follow.engaged && empty_follow.target_entity_id == 0);
    auto spectator_entity = std::make_unique<walker>();
    (void)spectator_entity->set_order_family(
        Order::Living, FAMILY_SOLDIER);
    spectator_entity->set_dead(0);
    walker* const spectator_target = spectator_entity.get();
    empty_follow_world.oblist.push_back(
        std::move(spectator_entity));
    check(spectator_target != nullptr &&
        spectator_target->entity_id() != 0);
    if (spectator_target != nullptr) {
        const std::uint32_t spectator_id =
            spectator_target->entity_id();
        maintain_curses_follow(
            empty_follow, empty_follow_client,
            empty_follow_world, 0);
        check(empty_follow.engaged &&
            empty_follow.target_entity_id == spectator_id);
    }

    // A decided CTF match returning to the same scenario is a rematch, so the
    // network-win persistence path must leave both memory and the active
    // company shelf untouched.
    SaveData rematch_save;
    rematch_save.scen_num = 7;
    rematch_save.m_score[0] = 1234;
    GameWorld rematch_world;
    rematch_world.id = 7;
    rematch_world.type = static_cast<char>(
        rematch_world.type | GameWorld::TYPE_SCRIPTED);
    rematch_world.mode.active = true;
    rematch_world.mode.init_attempted = true;
    rematch_world.mode.win_latched = true;
    rematch_world.mode.winner_team = 1;
    const std::string active_company_before =
        og::data::active_company_slot();
    const std::string rematch_probe_slot =
        make_network_player_name() + "-rematch";
    const std::string rematch_probe_file =
        "save/" + rematch_probe_slot + ".gtl";
    const std::string rematch_probe_temp =
        "save/" + rematch_probe_slot + ".tmp.gtl";
    const bool rematch_probe_available =
        !user_file_exists(rematch_probe_file) &&
        !user_file_exists(rematch_probe_temp);
    check(rematch_probe_available);
    if (rematch_probe_available) {
        {
            og::data::ScopedActiveCompany rematch_company(
                rematch_probe_slot);
            check(rematch_company.applied());
            if (rematch_company.applied()) {
                persist_curses_networked_win(
                    rematch_save, rematch_world, 0,
                    rematch_save.scen_num);
                check(rematch_save.scen_num == 7 &&
                    rematch_save.m_score[0] == 1234 &&
                    !user_file_exists(rematch_probe_file) &&
                    !user_file_exists(rematch_probe_temp));
            }
        }
        // Any artifact here was created after the collision-proof ownership
        // check above, so cleanup cannot remove pre-existing user data.
        (void)remove_user_file(rematch_probe_file);
        (void)remove_user_file(rematch_probe_temp);
    }
    check(og::data::active_company_slot() ==
        active_company_before);

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    std::string err;
    check(HostCursesSession::create({}, {}, 1, nullptr, host_client, 0,
                                    /*host_company_save=*/nullptr, &err) ==
            nullptr &&
        !err.empty());

    og::sim::LobbySaveDataEquivalent equivalent;
    equivalent.current_campaign = "gladiator";
    equivalent.scen_num = 1;
    equivalent.numplayers = 1;
    equivalent.allied_mode = 0;
    equivalent.team_list.push_back(og::sim::LobbyCharacterSlot{
        .slot_index = 0,
        .character = fixture_slot.character,
        .deployed = true,
        .owner_player_index = 0,
        .owner_save_slot =
            fixture_slot.slot_index,
    });
    std::vector<og::sim::LobbyPlayerBinding> bindings{
        og::sim::LobbyPlayerBinding{
            .peer_id = host_client->local_peer_id(),
            .player_index = 0,
            .team = 2,
        },
    };
    std::unique_ptr<HostCursesSession> host =
        HostCursesSession::create(equivalent, bindings, 1, server, host_client,
                                  0, /*host_company_save=*/nullptr, &err);
    check(host != nullptr);
    if (host != nullptr) {
        GameWorld& mirror = host->mirror_world();
        GameWorld& authoritative = host->server_world_ref();
        const std::uint32_t initial_tick = authoritative.tick_count_;
        const std::uint32_t initial_next_input_tick = host->next_input_tick();
        const bool initial_worlds_valid =
            &mirror != &authoritative &&
            !mirror.oblist.empty() &&
            !authoritative.oblist.empty();
        InputState input;
        host->send_input(input);
        host->advance();
        const std::uint32_t advanced_tick = authoritative.tick_count_;
        (void)host->drain_messages();
        const std::vector<std::string> second_drain = host->drain_messages();
        host->commit_result_to_save();
        (void)host->server();
        (void)host->client();
        const bool exit_hook_accepts =
            host->server().on_exit_accepted &&
            host->server().on_exit_accepted(2);

        og::sim::ExitPromptBroadcastMessage host_prompt;
        host_prompt.destination_level = 2;
        host_prompt.prompt_text = "Host callback prompt";
        server->send_exit_prompt_broadcast(
            host_client->local_peer_id(),
            std::make_shared<og::sim::ExitPromptBroadcastMessage>(
                host_prompt));
        host->client().poll_messages();
        host->server().poll_incoming_messages();
        const auto& host_prompt_messages =
            host->server().last_polled_messages();
        const bool host_prompt_accepted =
            std::any_of(
                host_prompt_messages.begin(),
                host_prompt_messages.end(),
                [](const og::sim::TypedReceivedMessage& message) {
                    return message.kind ==
                            og::sim::TypedReceivedMessageKind::
                                ExitPromptResponse &&
                        message.exit_prompt_response &&
                        message.exit_prompt_response->accepted;
                });
        const bool stable_before_abort =
            initial_worlds_valid &&
            // Staged lobby (#218): the constructor burns ZERO sim ticks —
            // the world is untensed until the session loop's first step
            // opens the launch gate (the old shape burned 6 handshake
            // ticks, so this pinned initial_tick > 0).
            initial_tick == 0 &&
            initial_next_input_tick == initial_tick + 1 &&
            advanced_tick >= 1 &&
            host->next_input_tick() == authoritative.tick_count_ + 1 &&
            !host->ended() &&
            host->ending() == 0 &&
            host->next_level() <= 0 &&
            second_drain.empty() &&
            exit_hook_accepts &&
            host_prompt_accepted;

        // A pending player respawn deliberately suppresses spectator-follow
        // engagement: the renderer must keep the local corpse/countdown view.
        walker* pending_corpse = nullptr;
        for (const auto& entity : mirror.oblist) {
            walker* const candidate = entity.get();
            if (candidate != nullptr && candidate->myguy != nullptr &&
                candidate->user() == 0) {
                pending_corpse = candidate;
                break;
            }
        }
        check(pending_corpse != nullptr);
        if (pending_corpse != nullptr) {
            const short dead_before = pending_corpse->dead();
            const char type_before = mirror.type;
            const og::sim::ModeState mode_before = mirror.mode;
            pending_corpse->set_dead(1);
            mirror.type = static_cast<char>(
                mirror.type | GameWorld::TYPE_SCRIPTED);
            mirror.mode.active = true;
            mirror.mode.init_attempted = true;
            mirror.respawn.respawn_queue.push_back(
                og::sim::RespawnEntry{
                    .kind = 0,
                    .walker_entity_id =
                        pending_corpse->entity_id(),
                });
            CursesFollowState pending_follow{
                .engaged = true,
                .target_entity_id = pending_corpse->entity_id(),
            };
            maintain_curses_follow(
                pending_follow, host->client(), mirror, 0);
            check(!pending_follow.engaged &&
                pending_follow.target_entity_id == 0);
            pending_corpse->set_dead(dead_before);
            mirror.type = type_before;
            mirror.mode = mode_before;
        }

        host->request_abort();
        bool ended_after_abort = false;
        for (int i = 0; i < 25; ++i) {
            host->advance();
            ended_after_abort = ended_after_abort || host->ended();
        }
        check(stable_before_abort &&
            ended_after_abort &&
            host->ended() &&
            host->ending() != 0);
    }

    err.clear();
    check(JoinCursesSession::create(equivalent, 1, nullptr, 1, 0, &err) ==
              nullptr &&
          !err.empty());

    auto join_server = og::sim::InProcessTransport::create_server();
    join_server->accept_connections();
    auto join_client = join_server->create_client_transport();
    std::unique_ptr<JoinCursesSession> join =
        JoinCursesSession::create(equivalent, 1, join_client,
                                  join_client->local_peer_id(), 0, &err);
    check(join != nullptr);
    if (join != nullptr) {
        check(!curses_network_testing_inject_mode(*join, 30) &&
            curses_network_testing_force_server_win(*join, 5) == -1 &&
            curses_network_testing_clear_server_team(*join, 0) == -1);
        GameWorld& mirror = join->mirror_world();
        const std::uint32_t initial_tick = mirror.tick_count_;
        const std::uint32_t initial_next_input_tick = join->next_input_tick();
        InputState input;
        join->send_input(input);
        join->advance();
        (void)join->drain_messages();
        const std::vector<std::string> second_drain = join->drain_messages();
        join->commit_result_to_save();
        (void)join->client();
        og::sim::ExitPromptBroadcastMessage join_prompt;
        join_prompt.destination_level = 2;
        join_prompt.prompt_text = "Join callback prompt";
        join_server->send_exit_prompt_broadcast(
            join_client->local_peer_id(),
            std::make_shared<og::sim::ExitPromptBroadcastMessage>(
                join_prompt));
        join->advance();
        const auto join_prompt_messages = join_server->poll_typed();
        const bool join_prompt_accepted =
            std::any_of(
                join_prompt_messages.begin(),
                join_prompt_messages.end(),
                [](const og::sim::TypedReceivedMessage& message) {
                    return message.kind ==
                            og::sim::TypedReceivedMessageKind::
                                ExitPromptResponse &&
                        message.exit_prompt_response &&
                        message.exit_prompt_response->accepted;
                });
        const bool stable_before_abort =
            !mirror.oblist.empty() &&
            initial_next_input_tick == initial_tick + 1 &&
            join->next_input_tick() == mirror.tick_count_ + 1 &&
            !join->ended() &&
            join->ending() == 0 &&
            join->next_level() <= 0 &&
            second_drain.empty() &&
            join_prompt_accepted;
        join->request_abort();
        join->advance();
        check(stable_before_abort && !join->ended());
    }

    // #207 design point 4 ("every machine"): the joiner seeds its session
    // save from THIS machine's own company file before the lobby-config
    // apply, so a negotiated landing on a level this machine has cleared
    // (same campaign) arms an excursion whose origin is this machine's own
    // cursor — the fold then restores it before the per-player persist.
    {
        const std::string seed_slot = make_network_player_name() + "-jseed";
        const std::string seed_file = "save/" + seed_slot + ".gtl";
        const std::string seed_temp = "save/" + seed_slot + ".tmp.gtl";
        const bool seed_probe_available =
            !user_file_exists(seed_file) && !user_file_exists(seed_temp);
        check(seed_probe_available);
        if (seed_probe_available) {
            {
                og::data::ScopedActiveCompany seed_company(seed_slot);
                check(seed_company.applied());
                if (seed_company.applied()) {
                    SaveData own;
                    own.reset();
                    own.save_name = "JOIN SEED";
                    own.current_campaign = "gladiator";
                    own.scen_num = 3;
                    own.add_level_completed("gladiator", 1);
                    check(own.save(seed_slot));

                    auto seed_server =
                        og::sim::InProcessTransport::create_server();
                    seed_server->accept_connections();
                    auto seed_client = seed_server->create_client_transport();
                    err.clear();
                    std::unique_ptr<JoinCursesSession> seeded =
                        JoinCursesSession::create(
                            equivalent, 1, seed_client,
                            seed_client->local_peer_id(), 0, &err);
                    check(seeded != nullptr);
                    if (seeded != nullptr) {
                        const SaveData& session_save =
                            seeded->testing_client_save();
                        check(session_save.replay_level == 1 &&
                            session_save.replay_origin == 3 &&
                            session_save.scen_num == 1);
                    }
                }
            }
            (void)remove_user_file(seed_file);
            (void)remove_user_file(seed_temp);
        }
    }

    // V5 Option A (host history) at the internal seam: HostCursesSession
    // seeds its authoritative save from the host machine's company save.
    // REPLAY intent (the arm) survives into server_save_ AND the mirror
    // client_save_ copy, alongside the completed set and the campaign
    // decision book; the VISIT twin (unarmed + completed) stays unarmed —
    // the dedicated auto-arm must not run for a player-seeded session —
    // and its authoritative world loads purged.
    {
        SaveData host_company;
        host_company.reset();
        host_company.current_campaign = "gladiator";
        host_company.add_level_completed("gladiator", 1);
        host_company.scen_num = 3;
        check(host_company.campaign_state_set("gladiator", "watch_paid", 900));
        host_company.arm_replay(1);

        auto replay_server = og::sim::InProcessTransport::create_server();
        replay_server->accept_connections();
        auto replay_client = replay_server->create_client_transport();
        std::vector<og::sim::LobbyPlayerBinding> replay_bindings{
            og::sim::LobbyPlayerBinding{
                .peer_id = replay_client->local_peer_id(),
                .player_index = 0,
                .team = 0,
            },
        };
        err.clear();
        std::unique_ptr<HostCursesSession> replay_host =
            HostCursesSession::create(equivalent, replay_bindings, 1,
                                      replay_server, replay_client, 0,
                                      &host_company, &err);
        check(replay_host != nullptr);
        if (replay_host != nullptr) {
            const SaveData& server_save = replay_host->testing_server_save();
            check(server_save.replay_armed_for(1) &&
                server_save.replay_origin == 3 &&
                server_save.is_level_completed(1) &&
                server_save.campaign_state_get("gladiator", "watch_paid") ==
                    900);
            // The mirror copy carries the same seeded history (the
            // dropped-field pattern through copy_headless_server_save_data).
            int living_count = 0;
            for (const auto& up : replay_host->server_world_ref().oblist) {
                const walker* w = up.get();
                if (w != nullptr && !w->dead() &&
                    w->query_order() == Order::Living &&
                    w->team_num() != 0 && w->myguy == nullptr)
                    ++living_count;
            }
            check(living_count > 0);
        }

        // The VISIT twin: same history, no arm — stays a visit (no
        // auto-arm) and the authoritative world loads purged.
        host_company.clear_replay_arm();
        host_company.scen_num = 1;
        auto visit_server = og::sim::InProcessTransport::create_server();
        visit_server->accept_connections();
        auto visit_client = visit_server->create_client_transport();
        std::vector<og::sim::LobbyPlayerBinding> visit_bindings{
            og::sim::LobbyPlayerBinding{
                .peer_id = visit_client->local_peer_id(),
                .player_index = 0,
                .team = 0,
            },
        };
        err.clear();
        std::unique_ptr<HostCursesSession> visit_host =
            HostCursesSession::create(equivalent, visit_bindings, 1,
                                      visit_server, visit_client, 0,
                                      &host_company, &err);
        check(visit_host != nullptr);
        if (visit_host != nullptr) {
            const SaveData& server_save = visit_host->testing_server_save();
            check(server_save.replay_level == 0 &&
                server_save.is_level_completed(1));
            int hostile_count = 0;
            for (const auto& up : visit_host->server_world_ref().oblist) {
                const walker* w = up.get();
                if (w != nullptr && !w->dead() &&
                    w->query_order() == Order::Living &&
                    w->team_num() != 0 && w->myguy == nullptr)
                    ++hostile_count;
            }
            check(hostile_count == 0);
        }
    }

    // The authored-team helper reads only from the save's mounted campaign.
    // Pin both the failed-load and successful-load paths, then restore the
    // process mount exactly so shuffled neighbors see their original package.
    CursesNetworkTestingMountRestore mount_restore;
    const std::string ctf_campaign = "modes";
    const CampaignPackageIoError ctf_mount =
        mount_campaign_package_with_error(ctf_campaign);
    check(ctf_mount == CampaignPackageIoError::None);
    if (ctf_mount == CampaignPackageIoError::None) {
        SaveData ctf_save;
        ctf_save.current_campaign = ctf_campaign;
        ctf_save.scen_num = 32000;
        check(ctf_authored_team_mask_for_save(ctf_save) == 0);
        ctf_save.scen_num = 500;
        check(ctf_authored_team_mask_for_save(ctf_save) != 0);
    }
    check(mount_restore.restore() &&
        get_mounted_campaign() ==
            mount_restore.mounted_before());

    return failed_check == 0 ? 0 : -failed_check;
}
