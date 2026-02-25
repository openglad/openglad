cmake_minimum_required(VERSION 3.25)
project(OpenGlad VERSION 1.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Target C++20 (C++23 when toolchains catch up)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C standard for external C libraries
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_TESTING "Build the test suite" OFF)
option(BUILD_EDITOR "Build the scenario editor (openscen)" ON)
option(OPENGLAD_INSTALL_ASSETS "Install OpenGlad runtime asset directories" ON)
option(ENABLE_COVERAGE "Enable gcov/lcov coverage instrumentation (tests only)" OFF)
option(ENABLE_SANITIZERS "Enable compiler sanitizers for project targets" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis during C/C++ compilation (if found)" OFF)
option(ENABLE_CPPCHECK "Enable cppcheck analysis during C/C++ compilation (if found)" OFF)
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

# Main game C++ sources
set(GAME_SOURCES
    ${SRC_DIR}/sdl_client/glad.cpp
    ${SRC_DIR}/sdl_client/runtime/game_loop.cpp
    ${SRC_DIR}/runtime/game_context.cpp
    ${SRC_DIR}/sdl_client/runtime/game_session.cpp
    ${SRC_DIR}/sdl_client/runtime/glad_gameplay.cpp
    ${SRC_DIR}/sdl_client/runtime/legacy_globals.cpp
    ${SRC_DIR}/sdl_client/runtime/score_panel.cpp
    ${SRC_DIR}/sdl_client/runtime/screen_lifecycle.cpp
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/sdl_client/ui/button.cpp
    ${SRC_DIR}/entities/effect.cpp
    ${SRC_DIR}/entities/family_registry.cpp
    ${SRC_DIR}/entities/families/family_archer.cpp
    ${SRC_DIR}/entities/families/family_archmage.cpp
    ${SRC_DIR}/entities/families/family_barbarian.cpp
    ${SRC_DIR}/entities/families/family_big_orc.cpp
    ${SRC_DIR}/entities/families/family_cleric.cpp
    ${SRC_DIR}/entities/families/family_druid.cpp
    ${SRC_DIR}/entities/families/family_elf.cpp
    ${SRC_DIR}/entities/families/family_faerie.cpp
    ${SRC_DIR}/entities/families/family_fire_elemental.cpp
    ${SRC_DIR}/entities/families/family_ghost.cpp
    ${SRC_DIR}/entities/families/family_giant_skeleton.cpp
    ${SRC_DIR}/entities/families/family_golem.cpp
    ${SRC_DIR}/entities/families/family_mage.cpp
    ${SRC_DIR}/entities/families/family_orc.cpp
    ${SRC_DIR}/entities/families/family_skeleton.cpp
    ${SRC_DIR}/entities/families/family_slime.cpp
    ${SRC_DIR}/entities/families/family_soldier.cpp
    ${SRC_DIR}/entities/families/family_thief.cpp
    ${SRC_DIR}/entities/families/family_tower1.cpp
    ${SRC_DIR}/entities/weapon_family_registry.cpp
    ${SRC_DIR}/entities/families/weapon_family_knife.cpp
    ${SRC_DIR}/entities/families/weapon_family_rock.cpp
    ${SRC_DIR}/entities/families/weapon_family_projectiles.cpp
    ${SRC_DIR}/entities/families/weapon_family_wave.cpp
    ${SRC_DIR}/entities/families/weapon_family_door.cpp
    ${SRC_DIR}/entities/families/weapon_family_animate.cpp
    ${SRC_DIR}/entities/effect_family_registry.cpp
    ${SRC_DIR}/entities/families/effect_family_ghost_scare.cpp
    ${SRC_DIR}/entities/families/effect_family_shield.cpp
    ${SRC_DIR}/entities/families/effect_family_knife_back.cpp
    ${SRC_DIR}/entities/families/effect_family_cloud.cpp
    ${SRC_DIR}/entities/families/effect_family_chain.cpp
    ${SRC_DIR}/entities/families/effect_family_door_open.cpp
    ${SRC_DIR}/entities/families/effect_family_bomb.cpp
    ${SRC_DIR}/entities/treasure_family_registry.cpp
    ${SRC_DIR}/entities/families/treasure_family_consumables.cpp
    ${SRC_DIR}/entities/families/treasure_family_valuables.cpp
    ${SRC_DIR}/entities/families/treasure_family_navigation.cpp
    ${SRC_DIR}/entities/generator_family_registry.cpp
    ${SRC_DIR}/sdl_client/runtime/game.cpp
    ${SRC_DIR}/sdl_client/runtime/guy_create.cpp
    ${SRC_DIR}/sdl_client/render/graphlib.cpp
    ${SRC_DIR}/entities/guy.cpp
    ${SRC_DIR}/sdl_client/ui/help.cpp
    ${SRC_DIR}/sdl_client/input/input.cpp
    ${SRC_DIR}/input/input_state.cpp
    ${SRC_DIR}/sdl_client/ui/intro.cpp
    ${SRC_DIR}/entities/living.cpp
    ${SRC_DIR}/entities/obmap.cpp
    ${SRC_DIR}/sdl_client/render/pal32.cpp
    ${SRC_DIR}/sdl_client/ui/picker.cpp
    ${SRC_DIR}/sdl_client/ui/picker_dialogs.cpp
    ${SRC_DIR}/sdl_client/ui/picker_input.cpp
    ${SRC_DIR}/sdl_client/ui/picker_main_menu.cpp
    ${SRC_DIR}/sdl_client/ui/picker_accessible_levels.cpp
    ${SRC_DIR}/sdl_client/ui/picker_team_build.cpp
    ${SRC_DIR}/ui/menu_model.cpp
    ${SRC_DIR}/ui/picker_common.cpp
    ${SRC_DIR}/ui/picker_state.cpp
    ${SRC_DIR}/sdl_client/render/pixie.cpp
    ${SRC_DIR}/sdl_client/render/pixien.cpp
    ${SRC_DIR}/sdl_client/render/radar.cpp
    ${SRC_DIR}/sdl_client/runtime/screen.cpp
    ${SRC_DIR}/runtime/smooth.cpp
    ${SRC_DIR}/sdl_client/platform/sound.cpp
    ${SRC_DIR}/runtime/stats.cpp
    ${SRC_DIR}/sdl_client/render/text.cpp
    ${SRC_DIR}/entities/treasure.cpp
    ${SRC_DIR}/sdl_client/render/video.cpp
    ${SRC_DIR}/sdl_client/render/view.cpp
    ${SRC_DIR}/sdl_client/render/walker_draw.cpp
    ${SRC_DIR}/sdl_client/render/obmap_debug_draw.cpp
    ${SRC_DIR}/entities/walker.cpp
    ${SRC_DIR}/entities/walker_combat.cpp
    ${SRC_DIR}/entities/walker_movement.cpp
    ${SRC_DIR}/entities/walker_pathing.cpp
    ${SRC_DIR}/entities/walker_specials.cpp
	    ${SRC_DIR}/entities/weap.cpp
	    ${SRC_DIR}/sdl_client/render/sai2x.cpp
		    ${SRC_DIR}/core/util.cpp
		    ${SRC_DIR}/sdl_client/io/platform_io.cpp
		    ${SRC_DIR}/io/platform_io_common.cpp
		    ${SRC_DIR}/io/physfs_api.cpp
		    ${SRC_DIR}/io/zip_api.cpp
		    ${SRC_DIR}/io/yaml_stream.cpp
		    ${SRC_DIR}/io/og_file.cpp
		    ${SRC_DIR}/data/gparser.cpp
		    ${SRC_DIR}/runtime/gloader.cpp
    ${SRC_DIR}/data/pixie_data.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/runtime/level_data.cpp
    ${SRC_DIR}/sdl_client/ui/level_picker.cpp
    ${SRC_DIR}/sdl_client/ui/level_editor.cpp
    ${SRC_DIR}/sdl_client/ui/level_editor_ui.cpp
    ${SRC_DIR}/sdl_client/ui/level_editor_tools.cpp
    ${SRC_DIR}/sdl_client/ui/level_editor_file_ops.cpp
    ${SRC_DIR}/sdl_client/ui/campaign_picker.cpp
    ${SRC_DIR}/sdl_client/ui/results_screen.cpp
    ${SRC_DIR}/runtime/save_data.cpp
    ${SRC_DIR}/sim/sim_entity.cpp
    ${SRC_DIR}/sim/sim_event_log.cpp
    ${SRC_DIR}/runtime/sim_input_handler.cpp
    ${SRC_DIR}/sdl_client/runtime/input_event_bridge.cpp
    ${SRC_DIR}/sdl_client/runtime/walker_render_bridge.cpp
    ${SRC_DIR}/sdl_client/runtime/sdl_context_services.cpp
    ${SRC_DIR}/sdl_client/runtime/cheat_handler.cpp
    ${SRC_DIR}/sdl_client/render/sdl_level_render.cpp
)

set(GAME_SOURCES_NO_MAIN ${GAME_SOURCES})
list(REMOVE_ITEM GAME_SOURCES_NO_MAIN ${SRC_DIR}/sdl_client/glad.cpp)

# Internal module decomposition (Phase 1 architecture foundations).
# Keep these lists in sync with GAME_SOURCES_NO_MAIN so targets remain behaviorally equivalent.
set(OG_CORE_SOURCES
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/core/util.cpp
)

set(OG_SIM_SOURCES
    ${SRC_DIR}/sim/sim_entity.cpp
    ${SRC_DIR}/sim/sim_event_log.cpp
)

set(OG_DATA_SOURCES
    ${SRC_DIR}/data/gparser.cpp
    ${SRC_DIR}/data/pixie_data.cpp
)

set(OG_ENTITIES_SOURCES
    ${SRC_DIR}/entities/effect.cpp
    ${SRC_DIR}/entities/family_registry.cpp
    ${SRC_DIR}/entities/families/family_archer.cpp
    ${SRC_DIR}/entities/families/family_archmage.cpp
    ${SRC_DIR}/entities/families/family_barbarian.cpp
    ${SRC_DIR}/entities/families/family_big_orc.cpp
    ${SRC_DIR}/entities/families/family_cleric.cpp
    ${SRC_DIR}/entities/families/family_druid.cpp
    ${SRC_DIR}/entities/families/family_elf.cpp
    ${SRC_DIR}/entities/families/family_faerie.cpp
    ${SRC_DIR}/entities/families/family_fire_elemental.cpp
    ${SRC_DIR}/entities/families/family_ghost.cpp
    ${SRC_DIR}/entities/families/family_giant_skeleton.cpp
    ${SRC_DIR}/entities/families/family_golem.cpp
    ${SRC_DIR}/entities/families/family_mage.cpp
    ${SRC_DIR}/entities/families/family_orc.cpp
    ${SRC_DIR}/entities/families/family_skeleton.cpp
    ${SRC_DIR}/entities/families/family_slime.cpp
    ${SRC_DIR}/entities/families/family_soldier.cpp
    ${SRC_DIR}/entities/families/family_thief.cpp
    ${SRC_DIR}/entities/families/family_tower1.cpp
    ${SRC_DIR}/entities/weapon_family_registry.cpp
    ${SRC_DIR}/entities/families/weapon_family_knife.cpp
    ${SRC_DIR}/entities/families/weapon_family_rock.cpp
    ${SRC_DIR}/entities/families/weapon_family_projectiles.cpp
    ${SRC_DIR}/entities/families/weapon_family_wave.cpp
    ${SRC_DIR}/entities/families/weapon_family_door.cpp
    ${SRC_DIR}/entities/families/weapon_family_animate.cpp
    ${SRC_DIR}/entities/effect_family_registry.cpp
    ${SRC_DIR}/entities/families/effect_family_ghost_scare.cpp
    ${SRC_DIR}/entities/families/effect_family_shield.cpp
    ${SRC_DIR}/entities/families/effect_family_knife_back.cpp
    ${SRC_DIR}/entities/families/effect_family_cloud.cpp
    ${SRC_DIR}/entities/families/effect_family_chain.cpp
    ${SRC_DIR}/entities/families/effect_family_door_open.cpp
    ${SRC_DIR}/entities/families/effect_family_bomb.cpp
    ${SRC_DIR}/entities/treasure_family_registry.cpp
    ${SRC_DIR}/entities/families/treasure_family_consumables.cpp
    ${SRC_DIR}/entities/families/treasure_family_valuables.cpp
    ${SRC_DIR}/entities/families/treasure_family_navigation.cpp
    ${SRC_DIR}/entities/generator_family_registry.cpp
    ${SRC_DIR}/entities/guy.cpp
    ${SRC_DIR}/entities/living.cpp
    ${SRC_DIR}/entities/obmap.cpp
    ${SRC_DIR}/entities/treasure.cpp
    ${SRC_DIR}/entities/walker.cpp
    ${SRC_DIR}/entities/walker_combat.cpp
    ${SRC_DIR}/entities/walker_movement.cpp
    ${SRC_DIR}/entities/walker_pathing.cpp
    ${SRC_DIR}/entities/walker_specials.cpp
    ${SRC_DIR}/entities/weap.cpp
)

set(OG_RUNTIME_SOURCES
    ${SRC_DIR}/sdl_client/runtime/game.cpp
    ${SRC_DIR}/runtime/game_context.cpp
    ${SRC_DIR}/sdl_client/runtime/game_loop.cpp
    ${SRC_DIR}/sdl_client/runtime/game_session.cpp
    ${SRC_DIR}/sdl_client/runtime/glad_gameplay.cpp
    ${SRC_DIR}/sdl_client/runtime/legacy_globals.cpp
    ${SRC_DIR}/sdl_client/runtime/score_panel.cpp
    ${SRC_DIR}/sdl_client/runtime/screen_lifecycle.cpp
    ${SRC_DIR}/sdl_client/runtime/screen.cpp
    ${SRC_DIR}/sdl_client/runtime/guy_create.cpp
    ${SRC_DIR}/runtime/stats.cpp
    ${SRC_DIR}/runtime/sim_input_handler.cpp
    ${SRC_DIR}/sdl_client/runtime/input_event_bridge.cpp
    ${SRC_DIR}/runtime/smooth.cpp
    ${SRC_DIR}/gameplay/game_world.cpp
    ${SRC_DIR}/runtime/gloader.cpp
    ${SRC_DIR}/runtime/level_data.cpp
    ${SRC_DIR}/sdl_client/runtime/walker_render_bridge.cpp
    ${SRC_DIR}/sdl_client/runtime/sdl_context_services.cpp
    ${SRC_DIR}/sdl_client/runtime/cheat_handler.cpp
    ${SRC_DIR}/runtime/save_data.cpp
)

set(OG_RENDER_SOURCES
    ${SRC_DIR}/sdl_client/render/graphlib.cpp
    ${SRC_DIR}/sdl_client/render/pal32.cpp
    ${SRC_DIR}/sdl_client/render/pixie.cpp
    ${SRC_DIR}/sdl_client/render/pixien.cpp
    ${SRC_DIR}/sdl_client/render/radar.cpp
    ${SRC_DIR}/sdl_client/render/sai2x.cpp
    ${SRC_DIR}/sdl_client/render/text.cpp
    ${SRC_DIR}/sdl_client/render/video.cpp
    ${SRC_DIR}/sdl_client/render/view.cpp
    ${SRC_DIR}/sdl_client/render/walker_draw.cpp
    ${SRC_DIR}/sdl_client/render/obmap_debug_draw.cpp
    ${SRC_DIR}/sdl_client/render/sdl_level_render.cpp
)

set(OG_INPUT_SOURCES
    ${SRC_DIR}/sdl_client/input/input.cpp
    ${SRC_DIR}/input/input_state.cpp
)

set(OG_UI_SOURCES
    ${SRC_DIR}/sdl_client/ui/button.cpp
    ${SRC_DIR}/sdl_client/ui/campaign_picker.cpp
    ${SRC_DIR}/sdl_client/ui/help.cpp
    ${SRC_DIR}/sdl_client/ui/intro.cpp
    ${SRC_DIR}/sdl_client/ui/level_editor.cpp
    ${SRC_DIR}/sdl_client/ui/level_editor_file_ops.cpp
    ${SRC_DIR}/sdl_client/ui/level_editor_tools.cpp
    ${SRC_DIR}/sdl_client/ui/level_editor_ui.cpp
    ${SRC_DIR}/sdl_client/ui/level_picker.cpp
    ${SRC_DIR}/sdl_client/ui/picker.cpp
    ${SRC_DIR}/sdl_client/ui/picker_accessible_levels.cpp
    ${SRC_DIR}/sdl_client/ui/picker_dialogs.cpp
    ${SRC_DIR}/sdl_client/ui/picker_input.cpp
    ${SRC_DIR}/sdl_client/ui/picker_main_menu.cpp
    ${SRC_DIR}/sdl_client/ui/picker_team_build.cpp
    ${SRC_DIR}/ui/menu_model.cpp
    ${SRC_DIR}/ui/picker_common.cpp
    ${SRC_DIR}/ui/picker_state.cpp
    ${SRC_DIR}/sdl_client/ui/results_screen.cpp
)

set(OG_IO_SOURCES
    ${SRC_DIR}/sdl_client/io/platform_io.cpp
    ${SRC_DIR}/io/platform_io_common.cpp
    ${SRC_DIR}/io/physfs_api.cpp
    ${SRC_DIR}/io/zip_api.cpp
    ${SRC_DIR}/io/yaml_stream.cpp
    ${SRC_DIR}/io/og_file.cpp
)

set(OG_PLATFORM_SOURCES
    ${SRC_DIR}/sdl_client/platform/sound.cpp
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
    ${THIRD_PARTY_DIR}/physfs/extras/physfsrwops.c
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
    ${THIRD_PARTY_DIR}/micropather
    ${THIRD_PARTY_DIR}/yam
    ${THIRD_PARTY_DIR}/libyaml/include
    ${THIRD_PARTY_DIR}/physfs
    ${THIRD_PARTY_DIR}/physfs/extras
    ${THIRD_PARTY_DIR}/physfs/zlib123
    ${THIRD_PARTY_DIR}/libzip
)

set(COMMON_DEFINITIONS
    USE_BMP_SCREENSHOT=1
    _USE_MATH_DEFINES
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

# --------------------------------------------------------------------------
# Helper functions to configure targets with common settings
# --------------------------------------------------------------------------

function(configure_openglad_library target)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    )
    if(NOT EMSCRIPTEN)
        # Many legacy public headers still include SDL types; keep SDL includes
        # visible to downstream targets until those headers are disentangled.
        # Use the IMPORTED_TARGET from pkg_check_modules for reliable cross-platform
        # include/flag propagation (especially macOS Homebrew framework paths).
        target_link_libraries(${target} PUBLIC PkgConfig::SDL2)
    endif()
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE --coverage)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

function(configure_openglad_runtime_target target)
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE
            -sUSE_SDL=2
            -sUSE_SDL_MIXER=2
            -Wno-constant-conversion
            -Wno-parentheses-equality
            -Wno-pointer-bool-conversion
        )
        target_link_options(${target} PRIVATE
            -sUSE_SDL=2
            -sUSE_SDL_MIXER=2
        )
    else()
        # Native builds link SDL/system deps via og_runtime_deps, typically pulled
        # in transitively by og_game / og_game_test. Keep this function a no-op
        # on native platforms to avoid brittle link-order issues with static libs.
    endif()
endfunction()

function(configure_openglad_external_target target)
    target_include_directories(${target} PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
    if(NOT EMSCRIPTEN)
        target_link_libraries(${target} PRIVATE PkgConfig::SDL2)
    endif()
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
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2 SDL2_mixer)

    # System/runtime link deps (kept as a target so we can control ordering).
    add_library(og_runtime_deps INTERFACE)
    target_link_libraries(og_runtime_deps INTERFACE PkgConfig::SDL2 m pthread)
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
    # --- internal module targets (target-based architecture) ---
    add_library(og_core STATIC ${OG_CORE_SOURCES})
    configure_openglad_library(og_core)

    add_library(og_sim STATIC ${OG_SIM_SOURCES})
    configure_openglad_library(og_sim)

    add_library(og_data STATIC ${OG_DATA_SOURCES})
    configure_openglad_library(og_data)

    add_library(og_entities STATIC ${OG_ENTITIES_SOURCES})
    configure_openglad_library(og_entities)
    target_include_directories(og_entities PRIVATE ${THIRD_PARTY_DIR}/micropather)

    add_library(og_io STATIC ${OG_IO_SOURCES})
    configure_openglad_library(og_io)
    target_include_directories(og_io PRIVATE
        ${THIRD_PARTY_DIR}/yam
        ${THIRD_PARTY_DIR}/libyaml/include
        ${THIRD_PARTY_DIR}/physfs
        ${THIRD_PARTY_DIR}/physfs/extras
        ${THIRD_PARTY_DIR}/libzip
    )
    # og_io is the only OpenGlad module that links PhysFS/libzip/libyaml.
    target_link_libraries(og_io PUBLIC ${OG_IO_EXTERNAL_LIBS})

    add_library(og_runtime STATIC ${OG_RUNTIME_SOURCES})
    configure_openglad_library(og_runtime)

    add_library(og_render STATIC ${OG_RENDER_SOURCES})
    configure_openglad_library(og_render)

    add_library(og_input STATIC ${OG_INPUT_SOURCES})
    configure_openglad_library(og_input)

    add_library(og_ui STATIC ${OG_UI_SOURCES})
    configure_openglad_library(og_ui)

    add_library(og_platform STATIC ${OG_PLATFORM_SOURCES})
    configure_openglad_library(og_platform)

    # --- aggregate target (non-main code) ---
    add_library(og_game INTERFACE)
    if((CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND NOT APPLE)
        # Static libs have cyclic references today; group-rescan avoids link-order
        # dependence while we untangle module direction rules.
        # Apple ld64 does not support --start-group/--end-group (and rescans by default).
        target_link_libraries(og_game INTERFACE "-Wl,--start-group")
    endif()
    target_link_libraries(og_game INTERFACE
        og_core
        og_sim
        og_data
        og_entities
        og_io
        og_runtime
        og_render
        og_input
        og_ui
        og_platform
        ${OG_EXTERNAL_LIBS}
    )
    if((CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND NOT APPLE)
        target_link_libraries(og_game INTERFACE "-Wl,--end-group")
    endif()
    target_link_libraries(og_game INTERFACE og_runtime_deps)
    add_dependencies(og_game check_vendor_leaks)

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
    add_executable(openglad ${SRC_DIR}/sdl_client/glad.cpp)
    configure_openglad_library(openglad)
    target_link_libraries(openglad PRIVATE og_game)
    configure_openglad_runtime_target(openglad)
    add_runtime_assets_dependency(openglad)

    # --- openscen (level editor) ---
    if(BUILD_EDITOR)
        add_executable(openscen ${SRC_DIR}/sdl_client/glad.cpp)
        configure_openglad_library(openscen)
        target_link_libraries(openscen PRIVATE og_game)
        configure_openglad_runtime_target(openscen)
        target_compile_definitions(openscen PRIVATE OPENSCEN)
        add_runtime_assets_dependency(openscen)
    endif()

    # --- openglad_demo (multi-session demo) ---
    # Runs 12 concurrent AI-controlled game sessions in a 4x3 grid,
    # demonstrating the de-singletonized engine.
    add_executable(openglad_demo ${SRC_DIR}/sdl_client/demo.cpp)
    configure_openglad_library(openglad_demo)
    target_link_libraries(openglad_demo PRIVATE og_game)
    configure_openglad_runtime_target(openglad_demo)
    add_runtime_assets_dependency(openglad_demo)

    # --- openglad_text (headless text-based simulation client) ---
    # Compiles shared game sources without SDL. Uses link-time dispatch
    # (stubs in platform_headless.cpp) instead of preprocessor guards.
    set(HEADLESS_SOURCES
        # text client
        ${SRC_DIR}/text_client/main.cpp
        ${SRC_DIR}/text_client/platform_headless.cpp
        ${SRC_DIR}/text_client/walker_headless.cpp
        ${SRC_DIR}/text_client/text_picker.cpp
        ${SRC_DIR}/text_client/text_protocol.cpp
        ${SRC_DIR}/ui/menu_model.cpp
        ${SRC_DIR}/ui/picker_common.cpp
        ${SRC_DIR}/ui/picker_state.cpp
        # core
        ${OG_CORE_SOURCES}
        # sim
        ${OG_SIM_SOURCES}
        # data
        ${OG_DATA_SOURCES}
        # entities
        ${OG_ENTITIES_SOURCES}
        # input (SDL-free data structs only)
        ${SRC_DIR}/input/input_state.cpp
        # io (shared SDL-free helpers + PhysFS/zip/yaml)
        ${SRC_DIR}/io/platform_io_common.cpp
        ${SRC_DIR}/io/physfs_api.cpp
        ${SRC_DIR}/io/zip_api.cpp
        ${SRC_DIR}/io/yaml_stream.cpp
        ${SRC_DIR}/io/og_file.cpp
        # runtime (SDL-free subset)
        ${SRC_DIR}/runtime/game_context.cpp
        ${SRC_DIR}/runtime/sim_input_handler.cpp
        ${SRC_DIR}/gameplay/game_world.cpp
        ${SRC_DIR}/runtime/gloader.cpp
        ${SRC_DIR}/runtime/level_data.cpp
        ${SRC_DIR}/runtime/smooth.cpp
        ${SRC_DIR}/runtime/stats.cpp
        ${SRC_DIR}/runtime/save_data.cpp
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

    install(TARGETS
        project_warnings
        project_sanitizers
        og_core
        og_sim
	        og_data
	        og_entities
	        og_io
	        og_runtime
        og_render
        og_input
        og_ui
        og_platform
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

    # --- openglad_test ---
    if(BUILD_TESTING)
        # --- Headless unit tests (no SDL/video init) ---
        set(OG_UNIT_TEST_SOURCES
            ${CMAKE_SOURCE_DIR}/tests/unit/unit_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_session_raii.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_event_log.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_game_world_headless.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_family_registry.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_input_actions.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_entity.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_physfs_wrappers.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_picker_common.cpp
            ${CMAKE_SOURCE_DIR}/tests/unit/test_spectator_mode.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_cleric.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_druid.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_orc.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_family_thief.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_gparser_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_living_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_save_data_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_sim_input_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_game_world.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smooth_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_movement_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_specials_unit.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_zip_api.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_coverage_misc.cpp
        )

        add_executable(og_unit_tests
            ${OG_UNIT_TEST_SOURCES}
        )
        configure_openglad_library(og_unit_tests)
        target_link_libraries(og_unit_tests PRIVATE og_game)
        configure_openglad_runtime_target(og_unit_tests)
        add_test(NAME og_unit_tests COMMAND og_unit_tests)

        set(TEST_SOURCES
            ${CMAKE_SOURCE_DIR}/tests/test_framework.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
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
            ${CMAKE_SOURCE_DIR}/tests/test_menu_layout.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_help.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_load_levels.cpp
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
        )

        # Helper: select test source files from TEST_SOURCES by basename.
        # Eliminates duplicate full-path entries across test target lists.
        function(og_test_subset outvar)
            cmake_parse_arguments(ARG "" "" "FILES;EXTRA" ${ARGN})
            set(result)
            foreach(src IN LISTS TEST_SOURCES)
                cmake_path(GET src FILENAME fname)
                if(fname IN_LIST ARG_FILES)
                    list(APPEND result "${src}")
                endif()
            endforeach()
            list(APPEND result ${ARG_EXTRA})
            set(${outvar} "${result}" PARENT_SCOPE)
        endfunction()

        # Module-level test binaries (kept in addition to the full integration suite).
        # These allow faster targeted iteration without removing broad end-to-end coverage.

        # DATA_TEST_SOURCES: pure subset of TEST_SOURCES (data/IO-focused tests).
        og_test_subset(DATA_TEST_SOURCES FILES
            test_framework.cpp
            test_main.cpp
            test_campaign_and_level_picker.cpp
            test_gparser_funcs.cpp
            test_io_filesystem.cpp
            test_io_funcs.cpp
            test_io_new_file_fixture.cpp
            test_io_zip_unzip.cpp
            test_level_data_load_versions.cpp
            test_level_data_ops.cpp
            test_save_data_versions.cpp
            test_save_load.cpp
            test_util.cpp
        )

        # RUNTIME_TEST_SOURCES: subset of TEST_SOURCES plus coverage-only files.
        # The FILES list selects from the master TEST_SOURCES; EXTRA lists files
        # unique to this target (not in the master integration suite).
        og_test_subset(RUNTIME_TEST_SOURCES
            FILES
                test_framework.cpp
                test_main.cpp
                test_campaign_and_level_picker.cpp
                test_effect_act.cpp
                test_effect_chain_and_door.cpp
                test_effect_extended.cpp
                test_effect_helpers.cpp
                test_effect_more_paths.cpp
                test_effect_weapon_interactions.cpp
                test_family_behaviors.cpp
                test_family_data.cpp
                test_game_loop.cpp
                test_gparser_funcs.cpp
                test_input.cpp
                test_input_joystick.cpp
                test_input_keybinds.cpp
                test_input_more.cpp
                test_io_filesystem.cpp
                test_io_funcs.cpp
                test_io_new_file_fixture.cpp
                test_io_zip_unzip.cpp
                test_level_data_error_paths.cpp
                test_level_data_load_versions.cpp
                test_level_data_ops.cpp
                test_level_editor_helpers.cpp
                test_level_editor_interactions.cpp
                test_level_editor_prompt_block.cpp
                test_level_editor_smoke.cpp
                test_living_combat.cpp
                test_living_funcs.cpp
                test_menu_layout.cpp
                test_picker_detail_menu_driven.cpp
                test_picker_dialogs_real.cpp
                test_picker_menu_nav.cpp
                test_picker_ownership_fixture.cpp
                test_picker_state_machine.cpp
                test_picker_uncovered.cpp
                test_results_screen_branches.cpp
                test_results_screen_full_ui.cpp
                test_results_screen_internal_helper.cpp
                test_save_data_versions.cpp
                test_save_load.cpp
                test_smooth_matrix.cpp
                test_stats.cpp
                test_stats_commands.cpp
                test_stats_extended.cpp
                test_stats_hit_response_more.cpp
                test_stats_hit_response_teleport.cpp
                test_stats_more_paths.cpp
                test_stats_navigation.cpp
                test_stats_right_walk_direct_walk.cpp
                test_treasure_eat.cpp
                test_view_draw.cpp
                test_view_funcs.cpp
                test_view_get_keypress_and_edge_cases.cpp
                test_view_input_more_paths.cpp
                test_view_input_paths.cpp
                test_view_input_prefs_and_redraw.cpp
                test_view_input_smoke.cpp
                test_view_redraw.cpp
                test_view_resize.cpp
                test_walker_combat.cpp
                test_walker_core_more.cpp
                test_walker_death.cpp
                test_walker_extended.cpp
                test_walker_more.cpp
                test_walker_movement.cpp
                test_walker_pathing.cpp
                test_walker_specials.cpp
                test_weap_behavior.cpp
            EXTRA
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

        add_library(og_game_test STATIC ${GAME_SOURCES_NO_MAIN} ${SRC_DIR}/test_trace.cpp)
        configure_openglad_library(og_game_test)
        target_include_directories(og_game_test PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
        target_compile_definitions(og_game_test PRIVATE TESTING)
        target_link_libraries(og_game_test PUBLIC ${OG_EXTERNAL_LIBS} ${OG_IO_EXTERNAL_LIBS} og_runtime_deps)
        add_dependencies(og_game_test check_vendor_leaks)

        add_executable(openglad_test
            ${SRC_DIR}/sdl_client/glad.cpp
            ${TEST_SOURCES}
        )
        configure_openglad_library(openglad_test)
        target_compile_definitions(openglad_test PRIVATE TESTING)
        target_include_directories(openglad_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
        # External API smoke tests include vendored headers directly.
        target_include_directories(openglad_test PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
        target_link_libraries(openglad_test PRIVATE og_game_test)
        configure_openglad_runtime_target(openglad_test)
        add_runtime_assets_dependency(openglad_test)

        add_executable(og_data_tests
            ${SRC_DIR}/sdl_client/glad.cpp
            ${DATA_TEST_SOURCES}
        )
        configure_openglad_library(og_data_tests)
        target_compile_definitions(og_data_tests PRIVATE TESTING)
        target_include_directories(og_data_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
        target_include_directories(og_data_tests PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
        target_link_libraries(og_data_tests PRIVATE og_game_test)
        configure_openglad_runtime_target(og_data_tests)
        add_runtime_assets_dependency(og_data_tests)

        add_executable(og_runtime_tests
            ${SRC_DIR}/sdl_client/glad.cpp
            ${RUNTIME_TEST_SOURCES}
        )
        configure_openglad_library(og_runtime_tests)
        target_compile_definitions(og_runtime_tests PRIVATE TESTING)
        target_include_directories(og_runtime_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
        target_include_directories(og_runtime_tests PRIVATE ${OG_THIRD_PARTY_INCLUDE_DIRS})
        target_link_libraries(og_runtime_tests PRIVATE og_game_test)
        configure_openglad_runtime_target(og_runtime_tests)
        add_runtime_assets_dependency(og_runtime_tests)

        enable_testing()
        add_test(NAME openglad_test COMMAND openglad_test)
        # Optional filtered runs for faster targeted iteration.
        # openglad_test supports: ./openglad_test [filter_substring]
        add_test(NAME openglad_test_menu COMMAND openglad_test menu)
        add_test(NAME openglad_test_picker COMMAND openglad_test picker)
        add_test(NAME og_data_tests COMMAND og_data_tests)
        add_test(NAME og_runtime_tests COMMAND og_runtime_tests)
        set_tests_properties(openglad_test PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 600
        )
        set_tests_properties(openglad_test_menu PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 600
        )
	        set_tests_properties(openglad_test_picker PROPERTIES
	            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	            TIMEOUT 20
	        )
        set_tests_properties(og_data_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(og_runtime_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )

        add_test(NAME openglad_text_sim
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test_text_client.sh $<TARGET_FILE:openglad_text>
        )
        set_tests_properties(openglad_text_sim PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )

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

        if(ENABLE_COVERAGE)
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                # Coverage builds can be extremely slow at -O0; -O1 is usually
                # a good compromise while still producing accurate line coverage.
                target_compile_options(og_game_test PRIVATE -O1 -g --coverage)
                target_compile_options(og_unit_tests PRIVATE -O1 -g --coverage)
                target_compile_options(openglad_test PRIVATE -O1 -g --coverage)
                target_compile_options(og_data_tests PRIVATE -O1 -g --coverage)
                target_compile_options(og_runtime_tests PRIVATE -O1 -g --coverage)
                target_link_options(og_unit_tests PRIVATE --coverage)
                target_link_options(openglad_test PRIVATE --coverage)
                target_link_options(og_data_tests PRIVATE --coverage)
                target_link_options(og_runtime_tests PRIVATE --coverage)
                target_compile_definitions(og_unit_tests PRIVATE ENABLE_COVERAGE)
                target_compile_definitions(openglad_test PRIVATE ENABLE_COVERAGE)
                target_compile_definitions(og_data_tests PRIVATE ENABLE_COVERAGE)
                target_compile_definitions(og_runtime_tests PRIVATE ENABLE_COVERAGE)
            else()
                message(WARNING "ENABLE_COVERAGE is ON but compiler is not GCC/Clang; ignoring coverage flags")
            endif()
        endif()
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

    # fuzz_pixie: exercises .pix sprite data parsing (header + payload)
    add_fuzz_target(fuzz_pixie ${SRC_DIR}/fuzz/fuzz_pixie.cpp)
    target_sources(fuzz_pixie PRIVATE ${SRC_DIR}/data/pixie_data.cpp)
    target_link_libraries(fuzz_pixie PRIVATE ${SDL2_LIBRARIES})

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

    add_executable(play ${SRC_DIR}/sdl_client/glad.cpp)
    configure_openglad_library(play)
    target_link_libraries(play PRIVATE og_game_web)
    configure_openglad_runtime_target(play)
    target_compile_definitions(play PRIVATE __ASYNCIFY__)

    target_link_options(play PRIVATE
        "SHELL:-sSDL2_MIXER_FORMATS=[\"wav\",\"ogg\"]"
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sASYNCIFY
        -sASYNCIFY_STACK_SIZE=65536
        -sEXIT_RUNTIME=0
        -lidbfs.js
        "SHELL:-sEXPORTED_FUNCTIONS=[\"_main\",\"_on_idbfs_sync_done\"]"
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
endif()
