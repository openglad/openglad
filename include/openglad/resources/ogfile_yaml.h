/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/resources/og_file.h>
#include <cstddef>

// Bridge functions: OgFile ↔ YAML parser/emitter handlers.
// These match the ReadHandler/WriteHandler signatures in yaml_stream.h.

inline int ogfile_read_handler(void* data, unsigned char* buffer,
                               std::size_t size, std::size_t* size_read)
{
    auto* f = static_cast<og::io::OgFile*>(data);
    *size_read = f->read(buffer, 1, size);
    return 1;
}

inline int ogfile_write_handler(void* data, unsigned char* buffer,
                                std::size_t size)
{
    auto* f = static_cast<og::io::OgFile*>(data);
    f->write(buffer, 1, size);
    return 1;
}
