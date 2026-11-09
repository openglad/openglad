/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "test_trace.h"

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


bool trace_stderr_enabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("OG_TEST_TRACE_STDERR");
        return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

void trace_write(const char* category, const char* format, ...) {
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
