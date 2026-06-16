// Fuzz harness for .fss (scenario) file parsing.
//
// The FSS format (version 6+) is a binary format containing:
//   3 bytes  - "FSS" header
//   1 byte   - version number
//   8 bytes  - grid name
//   1 byte   - scenario type
//   2 bytes  - object count (short)
//   Per object (version 6+, 39 bytes each):
//     1 byte  - order
//     1 byte  - family
//     2 bytes - x position
//     2 bytes - y position
//     1 byte  - team
//     1 byte  - facing
//     1 byte  - command
//     1 byte  - level
//     12 bytes - name
//     10 bytes - reserved
//     ... (version 7+ add extra fields)
//   1 byte   - number of description lines
//   Per line:
//     1 byte  - line width
//     N bytes - line text
//   ... (version 7+ add par_value, time_limit fields)
//
// This harness exercises the binary read/parse logic from
// load_scenario_version() without creating game entities.

#include <SDL2/SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

static bool rw_read_exact(SDL_RWops *rw, void *dst, size_t sz, size_t count)
{
    return rw && SDL_RWread(rw, dst, sz, count) == count;
}

static void fuzz_parse_scenario(const uint8_t *data, size_t size)
{
    SDL_RWops *rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    if (!rw)
        return;

    // Read and validate FSS header
    std::array<char, 4> header = {};
    if (!rw_read_exact(rw, header.data(), 1, 3))
    {
        SDL_RWclose(rw);
        return;
    }
    if (header[0] != 'F' || header[1] != 'S' || header[2] != 'S')
    {
        SDL_RWclose(rw);
        return;
    }

    char version = 0;
    if (!rw_read_exact(rw, &version, 1, 1))
    {
        SDL_RWclose(rw);
        return;
    }
    if (version < 2 || version > 9)
    {
        SDL_RWclose(rw);
        return;
    }

    // Read grid name (8 bytes)
    std::array<char, 9> gridname = {};
    if (!rw_read_exact(rw, gridname.data(), 8, 1))
    {
        SDL_RWclose(rw);
        return;
    }
    gridname[8] = '\0';

    // Version 6+ has scenario type
    char scen_type = 0;
    if (version >= 6)
    {
        if (!rw_read_exact(rw, &scen_type, 1, 1))
        {
            SDL_RWclose(rw);
            return;
        }
    }

    // Read object count
    int16_t listsize = 0;
    if (!rw_read_exact(rw, &listsize, 2, 1))
    {
        SDL_RWclose(rw);
        return;
    }
    if (listsize < 0 || listsize > 4096)
    {
        SDL_RWclose(rw);
        return;
    }

    // Read each object's fields
    for (int16_t i = 0; i < listsize; i++)
    {
        uint8_t order = 0, family = 0, team = 0, facing = 0, command = 0;
        int16_t xpos = 0, ypos = 0;

        if (!rw_read_exact(rw, &order, 1, 1) ||
            !rw_read_exact(rw, &family, 1, 1) ||
            !rw_read_exact(rw, &xpos, 2, 1) ||
            !rw_read_exact(rw, &ypos, 2, 1) ||
            !rw_read_exact(rw, &team, 1, 1) ||
            !rw_read_exact(rw, &facing, 1, 1) ||
            !rw_read_exact(rw, &command, 1, 1))
        {
            SDL_RWclose(rw);
            return;
        }

        if (version >= 6)
        {
            uint8_t level = 0;
            std::array<char, 12> name = {};
            std::array<char, 10> reserved = {};
            if (!rw_read_exact(rw, &level, 1, 1) ||
                !rw_read_exact(rw, name.data(), 12, 1) ||
                !rw_read_exact(rw, reserved.data(), 10, 1))
            {
                SDL_RWclose(rw);
                return;
            }
        }
        else if (version >= 4)
        {
            // v4-5: extra reserved
            std::array<char, 11> reserved = {};
            if (!rw_read_exact(rw, reserved.data(), 11, 1))
            {
                SDL_RWclose(rw);
                return;
            }
        }
    }

    // Read description lines
    uint8_t numlines = 0;
    if (version >= 3)
    {
        if (!rw_read_exact(rw, &numlines, 1, 1))
        {
            SDL_RWclose(rw);
            return;
        }

        for (uint8_t i = 0; i < numlines; i++)
        {
            uint8_t width = 0;
            if (!rw_read_exact(rw, &width, 1, 1))
            {
                SDL_RWclose(rw);
                return;
            }

            // Read and discard line content
            std::array<char, 256> buf;
            int remaining = width;
            while (remaining > 0)
            {
                int chunk = remaining < static_cast<int>(buf.size()) ? remaining : static_cast<int>(buf.size());
                if (!rw_read_exact(rw, buf.data(), static_cast<size_t>(chunk), 1))
                {
                    SDL_RWclose(rw);
                    return;
                }
                remaining -= chunk;
            }
        }
    }

    // Version 7+: par_value and time_limit
    if (version >= 7)
    {
        int16_t par_value = 0;
        int16_t time_limit = 0;
        rw_read_exact(rw, &par_value, 2, 1);
        rw_read_exact(rw, &time_limit, 2, 1);
    }

    SDL_RWclose(rw);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0)
        return 0;

    fuzz_parse_scenario(data, size);
    return 0;
}
