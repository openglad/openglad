/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Extracted from src/glad.cpp so non-app binaries (tests/tools) can link
 * gameplay entrypoints without pulling in main().
 */

#include <openglad/platform/game_loop.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/interface/replay_runtime.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/picker_lobby_network_runtime.h>

#include <algorithm>
#include <iterator>
#include <optional>

// theprefs is now a macro defined in view.h (via game_session.h)

// g_frame_state removed — now lives in GameSession::frame_state_
static inline GameLoopFrameState& g_frame_state() {
    return og::runtime::current_session->frame_state_;
}

#ifdef __EMSCRIPTEN__
#include <openglad/platform/emscripten/web_runtime_diagnostics.h>
#endif

#ifdef TESTING
// Remove exits so levels can auto-complete when all enemies die.
bool g_test_remove_exits = false;
#endif

namespace
{

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
    // v8: the deploy flag rides the lobby SLOT, not the guy payload; roster
    // assembly only materializes deployed slots, so mark the guy explicitly.
    result->deployed = true;
    // GTL v16 campaign_tag deliberately stays 0 here: this is a MISSION
    // roster, whose slot_index is a compacted combined-roster position rather
    // than any machine's private save slot, so there is no record to read the
    // tag off. Assignment is menu-authored and nothing in a level touches it;
    // merge_owned_guys_from re-stamps every survivor from the disk slot on the
    // way back (see guy.h).
    return result;
}

void apply_lobby_game_start_config(
    screen& current_screen,
    const og::ui::PickerLobbyGameStartConfig& lobby_config)
{
    SaveData& save = current_screen.save_data;
    const og::sim::LobbySaveDataEquivalent& config_save =
        lobby_config.save_data;

    save.current_campaign = config_save.current_campaign.empty()
        ? std::string("gladiator")
        : config_save.current_campaign;
    save.scen_num = config_save.scen_num > 0 ? config_save.scen_num : 1;
    save.current_levels[save.current_campaign] = save.scen_num;
    save.numplayers = config_save.numplayers;
    save.allied_mode = static_cast<short>(config_save.allied_mode);
    save.ctf_team_count = static_cast<short>(config_save.ctf_team_count);
    save.ctf_capture_limit = static_cast<short>(config_save.ctf_capture_limit);
    save.ctf_respawn_ticks = static_cast<short>(config_save.ctf_respawn_ticks);
    save.ctf_strip_scenario_troops =
        static_cast<short>(config_save.ctf_strip_scenario_troops);
    save.respawn_mode = static_cast<short>(config_save.respawn_mode);
    save.generator_rate = static_cast<short>(config_save.generator_rate);
    save.keep_fallen_heroes = static_cast<short>(config_save.keep_fallen_heroes);
    save.cross_control = static_cast<short>(config_save.cross_control);
    save.infinite_gold = static_cast<short>(config_save.infinite_gold);
    // The old network handoff collapsed every allied-mode client onto team 0.
    // allied_mode remains in the save/replay format, but Base Camp's explicit
    // per-seat assignment is now authoritative in every mode.
    save.my_team = lobby_config.my_team >= 0 &&
            lobby_config.my_team < MAX_PLAYERS
        ? lobby_config.my_team
        : 0;

    for (auto& member : save.team_list)
        member.reset();
    save.team_size = 0;

    for (const auto& slot : config_save.team_list)
    {
        if (slot.slot_index >= save.team_list.size())
            continue;

        save.team_list[slot.slot_index] =
            make_guy_from_lobby_character(slot.character);
        save.team_list[slot.slot_index]->deployed = slot.deployed;
        save.team_list[slot.slot_index]->owner_player_index =
            slot.owner_player_index;
        save.team_list[slot.slot_index]->owner_save_slot = slot.owner_save_slot;
        if (static_cast<std::size_t>(save.team_size) < save.team_list.size())
            ++save.team_size;  // cap so team_size can never exceed the fixed team_list
    }

    if (og::runtime::current_session != nullptr)
    {
        og::runtime::current_session->current_difficulty_ =
            static_cast<std::int32_t>(lobby_config.difficulty);
        og::runtime::current_session->networked_session_ =
            lobby_config.is_networked;
        og::runtime::current_session->isolated_company_session_ =
            !lobby_config.is_networked;
        og::runtime::current_session->own_player_indices_ =
            lobby_config.local_player_indices;
        if (og::runtime::current_session->own_player_indices_.empty() &&
            lobby_config.local_player_index != 0xffu)
        {
            // Single-seat compatibility: older config producers fill only the
            // seat-0 index.
            og::runtime::current_session->own_player_indices_ = {
                lobby_config.local_player_index};
        }
    }

    // A lobby start config is a mission roster, not the private company. For
    // network play it is the combined roster; for local play it is an isolated
    // full-company copy whose deployed members enter regardless of player-seat
    // color. Seed both into the transient slot so launching or aborting a
    // mission can never replace the private company. Wins merge owned progress
    // back into the private save.
    const std::string seed_slot = "netsession";
    if (!save.save(seed_slot))
        LogError("glad_init_lobby_save_failed slot={} reason=write_failed\n",
                 seed_slot);
}

void apply_lobby_seat_assignments(
    screen& current_screen,
    const og::ui::PickerLobbyGameStartConfig& lobby_config)
{
    const short numviews = std::min<short>(
        current_screen.numviews,
        static_cast<short>(std::size(current_screen.viewob)));
    if (numviews <= 0 ||
        lobby_config.local_seat_teams.size() <
            static_cast<std::size_t>(numviews))
    {
        return;
    }

    // load_saved_game performs the legacy distinct-color assignment first.
    // A lobby has an explicit seat map, so release those provisional claims
    // before applying it. In local Together mode every entry is Player 1's
    // preferred team; sequential find_next_control calls then claim distinct
    // unclaimed heroes from that shared pool.
    for (short view_index = 0; view_index < numviews; ++view_index)
    {
        viewscreen* const view = current_screen.viewob[view_index].get();
        if (view == nullptr || view->control == nullptr)
            continue;
        if (view->control->user() == view_index)
        {
            view->control->set_user(-1);
            view->control->restore_act_type();
        }
        view->control = nullptr;
    }

    const bool spectator = current_screen.save_data.numplayers == 0;
    for (short view_index = 0; view_index < numviews; ++view_index)
    {
        viewscreen* const view = current_screen.viewob[view_index].get();
        if (view == nullptr)
            continue;

        view->my_team = lobby_config.local_seat_teams[
            static_cast<std::size_t>(view_index)];
        view->control = view->find_next_control();
        if (spectator || view->control == nullptr)
            continue;

        if (view->control->user() == -1)
        {
            view->control->set_user(static_cast<signed char>(view_index));
            view->control->set_act_type(ACT_CONTROL);
            view->control->stats()->clear_command();
        }
        if (view_index == 0)
            current_screen.world().control_hp =
                view->control->stats()->hitpoints();
    }
}

} // namespace

// The two global settings GAME SETTINGS / GRAPHICS FX took over from the
// retired in-game options menu (docs/pause-menu-design.md §7.3), applied to
// the world every mission starts with: the sim cadence (which the old menu
// changed live and never persisted) and the lava/water palette rotation
// (which the screen constructor hardcoded on). Namespace scope so the
// seeding can be driven without standing up a whole level.
void apply_level_start_global_settings(screen& game_screen)
{
    game_screen.world().timer_wait = static_cast<signed char>(
        og::ui::parse_timer_wait(cfg.get_setting("gameplay", "timer_wait")));
    // Absent key => ON. This setting's identity state is the classic
    // rotation, so a config that never mentioned it (a process that never
    // ran load_settings) must still cycle, unlike every other effects key.
    const std::string cycling = cfg.get_setting("effects", "color_cycling");
    game_screen.cyclemode = cycling.empty() || cycling == "on" ? 1 : 0;
}

void glad_init(bool preserve_frame_timing,
               const og::ui::PickerLobbyGameStartConfig* lobby_config);

// Initialize the game for playing (called before game loop starts).
void glad_init(bool preserve_frame_timing)
{
    const std::optional<og::ui::PickerLobbyGameStartConfig> lobby_config =
        picker_lobby_consume_game_start_config();
    glad_init(preserve_frame_timing,
              lobby_config.has_value() ? &*lobby_config : nullptr);
}

void glad_init(bool preserve_frame_timing,
               const og::ui::PickerLobbyGameStartConfig* lobby_config)
{
    screen* current_screen = og::runtime::current_session->myscreen_;
    if (current_screen == nullptr)
    {
        LogError("glad_init_failed reason=missing_screen\n");
        return;
    }
    clear_keyboard();
	current_screen->set_active_canvas(current_screen->last_presented_canvas());
    current_screen->fadeblack(0);
	// Fade the currently visible picker/UI frame out first. Then route every
	// gameplay clear, load, redraw and fade-in to World; on Emscripten this
	// must happen here because there is no native gameplay canvas scope.
	current_screen->set_active_canvas(CanvasTarget::World);
    current_screen->clearbuffer();

    if (lobby_config != nullptr)
        apply_lobby_game_start_config(*current_screen, *lobby_config);
    else
        og::runtime::current_session->isolated_company_session_ = false;

    // Load the default saved-game, or the lobby-supplied in-memory config.
    const std::string default_slot = lobby_config != nullptr
        ? std::string()
        : og::data::active_company_slot();
    load_saved_game(default_slot.c_str(), current_screen);
    if (lobby_config != nullptr)
        apply_lobby_seat_assignments(*current_screen, *lobby_config);
#ifdef __EMSCRIPTEN__
    og::platform::web::finalize_jitter_capture_profile_after_load(
        *current_screen);
#endif
    const short safe_numviews = std::min<short>(
        current_screen->numviews, static_cast<short>(std::size(current_screen->viewob)));
    for (short view_index = 0; view_index < safe_numviews; ++view_index)
    {
        if (current_screen->viewob[view_index] != nullptr)
            current_screen->viewob[view_index]->clear_text();
    }
    current_screen->world().tick_count_ = 0;
    current_screen->world().reset_level_progress();
    current_screen->world().clear_removed_entity_ids();
    current_screen->world().clear_grid_dirty_tiles();
    // Seeded before the replay recorder is armed below, so the header
    // captures the speed the mission actually runs at.
    apply_level_start_global_settings(*current_screen);
    if (current_game != nullptr && current_game->sim_events != nullptr)
        current_game->sim_events->clear();

#ifdef TESTING
    if (g_test_remove_exits) {
        for (auto& uptr : current_screen->world().fxlist) {
            walker* w = uptr.get();
            if (w && w->query_order() == Order::Treasure && w->family() == FAMILY_EXIT)
                w->set_dead(1);
        }
    }
#endif

    // This will update the 'control' so the screen centers on our guy
    current_screen->continuous_input();

    current_screen->redraw();
    current_screen->fadeblack(1);

    current_screen->redraw();
    current_screen->refresh();
    read_scenario(current_screen);
    og::runtime::GameSession* const gameplay_session =
        og::runtime::current_game_session;
    if (gameplay_session == nullptr)
    {
        LogError("glad_init_failed reason=missing_game_session\n");
        return;
    }
    if (!og::platform::install_picker_lobby_gameplay_runtime(
            og::ui::active_picker_lobby_client(),
            *gameplay_session,
            *current_screen))
    {
        og::runtime::reset_local_transport_shadow(
            *gameplay_session, *current_screen);
    }
    current_screen->redrawme = 1;
    current_screen->framecount = 0;
    current_screen->timerstart = static_cast<Uint32>(query_timer_control());
    og::runtime::begin_replay_recording(*current_screen);

    GameLoopFrameState fresh_state;
    if (preserve_frame_timing) {
        fresh_state.last_frame_time = g_frame_state().last_frame_time;
        fresh_state.accumulated_time = g_frame_state().accumulated_time;
    }
    fresh_state.cycletime = 3;
    g_frame_state() = fresh_state;
}

void glad_main(Sint32 playermode)
{
    screen* current_screen = og::runtime::current_session->myscreen_;
    if (current_screen == nullptr)
    {
        LogError("glad_main_failed mode={} reason=missing_screen\n", playermode);
        return;
    }

    struct GameplayActiveScope final {
        GameplayActiveScope()
        {
            if (og::runtime::current_session)
                og::runtime::current_session->gameplay_active_ = true;
        }
        ~GameplayActiveScope()
        {
            if (og::runtime::current_session)
                og::runtime::current_session->gameplay_active_ = false;
        }
    } gameplay_scope;

    glad_init(false);
	// glad_init routes to World after fading the picker. Keep the last gameplay
	// or results frame active when returning so the caller can fade exactly
	// what was most recently presented before it selects UI for the picker.

#ifdef __EMSCRIPTEN__
    // For Emscripten, the unified main loop in main() handles game_frame() calls
    // via the state machine. We just need to initialize - don't start another loop.
#else
    og::runtime::run_native_game_loop(
        *current_screen, g_frame_state(), GameLoopDeps{});

    if (og::runtime::current_game_session != nullptr)
        og::runtime::clear_local_transport_shadow(
            *og::runtime::current_game_session);
    clear_keyboard();
    // Delete all of our current information and abort ..
    current_screen->world().delete_objects();
#endif
}

// remaining_foes (LevelRuntimeData overload) is in level_data.cpp.
// This screen* overload delegates to it for backward compatibility.
short remaining_foes(LevelRuntimeData& level, walker* myguy);

// remaining_foes returns # of livings left not friendly to myguy
short remaining_foes(screen* s, walker* myguy)
{
    return remaining_foes(s->level_runtime_data(), myguy);
}

// remaining_team returns # of livings left on team myteam.
short remaining_team(screen* s, char myteam)
{
    short myfoes = 0;

    const auto& foelist = s->world().oblist;
    for (auto& uptr : foelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() &&
            (w->query_order() == Order::Living) &&
            (myteam == w->team_num()))
            myfoes++;
    }

    return myfoes;
}
