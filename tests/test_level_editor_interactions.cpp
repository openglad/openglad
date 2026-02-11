#include "graph.h"
#include "test_framework.h"
#include "test_input_helpers.h"

extern screen* myscreen;

// From level_editor.cpp
Sint32 level_editor();

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

    // Toggle a couple modes and click in the main window to exercise mouse paths.
    inject_key_press(SDLK_o, 10);
    inject_key_press(SDLK_t, 10);
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

