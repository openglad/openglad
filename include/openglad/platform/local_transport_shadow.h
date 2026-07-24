#pragma once

#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/resources/win_shares.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct InputState;
class screen;
class GameWorld;
class viewscreen;
class walker;

namespace og::runtime {

class GameSession;
struct SessionState;

// One local seat of a networked machine: the GLOBAL player_index this seat's
// view follows and the explicit GAMEPLAY team the view renders for. Assigning
// a seat never recolors fighters. Seat order == view order == InputState slot
// order.
struct LocalSeatBinding {
    std::size_t player_index = 0;
    short team = 0;
};

// §4.5 follow camera: per-view watched-target state for a networked machine
// with no controllable walker (0-deploy, all-dead, spectator). Client-side
// only — the server's whole contribution is the null seat (ControlChange
// entity 0). Lives on the local transport runtime so the per-snapshot
// control re-sync cannot stomp the player's choice, and is honored by
// select_control_for_view BEFORE the mapped-entity branch, so the mapped
// user-tag stamp never runs for a follow target ([NET-R6] no local stamps).
struct DisplayFollowState {
    bool engaged = false;
    std::uint32_t target_entity_id = 0;
};

bool local_transport_active(const SessionState& session) noexcept;
std::size_t local_transport_client_count(const GameSession& session) noexcept;
bool local_transport_shadow_is_paused(const GameSession& session) noexcept;
void reset_local_transport_shadow(GameSession& session, screen& gameplay_screen);

namespace detail {

walker* select_control_for_view(
    viewscreen* view,
    std::span<const std::uint32_t> controlled_entity_ids,
    GameWorld* world,
    std::optional<std::size_t> player_index = std::nullopt,
    const DisplayFollowState* follow = nullptr);

// §4.5 follow engagement/maintenance for one view, run by
// sync_display_controls BEFORE select_control_for_view honors the state
// (exposed for tests). Engagement: the view's seat maps to entity 0 (or it
// has no seat) AND no respawn-retained corpse holds the camera. A live
// mapped walker disengages; a dead/unresolved target auto-advances to the
// next eligible one (0 = static camera).
void update_display_view_follow(
    DisplayFollowState& follow,
    viewscreen* view,
    std::optional<std::size_t> player_index,
    std::span<const std::uint32_t> controlled_entity_ids,
    GameWorld* world);

// Networked "as if played alone" persist: merge only the characters owned by
// this machine's own seat(s), plus those seats' post-win team cash/score totals,
// from world_screen back into this peer's on-disk save0. The permadeath rule
// (keep_fallen_heroes) is read from the SESSION save carried on world_screen —
// the lobby-negotiated setting — never from the stale on-disk copy. Exposed
// here so tests can pin that contract.
void persist_owned_characters_to_save0(const screen& world_screen,
                                       std::uint8_t own_player_index);
// Multi-seat variant: ONE load/merge/save cycle covering every local seat's
// characters. kNoOwner entries are ignored; effectively empty = no-op.
void persist_owned_characters_to_save0(
    const screen& world_screen,
    std::span<const std::uint8_t> own_player_indices);
// Production network variant. Cash/score arrays are keyed by GAMEPLAY TEAM,
// not global player index, so the full bindings are required (player indices
// may exceed the four team-total slots). This compatibility/test entry point
// infers the completed level from world.id; production callers pass it
// explicitly to persist_network_win_to_save0 below.
void persist_owned_characters_to_save0(
    const screen& world_screen,
    std::span<const LocalSeatBinding> own_seats);

// Production network win persist. The private campaign cursor is advanced for
// every machine, including a zero-seat spectator. Roster and the just-finished
// completion mark are merged only when own_seats contains a valid owned player
// AND this machine deployed >= 1 character (§4.7). Team wallets are the disk
// baseline PLUS this machine's deploy-ratio share of the fold delta carried in
// `capture` (§4.6 Edit 2) — never the whole session pot. completed_level is the
// local level that actually finished; the server/session's unrelated
// completed-level history is never copied to save0.
[[nodiscard]] bool persist_network_win_to_save0(
    const screen& world_screen,
    std::span<const LocalSeatBinding> own_seats,
    int completed_level,
    const og::progression::NetWinFoldCapture& capture);

// Network withdraw persist. Load the private save0 and update only its campaign
// cursor (current_campaign/scen_num; SaveData::save updates current_levels).
// Roster, wallets, match gains, and completed-level history remain untouched.
[[nodiscard]] bool persist_private_campaign_cursor_to_save0(
    const screen& session_screen,
    int destination_level);

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
    std::vector<LocalSeatBinding> local_seats);
// Single-seat compatibility shim: one seat following local_player_index on
// the save's my_team.
void reset_network_client_transport_shadow(
    GameSession& session,
    screen& gameplay_screen,
    std::shared_ptr<og::sim::ITransport> client_transport,
    og::sim::PeerId server_peer_id,
    std::size_t local_player_index);
void clear_local_transport_shadow(GameSession& session) noexcept;
// §2.8 follow caption: per-global-player-index company display names from
// the lobby state, stamped onto the installed runtime right after a
// networked install so the follow caption can name a followed hero's owning
// machine. Unknown indices stay empty (AI targets render name-only).
// Consumption-only — nothing new rides the wire.
void local_transport_shadow_set_player_companies(
    GameSession& session,
    std::span<const std::pair<std::uint8_t, std::string>> companies);
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

// Test-only: attribute an exit/withdraw request to a given player and emit it on
// the authoritative server, as if that player had touched an exit treasure.
// Returns false (emitting nothing) if that player has no server-side control.
bool local_transport_shadow_testing_open_exit_prompt(
    GameSession& session,
    std::size_t player_index,
    int destination_level,
    bool withdraw = false);

// Test-only: whether the authoritative server still has an unanswered exit prompt
// (true means a peer was asked to confirm and the sim is frozen waiting on it).
bool local_transport_shadow_testing_server_pending_exit_prompt(
    GameSession& session) noexcept;

// Test-only: drive the shadow's internal win finalize (the FIRST finalizer in
// every local/host flow: shared win fold + rematch gate + the persist tail
// gated on the mode's persist_after_win()) and withdraw finalize (run-end
// hook + roster reload + cursor write) directly against a screen, exactly as
// the GameServer hooks do. Lets e2e tests pin the shadow's fold/run-end
// behavior without standing up the full transport runtime.
bool local_transport_shadow_testing_finalize_win(screen& gameplay_screen,
                                                 int next_level,
                                                 bool networked,
                                                 std::uint8_t own_player_index);
bool local_transport_shadow_testing_finalize_win(
    screen& gameplay_screen,
    int next_level,
    bool networked,
    std::span<const std::uint8_t> own_player_indices);
bool local_transport_shadow_testing_finalize_withdraw(screen& gameplay_screen,
                                                      int destination_level,
                                                      bool networked);

// Test-only: drive the display's level-transition load (the InitialSetup
// callback's prepare + one mode-heal retry) directly against a screen.
// Returns what the callback would act on: false = the load failed even after
// the heal, i.e. the session would end loudly instead of keeping the stale
// world.
bool local_transport_shadow_testing_display_transition(screen& gameplay_screen,
                                                       int level_id);
#endif

} // namespace og::runtime
