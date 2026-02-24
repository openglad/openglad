#include "test_framework.h"
#include "SDL.h"
#include <openglad/runtime/screen.h>
// myscreen is now a macro defined in base.h (via game_session.h)

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
const char* g_test_exact = nullptr;

static bool test_matches_filter(const char* test_name, const char* filter)
{
    if (!filter || !*filter)
        return true;
    if (!test_name)
        return false;

    // Support comma-separated substrings: "foo,bar" runs tests whose names
    // contain "foo" OR "bar". Backwards compatible with the previous single
    // substring behavior.
    const char* p = filter;
    while (*p)
    {
        // Skip leading commas/spaces.
        while (*p == ',' || *p == ' ')
            ++p;
        if (!*p)
            break;

        const char* start = p;
        while (*p && *p != ',')
            ++p;
        const size_t len = static_cast<size_t>(p - start);
        if (len > 0)
        {
            // Copy token into a small buffer for strstr (avoids non-terminated slices).
            char tok[256];
            if (len < sizeof(tok))
            {
                memcpy(tok, start, len);
                tok[len] = '\0';
                if (strstr(test_name, tok))
                    return true;
            }
            else
            {
                // Token too long; fall back to previous behavior.
                if (strstr(test_name, filter))
                    return true;
                return false;
            }
        }
    }
    return false;
}

static bool test_matches_exact_selector(const char* test_name, const char* selector)
{
    if (!selector || !*selector || !test_name)
        return false;

    const char* p = selector;
    while (*p)
    {
        while (*p == ',' || *p == ' ')
            ++p;
        if (!*p)
            break;

        const char* start = p;
        while (*p && *p != ',')
            ++p;
        const size_t len = static_cast<size_t>(p - start);
        if (len == 0)
            continue;
        if (strlen(test_name) == len && strncmp(test_name, start, len) == 0)
            return true;
    }
    return false;
}

void list_all_tests(FILE* out)
{
    FILE* stream = out ? out : stdout;
    for (int i = 0; i < g_test_registry_count; i++) {
        if (g_test_registry[i].name)
            fprintf(stream, "%s\n", g_test_registry[i].name);
    }
}

static bool test_is_selected(const char* test_name)
{
    if (!test_name)
        return false;
    if (g_test_exact && *g_test_exact)
        return test_matches_exact_selector(test_name, g_test_exact);
    return test_matches_filter(test_name, g_test_filter);
}

void run_all_tests() {
    int total_to_run = 0;
    if (g_test_exact && *g_test_exact) {
        for (int i = 0; i < g_test_registry_count; i++) {
            if (test_is_selected(g_test_registry[i].name))
                total_to_run++;
        }
        fprintf(stderr, "\n=== Running %d/%d tests (exact: \"%s\") ===\n\n",
                total_to_run, g_test_registry_count, g_test_exact);
    } else if (g_test_filter) {
        for (int i = 0; i < g_test_registry_count; i++) {
            if (test_is_selected(g_test_registry[i].name))
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
        if (!test_is_selected(g_test_registry[i].name))
            continue;
        run_idx++;
        fprintf(stderr, "  [%d/%d] %s ... ", run_idx, total_to_run, g_test_registry[i].name);
        int failed_before = g_tests_failed;
        if (g_test_registry[i].setup)
            g_test_registry[i].setup();
        g_test_registry[i].fn();
        if (g_test_registry[i].teardown)
            g_test_registry[i].teardown();

        // Test isolation: the test process is long-lived and uses global state.
        // Clear spawned objects and spatial index after each test so no test can
        // leak walkers into later tests (ASan/UAF + order-dependent failures).
        if (myscreen != nullptr)
            myscreen->level_data.delete_objects();

        if (g_tests_failed == failed_before) {
            g_tests_passed++;
            g_tests_run++;
            fprintf(stderr, "PASS\n");
        }
    }

    fprintf(stderr, "\n=== Results: %d passed, %d failed, %d total ===\n\n",
            g_tests_passed, g_tests_failed, g_tests_run);
}
