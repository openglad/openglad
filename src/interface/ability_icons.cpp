/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/interface/ability_icons.h>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/our_palette.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

namespace og::ui {

namespace {

constexpr std::size_t kIconPixels =
    static_cast<std::size_t>(kAbilityIconSize) * kAbilityIconSize;

// One 16x16 frame owning a copy of `pixels`.
PixieData one_frame(const unsigned char* pixels)
{
    auto data = std::make_unique<unsigned char[]>(kIconPixels);
    std::copy(pixels, pixels + kIconPixels, data.get());
    return PixieData(1, kAbilityIconSize, kAbilityIconSize, data.release());
}

} // namespace

double icon_palette_luma(int index)
{
    // our_pal_lookup holds 6-bit channels; the loader and pix/openglad.gpl
    // widen them as v * 255 / 63, so the brightest entry (57) is 230.
    const auto channel = [index](int c) {
        return static_cast<double>(
            our_pal_lookup(index * 3 + c) * 255u / 63u);
    };
    return 0.299 * channel(0) + 0.587 * channel(1) + 0.114 * channel(2);
}

const std::array<unsigned char, 256>& locked_icon_lut()
{
    static const std::array<unsigned char, 256> lut = [] {
        std::array<unsigned char, 256> t{};
        for (int i = 1; i < 256; ++i) {
            if (i == 16) {
                t[16] = 16;
                continue;
            }
            const double step =
                std::floor(icon_palette_luma(i) / 230.0 * 10.0 + 0.5);
            t[static_cast<std::size_t>(i)] = static_cast<unsigned char>(
                21 + std::clamp(static_cast<int>(step), 0, 10));
        }
        return t;
    }();
    return lut;
}

const char* declared_icon_path(const FamilyDescriptor& fd,
                               og::sim::belt::Entry entry)
{
    if (entry.slot < 1 || entry.slot >= FD_NUM_SPECIALS)
        return nullptr;
    return entry.alt ? fd.alternate_icons[entry.slot]
                     : fd.special_icons[entry.slot];
}

AbilityIcons::AbilityIcons(std::string generic_path)
    : generic_path_(std::move(generic_path)),
      generation_(og::resources::sprite_source_generation())
{
}

void AbilityIcons::drop_if_stale()
{
    const unsigned now = og::resources::sprite_source_generation();
    if (now == generation_)
        return;
    TRACE("ability_icons", "reload generation=%u was=%u cached=%zu", now,
          generation_, by_path_.size());
    by_path_.clear();
    generation_ = now;
}

const AbilityIcon* AbilityIcons::load(const std::string& path)
{
    auto found = by_path_.find(path);
    if (found == by_path_.end()) {
        ++loads_;
        AbilityIcon icon;
        const PixieData file = read_pixie_file(path.c_str());
        if (file.valid() && file.w == kAbilityIconSize &&
            file.h == kAbilityIconSize) {
            // The first frame only: an icon is one picture.
            const auto& lut = locked_icon_lut();
            unsigned char grey[kIconPixels];
            for (std::size_t i = 0; i < kIconPixels; ++i)
                grey[i] = lut[file.data[i]];
            icon.ready = one_frame(file.data.get());
            icon.locked = one_frame(grey);
            TRACE("ability_icons", "load path=%s ok", path.c_str());
        } else {
            TRACE("ability_icons", "load path=%s refused valid=%d w=%d h=%d",
                  path.c_str(), file.valid() ? 1 : 0,
                  static_cast<int>(file.w), static_cast<int>(file.h));
        }
        found = by_path_.emplace(path, std::move(icon)).first;
    }
    return found->second.ready.valid() ? &found->second : nullptr;
}

const AbilityIcon* AbilityIcons::icon_at(const char* path)
{
    drop_if_stale();
    if (path != nullptr && path[0] != '\0') {
        if (const AbilityIcon* hit = load(path))
            return hit;
    }
    TRACE("ability_icons", "generic for path=%s",
          path != nullptr ? path : "(none)");
    return load(generic_path_);
}

const AbilityIcon* AbilityIcons::icon(const FamilyDescriptor& fd,
                                      og::sim::belt::Entry entry)
{
    return icon_at(declared_icon_path(fd, entry));
}

} // namespace og::ui
