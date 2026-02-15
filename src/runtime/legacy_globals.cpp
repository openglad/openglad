/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/legacy/base.h>

// Legacy global shims (transitional). Ownership is being moved into
// og::runtime::GameSession, but these variables must remain linkable for
// non-app binaries (tests/tools).
screen* myscreen = nullptr;

