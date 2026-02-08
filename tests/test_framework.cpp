#include "test_framework.h"
#include "SDL.h"

static SDL_mutex* s_allbuttons_mutex = NULL;

SDL_mutex* get_allbuttons_mutex()
{
    if (!s_allbuttons_mutex)
        s_allbuttons_mutex = SDL_CreateMutex();
    return s_allbuttons_mutex;
}

int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

TestEntry g_test_registry[MAX_TESTS];
int g_test_registry_count = 0;

void run_all_tests() {
    fprintf(stderr, "\n=== Running %d tests ===\n\n", g_test_registry_count);

    for (int i = 0; i < g_test_registry_count; i++) {
        fprintf(stderr, "  [%d/%d] %s ... ", i + 1, g_test_registry_count, g_test_registry[i].name);
        int failed_before = g_tests_failed;
        g_test_registry[i].fn();
        if (g_tests_failed == failed_before) {
            g_tests_passed++;
            g_tests_run++;
            fprintf(stderr, "PASS\n");
        }
    }

    fprintf(stderr, "\n=== Results: %d passed, %d failed, %d total ===\n\n",
            g_tests_passed, g_tests_failed, g_tests_run);
}
