/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

class PixieData;

// Abstract render component for walker entities.
// SDL builds create a PixieNWalkerRender (wrapping pixieN);
// headless builds leave render_ as nullptr.
class IWalkerRender {
public:
    virtual ~IWalkerRender() = default;
    virtual const unsigned char* bmp_data() const = 0;
    virtual void set_frame(short framenum) = 0;
    virtual void set_data(const PixieData& data) = 0;

protected:
    IWalkerRender() = default;
};
