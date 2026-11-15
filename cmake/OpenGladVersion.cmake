# The Openglad version is <major>.<commit count>: the major is the ONLY
# hand-edited number in the repository; the minor is `git rev-list --count
# HEAD` of the built commit (strictly monotonic on master, so 2.1073 names
# exactly one master commit). Unknown history — no git, a shallow clone, a
# source tarball — yields minor 0 ("2.0", never a real release) unless the
# packager passes -DOPENGLAD_COMMIT_COUNT. Both the configure-time
# project(VERSION) and the build-time og_git_hash.h stamp call
# og_version_compute(); there is no second implementation. The stamp also
# records whether this is a master build (og_version_on_master): the
# workflow's or packager's answer when given, else a commit on master's
# history built from a tree with no uncommitted changes. Only then is the
# number shown to a player (#508).
set(OPENGLAD_VERSION_MAJOR 2)

set(OPENGLAD_COMMIT_COUNT "" CACHE STRING
    "Override the commit count used as the minor version (packagers without git history)")
set(OPENGLAD_GIT_HASH "" CACHE STRING
    "Override the short commit hash stamped into the build (packagers without git history)")
set(OPENGLAD_ON_MASTER "" CACHE STRING
    "Whether this is a master build (ON/OFF), deciding alone; empty asks git")

# og_version_tree_dirty(<src_root> <out_var>)
#   1 when the tree has uncommitted changes to tracked files (`git status
#   --porcelain --untracked-files=no` prints anything), else 0; an untracked
#   file is not a change. The one test for "uncommitted changes": the "+" on
#   the hash and the master answer both ask it.
function(og_version_tree_dirty src_root out_var)
    execute_process(
        COMMAND git status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${src_root}"
        OUTPUT_VARIABLE status
        ERROR_QUIET
    )
    if("${status}" STREQUAL "")
        set(${out_var} 0 PARENT_SCOPE)
    else()
        set(${out_var} 1 PARENT_SCOPE)
    endif()
endfunction()

# og_version_compute(<src_root> <out_minor_var> <out_hash_var>)
#   minor: OPENGLAD_COMMIT_COUNT when non-empty; else `git rev-list --count HEAD`
#          unless the repository is shallow or git fails; else 0.
#   hash : OPENGLAD_GIT_HASH when non-empty; else `git rev-parse --short=8 HEAD`,
#          suffixed "+" when og_version_tree_dirty says the tree has
#          uncommitted changes; else "nogit".
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
        if(NOT hash)
            set(hash "nogit")
        else()
            og_version_tree_dirty("${src_root}" dirty)
            if(dirty)
                set(hash "${hash}+")
            endif()
        endif()
    endif()

    set(${out_minor} "${minor}" PARENT_SCOPE)
    set(${out_hash} "${hash}" PARENT_SCOPE)
endfunction()

# og_version_on_master(<src_root> <out_var>)
#   1 for a master build, else 0. Only then is the commit count a version
#   (#508): a pull-request or local branch build counts commits no one else
#   will ever have, and a tree with uncommitted changes is not the commit it
#   counts, so either shows its hash alone ("7e7f4079+" for the second).
#   OPENGLAD_ON_MASTER decides alone when non-empty: the release and web
#   workflows pass ON for a master push or a master dispatch and OFF for a
#   pull request, and a packager's ON or OFF is the packager's word.
#   Otherwise git decides: 1 when `git merge-base --is-ancestor HEAD
#   origin/master` holds and og_version_tree_dirty finds no uncommitted
#   change; anything git cannot answer (no git, a repository with no commit,
#   no origin/master, a shallow clone) is 0. GitHash.cmake calls it on every
#   build, so the stamp follows HEAD, the tree and origin/master as they move.
function(og_version_on_master src_root out_var)
    if(NOT "${OPENGLAD_ON_MASTER}" STREQUAL "")
        string(TOUPPER "${OPENGLAD_ON_MASTER}" answer)
        if(answer MATCHES "^(ON|1|TRUE|YES)$")
            set(on 1)
        elseif(answer MATCHES "^(OFF|0|FALSE|NO)$")
            set(on 0)
        else()
            message(FATAL_ERROR
                "OPENGLAD_ON_MASTER must be ON or OFF (empty asks git), got "
                "'${OPENGLAD_ON_MASTER}'")
        endif()
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
            COMMAND git merge-base --is-ancestor HEAD origin/master
            WORKING_DIRECTORY "${src_root}"
            OUTPUT_QUIET
            ERROR_QUIET
            RESULT_VARIABLE ancestor_rc
        )
        set(on 0)
        if(shallow_rc EQUAL 0 AND shallow STREQUAL "false"
           AND ancestor_rc EQUAL 0)
            og_version_tree_dirty("${src_root}" dirty)
            if(NOT dirty)
                set(on 1)
            endif()
        endif()
    endif()
    set(${out_var} "${on}" PARENT_SCOPE)
endfunction()
