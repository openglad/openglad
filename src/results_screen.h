#ifndef _RESULTS_SCREEN_H__
#define _RESULTS_SCREEN_H__

#include <map>
#include "guy.h"

bool results_screen(int ending, int nextlevel);  // When no change to the guys has happened.
bool results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after);

#endif
