# Regenerated every build (cheap), rewritten only when the hash changes so
# nothing rebuilds on a no-op. Produces og_git_hash.h defining
# OPENGLAD_GIT_HASH — the short commit id the main menu stamps bottom-left,
# so a running binary can always be matched to its commit (stale-build
# confusion is a solved argument, not a debugging session).
execute_process(
    COMMAND git rev-parse --short=8 HEAD
    WORKING_DIRECTORY "${SRC_ROOT}"
    OUTPUT_VARIABLE OG_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${SRC_ROOT}"
    OUTPUT_VARIABLE OG_GIT_DIRTY
    ERROR_QUIET
)
if(NOT OG_GIT_HASH)
    set(OG_GIT_HASH "nogit")
endif()
if(NOT "${OG_GIT_DIRTY}" STREQUAL "")
    set(OG_GIT_HASH "${OG_GIT_HASH}+")
endif()
set(content "#pragma once\n#define OPENGLAD_GIT_HASH \"${OG_GIT_HASH}\"\n")
if(EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" old)
else()
    set(old "")
endif()
if(NOT "${content}" STREQUAL "${old}")
    file(WRITE "${OUT_FILE}" "${content}")
endif()
