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

#define OG_UNIT_TEST(name) \
    static void name(); \
    static ::og::unit::Registrar name##_registrar(#name, &name); \
    static void name()

#define OG_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "ASSERT FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while (0)

