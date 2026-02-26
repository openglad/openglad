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

#include <cstdint>
#include <cctype>
#include <optional>
#include <format>
#include <string>
#include <string_view>
#include <utility>

void init_logging();  // Set up logging output (call early in main)

// Implementation functions - use the Log/LogWarn/LogError wrappers instead
void LogImpl(const char* msg);
void LogWarnImpl(const char* msg);
void LogErrorImpl(const char* msg);

// Single-argument overloads (no formatting needed)
inline void Log(const char* msg) { LogImpl(msg); }
inline void LogWarn(const char* msg) { LogWarnImpl(msg); }
inline void LogError(const char* msg) { LogErrorImpl(msg); }

inline void Log(std::string_view msg) { LogImpl(std::string(msg).c_str()); }
inline void LogWarn(std::string_view msg) { LogWarnImpl(std::string(msg).c_str()); }
inline void LogError(std::string_view msg) { LogErrorImpl(std::string(msg).c_str()); }

// std::format overloads - type-safe formatting
template<typename... Args>
void Log(std::format_string<Args...> fmt, Args&&... args) {
    LogImpl(std::format(fmt, std::forward<Args>(args)...).c_str());
}
template<typename... Args>
void LogWarn(std::format_string<Args...> fmt, Args&&... args) {
    LogWarnImpl(std::format(fmt, std::forward<Args>(args)...).c_str());
}
template<typename... Args>
void LogError(std::format_string<Args...> fmt, Args&&... args) {
    LogErrorImpl(std::format(fmt, std::forward<Args>(args)...).c_str());
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
