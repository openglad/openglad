#include "SDL.h"
#include "test_framework.h"

#include <cstdint>
#include <vector>

// sai2x.cpp does not expose these functions via a header.
extern int Init_2xSaI();
extern void Super2xSaI_ex(unsigned char* src, Uint32 src_pitch, unsigned char* unused,
                          unsigned char* dest, Uint32 dest_pitch, Uint32 width, Uint32 height);

static void fill_pattern(std::vector<unsigned char>& buf, int w, int h)
{
    // ARGB8888 pixels, 4 bytes per pixel.
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const int i = (y * w + x) * 4;
            buf[i + 0] = 0xFF;                       // A
            buf[i + 1] = static_cast<unsigned char>(x * 40 + y * 5); // R
            buf[i + 2] = static_cast<unsigned char>(y * 40 + x * 3); // G
            buf[i + 3] = static_cast<unsigned char>(x * 20);         // B
        }
    }
}

void test_sai2x_super2xsai_ex_runs_on_small_buffer()
{
    // Initialize masks for 32bpp.
    Init_2xSaI();

    const int w = 8;
    const int h = 8;
    const int src_pitch = w * 4;
    const int dw = w * 2;
    const int dh = h * 2;
    const int dst_pitch = dw * 4;

    std::vector<unsigned char> src(src_pitch * h);
    std::vector<unsigned char> dst(dst_pitch * dh);
    fill_pattern(src, w, h);

    // Runs the filter. We don't validate output quality; the goal is to
    // execute the scaler code paths under coverage.
    Super2xSaI_ex(src.data(), src_pitch, nullptr, dst.data(), dst_pitch, w, h);

    // Basic sanity: output buffer is non-zero somewhere.
    bool any = false;
    for (unsigned char b : dst)
    {
        if (b != 0)
        {
            any = true;
            break;
        }
    }
    TEST_ASSERT(any, "Super2xSaI_ex should write to the output buffer");
}
REGISTER_TEST(test_sai2x_super2xsai_ex_runs_on_small_buffer);

