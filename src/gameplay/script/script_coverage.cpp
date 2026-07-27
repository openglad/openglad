/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/script/script_coverage.h>

// This TU is deliberately dependency-free apart from Lua: it is compiled
// both into og_gameplay (the runtime recorder) and standalone into the
// og_lua_lines helper the report tool shells out to, and the helper must not
// drag in — or gcov-instrument — the rest of the game.
//
// src/gameplay/script/ is the one directory allowed to include Lua headers
// (scripts/check_vendor_leaks.sh). executable_lines() additionally needs
// Lua's INTERNAL headers: the public debug API can hand back the executable
// lines of one function (debug.getinfo(f, "L")) but gives no way to reach the
// prototypes nested inside it, and a pack file is almost entirely nested
// closures. Walking Proto::p is how Lua itself would answer the question, and
// the vendored copy is pinned (v5.4.8, always fetched, never a system Lua),
// so there is no version skew to guard against.
#include <lua.h>
#include <lauxlib.h>

#include <lobject.h>
#include <ldebug.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <utility>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace og::script::coverage {

namespace detail {
bool g_enabled = false;
}  // namespace detail

namespace {

// --- sha256 ----------------------------------------------------------------
//
// The dump format is content-addressed: a declared source is named, keyed
// and verified by the hash of its own bytes. Written out longhand here
// rather than pulled from OpenSSL because this TU is compiled a second time
// into og_lua_lines, which deliberately links Lua and nothing else so that a
// report tool cannot write .gcda into the build it is measuring.

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

std::uint32_t rotr32(std::uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

void sha256_compress(std::array<std::uint32_t, 8>& state,
                     const unsigned char* block)
{
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; i++) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; i++) {
        const std::uint32_t s0 =
            rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 =
            rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::array<std::uint32_t, 8> v = state;
    for (std::size_t i = 0; i < 64; i++) {
        const std::uint32_t s1 =
            rotr32(v[4], 6) ^ rotr32(v[4], 11) ^ rotr32(v[4], 25);
        const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t t1 = v[7] + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 =
            rotr32(v[0], 2) ^ rotr32(v[0], 13) ^ rotr32(v[0], 22);
        const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const std::uint32_t t2 = s0 + maj;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (std::size_t i = 0; i < 8; i++)
        state[i] += v[i];
}

// Basename of the running executable. Recorded in every dump so the report
// can check WHICH recorder processes contributed rather than only how much
// they found: losing two thirds of the suite's processes otherwise reads as
// a slightly lower percentage and is absorbed by the slack above the bar.
// Empty where the platform offers no answer — the report treats that as a
// hard failure rather than skipping the check.
std::string current_program_name()
{
#if defined(__linux__)
    std::error_code ec;
    const std::filesystem::path exe =
        std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : exe.filename().string();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size + 1, '\0');
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    return std::filesystem::path(buffer.c_str()).filename().string();
#else
    return {};
#endif
}

struct FnEntry {
    std::string label;  // smallest registered label (stable across runs)
    std::uint64_t body_hits = 0;  // line events inside this prototype
};

// A prototype's identity: the chunk it was compiled from and the SPAN of
// source lines it occupies. The span, not `linedefined` alone — two
// prototypes can start on the same line, and merging them let one cover the
// other. See the header.
using FnKey = std::tuple<std::string, int, int>;

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

struct Recorder {
    std::mutex mu;
    // chunk → line → hit count.
    std::map<std::string, std::map<int, std::uint64_t>> lines;
    // (chunk, line_defined, last_line_defined) → per-function record.
    std::map<FnKey, FnEntry> functions;
    // (chunk, sha256(source)) → (source, real directory/archive), for every
    // script the engine loaded.
    std::map<SourceKey, SourceEntry> sources;
    // Chosen on the first flush and reused after, so flushing twice (an
    // explicit pre-_exit flush plus the exit-time one) rewrites one file
    // instead of double-counting the same hits across two.
    std::string dump_path;

    ~Recorder();
};

// Resolved once at static-init time; see the header's determinism contract.
std::string read_output_dir()
{
    const char* dir = std::getenv("OPENGLAD_LUA_COVERAGE");
    if (dir == nullptr || dir[0] == '\0') return {};
    return std::string(dir);
}

// DELIBERATELY leaked — never destroyed. The Recorder flushes from its own
// destructor and needs the path then; a plain function-local static would be
// destroyed first whenever it happened to be constructed later (it is: the
// recorder starts recording long before anything asks where to write), and
// the flush would read a dead string and silently write nothing.
std::string& output_dir_storage()
{
    static std::string* dir = new std::string(read_output_dir());
    return *dir;
}

Recorder& recorder()
{
    static Recorder r;
    return r;
}

// One dump file per process. The name only has to be distinct across the test
// binaries a run launches — the report merges whatever it finds — so a 64-bit
// mix of the monotonic clock and an ASLR'd address does the job without a pid
// (pids get reused between sequential ctest entries) and without a
// probe-and-retry loop whose fallback nothing could ever exercise.
std::string unique_dump_path(const std::string& dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    std::uint64_t token =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    token ^= static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&output_dir_storage));
    char name[64];
    std::snprintf(name, sizeof(name), "lua-%016llx.luacov",
                  static_cast<unsigned long long>(token));
    return (std::filesystem::path(dir) / std::string(name)).string();
}

Recorder::~Recorder()
{
    flush_to_output_dir();
}

// Static initializer: arms the recorder before main() and therefore before
// any Lua can run. Ordering is safe because nothing else in the program runs
// script code during static initialization.
[[maybe_unused]] const bool g_armed =
    (detail::g_enabled = !read_output_dir().empty());

}  // namespace

std::string sha256_hex(std::string_view data)
{
    std::array<std::uint32_t, 8> state = {0x6a09e667u, 0xbb67ae85u,
                                          0x3c6ef372u, 0xa54ff53au,
                                          0x510e527fu, 0x9b05688cu,
                                          0x1f83d9abu, 0x5be0cd19u};
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const std::size_t total = data.size();
    std::size_t offset = 0;
    for (; offset + 64 <= total; offset += 64)
        sha256_compress(state, bytes + offset);

    // Tail: the remainder, the 0x80 terminator, zero padding, and the length
    // in bits as a big-endian 64-bit value. Two blocks when the remainder
    // leaves no room for the length field.
    std::array<unsigned char, 128> tail{};
    const std::size_t rest = total - offset;
    std::memcpy(tail.data(), bytes + offset, rest);
    tail[rest] = 0x80;
    const std::size_t tail_len = (rest < 56) ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(total) * 8u;
    for (std::size_t i = 0; i < 8; i++) {
        tail[tail_len - 1 - i] =
            static_cast<unsigned char>((bits >> (8 * i)) & 0xffu);
    }
    for (std::size_t i = 0; i < tail_len; i += 64)
        sha256_compress(state, tail.data() + i);

    std::string hex;
    hex.reserve(64);
    for (const std::uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4)
            hex.push_back("0123456789abcdef"[(word >> shift) & 0xfu]);
    }
    return hex;
}

std::string program_name()
{
    // Resolved once: the answer cannot change, and a dump is written from a
    // destructor where doing less work is better.
    static const std::string name = current_program_name();
    return name;
}

const std::string& output_dir()
{
    return output_dir_storage();
}

void set_enabled_for_testing(bool on)
{
    detail::g_enabled = on;
}

void set_output_dir_for_testing(std::string dir)
{
    output_dir_storage() = std::move(dir);
    // Forget the memoized dump path with the directory that produced it.
    // Without this a test that redirects the output somewhere temporary
    // permanently poisons the process: the exit-time flush would keep
    // rewriting the old (by then deleted) path and the binary's whole Lua
    // contribution would vanish from the run with no diagnostic at all.
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    r.dump_path.clear();
}

void flush_to_output_dir()
{
    if (!detail::g_enabled) return;
    const std::string& dir = output_dir_storage();
    if (dir.empty()) return;
    Recorder& r = recorder();
    if (r.dump_path.empty())
        r.dump_path = unique_dump_path(dir);
    write_raw_report(r.dump_path);
}

void reset()
{
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    r.lines.clear();
    r.functions.clear();
    r.sources.clear();
}

void record_line(std::string_view chunk, int line)
{
    if (!detail::g_enabled || line <= 0 || chunk.empty())
        return;
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    r.lines[std::string(chunk)][line]++;
}

void declare_function(std::string_view chunk, int line_defined,
                      int last_line_defined, std::string_view label)
{
    if (!detail::g_enabled || chunk.empty()) return;
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    FnEntry& e =
        r.functions[FnKey{std::string(chunk), line_defined,
                          last_line_defined}];
    std::string incoming(label);
    // One Lua function registered under two names (the same closure used for
    // two hooks) would otherwise get a run-order-dependent label. Smallest
    // wins so every process and every merge agrees.
    if (e.label.empty() || incoming < e.label)
        e.label = std::move(incoming);
}

void record_function_line(std::string_view chunk, int line_defined,
                          int last_line_defined)
{
    if (!detail::g_enabled || chunk.empty() || line_defined < 0)
        return;
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    r.functions[FnKey{std::string(chunk), line_defined, last_line_defined}]
        .body_hits++;
}

void declare_pack_source(std::string_view chunk, std::string_view source,
                         std::string_view origin)
{
    if (!detail::g_enabled || chunk.empty())
        return;
    // Keyed by (chunk, digest), so re-declaring a name with DIFFERENT bytes
    // adds a record instead of erasing the previous one. Re-declaring the
    // same bytes — every level load re-registers the same pack scripts —
    // still collapses onto one entry.
    SourceKey key{std::string(chunk), sha256_hex(source)};
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    r.sources[std::move(key)] = {std::string(source), std::string(origin)};
}

std::vector<LineHit> line_hits()
{
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    std::vector<LineHit> out;
    for (const auto& [chunk, per_line] : r.lines)
        for (const auto& [line, count] : per_line)
            out.push_back({chunk, line, count});
    return out;
}

std::vector<FunctionRecord> function_records()
{
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    std::vector<FunctionRecord> out;
    for (const auto& [key, entry] : r.functions) {
        out.push_back({std::get<0>(key), std::get<1>(key), std::get<2>(key),
                       entry.label, entry.body_hits});
    }
    return out;
}

std::vector<PackSourceRecord> pack_sources()
{
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    std::vector<PackSourceRecord> out;
    for (const auto& [key, entry] : r.sources)
        out.push_back({key.first, entry.source, entry.origin, key.second});
    return out;
}

struct ScopedRecording::State {
    std::map<std::string, std::map<int, std::uint64_t>> lines;
    std::map<FnKey, FnEntry> functions;
    std::map<SourceKey, SourceEntry> sources;
};

ScopedRecording::ScopedRecording()
    : saved_(std::make_unique<State>()), previously_enabled_(detail::g_enabled)
{
    {
        Recorder& r = recorder();
        const std::lock_guard<std::mutex> lock(r.mu);
        saved_->lines = std::move(r.lines);
        saved_->functions = std::move(r.functions);
        saved_->sources = std::move(r.sources);
    }
    reset();  // the moved-from maps are unspecified; make them empty
    detail::g_enabled = true;
}

ScopedRecording::~ScopedRecording()
{
    detail::g_enabled = previously_enabled_;
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    r.lines = std::move(saved_->lines);
    r.functions = std::move(saved_->functions);
    r.sources = std::move(saved_->sources);
}

namespace {

// Sidecar file name for a declared source: CONTENT-ADDRESSED, with a
// human-readable stem in front of it.
//
// The stem is squashed out of the chunk name because a chunk name is a
// virtual path ("packs/x/scripts/y.lua") and cannot be a file name as-is;
// it exists only so the directory is readable. THE DIGEST IS THE NAME. When
// the name came from a hash of the CHUNK NAME instead, two processes that
// compiled different bytes under one chunk name wrote to one path, one
// overwrote the other, and which one survived came down to rename() order —
// so the same tree flipped between "hits scored against the wrong source"
// and a hard "not repository content" failure from run to run. Two distinct
// sources can no longer collide, and identical sources from any number of
// processes still write identical bytes to one path.
//
// Every character is from [A-Za-z0-9._-] so the reader can validate the
// field with a character-class check instead of trusting a path from a file.
std::string source_file_name(const std::string& chunk, const std::string& digest)
{
    std::string stem;
    stem.reserve(chunk.size());
    for (const char c : chunk) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '-';
        stem.push_back(safe ? c : '_');
    }
    if (stem.size() > 60)
        stem.erase(0, stem.size() - 60);
    return stem + "-" + digest + ".lua";
}

// Publish one declared source next to the dump. Written through a temp file
// and renamed so a reader never sees a half-written script and so two test
// processes flushing at once cannot interleave: they write identical bytes,
// and rename() picks a winner atomically.
bool write_source_file(const std::filesystem::path& dir,
                       const std::string& name, const std::string& body)
{
    const std::filesystem::path final_path = dir / name;
    std::filesystem::path tmp_path = final_path;
    char token[32];
    std::snprintf(token, sizeof(token), ".tmp-%llx",
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(&body) ^
                      static_cast<std::uintptr_t>(
                          std::chrono::steady_clock::now()
                              .time_since_epoch()
                              .count())));
    tmp_path += token;
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out.flush();
        if (!out)
            return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

}  // namespace

bool write_raw_report(const std::string& path)
{
    // Snapshot under the lock, write outside it.
    const std::vector<LineHit> hits = line_hits();
    const std::vector<FunctionRecord> fns = function_records();
    const std::vector<PackSourceRecord> srcs = pack_sources();

    // Declared sources are the inventory half of the denominator: they let
    // the report see a script that exists only inside a .glad or only as a
    // C++ string literal. They go beside the dump, one file each.
    const std::filesystem::path dump_path(path);
    const std::filesystem::path source_dir = dump_path.parent_path() / "sources";
    std::vector<const PackSourceRecord*> declared;
    std::vector<std::string> declared_files;
    if (!srcs.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(source_dir, ec);
        for (const PackSourceRecord& s : srcs) {
            const std::string name = source_file_name(s.chunk, s.digest);
            if (!write_source_file(source_dir, name, s.source))
                return false;
            declared.push_back(&s);
            declared_files.push_back(name);
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << "# openglad-lua-coverage 4\n";
    // Which process wrote this. The report checks the POPULATION of recorder
    // processes against a committed manifest, because a suite that quietly
    // lost most of its processes looks exactly like slightly lower coverage.
    out << "P\t" << program_name() << '\n';
    // The sidecar field is a bare file name, never a path: the reader joins
    // it under <dump dir>/sources/ itself. It used to be a path joined onto
    // the dump directory unvalidated, and pathlib's "/" DISCARDS the base
    // when the right-hand side is absolute — so a dump could name any file
    // on the machine as the source its hits should be scored against.
    for (std::size_t i = 0; i < declared.size(); i++)
        out << "S\t" << declared[i]->chunk << '\t' << declared_files[i] << '\t'
            << declared[i]->digest << '\t' << declared[i]->origin << '\n';
    for (const LineHit& h : hits)
        out << "L\t" << h.chunk << '\t' << h.line << '\t' << h.count << '\n';
    for (const FunctionRecord& f : fns)
        out << "F\t" << f.chunk << '\t' << f.line_defined << '\t'
            << f.last_line_defined << '\t' << f.body_hits << '\t' << f.label
            << '\n';
    out.flush();
    return static_cast<bool>(out);
}

// ---------------------------------------------------------------------------
// Static analysis: executable lines
// ---------------------------------------------------------------------------

namespace {

// Mirrors Lua's own collectvalidlines() (ldblib.c), recursed into nested
// prototypes, and collecting each prototype's (linedefined, lastlinedefined)
// SPAN on the way past. Instruction 0 of a vararg function is OP_VARARGPREP,
// which carries the function's declaration line but no user code; Lua skips
// it when answering debug.getinfo(f, "L") and so do we.
//
// EVERY prototype counts as a function — hook, local helper, anonymous
// callback alike — and the main chunk (span 0..0) counts too, so a shipped
// script that nothing loads is a whole file of function misses rather than
// an absence. The span is the identity because `linedefined` alone is not
// unique: `local a, b = f(function() end), f(function()\n...\nend)` puts two
// prototypes on one start line, and the runtime hook that reports only that
// line would mark both covered when either ran.
void collect_protos(const Proto* p, std::vector<int>& lines,
                    std::vector<FunctionSpan>& functions)
{
    if (p == nullptr)
        return;
    functions.push_back({p->linedefined, p->lastlinedefined});
    int i = (p->is_vararg != 0) ? 1 : 0;
    for (; i < p->sizelineinfo; i++) {
        const int line = luaG_getfuncline(p, i);
        if (line > 0)
            lines.push_back(line);
    }
    for (int j = 0; j < p->sizep; j++)
        collect_protos(p->p[j], lines, functions);
}

template <typename T>
void sort_unique(std::vector<T>& v)
{
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

}  // namespace

SourceFacts source_facts(std::string_view source, std::string_view chunk_name)
{
    SourceFacts result;
    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        result.error = "out of memory";
        return result;
    }
    const std::string name(chunk_name);
    if (luaL_loadbuffer(L, source.data(), source.size(), name.c_str()) !=
        LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        result.error = (msg != nullptr) ? msg : "compile error";
        lua_close(L);
        return result;
    }
    // luaL_loadbuffer leaves the main-chunk Lua closure on the stack;
    // lua_topointer hands back the collectable object itself for a function,
    // which for a Lua closure is the LClosure carrying the Proto tree.
    const auto* cl = static_cast<const LClosure*>(lua_topointer(L, -1));
    if (cl != nullptr)
        collect_protos(cl->p, result.lines, result.functions);
    lua_close(L);

    sort_unique(result.lines);
    sort_unique(result.functions);
    result.ok = true;
    return result;
}

}  // namespace og::script::coverage
