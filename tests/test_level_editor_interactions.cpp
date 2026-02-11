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
    inject_click(200, 65, 20);  // Map size...

    // Open Level > Profile submenu and exercise title/description prompts.
    SDL_Delay(30);
    inject_click(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click(90, 45, 20);   // Profile >
    SDL_Delay(30);
    inject_click(200, 45, 20);  // Title...
    inject_click(200, 65, 20);  // Description...

    // Level info / resmooth / clear paths.
    SDL_Delay(30);
    inject_click(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click(90, 25, 20);   // Info...
    SDL_Delay(30);
    inject_click(90, 105, 20);  // Resmooth terrain
    inject_click(90, 125, 20);  // Clear all terrain
    inject_click(90, 145, 20);  // Clear all objects

    // Campaign menu paths (info/profile/details/validate).
    SDL_Delay(30);
    inject_click(45, 10, 20);   // Campaign (top menu)
    SDL_Delay(30);
    inject_click(45, 25, 20);   // Info...
    SDL_Delay(30);
    inject_click(45, 45, 20);   // Profile >
    SDL_Delay(30);
    inject_click(120, 45, 20);  // Title...
    inject_click(120, 65, 20);  // Description...
    inject_click(120, 85, 20);  // Authors...
    inject_click(120, 105, 20); // Contributors...
    SDL_Delay(30);
    inject_click(45, 10, 20);   // Campaign
    SDL_Delay(30);
    inject_click(45, 65, 20);   // Details >
    SDL_Delay(30);
    inject_click(120, 65, 20);  // Version...
    inject_click(120, 85, 20);  // Suggested power...
    inject_click(120, 105, 20); // First level...
    SDL_Delay(30);
    inject_click(45, 10, 20);   // Campaign
    SDL_Delay(30);
    inject_click(45, 85, 20);   // Validate

    // Mode menu selections.
    SDL_Delay(30);
    inject_click(140, 10, 20);  // Edit (top menu)
    SDL_Delay(30);
    inject_click(140, 25, 20);  // Terrain mode
    inject_click(140, 45, 20);  // Object mode
    inject_click(140, 65, 20);  // Select mode

    // Mode toggles and a couple keypaths.
    SDL_Delay(30);
    inject_key_press(SDLK_o, 10); // Terrain -> Object
    inject_key_press(SDLK_t, 10); // Object -> Terrain
    inject_key_press(SDLK_o, 10); // Terrain -> Object
    inject_key_press(SDLK_RIGHTBRACKET, 10);
    inject_key_press(SDLK_LEFTBRACKET, 10);

    inject_key_press(SDLK_o, 10); // Object -> Select
    inject_key_press(SDLK_DELETE, 10);

    // Trigger resmooth (F5) and palette load (F9) paths.
    inject_key_press(SDLK_F5, 10);
    inject_key_press(SDLK_F9, 10);
    inject_key_press(SDLK_g, 10);
    inject_key_press(SDLK_0, 10);
    inject_key_press(SDLK_1, 10);
    inject_key_press(SDLK_2, 10);
    inject_key_press(SDLK_3, 10);
    inject_key_press(SDLK_4, 10);
    inject_key_press(SDLK_5, 10);
    inject_key_press(SDLK_6, 10);
    inject_key_press(SDLK_7, 10);
    inject_key_press(SDLK_w, 10);
    inject_key_press(SDLK_a, 10);
    inject_key_press(SDLK_s, 10);
    inject_key_press(SDLK_d, 10);

    // File menu paths.
    SDL_Delay(30);
    inject_click(15, 10, 20);   // File
    SDL_Delay(30);
    inject_click(15, 25, 20);   // Campaign >
    SDL_Delay(30);
    inject_click(85, 25, 20);   // New
    inject_click(85, 45, 20);   // Import...
    inject_click(85, 65, 20);   // Share...
    inject_click(85, 85, 20);   // Load...
    inject_click(85, 105, 20);  // Save
    inject_click(85, 125, 20);  // Save As...
    SDL_Delay(30);
    inject_click(15, 10, 20);   // File
    SDL_Delay(30);
    inject_click(15, 45, 20);   // Level >
    SDL_Delay(30);
    inject_click(85, 45, 20);   // New
    inject_click(85, 65, 20);   // Load...
    inject_click(85, 85, 20);   // Save
    inject_click(85, 105, 20);  // Save As...

    // Force the ESC quit prompt path (TESTING returns default without blocking).
    levelchanged = 1;
    campaignchanged = 1;

    // Right-click pick path.
    SDL_Event right_down{};
    right_down.type = SDL_MOUSEBUTTONDOWN;
    right_down.button.button = SDL_BUTTON_RIGHT;
    right_down.button.state = SDL_PRESSED;
    right_down.button.x = 120;
    right_down.button.y = 100;
    SDL_PushEvent(&right_down);
    SDL_Delay(10);
    SDL_Event right_up{};
    right_up.type = SDL_MOUSEBUTTONUP;
    right_up.button.button = SDL_BUTTON_RIGHT;
    right_up.button.state = SDL_RELEASED;
    right_up.button.x = 120;
    right_up.button.y = 100;
    SDL_PushEvent(&right_up);

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
