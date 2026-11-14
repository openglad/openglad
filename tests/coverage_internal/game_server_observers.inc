void GameServer::testing_poll_transport_without_step()
{
    const std::uint32_t next_tick = world_.tick_count_ + 1u;
    poll_incoming_messages();
    process_non_input_messages(next_tick);
}

std::uint32_t GameServer::testing_last_received_input_tick(
    std::size_t player_index) const noexcept
{
    for (const auto& [peer_id, client] : clients_)
    {
        (void)peer_id;
        for (const BoundPlayer& seat : client.bound_players)
        {
            if (seat.player_index == player_index)
                return seat.last_received_input_tick;
        }
    }
    return 0u;
}
