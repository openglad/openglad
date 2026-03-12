#ifndef _TEST_FRAMEWORK_H__
#define _TEST_FRAMEWORK_H__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
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

struct OgTestMessage {
    std::string str;

    template<typename T>
    OgTestMessage& operator<<(T val)
    {
        std::ostringstream oss;
        oss << val;
        str += oss.str();
        return *this;
    }
};

struct OgAssertHelper {
    const char* file;
    int line;
    const char* expr;

    void operator=(OgTestMessage msg)
    {
        fprintf(stderr, "  FAIL: %s", expr);
        if (!msg.str.empty())
            fprintf(stderr, " - %s", msg.str.c_str());
        fprintf(stderr, " (%s:%d)\n", file, line);
        g_tests_failed++;
    }
};

#define MAX_TESTS 4096
extern TestEntry g_test_registry[MAX_TESTS];
extern int g_test_registry_count;

#define OG_TEST_CONCAT_IMPL(a, b) a##b
#define OG_TEST_CONCAT(a, b) OG_TEST_CONCAT_IMPL(a, b)
#define OG_TEST_PASTE(a, b) OG_TEST_CONCAT(OG_TEST_CONCAT(a, _), b)

#define OG_ADD_TEST_NAMED(func, test_name, setup_fn, teardown_fn) \
    static struct OG_TEST_PASTE(_reg, func) { \
        OG_TEST_PASTE(_reg, func)() { \
            if (g_test_registry_count >= MAX_TESTS) { \
                fprintf(stderr, "FATAL: too many tests registered (max %d)\n", MAX_TESTS); \
                abort(); \
            } \
            g_test_registry[g_test_registry_count].name = test_name; \
            g_test_registry[g_test_registry_count].fn = func; \
            g_test_registry[g_test_registry_count].setup = setup_fn; \
            g_test_registry[g_test_registry_count].teardown = teardown_fn; \
            g_test_registry_count++; \
        } \
    } OG_TEST_PASTE(_reg_instance, func)

#define ASSERT_TRUE(cond) \
    if (cond) ; \
    else return OgAssertHelper{__FILE__, __LINE__, #cond} = OgTestMessage{}

#define ASSERT_EQ(expected, actual) \
    if ((expected) == (actual)) ; \
    else return OgAssertHelper{__FILE__, __LINE__, \
        "ASSERT_EQ(" #expected ", " #actual ")"} = \
        (OgTestMessage{} << "Expected: " << (expected) << ", Actual: " << (actual))

#define ASSERT_STREQ(expected, actual) \
    if (std::strcmp((expected), (actual)) == 0) ; \
    else return OgAssertHelper{__FILE__, __LINE__, \
        "ASSERT_STREQ(" #expected ", " #actual ")"} = \
        (OgTestMessage{} << "Expected: \"" << (expected) << "\", Actual: \"" << (actual) << "\"")

#define TEST(suite, name) \
    static void OG_TEST_PASTE(suite, name)(); \
    OG_ADD_TEST_NAMED(OG_TEST_PASTE(suite, name), #suite "." #name, nullptr, nullptr); \
    static void OG_TEST_PASTE(suite, name)()

#define TEST_F(fixture, name) \
    struct OG_TEST_CONCAT(OG_TEST_PASTE(fixture, name), _cls) : fixture { void TestBody(); }; \
    static void OG_TEST_CONCAT(OG_TEST_PASTE(fixture, name), _fn)() { \
        OG_TEST_CONCAT(OG_TEST_PASTE(fixture, name), _cls) inst; \
        const int failed_before = g_tests_failed; \
        inst.SetUp(); \
        if (g_tests_failed == failed_before) \
            inst.TestBody(); \
        inst.TearDown(); \
    } \
    OG_ADD_TEST_NAMED(OG_TEST_CONCAT(OG_TEST_PASTE(fixture, name), _fn), #fixture "." #name, nullptr, nullptr); \
    void OG_TEST_CONCAT(OG_TEST_PASTE(fixture, name), _cls)::TestBody()

void run_all_tests();
void list_all_tests(FILE* out);

// Optional filter: if non-null, only tests whose names contain this substring run
extern const char* g_test_filter;
// Optional exact selector: comma-separated exact test names to run.
extern const char* g_test_exact;

#endif // _TEST_FRAMEWORK_H__
