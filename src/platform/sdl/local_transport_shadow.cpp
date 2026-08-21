#include <openglad/platform/local_transport_shadow.h>

#include <openglad/core/constants.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/view_layout.h>
#include <openglad/interface/screen.h>
#include <openglad/server/match_stage.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/web_back_key.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/progression.h>

#include <algorithm>
#include <climits>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

short load_saved_game(const char* filename, screen* scr);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
Uint32 get_time_bonus(int playernum);
void popup_dialog(const char* title, const char* message);
#ifdef TESTING
// glad_gameplay.cpp's exit-removal test hook; the staged adoption must apply
// it to the authoritative world too (the legacy display-seed keyframe used to
// carry the dead exits across).
extern bool g_test_remove_exits;
#endif

namespace
{

struct LocalTransportClient {
    std::shared_ptr<og::sim::ITransport> transport;
    og::sim::PeerId server_peer_id = 0;
    std::unique_ptr<og::sim::GameClient> game_client;
    bool drives_display = false;
    // Which InputState slots this client forwards to the server: {k} for
    // in-process splitscreen client k, {0..N-1} for a networked machine's
    // single connection carrying N local seats.
    std::vector<std::size_t> input_slots;
};

// On web the back/cancel key is Backspace (see web_back_key.h), so the pause
// overlay hint names the key that actually works there. The hint is shown
// only for a REMOTE peer's pause — the local pauser is looking at the pause
// menu itself — and points at the menu that hosts RESUME/QUIT now.
constexpr std::string_view kPauseOverlayMenuHint =
    og::input::kWebBackKeyMode ? "BKSP: Menu" : "ESC: Menu";

walker* resolve_control_from_entity_id(GameWorld& world, std::uint32_t entity_id);

} // namespace

namespace og::runtime {

struct LocalTransportRuntime {
    enum class Mode : std::uint8_t {
        Authoritative,
        ClientOnly,
    };

    Mode mode = Mode::Authoritative;
    std::unique_ptr<og::runtime::GameSession> server_session;
    std::shared_ptr<og::sim::ITransport> server_transport;
    std::unique_ptr<og::sim::GameServer> server;
    std::vector<LocalTransportClient> clients;
    std::size_t display_client_index = 0;
    bool display_session_finished = false;
    // Genuine networked multiplayer session: the live combined roster is kept
    // off each player's real save0 and each player persists only its own
    // characters' progress. False for plain local single-player / splitscreen.
    bool networked = false;
    // True for a local lobby game using a mission-isolated copy of the private
    // company. It uses the transient session seed and owner-aware merge
    // persistence without enabling network gameplay semantics.
    bool isolated_company = false;
    // This machine's local seats. Player indices filter owned characters;
    // gameplay teams select the corresponding cash/score totals.
    std::vector<LocalSeatBinding> own_seats;
    // A completed client level can surface through more than one terminal
    // message callback. Suppress later callbacks after the first successful
    // private persist; a failed first attempt remains eligible for one retry.
    bool owned_progress_persisted = false;
    // EndGame with a nonterminal destination can precede the next level's
    // InitialSetup by multiple websocket frames. Keep polling while the old
    // display world has end=1 so split delivery cannot strand the client.
    bool awaiting_level_transition = false;
    // §4.5 follow camera: per-view watched-target state (networked sessions
    // only; local shadows never engage). Runtime-owned so the per-snapshot
    // control re-sync cannot stomp the player's choice. Reset on level
    // transitions along with the rebuilt views.
    std::array<DisplayFollowState, MAX_PLAYERS> view_follow = {};
    // §2.8 follow caption: company display names keyed by GLOBAL player
    // index, stamped from the lobby state after a networked install.
    std::array<std::string, og::sim::kMaxGlobalPlayers> player_company = {};

    [[nodiscard]] screen* server_screen() const
    {
        return server_session ? server_session->myscreen_ : nullptr;
    }

    [[nodiscard]] og::sim::GameClient* display_client() const
    {
        if (display_client_index >= clients.size())
            return nullptr;
        return clients[display_client_index].game_client.get();
    }

    [[nodiscard]] bool authoritative_mode() const noexcept
    {
        return mode == Mode::Authoritative && server_session != nullptr &&
            server != nullptr && server_transport != nullptr;
    }
};

} // namespace og::runtime

namespace og::runtime::detail {

namespace {

// Respawn keep-alive (CTF match or classic respawn mode): while a player is
// dead the server nulls its control (ControlChange entity 0), but the view
// must keep following the corpse so the camera holds and the RESPAWN IN
// countdown renders. Retain the previous control when it still resolves in
// this display world (re-verified by id every sync — clients can receive
// removals) and is either a dead myguy corpse with a pending revive entry,
// or already alive wearing this player's user tag (the one-tick window
// between the mirror's revive snapshot and the server's reclaim
// ControlChange). An explicit nonzero mapped id always wins, so a player who
// switched bodies never has the old corpse steal the view back; once the
// pending entry disappears without a revive, this returns nullptr.
walker* respawn_retained_control(viewscreen* view,
                                 GameWorld* world,
                                 std::optional<std::size_t> player_index)
{
    if ((og::sim::mode_scripted_active(*world) ||
         og::sim::classic_respawn_active(*world)) &&
        view->control != nullptr &&
        world->find_by_id(view->control->entity_id()) == view->control)
    {
        walker* const previous = view->control;
        if (!previous->dead() && player_index.has_value() &&
            previous->user() == static_cast<int>(*player_index))
        {
            return previous;
        }
        if (previous->dead() && previous->myguy != nullptr &&
            og::sim::respawn_pending_player(world->respawn,
                                                previous->entity_id()))
        {
            return previous;
        }
    }
    return nullptr;
}

} // namespace

walker* select_control_for_view(
    viewscreen* view,
    std::span<const std::uint32_t> controlled_entity_ids,
    GameWorld* world,
    std::optional<std::size_t> player_index,
    const DisplayFollowState* follow)
{
    if (view == nullptr || world == nullptr)
        return nullptr;

    // §4.5 follow camera, checked BEFORE the mapped-entity branch: an
    // engaged view renders its watched target and never reaches the mapped
    // user-tag stamp below — the no-local-stamp rule ([NET-R6]) holds
    // structurally. Target upkeep (engage/disengage/auto-advance) happened
    // in update_display_view_follow before this call; an unresolved target
    // here means a static camera this frame, exactly like a seatless view.
    if (follow != nullptr && follow->engaged)
    {
        walker* const target =
            resolve_control_from_entity_id(*world, follow->target_entity_id);
        return (target != nullptr && !target->dead()) ? target : nullptr;
    }

    if (player_index.has_value() &&
        *player_index < controlled_entity_ids.size())
    {
        walker* const mapped =
            resolve_control_from_entity_id(*world, controlled_entity_ids[*player_index]);
        if (mapped != nullptr)
        {
            // On the tick a switch happens the ControlChange mapping arrives
            // ahead of the delta snapshot carrying the new control's user
            // tag, so the mirror walker still wears user == -1 for one tick
            // and the HUD (gated on a human-claimed control) blinks off.
            // The mapping IS the authority on who player_index controls;
            // stamp the tag now — the next snapshot writes the same value
            // (display-mirror-only state, never fed back to the sim). (A10)
            const signed char player_tag =
                static_cast<signed char>(static_cast<int>(*player_index));
            if (mapped->user() != player_tag)
                mapped->set_user(player_tag);
            return mapped;
        }
    }

    walker* const retained =
        respawn_retained_control(view, world, player_index);
    if (retained != nullptr)
        return retained;

    for (const std::uint32_t entity_id : controlled_entity_ids)
    {
        walker* const entity = resolve_control_from_entity_id(*world, entity_id);
        if (entity != nullptr && entity->team_num() == view->my_team)
            return entity;
    }

    return nullptr;
}

// §4.5 engagement/maintenance, run by sync_display_controls before
// select_control_for_view honors the state.
void update_display_view_follow(
    DisplayFollowState& follow,
    viewscreen* view,
    std::optional<std::size_t> player_index,
    std::span<const std::uint32_t> controlled_entity_ids,
    GameWorld* world)
{
    if (view == nullptr || world == nullptr)
        return;

    // A live mapped walker means the seat controls again: disengage.
    if (player_index.has_value() &&
        *player_index < controlled_entity_ids.size() &&
        resolve_control_from_entity_id(
            *world, controlled_entity_ids[*player_index]) != nullptr)
    {
        follow = {};
        return;
    }

    // A respawn-retained corpse keeps the camera home (the RESPAWN IN
    // countdown view); follow engages only once nothing is retained.
    if (!follow.engaged &&
        respawn_retained_control(view, world, player_index) != nullptr)
    {
        follow = {};
        return;
    }

    if (!follow.engaged)
    {
        follow.engaged = true;
        follow.target_entity_id =
            og::sim::default_follow_target_id(*world, controlled_entity_ids);
        return;
    }

    // Auto-advance a dead/unresolved target; 0 = static camera until
    // something watchable appears.
    walker* const target =
        resolve_control_from_entity_id(*world, follow.target_entity_id);
    if (target == nullptr)
    {
        follow.target_entity_id =
            og::sim::default_follow_target_id(*world, controlled_entity_ids);
    }
    else if (target->dead() && target->myguy != nullptr &&
             og::sim::respawn_pending_player(world->respawn,
                                                 target->entity_id()))
    {
        // A spectator may be following somebody else's respawning hero.
        // Preserve that explicit selection through the corpse/revive window;
        // respawn_retained_control is for the seat's OWN corpse and must not
        // clear follow state merely because view->control points at this one.
        return;
    }
    else if (target->dead())
    {
        follow.target_entity_id =
            og::sim::next_follow_target_id(*world, target, false);
    }
}

// Networked "as if played alone" persist: write only the characters owned by
// this machine's own seat(s) back into this peer's real save0, leaving every
// other slot (other players' characters, this player's not-brought characters)
// intact. world_screen is the authoritative server world on the host, or the
// snapshot mirror on a client; both carry owner tags on each myguy.
void persist_owned_characters_to_save0(const screen& world_screen,
                                       std::uint8_t own_player_index)
{
    if (own_player_index == guy::kNoOwner)
        return;

    const std::array<LocalSeatBinding, 1> seats = {LocalSeatBinding{
        .player_index = own_player_index,
        .team = world_screen.save_data.my_team,
    }};
    persist_owned_characters_to_save0(world_screen,
                                      std::span<const LocalSeatBinding>(seats));
}

void persist_owned_characters_to_save0(
    const screen& world_screen,
    std::span<const std::uint8_t> own_player_indices)
{
    std::vector<LocalSeatBinding> seats;
    seats.reserve(own_player_indices.size());
    for (const std::uint8_t owner : own_player_indices)
    {
        if (owner == guy::kNoOwner)
            continue;

        // Compatibility/test overload: recover the owner's gameplay team from
        // its tagged roster member. Production paths pass explicit bindings so
        // an empty roster can still persist its wallet.
        short team = world_screen.save_data.my_team;
        for (const auto& uptr : world_screen.world().oblist)
        {
            const walker* const entity = uptr.get();
            if (entity != nullptr && entity->myguy != nullptr &&
                entity->myguy->owner_player_index == owner)
            {
                team = static_cast<short>(entity->team_num());
                break;
            }
        }
        seats.push_back(LocalSeatBinding{
            .player_index = owner,
            .team = team,
        });
    }
    persist_owned_characters_to_save0(
        world_screen, std::span<const LocalSeatBinding>(seats));
}

void persist_owned_characters_to_save0(
    const screen& world_screen,
    std::span<const LocalSeatBinding> own_seats)
{
    const bool has_valid_owner = std::any_of(
        own_seats.begin(), own_seats.end(), [](const LocalSeatBinding& seat) {
            return seat.player_index < og::sim::kMaxGlobalPlayers &&
                seat.player_index != guy::kNoOwner;
        });
    if (!has_valid_owner)
        return;

    // Compatibility/test entry point: no fold delta is available here, so the
    // empty capture banks no money (own_deployed 0 -> no wallet change, no
    // completion). It still merges the owned roster and advances the cursor.
    // Production win persistence always threads the real capture through
    // finalize_level_and_advance_cursor / persist_client_win_progress_once.
    const og::progression::NetWinFoldCapture empty_capture;
    (void)persist_network_win_to_save0(
        world_screen, own_seats, world_screen.world().id, empty_capture);
}

bool persist_network_win_to_save0(
    const screen& world_screen,
    std::span<const LocalSeatBinding> own_seats,
    int completed_level,
    const og::progression::NetWinFoldCapture& capture)
{
    std::vector<std::uint8_t> owners;
    owners.reserve(own_seats.size());
    std::optional<std::size_t> primary_team;
    for (const LocalSeatBinding& seat : own_seats)
    {
        if (seat.player_index >= og::sim::kMaxGlobalPlayers ||
            seat.player_index == guy::kNoOwner)
        {
            continue;
        }

        owners.push_back(static_cast<std::uint8_t>(seat.player_index));
        if (seat.team < 0 || seat.team >= MAX_PLAYERS)
            continue;
        if (!primary_team.has_value())
            primary_team = static_cast<std::size_t>(seat.team);
    }

    // The SDL wrapper just resolves the active company slot + this machine's
    // seats; the roster merge, baseline+share wallet overlay, §4.7 completion
    // gate, cursor advance and LevelWin autosave live in the SDL-free shared
    // core (also used by the curses networked client).
    return og::progression::persist_networked_win(
        og::data::active_company_slot(), world_screen.save_data,
        world_screen.world(), std::span<const std::uint8_t>(owners),
        primary_team, capture, completed_level);
}

bool persist_private_campaign_cursor_to_save0(
    const screen& session_screen,
    int destination_level)
{
    if (destination_level < 0)
    {
        LogError("net_cursor_persist_invalid_destination level={}\n",
                 destination_level);
        return false;
    }

    SaveData private_save;
    const SaveDataIoError load_error =
        private_save.load_with_error(og::data::active_company_slot());
    if (load_error != SaveDataIoError::None)
    {
        LogError("net_cursor_persist_load_failed level={} error={}\n",
                 destination_level,
                 static_cast<int>(load_error));
        return false;
    }

    // Cursor-only by construction: every other field came from private save0
    // and is left unchanged. SaveData::save updates current_levels for the new
    // current_campaign/scen_num pair.
    private_save.current_campaign = session_screen.save_data.current_campaign;
    private_save.scen_num = static_cast<short>(destination_level);
    const SaveDataIoError save_error =
        private_save.save_with_error(og::data::active_company_slot());
    if (save_error != SaveDataIoError::None)
    {
        LogError("net_cursor_persist_save_failed level={} error={}\n",
                 destination_level,
                 static_cast<int>(save_error));
        return false;
    }
    return true;
}

} // namespace og::runtime::detail

namespace
{
class ScopedSessionGameplayActivation
{
public:
    explicit ScopedSessionGameplayActivation(og::runtime::SessionState& session)
        : session_(session)
        , previous_(session.gameplay_active_)
    {
        session_.gameplay_active_ = true;
    }

    ~ScopedSessionGameplayActivation()
    {
        session_.gameplay_active_ = previous_;
    }

    ScopedSessionGameplayActivation(const ScopedSessionGameplayActivation&) = delete;
    ScopedSessionGameplayActivation& operator=(
        const ScopedSessionGameplayActivation&) = delete;

private:
    og::runtime::SessionState& session_;
    bool previous_ = false;
};

std::size_t compute_local_player_count(const screen& gameplay_screen)
{
    return std::min<std::size_t>(
        static_cast<std::size_t>(std::max<short>(
            gameplay_screen.save_data.numplayers > 0
                ? gameplay_screen.save_data.numplayers
                : gameplay_screen.numviews,
            1)),
        static_cast<std::size_t>(MAX_PLAYERS));
}

void prepare_server_session_for_gameplay(og::runtime::GameSession& server_session)
{
    screen* const server_screen = server_session.myscreen_;
    if (server_screen == nullptr)
        return;

    server_screen->world().tick_count_ = 0;
    server_screen->world().reset_level_progress();
    server_screen->world().clear_removed_entity_ids();
    server_screen->world().clear_grid_dirty_tiles();
    if (current_game != nullptr && current_game->sim_events != nullptr)
        current_game->sim_events->clear();
}

// Staged adoption (#218): the session settings the legacy display-seed
// keyframe used to deliver into the authoritative world, now stamped
// explicitly. The cfg sim cadence (glad_init's
// apply_level_start_global_settings writes it on the display world) and the
// session's primary team (the classic completion scan keys on my_team; the
// wire equivalent carries none, so the staged save holds the headless 0).
// Both are replicated scalars, so the first snapshots deliver them onward
// exactly as the seed keyframe did. Under TESTING the display-side
// g_test_remove_exits hook must also reach the adopted world — the legacy
// path inherited the dead exits through the seed keyframe.
void adopt_display_session_settings(screen& server_screen,
                                    screen& gameplay_screen)
{
    server_screen.world().timer_wait = gameplay_screen.world().timer_wait;
    server_screen.world().my_team = gameplay_screen.world().my_team;
    server_screen.save_data.my_team = gameplay_screen.save_data.my_team;
    // The session save keeps THIS machine's local seat count (the staged
    // save carries the lobby-global one) — the authoritative screen's views
    // are the host machine's projection, exactly as the legacy reload
    // preserved it.
    server_screen.save_data.numplayers = gameplay_screen.save_data.numplayers;
#ifdef TESTING
    if (g_test_remove_exits)
    {
        for (auto& uptr : server_screen.world().fxlist)
        {
            walker* const w = uptr.get();
            if (w != nullptr && w->query_order() == Order::Treasure &&
                w->family() == FAMILY_EXIT)
            {
                w->set_dead(1);
            }
        }
    }
#endif
}

void apply_palette_id(screen& gameplay_screen, std::uint8_t palette_id)
{
    if (palette_id == 0u)
    {
        gameplay_screen.world().current_palette_id = 0;
        set_palette(gameplay_screen.ourpalette);
    }
    else
    {
        gameplay_screen.world().current_palette_id = 1;
        set_palette(gameplay_screen.bluepalette);
    }
    gameplay_screen.redrawme = 1;
}

void poll_local_transport_client(screen& gameplay_screen,
                                 LocalTransportClient& client,
                                 int budget)
{
    if (!client.game_client)
        return;

    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "local_transport_shadow",
            client.drives_display ? "poll_display_client"
                                  : "poll_background_client"));

    const float current_render_alpha =
        client.drives_display
            ? client.game_client->render_interpolation_alpha(
                  gameplay_screen.render_interpolation_speed_factor())
            : 1.0f;
    client.game_client->poll_messages(current_render_alpha, budget);
}

void release_world_control_claims(GameWorld& world)
{
    for (auto& uptr : world.oblist)
    {
        walker* const entity = uptr.get();
        if (entity == nullptr || entity->user() == -1)
            continue;

        entity->set_user(-1);
        entity->restore_act_type();
    }

    world.control_hp = 0.0f;
}

// No default slot on purpose (§3.4): both callers pass explicitly — the
// transient "netsession" scratch for networked play, else the active company.
bool save_shadow_save_data(screen& gameplay_screen,
                           const char* action,
                           const std::string& slot)
{
    const SaveDataIoError save_error =
        gameplay_screen.save_data.save_with_error(slot);
    if (save_error == SaveDataIoError::None)
        return true;

    LogError("local_transport_shadow_save_failed action={} slot={} error={}\n",
             action,
             slot,
             static_cast<int>(save_error));
    return false;
}

// Tally the just-finished level into the save (scores/cash/time bonus, mark it
// completed, advance scen_num to next_level, rebuild the team roster) and persist
// it — to the transient "netsession" slot for networked play (plus this peer's
// own characters merged back into save0), or to save0 directly otherwise. Does
// NOT load the next level. Returns false if the persist failed.
bool finalize_level_and_advance_cursor(
    screen& gameplay_screen,
    int next_level,
    bool networked,
    bool isolated_company,
    std::span<const og::runtime::LocalSeatBinding> own_seats)
{
    gameplay_screen.sync_save_data_from_world();

    // The shared win fold (og::progression::apply_win_fold) is the canonical
    // tally: this site donated its ordering. Time bonus is caller-computed
    // from the LIVE m_score before the fold zeroes it; the CTF rematch shape
    // (which this site historically missed) is evaluated before the cursor
    // moves.
    og::progression::WinFoldContext fold_ctx;
    for (std::size_t team_index = 0;
         team_index < fold_ctx.time_bonus.size();
         ++team_index)
    {
        fold_ctx.time_bonus[team_index] =
            get_time_bonus(static_cast<int>(team_index));
    }
    fold_ctx.rematch_shape = og::progression::mode_rematch_shape(
        gameplay_screen.world(),
        gameplay_screen.save_data,
        static_cast<short>(next_level));
    fold_ctx.finished_level = gameplay_screen.save_data.scen_num;
    fold_ctx.outcome.ending = 0;
    fold_ctx.outcome.next_level = static_cast<short>(next_level);
    fold_ctx.outcome.networked = networked;

    // §4.6: capture the deploy roster BEFORE the fold (dead heroes still ride
    // the oblist here — update_guys drops them from team_list only), then read
    // the applied per-team deltas back out for the money split.
    og::progression::NetWinFoldCapture win_capture;
    win_capture.deployed = og::progression::collect_deployed_contributors(
        gameplay_screen.world());

    og::progression::apply_win_fold(
        gameplay_screen.save_data, gameplay_screen.world(), fold_ctx);

    win_capture.cash_delta = fold_ctx.applied_cash_delta;
    win_capture.score_delta = fold_ctx.applied_score_delta;

    // Persist tail (site-owned policy), gated on the mode's win persistence.
    if (!og::mode::current_progression().persist_after_win())
        return true;

    // Mission-only rosters go to the transient slot, never the private company:
    // a network game holds the combined roster, while a local lobby game holds
    // an isolated mission copy. Merge this machine's owned characters and
    // rewards back into the untouched private-company baseline.
    if (networked || isolated_company)
    {
        if (!save_shadow_save_data(gameplay_screen, "complete_level",
                                   "netsession"))
            return false;
        return og::runtime::detail::persist_network_win_to_save0(
            gameplay_screen, own_seats, fold_ctx.finished_level, win_capture);
    }

    // §3.8: the local shadow-finalize write is a WindowEvent-kind autosave —
    // stamp + atomic write to the active company, and deliberately NO backup:
    // the display's screen::endgame LevelWin autosave owns the once-per-win
    // snapshot, so a LevelWin here would double it.
    const SaveDataIoError shadow_save_error = og::data::company_autosave(
        gameplay_screen.save_data, og::data::CompanyAutosaveKind::WindowEvent);
    if (shadow_save_error != SaveDataIoError::None)
    {
        LogError("local_transport_shadow_save_failed action={} slot={} error={}\n",
                 "complete_level",
                 og::data::active_company_slot(),
                 static_cast<int>(shadow_save_error));
        return false;
    }
    return true;
}

// Withdraw (retreat) discards the current level's gains: reload the roster from
// the session store (the transient "netsession" slot for networked play — the
// combined roster — else save0) and point the cursor at destination_level, then
// persist. Unlike a win this awards no money/time bonus and does NOT mark the
// level completed. Does NOT load the destination level. Returns false on I/O
// failure.
bool finalize_withdraw_and_advance_cursor(screen& gameplay_screen,
                                          int destination_level,
                                          bool networked,
                                          bool isolated_company)
{
    // Withdraw/quit-mission is a run-ending, non-win exit: route the mode's
    // run-end policy (Classic: no-op) before the roster reload discards the
    // level's state. Idempotent with the display-side screen::endgame hook.
    {
        og::mode::LevelOutcome run_outcome;
        run_outcome.ending = 1;
        run_outcome.next_level = static_cast<short>(destination_level);
        run_outcome.networked = networked;
        run_outcome.withdrawn = true;
        og::mode::current_progression().on_run_ended(
            gameplay_screen.save_data, gameplay_screen.world(), run_outcome);
    }

    const std::string slot = networked || isolated_company
        ? std::string("netsession")
        : og::data::active_company_slot();
    const SaveDataIoError load_error =
        gameplay_screen.save_data.load_with_error(slot);
    if (load_error != SaveDataIoError::None)
    {
        LogError("local_transport_shadow_withdraw_load_failed level={} slot={} error={}\n",
                 destination_level,
                 slot,
                 static_cast<int>(load_error));
        return false;
    }

    // Go to the exit's level
    gameplay_screen.save_data.scen_num = static_cast<short>(destination_level);
    // Autosave because we escaped to a new level
    // Save with the new current level
    if (!save_shadow_save_data(gameplay_screen, "withdraw", slot))
        return false;

    if (!networked && !isolated_company)
        return true;

    // The transient write above restores the mission's pre-level roster.
    // Independently advance the private company's cursor so the post-game menu
    // does not re-advertise its stale pre-round level. This is deliberately
    // ownership-agnostic (spectator hosts have zero seats) and cursor-only
    // (withdraw awards no roster, cash, score, or completion gain).
    return og::runtime::detail::persist_private_campaign_cursor_to_save0(
        gameplay_screen, destination_level);
}

void apply_initial_setup_to_client_save(
    screen& gameplay_screen,
    const og::sim::InitialSetupMessage& message)
{
    gameplay_screen.save_data.scen_num = static_cast<short>(message.level_id);
    gameplay_screen.save_data.my_team = static_cast<short>(message.my_team);
    gameplay_screen.save_data.allied_mode =
        static_cast<short>(message.allied_mode);

    std::set<int>& completed =
        gameplay_screen.save_data
            .completed_levels[gameplay_screen.save_data.current_campaign];
    completed.clear();
    for (const std::int32_t level_id : message.completed_levels)
        completed.insert(level_id);
}

void sync_single_display_team_from_save(screen& gameplay_screen)
{
    if (gameplay_screen.numviews != 1 || gameplay_screen.viewob[0] == nullptr)
        return;

    gameplay_screen.viewob[0]->my_team = gameplay_screen.save_data.my_team;
}

bool prepare_display_level_for_initial_setup(
    screen& gameplay_screen,
    const og::sim::InitialSetupMessage& message)
{
    gameplay_screen.cleanup(gameplay_screen.numviews);
    gameplay_screen.initialize_views();
    apply_initial_setup_to_client_save(gameplay_screen, message);
    gameplay_screen.sync_world_from_save_data();
    gameplay_screen.world().id = static_cast<short>(message.level_id);
    if (!gameplay_screen.load_level())
    {
        LogError("local_transport_shadow_client_level_load_failed level={}\n",
                 message.level_id);
        return false;
    }

    gameplay_screen.sync_world_from_save_data();
    gameplay_screen.world().tick_count_ = 0;
    gameplay_screen.world().reset_level_progress();
    gameplay_screen.world().clear_removed_entity_ids();
    gameplay_screen.world().clear_grid_dirty_tiles();
    // A level transition means the session CONTINUES into the next level. The
    // display ran results for the just-completed level (screen::endgame on the
    // forwarded EndGame event), which set world.end=1 / game-over flags. Loading
    // the next level does not otherwise clear them, so without this reset
    // finish_tick would see world().end != 0 and tear the session down — the
    // freshly loaded level would freeze ("in a level but no one can move").
    gameplay_screen.world().end = 0;
    gameplay_screen.world().game_ended = false;
    gameplay_screen.world().ending = 0;
    gameplay_screen.world().retry = false;
    gameplay_screen.world().next_level = -1;
    gameplay_screen.world().level_done = 0;
    sync_single_display_team_from_save(gameplay_screen);
    gameplay_screen.redrawme = 1;
    return true;
}

// Level-transition load with ONE heal attempt: the mounted mode may be able
// to regenerate the level's files (tower floors are derived from (seed, N) in
// the save), so ask it to make the level available and retry once. Returns
// false only when the retry also fails — the caller must then END the display
// session loudly instead of silently keeping the stale world (the previous
// level would keep rendering while the authoritative sim runs the next one:
// an unrecoverable ghost session).
bool prepare_display_level_with_heal(
    screen& gameplay_screen,
    const og::sim::InitialSetupMessage& message)
{
    if (prepare_display_level_for_initial_setup(gameplay_screen, message))
        return true;

    og::mode::current_progression().ensure_level_available(
        gameplay_screen.save_data);
    if (prepare_display_level_for_initial_setup(gameplay_screen, message))
    {
        TRACE("net", "display_transition_healed level=%d",
              static_cast<int>(message.level_id));
        return true;
    }

    LogError(
        "local_transport_shadow_display_transition_failed level={} — "
        "ending the session instead of keeping the stale world\n",
        message.level_id);
    TRACE("net", "display_transition_failed level=%d",
          static_cast<int>(message.level_id));
    return false;
}

walker* resolve_control_from_entity_id(GameWorld& world, std::uint32_t entity_id)
{
    return entity_id != 0u ? world.find_by_id(entity_id) : nullptr;
}

void dispatch_display_event_batch(screen& gameplay_screen,
                                  const og::sim::SimEventBatch& batch)
{
    gameplay_screen.dispatch_sim_event_batch(batch);
}

bool batch_ends_display_session(const og::sim::SimEventBatch& batch)
{
    return std::any_of(
        batch.events.begin(),
        batch.events.end(),
        [](const og::sim::Event& event) {
            if (event.kind == og::sim::EventKind::SetEnd)
                return true;
            // An EndGame event carries the next level in b. A nonnegative
            // destination normally means an in-session transition; a
            // return-to-lobby batch carries SetEnd as its explicit terminal
            // marker because it also needs that destination for persistence.
            // A negative destination is terminal on its own.
            if (event.kind == og::sim::EventKind::EndGame)
                return static_cast<std::int32_t>(event.b) < 0;
            return false;
        });
}

bool batch_contains_endgame(const og::sim::SimEventBatch& batch)
{
    return std::any_of(
        batch.events.begin(), batch.events.end(),
        [](const og::sim::Event& event) {
            return event.kind == og::sim::EventKind::EndGame;
        });
}

og::sim::SimEventBatch without_redundant_set_end(
    const og::sim::SimEventBatch& batch)
{
    if (!batch_contains_endgame(batch))
        return batch;

    // SetEnd alongside EndGame is a transport-level terminal marker. Let
    // screen::endgame run first so it can fold rewards/cursor; setting end
    // before EndGame would make screen::endgame return without doing so.
    og::sim::SimEventBatch result = batch;
    std::erase_if(result.events, [](const og::sim::Event& event) {
        return event.kind == og::sim::EventKind::SetEnd;
    });
    return result;
}

// True only when this batch carries a WON level end (EndGame with ending==0 —
// a completed/exited level). A withdraw, abort, or defeat carries ending==1.
// The per-player save0 merge only happens on a win, so deaths persist on a win
// while a withdraw/abort reverts the roster to its pre-level state.
bool batch_is_level_won(const og::sim::SimEventBatch& batch)
{
    for (const auto& event : batch.events)
    {
        if (event.kind == og::sim::EventKind::EndGame)
            return static_cast<std::int32_t>(event.a) == 0;
    }
    return false;
}

std::optional<int> batch_withdraw_destination(
    const og::sim::SimEventBatch& batch)
{
    for (const auto& event : batch.events)
    {
        if (event.kind != og::sim::EventKind::EndGame ||
            static_cast<std::int32_t>(event.a) != 1)
        {
            continue;
        }

        const std::int32_t destination =
            static_cast<std::int32_t>(event.b);
        if (destination >= 0)
            return static_cast<int>(destination);
    }
    return std::nullopt;
}

void respond_to_exit_prompt(screen& gameplay_screen,
                            og::sim::GameClient& game_client,
                            const og::sim::ExitPromptBroadcastMessage& prompt)
{
    const bool accepted =
        yes_or_no_prompt("Exit Field", prompt.prompt_text.c_str(), false);
    gameplay_screen.redrawme = 1;
    game_client.send_exit_prompt_response(accepted);
}

std::string pause_overlay_text(const og::sim::PauseBroadcastMessage* pause)
{
    if (pause == nullptr || pause->player_name.empty())
        return "PAUSED";

    return "PAUSED by " + pause->player_name;
}

// True when the pause owner's seat does NOT belong to this machine. Local
// (non-networked) shadows own every seat, so their pause is always local; a
// networked machine compares against its own_seats.
bool pause_owner_is_remote(const og::runtime::LocalTransportRuntime& runtime,
                           std::size_t owner)
{
    if (!runtime.networked)
        return false;
    return std::none_of(
        runtime.own_seats.begin(), runtime.own_seats.end(),
        [owner](const og::runtime::LocalSeatBinding& seat) {
            return seat.player_index == owner;
        });
}

bool pause_owned_by_remote_peer(
    const og::runtime::LocalTransportRuntime& runtime,
    const og::sim::GameClient& game_client)
{
    if (!game_client.last_pause_broadcast().has_value())
        return false;
    return pause_owner_is_remote(
        runtime, game_client.last_pause_broadcast()->player_index);
}

void refresh_pause_overlay_text(viewscreen& view,
                                const std::string& text,
                                bool include_menu_hint)
{
    view.refresh_display_text(text, 1);
    if (include_menu_hint)
        view.refresh_display_text(kPauseOverlayMenuHint, 1);
}

void render_pause_overlay(screen& gameplay_screen,
                          const og::sim::GameClient& game_client,
                          bool remote_pause)
{
    if (!game_client.baseline().has_value() ||
        !game_client.baseline()->paused)
    {
        return;
    }

    const std::string text =
        pause_overlay_text(game_client.last_pause_broadcast().has_value()
                               ? &*game_client.last_pause_broadcast()
                               : nullptr);
    for (int index = 0; index < gameplay_screen.numviews; ++index)
    {
        viewscreen* const view = gameplay_screen.viewob[index].get();
        if (view == nullptr)
            continue;

        refresh_pause_overlay_text(*view, text, remote_pause);
    }
    gameplay_screen.redrawme = 1;
}

void configure_background_game_client(screen& gameplay_screen,
                                      og::sim::GameClient& game_client)
{
    game_client.set_initial_setup_callback(
        [&game_client](
            const og::sim::InitialSetupMessage&,
            bool is_level_transition) {
            if (is_level_transition)
                game_client.send_client_ready();
        });
    // Local split-screen: players 2..N are background clients that share the one
    // display. The exit/withdraw prompt is sent only to the player who triggered
    // it, so a non-display player MUST still be able to answer it on the shared
    // screen — otherwise the server stays frozen on the pending prompt and the
    // game hangs. (For networked host/client this same client is reconfigured by
    // configure_display_game_client right after, which installs the equivalent
    // callback, so setting it here first is harmless.)
    game_client.set_exit_prompt_callback(
        [&gameplay_screen, client_ptr = &game_client](
            const og::sim::ExitPromptBroadcastMessage& prompt) {
            respond_to_exit_prompt(gameplay_screen, *client_ptr, prompt);
        });
}

walker* find_control_for_view(viewscreen* view,
                              std::optional<std::size_t> player_index,
                              std::span<const std::uint32_t>
                                  controlled_entity_ids,
                              GameWorld* world,
                              const og::runtime::DisplayFollowState* follow =
                                  nullptr)
{
    return og::runtime::detail::select_control_for_view(
        view, controlled_entity_ids, world, player_index, follow);
}

// §2.8 follow caption: the company of the watched walker's owning machine,
// resolved through its roster owner tag ("" for AI targets / unknown).
std::string follow_company_for_control(
    const og::runtime::LocalTransportRuntime* runtime, const walker* control)
{
    if (runtime == nullptr || control == nullptr || control->myguy == nullptr)
        return {};
    const std::uint8_t owner = control->myguy->owner_player_index;
    if (owner >= runtime->player_company.size())
        return {};
    return runtime->player_company[owner];
}

// View i follows seats[i] (a networked machine's local seat list). An empty
// NETWORK seat list is a real zero-seat spectator and therefore has no player
// binding at all. Only the legacy local-shadow path treats an empty list as
// split-screen, where the in-process peers are bound 1:1 in view order.
void sync_display_controls(
    screen& gameplay_screen,
    std::span<const std::uint32_t> controlled_entity_ids,
    GameWorld* world,
    const std::vector<og::runtime::LocalSeatBinding>& display_seats,
    og::runtime::LocalTransportRuntime* runtime = nullptr)
{
    if (world == nullptr)
        return;

    const std::size_t player_count = compute_local_player_count(gameplay_screen);
    for (std::size_t index = 0; index < player_count; ++index)
    {
        viewscreen* const view = gameplay_screen.viewob[index].get();
        if (view == nullptr)
            continue;

        std::optional<std::size_t> player_index;
        if (!display_seats.empty())
        {
            player_index = index < display_seats.size()
                ? std::optional<std::size_t>(display_seats[index].player_index)
                : std::nullopt;
        }
        else if (runtime == nullptr || !runtime->networked)
        {
            player_index = index;
        }
        view->global_player_index_ = player_index.has_value()
            ? static_cast<short>(*player_index)
            : static_cast<short>(-1);

        // §4.5: only genuine networked sessions engage the follow camera —
        // local shadows (splitscreen, demo spectator) keep today's paths.
        og::runtime::DisplayFollowState* follow = nullptr;
        if (runtime != nullptr && runtime->networked &&
            index < runtime->view_follow.size())
        {
            follow = &runtime->view_follow[index];
            og::runtime::detail::update_display_view_follow(
                *follow, view, player_index, controlled_entity_ids, world);
        }

        view->control =
            find_control_for_view(
                view, player_index, controlled_entity_ids, world, follow);
        view->following_ = follow != nullptr && follow->engaged;
        view->follow_company_ = view->following_
            ? follow_company_for_control(runtime, view->control)
            : std::string();
    }
}

// Multi-seat machines stamp each view's team from its seat binding (the
// roster-order guess from load_saved_game is wrong for a joiner whose seats
// landed on other teams). Single-seat flows keep today's my_team handling
// (sync_single_display_team_from_save / load_saved_game) untouched.
void stamp_display_seat_teams(
    screen& gameplay_screen,
    const std::vector<og::runtime::LocalSeatBinding>& display_seats)
{
    if (display_seats.size() < 2)
        return;

    const std::size_t view_count = std::min<std::size_t>(
        static_cast<std::size_t>(gameplay_screen.numviews),
        display_seats.size());
    for (std::size_t index = 0; index < view_count; ++index)
    {
        viewscreen* const view = gameplay_screen.viewob[index].get();
        if (view != nullptr)
            view->my_team = display_seats[index].team;
    }
}

void persist_client_win_progress_once(
    og::runtime::LocalTransportRuntime& runtime,
    screen& gameplay_screen,
    std::span<const og::runtime::LocalSeatBinding> display_seats)
{
    if (runtime.owned_progress_persisted)
        return;

    const std::span<const og::runtime::LocalSeatBinding> owned_seats =
        og::ui::is_spectator_mode(gameplay_screen.save_data)
        ? std::span<const og::runtime::LocalSeatBinding>()
        : display_seats;
    // §4.6: the client's screen::endgame folds inside event dispatch and
    // latches the win capture (deploy roster + applied deltas) onto the screen;
    // this persist, which runs afterwards, reads that latch to size the machine
    // share. A missing latch (defensive) falls back to no money.
    static const og::progression::NetWinFoldCapture kEmptyCapture;
    const og::progression::NetWinFoldCapture& capture =
        gameplay_screen.pending_net_win_capture_.has_value()
            ? *gameplay_screen.pending_net_win_capture_
            : kEmptyCapture;
    // Latch only a successful persist. A win can surface through both the sim
    // and game-flow callbacks; if the first disk attempt fails, the alternate
    // callback gets one safe retry instead of being suppressed by the guard.
    runtime.owned_progress_persisted =
        og::runtime::detail::persist_network_win_to_save0(
            gameplay_screen, owned_seats, gameplay_screen.world().id, capture);
}

void configure_display_game_client(
    og::runtime::LocalTransportRuntime& runtime,
    screen& gameplay_screen,
    og::sim::GameClient& display_client,
    std::vector<og::runtime::LocalSeatBinding> display_seats)
{
    gameplay_screen.set_render_interpolation_client(&display_client);
    display_client.set_initial_setup_callback(
        [&gameplay_screen, runtime_ptr = &runtime, display_seats,
         display_client_ptr = &display_client](
            const og::sim::InitialSetupMessage& message,
            bool is_level_transition) {
            // Terminal EndGame handling owns old-level persistence before an
            // in-session InitialSetup arrives. Persisting unconditionally here
            // used to save abandoned withdraw/abort gains, and an accepted
            // exit could arrive before the client's win fold. The server now
            // sends a forced final keyframe + EndGame before every transition.

            apply_initial_setup_to_client_save(gameplay_screen, message);

            if (!is_level_transition)
            {
                sync_single_display_team_from_save(gameplay_screen);
                stamp_display_seat_teams(gameplay_screen, display_seats);
                gameplay_screen.redrawme = 1;
                return;
            }

            if (!prepare_display_level_with_heal(gameplay_screen, message))
            {
                // Fail LOUDLY: a swallowed transition failure used to keep
                // the stale world on screen while the authoritative sim
                // moved on — surface it and end the display session.
                popup_dialog("Level Load Failed",
                             "Could not load the next level.");
                gameplay_screen.redrawme = 1;
                if (runtime_ptr != nullptr)
                {
                    runtime_ptr->awaiting_level_transition = false;
                    runtime_ptr->display_session_finished = true;
                }
                return;
            }

            // The transition rebuilt the views; restore multi-seat view teams.
            stamp_display_seat_teams(gameplay_screen, display_seats);
            if (runtime_ptr != nullptr)
            {
                // This GameClient/runtime survives dedicated-server in-session
                // transitions. The old level is fully persisted and wiped at
                // this point, so arm the once-guard for the new level and
                // clear the transient EndGame latch set before InitialSetup.
                runtime_ptr->owned_progress_persisted = false;
                runtime_ptr->awaiting_level_transition = false;
                runtime_ptr->display_session_finished = false;
                // Follow targets belong to the wiped level (§4.5); the next
                // control re-sync re-engages against the new world.
                runtime_ptr->view_follow = {};
            }
            display_client_ptr->send_client_ready();
        });
    display_client.set_control_mapping_callback(
        [&gameplay_screen, display_seats, runtime_ptr = &runtime](
            const og::sim::ControlledEntityIds& controlled_entity_ids,
            GameWorld* world) {
            sync_display_controls(
                gameplay_screen,
                controlled_entity_ids,
                world,
                display_seats,
                runtime_ptr);
        });
    display_client.set_sim_event_batch_callback(
        [&gameplay_screen, runtime_ptr = &runtime, display_seats](
            const og::sim::SimEventBatch& batch) {
            const bool was_live = gameplay_screen.world().end == 0;
            dispatch_display_event_batch(gameplay_screen, batch);
            // A won level can end HERE instead of in the game-flow callback:
            // the win broadcast sends snapshot (game_ended=true) + sim batch
            // (palette/redraw) + the EndGame game-flow batch, in that order.
            // Dispatching the SIM batch already trips the mirror's
            // "game_ended && !end" -> endgame() path. Persist the WON outcome
            // here so a terminal processing break cannot skip it; ending==0 is
            // a completed level, while withdraw/abort/defeat is not. The
            // once-guard makes alternate callback shapes mutually exclusive.
            if (was_live && gameplay_screen.world().end != 0 &&
                runtime_ptr != nullptr)
            {
                if (runtime_ptr->mode ==
                        og::runtime::LocalTransportRuntime::Mode::ClientOnly &&
                    gameplay_screen.world().game_ended &&
                    gameplay_screen.world().ending == 0 &&
                    !gameplay_screen.world().retry)
                {
                    og::sim::classic_respawn_flush_pending(
                        gameplay_screen.world());
                    persist_client_win_progress_once(
                        *runtime_ptr, gameplay_screen, display_seats);
                }
                const bool expects_transition =
                    gameplay_screen.world().next_level >= 0 &&
                    !gameplay_screen.world().retry;
                runtime_ptr->awaiting_level_transition = expects_transition;
                runtime_ptr->display_session_finished = !expects_transition;
            }
        });
    display_client.set_game_flow_event_batch_callback(
        [&gameplay_screen, runtime_ptr = &runtime, display_seats](
            const og::sim::SimEventBatch& batch) {
            // A won level can end while heroes sit in the classic respawn
            // queue (the synchronous exit-accept path fires the EndGame
            // before any post-flush snapshot reaches this mirror). Revive
            // them on the mirror BEFORE any display-side roster persist —
            // screen::endgame's solo save0 autosave inside the dispatch, and
            // the client save0 merge below. Idempotent: on in-tick end
            // shapes the flushed queue already arrived empty.
            const bool won_level = batch_is_level_won(batch);
            const std::optional<int> withdraw_destination =
                batch_withdraw_destination(batch);
            const bool ends_session = batch_ends_display_session(batch);
            const bool expects_transition =
                batch_contains_endgame(batch) && !ends_session;
            if (won_level)
                og::sim::classic_respawn_flush_pending(gameplay_screen.world());
            if (runtime_ptr != nullptr && withdraw_destination.has_value() &&
                runtime_ptr->mode ==
                    og::runtime::LocalTransportRuntime::Mode::ClientOnly)
            {
                // The authoritative EndGame confirms the withdraw and carries
                // its destination. Every client, including a spectator, moves
                // only its private campaign cursor; abandoned level state and
                // the host's roster/wallet/history never enter save0.
                (void)og::runtime::detail::
                    persist_private_campaign_cursor_to_save0(
                        gameplay_screen, *withdraw_destination);
            }
            const og::sim::SimEventBatch display_batch =
                without_redundant_set_end(batch);
            dispatch_display_event_batch(gameplay_screen, display_batch);
            if (runtime_ptr != nullptr &&
                (gameplay_screen.world().end != 0 ||
                 ends_session))
            {
                // Persist this client's own seats' characters back to save0
                // only on a WON level (the host persists server-side). A
                // withdraw / abort / defeat (ending != 0) must NOT persist —
                // the roster reverts to its pre-level state, so a character
                // that died during a level we later abandon is not lost.
                if (runtime_ptr->mode ==
                        og::runtime::LocalTransportRuntime::Mode::ClientOnly &&
                    won_level && !gameplay_screen.world().retry)
                {
                    persist_client_win_progress_once(
                        *runtime_ptr, gameplay_screen, display_seats);
                }
                runtime_ptr->awaiting_level_transition = expects_transition;
                runtime_ptr->display_session_finished = !expects_transition;
            }
        });
    display_client.set_message_processing_break_callback(
        [&gameplay_screen, runtime_ptr = &runtime]() {
            return gameplay_screen.world().end != 0 &&
                (runtime_ptr == nullptr ||
                 !runtime_ptr->awaiting_level_transition);
        });
    display_client.set_exit_prompt_callback(
        [&gameplay_screen, display_client_ptr = &display_client](
            const og::sim::ExitPromptBroadcastMessage& prompt) {
            respond_to_exit_prompt(
                gameplay_screen, *display_client_ptr, prompt);
        });
    display_client.set_pause_broadcast_callback(
        [&gameplay_screen,
         runtime_ptr = &runtime](const og::sim::PauseBroadcastMessage& pause) {
            const std::string text = pause_overlay_text(&pause);
            const bool remote_pause =
                runtime_ptr != nullptr &&
                pause_owner_is_remote(*runtime_ptr, pause.player_index);
            for (int view_index = 0; view_index < gameplay_screen.numviews;
                 ++view_index)
            {
                viewscreen* const view =
                    gameplay_screen.viewob[view_index].get();
                if (view == nullptr)
                    continue;
                refresh_pause_overlay_text(*view, text, remote_pause);
            }
            gameplay_screen.redrawme = 1;
        });
    display_client.set_palette_sync_callback(
        [&gameplay_screen](std::uint8_t palette_id) {
            apply_palette_id(gameplay_screen, palette_id);
        });
    display_client.set_connection_lost_callback(
        [&gameplay_screen, runtime_ptr = &runtime]() {
            popup_dialog("Connection Lost",
                         "Lost connection to the server.");
            gameplay_screen.redrawme = 1;
            if (runtime_ptr != nullptr)
            {
                runtime_ptr->awaiting_level_transition = false;
                runtime_ptr->display_session_finished = true;
            }
        });
}

// --- Mid-game local seat add/remove helpers (design §5) ---------------------

// Common gate: a plain local authoritative shadow, mid-level, with a display
// screen. Networked sessions, spectator autoplay, and replay playback are out
// of scope by design (§5 is "non-networked sessions only").
bool local_seat_mutation_allowed(
    const og::runtime::LocalTransportRuntime* runtime,
    const og::runtime::SessionState& session)
{
    if (runtime == nullptr || !runtime->authoritative_mode())
        return false;
    if (runtime->networked || session.networked_session_)
        return false;
    if (runtime->display_session_finished || session.replay_playback_active_)
        return false;
    if (session.myscreen_ == nullptr || session.myscreen_->world().end != 0)
        return false;
    if (og::ui::is_spectator_mode(session.myscreen_->save_data))
        return false;
    // Structural sanity: the local install binds one in-process peer per seat
    // (peer k <-> seat k <-> view k); a mismatch means this runtime is not a
    // plain local shadow and seat surgery would corrupt it.
    return runtime->clients.size() ==
        compute_local_player_count(*session.myscreen_);
}

// Average level of the live walkers on `team`, clamped >= 1 — the stock
// joiner's power match.
int average_team_level(GameWorld& world, short team)
{
    int level_sum = 0;
    int level_count = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* const entity = uptr.get();
        if (entity == nullptr || entity->dead() ||
            entity->query_order() != Order::Living ||
            static_cast<short>(entity->team_num()) != team ||
            entity->stats() == nullptr)
        {
            continue;
        }
        level_sum += static_cast<int>(entity->stats()->level());
        ++level_count;
    }
    return level_count > 0 ? std::max(level_sum / level_count, 1) : 1;
}

// Deterministic placement for a mid-level stock spawn: the team's respawn
// anchors first (populated by scripted/classic-respawn rounds), then an
// expanding ring around a live teammate (player-controlled preferred).
// Level-entry start markers are consumed and destroyed at load, and nothing
// here may draw world.rng_ — a fully blocked neighborhood fails the add
// instead of teleporting (the respawn engine's standing rule).
bool place_stock_seat_walker(GameWorld& world, walker* w, short team)
{
    if (team >= 0 &&
        static_cast<std::size_t>(team) < std::size(world.respawn.anchor_count))
    {
        for (std::uint8_t i = 0; i < world.respawn.anchor_count[team]; ++i)
        {
            const short x = world.respawn.anchor_x[team][i];
            const short y = world.respawn.anchor_y[team][i];
            if (!og::sim::respawn_spot_clear(world, w, x, y, /*floor=*/0))
                continue;
            w->set_floor(0); // set_floor BEFORE setxy: the obmap is floor-keyed
            w->setxy(x, y);
            return true;
        }
    }

    walker* anchor = nullptr;
    for (const auto& uptr : world.oblist)
    {
        walker* const entity = uptr.get();
        if (entity == nullptr || entity == w || entity->dead() ||
            entity->query_order() != Order::Living ||
            static_cast<short>(entity->team_num()) != team)
        {
            continue;
        }
        if (anchor == nullptr || entity->user() != -1)
            anchor = entity;
        if (entity->user() != -1)
            break; // a player-held walker is the preferred ring center
    }
    if (anchor == nullptr)
        return false;

    static constexpr short kRing[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
    };
    for (short radius = 1; radius <= 3; ++radius)
    {
        for (const auto& dir : kRing)
        {
            const short x = static_cast<short>(
                anchor->xpos() + dir[0] * radius * GRID_SIZE);
            const short y = static_cast<short>(
                anchor->ypos() + dir[1] * radius * GRID_SIZE);
            if (!og::sim::respawn_spot_clear(world, w, x, y, anchor->floor()))
                continue;
            w->set_floor(anchor->floor());
            w->setxy(x, y);
            return true;
        }
    }
    return false;
}

// Resolve the joining seat's walker on the authoritative world: the existing
// claim scan first; else a stock NON-ROSTER soldier — no myguy, because
// SaveData::update_guys rebuilds team_list from the oblist at level end and a
// myguy here would recruit the joiner into the company permanently —
// power-matched to the team's average level.
walker* resolve_or_spawn_seat_walker(GameWorld& world,
                                     short team,
                                     short player_index,
                                     bool& spawned_out)
{
    spawned_out = false;
    walker* const claimed =
        og::sim::sim_find_next_control_owned(world, team, player_index);
    if (claimed != nullptr)
        return claimed;

    // Power-match BEFORE add_ob so the fresh walker's default level cannot
    // drag its own average down.
    const int level = average_team_level(world, team);
    walker* const stock = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (stock == nullptr)
        return nullptr;
    stock->set_team_num(static_cast<unsigned char>(team));
    stock->set_real_team_num(255);
    if (stock->stats() != nullptr)
        stock->stats()->set_level(level);
    if (!place_stock_seat_walker(world, stock, team))
    {
        world.remove_ob(stock);
        return nullptr;
    }
    // setxy routes obmap updates through obmap::move, which early-outs on an
    // unchanged position — re-register explicitly (the respawn engine's
    // ensure_obmap_registration rule).
    if (world.myobmap != nullptr && !stock->ignore() && !stock->dead() &&
        world.myobmap->walker_to_pos.find(stock) ==
            world.myobmap->walker_to_pos.end())
    {
        world.myobmap->add(stock, stock->xpos(), stock->ypos());
    }
    stock->set_spawn_point(stock->xpos(), stock->ypos(),
                           static_cast<std::uint8_t>(stock->floor()));
    spawned_out = true;
    return stock;
}

// Construct display view `view_index` for a `view_count`-seat split. Ordering
// is load-bearing: the caller must have set numplayers and numviews already —
// viewscreen::resize (called by the constructor) reads active_screen()'s
// numviews at call time.
void build_display_view(screen& gameplay_screen, int view_count, int view_index)
{
    const og::view_layout::ViewLayout r = og::view_layout::compute_view_layout(
        view_count, view_index, og::view_layout::kModeFull,
        gameplay_screen.world_canvas_w(), gameplay_screen.world_canvas_h());
    gameplay_screen.viewob[view_index] = std::make_unique<viewscreen>(
        static_cast<short>(r.x), static_cast<short>(r.y),
        static_cast<short>(r.w), static_cast<short>(r.h),
        static_cast<short>(view_index));
}

} // namespace

namespace og::runtime {

bool local_transport_active(const SessionState& session) noexcept
{
    return session.has_local_transport_runtime();
}

// §4.5 follow cycle (declared in session_state.h so the render layer's input
// path can reach it). has_local_transport_runtime() is overridden ONLY by
// GameSession, so a true result guarantees the downcast — the exact
// local_transport_active seam pattern.
bool display_follow_cycle_target(SessionState& session, int view_index,
                                 bool reverse)
{
    if (!session.has_local_transport_runtime() || session.myscreen_ == nullptr)
        return false;
    GameSession& game_session = static_cast<GameSession&>(session);
    const auto runtime = game_session.local_transport_runtime_;
    if (runtime == nullptr || !runtime->networked || view_index < 0 ||
        static_cast<std::size_t>(view_index) >= runtime->view_follow.size())
    {
        return false;
    }
    DisplayFollowState& follow =
        runtime->view_follow[static_cast<std::size_t>(view_index)];
    if (!follow.engaged)
        return false;

    GameWorld& world = session.myscreen_->world();
    walker* const current =
        resolve_control_from_entity_id(world, follow.target_entity_id);
    follow.target_entity_id =
        og::sim::next_follow_target_id(world, current, reverse);

    // Reflect the choice immediately (the next control re-sync would land it
    // one frame later): the target rides view->control WITHOUT a user-tag
    // stamp ([NET-R6]).
    if (view_index < session.myscreen_->numviews)
    {
        viewscreen* const view =
            session.myscreen_->viewob[static_cast<std::size_t>(view_index)]
                .get();
        if (view != nullptr)
        {
            walker* const target = resolve_control_from_entity_id(
                world, follow.target_entity_id);
            view->control =
                (target != nullptr && !target->dead()) ? target : nullptr;
            view->follow_company_ =
                follow_company_for_control(runtime.get(), view->control);
        }
    }
    session.myscreen_->redrawme = 1;
    return true;
}

void local_transport_shadow_set_player_companies(
    GameSession& session,
    std::span<const std::pair<std::uint8_t, std::string>> companies)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr)
        return;
    for (const auto& [player_index, company] : companies)
    {
        if (player_index < runtime->player_company.size())
            runtime->player_company[player_index] = company;
    }
}

std::size_t local_transport_client_count(const GameSession& session) noexcept
{
    const auto runtime = session.local_transport_runtime_;
    return runtime != nullptr ? runtime->clients.size() : 0u;
}

#ifdef TESTING
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

#include "../../../tests/coverage_internal/local_transport_shadow_exit_prompt.inc"

bool local_transport_shadow_testing_server_pending_exit_prompt(
    GameSession& session) noexcept
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr || runtime->server == nullptr)
        return false;
    return runtime->server->pending_exit_prompt();
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
#endif

bool local_transport_shadow_is_paused(const GameSession& session) noexcept
{
    const auto runtime = session.local_transport_runtime_;
    const og::sim::GameClient* const display_client =
        runtime != nullptr ? runtime->display_client() : nullptr;
    return display_client != nullptr &&
        display_client->baseline().has_value() &&
        display_client->baseline()->paused;
}

bool local_transport_shadow_toggle_pause(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    og::sim::GameClient* const display_client =
        runtime != nullptr ? runtime->display_client() : nullptr;
    if (display_client == nullptr)
        return false;

    if (display_client->baseline().has_value() &&
        display_client->baseline()->paused)
    {
        display_client->send_pause_response();
    }
    else
    {
        display_client->send_pause_request();
    }

    if (session.myscreen_ != nullptr)
        session.myscreen_->redrawme = 1;
    return true;
}

bool local_transport_shadow_abort_level(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr)
        return true; // no networking: caller ends the mission locally

    if (runtime->authoritative_mode() && runtime->server != nullptr)
    {
        // Host / local split-screen: end the level authoritatively. The server
        // world.end=1 propagates to every display via the broadcast snapshot, so
        // all peers return to the team-build menu.
        screen* const server_screen = runtime->server_screen();
        if (server_screen == nullptr)
            return true;

        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(&runtime->server_session->game_);
        // Esc-abort ends the run without passing screen::endgame or the
        // withdraw finalize (the display ends on the mirrored world.end, and
        // the caller shows results directly), so this is a run-end routing
        // site of its own: route the mode's policy (Classic: no-op) exactly
        // once, on the authoritative save.
        {
            og::mode::LevelOutcome run_outcome;
            run_outcome.ending = 1;
            run_outcome.next_level =
                static_cast<short>(server_screen->world().current_scenario);
            run_outcome.networked = runtime->networked;
            run_outcome.withdrawn = true;
            og::mode::current_progression().on_run_ended(
                server_screen->save_data, server_screen->world(), run_outcome);
        }
        server_screen->world().end = 1;
        runtime->server->broadcast_current_state(
            og::sim::SnapshotCaptureMode::Peek,
            og::sim::EventDeliveryMode::Skip);
        return true;
    }

    // Networked client (no authoritative server): ask the server to withdraw
    // ALL players to the current level. The display stays live until the
    // server's terminal broadcast ends it, so this peer's character is NOT left
    // behind as AI. Return false so the caller waits for that broadcast instead
    // of tearing down locally (which would just disconnect this one player).
    if (runtime->display_client_index < runtime->clients.size() &&
        runtime->clients[runtime->display_client_index].game_client != nullptr)
    {
        runtime->clients[runtime->display_client_index]
            .game_client->request_level_abort();
        return false;
    }

    return true; // no client to relay through: fall back to a local end
}

bool local_transport_shadow_restart_level(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr || runtime->networked ||
        !runtime->authoritative_mode() || runtime->server == nullptr)
    {
        return false;
    }

    screen* const server_screen = runtime->server_screen();
    if (server_screen == nullptr)
        return false;

    auto server_scope = runtime->server_session->activate();
    GameplayContextGuard server_gameplay_scope(&runtime->server_session->game_);
    // The abort recipe (see local_transport_shadow_abort_level): route the
    // mode's run-end policy once on the authoritative save, end the level
    // with no results screen and no roster persist...
    {
        og::mode::LevelOutcome run_outcome;
        run_outcome.ending = 1;
        run_outcome.next_level =
            static_cast<short>(server_screen->world().current_scenario);
        run_outcome.networked = false;
        run_outcome.withdrawn = true;
        og::mode::current_progression().on_run_ended(
            server_screen->save_data, server_screen->world(), run_outcome);
    }
    // ...PLUS retry, set BEFORE the final broadcast so a drained snapshot
    // mirrors it. The display copy is also stamped directly: the caller
    // returns from the frame without another drain, and the native
    // `do { glad_main } while (world().retry)` loop (and the web
    // Playing-on-done re-entry) read the DISPLAY world.
    server_screen->world().retry = true;
    server_screen->world().end = 1;
    runtime->server->broadcast_current_state(
        og::sim::SnapshotCaptureMode::Peek,
        og::sim::EventDeliveryMode::Skip);
    if (session.myscreen_ != nullptr)
        session.myscreen_->world().retry = true;
    TRACE("pause_menu", "restart_level scen=%d",
          static_cast<int>(server_screen->world().current_scenario));
    return true;
}

bool local_transport_shadow_can_add_player(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    if (!local_seat_mutation_allowed(runtime.get(), session))
        return false;
    return compute_local_player_count(*session.myscreen_) <
        static_cast<std::size_t>(MAX_PLAYERS);
}

bool local_transport_shadow_add_local_player(GameSession& session)
{
    if (!local_transport_shadow_can_add_player(session))
        return false;

    const auto runtime = session.local_transport_runtime_;
    screen& gameplay_screen = *session.myscreen_;
    screen* const server_screen = runtime->server_screen();
    const auto inprocess_server_transport =
        std::dynamic_pointer_cast<og::sim::InProcessTransport>(
            runtime->server_transport);
    if (server_screen == nullptr || !inprocess_server_transport)
        return false;

    const std::size_t new_index = compute_local_player_count(gameplay_screen);
    // The new seat plays view 0's team (the common allied/co-op case). Its
    // key profile is whatever slot N of the profile pool holds — the rotation
    // seeded it, so nothing is reset here.
    viewscreen* const lead_view = gameplay_screen.viewob[0].get();
    const short team = lead_view != nullptr ? lead_view->my_team
                                            : gameplay_screen.world().my_team;

    // --- Server side: resolve the walker, connect + bind the new peer, and
    // broadcast the mapping. This runs between ticks (a pending pause only
    // suspends world ticks), so the bind's world mutation lands at a
    // deterministic point. runtime->own_seats is deliberately NOT rebound:
    // the finalize callbacks captured a copy at install, the new seat always
    // shares view 0's team (so the wallet team set is unchanged), and the
    // next launch rebuilds seats from the lobby config.
    LocalTransportClient client;
    bool spawned = false;
    {
        auto server_scope =
            runtime->server_session->activate(/*swap_render=*/false);
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        GameWorld& server_world = server_screen->world();
        walker* const control = resolve_or_spawn_seat_walker(
            server_world, team, static_cast<short>(new_index), spawned);
        if (control == nullptr)
        {
            TRACE("seats", "add_player_failed team=%d", static_cast<int>(team));
            return false;
        }

        client.transport =
            inprocess_server_transport->create_client_transport();
        client.server_peer_id =
            std::dynamic_pointer_cast<og::sim::InProcessTransport>(
                client.transport)
                ->local_peer_id();
        client.drives_display = false;
        client.input_slots = {new_index};
        runtime->server->connect_client(client.server_peer_id);
        runtime->server->bind_player(client.server_peer_id,
                                     new_index,
                                     team,
                                     control,
                                     static_cast<std::uint8_t>(new_index));
        runtime->server->send_initial_snapshot(
            client.server_peer_id, og::sim::SnapshotCaptureMode::Peek);
        // bind_player does not broadcast — this ControlChange is what lets
        // the display client map the new seat onto its new view.
        runtime->server->set_player_control(new_index, control);
        server_screen->save_data.numplayers =
            static_cast<unsigned char>(new_index + 1);
        TRACE("seats", "add_player index=%d team=%d source=%s entity=%u",
              static_cast<int>(new_index), static_cast<int>(team),
              spawned ? "spawned" : "claimed", control->entity_id());
    }

    // --- Display side. Order is load-bearing: numplayers first
    // (compute_local_player_count is the hidden coupling), then numviews
    // (viewscreen::resize reads it), then the view, then relayout.
    gameplay_screen.save_data.numplayers =
        static_cast<unsigned char>(new_index + 1);
    // The new seat reads profile-pool slot N — which can duplicate a mapping
    // an active seat already cycled onto (P1 on ARROWS, slot 1's live keymap
    // IS factory arrows). Land it on the first unchosen mapping instead.
    if (og::ui::ensure_unique_seat_mapping(cfg, static_cast<int>(new_index),
                                           static_cast<int>(new_index + 1)))
    {
        save_player_control_settings_to_cfg(cfg);
        cfg.save_settings();
    }
    gameplay_screen.numviews = static_cast<short>(new_index + 1);
    build_display_view(gameplay_screen, static_cast<int>(new_index + 1),
                       static_cast<int>(new_index));
    viewscreen* const new_view = gameplay_screen.viewob[new_index].get();
    if (new_view != nullptr)
        new_view->my_team = team;
    gameplay_screen.relayout_views();
    runtime->view_follow[new_index] = {};

    client.game_client = std::make_unique<og::sim::GameClient>(
        *client.transport, client.server_peer_id, nullptr);
    configure_background_game_client(gameplay_screen, *client.game_client);
    runtime->clients.push_back(std::move(client));

    // Drain the initial snapshot now so the new background client is ready
    // (it must be able to answer exit prompts) without waiting a frame.
    {
        auto client_scope = session.activate(/*swap_render=*/false);
        GameplayContextGuard client_gameplay_scope(&session.game_);
        poll_local_transport_client(gameplay_screen, runtime->clients.back(),
                                    INT_MAX);
    }
    gameplay_screen.redrawme = 1;
    return true;
}

bool local_transport_shadow_can_remove_player(GameSession& session,
                                              int player_index)
{
    const auto runtime = session.local_transport_runtime_;
    if (!local_seat_mutation_allowed(runtime.get(), session))
        return false;
    const std::size_t count = compute_local_player_count(*session.myscreen_);
    return count >= 2u && player_index >= 0 &&
        static_cast<std::size_t>(player_index) < count;
}

bool local_transport_shadow_remove_local_player(GameSession& session,
                                                int player_index)
{
    if (!local_transport_shadow_can_remove_player(session, player_index))
        return false;

    const auto runtime = session.local_transport_runtime_;
    screen& gameplay_screen = *session.myscreen_;
    screen* const server_screen = runtime->server_screen();
    if (server_screen == nullptr)
        return false;

    const std::size_t old_count = compute_local_player_count(gameplay_screen);
    const std::size_t removed = static_cast<std::size_t>(player_index);
    const std::size_t last = old_count - 1;

    // Surviving view teams, captured before any rebuild: view k (k >= removed)
    // inherits old view k+1's team.
    std::array<short, MAX_PLAYERS> view_teams = {};
    for (std::size_t k = 0; k < old_count; ++k)
    {
        viewscreen* const view = gameplay_screen.viewob[k].get();
        view_teams[k] = view != nullptr ? view->my_team
                                        : gameplay_screen.world().my_team;
    }

    // --- Server side: clear the removed mapping FIRST (the disconnect path
    // never clears player_controls_ or broadcasts entity 0), release the
    // walker to AI, shift the surviving bindings down across the FIXED peers
    // in ascending player-index order (deterministic), then disconnect the
    // vacated LAST peer — never peer 0, the host peer whose kick cascades.
    // Removing any seat, seat 0 included, therefore keeps the display client
    // (peer 0) alive: only bindings move, peers keep their identities.
    {
        auto server_scope =
            runtime->server_session->activate(/*swap_render=*/false);
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);

        walker* const removed_control =
            runtime->server->player_control(removed);
        runtime->server->set_player_control(removed, nullptr);
        if (removed_control != nullptr &&
            removed_control->user() == static_cast<int>(removed))
        {
            // The walker stays alive as AI on its team (the disconnect
            // semantics every leave path converges on).
            removed_control->set_user(-1);
            removed_control->restore_act_type();
        }

        for (std::size_t k = removed + 1; k < old_count; ++k)
        {
            walker* const control = runtime->server->player_control(k);
            // Pure renumber: the walker keeps ACT_CONTROL and its command
            // state; only the player tag moves down with the seat.
            if (control != nullptr &&
                control->user() == static_cast<int>(k))
            {
                control->set_user(static_cast<signed char>(k - 1));
            }
            runtime->server->bind_player(
                runtime->clients[k - 1].server_peer_id,
                k - 1,
                view_teams[k],
                control,
                static_cast<std::uint8_t>(k - 1));
            // bind_player is silent — broadcast the renumbered mapping.
            runtime->server->set_player_control(k - 1, control);
        }
        if (removed != last)
            runtime->server->set_player_control(last, nullptr);
        runtime->server->disconnect_client(
            runtime->clients[last].server_peer_id);
        server_screen->save_data.numplayers =
            static_cast<unsigned char>(old_count - 1);
        if (runtime->server_session->game_.sim_events != nullptr)
        {
            runtime->server_session->game_.sim_events->push_notification(
                "Player " + std::to_string(player_index + 1) + " left", 40);
        }
        TRACE("seats", "remove_player index=%d count=%d", player_index,
              static_cast<int>(old_count - 1));
    }

    runtime->clients.pop_back();

    // --- Display side: numplayers first, then the shrink. numviews may drop
    // before the rebuild (every surviving slot already has a live view), and
    // viewob[i] stays non-null for all i < numviews at every instant.
    gameplay_screen.save_data.numplayers =
        static_cast<unsigned char>(old_count - 1);
    gameplay_screen.numviews = static_cast<short>(old_count - 1);
    for (std::size_t k = removed; k + 1 < old_count; ++k)
    {
        build_display_view(gameplay_screen, static_cast<int>(old_count - 1),
                           static_cast<int>(k));
        viewscreen* const view = gameplay_screen.viewob[k].get();
        if (view != nullptr)
            view->my_team = view_teams[k + 1];
        runtime->view_follow[k] = runtime->view_follow[k + 1];
    }
    gameplay_screen.viewob[last].reset();
    runtime->view_follow[last] = {};
    gameplay_screen.relayout_views();

    // Key profiles follow the seats down; the freed profile rotates to the
    // first inactive slot so a later add reuses a distinct mapping (existing,
    // tested primitive).
    (void)compact_player_controls_after_removal(player_index,
                                                static_cast<int>(old_count));

    gameplay_screen.redrawme = 1;
    return true;
}

// Staged adoption is only valid for the level the launch actually loads.
// The stage tracks the LOBBY's inputs; a start config mutated after
// consumption (the web jitter-capture profile overrides scen_num at GO) can
// name a different level than the staged world. Adopting anyway is a
// guaranteed soft-lock: the display runs one map, the authoritative world
// another, so every full-grid keyframe is rejected (size mismatch) until the
// client fatally desyncs into the "Connection Lost" popup. A mismatched
// stage is a stale stage ensure_current could not see — the caller disposes
// it and takes the legacy display-seed path, which seeds the authoritative
// world FROM the display and therefore cannot diverge.
static bool stage_matches_launch_level(const og::server::MatchStage& stage,
                                       const SaveData& launch_save)
{
    return stage.staged_save().scen_num == launch_save.scen_num &&
        stage.staged_save().current_campaign == launch_save.current_campaign;
}

static bool consume_mismatched_stage(og::server::MatchStage& stage,
                                     const SaveData& launch_save)
{
    if (stage_matches_launch_level(stage, launch_save))
        return false;
    LogError(
        "transport_shadow_stage_level_mismatch staged={}:{} launch={}:{} "
        "— taking the display-seed fallback\n",
        stage.staged_save().current_campaign,
        stage.staged_save().scen_num,
        launch_save.current_campaign,
        launch_save.scen_num);
    TRACE("net", "stage_level_mismatch staged=%d launch=%d",
          static_cast<int>(stage.staged_save().scen_num),
          static_cast<int>(launch_save.scen_num));
    stage.dispose();
    return true;
}

void reset_local_transport_shadow(GameSession& session,
                                  screen& gameplay_screen,
                                  og::server::MatchStage* match_stage)
{
    if (current_game == nullptr || current_game->sim_events == nullptr)
    {
        clear_local_transport_shadow(session);
        return;
    }

    // Staged lobby (#218): adopt the owner's staged world as the
    // authoritative world instead of double-loading + seeding from the
    // display keyframe. Replay playback keeps the legacy path — the
    // recording's initial snapshot IS a staged world of its era, and staging
    // would re-derive a different world from a fresh seed. ensure_current is
    // the GO force: a dirty stage restages synchronously here; a Failed or
    // missing stage falls back to the legacy path, and so does a stage for a
    // different level than the display loaded (consume_mismatched_stage).
    const bool adopt_stage = match_stage != nullptr &&
        !session.replay_playback_active_ &&
        match_stage->ensure_current(og::server::stage_clock_now_ms()) &&
        !consume_mismatched_stage(*match_stage, gameplay_screen.save_data);

    gameplay_screen.set_render_interpolation_client(nullptr);
    session.local_transport_runtime_.reset();
    session.relay_transport_active_ = false;
    session.relay_speed_warning_shown_ = false;

    auto runtime = std::make_shared<LocalTransportRuntime>();
    runtime->networked = session.networked_session_;
    runtime->isolated_company = session.isolated_company_session_;
    const std::size_t player_count = compute_local_player_count(gameplay_screen);
    std::array<std::uint32_t, MAX_PLAYERS> control_entity_ids = {};
    std::array<short, MAX_PLAYERS> player_teams = {};
    for (std::size_t index = 0; index < player_count; ++index)
    {
        viewscreen* const view = gameplay_screen.viewob[index].get();
        player_teams[index] =
            view != nullptr ? view->my_team : gameplay_screen.world().my_team;
        control_entity_ids[index] =
            view != nullptr && view->control != nullptr
                ? view->control->entity_id()
                : 0u;
    }
    const std::size_t owned_seat_count = std::min<std::size_t>(
        session.own_player_indices_.size(), player_count);
    for (std::size_t seat_order = 0;
         seat_order < owned_seat_count;
         ++seat_order)
    {
        // own_player_indices_ contains GLOBAL ids (a one-seat joiner may own
        // player 6), while player_teams is indexed by this machine's LOCAL
        // view/seat order.
        runtime->own_seats.push_back(LocalSeatBinding{
            .player_index = session.own_player_indices_[seat_order],
            .team = player_teams[seat_order],
        });
    }

    GameSession::Config server_cfg;
    server_cfg.numviews = gameplay_screen.numviews;
    server_cfg.create_display = false;
    server_cfg.install_legacy_globals = false;
    runtime->server_session = std::make_unique<GameSession>(server_cfg);
    runtime->server_session->isolated_company_session_ =
        runtime->isolated_company;
    runtime->server_transport = og::sim::InProcessTransport::create_server();
    runtime->server_transport->accept_connections();
    const auto inprocess_server_transport =
        std::dynamic_pointer_cast<og::sim::InProcessTransport>(
            runtime->server_transport);
    if (!inprocess_server_transport)
        return;

    {
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        screen* const server_screen = runtime->server_screen();
        if (server_screen == nullptr)
            return;
        // A GTL no longer owns the local player count. This fresh
        // authoritative screen must inherit the live launch configuration
        // before the install uses that runtime projection to build views.
        server_screen->save_data.numplayers =
            gameplay_screen.save_data.numplayers;
        if (adopt_stage)
        {
            // Staged adoption (#218): the authoritative world is the lobby's
            // dormant staged world — no server-side level load, no display
            // keyframe seed, no fresh weather roll (the staged load rolled
            // this round's kind under the match-seed pin, and it rides the
            // adopted state). Prep-clear FIRST (reset_level_progress re-arms
            // the on_load latch; the adopt claims it truthfully after), then
            // append the staged announcements the clear must not eat.
            prepare_server_session_for_gameplay(*runtime->server_session);
            if (!og::server::adopt_staged_world(
                    server_screen->level_runtime_data(),
                    server_screen->save_data,
                    *match_stage))
            {
                return;
            }
            runtime->server_session->game_.sim_events->append(
                match_stage->take_events());
            match_stage->dispose();
            adopt_display_session_settings(*server_screen, gameplay_screen);
        }
        else
        {
            // Legacy display-seed path: replay playback (the recorded initial
            // snapshot is the truth) and the no-stage fallback.
            //
            // Replay arm (#207): SESSION-only like cross_control below, but it
            // must be seeded BEFORE the load — load_saved_game carries a
            // pre-set arm across its own disk round-trip and its completed-
            // level purge reads it, so the authoritative world loads the
            // restored census (and the shadow fold later restores the origin).
            server_screen->save_data.replay_level =
                gameplay_screen.save_data.replay_level;
            server_screen->save_data.replay_origin =
                gameplay_screen.save_data.replay_origin;
            if (load_saved_game(runtime->networked || runtime->isolated_company
                                   ? "netsession"
                                   : og::data::active_company_slot().c_str(),
                               server_screen) == 0)
            {
                return;
            }
            // cross_control is SESSION-only (never serialized), so the slot
            // round-trip through disk just dropped it — carry the lobby-config
            // value from the display save (the same dropped-field trap
            // copy_headless_server_save_data guards against on the headless path).
            server_screen->save_data.cross_control =
                gameplay_screen.save_data.cross_control;
            prepare_server_session_for_gameplay(*runtime->server_session);
            og::sim::apply_snapshot(
                server_screen->world(),
                og::sim::capture_keyframe_snapshot(gameplay_screen.world()));
        }
        // §4.4 "policy off in every local session": the adopted/seeded world
        // replays whatever its source carried — after a networked
        // owner-locked round that includes control_policy=1 plus a stale
        // machine map (nothing else resets them: level loads rebuild the
        // attached world through replace_loaded_world_state, which
        // deliberately preserves both scalars for the dedicated server's
        // in-session transitions). Stamp the legacy shared pool explicitly so
        // no local round can ever run owner-locked; the install's initial
        // snapshots then heal the display mirror too. A networked fallback
        // through this install keeps the seeded values — the networked
        // installs stamp the real policy themselves. (The stage stamps this
        // too for non-networked configs; the re-stamp is idempotent and keeps
        // the guarantee independent of which branch ran above.)
        if (!runtime->networked)
        {
            std::array<std::uint8_t, og::sim::kPlayerMachineSlots>
                no_machines;
            no_machines.fill(og::sim::kPlayerMachineNone);
            og::sim::set_control_policy(server_screen->world(),
                                        og::sim::kControlPolicyLegacy,
                                        no_machines);
        }
        // Authoritative roll for this round's weather — legacy path only:
        // the staged load already rolled under the match-seed pin, and
        // replay playback keeps the RECORDED kind the seed snapshot carried.
        // Runs once per round — the return-to-team-build flow re-enters this
        // install for the next level.
        // NOTE: current_session is the SERVER session inside this scope —
        // the playback flag lives on the outer gameplay session.
        if (!adopt_stage && !session.replay_playback_active_)
            server_screen->world().roll_weather();
        // Mirror the kind onto the display world NOW (the first tick's
        // snapshot would deliver it anyway): the replay recorder snapshots
        // the display world right after this install, and the recording
        // must carry the kind the session actually plays under.
        gameplay_screen.world().set_weather(server_screen->world().weather());
        for (std::size_t index = 0; index < player_count; ++index)
        {
            if (server_screen->viewob[index] == nullptr)
                continue;
            server_screen->viewob[index]->my_team = player_teams[index];
            // Display-entity ids exist only in a display-seeded world; a
            // stage-built world claims controls through the null bind below,
            // like the dedicated server.
            server_screen->viewob[index]->control = adopt_stage
                ? nullptr
                : resolve_control_from_entity_id(
                      server_screen->world(), control_entity_ids[index]);
        }
    }

    screen* const server_screen = runtime->server_screen();
    if (server_screen == nullptr)
        return;

    runtime->server = std::make_unique<og::sim::GameServer>(
        server_screen->world(),
        *runtime->server_session->game_.sim_events,
        *runtime->server_transport);
    // Between levels, EVERY mode returns every player to the team-build
    // "Continue" menu instead of auto-advancing the next level in-session:
    // single-player, local split-screen, and networked alike. (Networked keeps
    // the connection live across the menu; local just re-enters go_menu.)
    runtime->server->set_return_to_lobby_mode(true);
    runtime->server->on_save_sync = [server_screen] {
        server_screen->sync_save_data_from_world();
    };
    runtime->server->on_level_transition =
        [server_screen,
         display_screen = &gameplay_screen,
         networked = runtime->networked,
         isolated_company = runtime->isolated_company,
         own_seats = runtime->own_seats,
         server_session = runtime->server_session.get()](
            int level_id) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            server_screen->framecount = display_screen->framecount;
            // Every level-end returns every player to the team-build "Continue"
            // menu — single-player, local split-screen, and networked alike.
            // Finalize per-player progress + advance the cursor only; the server
            // then forwards a terminal EndGame so each display ends and go_menu
            // shows the menu (the next level is started fresh from there). The
            // game NEVER auto-advances the next level in-session, in any mode.
            return finalize_level_and_advance_cursor(
                *server_screen, level_id, networked, isolated_company,
                own_seats);
        };
    runtime->server->on_exit_accepted =
        [server_screen,
         display_screen = &gameplay_screen,
         networked = runtime->networked,
         isolated_company = runtime->isolated_company,
         own_seats = runtime->own_seats,
         server_session = runtime->server_session.get()](
            int destination) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            server_screen->framecount = display_screen->framecount;
            // Taking an exit portal completes the level like a win: finalize the
            // cursor only and let the server forward a terminal EndGame so every
            // display returns to the team-build menu. NEVER auto-advance the next
            // level in-session — in any mode (see on_level_transition).
            return finalize_level_and_advance_cursor(
                *server_screen, destination, networked, isolated_company,
                own_seats);
        };
    runtime->server->on_withdraw_accepted =
        [server_screen,
         networked = runtime->networked,
         isolated_company = runtime->isolated_company,
         server_session = runtime->server_session.get()](
            int destination) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            // Withdraw/retreat points the cursor at the destination and returns
            // every display to the team-build menu (which loads it fresh). Never
            // load the destination level in-session — in any mode.
            return finalize_withdraw_and_advance_cursor(
                *server_screen, destination, networked, isolated_company);
        };

    runtime->clients.reserve(player_count);
    for (std::size_t index = 0; index < player_count; ++index)
    {
        LocalTransportClient client;
        client.transport = inprocess_server_transport->create_client_transport();
        client.server_peer_id =
            std::dynamic_pointer_cast<og::sim::InProcessTransport>(
                client.transport)
                ->local_peer_id();
        client.drives_display = (index == 0);

        const og::sim::PeerId peer_id = client.server_peer_id;
        runtime->server->connect_client(peer_id);
        // Staged adoption binds null (the dedicated-server shape): the
        // display's provisional entity ids do not exist in a stage-built
        // world, so bind_player's own claim scan resolves each seat.
        walker* const initial_control = adopt_stage
            ? nullptr
            : resolve_control_from_entity_id(
                  server_screen->world(), control_entity_ids[index]);
        runtime->server->bind_player(
            peer_id,
            index,
            player_teams[index],
            initial_control,
            static_cast<std::uint8_t>(index));
        client.input_slots = {index};
        client.game_client = std::make_unique<og::sim::GameClient>(
            *client.transport,
            peer_id,
            client.drives_display ? &gameplay_screen.world() : nullptr);
        og::sim::GameClient* const local_client = client.game_client.get();
        configure_background_game_client(gameplay_screen, *local_client);
        if (client.drives_display)
            configure_display_game_client(
                *runtime, gameplay_screen, *local_client, {});
        runtime->clients.push_back(std::move(client));
    }

    {
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        runtime->server->send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    }
    {
        auto client_scope = session.activate(/*swap_render=*/false);
        GameplayContextGuard client_gameplay_scope(&session.game_);
        for (auto& client : runtime->clients)
            poll_local_transport_client(gameplay_screen, client, INT_MAX);
    }

    session.local_transport_runtime_ = std::move(runtime);
}

void reset_network_host_transport_shadow(
    GameSession& session,
    screen& gameplay_screen,
    std::shared_ptr<og::sim::ITransport> server_transport,
    std::shared_ptr<og::sim::InProcessTransport> local_client_transport,
    const std::vector<og::sim::LobbyPlayerBinding>& player_bindings,
    og::server::MatchStage* match_stage)
{
    if (current_game == nullptr || current_game->sim_events == nullptr ||
        !server_transport || !local_client_transport)
    {
        clear_local_transport_shadow(session);
        return;
    }

    // Staged lobby (#218): see reset_local_transport_shadow — the staged
    // world becomes the authoritative world; replay playback, a Failed/
    // missing stage, and a stage for a different level than the display
    // loaded (consume_mismatched_stage) keep the legacy display-seed path.
    const bool adopt_stage = match_stage != nullptr &&
        !session.replay_playback_active_ &&
        match_stage->ensure_current(og::server::stage_clock_now_ms()) &&
        !consume_mismatched_stage(*match_stage, gameplay_screen.save_data);

    gameplay_screen.set_render_interpolation_client(nullptr);
    session.local_transport_runtime_.reset();

    auto runtime = std::make_shared<LocalTransportRuntime>();
    runtime->mode = LocalTransportRuntime::Mode::Authoritative;
    runtime->networked = session.networked_session_;
    runtime->server_transport = std::move(server_transport);
    runtime->server_transport->accept_connections();

    GameSession::Config server_cfg;
    server_cfg.numviews = gameplay_screen.numviews;
    server_cfg.create_display = false;
    server_cfg.install_legacy_globals = false;
    runtime->server_session = std::make_unique<GameSession>(server_cfg);

    // This machine's local seats are the lobby bindings on the loopback peer,
    // in local_slot order. Each binding carries the session's explicit
    // gameplay team. Character roster colors remain independent combat teams;
    // a seat assignment never rewrites allegiance.
    const og::sim::PeerId loopback_peer_id =
        local_client_transport->local_peer_id();
    std::vector<LocalSeatBinding> host_seats;
    {
        std::vector<const og::sim::LobbyPlayerBinding*> local_bindings;
        for (const og::sim::LobbyPlayerBinding& binding : player_bindings)
        {
            if (binding.peer_id == loopback_peer_id)
                local_bindings.push_back(&binding);
        }
        std::sort(local_bindings.begin(),
                  local_bindings.end(),
                  [](const og::sim::LobbyPlayerBinding* lhs,
                     const og::sim::LobbyPlayerBinding* rhs) {
                      return lhs->local_slot < rhs->local_slot;
                  });
        for (const og::sim::LobbyPlayerBinding* binding : local_bindings)
        {
            host_seats.push_back(LocalSeatBinding{
                .player_index = binding->player_index,
                .team = static_cast<short>(binding->team),
            });
        }
    }
    if (!host_seats.empty())
    {
        runtime->own_seats = host_seats;
    }

    // Multi-seat hosts stamp each view's team from its seat before the server
    // views are seeded from the display views below.
    stamp_display_seat_teams(gameplay_screen, host_seats);

    const std::size_t host_view_count =
        compute_local_player_count(gameplay_screen);
    std::array<short, MAX_PLAYERS> view_teams = {};
    std::array<std::uint32_t, MAX_PLAYERS> view_control_ids = {};
    for (std::size_t index = 0; index < host_view_count; ++index)
    {
        viewscreen* const view = gameplay_screen.viewob[index].get();
        view_teams[index] =
            view != nullptr ? view->my_team : gameplay_screen.world().my_team;
        view_control_ids[index] =
            view != nullptr && view->control != nullptr
                ? view->control->entity_id()
                : 0u;
    }

    {
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        screen* const server_screen = runtime->server_screen();
        if (server_screen == nullptr)
            return;
        // A GTL no longer owns the local player count. This fresh
        // authoritative screen must inherit the live launch configuration
        // before the install uses that runtime projection to build views.
        server_screen->save_data.numplayers =
            gameplay_screen.save_data.numplayers;
        if (adopt_stage)
        {
            // Staged adoption (#218): no server-side level load, no display
            // keyframe seed, no fresh weather roll — see
            // reset_local_transport_shadow. Prep-clear FIRST, adopt (claims
            // the on_load latch truthfully), append the staged announcements
            // the clear must not eat, dispose the stage.
            prepare_server_session_for_gameplay(*runtime->server_session);
            if (!og::server::adopt_staged_world(
                    server_screen->level_runtime_data(),
                    server_screen->save_data,
                    *match_stage))
            {
                return;
            }
            runtime->server_session->game_.sim_events->append(
                match_stage->take_events());
            match_stage->dispose();
            adopt_display_session_settings(*server_screen, gameplay_screen);
        }
        else
        {
            // Legacy display-seed path (replay playback / no-stage fallback).
            //
            // Replay arm (#207): seed BEFORE the load — see
            // reset_local_transport_shadow above (the authoritative purge and
            // the shadow fold's origin-restore both read it).
            server_screen->save_data.replay_level =
                gameplay_screen.save_data.replay_level;
            server_screen->save_data.replay_origin =
                gameplay_screen.save_data.replay_origin;
            if (load_saved_game(runtime->networked
                                   ? "netsession"
                                   : og::data::active_company_slot().c_str(),
                               server_screen) == 0)
            {
                return;
            }
            // Session-only cross_control dropped by the disk round-trip — carry
            // it (see reset_local_transport_shadow above).
            server_screen->save_data.cross_control =
                gameplay_screen.save_data.cross_control;

            prepare_server_session_for_gameplay(*runtime->server_session);
            og::sim::apply_snapshot(
                server_screen->world(),
                og::sim::capture_keyframe_snapshot(gameplay_screen.world()));
            // Authoritative roll (see reset_local_transport_shadow): after the
            // display-world seed snapshot, once per hosted round.
            // NOTE: current_session is the SERVER session inside this scope —
            // the playback flag lives on the outer gameplay session.
            if (!session.replay_playback_active_)
                server_screen->world().roll_weather();
        }
        // See the local install above: display world mirrors the kind now.
        gameplay_screen.world().set_weather(server_screen->world().weather());
        if (!adopt_stage)
            release_world_control_claims(server_screen->world());
        for (std::size_t index = 0; index < host_view_count; ++index)
        {
            if (server_screen->viewob[index] == nullptr)
                continue;
            server_screen->viewob[index]->my_team = view_teams[index];
            // A stage-built world has no display entity ids; the null binds
            // below claim controls the dedicated-server way.
            server_screen->viewob[index]->control = adopt_stage
                ? nullptr
                : resolve_control_from_entity_id(
                      server_screen->world(), view_control_ids[index]);
        }
    }

    screen* const server_screen = runtime->server_screen();
    if (server_screen == nullptr)
        return;

    // §4.4 control-policy install: derive owner-locked from the game-start
    // config (the display save carries cross_control + the deploy-filtered
    // owner tags from apply_lobby_game_start_config — never a
    // disk-round-tripped save) and stamp the machine map BEFORE the seats
    // bind below: owner-locked bind scans consult it, and snapshot v9
    // replicates both scalars to every client mirror. The staged world
    // arrives with the policy already installed from the SAME bindings
    // (MatchStage step 5); the adoption skips the re-install.
    if (!adopt_stage)
    {
        og::sim::install_control_policy(
            server_screen->world(),
            runtime->networked,
            gameplay_screen.save_data.cross_control != 0,
            player_bindings,
            gameplay_screen.save_data.team_list);
    }

    runtime->server = std::make_unique<og::sim::GameServer>(
        server_screen->world(),
        *runtime->server_session->game_.sim_events,
        *runtime->server_transport);
    // Between levels, EVERY mode returns every player to the team-build
    // "Continue" menu instead of auto-advancing the next level in-session:
    // single-player, local split-screen, and networked alike. (Networked keeps
    // the connection live across the menu; local just re-enters go_menu.)
    runtime->server->set_return_to_lobby_mode(true);
    runtime->server->on_save_sync = [server_screen] {
        server_screen->sync_save_data_from_world();
    };
    runtime->server->on_level_transition =
        [server_screen,
         display_screen = &gameplay_screen,
         networked = runtime->networked,
         own_seats = runtime->own_seats,
         server_session = runtime->server_session.get()](
            int level_id) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            server_screen->framecount = display_screen->framecount;
            // Every level-end returns every player to the team-build "Continue"
            // menu — single-player, local split-screen, and networked alike.
            // Finalize per-player progress + advance the cursor only; the server
            // then forwards a terminal EndGame so each display ends and go_menu
            // shows the menu (the next level is started fresh from there). The
            // game NEVER auto-advances the next level in-session, in any mode.
            return finalize_level_and_advance_cursor(
                *server_screen, level_id, networked,
                /*isolated_company=*/false, own_seats);
        };
    runtime->server->on_exit_accepted =
        [server_screen,
         display_screen = &gameplay_screen,
         networked = runtime->networked,
         own_seats = runtime->own_seats,
         server_session = runtime->server_session.get()](
            int destination) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            server_screen->framecount = display_screen->framecount;
            // Taking an exit portal completes the level like a win: finalize the
            // cursor only and let the server forward a terminal EndGame so every
            // display returns to the team-build menu. NEVER auto-advance the next
            // level in-session — in any mode (see on_level_transition).
            return finalize_level_and_advance_cursor(
                *server_screen, destination, networked,
                /*isolated_company=*/false, own_seats);
        };
    runtime->server->on_withdraw_accepted =
        [server_screen,
         networked = runtime->networked,
         server_session = runtime->server_session.get()](
            int destination) {
            auto server_scope = server_session->activate();
            GameplayContextGuard server_gameplay_scope(&server_session->game_);
            // Withdraw/retreat points the cursor at the destination and returns
            // every display to the team-build menu (which loads it fresh). Never
            // load the destination level in-session — in any mode.
            return finalize_withdraw_and_advance_cursor(
                *server_screen, destination, networked,
                /*isolated_company=*/false);
        };

    // Connected peers with no seat binding are real spectators. Admit them
    // BEFORE the explicit initial-snapshot pass; transport discovery alone is
    // deliberately not enough to authorize an unknown zero-token connection.
    // Register loopback first so a zero-seat host remains the gameplay host.
    const auto register_lobby_peer =
        [&player_bindings, &runtime](og::sim::PeerId peer_id) {
            const bool owns_seat = std::any_of(
                player_bindings.begin(),
                player_bindings.end(),
                [peer_id](const og::sim::LobbyPlayerBinding& binding) {
                    return binding.peer_id == peer_id;
                });
            if (owns_seat)
                runtime->server->connect_client(peer_id);
            else
                runtime->server->connect_spectator(peer_id);
        };
    register_lobby_peer(loopback_peer_id);
    for (const og::sim::PeerId peer_id :
         runtime->server_transport->connected_peers())
    {
        if (peer_id != loopback_peer_id)
            register_lobby_peer(peer_id);
    }
    for (const og::sim::LobbyPlayerBinding& binding : player_bindings)
    {
        runtime->server->connect_client(binding.peer_id);
        runtime->server->bind_player(
            binding.peer_id,
            binding.player_index,
            binding.team,
            nullptr,
            binding.local_slot);
    }

    LocalTransportClient client;
    client.transport = local_client_transport;
    client.server_peer_id = loopback_peer_id;
    client.drives_display = true;
    // The host's single loopback connection carries all of its seats' inputs:
    // seat k reads keyboard map k, so slot k drives local_slot k.
    for (std::size_t slot = 0;
         slot < host_seats.size() &&
         slot < static_cast<std::size_t>(MAX_PLAYERS);
         ++slot)
    {
        client.input_slots.push_back(slot);
    }
    client.game_client = std::make_unique<og::sim::GameClient>(
        *client.transport,
        client.server_peer_id,
        &gameplay_screen.world());
    configure_background_game_client(gameplay_screen, *client.game_client);
    configure_display_game_client(
        *runtime,
        gameplay_screen,
        *client.game_client,
        host_seats);
    runtime->clients.push_back(std::move(client));

    {
        auto server_scope = runtime->server_session->activate();
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        runtime->server->send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    }
    {
        auto client_scope = session.activate(/*swap_render=*/false);
        GameplayContextGuard client_gameplay_scope(&session.game_);
        for (auto& local_client : runtime->clients)
            poll_local_transport_client(gameplay_screen, local_client, INT_MAX);
    }

    session.local_transport_runtime_ = std::move(runtime);
}

void reset_network_client_transport_shadow(
    GameSession& session,
    screen& gameplay_screen,
    std::shared_ptr<og::sim::ITransport> client_transport,
    og::sim::PeerId server_peer_id,
    std::vector<LocalSeatBinding> local_seats)
{
    if (!client_transport || server_peer_id == 0)
    {
        clear_local_transport_shadow(session);
        return;
    }

    gameplay_screen.set_render_interpolation_client(nullptr);
    session.local_transport_runtime_.reset();

    auto runtime = std::make_shared<LocalTransportRuntime>();
    runtime->mode = LocalTransportRuntime::Mode::ClientOnly;
    runtime->networked = session.networked_session_;
    if (!local_seats.empty())
        runtime->own_seats = local_seats;

    // Multi-seat joiners stamp view teams from their seats (the roster-order
    // guess from load_saved_game is wrong for them).
    stamp_display_seat_teams(gameplay_screen, local_seats);

    LocalTransportClient client;
    client.transport = std::move(client_transport);
    client.server_peer_id = server_peer_id;
    client.drives_display = true;
    // One connection carries all local seats: slot k drives local_slot k.
    for (std::size_t slot = 0;
         slot < local_seats.size() &&
         slot < static_cast<std::size_t>(MAX_PLAYERS);
         ++slot)
    {
        client.input_slots.push_back(slot);
    }
    client.game_client = std::make_unique<og::sim::GameClient>(
        *client.transport,
        client.server_peer_id,
        &gameplay_screen.world());
    configure_background_game_client(gameplay_screen, *client.game_client);
    configure_display_game_client(
        *runtime,
        gameplay_screen,
        *client.game_client,
        std::move(local_seats));
    runtime->clients.push_back(std::move(client));

    {
        auto client_scope = session.activate(/*swap_render=*/false);
        GameplayContextGuard client_gameplay_scope(&session.game_);
        poll_local_transport_client(gameplay_screen, runtime->clients.front(), INT_MAX);
    }

    session.local_transport_runtime_ = std::move(runtime);
}

void reset_network_client_transport_shadow(
    GameSession& session,
    screen& gameplay_screen,
    std::shared_ptr<og::sim::ITransport> client_transport,
    og::sim::PeerId server_peer_id,
    std::size_t local_player_index)
{
    std::vector<LocalSeatBinding> local_seats;
    local_seats.push_back(LocalSeatBinding{
        .player_index = local_player_index,
        .team = gameplay_screen.save_data.my_team,
    });
    reset_network_client_transport_shadow(
        session,
        gameplay_screen,
        std::move(client_transport),
        server_peer_id,
        std::move(local_seats));
}

void clear_local_transport_shadow(GameSession& session) noexcept
{
    if (session.myscreen_ != nullptr)
        session.myscreen_->set_render_interpolation_client(nullptr);
    session.local_transport_runtime_.reset();
    session.relay_transport_active_ = false;
    session.relay_speed_warning_shown_ = false;
    session.networked_session_ = false;
    session.isolated_company_session_ = false;
    session.own_player_indices_.clear();
}

void local_transport_shadow_send_input(GameSession& session,
                                       const InputState& input,
                                       std::uint32_t tick)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr)
        return;

    const bool spectator =
        session.myscreen_ != nullptr &&
        og::ui::is_spectator_mode(session.myscreen_->save_data);
    for (std::size_t index = 0; index < runtime->clients.size(); ++index)
    {
        LocalTransportClient& client = runtime->clients[index];
        InputState player_input{};
        player_input.quit_requested = input.quit_requested;
        player_input.timer_wait_request =
            index == runtime->display_client_index
                ? input.timer_wait_request
                : kNoTimerWaitRequest;
        if (!spectator)
        {
            // Slot-for-slot copy: the server routes players[slot] to the seat
            // bound with local_slot == slot on this connection.
            for (const std::size_t slot : client.input_slots)
            {
                if (slot < static_cast<std::size_t>(MAX_PLAYERS))
                    player_input.players[slot] = input.players[slot];
            }
        }
        client.game_client->send_input(player_input, tick);
    }
}

namespace {

// Shared body of finish_tick and pump_paused: one authoritative server step
// (when this machine hosts one) followed by a full client-mirror drain.
// Returns false when the display session/world ended underneath the caller.
// The runtime is passed in because only the named friend functions may read
// session.local_transport_runtime_.
bool local_transport_shadow_step_and_drain(
    GameSession& session,
    const std::shared_ptr<LocalTransportRuntime>& runtime)
{
    if (runtime == nullptr)
        return false;

    if (session.myscreen_ == nullptr)
        return false;

    if (!runtime->awaiting_level_transition &&
        (runtime->display_session_finished ||
         session.myscreen_->world().end != 0))
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "local_transport_shadow", "finish_tick_display_finished"));
        runtime->display_session_finished = true;
        session.myscreen_->world().end = 1;
        return false;
    }

    if (runtime->authoritative_mode() &&
        !runtime->awaiting_level_transition)
    {
        // Every client in this runtime is an in-process transport living in
        // THIS process (the networked case only ever holds the loopback
        // display client here — remote peers are not runtime clients). Such
        // peers must never be liveness- or desync-disconnected: closing one
        // makes the next local send throw ("InProcessTransport peer N is not
        // connected"). Marked idempotently every step so mid-game ADD PLAYER
        // seats are covered the moment they exist.
        for (const LocalTransportClient& local_client : runtime->clients)
            runtime->server->mark_peer_local(local_client.server_peer_id);
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "local_transport_shadow", "finish_tick_authoritative_step"));
        // Single-thread authoritative order:
        //  3-9. Install the server session/context and run one authoritative step.
        auto server_scope = runtime->server_session->activate(/*swap_render=*/false);
        GameplayContextGuard server_gameplay_scope(
            &runtime->server_session->game_);
        ScopedSessionGameplayActivation gameplay_active(*runtime->server_session);
        runtime->server->step();
    }

    // 10-14. Restore the display session, then drain snapshots/events into the
    // client mirror before rendering.
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "local_transport_shadow", "finish_tick_client_drain_begin"));
        auto client_scope = session.activate(/*swap_render=*/false);
        GameplayContextGuard client_gameplay_scope(&session.game_);
        for (auto& client : runtime->clients)
        {
            poll_local_transport_client(
                *session.myscreen_,
                client,
                og::sim::MAX_INBOUND_MESSAGES_PER_TICK);
            if (client.game_client &&
                client.game_client->messages_drained_last_call() >=
                    og::sim::MAX_INBOUND_MESSAGES_PER_TICK)
            {
                og::runtime::emit_runtime_trace(
                    og::runtime::make_runtime_trace_record(
                        "local_transport_shadow",
                        "shadow_inbound_overflow"));
            }
            if (!runtime->awaiting_level_transition &&
                (runtime->display_session_finished ||
                 session.myscreen_->world().end != 0))
            {
                og::runtime::emit_runtime_trace(
                    og::runtime::make_runtime_trace_record(
                        "local_transport_shadow", "finish_tick_client_drain_end"));
                runtime->display_session_finished = true;
                session.myscreen_->world().end = 1;
                return false;
            }
        }
        if (runtime->display_client_index < runtime->clients.size() &&
            runtime->clients[runtime->display_client_index].game_client)
        {
            og::sim::GameClient& display_client =
                *runtime->clients[runtime->display_client_index].game_client;
            render_pause_overlay(
                *session.myscreen_,
                display_client,
                pause_owned_by_remote_peer(*runtime, display_client));
        }
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "local_transport_shadow", "finish_tick_client_drain_end"));
    }
    return true;
}

} // namespace

void local_transport_shadow_finish_tick(GameSession& session)
{
    (void)local_transport_shadow_step_and_drain(
        session, session.local_transport_runtime_);
}

bool local_transport_shadow_pump_paused(GameSession& session)
{
    return local_transport_shadow_step_and_drain(
        session, session.local_transport_runtime_);
}

void local_transport_shadow_request_pause_keepalive(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    og::sim::GameClient* const display_client =
        runtime != nullptr ? runtime->display_client() : nullptr;
    if (display_client == nullptr)
        return;

    // Deliberately NOT a toggle: the pause menu sends this both to open its
    // pause and to keep it alive. Server-side, a repeat from the pause's
    // owner refreshes the auto-resume deadline instead of being rejected.
    display_client->send_pause_request();
}

std::string local_transport_shadow_remote_pause_owner(GameSession& session)
{
    const auto runtime = session.local_transport_runtime_;
    const og::sim::GameClient* const display_client =
        runtime != nullptr ? runtime->display_client() : nullptr;
    if (display_client == nullptr ||
        !display_client->baseline().has_value() ||
        !display_client->baseline()->paused)
    {
        return {};
    }
    if (!pause_owned_by_remote_peer(*runtime, *display_client))
        return {};

    const auto& broadcast = display_client->last_pause_broadcast();
    if (broadcast.has_value() && !broadcast->player_name.empty())
        return broadcast->player_name;
    return "P" +
        std::to_string(
            static_cast<int>(
                display_client->baseline()->pause_player_index) +
            1);
}

} // namespace og::runtime
