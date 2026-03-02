#pragma once

class screen;

// Compatibility shims used by interface code while global state is removed.
screen* create_global_screen(short numviews = 1);
void destroy_global_screen();
