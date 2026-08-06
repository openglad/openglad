# --------------------------------------------------------------------------
# Test executables, groups and CTest registrations.
# Included from the if(BUILD_TESTING) block inside if(NOT EMSCRIPTEN) in the
# top-level CMakeLists.txt, so it runs at that scope and sees every component
# target.
# --------------------------------------------------------------------------

find_package(GTest REQUIRED)

set(ALL_INTEGRATION_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/tests/integration/test_trace_buffer.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_startup.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_combat_math.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_loader_and_walker.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_palette.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_util.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_game_loop.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_input.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_save_load.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_menu.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_game_launch.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_progress.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_back_to_mainmenu.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_new_game.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_hire_team.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_train_team.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_save_load_team.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_team.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_options_menu.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_networking_menu.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_menu_layout.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_help.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_load_levels.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_replay.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_snapshot_size_benchmark.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_difficulty.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_guy.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_go_no_team.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_fairy_death.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_overpowered_team.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_io_filesystem.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_smoother.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_primitives.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_effects_prims.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_render_effects.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_decor_render.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stair_overlay.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_text_rendering.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_campaign_and_level_picker.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_campaign_sprite_uaf.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_intro_smoke.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_dirty_tracking_safety.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_sai2x_scaler.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_canvas_scale.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_menu_driven.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_options_menu_driven.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_effect_helpers.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_effect_chain_and_door.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_effect_weapon_interactions.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_game_context.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_guy_calcs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_funcs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_funcs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_network_client.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_local_transport_shadow_budget.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_frame_pacing_jitter.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_extended.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_navigation.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_screen_funcs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_extended.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_extended.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_living_funcs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_effect_extended.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_guy_extended.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_combat.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_specials.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_commands.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_right_walk_direct_walk.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_hit_response_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_hit_response_teleport.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_draw.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_draw.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_living_combat.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_gloader_funcs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_screen_extended.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_resize.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_redraw.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_data_ops.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_data_error_paths.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_png_conversion.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_aseprite_round_trip.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_smooth_ops.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_smooth_more_branches.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_smooth_matrix.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_radar_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_treasure_eat.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_weap_behavior.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_effect_act.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_text_render.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_text_input_and_width.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_death.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_io_funcs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_fade.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_extra.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_gparser_funcs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_movement.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_yaml.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_physfs_zip.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_save_data_versions.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_company_io.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_editor_smoke.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_zlib.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_libzip.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_lifecycle.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_accessible_levels.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_input_smoke.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_timed_dialog.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_costs.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_buffers.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_pixel_ops.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_video_modes_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_physfs_archivers.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_editor_interactions.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_input_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_help_parsing.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_io_zip_unzip.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_io_new_file_fixture.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_glad_hud.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_editor_helpers.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_help_smoke.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_input_keybinds.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_input_direction_grace.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_input_latch.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_pathing.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_yaml_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_physfs_ops.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_zlib_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_input_paths.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_get_keypress_and_edge_cases.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_input_prefs_and_redraw.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_menu_nav.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_yaml_document.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_yaml_api_builder.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_physfs_unicode.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_physfs_byteorder.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_input_joystick.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_zlib_api_edges.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_external_physfs_api_edges.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_uncovered.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_ownership_fixture.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_state_machine.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_editor_prompt_block.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_data_load_versions.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_effect_more_paths.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_core_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_input_more_paths.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_more_paths.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_text_input_ex_value.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_detail_menu_driven.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_view_team_list.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_effect_death_more.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_input_event_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_results_screen_branches.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_results_screen_internal_helper.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_results_screen_full_ui.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_picker_dialogs_real.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_family_data.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_family_behaviors.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_entity_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_io_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_io_platform_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_data_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_mass_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_menu_model.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_runtime_coverage_paths.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_sim_input_handler.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_smooth_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/parity_runner.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/state_dump.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/fact_predicate.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/parity_bootstrap.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/scenario_runtime.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/parity_test_main.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/golden_evaluation_helper.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/test_parity_scenarios.cpp
    ${CMAKE_SOURCE_DIR}/tests/parity/test_parity_coverage_gate.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_ctf_ui.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_mode_ui.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_tower_run.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_menu_pins.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_menu_engine.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_company_list.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_cloud_ui.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_uxshots_probe.cpp
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
        ${CMAKE_SOURCE_DIR}/tests/integration/integration_main.cpp
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
    ${SRC_DIR}/core/test_trace.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/integration/sdl_video_default_lifecycle.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/integration/runtime_bootstrap_lifecycle.cpp
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
    test_campaign_sprite_uaf.cpp
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
    test_cloud_ui.cpp
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
# as the engine lands, tests/integration/test_menu_engine.cpp.
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
    test_input_latch.cpp
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

og_add_test_group(og_test_matchup FILES
    test_ctf_ui.cpp
    test_mode_ui.cpp
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
target_link_libraries(scenario_facts_dump PRIVATE og_game_test project_warnings)
# Coverage builds: libog_game_test.a is compiled with --coverage and
# leaves undefined references to __gcov_init / __gcov_exit /
# __gcov_merge_add in the static archive. Link against gcov via
# --coverage so the standalone executable resolves them. The ASan
# / TSan presets do NOT route through configure_openglad_library
# here because pulling in project_sanitizers transitively slows
# og_test_view's tick budget enough to time out at 180s on CI.
# Only the SANITIZERS are skipped: project_warnings is linked above,
# so this tool is audited like every other first-party target.
if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(scenario_facts_dump PRIVATE ${OG_COVERAGE_COMPILE_OPTIONS})
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
# Building the parity harness also re-validates the canary pins, so
# pin rot surfaces on the next build rather than as a quietly
# toothless canary run weeks later. (Interpreter-gated like the
# og_gameplay lint dependency: a machine without Python 3 still
# builds the harness, and every Linux dev/CI build keeps
# validating the pins.)
if(Python3_Interpreter_FOUND)
    add_dependencies(og_test_parity check_mutation_pins)
endif()

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
    ${CMAKE_SOURCE_DIR}/tests/unit/test_campaign_ids.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_frame_rate_config.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_view_layout.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_scale_mode.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_display_state.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_frame_deadline_pacer.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_cadence.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_astar.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_combat_softcap.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_scroll_view_layout.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_text_wrap.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_web_back_key.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_web_control_defaults.cpp
)

og_add_unit_group(og_unit_sim FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_session_raii.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_game_loop_wrapper.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_lobby_server.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_net_transport.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_pack_transfer.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_pack_transfer_errors.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/unit/test_delayed_spawn_wake.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/integration/test_sim_world.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_sim_input_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_control_policy.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_scenario_strip.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_ctf_network.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/unit/test_pack_lua_paths.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_family_cleric.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_family_druid.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_family_orc.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_family_thief.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_picker_common.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_menu_spec.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_cloud_save_client.cpp
)

og_add_unit_group(og_unit_script FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_script_host.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_script_bindings_errors.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_script_bindings_math.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_script_bindings_props.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_script_hooks.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_level_scripts.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_classpack_lua_bind.cpp
)
# The P8-A tests forge REAL stripped Lua bytecode through the C API
# (string.dump is sandbox-stripped, so the attack artifact has to be
# generated in the test) and prove the engine and the report refuse
# it. og_lua is already in this binary's link closure via og_game;
# naming it here only propagates its include directory to the test
# sources. tests/ sits outside the src/ vendor-leak boundary
# (scripts/check_vendor_leaks.sh), so the include is legal there.
target_link_libraries(og_unit_script PRIVATE og_lua)

og_add_unit_group(og_unit_entity FILES
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_movement_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_walker_specials_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_living_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_stats_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_stats_fright.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_silliness_battery.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_smooth_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_coverage_misc.cpp
)

og_add_unit_group(og_unit_data FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_classpack_install.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_classpack_lua_decl.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_classpack_lua_install.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_family_registry_golden.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/family_registry_dump.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_level_data_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_save_data_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_company.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_gparser_unit.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_zip_api.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_campaign_metadata.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_campaign_packs.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_campaign_sprite_reload.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_game_mode.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_progression.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_win_shares.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_physfs_wrappers.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_input_actions.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_spectator_mode.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_concept_levels.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_westlands_levels.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_longseason_levels.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_levels.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_builtin_archives.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_decor_format.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_level_file_io_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_migrated_campaigns.cpp
)

# test_builtin_archives byte-compares every staged-archive member
# against the committed campaign source trees (one root per
# campaign — packs/ subtrees included); test_modes_levels reads the
# modes pack sources for its manifest checks.
target_compile_definitions(og_unit_data PRIVATE
    OG_CAMPAIGNS_SOURCE_DIR="${CMAKE_SOURCE_DIR}/campaigns"
)

og_add_unit_group(og_unit_respawn FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_respawn_engine.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_classic_respawn.cpp
)

og_add_unit_group(og_unit_mode FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_mode_bindings.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_mode_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_mode_tick.cpp
)

og_add_unit_group(og_unit_modes FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_ctf.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_tdm.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_mutant.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_strip.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_items.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_basketball.cpp
)
# The modes-pack fixture zips the CURRENT repo pack sources into its
# temp .glad so the tests always exercise the shipped bytes.
target_compile_definitions(og_unit_modes PRIVATE
    OG_MODES_PACK_SOURCE_DIR="${CMAKE_SOURCE_DIR}/campaigns/modes/packs/modes.core"
)

og_add_unit_group(og_unit_soccer FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_soccer.cpp
)
target_compile_definitions(og_unit_soccer PRIVATE
    OG_MODES_PACK_SOURCE_DIR="${CMAKE_SOURCE_DIR}/campaigns/modes/packs/modes.core"
)

og_add_unit_group(og_unit_onslaught FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_modes_onslaught.cpp
)
target_compile_definitions(og_unit_onslaught PRIVATE
    OG_MODES_PACK_SOURCE_DIR="${CMAKE_SOURCE_DIR}/campaigns/modes/packs/modes.core"
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
    ${CMAKE_SOURCE_DIR}/src/core/test_trace.cpp
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
    ${CMAKE_SOURCE_DIR}/tests/unit/test_text_campaign_sprites.cpp
    ${SRC_DIR}/core/test_trace.cpp
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
        ${CMAKE_SOURCE_DIR}/src/core/test_trace.cpp
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
