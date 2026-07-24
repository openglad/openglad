namespace og::ui::detail {

namespace {

class ScopedRelayBaseUrlEnv
{
public:
    ScopedRelayBaseUrlEnv()
    {
        if (const char* const value =
                std::getenv("OPENGLAD_RELAY_BASE_URL");
            value != nullptr)
        {
            saved_ = value;
        }
    }

    ~ScopedRelayBaseUrlEnv()
    {
        set(saved_);
    }

    ScopedRelayBaseUrlEnv(const ScopedRelayBaseUrlEnv&) = delete;
    ScopedRelayBaseUrlEnv& operator=(const ScopedRelayBaseUrlEnv&) = delete;

    void set(const std::optional<std::string>& value) const
    {
#ifdef _WIN32
        (void)_putenv_s(
            "OPENGLAD_RELAY_BASE_URL",
            value.value_or("").c_str());
#else
        if (value.has_value())
        {
            (void)setenv(
                "OPENGLAD_RELAY_BASE_URL",
                value->c_str(),
                1);
        }
        else
        {
            (void)unsetenv("OPENGLAD_RELAY_BASE_URL");
        }
#endif
    }

private:
    std::optional<std::string> saved_;
};

class ScopedClearedRosterNames
{
public:
    explicit ScopedClearedRosterNames(SaveData& save)
        : save_(save)
    {
        // Copy every name before mutating any member so an allocation failure
        // during construction still leaves the roster untouched.
        for (std::size_t i = 0; i < save_.team_list.size(); ++i)
        {
            if (save_.team_list[i] != nullptr)
                saved_names_[i] = save_.team_list[i]->name;
        }
        for (auto& member : save_.team_list)
        {
            if (member != nullptr)
                member->name.clear();
        }
    }

    ~ScopedClearedRosterNames()
    {
        if (first_member_replaced_)
            save_.team_list[0] = std::move(saved_first_member_);
        for (std::size_t i = 0; i < save_.team_list.size(); ++i)
        {
            if (save_.team_list[i] != nullptr)
                save_.team_list[i]->name = std::move(saved_names_[i]);
        }
    }

    ScopedClearedRosterNames(const ScopedClearedRosterNames&) = delete;
    ScopedClearedRosterNames& operator=(
        const ScopedClearedRosterNames&) = delete;

    void install_first_member(std::unique_ptr<guy> replacement)
    {
        saved_first_member_ = std::move(save_.team_list[0]);
        save_.team_list[0] = std::move(replacement);
        first_member_replaced_ = true;
    }

private:
    SaveData& save_;
    std::array<std::string, MAX_TEAM_SIZE> saved_names_;
    std::unique_ptr<guy> saved_first_member_;
    bool first_member_replaced_ = false;
};

} // namespace

int picker_lobby_network_testing_exercise_internal_helpers()
{
    int checks = 0;
    int check_index = 0;
    int failed_check = 0;
    bool failed = false;
    const auto check = [&checks, &check_index, &failed_check, &failed](bool condition) {
        ++check_index;
        if (condition)
            ++checks;
        else
        {
            failed = true;
            if (failed_check == 0)
                failed_check = check_index;
        }
    };

    check(trim_copy(" \t host player \n ") == "host player");
    check(trim_copy(" \t\n ") == "");
    check(url_encode_component("a b+/~") == "a%20b%2B%2F~");
    check(relay_api_base_url(" https://relay.example/api/// ") ==
          "https://relay.example/api");
    check(relay_api_base_url("https://relay.example/base") ==
          "https://relay.example/base/api");
    check(!crc32_hex_for_file({}).has_value());
    check(campaign_content_hash("").empty());
    check(campaign_content_hash("__openglad_missing_campaign_fixture__").empty());
    check(relay_base_url_or_default(" https://relay.example/custom ") ==
          "https://relay.example/custom");
    {
        ScopedRelayBaseUrlEnv relay_env;
        relay_env.set("https://relay.env.example");
        check(relay_base_url_or_default("") ==
              "https://relay.env.example");
        relay_env.set("");
        check(relay_base_url_or_default("") ==
              std::string(kDefaultRelayBaseUrl));
    }
    check(build_relay_room_websocket_url_impl(
              "https://relay.example",
              "ROOM-1",
              "owner token") ==
          "wss://relay.example/api/room/ROOM-1?owner_token=owner%20token");
    check(build_relay_room_websocket_url_impl(
              "http://relay.example/api",
              "ROOM-2",
              "") ==
          "ws://relay.example/api/room/ROOM-2");
    check(build_relay_room_create_url(
              "wss://relay.example/",
              "",
              "Campaign One",
              "Host+Name") ==
          "https://relay.example/api/create?campaign_name=Campaign%20One&host=Host%2BName");
    check(build_relay_room_list_url("ws://relay.example", "hash one") ==
          "http://relay.example/api/rooms?campaign=hash%20one");
    check(build_relay_room_list_url("wss://relay.example", "") ==
          "https://relay.example/api/rooms");

    check(!find_json_field_value(R"({"code" "missing-colon"})", "code").has_value());
    check(!find_json_field_value(R"({"other":1})", "code").has_value());
    check(!find_json_field_value(R"({"code":   )", "code").has_value());
    check(find_json_field_value(R"({"code":   "A"})", "code").has_value());
    check(extract_json_string_field(R"({"text":"a\\b"})", "text").value_or("") ==
          "a\\b");
    check(!extract_json_string_field(R"({"text":123})", "text").has_value());
    check(!extract_json_string_field(R"({"text":"unterminated})", "text").has_value());
    check(extract_json_u32_field(R"({"count":42})", "count").value_or(0) == 42);
    check(!extract_json_u32_field(R"({"count":"42"})", "count").has_value());
    check(!extract_json_u32_field(
              R"({"count":999999999999999999999999})",
              "count").has_value());
    check(extract_json_i64_field(R"({"created_at":123456})", "created_at").value_or(0) ==
          123456);
    check(!extract_json_i64_field(R"({"created_at":-1})", "created_at").has_value());
    check(!extract_json_i64_field(
              R"({"created_at":999999999999999999999999})",
              "created_at").has_value());

    og::runtime::SessionState* const saved_current_session =
        og::runtime::current_session;
    og::runtime::current_session = nullptr;
    check(current_picker_save() == nullptr);
    check(current_campaign_hash() == campaign_content_hash(kDefaultCampaignId));
    check(!current_campaign_name().empty());
    check(current_host_name(0).empty());
    og::runtime::current_session = saved_current_session;

    const auto created_from_code =
        parse_relay_room_create_response_impl(
            R"({"code":"ROOM-A","owner_token":"owner-a"})");
    check(created_from_code.room_code == "ROOM-A" &&
          created_from_code.owner_token == "owner-a");
    const auto created_from_room_code =
        parse_relay_room_create_response_impl(R"({"room_code":"ROOM-B"})");
    check(created_from_room_code.room_code == "ROOM-B" &&
          created_from_room_code.owner_token.empty());
    const auto created_from_plain =
        parse_relay_room_create_response_impl(" ROOM-C ");
    check(created_from_plain.room_code == "ROOM-C");
    try
    {
        (void)parse_relay_room_create_response_impl(" \n\t ");
        failed = true;
    }
    catch (const std::runtime_error&)
    {
        ++checks;
    }

    check(split_top_level_json_objects("").empty());
    check(split_top_level_json_objects(R"({"not_rooms":[]})").empty());
    check(split_top_level_json_objects(R"(not-json)").empty());
    const auto split_objects = split_top_level_json_objects(
        R"([{"code":"A","note":"literal { brace }"},{"code":"B","note":"escaped \\ slash"}])");
    check(split_objects.size() == 2);
    const auto relay_rooms = parse_relay_room_list(
        R"([{"campaign_hash":"skip"},{"code":" room-1 ","campaign_hash":"hash","campaign_name":"Campaign","host_name":"Host","player_count":3,"created_at":55}])");
    check(relay_rooms.size() == 1 &&
          relay_rooms[0].code == "ROOM-1" &&
          relay_rooms[0].player_count == 3 &&
          relay_rooms[0].created_at_ms == 55);
    const auto wrapped_relay_rooms =
        parse_relay_room_list(R"({"rooms":[{"code":"room-2"}]})");
    check(wrapped_relay_rooms.size() == 1);
    check(!wrapped_relay_rooms.empty() &&
          wrapped_relay_rooms[0].code == "ROOM-2" &&
          wrapped_relay_rooms[0].campaign_hash.empty() &&
          wrapped_relay_rooms[0].player_count == 0 &&
          wrapped_relay_rooms[0].created_at_ms == 0);
    const auto invalid_relay_room_result =
        parse_relay_room_list_result(R"([{"code":"bad room"}])");
    check(invalid_relay_room_result.rooms.empty() &&
          !invalid_relay_room_result.error.empty());
    check(select_lan_ipv4_address(
              std::optional<std::string>{"10.0.0.8"},
              std::optional<std::string>{"192.168.1.9"}) == "10.0.0.8");
    check(select_lan_ipv4_address(
              std::nullopt,
              std::optional<std::string>{"192.168.1.9"}) == "192.168.1.9");
    check(select_lan_ipv4_address(std::nullopt, std::nullopt) ==
          "127.0.0.1");
#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))
    in_addr private_address = {};
    check(inet_pton(AF_INET, "192.168.50.7", &private_address) == 1);
    check(usable_lan_ipv4_string(private_address) ==
          std::optional<std::string>{"192.168.50.7"});
    in_addr loopback_address = {};
    check(inet_pton(AF_INET, "127.0.0.2", &loopback_address) == 1);
    check(!usable_lan_ipv4_string(loopback_address).has_value());
    in_addr unspecified_address = {};
    check(!usable_lan_ipv4_string(unspecified_address).has_value());

    const std::optional<std::string> hostname_address =
        detect_lan_ipv4_via_hostname();
    bool hostname_address_valid = !hostname_address.has_value();
    if (hostname_address.has_value())
    {
        in_addr parsed_hostname_address = {};
        hostname_address_valid =
            inet_pton(
                AF_INET,
                hostname_address->c_str(),
                &parsed_hostname_address) == 1 &&
            usable_lan_ipv4_string(parsed_hostname_address) ==
                hostname_address;
    }
    check(hostname_address_valid);

    const std::optional<std::string> route_address =
        detect_lan_ipv4_via_udp_route();
    bool route_address_valid = !route_address.has_value();
    if (route_address.has_value())
    {
        in_addr parsed_route_address = {};
        route_address_valid =
            inet_pton(
                AF_INET,
                route_address->c_str(),
                &parsed_route_address) == 1 &&
            usable_lan_ipv4_string(parsed_route_address) == route_address;
    }
    check(route_address_valid);
#endif
#ifndef __EMSCRIPTEN__
    {
        NativeRelayRoomListWorkerOwner owner;
        owner.reap_completed();
    }
#endif

    // [NET-R5] networked_save_slot_editable branch matrix: own untagged /
    // own-tagged / foreign-tagged / empty / out-of-range / no-save /
    // no-state shapes.
    {
        SaveData editability_save;
        editability_save.team_list[0] = std::make_unique<guy>(0);
        editability_save.team_list[1] = std::make_unique<guy>(0);
        editability_save.team_list[1]->owner_player_index = 3;  // foreign
        editability_save.team_list[2] = std::make_unique<guy>(0);
        editability_save.team_list[2]->owner_player_index = 0;  // own seat
        editability_save.team_size = 3;

        og::sim::LobbyState editability_state;
        og::sim::LobbyPlayer own_seat;
        own_seat.player_index = 0;
        own_seat.seat_id = 101;
        own_seat.name = "editability-check";
        editability_state.players.push_back(own_seat);
        editability_state.local_seat_ids = {own_seat.seat_id};

        check(!networked_save_slot_editable(
            static_cast<std::size_t>(MAX_TEAM_SIZE),
            &editability_save, &editability_state));
        check(networked_save_slot_editable(  // untagged private row
            0u, &editability_save, &editability_state));
        check(networked_save_slot_editable(  // empty slot (hire may fill)
            5u, &editability_save, &editability_state));
        check(!networked_save_slot_editable(  // foreign-tagged row locks
            1u, &editability_save, &editability_state));
        check(networked_save_slot_editable(  // own-tagged (post-merge) row
            2u, &editability_save, &editability_state));
        check(!networked_save_slot_editable(  // tagged + no lobby view
            2u, &editability_save, nullptr));
        check(networked_save_slot_editable(  // no picker session yet
            1u, nullptr, &editability_state));
        editability_state.local_seat_ids.clear();
        check(!networked_save_slot_editable(  // tagged + no ownership grant
            2u, &editability_save, &editability_state));
    }

    SaveData save;
    save.current_campaign = "campaign-one";
    save.scen_num = 4;
    save.allied_mode = 1;
    save.my_team = 2;
    auto make_member = [](int family,
                          const std::string& name,
                          short team,
                          int id) {
        auto member = std::make_unique<guy>(family);
        member->name = name;
        member->teamnum = team;
        member->id = id;
        member->strength = static_cast<short>(10 + id);
        member->dexterity = static_cast<short>(11 + id);
        member->constitution = static_cast<short>(12 + id);
        member->intelligence = static_cast<short>(13 + id);
        member->armor = static_cast<short>(14 + id);
        member->exp = static_cast<std::uint32_t>(100 + id);
        member->kills = static_cast<short>(id);
        member->level_kills = id + 1;
        member->total_damage = id + 2;
        member->total_hits = id + 3;
        member->total_shots = id + 4;
        member->scen_damage = static_cast<float>(id + 5);
        member->scen_kills = static_cast<short>(id + 6);
        member->scen_damage_taken = static_cast<float>(id + 7);
        member->scen_min_hp = static_cast<float>(id + 8);
        member->scen_shots = static_cast<short>(id + 9);
        member->scen_hits = static_cast<short>(id + 10);
        member->level = static_cast<short>(id + 11);
        return member;
    };
    save.team_list[0] = make_member(0, "Local A", 2, 1);
    save.team_list[2] = make_member(1, "Local B", 2, 2);
    save.team_list[3] = make_member(2, "Other Team", 3, 3);
    save.team_size = 3;
    save.numplayers = 1;

    check(resolve_initial_local_team(save) == 2);
    save.my_team = 7;
    check(resolve_initial_local_team(save) == 2);
    SaveData empty_save;
    check(resolve_initial_local_team(empty_save) == 0);
    check(gameplay_start_team(0, 3, 2) == 3);
    check(gameplay_start_team(1, 3, 2) == 3);

    std::array<bool, MAX_TEAM_SIZE> excluded_slots{};
    excluded_slots.fill(false);
    excluded_slots[2] = true;
    og::sim::LobbyPlayer local_player =
        build_local_lobby_player(save, "local-player", 2, &excluded_slots);
    check(local_player.name == "local-player" &&
          local_player.character_slots.size() == 2 &&
          local_player.character_slots[0].slot_index == 0 &&
          local_player.character_slots[1].slot_index == 3);
    og::sim::LobbyMessage join_message =
        make_join_message(save, "local-player", 2, &excluded_slots);
    check(join_message.kind() == og::sim::LobbyMessageKind::Join);
    check(std::get<og::sim::LobbyJoinMessage>(join_message.payload)
              .extra_players.empty());

    // Multi-seat declarations: derived names, distinct seat teams (tracked
    // seat-0 team first, then the save's remaining teams, then free padding).
    check(derive_local_seat_name("base", 0) == "base");
    check(derive_local_seat_name("base", 2) == "base#2");
    check(is_local_seat_name("base", "base"));
    check(is_local_seat_name("base#3", "base"));
    check(!is_local_seat_name("base3", "base"));
    check(!is_local_seat_name("other", "base"));
    const std::vector<short> declared_teams =
        resolve_local_seat_declaration_teams(save, 2, 3);
    check(declared_teams.size() == 3 &&
          declared_teams[0] == 2 &&
          declared_teams[1] == 3 &&
          declared_teams[2] == 0);
    const std::vector<short> one_declared_team =
        resolve_local_seat_declaration_teams(save, 2, 1);
    check(one_declared_team == std::vector<short>{2});
    check(seed_local_seat_assignments(save, true) ==
          std::vector<short>{2});
    std::vector<short> resized_teams{0, 1, 2, 3};
    resize_local_seat_assignments(
        resized_teams, save, false, 2);
    check(resized_teams == std::vector<short>({0, 1}));
    resized_teams.clear();
    SaveData empty_assignment_save;
    resize_local_seat_assignments(
        resized_teams, empty_assignment_save, false, MAX_PLAYERS);
    std::vector<short> expected_default_teams;
    expected_default_teams.reserve(MAX_PLAYERS);
    for (short team = 0; team < MAX_PLAYERS; ++team)
        expected_default_teams.push_back(team);
    check(resized_teams == expected_default_teams);
    const auto default_seat =
        build_local_lobby_seats(save, "default-seat", {});
    check(default_seat.size() == 1 &&
          default_seat.front().team == resolve_initial_local_team(save));
    const std::vector<og::sim::LobbyPlayer> declared_seats =
        build_local_lobby_seats(save, "local-player", 2, 2, &excluded_slots);
    check(declared_seats.size() == 2 &&
          declared_seats[0].name == "local-player" &&
          declared_seats[1].name == "local-player#1" &&
          declared_seats[0].team == 2 &&
          declared_seats[1].team == 3);
    const og::sim::LobbyMessage multi_join_message =
        make_join_message(save, "local-player", 2, &excluded_slots, 2);
    const auto& multi_join =
        std::get<og::sim::LobbyJoinMessage>(multi_join_message.payload);
    check(multi_join.player.name == "local-player" &&
          multi_join.extra_players.size() == 1 &&
          multi_join.extra_players[0].name == "local-player#1");

    // Keep the state-equivalent exercise below to one local + one remote slot.
    local_player.character_slots.resize(1);

    og::sim::LobbyMessage settings_message = make_settings_message(save);
    check(settings_message.kind() == og::sim::LobbyMessageKind::SettingsChange);

    og::sim::LobbyState state;
    state.settings.campaign_id = "";
    state.settings.scenario_id = 0;
    state.settings.difficulty = 5;
    state.settings.allied_mode = 1;
    state.host_player_id = 1;
    local_player.player_index = 1;
    local_player.seat_id = 201;
    local_player.is_host = true;
    og::sim::LobbyPlayer remote_player;
    remote_player.player_index = 2;
    remote_player.seat_id = 202;
    remote_player.name = "remote-player";
    remote_player.team = 3;
    remote_player.character_slots.push_back(local_player.character_slots[0]);
    remote_player.character_slots[0].slot_index = 5;
    remote_player.character_slots[0].character.name = "Remote";
    remote_player.character_slots[0].character.teamnum = 3;
    state.players = {remote_player, local_player};
    state.local_seat_ids = {local_player.seat_id};

    if (SaveData* const active_save = current_picker_save();
        active_save != nullptr)
    {
        ScopedClearedRosterNames roster_restore(*active_save);
        check(current_host_name(-1).empty());
        auto fallback_member = std::make_unique<guy>(0);
        fallback_member->name = "Fallback Host";
        roster_restore.install_first_member(std::move(fallback_member));
        const std::string fallback_host_name = current_host_name(-1);
        check(fallback_host_name == "Fallback Host");
    }

    check(find_local_player(state) != nullptr &&
          find_local_player(state)->seat_id == local_player.seat_id);
    check(local_player_is_host(state));
    og::sim::LobbyState remote_view = state;
    remote_view.local_seat_ids = {remote_player.seat_id};
    check(!local_player_is_host(remote_view));

    // Names and company/roster content are client-controlled. Even an exact
    // spoof sorted before our player must not become editable, targetable, or
    // eligible for the local gameplay binding.
    og::sim::LobbyState spoofed_state = state;
    spoofed_state.players.front().name = local_player.name;
    spoofed_state.players.front().company = local_player.company;
    check(find_local_player(spoofed_state) != nullptr &&
          find_local_player(spoofed_state)->seat_id == local_player.seat_id);

    // A reply from an older GO attempt may be queued immediately before a
    // new request. Only the matching request ID may resolve the new pending
    // attempt; followers with no local request still accept the broadcast.
    og::sim::LobbyState stale_denial = state;
    stale_denial.last_start_request_id = 40;
    stale_denial.last_start_denial =
        og::sim::start_denial_reason_value(
            og::sim::StartDenialReason::MachinesNotReady);
    check(!start_denial_matches_request(stale_denial, 41));
    stale_denial.last_start_request_id = 41;
    check(start_denial_matches_request(stale_denial, 41));
    og::sim::LobbyMessage stale_confirmation;
    stale_confirmation.payload = og::sim::LobbyStartGameMessage{
        .player_index = local_player.player_index,
        .request_id = 40,
    };
    check(!start_confirmation_matches_request(stale_confirmation, 41));
    check(start_confirmation_matches_request(stale_confirmation, 0));
    std::get<og::sim::LobbyStartGameMessage>(
        stale_confirmation.payload).request_id = 41;
    check(start_confirmation_matches_request(stale_confirmation, 41));

    // Team/ready requests must likewise ignore an unrelated state queued
    // before their own echo. The shared bounded waiter keeps polling until
    // the requested authoritative value appears, and still terminates when a
    // denial leaves it unchanged.
    int echo_polls = 0;
    bool requested_value = false;
    check(wait_for_authoritative_lobby_value(
        [&] {
            ++echo_polls;
            if (echo_polls == 2)
                requested_value = true;
        },
        [&] { return requested_value; },
        3,
        false));
    check(echo_polls == 2);
    int denied_polls = 0;
    check(!wait_for_authoritative_lobby_value(
        [&] { ++denied_polls; },
        [] { return false; },
        3,
        false));
    check(denied_polls == 3);
    int yielding_polls = 0;
    check(!wait_for_authoritative_lobby_value(
        [&] { ++yielding_polls; },
        [] { return false; },
        1,
        true));
    check(yielding_polls == 1);
    const auto equivalent =
        build_save_data_equivalent_from_state(state, false);
    check(equivalent.current_campaign == std::string(kDefaultCampaignId) &&
          equivalent.scen_num == 1 &&
          equivalent.numplayers == 1 &&
          equivalent.team_list.size() == 2 &&
          equivalent.team_list[0].character.teamnum == 2 &&
          equivalent.team_list[0].slot_index == 0 &&
          equivalent.team_list[1].slot_index == 1 &&
          equivalent.team_list[0].owner_save_slot == 0 &&
          equivalent.team_list[1].owner_save_slot == 5 &&
          equivalent.team_list[0].owner_player_index == 1 &&
          equivalent.team_list[1].owner_player_index == 2);
    og::sim::LobbyState colliding_private_slots = state;
    colliding_private_slots.players[0].character_slots[0].slot_index = 0;
    const auto collision_equivalent =
        build_save_data_equivalent_from_state(colliding_private_slots, false);
    check(collision_equivalent.team_list.size() == 2 &&
          collision_equivalent.team_list[0].slot_index == 0 &&
          collision_equivalent.team_list[1].slot_index == 1 &&
          collision_equivalent.team_list[0].owner_save_slot == 0 &&
          collision_equivalent.team_list[1].owner_save_slot == 0 &&
          collision_equivalent.team_list[0].owner_player_index == 2 &&
          collision_equivalent.team_list[1].owner_player_index == 1);
    check(build_save_data_equivalent_from_state(state, true).numplayers == 0);
    SaveData applied_save;
    applied_save.team_list[0] = make_member(0, "Private", 0, 9);
    applied_save.team_size = 1;
    apply_lobby_state_to_save(state, applied_save, true, 4, 1u);
    check(applied_save.current_campaign == std::string(kDefaultCampaignId) &&
          applied_save.scen_num == 1 &&
          applied_save.numplayers == 0 &&
          applied_save.my_team == 4 &&
          applied_save.team_size == 1 &&
          applied_save.team_list[0] != nullptr &&
          applied_save.team_list[0]->name == "Private" &&
          applied_save.team_list[0]->teamnum == local_player.team);

    og::sim::LobbyState oversized_state = state;
    oversized_state.players.clear();
    oversized_state.players.push_back(remote_player);
    oversized_state.players[0].character_slots.clear();
    for (std::uint8_t slot = 0; slot < MAX_TEAM_SIZE + 1; ++slot)
    {
        og::sim::LobbyCharacterSlot lobby_slot = local_player.character_slots[0];
        lobby_slot.slot_index = slot;
        oversized_state.players[0].character_slots.push_back(lobby_slot);
    }
    apply_lobby_state_to_save(oversized_state, applied_save, false, 1, 1u);
    check(applied_save.team_size == 1 &&
          applied_save.team_list[0] != nullptr &&
          applied_save.team_list[0]->name == "Private");
    og::sim::LobbyState invalid_private_slot_state = state;
    invalid_private_slot_state.players.back().character_slots[0].slot_index =
        static_cast<std::uint8_t>(MAX_TEAM_SIZE);
    apply_lobby_state_to_save(
        invalid_private_slot_state, applied_save, false, 1, 1u);
    check(applied_save.team_list[0] != nullptr &&
          applied_save.team_list[0]->name == "Private");

    // §4.2 [NET-F2] reconciliation: an echo whose own-seat deploy flag
    // differs adopts the server's flag into the private roster and reports
    // it (benched counted separately); a flag-identical re-apply reports
    // nothing (the convergence loop terminates). Benched slots also drop
    // out of the joiner-mirror equivalent (assembly filter).
    og::sim::LobbyState benched_state = state;
    for (og::sim::LobbyPlayer& benched_player : benched_state.players)
    {
        if (benched_player.name == "local-player")
            benched_player.character_slots[0].deployed = false;
    }
    check(applied_save.team_list[0]->deployed);
    const LobbyStateApplyResult reconciled = apply_lobby_state_to_save(
        benched_state, applied_save, true, 4, 1u);
    check(reconciled.deploy_flags_adopted == 1 &&
          reconciled.deploy_benched == 1 &&
          applied_save.team_list[0] != nullptr &&
          !applied_save.team_list[0]->deployed);
    const LobbyStateApplyResult reconciled_again = apply_lobby_state_to_save(
        benched_state, applied_save, true, 4, 1u);
    check(reconciled_again.deploy_flags_adopted == 0 &&
          reconciled_again.deploy_benched == 0);
    const auto benched_equivalent =
        build_save_data_equivalent_from_state(benched_state, false);
    check(benched_equivalent.team_list.size() == 1 &&
          benched_equivalent.team_list[0].owner_player_index == 2);
    applied_save.team_list[0]->deployed = true;

    class RawTransport final : public og::sim::ITransport {
    public:
        std::vector<og::sim::ReceivedMessage> incoming;
        std::vector<std::vector<std::uint8_t>> sent;
        int accept_count = 0;
        std::vector<og::sim::PeerId> disconnected;

        void send(og::sim::PeerId,
                  const std::uint8_t* data,
                  std::size_t len) override
        {
            sent.emplace_back(data, data + len);
        }

        std::vector<og::sim::ReceivedMessage> poll() override
        {
            auto result = std::move(incoming);
            incoming.clear();
            return result;
        }

        void accept_connections() override { ++accept_count; }
        void disconnect(og::sim::PeerId peer_id) override
        {
            disconnected.push_back(peer_id);
        }
        std::vector<og::sim::PeerId> connected_peers() const override
        {
            return {7};
        }
    };

    RawTransport raw_transport;
    og::sim::ITransport& raw_transport_interface = raw_transport;
    raw_transport_interface.accept_connections();
    raw_transport_interface.disconnect(7);
    check(raw_transport.accept_count == 1 &&
          raw_transport.disconnected == std::vector<og::sim::PeerId>{7} &&
          raw_transport_interface.connected_peers() ==
          std::vector<og::sim::PeerId>{7});
    raw_transport.incoming.push_back({1, {0}});
    std::vector<std::uint8_t> unknown_message;
    og::sim::append_transport_header(unknown_message, 99, 0);
    raw_transport.incoming.push_back({2, unknown_message});
    std::vector<std::uint8_t> malformed_lobby;
    og::sim::append_transport_header(
        malformed_lobby,
        og::sim::kLobbyMessageType,
        0);
    raw_transport.incoming.push_back({3, malformed_lobby});
    std::vector<std::uint8_t> malformed_state;
    og::sim::append_transport_header(
        malformed_state,
        og::sim::kLobbyStateMessageType,
        0);
    raw_transport.incoming.push_back({3, malformed_state});
    raw_transport.incoming.push_back(
        {4, og::sim::serialize_lobby_message(join_message)});
    raw_transport.incoming.push_back(
        {5, og::sim::serialize_lobby_state_message(state)});
    const auto typed_from_raw = poll_lobby_transport_messages(raw_transport);
    check(typed_from_raw.size() == 2 &&
          typed_from_raw[0].kind == og::sim::TypedReceivedMessageKind::LobbyMessage &&
          typed_from_raw[1].kind == og::sim::TypedReceivedMessageKind::LobbyState);
    send_lobby_message(raw_transport, 9, std::move(join_message));
    check(!raw_transport.sent.empty());

    class TypedTransport final : public og::sim::ITransport {
    public:
        std::vector<og::sim::TypedReceivedMessage> typed;
        int send_count = 0;
        int accept_count = 0;
        std::vector<og::sim::PeerId> disconnected;

        bool supports_typed_messages() const noexcept override
        {
            return true;
        }

        std::vector<og::sim::TypedReceivedMessage> poll_typed() override
        {
            return std::move(typed);
        }

        void send(og::sim::PeerId,
                  const std::uint8_t*,
                  std::size_t) override
        {
            ++send_count;
        }
        std::vector<og::sim::ReceivedMessage> poll() override
        {
            return {};
        }
        void accept_connections() override { ++accept_count; }
        void disconnect(og::sim::PeerId peer_id) override
        {
            disconnected.push_back(peer_id);
        }
        std::vector<og::sim::PeerId> connected_peers() const override
        {
            return {};
        }
    };

    TypedTransport typed_transport;
    og::sim::ITransport& typed_transport_interface = typed_transport;
    typed_transport_interface.accept_connections();
    typed_transport_interface.disconnect(7);
    check(typed_transport_interface.connected_peers().empty());
    typed_transport_interface.send(7, nullptr, 0);
    check(typed_transport.send_count == 1 &&
          typed_transport.accept_count == 1 &&
          typed_transport.disconnected ==
              std::vector<og::sim::PeerId>{7});
    check(typed_transport_interface.poll().empty());
    og::sim::TypedReceivedMessage typed_message;
    typed_message.peer_id = 1;
    typed_message.kind = og::sim::TypedReceivedMessageKind::LobbyState;
    typed_message.lobby_state = std::make_shared<og::sim::LobbyState>(state);
    typed_transport.typed.push_back(std::move(typed_message));
    check(poll_lobby_transport_messages(typed_transport).size() == 1);

    check(resolve_pending_local_team(state, -1) == 0);
    og::sim::LobbyState full_state;
    for (short team = 0; team < MAX_PLAYERS; ++team)
    {
        og::sim::LobbyPlayer player;
        player.name = std::format("player-{}", team);
        player.team = team;
        full_state.players.push_back(std::move(player));
    }
    check(resolve_pending_local_team(full_state, 3) == 3);
    og::sim::LobbyState sparse_ctf_state;
    sparse_ctf_state.settings.campaign_id = std::string(og::kCtfCampaignId);
    sparse_ctf_state.settings.ctf_team_count = 2;
    sparse_ctf_state.settings.ctf_authored_team_mask = 0b1101u;
    check(resolve_pending_local_team(sparse_ctf_state, 2) == 2);
    check(resolve_pending_local_team(sparse_ctf_state, 1) == 0);
    sparse_ctf_state.settings.ctf_team_count = 0;
    check(resolve_pending_local_team(sparse_ctf_state, 3) == 3);
    check(resolve_pending_local_team(sparse_ctf_state, 1) == 0);
    og::sim::LobbyState pending_base_state = state;
    pending_base_state.local_seat_ids.clear();
    og::sim::LobbyPlayer empty_pending;
    check(build_pending_local_lobby_state(
              pending_base_state,
              {empty_pending},
              1).state.players.size() == pending_base_state.players.size());
    og::sim::LobbyPlayer pending_player = local_player;
    pending_player.name = "pending-player";
    // Explicit assignments are shareable, so requested team 3 sticks even
    // though "remote-player" holds it.
    const auto pending_state = build_pending_local_lobby_state(
        pending_base_state,
        {pending_player},
        3);
    check(pending_state.state.players.size() ==
              pending_base_state.players.size() + 1 &&
          pending_state.local_team == 3 &&
          pending_state.state.local_seat_ids.size() == 1 &&
          find_local_player(pending_state.state) != nullptr &&
          find_local_player(pending_state.state)->name == "pending-player");
    check(build_pending_local_lobby_state(
              state,
              {pending_player},
              3).state.players.size() == state.players.size());
    og::sim::LobbyState colliding_pending_id_state = pending_base_state;
    colliding_pending_id_state.players.front().seat_id =
        std::numeric_limits<og::sim::LobbySeatId>::max();
    const auto collision_safe_pending = build_pending_local_lobby_state(
        colliding_pending_id_state,
        {pending_player},
        3);
    check(collision_safe_pending.state.local_seat_ids.size() == 1 &&
          collision_safe_pending.state.local_seat_ids.front() ==
              std::numeric_limits<og::sim::LobbySeatId>::max() - 1u);
    // Classic non-allied lobbies also preserve the explicit shared choice.
    og::sim::LobbyState exclusive_state = pending_base_state;
    exclusive_state.settings.allied_mode = 0;
    const auto exclusive_pending = build_pending_local_lobby_state(
        exclusive_state,
        {pending_player},
        3);
    check(exclusive_pending.local_team == 3);
    // Multi-seat pending merges preserve an intentional same-team choice.
    og::sim::LobbyPlayer pending_seat1 = pending_player;
    pending_seat1.name = "pending-player#1";
    pending_seat1.team = 3;
    const auto multi_pending = build_pending_local_lobby_state(
        pending_base_state,
        {pending_player, pending_seat1},
        3);
    check(multi_pending.state.players.size() ==
              pending_base_state.players.size() + 2 &&
          multi_pending.local_team == 3 &&
          multi_pending.state.local_seat_ids.size() == 2 &&
          multi_pending.state.players.back().team == 3);

    check(build_host_transport_failure_message("direct failed", "relay failed") ==
          "Direct: direct failed\nRelay: relay failed");
    check(build_host_transport_failure_message("direct failed", "") ==
          "direct failed");
    check(build_host_transport_failure_message("", "relay failed") ==
          "relay failed");
    check(build_host_transport_failure_message("", "") ==
          "Unable to host a network lobby.");

    if (og::runtime::current_game_session != nullptr)
    {
        og::runtime::GameSession* const saved_game_session =
            og::runtime::current_game_session;
        og::runtime::current_game_session = nullptr;
        clear_active_gameplay_shadow();
        og::runtime::current_game_session = saved_game_session;
    }

    return failed ? -failed_check : 0;
}

} // namespace og::ui::detail
