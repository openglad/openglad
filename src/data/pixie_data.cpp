/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "data/pixie_data.h"


PixieData::PixieData()
    : frames(0), w(0), h(0), data(nullptr)
{}

PixieData::PixieData(unsigned char frames_, unsigned char w_, unsigned char h_, unsigned char* data_)
    : frames(frames_), w(w_), h(h_), data(data_)
{}

PixieData::PixieData(PixieData&& other) noexcept
    : frames(other.frames), w(other.w), h(other.h), data(std::move(other.data))
{
    other.frames = 0;
    other.w = 0;
    other.h = 0;
}

PixieData& PixieData::operator=(PixieData&& other) noexcept
{
    if (this != &other)
    {
        frames = other.frames;
        w = other.w;
        h = other.h;
        data = std::move(other.data);
        other.frames = 0;
        other.w = 0;
        other.h = 0;
    }
    return *this;
}

bool PixieData::valid() const
{
    return (data != nullptr && frames != 0 && w != 0 && h != 0);
}

void PixieData::free()
{
    frames = 0;
    w = 0;
    h = 0;
    data.reset();
}
