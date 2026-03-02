#ifndef _TEST_FRAMEWORK_H__
#define _TEST_FRAMEWORK_H__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "SDL.h"

extern int g_tests_run;
extern int g_tests_passed;
extern int g_tests_failed;

typedef void (*test_func_t)();
typedef void (*test_hook_t)();

struct TestEntry {
    const char* name;
    test_func_t fn;
    test_hook_t setup;
    test_hook_t teardown;
};

#define MAX_TESTS 4096
extern TestEntry g_test_registry[MAX_TESTS];
extern int g_test_registry_count;

#define REGISTER_TEST(func) \
    static struct _reg_##func { \
        _reg_##func() { \
            if (g_test_registry_count >= MAX_TESTS) { \
                fprintf(stderr, "FATAL: too many tests registered (max %d)\n", MAX_TESTS); \
                abort(); \
            } \
            g_test_registry[g_test_registry_count].name = #func; \
            g_test_registry[g_test_registry_count].fn = func; \
            g_test_registry[g_test_registry_count].setup = nullptr; \
            g_test_registry[g_test_registry_count].teardown = nullptr; \
            g_test_registry_count++; \
        } \
    } _reg_instance_##func

#define REGISTER_TEST_WITH_FIXTURE(func, setup_fn, teardown_fn) \
    static struct _reg_##func { \
        _reg_##func() { \
            if (g_test_registry_count >= MAX_TESTS) { \
                fprintf(stderr, "FATAL: too many tests registered (max %d)\n", MAX_TESTS); \
                abort(); \
            } \
            g_test_registry[g_test_registry_count].name = #func; \
            g_test_registry[g_test_registry_count].fn = func; \
            g_test_registry[g_test_registry_count].setup = setup_fn; \
            g_test_registry[g_test_registry_count].teardown = teardown_fn; \
            g_test_registry_count++; \
        } \
    } _reg_fixture_instance_##func

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            g_tests_failed++; \
            g_tests_run++; \
            return; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(expected, actual, msg) \
    do { \
        if ((expected) != (actual)) { \
            fprintf(stderr, "  FAIL: %s (expected %d, got %d) (%s:%d)\n", \
                    msg, static_cast<int>(expected), static_cast<int>(actual), __FILE__, __LINE__); \
            g_tests_failed++; \
            g_tests_run++; \
            return; \
        } \
    } while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, msg) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            fprintf(stderr, "  FAIL: %s (expected \"%s\", got \"%s\") (%s:%d)\n", \
                    msg, (expected), (actual), __FILE__, __LINE__); \
            g_tests_failed++; \
            g_tests_run++; \
            return; \
        } \
    } while(0)

void run_all_tests();
void list_all_tests(FILE* out);

// Optional filter: if non-null, only tests whose names contain this substring run
extern const char* g_test_filter;
// Optional exact selector: comma-separated exact test names to run.
extern const char* g_test_exact;

#endif // _TEST_FRAMEWORK_H__
