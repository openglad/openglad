#pragma once

#include <map>
#include <openglad/gameplay/guy.h>
bool results_screen(int ending, int nextlevel);  // When no change to the guys has happened.
bool results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after);

#ifdef TESTING
void results_screen_testing_set_force_full(bool enabled);
int results_screen_test_exercise_internal();
#endif
