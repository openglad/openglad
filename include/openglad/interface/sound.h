/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <memory>

class soundob
{
public:
    virtual ~soundob() = default;

    virtual void play_sound(short whichsound) = 0;
    virtual unsigned char set_sound(bool silent) = 0;
};

std::unique_ptr<soundob> create_soundob(bool silent = false);
