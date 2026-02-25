#pragma once

#include <cstdint>
#include <memory>

#include <openglad/legacy/pixdefs.h>
#include <openglad/data/pixie_data.h>

class LevelRender;

struct LevelVisuals {
    PixieData pixdata[PIX_MAX];
    std::unique_ptr<LevelRender> renderer_;
    std::int32_t topx = 0;
    std::int32_t topy = 0;
};
