# Regenerated every build (cheap), rewritten only when the stamp changes so
# nothing rebuilds on a no-op. Produces og_git_hash.h defining
# OPENGLAD_GIT_HASH plus the version numbers — the version and short commit
# id the main menu stamps bottom-centre, so a running binary can always be
# matched to its commit (stale-build confusion is a solved argument, not a
# debugging session). The numbers come from og_version_compute() in
# OpenGladVersion.cmake; this file has no version logic of its own.
include(${CMAKE_CURRENT_LIST_DIR}/OpenGladVersion.cmake)

og_version_compute("${SRC_ROOT}" OG_VERSION_MINOR OG_GIT_HASH)

set(content "#pragma once\n")
string(APPEND content "#define OPENGLAD_GIT_HASH \"${OG_GIT_HASH}\"\n")
string(APPEND content "#define OPENGLAD_VERSION_MAJOR ${OPENGLAD_VERSION_MAJOR}\n")
string(APPEND content "#define OPENGLAD_VERSION_MINOR ${OG_VERSION_MINOR}\n")
string(APPEND content
    "#define OPENGLAD_VERSION_STRING \"${OPENGLAD_VERSION_MAJOR}.${OG_VERSION_MINOR}\"\n")
if(EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" old)
else()
    set(old "")
endif()
if(NOT "${content}" STREQUAL "${old}")
    file(WRITE "${OUT_FILE}" "${content}")
endif()
