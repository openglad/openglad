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
set(EXT_DIR ${SRC_DIR}/external)

# Main game C++ sources
set(GAME_SOURCES
    ${SRC_DIR}/glad.cpp
    ${SRC_DIR}/runtime/game_loop.cpp
    ${SRC_DIR}/runtime/game_context.cpp
    ${SRC_DIR}/runtime/screen_lifecycle.cpp
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/input/button.cpp
    ${SRC_DIR}/entities/effect.cpp
    ${SRC_DIR}/runtime/game.cpp
    ${SRC_DIR}/render/graphlib.cpp
    ${SRC_DIR}/entities/guy.cpp
    ${SRC_DIR}/ui/help.cpp
    ${SRC_DIR}/input/input.cpp
    ${SRC_DIR}/ui/intro.cpp
    ${SRC_DIR}/entities/living.cpp
    ${SRC_DIR}/entities/obmap.cpp
    ${SRC_DIR}/render/pal32.cpp
    ${SRC_DIR}/ui/picker.cpp
    ${SRC_DIR}/ui/picker_dialogs.cpp
    ${SRC_DIR}/ui/picker_input.cpp
    ${SRC_DIR}/ui/picker_main_menu.cpp
    ${SRC_DIR}/ui/picker_accessible_levels.cpp
    ${SRC_DIR}/ui/picker_team_build.cpp
    ${SRC_DIR}/render/pixie.cpp
    ${SRC_DIR}/render/pixien.cpp
    ${SRC_DIR}/render/radar.cpp
    ${SRC_DIR}/runtime/screen.cpp
    ${SRC_DIR}/render/smooth.cpp
    ${SRC_DIR}/platform/sound.cpp
    ${SRC_DIR}/core/stats.cpp
    ${SRC_DIR}/render/text.cpp
    ${SRC_DIR}/entities/treasure.cpp
    ${SRC_DIR}/render/video.cpp
    ${SRC_DIR}/render/view.cpp
    ${SRC_DIR}/entities/walker.cpp
    ${SRC_DIR}/entities/walker_combat.cpp
    ${SRC_DIR}/entities/walker_movement.cpp
    ${SRC_DIR}/entities/walker_pathing.cpp
    ${SRC_DIR}/entities/walker_specials.cpp
    ${SRC_DIR}/entities/weap.cpp
    ${SRC_DIR}/render/sai2x.cpp
    ${SRC_DIR}/core/util.cpp
    ${SRC_DIR}/platform/io.cpp
    ${SRC_DIR}/data/gparser.cpp
    ${SRC_DIR}/data/gloader.cpp
    ${SRC_DIR}/data/pixie_data.cpp
    ${SRC_DIR}/data/level_data.cpp
    ${SRC_DIR}/ui/level_picker.cpp
    ${SRC_DIR}/ui/level_editor.cpp
    ${SRC_DIR}/ui/level_editor_ui.cpp
    ${SRC_DIR}/ui/level_editor_tools.cpp
    ${SRC_DIR}/ui/level_editor_file_ops.cpp
    ${SRC_DIR}/ui/campaign_picker.cpp
    ${SRC_DIR}/ui/results_screen.cpp
    ${SRC_DIR}/data/save_data.cpp
)

set(GAME_SOURCES_NO_MAIN ${GAME_SOURCES})
list(REMOVE_ITEM GAME_SOURCES_NO_MAIN ${SRC_DIR}/glad.cpp)

# Internal module decomposition (Phase 1 architecture foundations).
# Keep these lists in sync with GAME_SOURCES_NO_MAIN so targets remain behaviorally equivalent.
set(OG_CORE_SOURCES
    ${SRC_DIR}/core/combat_math.cpp
    ${SRC_DIR}/core/stats.cpp
    ${SRC_DIR}/core/util.cpp
)

set(OG_DATA_SOURCES
    ${SRC_DIR}/data/gloader.cpp
    ${SRC_DIR}/data/gparser.cpp
    ${SRC_DIR}/data/level_data.cpp
    ${SRC_DIR}/data/pixie_data.cpp
    ${SRC_DIR}/data/save_data.cpp
)

set(OG_ENTITIES_SOURCES
    ${SRC_DIR}/entities/effect.cpp
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
    ${SRC_DIR}/runtime/game.cpp
    ${SRC_DIR}/runtime/game_context.cpp
    ${SRC_DIR}/runtime/game_loop.cpp
    ${SRC_DIR}/runtime/screen_lifecycle.cpp
    ${SRC_DIR}/runtime/screen.cpp
)

set(OG_RENDER_SOURCES
    ${SRC_DIR}/render/graphlib.cpp
    ${SRC_DIR}/render/pal32.cpp
    ${SRC_DIR}/render/pixie.cpp
    ${SRC_DIR}/render/pixien.cpp
    ${SRC_DIR}/render/radar.cpp
    ${SRC_DIR}/render/sai2x.cpp
    ${SRC_DIR}/render/smooth.cpp
    ${SRC_DIR}/render/text.cpp
    ${SRC_DIR}/render/video.cpp
    ${SRC_DIR}/render/view.cpp
)

set(OG_INPUT_SOURCES
    ${SRC_DIR}/input/button.cpp
    ${SRC_DIR}/input/input.cpp
)

set(OG_UI_SOURCES
    ${SRC_DIR}/ui/campaign_picker.cpp
    ${SRC_DIR}/ui/help.cpp
    ${SRC_DIR}/ui/intro.cpp
    ${SRC_DIR}/ui/level_editor.cpp
    ${SRC_DIR}/ui/level_editor_file_ops.cpp
    ${SRC_DIR}/ui/level_editor_tools.cpp
    ${SRC_DIR}/ui/level_editor_ui.cpp
    ${SRC_DIR}/ui/level_picker.cpp
    ${SRC_DIR}/ui/picker.cpp
    ${SRC_DIR}/ui/picker_accessible_levels.cpp
    ${SRC_DIR}/ui/picker_dialogs.cpp
    ${SRC_DIR}/ui/picker_input.cpp
    ${SRC_DIR}/ui/picker_main_menu.cpp
    ${SRC_DIR}/ui/picker_team_build.cpp
    ${SRC_DIR}/ui/results_screen.cpp
)

set(OG_PLATFORM_SOURCES
    ${SRC_DIR}/platform/io.cpp
    ${SRC_DIR}/platform/sound.cpp
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

# External C sources - physfs (3.2.0)
set(PHYSFS_SOURCES
    ${EXT_DIR}/physfs/physfs.c
    ${EXT_DIR}/physfs/physfs_byteorder.c
    ${EXT_DIR}/physfs/physfs_unicode.c
    ${EXT_DIR}/physfs/physfs_archiver_dir.c
    ${EXT_DIR}/physfs/physfs_archiver_unpacked.c
    ${EXT_DIR}/physfs/physfs_archiver_grp.c
    ${EXT_DIR}/physfs/physfs_archiver_hog.c
    ${EXT_DIR}/physfs/physfs_archiver_mvl.c
    ${EXT_DIR}/physfs/physfs_archiver_qpak.c
    ${EXT_DIR}/physfs/physfs_archiver_wad.c
    ${EXT_DIR}/physfs/physfs_archiver_zip.c
    ${EXT_DIR}/physfs/physfs_archiver_slb.c
    ${EXT_DIR}/physfs/physfs_archiver_vdf.c
    ${EXT_DIR}/physfs/physfs_archiver_iso9660.c
    ${EXT_DIR}/physfs/physfs_archiver_7z.c
    ${EXT_DIR}/physfs/physfs_platform_posix.c
    ${EXT_DIR}/physfs/physfs_platform_unix.c
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

# External C sources - libzip (1.11.3)
set(LIBZIP_SOURCES
    ${EXT_DIR}/libzip/zip_add.c
    ${EXT_DIR}/libzip/zip_add_dir.c
    ${EXT_DIR}/libzip/zip_add_entry.c
    ${EXT_DIR}/libzip/zip_algorithm_deflate.c
    ${EXT_DIR}/libzip/zip_buffer.c
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
    ${EXT_DIR}/libzip/zip_file_get_external_attributes.c
    ${EXT_DIR}/libzip/zip_file_get_offset.c
    ${EXT_DIR}/libzip/zip_file_rename.c
    ${EXT_DIR}/libzip/zip_file_replace.c
    ${EXT_DIR}/libzip/zip_file_set_comment.c
    ${EXT_DIR}/libzip/zip_file_set_encryption.c
    ${EXT_DIR}/libzip/zip_file_set_external_attributes.c
    ${EXT_DIR}/libzip/zip_file_set_mtime.c
    ${EXT_DIR}/libzip/zip_file_strerror.c
    ${EXT_DIR}/libzip/zip_fopen.c
    ${EXT_DIR}/libzip/zip_fopen_encrypted.c
    ${EXT_DIR}/libzip/zip_fopen_index.c
    ${EXT_DIR}/libzip/zip_fopen_index_encrypted.c
    ${EXT_DIR}/libzip/zip_fread.c
    ${EXT_DIR}/libzip/zip_fseek.c
    ${EXT_DIR}/libzip/zip_ftell.c
    ${EXT_DIR}/libzip/zip_get_archive_comment.c
    ${EXT_DIR}/libzip/zip_get_archive_flag.c
    ${EXT_DIR}/libzip/zip_get_encryption_implementation.c
    ${EXT_DIR}/libzip/zip_get_file_comment.c
    ${EXT_DIR}/libzip/zip_get_name.c
    ${EXT_DIR}/libzip/zip_get_num_entries.c
    ${EXT_DIR}/libzip/zip_get_num_files.c
    ${EXT_DIR}/libzip/zip_hash.c
    ${EXT_DIR}/libzip/zip_io_util.c
    ${EXT_DIR}/libzip/zip_libzip_version.c
    ${EXT_DIR}/libzip/zip_memdup.c
    ${EXT_DIR}/libzip/zip_name_locate.c
    ${EXT_DIR}/libzip/zip_new.c
    ${EXT_DIR}/libzip/zip_open.c
    ${EXT_DIR}/libzip/zip_pkware.c
    ${EXT_DIR}/libzip/zip_progress.c
    ${EXT_DIR}/libzip/zip_random_unix.c
    ${EXT_DIR}/libzip/zip_rename.c
    ${EXT_DIR}/libzip/zip_replace.c
    ${EXT_DIR}/libzip/zip_set_archive_comment.c
    ${EXT_DIR}/libzip/zip_set_archive_flag.c
    ${EXT_DIR}/libzip/zip_set_default_password.c
    ${EXT_DIR}/libzip/zip_set_file_comment.c
    ${EXT_DIR}/libzip/zip_set_file_compression.c
    ${EXT_DIR}/libzip/zip_set_name.c
    ${EXT_DIR}/libzip/zip_source_accept_empty.c
    ${EXT_DIR}/libzip/zip_source_begin_write.c
    ${EXT_DIR}/libzip/zip_source_begin_write_cloning.c
    ${EXT_DIR}/libzip/zip_source_buffer.c
    ${EXT_DIR}/libzip/zip_source_call.c
    ${EXT_DIR}/libzip/zip_source_close.c
    ${EXT_DIR}/libzip/zip_source_commit_write.c
    ${EXT_DIR}/libzip/zip_source_compress.c
    ${EXT_DIR}/libzip/zip_source_crc.c
    ${EXT_DIR}/libzip/zip_source_error.c
    ${EXT_DIR}/libzip/zip_source_file_common.c
    ${EXT_DIR}/libzip/zip_source_file_stdio.c
    ${EXT_DIR}/libzip/zip_source_file_stdio_named.c
    ${EXT_DIR}/libzip/zip_source_free.c
    ${EXT_DIR}/libzip/zip_source_function.c
    ${EXT_DIR}/libzip/zip_source_get_dostime.c
    ${EXT_DIR}/libzip/zip_source_get_file_attributes.c
    ${EXT_DIR}/libzip/zip_source_is_deleted.c
    ${EXT_DIR}/libzip/zip_source_layered.c
    ${EXT_DIR}/libzip/zip_source_open.c
    ${EXT_DIR}/libzip/zip_source_pass_to_lower_layer.c
    ${EXT_DIR}/libzip/zip_source_pkware_decode.c
    ${EXT_DIR}/libzip/zip_source_pkware_encode.c
    ${EXT_DIR}/libzip/zip_source_read.c
    ${EXT_DIR}/libzip/zip_source_remove.c
    ${EXT_DIR}/libzip/zip_source_rollback_write.c
    ${EXT_DIR}/libzip/zip_source_seek.c
    ${EXT_DIR}/libzip/zip_source_seek_write.c
    ${EXT_DIR}/libzip/zip_source_stat.c
    ${EXT_DIR}/libzip/zip_source_supports.c
    ${EXT_DIR}/libzip/zip_source_tell.c
    ${EXT_DIR}/libzip/zip_source_tell_write.c
    ${EXT_DIR}/libzip/zip_source_window.c
    ${EXT_DIR}/libzip/zip_source_write.c
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
    ${CMAKE_SOURCE_DIR}/include
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
    target_include_directories(${target} PRIVATE ${COMMON_INCLUDE_DIRS})
    if(NOT EMSCRIPTEN)
        target_include_directories(${target} PRIVATE ${SDL2_INCLUDE_DIRS})
    endif()
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
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
        target_include_directories(${target} PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${SDL2_LIBRARIES} m pthread)
    endif()
endfunction()

function(configure_openglad_external_target target)
    target_include_directories(${target} PRIVATE ${COMMON_INCLUDE_DIRS})
    if(NOT EMSCRIPTEN)
        target_include_directories(${target} PRIVATE ${SDL2_INCLUDE_DIRS})
    endif()
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Vendored third-party code: keep warnings actionable in OpenGlad code by silencing
        # warnings from upstream C/C++ sources. Track upstream versions in
        # src/external/VENDORED_LIBS.md and prefer upgrades over refactors-in-place.
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

function(add_runtime_assets_dependency target)
    add_dependencies(${target} stage_runtime_assets)
endfunction()

# graph.h include guardrail: allow transitional files, block new include spread.
add_custom_target(check_graph_h_includes
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_graph_includes.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking graph.h include allowlist"
    VERBATIM
)

# --------------------------------------------------------------------------
# External dependency targets
# --------------------------------------------------------------------------

if(NOT EMSCRIPTEN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2 SDL2_mixer)
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
# PhysFS 3.x uses built-in miniz for ZIP support; no external zlib/libzip needed

add_library(og_ext_micropather STATIC ${EXT_DIR}/micropather/micropather.cpp)
configure_openglad_external_target(og_ext_micropather)

add_library(og_ext_yam STATIC ${EXT_DIR}/yam/yam.cpp)
configure_openglad_external_target(og_ext_yam)

set(OG_EXTERNAL_LIBS
    og_ext_micropather
    og_ext_yam
    og_ext_yaml
    og_ext_physfs
    og_ext_libzip
)

if(NOT EMSCRIPTEN)
    # --- internal module targets (target-based architecture) ---
    add_library(og_core OBJECT ${OG_CORE_SOURCES})
    configure_openglad_library(og_core)

    add_library(og_data OBJECT ${OG_DATA_SOURCES})
    configure_openglad_library(og_data)

    add_library(og_entities OBJECT ${OG_ENTITIES_SOURCES})
    configure_openglad_library(og_entities)

    add_library(og_runtime OBJECT ${OG_RUNTIME_SOURCES})
    configure_openglad_library(og_runtime)

    add_library(og_render OBJECT ${OG_RENDER_SOURCES})
    configure_openglad_library(og_render)

    add_library(og_input OBJECT ${OG_INPUT_SOURCES})
    configure_openglad_library(og_input)

    add_library(og_ui OBJECT ${OG_UI_SOURCES})
    configure_openglad_library(og_ui)

    add_library(og_platform OBJECT ${OG_PLATFORM_SOURCES})
    configure_openglad_library(og_platform)

    # --- shared runtime library (non-main code) ---
    add_library(og_game STATIC
        $<TARGET_OBJECTS:og_core>
        $<TARGET_OBJECTS:og_data>
        $<TARGET_OBJECTS:og_entities>
        $<TARGET_OBJECTS:og_runtime>
        $<TARGET_OBJECTS:og_render>
        $<TARGET_OBJECTS:og_input>
        $<TARGET_OBJECTS:og_ui>
        $<TARGET_OBJECTS:og_platform>
    )
    configure_openglad_library(og_game)
    target_link_libraries(og_game PUBLIC ${OG_EXTERNAL_LIBS})
    add_dependencies(og_game check_graph_h_includes)

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
    add_executable(openglad ${SRC_DIR}/glad.cpp)
    configure_openglad_library(openglad)
    target_link_libraries(openglad PRIVATE og_game)
    configure_openglad_runtime_target(openglad)
    add_runtime_assets_dependency(openglad)

    # --- openscen (level editor) ---
    if(BUILD_EDITOR)
        add_executable(openscen ${SRC_DIR}/glad.cpp)
        configure_openglad_library(openscen)
        target_link_libraries(openscen PRIVATE og_game)
        configure_openglad_runtime_target(openscen)
        target_compile_definitions(openscen PRIVATE OPENSCEN)
        add_runtime_assets_dependency(openscen)
    endif()

    install(TARGETS
        project_warnings
        project_sanitizers
        og_ext_micropather
        og_ext_yam
        og_ext_yaml
        og_ext_zlib
        og_ext_libzip
        og_ext_physfs
        og_game
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
        )

        # Module-level test binaries (kept in addition to the full integration suite).
        # These allow faster targeted iteration without removing broad end-to-end coverage.
        set(DATA_TEST_SOURCES
            ${CMAKE_SOURCE_DIR}/tests/test_framework.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_save_load.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_save_data_versions.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_ops.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_campaign_and_level_picker.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_filesystem.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_zip_unzip.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_io_new_file_fixture.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_gparser_funcs.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_level_data_load_versions.cpp
        )

        set(RUNTIME_TEST_SOURCES
            ${CMAKE_SOURCE_DIR}/tests/test_framework.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_game_loop.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_more.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_keybinds.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_input_joystick.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_movement.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_combat.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_specials.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_pathing.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_input_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_view_input_smoke.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_effect_more_paths.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_walker_core_more.cpp
        )

        add_library(og_game_test STATIC ${GAME_SOURCES_NO_MAIN} ${SRC_DIR}/test_trace.cpp)
        configure_openglad_library(og_game_test)
        target_compile_definitions(og_game_test PRIVATE TESTING)
        target_link_libraries(og_game_test PUBLIC ${OG_EXTERNAL_LIBS})
        add_dependencies(og_game_test check_graph_h_includes)

        add_executable(openglad_test
            ${SRC_DIR}/glad.cpp
            ${TEST_SOURCES}
        )
        configure_openglad_library(openglad_test)
        target_compile_definitions(openglad_test PRIVATE TESTING)
        target_include_directories(openglad_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
        target_link_libraries(openglad_test PRIVATE og_game_test)
        configure_openglad_runtime_target(openglad_test)
        add_runtime_assets_dependency(openglad_test)

        add_executable(og_data_tests
            ${SRC_DIR}/glad.cpp
            ${DATA_TEST_SOURCES}
        )
        configure_openglad_library(og_data_tests)
        target_compile_definitions(og_data_tests PRIVATE TESTING)
        target_include_directories(og_data_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
        target_link_libraries(og_data_tests PRIVATE og_game_test)
        configure_openglad_runtime_target(og_data_tests)
        add_runtime_assets_dependency(og_data_tests)

        add_executable(og_runtime_tests
            ${SRC_DIR}/glad.cpp
            ${RUNTIME_TEST_SOURCES}
        )
        configure_openglad_library(og_runtime_tests)
        target_compile_definitions(og_runtime_tests PRIVATE TESTING)
        target_include_directories(og_runtime_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
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
        )
        set_tests_properties(openglad_test_menu PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(openglad_test_picker PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(og_data_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(og_runtime_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )

        if(ENABLE_COVERAGE)
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                # Coverage builds can be extremely slow at -O0; -O1 is usually
                # a good compromise while still producing accurate line coverage.
                target_compile_options(og_game_test PRIVATE -O1 -g --coverage)
                target_compile_options(openglad_test PRIVATE -O1 -g --coverage)
                target_compile_options(og_data_tests PRIVATE -O1 -g --coverage)
                target_compile_options(og_runtime_tests PRIVATE -O1 -g --coverage)
                target_link_options(openglad_test PRIVATE --coverage)
                target_link_options(og_data_tests PRIVATE --coverage)
                target_link_options(og_runtime_tests PRIVATE --coverage)
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
# Emscripten target
# --------------------------------------------------------------------------

if(EMSCRIPTEN)
    add_library(og_game_web STATIC ${GAME_SOURCES_NO_MAIN})
    configure_openglad_library(og_game_web)
    target_link_libraries(og_game_web PUBLIC ${OG_EXTERNAL_LIBS})
    add_dependencies(og_game_web check_graph_h_includes)

    add_executable(play ${SRC_DIR}/glad.cpp)
    configure_openglad_library(play)
    target_link_libraries(play PRIVATE og_game_web)
    configure_openglad_runtime_target(play)

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
