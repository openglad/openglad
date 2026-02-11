cmake_minimum_required(VERSION 3.25)
project(OpenGlad VERSION 1.0 LANGUAGES C CXX)

# Target C++20 (C++23 when toolchains catch up)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C standard for external C libraries
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)

# Build options
option(BUILD_TESTING "Build the test suite" OFF)
option(BUILD_EDITOR "Build the scenario editor (openscen)" ON)
option(ENABLE_COVERAGE "Enable gcov/lcov coverage instrumentation (tests only)" OFF)

# Coverage runs are only meaningful if the tests are built and executed.
# The repo's documented coverage pipeline configures with ENABLE_COVERAGE=ON
# and then invokes `ctest`, so ensure BUILD_TESTING is enabled in that mode.
if(ENABLE_COVERAGE AND NOT BUILD_TESTING)
    set(BUILD_TESTING ON CACHE BOOL "Build the test suite" FORCE)
endif()

# --------------------------------------------------------------------------
# Source file lists
# --------------------------------------------------------------------------

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXT_DIR ${SRC_DIR}/external)

# Main game C++ sources
set(GAME_SOURCES
    ${SRC_DIR}/glad.cpp
    ${SRC_DIR}/game_loop.cpp
    ${SRC_DIR}/game_context.cpp
    ${SRC_DIR}/combat_math.cpp
    ${SRC_DIR}/button.cpp
    ${SRC_DIR}/effect.cpp
    ${SRC_DIR}/game.cpp
    ${SRC_DIR}/graphlib.cpp
    ${SRC_DIR}/guy.cpp
    ${SRC_DIR}/help.cpp
    ${SRC_DIR}/input.cpp
    ${SRC_DIR}/intro.cpp
    ${SRC_DIR}/living.cpp
    ${SRC_DIR}/obmap.cpp
    ${SRC_DIR}/pal32.cpp
    ${SRC_DIR}/picker.cpp
    ${SRC_DIR}/pixie.cpp
    ${SRC_DIR}/pixien.cpp
    ${SRC_DIR}/radar.cpp
    ${SRC_DIR}/screen.cpp
    ${SRC_DIR}/smooth.cpp
    ${SRC_DIR}/sound.cpp
    ${SRC_DIR}/stats.cpp
    ${SRC_DIR}/text.cpp
    ${SRC_DIR}/treasure.cpp
    ${SRC_DIR}/video.cpp
    ${SRC_DIR}/view.cpp
    ${SRC_DIR}/walker.cpp
    ${SRC_DIR}/weap.cpp
    ${SRC_DIR}/sai2x.cpp
    ${SRC_DIR}/util.cpp
    ${SRC_DIR}/io.cpp
    ${SRC_DIR}/gparser.cpp
    ${SRC_DIR}/gloader.cpp
    ${SRC_DIR}/pixie_data.cpp
    ${SRC_DIR}/level_data.cpp
    ${SRC_DIR}/level_picker.cpp
    ${SRC_DIR}/level_editor.cpp
    ${SRC_DIR}/campaign_picker.cpp
    ${SRC_DIR}/results_screen.cpp
    ${SRC_DIR}/save_data.cpp
)

# External C++ sources
set(EXT_CXX_SOURCES
    ${EXT_DIR}/micropather/micropather.cpp
    ${EXT_DIR}/yam/yam.cpp
)

# External C sources - libyaml
set(LIBYAML_SOURCES
    ${EXT_DIR}/libyaml/src/api.c
    ${EXT_DIR}/libyaml/src/loader.c
    ${EXT_DIR}/libyaml/src/parser.c
    ${EXT_DIR}/libyaml/src/reader.c
    ${EXT_DIR}/libyaml/src/scanner.c
    ${EXT_DIR}/libyaml/src/emitter.c
    ${EXT_DIR}/libyaml/src/dumper.c
    ${EXT_DIR}/libyaml/src/writer.c
)

# External C sources - physfs
set(PHYSFS_SOURCES
    ${EXT_DIR}/physfs/physfs.c
    ${EXT_DIR}/physfs/physfs_byteorder.c
    ${EXT_DIR}/physfs/physfs_unicode.c
    ${EXT_DIR}/physfs/archivers/dir.c
    ${EXT_DIR}/physfs/archivers/zip.c
    ${EXT_DIR}/physfs/archivers/grp.c
    ${EXT_DIR}/physfs/archivers/qpak.c
    ${EXT_DIR}/physfs/archivers/hog.c
    ${EXT_DIR}/physfs/archivers/mvl.c
    ${EXT_DIR}/physfs/archivers/wad.c
    ${EXT_DIR}/physfs/platform/posix.c
    ${EXT_DIR}/physfs/platform/unix.c
    ${EXT_DIR}/physfs/extras/physfsrwops.c
)

# External C sources - zlib
set(ZLIB_SOURCES
    ${EXT_DIR}/physfs/zlib123/adler32.c
    ${EXT_DIR}/physfs/zlib123/compress.c
    ${EXT_DIR}/physfs/zlib123/crc32.c
    ${EXT_DIR}/physfs/zlib123/deflate.c
    ${EXT_DIR}/physfs/zlib123/inffast.c
    ${EXT_DIR}/physfs/zlib123/inflate.c
    ${EXT_DIR}/physfs/zlib123/inftrees.c
    ${EXT_DIR}/physfs/zlib123/trees.c
    ${EXT_DIR}/physfs/zlib123/zutil.c
)

# External C sources - libzip
set(LIBZIP_SOURCES
    ${EXT_DIR}/libzip/zip_add.c
    ${EXT_DIR}/libzip/zip_add_dir.c
    ${EXT_DIR}/libzip/zip_add_entry.c
    ${EXT_DIR}/libzip/zip_close.c
    ${EXT_DIR}/libzip/zip_delete.c
    ${EXT_DIR}/libzip/zip_dir_add.c
    ${EXT_DIR}/libzip/zip_dirent.c
    ${EXT_DIR}/libzip/zip_discard.c
    ${EXT_DIR}/libzip/zip_entry.c
    ${EXT_DIR}/libzip/zip_error.c
    ${EXT_DIR}/libzip/zip_error_clear.c
    ${EXT_DIR}/libzip/zip_error_get.c
    ${EXT_DIR}/libzip/zip_error_get_sys_type.c
    ${EXT_DIR}/libzip/zip_error_strerror.c
    ${EXT_DIR}/libzip/zip_error_to_str.c
    ${EXT_DIR}/libzip/zip_extra_field.c
    ${EXT_DIR}/libzip/zip_extra_field_api.c
    ${EXT_DIR}/libzip/zip_fclose.c
    ${EXT_DIR}/libzip/zip_fdopen.c
    ${EXT_DIR}/libzip/zip_file_add.c
    ${EXT_DIR}/libzip/zip_file_error_clear.c
    ${EXT_DIR}/libzip/zip_file_error_get.c
    ${EXT_DIR}/libzip/zip_file_get_comment.c
    ${EXT_DIR}/libzip/zip_file_get_offset.c
    ${EXT_DIR}/libzip/zip_file_rename.c
    ${EXT_DIR}/libzip/zip_file_replace.c
    ${EXT_DIR}/libzip/zip_file_set_comment.c
    ${EXT_DIR}/libzip/zip_file_strerror.c
    ${EXT_DIR}/libzip/zip_filerange_crc.c
    ${EXT_DIR}/libzip/zip_fopen.c
    ${EXT_DIR}/libzip/zip_fopen_encrypted.c
    ${EXT_DIR}/libzip/zip_fopen_index.c
    ${EXT_DIR}/libzip/zip_fopen_index_encrypted.c
    ${EXT_DIR}/libzip/zip_fread.c
    ${EXT_DIR}/libzip/zip_get_archive_comment.c
    ${EXT_DIR}/libzip/zip_get_archive_flag.c
    ${EXT_DIR}/libzip/zip_get_compression_implementation.c
    ${EXT_DIR}/libzip/zip_get_encryption_implementation.c
    ${EXT_DIR}/libzip/zip_get_file_comment.c
    ${EXT_DIR}/libzip/zip_get_name.c
    ${EXT_DIR}/libzip/zip_get_num_entries.c
    ${EXT_DIR}/libzip/zip_get_num_files.c
    ${EXT_DIR}/libzip/zip_memdup.c
    ${EXT_DIR}/libzip/zip_name_locate.c
    ${EXT_DIR}/libzip/zip_new.c
    ${EXT_DIR}/libzip/zip_open.c
    ${EXT_DIR}/libzip/zip_rename.c
    ${EXT_DIR}/libzip/zip_replace.c
    ${EXT_DIR}/libzip/zip_set_archive_comment.c
    ${EXT_DIR}/libzip/zip_set_archive_flag.c
    ${EXT_DIR}/libzip/zip_set_default_password.c
    ${EXT_DIR}/libzip/zip_set_file_comment.c
    ${EXT_DIR}/libzip/zip_set_file_compression.c
    ${EXT_DIR}/libzip/zip_set_name.c
    ${EXT_DIR}/libzip/zip_source_buffer.c
    ${EXT_DIR}/libzip/zip_source_close.c
    ${EXT_DIR}/libzip/zip_source_crc.c
    ${EXT_DIR}/libzip/zip_source_deflate.c
    ${EXT_DIR}/libzip/zip_source_error.c
    ${EXT_DIR}/libzip/zip_source_file.c
    ${EXT_DIR}/libzip/zip_source_filep.c
    ${EXT_DIR}/libzip/zip_source_free.c
    ${EXT_DIR}/libzip/zip_source_function.c
    ${EXT_DIR}/libzip/zip_source_layered.c
    ${EXT_DIR}/libzip/zip_source_open.c
    ${EXT_DIR}/libzip/zip_source_pkware.c
    ${EXT_DIR}/libzip/zip_source_pop.c
    ${EXT_DIR}/libzip/zip_source_read.c
    ${EXT_DIR}/libzip/zip_source_stat.c
    ${EXT_DIR}/libzip/zip_source_window.c
    ${EXT_DIR}/libzip/zip_source_zip.c
    ${EXT_DIR}/libzip/zip_source_zip_new.c
    ${EXT_DIR}/libzip/zip_stat.c
    ${EXT_DIR}/libzip/zip_stat_index.c
    ${EXT_DIR}/libzip/zip_stat_init.c
    ${EXT_DIR}/libzip/zip_strerror.c
    ${EXT_DIR}/libzip/zip_string.c
    ${EXT_DIR}/libzip/zip_unchange.c
    ${EXT_DIR}/libzip/zip_unchange_all.c
    ${EXT_DIR}/libzip/zip_unchange_archive.c
    ${EXT_DIR}/libzip/zip_unchange_data.c
    ${EXT_DIR}/libzip/zip_utf-8.c
    ${EXT_DIR}/libzip/zip_err_str.c
    ${EXT_DIR}/libzip/mkstemp.c
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

set(COMMON_INCLUDE_DIRS
    ${SRC_DIR}
    ${EXT_DIR}/micropather
    ${EXT_DIR}/yam
    ${EXT_DIR}/libyaml/include
    ${EXT_DIR}/physfs
    ${EXT_DIR}/physfs/extras
    ${EXT_DIR}/physfs/zlib123
    ${EXT_DIR}/libzip
)

set(COMMON_DEFINITIONS
    USE_BMP_SCREENSHOT=1
    PHYSFS_NO_CDROM_SUPPORT=1
    PHYSFS_SUPPORTS_ZIP=1
    PHYSFS_SUPPORTS_GRP=0
    PHYSFS_SUPPORTS_WAD=0
    PHYSFS_SUPPORTS_HOG=0
    PHYSFS_SUPPORTS_MVL=0
    PHYSFS_SUPPORTS_QPAK=0
    PHYSFS_SUPPORTS_LZMA=0
    YAML_DECLARE_STATIC
)

# --------------------------------------------------------------------------
# Helper function to configure a target with common settings
# --------------------------------------------------------------------------

function(configure_openglad_target target)
    target_include_directories(${target} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})

    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE
            -sUSE_SDL=2
            -sUSE_SDL_MIXER=2
            -Wno-constant-conversion
            -Wno-parentheses-equality
            -Wno-pointer-bool-conversion
        )
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2 SDL2_mixer)
        target_include_directories(${target} PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${SDL2_LIBRARIES} m pthread)
        target_compile_options(${target} PRIVATE -Wno-parentheses)
    endif()

    # Suppress warnings for external C code
    set_source_files_properties(${EXTERNAL_C_SOURCES} ${EXT_CXX_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w")
endfunction()

# --------------------------------------------------------------------------
# Native targets
# --------------------------------------------------------------------------

if(NOT EMSCRIPTEN)
    # --- openglad (main game) ---
    add_executable(openglad ${GAME_SOURCES} ${EXT_CXX_SOURCES} ${EXTERNAL_C_SOURCES})
    configure_openglad_target(openglad)

    # --- openscen (level editor) ---
    if(BUILD_EDITOR)
        add_executable(openscen ${GAME_SOURCES} ${EXT_CXX_SOURCES} ${EXTERNAL_C_SOURCES})
        configure_openglad_target(openscen)
        target_compile_definitions(openscen PRIVATE OPENSCEN)
    endif()

    # --- openglad_test ---
    if(BUILD_TESTING)
        set(TEST_SOURCES
            ${CMAKE_SOURCE_DIR}/tests/test_framework.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
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
            ${CMAKE_SOURCE_DIR}/tests/test_walker_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_helpers.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_game_context.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_guy_calcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_screen_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_living_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_guy_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_combat.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_specials.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_stats_commands.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_draw.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_video_draw.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_living_combat.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_gloader_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_screen_extended.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_resize.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_redraw.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_ops.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_smooth_ops.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_treasure_eat.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_weap_behavior.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_act.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_text_render.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_death.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_funcs.cpp
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
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_archivers.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_editor_interactions.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_help_parsing.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_zip_unzip.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_glad_hud.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_editor_helpers.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_help_smoke.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_keybinds.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_pathing.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_yaml_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_ops.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_zlib_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_input_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_picker_menu_nav.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_yaml_document.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_yaml_api_builder.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_unicode.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_physfs_byteorder.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_joystick.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_external_zlib_api_edges.cpp
        )

        # Game sources include test_trace.cpp when TESTING is defined
        set(TEST_GAME_SOURCES ${GAME_SOURCES})
        list(APPEND TEST_GAME_SOURCES ${SRC_DIR}/test_trace.cpp)

        add_executable(openglad_test
            ${TEST_GAME_SOURCES}
            ${EXT_CXX_SOURCES}
            ${EXTERNAL_C_SOURCES}
            ${TEST_SOURCES}
        )
        configure_openglad_target(openglad_test)
        target_compile_definitions(openglad_test PRIVATE TESTING)
        target_include_directories(openglad_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)

        enable_testing()
        add_test(NAME openglad_test COMMAND openglad_test)
        set_tests_properties(openglad_test PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )

        if(ENABLE_COVERAGE)
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                # Coverage builds can be extremely slow at -O0; -O1 is usually
                # a good compromise while still producing accurate line coverage.
                target_compile_options(openglad_test PRIVATE -O1 -g --coverage)
                target_link_options(openglad_test PRIVATE --coverage)
                target_compile_definitions(openglad_test PRIVATE ENABLE_COVERAGE)
            else()
                message(WARNING "ENABLE_COVERAGE is ON but compiler is not GCC/Clang; ignoring coverage flags")
            endif()
        endif()
    endif()
endif()

# --------------------------------------------------------------------------
# Emscripten target
# --------------------------------------------------------------------------

if(EMSCRIPTEN)
    add_executable(play ${GAME_SOURCES} ${EXT_CXX_SOURCES} ${EXTERNAL_C_SOURCES})
    configure_openglad_target(play)

    target_link_options(play PRIVATE
        -sUSE_SDL=2
        -sUSE_SDL_MIXER=2
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
