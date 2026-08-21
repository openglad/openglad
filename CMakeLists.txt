cmake_minimum_required(VERSION 3.25)
project(OpenGlad VERSION 1.0 LANGUAGES C CXX)

# In-source builds are refused, before a single file is generated.
#
# `cmake -S . -B .` makes the build tree the WORKING tree, and from there any
# target that owns its output directory owns tracked sources instead. That is
# not hypothetical here: stage_runtime_assets MIRRORS cfg/ pix/
# sound/ packs/ into the build tree, and mirroring deletes the destination
# before it copies — in-source that reads `rm -rf <srcdir>/packs` followed by
# a copy of packs/ onto itself, i.e. unrecoverable loss of committed assets
# from a plain `ninja`. Every preset in CMakePresets.json builds out of tree;
# this guard is for the hand-typed invocation that does not.
#
# Real paths, so a symlinked or `.`-suffixed spelling of the same directory
# cannot slip past the comparison.
file(REAL_PATH "${CMAKE_SOURCE_DIR}" OG_SOURCE_DIR_REAL)
file(REAL_PATH "${CMAKE_BINARY_DIR}" OG_BINARY_DIR_REAL)
if(OG_SOURCE_DIR_REAL STREQUAL OG_BINARY_DIR_REAL)
    message(FATAL_ERROR
        "OpenGlad refuses in-source builds: the build directory is the source "
        "directory (${OG_SOURCE_DIR_REAL}).\n"
        "Targets here mirror runtime asset directories into the build tree, "
        "which deletes the destination first — in-source that would delete "
        "the repository's own cfg/, pix/, sound/ and packs/.\n"
        "Build out of tree instead, e.g.:\n"
        "    cmake --preset ci-test && cmake --build --preset ci-test\n"
        "Then remove the CMakeCache.txt and CMakeFiles/ this attempt left in "
        "the source directory.")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckCXXSourceCompiles)
include(FetchContent)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0" AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for legacy fetched dependencies")
endif()

# CMake 4.2 warns (as a dev warning, ten times over on the Windows runner)
# unless the project states which behaviour it wants here.
if(POLICY CMP0205)
    cmake_policy(SET CMP0205 NEW)
endif()

# Target C++20 (C++23 when toolchains catch up)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C standard for external C libraries
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

check_cxx_source_compiles([=[
    #include <format>
    #include <string>

    int main()
    {
        const std::string value = std::format("{} {}", "OpenGlad", 22);
        return value == "OpenGlad 22" ? 0 : 1;
    }
]=] OPENGLAD_HAS_WORKING_STD_FORMAT)

# Build options
option(BUILD_TESTING "Build the test suite" OFF)
option(BUILD_EDITOR "Build the scenario editor (openscen)" ON)
option(OPENGLAD_INSTALL_ASSETS "Install OpenGlad runtime asset directories" ON)
option(ENABLE_COVERAGE "Enable gcov/lcov coverage instrumentation (tests only)" OFF)
option(ENABLE_SANITIZERS "Enable compiler sanitizers for project targets" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis during C/C++ compilation (if found)" OFF)
option(ENABLE_CPPCHECK "Enable cppcheck analysis during C/C++ compilation (if found)" OFF)
option(VALIDATE_SERIALIZATION "Round-trip in-process transport typed messages through serializers" OFF)
option(OPENGLAD_WARNINGS_AS_ERRORS "Treat first-party compiler warnings as errors" OFF)
option(OPENGLAD_USE_SYSTEM_DEPS "Prefer system/package-manager dependencies over fetched upstream sources" ON)
option(OPENGLAD_REQUIRE_SYSTEM_DEPS "Fail if OPENGLAD_USE_SYSTEM_DEPS cannot find a dependency package" OFF)
option(OPENGLAD_FETCH_DEPS "Fetch missing upstream dependencies with CMake FetchContent" ON)
option(OPENGLAD_FETCH_ZLIB "Fetch zlib from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_LIBYAML "Fetch libyaml from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_LIBZIP "Fetch libzip from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_PHYSFS "Fetch PhysFS from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_LODEPNG "Fetch lodepng from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_IXWEBSOCKET "Fetch IXWebSocket from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
option(OPENGLAD_FETCH_SDL3 "Fetch SDL3 from upstream when no package target is available" ${OPENGLAD_FETCH_DEPS})
# Lua is ALWAYS vendored (independent of OPENGLAD_FETCH_DEPS): the scripting
# VM must be the pinned version compiled as C++ on every platform — a system
# Lua (C linkage, longjmp error unwinding) would break binding-layer RAII and
# cross-peer determinism. Offline builds pre-populate FETCHCONTENT_SOURCE_DIR_LUA.
option(OPENGLAD_FETCH_LUA "Fetch Lua from upstream (always vendored; no system fallback)" ON)
option(OPENGLAD_USE_PACKAGED_IXWEBSOCKET "Prefer an installed IXWebSocket package over fetching the pinned upstream commit" ON)
set(OPENGLAD_ZLIB_GIT_REPOSITORY "https://github.com/madler/zlib.git" CACHE STRING "zlib upstream repository")
set(OPENGLAD_ZLIB_GIT_TAG "v1.3.2" CACHE STRING "zlib upstream tag/commit to use when fetching")
set(OPENGLAD_LIBYAML_GIT_REPOSITORY "https://github.com/yaml/libyaml.git" CACHE STRING "libyaml upstream repository")
set(OPENGLAD_LIBYAML_GIT_TAG "0.2.5" CACHE STRING "libyaml upstream tag/commit to use when fetching")
set(OPENGLAD_LIBZIP_GIT_REPOSITORY "https://github.com/nih-at/libzip.git" CACHE STRING "libzip upstream repository")
set(OPENGLAD_LIBZIP_GIT_TAG "v1.11.4" CACHE STRING "libzip upstream tag/commit to use when fetching")
set(OPENGLAD_PHYSFS_GIT_REPOSITORY "https://github.com/icculus/physfs.git" CACHE STRING "PhysFS upstream repository")
set(OPENGLAD_PHYSFS_GIT_TAG "release-3.2.0" CACHE STRING "PhysFS upstream tag/commit to use when fetching")
set(OPENGLAD_LODEPNG_GIT_REPOSITORY "https://github.com/lvandeve/lodepng.git" CACHE STRING "lodepng upstream repository")
set(OPENGLAD_LODEPNG_GIT_TAG "ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a" CACHE STRING "lodepng upstream commit to use when fetching")
set(OPENGLAD_IXWEBSOCKET_GIT_REPOSITORY "https://github.com/machinezone/IXWebSocket.git" CACHE STRING "IXWebSocket upstream repository")
set(OPENGLAD_IXWEBSOCKET_GIT_TAG "64fae7676bd8fe31f7cb4bcde7a6841892dad65e" CACHE STRING "IXWebSocket upstream commit to use when fetching")
set(OPENGLAD_SDL3_GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git" CACHE STRING "SDL3 upstream repository")
set(OPENGLAD_SDL3_GIT_TAG "release-3.4.8" CACHE STRING "SDL3 upstream tag/commit to use when fetching")
set(OPENGLAD_LUA_GIT_REPOSITORY "https://github.com/lua/lua.git" CACHE STRING "Lua upstream repository")
set(OPENGLAD_LUA_GIT_TAG "v5.4.8" CACHE STRING "Lua upstream tag/commit to use when fetching")
set(SANITIZERS "address;undefined" CACHE STRING "Sanitizers to enable when ENABLE_SANITIZERS is ON")

# Coverage runs are only meaningful if the tests are built and executed.
# The repo's documented coverage pipeline configures with ENABLE_COVERAGE=ON
# and then invokes `ctest`, so ensure BUILD_TESTING is enabled in that mode.
if(ENABLE_COVERAGE AND NOT BUILD_TESTING)
    set(BUILD_TESTING ON CACHE BOOL "Build the test suite" FORCE)
endif()

# Coverage instrumentation measures whatever function/line set the compiler
# emits, so the compile flags must pin the optimization level rather than
# inherit it. Some toolchains inject an -O when the command line carries none
# (nixpkgs' cc-wrapper appends -O2 as part of its fortify hardening, so this
# repo's "Debug -g" coverage build silently became -O2 there), and at -O2 GCC
# inlines small internal functions out of existence: no standalone body means
# no gcov function record, and the local function denominator measured 4438
# where CI's plain -O0 gcc measured 4993 — with the missing records exactly
# the fully-inlined helpers and lambdas. An explicit -O0 outranks any
# injected default on every driver, so every environment measures the same
# code. Keep -O0 next to --coverage everywhere instrumentation is added; see
# scripts/coverage/README.md ("Optimization level") for the full story.
#
# Known cosmetic residue under that same wrapper: it also injects
# -D_FORTIFY_SOURCE=3 AFTER the user flags (unlike its -O2, which precedes
# them), so no -U/-D here can override it, and once -O0 wins glibc's
# features.h prints "#warning _FORTIFY_SOURCE requires compiling with
# optimization" per TU. That warning is truthful noise: fortify is inert
# without __OPTIMIZE__, so the measured code is unaffected. Environments
# that inject neither flag (CI's stock gcc) build warning-free.
set(OG_COVERAGE_COMPILE_OPTIONS --coverage -O0)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()
endif()

if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES cppcheck)
    if(CPPCHECK_EXE)
        set(CMAKE_C_CPPCHECK "${CPPCHECK_EXE};--std=c11")
        set(CMAKE_CXX_CPPCHECK "${CPPCHECK_EXE};--std=c++20")
        message(STATUS "cppcheck enabled: ${CPPCHECK_EXE}")
    else()
        message(WARNING "ENABLE_CPPCHECK is ON, but cppcheck was not found")
    endif()
endif()

# --------------------------------------------------------------------------
# Tooling targets for warnings/sanitizers (project sources only)
# --------------------------------------------------------------------------

# Every first-party target links project_warnings, and the set has to mean the
# same thing on every compiler or the gate is only as strong as its weakest
# one. -Wsign-conversion is spelled out because GCC's C++ -Wconversion does NOT
# imply it while Clang's does: leaving it implicit meant the GCC the CI gate
# runs on audited ~3200 fewer sites than the Clang/AppleClang/emscripten builds
# CI also runs. Naming it here makes both compilers audit the same thing.
add_library(project_warnings INTERFACE)
set(OG_WARNING_FLAGS
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
if(EMSCRIPTEN)
    # Emscripten's clang reports three diagnostics on SDL/port headers that the
    # game cannot act on. They have to be appended HERE, after -Wall: a target's
    # own PRIVATE options are emitted before the options it inherits from an
    # INTERFACE library, so the same flags set on the runtime targets were
    # ordered ahead of -Wall and silently re-enabled by it.
    list(APPEND OG_WARNING_FLAGS
        -Wno-constant-conversion
        -Wno-parentheses-equality
        -Wno-pointer-bool-conversion
    )
endif()
if(OPENGLAD_WARNINGS_AS_ERRORS)
    list(APPEND OG_WARNING_FLAGS -Werror)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:${OG_WARNING_FLAGS}>
        $<$<COMPILE_LANGUAGE:C>:${OG_WARNING_FLAGS}>
    )
elseif(MSVC)
    target_compile_options(project_warnings INTERFACE /W4 /permissive-)
    if(OPENGLAD_WARNINGS_AS_ERRORS)
        target_compile_options(project_warnings INTERFACE /WX)
    endif()
endif()

add_library(project_sanitizers INTERFACE)
if(ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(EMSCRIPTEN)
            message(WARNING "ENABLE_SANITIZERS is ON but EMSCRIPTEN build does not enable native sanitizers")
        else()
            string(REPLACE ";" "," SANITIZER_LIST "${SANITIZERS}")
            target_compile_options(project_sanitizers INTERFACE -fno-omit-frame-pointer -fsanitize=${SANITIZER_LIST})
            target_link_options(project_sanitizers INTERFACE -fsanitize=${SANITIZER_LIST})
        endif()
    else()
        message(WARNING "ENABLE_SANITIZERS is ON but compiler is not GCC/Clang; ignoring sanitizer flags")
    endif()
endif()

# Emscripten disables C++ exception catching by default (-fignore-exceptions):
# any `throw` calls abort(), killing the wasm runtime and freezing the page.
# The game uses exceptions for recoverable errors (networking lobby/relay
# failures surface as popup dialogs), so enable JS-based exception handling
# for every target in the web build. Wasm-native EH (-fwasm-exceptions) is
# not an option here: it is incompatible with -sASYNCIFY, which the web build
# requires for its blocking menu loops and async relay fetches.
if(EMSCRIPTEN)
    add_compile_options(-fexceptions)
    add_link_options(-fexceptions)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/OpenGladSources.cmake)

# --------------------------------------------------------------------------
# Common include directories and compile definitions
# --------------------------------------------------------------------------

set(OG_PUBLIC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

set(COMMON_DEFINITIONS
    USE_BMP_SCREENSHOT=1
    _USE_MATH_DEFINES
    OPENGLAD_VALIDATE_SERIALIZATION=$<BOOL:${VALIDATE_SERIALIZATION}>
    PHYSFS_NO_CDROM_SUPPORT=1
    PHYSFS_SUPPORTS_ZIP=1
    PHYSFS_SUPPORTS_7Z=0
    PHYSFS_SUPPORTS_GRP=0
    PHYSFS_SUPPORTS_WAD=0
    PHYSFS_SUPPORTS_HOG=0
    PHYSFS_SUPPORTS_MVL=0
    PHYSFS_SUPPORTS_QPAK=0
    PHYSFS_SUPPORTS_SLB=0
    PHYSFS_SUPPORTS_ISO9660=0
    PHYSFS_SUPPORTS_VDF=0
)

if(NOT OPENGLAD_HAS_WORKING_STD_FORMAT)
    list(APPEND COMMON_DEFINITIONS OPENGLAD_NEEDS_STD_FORMAT_SHIM=1)
endif()

set(OG_SANITIZER_TEST_ENVIRONMENT)
if(ENABLE_SANITIZERS)
    list(FIND SANITIZERS "address" OG_HAS_ASAN)
    list(FIND SANITIZERS "undefined" OG_HAS_UBSAN)
    list(FIND SANITIZERS "thread" OG_HAS_TSAN)

    if(NOT OG_HAS_ASAN EQUAL -1)
        list(APPEND OG_SANITIZER_TEST_ENVIRONMENT
            "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1")
    endif()
    if(NOT OG_HAS_UBSAN EQUAL -1)
        list(APPEND OG_SANITIZER_TEST_ENVIRONMENT
            "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
    endif()
    if(NOT OG_HAS_TSAN EQUAL -1)
        list(APPEND OG_SANITIZER_TEST_ENVIRONMENT
            "TSAN_OPTIONS=halt_on_error=1")
    endif()
endif()

# --------------------------------------------------------------------------
# Helper functions to configure targets with common settings
# --------------------------------------------------------------------------

function(configure_openglad_library target)
    # Every target that can compile src/interface/ui/picker.cpp needs the
    # generated og_git_hash.h (main-menu commit stamp) — wire the include
    # dir and the generator dependency centrally, not per-target.
    target_include_directories(${target} PRIVATE ${CMAKE_BINARY_DIR})
    if(TARGET og_git_hash_gen)
        add_dependencies(${target} og_git_hash_gen)
    endif()
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE ${OG_COVERAGE_COMPILE_OPTIONS})
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

function(configure_component_library target include_root)
    target_include_directories(${target} PRIVATE ${include_root})
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE ${OG_COVERAGE_COMPILE_OPTIONS})
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

function(configure_openglad_sdl_target target)
    if(NOT EMSCRIPTEN AND OG_NATIVE_SDL_AVAILABLE)
        # Opt-in SDL wiring: keeps gameplay-only targets free from platform deps.
        target_link_libraries(${target} PRIVATE ${OG_SDL3_TARGET})
    endif()
endfunction()

function(configure_openglad_runtime_target target)
    target_link_libraries(${target} PRIVATE project_warnings project_sanitizers)
    if(EMSCRIPTEN)
        # em++ prints "sdl3 port is still experimental [-Wexperimental]" once
        # per translation unit carrying --use-port, ~149 lines per build. It is
        # the toolchain's notice about a third-party port, not a diagnostic
        # about this code, and -Wno-experimental does NOT suppress it on the
        # pinned emsdk — measured, in both orderings relative to --use-port.
        # The flag is kept because it is the documented spelling and costs
        # nothing if a later emsdk routes the notice through -W handling; do
        # not read its presence as evidence the notice is gone. The three -Wno-
        # diagnostic flags this target used to carry now live in
        # project_warnings, where they land after -Wall and take effect.
        target_compile_options(${target} PRIVATE
            -Wno-experimental
            --use-port=sdl3
        )
        target_link_options(${target} PRIVATE
            -Wno-experimental
            --use-port=sdl3
        )
    else()
        # Native builds link SDL/system deps via og_runtime_deps, typically pulled
        # in transitively by og_game / og_game_test. Keep this function a no-op
        # on native platforms to avoid brittle link-order issues with static libs.
    endif()
endfunction()

function(configure_openglad_external_target target)
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Keep warnings actionable in OpenGlad code by silencing fetched or
        # packaged compatibility-library sources.
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

function(add_runtime_assets_dependency target)
    add_dependencies(${target} stage_runtime_assets)
endfunction()

function(prepare_component_include_root out_var component_name)
    set(root "${CMAKE_BINARY_DIR}/component_includes/${component_name}")
    set(openglad_root "${root}/openglad")
    file(REMOVE_RECURSE "${root}")
    file(MAKE_DIRECTORY "${openglad_root}")
    foreach(prefix IN LISTS ARGN)
        set(src_dir "${CMAKE_SOURCE_DIR}/include/openglad/${prefix}")
        if(NOT EXISTS "${src_dir}")
            continue()
        endif()
        set(dst_dir "${openglad_root}/${prefix}")
        if(EXISTS "${dst_dir}")
            file(REMOVE_RECURSE "${dst_dir}")
        endif()
        file(CREATE_LINK "${src_dir}" "${dst_dir}" SYMBOLIC COPY_ON_ERROR)
    endforeach()
    set(${out_var} "${root}" PARENT_SCOPE)
endfunction()

# External dependency header leak check: no package headers in public or unrelated code.
add_custom_target(check_vendor_leaks
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_vendor_leaks.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking external dependency header isolation"
    VERBATIM
)

# Render must not write sim-read entity state (the "drawcycle" class of bug:
# render-only writes freeze in the headless sim). See the script header.
add_custom_target(check_render_no_sim_writes
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_render_no_sim_writes.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking render layer does not write synced entity state"
    VERBATIM
)

# --------------------------------------------------------------------------
# Python-driven repo checks: resolve the interpreter at configure time.
#
# These targets used to spell the interpreter as a bare `python3` resolved
# from PATH at build time. MSYS2/mingw64 has no `python3` name, so on the
# Windows CI runner the og_gameplay build dependency below died with "no such
# file or directory" before compiling anything. find_package resolves a
# concrete interpreter path once, at configure time (consulting the registry
# on Windows), and every Python-running target below uses that path.
find_package(Python3 COMPONENTS Interpreter QUIET)
# The pack-Lua lint's file list is DEFINED over `git ls-files` — see
# scripts/lua_inventory.py, which refuses to fall back to a directory walk by
# design — so the per-build lint dependency additionally needs git.
find_package(Git QUIET)

# Hand every Python check target the git find_package just resolved. The
# scripts must not trust PATH alone: on the Windows CI runner the MSYS2
# shell running this configure sees git through POSIX-style PATH entries,
# while FindPython3 above resolves a NATIVE (registry) interpreter — and a
# native process spawned at BUILD time cannot turn the bare name "git" back
# into anything (WinError 2 mid-build, right after this configure's
# find_package(Git) and FetchContent clones succeeded in the same shell).
# Passing the resolved path through the environment closes the gap for all
# four Python-running targets at once; a script invoked by hand still falls
# back to PATH, and a machine with no git anywhere still gets the scripts'
# own clear refusal (lua_inventory.git_executable) instead of a traceback.
set(OG_PY_GIT_ENV "")
if(GIT_EXECUTABLE)
    set(OG_PY_GIT_ENV
        ${CMAKE_COMMAND} -E env "OG_GIT_EXECUTABLE=${GIT_EXECUTABLE}")
endif()

if(NOT Python3_Interpreter_FOUND)
    message(STATUS
        "No Python 3 interpreter found - skipping the per-build repo-check "
        "dependencies (og_gameplay -> check_lua_statement_lines, "
        "og_test_parity -> check_mutation_pins) in this build tree ONLY. "
        "The authoritative full lint still gates coverage_report (which "
        "hard-requires the interpreter and will fail loudly if invoked "
        "here), and CI's Linux lanes run all of these checks.")
elseif(NOT GIT_FOUND)
    message(STATUS
        "No git found - skipping the per-build pack-Lua lint dependency "
        "(og_gameplay -> check_lua_statement_lines) in this build tree "
        "ONLY: its file list is defined over `git ls-files`. The "
        "authoritative full lint still gates coverage_report, and CI's "
        "Linux lanes run it with git present.")
endif()

# Defines <name> to run ${Python3_EXECUTABLE} <args...> when an interpreter
# exists. When none does, the target still EXISTS — a documented target name
# must never silently vanish or turn into a no-op — but building it fails
# loudly, saying what is missing, so "did not run" can never read as
# "passed" on a machine without Python.
function(og_add_python_check_target name comment)
    if(Python3_Interpreter_FOUND)
        add_custom_target(${name}
            COMMAND ${OG_PY_GIT_ENV} ${Python3_EXECUTABLE} ${ARGN}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "${comment}"
            VERBATIM
        )
    else()
        add_custom_target(${name}
            COMMAND ${CMAKE_COMMAND} -E echo
                    "${name}: this check needs a Python 3 interpreter and none was found at configure time. Install Python 3 and reconfigure."
            COMMAND ${CMAKE_COMMAND} -E false
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "${comment}"
            VERBATIM
        )
    endif()
endfunction()

# Mutation-canary pins name a file, a LINE and the exact text to replace. Any
# insertion earlier in a pinned file silently shifts them off their anchor, and
# a pin that no longer matches mutates nothing — the canary then reports a
# clean run for a scenario it is no longer guarding. Cheap to check, invisible
# if you do not.
og_add_python_check_target(check_mutation_pins
    "Checking mutation-canary pins still anchor"
    ${CMAKE_SOURCE_DIR}/scripts/parity/check_mutation_pins.py
)

# Pack Lua: one measurable decision per line. Line coverage counts LINES, so
# anything sharing a line with something already covered shares its coverage
# point and new untested logic can be folded on at zero cost — a second
# statement (`if low then flee() end`), a second `function` (two prototypes on
# one line), or a second `and`/`or` (a whole extra condition). See the script
# header; the list it lints is scripts/lua_inventory.py, the same one the
# coverage denominator is built from.
#
# Two targets, one list, split by duty:
#   check_lua_statement_lines       --tracked-only. The per-build dependency
#       of og_gameplay: fast, stable, and junk-immune — a scratch or
#       half-written untracked .lua on a developer machine must not break
#       every ninja invocation.
#   check_lua_statement_lines_full  the whole inventory — untracked files
#       included, .glad archive members, embedded C++ literals. Gates
#       coverage_report, where an untracked shipped-Lua candidate MUST fail
#       loudly by its repository path: that is how un-`git add`-ed game logic
#       is caught before it is committed. (CI checkouts have no untracked
#       files, so the tracked-only build lint already covers everything
#       there.)
og_add_python_check_target(check_lua_statement_lines
    "Checking pack Lua holds one statement per line (tracked files)"
    ${CMAKE_SOURCE_DIR}/scripts/check_lua_statement_lines.py
    --tracked-only
)
og_add_python_check_target(check_lua_statement_lines_full
    "Checking pack Lua holds one statement per line (full inventory)"
    ${CMAKE_SOURCE_DIR}/scripts/check_lua_statement_lines.py
)

# lua-language-server --check over the workspace with the generated og.*
# stubs (scripts/check_luals.py): zero diagnostics under packs/ and
# docs/modding/. It gates coverage_report (below, with api_stub_check), but is
# deliberately NOT an og_gameplay build dependency — per-build cost stays
# flat, and the binary comes from the dev shell (flake.nix). Like every check
# target here, a machine without the tool gets a loud failure when the target
# is BUILT, never a silent pass.
find_program(OG_LUALS_EXECUTABLE lua-language-server)
if(Python3_Interpreter_FOUND AND OG_LUALS_EXECUTABLE)
    add_custom_target(check_luals
        COMMAND ${OG_PY_GIT_ENV} ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/scripts/check_luals.py
                --luals-exe ${OG_LUALS_EXECUTABLE}
                --log-dir ${CMAKE_BINARY_DIR}/luals-check
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking pack Lua types with lua-language-server (og.* stubs)"
        USES_TERMINAL
        VERBATIM
    )
else()
    if(NOT OG_LUALS_EXECUTABLE)
        set(OG_LUALS_MISSING "lua-language-server (the dev shell in flake.nix provides it)")
    else()
        set(OG_LUALS_MISSING "a Python 3 interpreter")
    endif()
    add_custom_target(check_luals
        COMMAND ${CMAKE_COMMAND} -E echo
                "check_luals: this check needs ${OG_LUALS_MISSING} and none was found at configure time. Install it and reconfigure."
        COMMAND ${CMAKE_COMMAND} -E false
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking pack Lua types with lua-language-server (og.* stubs)"
        VERBATIM
    )
endif()

# --------------------------------------------------------------------------
# Built-in campaign archives (.glad): deterministic build outputs
# --------------------------------------------------------------------------
# The eight shipped campaigns are committed as SOURCE TREES under
# campaigns/<id>/ (campaign.yaml, scen/, pix/, and — for pack-bearing
# campaigns — packs/<pack-id>/). The .glad zip the game mounts is composed
# here, at build time, by scripts/make_glad.py — a deterministic writer
# (stored members, bytewise-sorted paths, fixed timestamps), so two builds
# of the same tree produce byte-identical archives and every campaign
# change reviews as a plain file diff. The tree mirrors the archive 1:1:
# one root per campaign, no path remapping (the per-campaign README.md is
# repo documentation and is the one file make_glad.py excludes).
#
# Consumers: stage_runtime_assets hangs off og_builtin_campaigns (native),
# the emscripten `play` target preloads ${CMAKE_BINARY_DIR}/builtin, and
# the asset install rule packages the same directory.
if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR
        "OpenGlad needs a Python 3 interpreter at build time: the built-in "
        "campaign archives (builtin/*.glad) are composed from campaigns/ by "
        "scripts/make_glad.py. Install python3 and reconfigure.")
endif()

# Build-time git stamp: og_git_hash.h in the build tree, regenerated every
# build, rewritten only on change (see cmake/GitHash.cmake).
add_custom_target(og_git_hash_gen
    COMMAND ${CMAKE_COMMAND}
        -DSRC_ROOT=${CMAKE_SOURCE_DIR}
        -DOUT_FILE=${CMAKE_BINARY_DIR}/og_git_hash.h
        -P ${CMAKE_SOURCE_DIR}/cmake/GitHash.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/og_git_hash.h
    COMMENT "Stamping git hash"
)

set(OG_BUILTIN_CAMPAIGN_IDS
    concept
    gladiator
    imaginations
    longseason
    modes
    tower
    tryxian
    westlands
)
# Stamp files decouple ninja's up-to-date tracking from the archive mtime:
# make_glad.py leaves a byte-identical archive untouched, so an edit that
# does not change composed bytes never re-triggers the consumers that
# depend on the archive file (the emscripten preload link, packaging).
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/campaign_stamps")
set(OG_BUILTIN_CAMPAIGN_STAMPS "")
set(OG_BUILTIN_CAMPAIGN_ARCHIVES "")
foreach(og_campaign IN LISTS OG_BUILTIN_CAMPAIGN_IDS)
    set(og_campaign_glad "${CMAKE_BINARY_DIR}/builtin/${og_campaign}.glad")
    set(og_campaign_stamp
        "${CMAKE_BINARY_DIR}/campaign_stamps/${og_campaign}.stamp")
    set(og_campaign_root "${CMAKE_SOURCE_DIR}/campaigns/${og_campaign}")
    file(GLOB_RECURSE og_campaign_inputs CONFIGURE_DEPENDS
        "${og_campaign_root}/*")
    add_custom_command(
        OUTPUT "${og_campaign_stamp}"
        BYPRODUCTS "${og_campaign_glad}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/make_glad.py"
                "${og_campaign_glad}" --root "${og_campaign_root}"
        COMMAND ${CMAKE_COMMAND} -E touch "${og_campaign_stamp}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/make_glad.py"
                ${og_campaign_inputs}
        COMMENT "Composing builtin/${og_campaign}.glad from campaign sources"
        VERBATIM
    )
    list(APPEND OG_BUILTIN_CAMPAIGN_STAMPS "${og_campaign_stamp}")
    list(APPEND OG_BUILTIN_CAMPAIGN_ARCHIVES "${og_campaign_glad}")
endforeach()
add_custom_target(og_builtin_campaigns DEPENDS ${OG_BUILTIN_CAMPAIGN_STAMPS})

include(${CMAKE_SOURCE_DIR}/cmake/OpenGladDependencies.cmake)


if(NOT EMSCRIPTEN)
    # --- component targets (Phase 12) ---
    prepare_component_include_root(OG_CORE_INCLUDE_ROOT core
        core)
    prepare_component_include_root(OG_GAMEPLAY_INCLUDE_ROOT gameplay
        core gameplay)
    prepare_component_include_root(OG_RESOURCES_INCLUDE_ROOT resources
        core gameplay resources)
    # `server` is visible to og_interface for the staged lobby (#218): the
    # headless level pipeline + MatchStage are dual-listed into og_interface
    # (see cmake/OpenGladSources.cmake) while their headers deliberately keep
    # their include/openglad/server/ paths — the level_runtime_data.cpp
    # dual-residency precedent, documented in match_stage.h.
    prepare_component_include_root(OG_INTERFACE_INCLUDE_ROOT interface
        core gameplay resources interface server)

    add_library(og_core STATIC ${OG_CORE_SOURCES})
    configure_component_library(og_core "${OG_CORE_INCLUDE_ROOT}")

    add_library(og_gameplay STATIC ${OG_GAMEPLAY_COMPONENT_SOURCES})
    configure_component_library(og_gameplay "${OG_GAMEPLAY_INCLUDE_ROOT}")
    target_link_libraries(og_gameplay PUBLIC og_core og_ext_zlib PRIVATE og_lua)
    # og_gameplay is what runs pack Lua, and every build builds it, so this is
    # where the one-statement-per-line rule gets enforced: a branch folded
    # onto an already-covered line is invisible to the coverage gate, so it
    # has to be caught at build time instead. The dependency needs both a
    # Python 3 interpreter and git (the lint's file list is `git ls-files`;
    # OG_PY_GIT_ENV hands the script the configure-time git, which is what
    # lets this run on the MSYS2/mingw64 CI runner — registry-native Python
    # there cannot resolve the bare name "git" from the msys2 PATH). A
    # machine genuinely lacking either tool still builds the game, and the
    # authoritative full lint keeps running where the coverage gate runs
    # (the coverage_report target locally, the Linux coverage lane in CI,
    # plus this same tracked-only lint on every Linux CI build).
    if(Python3_Interpreter_FOUND AND GIT_FOUND)
        add_dependencies(og_gameplay check_lua_statement_lines)
    endif()

    add_library(og_resources STATIC ${OG_RESOURCES_COMPONENT_SOURCES})
    configure_component_library(og_resources "${OG_RESOURCES_INCLUDE_ROOT}")
    target_link_libraries(og_resources PUBLIC
        og_core
        og_gameplay
        ${OG_IO_EXTERNAL_LIBS}
    )

    add_library(og_interface STATIC ${OG_INTERFACE_COMPONENT_SOURCES})
    configure_component_library(og_interface "${OG_INTERFACE_INCLUDE_ROOT}")
    configure_openglad_sdl_target(og_interface)
    # og_interface contains src/interface/render/; enforce render stays read-only
    # on synced entity state (the drawcycle-class guard).
    add_dependencies(og_interface check_render_no_sim_writes og_git_hash_gen)
    target_include_directories(og_interface PRIVATE ${CMAKE_BINARY_DIR})
    target_link_libraries(og_interface PUBLIC
        og_core
        og_gameplay
        og_resources
    )

    if(OG_NETWORK_SOURCES)
        add_library(og_platform_ws_transport STATIC ${OG_NETWORK_SOURCES})
        configure_component_library(og_platform_ws_transport "${OG_PUBLIC_INCLUDE_DIR}")
        if(NOT EMSCRIPTEN)
            target_link_libraries(og_platform_ws_transport PUBLIC og_ext_ixwebsocket)
        endif()
        add_dependencies(og_platform_ws_transport check_vendor_leaks)
    endif()

    add_library(og_platform_sdl STATIC ${OG_PLATFORM_SDL_COMPONENT_SOURCES})
    configure_component_library(og_platform_sdl "${OG_PUBLIC_INCLUDE_DIR}")
    configure_openglad_sdl_target(og_platform_sdl)
    target_link_libraries(og_platform_sdl PUBLIC
        og_core
        og_gameplay
        og_resources
        og_interface
    )
    if(TARGET og_platform_ws_transport)
        target_link_libraries(og_platform_sdl PUBLIC og_platform_ws_transport)
    endif()
    if(NOT EMSCRIPTEN)
        target_link_libraries(og_platform_sdl PRIVATE og_ext_ixwebsocket)
    endif()
    add_dependencies(og_platform_sdl check_vendor_leaks)

    add_library(og_platform_emscripten_transport STATIC
        ${SRC_DIR}/platform/emscripten/net_transport_emscripten_ws.cpp
        ${SRC_DIR}/platform/emscripten/net_transport_relay_ws.cpp
    )
    configure_openglad_library(og_platform_emscripten_transport)
    if(BUILD_TESTING)
        target_compile_definitions(og_platform_emscripten_transport PRIVATE TESTING)
    endif()
    add_dependencies(og_platform_emscripten_transport check_vendor_leaks)

    # Aggregate target for executables/tests.
    add_library(og_game INTERFACE)
    if(CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
        target_link_libraries(og_game INTERFACE
            "$<LINK_GROUP:RESCAN,og_platform_sdl,og_interface,og_resources,og_gameplay,og_core>"
            og_runtime_deps
        )
    else()
        target_link_libraries(og_game INTERFACE
            og_platform_sdl
            og_interface
            og_resources
            og_gameplay
            og_core
            og_platform_sdl
            og_runtime_deps
        )
    endif()
    # Stage runtime assets into the build tree so tests and local runs do not
    # depend on pre-existing user data.
    #
    # MIRROR semantics: each staged directory is deleted before the copy.
    # copy_directory alone never removes files that were deleted or renamed in
    # the repo, and binaries resolve assets exe-adjacent (get_asset_path), so a
    # stale staged file silently contaminates every gate that runs a build-tree
    # binary. Mirroring makes the build-tree asset set match the repository
    # even after files are deleted or renamed.
    #
    # The rm is emitted PER DIRECTORY and only when that directory's staged
    # location is not the repository copy itself. `rm -rf X && copy X X` is
    # not a mirror, it is a delete: copy_directory fails on the source it was
    # just handed the corpse of, and the tracked files are gone. The
    # in-source-build guard at the top of this file already refuses the way
    # that happens in practice; this is the belt that holds if the guard is
    # ever bypassed or a staged path is otherwise aimed at its own source.
    # builtin/ is NOT in this list: the campaign archives are build outputs
    # composed into ${CMAKE_BINARY_DIR}/builtin by og_builtin_campaigns
    # (see "Built-in campaign archives" above), not repository files.
    set(OG_RUNTIME_ASSET_DIRS cfg pix sound packs)
    set(OG_STAGE_ASSET_COMMANDS "")
    foreach(og_asset_dir IN LISTS OG_RUNTIME_ASSET_DIRS)
        if("${OG_SOURCE_DIR_REAL}/${og_asset_dir}" STREQUAL
           "${OG_BINARY_DIR_REAL}/${og_asset_dir}")
            list(APPEND OG_STAGE_ASSET_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E echo
                    "stage_runtime_assets: ${og_asset_dir}/ already IS the staged copy -- skipping, because mirroring it would delete it")
            continue()
        endif()
        list(APPEND OG_STAGE_ASSET_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                ${CMAKE_BINARY_DIR}/${og_asset_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/${og_asset_dir}
                ${CMAKE_BINARY_DIR}/${og_asset_dir})
    endforeach()
    add_custom_target(stage_runtime_assets
        ${OG_STAGE_ASSET_COMMANDS}
        COMMENT "Staging runtime asset directories"
        VERBATIM
    )
    # Everything that runs next to staged assets also needs the built-in
    # campaign archives in ${CMAKE_BINARY_DIR}/builtin.
    add_dependencies(stage_runtime_assets og_builtin_campaigns)

    # --- openglad (main game) ---
    add_executable(openglad ${SRC_DIR}/platform/sdl/glad.cpp)
    configure_openglad_library(openglad)
    configure_openglad_sdl_target(openglad)
    target_link_libraries(openglad PRIVATE og_game)
    configure_openglad_runtime_target(openglad)
    add_runtime_assets_dependency(openglad)

    # --- openscen (level editor) ---
    if(BUILD_EDITOR)
        add_executable(openscen ${SRC_DIR}/platform/sdl/glad.cpp)
        configure_openglad_library(openscen)
        configure_openglad_sdl_target(openscen)
        target_link_libraries(openscen PRIVATE og_game)
        configure_openglad_runtime_target(openscen)
        target_compile_definitions(openscen PRIVATE OPENSCEN)
        add_runtime_assets_dependency(openscen)
    endif()

    # --- openglad_demo (multi-session demo) ---
    # Runs 24 concurrent AI-controlled game sessions in a 6x4 grid scaled to
    # fill the display, demonstrating the de-singletonized engine.
    add_executable(openglad_demo ${SRC_DIR}/platform/sdl/demo.cpp)
    configure_openglad_library(openglad_demo)
    configure_openglad_sdl_target(openglad_demo)
    target_link_libraries(openglad_demo PRIVATE og_game)
    configure_openglad_runtime_target(openglad_demo)
    add_runtime_assets_dependency(openglad_demo)

    # --- openglad_text (headless text-based simulation client) ---
    # Compiles shared game sources without SDL. Uses link-time dispatch
    # (stubs in platform_headless.cpp) instead of preprocessor guards.
    set(HEADLESS_SOURCES
        # text client
        ${SRC_DIR}/platform/text/main.cpp
        ${SRC_DIR}/platform/text/text_platform_globals.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/platform/text/text_picker.cpp
        ${SRC_DIR}/platform/text/text_protocol.cpp
        ${SRC_DIR}/interface/ui/menu_binding.cpp
        ${SRC_DIR}/interface/ui/menu_model.cpp
        ${SRC_DIR}/interface/ui/terminal_menu_model.cpp
        ${SRC_DIR}/interface/ui/picker_common.cpp
        ${SRC_DIR}/interface/ui/campaign_picker_session.cpp
        ${SRC_DIR}/interface/ui/cloud_save_client.cpp
        ${SRC_DIR}/interface/ui/picker_state.cpp
        # core
        ${OG_CORE_SOURCES}
        # gameplay (the SDL-free subset: everything but net_transport_multiplex)
        ${OG_GAMEPLAY_SIM_SOURCES}
        ${OG_GAMEPLAY_ENTITY_SOURCES}
        ${OG_GAMEPLAY_WORLD_SOURCES}
        ${OG_SCRIPT_SOURCES}
        # mapgen builder lib (tower_floor_gen in OG_RESOURCES_DATA_SOURCES
        # needs it; component-linking targets get it from og_gameplay instead)
        ${SRC_DIR}/gameplay/mapgen/builders.cpp
        ${SRC_DIR}/gameplay/mapgen/audits.cpp
        # resources
        ${OG_RESOURCES_DATA_SOURCES}
        ${OG_RESOURCES_IO_SOURCES}
        # interface (SDL-free data structs and session plumbing only)
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        # staged lobby (#218): headless level pipeline + MatchStage (the text
        # picker stages its preview locally through the same one function)
        ${SRC_DIR}/server/headless_level_hooks.cpp
        ${SRC_DIR}/server/headless_server_runtime.cpp
        ${SRC_DIR}/server/match_stage.cpp
        # platform (SDL-free subset)
        ${SRC_DIR}/platform/game_context.cpp
    )

    set(HEADLESS_SERVER_RUNTIME_SOURCES
        ${SRC_DIR}/server/headless_server_runtime.cpp
        ${SRC_DIR}/server/headless_level_hooks.cpp
        ${SRC_DIR}/server/match_stage.cpp
        ${SRC_DIR}/server/headless_tick_interval.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
    )

        set(HEADLESS_SERVER_SOURCES
            ${SRC_DIR}/server/server_main.cpp
            ${SRC_DIR}/platform/text/text_platform_globals.cpp
            ${HEADLESS_SERVER_RUNTIME_SOURCES}
        )

        set(HEADLESS_TEST_SOURCES ${HEADLESS_SOURCES})
        list(REMOVE_ITEM HEADLESS_TEST_SOURCES
            ${SRC_DIR}/platform/text/main.cpp
        )

    add_executable(openglad_text ${HEADLESS_SOURCES})
    target_include_directories(openglad_text PUBLIC
        $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
    )
    target_compile_definitions(openglad_text PRIVATE
        ${COMMON_DEFINITIONS}
    )
    target_link_libraries(openglad_text PRIVATE
        project_warnings
        project_sanitizers
        ${OG_IO_EXTERNAL_LIBS}
        m
        pthread
    )
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(openglad_text PRIVATE ${OG_COVERAGE_COMPILE_OPTIONS})
        target_link_options(openglad_text PRIVATE --coverage)
    endif()
    add_runtime_assets_dependency(openglad_text)

    if(TARGET og_platform_ws_transport)
        add_executable(openglad_server ${HEADLESS_SERVER_SOURCES})
        configure_openglad_library(openglad_server)
        target_link_libraries(openglad_server PRIVATE
            og_core
            og_gameplay
            og_resources
            og_platform_ws_transport
            m
            pthread
        )
        add_runtime_assets_dependency(openglad_server)
    endif()

    # --- concept_mapgen (Z-axis "Concept Playground" campaign generator) ---
    # SDL-free authoring tool that writes campaigns/concept/
    # (the archive is composed from that tree by og_builtin_campaigns).
    # Not part of the default build; run via scripts/generate_concept_campaign.sh.
    add_executable(concept_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/concept_mapgen/main.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        # Headless level-data hooks (C4 extraction): platform_headless.cpp
        # calls headless_level_data_hooks(), which now lives here.
        ${SRC_DIR}/server/headless_level_hooks.cpp
    )
    configure_openglad_library(concept_mapgen)
    target_link_libraries(concept_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(concept_mapgen)

    # --- imaginations_mapgen ("Imaginations" dream-log campaign generator) ---
    # SDL-free authoring tool that writes campaigns/imaginations/
    # (the archive is composed from that tree by og_builtin_campaigns).
    # Not part of the default build; run via
    # scripts/generate_imaginations_campaign.sh.
    add_executable(imaginations_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/imaginations_mapgen/main.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        # Headless level-data hooks (C4 extraction): platform_headless.cpp
        # calls headless_level_data_hooks(), which now lives here.
        ${SRC_DIR}/server/headless_level_hooks.cpp
    )
    configure_openglad_library(imaginations_mapgen)
    target_link_libraries(imaginations_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(imaginations_mapgen)

    # --- modes_mapgen ("Multiplayer Game Modes" campaign generator) ---
    # SDL-free authoring tool that writes campaigns/modes/
    # (the six-mode campaign absorbing arenas + ctf; the archive is composed
    # from that tree — packs/ subtree included — by og_builtin_campaigns)
    # and regenerates the committed level manifest lib/mode_levels.lua in
    # the campaign's hand-authored pack.
    # Not part of the default build; run via scripts/generate_modes_campaign.sh.
    add_executable(modes_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/main.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/builders_common.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/grid_canvas.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/manifest.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/levels_tdm.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/levels_ctf.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/levels_onslaught.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/levels_soccer.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/levels_basketball.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/levels_mutant.cpp
        ${CMAKE_SOURCE_DIR}/tools/modes_mapgen/levels_ffa.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        # Headless level-data hooks (C4 extraction): platform_headless.cpp
        # calls headless_level_data_hooks(), which now lives here.
        ${SRC_DIR}/server/headless_level_hooks.cpp
    )
    configure_openglad_library(modes_mapgen)
    target_link_libraries(modes_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(modes_mapgen)

    # --- westlands_mapgen ("War of the Westlands" campaign generator) ---
    # SDL-free authoring tool that writes campaigns/westlands/,
    # the 26-level story campaign built act by act (one source file per act).
    # Not part of the default build; run via scripts/generate_westlands_campaign.sh.
    add_executable(westlands_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/main.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_act1.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_act2.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_act3a.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_act3b.cpp
        ${CMAKE_SOURCE_DIR}/tools/westlands_mapgen/levels_finale.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        # Headless level-data hooks (C4 extraction): platform_headless.cpp
        # calls headless_level_data_hooks(), which now lives here.
        ${SRC_DIR}/server/headless_level_hooks.cpp
    )
    configure_openglad_library(westlands_mapgen)
    target_link_libraries(westlands_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(westlands_mapgen)

    # --- longseason_mapgen ("The Long Season" campaign generator) ---
    # SDL-free authoring tool that writes campaigns/longseason/,
    # the 19-level Brass Kettle Company story campaign built season by season
    # (one source file per season). Not part of the default build; run via
    # scripts/generate_longseason_campaign.sh.
    add_executable(longseason_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/main.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_spring.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_summer.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_autumn.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_winter.cpp
        ${CMAKE_SOURCE_DIR}/tools/longseason_mapgen/levels_reckoning.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        # Headless level-data hooks (C4 extraction): platform_headless.cpp
        # calls headless_level_data_hooks(), which now lives here.
        ${SRC_DIR}/server/headless_level_hooks.cpp
    )
    configure_openglad_library(longseason_mapgen)
    target_link_libraries(longseason_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(longseason_mapgen)

    # --- tower_mapgen ("The Endless Tower" Gate-only package generator) ---
    # SDL-free authoring tool that writes campaigns/tower/:
    # campaign.yaml (`mode: tower`) + icon + the authored Gate (scen700)
    # ONLY — generated floors are written to the user path at run time and
    # must never ship in the package (tower-triple spec D7). Not part of the
    # default build; run via scripts/generate_tower_campaign.sh.
    add_executable(tower_mapgen EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/tower_mapgen/main.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        # Headless level-data hooks (C4 extraction): platform_headless.cpp
        # calls headless_level_data_hooks(), which now lives here.
        ${SRC_DIR}/server/headless_level_hooks.cpp
    )
    configure_openglad_library(tower_mapgen)
    target_link_libraries(tower_mapgen PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(tower_mapgen)

    # --- grid_migrate (BASE + DECOR migration for hand-authored packages) ---
    # SDL-free tool that rewrites a stock campaign (campaigns/<id>/ source
    # tree) so legacy combined tiles
    # (torches/brazier/boulders/rubble) become base ground + a decor plane
    # (.fss v11), with an in-memory old-vs-new equivalence proof (passability
    # matrix, concealment, damage, door genre, entity streams, footing) that
    # exits nonzero on any mismatch. Not part of the default build; run via
    # scripts/migrate_stock_campaigns.sh.
    add_executable(grid_migrate EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/grid_migrate/main.cpp
        ${SRC_DIR}/platform/text/platform_headless.cpp
        ${SRC_DIR}/platform/text/walker_headless.cpp
        ${SRC_DIR}/interface/input/input_mappings.cpp
        ${SRC_DIR}/interface/input/input_state.cpp
        ${SRC_DIR}/platform/game_context.cpp
        ${SRC_DIR}/interface/session_state.cpp
        ${SRC_DIR}/interface/level_runtime_data.cpp
        ${SRC_DIR}/interface/platform_bridge.cpp
        # Headless level-data hooks (C4 extraction): platform_headless.cpp
        # calls headless_level_data_hooks(), which now lives here.
        ${SRC_DIR}/server/headless_level_hooks.cpp
    )
    configure_openglad_library(grid_migrate)
    target_link_libraries(grid_migrate PRIVATE
        og_core
        og_gameplay
        og_resources
        m
        pthread
    )
    add_runtime_assets_dependency(grid_migrate)

    # --- openglad_curses (zero-SDL ncurses text client) ---
    # A different renderer/input backend for the same game. Built from the same
    # SDL-free sources as the headless server (sim + resources + networking core
    # + headless platform glue) plus the shared menu model and the curses front
    # end, linking ncurses instead of SDL. See docs/ncurses-client.md.
    # The curses client implementation, minus main() and the process globals so
    # the same translation units can be reused by other targets. The curses test
    # binary supplies its own main but links the production process globals.
    set(OG_CURSES_LIB_SOURCES
        ${SRC_DIR}/platform/curses/curses_app.cpp
        ${SRC_DIR}/platform/curses/curses_terminal.cpp
        ${SRC_DIR}/platform/curses/headless_terminal.cpp
        ${SRC_DIR}/platform/curses/clock.cpp
        ${SRC_DIR}/platform/curses/glyph_map.cpp
        ${SRC_DIR}/platform/curses/kitty_keys.cpp
        ${SRC_DIR}/platform/curses/curses_input.cpp
        ${SRC_DIR}/platform/curses/curses_renderer.cpp
        ${SRC_DIR}/platform/curses/curses_picker_client.cpp
        ${SRC_DIR}/platform/curses/curses_game_runtime.cpp
        ${SRC_DIR}/platform/curses/curses_network.cpp
    )
    # Shared SDL-free menu model + business logic (also compiled into openglad_text).
    set(OG_CURSES_MENU_SOURCES
        ${SRC_DIR}/interface/ui/menu_binding.cpp
        ${SRC_DIR}/interface/ui/menu_model.cpp
        ${SRC_DIR}/interface/ui/terminal_menu_model.cpp
        ${SRC_DIR}/interface/ui/picker_common.cpp
        ${SRC_DIR}/interface/ui/campaign_picker_session.cpp
        ${SRC_DIR}/interface/ui/cloud_save_client.cpp
        ${SRC_DIR}/interface/ui/picker_state.cpp
    )
    set(OG_CURSES_SOURCES
        ${SRC_DIR}/platform/curses/main.cpp
        ${SRC_DIR}/platform/curses/curses_platform_globals.cpp
        ${OG_CURSES_LIB_SOURCES}
        ${OG_CURSES_MENU_SOURCES}
        ${HEADLESS_SERVER_RUNTIME_SOURCES}
    )

    # ncurses detection: prefer wide-char ncursesw via pkg-config, fall back to
    # narrow ncurses, then to CMake's FindCurses.
    set(OG_CURSES_FOUND FALSE)
    set(OG_CURSES_NCURSES_LINK "")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OG_NCURSESW QUIET IMPORTED_TARGET ncursesw)
        if(OG_NCURSESW_FOUND)
            set(OG_CURSES_NCURSES_LINK PkgConfig::OG_NCURSESW)
            set(OG_CURSES_FOUND TRUE)
        else()
            pkg_check_modules(OG_NCURSES QUIET IMPORTED_TARGET ncurses)
            if(OG_NCURSES_FOUND)
                set(OG_CURSES_NCURSES_LINK PkgConfig::OG_NCURSES)
                set(OG_CURSES_FOUND TRUE)
            endif()
        endif()
    endif()
    if(NOT OG_CURSES_FOUND)
        set(CURSES_NEED_NCURSES TRUE)
        find_package(Curses QUIET)
        if(CURSES_FOUND)
            set(OG_CURSES_FOUND TRUE)
        endif()
    endif()

    if(OG_CURSES_FOUND AND TARGET og_platform_ws_transport)
        add_executable(openglad_curses ${OG_CURSES_SOURCES})
        configure_openglad_library(openglad_curses)
        if(OG_CURSES_NCURSES_LINK)
            target_link_libraries(openglad_curses PRIVATE ${OG_CURSES_NCURSES_LINK})
        else()
            target_include_directories(openglad_curses PRIVATE ${CURSES_INCLUDE_DIRS})
            target_link_libraries(openglad_curses PRIVATE ${CURSES_LIBRARIES})
        endif()
        target_link_libraries(openglad_curses PRIVATE
            og_core
            og_gameplay
            og_resources
            og_platform_ws_transport
            m
            pthread
        )
        add_runtime_assets_dependency(openglad_curses)
        message(STATUS "openglad_curses: enabled (zero-SDL ncurses client)")
    else()
        message(STATUS "openglad_curses: disabled (ncurses not found or networking unavailable)")
    endif()

    install(TARGETS
        project_warnings
        project_sanitizers
        og_core
        og_gameplay
        og_resources
        og_interface
        og_platform_sdl
        og_ext_lodepng
        og_ext_yaml
        og_ext_zlib
        og_ext_libzip
        og_ext_physfs
        og_ext_ixwebsocket
        og_lua
        openglad
        EXPORT OpenGladTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(OG_ZLIB_FROM_FETCH AND TARGET ${OG_ZLIB_FETCH_TARGET})
        install(TARGETS ${OG_ZLIB_FETCH_TARGET}
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(FILES
            ${zlib_SOURCE_DIR}/zlib.h
            ${zlib_BINARY_DIR}/zconf.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()

    if(OG_LIBYAML_FROM_FETCH AND TARGET ${OG_LIBYAML_FETCH_TARGET})
        install(TARGETS ${OG_LIBYAML_FETCH_TARGET}
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(OG_LIBZIP_FROM_FETCH AND TARGET ${OG_LIBZIP_FETCH_TARGET})
        install(TARGETS ${OG_LIBZIP_FETCH_TARGET}
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(FILES
            ${libzip_SOURCE_DIR}/lib/zip.h
            ${libzip_BINARY_DIR}/zipconf.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()

    if(OG_PHYSFS_FROM_FETCH AND TARGET ${OG_PHYSFS_FETCH_TARGET})
        install(TARGETS ${OG_PHYSFS_FETCH_TARGET}
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(FILES
            ${physfs_SOURCE_DIR}/src/physfs.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/openglad/external/physfs
        )
    endif()

    if(OG_SDL3_FROM_FETCH AND TARGET SDL3-static)
        # og_interface/og_platform_sdl carry SDL3-static as a $<LINK_ONLY:>
        # dep in the export set, so the fetched target must be exported too
        # (same treatment as the other fetched static deps above), along
        # with the SDL3_Headers INTERFACE target it links.
        install(TARGETS SDL3-static SDL3_Headers
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(DIRECTORY ${sdl3_SOURCE_DIR}/include/SDL3
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()

    if(OG_LODEPNG_FROM_FETCH)
        install(FILES
            ${lodepng_SOURCE_DIR}/lodepng.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/openglad/external/lodepng
        )
    endif()

    if(OG_IXWEBSOCKET_FROM_FETCH AND TARGET ixwebsocket)
        install(TARGETS ixwebsocket
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ixwebsocket
        )
    endif()

    if(TARGET og_platform_ws_transport)
        install(TARGETS og_platform_ws_transport
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

        install(TARGETS openglad_server
            EXPORT OpenGladTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(TARGET openglad_curses)
        install(TARGETS openglad_curses
            EXPORT OpenGladTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(BUILD_EDITOR)
        install(TARGETS openscen
            EXPORT OpenGladTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(OPENGLAD_INSTALL_ASSETS)
        install(
            DIRECTORY
                ${CMAKE_SOURCE_DIR}/cfg
                ${CMAKE_SOURCE_DIR}/pix
                ${CMAKE_SOURCE_DIR}/sound
                # The campaign archives are BUILD outputs (composed from
                # campaigns/ by og_builtin_campaigns), so they install from
                # the build tree.
                ${CMAKE_BINARY_DIR}/builtin
            DESTINATION ${CMAKE_INSTALL_DATADIR}/openglad
        )
    endif()

    set(OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES "")
    if(OG_NATIVE_SDL_AVAILABLE AND OG_SDL3_FROM_PACKAGE)
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET SDL3::SDL3)
    find_dependency(SDL3 CONFIG)
endif()
_openglad_require_imported_target(\"SDL3::SDL3\" \"SDL3\")

")
    endif()
    if(OG_SYSTEM_ZLIB_TARGET)
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"find_dependency(ZLIB)
_openglad_require_imported_target(\"${OG_SYSTEM_ZLIB_TARGET}\" \"zlib\")

")
    endif()
    if(OG_SYSTEM_LIBYAML_TARGET STREQUAL "PkgConfig::OG_LIBYAML")
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET PkgConfig::OG_LIBYAML)
    find_dependency(PkgConfig)
    pkg_check_modules(OG_LIBYAML QUIET IMPORTED_TARGET yaml-0.1)
endif()
_openglad_require_imported_target(\"PkgConfig::OG_LIBYAML\" \"libyaml/yaml-0.1\")

")
    elseif(OG_SYSTEM_LIBYAML_TARGET)
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_LIBYAML_TARGET})
    find_package(yaml CONFIG QUIET)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_LIBYAML_TARGET}\" \"libyaml\")

")
    endif()
    if(OG_SYSTEM_LIBZIP_TARGET)
        if(OG_SYSTEM_LIBZIP_TARGET MATCHES "^PkgConfig::")
            string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_LIBZIP_TARGET})
    find_dependency(PkgConfig)
    pkg_check_modules(OG_LIBZIP QUIET IMPORTED_TARGET libzip)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_LIBZIP_TARGET}\" \"libzip\")

")
        else()
            string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_LIBZIP_TARGET})
    find_package(libzip CONFIG QUIET)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_LIBZIP_TARGET}\" \"libzip\")

")
        endif()
    endif()
    if(OG_SYSTEM_PHYSFS_TARGET)
        if(OG_SYSTEM_PHYSFS_TARGET MATCHES "^PkgConfig::")
            string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_PHYSFS_TARGET})
    find_dependency(PkgConfig)
    pkg_check_modules(OG_PHYSFS QUIET IMPORTED_TARGET physfs)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_PHYSFS_TARGET}\" \"PhysFS\")

")
        else()
            string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_SYSTEM_PHYSFS_TARGET})
    find_package(PhysFS CONFIG QUIET)
    find_package(physfs CONFIG QUIET)
endif()
_openglad_require_imported_target(\"${OG_SYSTEM_PHYSFS_TARGET}\" \"PhysFS\")

")
        endif()
    endif()
    if(OG_IXWEBSOCKET_FROM_PACKAGE)
        string(APPEND OG_OPENGLAD_CONFIG_FIND_DEPENDENCIES
"if(NOT TARGET ${OG_IXWEBSOCKET_TARGET})
    find_dependency(Threads)
    find_package(ixwebsocket CONFIG QUIET)
endif()
_openglad_require_imported_target(\"${OG_IXWEBSOCKET_TARGET}\" \"IXWebSocket\")

")
    endif()

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/OpenGladConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        ${CMAKE_SOURCE_DIR}/cmake/OpenGladConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/OpenGladConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenGlad
    )

    install(EXPORT OpenGladTargets
        FILE OpenGladTargets.cmake
        NAMESPACE OpenGlad::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenGlad
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/OpenGladConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/OpenGladConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenGlad
    )

    if(BUILD_TESTING)
        include(${CMAKE_SOURCE_DIR}/cmake/OpenGladTests.cmake)
    endif()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/OpenGladCoverage.cmake)

# --------------------------------------------------------------------------
# Fuzz targets (libFuzzer / clang)
# --------------------------------------------------------------------------

option(ENABLE_FUZZING "Build fuzz testing harnesses (requires clang with libFuzzer)" OFF)

if(ENABLE_FUZZING)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ENABLE_FUZZING requires Clang (for libFuzzer support)")
    endif()

    set(FUZZ_SANITIZER_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    set(FUZZ_LINK_FLAGS -fsanitize=fuzzer,address,undefined)

    # Each fuzz target is a standalone executable that links only what it needs.
    # The harnesses use SDL_IOFromConstMem to feed data, so they need SDL3 but
    # not the full game engine.

    function(add_fuzz_target name source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE
            $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
        )
        target_compile_definitions(${name} PRIVATE ${COMMON_DEFINITIONS})
        target_link_libraries(${name} PRIVATE project_warnings)
        target_compile_options(${name} PRIVATE ${FUZZ_SANITIZER_FLAGS})
        target_link_options(${name} PRIVATE ${FUZZ_LINK_FLAGS})
    endfunction()

    # fuzz_scenario: exercises .fss scenario binary parsing
    add_fuzz_target(fuzz_scenario ${SRC_DIR}/fuzz/fuzz_scenario.cpp)
    target_link_libraries(fuzz_scenario PRIVATE ${OG_SDL3_TARGET})

    # fuzz_savefile: exercises .gtl save file binary parsing
    add_fuzz_target(fuzz_savefile ${SRC_DIR}/fuzz/fuzz_savefile.cpp)
    target_link_libraries(fuzz_savefile PRIVATE ${OG_SDL3_TARGET})

endif()

# --------------------------------------------------------------------------
# Emscripten target
# --------------------------------------------------------------------------

	if(EMSCRIPTEN)
	    add_library(og_game_web STATIC ${GAME_SOURCES_NO_MAIN})
	    add_dependencies(og_game_web og_git_hash_gen)
	    target_include_directories(og_game_web PRIVATE ${CMAKE_BINARY_DIR})
	    configure_openglad_library(og_game_web)
	    configure_openglad_runtime_target(og_game_web)
	    target_link_libraries(og_game_web PUBLIC ${OG_IO_EXTERNAL_LIBS})
	    # Define __ASYNCIFY__ so that YIELD_SLEEP expands to emscripten_sleep()
	    # instead of a no-op.  The -sASYNCIFY linker flag below enables the
	    # Binaryen transform, but does not define the preprocessor macro.
	    target_compile_definitions(og_game_web PRIVATE __ASYNCIFY__)

    # web_touch_bridge.cpp compiles into the executable directly, NOT (only)
    # the og_game_web archive: nothing in C++ references its
    # EMSCRIPTEN_KEEPALIVE exports, and the linker never pulls an archive
    # member with no referenced symbols — KEEPALIVE cannot save an object
    # that is not linked at all. As a direct source its exports always land
    # in Module for the shell's touch overlay bridge.
    add_executable(play
        ${SRC_DIR}/platform/sdl/glad.cpp
        ${SRC_DIR}/platform/emscripten/web_touch_bridge.cpp
    )
    configure_openglad_library(play)
    target_link_libraries(play PRIVATE og_game_web)
    configure_openglad_runtime_target(play)
    target_compile_definitions(play PRIVATE __ASYNCIFY__)

    # Emscripten 6.0.2 flipped GROWABLE_ARRAYBUFFERS to default-on; with
    # ALLOW_MEMORY_GROWTH the heap becomes a resizable ArrayBuffer and Chrome's
    # TextDecoder.decode throws "ArrayBuffer must not be resizable" inside
    # UTF8ArrayToString (getUnsharedTextDecoderView only guards SHARED_MEMORY),
    # aborting boot. Force it off to restore pre-6.0.2 behavior; drop this once
    # upstream fixes the decode path. Guarded: the setting first appears in
    # emscripten 6.0.x and older SDKs error on unknown -s flags (CI now pins
    # emsdk 6.0.3, so this guard is active there).
    if(EMSCRIPTEN_VERSION VERSION_GREATER_EQUAL "6.0.1")
        target_link_options(play PRIVATE -sGROWABLE_ARRAYBUFFERS=0)
    endif()

    target_link_options(play PRIVATE
        "SHELL:--bind"
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sASYNCIFY
        -sASYNCIFY_STACK_SIZE=65536
        # With -fexceptions an exception that escapes every C++ handler shows
        # up as an uncaught JS exception whose message is a raw exception
        # pointer; this keeps type and what() visible in the browser console.
        -sEXCEPTION_STACK_TRACES=1
        -sEXIT_RUNTIME=0
        "SHELL:-sMINIFY_HTML=0"
        -lwebsocket.js
        -lidbfs.js
        "SHELL:-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\"]"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/cfg@/cfg"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/pix@/pix"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/sound@/sound"
        # Build output, not a repository dir: composed by og_builtin_campaigns.
        "SHELL:--preload-file ${CMAKE_BINARY_DIR}/builtin@/builtin"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/packs@/packs"
    )

    # The preload step packs ${CMAKE_BINARY_DIR}/builtin at link time, so the
    # archives must exist first, and a changed archive must relink.
    add_dependencies(play og_builtin_campaigns)
    list(JOIN OG_BUILTIN_CAMPAIGN_ARCHIVES ";" OG_PLAY_CAMPAIGN_LINK_DEPENDS)
    set_target_properties(play PROPERTIES
        LINK_DEPENDS
            "${CMAKE_SOURCE_DIR}/web/shell.html;${CMAKE_SOURCE_DIR}/web/manifest.webmanifest;${CMAKE_SOURCE_DIR}/web/icon.svg;${CMAKE_SOURCE_DIR}/web/icon-192.png;${CMAKE_SOURCE_DIR}/web/icon-512.png;${CMAKE_SOURCE_DIR}/web/apple-touch-icon.png;${OG_PLAY_CAMPAIGN_LINK_DEPENDS}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
        SUFFIX ".html"
    )

    add_custom_command(
        TARGET play
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/web/index.html
            ${CMAKE_SOURCE_DIR}/web/help.html
            ${CMAKE_SOURCE_DIR}/web/_worker.js
            ${CMAKE_SOURCE_DIR}/web/manifest.webmanifest
            ${CMAKE_SOURCE_DIR}/web/icon.svg
            ${CMAKE_SOURCE_DIR}/web/icon-192.png
            ${CMAKE_SOURCE_DIR}/web/icon-512.png
            ${CMAKE_SOURCE_DIR}/web/apple-touch-icon.png
            $<TARGET_FILE_DIR:play>
        COMMENT "Copying web landing page assets to dist/"
        VERBATIM
    )
endif()
