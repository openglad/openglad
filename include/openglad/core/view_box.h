#pragma once

#include <cstdint>

namespace og::core {

// Whether a box at world pixel (x, y), w by h pixels, is on a view whose
// top-left world pixel is (topx, topy) and which shows xview by yview pixels.
// This is the old game's on-screen test (pixie::on_screen, src/pixie.cpp at
// 7ff1654a): the box is off only when it lies wholly to one side of the view,
// so a box touching an edge counts as on. The old game asked it before every
// in-world sound; the client asks it of a placed sound's box (#468) and the
// parity recorder of the sound's walker.
[[nodiscard]] constexpr bool box_on_view(std::int32_t x, std::int32_t y,
                                         std::int32_t w, std::int32_t h,
                                         std::int32_t topx, std::int32_t topy,
                                         std::int32_t xview,
                                         std::int32_t yview) noexcept
{
    return !(x + w < topx || x > topx + xview ||
             y + h < topy || y > topy + yview);
}

} // namespace og::core
