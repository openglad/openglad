// The shared state-machine tests normally use mock clients.  Expose only the
// concrete adapter construction boundary so tests can drive and assert its
// real IPickerClient operations directly.
void campaign_picker_testing_abort();  // campaign_picker_internal.inc

std::unique_ptr<og::ui::IPickerClient> picker_testing_create_sdl_client()
{
    return std::make_unique<SdlPickerClient>();
}

struct PickerSdlClientTestAccess
{
    enum class RequestBehavior
    {
        Pending,
        Complete,
        ThrowStandard,
        ThrowUnknown,
    };

    class RelayRequest final : public og::ui::IPickerRelayRoomListRequest
    {
    public:
        explicit RelayRequest(
            RequestBehavior behavior,
            og::ui::PickerRelayRoomListResult result = {})
            : behavior_(behavior), result_(std::move(result))
        {
        }

        std::optional<og::ui::PickerRelayRoomListResult> poll() override
        {
            ++poll_calls;
            switch (behavior_)
            {
            case RequestBehavior::Pending:
                return std::nullopt;
            case RequestBehavior::Complete:
                return std::move(result_);
            case RequestBehavior::ThrowStandard:
                throw std::runtime_error("relay poll exploded");
            case RequestBehavior::ThrowUnknown:
                throw 17;
            }
            return std::nullopt;
        }

        int poll_calls = 0;

    private:
        RequestBehavior behavior_;
        og::ui::PickerRelayRoomListResult result_;
    };

    static og::ui::PickerRelayRoomInfo room(std::string code,
                                             std::string host_name = {})
    {
        og::ui::PickerRelayRoomInfo result;
        result.code = std::move(code);
        result.host_name = std::move(host_name);
        return result;
    }

    static int run()
    {
        int check_index = 0;
        int failed_check = 0;
        const auto check = [&check_index, &failed_check](bool condition) {
            ++check_index;
            if (!condition && failed_check == 0)
                failed_check = check_index;
        };

        struct BridgeRestore
        {
            PlatformBridge saved = platform_bridge();
            ~BridgeRestore() { set_platform_bridge(std::move(saved)); }
        } bridge_restore;
        struct ActiveClientRestore
        {
            og::ui::IPickerLobbyClient* saved =
                og::ui::active_picker_lobby_client();
            ~ActiveClientRestore()
            {
                og::ui::install_active_picker_lobby_client(saved);
            }
        } active_client_restore;
        struct CampaignRestore
        {
            SaveData& save;
            std::string campaign = save.current_campaign;
            ~CampaignRestore() { save.current_campaign = campaign; }
        } campaign_restore{
            og::runtime::current_session->myscreen_->save_data};
        struct RelayEnvironmentRestore
        {
            bool existed = false;
            std::string value;

            RelayEnvironmentRestore()
            {
                if (const char* current =
                        std::getenv("OPENGLAD_RELAY_BASE_URL"))
                {
                    existed = true;
                    value = current;
                }
            }

            ~RelayEnvironmentRestore()
            {
#ifdef _WIN32
                _putenv_s("OPENGLAD_RELAY_BASE_URL",
                          existed ? value.c_str() : "");
#else
                if (existed)
                    setenv("OPENGLAD_RELAY_BASE_URL", value.c_str(), 1);
                else
                    unsetenv("OPENGLAD_RELAY_BASE_URL");
#endif
            }
        } relay_environment_restore;
        const auto set_relay_environment = [](const char* value) {
#ifdef _WIN32
            _putenv_s("OPENGLAD_RELAY_BASE_URL", value);
#else
            setenv("OPENGLAD_RELAY_BASE_URL", value, 1);
#endif
        };
        set_relay_environment("https://relay.example.test/api");

        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        save.current_campaign = "relay-state-test";

        enum class BeginBehavior
        {
            Request,
            Null,
            ThrowStandard,
            ThrowUnknown,
        };
        BeginBehavior begin_behavior = BeginBehavior::Request;
        int begin_calls = 0;
        std::string requested_base_url;
        std::string requested_campaign;
        PlatformBridge bridge = platform_bridge();
        bridge.list_relay_rooms = {};
        bridge.begin_list_relay_rooms =
            [&](const std::string& base_url,
                const std::string& campaign)
            -> std::unique_ptr<og::ui::IPickerRelayRoomListRequest> {
            ++begin_calls;
            requested_base_url = base_url;
            requested_campaign = campaign;
            switch (begin_behavior)
            {
            case BeginBehavior::Request:
                return std::make_unique<RelayRequest>(
                    RequestBehavior::Pending);
            case BeginBehavior::Null:
                return nullptr;
            case BeginBehavior::ThrowStandard:
                throw std::runtime_error("relay begin exploded");
            case BeginBehavior::ThrowUnknown:
                throw 23;
            }
            return nullptr;
        };
        set_platform_bridge(std::move(bridge));

        SdlPickerClient client;
        client.networking_settings_.use_room_code = true;
        client.relay_rooms_.rooms.push_back(room("OLD"));
        client.relay_rooms_.pending_rooms.push_back(room("PENDING-OLD"));
        client.relay_rooms_.pending_error = "old error";
        client.relay_rooms_.update_pending = true;
        client.relay_rooms_.request = std::make_unique<RelayRequest>(
            RequestBehavior::Pending);
        client.begin_relay_room_list_view();
        check(client.relay_rooms_.view_generation == 1);
        check(client.relay_rooms_.view_campaign_tag == save.current_campaign);
        check(client.relay_rooms_.view_base_url ==
              "https://relay.example.test/api");
        check(client.relay_rooms_.rooms.empty());
        check(client.relay_rooms_.pending_rooms.empty());
        check(client.relay_rooms_.pending_error.empty());
        check(!client.relay_rooms_.update_pending);
        check(client.relay_rooms_.request == nullptr);

        client.relay_rooms_.snapshot_generation =
            client.relay_rooms_.view_generation;
        client.relay_rooms_.snapshot_campaign_tag =
            client.relay_rooms_.view_campaign_tag;
        client.relay_rooms_.snapshot_base_url =
            client.relay_rooms_.view_base_url;
        check(client.relay_room_snapshot_is_current());
        client.relay_rooms_.snapshot_campaign_tag = "stale";
        check(!client.relay_room_snapshot_is_current());

        client.stage_relay_room_list_error("offline");
        check(client.relay_rooms_.update_pending);
        check(client.relay_rooms_.pending_error == "offline");
        client.commit_pending_relay_room_list();
        check(client.relay_rooms_.fetched);
        check(client.relay_rooms_.error == "offline");
        check(!client.relay_rooms_.update_pending);

        client.relay_rooms_.rooms = {room("CURRENT")};
        client.stage_relay_room_list_error("stale error");
        ++client.relay_rooms_.pending_generation;
        client.commit_pending_relay_room_list();
        check(client.relay_rooms_.rooms.size() == 1 &&
              client.relay_rooms_.rooms[0].code == "CURRENT");
        check(!client.relay_rooms_.update_pending);
        client.commit_pending_relay_room_list();

        const auto bind_request_to_current_view =
            [&client](std::unique_ptr<RelayRequest> request) {
                client.relay_rooms_.request = std::move(request);
                client.relay_rooms_.request_generation =
                    client.relay_rooms_.view_generation;
                client.relay_rooms_.request_campaign_tag =
                    client.relay_rooms_.view_campaign_tag;
                client.relay_rooms_.request_base_url =
                    client.relay_rooms_.view_base_url;
            };

        auto pending_request = std::make_unique<RelayRequest>(
            RequestBehavior::Pending);
        RelayRequest* const pending_raw = pending_request.get();
        bind_request_to_current_view(std::move(pending_request));
        client.poll_relay_room_list_request();
        check(pending_raw->poll_calls == 1);
        check(client.relay_rooms_.request.get() == pending_raw);

        og::ui::PickerRelayRoomListResult completed_result;
        completed_result.rooms.push_back(room("GLAD-LIVE", "Host"));
        bind_request_to_current_view(std::make_unique<RelayRequest>(
            RequestBehavior::Complete, std::move(completed_result)));
        client.poll_relay_room_list_request();
        check(client.relay_rooms_.request == nullptr);
        check(client.relay_rooms_.update_pending);
        check(client.relay_rooms_.pending_rooms.size() == 1);
        client.commit_pending_relay_room_list();
        check(client.relay_rooms_.rooms.size() == 1 &&
              client.relay_rooms_.rooms[0].code == "GLAD-LIVE");
        check(client.relay_room_snapshot_is_current());

        og::ui::PickerRelayRoomListResult stale_result;
        stale_result.rooms.push_back(room("GLAD-STALE"));
        bind_request_to_current_view(std::make_unique<RelayRequest>(
            RequestBehavior::Complete, std::move(stale_result)));
        --client.relay_rooms_.request_generation;
        client.poll_relay_room_list_request();
        check(client.relay_rooms_.request == nullptr);
        check(!client.relay_rooms_.update_pending);
        check(client.relay_rooms_.rooms[0].code == "GLAD-LIVE");

        bind_request_to_current_view(std::make_unique<RelayRequest>(
            RequestBehavior::ThrowStandard));
        client.poll_relay_room_list_request();
        check(client.relay_rooms_.request == nullptr);
        check(client.relay_rooms_.pending_error == "relay poll exploded");
        client.clear_pending_relay_room_list();

        bind_request_to_current_view(std::make_unique<RelayRequest>(
            RequestBehavior::ThrowStandard));
        client.relay_rooms_.request_campaign_tag = "stale";
        client.poll_relay_room_list_request();
        check(!client.relay_rooms_.update_pending);

        bind_request_to_current_view(std::make_unique<RelayRequest>(
            RequestBehavior::ThrowUnknown));
        client.poll_relay_room_list_request();
        check(client.relay_rooms_.pending_error ==
              "Relay room listing failed while polling.");
        client.clear_pending_relay_room_list();

        const int calls_before_disabled = begin_calls;
        client.networking_settings_.use_room_code = false;
        client.refresh_relay_room_list(true);
        check(begin_calls == calls_before_disabled);
        client.networking_settings_.use_room_code = true;
        client.relay_rooms_.request = std::make_unique<RelayRequest>(
            RequestBehavior::Pending);
        client.refresh_relay_room_list(true);
        check(begin_calls == calls_before_disabled);
        client.cancel_relay_room_list_request();

        client.relay_rooms_.next_refresh_ms =
            og::input_native::ticks_ms() + 100000u;
        client.refresh_relay_room_list(false);
        check(begin_calls == calls_before_disabled);

        client.relay_rooms_.view_base_url_error = "invalid base";
        client.refresh_relay_room_list(true);
        check(client.relay_rooms_.pending_error == "invalid base");
        client.clear_pending_relay_room_list();
        client.relay_rooms_.view_base_url_error.clear();

        begin_behavior = BeginBehavior::Request;
        client.refresh_relay_room_list(true);
        check(begin_calls == calls_before_disabled + 1);
        check(client.relay_rooms_.request != nullptr);
        check(requested_base_url == client.relay_rooms_.view_base_url);
        check(requested_campaign == client.relay_rooms_.view_campaign_tag);
        check(client.relay_rooms_.request_generation ==
              client.relay_rooms_.view_generation);
        client.cancel_relay_room_list_request();

        begin_behavior = BeginBehavior::ThrowStandard;
        client.refresh_relay_room_list(true);
        check(client.relay_rooms_.pending_error == "relay begin exploded");
        client.clear_pending_relay_room_list();
        begin_behavior = BeginBehavior::ThrowUnknown;
        client.refresh_relay_room_list(true);
        check(client.relay_rooms_.pending_error ==
              "Relay room listing failed.");
        client.clear_pending_relay_room_list();

        // A platform async hook may decline a request.  The interface fallback
        // still supplies an immediate, empty terminal request.
        begin_behavior = BeginBehavior::Null;
        client.refresh_relay_room_list(true);
        check(client.relay_rooms_.request != nullptr);
        client.poll_relay_room_list_request();
        check(client.relay_rooms_.update_pending);
        client.commit_pending_relay_room_list();
        check(client.relay_rooms_.fetched && client.relay_rooms_.rooms.empty());

        const std::uint64_t generation_before =
            client.relay_rooms_.view_generation;
        client.ensure_relay_room_list_view_is_current();
        check(client.relay_rooms_.view_generation == generation_before);
        save.current_campaign = "relay-state-test.changed";
        client.ensure_relay_room_list_view_is_current();
        check(client.relay_rooms_.view_generation == generation_before + 1);
        check(client.relay_rooms_.view_campaign_tag == save.current_campaign);

        set_relay_environment("ftp://invalid-relay.example.test");
        client.begin_relay_room_list_view();
        check(client.relay_rooms_.view_base_url.empty());
        check(!client.relay_rooms_.view_base_url_error.empty());
        check(client.relay_rooms_.update_pending);
        client.commit_pending_relay_room_list();
        check(!client.relay_rooms_.error.empty());

        trace_clear();
        client.networking_settings_.port_text = "0";
        check(!client.submit_network_host());
        check(trace_contains("popup", "1 to 65535"));
        trace_clear();
        client.networking_settings_.use_room_code = false;
        client.networking_settings_.port_text = "12345";
        client.networking_settings_.ip_address = "  \t";
        check(!client.submit_network_join());
        check(trace_contains("popup", "IP address"));
        trace_clear();
        client.networking_settings_.use_room_code = true;
        client.networking_settings_.room_code = "GLAD-ROOM";
        check(!client.submit_network_join());
        check(trace_contains("popup", "Relay base URL"));

        client.networking_settings_.use_room_code = false;
        check(client.prompt_for_relay_room_code_if_blank());
        client.networking_settings_.use_room_code = true;
        client.networking_settings_.room_code = "GLAD-PRESET";
        check(client.prompt_for_relay_room_code_if_blank());
        client.cancel_relay_room_list_request();
        trace_clear();
        return failed_check == 0 ? 0 : -failed_check;
    }

    // The bridge and active-client state every single-shot probe below
    // borrows and hands back.
    struct ProbeRestore
    {
        PlatformBridge saved_bridge = platform_bridge();
        og::ui::IPickerLobbyClient* saved_active =
            og::ui::active_picker_lobby_client();
        ~ProbeRestore()
        {
            set_platform_bridge(std::move(saved_bridge));
            og::ui::install_active_picker_lobby_client(saved_active);
        }
    };

    // DIRECT (LAN) JOIN: the options the SDL client hands the platform join
    // factory. The stub factory declines (nullptr), so the session stays on
    // its local client and the join reports failure.
    static bool submit_direct_join(const std::string& ip_address,
                                   const std::string& port_text,
                                   int& factory_calls, bool& direct_mode,
                                   std::string& endpoint)
    {
        ProbeRestore restore;
        factory_calls = 0;
        direct_mode = false;
        endpoint.clear();
        PlatformBridge bridge = platform_bridge();
        bridge.create_join_picker_lobby_client =
            [&](const og::ui::PickerJoinGameOptions& options)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
            ++factory_calls;
            direct_mode = options.mode == og::ui::PickerJoinMode::Direct;
            endpoint = options.direct_endpoint;
            return nullptr;
        };
        set_platform_bridge(std::move(bridge));

        SdlPickerClient client;
        client.networking_settings_.use_room_code = false;
        client.networking_settings_.ip_address = ip_address;
        client.networking_settings_.port_text = port_text;
        return client.submit_network_join();
    }

    // JOIN in relay mode with a blank room code: the active-room prompt runs
    // (answered from the TESTING prompt queue) against an offline room list
    // (no platform list hooks), and the code the client stores comes back.
    static bool join_relay_prompt(std::string& stored_room_code)
    {
        ProbeRestore restore;
        PlatformBridge bridge = platform_bridge();
        bridge.begin_list_relay_rooms = {};
        bridge.list_relay_rooms = {};
        set_platform_bridge(std::move(bridge));

        SdlPickerClient client;
        client.networking_settings_.use_room_code = true;
        client.networking_settings_.room_code.clear();
        const bool proceed = client.prompt_for_relay_room_code_if_blank();
        stored_room_code = client.networking_settings_.room_code;
        client.cancel_relay_room_list_request();
        return proceed;
    }

    // SET CAMPAIGN through the real browser: `cancel` leaves it without a
    // pick (the TESTING abort is the browser's own BACK); otherwise the
    // un-driven browser accepts the campaign under its cursor.
    static std::string show_campaign_select(bool cancel)
    {
        ProbeRestore restore;
        SdlPickerClient client;
        if (cancel)
            campaign_picker_testing_abort();
        return client.show_campaign_select();
    }
};

bool picker_testing_submit_direct_join(const std::string& ip_address,
                                       const std::string& port_text,
                                       int& factory_calls, bool& direct_mode,
                                       std::string& endpoint)
{
    return PickerSdlClientTestAccess::submit_direct_join(
        ip_address, port_text, factory_calls, direct_mode, endpoint);
}

bool picker_testing_join_relay_prompt(std::string& stored_room_code)
{
    return PickerSdlClientTestAccess::join_relay_prompt(stored_room_code);
}

std::string picker_testing_show_campaign_select(bool cancel)
{
    return PickerSdlClientTestAccess::show_campaign_select(cancel);
}

int picker_testing_exercise_sdl_client_internal_paths()
{
    return PickerSdlClientTestAccess::run();
}

// LINEUP §6 (held-click deferral): register an OWNED lobby client slot so the
// per-frame kicked revert can be driven from a test without standing up a
// live SdlPickerClient. Pass nullptr to unregister. The slot is the same one
// SdlPickerClient's constructor installs.
void picker_testing_set_lobby_client_owner(
    std::unique_ptr<og::ui::IPickerLobbyClient>* owner)
{
    g_picker_lobby_client_owner = owner;
}
