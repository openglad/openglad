#include "test_framework.h"
#include "SDL.h"

static SDL_mutex* s_allbuttons_mutex = nullptr;

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

const char* g_test_filter = nullptr;

void run_all_tests() {
    int total_to_run = 0;
    if (g_test_filter) {
        for (int i = 0; i < g_test_registry_count; i++) {
            if (strstr(g_test_registry[i].name, g_test_filter))
                total_to_run++;
        }
        fprintf(stderr, "\n=== Running %d/%d tests (filter: \"%s\") ===\n\n",
                total_to_run, g_test_registry_count, g_test_filter);
    } else {
        total_to_run = g_test_registry_count;
        fprintf(stderr, "\n=== Running %d tests ===\n\n", g_test_registry_count);
    }

    int run_idx = 0;
    for (int i = 0; i < g_test_registry_count; i++) {
        if (g_test_filter && !strstr(g_test_registry[i].name, g_test_filter))
            continue;
        run_idx++;
        fprintf(stderr, "  [%d/%d] %s ... ", run_idx, total_to_run, g_test_registry[i].name);
        int failed_before = g_tests_failed;
        if (g_test_registry[i].setup)
            g_test_registry[i].setup();
        g_test_registry[i].fn();
        if (g_test_registry[i].teardown)
            g_test_registry[i].teardown();
        if (g_tests_failed == failed_before) {
            g_tests_passed++;
            g_tests_run++;
            fprintf(stderr, "PASS\n");
        }
    }

    fprintf(stderr, "\n=== Results: %d passed, %d failed, %d total ===\n\n",
            g_tests_passed, g_tests_failed, g_tests_run);
}
