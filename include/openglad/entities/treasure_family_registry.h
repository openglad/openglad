/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

struct TreasureFamilyDescriptor;

void init_treasure_family_registry();
const TreasureFamilyDescriptor* get_treasure_family_descriptor(int family_id);
