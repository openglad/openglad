#include "test_trace.h"

#ifdef TESTING

#include <cstring>
#include <cstdio>
#include <mutex>

std::vector<TraceEntry> g_trace_buffer;
std::mutex g_trace_mutex;

bool trace_contains(const char* category, const char* substring) {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    for (const auto& entry : g_trace_buffer) {
        if (entry.category == category && entry.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int trace_count(const char* category) {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int count = 0;
    for (const auto& entry : g_trace_buffer) {
        if (entry.category == category) {
            count++;
        }
    }
    return count;
}

void trace_clear() {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    g_trace_buffer.clear();
}

void trace_dump() {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "=== Trace Dump (%zu entries) ===\n", g_trace_buffer.size());
    for (const auto& entry : g_trace_buffer) {
        fprintf(stderr, "  [%s] %s\n", entry.category.c_str(), entry.message.c_str());
    }
    fprintf(stderr, "=== End Trace Dump ===\n");
}

#endif // TESTING
