# Phase 2: GTest-Compatible Naming

**Goal:** Rewrite all test files to use GoogleTest-style syntax (`TEST()`,
`ASSERT_TRUE()`, etc.) while still backed by the custom framework. No GoogleTest
dependency required. This is a mechanical, scriptable transformation.

## Compatibility Macros

Add GTest-compatible macros to `test_framework.h`. These wrap the existing custom
framework with GTest-style signatures.

**Streaming assertion support:**

```cpp
// Accumulates << messages, prints on destruction
struct OgTestMessage {
    std::string str;
    template<typename T>
    OgTestMessage& operator<<(const T& val) {
        std::ostringstream oss;
        oss << val;
        str += oss.str();
        return *this;
    }
};

// Records assertion failure; used with `return` to exit test on failure
struct OgAssertHelper {
    const char* file;
    int line;
    const char* expr;

    void operator=(OgTestMessage msg) {
        fprintf(stderr, "  FAIL: %s", expr);
        if (!msg.str.empty())
            fprintf(stderr, " - %s", msg.str.c_str());
        fprintf(stderr, " (%s:%d)\n", file, line);
        g_tests_failed++;
    }
};
```

**Assertion macros (fatal — test stops on failure):**

```cpp
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
```

**Registration macros:**

```cpp
#define OG_TEST_PASTE(a, b) a##_##b

// TEST(Suite, Name) — self-registering test
#define TEST(suite, name) \
    static void OG_TEST_PASTE(suite, name)(); \
    REGISTER_TEST(OG_TEST_PASTE(suite, name)); \
    static void OG_TEST_PASTE(suite, name)()

// TEST_F(Fixture, Name) — test with fixture class (SetUp/TearDown)
#define TEST_F(fixture, name) \
    struct fixture##_##name##_cls : fixture { void TestBody(); }; \
    static void fixture##_##name##_fn() { \
        fixture##_##name##_cls inst; \
        inst.SetUp(); \
        inst.TestBody(); \
        inst.TearDown(); \
    } \
    REGISTER_TEST(fixture##_##name##_fn); \
    void fixture##_##name##_cls::TestBody()
```

**Unit test compatibility** (in `unit.h`):

```cpp
// TEST(Suite, Name) for unit tests — wraps OG_UNIT_TEST registration
#define TEST(suite, name) \
    static void suite##_##name(); \
    static ::og::unit::Registrar suite##_##name##_registrar( \
        #suite "." #name, &suite##_##name); \
    static void suite##_##name()

// ASSERT_TRUE — aborts on failure (matches current OG_ASSERT severity)
#define ASSERT_TRUE(cond) OG_ASSERT(cond)
```

## Mechanical Rewrites

Every test file gets a mechanical transformation. The patterns are:

**Registration:**
```cpp
// BEFORE                              // AFTER
REGISTER_TEST(test_foo);               // deleted — TEST() self-registers
void test_foo() {                      TEST(WalkerCombat, foo) {
```

**Assertions (fatal — test stops on failure):**
```cpp
// BEFORE                                      // AFTER
TEST_ASSERT(cond, "msg");                      ASSERT_TRUE(cond) << "msg";
TEST_ASSERT_EQ(expected, actual, "msg");       ASSERT_EQ(expected, actual) << "msg";
TEST_ASSERT_STR_EQ(expected, actual, "msg");   ASSERT_STREQ(expected, actual) << "msg";
OG_ASSERT(cond);                               ASSERT_TRUE(cond);
```

**Fixtures:**
```cpp
// BEFORE
void setup_foo() { ... }
void teardown_foo() { ... }
void test_bar() { ... }
REGISTER_TEST_WITH_FIXTURE(test_bar, setup_foo, teardown_foo);

// AFTER
class FooFixture {
public:
    void SetUp() { ... }
    void TearDown() { ... }
};
TEST_F(FooFixture, bar) { ... }
```

**File-local wrapper macros:**
```cpp
// BEFORE (test_walker_specials.cpp — 44 tests)
#define REGISTER_SPECIAL_TEST(func) \
    REGISTER_TEST_WITH_FIXTURE(func, nullptr, teardown_walker_special_test)
void test_walker_special_soldier_charge() { ... }
REGISTER_SPECIAL_TEST(test_walker_special_soldier_charge);

// AFTER
class WalkerSpecialFixture {
public:
    void SetUp() {}
    void TearDown() { teardown_walker_special_test(); }
};
TEST_F(WalkerSpecialFixture, soldier_charge) { ... }
```

```cpp
// BEFORE (test_mass_coverage.cpp — 116 tests)
#define MASS_TEST(name, ...) \
    void name() { __VA_ARGS__ } \
    REGISTER_TEST(name)
MASS_TEST(test_mass_menunav_up, { (void)MenuNav{.up=1}; });

// AFTER
TEST(MassCoverage, menunav_up) { (void)MenuNav{.up=1}; }
```

**Trace-based assertions:**
```cpp
// BEFORE
trace_clear();
// ... trigger behavior ...
TEST_ASSERT(trace_contains("combat", "hit"), "expected hit trace");

// AFTER
trace_clear();
// ... trigger behavior ...
ASSERT_TRUE(trace_contains("combat", "hit")) << "expected hit trace";
```

## Test Suite Naming

Each test file maps to a GoogleTest suite. The suite name comes from the file:

| File | Suite Name |
|------|-----------:|
| test_walker_combat.cpp | WalkerCombat |
| test_effect_act.cpp | EffectAct |
| test_io_funcs.cpp | IoFuncs |
| test_external_yaml.cpp | ExternalYaml |

## Trace Header Consolidation

Update all files that include the legacy trace header to use the core path:

- `src/test_trace.cpp` — change `#include <openglad/legacy/test_trace.h>` to
  `#include <openglad/core/test_trace.h>`
- `src/test_trace.h` (transitional shim) — change to redirect to `<openglad/core/test_trace.h>`
- `src/platform/sdl/game.cpp`, `src/platform/sdl/video_sdl.cpp` — same change
- 23 test files that include `<openglad/legacy/test_trace.h>` — same change

(27 files total across test and non-test code. The headers are currently identical;
this just normalizes the include path before the legacy header is deleted in Phase 3.)

## What Stays (unchanged)

| File | Reason |
|------|--------|
| `tests/test_interact.h` | SDL button interaction helpers — not test framework |
| `tests/test_input_helpers.h` | SDL event injection — not test framework |
| `tests/test_game_world_fixture.h` | Helper struct for minimal game world — tests create as local variable |
| `tests/test_gameplay_context_scope.h` | RAII scope guard — framework-independent |
| `include/openglad/core/test_trace.h` | Canonical trace header (all includes consolidated here) |
| `src/test_trace.cpp` | Trace buffer implementation |

## Verification

1. All 1787 tests still pass
2. No test file references old macros (`REGISTER_TEST`, `TEST_ASSERT`, `OG_UNIT_TEST`)
3. All trace includes use `<openglad/core/test_trace.h>`
4. CI passes
