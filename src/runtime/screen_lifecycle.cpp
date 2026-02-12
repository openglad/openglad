#include "runtime/screen_lifecycle.h"

#include "base.h"
#include "runtime/game_context.h"
#include "runtime/screen.h"

namespace
{
std::unique_ptr<screen> g_screen_owner;
}

std::unique_ptr<screen>& global_screen_owner()
{
    return g_screen_owner;
}

screen* create_global_screen(short numviews)
{
    g_screen_owner = std::make_unique<screen>(numviews);
    myscreen = g_screen_owner.get();
    ctx().game_screen = myscreen;
    return myscreen;
}

void destroy_global_screen()
{
    const screen* screen_to_destroy = g_screen_owner.get();
    g_screen_owner.reset();
    if (myscreen == screen_to_destroy)
        myscreen = nullptr;
    if (ctx().game_screen == screen_to_destroy)
        ctx().game_screen = nullptr;
}
