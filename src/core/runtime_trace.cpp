#include <openglad/core/runtime_trace.h>

#include <chrono>
#include <mutex>
#include <utility>

namespace og::runtime {

namespace {

using TraceClock = std::chrono::steady_clock;

struct RuntimeTraceState
{
    bool enabled = false;
    std::vector<RuntimeTraceRecord> buffer;
    std::uint64_t next_trace_seq = 1u;
    std::uint64_t next_render_sample_seq = 1u;
    std::optional<RuntimeRenderSample> latest_render_sample;
    RuntimeTraceObserver trace_observer;
    RuntimeRenderSampleObserver render_sample_observer;
};

RuntimeTraceState& runtime_trace_state()
{
    static RuntimeTraceState state;
    return state;
}

std::mutex& runtime_trace_mutex()
{
    static std::mutex mutex;
    return mutex;
}

const TraceClock::time_point& runtime_trace_epoch()
{
    static const TraceClock::time_point epoch = TraceClock::now();
    return epoch;
}

void copy_latest_render_state(const RuntimeRenderSample& sample,
                              RuntimeTraceRecord& record)
{
    record.tick = sample.tick;
    record.interpolation_alpha = sample.interpolation_alpha;
    record.control_worldx = sample.control_worldx;
    record.control_worldy = sample.control_worldy;
    record.control_render_x = sample.control_render_x;
    record.control_render_y = sample.control_render_y;
    record.camera_topx = sample.camera_topx;
    record.camera_topy = sample.camera_topy;
    record.camera_topx_float = sample.camera_topx_float;
    record.camera_topy_float = sample.camera_topy_float;
}

} // namespace

std::uint64_t runtime_trace_now_ms()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            TraceClock::now() - runtime_trace_epoch())
            .count());
}

void set_runtime_trace_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    runtime_trace_state().enabled = enabled;
}

bool runtime_trace_enabled()
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    return runtime_trace_state().enabled;
}

void clear_runtime_trace()
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    runtime_trace_state().buffer.clear();
}

void reset_runtime_trace_capture_state()
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    RuntimeTraceState& state = runtime_trace_state();
    state.buffer.clear();
    state.next_trace_seq = 1u;
    state.next_render_sample_seq = 1u;
    state.latest_render_sample.reset();
}

RuntimeTraceRecord make_runtime_trace_record(std::string_view category,
                                             std::string_view event)
{
    RuntimeTraceRecord record;
    record.category = std::string(category);
    record.event = std::string(event);
    record.engine_time_ms = runtime_trace_now_ms();

    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    if (runtime_trace_state().latest_render_sample.has_value())
    {
        copy_latest_render_state(*runtime_trace_state().latest_render_sample,
                                 record);
    }
    return record;
}

void emit_runtime_trace(RuntimeTraceRecord record)
{
    RuntimeTraceObserver observer;
    {
        std::lock_guard<std::mutex> lock(runtime_trace_mutex());
        RuntimeTraceState& state = runtime_trace_state();
        if (!state.enabled)
            return;

        if (record.trace_seq == 0u)
            record.trace_seq = state.next_trace_seq++;
        if (record.engine_time_ms == 0u)
            record.engine_time_ms = runtime_trace_now_ms();
        if (record.tick == 0u && state.latest_render_sample.has_value())
            copy_latest_render_state(*state.latest_render_sample, record);

        state.buffer.push_back(record);
        observer = state.trace_observer;
    }

    if (observer)
        observer(record);
}

std::vector<RuntimeTraceRecord> drain_runtime_trace()
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    std::vector<RuntimeTraceRecord> drained;
    drained.swap(runtime_trace_state().buffer);
    return drained;
}

std::vector<RuntimeTraceRecord> copy_runtime_trace()
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    return runtime_trace_state().buffer;
}

void set_runtime_trace_observer(RuntimeTraceObserver observer)
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    runtime_trace_state().trace_observer = std::move(observer);
}

void clear_runtime_trace_observer()
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    runtime_trace_state().trace_observer = {};
}

void publish_runtime_render_sample(RuntimeRenderSample sample)
{
    RuntimeRenderSampleObserver observer;
    {
        std::lock_guard<std::mutex> lock(runtime_trace_mutex());
        RuntimeTraceState& state = runtime_trace_state();
        sample.contract_version = kRuntimeRenderSampleContractVersion;
        sample.render_sample_seq = state.next_render_sample_seq++;
        if (sample.engine_time_ms == 0u)
            sample.engine_time_ms = runtime_trace_now_ms();
        state.latest_render_sample = sample;
        observer = state.render_sample_observer;
    }

    if (observer)
        observer(sample);
}

std::optional<RuntimeRenderSample> latest_runtime_render_sample()
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    return runtime_trace_state().latest_render_sample;
}

void set_runtime_render_sample_observer(RuntimeRenderSampleObserver observer)
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    runtime_trace_state().render_sample_observer = std::move(observer);
}

void clear_runtime_render_sample_observer()
{
    std::lock_guard<std::mutex> lock(runtime_trace_mutex());
    runtime_trace_state().render_sample_observer = {};
}

} // namespace og::runtime
