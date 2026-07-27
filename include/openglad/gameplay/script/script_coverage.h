/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Coverage recorder for pack Lua.
//
// Why this exists: after the class-pack conversion, family behavior IS the
// Lua under packs/. The repo's gcovr gate measures src/ only, so it would
// report green while thousands of lines of game logic went untested. This
// records the two things a Lua coverage report needs —
//
//   * LINE coverage: which (chunk, line) pairs actually executed, via a
//     LUA_MASKLINE debug hook riding alongside the existing instruction
//     budget hook.
//   * FUNCTION coverage: which Lua FUNCTIONS ran a line of their own body.
//     Identity is the function's prototype, keyed by (chunk, linedefined,
//     lastlinedefined) — the same SPAN the static side enumerates and the
//     same one an lcov FN record is named after. The span, not the start
//     line alone: `local a, b = f(function() end), f(function()\n ... \nend)`
//     defines two prototypes that share a `linedefined`, and keying on that
//     alone collapsed them into one entry, so covering either marked both.
//     (`scripts/check_lua_statement_lines.py` additionally forbids two
//     `function` keywords on one line, which makes even an identical span
//     unrepresentable in shipped pack Lua.)
//
//     Deriving the hit from a line event inside the body is what makes a
//     hook that is DISPATCHED BUT NEVER ENTERED read as a miss: with the hit
//     stamped at the call site, a hook the engine decided to call counted as
//     covered whether or not control ever reached its body.
//
//     What this does NOT distinguish, and no line-derived metric can: a
//     function whose body is empty (`function() end` fires a line event on
//     its `end`, which carries OP_RETURN) and a function that raises on its
//     first statement (that statement has already been reported) both count
//     as covered. A dispatched no-op stub is indistinguishable from an
//     implementation. See scripts/coverage/README.md.
//
// ...plus the static half a denominator needs: source_facts() asks Lua
// itself which lines of a source file can hold a breakpoint AND which
// prototypes it defines, so a pack file no test ever loads counts as 0% of
// both metrics rather than as absent from either.
//
// ...plus the inventory the denominator has to be built from:
// declare_pack_source() records every script the ENGINE loads, whether it
// came from the host filesystem, from inside a campaign .glad, or from a C++
// string literal. Enumerating packs/ on disk is not the same set.
//
// DETERMINISM CONTRACT (docs/lua-classpacks-design.md §3). This is compiled
// into every build and gated at RUNTIME on the OPENGLAD_LUA_COVERAGE
// environment variable, deliberately: a compile-time gate would mean the
// binary the parity gate exercises is not the binary that ships, so
// "instrumentation cannot perturb the sim" would be untestable. With the
// recorder off:
//   * the debug hook installed by ScriptHost::Impl::protected_call is
//     bit-identical to the pre-coverage one — same function pointer, same
//     LUA_MASKCOUNT mask, same count — so the VM takes the same code path;
//   * every recording site is behind a single load of the `g_enabled`
//     global plus a perfectly-predicted branch, none of it per-instruction;
//   * nothing is allocated, opened or written.
// Even with the recorder ON nothing here is visible to script code or to
// sim state, so a coverage run stays deterministic too — it is only slower.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace og::script::coverage {

namespace detail {
// Hot-path flag; see the determinism contract above. Never read by the sim.
extern bool g_enabled;
}  // namespace detail

// True when pack-Lua coverage recording is armed for this process.
inline bool enabled()
{
    return detail::g_enabled;
}

// Directory the raw dump is written to at process exit (the value of
// OPENGLAD_LUA_COVERAGE). Empty when recording is off.
const std::string& output_dir();

// Test seam: arm/disarm the recorder regardless of the environment. Setting
// it true without an output directory records in memory only.
void set_enabled_for_testing(bool on);

// Test seam: redirect (or, with an empty string, disable) the exit dump.
void set_output_dir_for_testing(std::string dir);

// Drop everything recorded so far.
void reset();

// Test seam: record into a private, empty set for the lifetime of the object,
// then put back exactly what was there before. Tests cannot simply reset()
// the recorder — under an actual coverage run the process-global store is
// already accumulating real pack coverage, and a bare reset would throw its
// own binary's numbers away.
class ScopedRecording {
public:
    ScopedRecording();
    ~ScopedRecording();
    ScopedRecording(const ScopedRecording&) = delete;
    ScopedRecording& operator=(const ScopedRecording&) = delete;

private:
    struct State;
    std::unique_ptr<State> saved_;
    bool previously_enabled_ = false;
};

// --- recording sites (all no-ops unless enabled()) -------------------------

// A line of `chunk` executed. `chunk` is the name the source was loaded
// under, e.g. "packs/core/scripts/soldier.lua".
//
// GENERATION BINDING. Every hit recorded here (and by record_function_line
// below) is stored under (chunk, digest of the generation that was ACTIVE
// when it executed) — the digest of the most recent declare_pack_source for
// this chunk, or the empty marker when nothing was declared yet. It is not
// enough to keep every declared generation and let the report guess: one
// chunk name really does carry two different sources in one process (a
// regenerated pack cache mounts both under one path), and a report that
// credited a hit to EVERY declared generation whose grid contained the line
// let a never-loaded byte-variant of a shipped script score 86/101 lines off
// another file's execution. Declaration precedes execution in the engine
// (og::resources::register_mounted_pack_scripts declares before any VM can
// replay the script), so the active generation at record time IS the
// generation the VM compiled.
void record_line(std::string_view chunk, int line);

// A pack registered a hook function whose prototype spans
// (chunk, line_defined .. last_line_defined).
// `label` identifies the registration, e.g. "living/core:soldier/do_special".
// PURELY DIAGNOSTIC: it names the function in the report. Registration is
// not coverage — the denominator comes from the static prototype walk and
// the numerator from record_function_line below.
void declare_function(std::string_view chunk, int line_defined,
                      int last_line_defined, std::string_view label);

// A line belonging to the function spanning
// (chunk, line_defined .. last_line_defined) just executed — i.e. that
// function's body really ran. Called from the same line hook that feeds
// record_line, off the same lua_getinfo, which is why the two metrics cannot
// disagree about whether code ran.
void record_function_line(std::string_view chunk, int line_defined,
                          int last_line_defined);

// A script the ENGINE loaded, recorded with its source text and the real
// directory or archive it came from. This is the evidence half of the
// numerator: the report attributes a chunk's recorded hits to the bytes the
// recording process declared for that chunk, so a script that lives inside a
// campaign .glad — or is generated from a C++ string literal and never
// written to the host tree at all — is still measured against its real
// content.
//
// IDENTITY IS (chunk, sha256(source)), NOT the chunk name. One name can
// carry two different sources in one process — `tests/unit/
// test_pack_transfer_errors.cpp` regenerates a cached pack and mounts both
// generations under `packs/org.test.regen/scripts/a.lua` — and while the
// chunk name alone was the key, the second declaration silently ERASED the
// first. That is not a bookkeeping detail: the report keys hits by what was
// declared, so an overwrite let hits recorded against one source be scored
// against a different source's grid. Both declarations are now kept and both
// appear in the dump.
//
// Declaring also makes this digest the chunk's ACTIVE generation: every hit
// recorded from here on is stored — and dumped — under it, until the chunk
// is re-declared with different bytes (see record_line above). Re-declaring
// the same bytes (every level load does) changes nothing.
//
// `origin` is diagnostic only — it names the real directory or archive
// PhysFS resolved the script from, which is what a "these bytes are not
// repository content" failure has to print to be actionable. It is NOT a
// filter: an earlier revision kept only origins inside the repository, which
// made the denominator depend on where a test staged its files, and this
// comment used to say so. That filter is gone; content hashing replaced it.
//
// No-op unless enabled(); the source is copied only then.
void declare_pack_source(std::string_view chunk, std::string_view source,
                         std::string_view origin);

// --- readback --------------------------------------------------------------

struct LineHit {
    std::string chunk;
    // sha256 of the generation that was active when the hit executed; empty
    // when the chunk had no declared source at that moment (test Lua compiled
    // from a string literal — a mounted pack script is always declared before
    // it can run). A hit belongs to exactly ONE (chunk, digest); the report
    // never has to guess which declared generation executed.
    std::string digest;
    int line = 0;
    std::uint64_t count = 0;
};

struct FunctionRecord {
    std::string chunk;
    std::string digest;  // generation binding, exactly as in LineHit
    int line_defined = 0;
    int last_line_defined = 0;  // with line_defined, the prototype's identity
    std::string label;  // lexicographically smallest registered label
    // Line events observed INSIDE this function's body. Not a call count —
    // it is the evidence that the body ran at all, and 0 means it never did.
    std::uint64_t body_hits = 0;
};

struct PackSourceRecord {
    std::string chunk;
    std::string source;
    std::string origin;  // real directory or archive it was loaded from
    std::string digest;  // sha256 of `source`, lowercase hex; the identity
};

// sha256 of `data` as 64 lowercase hex digits. Exposed because the dump
// format is content-addressed and a reader has to be able to check it:
// the sidecar a dump points at is verified against the digest the dump
// recorded, so a dump cannot claim one script's bytes and ship another's.
std::string sha256_hex(std::string_view data);

// Basename of the running executable, or empty where the platform has no way
// to ask. Written into every dump so the report can tell which recorder
// processes contributed — a suite that lost two thirds of its processes
// otherwise just looks like slightly lower coverage. See
// scripts/coverage/recorder_processes.txt.
std::string program_name();

// All sorted — (chunk, digest, line), (chunk, digest, span), (chunk, digest)
// respectively — so callers and dumps are reproducible.
std::vector<LineHit> line_hits();
std::vector<FunctionRecord> function_records();
std::vector<PackSourceRecord> pack_sources();

// Write the raw dump (see scripts/coverage/README.md for the format) to
// `path`. Returns false if the file could not be written.
bool write_raw_report(const std::string& path);

// Write one uniquely-named dump into output_dir(). No-op when the recorder
// is off or no output directory was configured. Called automatically at
// process exit — but a harness that ends with _exit() (tests/integration_main
// .cpp does, which is why it also calls __gcov_dump by hand) skips static
// destructors and must call this itself. Repeated calls rewrite the SAME
// file, so an explicit flush followed by the exit-time one cannot
// double-count.
void flush_to_output_dir();

// --- static analysis -------------------------------------------------------

// What a Lua source contains, according to Lua: BOTH halves of the
// denominator come from one walk of one prototype tree, so the numerator and
// the denominator cannot end up on different grids.
//
// `lines` — every line that can hold a breakpoint, unioned over the main
// chunk and every nested prototype. This is Lua's own answer (the walk
// debug.getinfo(f, "L") performs, recursed through Proto::p), not a regex
// over the text — which matters because the two disagree in both directions.
// Comments, blank lines, `else` and the `end` of a control structure carry no
// instruction and are absent; but the `end` that closes a FUNCTION is present
// (it carries the enclosing chunk's OP_CLOSURE) and so is a bare `local x`
// with no initializer (it emits OP_LOADNIL).
//
// `functions` — the (linedefined, lastlinedefined) SPAN of EVERY prototype in
// the tree, the main chunk (0, 0) included. Every function counts: hook,
// local helper, anonymous callback, one-liner. That is the point — a metric
// that counted only registered hooks let an uncalled helper cost nothing, and
// let a whole script no test loads contribute no functions at all. The span
// rather than the start line, because two prototypes can begin on one line
// and keying on the start alone merged them into a single entry that either
// one could cover.
//
// See scripts/coverage/README.md.
struct FunctionSpan {
    int line_defined = 0;
    int last_line_defined = 0;

    friend bool operator==(const FunctionSpan& a, const FunctionSpan& b)
    {
        return a.line_defined == b.line_defined &&
               a.last_line_defined == b.last_line_defined;
    }
    friend bool operator<(const FunctionSpan& a, const FunctionSpan& b)
    {
        if (a.line_defined != b.line_defined)
            return a.line_defined < b.line_defined;
        return a.last_line_defined < b.last_line_defined;
    }
};

struct SourceFacts {
    bool ok = false;
    std::string error;      // compile error text when !ok
    std::vector<int> lines; // ascending, deduplicated
    // prototype spans, ascending by (line_defined, last_line_defined)
    std::vector<FunctionSpan> functions;
};

SourceFacts source_facts(std::string_view source, std::string_view chunk_name);

}  // namespace og::script::coverage
