/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/resources/gloader_ctf.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/order.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/og_file.h>

#include <algorithm>
#include <cstddef>
#include <memory>

namespace {

PixieData copy_pixie(const PixieData& d)
{
    PixieData result;
    if (!d.valid())
        return result;

    result.frames = d.frames;
    result.w = d.w;
    result.h = d.h;

    const std::size_t len =
        static_cast<std::size_t>(d.w) * d.h * d.frames;
    result.data = std::make_unique<unsigned char[]>(len);
    std::copy_n(d.data.get(), len, result.data.get());
    return result;
}

} // namespace

void register_ctf_treasure_entry(loader& l, int family, const char* pix_file,
                                 int fallback_family)
{
    const auto idx =
        static_cast<std::size_t>(PIX(Order::Treasure, family));
    const auto src =
        static_cast<std::size_t>(PIX(Order::Treasure, fallback_family));

    l.graphics[idx] = read_pixie_file(pix_file);
    if (!l.graphics[idx].valid())
        l.graphics[idx] = copy_pixie(l.graphics[src]);

    l.animations[idx] = l.animations[src];
    l.act_types[idx] = l.act_types[src];
    l.stepsizes[idx] = l.stepsizes[src];
    l.lineofsight[idx] = l.lineofsight[src];
    l.hitpoints[idx] = l.hitpoints[src];
    l.damage[idx] = l.damage[src];
    l.fire_frequency[idx] = l.fire_frequency[src];
}

void register_ctf_loader_entries(loader& l)
{
    // Dedicated sprites when present; graphics fall back to a copy of an
    // existing treasure column so the entities stay usable without assets.
    register_ctf_treasure_entry(l, og::FAMILY_FLAG, "flag.png", FAMILY_KEY);
    register_ctf_treasure_entry(l, og::FAMILY_CTF_POINT, "ctfpoint.png",
                                FAMILY_TELEPORTER);
}
