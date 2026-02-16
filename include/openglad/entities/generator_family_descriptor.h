/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

struct GeneratorFamilyDescriptor {
    int family_id;
    const char* name;
    int default_weapon;       // living family produced
    bool has_lifetime;        // true for TENT/BONES
    char spawn_ani_type;      // ANI_TELE_IN for TOWER
    bool clear_owner;         // true for TOWER/TREEHOUSE
};
