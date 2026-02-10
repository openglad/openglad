#include <vector>

#include "graph.h"
#include "game_loop.h"
#include "save_data.h"
#include "test_framework.h"
#include "util.h"

extern screen* myscreen;

short load_saved_game(const char* filename, screen* myscreen);

extern bool debug_draw_paths;
extern bool debug_draw_obmap;

struct EventScript {
    std::vector<SDL_Event> events;
    size_t idx = 0;
};

static int scripted_poll(void* userdata, SDL_Event* out)
{
    EventScript* s = static_cast<EventScript*>(userdata);
    if (s->idx >= s->events.size())
        return 0;
    *out = s->events[s->idx++];
    return 1;
}

// Adapter to match GameLoopDeps signature.
static EventScript* g_script = nullptr;
static int scripted_poll_adapter(SDL_Event* out)
{
    return scripted_poll(g_script, out);
}

void test_game_frame_toggles_debug_hotkeys()
{
    // Load a minimal scenario so screen::act() is safe to call.
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.save("test_game_loop_save");
    short load_result = load_saved_game("test_game_loop_save", myscreen);
    TEST_ASSERT(load_result != 0, "load_saved_game should succeed for scenario 1");

    // Ensure no frame delays.
    float old_speed = g_game_speed_factor;
    set_game_speed(0.0f);

    debug_draw_paths = false;
    debug_draw_obmap = false;

    EventScript script;
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_F11;
    script.events.push_back(e);
    e.key.keysym.sym = SDLK_F12;
    script.events.push_back(e);

    g_script = &script;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.poll_event = scripted_poll_adapter;

    (void)game_frame(*myscreen, st, deps);

    TEST_ASSERT(debug_draw_paths, "F11 should toggle debug_draw_paths");
    TEST_ASSERT(debug_draw_obmap, "F12 should toggle debug_draw_obmap");

    // Cleanup.
    g_script = nullptr;
    set_game_speed(old_speed);
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_game_frame_toggles_debug_hotkeys);
