/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

class treasure;
class walker;

struct TreasureFamilyDescriptor {
    int family_id;
    const char* name;
    bool init_ignore;         // true for STAIN
    short init_frame;         // -1 = no override
    bool (*on_eat)(treasure* self, walker* eater);
};
