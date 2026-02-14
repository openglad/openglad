/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include "SDL.h"

class walker;
class effect;

struct EffectFamilyDescriptor {
    int family_id;
    const char* name;
    bool loops_animation;     // true = loop cycle; false = one-shot
    Sint32 init_bit_flags;    // BIT_ flags set on creation in gloader

    bool (*on_act)(effect* self);
    bool (*on_death)(effect* self);
};
