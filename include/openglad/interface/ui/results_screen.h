#pragma once

#include <map>
#include <string>
#include <openglad/gameplay/guy.h>
bool results_screen(int ending, int nextlevel);  // When no change to the guys has happened.
bool results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after);

// Pure CTF results formatting (unit-testable without the modal UI).
std::string format_ctf_winner_banner(int winner_team);
std::string format_ctf_captures_line(int team, int captures, int capture_limit);

#ifdef TESTING
void results_screen_testing_set_force_full(bool enabled);
int results_screen_test_exercise_internal();
#endif
