/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// New Ability Select: the ability icons the HUD strip and the ABILITIES
// screen draw (docs/ability-select-design.md, "Icons").
//
// Every ability names a 16x16 indexed picture with `icon = "..."` on its
// specials row (and inside its `alternate` table). The path reaches the
// descriptor as special_icons[] / alternate_icons[] and goes to
// read_pixie_file unchanged: "icons/teleport.png" is core art under pix/,
// "packs/<id>/sprites/<name>.png" is a pack's own file.
//
// AbilityIcons loads each path once, on first use, and keeps its own copy of
// the pixels plus a grey "locked" copy made through locked_icon_lut(). When
// the sprite sources change (a campaign mount, a sprite-sheet swap;
// og::resources::sprite_source_generation()) the cache is dropped and the
// files are read again. An ability with no icon, or whose file does not load
// as one 16x16 picture, gets the generic tile pix/icons/_missing.png; when
// even that is missing the answer is nullptr and the caller draws the slot
// and its key label with no picture.

#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/pixie_data.h>

#include <array>
#include <map>
#include <string>

struct FamilyDescriptor;

namespace og::ui {

// The edge of every icon, in pixels; the HUD slot is this plus a 1 px frame.
inline constexpr int kAbilityIconSize = 16;
// The generic tile: a grey square with a white outlined question mark.
inline constexpr const char* kMissingIconPath = "icons/_missing.png";

// One loaded icon: the picture as drawn, and its grey copy for a locked slot.
// Both are one 16x16 frame owned by the cache (never a loader buffer).
struct AbilityIcon {
    PixieData ready;
    PixieData locked;
};

// The locked look, one byte per palette index: 0 stays 0 (transparent), 16
// stays 16 (outlines stay black), every other index goes to the grey of its
// luma on 21..31 (21 + round(luma / 230 * 10)). Never 17..20, so a locked
// picture never melts into the slot's 18 fill.
[[nodiscard]] const std::array<unsigned char, 256>& locked_icon_lut();

// The luma (0.299 R + 0.587 G + 0.114 B, 0..230) of an engine palette entry
// in 8-bit channels: the measure the locked LUT and the icon gates use.
[[nodiscard]] double icon_palette_luma(int index);

// The declared icon path of one belt entry, or nullptr when the family
// declares none for it (or the entry's slot is outside 1..5).
[[nodiscard]] const char* declared_icon_path(const FamilyDescriptor& fd,
                                             og::sim::belt::Entry entry);

class AbilityIcons {
public:
    // `generic_path` is the fallback tile; tests pass a missing path to
    // reach the "no picture at all" answer.
    explicit AbilityIcons(std::string generic_path = kMissingIconPath);

    // The icon for one belt entry of a family, or the generic tile when the
    // family declares none or the file does not load, or nullptr when even
    // the generic tile is missing.
    [[nodiscard]] const AbilityIcon* icon(const FamilyDescriptor& fd,
                                          og::sim::belt::Entry entry);

    // The icon a path names (the same cache and fallback as icon()).
    [[nodiscard]] const AbilityIcon* icon_at(const char* path);

    // How many files this set has read (successes and refusals alike):
    // what the tests use to see "loaded once" and "read again".
    [[nodiscard]] unsigned load_count() const noexcept { return loads_; }

private:
    // The cached picture of `path`, reading it on first use; nullptr when
    // the file does not load as a 16x16 picture.
    const AbilityIcon* load(const std::string& path);
    void drop_if_stale();

    std::string generic_path_;
    // path -> pixels; a path that failed to load is cached as an icon with
    // no data, so a missing file is read (and logged) once, not per frame.
    std::map<std::string, AbilityIcon> by_path_;
    unsigned generation_ = 0;
    unsigned loads_ = 0;
};

} // namespace og::ui
