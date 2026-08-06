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
class weap;

struct WeaponFamilyDescriptor {
    int family_id;
    const char* name;
    int fire_sound;           // sound to play when fired (SOUND_FWIP default)
    bool skip_sit_notify;     // true = don't notify when sitting (TREE, BLOOD, DOOR)
    bool is_auto_attackable;  // true = AI auto-attacks this on collision (TREE, GLOW, DOOR)
    std::int32_t init_bit_flags;    // BIT_ flags set on creation in gloader
    short init_lifetime;      // 0 = no override; GLOW = 350
    char init_ani_type;       // 0 = default; CIRCLE_PROTECTION = 5

    // Z-axis projectile physics (all default 0/false = legacy flat behavior).
    // worldz/vz are visual + drop-enabling only: they never affect the 2D path
    // and are not part of the parity dump. See docs/z-axis-design.md.
    float init_vz = 0.0f;          // initial vertical velocity (px/tick), set in gloader
    float gravity = 0.0f;          // per-tick decrease applied to vz (downward arc)
    short init_sizez = 0;          // cylinder height (0 = full-height sentinel)
    bool can_drop_floors = false;  // may fall through "air" to a lower floor

    // Pack-shipped art + motion (see FamilyDescriptor for the contract).
    // nullptr/0 = the gloader EntityDef row keeps supplying them.

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

    // Presentation data (curses glyph + radar blip); defaults are the
    // legacy `default:` branches of the UI switches.
    og::FamilyGlyph glyph{U'*', '*', og::GlyphColor::White, false, false};
    og::RadarBlip radar{};

    // Pack-declared fully-qualified id ("core:knife"); see FamilyDescriptor
    // for the contract (borrowed from the ClasspackStore, nullptr = none).
    const char* declared_id = nullptr;

    bool (*on_death)(weap* self);
    bool (*on_animate)(weap* self);
    bool (*on_hit_target)(walker* weapon, walker* target, walker* owner);
};
