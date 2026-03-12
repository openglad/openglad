#pragma once

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace og::unit {

using TestFn = void (*)();

struct TestCase {
    const char* name;
    TestFn fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, TestFn fn) { registry().push_back(TestCase{name, fn}); }
};

} // namespace og::unit

#define TEST(suite, name) \
    static void suite##_##name(); \
    static ::og::unit::Registrar suite##_##name##_registrar( \
        #suite "." #name, &suite##_##name); \
    static void suite##_##name()

#define OG_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "ASSERT FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while (0)

#define ASSERT_TRUE(cond) OG_ASSERT(cond)
