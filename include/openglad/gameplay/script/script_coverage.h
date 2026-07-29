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
//   * the ONE deliberate exception is per-COMPILE, not per-instruction:
//     bind_compiled_chunk() maintains the Proto registry even when off,
//     because a compile the registry never saw can reuse a freed Proto
//     address, and a later enable would then credit fresh code to a dead
//     generation. Disabled, that maintenance is pure map erasure — on a map
//     that stays empty in a process that never enabled recording — with no
//     digesting and no recording (see bind_compiled_chunk below);
//   * nothing is allocated, opened or written.
// Even with the recorder ON nothing here is visible to script code or to
// sim state, so a coverage run stays deterministic too — it is only slower.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Forward declarations only: this header must stay includable without the
// Lua headers (lua.h typedefs these exact struct names, so the declarations
// are compatible whichever is seen first).
struct lua_State;
struct lua_Debug;

namespace og::script::coverage {

namespace detail {
// Hot-path flag; see the determinism contract above. Never read by the sim.
extern bool g_enabled;
// Test seam (unconditional, like set_enabled_for_testing below — the game
// libraries are not recompiled for the unit binaries): invoked after a
// dump's temp file has been written in full and flushed, immediately before
// the rename() that publishes it — the one instant where a kill used to be
// able to leave a torn dump. The atomicity test points it at raise(SIGKILL)
// inside a forked child and then checks that the published path still holds
// the previous complete dump. Null in production and read only on the
// per-flush path, never per hit.
extern void (*g_between_dump_write_and_publish)();
}  // namespace detail

// True when pack-Lua coverage recording is armed for this process.
inline bool enabled()
{
    return detail::g_enabled;
}

// Directory the raw dump is written to at process exit (the value of
// OPENGLAD_LUA_COVERAGE). Empty when recording is off.
const std::string& output_dir();

// Interpret one OPENGLAD_LUA_COVERAGE value.
//
// THE VARIABLE IS A DIRECTORY PATH, NOT A BOOLEAN. `OPENGLAD_LUA_COVERAGE=1`
// reads like a switch and used to behave like one — it armed the recorder and
// then created a directory literally named `1` in whatever the process's
// working directory happened to be. A relative path is the same defect even
// when it is meant: the dump is written at process exit, from a cwd the
// recorder does not control and which differs between the test binaries a
// single run launches, so the dumps scatter. Only an absolute path is
// accepted; anything else returns empty (recording OFF) after saying so on
// stderr, which the coverage report then surfaces as an unmet Lua bar rather
// than as a plausible-looking number.
//
// Exposed so the rule is unit-testable. The process itself reads the
// environment exactly once, at static-init time.
std::string validate_output_dir(std::string_view value);

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

// --- recording sites -------------------------------------------------------
//
// All no-ops unless enabled(), with ONE carve-out: bind_compiled_chunk's
// registry maintenance runs unconditionally (its declaration below states
// the design and the cost bound).
//
// GENERATION BINDING IS PROTO-TRUE. A hit is credited to the generation
// whose COMPILED CODE is executing, never to a chunk-level "most recently
// declared" digest. The two genuinely diverge: one chunk name carries two
// different sources in one process (a regenerated pack cache mounts both
// under one path; a campaign .glad overrides a core script while a GameWorld
// compiled from the old bytes is still alive), and a still-live closure of
// the OLD generation keeps executing after the NEW one is declared. An
// earlier revision bound hits to the most recent declaration and claimed
// declaration order made that safe; it did not. A stale closure's hits went
// to the newest digest — silently covering lines of a source that never ran
// where the two grids overlapped (a dishonest pass), and hard-failing the
// report with an off-grid error where they did not (a false failure). Both
// from the one defect, both reproduced before the rewrite.
//
// So the binding happens where the truth is known: at COMPILE time.
// bind_compiled_chunk() walks the freshly compiled closure's prototype tree
// — the bytes and their digest are in hand at that moment — and registers
// every Proto* against (chunk, sha256 of those bytes); the line hook then
// resolves the EXECUTING function's Proto* through that registry and credits
// the hit to its own generation, whatever has been declared since.

// Register the closure luaL_loadbuffer just produced at stack slot `index`
// from `source` under the chunk name `chunk`. Every prototype in its tree is
// bound to (chunk, sha256(source)) — PROVIDED that exact pair has been
// declared via declare_pack_source(), which is the engine's invariant
// (og::resources::register_mounted_pack_scripts declares at mount time,
// before any VM can compile the script). Compiling UNDECLARED bytes (test
// Lua built from a string literal) instead scrubs any stale registry entry
// its prototypes' addresses may have inherited, so their hits fall through
// to the no-generation marker below.
//
// Why raw Proto addresses are sound keys, spelled out because the registry
// never unregisters:
//   * Lua's GC does not move objects, and a Proto stays reachable for as
//     long as any closure over it does — so any prototype alive enough to
//     EXECUTE still owns its address, and the entry written here for it is
//     the entry the hook will find.
//   * A collected Proto's address can be reused only after every closure
//     over it became unreachable, i.e. after that generation could no longer
//     execute. Its recorded hits already sit in the store keyed by (chunk,
//     digest) — nothing reads the registry backwards — so overwriting the
//     dead entry with the new occupant's binding is correct, not a race.
//   * The remaining hazard would be reuse by a prototype that never gets a
//     binding (undeclared test Lua). Both ScriptHost compile sites funnel
//     through this function, and an undeclared compile ERASES its
//     prototypes' addresses rather than skipping them, so a stale entry
//     cannot shadow live unregistered code.
//
// UNCONDITIONAL, unlike every other recording site: the registry
// maintenance above runs whether or not the recorder is enabled. That is
// the chosen design for the disable/re-enable hazard — with the maintenance
// gated on enabled(), a chunk compiled while recording was off was neither
// registered nor scrubbed, so a Proto address freed under a dead binding
// and reused by that compile RESURRECTED the dead generation the moment
// recording came back on: fresh, undeclared code executed and its hits were
// credited to a digest whose code no longer existed. (In production, arming
// is process-lifetime — only the test seam can toggle — but the invariant
// must not depend on that.) The off-path cost is bounded to make
// zero-cost-when-off stay honest: per compile, a mutex acquire plus one map
// erasure per prototype in the chunk — erasures against a map that STAYS
// EMPTY in a process that never enabled recording — never a digest, never
// an insertion, no allocation growth.
void bind_compiled_chunk(lua_State* L, int index, std::string_view chunk,
                         std::string_view source);

// The LUA_MASKLINE hook body: a line of the currently executing function
// just ran. One call feeds BOTH metrics — the line hit, and the executing
// prototype's body-ran evidence — off the same event, which is why the two
// cannot disagree about whether code ran. The executing function's Proto is
// resolved through the registry bind_compiled_chunk maintains and the hit is
// credited to ITS generation. When the prototype is unregistered (test Lua
// compiled with no matching declaration), the hit is recorded under the
// chunk name Lua reports with the no-generation marker; the report measures
// such a chunk only when it is not under packs/ — a packs/ chunk executing
// with no registered prototype stays the report's hard error.
void record_hook_line(lua_State* L, lua_Debug* ar);

// A pack registered the closure at stack slot `index` as a hook under
// `label`, e.g. "living/core:soldier/do_special". PURELY DIAGNOSTIC: it
// names the function in the report. Registration is not coverage — the
// denominator comes from the static prototype walk and the numerator from
// record_hook_line above. The closure's prototype resolves through the same
// registry, so the label (and the zero-hit record this creates for a
// registered-but-never-entered hook) lands on the generation that DEFINED
// the function, not on whichever generation was declared last.
void declare_function_closure(lua_State* L, int index, std::string_view label);

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
// Declaring is also what makes a later compile of these bytes BINDABLE:
// bind_compiled_chunk() consults the declared set and registers a compiled
// closure's prototypes only against a (chunk, digest) pair declared here. A
// declaration never rebinds anything retroactively — prototypes compiled
// from OTHER bytes keep crediting their own generation (see the recording
// sites above). Re-declaring the same bytes (every level load does) changes
// nothing.
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
    // sha256 of the generation whose compiled prototype recorded the hit;
    // empty when the executing code was compiled from bytes no declaration
    // covers (test Lua compiled from a string literal — a mounted pack
    // script is always declared before the engine can compile it). A hit
    // belongs to exactly ONE (chunk, digest); the report never has to guess
    // which generation executed.
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
// `path`. Returns false if the file could not be written. ATOMIC: the dump
// is streamed to a temp sibling and rename()d over `path`, exactly like its
// own sidecars, so a reader — or a SIGKILL — at any instant sees either the
// previous complete dump or the new complete dump, never a torn prefix that
// the report would read as tampering.
bool write_raw_report(const std::string& path);

// Write one uniquely-named dump into output_dir(). No-op when the recorder
// is off or no output directory was configured. Called automatically at
// process exit — but a harness that ends with _exit() (tests/integration_main
// .cpp does, which is why it also calls __gcov_dump by hand) skips static
// destructors and must call this itself. Repeated calls target the SAME
// file, and each call publishes atomically (see write_raw_report), so an
// explicit flush followed by the exit-time one can neither double-count nor
// re-truncate a complete dump into a torn one.
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
