#pragma once

#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/world_snapshot.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

class GameWorld;

namespace og::sim {

inline constexpr std::uint8_t kReplayFormatVersion = 9;
inline constexpr std::size_t kReplayHeaderSize = 32;

enum class ReplayIoError : std::uint8_t {
    None = 0,
    OpenReadFailed,
    OpenWriteFailed,
    InvalidHeader,
    UnsupportedVersion,
    MalformedData,
};

struct ReplayHeader {
    std::uint8_t version = kReplayFormatVersion;
    std::uint32_t initial_rng_state = 0;
    std::int32_t level_id = 0;
    std::uint8_t player_count = 0;
    std::int8_t timer_wait = 0;
    std::int16_t my_team = 0;
    std::int16_t allied_mode = 0;
    std::int16_t difficulty = 100;
    std::string campaign_id;
};

struct ReplayData {
    ReplayHeader header = {};
    WorldSnapshot initial_snapshot;
    std::vector<InputStateMessage> frames;
};

enum class ReplayCheckpointKind : std::uint8_t {
    Snapshot = 0,
    Keyframe = 1,
};

struct ReplayCheckpoint {
    std::uint32_t tick = 0;
    ReplayCheckpointKind kind = ReplayCheckpointKind::Keyframe;
    WorldSnapshot snapshot;
};

struct ReplayVerificationFailure {
    std::uint32_t tick = 0;
    std::string field;
    std::string expected_value;
    std::string actual_value;
};

std::vector<std::uint8_t> serialize_replay(const ReplayHeader& header,
                                           const WorldSnapshot& initial_snapshot,
                                           std::span<const InputStateMessage> frames);

std::optional<ReplayData> deserialize_replay(std::span<const std::uint8_t> bytes,
                                             ReplayIoError* out_error = nullptr);

std::optional<ReplayVerificationFailure>
find_first_snapshot_difference(std::uint32_t tick,
                               const WorldSnapshot& expected,
                               const WorldSnapshot& actual,
                               bool compare_dirty_masks = true);

class ReplayRecorder final {
public:
    ReplayRecorder() = default;
    explicit ReplayRecorder(const ReplayHeader& header)
        : header_(header)
    {
    }

    void clear() noexcept;
    void set_header(const ReplayHeader& header) noexcept { header_ = header; }

    const ReplayHeader& header() const noexcept { return header_; }
    const WorldSnapshot& initial_snapshot() const noexcept { return initial_snapshot_; }
    const std::vector<InputStateMessage>& frames() const noexcept { return frames_; }
    const std::vector<ReplayCheckpoint>& checkpoints() const noexcept { return checkpoints_; }
    std::size_t frame_count() const noexcept { return frames_.size(); }
    bool has_initial_snapshot() const noexcept { return has_initial_snapshot_; }

    void record_input(std::uint32_t tick, const InputState& input);
    void set_initial_snapshot(const WorldSnapshot& snapshot);
    void record_initial_world(GameWorld& world);
    void record_snapshot(std::uint32_t tick,
                         const WorldSnapshot& snapshot,
                         ReplayCheckpointKind kind = ReplayCheckpointKind::Snapshot);
    void record_world_snapshot(std::uint32_t tick, GameWorld& world);
    void record_world_keyframe(std::uint32_t tick, GameWorld& world);

    std::vector<std::uint8_t> serialize() const;
    bool write_file(const std::filesystem::path& path,
                    ReplayIoError* out_error = nullptr) const;

private:
    ReplayHeader header_ = {};
    WorldSnapshot initial_snapshot_ = {};
    bool has_initial_snapshot_ = false;
    std::vector<InputStateMessage> frames_;
    std::vector<ReplayCheckpoint> checkpoints_;
};

class ReplayPlayer final {
public:
    ReplayPlayer() = default;

    void clear() noexcept;
    bool load_bytes(std::span<const std::uint8_t> bytes,
                    ReplayIoError* out_error = nullptr);
    bool load_file(const std::filesystem::path& path,
                   ReplayIoError* out_error = nullptr);

    void reset() noexcept;
    void set_checkpoints(std::vector<ReplayCheckpoint> checkpoints);

    const ReplayHeader& header() const noexcept { return data_.header; }
    const WorldSnapshot& initial_snapshot() const noexcept { return data_.initial_snapshot; }
    const std::vector<InputStateMessage>& frames() const noexcept { return data_.frames; }
    const std::vector<ReplayCheckpoint>& checkpoints() const noexcept { return checkpoints_; }
    std::size_t frame_count() const noexcept { return data_.frames.size(); }
    std::size_t next_frame_index() const noexcept { return next_frame_index_; }
    bool has_next_frame() const noexcept { return next_frame_index_ < data_.frames.size(); }
    const std::optional<ReplayVerificationFailure>& first_divergence() const noexcept
    {
        return first_divergence_;
    }

    std::optional<InputStateMessage> next_frame();
    std::optional<ReplayVerificationFailure> verify_world(GameWorld& world,
                                                          std::uint32_t tick,
                                                          bool compare_dirty_masks = true);

private:
    ReplayData data_ = {};
    std::size_t next_frame_index_ = 0;
    std::vector<ReplayCheckpoint> checkpoints_;
    std::size_t next_checkpoint_index_ = 0;
    std::optional<ReplayVerificationFailure> first_divergence_;
};

} // namespace og::sim
