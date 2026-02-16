/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

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

    bool (*on_death)(weap* self);
    bool (*on_animate)(weap* self);
    bool (*on_hit_target)(walker* weapon, walker* target, walker* owner);
};
