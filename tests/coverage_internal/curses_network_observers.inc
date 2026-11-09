og::sim::LobbySaveDataEquivalent
curses_network_testing_build_join_save_equivalent(
    const og::sim::LobbyState& state)
{
    return build_join_save_equivalent_from_state(state);
}

bool curses_network_testing_inject_mode(CursesGameSession& session,
                                        short requested_respawn_ticks)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    if (host == nullptr)
        return false;
    return host->inject_mode_scenario_for_testing(requested_respawn_ticks);
}

int curses_network_testing_force_server_win(CursesGameSession& session,
                                            std::uint32_t pinned_team0_score)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    if (host == nullptr)
        return -1;
    return host->force_server_win_for_testing(pinned_team0_score);
}

int curses_network_testing_clear_server_team(CursesGameSession& session,
                                             short team)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    if (host == nullptr)
        return -1;
    return host->clear_server_team_for_testing(team);
}

// V5 Option A observers: the authoritative server world and its session save
// (host sessions only; null for a join session).
GameWorld* curses_network_testing_server_world(CursesGameSession& session)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    return host != nullptr ? &host->server_world_ref() : nullptr;
}

// Swap the host server's wall clock (steady_clock by default) so a test can
// move the pause and starvation timers without waiting.
bool curses_network_testing_set_server_clock(
    CursesGameSession& session, std::function<std::uint64_t()> source)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    if (host == nullptr)
        return false;
    host->server().set_wall_clock_ms_source(std::move(source));
    return true;
}

const SaveData* curses_network_testing_host_server_save(
    CursesGameSession& session)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    return host != nullptr ? &host->testing_server_save() : nullptr;
}

// The pack ids this lobby is currently offering joiners (protocol v10).
std::vector<std::string> curses_network_testing_hosted_pack_ids(
    CursesLobby& lobby)
{
    auto* const impl = dynamic_cast<CursesLobbyImpl*>(&lobby);
    return impl != nullptr ? impl->testing_hosted_pack_ids()
                           : std::vector<std::string>{};
}

std::unique_ptr<CursesLobby> make_host_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> combined_transport,
    std::shared_ptr<og::sim::InProcessTransport> host_client_transport,
    std::uint32_t pinned_match_seed)
{
    auto lobby = std::make_unique<CursesLobbyImpl>(LobbyRole::Host, save, difficulty);
    // A real match draws its seed from std::random_device (draw_match_seed);
    // an in-process test that then asserts on the staged world would be
    // asserting across a different level layout every run. A non-zero pin
    // makes the host's stage — and with it the joiner, which rebuilds the
    // seed from the host's lobby echo — reproducible.
    if (pinned_match_seed != 0)
        lobby->pin_match_seed_for_testing(pinned_match_seed);
    lobby->init_host_over_transport(std::move(combined_transport),
                                    std::move(host_client_transport));
    return lobby;
}

std::unique_ptr<CursesLobby> make_join_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> transport,
    og::sim::PeerId server_peer_id)
{
    auto lobby = std::make_unique<CursesLobbyImpl>(LobbyRole::Join, save, difficulty);
    lobby->init_join_over_transport(std::move(transport), server_peer_id);
    return lobby;
}
