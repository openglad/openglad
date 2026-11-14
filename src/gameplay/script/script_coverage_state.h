#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace og::script::coverage::detail {

struct FnEntry {
    std::string label;  // smallest registered label (stable across runs)
    std::uint64_t body_hits = 0;  // line events inside this prototype
};

// A prototype's identity WITHIN one generation of one chunk: the SPAN of
// source lines it occupies. The span, not `linedefined` alone — two
// prototypes can start on the same line, and merging them let one cover the
// other. See the header.
using SpanKey = std::pair<int, int>;  // (line_defined, last_line_defined)

// A declared source's identity: the chunk it was loaded under AND the
// sha256 of its bytes. The chunk name alone is NOT unique — one process can
// mount two different sources under one name (a regenerated pack cache does
// exactly that) — and while the name alone was the key the second
// declaration erased the first, which silently moved one source's recorded
// hits onto another source's grid. See the header.
using SourceKey = std::pair<std::string, std::string>;  // (chunk, digest)

struct SourceEntry {
    std::string source;
    std::string origin;
};

// Hits recorded against ONE generation of one chunk. Keyed by the digest of
// the source whose compiled prototype executed, never pooled per chunk:
// pooling is what forced the report to guess which declared generation a hit
// belonged to, and its guess — every generation whose grid contained the
// line — let execution of one file's bytes credit a different file's.
struct GenerationHits {
    std::map<int, std::uint64_t> lines;   // line → hit count
    std::map<SpanKey, FnEntry> functions; // prototype span → record
};

struct ChunkState {
    // digest ("" = the executing code was compiled from bytes no declaration
    // covers) → hits under that generation.
    std::map<std::string, GenerationHits> generations;
};

struct ProtoBinding {
    std::shared_ptr<const SourceKey> source;
    std::uint64_t measurement_epoch = 0;
    GenerationHits* generation = nullptr;
    FnEntry* function = nullptr;
};

struct Recorder {
    std::mutex mu;
    // chunk → per-generation hit grids.
    std::map<std::string, ChunkState> chunks;
    // (chunk, sha256(source)) → (source, real directory/archive), for every
    // script the engine loaded.
    std::map<SourceKey, SourceEntry> sources;
    // THE GENERATION BINDING (see the header): compiled prototype → the
    // (chunk, digest) it was compiled from, written by bind_compiled_chunk
    // at compile time and read by record_hook_line at execution time. One
    // shared key object per compile, since a chunk's whole tree binds to one
    // generation. Entries are overwritten or erased on address reuse — never
    // proactively unregistered — which is sound because a Proto alive enough
    // to execute is alive enough to still own its entry (the full argument
    // sits on bind_compiled_chunk's declaration). Maintained on EVERY
    // compile, recorder on or off — the off path only erases — so the map
    // cannot go stale across an enable-state transition.
    //
    // Deliberately NOT part of ScopedRecording's swapped state and NOT
    // touched by reset(): it is a fact about live VM objects ("this
    // prototype was compiled from these bytes"), not recorded measurement,
    // and a closure compiled before a scope keeps executing inside it.
    std::map<const void*, ProtoBinding> protos;
    // Cached map nodes remain stable during recording. Test scopes clear or
    // swap the measurement maps, so they advance this epoch under the mutex.
    std::uint64_t measurement_epoch = 1;
    // Chosen on the first flush and reused after, so flushing twice (an
    // explicit pre-_exit flush plus the exit-time one) atomically republishes
    // one file instead of double-counting the same hits across two.
    std::string dump_path;

    ~Recorder();
};

Recorder& recorder();
std::string& output_dir_storage();

}  // namespace og::script::coverage::detail
