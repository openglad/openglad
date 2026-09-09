/* The one translation unit that reads the generated build stamp.
 *
 * og_git_hash.h is written by cmake/GitHash.cmake on every build (rewritten
 * only when it changes), from the numbers cmake/OpenGladVersion.cmake
 * computes. Keeping the include here means a new commit relinks one object
 * instead of rebuilding every menu file, and there is exactly one place that
 * knows the macro names.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/version.h>

#include "og_git_hash.h"

#include <format>

namespace og::version {

int major()
{
	return OPENGLAD_VERSION_MAJOR;
}

int minor()
{
	return OPENGLAD_VERSION_MINOR;
}

std::string_view string()
{
	return OPENGLAD_VERSION_STRING;
}

std::string_view git_hash()
{
	return OPENGLAD_GIT_HASH;
}

std::string stamp()
{
	return std::format("v{} {}", string(), git_hash());
}

std::string cli_line()
{
	return std::format("openglad version {} ({})", string(), git_hash());
}

} // namespace og::version
