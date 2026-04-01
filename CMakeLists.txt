cmake_minimum_required(VERSION 3.25)
project(OpenGlad VERSION 1.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckCXXSourceCompiles)

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

# --------------------------------------------------------------------------
# Source file lists
# --------------------------------------------------------------------------

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

set(OG_NETWORK_SOURCES)
if(NOT EMSCRIPTEN)
    list(APPEND OG_NETWORK_SOURCES
        ${SRC_DIR}/platform/sdl/net_transport_websocket_client.cpp
        ${SRC_DIR}/platform/sdl/net_transport_websocket_server.cpp
        ${SRC_DIR}/platform/sdl/net_transport_relay_ws.cpp
    )
endif()

# Main game C++ sources
set(GAME_SOURCES
    ${SRC_DIR}/platform/sdl/glad.cpp
    ${SRC_DIR}/platform/sdl/game_loop.cpp
    ${SRC_DIR}/platform/sdl/frame_pacing.cpp
    ${SRC_DIR}/platform/game_context.cpp
    ${SRC_DIR}/platform/sdl/game_session.cpp
    ${SRC_DIR}/platform/sdl/glad_gameplay.cpp
    ${SRC_DIR}/platform/sdl/local_transport_shadow.cpp
    ${SRC_DIR}/platform/sdl/picker_lobby_network_client.cpp
    ${OG_NETWORK_SOURCES}
    ${SRC_DIR}/interface/score_panel.cpp
    ${SRC_DIR}/platform/sdl/screen_lifecycle.cpp
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/core/runtime_trace.cpp
    ${SRC_DIR}/interface/ui/button.cpp
    ${SRC_DIR}/gameplay/effect.cpp
    ${SRC_DIR}/gameplay/family_registry.cpp
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
    ${SRC_DIR}/interface/ui/menu_model.cpp
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
            ${THIRD_PARTY_DIR}/physfs/extras/physfsrwops.c
		    ${SRC_DIR}/resources/io/platform_io_common.cpp
		    ${SRC_DIR}/resources/io/filesystem.cpp
		    ${SRC_DIR}/resources/io/physfs_api.cpp
		    ${SRC_DIR}/resources/io/zip_api.cpp
		    ${SRC_DIR}/resources/io/yaml_stream.cpp
		    ${SRC_DIR}/resources/io/og_file.cpp
		    ${SRC_DIR}/resources/gparser.cpp
		    ${SRC_DIR}/resources/gloader.cpp
    ${SRC_DIR}/resources/level_file_io.cpp
    ${SRC_DIR}/resources/our_palette.cpp
    ${SRC_DIR}/resources/pixie_data.cpp
    ${SRC_DIR}/resources/save_data.cpp
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
    ${SRC_DIR}/gameplay/gameplay_context.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/gameplay/world_snapshot.cpp
    ${SRC_DIR}/gameplay/sim_input_handler.cpp
    ${SRC_DIR}/platform/sdl/input_event_bridge.cpp
    ${SRC_DIR}/interface/walker_render_bridge.cpp
    ${SRC_DIR}/interface/sdl_context_services.cpp
    ${SRC_DIR}/interface/cheat_handler.cpp
    ${SRC_DIR}/interface/render/sdl_level_render.cpp
    ${THIRD_PARTY_DIR}/lodepng/lodepng.cpp
    ${SRC_DIR}/gameplay/input_state_net.cpp
    ${SRC_DIR}/gameplay/replay.cpp
)

set(GAME_SOURCES_NO_MAIN ${GAME_SOURCES})
list(REMOVE_ITEM GAME_SOURCES_NO_MAIN ${SRC_DIR}/platform/sdl/glad.cpp)
if(EMSCRIPTEN)
    list(APPEND GAME_SOURCES_NO_MAIN
        ${SRC_DIR}/platform/emscripten/net_transport_emscripten_ws.cpp
        ${SRC_DIR}/platform/emscripten/net_transport_relay_ws.cpp
    )
endif()

# Internal module decomposition (Phase 1 architecture foundations).
# Keep these lists in sync with GAME_SOURCES_NO_MAIN so targets remain behaviorally equivalent.
set(OG_CORE_SOURCES
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/core/runtime_trace.cpp
    ${SRC_DIR}/platform/sdl/frame_pacing.cpp
    ${SRC_DIR}/core/util.cpp
)

set(OG_SIM_SOURCES
    ${SRC_DIR}/gameplay/sim_entity.cpp
    ${SRC_DIR}/gameplay/sim_event_log.cpp
    ${SRC_DIR}/gameplay/game_server.cpp
    ${SRC_DIR}/gameplay/lobby_server.cpp
    ${SRC_DIR}/gameplay/game_client.cpp
    ${SRC_DIR}/gameplay/net_transport.cpp
    ${SRC_DIR}/gameplay/net_transport_inprocess.cpp
    ${SRC_DIR}/gameplay/world_snapshot.cpp
    ${SRC_DIR}/gameplay/input_state_net.cpp
    ${SRC_DIR}/gameplay/replay.cpp
)

set(OG_DATA_SOURCES
    ${SRC_DIR}/resources/gparser.cpp
    ${SRC_DIR}/resources/gloader.cpp
    ${SRC_DIR}/resources/level_file_io.cpp
    ${SRC_DIR}/resources/our_palette.cpp
    ${SRC_DIR}/resources/pixie_data.cpp
    ${SRC_DIR}/resources/save_data.cpp
)

set(OG_ENTITIES_SOURCES
    ${SRC_DIR}/gameplay/effect.cpp
    ${SRC_DIR}/gameplay/family_registry.cpp
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
    ${SRC_DIR}/platform/sdl/screen_lifecycle.cpp
    ${SRC_DIR}/interface/screen.cpp
    ${SRC_DIR}/interface/platform_bridge.cpp
    ${SRC_DIR}/interface/guy_create.cpp
    ${SRC_DIR}/gameplay/stats.cpp
    ${SRC_DIR}/gameplay/gameplay_context.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/gameplay/sim_input_handler.cpp
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
    ${SRC_DIR}/interface/ui/menu_model.cpp
    ${SRC_DIR}/interface/ui/picker_lobby_client.cpp
    ${SRC_DIR}/interface/ui/picker_lobby_network_client.cpp
    ${SRC_DIR}/interface/ui/picker_common.cpp
    ${SRC_DIR}/interface/ui/picker_state.cpp
    ${SRC_DIR}/interface/ui/results_screen.cpp
)

set(OG_IO_SDL_SOURCES
    ${SRC_DIR}/resources/platform_io.cpp
    ${THIRD_PARTY_DIR}/physfs/extras/physfsrwops.c
)

set(OG_IO_SOURCES
    ${SRC_DIR}/resources/io/platform_io_common.cpp
    ${SRC_DIR}/resources/io/filesystem.cpp
    ${SRC_DIR}/resources/io/physfs_api.cpp
    ${SRC_DIR}/resources/io/zip_api.cpp
    ${SRC_DIR}/resources/io/yaml_stream.cpp
    ${SRC_DIR}/resources/io/og_file.cpp
    ${THIRD_PARTY_DIR}/lodepng/lodepng.cpp
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
    ${SRC_DIR}/gameplay/net_transport_multiplex.cpp
    ${SRC_DIR}/gameplay/stats.cpp
    ${SRC_DIR}/gameplay/gameplay_context.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/gameplay/sim_input_handler.cpp
    ${SRC_DIR}/gameplay/smooth.cpp
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

# External C++ sources
set(EXT_CXX_SOURCES
    ${THIRD_PARTY_DIR}/micropather/micropather.cpp
    ${THIRD_PARTY_DIR}/yam/yam.cpp
)

# External C sources - libyaml
set(LIBYAML_SOURCES
    ${THIRD_PARTY_DIR}/libyaml/src/api.c
    ${THIRD_PARTY_DIR}/libyaml/src/loader.c
    ${THIRD_PARTY_DIR}/libyaml/src/parser.c
    ${THIRD_PARTY_DIR}/libyaml/src/reader.c
    ${THIRD_PARTY_DIR}/libyaml/src/scanner.c
    ${THIRD_PARTY_DIR}/libyaml/src/emitter.c
    ${THIRD_PARTY_DIR}/libyaml/src/dumper.c
    ${THIRD_PARTY_DIR}/libyaml/src/writer.c
)

# External C sources - physfs (3.2.0)
set(PHYSFS_SOURCES
    ${THIRD_PARTY_DIR}/physfs/physfs.c
    ${THIRD_PARTY_DIR}/physfs/physfs_byteorder.c
    ${THIRD_PARTY_DIR}/physfs/physfs_unicode.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_dir.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_unpacked.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_grp.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_hog.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_mvl.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_qpak.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_wad.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_zip.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_slb.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_vdf.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_iso9660.c
    ${THIRD_PARTY_DIR}/physfs/physfs_archiver_7z.c
    ${THIRD_PARTY_DIR}/physfs/physfs_platform_posix.c
    ${THIRD_PARTY_DIR}/physfs/physfs_platform_unix.c
    ${THIRD_PARTY_DIR}/physfs/physfs_platform_windows.c
)

# External C sources - zlib
set(ZLIB_SOURCES
    ${THIRD_PARTY_DIR}/physfs/zlib123/adler32.c
    ${THIRD_PARTY_DIR}/physfs/zlib123/compress.c
    ${THIRD_PARTY_DIR}/physfs/zlib123/crc32.c
    ${THIRD_PARTY_DIR}/physfs/zlib123/deflate.c
    ${THIRD_PARTY_DIR}/physfs/zlib123/inffast.c
    ${THIRD_PARTY_DIR}/physfs/zlib123/inflate.c
    ${THIRD_PARTY_DIR}/physfs/zlib123/inftrees.c
    ${THIRD_PARTY_DIR}/physfs/zlib123/trees.c
    ${THIRD_PARTY_DIR}/physfs/zlib123/zutil.c
)

# External C sources - libzip (1.11.3)
set(LIBZIP_SOURCES
    ${THIRD_PARTY_DIR}/libzip/zip_add.c
    ${THIRD_PARTY_DIR}/libzip/zip_add_dir.c
    ${THIRD_PARTY_DIR}/libzip/zip_add_entry.c
    ${THIRD_PARTY_DIR}/libzip/zip_algorithm_deflate.c
    ${THIRD_PARTY_DIR}/libzip/zip_buffer.c
    ${THIRD_PARTY_DIR}/libzip/zip_close.c
    ${THIRD_PARTY_DIR}/libzip/zip_delete.c
    ${THIRD_PARTY_DIR}/libzip/zip_dir_add.c
    ${THIRD_PARTY_DIR}/libzip/zip_dirent.c
    ${THIRD_PARTY_DIR}/libzip/zip_discard.c
    ${THIRD_PARTY_DIR}/libzip/zip_entry.c
    ${THIRD_PARTY_DIR}/libzip/zip_error.c
    ${THIRD_PARTY_DIR}/libzip/zip_error_clear.c
    ${THIRD_PARTY_DIR}/libzip/zip_error_get.c
    ${THIRD_PARTY_DIR}/libzip/zip_error_get_sys_type.c
    ${THIRD_PARTY_DIR}/libzip/zip_error_strerror.c
    ${THIRD_PARTY_DIR}/libzip/zip_error_to_str.c
    ${THIRD_PARTY_DIR}/libzip/zip_extra_field.c
    ${THIRD_PARTY_DIR}/libzip/zip_extra_field_api.c
    ${THIRD_PARTY_DIR}/libzip/zip_fclose.c
    ${THIRD_PARTY_DIR}/libzip/zip_fdopen.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_add.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_error_clear.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_error_get.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_get_comment.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_get_external_attributes.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_get_offset.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_rename.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_replace.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_set_comment.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_set_encryption.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_set_external_attributes.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_set_mtime.c
    ${THIRD_PARTY_DIR}/libzip/zip_file_strerror.c
    ${THIRD_PARTY_DIR}/libzip/zip_fopen.c
    ${THIRD_PARTY_DIR}/libzip/zip_fopen_encrypted.c
    ${THIRD_PARTY_DIR}/libzip/zip_fopen_index.c
    ${THIRD_PARTY_DIR}/libzip/zip_fopen_index_encrypted.c
    ${THIRD_PARTY_DIR}/libzip/zip_fread.c
    ${THIRD_PARTY_DIR}/libzip/zip_fseek.c
    ${THIRD_PARTY_DIR}/libzip/zip_ftell.c
    ${THIRD_PARTY_DIR}/libzip/zip_get_archive_comment.c
    ${THIRD_PARTY_DIR}/libzip/zip_get_archive_flag.c
    ${THIRD_PARTY_DIR}/libzip/zip_get_encryption_implementation.c
    ${THIRD_PARTY_DIR}/libzip/zip_get_file_comment.c
    ${THIRD_PARTY_DIR}/libzip/zip_get_name.c
    ${THIRD_PARTY_DIR}/libzip/zip_get_num_entries.c
    ${THIRD_PARTY_DIR}/libzip/zip_get_num_files.c
    ${THIRD_PARTY_DIR}/libzip/zip_hash.c
    ${THIRD_PARTY_DIR}/libzip/zip_io_util.c
    ${THIRD_PARTY_DIR}/libzip/zip_libzip_version.c
    ${THIRD_PARTY_DIR}/libzip/zip_memdup.c
    ${THIRD_PARTY_DIR}/libzip/zip_name_locate.c
    ${THIRD_PARTY_DIR}/libzip/zip_new.c
    ${THIRD_PARTY_DIR}/libzip/zip_open.c
    ${THIRD_PARTY_DIR}/libzip/zip_pkware.c
    ${THIRD_PARTY_DIR}/libzip/zip_progress.c
    ${THIRD_PARTY_DIR}/libzip/zip_random_unix.c
    ${THIRD_PARTY_DIR}/libzip/zip_rename.c
    ${THIRD_PARTY_DIR}/libzip/zip_replace.c
    ${THIRD_PARTY_DIR}/libzip/zip_set_archive_comment.c
    ${THIRD_PARTY_DIR}/libzip/zip_set_archive_flag.c
    ${THIRD_PARTY_DIR}/libzip/zip_set_default_password.c
    ${THIRD_PARTY_DIR}/libzip/zip_set_file_comment.c
    ${THIRD_PARTY_DIR}/libzip/zip_set_file_compression.c
    ${THIRD_PARTY_DIR}/libzip/zip_set_name.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_accept_empty.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_begin_write.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_begin_write_cloning.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_buffer.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_call.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_close.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_commit_write.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_compress.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_crc.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_error.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_file_common.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_file_stdio.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_file_stdio_named.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_free.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_function.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_get_dostime.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_get_file_attributes.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_is_deleted.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_layered.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_open.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_pass_to_lower_layer.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_pkware_decode.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_pkware_encode.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_read.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_remove.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_rollback_write.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_seek.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_seek_write.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_stat.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_supports.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_tell.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_tell_write.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_window.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_write.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_zip.c
    ${THIRD_PARTY_DIR}/libzip/zip_source_zip_new.c
    ${THIRD_PARTY_DIR}/libzip/zip_stat.c
    ${THIRD_PARTY_DIR}/libzip/zip_stat_index.c
    ${THIRD_PARTY_DIR}/libzip/zip_stat_init.c
    ${THIRD_PARTY_DIR}/libzip/zip_strerror.c
    ${THIRD_PARTY_DIR}/libzip/zip_string.c
    ${THIRD_PARTY_DIR}/libzip/zip_unchange.c
    ${THIRD_PARTY_DIR}/libzip/zip_unchange_all.c
    ${THIRD_PARTY_DIR}/libzip/zip_unchange_archive.c
    ${THIRD_PARTY_DIR}/libzip/zip_unchange_data.c
    ${THIRD_PARTY_DIR}/libzip/zip_utf-8.c
    ${THIRD_PARTY_DIR}/libzip/zip_err_str.c
)

# All external C sources combined
set(EXTERNAL_C_SOURCES
    ${LIBYAML_SOURCES}
    ${PHYSFS_SOURCES}
    ${ZLIB_SOURCES}
    ${LIBZIP_SOURCES}
)

# --------------------------------------------------------------------------
# Common include directories and compile definitions
# --------------------------------------------------------------------------

set(OG_PUBLIC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(OG_THIRD_PARTY_INCLUDE_DIRS
    ${THIRD_PARTY_DIR}/ixwebsocket
    ${THIRD_PARTY_DIR}/micropather
    ${THIRD_PARTY_DIR}/yam
    ${THIRD_PARTY_DIR}/libyaml/include
    ${THIRD_PARTY_DIR}/physfs
    ${THIRD_PARTY_DIR}/physfs/extras
    ${THIRD_PARTY_DIR}/physfs/zlib123
    ${THIRD_PARTY_DIR}/libzip
    ${THIRD_PARTY_DIR}/lodepng
)

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
    YAML_DECLARE_STATIC
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
        target_link_libraries(${target} PRIVATE PkgConfig::SDL2)
    endif()
endfunction()

function(configure_openglad_runtime_target target)
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE
            -sUSE_SDL=2
            -sUSE_SDL_MIXER=2
            "SHELL:-sSDL2_MIXER_FORMATS=[\"wav\"]"
            -Wno-constant-conversion
            -Wno-parentheses-equality
            -Wno-pointer-bool-conversion
        )
        target_link_options(${target} PRIVATE
            -sUSE_SDL=2
            -sUSE_SDL_MIXER=2
            "SHELL:-sSDL2_MIXER_FORMATS=[\"wav\"]"
        )
    else()
        # Native builds link SDL/system deps via og_runtime_deps, typically pulled
        # in transitively by og_game / og_game_test. Keep this function a no-op
        # on native platforms to avoid brittle link-order issues with static libs.
    endif()
endfunction()

function(configure_openglad_external_target target)
    target_include_directories(${target} PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Vendored third-party code: keep warnings actionable in OpenGlad code by silencing
        # warnings from upstream C/C++ sources. Track upstream versions in
        # third_party/VENDORED_LIBS.md and prefer upgrades over refactors-in-place.
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

# Vendor header leak check: no third-party headers in public or internal code.
add_custom_target(check_vendor_leaks
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_vendor_leaks.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking vendor header isolation"
    VERBATIM
)

# --------------------------------------------------------------------------
# External dependency targets
# --------------------------------------------------------------------------

if(NOT EMSCRIPTEN)
    find_package(PkgConfig QUIET)

    set(OG_NATIVE_SDL_AVAILABLE OFF)
    set(OG_NATIVE_SDL_REQUESTED ON)
    if(CMAKE_DISABLE_FIND_PACKAGE_SDL2 OR
       CMAKE_DISABLE_FIND_PACKAGE_SDL2_MIXER OR
       CMAKE_DISABLE_FIND_PACKAGE_SDL2_mixer)
        set(OG_NATIVE_SDL_REQUESTED OFF)
    endif()
    if(OG_NATIVE_SDL_REQUESTED AND PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2 SDL2_mixer)
        if(SDL2_FOUND)
            set(OG_NATIVE_SDL_AVAILABLE ON)
        else()
            message(STATUS "SDL2/SDL2_mixer not found; SDL-native targets require SDL to build")
        endif()
    elseif(OG_NATIVE_SDL_REQUESTED)
        message(STATUS "PkgConfig not found; SDL-native targets require SDL to build")
    else()
        message(STATUS "SDL2 lookup disabled; configuring headless/native non-SDL targets")
    endif()

    # System/runtime link deps (kept as a target so we can control ordering).
    add_library(og_runtime_deps INTERFACE)
    if(OG_NATIVE_SDL_AVAILABLE)
        target_link_libraries(og_runtime_deps INTERFACE PkgConfig::SDL2 m pthread)
    else()
        target_link_libraries(og_runtime_deps INTERFACE m pthread)
    endif()

    # Native-only WebSocket dependency. Emscripten uses the browser WebSocket API.
    set(BUILD_DEMO OFF)
    set(USE_TLS OFF)
    set(USE_ZLIB OFF)
    set(IXWEBSOCKET_INSTALL OFF)
    add_subdirectory(${THIRD_PARTY_DIR}/ixwebsocket)
    get_target_property(IXWEBSOCKET_PUBLIC_HEADERS ixwebsocket PUBLIC_HEADER)
    if(IXWEBSOCKET_PUBLIC_HEADERS)
        set(IXWEBSOCKET_PUBLIC_HEADERS_ABSOLUTE)
        foreach(ixwebsocket_header IN LISTS IXWEBSOCKET_PUBLIC_HEADERS)
            if(IS_ABSOLUTE "${ixwebsocket_header}")
                list(APPEND IXWEBSOCKET_PUBLIC_HEADERS_ABSOLUTE "${ixwebsocket_header}")
            else()
                list(APPEND IXWEBSOCKET_PUBLIC_HEADERS_ABSOLUTE
                    "${THIRD_PARTY_DIR}/ixwebsocket/${ixwebsocket_header}"
                )
            endif()
        endforeach()
        set_target_properties(ixwebsocket PROPERTIES
            PUBLIC_HEADER "${IXWEBSOCKET_PUBLIC_HEADERS_ABSOLUTE}"
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ixwebsocket PRIVATE -w)
    endif()

    add_library(og_ext_ixwebsocket INTERFACE)
    target_link_libraries(og_ext_ixwebsocket INTERFACE ixwebsocket)
endif()

add_library(og_ext_yaml STATIC ${LIBYAML_SOURCES})
configure_openglad_external_target(og_ext_yaml)
target_compile_definitions(og_ext_yaml PRIVATE HAVE_CONFIG_H=1)

add_library(og_ext_zlib STATIC ${ZLIB_SOURCES})
configure_openglad_external_target(og_ext_zlib)

add_library(og_ext_libzip STATIC ${LIBZIP_SOURCES})
configure_openglad_external_target(og_ext_libzip)
target_link_libraries(og_ext_libzip PUBLIC og_ext_zlib)

add_library(og_ext_physfs STATIC ${PHYSFS_SOURCES})
configure_openglad_external_target(og_ext_physfs)
if(EMSCRIPTEN)
    # physfsrwops.c needs SDL headers
    target_compile_options(og_ext_physfs PRIVATE -sUSE_SDL=2)
endif()
if(WIN32)
    # physfs_platform_windows.c needs shell32 for SHGetFolderPathW
    target_link_libraries(og_ext_physfs PRIVATE shell32)
endif()
# PhysFS 3.x uses built-in miniz for ZIP support; no external zlib/libzip needed

add_library(og_ext_micropather STATIC ${THIRD_PARTY_DIR}/micropather/micropather.cpp)
configure_openglad_external_target(og_ext_micropather)

add_library(og_ext_yam STATIC ${THIRD_PARTY_DIR}/yam/yam.cpp)
configure_openglad_external_target(og_ext_yam)

set(OG_IO_EXTERNAL_LIBS
    og_ext_yam
    og_ext_yaml
    og_ext_physfs
    og_ext_libzip
)

set(OG_EXTERNAL_LIBS
    og_ext_micropather
)

if(NOT EMSCRIPTEN)
    # --- component targets (Phase 12) ---
    prepare_component_include_root(OG_CORE_INCLUDE_ROOT core
        core interface platform)
    prepare_component_include_root(OG_GAMEPLAY_INCLUDE_ROOT gameplay
        core gameplay platform)
    prepare_component_include_root(OG_RESOURCES_INCLUDE_ROOT resources
        core gameplay resources)
    prepare_component_include_root(OG_INTERFACE_INCLUDE_ROOT interface
        core gameplay resources interface platform)

    add_library(og_core STATIC ${OG_CORE_SOURCES})
    configure_component_library(og_core "${OG_CORE_INCLUDE_ROOT}")

    add_library(og_gameplay STATIC ${OG_GAMEPLAY_COMPONENT_SOURCES})
    configure_component_library(og_gameplay "${OG_GAMEPLAY_INCLUDE_ROOT}")
    target_include_directories(og_gameplay PRIVATE ${THIRD_PARTY_DIR}/micropather)
    target_link_libraries(og_gameplay PUBLIC og_core ${OG_EXTERNAL_LIBS} og_ext_zlib)

    add_library(og_resources STATIC ${OG_RESOURCES_COMPONENT_SOURCES})
    configure_component_library(og_resources "${OG_RESOURCES_INCLUDE_ROOT}")
    target_include_directories(og_resources PRIVATE
        ${THIRD_PARTY_DIR}/yam
        ${THIRD_PARTY_DIR}/libyaml/include
        ${THIRD_PARTY_DIR}/physfs
        ${THIRD_PARTY_DIR}/physfs/extras
        ${THIRD_PARTY_DIR}/libzip
        ${THIRD_PARTY_DIR}/lodepng
    )
    target_link_libraries(og_resources PUBLIC
        og_core
        og_gameplay
        ${OG_IO_EXTERNAL_LIBS}
    )

    add_library(og_interface STATIC ${OG_INTERFACE_COMPONENT_SOURCES})
    configure_component_library(og_interface "${OG_INTERFACE_INCLUDE_ROOT}")
    configure_openglad_sdl_target(og_interface)
    target_link_libraries(og_interface PUBLIC
        og_core
        og_gameplay
        og_resources
    )

    if(OG_NETWORK_SOURCES)
        add_library(og_platform_ws_transport STATIC ${OG_NETWORK_SOURCES})
        configure_component_library(og_platform_ws_transport "${OG_PUBLIC_INCLUDE_DIR}")
        if(NOT EMSCRIPTEN)
            target_link_libraries(og_platform_ws_transport PUBLIC ixwebsocket)
        endif()
        add_dependencies(og_platform_ws_transport check_vendor_leaks)
    endif()

    add_library(og_platform_sdl STATIC ${OG_PLATFORM_SDL_COMPONENT_SOURCES})
    configure_component_library(og_platform_sdl "${OG_PUBLIC_INCLUDE_DIR}")
    configure_openglad_sdl_target(og_platform_sdl)
    target_include_directories(og_platform_sdl PRIVATE
        ${OG_THIRD_PARTY_INCLUDE_DIRS}
        ${THIRD_PARTY_DIR}/yam
        ${THIRD_PARTY_DIR}/libyaml/include
        ${THIRD_PARTY_DIR}/physfs
        ${THIRD_PARTY_DIR}/physfs/extras
        ${THIRD_PARTY_DIR}/libzip
    )
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
        target_link_libraries(og_platform_sdl PRIVATE ixwebsocket)
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
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/platform/text/text_picker.cpp
        ${SRC_DIR}/platform/text/text_protocol.cpp
        ${SRC_DIR}/interface/ui/menu_model.cpp
        ${SRC_DIR}/interface/ui/picker_common.cpp
        ${SRC_DIR}/interface/ui/picker_state.cpp
        # core
        ${OG_CORE_SOURCES}
        # sim
        ${OG_SIM_SOURCES}
        # data
        ${OG_DATA_SOURCES}
        # entities
        ${OG_ENTITIES_SOURCES}
        # input (SDL-free data structs only)
        ${SRC_DIR}/interface/input/input_state.cpp
        # io (shared SDL-free helpers + PhysFS/zip/yaml)
        ${SRC_DIR}/resources/io/platform_io_common.cpp
        ${SRC_DIR}/resources/io/filesystem.cpp
        ${SRC_DIR}/resources/io/physfs_api.cpp
        ${SRC_DIR}/resources/io/zip_api.cpp
        ${SRC_DIR}/resources/io/yaml_stream.cpp
        ${SRC_DIR}/resources/io/og_file.cpp
        ${SRC_DIR}/resources/our_palette.cpp
        ${THIRD_PARTY_DIR}/lodepng/lodepng.cpp
        # runtime (SDL-free subset)
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/gameplay/gameplay_context.cpp
        ${SRC_DIR}/gameplay/game_world.cpp
        ${SRC_DIR}/gameplay/sim_input_handler.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        ${SRC_DIR}/gameplay/smooth.cpp
        ${SRC_DIR}/gameplay/stats.cpp
    )

    set(HEADLESS_SERVER_RUNTIME_SOURCES
        ${SRC_DIR}/server/headless_server_runtime.cpp
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
        ${HEADLESS_SERVER_RUNTIME_SOURCES}
    )

    add_executable(openglad_text ${HEADLESS_SOURCES})
    target_include_directories(openglad_text PUBLIC
        $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
    )
    target_include_directories(openglad_text PRIVATE
        ${THIRD_PARTY_DIR}/micropather
        ${THIRD_PARTY_DIR}/yam
        ${THIRD_PARTY_DIR}/libyaml/include
        ${THIRD_PARTY_DIR}/physfs
        ${THIRD_PARTY_DIR}/physfs/extras
        ${THIRD_PARTY_DIR}/libzip
        ${THIRD_PARTY_DIR}/lodepng
    )
    target_compile_definitions(openglad_text PRIVATE
        ${COMMON_DEFINITIONS}
    )
    target_link_libraries(openglad_text PRIVATE
        project_warnings
        project_sanitizers
        og_ext_micropather
        ${OG_IO_EXTERNAL_LIBS}
        m
        pthread
    )
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

    install(TARGETS
        project_warnings
        project_sanitizers
        og_core
        og_gameplay
        og_resources
        og_interface
        og_platform_sdl
        og_ext_micropather
        og_ext_yam
        og_ext_yaml
        og_ext_zlib
        og_ext_libzip
        og_ext_physfs
        openglad
        EXPORT OpenGladTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(NOT EMSCRIPTEN)
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
                ${CMAKE_SOURCE_DIR}/extra_campaigns
            DESTINATION ${CMAKE_INSTALL_DATADIR}/openglad
        )
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
            ${CMAKE_SOURCE_DIR}/tests/test_save_menu.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_fairy_death.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_overpowered_team.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_filesystem.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smoother.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_primitives.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_text_rendering.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_campaign_and_level_picker.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_intro_smoke.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_dirty_tracking_safety.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_sai2x_scaler.cpp
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
                ${OG_THIRD_PARTY_INCLUDE_DIRS}
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
        target_include_directories(og_game_test PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
        target_compile_definitions(og_game_test PRIVATE TESTING)
        target_link_libraries(og_game_test PUBLIC ${OG_EXTERNAL_LIBS} ${OG_IO_EXTERNAL_LIBS} og_runtime_deps)
        if(NOT EMSCRIPTEN)
            target_link_libraries(og_game_test PUBLIC ixwebsocket)
        endif()
        add_dependencies(og_game_test check_vendor_leaks)

        enable_testing()

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
        )

        og_add_test_group(og_test_snapshot_safety FILES
            test_dirty_tracking_safety.cpp
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
            test_video_buffers.cpp
            test_video_draw.cpp
            test_video_extended.cpp
            test_video_extra.cpp
            test_video_fade.cpp
            test_video_modes_more.cpp
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
        )

        og_add_test_group(og_test_menu_ui FILES
            test_back_to_mainmenu.cpp
            test_menu.cpp
            test_menu_layout.cpp
            test_new_game.cpp
            test_hire_team.cpp
            test_train_team.cpp
            test_save_menu.cpp
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

        og_add_test_group(og_test_input FILES
            test_input.cpp
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

        og_add_unit_group(og_unit_sim FILES
            ${CMAKE_SOURCE_DIR}/tests/unit/test_session_raii.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_game_loop_wrapper.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_lobby_server.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport.cpp
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
            ${CMAKE_SOURCE_DIR}/tests/unit/test_game_world_entity_ids.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_world_snapshot.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_sim_world.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_sim_input_unit.cpp
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
        )

        og_add_unit_group(og_unit_entity FILES
            ${CMAKE_SOURCE_DIR}/tests/test_walker_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_movement_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_specials_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_living_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smooth_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_coverage_misc.cpp
        )

        og_add_unit_group(og_unit_data FILES
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_save_data_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_gparser_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_zip_api.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_physfs_wrappers.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_input_actions.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_spectator_mode.cpp
        )

        add_executable(og_unit_server
            ${CMAKE_SOURCE_DIR}/tests/unit/headless_unit_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_headless_server_runtime.cpp
            ${CMAKE_SOURCE_DIR}/src/test_trace.cpp
            ${HEADLESS_SERVER_RUNTIME_SOURCES}
        )
        configure_openglad_library(og_unit_server)
        target_compile_definitions(og_unit_server PRIVATE TESTING)
        target_include_directories(og_unit_server PRIVATE
            ${CMAKE_SOURCE_DIR}/tests
            ${OG_THIRD_PARTY_INCLUDE_DIRS}
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

        if(ENABLE_COVERAGE)
            # Coverage-instrumented UI-heavy binaries can time out on 4-core CI
            # runners when CTest schedules them all at once.
            set_tests_properties(
                og_test_game_core
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
            set_tests_properties(og_unit_sim PROPERTIES
                PROCESSORS 2
                TIMEOUT 420
            )
        endif()
        if(ENABLE_SANITIZERS)
            # Reserve the full 4-core GitHub runner for og_test_game_core.
            # Under ASan/UBSan it can time out only when CTest schedules it
            # beside other heavyweight integration binaries.
            set_tests_properties(og_test_game_core PROPERTIES PROCESSORS 4)
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

        if(ENABLE_COVERAGE)
            add_test(NAME openglad_demo_smoke
                COMMAND ${CMAKE_COMMAND} -E env
                    SDL_VIDEODRIVER=dummy
                    SDL_AUDIODRIVER=dummy
                    SDL_RENDER_DRIVER=software
                    OPENGLAD_DEMO_MAX_FRAMES=1
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
    # The harnesses use SDL_RWFromConstMem to feed data, so they need SDL2 but
    # not the full game engine.

    function(add_fuzz_target name source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE
            $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
        )
        if(NOT EMSCRIPTEN)
            target_include_directories(${name} SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
        endif()
        target_include_directories(${name} PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
        target_compile_definitions(${name} PRIVATE ${COMMON_DEFINITIONS})
        target_compile_options(${name} PRIVATE ${FUZZ_SANITIZER_FLAGS})
        target_link_options(${name} PRIVATE ${FUZZ_LINK_FLAGS})
    endfunction()

    # fuzz_scenario: exercises .fss scenario binary parsing
    add_fuzz_target(fuzz_scenario ${SRC_DIR}/fuzz/fuzz_scenario.cpp)
    target_link_libraries(fuzz_scenario PRIVATE ${SDL2_LIBRARIES})

    # fuzz_savefile: exercises .gtl save file binary parsing
    add_fuzz_target(fuzz_savefile ${SRC_DIR}/fuzz/fuzz_savefile.cpp)
    target_link_libraries(fuzz_savefile PRIVATE ${SDL2_LIBRARIES})

endif()

# --------------------------------------------------------------------------
# Emscripten target
# --------------------------------------------------------------------------

	if(EMSCRIPTEN)
	    add_library(og_game_web STATIC ${GAME_SOURCES_NO_MAIN})
	    configure_openglad_library(og_game_web)
	    configure_openglad_runtime_target(og_game_web)
	    target_include_directories(og_game_web PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
	    target_link_libraries(og_game_web PUBLIC ${OG_EXTERNAL_LIBS} ${OG_IO_EXTERNAL_LIBS})
	    # Define __ASYNCIFY__ so that YIELD_SLEEP expands to emscripten_sleep()
	    # instead of a no-op.  The -sASYNCIFY linker flag below enables the
	    # Binaryen transform, but does not define the preprocessor macro.
	    target_compile_definitions(og_game_web PRIVATE __ASYNCIFY__)

    add_executable(play ${SRC_DIR}/platform/sdl/glad.cpp)
    configure_openglad_library(play)
    target_link_libraries(play PRIVATE og_game_web)
    configure_openglad_runtime_target(play)
    target_compile_definitions(play PRIVATE __ASYNCIFY__)

    target_link_options(play PRIVATE
        "SHELL:--bind"
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sASYNCIFY
        -sASYNCIFY_STACK_SIZE=65536
        -sEXIT_RUNTIME=0
        "SHELL:-sMINIFY_HTML=0"
        -lwebsocket.js
        -lidbfs.js
        "SHELL:-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\"]"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/cfg@/cfg"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/pix@/pix"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/sound@/sound"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/extra_campaigns@/extra_campaigns"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/builtin@/builtin"
    )

    set_target_properties(play PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
        SUFFIX ".html"
    )

    add_custom_command(
        TARGET play
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/web/index.html
            ${CMAKE_SOURCE_DIR}/web/help.html
            ${CMAKE_SOURCE_DIR}/web/hero.png
            $<TARGET_FILE_DIR:play>
        COMMENT "Copying web landing page assets to dist/"
        VERBATIM
    )
endif()
