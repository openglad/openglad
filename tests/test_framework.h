#ifndef _TEST_FRAMEWORK_H__
#define _TEST_FRAMEWORK_H__

#include <cstdio>
#include <cstring>

extern int g_tests_run;
extern int g_tests_passed;
extern int g_tests_failed;

typedef void (*test_func_t)();

struct TestEntry {
    const char* name;
    test_func_t fn;
};

#define MAX_TESTS 256
extern TestEntry g_test_registry[MAX_TESTS];
extern int g_test_registry_count;

#define REGISTER_TEST(func) \
    static struct _reg_##func { \
        _reg_##func() { \
            g_test_registry[g_test_registry_count].name = #func; \
            g_test_registry[g_test_registry_count].fn = func; \
            g_test_registry_count++; \
        } \
    } _reg_instance_##func

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
                    msg, (int)(expected), (int)(actual), __FILE__, __LINE__); \
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

#endif // _TEST_FRAMEWORK_H__
