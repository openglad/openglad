# --------------------------------------------------------------------------
# Component source lists.
#
# Included by the top-level CMakeLists.txt; runs in that scope.
# --------------------------------------------------------------------------

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

set(OG_NETWORK_SOURCES)
if(NOT EMSCRIPTEN)
    list(APPEND OG_NETWORK_SOURCES
        ${SRC_DIR}/platform/sdl/net_transport_websocket_client.cpp
        ${SRC_DIR}/platform/sdl/net_transport_websocket_server.cpp
        ${SRC_DIR}/platform/sdl/net_transport_relay_ws.cpp
    )
endif()

# Deterministic Lua scripting VM (class packs / level scripts). Shared by the
# SDL game, the component libraries, and the headless clients.
set(OG_SCRIPT_SOURCES
    ${SRC_DIR}/gameplay/script/script_host.cpp
    ${SRC_DIR}/gameplay/script/script_coverage.cpp
    ${SRC_DIR}/gameplay/script/pack_scripts.cpp
    ${SRC_DIR}/gameplay/script/world_scripts.cpp
    ${SRC_DIR}/gameplay/script/bindings_entity.cpp
    ${SRC_DIR}/gameplay/script/campaign_hooks.cpp
    ${SRC_DIR}/gameplay/script/family_decl.cpp
)

# Component source lists. These are the single source of truth: each component
# list is compiled into the matching og_* static library, and
# GAME_SOURCES_NO_MAIN below is DERIVED from them, so a new file is added in
# exactly one place.
#
# A few components are split into named sub-lists because the SDL-free targets
# (openglad_text, openglad_server, openglad_curses) link a strict subset of a
# component rather than the whole thing. Those sub-lists are the only reason
# the granularity exists — compose, never duplicate.
#
# Dependencies flow inward: core <- gameplay <- resources <- interface <-
# platform_sdl. See docs/ARCHITECTURE.md ("Dependency Direction Rules").

set(OG_CORE_SOURCES
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/core/frame_pacing.cpp
    ${SRC_DIR}/core/frame_rate_config.cpp
    ${SRC_DIR}/core/runtime_trace.cpp
    ${SRC_DIR}/core/text_wrap.cpp
    ${SRC_DIR}/core/util.cpp
    ${SRC_DIR}/core/weather.cpp
)

# gameplay: deterministic tick loop, snapshots, and the SDL-free networking core.
set(OG_GAMEPLAY_SIM_SOURCES
    ${SRC_DIR}/gameplay/sim_entity.cpp
    ${SRC_DIR}/gameplay/sim_event_log.cpp
    ${SRC_DIR}/gameplay/game_server.cpp
    ${SRC_DIR}/gameplay/lobby_server.cpp
    ${SRC_DIR}/gameplay/game_client.cpp
    ${SRC_DIR}/gameplay/net_transport.cpp
    ${SRC_DIR}/gameplay/net_transport_inprocess.cpp
    ${SRC_DIR}/gameplay/pack_transfer.cpp
    ${SRC_DIR}/gameplay/world_snapshot.cpp
    ${SRC_DIR}/gameplay/input_state_net.cpp
    ${SRC_DIR}/gameplay/replay.cpp
    ${SRC_DIR}/gameplay/scenario_strip.cpp
)

# gameplay: the walker family and its registries.
set(OG_GAMEPLAY_ENTITY_SOURCES
    ${SRC_DIR}/gameplay/effect.cpp
    ${SRC_DIR}/gameplay/family_registry.cpp
    ${SRC_DIR}/gameplay/families/family_string_ids.cpp
    ${SRC_DIR}/gameplay/weapon_family_registry.cpp
    ${SRC_DIR}/gameplay/effect_family_registry.cpp
    ${SRC_DIR}/gameplay/treasure_family_registry.cpp
    ${SRC_DIR}/gameplay/generator_family_registry.cpp
    ${SRC_DIR}/gameplay/guy.cpp
    ${SRC_DIR}/gameplay/living.cpp
    ${SRC_DIR}/gameplay/obmap.cpp
    ${SRC_DIR}/gameplay/treasure.cpp
    ${SRC_DIR}/gameplay/walker.cpp
    ${SRC_DIR}/gameplay/walker_combat.cpp
    ${SRC_DIR}/gameplay/walker_movement.cpp
    ${SRC_DIR}/gameplay/walker_pathing.cpp
    ${SRC_DIR}/gameplay/walker_specials.cpp
    ${SRC_DIR}/gameplay/weap.cpp
)

# gameplay: GameWorld and the per-tick services around it.
set(OG_GAMEPLAY_WORLD_SOURCES
    ${SRC_DIR}/gameplay/astar.cpp
    ${SRC_DIR}/gameplay/gameplay_context.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/gameplay/game_world_weather.cpp
    ${SRC_DIR}/gameplay/respawn/respawn.cpp
    ${SRC_DIR}/gameplay/mode/mode_tick.cpp
    ${SRC_DIR}/gameplay/sim_input_handler.cpp
    ${SRC_DIR}/gameplay/sim_control_policy.cpp
    ${SRC_DIR}/gameplay/smooth.cpp
    ${SRC_DIR}/gameplay/stats.cpp
)

# resources: level/save/campaign serialization and the sprite asset readers.
set(OG_RESOURCES_DATA_SOURCES
    ${SRC_DIR}/resources/gparser.cpp
    ${SRC_DIR}/resources/gloader.cpp
    ${SRC_DIR}/resources/campaign_metadata.cpp
    ${SRC_DIR}/resources/game_mode.cpp
    ${SRC_DIR}/resources/progression.cpp
    ${SRC_DIR}/resources/win_shares.cpp
    ${SRC_DIR}/resources/tower_progression.cpp
    ${SRC_DIR}/resources/mapgen/tower_floor_gen.cpp
    ${SRC_DIR}/resources/level_file_io.cpp
    ${SRC_DIR}/resources/our_palette.cpp
    ${SRC_DIR}/resources/pixie_data.cpp
    ${SRC_DIR}/resources/save_data.cpp
    ${SRC_DIR}/resources/company.cpp
    ${SRC_DIR}/resources/packs.cpp
    ${SRC_DIR}/resources/pack_transfer_io.cpp
)

# resources: SDL-free filesystem, archive and yaml layer.
set(OG_RESOURCES_IO_SOURCES
    ${SRC_DIR}/resources/io/platform_io_common.cpp
    ${SRC_DIR}/resources/io/filesystem.cpp
    ${SRC_DIR}/resources/io/physfs_api.cpp
    ${SRC_DIR}/resources/io/zip_api.cpp
    ${SRC_DIR}/resources/campaign_yaml.cpp
    ${SRC_DIR}/resources/io/og_file.cpp
)

set(OG_GAMEPLAY_COMPONENT_SOURCES
    ${OG_GAMEPLAY_SIM_SOURCES}
    ${OG_GAMEPLAY_ENTITY_SOURCES}
    ${OG_GAMEPLAY_WORLD_SOURCES}
    ${OG_SCRIPT_SOURCES}
    ${SRC_DIR}/gameplay/net_transport_multiplex.cpp
    ${SRC_DIR}/gameplay/mapgen/builders.cpp
    ${SRC_DIR}/gameplay/mapgen/audits.cpp
)

set(OG_RESOURCES_COMPONENT_SOURCES
    ${OG_RESOURCES_DATA_SOURCES}
    ${OG_RESOURCES_IO_SOURCES}
)

set(OG_INTERFACE_COMPONENT_SOURCES
    ${SRC_DIR}/interface/cheat_handler.cpp
    ${SRC_DIR}/interface/fps_overlay.cpp
    ${SRC_DIR}/interface/guy_create.cpp
    ${SRC_DIR}/interface/input/input.cpp
    ${SRC_DIR}/interface/input/input_mappings.cpp
    ${SRC_DIR}/interface/input/input_state.cpp
    ${SRC_DIR}/interface/level_runtime_data.cpp
    ${SRC_DIR}/interface/platform_bridge.cpp
    ${SRC_DIR}/interface/render/depth_fx.cpp
    ${SRC_DIR}/interface/render/effects.cpp
    ${SRC_DIR}/interface/render/graphlib.cpp
    ${SRC_DIR}/interface/render/obmap_debug_draw.cpp
    ${SRC_DIR}/interface/render/pal32.cpp
    ${SRC_DIR}/interface/render/pixie.cpp
    ${SRC_DIR}/interface/render/pixien.cpp
    ${SRC_DIR}/interface/render/radar.cpp
    ${SRC_DIR}/interface/render/sdl_level_render.cpp
    ${SRC_DIR}/interface/render/text.cpp
    ${SRC_DIR}/interface/render/view.cpp
    ${SRC_DIR}/interface/render/walker_draw.cpp
    ${SRC_DIR}/interface/replay_runtime.cpp
    ${SRC_DIR}/interface/screen.cpp
    ${SRC_DIR}/interface/score_panel.cpp
    ${SRC_DIR}/interface/sdl_context_services.cpp
    ${SRC_DIR}/interface/session_state.cpp
    ${SRC_DIR}/interface/ui/button.cpp
    ${SRC_DIR}/interface/ui/campaign_picker.cpp
    ${SRC_DIR}/interface/ui/cloud_save_client.cpp
    ${SRC_DIR}/interface/ui/help.cpp
    ${SRC_DIR}/interface/ui/intro.cpp
    ${SRC_DIR}/interface/ui/level_editor.cpp
    ${SRC_DIR}/interface/ui/level_editor_file_ops.cpp
    ${SRC_DIR}/interface/ui/level_editor_tools.cpp
    ${SRC_DIR}/interface/ui/level_editor_ui.cpp
    ${SRC_DIR}/interface/ui/level_picker.cpp
    ${SRC_DIR}/interface/ui/menu_binding.cpp
    ${SRC_DIR}/interface/ui/menu_model.cpp
    ${SRC_DIR}/interface/ui/input_cycler.cpp
    ${SRC_DIR}/interface/ui/menu_screen_runner.cpp
    ${SRC_DIR}/interface/ui/menu_screen_specs.cpp
    ${SRC_DIR}/interface/ui/pause_menu.cpp
    ${SRC_DIR}/interface/ui/picker.cpp
    ${SRC_DIR}/interface/ui/picker_accessible_levels.cpp
    ${SRC_DIR}/interface/ui/picker_common.cpp
    ${SRC_DIR}/interface/ui/picker_dialogs.cpp
    ${SRC_DIR}/interface/ui/picker_input.cpp
    ${SRC_DIR}/interface/ui/picker_lobby_client.cpp
    ${SRC_DIR}/interface/ui/picker_lobby_network_client.cpp
    ${SRC_DIR}/interface/ui/picker_main_menu.cpp
    ${SRC_DIR}/interface/ui/picker_state.cpp
    ${SRC_DIR}/interface/ui/picker_team_build.cpp
    ${SRC_DIR}/interface/ui/results_screen.cpp
    ${SRC_DIR}/interface/ui/scroll_view_layout.cpp
    ${SRC_DIR}/interface/ui/terminal_menu_model.cpp
    ${SRC_DIR}/interface/walker_render_bridge.cpp
)

set(OG_PLATFORM_SDL_COMPONENT_SOURCES
    ${SRC_DIR}/platform/game_context.cpp
    ${SRC_DIR}/platform/sdl/game.cpp
    ${SRC_DIR}/platform/sdl/game_loop.cpp
    ${SRC_DIR}/platform/sdl/game_session.cpp
    ${SRC_DIR}/platform/sdl/glad_gameplay.cpp
    ${SRC_DIR}/platform/sdl/input_event_bridge.cpp
    ${SRC_DIR}/platform/sdl/local_transport_shadow.cpp
    ${SRC_DIR}/platform/sdl/native_input.cpp
    ${SRC_DIR}/platform/sdl/physfs_rwops_bridge.cpp
    ${SRC_DIR}/platform/sdl/picker_lobby_network_client.cpp
    ${SRC_DIR}/platform/sdl/sai2x.cpp
    ${SRC_DIR}/platform/sdl/screen_lifecycle.cpp
    ${SRC_DIR}/platform/sdl/sound.cpp
    ${SRC_DIR}/platform/sdl/video_sdl.cpp
    ${SRC_DIR}/resources/platform_io.cpp
)

# Every game translation unit except main(). Consumed by the test aggregate and
# by og_game_web (Emscripten links one library instead of the component set).
set(GAME_SOURCES_NO_MAIN
    ${OG_CORE_SOURCES}
    ${OG_GAMEPLAY_COMPONENT_SOURCES}
    ${OG_RESOURCES_COMPONENT_SOURCES}
    ${OG_INTERFACE_COMPONENT_SOURCES}
    ${OG_PLATFORM_SDL_COMPONENT_SOURCES}
    ${OG_NETWORK_SOURCES}
)
if(EMSCRIPTEN)
    list(APPEND GAME_SOURCES_NO_MAIN
        ${SRC_DIR}/platform/emscripten/web_runtime_diagnostics.cpp
        ${SRC_DIR}/platform/emscripten/web_touch_bridge.cpp
        ${SRC_DIR}/platform/emscripten/net_transport_emscripten_ws.cpp
        ${SRC_DIR}/platform/emscripten/net_transport_relay_ws.cpp
    )
endif()
