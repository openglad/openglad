#include <openglad/interface/ui/picker_lobby_client.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/io_common.h>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern bool g_start_game_requested;

namespace {

struct LocalLobbyPeer {
    std::shared_ptr<og::sim::InProcessTransport> transport;
    og::sim::PeerId peer_id = 0;
    short team = 0;
    std::string name;
};

struct OrderedLobbySlot {
    std::uint8_t slot_index = 0;
    std::size_t player_order = 0;
    std::size_t slot_order = 0;
    const og::sim::LobbyCharacterSlot* slot = nullptr;
};

struct PreservedSaveSlot {
    std::uint8_t slot_index = 0;
    std::unique_ptr<guy> member;
};

short gameplay_start_team(short allied_mode, short requested_team) noexcept
{
    return allied_mode != 0 ? 0 : requested_team;
}

og::sim::LobbyCharacterData make_lobby_character_data(const guy& source)
{
    og::sim::LobbyCharacterData character;
    character.guy_id = source.id;
    character.name = source.name;
    character.family = static_cast<std::int8_t>(source.family);
    character.strength = source.strength;
    character.dexterity = source.dexterity;
    character.constitution = source.constitution;
    character.intelligence = source.intelligence;
    character.armor = source.armor;
    character.exp = source.exp;
    character.kills = source.kills;
    character.level_kills = source.level_kills;
    character.total_damage = source.total_damage;
    character.total_hits = source.total_hits;
    character.total_shots = source.total_shots;
    character.teamnum = source.teamnum;
    character.scen_damage = source.scen_damage;
    character.scen_kills = source.scen_kills;
    character.scen_damage_taken = source.scen_damage_taken;
    character.scen_min_hp = source.scen_min_hp;
    character.scen_shots = source.scen_shots;
    character.scen_hits = source.scen_hits;
    character.level = source.level;
    return character;
}

std::unique_ptr<guy> make_guy_from_lobby_character(
    const og::sim::LobbyCharacterData& character)
{
    auto result = std::make_unique<guy>(character.family);
    result->id = character.guy_id;
    result->name = character.name;
    result->family = static_cast<char>(character.family);
    result->strength = character.strength;
    result->dexterity = character.dexterity;
    result->constitution = character.constitution;
    result->intelligence = character.intelligence;
    result->armor = character.armor;
    result->exp = character.exp;
    result->kills = character.kills;
    result->level_kills = character.level_kills;
    result->total_damage = character.total_damage;
    result->total_hits = character.total_hits;
    result->total_shots = character.total_shots;
    result->teamnum = character.teamnum;
    result->scen_damage = character.scen_damage;
    result->scen_kills = character.scen_kills;
    result->scen_damage_taken = character.scen_damage_taken;
    result->scen_min_hp = character.scen_min_hp;
    result->scen_shots = character.scen_shots;
    result->scen_hits = character.scen_hits;
    result->level = character.level;
    // v8: the deploy flag rides the lobby SLOT, not the guy payload. This
    // copy default-deploys; the LOCAL round-trip rebuild overrides it from
    // the slot right after (a benched member must survive the state sync —
    // the local lobby echoes the machine's own roster, it is not the
    // match-assembly filter).
    result->deployed = true;
    return result;
}

std::vector<short> build_peer_team_mapping(const SaveData& save, bool spectator_mode)
{
    // The seat derivation gameplay uses (distinct nonzero teams in slot
    // order, my_team hoisted first) so the lobby's Player 1 team matches the
    // loader's first view.
    std::vector<short> teams = og::ui::derive_local_seat_teams(save);
    const int required_players = spectator_mode
        ? 0
        : std::clamp<int>(save.numplayers, 1, MAX_PLAYERS);
    const int target_count = required_players;

    if (static_cast<int>(teams.size()) > target_count)
        teams.resize(static_cast<std::size_t>(target_count));

    for (short candidate = 0;
         static_cast<int>(teams.size()) < target_count && candidate < MAX_PLAYERS;
         ++candidate)
    {
        if (std::find(teams.begin(), teams.end(), candidate) == teams.end())
            teams.push_back(candidate);
    }

    if (teams.empty())
        teams.push_back(0);

    return teams;
}

std::vector<short> collect_active_player_teams(const og::sim::LobbyState& state)
{
    std::vector<short> teams;
    teams.reserve(state.players.size());
    for (const og::sim::LobbyPlayer& player : state.players)
    {
        const short team = static_cast<short>(player.team);
        if (std::find(teams.begin(), teams.end(), team) == teams.end())
            teams.push_back(team);
    }
    return teams;
}

std::vector<PreservedSaveSlot> take_preserved_save_slots(
    SaveData& save,
    const std::vector<short>& active_teams)
{
    std::vector<PreservedSaveSlot> preserved;
    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
    {
        if (!save.team_list[slot_index])
            continue;

        const short team = save.team_list[slot_index]->teamnum;
        if (std::find(active_teams.begin(), active_teams.end(), team) != active_teams.end())
            continue;

        preserved.push_back(PreservedSaveSlot{
            .slot_index = static_cast<std::uint8_t>(slot_index),
            .member = std::move(save.team_list[slot_index]),
        });
    }
    return preserved;
}

void restore_preserved_save_slots(
    SaveData& save,
    std::vector<PreservedSaveSlot> preserved)
{
    for (PreservedSaveSlot& preserved_slot : preserved)
    {
        if (!preserved_slot.member)
            continue;

        std::size_t restore_index = preserved_slot.slot_index;
        if (restore_index >= save.team_list.size() || save.team_list[restore_index])
        {
            restore_index = save.team_list.size();
            for (std::size_t candidate = 0; candidate < save.team_list.size(); ++candidate)
            {
                if (!save.team_list[candidate])
                {
                    restore_index = candidate;
                    break;
                }
            }
        }

        if (restore_index >= save.team_list.size())
            continue;

        save.team_list[restore_index] = std::move(preserved_slot.member);
        if (save.team_size < static_cast<unsigned char>(save.team_list.size()))
            save.team_size++;
    }
}

class LocalPickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override
    {
        shutdown();

        server_transport_ = og::sim::InProcessTransport::create_server();
        server_transport_->accept_connections();
        // local_session: this in-process server echoes the SOLO picker's own
        // settings back to it; the tower (local-only) must survive the echo
        // (tower-triple §5.9 — networked servers keep the rejection backstop).
        server_ = std::make_unique<og::sim::LobbyServer>(*server_transport_,
                                                         /*local_session=*/true);
        spectator_mode_ =
            og::runtime::current_session->myscreen_->save_data.numplayers == 0;

        const SaveData& save = og::runtime::current_session->myscreen_->save_data;
        const auto peer_teams = build_peer_team_mapping(save, spectator_mode_);
        ensure_peer_count(static_cast<int>(peer_teams.size()));
        commit_from_save(true, true);
    }

    void shutdown() override
    {
        if (server_)
        {
            for (const auto& peer : peers_)
                server_->disconnect_client(peer.peer_id);
        }

        peers_.clear();
        state_.reset();
        server_.reset();
        server_transport_.reset();
        spectator_mode_ = false;
        start_request_pending_ = false;
        pending_game_start_config_.reset();
    }

    void sync_from_save() override
    {
        commit_from_save(true, true);
    }

    void sync_roster_from_save() override
    {
        commit_from_save(false, true);
    }

    void sync_settings_from_save() override
    {
        commit_from_save(true, false);
    }

    void poll_and_apply() override
    {
        if (!ensure_initialized())
            return;

        poll_messages();
        apply_state_to_save();
    }

    void set_player_mode(int player_count) override
    {
        if (!ensure_initialized())
            return;

        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        spectator_mode_ = player_count == 0;
        if (!spectator_mode_)
        {
            save.numplayers = static_cast<unsigned char>(
                std::clamp(player_count, 1, MAX_PLAYERS));
        }

        sync_roster_from_save();
    }

    bool request_start_game() override
    {
        if (!ensure_initialized() || peers_.empty())
            return false;

        pending_game_start_config_.reset();
        start_request_pending_ = true;
        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyStartGameMessage{.player_index = 0u};
        peers_.front().transport->send_lobby_message(
            peers_.front().peer_id,
            std::make_shared<og::sim::LobbyMessage>(std::move(message)));
        poll_messages();
        apply_state_to_save();
        if (g_start_game_requested)
            pending_game_start_config_ = build_game_start_config();
        return g_start_game_requested;
    }

    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        if (!server_)
            return std::nullopt;

        og::ui::PickerLobbyGameStartConfig config;
        config.save_data = server_->build_save_data_equivalent();
        // The lobby always has at least a connected host peer, even for spectator
        // starts. Preserve the picker's zero-player mode so gameplay stays in
        // spectator/autoplay instead of being coerced into a 1-player start.
        if (spectator_mode_)
            config.save_data.numplayers = 0;
        config.difficulty =
            static_cast<std::int16_t>(server_->state().settings.difficulty);
        config.my_team = gameplay_start_team(
            config.save_data.allied_mode,
            !peers_.empty() ? peers_.front().team : 0);
        // Symmetry with the networked clients: one seat per local in-process
        // peer, in seat/view order. The local (is_networked == false) start
        // path ignores these — reset_local_transport_shadow binds seats 1:1
        // by view index on its own.
        if (!spectator_mode_)
        {
            for (std::size_t index = 0; index < peers_.size(); ++index)
            {
                config.local_player_indices.push_back(
                    static_cast<std::uint8_t>(index));
                config.local_seat_teams.push_back(gameplay_start_team(
                    config.save_data.allied_mode, peers_[index].team));
            }
        }
        return config;
    }

    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        std::optional<og::ui::PickerLobbyGameStartConfig> config =
            std::move(pending_game_start_config_);
        pending_game_start_config_.reset();
        return config;
    }

    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return start_request_pending_;
    }

    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return pending_game_start_config_.has_value();
    }

    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return true;
    }

    bool request_team_change(short team) override
    {
        if (!ensure_initialized())
            return false;

        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        if (!og::ui::team_has_members(save, team))
            return false;
        if (!og::ui::set_preferred_team(save, team))
            return false;

        // The roster commit re-derives the synthetic seats; the my_team hoist
        // re-seats Player 1 onto the requested team.
        commit_from_save(false, true);
        return true;
    }

    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players() const override
    {
        if (!state_.has_value())
            return {};
        return state_->players;
    }

private:
    bool ensure_initialized()
    {
        if (server_)
            return true;
        if (!og::runtime::current_session || !og::runtime::current_session->myscreen_)
            return false;
        initialize_from_save();
        return server_ != nullptr;
    }

    void commit_from_save(bool include_settings, bool include_roster)
    {
        if (!ensure_initialized())
            return;

        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        const auto peer_teams = build_peer_team_mapping(save, spectator_mode_);
        ensure_peer_count(static_cast<int>(peer_teams.size()));

        if (include_settings)
            send_host_settings();
        if (include_roster)
            send_player_joins(peer_teams);
        poll_messages();
        apply_state_to_save();
    }

    void ensure_peer_count(int target_count)
    {
        target_count = std::clamp(target_count, 1, MAX_PLAYERS);

        while (static_cast<int>(peers_.size()) > target_count)
        {
            server_->disconnect_client(peers_.back().peer_id);
            peers_.pop_back();
        }

        while (static_cast<int>(peers_.size()) < target_count)
        {
            auto transport = server_transport_->create_client_transport();
            const og::sim::PeerId peer_id = transport->local_peer_id();
            server_->connect_client(peer_id);
            peers_.push_back(LocalLobbyPeer{
                .transport = std::move(transport),
                .peer_id = peer_id,
                .team = static_cast<short>(peers_.size()),
                .name = std::format("Player {}", peers_.size() + 1),
            });
        }
    }

    void send_host_settings()
    {
        if (peers_.empty())
            return;

        const SaveData& save = og::runtime::current_session->myscreen_->save_data;
        og::sim::LobbySettings settings;
        settings.campaign_id = save.current_campaign;
        settings.scenario_id = save.scen_num;
        settings.difficulty =
            static_cast<std::int16_t>(og::runtime::current_session->current_difficulty_);
        settings.allied_mode = save.allied_mode;
        settings.ctf_team_count = save.ctf_team_count;
        settings.ctf_capture_limit = save.ctf_capture_limit;
        settings.ctf_respawn_ticks = save.ctf_respawn_ticks;
        settings.ctf_strip_scenario_troops = save.ctf_strip_scenario_troops;
        settings.respawn_mode = save.respawn_mode;
        settings.generator_rate = save.generator_rate;
        settings.keep_fallen_heroes = save.keep_fallen_heroes;
        settings.cross_control = save.cross_control;

        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbySettingsChangeMessage{
            .player_index = 0u,
            .settings = std::move(settings),
        };
        peers_.front().transport->send_lobby_message(
            peers_.front().peer_id,
            std::make_shared<og::sim::LobbyMessage>(std::move(message)));
    }

    void send_player_joins(const std::vector<short>& peer_teams)
    {
        const SaveData& save = og::runtime::current_session->myscreen_->save_data;

        for (std::size_t peer_index = 0;
             peer_index < peers_.size() && peer_index < peer_teams.size();
             ++peer_index)
        {
            LocalLobbyPeer& peer = peers_[peer_index];
            peer.team = peer_teams[peer_index];
            if (peer.name.empty())
                peer.name = std::format("Player {}", peer_index + 1);

            og::sim::LobbyPlayer player;
            player.name = peer.name;
            // v8: every seat of one machine advertises the same active
            // company display name (SaveData::save_name).
            player.company = save.save_name;
            player.team = peer.team;
            player.ready = false;
            player.is_host = peer_index == 0;

            for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index)
            {
                const auto& member = save.team_list[slot_index];
                if (!member || member->teamnum != peer.team)
                    continue;

                player.character_slots.push_back(og::sim::LobbyCharacterSlot{
                    .slot_index = static_cast<std::uint8_t>(slot_index),
                    .character = make_lobby_character_data(*member),
                    // v8: stamped from the save guy's v14 deploy flag.
                    .deployed = member->deployed,
                });
            }

            og::sim::LobbyMessage join_message;
            join_message.payload = og::sim::LobbyJoinMessage{
                .player = std::move(player),
            };
            peer.transport->send_lobby_message(
                peer.peer_id,
                std::make_shared<og::sim::LobbyMessage>(std::move(join_message)));
        }
    }

    void poll_messages()
    {
        server_->poll_incoming_messages();
        for (auto& peer : peers_)
        {
            for (const auto& message : peer.transport->poll_typed())
            {
                switch (message.kind)
                {
                case og::sim::TypedReceivedMessageKind::LobbyState:
                    if (message.lobby_state)
                        state_ = *message.lobby_state;
                    break;
                case og::sim::TypedReceivedMessageKind::LobbyMessage:
                    if (message.lobby_message
                        && message.lobby_message->kind()
                            == og::sim::LobbyMessageKind::StartGame)
                    {
                        start_request_pending_ = false;
                        g_start_game_requested = true;
                        pending_game_start_config_ = build_game_start_config();
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

    void apply_state_to_save()
    {
        if (!state_.has_value())
            return;

        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        const std::vector<short> active_teams =
            collect_active_player_teams(*state_);
        std::vector<PreservedSaveSlot> preserved_slots =
            take_preserved_save_slots(save, active_teams);

        const std::string previous_campaign = save.current_campaign;
        const short previous_scen_num = save.scen_num;
        save.current_campaign = state_->settings.campaign_id.empty()
            ? std::string("org.openglad.gladiator")
            : state_->settings.campaign_id;
        save.scen_num = state_->settings.scenario_id > 0
            ? state_->settings.scenario_id
            : 1;

        // Mount/save coherence: whatever campaign the lobby state settles on
        // must also be the MOUNTED campaign — a cursor rewrite without a
        // remount leaves the display bootstrapping levels from one package
        // while the authoritative sim loads another (the tower ghost-session
        // shape). sync_campaign_mount_to_save no-ops when already coherent;
        // if the package cannot be mounted, refuse the override and keep the
        // save on the campaign that IS mounted.
        if (!og::ui::sync_campaign_mount_to_save(save))
        {
            // The failed mount put the previously mounted package back;
            // follow it (falling back to the save's previous pair if the
            // mount query comes back empty) so mount == save holds either way.
            const std::string mounted = get_mounted_campaign();
            LogError(
                "picker_lobby_apply_state_remount_failed campaign={} — "
                "keeping {}\n",
                save.current_campaign,
                mounted.empty() ? previous_campaign : mounted);
            save.current_campaign =
                mounted.empty() ? previous_campaign : mounted;
            save.scen_num = previous_scen_num;
        }
        save.allied_mode = state_->settings.allied_mode;
        save.ctf_team_count = state_->settings.ctf_team_count;
        save.ctf_capture_limit = state_->settings.ctf_capture_limit;
        save.ctf_respawn_ticks = state_->settings.ctf_respawn_ticks;
        save.ctf_strip_scenario_troops = state_->settings.ctf_strip_scenario_troops;
        save.respawn_mode = state_->settings.respawn_mode;
        save.generator_rate = state_->settings.generator_rate;
        save.keep_fallen_heroes = state_->settings.keep_fallen_heroes;
        save.cross_control = state_->settings.cross_control;
        save.numplayers = static_cast<unsigned char>(
            spectator_mode_
                ? 0
                : std::min<std::size_t>(
                      state_->players.size(),
                      static_cast<std::size_t>(MAX_PLAYERS)));

        std::vector<OrderedLobbySlot> ordered_slots;
        for (std::size_t player_index = 0; player_index < state_->players.size();
             ++player_index)
        {
            const og::sim::LobbyPlayer& player = state_->players[player_index];
            for (std::size_t slot_order = 0;
                 slot_order < player.character_slots.size();
                 ++slot_order)
            {
                ordered_slots.push_back(OrderedLobbySlot{
                    .slot_index = player.character_slots[slot_order].slot_index,
                    .player_order = player_index,
                    .slot_order = slot_order,
                    .slot = &player.character_slots[slot_order],
                });
            }
        }

        std::sort(ordered_slots.begin(), ordered_slots.end(),
                  [](const OrderedLobbySlot& lhs, const OrderedLobbySlot& rhs) {
                      if (lhs.slot_index != rhs.slot_index)
                          return lhs.slot_index < rhs.slot_index;
                      if (lhs.player_order != rhs.player_order)
                          return lhs.player_order < rhs.player_order;
                      return lhs.slot_order < rhs.slot_order;
                  });

        const bool slots_are_dense = std::all_of(
            ordered_slots.begin(), ordered_slots.end(),
            [&ordered_slots](const OrderedLobbySlot& slot) {
                return static_cast<std::size_t>(slot.slot_index) ==
                    static_cast<std::size_t>(&slot - ordered_slots.data());
            });

        for (auto& member : save.team_list)
            member.reset();
        save.team_size = 0;

        for (std::size_t index = 0; index < ordered_slots.size(); ++index)
        {
            std::uint8_t slot_index = ordered_slots[index].slot_index;
            if (!slots_are_dense)
                slot_index = static_cast<std::uint8_t>(index);
            if (slot_index >= save.team_list.size())
                continue;

            save.team_list[slot_index] =
                make_guy_from_lobby_character(ordered_slots[index].slot->character);
            // §4.2: preserve the slot's deploy flag through the round-trip —
            // the local lobby state carries the machine's WHOLE roster
            // (benched included) and this rebuild writes back into the real
            // save, so re-deploying here would wipe a benched selection.
            save.team_list[slot_index]->deployed =
                ordered_slots[index].slot->deployed;
            save.team_size++;
        }

        og::runtime::current_session->current_difficulty_ =
            static_cast<std::int32_t>(state_->settings.difficulty);

        restore_preserved_save_slots(save, std::move(preserved_slots));
    }

    std::shared_ptr<og::sim::InProcessTransport> server_transport_;
    std::unique_ptr<og::sim::LobbyServer> server_;
    std::vector<LocalLobbyPeer> peers_;
    std::optional<og::sim::LobbyState> state_;
    bool spectator_mode_ = false;
    bool start_request_pending_ = false;
    std::optional<og::ui::PickerLobbyGameStartConfig> pending_game_start_config_;
};

og::ui::IPickerLobbyClient* g_active_picker_lobby_client = nullptr;
std::unique_ptr<og::ui::IPickerLobbyClient> g_standalone_picker_lobby_client;

og::ui::IPickerLobbyClient& resolve_picker_lobby_client()
{
    if (g_active_picker_lobby_client)
        return *g_active_picker_lobby_client;

    if (!g_standalone_picker_lobby_client)
    {
        g_standalone_picker_lobby_client =
            og::ui::create_local_picker_lobby_client();
    }
    return *g_standalone_picker_lobby_client;
}

og::ui::IPickerLobbyClient* maybe_picker_lobby_client() noexcept
{
    if (g_active_picker_lobby_client)
        return g_active_picker_lobby_client;
    return g_standalone_picker_lobby_client.get();
}

bool active_picker_lobby_slot_editable_callback(int slot_index)
{
    if (slot_index < 0 || slot_index >= MAX_TEAM_SIZE)
        return false;
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
    {
        return client->is_save_slot_editable(
            static_cast<std::size_t>(slot_index));
    }
    return true;
}

const bool g_picker_save_slot_editable_callback_registered = [] {
    og::ui::g_picker_save_slot_editable_callback =
        &active_picker_lobby_slot_editable_callback;
    return true;
}();

} // namespace

namespace og::ui {

std::unique_ptr<IPickerLobbyClient> create_local_picker_lobby_client()
{
    return std::make_unique<LocalPickerLobbyClient>();
}

void install_active_picker_lobby_client(IPickerLobbyClient* client) noexcept
{
    g_active_picker_lobby_client = client;
}

IPickerLobbyClient* active_picker_lobby_client() noexcept
{
    return g_active_picker_lobby_client;
}

} // namespace og::ui

void picker_lobby_initialize_from_save()
{
    resolve_picker_lobby_client().initialize_from_save();
}

void picker_lobby_shutdown()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        client->shutdown();
    if (!og::ui::active_picker_lobby_client())
        g_standalone_picker_lobby_client.reset();
}

void picker_lobby_sync_from_save()
{
    resolve_picker_lobby_client().sync_from_save();
}

void picker_lobby_sync_roster_from_save()
{
    resolve_picker_lobby_client().sync_roster_from_save();
}

void picker_lobby_sync_settings_from_save()
{
    resolve_picker_lobby_client().sync_settings_from_save();
}

void picker_reinitialize_lobby_after_game()
{
    g_start_game_requested = false;
    // Networked clients reuse the live connection that survived gameplay and
    // re-sync the advanced campaign cursor; local/single-player rebuilds from
    // the save (the default resume_after_level()).
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        client->resume_after_level();
    else
        picker_lobby_initialize_from_save();
}

void picker_lobby_poll()
{
    // During active gameplay the per-level GameServer/GameClient own the shared
    // transport; the lobby's LobbyServer is kept alive but DORMANT (it now
    // survives gameplay so the next level can be coordinated). Polling it here
    // would drain the gameplay message stream out from under the GameServer, so
    // never poll the lobby while a level is running — it resumes between levels.
    if (og::runtime::current_session != nullptr &&
        og::runtime::current_session->gameplay_active_)
    {
        return;
    }
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        client->poll_and_apply();
}

void picker_lobby_set_player_mode(int player_count)
{
    resolve_picker_lobby_client().set_player_mode(player_count);
}

bool picker_lobby_request_start()
{
    return resolve_picker_lobby_client().request_start_game();
}

std::optional<og::ui::PickerLobbyGameStartConfig>
picker_lobby_consume_game_start_config()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->consume_game_start_config();
    return std::nullopt;
}

bool picker_lobby_start_request_pending()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->start_request_pending();
    return false;
}

bool picker_lobby_has_game_start_config()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->has_game_start_config();
    return false;
}

std::vector<std::string> picker_lobby_status_lines()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->status_lines();
    return {};
}

bool picker_lobby_host_controls_visible()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->host_controls_visible();
    return true;
}

bool picker_lobby_request_team_change(short team)
{
    return resolve_picker_lobby_client().request_team_change(team);
}

bool picker_lobby_set_ready(bool ready)
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->set_ready(ready);
    return false;
}

bool picker_lobby_local_ready()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->local_ready();
    return false;
}

og::sim::StartDenialReason picker_lobby_last_start_denial()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->last_start_denial();
    return og::sim::StartDenialReason::None;
}

std::vector<og::sim::LobbyPlayer> picker_lobby_players()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->lobby_players();
    return {};
}

bool picker_lobby_is_networked()
{
    if (og::ui::IPickerLobbyClient* const client = maybe_picker_lobby_client())
        return client->is_networked_session();
    return false;
}
