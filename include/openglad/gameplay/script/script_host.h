/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Deterministic, sandboxed Lua VM for class packs and level scripts.
// See docs/lua-classpacks-design.md for the determinism cookbook the
// embedded environment enforces. Lua headers never leak out of
// src/gameplay/script/ (pimpl; enforced by check_vendor_leaks.sh).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace og::script {

struct ScriptError {
    std::string where;      // chunk name or hook identifier
    std::string message;    // Lua error text (with traceback when available)
    std::uint64_t count = 1;  // occurrences collapsed into this record
};

// Upper bound on distinct error records a host keeps. Hooks that error live
// on hot paths (an effect's on_act erroring once per tick per instance), and
// nothing in production drains the store, so it must not grow without bound.
// Repeats collapse into ScriptError::count and the first errors — the ones
// that actually explain a broken pack — are the ones retained.
inline constexpr std::size_t kMaxStoredScriptErrors = 64;

// Upper bound on stored og.log()/print() lines, for the same reason: a pack
// that logs every tick would grow the transcript for the life of the world.
// Unlike errors, the log evicts OLDEST-first — the recent tail is the useful
// part, and it keeps log().back() meaning "most recent".
inline constexpr std::size_t kMaxStoredScriptLogLines = 512;

struct ScriptLimits {
    // Whole-VM allocation cap; blowing it raises a deterministic Lua error.
    std::size_t memory_bytes = 32u * 1024u * 1024u;
    // Per host-entry instruction budget (checked every few thousand VM
    // instructions); identical scripts trip it identically on every peer.
    std::int64_t instructions_per_call = 5'000'000;
};

class ScriptHost {
public:
    explicit ScriptHost(ScriptLimits limits = {});
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Compile and execute a chunk inside the sandbox. Chunks with the same
    // non-empty env_key share one write-isolated environment (a pack's
    // scripts see each other's globals); chunks with an empty env_key get a
    // fresh isolated environment. All environments read through to the
    // shared sandbox root, so packs cannot clobber each other or the
    // stdlib. Returns false on compile/runtime error (recorded in errors()).
    bool run_chunk(std::string_view chunk_name, std::string_view source,
                   std::string_view env_key = {});

    // Evaluate a single expression in a fresh sandboxed environment and
    // coerce the result. nullopt on error or type mismatch (error recorded).
    // eval_integer requires an integer-subtype result (no silent coercion).
    std::optional<std::int64_t> eval_integer(std::string_view expr);
    std::optional<double> eval_number(std::string_view expr);
    std::optional<bool> eval_boolean(std::string_view expr);
    std::optional<std::string> eval_string(std::string_view expr);

    // True if the dotted path (e.g. "string.format", "os.time") resolves to a
    // non-nil value in the sandbox root. Used by tests to pin the sandbox.
    bool sandbox_has(std::string_view dotted_path);

    // Stored error records, in first-seen order. Bounded by
    // kMaxStoredScriptErrors; repeats of an already-stored (where, message)
    // pair bump that record's count instead of appending.
    const std::vector<ScriptError>& errors() const;
    // Occurrences discarded because the store was full — the "+N more" of a
    // diagnostic. Repeats folded into a stored record are not counted here.
    std::uint64_t dropped_error_count() const;
    void clear_errors();

    std::size_t memory_used() const;
    std::size_t memory_limit() const;

    // Lines emitted by scripts through print()/og.log(), in emission order.
    // Bounded to the most recent kMaxStoredScriptLogLines; every line is
    // traced unconditionally regardless of what the store keeps.
    const std::vector<std::string>& log() const;

    // Log lines evicted because the transcript was full.
    std::uint64_t dropped_log_line_count() const;

    // Internal access for the og.* binding layer (src/gameplay/script/ only;
    // Impl is defined in script_host_impl.h which is private to that dir).
    struct Impl;
    Impl& impl();

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace og::script
