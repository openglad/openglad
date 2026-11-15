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

// "2.1073", or "2.0" when the history is unknown. The counted number; a
// player sees it only when on_master().
std::string_view string();

// "7e7f4079"; "7e7f4079+" for a tree with uncommitted changes to tracked
// files; "nogit" without git.
std::string_view git_hash();

// True for a master build (og_version_on_master in
// cmake/OpenGladVersion.cmake): the built commit is on master's history and,
// when git decides, the tree had no uncommitted changes. Only then is the
// commit count a version: a pull-request or local branch build, or a local
// build with changes not yet committed, shows its hash alone (#508).
bool on_master();

// What a player is told this build is: "2.1073" on master, else "7e7f4079".
// The help footer prints it.
std::string identity();

// The main-menu build stamp: "v2.1073 7e7f4079" on master, "7e7f4079"
// (or "7e7f4079+") elsewhere.
std::string stamp();

// The -v / --version line: "openglad version 2.1073 (7e7f4079)" on master,
// "openglad commit 7e7f4079" elsewhere.
std::string cli_line();

// The three lines, composed from given parts so one test binary can assert
// both kinds of build; identity(), stamp() and cli_line() pass their own.
std::string compose_identity(bool on_master, std::string_view version,
                             std::string_view hash);
std::string compose_stamp(bool on_master, std::string_view version,
                          std::string_view hash);
std::string compose_cli_line(bool on_master, std::string_view version,
                             std::string_view hash);

} // namespace og::version
