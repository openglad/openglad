#include "graph.h"
#include "test_framework.h"
#include "test_input_helpers.h"

extern screen* myscreen;

// From level_editor.cpp
Sint32 level_editor();
extern Sint32 levelchanged;
extern Sint32 campaignchanged;

struct EditorThreadState {
    bool started;
    bool finished;
};

static int editor_injector_thread(void* data)
{
    EditorThreadState* st = (EditorThreadState*)data;
    st->started = true;

    // Give the editor time to initialize and enter its main loop.
    SDL_Delay(300);

    // Open Level menu, toggle goal flags (Level > Goals > toggles).
    inject_click(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click(90, 85, 20);   // Goals >
    SDL_Delay(30);
    inject_click(200, 85, 20);  // Defeat enemies toggle
    inject_click(200, 105, 20); // Beat generators toggle
    inject_click(200, 125, 20); // Protect NPCs toggle

    // Open Level > Details submenu and exercise prompt_for_string (TESTING fast path).
    SDL_Delay(30);
    inject_click(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click(90, 65, 20);   // Details >
    SDL_Delay(30);
    inject_click(200, 85, 20);  // Par value...
    inject_click(200, 105, 20); // Time limit...

    // Mode toggles and a couple keypaths.
    SDL_Delay(30);
    inject_key_press(SDLK_o, 10); // Terrain -> Object
    inject_key_press(SDLK_RIGHTBRACKET, 10);
    inject_key_press(SDLK_LEFTBRACKET, 10);

    inject_key_press(SDLK_o, 10); // Object -> Select
    inject_key_press(SDLK_DELETE, 10);

    // Trigger resmooth (F5) and palette load (F9) paths.
    inject_key_press(SDLK_F5, 10);
    inject_key_press(SDLK_F9, 10);

    // Force the ESC quit prompt path (TESTING returns default without blocking).
    levelchanged = 1;
    campaignchanged = 1;
    inject_key_press(SDLK_ESCAPE, 10);

    // Exercise a click in the main window.
    inject_click(100, 100, 10);

    // Let it draw a few frames, then request exit.
    SDL_Delay(300);
    myscreen->end = 1;

    st->finished = true;
    return 0;
}

void test_level_editor_runs_and_handles_basic_input()
{
    // Ensure the editor loop runs for a short period.
    myscreen->end = 0;

    EditorThreadState st{false, false};
    SDL_Thread* thread = SDL_CreateThread(editor_injector_thread, "editor_injector", &st);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    // This blocks until myscreen->end is set by the injector.
    (void)level_editor();

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    // Reset end flag for subsequent tests.
    myscreen->end = 0;

    TEST_ASSERT(st.started, "injector thread should have started");
    TEST_ASSERT(st.finished, "injector thread should have finished");
}
REGISTER_TEST(test_level_editor_runs_and_handles_basic_input);
