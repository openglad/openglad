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

#pragma once

// Full platform I/O header (SDL build).
// Includes SDL-free declarations plus SDL_RWops helpers.

#include <openglad/resources/io_common.h>
#include "SDL.h"
#include <memory>

struct SDLRWopsCloser {
    void operator()(SDL_RWops* rwops) const
    {
        if (rwops != nullptr)
            SDL_RWclose(rwops);
    }
};
using RwopsPtr = std::unique_ptr<SDL_RWops, SDLRWopsCloser>;

SDL_RWops* open_read_file(const char* file, bool debug = false);
SDL_RWops* open_read_file(const char* path, const char* file);
SDL_RWops* open_write_file(const char* file);
SDL_RWops* open_write_file(const char* path, const char* file);

int rwops_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read);
int rwops_write_handler(void *data, unsigned char *buffer, size_t size);
