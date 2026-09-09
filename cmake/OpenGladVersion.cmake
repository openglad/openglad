# The Openglad version is <major>.<commit count>: the major is the ONLY
# hand-edited number in the repository; the minor is `git rev-list --count
# HEAD` of the built commit (strictly monotonic on master, so 2.1073 names
# exactly one master commit). Unknown history — no git, a shallow clone, a
# source tarball — yields minor 0 ("2.0", never a real release) unless the
# packager passes -DOPENGLAD_COMMIT_COUNT. Both the configure-time
# project(VERSION) and the build-time og_git_hash.h stamp call
# og_version_compute(); there is no second implementation.
set(OPENGLAD_VERSION_MAJOR 2)

set(OPENGLAD_COMMIT_COUNT "" CACHE STRING
    "Override the commit count used as the minor version (packagers without git history)")
set(OPENGLAD_GIT_HASH "" CACHE STRING
    "Override the short commit hash stamped into the build (packagers without git history)")

# og_version_compute(<src_root> <out_minor_var> <out_hash_var>)
#   minor: OPENGLAD_COMMIT_COUNT when non-empty; else `git rev-list --count HEAD`
#          unless the repository is shallow or git fails; else 0.
#   hash : OPENGLAD_GIT_HASH when non-empty; else `git rev-parse --short=8 HEAD`,
#          suffixed "+" when `git status --porcelain --untracked-files=no` is
#          non-empty; else "nogit".
#
# The overrides arrive as EMPTY normal variables when the build passes
# -DOPENGLAD_COMMIT_COUNT= with nothing set, so every test here is
# "non-empty", never DEFINED.
function(og_version_compute src_root out_minor out_hash)
    if(NOT "${OPENGLAD_COMMIT_COUNT}" STREQUAL "")
        # Validated here rather than left to project(VERSION 2.abc): the
        # override lands in a cache entry and in the generated header, so a
        # typo would otherwise surface as a cryptic configure error or as a
        # #define that does not compile.
        if(NOT "${OPENGLAD_COMMIT_COUNT}" MATCHES "^[0-9]+$")
            message(FATAL_ERROR
                "OPENGLAD_COMMIT_COUNT must be a non-negative integer, got "
                "'${OPENGLAD_COMMIT_COUNT}'")
        endif()
        set(minor "${OPENGLAD_COMMIT_COUNT}")
    else()
        execute_process(
            COMMAND git rev-parse --is-shallow-repository
            WORKING_DIRECTORY "${src_root}"
            OUTPUT_VARIABLE shallow
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE shallow_rc
        )
        execute_process(
            COMMAND git rev-list --count HEAD
            WORKING_DIRECTORY "${src_root}"
            OUTPUT_VARIABLE count
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE count_rc
        )
        if(count_rc EQUAL 0 AND count MATCHES "^[0-9]+$"
           AND NOT shallow STREQUAL "true")
            set(minor "${count}")
        else()
            set(minor 0)
        endif()
    endif()

    if(NOT "${OPENGLAD_GIT_HASH}" STREQUAL "")
        set(hash "${OPENGLAD_GIT_HASH}")
    else()
        execute_process(
            COMMAND git rev-parse --short=8 HEAD
            WORKING_DIRECTORY "${src_root}"
            OUTPUT_VARIABLE hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND git status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${src_root}"
            OUTPUT_VARIABLE dirty
            ERROR_QUIET
        )
        if(NOT hash)
            set(hash "nogit")
        elseif(NOT "${dirty}" STREQUAL "")
            set(hash "${hash}+")
        endif()
    endif()

    set(${out_minor} "${minor}" PARENT_SCOPE)
    set(${out_hash} "${hash}" PARENT_SCOPE)
endfunction()
