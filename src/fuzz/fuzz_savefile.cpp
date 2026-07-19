// Fuzz harness for .gtl (save file) header parsing.
//
// The GTL format (version 9) is:
//   3 bytes  - "GTL" header
//   1 byte   - version number
//   2 bytes  - registered mark (v7+)
//   40 bytes - saved game name (v2+)
//   40 bytes - campaign ID (v8+)
//   2 bytes  - scenario number
//   4 bytes  - cash (uint32)
//   4 bytes  - score (uint32)
//   4x(4+4) bytes - per-team cash+score (v6+, 4 teams)
//   2 bytes  - allied mode (v7+)
//   2 bytes  - team list size (short)
//   1 byte   - number of players
//   31 bytes - reserved; v14+ reinterprets as:
//     8 bytes  - last-played unix seconds (int64, offset 133)
//     23 bytes - reserved (zero-filled)
//   Per team member (58 bytes each):
//     1 byte  - order
//     1 byte  - family
//     12 bytes - name
//     2 bytes each - str, dex, con, int, arm, lev
//     4 bytes - exp (uint32)
//     2 bytes - kills
//     4 bytes - level_kills
//     4 bytes - total_damage
//     4 bytes - total_hits
//     4 bytes - total_shots
//     2 bytes - team number
//     8 bytes - reserved; v14+ reinterprets as:
//       1 byte  - deployed flag (guy offset +50)
//       7 bytes - reserved (zero-filled)
//   Campaign progress data (v8+):
//     2 bytes  - num_campaigns
//     Per campaign:
//       40 bytes - campaign ID
//       2 bytes  - current level
//       2 bytes  - num_levels
//       Per level: 2 bytes - level index
//
// This harness exercises the binary read/parse logic from
// SaveData::load() without touching the filesystem or game state.

#include <SDL3/SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>

static constexpr int MAX_TEAM_SIZE = 24;

static bool rw_read_exact(SDL_IOStream *rw, void *dst, size_t sz, size_t count)
{
    return rw && SDL_ReadIO(rw, dst, sz * count) == sz * count;
}

static void fuzz_parse_savefile(const uint8_t *data, size_t size)
{
    SDL_IOStream *rw = SDL_IOFromConstMem(data, size);
    if (!rw)
        return;

    // Read and validate GTL header
    std::array<char, 4> header = {};
    if (!rw_read_exact(rw, header.data(), 3, 1))
    {
        SDL_CloseIO(rw);
        return;
    }
    if (header[0] != 'G' || header[1] != 'T' || header[2] != 'L')
    {
        SDL_CloseIO(rw);
        return;
    }

    uint8_t version = 0;
    if (!rw_read_exact(rw, &version, 1, 1))
    {
        SDL_CloseIO(rw);
        return;
    }

    // Version 7+: registered mark
    if (version >= 7)
    {
        int16_t registered = 0;
        if (!rw_read_exact(rw, &registered, 2, 1))
        {
            SDL_CloseIO(rw);
            return;
        }
    }

    // Version 2+: saved game name
    if (version >= 2)
    {
        std::array<char, 40> savedgame = {};
        if (!rw_read_exact(rw, savedgame.data(), 40, 1))
        {
            SDL_CloseIO(rw);
            return;
        }
    }
    else if (version < 2)
    {
        // Unsupported version
        SDL_CloseIO(rw);
        return;
    }

    // Version 8+: campaign ID
    if (version >= 8)
    {
        std::array<char, 41> campaign = {};
        if (!rw_read_exact(rw, campaign.data(), 1, 40))
        {
            SDL_CloseIO(rw);
            return;
        }
    }

    // Scenario number
    int16_t scen_num = 0;
    if (!rw_read_exact(rw, &scen_num, 2, 1))
    {
        SDL_CloseIO(rw);
        return;
    }

    // Cash and score
    uint32_t cash = 0, score = 0;
    if (!rw_read_exact(rw, &cash, 4, 1) ||
        !rw_read_exact(rw, &score, 4, 1))
    {
        SDL_CloseIO(rw);
        return;
    }

    // Version 6+: per-team cash and score (4 teams)
    if (version >= 6)
    {
        for (int i = 0; i < 4; i++)
        {
            uint32_t tcash = 0, tscore = 0;
            if (!rw_read_exact(rw, &tcash, 4, 1) ||
                !rw_read_exact(rw, &tscore, 4, 1))
            {
                SDL_CloseIO(rw);
                return;
            }
        }
    }

    // Version 7+: allied mode
    if (version >= 7)
    {
        int16_t allied = 0;
        if (!rw_read_exact(rw, &allied, 2, 1))
        {
            SDL_CloseIO(rw);
            return;
        }
    }

    // Team list size
    int16_t listsize = 0;
    if (!rw_read_exact(rw, &listsize, 2, 1))
    {
        SDL_CloseIO(rw);
        return;
    }
    if (listsize < 0 || listsize > MAX_TEAM_SIZE)
    {
        SDL_CloseIO(rw);
        return;
    }

    // Number of players
    uint8_t numplayers = 0;
    if (!rw_read_exact(rw, &numplayers, 1, 1))
    {
        SDL_CloseIO(rw);
        return;
    }

    // Reserved (31 bytes); v14+ reads an 8-byte timestamp + 23 reserved
    std::array<char, 31> filler = {};
    if (version >= 14)
    {
        int64_t last_played = 0;
        if (!rw_read_exact(rw, &last_played, 8, 1) ||
            !rw_read_exact(rw, filler.data(), 23, 1))
        {
            SDL_CloseIO(rw);
            return;
        }
    }
    else if (!rw_read_exact(rw, filler.data(), 31, 1))
    {
        SDL_CloseIO(rw);
        return;
    }

    // Read team member data (58 bytes each)
    for (int16_t i = 0; i < listsize; i++)
    {
        uint8_t order = 0;
        char family = 0;
        std::array<char, 12> name = {};
        int16_t str = 0, dex = 0, con = 0, intel = 0, arm = 0, lev = 0;
        uint32_t exp = 0;
        int16_t kills = 0;
        int32_t level_kills = 0;
        int32_t td = 0, th = 0, ts = 0;
        int16_t teamnum = 0;
        std::array<char, 8> reserved = {};

        if (!rw_read_exact(rw, &order, 1, 1) ||
            !rw_read_exact(rw, &family, 1, 1) ||
            !rw_read_exact(rw, name.data(), 12, 1) ||
            !rw_read_exact(rw, &str, 2, 1) ||
            !rw_read_exact(rw, &dex, 2, 1) ||
            !rw_read_exact(rw, &con, 2, 1) ||
            !rw_read_exact(rw, &intel, 2, 1) ||
            !rw_read_exact(rw, &arm, 2, 1) ||
            !rw_read_exact(rw, &lev, 2, 1) ||
            !rw_read_exact(rw, &exp, 4, 1) ||
            !rw_read_exact(rw, &kills, 2, 1) ||
            !rw_read_exact(rw, &level_kills, 4, 1) ||
            !rw_read_exact(rw, &td, 4, 1) ||
            !rw_read_exact(rw, &th, 4, 1) ||
            !rw_read_exact(rw, &ts, 4, 1) ||
            !rw_read_exact(rw, &teamnum, 2, 1))
        {
            SDL_CloseIO(rw);
            return;
        }

        // Reserved (8 bytes); v14+ reads a 1-byte deploy flag + 7 reserved
        if (version >= 14)
        {
            uint8_t deployed = 0;
            if (!rw_read_exact(rw, &deployed, 1, 1) ||
                !rw_read_exact(rw, reserved.data(), 7, 1))
            {
                SDL_CloseIO(rw);
                return;
            }
        }
        else if (!rw_read_exact(rw, reserved.data(), 8, 1))
        {
            SDL_CloseIO(rw);
            return;
        }
    }

    // Campaign progress data
    if (version >= 8)
    {
        int16_t num_campaigns = 0;
        if (!rw_read_exact(rw, &num_campaigns, 2, 1))
        {
            SDL_CloseIO(rw);
            return;
        }
        // Clamp to avoid excessive iteration
        if (num_campaigns < 0 || num_campaigns > 1000)
        {
            SDL_CloseIO(rw);
            return;
        }

        for (int16_t i = 0; i < num_campaigns; i++)
        {
            std::array<char, 41> campaign = {};
            int16_t current_level = 0;
            int16_t num_levels = 0;

            if (!rw_read_exact(rw, campaign.data(), 1, 40) ||
                !rw_read_exact(rw, &current_level, 2, 1) ||
                !rw_read_exact(rw, &num_levels, 2, 1))
            {
                SDL_CloseIO(rw);
                return;
            }

            if (num_levels < 0 || num_levels > 10000)
            {
                SDL_CloseIO(rw);
                return;
            }

            for (int16_t j = 0; j < num_levels; j++)
            {
                int16_t level_index = 0;
                if (!rw_read_exact(rw, &level_index, 2, 1))
                {
                    SDL_CloseIO(rw);
                    return;
                }
            }
        }
    }
    else if (version >= 5)
    {
        // Versions 5-7: flat 500-byte level status array
        std::array<char, 500> levelstatus = {};
        if (!rw_read_exact(rw, levelstatus.data(), 500, 1))
        {
            SDL_CloseIO(rw);
            return;
        }
    }
    else
    {
        // Versions 2-4: flat 200-byte level status array
        std::array<char, 200> levelstatus = {};
        rw_read_exact(rw, levelstatus.data(), 200, 1);
    }

    SDL_CloseIO(rw);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0)
        return 0;

    fuzz_parse_savefile(data, size);
    return 0;
}
