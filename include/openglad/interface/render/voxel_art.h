/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Authored voxel art (docs/voxel-render-design.md §16).
//
// A figure is a TEXT file, the way a sprite is a PNG: a header, a legend
// mapping single characters to palette indices, then the z-layers bottom-up,
// each one a depth x width character grid seen from above with the model's
// back at the top. '.' is empty. Diffable, reviewable, and editable by anyone
// who can edit pixel art — which is the point, because these are drawn by
// hand from the sprite frames and will keep being redrawn.
//
//     # a comment
//     name    footman
//     size    14 10          # width (x, left to right), depth (y, back to front)
//     cell    1.0            # world units per voxel, optional (default 1)
//     anchor  8 11           # footprint centre in sprite space, optional
//     color   W 31           # legend: one character -> one palette index
//     color   B 255          # the team band (248..255) is allowed
//     layer   0
//     ..............         # d rows of w characters, back row first
//     ...
//
// Palette INDICES, never RGB — the team band has to survive to the blitter.
// SDL-free, deterministic, no external dependency.

#include <openglad/interface/render/voxel_scene.h>

#include <string>

namespace og::render {

// Parse one .voxtxt document. On failure returns false and fills `error` with
// a "<source>:<line>: what went wrong" message.
bool voxel_art_parse(const std::string& text, const std::string& source,
                     VoxelModel& out, std::string& error);

// Read and parse a file. Same contract; a missing file is an error, not an
// empty model.
bool voxel_art_load_file(const std::string& path, VoxelModel& out,
                         std::string& error);

} // namespace og::render
