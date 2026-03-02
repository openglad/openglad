// Fuzz harness for .pix (pixie) sprite data parsing.
//
// The pixie format is:
//   1 byte  - number of frames
//   1 byte  - width
//   1 byte  - height
//   N bytes - pixel data (width * height * frames)
//
// This harness feeds fuzzer-generated data through the same parsing
// logic as read_pixie_file() in graphlib.cpp, using an in-memory
// SDL_RWops instead of file I/O.

#include <SDL2/SDL.h>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <openglad/resources/pixie_data.h>

static PixieData parse_pixie_from_buffer(const uint8_t *data, size_t size)
{
    PixieData result;

    SDL_RWops *rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    if (!rw)
        return result;

    auto rw_read_exact = [](SDL_RWops *rwops, void *dst, size_t sz, size_t count) {
        return rwops && SDL_RWread(rwops, dst, sz, count) == count;
    };

    if (!rw_read_exact(rw, &result.frames, 1, 1) ||
        !rw_read_exact(rw, &result.w, 1, 1) ||
        !rw_read_exact(rw, &result.h, 1, 1))
    {
        SDL_RWclose(rw);
        return result;
    }

    size_t payload = static_cast<size_t>(result.w) * result.h * result.frames;
    if (payload == 0)
    {
        SDL_RWclose(rw);
        result.free();
        return result;
    }

    result.data = std::make_unique<unsigned char[]>(payload);
    if (!rw_read_exact(rw, result.data.get(), 1, payload))
    {
        result.free();
    }

    SDL_RWclose(rw);
    return result;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0)
        return 0;

    PixieData pd = parse_pixie_from_buffer(data, size);
    // PixieData destructor handles cleanup
    return 0;
}
