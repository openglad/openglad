/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// New Specials fearlessness (see fearless.h). For now only the walker's own
// mark counts; the banner's rally aura joins it later.
#include <openglad/gameplay/fearless.h>

#include <openglad/gameplay/walker.h>

namespace og::sim {

bool fearless(const walker& w)
{
    return w.fearless_bit();
}

}  // namespace og::sim
