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

// The world's difficulty as a percent for the sim's scaling math: 100 when
// there is no running game or world, or when the world's difficulty is not
// positive. One definition (walker.cpp) for walker's and living's callers.
std::uint32_t query_difficulty_percent();

namespace og::sim {

// One rule, two readers (living.cpp defines both). A12a: a team-0 walker
// with no player character takes the difficulty percent like its foes, but
// never at 100% (the skip, not a multiply by 1, keeps the float stream
// bit-exact). living::set_difficulty asks it of a body.
bool team0_body_takes_difficulty(const walker& body, std::uint32_t percent);

// #473: a team-0 shot takes the percent exactly when its shooter's body did:
// its owner is a Living that team0_body_takes_difficulty answers yes for.
// An unowned team-0 object (a placed weapon, treasure or effect) never does.
// walker::set_difficulty's default arm asks it.
bool team0_shot_takes_difficulty(const walker& shot, std::uint32_t percent);

}  // namespace og::sim
