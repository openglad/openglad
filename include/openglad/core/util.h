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
// util.h
//
// misc helper functions, and timer
//
#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

void init_logging();  // Set up logging output (call early in main)

// Implementation functions - use the Log/LogWarn/LogError wrappers instead
void LogImpl(const char* msg);
void LogWarnImpl(const char* msg);
void LogErrorImpl(const char* msg);

namespace og::detail {

inline void log_plain(void (*sink)(const char*), std::string_view msg)
{
    const std::string owned(msg);
    sink(owned.c_str());
}

template<typename T>
inline std::string log_value_to_string(T&& value)
{
    using Value = std::decay_t<T>;
    if constexpr (std::is_same_v<Value, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<Value, std::string>) {
        return value;
    } else if constexpr (std::is_same_v<Value, std::string_view>) {
        return std::string(value);
    } else {
        std::ostringstream stream;
        stream << std::forward<T>(value);
        return stream.str();
    }
}

template<typename... Args>
inline void log_formatted(void (*sink)(const char*),
                          std::string_view fmt,
                          Args&&... args)
{
    const std::string replacements[] = {
        log_value_to_string(std::forward<Args>(args))...};
    std::string owned;
    owned.reserve(fmt.size() + sizeof...(Args) * 8);

    std::size_t replacement_index = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '{') {
            if ((i + 1) < fmt.size() && fmt[i + 1] == '{') {
                owned.push_back('{');
                ++i;
                continue;
            }

            const std::size_t close = fmt.find('}', i + 1);
            if (close == std::string_view::npos) {
                owned.push_back('{');
                continue;
            }

            if (replacement_index < sizeof...(Args)) {
                owned += replacements[replacement_index++];
            } else {
                owned.append(fmt.substr(i, close - i + 1));
            }
            i = close;
            continue;
        }

        if (fmt[i] == '}' && (i + 1) < fmt.size() && fmt[i + 1] == '}') {
            owned.push_back('}');
            ++i;
            continue;
        }

        owned.push_back(fmt[i]);
    }

    sink(owned.c_str());
}

} // namespace og::detail

namespace og::core {

[[nodiscard]] inline float rounded_render_tick_interval_ms(short timer_wait, float speed_factor)
{
    constexpr float kTimerWaitToMs = 13.6f;

    const float interval_ms =
        static_cast<float>(std::max<int>(timer_wait, 0)) * kTimerWaitToMs;
    if (speed_factor <= 0.0f || interval_ms <= 0.0f)
        return 0.0f;

    const float scaled_interval_ms = interval_ms / speed_factor;
    if (scaled_interval_ms <= 0.0f)
        return 0.0f;

    return static_cast<float>(std::max<std::uint32_t>(
        1u, static_cast<std::uint32_t>(std::lround(scaled_interval_ms))));
}

} // namespace og::core

// Single-argument overloads (no formatting needed)
inline void Log(const char* msg) { LogImpl(msg); }
inline void LogWarn(const char* msg) { LogWarnImpl(msg); }
inline void LogError(const char* msg) { LogErrorImpl(msg); }

inline void Log(std::string_view msg) { og::detail::log_plain(LogImpl, msg); }
inline void LogWarn(std::string_view msg)
{
    og::detail::log_plain(LogWarnImpl, msg);
}
inline void LogError(std::string_view msg)
{
    og::detail::log_plain(LogErrorImpl, msg);
}

template<typename... Args>
void Log(std::string_view fmt, Args&&... args)
{
    og::detail::log_formatted(LogImpl, fmt, std::forward<Args>(args)...);
}
template<typename... Args>
void LogWarn(std::string_view fmt, Args&&... args)
{
    og::detail::log_formatted(LogWarnImpl, fmt, std::forward<Args>(args)...);
}
template<typename... Args>
void LogError(std::string_view fmt, Args&&... args)
{
    og::detail::log_formatted(LogErrorImpl, fmt, std::forward<Args>(args)...);
}

void change_time(std::uint32_t new_count);

void grab_timer();
void release_timer();
void reset_timer();
std::int32_t query_timer();
std::int32_t query_timer_control();

void time_delay(std::int32_t);

// Game speed factor: 1.0 = normal, 2.0 = 2x speed, 0.0 = max speed (no delays)
// g_game_speed_factor moved into GameSession; macro defined in base.h.
// set_game_speed() declared in base.h, defined in game_session.cpp.
// (Kept as comment here so searchers find the new location.)

// Zardus: add: lowercase func
void lowercase(char *);

// kari: lowercase for std::strings
void lowercase(std::string &);

//buffers: add: uppercase func
void uppercase(char *);

// kari: uppercase for std::strings
void uppercase(std::string &);

// Zardus: add: set_mult func
void set_mult(int);

// Safe integer parsing helpers (preferred over atoi()).
// - parse_int_prefix(): trims leading whitespace, parses a leading integer, allows trailing junk.
// - parse_int_strict(): trims leading/trailing whitespace, requires full consumption.
std::optional<int> parse_int_prefix(std::string_view s);
std::optional<int> parse_int_strict(std::string_view s);
