// Test-only definition included by curses_network.cpp under TESTING. Keeping
// this orchestration in tests/ prevents coverage tools from treating it as
// shipped curses networking logic while preserving translation-unit access.

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

    og::sim::LobbyPlayer player =
        build_local_lobby_player(save, "local-player", 2);
    check(player.name == "local-player" && player.team == 2 &&
        player.character_slots.size() == 1 &&
        player.character_slots.front().character.name == "Curses Soldier");

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
    const og::sim::LobbyMessage rules_message = make_settings_message(save, 5);
    const auto* rules = std::get_if<og::sim::LobbySettingsChangeMessage>(
        &rules_message.payload);
    check(rules != nullptr &&
        rules->settings.difficulty == 5 &&
        rules->settings.ctf_respawn_ticks == 60 &&
        rules->settings.respawn_mode == 2 &&
        rules->settings.generator_rate == 50 &&
        rules->settings.keep_fallen_heroes == 1 &&
        rules->settings.cross_control == 1);
    save.ctf_respawn_ticks = 0;
    save.respawn_mode = 0;
    save.generator_rate = 0;
    save.keep_fallen_heroes = 0;
    save.cross_control = 0;

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
    RawTransport raw({
        og::sim::ReceivedMessage{.peer_id = 1, .data = {0xff}},
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

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    std::string err;
    check(HostCursesSession::create({}, {}, 1, nullptr, host_client, 0, &err) ==
            nullptr &&
        !err.empty());

    og::sim::LobbySaveDataEquivalent equivalent;
    equivalent.current_campaign = "org.openglad.gladiator";
    equivalent.scen_num = 1;
    equivalent.numplayers = 1;
    equivalent.allied_mode = 0;
    equivalent.team_list.push_back(og::sim::LobbyCharacterSlot{
        .slot_index = 0,
        .character = player.character_slots.front().character,
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
                                  0, &err);
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
        const bool stable_before_abort =
            initial_worlds_valid &&
            initial_tick > 0 &&
            initial_next_input_tick == initial_tick + 1 &&
            advanced_tick >= initial_tick &&
            host->next_input_tick() == authoritative.tick_count_ + 1 &&
            !host->ended() &&
            host->ending() == 0 &&
            host->next_level() <= 0 &&
            second_drain.empty();
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
        const bool stable_before_abort =
            !mirror.oblist.empty() &&
            initial_next_input_tick == initial_tick + 1 &&
            join->next_input_tick() == mirror.tick_count_ + 1 &&
            !join->ended() &&
            join->ending() == 0 &&
            join->next_level() <= 0 &&
            second_drain.empty();
        join->request_abort();
        join->advance();
        check(stable_before_abort && !join->ended());
    }

    return failed_check == 0 ? 0 : -failed_check;
}
