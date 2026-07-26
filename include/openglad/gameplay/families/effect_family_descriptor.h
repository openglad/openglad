/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/core/family_presentation.h>

#include <cstdint>

class walker;
class effect;

struct EffectFamilyDescriptor {
    int family_id;
    const char* name;
    bool loops_animation;     // true = loop cycle; false = one-shot
    bool creates_hit_effect;  // true = creates hit animation on damage (like weapons do)
    std::int32_t init_bit_flags;    // BIT_ flags set on creation in gloader

    // Pack-shipped art + motion (see FamilyDescriptor for the contract).
    const char* pix_filename = nullptr;
    const signed char* const* anim_table = nullptr;
    int anim_row_count = 0;

    // Presentation data; defaults are the legacy `default:` branches.
    og::FamilyGlyph glyph{U'*', '*', og::GlyphColor::White, false, false};
    og::RadarBlip radar{};

    bool (*on_act)(effect* self);
    bool (*on_death)(effect* self);
};
