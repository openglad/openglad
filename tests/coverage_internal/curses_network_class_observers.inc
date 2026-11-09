namespace {

const SaveData& HostCursesSession::testing_server_save() const { return server_save_; }

bool HostCursesSession::inject_mode_scenario_for_testing(short requested_respawn_ticks)
{
    GameplayContext* const saved = current_game;
    current_game = &server_ctx_;
    GameWorld& world = server_world();
    world.type |= GameWorld::TYPE_SCRIPTED;
    // Hand-arm the mode (a mounted pack's on_mode_init would do this on
    // the first scripted tick) with a name and a scoreboard HUD line so
    // the mirror renderers have replicated text to show.
    world.mode.active = true;
    world.mode.init_attempted = true;
    std::strncpy(world.mode.name.data(), "CTF", world.mode.name.size() - 1);
    world.mode.hud[0].team = 0;
    std::strncpy(world.mode.hud[0].text.data(), "Caps 0:0",
                 world.mode.hud[0].text.size() - 1);
    if (requested_respawn_ticks > 0) {
        world.ctf_requested_respawn_ticks = requested_respawn_ticks;
        world.respawn.respawn_ticks =
            static_cast<std::uint16_t>(requested_respawn_ticks);
    }

    bool ok = true;
    const auto spawn_anchor = [&world, &ok](int team, int x, int y) {
        walker* const marker = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (marker == nullptr) {
            ok = false;
            return;
        }
        marker->setxy(static_cast<short>(x), static_cast<short>(y));
        marker->set_team_num(static_cast<unsigned char>(team));
        // The level bootstrap consumes start markers (the anchor scan
        // reads dead markers by design); a live marker acts.
        marker->set_dead(1);
    };

    const int far_x = std::max(160, static_cast<int>(world.pixmaxx) - 48);
    const int far_y = std::max(160, static_cast<int>(world.pixmaxy) - 48);
    spawn_anchor(0, 80, 48);
    spawn_anchor(0, 48, 80);
    spawn_anchor(1, far_x - 32, far_y);
    spawn_anchor(1, far_x, far_y - 32);
    og::sim::respawn_scan_anchors(world);

    client_level_->world().type |= GameWorld::TYPE_SCRIPTED;
    current_game = saved;
    return ok;
}

int HostCursesSession::force_server_win_for_testing(std::uint32_t pinned_team0_score)
{
    GameplayContext* const saved = current_game;
    current_game = &server_ctx_;
    GameWorld& world = server_world();
    world.m_score[0] = pinned_team0_score;
    for (auto& uptr : world.fxlist) {
        walker* const w = uptr.get();
        if (w != nullptr && w->query_order() == Order::Treasure &&
            w->family() == FAMILY_EXIT)
            w->set_dead(1);
    }
    const unsigned char host_team =
        static_cast<unsigned char>(world.my_team);
    int slain = 0;
    for (auto& uptr : world.oblist) {
        walker* const w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->is_friendly_to_team(host_team) == 0) {
            w->set_dead(1);
            ++slain;
        }
    }
    current_game = saved;
    return slain;
}

int HostCursesSession::clear_server_team_for_testing(short team)
{
    GameplayContext* const saved = current_game;
    current_game = &server_ctx_;
    GameWorld& world = server_world();
    int slain = 0;
    for (auto& uptr : world.oblist) {
        walker* const w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living &&
            static_cast<short>(w->team_num()) == team) {
            w->set_dead(1);
            ++slain;
        }
    }
    current_game = saved;
    return slain;
}

const SaveData& JoinCursesSession::testing_client_save() const { return client_save_; }

std::vector<std::string> CursesLobbyImpl::testing_hosted_pack_ids() const
{
    std::vector<std::string> ids;
    if (server_ == nullptr)
        return ids;
    for (const og::sim::HostedPack& pack : server_->pack_host().packs())
        ids.push_back(pack.manifest.pack_id);
    return ids;
}

} // namespace

namespace {

void CursesLobbyImpl::pin_match_seed_for_testing(std::uint32_t seed)
{ match_seed_ = seed; }

void CursesLobbyImpl::init_host_over_transport(
        std::shared_ptr<og::sim::ITransport> combined_transport,
        std::shared_ptr<og::sim::InProcessTransport> host_client_transport)
{
    combined_transport_ = std::move(combined_transport);
    host_client_transport_ = std::move(host_client_transport);
    combined_transport_->accept_connections();
    server_ = std::make_unique<og::sim::LobbyServer>(*combined_transport_);
    hosted_packs_.reset();
    sync_hosted_packs();
    wire_start_gate();

    send_lobby_message(*host_client_transport_,
                       host_client_transport_->local_peer_id(),
                       make_settings_message(save_, difficulty_));
    send_lobby_message(*host_client_transport_,
                       host_client_transport_->local_peer_id(),
                       make_join_message(save_, player_name_, local_team_));
    pump_once();
}

void CursesLobbyImpl::init_join_over_transport(std::shared_ptr<og::sim::ITransport> transport,
                                  og::sim::PeerId server_peer_id)
{
    transport_ = std::move(transport);
    server_peer_id_ = server_peer_id;
    join_sent_ = false;
    make_pack_client();
    pump_once();
}

} // namespace

namespace {

og::sim::GameServer& HostCursesSession::server()
{ return *server_; }

og::sim::GameClient& HostCursesSession::client()
{ return *client_; }

GameWorld& HostCursesSession::server_world_ref()
{ return server_world(); }

og::sim::GameClient& JoinCursesSession::client()
{ return *client_; }

const std::optional<og::sim::LobbyState>& CursesLobbyImpl::state() const
{ return state_; }

bool CursesLobbyImpl::start_negotiated() const
{ return start_negotiated_; }

} // namespace
