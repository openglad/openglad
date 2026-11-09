#include "script_coverage.h"
#include "../../src/gameplay/script/script_coverage_state.h"

namespace og::script::coverage {

using namespace detail;

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

void reset()
{
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mu);
    ++r.measurement_epoch;
    r.chunks.clear();
    r.sources.clear();
}

struct ScopedRecording::State {
    std::map<std::string, ChunkState> chunks;
    std::map<SourceKey, SourceEntry> sources;
};

ScopedRecording::ScopedRecording()
    : saved_(std::make_unique<State>()), previously_enabled_(detail::g_enabled)
{
    {
        Recorder& r = recorder();
        const std::lock_guard<std::mutex> lock(r.mu);
        // Hits and declarations travel together; the Proto registry stays
        // put on purpose — it is a fact about live VM objects, not recorded
        // measurement (see the Recorder declaration).
        ++r.measurement_epoch;
        saved_->chunks = std::move(r.chunks);
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
    ++r.measurement_epoch;
    r.chunks = std::move(saved_->chunks);
    r.sources = std::move(saved_->sources);
}

}  // namespace og::script::coverage
