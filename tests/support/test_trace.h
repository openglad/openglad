#pragma once

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

bool trace_stderr_enabled();
void trace_write(const char* category, const char* format, ...);
