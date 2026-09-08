#pragma once

#include <string>
#include <string_view>

// The build's identity. Numbers come from the generated og_git_hash.h
// (cmake/GitHash.cmake -> cmake/OpenGladVersion.cmake), included by
// src/core/version.cpp only; everything else asks through these functions.
namespace og::version {

// The hand-edited major. 2 is the current generation.
int major();

// Commit count of the built commit; 0 when the history is unknown (no git,
// a shallow clone, a source tarball) and no packager override was given.
int minor();

// "2.1073", or "2.0" when the history is unknown.
std::string_view string();

// "7e7f4079"; "7e7f4079+" for a dirty tree; "nogit" without git.
std::string_view git_hash();

// "v2.1073 7e7f4079" — the main-menu build stamp.
std::string stamp();

// "openglad version 2.1073 (7e7f4079)" — the -v / --version line.
std::string cli_line();

} // namespace og::version
