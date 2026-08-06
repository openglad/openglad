/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/core/family_presentation.h>

struct GeneratorFamilyDescriptor {
    int family_id;
    const char* name;
    int default_weapon;       // living family produced
    bool has_lifetime;        // true for TENT/BONES
    char spawn_ani_type;      // ANI_TELE_IN for TOWER
    bool clear_owner;         // true for TOWER/TREEHOUSE

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
    // editor_label is the level editor's palette caption ("MAGE TOWER").
    og::FamilyGlyph glyph{U'#', '#', og::GlyphColor::Default, false, false};
    og::RadarBlip radar{};
    const char* editor_label = "GENERATOR";

    // Pack-declared fully-qualified id ("core:tent"); see FamilyDescriptor
    // for the contract (borrowed from the ClasspackStore, nullptr = none).
    const char* declared_id = nullptr;
};
