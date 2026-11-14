screen* local_transport_shadow_testing_server_screen(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr || !runtime->authoritative_mode())
        return nullptr;
    return runtime->server_screen();
}

// Test-only: the authoritative GameServer itself (nullptr when this session
// hosts none). Lets regression tests pin transport-level health — e.g. that
// a mid-game seat add never sends the display mirror into the snapshot-hash
// desync strike-out that disconnects the display peer.
og::sim::GameServer* local_transport_shadow_testing_server(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr || !runtime->authoritative_mode())
        return nullptr;
    return runtime->server.get();
}

og::sim::GameClient* local_transport_shadow_testing_display_client(
    GameSession& session) noexcept
{
    const auto runtime = session.local_transport_runtime_;
    return runtime != nullptr ? runtime->display_client() : nullptr;
}

#include "local_transport_shadow_exit_prompt.inc"

bool local_transport_shadow_testing_server_pending_exit_prompt(
    GameSession& session) noexcept
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr || runtime->server == nullptr)
        return false;
    return runtime->server->pending_exit_prompt();
}

bool local_transport_shadow_testing_set_runtime_networked(GameSession& session,
                                                          bool networked) noexcept
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr)
        return false;
    runtime->networked = networked;
    return true;
}

bool local_transport_shadow_testing_finalize_win(screen& gameplay_screen,
                                                 int next_level,
                                                 bool networked,
                                                 std::uint8_t own_player_index)
{
    const std::array<LocalSeatBinding, 1> seats = {LocalSeatBinding{
        .player_index = own_player_index,
        .team = gameplay_screen.save_data.my_team,
    }};
    return finalize_level_and_advance_cursor(
        gameplay_screen,
        next_level,
        networked,
        /*isolated_company=*/false,
        own_player_index == guy::kNoOwner
            ? std::span<const LocalSeatBinding>()
            : std::span<const LocalSeatBinding>(seats));
}

bool local_transport_shadow_testing_finalize_win(
    screen& gameplay_screen,
    int next_level,
    bool networked,
    std::span<const std::uint8_t> own_player_indices)
{
    std::vector<LocalSeatBinding> seats;
    seats.reserve(own_player_indices.size());
    for (const std::uint8_t owner : own_player_indices)
    {
        if (owner == guy::kNoOwner)
            continue;
        seats.push_back(LocalSeatBinding{
            .player_index = owner,
            .team = gameplay_screen.save_data.my_team,
        });
    }
    return finalize_level_and_advance_cursor(
        gameplay_screen,
        next_level,
        networked,
        /*isolated_company=*/false,
        std::span<const LocalSeatBinding>(seats));
}

bool local_transport_shadow_testing_finalize_withdraw(screen& gameplay_screen,
                                                      int destination_level,
                                                      bool networked)
{
    return finalize_withdraw_and_advance_cursor(
        gameplay_screen, destination_level, networked,
        /*isolated_company=*/false);
}

bool local_transport_shadow_testing_display_transition(screen& gameplay_screen,
                                                       int level_id)
{
    og::sim::InitialSetupMessage message;
    message.level_id = level_id;
    message.current_scenario = static_cast<std::int16_t>(level_id);
    message.my_team =
        static_cast<std::int16_t>(gameplay_screen.save_data.my_team);
    message.allied_mode =
        static_cast<std::int16_t>(gameplay_screen.save_data.allied_mode);
    return prepare_display_level_with_heal(gameplay_screen, message);
}
