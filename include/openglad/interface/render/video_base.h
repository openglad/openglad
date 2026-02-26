#pragma once

#include <openglad/legacy/base.h>
#include <array>
#include <span>

namespace og::render {

struct Rect {
    Sint32 x;
    Sint32 y;
    Uint32 w;
    Uint32 h;
};

class VideoBase {
public:
    virtual ~VideoBase() = default;

    virtual void clearbuffer() = 0;
    virtual void clearbuffer(int x, int y, int w, int h) = 0;
    virtual std::span<unsigned char> getbuffer() = 0;
    virtual void point(Sint32 x, Sint32 y, unsigned char color) = 0;
    virtual void pointb(Sint32 x, Sint32 y, unsigned char color) = 0;
    virtual void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                          unsigned char color, Sint32 filled) = 0;
    virtual void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                             Sint32 border) = 0;
    virtual void buffer_to_screen(Sint32 viewstartx, Sint32 viewstarty,
                                  Sint32 viewwidth, Sint32 viewheight) = 0;
    virtual void swap() = 0;
};

} // namespace og::render
