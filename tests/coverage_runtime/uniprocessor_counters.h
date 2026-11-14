/* Forced into every coverage-instrumented translation unit with -include
 * (CMakeLists.txt, og_instrument_for_coverage). It is valid C and C++.
 *
 * Such a unit is assembled with -Wa,-momit-lock-prefix=yes: its counter
 * updates are single read-modify-write instructions WITHOUT the lock prefix,
 * which are exact only while every thread of the process runs on ONE CPU. The
 * reference below makes that a link-time fact instead of a convention: an
 * executable that holds one stripped object cannot link without
 * tests/coverage_runtime/uniprocessor_counters.cpp, whose constructor
 * confines the process before a second thread can exist.
 * scripts/coverage/README.md ("Counter races") has the whole argument. */
#ifndef OG_TESTS_COVERAGE_RUNTIME_UNIPROCESSOR_COUNTERS_H
#define OG_TESTS_COVERAGE_RUNTIME_UNIPROCESSOR_COUNTERS_H

#ifdef __cplusplus
extern "C" {
#endif
extern const char og_stripped_lock_prefixes_need_the_one_cpu_runtime;
#ifdef __cplusplus
}
#endif

__attribute__((used)) static const char* const og_one_cpu_runtime_anchor =
    &og_stripped_lock_prefixes_need_the_one_cpu_runtime;

#endif
