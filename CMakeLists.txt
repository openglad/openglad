cmake_minimum_required(VERSION 3.25)
project(OpenGlad VERSION 1.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckCXXSourceCompiles)
include(FetchContent)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0" AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for legacy fetched dependencies")
endif()

# Target C++20 (C++23 when toolchains catch up)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C standard for external C libraries
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

check_cxx_source_compiles([=[
    #include <format>
    #include <string>

    int main()
    {
        const std::string value = std::format("{} {}", "OpenGlad", 22);
        return value == "OpenGlad 22" ? 0 : 1;
    }
]=] OPENGLAD_HAS_WORKING_STD_FORMAT)

# Build options
option(BUILD_TESTING "Build the test suite" OFF)
option(BUILD_EDITOR "Build the scenario editor (openscen)" ON)
option(OPENGLAD_INSTALL_ASSETS "Install OpenGlad runtime asset directories" ON)
option(ENABLE_COVERAGE "Enable gcov/lcov coverage instrumentation (tests only)" OFF)
option(ENABLE_SANITIZERS "Enable compiler sanitizers for project targets" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis during C/C++ compilation (if found)" OFF)
option(ENABLE_CPPCHECK "Enable cppcheck analysis during C/C++ compilation (if found)" OFF)
option(VALIDATE_SERIALIZATION "Round-trip in-process transport typed messages through serializers" OFF)
option(OPENGLAD_USE_SYSTEM_DEPS "Prefer system/package-manager dependencies over fetched upstream sources" ON)
option(OPENGLAD_REQUIRE_SYSTEM_DEPS "Fail if OPENGLAD_USE_SYSTEM_DEPS cannot find a dependency package" OFF)
option(OPENGLAD_FETCH_DEPS "Fetch missing upstream dependencies with CMake FetchContent" ON)
option(OPENGLAD_FETCH_ZLIB "Fetch zlib from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_LIBYAML "Fetch libyaml from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_LIBZIP "Fetch libzip from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_PHYSFS "Fetch PhysFS from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_LODEPNG "Fetch lodepng from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_IXWEBSOCKET "Fetch IXWebSocket from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_SDL3 "Fetch SDL3 from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
# Lua is ALWAYS vendored (independent of OPENGLAD_FETCH_DEPS): the scripting
# VM must be the pinned version compiled as C++ on every platform — a system
# Lua (C linkage, longjmp error unwinding) would break binding-layer RAII and
# cross-peer determinism. Offline builds pre-populate FETCHCONTENT_SOURCE_DIR_LUA.
option(OPENGLAD_FETCH_LUA "Fetch Lua from upstream (always vendored; no system fallback)" ON)
option(OPENGLAD_USE_PACKAGED_IXWEBSOCKET "Prefer an installed IXWebSocket package over fetching the pinned upstream commit" ON)
set(OPENGLAD_ZLIB_GIT_REPOSITORY "https://github.com/madler/zlib.git" CACHE STRING "zlib upstream repository")
set(OPENGLAD_ZLIB_GIT_TAG "v1.3.2" CACHE STRING "zlib upstream tag/commit to use when fetching")
set(OPENGLAD_LIBYAML_GIT_REPOSITORY "https://github.com/yaml/libyaml.git" CACHE STRING "libyaml upstream repository")
set(OPENGLAD_LIBYAML_GIT_TAG "0.2.5" CACHE STRING "libyaml upstream tag/commit to use when fetching")
set(OPENGLAD_LIBZIP_GIT_REPOSITORY "https://github.com/nih-at/libzip.git" CACHE STRING "libzip upstream repository")
set(OPENGLAD_LIBZIP_GIT_TAG "v1.11.4" CACHE STRING "libzip upstream tag/commit to use when fetching")
set(OPENGLAD_PHYSFS_GIT_REPOSITORY "https://github.com/icculus/physfs.git" CACHE STRING "PhysFS upstream repository")
set(OPENGLAD_PHYSFS_GIT_TAG "release-3.2.0" CACHE STRING "PhysFS upstream tag/commit to use when fetching")
set(OPENGLAD_LODEPNG_GIT_REPOSITORY "https://github.com/lvandeve/lodepng.git" CACHE STRING "lodepng upstream repository")
set(OPENGLAD_LODEPNG_GIT_TAG "ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a" CACHE STRING "lodepng upstream commit to use when fetching")
set(OPENGLAD_IXWEBSOCKET_GIT_REPOSITORY "https://github.com/machinezone/IXWebSocket.git" CACHE STRING "IXWebSocket upstream repository")
set(OPENGLAD_IXWEBSOCKET_GIT_TAG "64fae7676bd8fe31f7cb4bcde7a6841892dad65e" CACHE STRING "IXWebSocket upstream commit to use when fetching")
set(OPENGLAD_SDL3_GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git" CACHE STRING "SDL3 upstream repository")
set(OPENGLAD_SDL3_GIT_TAG "release-3.4.8" CACHE STRING "SDL3 upstream tag/commit to use when fetching")
set(OPENGLAD_LUA_GIT_REPOSITORY "https://github.com/lua/lua.git" CACHE STRING "Lua upstream repository")
set(OPENGLAD_LUA_GIT_TAG "v5.4.8" CACHE STRING "Lua upstream tag/commit to use when fetching")
set(SANITIZERS "address;undefined" CACHE STRING "Sanitizers to enable when ENABLE_SANITIZERS is ON")

# Coverage runs are only meaningful if the tests are built and executed.
# The repo's documented coverage pipeline configures with ENABLE_COVERAGE=ON
# and then invokes `ctest`, so ensure BUILD_TESTING is enabled in that mode.
if(ENABLE_COVERAGE AND NOT BUILD_TESTING)
    set(BUILD_TESTING ON CACHE BOOL "Build the test suite" FORCE)
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()
endif()

if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES cppcheck)
    if(CPPCHECK_EXE)
        set(CMAKE_C_CPPCHECK "${CPPCHECK_EXE};--std=c11")
        set(CMAKE_CXX_CPPCHECK "${CPPCHECK_EXE};--std=c++20")
        message(STATUS "cppcheck enabled: ${CPPCHECK_EXE}")
    else()
        message(WARNING "ENABLE_CPPCHECK is ON, but cppcheck was not found")
    endif()
endif()

# --------------------------------------------------------------------------
# Tooling targets for warnings/sanitizers (project sources only)
# --------------------------------------------------------------------------

add_library(project_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
        $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
    )
elseif(MSVC)
    target_compile_options(project_warnings INTERFACE /W4 /permissive-)
endif()

add_library(project_sanitizers INTERFACE)
if(ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(EMSCRIPTEN)
            message(WARNING "ENABLE_SANITIZERS is ON but EMSCRIPTEN build does not enable native sanitizers")
        else()
            string(REPLACE ";" "," SANITIZER_LIST "${SANITIZERS}")
            target_compile_options(project_sanitizers INTERFACE -fno-omit-frame-pointer -fsanitize=${SANITIZER_LIST})
            target_link_options(project_sanitizers INTERFACE -fsanitize=${SANITIZER_LIST})
        endif()
    else()
        message(WARNING "ENABLE_SANITIZERS is ON but compiler is not GCC/Clang; ignoring sanitizer flags")
    endif()
endif()

# Emscripten disables C++ exception catching by default (-fignore-exceptions):
# any `throw` calls abort(), killing the wasm runtime and freezing the page.
# The game uses exceptions for recoverable errors (networking lobby/relay
# failures surface as popup dialogs), so enable JS-based exception handling
# for every target in the web build. Wasm-native EH (-fwasm-exceptions) is
# not an option here: it is incompatible with -sASYNCIFY, which the web build
# requires for its blocking menu loops and async relay fetches.
if(EMSCRIPTEN)
    add_compile_options(-fexceptions)
    add_link_options(-fexceptions)
endif()

# --------------------------------------------------------------------------
# Source file lists
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
    ${SRC_DIR}/gameplay/script/pack_scripts.cpp
    ${SRC_DIR}/gameplay/script/world_scripts.cpp
    ${SRC_DIR}/gameplay/script/bindings_entity.cpp
)

# Main game C++ sources
set(GAME_SOURCES
    ${SRC_DIR}/platform/sdl/glad.cpp
    ${OG_SCRIPT_SOURCES}
    ${SRC_DIR}/platform/sdl/game_loop.cpp
    ${SRC_DIR}/core/frame_pacing.cpp
    ${SRC_DIR}/core/frame_rate_config.cpp
    ${SRC_DIR}/platform/game_context.cpp
    ${SRC_DIR}/platform/sdl/game_session.cpp
    ${SRC_DIR}/platform/sdl/glad_gameplay.cpp
    ${SRC_DIR}/platform/sdl/local_transport_shadow.cpp
    ${SRC_DIR}/platform/sdl/picker_lobby_network_client.cpp
    ${OG_NETWORK_SOURCES}
    ${SRC_DIR}/interface/score_panel.cpp
    ${SRC_DIR}/interface/fps_overlay.cpp
    ${SRC_DIR}/platform/sdl/screen_lifecycle.cpp
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/core/runtime_trace.cpp
    ${SRC_DIR}/core/weather.cpp
    ${SRC_DIR}/interface/ui/button.cpp
    ${SRC_DIR}/gameplay/effect.cpp
    ${SRC_DIR}/gameplay/family_registry.cpp
    ${SRC_DIR}/gameplay/families/family_string_ids.cpp
    ${SRC_DIR}/gameplay/families/family_archer.cpp
    ${SRC_DIR}/gameplay/families/family_archmage.cpp
    ${SRC_DIR}/gameplay/families/family_barbarian.cpp
    ${SRC_DIR}/gameplay/families/family_big_orc.cpp
    ${SRC_DIR}/gameplay/families/family_cleric.cpp
    ${SRC_DIR}/gameplay/families/family_druid.cpp
    ${SRC_DIR}/gameplay/families/family_elf.cpp
    ${SRC_DIR}/gameplay/families/family_faerie.cpp
    ${SRC_DIR}/gameplay/families/family_fire_elemental.cpp
    ${SRC_DIR}/gameplay/families/family_ghost.cpp
    ${SRC_DIR}/gameplay/families/family_giant_skeleton.cpp
    ${SRC_DIR}/gameplay/families/family_golem.cpp
    ${SRC_DIR}/gameplay/families/family_mage.cpp
    ${SRC_DIR}/gameplay/families/family_orc.cpp
    ${SRC_DIR}/gameplay/families/family_skeleton.cpp
    ${SRC_DIR}/gameplay/families/family_slime.cpp
    ${SRC_DIR}/gameplay/families/family_soldier.cpp
    ${SRC_DIR}/gameplay/families/family_thief.cpp
    ${SRC_DIR}/gameplay/families/family_tower1.cpp
    ${SRC_DIR}/gameplay/weapon_family_registry.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_knife.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_rock.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_projectiles.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_wave.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_door.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_animate.cpp
    ${SRC_DIR}/gameplay/effect_family_registry.cpp
    ${SRC_DIR}/gameplay/families/effect_family_ghost_scare.cpp
    ${SRC_DIR}/gameplay/families/effect_family_shield.cpp
    ${SRC_DIR}/gameplay/families/effect_family_knife_back.cpp
    ${SRC_DIR}/gameplay/families/effect_family_cloud.cpp
    ${SRC_DIR}/gameplay/families/effect_family_chain.cpp
    ${SRC_DIR}/gameplay/families/effect_family_door_open.cpp
    ${SRC_DIR}/gameplay/families/effect_family_bomb.cpp
    ${SRC_DIR}/gameplay/treasure_family_registry.cpp
    ${SRC_DIR}/gameplay/families/treasure_family_consumables.cpp
    ${SRC_DIR}/gameplay/families/treasure_family_valuables.cpp
    ${SRC_DIR}/gameplay/families/treasure_family_navigation.cpp
    ${SRC_DIR}/gameplay/families/treasure_family_ctf.cpp
    ${SRC_DIR}/gameplay/generator_family_registry.cpp
    ${SRC_DIR}/platform/sdl/game.cpp
    ${SRC_DIR}/interface/guy_create.cpp
    ${SRC_DIR}/interface/render/graphlib.cpp
    ${SRC_DIR}/gameplay/guy.cpp
    ${SRC_DIR}/interface/ui/help.cpp
    ${SRC_DIR}/interface/input/input.cpp
    ${SRC_DIR}/interface/input/input_state.cpp
    ${SRC_DIR}/interface/ui/intro.cpp
    ${SRC_DIR}/gameplay/living.cpp
    ${SRC_DIR}/gameplay/obmap.cpp
    ${SRC_DIR}/interface/render/pal32.cpp
    ${SRC_DIR}/interface/ui/picker.cpp
    ${SRC_DIR}/interface/ui/picker_dialogs.cpp
    ${SRC_DIR}/interface/ui/picker_input.cpp
    ${SRC_DIR}/interface/ui/picker_main_menu.cpp
    ${SRC_DIR}/interface/ui/picker_accessible_levels.cpp
    ${SRC_DIR}/interface/ui/picker_team_build.cpp
    ${SRC_DIR}/interface/ui/menu_binding.cpp
    ${SRC_DIR}/interface/ui/menu_model.cpp
    ${SRC_DIR}/interface/ui/menu_screen_runner.cpp
    ${SRC_DIR}/interface/ui/menu_screen_specs.cpp
    ${SRC_DIR}/interface/ui/terminal_menu_model.cpp
    ${SRC_DIR}/interface/ui/picker_lobby_client.cpp
    ${SRC_DIR}/interface/ui/picker_lobby_network_client.cpp
    ${SRC_DIR}/interface/ui/picker_common.cpp
    ${SRC_DIR}/interface/ui/picker_state.cpp
    ${SRC_DIR}/interface/render/pixie.cpp
    ${SRC_DIR}/interface/render/pixien.cpp
    ${SRC_DIR}/interface/render/radar.cpp
    ${SRC_DIR}/interface/screen.cpp
    ${SRC_DIR}/interface/platform_bridge.cpp
    ${SRC_DIR}/gameplay/smooth.cpp
    ${SRC_DIR}/platform/sdl/sound.cpp
    ${SRC_DIR}/platform/sdl/native_input.cpp
    ${SRC_DIR}/gameplay/stats.cpp
    ${SRC_DIR}/interface/render/text.cpp
    ${SRC_DIR}/gameplay/treasure.cpp
    ${SRC_DIR}/platform/sdl/video_sdl.cpp
    ${SRC_DIR}/interface/render/view.cpp
    ${SRC_DIR}/interface/render/walker_draw.cpp
    ${SRC_DIR}/interface/render/effects.cpp
    ${SRC_DIR}/interface/render/depth_fx.cpp
    ${SRC_DIR}/interface/render/obmap_debug_draw.cpp
    ${SRC_DIR}/gameplay/walker.cpp
    ${SRC_DIR}/gameplay/walker_combat.cpp
    ${SRC_DIR}/gameplay/walker_movement.cpp
    ${SRC_DIR}/gameplay/walker_pathing.cpp
    ${SRC_DIR}/gameplay/walker_specials.cpp
	    ${SRC_DIR}/gameplay/weap.cpp
	    ${SRC_DIR}/platform/sdl/sai2x.cpp
		    ${SRC_DIR}/core/util.cpp
		    ${SRC_DIR}/resources/platform_io.cpp
            ${SRC_DIR}/platform/sdl/physfs_rwops_bridge.cpp
		    ${SRC_DIR}/resources/io/platform_io_common.cpp
		    ${SRC_DIR}/resources/io/filesystem.cpp
		    ${SRC_DIR}/resources/io/physfs_api.cpp
		    ${SRC_DIR}/resources/io/zip_api.cpp
		    ${SRC_DIR}/resources/campaign_yaml.cpp
		    ${SRC_DIR}/resources/classpack_yaml.cpp
		    ${SRC_DIR}/resources/io/og_file.cpp
		    ${SRC_DIR}/resources/gparser.cpp
		    ${SRC_DIR}/resources/gloader.cpp
    ${SRC_DIR}/resources/gloader_ctf.cpp
    ${SRC_DIR}/resources/campaign_metadata.cpp
    ${SRC_DIR}/resources/game_mode.cpp
    ${SRC_DIR}/resources/progression.cpp
    ${SRC_DIR}/resources/win_shares.cpp
    ${SRC_DIR}/resources/tower_progression.cpp
    ${SRC_DIR}/resources/mapgen/tower_floor_gen.cpp
    ${SRC_DIR}/gameplay/mapgen/builders.cpp
    ${SRC_DIR}/gameplay/mapgen/audits.cpp
    ${SRC_DIR}/resources/level_file_io.cpp
    ${SRC_DIR}/resources/our_palette.cpp
    ${SRC_DIR}/resources/pixie_data.cpp
    ${SRC_DIR}/resources/save_data.cpp
    ${SRC_DIR}/resources/company.cpp
    ${SRC_DIR}/resources/packs.cpp
    ${SRC_DIR}/resources/pack_transfer_io.cpp
    ${SRC_DIR}/interface/session_state.cpp
    ${SRC_DIR}/interface/replay_runtime.cpp
    ${SRC_DIR}/interface/level_runtime_data.cpp
    ${SRC_DIR}/interface/ui/level_picker.cpp
    ${SRC_DIR}/interface/ui/level_editor.cpp
    ${SRC_DIR}/interface/ui/level_editor_ui.cpp
    ${SRC_DIR}/interface/ui/level_editor_tools.cpp
    ${SRC_DIR}/interface/ui/level_editor_file_ops.cpp
    ${SRC_DIR}/interface/ui/campaign_picker.cpp
    ${SRC_DIR}/interface/ui/results_screen.cpp
    ${SRC_DIR}/gameplay/sim_entity.cpp
    ${SRC_DIR}/gameplay/sim_event_log.cpp
    ${SRC_DIR}/gameplay/game_server.cpp
    ${SRC_DIR}/gameplay/lobby_server.cpp
    ${SRC_DIR}/gameplay/game_client.cpp
    ${SRC_DIR}/gameplay/net_transport.cpp
    ${SRC_DIR}/gameplay/net_transport_multiplex.cpp
    ${SRC_DIR}/gameplay/net_transport_inprocess.cpp
    ${SRC_DIR}/gameplay/pack_transfer.cpp
    ${SRC_DIR}/gameplay/astar.cpp
    ${SRC_DIR}/gameplay/gameplay_context.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/gameplay/game_world_weather.cpp
    ${SRC_DIR}/gameplay/ctf/ctf.cpp
    ${SRC_DIR}/gameplay/ctf/ctf_ai.cpp
    ${SRC_DIR}/gameplay/world_snapshot.cpp
    ${SRC_DIR}/gameplay/sim_input_handler.cpp
    ${SRC_DIR}/gameplay/sim_control_policy.cpp
    ${SRC_DIR}/platform/sdl/input_event_bridge.cpp
    ${SRC_DIR}/interface/walker_render_bridge.cpp
    ${SRC_DIR}/interface/sdl_context_services.cpp
    ${SRC_DIR}/interface/cheat_handler.cpp
    ${SRC_DIR}/interface/render/sdl_level_render.cpp
    ${SRC_DIR}/gameplay/input_state_net.cpp
    ${SRC_DIR}/gameplay/replay.cpp
)

set(GAME_SOURCES_NO_MAIN ${GAME_SOURCES})
list(REMOVE_ITEM GAME_SOURCES_NO_MAIN ${SRC_DIR}/platform/sdl/glad.cpp)
if(EMSCRIPTEN)
    list(APPEND GAME_SOURCES_NO_MAIN
        ${SRC_DIR}/platform/emscripten/web_runtime_diagnostics.cpp
        ${SRC_DIR}/platform/emscripten/web_touch_bridge.cpp
        ${SRC_DIR}/platform/emscripten/net_transport_emscripten_ws.cpp
        ${SRC_DIR}/platform/emscripten/net_transport_relay_ws.cpp
    )
endif()

# Internal module decomposition (Phase 1 architecture foundations).
# Keep these lists in sync with GAME_SOURCES_NO_MAIN so targets remain behaviorally equivalent.
set(OG_CORE_SOURCES
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/core/frame_pacing.cpp
    ${SRC_DIR}/core/frame_rate_config.cpp
    ${SRC_DIR}/core/runtime_trace.cpp
    ${SRC_DIR}/core/util.cpp
    ${SRC_DIR}/core/weather.cpp
)

set(OG_SIM_SOURCES
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
)

set(OG_DATA_SOURCES
    ${SRC_DIR}/resources/classpack_yaml.cpp
    ${SRC_DIR}/resources/gparser.cpp
    ${SRC_DIR}/resources/gloader.cpp
    ${SRC_DIR}/resources/gloader_ctf.cpp
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

set(OG_ENTITIES_SOURCES
    ${SRC_DIR}/gameplay/effect.cpp
    ${SRC_DIR}/gameplay/family_registry.cpp
    ${SRC_DIR}/gameplay/families/family_string_ids.cpp
    ${SRC_DIR}/gameplay/families/family_archer.cpp
    ${SRC_DIR}/gameplay/families/family_archmage.cpp
    ${SRC_DIR}/gameplay/families/family_barbarian.cpp
    ${SRC_DIR}/gameplay/families/family_big_orc.cpp
    ${SRC_DIR}/gameplay/families/family_cleric.cpp
    ${SRC_DIR}/gameplay/families/family_druid.cpp
    ${SRC_DIR}/gameplay/families/family_elf.cpp
    ${SRC_DIR}/gameplay/families/family_faerie.cpp
    ${SRC_DIR}/gameplay/families/family_fire_elemental.cpp
    ${SRC_DIR}/gameplay/families/family_ghost.cpp
    ${SRC_DIR}/gameplay/families/family_giant_skeleton.cpp
    ${SRC_DIR}/gameplay/families/family_golem.cpp
    ${SRC_DIR}/gameplay/families/family_mage.cpp
    ${SRC_DIR}/gameplay/families/family_orc.cpp
    ${SRC_DIR}/gameplay/families/family_skeleton.cpp
    ${SRC_DIR}/gameplay/families/family_slime.cpp
    ${SRC_DIR}/gameplay/families/family_soldier.cpp
    ${SRC_DIR}/gameplay/families/family_thief.cpp
    ${SRC_DIR}/gameplay/families/family_tower1.cpp
    ${SRC_DIR}/gameplay/weapon_family_registry.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_knife.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_rock.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_projectiles.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_wave.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_door.cpp
    ${SRC_DIR}/gameplay/families/weapon_family_animate.cpp
    ${SRC_DIR}/gameplay/effect_family_registry.cpp
    ${SRC_DIR}/gameplay/families/effect_family_ghost_scare.cpp
    ${SRC_DIR}/gameplay/families/effect_family_shield.cpp
    ${SRC_DIR}/gameplay/families/effect_family_knife_back.cpp
    ${SRC_DIR}/gameplay/families/effect_family_cloud.cpp
    ${SRC_DIR}/gameplay/families/effect_family_chain.cpp
    ${SRC_DIR}/gameplay/families/effect_family_door_open.cpp
    ${SRC_DIR}/gameplay/families/effect_family_bomb.cpp
    ${SRC_DIR}/gameplay/treasure_family_registry.cpp
    ${SRC_DIR}/gameplay/families/treasure_family_consumables.cpp
    ${SRC_DIR}/gameplay/families/treasure_family_valuables.cpp
    ${SRC_DIR}/gameplay/families/treasure_family_navigation.cpp
    ${SRC_DIR}/gameplay/families/treasure_family_ctf.cpp
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

set(OG_RUNTIME_SOURCES
    ${SRC_DIR}/platform/sdl/game.cpp
    ${SRC_DIR}/platform/game_context.cpp
    ${SRC_DIR}/platform/sdl/game_loop.cpp
    ${SRC_DIR}/platform/sdl/game_session.cpp
    ${SRC_DIR}/platform/sdl/glad_gameplay.cpp
    ${SRC_DIR}/platform/sdl/local_transport_shadow.cpp
    ${SRC_DIR}/gameplay/net_transport_multiplex.cpp
    ${SRC_DIR}/interface/score_panel.cpp
    ${SRC_DIR}/interface/fps_overlay.cpp
    ${SRC_DIR}/platform/sdl/screen_lifecycle.cpp
    ${SRC_DIR}/interface/screen.cpp
    ${SRC_DIR}/interface/platform_bridge.cpp
    ${SRC_DIR}/interface/guy_create.cpp
    ${SRC_DIR}/gameplay/stats.cpp
    ${SRC_DIR}/gameplay/astar.cpp
    ${SRC_DIR}/gameplay/gameplay_context.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/gameplay/game_world_weather.cpp
    ${SRC_DIR}/gameplay/sim_input_handler.cpp
    ${SRC_DIR}/gameplay/sim_control_policy.cpp
    ${SRC_DIR}/platform/sdl/input_event_bridge.cpp
    ${SRC_DIR}/gameplay/smooth.cpp
    ${SRC_DIR}/interface/level_runtime_data.cpp
    ${SRC_DIR}/interface/walker_render_bridge.cpp
    ${SRC_DIR}/interface/sdl_context_services.cpp
    ${SRC_DIR}/interface/cheat_handler.cpp
)

set(OG_RENDER_SOURCES
    ${SRC_DIR}/interface/render/graphlib.cpp
    ${SRC_DIR}/interface/render/pal32.cpp
    ${SRC_DIR}/interface/render/pixie.cpp
    ${SRC_DIR}/interface/render/pixien.cpp
    ${SRC_DIR}/interface/render/radar.cpp
    ${SRC_DIR}/interface/render/text.cpp
    ${SRC_DIR}/interface/render/view.cpp
    ${SRC_DIR}/interface/render/walker_draw.cpp
    ${SRC_DIR}/interface/render/effects.cpp
    ${SRC_DIR}/interface/render/depth_fx.cpp
    ${SRC_DIR}/interface/render/obmap_debug_draw.cpp
    ${SRC_DIR}/interface/render/sdl_level_render.cpp
)

set(OG_INPUT_SOURCES
    ${SRC_DIR}/interface/input/input.cpp
    ${SRC_DIR}/interface/input/input_state.cpp
)

set(OG_UI_SOURCES
    ${SRC_DIR}/interface/ui/button.cpp
    ${SRC_DIR}/interface/ui/campaign_picker.cpp
    ${SRC_DIR}/interface/ui/help.cpp
    ${SRC_DIR}/interface/ui/intro.cpp
    ${SRC_DIR}/interface/ui/level_editor.cpp
    ${SRC_DIR}/interface/ui/level_editor_file_ops.cpp
    ${SRC_DIR}/interface/ui/level_editor_tools.cpp
    ${SRC_DIR}/interface/ui/level_editor_ui.cpp
    ${SRC_DIR}/interface/ui/level_picker.cpp
    ${SRC_DIR}/interface/ui/picker.cpp
    ${SRC_DIR}/interface/ui/picker_accessible_levels.cpp
    ${SRC_DIR}/interface/ui/picker_dialogs.cpp
    ${SRC_DIR}/interface/ui/picker_input.cpp
    ${SRC_DIR}/interface/ui/picker_main_menu.cpp
    ${SRC_DIR}/interface/ui/picker_team_build.cpp
    ${SRC_DIR}/interface/ui/menu_binding.cpp
    ${SRC_DIR}/interface/ui/menu_model.cpp
    ${SRC_DIR}/interface/ui/menu_screen_runner.cpp
    ${SRC_DIR}/interface/ui/menu_screen_specs.cpp
    ${SRC_DIR}/interface/ui/terminal_menu_model.cpp
    ${SRC_DIR}/interface/ui/picker_lobby_client.cpp
    ${SRC_DIR}/interface/ui/picker_lobby_network_client.cpp
    ${SRC_DIR}/interface/ui/picker_common.cpp
    ${SRC_DIR}/interface/ui/picker_state.cpp
    ${SRC_DIR}/interface/ui/results_screen.cpp
)

set(OG_IO_SDL_SOURCES
    ${SRC_DIR}/resources/platform_io.cpp
    ${SRC_DIR}/platform/sdl/physfs_rwops_bridge.cpp
)

set(OG_IO_SOURCES
    ${SRC_DIR}/resources/io/platform_io_common.cpp
    ${SRC_DIR}/resources/io/filesystem.cpp
    ${SRC_DIR}/resources/io/physfs_api.cpp
    ${SRC_DIR}/resources/io/zip_api.cpp
    ${SRC_DIR}/resources/campaign_yaml.cpp
    ${SRC_DIR}/resources/io/og_file.cpp
)

set(OG_PLATFORM_SOURCES
    ${SRC_DIR}/platform/sdl/native_input.cpp
    ${SRC_DIR}/platform/sdl/sai2x.cpp
    ${SRC_DIR}/platform/sdl/sound.cpp
    ${SRC_DIR}/platform/sdl/video_sdl.cpp
)

# Phase 12 component targets
set(OG_GAMEPLAY_COMPONENT_SOURCES
    ${OG_SIM_SOURCES}
    ${OG_ENTITIES_SOURCES}
    ${OG_SCRIPT_SOURCES}
    ${SRC_DIR}/gameplay/net_transport_multiplex.cpp
    ${SRC_DIR}/gameplay/stats.cpp
    ${SRC_DIR}/gameplay/astar.cpp
    ${SRC_DIR}/gameplay/gameplay_context.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/gameplay/game_world_weather.cpp
    ${SRC_DIR}/gameplay/ctf/ctf.cpp
    ${SRC_DIR}/gameplay/ctf/ctf_ai.cpp
    ${SRC_DIR}/gameplay/sim_input_handler.cpp
    ${SRC_DIR}/gameplay/sim_control_policy.cpp
    ${SRC_DIR}/gameplay/smooth.cpp
    ${SRC_DIR}/gameplay/mapgen/builders.cpp
    ${SRC_DIR}/gameplay/mapgen/audits.cpp
)

set(OG_RESOURCES_COMPONENT_SOURCES
    ${OG_DATA_SOURCES}
    ${OG_IO_SOURCES}
)

set(OG_INTERFACE_COMPONENT_SOURCES
    ${OG_INPUT_SOURCES}
    ${OG_RENDER_SOURCES}
    ${OG_UI_SOURCES}
    ${SRC_DIR}/interface/score_panel.cpp
    ${SRC_DIR}/interface/fps_overlay.cpp
    ${SRC_DIR}/interface/screen.cpp
    ${SRC_DIR}/interface/platform_bridge.cpp
    ${SRC_DIR}/interface/guy_create.cpp
    ${SRC_DIR}/interface/replay_runtime.cpp
    ${SRC_DIR}/interface/session_state.cpp
    ${SRC_DIR}/interface/level_runtime_data.cpp
    ${SRC_DIR}/interface/walker_render_bridge.cpp
    ${SRC_DIR}/interface/sdl_context_services.cpp
    ${SRC_DIR}/interface/cheat_handler.cpp
)

set(OG_PLATFORM_SDL_COMPONENT_SOURCES
    ${OG_PLATFORM_SOURCES}
    ${OG_IO_SDL_SOURCES}
    ${SRC_DIR}/platform/sdl/game.cpp
    ${SRC_DIR}/platform/game_context.cpp
    ${SRC_DIR}/platform/sdl/game_loop.cpp
    ${SRC_DIR}/platform/sdl/game_session.cpp
    ${SRC_DIR}/platform/sdl/glad_gameplay.cpp
    ${SRC_DIR}/platform/sdl/local_transport_shadow.cpp
    ${SRC_DIR}/platform/sdl/picker_lobby_network_client.cpp
    ${SRC_DIR}/platform/sdl/screen_lifecycle.cpp
    ${SRC_DIR}/platform/sdl/input_event_bridge.cpp
)

# --------------------------------------------------------------------------
# Common include directories and compile definitions
# --------------------------------------------------------------------------

set(OG_PUBLIC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

set(COMMON_DEFINITIONS
    USE_BMP_SCREENSHOT=1
    _USE_MATH_DEFINES
    OPENGLAD_VALIDATE_SERIALIZATION=$<BOOL:${VALIDATE_SERIALIZATION}>
    PHYSFS_NO_CDROM_SUPPORT=1
    PHYSFS_SUPPORTS_ZIP=1
    PHYSFS_SUPPORTS_7Z=0
    PHYSFS_SUPPORTS_GRP=0
    PHYSFS_SUPPORTS_WAD=0
    PHYSFS_SUPPORTS_HOG=0
    PHYSFS_SUPPORTS_MVL=0
    PHYSFS_SUPPORTS_QPAK=0
    PHYSFS_SUPPORTS_SLB=0
    PHYSFS_SUPPORTS_ISO9660=0
    PHYSFS_SUPPORTS_VDF=0
)

if(NOT OPENGLAD_HAS_WORKING_STD_FORMAT)
    list(APPEND COMMON_DEFINITIONS OPENGLAD_NEEDS_STD_FORMAT_SHIM=1)
endif()

set(OG_SANITIZER_TEST_ENVIRONMENT)
if(ENABLE_SANITIZERS)
    list(FIND SANITIZERS "address" OG_HAS_ASAN)
    list(FIND SANITIZERS "undefined" OG_HAS_UBSAN)
    list(FIND SANITIZERS "thread" OG_HAS_TSAN)

    if(NOT OG_HAS_ASAN EQUAL -1)
        list(APPEND OG_SANITIZER_TEST_ENVIRONMENT
            "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1")
    endif()
    if(NOT OG_HAS_UBSAN EQUAL -1)
        list(APPEND OG_SANITIZER_TEST_ENVIRONMENT
            "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
    endif()
    if(NOT OG_HAS_TSAN EQUAL -1)
        list(APPEND OG_SANITIZER_TEST_ENVIRONMENT
            "TSAN_OPTIONS=halt_on_error=1")
    endif()
endif()

# --------------------------------------------------------------------------
# Helper functions to configure targets with common settings
# --------------------------------------------------------------------------

function(configure_openglad_library target)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE --coverage)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

function(configure_component_library target include_root)
    target_include_directories(${target} PRIVATE ${include_root})
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE --coverage)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

function(configure_openglad_sdl_target target)
    if(NOT EMSCRIPTEN AND OG_NATIVE_SDL_AVAILABLE)
        # Opt-in SDL wiring: keeps gameplay-only targets free from platform deps.
        target_link_libraries(${target} PRIVATE ${OG_SDL3_TARGET})
    endif()
endfunction()

function(configure_openglad_runtime_target target)
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE
            --use-port=sdl3
            -Wno-constant-conversion
            -Wno-parentheses-equality
            -Wno-pointer-bool-conversion
        )
        target_link_options(${target} PRIVATE
            --use-port=sdl3
        )
    else()
        # Native builds link SDL/system deps via og_runtime_deps, typically pulled
        # in transitively by og_game / og_game_test. Keep this function a no-op
        # on native platforms to avoid brittle link-order issues with static libs.
    endif()
endfunction()

function(configure_openglad_external_target target)
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Keep warnings actionable in OpenGlad code by silencing fetched or
        # packaged compatibility-library sources.
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

function(add_runtime_assets_dependency target)
    add_dependencies(${target} stage_runtime_assets)
endfunction()

function(prepare_component_include_root out_var component_name)
    set(root "${CMAKE_BINARY_DIR}/component_includes/${component_name}")
    set(openglad_root "${root}/openglad")
    file(REMOVE_RECURSE "${root}")
    file(MAKE_DIRECTORY "${openglad_root}")
    foreach(prefix IN LISTS ARGN)
        set(src_dir "${CMAKE_SOURCE_DIR}/include/openglad/${prefix}")
        if(NOT EXISTS "${src_dir}")
            continue()
        endif()
        set(dst_dir "${openglad_root}/${prefix}")
        if(EXISTS "${dst_dir}")
            file(REMOVE_RECURSE "${dst_dir}")
        endif()
        file(CREATE_LINK "${src_dir}" "${dst_dir}" SYMBOLIC COPY_ON_ERROR)
    endforeach()
    set(${out_var} "${root}" PARENT_SCOPE)
endfunction()

# External dependency header leak check: no package headers in public or unrelated code.
add_custom_target(check_vendor_leaks
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_vendor_leaks.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking external dependency header isolation"
    VERBATIM
)

# Render must not write sim-read entity state (the "drawcycle" class of bug:
# render-only writes freeze in the headless sim). See the script header.
add_custom_target(check_render_no_sim_writes
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_render_no_sim_writes.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking render layer does not write synced entity state"
    VERBATIM
)

# --------------------------------------------------------------------------
# External dependency targets
# --------------------------------------------------------------------------

if(NOT EMSCRIPTEN)
    find_package(PkgConfig QUIET)
endif()

set(OG_SYSTEM_ZLIB_TARGET "")
set(OG_SYSTEM_LIBZIP_TARGET "")
set(OG_SYSTEM_LIBYAML_TARGET "")
set(OG_SYSTEM_PHYSFS_TARGET "")
set(OG_ZLIB_FROM_FETCH OFF)
set(OG_LIBYAML_FROM_FETCH OFF)
set(OG_LIBZIP_FROM_FETCH OFF)
set(OG_PHYSFS_FROM_FETCH OFF)
set(OG_LODEPNG_FROM_FETCH OFF)
set(OG_ZLIB_FETCH_TARGET "")
set(OG_LIBYAML_FETCH_TARGET "")
set(OG_LIBZIP_FETCH_TARGET "")
set(OG_PHYSFS_FETCH_TARGET "")
set(OG_IXWEBSOCKET_TARGET "")
set(OG_IXWEBSOCKET_FROM_PACKAGE OFF)
set(OG_IXWEBSOCKET_FROM_FETCH OFF)

if(OPENGLAD_USE_SYSTEM_DEPS AND NOT EMSCRIPTEN)
    find_package(ZLIB QUIET)
    if(TARGET ZLIB::ZLIB)
        set(OG_SYSTEM_ZLIB_TARGET ZLIB::ZLIB)
    endif()

    if(PkgConfig_FOUND)
        pkg_check_modules(OG_LIBZIP QUIET IMPORTED_TARGET libzip)
        if(TARGET PkgConfig::OG_LIBZIP)
            set(OG_SYSTEM_LIBZIP_TARGET PkgConfig::OG_LIBZIP)
        endif()
    endif()
    if(NOT OG_SYSTEM_LIBZIP_TARGET)
        find_package(libzip CONFIG QUIET)
        foreach(candidate IN ITEMS libzip::zip libzip::libzip zip)
            if(NOT OG_SYSTEM_LIBZIP_TARGET AND TARGET ${candidate})
                set(OG_SYSTEM_LIBZIP_TARGET ${candidate})
            endif()
        endforeach()
    endif()

    if(PkgConfig_FOUND)
        pkg_check_modules(OG_LIBYAML QUIET IMPORTED_TARGET yaml-0.1)
        if(TARGET PkgConfig::OG_LIBYAML)
            set(OG_SYSTEM_LIBYAML_TARGET PkgConfig::OG_LIBYAML)
        endif()
    endif()
    if(NOT OG_SYSTEM_LIBYAML_TARGET)
        find_package(yaml CONFIG QUIET)
        foreach(candidate IN ITEMS yaml::yaml yaml)
            if(NOT OG_SYSTEM_LIBYAML_TARGET AND TARGET ${candidate})
                set(OG_SYSTEM_LIBYAML_TARGET ${candidate})
            endif()
        endforeach()
    endif()

    find_package(PhysFS CONFIG QUIET)
    find_package(physfs CONFIG QUIET)
    foreach(candidate IN ITEMS PhysFS::PhysFS PhysFS::physfs unofficial::physfs::physfs physfs)
        if(NOT OG_SYSTEM_PHYSFS_TARGET AND TARGET ${candidate})
            set(OG_SYSTEM_PHYSFS_TARGET ${candidate})
        endif()
    endforeach()
    if(NOT OG_SYSTEM_PHYSFS_TARGET AND PkgConfig_FOUND)
        pkg_check_modules(OG_PHYSFS QUIET IMPORTED_TARGET physfs)
        if(TARGET PkgConfig::OG_PHYSFS)
            set(OG_SYSTEM_PHYSFS_TARGET PkgConfig::OG_PHYSFS)
        endif()
    endif()

    find_path(OG_SYSTEM_LODEPNG_INCLUDE_DIR NAMES lodepng.h)
    find_library(OG_SYSTEM_LODEPNG_LIBRARY NAMES lodepng)

endif()

if(OPENGLAD_REQUIRE_SYSTEM_DEPS AND NOT OPENGLAD_USE_SYSTEM_DEPS AND NOT EMSCRIPTEN)
    message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but OPENGLAD_USE_SYSTEM_DEPS is OFF")
endif()

if(OPENGLAD_REQUIRE_SYSTEM_DEPS AND OPENGLAD_USE_SYSTEM_DEPS AND NOT EMSCRIPTEN)
    if(NOT OG_SYSTEM_ZLIB_TARGET)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but zlib was not found")
    endif()
    if(NOT OG_SYSTEM_LIBZIP_TARGET)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but libzip was not found")
    endif()
    if(NOT OG_SYSTEM_LIBYAML_TARGET)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but libyaml/yaml-0.1 was not found")
    endif()
    if(NOT OG_SYSTEM_PHYSFS_TARGET)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but PhysFS was not found")
    endif()
    if(NOT OG_SYSTEM_LODEPNG_INCLUDE_DIR OR NOT OG_SYSTEM_LODEPNG_LIBRARY)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but lodepng was not found")
    endif()
endif()

if(OG_SYSTEM_ZLIB_TARGET)
    add_library(og_ext_zlib INTERFACE)
    target_link_libraries(og_ext_zlib INTERFACE ${OG_SYSTEM_ZLIB_TARGET})
    message(STATUS "OpenGlad: using system zlib (${OG_SYSTEM_ZLIB_TARGET})")
elseif(OPENGLAD_FETCH_ZLIB)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "Build zlib tests" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "Build zlib shared library" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "Build zlib static library" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "Install zlib" FORCE)
    FetchContent_Declare(zlib
        GIT_REPOSITORY ${OPENGLAD_ZLIB_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_ZLIB_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(zlib)
    if(NOT TARGET zlibstatic)
        message(FATAL_ERROR "Fetched zlib did not define the expected zlibstatic target")
    endif()
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    add_library(og_ext_zlib INTERFACE)
    target_link_libraries(og_ext_zlib INTERFACE zlibstatic)
    set(OG_ZLIB_FROM_FETCH ON)
    set(OG_ZLIB_FETCH_TARGET zlibstatic)
    message(STATUS "OpenGlad: using fetched zlib ${OPENGLAD_ZLIB_GIT_TAG}")
else()
    message(FATAL_ERROR "zlib was not found. Enable OPENGLAD_FETCH_ZLIB or provide a package.")
endif()

if(OG_SYSTEM_LIBYAML_TARGET)
    add_library(og_ext_yaml INTERFACE)
    target_link_libraries(og_ext_yaml INTERFACE ${OG_SYSTEM_LIBYAML_TARGET})
    message(STATUS "OpenGlad: using system libyaml (${OG_SYSTEM_LIBYAML_TARGET})")
elseif(OPENGLAD_FETCH_LIBYAML)
    set(OG_SAVED_BUILD_TESTING "${BUILD_TESTING}")
    set(OG_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(YAML_STATIC_LIB_NAME yaml CACHE STRING "libyaml static library name" FORCE)
    FetchContent_Declare(libyaml
        GIT_REPOSITORY ${OPENGLAD_LIBYAML_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_LIBYAML_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(libyaml)
    set(BUILD_TESTING "${OG_SAVED_BUILD_TESTING}" CACHE BOOL "Build the test suite" FORCE)
    set(BUILD_SHARED_LIBS "${OG_SAVED_BUILD_SHARED_LIBS}" CACHE BOOL "Build shared libraries" FORCE)
    if(NOT TARGET yaml)
        message(FATAL_ERROR "Fetched libyaml did not define the expected yaml target")
    endif()
    add_library(og_ext_yaml INTERFACE)
    target_link_libraries(og_ext_yaml INTERFACE yaml)
    set(OG_LIBYAML_FROM_FETCH ON)
    set(OG_LIBYAML_FETCH_TARGET yaml)
    message(STATUS "OpenGlad: using fetched libyaml ${OPENGLAD_LIBYAML_GIT_TAG}")
else()
    message(FATAL_ERROR "libyaml was not found. Enable OPENGLAD_FETCH_LIBYAML or provide a package.")
endif()

if(OG_SYSTEM_LIBZIP_TARGET)
    add_library(og_ext_libzip INTERFACE)
    target_link_libraries(og_ext_libzip INTERFACE ${OG_SYSTEM_LIBZIP_TARGET})
    message(STATUS "OpenGlad: using system libzip (${OG_SYSTEM_LIBZIP_TARGET})")
elseif(OPENGLAD_FETCH_LIBZIP)
    set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "Build libzip with CommonCrypto" FORCE)
    set(ENABLE_GNUTLS OFF CACHE BOOL "Build libzip with GnuTLS" FORCE)
    set(ENABLE_MBEDTLS OFF CACHE BOOL "Build libzip with mbedTLS" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL "Build libzip with OpenSSL" FORCE)
    set(ENABLE_WINDOWS_CRYPTO OFF CACHE BOOL "Build libzip with Windows crypto" FORCE)
    set(ENABLE_BZIP2 OFF CACHE BOOL "Build libzip with bzip2" FORCE)
    set(ENABLE_LZMA OFF CACHE BOOL "Build libzip with lzma" FORCE)
    set(ENABLE_ZSTD OFF CACHE BOOL "Build libzip with zstd" FORCE)
    set(ENABLE_FDOPEN ON CACHE BOOL "Build libzip fdopen support" FORCE)
    set(BUILD_TOOLS OFF CACHE BOOL "Build libzip tools" FORCE)
    set(BUILD_REGRESS OFF CACHE BOOL "Build libzip regression tests" FORCE)
    set(BUILD_OSSFUZZ OFF CACHE BOOL "Build libzip oss-fuzz targets" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build libzip examples" FORCE)
    set(BUILD_DOC OFF CACHE BOOL "Build libzip docs" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(LIBZIP_DO_INSTALL OFF CACHE BOOL "Install libzip" FORCE)
    set(ZLIB_LINK_LIBRARY_NAME z CACHE STRING "zlib pkg-config link name" FORCE)
    FetchContent_Declare(libzip
        GIT_REPOSITORY ${OPENGLAD_LIBZIP_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_LIBZIP_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
        PATCH_COMMAND
            ${CMAKE_COMMAND}
            -DOPENGLAD_LIBZIP_SOURCE_DIR=<SOURCE_DIR>
            -DOPENGLAD_LIBZIP_USE_FETCHED_ZLIB=${OG_ZLIB_FROM_FETCH}
            -P ${CMAKE_SOURCE_DIR}/cmake/OpenGladPatchLibzip.cmake
    )
    FetchContent_MakeAvailable(libzip)
    if(NOT TARGET zip)
        message(FATAL_ERROR "Fetched libzip did not define the expected zip target")
    endif()
    add_library(og_ext_libzip INTERFACE)
    target_link_libraries(og_ext_libzip INTERFACE zip)
    set(OG_LIBZIP_FROM_FETCH ON)
    set(OG_LIBZIP_FETCH_TARGET zip)
    message(STATUS "OpenGlad: using fetched libzip ${OPENGLAD_LIBZIP_GIT_TAG}")
else()
    message(FATAL_ERROR "libzip was not found. Enable OPENGLAD_FETCH_LIBZIP or provide a package.")
endif()

if(OG_SYSTEM_PHYSFS_TARGET)
    add_library(og_ext_physfs INTERFACE)
    target_link_libraries(og_ext_physfs INTERFACE ${OG_SYSTEM_PHYSFS_TARGET})
    message(STATUS "OpenGlad: using system PhysFS (${OG_SYSTEM_PHYSFS_TARGET})")
elseif(OPENGLAD_FETCH_PHYSFS)
    set(PHYSFS_ARCHIVE_ZIP ON CACHE BOOL "Enable PhysFS ZIP support" FORCE)
    set(PHYSFS_ARCHIVE_7Z OFF CACHE BOOL "Enable PhysFS 7z support" FORCE)
    set(PHYSFS_ARCHIVE_GRP OFF CACHE BOOL "Enable PhysFS GRP support" FORCE)
    set(PHYSFS_ARCHIVE_WAD OFF CACHE BOOL "Enable PhysFS WAD support" FORCE)
    set(PHYSFS_ARCHIVE_HOG OFF CACHE BOOL "Enable PhysFS HOG support" FORCE)
    set(PHYSFS_ARCHIVE_MVL OFF CACHE BOOL "Enable PhysFS MVL support" FORCE)
    set(PHYSFS_ARCHIVE_QPAK OFF CACHE BOOL "Enable PhysFS QPAK support" FORCE)
    set(PHYSFS_ARCHIVE_SLB OFF CACHE BOOL "Enable PhysFS SLB support" FORCE)
    set(PHYSFS_ARCHIVE_ISO9660 OFF CACHE BOOL "Enable PhysFS ISO9660 support" FORCE)
    set(PHYSFS_ARCHIVE_VDF OFF CACHE BOOL "Enable PhysFS VDF support" FORCE)
    set(PHYSFS_BUILD_STATIC ON CACHE BOOL "Build PhysFS static library" FORCE)
    set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "Build PhysFS shared library" FORCE)
    set(PHYSFS_BUILD_TEST OFF CACHE BOOL "Build PhysFS test program" FORCE)
    set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "Build PhysFS docs" FORCE)
    set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "Disable PhysFS install rules" FORCE)
    set(PHYSFS_TARGETNAME_DIST physfs-dist CACHE STRING "Name of PhysFS dist build target" FORCE)
    set(PHYSFS_TARGETNAME_UNINSTALL physfs-uninstall CACHE STRING "Name of PhysFS uninstall build target" FORCE)
    FetchContent_Declare(physfs
        GIT_REPOSITORY ${OPENGLAD_PHYSFS_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_PHYSFS_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(physfs)
    if(NOT TARGET physfs-static)
        message(FATAL_ERROR "Fetched PhysFS did not define the expected physfs-static target")
    endif()
    target_include_directories(physfs-static PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/openglad/external/physfs>
    )
    add_library(og_ext_physfs INTERFACE)
    target_link_libraries(og_ext_physfs INTERFACE physfs-static)
    set(OG_PHYSFS_FROM_FETCH ON)
    set(OG_PHYSFS_FETCH_TARGET physfs-static)
    message(STATUS "OpenGlad: using fetched PhysFS ${OPENGLAD_PHYSFS_GIT_TAG}")
else()
    message(FATAL_ERROR "PhysFS was not found. Enable OPENGLAD_FETCH_PHYSFS or provide a package.")
endif()

if(OG_SYSTEM_LODEPNG_INCLUDE_DIR AND OG_SYSTEM_LODEPNG_LIBRARY)
    add_library(og_ext_lodepng INTERFACE)
    target_include_directories(og_ext_lodepng INTERFACE "${OG_SYSTEM_LODEPNG_INCLUDE_DIR}")
    target_link_libraries(og_ext_lodepng INTERFACE "${OG_SYSTEM_LODEPNG_LIBRARY}")
    message(STATUS "OpenGlad: using system lodepng (${OG_SYSTEM_LODEPNG_LIBRARY})")
elseif(OPENGLAD_FETCH_LODEPNG)
    FetchContent_Declare(lodepng
        GIT_REPOSITORY ${OPENGLAD_LODEPNG_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_LODEPNG_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(lodepng)
    FetchContent_GetProperties(lodepng)
    add_library(og_ext_lodepng STATIC "${lodepng_SOURCE_DIR}/lodepng.cpp")
    configure_openglad_external_target(og_ext_lodepng)
    target_include_directories(og_ext_lodepng PUBLIC
        $<BUILD_INTERFACE:${lodepng_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/openglad/external/lodepng>
    )
    set(OG_LODEPNG_FROM_FETCH ON)
    message(STATUS "OpenGlad: using fetched lodepng ${OPENGLAD_LODEPNG_GIT_TAG}")
else()
    message(FATAL_ERROR "lodepng was not found. Enable OPENGLAD_FETCH_LODEPNG or provide a package.")
endif()

# --- Lua 5.4 (deterministic scripting VM for class packs) ---
# Always vendored at a pinned tag: the VM must behave identically on every
# platform and peer, so a system Lua (different version/patches) is never
# acceptable. Compiled as C++ so Lua errors unwind as exceptions (RAII-safe
# in the binding layer; the web build already compiles with -fexceptions).
if(OPENGLAD_FETCH_LUA)
    FetchContent_Declare(lua
        GIT_REPOSITORY ${OPENGLAD_LUA_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_LUA_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(lua)
    FetchContent_GetProperties(lua)
    set(OG_LUA_SOURCES
        ${lua_SOURCE_DIR}/lapi.c
        ${lua_SOURCE_DIR}/lauxlib.c
        ${lua_SOURCE_DIR}/lbaselib.c
        ${lua_SOURCE_DIR}/lcode.c
        ${lua_SOURCE_DIR}/lcorolib.c
        ${lua_SOURCE_DIR}/lctype.c
        ${lua_SOURCE_DIR}/ldblib.c
        ${lua_SOURCE_DIR}/ldebug.c
        ${lua_SOURCE_DIR}/ldo.c
        ${lua_SOURCE_DIR}/ldump.c
        ${lua_SOURCE_DIR}/lfunc.c
        ${lua_SOURCE_DIR}/lgc.c
        ${lua_SOURCE_DIR}/llex.c
        ${lua_SOURCE_DIR}/lmathlib.c
        ${lua_SOURCE_DIR}/lmem.c
        ${lua_SOURCE_DIR}/lobject.c
        ${lua_SOURCE_DIR}/lopcodes.c
        ${lua_SOURCE_DIR}/loslib.c
        ${lua_SOURCE_DIR}/lparser.c
        ${lua_SOURCE_DIR}/lstate.c
        ${lua_SOURCE_DIR}/lstring.c
        ${lua_SOURCE_DIR}/lstrlib.c
        ${lua_SOURCE_DIR}/ltable.c
        ${lua_SOURCE_DIR}/ltablib.c
        ${lua_SOURCE_DIR}/ltm.c
        ${lua_SOURCE_DIR}/lundump.c
        ${lua_SOURCE_DIR}/lutf8lib.c
        ${lua_SOURCE_DIR}/lvm.c
        ${lua_SOURCE_DIR}/lzio.c
        ${lua_SOURCE_DIR}/linit.c
        ${lua_SOURCE_DIR}/liolib.c
        ${lua_SOURCE_DIR}/loadlib.c
    )
    add_library(og_lua STATIC ${OG_LUA_SOURCES})
    configure_openglad_external_target(og_lua)
    set_source_files_properties(${OG_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
    target_include_directories(og_lua PUBLIC
        $<BUILD_INTERFACE:${lua_SOURCE_DIR}>
    )
    # Fixed string-hash seed: table/string behavior must not vary per run or
    # per platform (class packs are part of the deterministic sim). Lua 5.4.8
    # guards its whole address-mixing default with #if !defined(luai_makeseed),
    # so this macro replaces it outright. Function-style macros cannot go
    # through COMPILE_DEFINITIONS (CMake drops them), hence the raw -D.
    target_compile_options(og_lua PRIVATE "-Dluai_makeseed(L)=0x9e3779b9u")
    message(STATUS "OpenGlad: using fetched Lua ${OPENGLAD_LUA_GIT_TAG}")
else()
    message(FATAL_ERROR "Lua is required. Enable OPENGLAD_FETCH_LUA (Lua is always vendored; system copies are not supported).")
endif()

if(NOT EMSCRIPTEN)
    set(OG_NATIVE_SDL_AVAILABLE OFF)
    set(OG_NATIVE_SDL_REQUESTED ON)
    set(OG_SDL3_TARGET "")
    set(OG_SDL3_FROM_PACKAGE OFF)
    set(OG_SDL3_FROM_FETCH OFF)
    if(CMAKE_DISABLE_FIND_PACKAGE_SDL3)
        set(OG_NATIVE_SDL_REQUESTED OFF)
    endif()
    if(OG_NATIVE_SDL_REQUESTED)
        if(OPENGLAD_USE_SYSTEM_DEPS)
            find_package(SDL3 CONFIG QUIET)
            if(TARGET SDL3::SDL3)
                set(OG_SDL3_TARGET SDL3::SDL3)
                set(OG_SDL3_FROM_PACKAGE ON)
                message(STATUS "OpenGlad: using system SDL3 (SDL3::SDL3)")
            elseif(PkgConfig_FOUND)
                pkg_check_modules(OG_SDL3 QUIET IMPORTED_TARGET sdl3)
                if(TARGET PkgConfig::OG_SDL3)
                    set(OG_SDL3_TARGET PkgConfig::OG_SDL3)
                    set(OG_SDL3_FROM_PACKAGE ON)
                    message(STATUS "OpenGlad: using system SDL3 (PkgConfig::OG_SDL3)")
                endif()
            endif()
        endif()
        if(NOT OG_SDL3_TARGET AND OPENGLAD_REQUIRE_SYSTEM_DEPS)
            message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but SDL3 was not found")
        endif()
        if(NOT OG_SDL3_TARGET AND OPENGLAD_FETCH_SDL3)
            set(SDL_SHARED OFF CACHE BOOL "Build SDL3 shared library" FORCE)
            set(SDL_STATIC ON CACHE BOOL "Build SDL3 static library" FORCE)
            set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3 test library" FORCE)
            FetchContent_Declare(sdl3
                GIT_REPOSITORY ${OPENGLAD_SDL3_GIT_REPOSITORY}
                GIT_TAG ${OPENGLAD_SDL3_GIT_TAG}
                GIT_SHALLOW FALSE
                GIT_PROGRESS TRUE
            )
            FetchContent_MakeAvailable(sdl3)
            if(NOT TARGET SDL3::SDL3)
                message(FATAL_ERROR "Fetched SDL3 did not define the expected SDL3::SDL3 target")
            endif()
            set(OG_SDL3_TARGET SDL3::SDL3)
            set(OG_SDL3_FROM_FETCH ON)
            message(STATUS "OpenGlad: using fetched SDL3 ${OPENGLAD_SDL3_GIT_TAG}")
        endif()
        if(OG_SDL3_TARGET)
            set(OG_NATIVE_SDL_AVAILABLE ON)
        else()
            message(STATUS "SDL3 not found; SDL-native targets require SDL3 to build")
        endif()
    else()
        message(STATUS "SDL3 lookup disabled; configuring headless/native non-SDL targets")
    endif()

    # System/runtime link deps (kept as a target so we can control ordering).
    add_library(og_runtime_deps INTERFACE)
    if(OG_NATIVE_SDL_AVAILABLE)
        target_link_libraries(og_runtime_deps INTERFACE ${OG_SDL3_TARGET} m pthread)
    else()
        target_link_libraries(og_runtime_deps INTERFACE m pthread)
    endif()

    # Native-only WebSocket dependency. Emscripten uses the browser WebSocket API.
    if(OPENGLAD_USE_PACKAGED_IXWEBSOCKET)
        find_package(Threads QUIET)
        find_package(ixwebsocket CONFIG QUIET)
        foreach(candidate IN ITEMS ixwebsocket::ixwebsocket ixwebsocket)
            if(NOT OG_IXWEBSOCKET_TARGET AND TARGET ${candidate})
                set(OG_IXWEBSOCKET_TARGET ${candidate})
                set(OG_IXWEBSOCKET_FROM_PACKAGE ON)
            endif()
        endforeach()
    endif()

    if(NOT OG_IXWEBSOCKET_TARGET AND OPENGLAD_FETCH_IXWEBSOCKET)
        set(BUILD_DEMO OFF CACHE BOOL "Build IXWebSocket demo" FORCE)
        # TLS is required in native builds: the default relay lives on
        # Cloudflare (https:// room create + wss:// room socket). On Apple,
        # IXWebSocket's default (Secure Transport) needs no extra packages; on
        # Linux/Windows use OpenSSL (Debian/Ubuntu: libssl-dev; GitHub
        # windows-latest runners ship OpenSSL).
        set(USE_TLS ON CACHE BOOL "Build IXWebSocket with TLS" FORCE)
        if(NOT APPLE)
            set(USE_OPEN_SSL ON CACHE BOOL "Use OpenSSL for IXWebSocket TLS" FORCE)
        endif()
        set(USE_ZLIB OFF CACHE BOOL "Build IXWebSocket with zlib" FORCE)
        set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "Install IXWebSocket" FORCE)
        FetchContent_Declare(ixwebsocket
            GIT_REPOSITORY ${OPENGLAD_IXWEBSOCKET_GIT_REPOSITORY}
            GIT_TAG ${OPENGLAD_IXWEBSOCKET_GIT_TAG}
            GIT_SHALLOW FALSE
            GIT_PROGRESS TRUE
        )
        FetchContent_MakeAvailable(ixwebsocket)
        set(OG_IXWEBSOCKET_TARGET ixwebsocket)
        set(OG_IXWEBSOCKET_FROM_FETCH ON)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(ixwebsocket PRIVATE -w)
        endif()
    endif()

    if(NOT OG_IXWEBSOCKET_TARGET)
        message(FATAL_ERROR "IXWebSocket was not found. Enable OPENGLAD_FETCH_IXWEBSOCKET or provide a package at the pinned upstream commit.")
    endif()

    add_library(og_ext_ixwebsocket INTERFACE)
    target_link_libraries(og_ext_ixwebsocket INTERFACE ${OG_IXWEBSOCKET_TARGET})
    if(OG_IXWEBSOCKET_FROM_PACKAGE)
        message(STATUS "OpenGlad: using packaged IXWebSocket (${OG_IXWEBSOCKET_TARGET})")
    elseif(OG_IXWEBSOCKET_FROM_FETCH)
        message(STATUS "OpenGlad: using fetched IXWebSocket ${OPENGLAD_IXWEBSOCKET_GIT_TAG}")
    endif()
endif()

set(OG_IO_EXTERNAL_LIBS
    og_ext_yaml
    og_ext_zlib
    og_ext_physfs
    og_ext_libzip
    og_ext_lodepng
    og_lua
)


if(NOT EMSCRIPTEN)
    # --- component targets (Phase 12) ---
    prepare_component_include_root(OG_CORE_INCLUDE_ROOT core
        core)
    prepare_component_include_root(OG_GAMEPLAY_INCLUDE_ROOT gameplay
        core gameplay)
    prepare_component_include_root(OG_RESOURCES_INCLUDE_ROOT resources
        core gameplay resources)
    prepare_component_include_root(OG_INTERFACE_INCLUDE_ROOT interface
        core gameplay resources interface)

    add_library(og_core STATIC ${OG_CORE_SOURCES})
    configure_component_library(og_core "${OG_CORE_INCLUDE_ROOT}")

    add_library(og_gameplay STATIC ${OG_GAMEPLAY_COMPONENT_SOURCES})
    configure_component_library(og_gameplay "${OG_GAMEPLAY_INCLUDE_ROOT}")
    target_link_libraries(og_gameplay PUBLIC og_core og_ext_zlib PRIVATE og_lua)

    add_library(og_resources STATIC ${OG_RESOURCES_COMPONENT_SOURCES})
    configure_component_library(og_resources "${OG_RESOURCES_INCLUDE_ROOT}")
    target_link_libraries(og_resources PUBLIC
        og_core
        og_gameplay
        ${OG_IO_EXTERNAL_LIBS}
    )

    add_library(og_interface STATIC ${OG_INTERFACE_COMPONENT_SOURCES})
    configure_component_library(og_interface "${OG_INTERFACE_INCLUDE_ROOT}")
    configure_openglad_sdl_target(og_interface)
    # og_interface contains src/interface/render/; enforce render stays read-only
    # on synced entity state (the drawcycle-class guard).
    add_dependencies(og_interface check_render_no_sim_writes)
    target_link_libraries(og_interface PUBLIC
        og_core
        og_gameplay
        og_resources
    )

    if(OG_NETWORK_SOURCES)
        add_library(og_platform_ws_transport STATIC ${OG_NETWORK_SOURCES})
        configure_component_library(og_platform_ws_transport "${OG_PUBLIC_INCLUDE_DIR}")
        if(NOT EMSCRIPTEN)
            target_link_libraries(og_platform_ws_transport PUBLIC og_ext_ixwebsocket)
        endif()
        add_dependencies(og_platform_ws_transport check_vendor_leaks)
    endif()

    add_library(og_platform_sdl STATIC ${OG_PLATFORM_SDL_COMPONENT_SOURCES})
    configure_component_library(og_platform_sdl "${OG_PUBLIC_INCLUDE_DIR}")
    configure_openglad_sdl_target(og_platform_sdl)
    target_link_libraries(og_platform_sdl PUBLIC
        og_core
        og_gameplay
        og_resources
        og_interface
    )
    if(TARGET og_platform_ws_transport)
        target_link_libraries(og_platform_sdl PUBLIC og_platform_ws_transport)
    endif()
    if(NOT EMSCRIPTEN)
        target_link_libraries(og_platform_sdl PRIVATE og_ext_ixwebsocket)
    endif()
    add_dependencies(og_platform_sdl check_vendor_leaks)

    add_library(og_platform_emscripten_transport STATIC
        ${SRC_DIR}/platform/emscripten/net_transport_emscripten_ws.cpp
        ${SRC_DIR}/platform/emscripten/net_transport_relay_ws.cpp
    )
    configure_openglad_library(og_platform_emscripten_transport)
    if(BUILD_TESTING)
        target_compile_definitions(og_platform_emscripten_transport PRIVATE TESTING)
    endif()
    add_dependencies(og_platform_emscripten_transport check_vendor_leaks)

    # Aggregate target for executables/tests.
    add_library(og_game INTERFACE)
    if(CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
        target_link_libraries(og_game INTERFACE
            "$<LINK_GROUP:RESCAN,og_platform_sdl,og_interface,og_resources,og_gameplay,og_core>"
            og_runtime_deps
        )
    else()
        target_link_libraries(og_game INTERFACE
            og_platform_sdl
            og_interface
            og_resources
            og_gameplay
            og_core
            og_platform_sdl
            og_runtime_deps
        )
    endif()
    # Stage runtime assets into the build tree so tests and local runs do not
    # depend on pre-existing user data.
    add_custom_target(stage_runtime_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/builtin ${CMAKE_BINARY_DIR}/builtin
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/cfg ${CMAKE_BINARY_DIR}/cfg
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/pix ${CMAKE_BINARY_DIR}/pix
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/sound ${CMAKE_BINARY_DIR}/sound
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/packs ${CMAKE_BINARY_DIR}/packs
        COMMENT "Staging runtime asset directories"
        VERBATIM
    )

    # --- openglad (main game) ---
    add_executable(openglad ${SRC_DIR}/platform/sdl/glad.cpp)
    configure_openglad_library(openglad)
    configure_openglad_sdl_target(openglad)
    target_link_libraries(openglad PRIVATE og_game)
    configure_openglad_runtime_target(openglad)
    add_runtime_assets_dependency(openglad)

    # --- openscen (level editor) ---
    if(BUILD_EDITOR)
        add_executable(openscen ${SRC_DIR}/platform/sdl/glad.cpp)
        configure_openglad_library(openscen)
        configure_openglad_sdl_target(openscen)
        target_link_libraries(openscen PRIVATE og_game)
        configure_openglad_runtime_target(openscen)
        target_compile_definitions(openscen PRIVATE OPENSCEN)
        add_runtime_assets_dependency(openscen)
    endif()

    # --- openglad_demo (multi-session demo) ---
    # Runs 12 concurrent AI-controlled game sessions in a 4x3 grid,
    # demonstrating the de-singletonized engine.
    add_executable(openglad_demo ${SRC_DIR}/platform/sdl/demo.cpp)
    configure_openglad_library(openglad_demo)
    configure_openglad_sdl_target(openglad_demo)
    target_link_libraries(openglad_demo PRIVATE og_game)
    configure_openglad_runtime_target(openglad_demo)
    add_runtime_assets_dependency(openglad_demo)

    # --- openglad_text (headless text-based simulation client) ---
    # Compiles shared game sources without SDL. Uses link-time dispatch
    # (stubs in platform_headless.cpp) instead of preprocessor guards.
    set(HEADLESS_SOURCES
        # text client
        ${SRC_DIR}/platform/text/main.cpp
        ${SRC_DIR}/platform/text/text_platform_globals.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/platform/text/text_picker.cpp
        ${SRC_DIR}/platform/text/text_protocol.cpp
        ${SRC_DIR}/interface/ui/menu_binding.cpp
        ${SRC_DIR}/interface/ui/menu_model.cpp
        ${SRC_DIR}/interface/ui/terminal_menu_model.cpp
        ${SRC_DIR}/interface/ui/picker_common.cpp
        ${SRC_DIR}/interface/ui/picker_state.cpp
        # core
        ${OG_CORE_SOURCES}
        # sim
        ${OG_SIM_SOURCES}
        # mapgen builder lib (tower_floor_gen in OG_DATA_SOURCES needs it;
        # component-linking targets get it from og_gameplay instead)
        ${SRC_DIR}/gameplay/mapgen/builders.cpp
        ${SRC_DIR}/gameplay/mapgen/audits.cpp
        # data
        ${OG_DATA_SOURCES}
        # entities
        ${OG_ENTITIES_SOURCES}
        # scripting VM
        ${OG_SCRIPT_SOURCES}
        # input (SDL-free data structs only)
        ${SRC_DIR}/interface/input/input_state.cpp
        # io (shared SDL-free helpers + PhysFS/zip/yaml)
        ${SRC_DIR}/resources/io/platform_io_common.cpp
        ${SRC_DIR}/resources/io/filesystem.cpp
        ${SRC_DIR}/resources/io/physfs_api.cpp
        ${SRC_DIR}/resources/io/zip_api.cpp
        ${SRC_DIR}/resources/campaign_yaml.cpp
        ${SRC_DIR}/resources/io/og_file.cpp
        ${SRC_DIR}/resources/our_palette.cpp
        # runtime (SDL-free subset)
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/gameplay/astar.cpp
        ${SRC_DIR}/gameplay/gameplay_context.cpp
        ${SRC_DIR}/gameplay/game_world.cpp
        ${SRC_DIR}/gameplay/game_world_weather.cpp
        ${SRC_DIR}/gameplay/ctf/ctf.cpp
        ${SRC_DIR}/gameplay/ctf/ctf_ai.cpp
        ${SRC_DIR}/gameplay/sim_input_handler.cpp
        ${SRC_DIR}/gameplay/sim_control_policy.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        ${SRC_DIR}/gameplay/smooth.cpp
        ${SRC_DIR}/gameplay/stats.cpp
    )

    set(HEADLESS_SERVER_RUNTIME_SOURCES
        ${SRC_DIR}/server/headless_server_runtime.cpp
        ${SRC_DIR}/server/headless_tick_interval.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )

        set(HEADLESS_SERVER_SOURCES
            ${SRC_DIR}/server/server_main.cpp
            ${SRC_DIR}/platform/text/text_platform_globals.cpp
            ${HEADLESS_SERVER_RUNTIME_SOURCES}
        )

        set(HEADLESS_TEST_SOURCES ${HEADLESS_SOURCES})
        list(REMOVE_ITEM HEADLESS_TEST_SOURCES
            ${SRC_DIR}/platform/text/main.cpp
        )

    add_executable(openglad_text ${HEADLESS_SOURCES})
    target_include_directories(openglad_text PUBLIC
        $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
    )
    target_compile_definitions(openglad_text PRIVATE
        ${COMMON_DEFINITIONS}
    )
    target_link_libraries(openglad_text PRIVATE
        project_warnings
        project_sanitizers
        ${OG_IO_EXTERNAL_LIBS}
        m
        pthread
    )
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(openglad_text PRIVATE --coverage)
        target_link_options(openglad_text PRIVATE --coverage)
    endif()
    add_runtime_assets_dependency(openglad_text)

    if(TARGET og_platform_ws_transport)
        add_executable(openglad_server ${HEADLESS_SERVER_SOURCES})
        configure_openglad_library(openglad_server)
        target_link_libraries(openglad_server PRIVATE
            og_core
            og_gameplay
            og_resources
            og_platform_ws_transport
            m
            pthread
        )
        add_runtime_assets_dependency(openglad_server)
    endif()

    # --- ctf_mapgen (CTF campaign generator) ---
    # SDL-free authoring tool that produces builtin/org.openglad.ctf.glad.
    # Not part of the default build; run via scripts/generate_ctf_campaign.sh.
    add_executable(ctf_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/ctf_mapgen/main.cpp
        ${CMAKE_SOURCE_DIR}/tools/ctf_mapgen/grid_painters.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )
    configure_openglad_library(ctf_mapgen)
    target_link_libraries(ctf_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(ctf_mapgen)

    # --- concept_mapgen (Z-axis "Concept Playground" campaign generator) ---
    # SDL-free authoring tool that produces builtin/org.openglad.concept.glad.
    # Not part of the default build; run via scripts/generate_concept_campaign.sh.
    add_executable(concept_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/concept_mapgen/main.cpp
        ${CMAKE_SOURCE_DIR}/tools/concept_mapgen/showcase_pack.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )
    configure_openglad_library(concept_mapgen)
    target_link_libraries(concept_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(concept_mapgen)

    # --- pack_export (class-pack descriptor exporter) ---
    # SDL-free tool that emits packs/core/classpack.yaml from the compiled-in
    # family registries (docs/lua-classpacks-design.md §4). Not part of the
    # default build; rerun whenever descriptor data changes in C++.
    add_executable(pack_export EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/pack_export/main.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )
    configure_openglad_library(pack_export)
    target_link_libraries(pack_export PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )

    # --- westlands_mapgen ("War of the Westlands" campaign generator) ---
    # SDL-free authoring tool that produces builtin/org.openglad.westlands.glad,
    # the 26-level story campaign built act by act (one source file per act).
    # Not part of the default build; run via scripts/generate_westlands_campaign.sh.
    add_executable(westlands_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/main.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_act1.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_act2.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_act3a.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_act3b.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_finale.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )
    configure_openglad_library(westlands_mapgen)
    target_link_libraries(westlands_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(westlands_mapgen)

    # --- longseason_mapgen ("The Long Season" campaign generator) ---
    # SDL-free authoring tool that produces builtin/org.openglad.longseason.glad,
    # the 19-level Brass Kettle Company story campaign built season by season
    # (one source file per season). Not part of the default build; run via
    # scripts/generate_longseason_campaign.sh.
    add_executable(longseason_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/main.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_spring.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_summer.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_autumn.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_winter.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_reckoning.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )
    configure_openglad_library(longseason_mapgen)
    target_link_libraries(longseason_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(longseason_mapgen)

    # --- tower_mapgen ("The Endless Tower" Gate-only package generator) ---
    # SDL-free authoring tool that produces builtin/org.openglad.tower.glad:
    # campaign.yaml (`mode: tower`) + icon + the authored Gate (scen700)
    # ONLY — generated floors are written to the user path at run time and
    # must never ship in the package (tower-triple spec D7). Not part of the
    # default build; run via scripts/generate_tower_campaign.sh.
    add_executable(tower_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/tower_mapgen/main.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )
    configure_openglad_library(tower_mapgen)
    target_link_libraries(tower_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(tower_mapgen)

    # --- grid_migrate (BASE + DECOR migration for hand-authored packages) ---
    # SDL-free tool that rewrites a stock .glad so legacy combined tiles
    # (torches/brazier/boulders/rubble) become base ground + a decor plane
    # (.fss v11), with an in-memory old-vs-new equivalence proof (passability
    # matrix, concealment, damage, door genre, entity streams, footing) that
    # exits nonzero on any mismatch. Not part of the default build; run via
    # scripts/migrate_stock_campaigns.sh.
    add_executable(grid_migrate EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/grid_migrate/main.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )
    configure_openglad_library(grid_migrate)
    target_link_libraries(grid_migrate PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(grid_migrate)

    # --- openglad_curses (zero-SDL ncurses text client) ---
    # A different renderer/input backend for the same game. Built from the same
    # SDL-free sources as the headless server (sim + resources + networking core
    # + headless platform glue) plus the shared menu model and the curses front
    # end, linking ncurses instead of SDL. See docs/ncurses-client.md.
    # The curses client implementation, minus main() and the process globals so
    # the same translation units can be reused by other targets. The curses test
    # binary supplies its own main but links the production process globals.
    set(OG_CURSES_LIB_SOURCES
        ${SRC_DIR}/platform/curses/curses_app.cpp
        ${SRC_DIR}/platform/curses/curses_terminal.cpp
        ${SRC_DIR}/platform/curses/headless_terminal.cpp
        ${SRC_DIR}/platform/curses/clock.cpp
        ${SRC_DIR}/platform/curses/glyph_map.cpp
        ${SRC_DIR}/platform/curses/kitty_keys.cpp
        ${SRC_DIR}/platform/curses/curses_input.cpp
        ${SRC_DIR}/platform/curses/curses_renderer.cpp
        ${SRC_DIR}/platform/curses/curses_picker_client.cpp
        ${SRC_DIR}/platform/curses/curses_game_runtime.cpp
        ${SRC_DIR}/platform/curses/curses_network.cpp
    )
    # Shared SDL-free menu model + business logic (also compiled into openglad_text).
    set(OG_CURSES_MENU_SOURCES
        ${SRC_DIR}/interface/ui/menu_binding.cpp
        ${SRC_DIR}/interface/ui/menu_model.cpp
        ${SRC_DIR}/interface/ui/terminal_menu_model.cpp
        ${SRC_DIR}/interface/ui/picker_common.cpp
        ${SRC_DIR}/interface/ui/picker_state.cpp
    )
    set(OG_CURSES_SOURCES
        ${SRC_DIR}/platform/curses/main.cpp
        ${SRC_DIR}/platform/curses/curses_platform_globals.cpp
        ${OG_CURSES_LIB_SOURCES}
        ${OG_CURSES_MENU_SOURCES}
        ${HEADLESS_SERVER_RUNTIME_SOURCES}
    )

    # ncurses detection: prefer wide-char ncursesw via pkg-config, fall back to
    # narrow ncurses, then to CMake's FindCurses.
    set(OG_CURSES_FOUND FALSE)
    set(OG_CURSES_NCURSES_LINK "")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OG_NCURSESW QUIET IMPORTED_TARGET ncursesw)
        if(OG_NCURSESW_FOUND)
            set(OG_CURSES_NCURSES_LINK PkgConfig::OG_NCURSESW)
            set(OG_CURSES_FOUND TRUE)
        else()
            pkg_check_modules(OG_NCURSES QUIET IMPORTED_TARGET ncurses)
            if(OG_NCURSES_FOUND)
                set(OG_CURSES_NCURSES_LINK PkgConfig::OG_NCURSES)
                set(OG_CURSES_FOUND TRUE)
            endif()
        endif()
    endif()
    if(NOT OG_CURSES_FOUND)
        set(CURSES_NEED_NCURSES TRUE)
        find_package(Curses QUIET)
        if(CURSES_FOUND)
            set(OG_CURSES_FOUND TRUE)
        endif()
    endif()

    if(OG_CURSES_FOUND AND TARGET og_platform_ws_transport)
        add_executable(openglad_curses ${OG_CURSES_SOURCES})
        configure_openglad_library(openglad_curses)
        if(OG_CURSES_NCURSES_LINK)
            target_link_libraries(openglad_curses PRIVATE ${OG_CURSES_NCURSES_LINK})
        else()
            target_include_directories(openglad_curses PRIVATE ${CURSES_INCLUDE_DIRS})
            target_link_libraries(openglad_curses PRIVATE ${CURSES_LIBRARIES})
        endif()
        target_link_libraries(openglad_curses PRIVATE
            og_core
            og_gameplay
            og_resources
            og_platform_ws_transport
            m
            pthread
        )
        add_runtime_assets_dependency(openglad_curses)
        message(STATUS "openglad_curses: enabled (zero-SDL ncurses client)")
    else()
        message(STATUS "openglad_curses: disabled (ncurses not found or networking unavailable)")
    endif()

    install(TARGETS
        project_warnings
        project_sanitizers
        og_core
        og_gameplay
        og_resources
        og_interface
        og_platform_sdl
        og_ext_lodepng
        og_ext_yaml
        og_ext_zlib
        og_ext_libzip
        og_ext_physfs
        og_ext_ixwebsocket
        og_lua
        openglad
        EXPORT OpenGladTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(OG_ZLIB_FROM_FETCH AND TARGET ${OG_ZLIB_FETCH_TARGET})
        install(TARGETS ${OG_ZLIB_FETCH_TARGET}
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(FILES
            ${zlib_SOURCE_DIR}/zlib.h
            ${zlib_BINARY_DIR}/zconf.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()

    if(OG_LIBYAML_FROM_FETCH AND TARGET ${OG_LIBYAML_FETCH_TARGET})
        install(TARGETS ${OG_LIBYAML_FETCH_TARGET}
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(OG_LIBZIP_FROM_FETCH AND TARGET ${OG_LIBZIP_FETCH_TARGET})
        install(TARGETS ${OG_LIBZIP_FETCH_TARGET}
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(FILES
            ${libzip_SOURCE_DIR}/lib/zip.h
            ${libzip_BINARY_DIR}/zipconf.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()

    if(OG_PHYSFS_FROM_FETCH AND TARGET ${OG_PHYSFS_FETCH_TARGET})
        install(TARGETS ${OG_PHYSFS_FETCH_TARGET}
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(FILES
            ${physfs_SOURCE_DIR}/src/physfs.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/openglad/external/physfs
        )
    endif()

    if(OG_SDL3_FROM_FETCH AND TARGET SDL3-static)
        # og_interface/og_platform_sdl carry SDL3-static as a $<LINK_ONLY:>
        # dep in the export set, so the fetched target must be exported too
        # (same treatment as the other fetched static deps above), along
        # with the SDL3_Headers INTERFACE target it links.
        install(TARGETS SDL3-static SDL3_Headers
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(DIRECTORY ${sdl3_SOURCE_DIR}/include/SDL3
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()

    if(OG_LODEPNG_FROM_FETCH)
        install(FILES
            ${lodepng_SOURCE_DIR}/lodepng.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/openglad/external/lodepng
        )
    endif()

    if(OG_IXWEBSOCKET_FROM_FETCH AND TARGET ixwebsocket)
        install(TARGETS ixwebsocket
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ixwebsocket
        )
    endif()

    if(TARGET og_platform_ws_transport)
        install(TARGETS og_platform_ws_transport
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

        install(TARGETS openglad_server
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(TARGET openglad_curses)
        install(TARGETS openglad_curses
            EXPORT OpenGladTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(BUILD_EDITOR)
        install(TARGETS openscen
            EXPORT OpenGladTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(OPENGLAD_INSTALL_ASSETS)
        install(
            DIRECTORY
                ${CMAKE_SOURCE_DIR}/cfg
                ${CMAKE_SOURCE_DIR}/pix
                ${CMAKE_SOURCE_DIR}/sound
                ${CMAKE_SOURCE_DIR}/builtin
            DESTINATION ${CMAKE_INSTALL_DATADIR}/openglad
        )
    endif()

    set(OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES "")
    if(OG_NATIVE_SDL_AVAILABLE AND OG_SDL3_FROM_PACKAGE)
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET SDL3::SDL3)
    find_dependency(SDL3 CONFIG)
endif()
_openglad_require_imported_target(\"SDL3::SDL3\" \"SDL3\")

")
    endif()
    if(OG_SYSTEM_ZLIB_TARGET)
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"find_dependency(ZLIB)
_openglad_require_imported_target(\"${OG_SYSTEM_ZLIB_TARGET}\" \"zlib\")

")
    endif()
    if(OG_SYSTEM_LIBYAML_TARGET STREQUAL "PkgConfig::OG_LIBYAML")
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET PkgConfig::OG_LIBYAML)
    find_dependency(PkgConfig)
    pkg_check_modules(OG_LIBYAML QUIET IMPORTED_TARGET yaml-0.1)
endif()
_openglad_require_imported_target(\"PkgConfig::OG_LIBYAML\" \"libyaml/yaml-0.1\")

")
    elseif(OG_SYSTEM_LIBYAML_TARGET)
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_LIBYAML_TARGET})
    find_package(yaml CONFIG QUIET)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_LIBYAML_TARGET}\" \"libyaml\")

")
    endif()
    if(OG_SYSTEM_LIBZIP_TARGET)
        if(OG_SYSTEM_LIBZIP_TARGET MATCHES "^PkgConfig::")
            string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_LIBZIP_TARGET})
    find_dependency(PkgConfig)
    pkg_check_modules(OG_LIBZIP QUIET IMPORTED_TARGET libzip)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_LIBZIP_TARGET}\" \"libzip\")

")
        else()
            string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_LIBZIP_TARGET})
    find_package(libzip CONFIG QUIET)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_LIBZIP_TARGET}\" \"libzip\")

")
        endif()
    endif()
    if(OG_SYSTEM_PHYSFS_TARGET)
        if(OG_SYSTEM_PHYSFS_TARGET MATCHES "^PkgConfig::")
            string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_PHYSFS_TARGET})
    find_dependency(PkgConfig)
    pkg_check_modules(OG_PHYSFS QUIET IMPORTED_TARGET physfs)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_PHYSFS_TARGET}\" \"PhysFS\")

")
        else()
            string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_PHYSFS_TARGET})
    find_package(PhysFS CONFIG QUIET)
    find_package(physfs CONFIG QUIET)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_PHYSFS_TARGET}\" \"PhysFS\")

")
        endif()
    endif()
    if(OG_IXWEBSOCKET_FROM_PACKAGE)
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_IXWEBSOCKET_TARGET})
    find_dependency(Threads)
    find_package(ixwebsocket CONFIG QUIET)
endif()
_openglad_require_imported_target(\"${OG_IXWEBSOCKET_TARGET}\" \"IXWebSocket\")

")
    endif()

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/OpenGladConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        ${CMAKE_SOURCE_DIR}/cmake/OpenGladConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/OpenGladConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenGlad
    )

    install(EXPORT OpenGladTargets
        FILE OpenGladTargets.cmake
        NAMESPACE OpenGlad::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenGlad
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/OpenGladConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/OpenGladConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenGlad
    )

    if(BUILD_TESTING)
        find_package(GTest REQUIRED)

        # Incremental coverage builds can retain gcov artifacts for the deleted
        # pre-GTest runners. Purge those stale files so gcovr does not fail on
        # missing sources after the Phase 3 framework swap.
        file(GLOB_RECURSE OG_STALE_LEGACY_TEST_ARTIFACTS
            "${CMAKE_BINARY_DIR}/CMakeFiles/*/tests/test_framework.cpp.gcda"
            "${CMAKE_BINARY_DIR}/CMakeFiles/*/tests/test_framework.cpp.gcno"
            "${CMAKE_BINARY_DIR}/CMakeFiles/*/tests/test_framework.cpp.o"
            "${CMAKE_BINARY_DIR}/CMakeFiles/*/tests/test_framework.cpp.o.d"
            "${CMAKE_BINARY_DIR}/CMakeFiles/*/tests/test_main.cpp.gcda"
            "${CMAKE_BINARY_DIR}/CMakeFiles/*/tests/test_main.cpp.gcno"
            "${CMAKE_BINARY_DIR}/CMakeFiles/*/tests/test_main.cpp.o"
            "${CMAKE_BINARY_DIR}/CMakeFiles/*/tests/test_main.cpp.o.d"
        )
        if(OG_STALE_LEGACY_TEST_ARTIFACTS)
            file(REMOVE ${OG_STALE_LEGACY_TEST_ARTIFACTS})
        endif()

        set(ALL_INTEGRATION_TEST_SOURCES
            ${CMAKE_SOURCE_DIR}/tests/test_trace_buffer.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_startup.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_combat_math.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_loader_and_walker.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_palette.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_util.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_game_loop.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_save_load.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_menu.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_game_launch.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_progress.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_back_to_mainmenu.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_new_game.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_hire_team.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_train_team.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_save_load_team.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_team.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_options_menu.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_networking_menu.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_menu_layout.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_help.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_load_levels.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_replay.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_snapshot_size_benchmark.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_difficulty.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_guy.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_go_no_team.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_fairy_death.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_overpowered_team.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_filesystem.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smoother.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_primitives.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_effects_prims.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_render_effects.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_decor_render.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stair_overlay.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_text_rendering.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_campaign_and_level_picker.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_intro_smoke.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_dirty_tracking_safety.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_sai2x_scaler.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_canvas_scale.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_menu_driven.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_options_menu_driven.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_helpers.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_chain_and_door.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_weapon_interactions.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_game_context.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_guy_calcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_network_client.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_local_transport_shadow_budget.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_frame_pacing_jitter.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_navigation.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_screen_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_living_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_guy_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_combat.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_specials.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_commands.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_right_walk_direct_walk.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_hit_response_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_hit_response_teleport.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_draw.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_draw.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_living_combat.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_gloader_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_screen_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_resize.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_redraw.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_ops.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_error_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_png_conversion.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_aseprite_round_trip.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smooth_ops.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smooth_more_branches.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smooth_matrix.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_radar_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_treasure_eat.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_weap_behavior.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_act.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_text_render.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_text_input_and_width.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_death.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_fade.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_extra.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_gparser_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_movement.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_yaml.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_zip.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_save_data_versions.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_company_io.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_editor_smoke.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_zlib.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_libzip.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_lifecycle.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_accessible_levels.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_input_smoke.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_timed_dialog.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_costs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_buffers.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_pixel_ops.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_modes_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_archivers.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_editor_interactions.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_help_parsing.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_zip_unzip.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_new_file_fixture.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_glad_hud.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_editor_helpers.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_help_smoke.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_keybinds.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_direction_grace.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_pathing.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_yaml_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_ops.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_zlib_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_input_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_get_keypress_and_edge_cases.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_input_prefs_and_redraw.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_menu_nav.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_yaml_document.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_yaml_api_builder.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_unicode.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_byteorder.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_joystick.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_zlib_api_edges.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_api_edges.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_uncovered.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_ownership_fixture.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_state_machine.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_editor_prompt_block.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_load_versions.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_more_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_core_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_input_more_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_more_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_text_input_ex_value.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_detail_menu_driven.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_team_list.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_death_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_event_dispatch.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_results_screen_branches.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_results_screen_internal_helper.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_results_screen_full_ui.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_dialogs_real.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_data.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_behaviors.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_entity_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_platform_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_mass_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_menu_model.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_runtime_coverage_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_sim_input_handler.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smooth_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/parity_runner.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/state_dump.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/fact_predicate.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/parity_bootstrap.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/scenario_runtime.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/parity_test_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/golden_evaluation_helper.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/test_parity_scenarios.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/test_parity_coverage_gate.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_ctf_ui.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_tower_run.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_menu_pins.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_menu_engine.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_company_list.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_uxshots_probe.cpp
        )

        function(og_add_test_group NAME)
            cmake_parse_arguments(ARG "" "" "FILES" ${ARGN})

            set(selected)
            foreach(src IN LISTS ALL_INTEGRATION_TEST_SOURCES)
                cmake_path(GET src FILENAME fname)
                if(fname IN_LIST ARG_FILES)
                    list(APPEND selected "${src}")
                endif()
            endforeach()

            add_executable(${NAME}
                ${CMAKE_SOURCE_DIR}/tests/integration_main.cpp
                ${selected}
            )
            configure_openglad_library(${NAME})
            configure_openglad_sdl_target(${NAME})
            target_compile_definitions(${NAME} PRIVATE TESTING)
            target_include_directories(${NAME} PRIVATE
                ${CMAKE_SOURCE_DIR}/tests
            )
            target_link_libraries(${NAME} PRIVATE og_game_test GTest::gtest)
            configure_openglad_runtime_target(${NAME})
            add_runtime_assets_dependency(${NAME})

            if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${NAME} PRIVATE -O1 -g)
                target_compile_definitions(${NAME} PRIVATE ENABLE_COVERAGE)
            endif()

            add_test(NAME ${NAME} COMMAND ${NAME})
            set_tests_properties(${NAME} PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 180
                LABELS "integration"
            )
            if(OG_SANITIZER_TEST_ENVIRONMENT)
                set_tests_properties(${NAME} PROPERTIES ENVIRONMENT
                    "${OG_SANITIZER_TEST_ENVIRONMENT}"
                )
            endif()
        endfunction()

        function(og_add_unit_group NAME)
            cmake_parse_arguments(ARG "" "" "FILES" ${ARGN})

            add_executable(${NAME}
                ${CMAKE_SOURCE_DIR}/tests/unit/unit_main.cpp
                ${ARG_FILES}
            )
            configure_openglad_library(${NAME})
            configure_openglad_sdl_target(${NAME})
            target_compile_definitions(${NAME} PRIVATE TESTING)
            target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
            target_link_libraries(${NAME} PRIVATE og_game GTest::gtest)
            configure_openglad_runtime_target(${NAME})
            add_runtime_assets_dependency(${NAME})

            if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${NAME} PRIVATE -O1 -g)
                target_compile_definitions(${NAME} PRIVATE ENABLE_COVERAGE)
            endif()

            add_test(NAME ${NAME} COMMAND ${NAME})
            set_tests_properties(${NAME} PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 180
                LABELS "unit"
            )
            if(OG_SANITIZER_TEST_ENVIRONMENT)
                set_tests_properties(${NAME} PROPERTIES ENVIRONMENT
                    "${OG_SANITIZER_TEST_ENVIRONMENT}"
                )
            endif()
        endfunction()

        add_library(og_game_test STATIC
            ${GAME_SOURCES_NO_MAIN}
            ${SRC_DIR}/platform/sdl/glad.cpp
            ${SRC_DIR}/test_trace.cpp
        )
        configure_openglad_library(og_game_test)
        configure_openglad_sdl_target(og_game_test)
        target_compile_definitions(og_game_test PRIVATE TESTING)
        target_link_libraries(og_game_test PUBLIC ${OG_IO_EXTERNAL_LIBS} og_runtime_deps)
        if(NOT EMSCRIPTEN)
            target_link_libraries(og_game_test PUBLIC og_ext_ixwebsocket)
        endif()
        add_dependencies(og_game_test check_vendor_leaks)

        enable_testing()

        # Isolated lifecycle regression for the canonical owning sdl_video()
        # constructor. Unlike the integration runner, this process begins with
        # no existing E_Screen and can verify both boot-mode reapplication and
        # owning teardown without disturbing another test's SDL globals.
        add_executable(og_test_sdl_video_lifecycle
            ${CMAKE_SOURCE_DIR}/tests/sdl_video_default_lifecycle.cpp
        )
        configure_openglad_library(og_test_sdl_video_lifecycle)
        configure_openglad_sdl_target(og_test_sdl_video_lifecycle)
        target_link_libraries(og_test_sdl_video_lifecycle PRIVATE og_game)
        configure_openglad_runtime_target(og_test_sdl_video_lifecycle)
        add_runtime_assets_dependency(og_test_sdl_video_lifecycle)
        add_test(NAME og_test_sdl_video_lifecycle
            COMMAND og_test_sdl_video_lifecycle
        )
        set_tests_properties(og_test_sdl_video_lifecycle PROPERTIES
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 60
        )
        if(OG_SANITIZER_TEST_ENVIRONMENT)
            set_tests_properties(og_test_sdl_video_lifecycle PROPERTIES
                ENVIRONMENT "${OG_SANITIZER_TEST_ENVIRONMENT}"
            )
        endif()

        # Run initialize -> owning session -> bootstrap exactly once in a
        # dedicated process. This keeps process-global RNG, input, config, and
        # SDL lifecycle mutations out of the shared integration-test runner.
        add_executable(og_test_runtime_bootstrap_lifecycle
            ${CMAKE_SOURCE_DIR}/tests/runtime_bootstrap_lifecycle.cpp
        )
        configure_openglad_library(og_test_runtime_bootstrap_lifecycle)
        configure_openglad_sdl_target(og_test_runtime_bootstrap_lifecycle)
        target_compile_definitions(og_test_runtime_bootstrap_lifecycle PRIVATE
            TESTING
        )
        target_link_libraries(og_test_runtime_bootstrap_lifecycle PRIVATE
            og_game_test
        )
        configure_openglad_runtime_target(og_test_runtime_bootstrap_lifecycle)
        add_runtime_assets_dependency(og_test_runtime_bootstrap_lifecycle)
        add_test(NAME og_test_runtime_bootstrap_lifecycle
            COMMAND og_test_runtime_bootstrap_lifecycle
        )
        set_tests_properties(og_test_runtime_bootstrap_lifecycle PROPERTIES
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 60
        )
        if(OG_SANITIZER_TEST_ENVIRONMENT)
            set_tests_properties(og_test_runtime_bootstrap_lifecycle PROPERTIES
                ENVIRONMENT "${OG_SANITIZER_TEST_ENVIRONMENT}"
            )
        endif()

        og_add_test_group(og_test_walker_combat FILES
            test_walker_combat.cpp
            test_walker_death.cpp
        )

        og_add_test_group(og_test_walker_move FILES
            test_walker_movement.cpp
            test_walker_pathing.cpp
        )

        og_add_test_group(og_test_walker_core FILES
            test_walker_specials.cpp
            test_walker_core_more.cpp
            test_walker_extended.cpp
            test_walker_more.cpp
        )

        og_add_test_group(og_test_families FILES
            test_family_behaviors.cpp
            test_family_data.cpp
        )

        og_add_test_group(og_test_effects FILES
            test_effect_act.cpp
            test_effect_chain_and_door.cpp
            test_effect_death_more.cpp
            test_effect_extended.cpp
            test_effect_helpers.cpp
            test_effect_more_paths.cpp
            test_effect_weapon_interactions.cpp
        )

        og_add_test_group(og_test_living FILES
            test_living_combat.cpp
            test_living_funcs.cpp
            test_weap_behavior.cpp
            test_treasure_eat.cpp
            test_entity_coverage.cpp
        )

        og_add_test_group(og_test_stats FILES
            test_stats.cpp
            test_stats_commands.cpp
            test_stats_extended.cpp
            test_stats_hit_response_more.cpp
            test_stats_hit_response_teleport.cpp
            test_stats_more_paths.cpp
            test_stats_navigation.cpp
            test_stats_right_walk_direct_walk.cpp
            test_stats_coverage.cpp
        )

        og_add_test_group(og_test_guy FILES
            test_guy.cpp
            test_guy_calcs.cpp
            test_guy_extended.cpp
        )

        og_add_test_group(og_test_game_core FILES
            test_combat_math.cpp
            test_game_context.cpp
            test_startup.cpp
            test_trace_buffer.cpp
            test_palette.cpp
            test_loader_and_walker.cpp
            test_gloader_funcs.cpp
            test_game_loop.cpp
            test_game_launch.cpp
            test_go_no_team.cpp
            test_fairy_death.cpp
            test_overpowered_team.cpp
            test_frame_pacing_jitter.cpp
        )

        og_add_test_group(og_test_snapshot_safety FILES
            test_dirty_tracking_safety.cpp
        )

        og_add_test_group(og_test_tower_run FILES
            test_tower_run.cpp
        )

        og_add_test_group(og_test_screen FILES
            test_screen_extended.cpp
            test_screen_funcs.cpp
            test_runtime_coverage_paths.cpp
        )

        og_add_test_group(og_test_view FILES
            test_view_draw.cpp
            test_view_redraw.cpp
            test_view_funcs.cpp
            test_view_lifecycle.cpp
            test_view_resize.cpp
            test_view_team_list.cpp
            test_radar_more.cpp
            test_glad_hud.cpp
            test_sai2x_scaler.cpp
            test_view_input_paths.cpp
            test_view_input_more_paths.cpp
            test_view_input_smoke.cpp
            test_view_input_prefs_and_redraw.cpp
            test_view_get_keypress_and_edge_cases.cpp
            test_view_menu_driven.cpp
            test_view_options_menu_driven.cpp
            test_view_team.cpp
        )

        og_add_test_group(og_test_rendering FILES
            test_canvas_scale.cpp
            test_video_buffers.cpp
            test_video_draw.cpp
            test_video_extended.cpp
            test_video_extra.cpp
            test_video_fade.cpp
            test_video_modes_more.cpp
            test_video_effects_prims.cpp
            test_render_effects.cpp
            test_decor_render.cpp
            test_stair_overlay.cpp
            test_video_pixel_ops.cpp
            test_video_primitives.cpp
            test_text_render.cpp
            test_text_rendering.cpp
            test_text_input_and_width.cpp
            test_text_input_ex_value.cpp
        )

        og_add_test_group(og_test_picker FILES
            test_picker_accessible_levels.cpp
            test_picker_costs.cpp
            test_picker_detail_menu_driven.cpp
            test_picker_dialogs_real.cpp
            test_picker_funcs.cpp
            test_picker_menu_nav.cpp
            test_picker_ownership_fixture.cpp
            test_picker_state_machine.cpp
            test_picker_timed_dialog.cpp
            test_picker_uncovered.cpp
            test_menu_model.cpp
        )

        og_add_test_group(og_test_picker_network FILES
            test_picker_network_client.cpp
            test_local_transport_shadow_budget.cpp
        )

        og_add_test_group(og_test_menu_ui FILES
            test_back_to_mainmenu.cpp
            test_menu.cpp
            test_menu_layout.cpp
            test_new_game.cpp
            test_hire_team.cpp
            test_train_team.cpp
            test_save_load_team.cpp
            test_options_menu.cpp
            test_networking_menu.cpp
            test_difficulty.cpp
            test_help.cpp
            test_help_parsing.cpp
            test_help_smoke.cpp
            test_results_screen_branches.cpp
            test_results_screen_full_ui.cpp
            test_results_screen_internal_helper.cpp
            test_intro_smoke.cpp
            test_campaign_and_level_picker.cpp
            test_level_progress.cpp
        )

        # WP1 menu-engine group (design §1.9 G10): fast, engine-focused menu
        # tests live here, NEVER in og_test_menu_ui (~130s vs the 180s
        # timeout). Holds the G2 pin-then-migrate exact-table oracles and,
        # as the engine lands, tests/test_menu_engine.cpp.
        og_add_test_group(og_test_menu_engine FILES
            test_menu_pins.cpp
            test_menu_engine.cpp
        )

        # Layer-F Company & Base Camp flow group (design G10): heavyweight
        # picker_main injector flows for the §2.2-§2.5 feature screens live
        # here, never in og_test_menu_ui (~130s vs the 180s timeout).
        og_add_test_group(og_test_basecamp FILES
            test_company_list.cpp
            test_uxshots_probe.cpp
        )

        og_add_test_group(og_test_input FILES
            test_input.cpp
            test_input_direction_grace.cpp
            test_input_event_dispatch.cpp
            test_input_joystick.cpp
            test_input_keybinds.cpp
            test_input_more.cpp
            test_sim_input_handler.cpp
        )

        og_add_test_group(og_test_level FILES
            test_level_data_error_paths.cpp
            test_level_data_load_versions.cpp
            test_level_data_ops.cpp
            test_level_editor_helpers.cpp
            test_level_editor_interactions.cpp
            test_level_editor_prompt_block.cpp
            test_level_editor_smoke.cpp
            test_level_data_coverage.cpp
            test_png_conversion.cpp
            test_aseprite_round_trip.cpp
        )

        og_add_test_group(og_test_io FILES
            test_io_filesystem.cpp
            test_io_funcs.cpp
            test_io_new_file_fixture.cpp
            test_io_zip_unzip.cpp
            test_gparser_funcs.cpp
            test_util.cpp
            test_save_data_versions.cpp
            test_company_io.cpp
            test_save_load.cpp
            test_load_levels.cpp
            test_io_coverage.cpp
            test_io_platform_coverage.cpp
        )

        og_add_test_group(og_test_snapshot_benchmark FILES
            test_replay.cpp
            test_snapshot_size_benchmark.cpp
        )

        og_add_test_group(og_test_smooth FILES
            test_smooth_matrix.cpp
            test_smooth_more_branches.cpp
            test_smooth_ops.cpp
            test_smoother.cpp
            test_smooth_coverage.cpp
        )

        og_add_test_group(og_test_external FILES
            test_external_libzip.cpp
            test_external_physfs_api_edges.cpp
            test_external_physfs_archivers.cpp
            test_external_physfs_byteorder.cpp
            test_external_physfs_ops.cpp
            test_external_physfs_unicode.cpp
            test_external_physfs_zip.cpp
            test_external_yaml.cpp
            test_external_yaml_api_builder.cpp
            test_external_yaml_document.cpp
            test_external_yaml_more.cpp
            test_external_zlib.cpp
            test_external_zlib_api_edges.cpp
            test_external_zlib_more.cpp
        )

        og_add_test_group(og_test_mass_coverage FILES
            test_mass_coverage.cpp
        )

        # Gameplay parity harness (Phase 04 skeleton). The og_test_parity group
        # captures a canonical schema-v1 state dump per scenario and compares
        # against the committed master golden under tests/parity/golden/.
        og_add_test_group(og_test_parity FILES
            test_parity_scenarios.cpp
            test_parity_coverage_gate.cpp
            parity_runner.cpp
            state_dump.cpp
            fact_predicate.cpp
            parity_bootstrap.cpp
            scenario_runtime.cpp
            parity_test_main.cpp
            golden_evaluation_helper.cpp
        )

        og_add_test_group(og_test_ctf FILES
            test_ctf_ui.cpp
        )
        target_include_directories(og_test_parity PRIVATE
            ${CMAKE_SOURCE_DIR}/tests/parity
        )
        target_compile_definitions(og_test_parity PRIVATE
            OG_PARITY_WORKSPACE_ROOT="${CMAKE_SOURCE_DIR}"
        )

        # Phase 01 (semantic-parity): pre-build tool that emits the
        # single-source-of-truth JSON for scripts/parity/evaluate_facts.py.
        # Runs automatically as a dependency of og_test_parity so the
        # generated file is always up to date with scenario_table.h.
        add_executable(scenario_facts_dump
            ${CMAKE_SOURCE_DIR}/tests/parity/scenario_facts_dump_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/fact_predicate.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/state_dump.cpp
        )
        target_include_directories(scenario_facts_dump PRIVATE
            ${CMAKE_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/tests/parity
            ${CMAKE_SOURCE_DIR}/include
        )
        target_compile_features(scenario_facts_dump PRIVATE cxx_std_20)
        target_link_libraries(scenario_facts_dump PRIVATE og_game_test)
        # Coverage builds: libog_game_test.a is compiled with --coverage and
        # leaves undefined references to __gcov_init / __gcov_exit /
        # __gcov_merge_add in the static archive. Link against gcov via
        # --coverage so the standalone executable resolves them. The ASan
        # / TSan presets do NOT route through configure_openglad_library
        # here because pulling in project_sanitizers transitively slows
        # og_test_view's tick budget enough to time out at 180s on CI.
        if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(scenario_facts_dump PRIVATE --coverage)
            target_link_options(scenario_facts_dump PRIVATE --coverage)
        endif()
        add_custom_command(
            OUTPUT  ${CMAKE_SOURCE_DIR}/tests/parity/scenario_facts_generated.json
            COMMAND scenario_facts_dump
                    --out ${CMAKE_SOURCE_DIR}/tests/parity/scenario_facts_generated.json
            DEPENDS scenario_facts_dump
                    ${CMAKE_SOURCE_DIR}/tests/parity/scenario_table.h
            COMMENT "Regenerating scenario_facts_generated.json"
            VERBATIM
        )
        add_custom_target(scenario_facts_generated ALL
            DEPENDS ${CMAKE_SOURCE_DIR}/tests/parity/scenario_facts_generated.json
        )
        add_dependencies(og_test_parity scenario_facts_generated)

        # Standalone smoke runner — Phase 02 verifier and the launcher
        # extended in Phase 07 to enumerate scenarios via --list.
        add_executable(parity_runner_smoke
            ${CMAKE_SOURCE_DIR}/tests/parity/parity_runner_smoke_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/parity_runner.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/state_dump.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/fact_predicate.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/parity_bootstrap.cpp
            ${CMAKE_SOURCE_DIR}/tests/parity/scenario_runtime.cpp
        )
        configure_openglad_library(parity_runner_smoke)
        configure_openglad_sdl_target(parity_runner_smoke)
        target_compile_definitions(parity_runner_smoke PRIVATE
            TESTING
            OG_PARITY_WORKSPACE_ROOT="${CMAKE_SOURCE_DIR}"
        )
        target_include_directories(parity_runner_smoke PRIVATE
            ${CMAKE_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/tests/parity
        )
        target_link_libraries(parity_runner_smoke PRIVATE og_game_test)
        configure_openglad_runtime_target(parity_runner_smoke)
        add_runtime_assets_dependency(parity_runner_smoke)

        og_add_unit_group(og_unit_core FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_frame_rate_config.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_view_layout.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_scale_mode.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_display_state.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_frame_deadline_pacer.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_cadence.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_astar.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_combat_softcap.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_web_back_key.cpp
        )

        og_add_unit_group(og_unit_sim FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_session_raii.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_game_loop_wrapper.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_lobby_server.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_pack_transfer.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_game_server_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_game_client_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport_inprocess.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport_multiplex.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport_relay_ws.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport_websocket_client.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport_websocket_server.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_input_state_net.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_replay.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_event_log.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_world_headless.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_entity.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_zaxis.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_zaxis_fx_floor.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_new_tiles.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_new_tiles_legacy_ids.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_decor.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_decor_art.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_npc_scenario_flags.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_guard_act_type.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_forest_pathing.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_melee_standoff.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_westlands_calibration.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_longseason_calibration.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_difficulty_scaling.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_game_world_entity_ids.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_weather.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_world_snapshot.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_world_snapshot_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_sim_world.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_sim_input_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_control_policy.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_ctf_network.cpp
        )
        if(TARGET og_platform_ws_transport)
            target_link_libraries(og_unit_sim PRIVATE og_platform_ws_transport)
        endif()

        og_add_unit_group(og_unit_emscripten_transport FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport_emscripten_ws.cpp
        )
        target_link_libraries(og_unit_emscripten_transport PRIVATE
            og_platform_emscripten_transport
        )
        target_include_directories(og_unit_emscripten_transport PRIVATE
            ${SRC_DIR}/platform/emscripten
        )

        og_add_unit_group(og_unit_families FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_family_registry.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_cleric.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_druid.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_orc.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_thief.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_picker_common.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_menu_spec.cpp
        )

        og_add_unit_group(og_unit_script FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_script_host.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_script_hooks.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_level_scripts.cpp
        )

        og_add_unit_group(og_unit_entity FILES
            ${CMAKE_SOURCE_DIR}/tests/test_walker_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_movement_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_specials_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_living_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_stats_fright.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_silliness_battery.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smooth_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_coverage_misc.cpp
        )

        og_add_unit_group(og_unit_data FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_classpack_yaml.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_save_data_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_company.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_gparser_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_zip_api.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_campaign_metadata.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_campaign_packs.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_game_mode.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_progression.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_win_shares.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_physfs_wrappers.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_input_actions.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_spectator_mode.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_concept_levels.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_westlands_levels.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_longseason_levels.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_decor_format.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_level_file_io_coverage.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_migrated_campaigns.cpp
        )

        og_add_unit_group(og_unit_ctf FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_ctf_core.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_ctf_respawn.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_ctf_ai.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_ctf_snapshot.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_ctf_levels.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_classic_respawn.cpp
        )

        og_add_unit_group(og_unit_mapgen FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_mapgen_builders.cpp
        )

        og_add_unit_group(og_unit_tower_gen FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_tower_floor_gen.cpp
        )

        og_add_unit_group(og_unit_tower_progression FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_tower_progression.cpp
        )

        og_add_unit_group(og_unit_tower_calibration FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_tower_calibration.cpp
        )

        add_executable(og_unit_server
            ${CMAKE_SOURCE_DIR}/tests/unit/headless_unit_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_headless_server_runtime.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_headless_server_tick_interval.cpp
            ${CMAKE_SOURCE_DIR}/src/test_trace.cpp
            ${SRC_DIR}/platform/text/text_platform_globals.cpp
            ${HEADLESS_SERVER_RUNTIME_SOURCES}
        )
        configure_openglad_library(og_unit_server)
        target_compile_definitions(og_unit_server PRIVATE TESTING)
        target_include_directories(og_unit_server PRIVATE
            ${CMAKE_SOURCE_DIR}/tests
        )
        target_link_libraries(og_unit_server PRIVATE
            og_core
            og_gameplay
            og_resources
            GTest::gtest
            m
            pthread
        )
        configure_openglad_runtime_target(og_unit_server)
        add_runtime_assets_dependency(og_unit_server)
        add_test(NAME og_unit_server COMMAND og_unit_server)
        set_tests_properties(og_unit_server PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180
            LABELS "unit"
        )
        if(OG_SANITIZER_TEST_ENVIRONMENT)
            set_tests_properties(og_unit_server PROPERTIES ENVIRONMENT
                "${OG_SANITIZER_TEST_ENVIRONMENT}"
            )
        endif()

        add_executable(og_unit_headless_platform
            ${CMAKE_SOURCE_DIR}/tests/unit/headless_unit_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_platform_headless.cpp
            ${SRC_DIR}/test_trace.cpp
            ${HEADLESS_TEST_SOURCES}
        )
        configure_openglad_library(og_unit_headless_platform)
        target_compile_definitions(og_unit_headless_platform PRIVATE TESTING)
        target_include_directories(og_unit_headless_platform PRIVATE
            ${CMAKE_SOURCE_DIR}/tests
        )
        target_link_libraries(og_unit_headless_platform PRIVATE
            ${OG_IO_EXTERNAL_LIBS}
            GTest::gtest
            m
            pthread
        )
        configure_openglad_runtime_target(og_unit_headless_platform)
        add_runtime_assets_dependency(og_unit_headless_platform)
        add_test(NAME og_unit_headless_platform COMMAND og_unit_headless_platform)
        set_tests_properties(og_unit_headless_platform PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180
            LABELS "unit"
        )
        if(OG_SANITIZER_TEST_ENVIRONMENT)
            set_tests_properties(og_unit_headless_platform PROPERTIES ENVIRONMENT
                "${OG_SANITIZER_TEST_ENVIRONMENT}"
            )
        endif()

        # --- og_test_curses (ncurses client test suite) ---
        # All curses tests are SDL-free and TTY-free (HeadlessTerminal + FakeClock),
        # so they run in CI like the headless unit binaries. Reuses the curses
        # implementation TUs (minus main) plus its own test main and the
        # production process globals.
        if(OG_CURSES_FOUND AND TARGET og_platform_ws_transport)
            add_executable(og_test_curses
                ${CMAKE_SOURCE_DIR}/tests/curses/curses_test_main.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_glyph_map.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_headless_terminal.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_kitty_keys.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_curses_app.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_curses_input.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_curses_renderer.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_curses_picker_client.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_curses_game_runtime.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_curses_network.cpp
                ${CMAKE_SOURCE_DIR}/tests/curses/test_curses_ctf.cpp
                ${CMAKE_SOURCE_DIR}/src/test_trace.cpp
                ${SRC_DIR}/platform/curses/curses_platform_globals.cpp
                ${OG_CURSES_LIB_SOURCES}
                ${OG_CURSES_MENU_SOURCES}
                ${HEADLESS_SERVER_RUNTIME_SOURCES}
            )
            configure_openglad_library(og_test_curses)
            target_compile_definitions(og_test_curses PRIVATE
                TESTING
                OPENGLAD_CURSES_TEST_EXECUTABLE="$<TARGET_FILE:openglad_curses>"
                OPENGLAD_SERVER_TEST_EXECUTABLE="$<TARGET_FILE:openglad_server>"
            )
            add_dependencies(og_test_curses openglad_curses openglad_server)
            target_include_directories(og_test_curses PRIVATE
                ${CMAKE_SOURCE_DIR}/tests
            )
            if(OG_CURSES_NCURSES_LINK)
                target_link_libraries(og_test_curses PRIVATE ${OG_CURSES_NCURSES_LINK})
            else()
                target_include_directories(og_test_curses PRIVATE ${CURSES_INCLUDE_DIRS})
                target_link_libraries(og_test_curses PRIVATE ${CURSES_LIBRARIES})
            endif()
            target_link_libraries(og_test_curses PRIVATE
                og_core
                og_gameplay
                og_resources
                og_platform_ws_transport
                GTest::gtest
                m
                pthread
            )
            configure_openglad_runtime_target(og_test_curses)
            add_runtime_assets_dependency(og_test_curses)
            add_test(NAME og_test_curses COMMAND og_test_curses)
            set_tests_properties(og_test_curses PROPERTIES
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                TIMEOUT 240
                LABELS "curses"
            )
            if(OG_SANITIZER_TEST_ENVIRONMENT)
                set_tests_properties(og_test_curses PROPERTIES ENVIRONMENT
                    "${OG_SANITIZER_TEST_ENVIRONMENT}"
                )
            endif()

            # Enforce the zero-SDL invariant of the ncurses client in CI: the
            # binary must contain no SDL symbols and not link libSDL3.
            add_test(NAME openglad_curses_link_no_sdl
                COMMAND ${CMAKE_COMMAND} -E env bash
                    ${CMAKE_SOURCE_DIR}/scripts/test_curses_no_sdl.sh
                    $<TARGET_FILE:openglad_curses>
            )
            set_tests_properties(openglad_curses_link_no_sdl PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 60
                LABELS "curses"
            )

            add_test(NAME openglad_curses_cli
                COMMAND ${CMAKE_COMMAND} -E env bash
                    ${CMAKE_SOURCE_DIR}/scripts/test_curses_cli.sh
                    $<TARGET_FILE:openglad_curses>
            )
            set_tests_properties(openglad_curses_cli PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 60
                LABELS "curses"
                RESOURCE_LOCK native_network_stack
            )
        endif()

        # The picker and menu UI suites include the live networking menu/client
        # coverage, and the picker-network integration/unit-sim binaries both
        # initialize IXWebSocket's native net system and transports. On Linux
        # they can trip EBADF/netlink failures when CTest overlaps them in
        # separate processes, so serialize just that shared resource.
        set_tests_properties(
            og_test_picker
            og_test_menu_ui
            og_test_picker_network
            og_unit_sim
            PROPERTIES RESOURCE_LOCK native_network_stack
        )
        if(TARGET og_test_curses)
            set_tests_properties(og_test_curses PROPERTIES
                RESOURCE_LOCK native_network_stack
            )
        endif()
        # og_unit_sim now bundles the live IXWebSocket/relay transport tests. Run
        # alone it finishes in ~1s (loopback handshakes are instant), but the
        # RESOURCE_LOCK above only keeps it off the OTHER network suites — CTest
        # still overlaps it with the heavy non-network integration binaries
        # (og_test_view, og_test_game_core, og_test_level). Under that CPU
        # contention the loopback websocket handshake starves and waits out the
        # transport's network_timeout, blowing past the 180s CTest budget (observed
        # at both -j6 and -j4). RUN_SERIAL takes it off the contended schedule so
        # the transports connect promptly in every lane.
        # ...and give it the same 600s budget the coverage lane uses. The bundled
        # at_12hz loopback-websocket sync test is wall-clock-bound with a 90s
        # internal network_timeout that assumes a generous ctest cap; the default
        # 180s is too tight when a sync stalls and waits that timeout out (the test
        # then blocks — main thread in the 12hz pacing sleep — rather than
        # deadlocking, so it completes given headroom). 600s keeps it green without
        # masking a genuine hang.
        set_tests_properties(og_unit_sim PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)
        # og_unit_data runs ~6s in ci-test but its campaign/battle/round-trip
        # suites multiply under instrumentation: the sanitizer/validation lane
        # needed 420s (block below), and the COVERAGE lane crossed the 180s
        # default on 2026-07-11 when the sprite-footprint and guard-policy
        # round-trip suites landed (og_unit_data (Timeout) on the Coverage CI
        # job). Give it the 420s budget in every lane instead of chasing the
        # threshold lane by lane — the standard lane finishes 70x under it,
        # and a genuine hang still trips well before the job cap.
        set_tests_properties(og_unit_data PROPERTIES TIMEOUT 420)
        # The dedicated picker-network suite is tiny when it runs alone, but
        # its live host/join transport handoff can stall on 4-core CI runners
        # when CTest overlaps it with the other long-running integration
        # binaries. Give it a dedicated budget and run it by itself in every
        # lane instead of relying on incidental scheduler order.
        set_tests_properties(og_test_picker_network PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 420
        )

        # og_test_menu_ui is the slowest integration binary: its injector flows
        # gate on fadeblack animations (~0.75-1s wall clock each), so it runs
        # ~148s standalone against the 180s group default — only ~18% headroom.
        # The difficulty/FX-menu work added ~31 more such waits. In a full
        # parallel ctest run it overlaps the other heavy integration binaries
        # (og_test_view/og_test_game_core/og_test_level) with no serialization,
        # blowing the 180s budget under load and making the standard pre-commit
        # gate (ctest --preset ci-test) intermittently red. The coverage and
        # sanitizer lanes already protect it below; give it the same dedicated
        # budget in EVERY lane, mirroring og_test_picker_network above.
        set_tests_properties(og_test_menu_ui PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 420
        )
        # The Base Camp group now includes the visual smoke matrix in addition
        # to the company-list flows. Both drive wall-clock fades from injector
        # threads, so isolate the combined ~80s binary and leave ample CI
        # headroom just as we do for the older menu UI group.
        set_tests_properties(og_test_basecamp PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 420
        )

        # The level-data suite produces and rewrites temp/scen/scen99.fss while
        # the parity bootstrap mounts the same source-tree scenario directory.
        # Preserve the serial suite's producer-before-consumer ordering and
        # keep those read/write windows from overlapping in parallel lanes.
        set_tests_properties(
            og_test_level
            og_test_parity
            PROPERTIES RESOURCE_LOCK source_scenario_fixtures
        )
        set_tests_properties(og_test_parity PROPERTIES DEPENDS og_test_level)

        if(ENABLE_COVERAGE)
            # These two binaries temporarily replace config fixtures in shared
            # source/build asset trees to exercise missing/invalid-file paths.
            # Coverage CTest now runs concurrently, so keep those mutation
            # windows globally isolated from sibling process startup and the
            # nested Emscripten build.
            set_tests_properties(
                og_test_io
                og_unit_data
                PROPERTIES RUN_SERIAL TRUE
            )
            # Coverage-instrumented UI-heavy binaries can time out on 4-core CI
            # runners when CTest schedules them all at once.
            set_tests_properties(
                og_test_game_core
                og_test_level
                og_test_view
                og_test_picker
                og_test_picker_network
                og_test_menu_ui
                PROPERTIES PROCESSORS 3
            )
            # The game-core integration binary also needs isolation in the
            # coverage lane. Running it beside even one other process on GitHub's
            # 4-core runners can trigger flaky asset/campaign test failures.
            set_tests_properties(og_test_game_core PROPERTIES PROCESSORS 4)
            # The coverage-instrumented unit-sim binary now includes live
            # WebSocket and relay transport tests. On GitHub's 4-core runners
            # it can exceed the coverage workflow timeout budget when CTest
            # schedules it beside other heavyweight binaries, so give it full
            # runner isolation in that lane.
            set_tests_properties(og_unit_sim PROPERTIES PROCESSORS 4)
            # Menu UI coverage still runs substantially slower on GitHub's
            # 4-core runners than it does locally, so match the dedicated
            # coverage workflow budget instead of leaving CTest at 300s here.
            set_tests_properties(og_test_game_core PROPERTIES TIMEOUT 420)
            set_tests_properties(og_test_level PROPERTIES TIMEOUT 420)
            set_tests_properties(og_test_view PROPERTIES TIMEOUT 420)
            set_tests_properties(og_test_menu_ui PROPERTIES TIMEOUT 420)
            set_tests_properties(og_test_picker PROPERTIES TIMEOUT 420)
            set_tests_properties(og_test_picker_network PROPERTIES TIMEOUT 420)
            set_tests_properties(og_unit_sim PROPERTIES TIMEOUT 600)
        endif()

        if(ENABLE_SANITIZERS OR VALIDATE_SERIALIZATION)
            # The gameplay suite now drives the full local transport path.
            # Validation mode round-trips every message and sanitizers magnify
            # the cost enough that the default 180s CTest budget is too tight.
            set_tests_properties(og_test_game_core PROPERTIES TIMEOUT 420)
            # The simulation unit binary now includes the WebSocket, relay,
            # and multiplex transport suites. Serialization validation
            # round-trips every message there too, which can push CI's
            # generic 180s budget over the edge when CTest schedules it
            # beside the heavyweight integration binaries.
            #
            # Match the 600s budget the non-sanitizer (line ~2622) and coverage
            # (line ~2666) lanes already give this binary: the bundled at_12hz
            # loopback-websocket sync test is wall-clock-bound with a 90s
            # internal network_timeout, and under TSan's thread serialization a
            # stalled handshake waits that timeout out — 420s left no headroom
            # for even one stall on top of the sanitizer compute cost, so the
            # TSan lane timed out here while ci-test/coverage stayed green.
            set_tests_properties(og_unit_sim PROPERTIES
                PROCESSORS 2
                TIMEOUT 600
            )
            # og_unit_data bundles the campaign level suites, whose battle
            # smokes and calibration cases run many-tick headless sims. Under
            # ASan/UBSan plus serialization round-tripping (which re-serializes
            # every snapshot each tick) the group's cost multiplies ~30x, so it
            # ran ~6s in ci-test but overran the generic 180s CTest budget in
            # the sanitizer/validation lane. Give it headroom there (the
            # standard lane stays at the 180s default, where it is comfortable).
            set_tests_properties(og_unit_data PROPERTIES
                PROCESSORS 2
                TIMEOUT 420
            )
        endif()
        if(ENABLE_SANITIZERS)
            # Reserve the full 4-core GitHub runner for og_test_game_core.
            # Under ASan/UBSan it can time out only when CTest schedules it
            # beside other heavyweight integration binaries.
            set_tests_properties(og_test_game_core PROPERTIES PROCESSORS 4)
            # The menu UI binary now spends enough time in redraw-heavy flows
            # that ASan/UBSan can push it past the generic 180s CTest budget
            # when GitHub's 4-core runners overlap it with the rest of the
            # integration batch. Run it alone in that lane and match the
            # timeout budget already used for the slower coverage variant.
            set_tests_properties(og_test_menu_ui PROPERTIES
                PROCESSORS 4
                TIMEOUT 420
            )
            # The picker suite now exercises live WebSocket lobby setup and
            # end-to-end host/join handoff paths. Under ASan/UBSan it can hit
            # GitHub's generic 180-second budget when scheduled beside the
            # rest of the UI-heavy integration binaries.
            set_tests_properties(og_test_picker PROPERTIES
                PROCESSORS 3
                TIMEOUT 420
            )
            # The dedicated picker-network suite uses live WebSocket/relay
            # transports to cover the host/join lobby client paths. Under
            # ASan/UBSan it can stall only when CTest overlaps it with the
            # rest of the integration batch, so isolate it in that lane.
            set_tests_properties(og_test_picker_network PROPERTIES
                PROCESSORS 2
                TIMEOUT 420
                RUN_SERIAL TRUE
            )
            # The replay/benchmark binary now exercises honest end-to-end
            # client sync, which remains correct under ASan/UBSan but slows
            # down enough on GitHub's 4-core runners to trip the generic
            # 180-second test budget when scheduled alongside other heavy
            # integration binaries.
            set_tests_properties(og_test_snapshot_benchmark PROPERTIES
                PROCESSORS 3
                TIMEOUT 420
            )
            # ASan/UBSan makes the view integration suite slow enough on
            # GitHub's shared runners to exceed CTest's generic 180s limit
            # when scheduled beside the other heavy integration binaries.
            set_tests_properties(og_test_view PROPERTIES
                PROCESSORS 3
                TIMEOUT 420
            )
        endif()

        set(OPENG_LAD_TEXT_SIM_EXEC_TIMEOUT 55)
        set(OPENG_LAD_TEXT_SIM_CTEST_TIMEOUT 60)
        if(ENABLE_SANITIZERS)
            # ASan/UBSan plus parallel load can push the headless text sim
            # past the normal 55s script timeout without indicating a hang.
            set(OPENG_LAD_TEXT_SIM_EXEC_TIMEOUT 120)
            set(OPENG_LAD_TEXT_SIM_CTEST_TIMEOUT 180)
        endif()

        add_test(NAME openglad_text_sim
            COMMAND ${CMAKE_COMMAND} -E env
                OPENGLAD_TEXT_TIMEOUT=${OPENG_LAD_TEXT_SIM_EXEC_TIMEOUT}
                ${CMAKE_SOURCE_DIR}/scripts/test_text_client.sh
                $<TARGET_FILE:openglad_text>
        )
        set_tests_properties(openglad_text_sim PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT ${OPENG_LAD_TEXT_SIM_CTEST_TIMEOUT}
        )

        if(TARGET openglad_server)
            add_test(NAME openglad_server_cli
                COMMAND ${CMAKE_COMMAND} -E env
                    bash
                    ${CMAKE_SOURCE_DIR}/scripts/test_headless_server_cli.sh
                    $<TARGET_FILE:openglad_server>
            )
            set_tests_properties(openglad_server_cli PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 60
                RESOURCE_LOCK native_network_stack
            )

            if(CMAKE_GENERATOR MATCHES "Ninja")
                add_test(NAME openglad_server_link_no_sdl
                    COMMAND ${CMAKE_COMMAND} -E env
                        bash
                        ${CMAKE_SOURCE_DIR}/scripts/test_headless_server_native_link.sh
                        ${CMAKE_BINARY_DIR}
                )
                set_tests_properties(openglad_server_link_no_sdl PROPERTIES
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    TIMEOUT 60
                )
            endif()
        endif()

        add_test(NAME openglad_sdl_startup_error
            COMMAND ${CMAKE_COMMAND} -E env
                bash
                ${CMAKE_SOURCE_DIR}/scripts/test_sdl_startup_error.sh
                $<TARGET_FILE:openglad>
        )
        set_tests_properties(openglad_sdl_startup_error PROPERTIES
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 60
        )

        if(ENABLE_COVERAGE)
            add_test(NAME openglad_demo_smoke
                COMMAND ${CMAKE_COMMAND} -E env
                    bash
                    ${CMAKE_SOURCE_DIR}/scripts/test_demo_smoke.sh
                    $<TARGET_FILE:openglad_demo>
            )
            set_tests_properties(openglad_demo_smoke PROPERTIES
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                TIMEOUT 120
            )
        endif()

        add_test(NAME openglad_text_picker_interactive
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test_text_picker_interactive.sh $<TARGET_FILE:openglad_text>
        )
        set_tests_properties(openglad_text_picker_interactive PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )

        add_test(NAME openglad_text_unsupported
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test_text_client_unsupported.sh $<TARGET_FILE:openglad_text>
        )
        set_tests_properties(openglad_text_unsupported PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )

        add_test(NAME emscripten_build_test
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test_emscripten_build.sh
        )
        set_tests_properties(emscripten_build_test PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            LABELS "emscripten;build"
            TIMEOUT 300
            SKIP_RETURN_CODE 77
        )
    endif()
endif()

# --------------------------------------------------------------------------
# Fuzz targets (libFuzzer / clang)
# --------------------------------------------------------------------------

option(ENABLE_FUZZING "Build fuzz testing harnesses (requires clang with libFuzzer)" OFF)

if(ENABLE_FUZZING)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ENABLE_FUZZING requires Clang (for libFuzzer support)")
    endif()

    set(FUZZ_SANITIZER_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    set(FUZZ_LINK_FLAGS -fsanitize=fuzzer,address,undefined)

    # Each fuzz target is a standalone executable that links only what it needs.
    # The harnesses use SDL_IOFromConstMem to feed data, so they need SDL3 but
    # not the full game engine.

    function(add_fuzz_target name source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE
            $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
        )
        target_compile_definitions(${name} PRIVATE ${COMMON_DEFINITIONS})
        target_compile_options(${name} PRIVATE ${FUZZ_SANITIZER_FLAGS})
        target_link_options(${name} PRIVATE ${FUZZ_LINK_FLAGS})
    endfunction()

    # fuzz_scenario: exercises .fss scenario binary parsing
    add_fuzz_target(fuzz_scenario ${SRC_DIR}/fuzz/fuzz_scenario.cpp)
    target_link_libraries(fuzz_scenario PRIVATE ${OG_SDL3_TARGET})

    # fuzz_savefile: exercises .gtl save file binary parsing
    add_fuzz_target(fuzz_savefile ${SRC_DIR}/fuzz/fuzz_savefile.cpp)
    target_link_libraries(fuzz_savefile PRIVATE ${OG_SDL3_TARGET})

endif()

# --------------------------------------------------------------------------
# Emscripten target
# --------------------------------------------------------------------------

	if(EMSCRIPTEN)
	    add_library(og_game_web STATIC ${GAME_SOURCES_NO_MAIN})
	    configure_openglad_library(og_game_web)
	    configure_openglad_runtime_target(og_game_web)
	    target_link_libraries(og_game_web PUBLIC ${OG_IO_EXTERNAL_LIBS})
	    # Define __ASYNCIFY__ so that YIELD_SLEEP expands to emscripten_sleep()
	    # instead of a no-op.  The -sASYNCIFY linker flag below enables the
	    # Binaryen transform, but does not define the preprocessor macro.
	    target_compile_definitions(og_game_web PRIVATE __ASYNCIFY__)

    # web_touch_bridge.cpp compiles into the executable directly, NOT (only)
    # the og_game_web archive: nothing in C++ references its
    # EMSCRIPTEN_KEEPALIVE exports, and the linker never pulls an archive
    # member with no referenced symbols — KEEPALIVE cannot save an object
    # that is not linked at all. As a direct source its exports always land
    # in Module for the shell's touch overlay bridge.
    add_executable(play
        ${SRC_DIR}/platform/sdl/glad.cpp
        ${SRC_DIR}/platform/emscripten/web_touch_bridge.cpp
    )
    configure_openglad_library(play)
    target_link_libraries(play PRIVATE og_game_web)
    configure_openglad_runtime_target(play)
    target_compile_definitions(play PRIVATE __ASYNCIFY__)

    # Emscripten 6.0.2 flipped GROWABLE_ARRAYBUFFERS to default-on; with
    # ALLOW_MEMORY_GROWTH the heap becomes a resizable ArrayBuffer and Chrome's
    # TextDecoder.decode throws "ArrayBuffer must not be resizable" inside
    # UTF8ArrayToString (getUnsharedTextDecoderView only guards SHARED_MEMORY),
    # aborting boot. Force it off to restore pre-6.0.2 behavior; drop this once
    # upstream fixes the decode path. Guarded: the setting first appears in
    # emscripten 6.0.x and older SDKs error on unknown -s flags (CI now pins
    # emsdk 6.0.3, so this guard is active there).
    if(EMSCRIPTEN_VERSION VERSION_GREATER_EQUAL "6.0.1")
        target_link_options(play PRIVATE -sGROWABLE_ARRAYBUFFERS=0)
    endif()

    target_link_options(play PRIVATE
        "SHELL:--bind"
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sASYNCIFY
        -sASYNCIFY_STACK_SIZE=65536
        # With -fexceptions an exception that escapes every C++ handler shows
        # up as an uncaught JS exception whose message is a raw exception
        # pointer; this keeps type and what() visible in the browser console.
        -sEXCEPTION_STACK_TRACES=1
        -sEXIT_RUNTIME=0
        "SHELL:-sMINIFY_HTML=0"
        -lwebsocket.js
        -lidbfs.js
        "SHELL:-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\"]"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/cfg@/cfg"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/pix@/pix"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/sound@/sound"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/builtin@/builtin"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/packs@/packs"
    )

    set_target_properties(play PROPERTIES
        LINK_DEPENDS
            "${CMAKE_SOURCE_DIR}/web/shell.html;${CMAKE_SOURCE_DIR}/web/manifest.webmanifest;${CMAKE_SOURCE_DIR}/web/icon.svg;${CMAKE_SOURCE_DIR}/web/icon-192.png;${CMAKE_SOURCE_DIR}/web/icon-512.png;${CMAKE_SOURCE_DIR}/web/apple-touch-icon.png"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
        SUFFIX ".html"
    )

    add_custom_command(
        TARGET play
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/web/index.html
            ${CMAKE_SOURCE_DIR}/web/help.html
            ${CMAKE_SOURCE_DIR}/web/_worker.js
            ${CMAKE_SOURCE_DIR}/web/manifest.webmanifest
            ${CMAKE_SOURCE_DIR}/web/icon.svg
            ${CMAKE_SOURCE_DIR}/web/icon-192.png
            ${CMAKE_SOURCE_DIR}/web/icon-512.png
            ${CMAKE_SOURCE_DIR}/web/apple-touch-icon.png
            $<TARGET_FILE_DIR:play>
        COMMENT "Copying web landing page assets to dist/"
        VERBATIM
    )
endif()
