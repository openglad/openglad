#pragma once

#include <map>
#include <string>
#include <vector>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/mode/mode_state.h>
bool results_screen(int ending, int nextlevel);  // When no change to the guys has happened.
bool results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after);

// Pure CTF results formatting (unit-testable without the modal UI).
std::string format_ctf_winner_banner(int winner_team);
// One-line capture summary ("CAPS 3:5:1:0") as drawable pieces: counts carry
// their team index (for the team ramp color), the prefix and ':' separators
// are neutral (team == -1).
struct CtfCapsSegment
{
    std::string text;
    int team;
};
std::vector<CtfCapsSegment> format_ctf_caps_segments(const og::sim::CtfState& ctf);

// Scripted-mode (TYPE_SCRIPTED) scoreboard line for the results overview:
// the mode's own HUD slot-0 text verbatim, in its team ramp color (team 255
// reads as neutral -1). Empty slot -> empty vector (nothing drawn).
std::vector<CtfCapsSegment> format_mode_scoreboard_segments(
    const og::sim::ModeState& mode);

#ifdef TESTING
void results_screen_testing_set_force_full(bool enabled);
int results_screen_test_exercise_internal();
// Injector-thread sync for the full results UI (no wall-clock pacing): true
// while the modal button loop is running, and a monotonically increasing
// loop-iteration counter (reset by results_screen_testing_set_force_full).
bool results_screen_testing_loop_live();
int results_screen_testing_frame_count();
#endif
