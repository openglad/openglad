/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//
// util.cpp
//
// random helper functions
//
#include <openglad/core/util.h>

#include <openglad/core/version.h>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cctype>
#include <cstring> //buffers: for strlen
#include <optional>
#include <string>
#include <sys/stat.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <thread>
#endif

static auto g_app_start = std::chrono::steady_clock::now();
static thread_local auto g_reset_time = std::chrono::steady_clock::now();

static std::uint32_t get_ticks_ms()
{
    auto now = std::chrono::steady_clock::now();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_app_start).count());
}

// g_game_speed_factor and set_game_speed() moved to runtime/game_session.cpp (Batch 7).

void init_logging()
{
    // No-op: logging uses fprintf/EM_ASM directly, no SDL needed.
}

#ifdef __EMSCRIPTEN__
void LogImpl(const char* msg)
{
    EM_ASM({ console.log(UTF8ToString($0)); }, msg);
}

void LogWarnImpl(const char* msg)
{
    EM_ASM({ console.warn(UTF8ToString($0)); }, msg);
}

void LogErrorImpl(const char* msg)
{
    EM_ASM({ console.error(UTF8ToString($0)); }, msg);
}
#else
void LogImpl(const char* msg)
{
    std::fprintf(stderr, "%s", msg);
}

void LogWarnImpl(const char* msg)
{
    std::fprintf(stderr, "[WARN] %s", msg);
}

void LogErrorImpl(const char* msg)
{
    std::fprintf(stderr, "[ERROR] %s", msg);
}
#endif

void change_time(std::uint32_t /*new_count*/)
{}

void grab_timer()
{}

void release_timer()
{}

void reset_timer()
{
    g_reset_time = std::chrono::steady_clock::now();
}

std::int32_t query_timer()
{
    // Zardus: why 13.6? With DOS timing, you had to divide 1,193,180 by the desired frequency and
    // that would return ticks / second. Gladiator used to use a frequency of 65536/4 ticks per hour,
    // or 1193180/16383 = 72.3 ticks per second. This translates into 13.6 milliseconds / tick
    auto elapsed_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_reset_time).count());
    return static_cast<std::int32_t>(elapsed_ms / 13.6);
}

std::int32_t query_timer_control()
{
    return static_cast<std::int32_t>(get_ticks_ms() / 13.6);
}

void time_delay(std::int32_t delay)
{
    if (delay <= 0) return;
    auto target_us = static_cast<std::int64_t>(delay * 13600); // microseconds
#ifdef __EMSCRIPTEN__
  #ifdef __ASYNCIFY__
    emscripten_sleep(static_cast<unsigned int>(target_us / 1000));
  #endif
#else
    auto start = std::chrono::steady_clock::now();
    // Sleep for the bulk of the time, leaving 2ms margin to avoid overshoot
    if (target_us > 3000) {
        std::this_thread::sleep_for(std::chrono::microseconds(target_us - 2000));
    }
    // Spin-wait for the remainder to hit the target precisely
    while (std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start).count() < target_us) {
        // busy-wait
    }
#endif
}

void lowercase(char * str)
{
    for (size_t i = 0, len = strlen(str); i < len; i++)
        str[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(str[i])));
}

//buffers: add: another extra routine.
void uppercase(char *str)
{
    for (size_t i = 0, len = strlen(str); i < len; i++)
        str[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[i])));
}

// kari: yet two extra
void lowercase(std::string &str)
{
    for(auto& c : str)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

void uppercase(std::string &str)
{
    for(auto& c : str)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

static std::string_view trim_left_ascii_ws(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    return s;
}

static std::string_view trim_right_ascii_ws(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::optional<int> parse_int_prefix(std::string_view s)
{
    s = trim_left_ascii_ws(s);
    if (s.empty())
        return std::nullopt;

    int value = 0;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr == begin)
        return std::nullopt;
    return value;
}

std::optional<int> parse_int_strict(std::string_view s)
{
    s = trim_right_ascii_ws(trim_left_ascii_ws(s));
    if (s.empty())
        return std::nullopt;

    int value = 0;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr == begin)
        return std::nullopt;

    std::string_view rest(result.ptr, static_cast<size_t>(end - result.ptr));
    rest = trim_right_ascii_ws(rest);
    if (!rest.empty())
        return std::nullopt;

    return value;
}
