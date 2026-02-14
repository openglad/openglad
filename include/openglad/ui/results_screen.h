#pragma once

#include <map>
#include "entities/guy.h"

bool results_screen(int ending, int nextlevel);  // When no change to the guys has happened.
bool results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after);

#ifdef TESTING
int results_screen_test_exercise_internal();
#endif
