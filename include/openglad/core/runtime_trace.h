#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace og::runtime {

inline constexpr std::uint32_t kRuntimeTraceContractVersion = 1u;
inline constexpr std::uint32_t kRuntimeRenderSampleContractVersion = 1u;

struct RuntimeRenderSample
{
    std::uint32_t contract_version = kRuntimeRenderSampleContractVersion;
    std::uint64_t render_sample_seq = 0u;
    std::uint64_t engine_time_ms = 0u;
    std::int32_t view_index = 0;
    std::uint32_t tick = 0u;
    std::int32_t timer_wait = 0;
    float speed_factor = 1.0f;
    float interpolation_alpha = 1.0f;
    float control_worldx = 0.0f;
    float control_worldy = 0.0f;
    float control_render_x = 0.0f;
    float control_render_y = 0.0f;
    std::int32_t camera_topx = 0;
    std::int32_t camera_topy = 0;
    float camera_topx_float = 0.0f;
    float camera_topy_float = 0.0f;
};

struct RuntimeTraceRecord
{
    std::uint64_t trace_seq = 0u;
    std::uint64_t engine_time_ms = 0u;
    double browser_time_ms = 0.0;
    std::string category;
    std::string event;
    std::uint32_t tick = 0u;
    std::string snapshot_kind;
    float interpolation_alpha = 1.0f;
    float control_worldx = 0.0f;
    float control_worldy = 0.0f;
    float control_render_x = 0.0f;
    float control_render_y = 0.0f;
    std::int32_t camera_topx = 0;
    std::int32_t camera_topy = 0;
    float camera_topx_float = 0.0f;
    float camera_topy_float = 0.0f;
};

using RuntimeTraceObserver = std::function<void(const RuntimeTraceRecord&)>;
using RuntimeRenderSampleObserver =
    std::function<void(const RuntimeRenderSample&)>;

[[nodiscard]] std::uint64_t runtime_trace_now_ms();
void set_runtime_trace_enabled(bool enabled);
[[nodiscard]] bool runtime_trace_enabled();
void clear_runtime_trace();
void reset_runtime_trace_capture_state();
[[nodiscard]] RuntimeTraceRecord make_runtime_trace_record(
    std::string_view category,
    std::string_view event);
void emit_runtime_trace(RuntimeTraceRecord record);
[[nodiscard]] std::vector<RuntimeTraceRecord> drain_runtime_trace();
[[nodiscard]] std::vector<RuntimeTraceRecord> copy_runtime_trace();
void set_runtime_trace_observer(RuntimeTraceObserver observer);
void clear_runtime_trace_observer();
void publish_runtime_render_sample(RuntimeRenderSample sample);
[[nodiscard]] std::optional<RuntimeRenderSample> latest_runtime_render_sample();
void set_runtime_render_sample_observer(RuntimeRenderSampleObserver observer);
void clear_runtime_render_sample_observer();

} // namespace og::runtime
