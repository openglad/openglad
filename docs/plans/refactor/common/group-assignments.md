# Detailed Group Assignments

Each integration group binary is built from the shared test main + test framework +
the group's specific test source files.

(`glad.cpp` is compiled into `og_game_test`, not per-binary.)

## Group 1: `og_test_walker_combat` (58 tests)

Walker combat mechanics, damage, death, blood, score.

| File | Tests |
|------|------:|
| test_walker_combat.cpp | 36 |
| test_walker_death.cpp | 22 |

## Group 2: `og_test_walker_move` (36 tests)

Walker movement, facing, pathfinding.

| File | Tests |
|------|------:|
| test_walker_movement.cpp | 33 |
| test_walker_pathing.cpp | 3 |

## Group 3: `og_test_walker_core` (98 tests)

Walker specials, core logic, collision, extended behavior.

| File | Tests |
|------|------:|
| test_walker_specials.cpp | 44 |
| test_walker_core_more.cpp | 25 |
| test_walker_extended.cpp | 19 |
| test_walker_more.cpp | 10 |

## Group 4: `og_test_families` (88 tests)

Per-family behaviors (all 15 families), difficulty, upgrade, death, AI.

| File | Tests |
|------|------:|
| test_family_behaviors.cpp | 83 |
| test_family_data.cpp | 5 |

## Group 5: `og_test_effects` (76 tests)

Effect entities: act logic, orbit, explosions, shields, doors, collisions.

| File | Tests |
|------|------:|
| test_effect_act.cpp | 22 |
| test_effect_chain_and_door.cpp | 3 |
| test_effect_death_more.cpp | 2 |
| test_effect_extended.cpp | 14 |
| test_effect_helpers.cpp | 18 |
| test_effect_more_paths.cpp | 16 |
| test_effect_weapon_interactions.cpp | 1 |

## Group 6: `og_test_living` (90 tests)

Living entities, weapons, treasures, entity coverage.

| File | Tests |
|------|------:|
| test_living_combat.cpp | 34 |
| test_living_funcs.cpp | 10 |
| test_weap_behavior.cpp | 24 |
| test_treasure_eat.cpp | 18 |
| test_entity_coverage.cpp | 4 |

## Group 7: `og_test_stats` (79 tests)

Statistics/command system: walk, fire, follow, hit response, blocked directions.

| File | Tests |
|------|------:|
| test_stats.cpp | 3 |
| test_stats_commands.cpp | 24 |
| test_stats_extended.cpp | 18 |
| test_stats_hit_response_more.cpp | 1 |
| test_stats_hit_response_teleport.cpp | 1 |
| test_stats_more_paths.cpp | 19 |
| test_stats_navigation.cpp | 1 |
| test_stats_right_walk_direct_walk.cpp | 4 |
| test_stats_coverage.cpp | 8 |

## Group 8: `og_test_guy` (76 tests)

Character (guy) data: creation, stats, leveling, costs, upgrades per family.

| File | Tests |
|------|------:|
| test_guy.cpp | 19 |
| test_guy_calcs.cpp | 31 |
| test_guy_extended.cpp | 26 |

## Group 9: `og_test_game_core` (85 tests)

Combat math, game context, startup, loaders, palette, game loop, full game sim.

| File | Tests |
|------|------:|
| test_combat_math.cpp | 39 |
| test_game_context.cpp | 13 |
| test_startup.cpp | 2 |
| test_trace_buffer.cpp | 2 |
| test_palette.cpp | 3 |
| test_loader_and_walker.cpp | 4 |
| test_gloader_funcs.cpp | 12 |
| test_game_loop.cpp | 4 |
| test_game_launch.cpp | 2 |
| test_go_no_team.cpp | 2 |
| test_fairy_death.cpp | 1 |
| test_overpowered_team.cpp | 1 |

## Group 10: `og_test_screen` (61 tests)

Screen/world: add/remove objects, find, targeting, runtime coverage paths.

| File | Tests |
|------|------:|
| test_screen_extended.cpp | 20 |
| test_screen_funcs.cpp | 16 |
| test_runtime_coverage_paths.cpp | 25 |

## Group 11: `og_test_view` (78 tests)

Viewscreen: drawing, redraw, resize, lifecycle, input, HUD, radar, scaler.

| File | Tests |
|------|------:|
| test_view_draw.cpp | 9 |
| test_view_redraw.cpp | 10 |
| test_view_funcs.cpp | 20 |
| test_view_lifecycle.cpp | 2 |
| test_view_resize.cpp | 10 |
| test_view_team_list.cpp | 1 |
| test_radar_more.cpp | 2 |
| test_glad_hud.cpp | 3 |
| test_sai2x_scaler.cpp | 1 |
| test_view_input_paths.cpp | 3 |
| test_view_input_more_paths.cpp | 1 |
| test_view_input_smoke.cpp | 2 |
| test_view_input_prefs_and_redraw.cpp | 3 |
| test_view_get_keypress_and_edge_cases.cpp | 4 |
| test_view_menu_driven.cpp | 1 |
| test_view_options_menu_driven.cpp | 1 |
| test_view_team.cpp | 5 |

## Group 12: `og_test_rendering` (113 tests)

Video primitives, pixel ops, draw modes, fading, text rendering and input.

| File | Tests |
|------|------:|
| test_video_buffers.cpp | 2 |
| test_video_draw.cpp | 25 |
| test_video_extended.cpp | 14 |
| test_video_extra.cpp | 28 |
| test_video_fade.cpp | 4 |
| test_video_modes_more.cpp | 3 |
| test_video_pixel_ops.cpp | 3 |
| test_video_primitives.cpp | 1 |
| test_text_render.cpp | 26 |
| test_text_rendering.cpp | 1 |
| test_text_input_and_width.cpp | 4 |
| test_text_input_ex_value.cpp | 2 |

## Group 13: `og_test_picker` (56 tests)

Picker menu: state machine, costs, dialogs, navigation, ownership, menu model.

| File | Tests |
|------|------:|
| test_picker_accessible_levels.cpp | 3 |
| test_picker_costs.cpp | 3 |
| test_picker_detail_menu_driven.cpp | 4 |
| test_picker_dialogs_real.cpp | 3 |
| test_picker_funcs.cpp | 13 |
| test_picker_menu_nav.cpp | 2 |
| test_picker_ownership_fixture.cpp | 1 |
| test_picker_state_machine.cpp | 17 |
| test_picker_timed_dialog.cpp | 1 |
| test_picker_uncovered.cpp | 4 |
| test_menu_model.cpp | 5 |

## Group 14: `og_test_menu_ui` (38 tests)

Menu navigation flows: main menu, hire, train, save, options, help, difficulty,
results screen, campaign/level picker, intro, level progress.

This group has the highest concentration of thread-based interactive tests.

| File | Tests |
|------|------:|
| test_back_to_mainmenu.cpp | 1 |
| test_menu.cpp | 3 |
| test_menu_layout.cpp | 7 |
| test_new_game.cpp | 1 |
| test_hire_team.cpp | 1 |
| test_train_team.cpp | 1 |
| test_save_menu.cpp | 1 |
| test_save_load_team.cpp | 2 |
| test_options_menu.cpp | 1 |
| test_difficulty.cpp | 1 |
| test_help.cpp | 1 |
| test_help_parsing.cpp | 2 |
| test_help_smoke.cpp | 2 |
| test_results_screen_branches.cpp | 2 |
| test_results_screen_full_ui.cpp | 1 |
| test_results_screen_internal_helper.cpp | 1 |
| test_intro_smoke.cpp | 1 |
| test_campaign_and_level_picker.cpp | 8 |
| test_level_progress.cpp | 1 |

## Group 15: `og_test_input` (55 tests)

Input handling: keyboard, joystick, keybinds, event dispatch, sim input handler.

| File | Tests |
|------|------:|
| test_input.cpp | 5 |
| test_input_event_dispatch.cpp | 1 |
| test_input_joystick.cpp | 3 |
| test_input_keybinds.cpp | 15 |
| test_input_more.cpp | 4 |
| test_sim_input_handler.cpp | 27 |

## Group 16: `og_test_level` (94 tests)

Level data: load/save versions, grid ops, error paths, level editor.

| File | Tests |
|------|------:|
| test_level_data_error_paths.cpp | 8 |
| test_level_data_load_versions.cpp | 26 |
| test_level_data_ops.cpp | 19 |
| test_level_editor_helpers.cpp | 4 |
| test_level_editor_interactions.cpp | 2 |
| test_level_editor_prompt_block.cpp | 3 |
| test_level_editor_smoke.cpp | 1 |
| test_level_data_coverage.cpp | 31 |

## Group 17: `og_test_io` (107 tests)

IO/filesystem, zip, config parsing, save/load, platform IO coverage.

| File | Tests |
|------|------:|
| test_io_filesystem.cpp | 2 |
| test_io_funcs.cpp | 14 |
| test_io_new_file_fixture.cpp | 2 |
| test_io_zip_unzip.cpp | 13 |
| test_gparser_funcs.cpp | 18 |
| test_util.cpp | 7 |
| test_save_data_versions.cpp | 16 |
| test_save_load.cpp | 3 |
| test_load_levels.cpp | 4 |
| test_io_coverage.cpp | 2 |
| test_io_platform_coverage.cpp | 26 |

## Group 18: `og_test_smooth` (57 tests)

Terrain auto-smoothing: query, genre mapping, tile masks, coverage branches.

| File | Tests |
|------|------:|
| test_smooth_matrix.cpp | 5 |
| test_smooth_more_branches.cpp | 8 |
| test_smooth_ops.cpp | 28 |
| test_smoother.cpp | 3 |
| test_smooth_coverage.cpp | 13 |

## Group 19: `og_test_external` (35 tests)

Vendored library smoke tests: PhysFS, zlib, libzip, libyaml.

| File | Tests |
|------|------:|
| test_external_libzip.cpp | 4 |
| test_external_physfs_api_edges.cpp | 1 |
| test_external_physfs_archivers.cpp | 2 |
| test_external_physfs_byteorder.cpp | 2 |
| test_external_physfs_ops.cpp | 1 |
| test_external_physfs_unicode.cpp | 3 |
| test_external_physfs_zip.cpp | 1 |
| test_external_yaml.cpp | 4 |
| test_external_yaml_api_builder.cpp | 2 |
| test_external_yaml_document.cpp | 3 |
| test_external_yaml_more.cpp | 3 |
| test_external_zlib.cpp | 2 |
| test_external_zlib_api_edges.cpp | 3 |
| test_external_zlib_more.cpp | 4 |

## Group 20: `og_test_mass_coverage` (116 tests)

Broad coverage smoke tests exercising uncovered paths across many subsystems
(video, text, screen, viewscreen, buttons, YAML, entities).

| File | Tests |
|------|------:|
| test_mass_coverage.cpp | 116 |

## Unit Groups

All unit groups are headless (no SDL). Each binary is built from `unit_main.cpp`
(headless GameSession) + its source files, linked against `og_game`.

## Unit Group 1: `og_unit_sim` (72 tests)

Session lifecycle, sim events, sim world, sim entity, sim input.

| File | Tests |
|------|------:|
| tests/unit/test_session_raii.cpp | 14 |
| tests/unit/test_sim_event_log.cpp | 7 |
| tests/unit/test_sim_world_headless.cpp | 30 |
| tests/unit/test_sim_entity.cpp | 10 |
| tests/test_sim_world.cpp | 5 |
| tests/test_sim_input_unit.cpp | 6 |

## Unit Group 2: `og_unit_families` (83 tests)

Family registry, per-family behaviors (cleric, druid, orc, thief), picker common logic.

| File | Tests |
|------|------:|
| tests/unit/test_family_registry.cpp | 11 |
| tests/test_family_cleric.cpp | 15 |
| tests/test_family_druid.cpp | 2 |
| tests/test_family_orc.cpp | 3 |
| tests/test_family_thief.cpp | 4 |
| tests/unit/test_picker_common.cpp | 48 |

## Unit Group 3: `og_unit_entity` (82 tests)

Walker, living, stats, smooth — pure logic unit tests + cross-cutting coverage.

| File | Tests |
|------|------:|
| tests/test_walker_unit.cpp | 12 |
| tests/test_walker_movement_unit.cpp | 10 |
| tests/test_walker_specials_unit.cpp | 2 |
| tests/test_living_unit.cpp | 7 |
| tests/test_stats_unit.cpp | 11 |
| tests/test_smooth_unit.cpp | 9 |
| tests/test_coverage_misc.cpp | 31 |

## Unit Group 4: `og_unit_data` (54 tests)

Level data, save data, config parsing, zip API, PhysFS wrappers, input actions, spectator.

| File | Tests |
|------|------:|
| tests/test_level_data_unit.cpp | 16 |
| tests/test_save_data_unit.cpp | 1 |
| tests/test_gparser_unit.cpp | 3 |
| tests/test_zip_api.cpp | 2 |
| tests/unit/test_physfs_wrappers.cpp | 2 |
| tests/unit/test_input_actions.cpp | 28 |
| tests/unit/test_spectator_mode.cpp | 2 |
