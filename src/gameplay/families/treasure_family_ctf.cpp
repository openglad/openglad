/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/core/constants.h>

const TreasureFamilyDescriptor& describe_treasure_flag()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = og::FAMILY_FLAG,
        .name = "FLAG",
        .init_ignore = false,
        .init_frame = 0,
        .on_eat = nullptr,
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
        .on_eat = nullptr,
    };
    return desc;
}
