#pragma once

#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct InputState;
class screen;
class GameWorld;
class viewscreen;
class walker;

namespace og::runtime {

class GameSession;
struct SessionState;

bool local_transport_active(const SessionState& session) noexcept;
std::size_t local_transport_client_count(const GameSession& session) noexcept;
bool local_transport_shadow_is_paused(const GameSession& session) noexcept;
void reset_local_transport_shadow(GameSession& session, screen& gameplay_screen);

namespace detail {

walker* select_control_for_view(
    viewscreen* view,
    const std::array<std::uint32_t, MAX_PLAYERS>& controlled_entity_ids,
    GameWorld* world,
    std::optional<std::size_t> player_index = std::nullopt);

} // namespace detail

void reset_network_host_transport_shadow(
    GameSession& session,
    screen& gameplay_screen,
    std::shared_ptr<og::sim::ITransport> server_transport,
    std::shared_ptr<og::sim::InProcessTransport> local_client_transport,
    const std::vector<og::sim::LobbyPlayerBinding>& player_bindings);
void reset_network_client_transport_shadow(
    GameSession& session,
    screen& gameplay_screen,
    std::shared_ptr<og::sim::ITransport> client_transport,
    og::sim::PeerId server_peer_id,
    std::size_t local_player_index);
void clear_local_transport_shadow(GameSession& session) noexcept;
bool local_transport_shadow_toggle_pause(GameSession& session);
// Abort the current mission. Host / local play ends it authoritatively and
// returns true. A networked client instead asks the server to withdraw ALL
// players and returns false, signalling the caller to keep the display loop
// running until the server's terminal broadcast ends it (so this client does
// not just disconnect and leave its character as AI).
bool local_transport_shadow_abort_level(GameSession& session);
void local_transport_shadow_send_input(GameSession& session,
                                       const InputState& input,
                                       std::uint32_t tick);
void local_transport_shadow_finish_tick(GameSession& session);

#ifdef TESTING
// Test-only: the authoritative server's screen for a host/local session, or
// nullptr for a client-only or non-networked session. Lets tests reach into the
// authoritative world (e.g. to clear foes and force a deterministic level win).
screen* local_transport_shadow_testing_server_screen(GameSession& session);
#endif

} // namespace og::runtime
