/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>
#include <memory>

#include <openglad/interface/level_render.h>
#include <openglad/gameplay/level_visuals_base.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/legacy/pixdefs.h>

struct LevelVisuals : public og::gameplay::ILevelVisuals
{
    PixieData pixdata[PIX_MAX];
    std::unique_ptr<LevelRender> renderer_;
    std::int32_t topx = 0;
    std::int32_t topy = 0;
};
