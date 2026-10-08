/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// New Specials possession (see possession.h). These are placeholders that
// keep every caller compiling: nothing possesses yet, so every answer is
// "no possession here".
#include <openglad/gameplay/possession.h>

namespace og::sim {

PossessResult possess(GameWorld& /*world*/, walker& /*rider*/,
                      walker& /*host*/, std::int16_t /*ticks*/)
{
    return {false, "CANNOT POSSESS"};
}

walker* release_possession(GameWorld& /*world*/, walker& /*either_side*/,
                           int /*overkill*/)
{
    return nullptr;
}

void possession_host_tick(GameWorld& /*world*/, living& /*host*/) {}

bool kit_on_death(GameWorld& /*world*/, walker& /*self*/)
{
    return false;
}

void possession_seat_redirect(GameWorld& /*world*/, walker*& /*control*/,
                              short /*player_num*/)
{
}

}  // namespace og::sim
