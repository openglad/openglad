#pragma once

#include <memory>

class screen;

std::unique_ptr<screen>& global_screen_owner();
screen* create_global_screen(short numviews = 1);
void destroy_global_screen();
