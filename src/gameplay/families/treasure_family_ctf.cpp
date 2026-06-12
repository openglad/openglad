/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/gameplay/treasure.h>

static bool flag_on_eat(treasure* self, walker* eater)
{
    return og::sim::ctf_on_flag_touch(self, eater);
}

static bool ctf_point_on_eat(treasure* self, walker* eater)
{
    return og::sim::ctf_on_point_touch(self, eater);
}

const TreasureFamilyDescriptor& describe_treasure_flag()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = og::FAMILY_FLAG,
        .name = "FLAG",
        .init_ignore = false,
        .init_frame = 0,
        .on_eat = flag_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_ctf_point()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = og::FAMILY_CTF_POINT,
        .name = "WAYPOINT",
        .init_ignore = false,
        .init_frame = 0,
        .on_eat = ctf_point_on_eat,
    };
    return desc;
}
