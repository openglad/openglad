#pragma once

// Where an in-world sound comes from, packed into the PlaySound event's third
// scalar (Event::c) so each machine can play it only while one of its own
// views shows the source (#468). The simulation cannot ask that itself: the
// server is headless and every machine has its own views.
//
// The packing: x in bits 20-31, y in bits 8-19 (each clamped to 0..4095; a
// grid is at most 255 tiles of 16 px, 4080 px) and the larger side of the
// source's box in bits 0-7 (clamped to 1..255, a sprite's side limit). The box
// becomes a square of that side: never stingier than the old game's test, at
// most |sizex - sizey| px more generous on the narrow axis. A packed source is
// never 0, so 0 stays "unplaced": a cue every machine plays.

#include <algorithm>
#include <cstdint>
#include <optional>

namespace og::sim {

inline constexpr std::int32_t kSoundSourceCoordMax = 4095;
inline constexpr std::int32_t kSoundSourceSpanMax = 255;

struct SoundSource final {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t span = 0;

    bool operator==(const SoundSource&) const = default;
};

[[nodiscard]] constexpr std::uint32_t pack_sound_source(std::int32_t x,
                                                        std::int32_t y,
                                                        std::int32_t sizex,
                                                        std::int32_t sizey) noexcept
{
    const auto px = static_cast<std::uint32_t>(std::clamp(x, 0, kSoundSourceCoordMax));
    const auto py = static_cast<std::uint32_t>(std::clamp(y, 0, kSoundSourceCoordMax));
    const auto span = static_cast<std::uint32_t>(
        std::clamp(std::max(sizex, sizey), 1, kSoundSourceSpanMax));
    return (px << 20) | (py << 8) | span;
}

[[nodiscard]] constexpr std::optional<SoundSource> unpack_sound_source(std::uint32_t c) noexcept
{
    // An unplaced sound (c == 0) has no box: every machine plays it.
    if (c == 0)
        return std::nullopt;
    return SoundSource{static_cast<std::int32_t>(c >> 20),
                       static_cast<std::int32_t>((c >> 8) & 0xFFFu),
                       static_cast<std::int32_t>(c & 0xFFu)};
}

} // namespace og::sim
