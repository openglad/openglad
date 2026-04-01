#pragma once

#ifdef __EMSCRIPTEN__

class screen;

namespace og::ui {
struct PickerLobbyGameStartConfig;
}

namespace og::platform::web {

bool should_skip_intro_for_tests();
void seed_test_save_if_requested();
void prepare_runtime_diagnostics_for_boot();
void reset_runtime_diagnostics_for_gameplay_start();
void maybe_apply_jitter_capture_start_config(
    og::ui::PickerLobbyGameStartConfig& config);
void finalize_jitter_capture_profile_after_load(screen& current_screen);

} // namespace og::platform::web

#endif
