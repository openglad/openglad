/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/core/family_presentation.h>

class treasure;
class walker;

struct TreasureFamilyDescriptor {
    int family_id;
    const char* name;
    bool init_ignore;         // true for STAIN
    short init_frame;         // -1 = no override

    // Pack-shipped art + motion (see FamilyDescriptor for the contract).

    // Base hitpoints for this family, written into the loader's table at
    // install. 0 = the gloader EntityDef row keeps supplying it (the same
    // sentinel contract as pix_filename/anim_table above). Non-living
    // families have no stats block of their own, so this is where a pack
    // declares durability for scenery it ships -- weapon HP is what makes a
    // TREE take 50 damage and a DOOR 5000 before it breaks.
    float hp = 0.0f;

    const char* pix_filename = nullptr;
    const signed char* const* anim_table = nullptr;
    int anim_row_count = 0;

    // Presentation data; defaults are the legacy `default:` branches.
    og::FamilyGlyph glyph{U'$', '$', og::GlyphColor::Yellow, false, false};
    og::RadarBlip radar{};

    // Pack-declared fully-qualified id ("core:gold"); see FamilyDescriptor
    // for the contract (borrowed from the ClasspackStore, nullptr = none).
    const char* declared_id = nullptr;

    bool (*on_eat)(treasure* self, walker* eater);
};
