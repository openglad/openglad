/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Dependency-free 64-bit FNV-1a. Used for content integrity/versioning of
// multiplayer class-pack transfers (docs/lua-classpacks-design.md §8): both
// peers hash the same bytes and compare. This is NOT tamper-proofing — pack
// payloads are sandboxed Lua/YAML/PNG by construction, so a collision or a
// forged hash can at worst waste a re-download. OpenSSL is deliberately not
// used here: the Emscripten transport has no OpenSSL, and every platform
// (native host, wasm client, headless server) must produce identical hashes.

#include <cstddef>
#include <cstdint>
#include <span>

namespace og::core {

inline constexpr std::uint64_t kFnv1a64OffsetBasis = 0xcbf29ce484222325ull;
inline constexpr std::uint64_t kFnv1a64Prime = 0x00000100000001b3ull;

// Incremental form: feed successive byte runs through `state`, starting from
// kFnv1a64OffsetBasis.
[[nodiscard]] constexpr std::uint64_t
fnv1a64_append(std::uint64_t state, const std::uint8_t* data,
               std::size_t len) noexcept
{
    for (std::size_t i = 0; i < len; ++i)
    {
        state ^= static_cast<std::uint64_t>(data[i]);
        state *= kFnv1a64Prime;
    }
    return state;
}

[[nodiscard]] constexpr std::uint64_t
fnv1a64(const std::uint8_t* data, std::size_t len) noexcept
{
    return fnv1a64_append(kFnv1a64OffsetBasis, data, len);
}

[[nodiscard]] inline std::uint64_t
fnv1a64(std::span<const std::uint8_t> bytes) noexcept
{
    return fnv1a64(bytes.data(), bytes.size());
}

} // namespace og::core
