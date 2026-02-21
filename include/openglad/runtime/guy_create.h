/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <memory>

class guy;
class walker;
class screen;

// Free functions extracted from guy class to avoid runtime (screen*)
// dependency in the entities layer.  Implementations in src/runtime/guy_create.cpp.
[[nodiscard]] std::unique_ptr<walker> guy_create_walker_owned(guy& g, screen* myscreen);
walker* guy_create_and_add_walker(guy& g, screen* myscreen);
