#ifndef _RESULTS_SCREEN_H__
#define _RESULTS_SCREEN_H__

#include <map>
#include "guy.h"

void results_screen(int ending, int nextlevel);  // When no change to the guys has happened.
void results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after);

#endif
