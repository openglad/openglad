#include <openglad/platform/sai2x.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// sai2x.cpp does not expose these functions via a header.
extern int Init_2xSaI();
extern void Super2xSaI_ex2(unsigned char* src, int srcx, int srcy, int srcw, int srch,
                           int src_pitch, int src_height, unsigned char* dst,
                           int dstx, int dsty, int dst_pitch);
extern void Scale_SuperEagle(unsigned char* src, int srcx, int srcy, int srcw, int srch,
                             int src_pitch, int src_height, unsigned char* dst,
                             int dstx, int dsty, int dst_pitch);
extern void Super2xSaI_ex(unsigned char* src, Uint32 src_pitch, unsigned char* unused,
                          unsigned char* dest, Uint32 dest_pitch, Uint32 width, Uint32 height);
extern void Super2xSaI(SDL_Surface* src, SDL_Surface* dest, int s_x, int s_y, int d_x, int d_y, int w, int h);

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

static void run_sai2x_ex2_and_supereagle_write_output();
static void run_sai2x_surface_wrapper_guards_and_scaling();
static void run_sai2x_screen_class_paths();

TEST(Sai2xScaler, sai2x_super2xsai_ex_runs_on_small_buffer)
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
    ASSERT_TRUE(any) << "Super2xSaI_ex should write to the output buffer";

    run_sai2x_ex2_and_supereagle_write_output();
    run_sai2x_surface_wrapper_guards_and_scaling();
    run_sai2x_screen_class_paths();
}


static void run_sai2x_ex2_and_supereagle_write_output()
{
    Init_2xSaI();

    const int w = 8;
    const int h = 8;
    const int src_pitch = w * 4;
    const int dw = w * 2;
    const int dh = h * 2;
    const int dst_pitch = dw * 4;

    std::vector<unsigned char> src(src_pitch * h);
    std::vector<unsigned char> dst_sai(dst_pitch * dh);
    std::vector<unsigned char> dst_eagle(dst_pitch * dh);
    fill_pattern(src, w, h);

    Super2xSaI_ex2(src.data(), 0, 0, w, h, src_pitch, h, dst_sai.data(), 0, 0, dst_pitch);
    Scale_SuperEagle(src.data(), 0, 0, w, h, src_pitch, h, dst_eagle.data(), 0, 0, dst_pitch);

    bool any_sai = false;
    for (unsigned char b : dst_sai) {
        if (b != 0) {
            any_sai = true;
            break;
        }
    }
    bool any_eagle = false;
    for (unsigned char b : dst_eagle) {
        if (b != 0) {
            any_eagle = true;
            break;
        }
    }

    ASSERT_TRUE(any_sai) << "Super2xSaI_ex2 should write to destination";
    ASSERT_TRUE(any_eagle) << "Scale_SuperEagle should write to destination";
}

static void run_sai2x_surface_wrapper_guards_and_scaling()
{
    Init_2xSaI();

    // Guard path: null surfaces should return without crashing.
    Super2xSaI(nullptr, nullptr, 0, 0, 0, 0, 8, 8);

    SDL_Surface* src = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 32, SDL_PIXELFORMAT_ARGB8888);
    ASSERT_TRUE(src != nullptr && dst != nullptr) << "surfaces should allocate";
    if (!src || !dst)
        return;

    std::vector<unsigned char> pattern(8 * 8 * 4);
    fill_pattern(pattern, 8, 8);
    std::memcpy(src->pixels, pattern.data(), pattern.size());
    SDL_FillSurfaceRect(dst, nullptr, 0x00000000u);

    // Guard path: tiny source should not run scaler.
    Super2xSaI(src, dst, 0, 0, 0, 0, 3, 3);

    // Happy path.
    Super2xSaI(src, dst, 0, 0, 0, 0, 8, 8);

    bool any = false;
    const unsigned char* out = reinterpret_cast<const unsigned char*>(dst->pixels);
    for (int i = 0; i < dst->pitch * dst->h; ++i) {
        if (out[i] != 0) {
            any = true;
            break;
        }
    }
    ASSERT_TRUE(any) << "Super2xSaI surface wrapper should write output on valid input";

    SDL_DestroySurface(src);
    SDL_DestroySurface(dst);
}

static void run_sai2x_screen_class_paths()
{
    {
        Screen s(RenderEngine::NoZoom, 320, 200, 0);
        s.clear();
        s.clear(10, 10, 20, 20);
        s.swap(0, 0, 40, 40);
        s.clear_window();
    }

    {
        Screen s(RenderEngine::SAI, 320, 200, 0);
        s.clear();
        s.swap(0, 0, 40, 40);
    }

    {
        Screen s(RenderEngine::Eagle, 320, 200, 0);
        s.clear();
        s.swap(0, 0, 40, 40);

        const std::string path = std::filesystem::temp_directory_path() / "openglad_sai2x_test.bmp";
        char bmp_path[512] = {};
        std::snprintf(bmp_path, sizeof(bmp_path), "%s", path.c_str());
        s.SaveBMP(s.render, bmp_path);
        ASSERT_TRUE(std::filesystem::exists(path)) << "SaveBMP should create output file";
        std::filesystem::remove(path);
    }
}
