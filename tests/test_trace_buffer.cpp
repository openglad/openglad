#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>

TEST(TraceBuffer, basic_paths)
{
    trace_clear();
    ASSERT_EQ(0, trace_count("alpha")) << "trace_count should start at 0 after clear";
    ASSERT_TRUE(!trace_contains("alpha", "hello")) << "trace_contains should be false for empty buffer";

    TRACE("alpha", "hello %d", 1);
    TRACE("beta", "world");
    TRACE("alpha", "another message");

    ASSERT_EQ(2, trace_count("alpha")) << "trace_count should count matching categories";
    ASSERT_EQ(1, trace_count("beta")) << "trace_count should count secondary category";
    ASSERT_TRUE(trace_contains("alpha", "hello")) << "trace_contains should match substrings";
    ASSERT_TRUE(!trace_contains("alpha", "missing")) << "trace_contains should reject missing substrings";

    trace_clear();
    ASSERT_EQ(0, trace_count("alpha")) << "trace_clear should empty the buffer";
}


TEST(TraceBuffer, trace_dump_no_crash_with_entries_and_empty_buffer)
{
    trace_clear();
    trace_dump();

    TRACE("dump", "first");
    TRACE("dump", "second");
    trace_dump();

    ASSERT_EQ(2, trace_count("dump")) << "trace_dump should not mutate entries";
    trace_clear();
}

