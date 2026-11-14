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

bool on_master()
{
	return OPENGLAD_VERSION_ON_MASTER != 0;
}

// #508: a build off master's history has no version, only its commit.
std::string compose_identity(bool on_master, std::string_view version,
                             std::string_view hash)
{
	return std::string(on_master ? version : hash);
}

std::string compose_stamp(bool on_master, std::string_view version,
                          std::string_view hash)
{
	if (on_master)
		return std::format("v{} {}", version, hash);
	return std::string(hash);
}

std::string compose_cli_line(bool on_master, std::string_view version,
                             std::string_view hash)
{
	if (on_master)
		return std::format("openglad version {} ({})", version, hash);
	return std::format("openglad commit {}", hash);
}

std::string identity()
{
	return compose_identity(on_master(), string(), git_hash());
}

std::string stamp()
{
	return compose_stamp(on_master(), string(), git_hash());
}

std::string cli_line()
{
	return compose_cli_line(on_master(), string(), git_hash());
}

} // namespace og::version
