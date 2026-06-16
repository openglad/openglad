#pragma once

#ifdef TESTING

#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>

struct TraceEntry {
    std::string category;
    std::string message;
};

extern std::vector<TraceEntry> g_trace_buffer;
extern std::mutex g_trace_mutex;

bool trace_contains(const char* category, const char* substring);
int trace_count(const char* category);
void trace_clear();
void trace_dump();

inline bool trace_stderr_enabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("OG_TEST_TRACE_STDERR");
        return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

inline void trace_write(const char* category, const char* format, ...) {
    std::array<char, 1024> buf{};
    va_list args;
    va_start(args, format);
    vsnprintf(buf.data(), buf.size(), format, args);
    va_end(args);
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    g_trace_buffer.push_back({category, buf.data()});
    if (trace_stderr_enabled())
        fprintf(stderr, "[TRACE:%s] %s\n", category, buf.data());
}

#define TRACE(cat, ...) trace_write(cat, __VA_ARGS__)

#else

#define TRACE(cat, ...) ((void)0)

#endif // TESTING
