# --------------------------------------------------------------------------
# External dependency resolution: system packages first, FetchContent as
# the fallback. See docs/external-dependencies.md for the pins.
#
# Included by the top-level CMakeLists.txt; runs in that scope.
# --------------------------------------------------------------------------

# --------------------------------------------------------------------------
# External dependency targets
# --------------------------------------------------------------------------

if(NOT EMSCRIPTEN)
    find_package(PkgConfig QUIET)
endif()

set(OG_SYSTEM_ZLIB_TARGET "")
set(OG_SYSTEM_LIBZIP_TARGET "")
set(OG_SYSTEM_LIBYAML_TARGET "")
set(OG_SYSTEM_PHYSFS_TARGET "")
set(OG_ZLIB_FROM_FETCH OFF)
set(OG_LIBYAML_FROM_FETCH OFF)
set(OG_LIBZIP_FROM_FETCH OFF)
set(OG_PHYSFS_FROM_FETCH OFF)
set(OG_LODEPNG_FROM_FETCH OFF)
set(OG_ZLIB_FETCH_TARGET "")
set(OG_LIBYAML_FETCH_TARGET "")
set(OG_LIBZIP_FETCH_TARGET "")
set(OG_PHYSFS_FETCH_TARGET "")
set(OG_IXWEBSOCKET_TARGET "")
set(OG_IXWEBSOCKET_FROM_PACKAGE OFF)
set(OG_IXWEBSOCKET_FROM_FETCH OFF)

if(OPENGLAD_USE_SYSTEM_DEPS AND NOT EMSCRIPTEN)
    find_package(ZLIB QUIET)
    if(TARGET ZLIB::ZLIB)
        set(OG_SYSTEM_ZLIB_TARGET ZLIB::ZLIB)
    endif()

    if(PkgConfig_FOUND)
        pkg_check_modules(OG_LIBZIP QUIET IMPORTED_TARGET libzip)
        if(TARGET PkgConfig::OG_LIBZIP)
            set(OG_SYSTEM_LIBZIP_TARGET PkgConfig::OG_LIBZIP)
        endif()
    endif()
    if(NOT OG_SYSTEM_LIBZIP_TARGET)
        find_package(libzip CONFIG QUIET)
        foreach(candidate IN ITEMS libzip::zip libzip::libzip zip)
            if(NOT OG_SYSTEM_LIBZIP_TARGET AND TARGET ${candidate})
                set(OG_SYSTEM_LIBZIP_TARGET ${candidate})
            endif()
        endforeach()
    endif()

    if(PkgConfig_FOUND)
        pkg_check_modules(OG_LIBYAML QUIET IMPORTED_TARGET yaml-0.1)
        if(TARGET PkgConfig::OG_LIBYAML)
            set(OG_SYSTEM_LIBYAML_TARGET PkgConfig::OG_LIBYAML)
        endif()
    endif()
    if(NOT OG_SYSTEM_LIBYAML_TARGET)
        find_package(yaml CONFIG QUIET)
        foreach(candidate IN ITEMS yaml::yaml yaml)
            if(NOT OG_SYSTEM_LIBYAML_TARGET AND TARGET ${candidate})
                set(OG_SYSTEM_LIBYAML_TARGET ${candidate})
            endif()
        endforeach()
    endif()

    find_package(PhysFS CONFIG QUIET)
    find_package(physfs CONFIG QUIET)
    foreach(candidate IN ITEMS PhysFS::PhysFS PhysFS::physfs unofficial::physfs::physfs physfs)
        if(NOT OG_SYSTEM_PHYSFS_TARGET AND TARGET ${candidate})
            set(OG_SYSTEM_PHYSFS_TARGET ${candidate})
        endif()
    endforeach()
    if(NOT OG_SYSTEM_PHYSFS_TARGET AND PkgConfig_FOUND)
        pkg_check_modules(OG_PHYSFS QUIET IMPORTED_TARGET physfs)
        if(TARGET PkgConfig::OG_PHYSFS)
            set(OG_SYSTEM_PHYSFS_TARGET PkgConfig::OG_PHYSFS)
        endif()
    endif()

    find_path(OG_SYSTEM_LODEPNG_INCLUDE_DIR NAMES lodepng.h)
    find_library(OG_SYSTEM_LODEPNG_LIBRARY NAMES lodepng)

endif()

if(OPENGLAD_REQUIRE_SYSTEM_DEPS AND NOT OPENGLAD_USE_SYSTEM_DEPS AND NOT EMSCRIPTEN)
    message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but OPENGLAD_USE_SYSTEM_DEPS is OFF")
endif()

if(OPENGLAD_REQUIRE_SYSTEM_DEPS AND OPENGLAD_USE_SYSTEM_DEPS AND NOT EMSCRIPTEN)
    if(NOT OG_SYSTEM_ZLIB_TARGET)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but zlib was not found")
    endif()
    if(NOT OG_SYSTEM_LIBZIP_TARGET)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but libzip was not found")
    endif()
    if(NOT OG_SYSTEM_LIBYAML_TARGET)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but libyaml/yaml-0.1 was not found")
    endif()
    if(NOT OG_SYSTEM_PHYSFS_TARGET)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but PhysFS was not found")
    endif()
    if(NOT OG_SYSTEM_LODEPNG_INCLUDE_DIR OR NOT OG_SYSTEM_LODEPNG_LIBRARY)
        message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but lodepng was not found")
    endif()
endif()

if(OG_SYSTEM_ZLIB_TARGET)
    add_library(og_ext_zlib INTERFACE)
    target_link_libraries(og_ext_zlib INTERFACE ${OG_SYSTEM_ZLIB_TARGET})
    message(STATUS "OpenGlad: using system zlib (${OG_SYSTEM_ZLIB_TARGET})")
elseif(OPENGLAD_FETCH_ZLIB)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "Build zlib tests" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "Build zlib shared library" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "Build zlib static library" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "Install zlib" FORCE)
    FetchContent_Declare(zlib
        SYSTEM
        GIT_REPOSITORY ${OPENGLAD_ZLIB_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_ZLIB_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(zlib)
    if(NOT TARGET zlibstatic)
        message(FATAL_ERROR "Fetched zlib did not define the expected zlibstatic target")
    endif()
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    add_library(og_ext_zlib INTERFACE)
    target_link_libraries(og_ext_zlib INTERFACE zlibstatic)
    set(OG_ZLIB_FROM_FETCH ON)
    set(OG_ZLIB_FETCH_TARGET zlibstatic)
    message(STATUS "OpenGlad: using fetched zlib ${OPENGLAD_ZLIB_GIT_TAG}")
else()
    message(FATAL_ERROR "zlib was not found. Enable OPENGLAD_FETCH_ZLIB or provide a package.")
endif()

if(OG_SYSTEM_LIBYAML_TARGET)
    add_library(og_ext_yaml INTERFACE)
    target_link_libraries(og_ext_yaml INTERFACE ${OG_SYSTEM_LIBYAML_TARGET})
    message(STATUS "OpenGlad: using system libyaml (${OG_SYSTEM_LIBYAML_TARGET})")
elseif(OPENGLAD_FETCH_LIBYAML)
    set(OG_SAVED_BUILD_TESTING "${BUILD_TESTING}")
    set(OG_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(YAML_STATIC_LIB_NAME yaml CACHE STRING "libyaml static library name" FORCE)
    FetchContent_Declare(libyaml
        SYSTEM
        GIT_REPOSITORY ${OPENGLAD_LIBYAML_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_LIBYAML_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(libyaml)
    set(BUILD_TESTING "${OG_SAVED_BUILD_TESTING}" CACHE BOOL "Build the test suite" FORCE)
    set(BUILD_SHARED_LIBS "${OG_SAVED_BUILD_SHARED_LIBS}" CACHE BOOL "Build shared libraries" FORCE)
    if(NOT TARGET yaml)
        message(FATAL_ERROR "Fetched libyaml did not define the expected yaml target")
    endif()
    add_library(og_ext_yaml INTERFACE)
    target_link_libraries(og_ext_yaml INTERFACE yaml)
    set(OG_LIBYAML_FROM_FETCH ON)
    set(OG_LIBYAML_FETCH_TARGET yaml)
    message(STATUS "OpenGlad: using fetched libyaml ${OPENGLAD_LIBYAML_GIT_TAG}")
else()
    message(FATAL_ERROR "libyaml was not found. Enable OPENGLAD_FETCH_LIBYAML or provide a package.")
endif()

if(OG_SYSTEM_LIBZIP_TARGET)
    add_library(og_ext_libzip INTERFACE)
    target_link_libraries(og_ext_libzip INTERFACE ${OG_SYSTEM_LIBZIP_TARGET})
    message(STATUS "OpenGlad: using system libzip (${OG_SYSTEM_LIBZIP_TARGET})")
elseif(OPENGLAD_FETCH_LIBZIP)
    set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "Build libzip with CommonCrypto" FORCE)
    set(ENABLE_GNUTLS OFF CACHE BOOL "Build libzip with GnuTLS" FORCE)
    set(ENABLE_MBEDTLS OFF CACHE BOOL "Build libzip with mbedTLS" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL "Build libzip with OpenSSL" FORCE)
    set(ENABLE_WINDOWS_CRYPTO OFF CACHE BOOL "Build libzip with Windows crypto" FORCE)
    set(ENABLE_BZIP2 OFF CACHE BOOL "Build libzip with bzip2" FORCE)
    set(ENABLE_LZMA OFF CACHE BOOL "Build libzip with lzma" FORCE)
    set(ENABLE_ZSTD OFF CACHE BOOL "Build libzip with zstd" FORCE)
    set(ENABLE_FDOPEN ON CACHE BOOL "Build libzip fdopen support" FORCE)
    set(BUILD_TOOLS OFF CACHE BOOL "Build libzip tools" FORCE)
    set(BUILD_REGRESS OFF CACHE BOOL "Build libzip regression tests" FORCE)
    set(BUILD_OSSFUZZ OFF CACHE BOOL "Build libzip oss-fuzz targets" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build libzip examples" FORCE)
    set(BUILD_DOC OFF CACHE BOOL "Build libzip docs" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(LIBZIP_DO_INSTALL OFF CACHE BOOL "Install libzip" FORCE)
    set(ZLIB_LINK_LIBRARY_NAME z CACHE STRING "zlib pkg-config link name" FORCE)
    FetchContent_Declare(libzip
        SYSTEM
        GIT_REPOSITORY ${OPENGLAD_LIBZIP_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_LIBZIP_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
        PATCH_COMMAND
            ${CMAKE_COMMAND}
            -DOPENGLAD_LIBZIP_SOURCE_DIR=<SOURCE_DIR>
            -DOPENGLAD_LIBZIP_USE_FETCHED_ZLIB=${OG_ZLIB_FROM_FETCH}
            -P ${CMAKE_SOURCE_DIR}/cmake/OpenGladPatchLibzip.cmake
    )
    FetchContent_MakeAvailable(libzip)
    if(NOT TARGET zip)
        message(FATAL_ERROR "Fetched libzip did not define the expected zip target")
    endif()
    add_library(og_ext_libzip INTERFACE)
    target_link_libraries(og_ext_libzip INTERFACE zip)
    set(OG_LIBZIP_FROM_FETCH ON)
    set(OG_LIBZIP_FETCH_TARGET zip)
    message(STATUS "OpenGlad: using fetched libzip ${OPENGLAD_LIBZIP_GIT_TAG}")
else()
    message(FATAL_ERROR "libzip was not found. Enable OPENGLAD_FETCH_LIBZIP or provide a package.")
endif()

if(OG_SYSTEM_PHYSFS_TARGET)
    add_library(og_ext_physfs INTERFACE)
    target_link_libraries(og_ext_physfs INTERFACE ${OG_SYSTEM_PHYSFS_TARGET})
    message(STATUS "OpenGlad: using system PhysFS (${OG_SYSTEM_PHYSFS_TARGET})")
elseif(OPENGLAD_FETCH_PHYSFS)
    set(PHYSFS_ARCHIVE_ZIP ON CACHE BOOL "Enable PhysFS ZIP support" FORCE)
    set(PHYSFS_ARCHIVE_7Z OFF CACHE BOOL "Enable PhysFS 7z support" FORCE)
    set(PHYSFS_ARCHIVE_GRP OFF CACHE BOOL "Enable PhysFS GRP support" FORCE)
    set(PHYSFS_ARCHIVE_WAD OFF CACHE BOOL "Enable PhysFS WAD support" FORCE)
    set(PHYSFS_ARCHIVE_HOG OFF CACHE BOOL "Enable PhysFS HOG support" FORCE)
    set(PHYSFS_ARCHIVE_MVL OFF CACHE BOOL "Enable PhysFS MVL support" FORCE)
    set(PHYSFS_ARCHIVE_QPAK OFF CACHE BOOL "Enable PhysFS QPAK support" FORCE)
    set(PHYSFS_ARCHIVE_SLB OFF CACHE BOOL "Enable PhysFS SLB support" FORCE)
    set(PHYSFS_ARCHIVE_ISO9660 OFF CACHE BOOL "Enable PhysFS ISO9660 support" FORCE)
    set(PHYSFS_ARCHIVE_VDF OFF CACHE BOOL "Enable PhysFS VDF support" FORCE)
    set(PHYSFS_BUILD_STATIC ON CACHE BOOL "Build PhysFS static library" FORCE)
    set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "Build PhysFS shared library" FORCE)
    set(PHYSFS_BUILD_TEST OFF CACHE BOOL "Build PhysFS test program" FORCE)
    set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "Build PhysFS docs" FORCE)
    set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "Disable PhysFS install rules" FORCE)
    set(PHYSFS_TARGETNAME_DIST physfs-dist CACHE STRING "Name of PhysFS dist build target" FORCE)
    set(PHYSFS_TARGETNAME_UNINSTALL physfs-uninstall CACHE STRING "Name of PhysFS uninstall build target" FORCE)
    FetchContent_Declare(physfs
        SYSTEM
        GIT_REPOSITORY ${OPENGLAD_PHYSFS_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_PHYSFS_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(physfs)
    if(NOT TARGET physfs-static)
        message(FATAL_ERROR "Fetched PhysFS did not define the expected physfs-static target")
    endif()
    target_include_directories(physfs-static PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/openglad/external/physfs>
    )
    add_library(og_ext_physfs INTERFACE)
    target_link_libraries(og_ext_physfs INTERFACE physfs-static)
    set(OG_PHYSFS_FROM_FETCH ON)
    set(OG_PHYSFS_FETCH_TARGET physfs-static)
    message(STATUS "OpenGlad: using fetched PhysFS ${OPENGLAD_PHYSFS_GIT_TAG}")
else()
    message(FATAL_ERROR "PhysFS was not found. Enable OPENGLAD_FETCH_PHYSFS or provide a package.")
endif()

if(OG_SYSTEM_LODEPNG_INCLUDE_DIR AND OG_SYSTEM_LODEPNG_LIBRARY)
    add_library(og_ext_lodepng INTERFACE)
    target_include_directories(og_ext_lodepng INTERFACE "${OG_SYSTEM_LODEPNG_INCLUDE_DIR}")
    target_link_libraries(og_ext_lodepng INTERFACE "${OG_SYSTEM_LODEPNG_LIBRARY}")
    message(STATUS "OpenGlad: using system lodepng (${OG_SYSTEM_LODEPNG_LIBRARY})")
elseif(OPENGLAD_FETCH_LODEPNG)
    FetchContent_Declare(lodepng
        SYSTEM
        GIT_REPOSITORY ${OPENGLAD_LODEPNG_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_LODEPNG_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(lodepng)
    FetchContent_GetProperties(lodepng)
    add_library(og_ext_lodepng STATIC "${lodepng_SOURCE_DIR}/lodepng.cpp")
    configure_openglad_external_target(og_ext_lodepng)
    target_include_directories(og_ext_lodepng PUBLIC
        $<BUILD_INTERFACE:${lodepng_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/openglad/external/lodepng>
    )
    set(OG_LODEPNG_FROM_FETCH ON)
    message(STATUS "OpenGlad: using fetched lodepng ${OPENGLAD_LODEPNG_GIT_TAG}")
else()
    message(FATAL_ERROR "lodepng was not found. Enable OPENGLAD_FETCH_LODEPNG or provide a package.")
endif()

# --- Lua 5.4 (deterministic scripting VM for class packs) ---
# Always vendored at a pinned tag: the VM must behave identically on every
# platform and peer, so a system Lua (different version/patches) is never
# acceptable. Compiled as C++ so Lua errors unwind as exceptions (RAII-safe
# in the binding layer; the web build already compiles with -fexceptions).
if(OPENGLAD_FETCH_LUA)
    FetchContent_Declare(lua
        SYSTEM
        GIT_REPOSITORY ${OPENGLAD_LUA_GIT_REPOSITORY}
        GIT_TAG ${OPENGLAD_LUA_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(lua)
    FetchContent_GetProperties(lua)
    set(OG_LUA_SOURCES
        ${lua_SOURCE_DIR}/lapi.c
        ${lua_SOURCE_DIR}/lauxlib.c
        ${lua_SOURCE_DIR}/lbaselib.c
        ${lua_SOURCE_DIR}/lcode.c
        ${lua_SOURCE_DIR}/lcorolib.c
        ${lua_SOURCE_DIR}/lctype.c
        ${lua_SOURCE_DIR}/ldblib.c
        ${lua_SOURCE_DIR}/ldebug.c
        ${lua_SOURCE_DIR}/ldo.c
        ${lua_SOURCE_DIR}/ldump.c
        ${lua_SOURCE_DIR}/lfunc.c
        ${lua_SOURCE_DIR}/lgc.c
        ${lua_SOURCE_DIR}/llex.c
        ${lua_SOURCE_DIR}/lmathlib.c
        ${lua_SOURCE_DIR}/lmem.c
        ${lua_SOURCE_DIR}/lobject.c
        ${lua_SOURCE_DIR}/lopcodes.c
        ${lua_SOURCE_DIR}/loslib.c
        ${lua_SOURCE_DIR}/lparser.c
        ${lua_SOURCE_DIR}/lstate.c
        ${lua_SOURCE_DIR}/lstring.c
        ${lua_SOURCE_DIR}/lstrlib.c
        ${lua_SOURCE_DIR}/ltable.c
        ${lua_SOURCE_DIR}/ltablib.c
        ${lua_SOURCE_DIR}/ltm.c
        ${lua_SOURCE_DIR}/lundump.c
        ${lua_SOURCE_DIR}/lutf8lib.c
        ${lua_SOURCE_DIR}/lvm.c
        ${lua_SOURCE_DIR}/lzio.c
        ${lua_SOURCE_DIR}/linit.c
        ${lua_SOURCE_DIR}/liolib.c
        ${lua_SOURCE_DIR}/loadlib.c
    )
    add_library(og_lua STATIC ${OG_LUA_SOURCES})
    configure_openglad_external_target(og_lua)
    set_source_files_properties(${OG_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
    # SYSTEM: first-party code includes lua.h, and -Wpedantic has nothing
    # useful to say about a vendored C library's headers.
    target_include_directories(og_lua SYSTEM PUBLIC
        $<BUILD_INTERFACE:${lua_SOURCE_DIR}>
    )
    # Fixed string-hash seed: table/string behavior must not vary per run or
    # per platform (class packs are part of the deterministic sim). Lua 5.4.8
    # guards its whole address-mixing default with #if !defined(luai_makeseed),
    # so this macro replaces it outright. Function-style macros cannot go
    # through COMPILE_DEFINITIONS (CMake drops them), hence the raw -D.
    target_compile_options(og_lua PRIVATE "-Dluai_makeseed(L)=0x9e3779b9u")
    message(STATUS "OpenGlad: using fetched Lua ${OPENGLAD_LUA_GIT_TAG}")
else()
    message(FATAL_ERROR "Lua is required. Enable OPENGLAD_FETCH_LUA (Lua is always vendored; system copies are not supported).")
endif()

if(NOT EMSCRIPTEN)
    set(OG_NATIVE_SDL_AVAILABLE OFF)
    set(OG_NATIVE_SDL_REQUESTED ON)
    set(OG_SDL3_TARGET "")
    set(OG_SDL3_FROM_PACKAGE OFF)
    set(OG_SDL3_FROM_FETCH OFF)
    if(CMAKE_DISABLE_FIND_PACKAGE_SDL3)
        set(OG_NATIVE_SDL_REQUESTED OFF)
    endif()
    if(OG_NATIVE_SDL_REQUESTED)
        if(OPENGLAD_USE_SYSTEM_DEPS)
            find_package(SDL3 CONFIG QUIET)
            if(TARGET SDL3::SDL3)
                set(OG_SDL3_TARGET SDL3::SDL3)
                set(OG_SDL3_FROM_PACKAGE ON)
                message(STATUS "OpenGlad: using system SDL3 (SDL3::SDL3)")
            elseif(PkgConfig_FOUND)
                pkg_check_modules(OG_SDL3 QUIET IMPORTED_TARGET sdl3)
                if(TARGET PkgConfig::OG_SDL3)
                    set(OG_SDL3_TARGET PkgConfig::OG_SDL3)
                    set(OG_SDL3_FROM_PACKAGE ON)
                    message(STATUS "OpenGlad: using system SDL3 (PkgConfig::OG_SDL3)")
                endif()
            endif()
        endif()
        if(NOT OG_SDL3_TARGET AND OPENGLAD_REQUIRE_SYSTEM_DEPS)
            message(FATAL_ERROR "OPENGLAD_REQUIRE_SYSTEM_DEPS is ON but SDL3 was not found")
        endif()
        if(NOT OG_SDL3_TARGET AND OPENGLAD_FETCH_SDL3)
            set(SDL_SHARED OFF CACHE BOOL "Build SDL3 shared library" FORCE)
            set(SDL_STATIC ON CACHE BOOL "Build SDL3 static library" FORCE)
            set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3 test library" FORCE)
            FetchContent_Declare(sdl3
                SYSTEM
                GIT_REPOSITORY ${OPENGLAD_SDL3_GIT_REPOSITORY}
                GIT_TAG ${OPENGLAD_SDL3_GIT_TAG}
                GIT_SHALLOW FALSE
                GIT_PROGRESS TRUE
            )
            FetchContent_MakeAvailable(sdl3)
            if(NOT TARGET SDL3::SDL3)
                message(FATAL_ERROR "Fetched SDL3 did not define the expected SDL3::SDL3 target")
            endif()
            set(OG_SDL3_TARGET SDL3::SDL3)
            set(OG_SDL3_FROM_FETCH ON)
            message(STATUS "OpenGlad: using fetched SDL3 ${OPENGLAD_SDL3_GIT_TAG}")
        endif()
        if(OG_SDL3_TARGET)
            set(OG_NATIVE_SDL_AVAILABLE ON)
        else()
            message(STATUS "SDL3 not found; SDL-native targets require SDL3 to build")
        endif()
    else()
        message(STATUS "SDL3 lookup disabled; configuring headless/native non-SDL targets")
    endif()

    # System/runtime link deps (kept as a target so we can control ordering).
    add_library(og_runtime_deps INTERFACE)
    if(OG_NATIVE_SDL_AVAILABLE)
        target_link_libraries(og_runtime_deps INTERFACE ${OG_SDL3_TARGET} m pthread)
    else()
        target_link_libraries(og_runtime_deps INTERFACE m pthread)
    endif()

    # Native-only WebSocket dependency. Emscripten uses the browser WebSocket API.
    if(OPENGLAD_USE_PACKAGED_IXWEBSOCKET)
        find_package(Threads QUIET)
        find_package(ixwebsocket CONFIG QUIET)
        foreach(candidate IN ITEMS ixwebsocket::ixwebsocket ixwebsocket)
            if(NOT OG_IXWEBSOCKET_TARGET AND TARGET ${candidate})
                set(OG_IXWEBSOCKET_TARGET ${candidate})
                set(OG_IXWEBSOCKET_FROM_PACKAGE ON)
            endif()
        endforeach()
    endif()

    if(NOT OG_IXWEBSOCKET_TARGET AND OPENGLAD_FETCH_IXWEBSOCKET)
        set(BUILD_DEMO OFF CACHE BOOL "Build IXWebSocket demo" FORCE)
        # TLS is required in native builds: the default relay lives on
        # Cloudflare (https:// room create + wss:// room socket). On Apple,
        # IXWebSocket's default (Secure Transport) needs no extra packages; on
        # Linux/Windows use OpenSSL (Debian/Ubuntu: libssl-dev; GitHub
        # windows-latest runners ship OpenSSL).
        set(USE_TLS ON CACHE BOOL "Build IXWebSocket with TLS" FORCE)
        if(NOT APPLE)
            set(USE_OPEN_SSL ON CACHE BOOL "Use OpenSSL for IXWebSocket TLS" FORCE)
        endif()
        set(USE_ZLIB OFF CACHE BOOL "Build IXWebSocket with zlib" FORCE)
        set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "Install IXWebSocket" FORCE)
        FetchContent_Declare(ixwebsocket
            SYSTEM
            GIT_REPOSITORY ${OPENGLAD_IXWEBSOCKET_GIT_REPOSITORY}
            GIT_TAG ${OPENGLAD_IXWEBSOCKET_GIT_TAG}
            GIT_SHALLOW FALSE
            GIT_PROGRESS TRUE
        )
        FetchContent_MakeAvailable(ixwebsocket)
        set(OG_IXWEBSOCKET_TARGET ixwebsocket)
        set(OG_IXWEBSOCKET_FROM_FETCH ON)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(ixwebsocket PRIVATE -w)
        endif()
    endif()

    if(NOT OG_IXWEBSOCKET_TARGET)
        message(FATAL_ERROR "IXWebSocket was not found. Enable OPENGLAD_FETCH_IXWEBSOCKET or provide a package at the pinned upstream commit.")
    endif()

    add_library(og_ext_ixwebsocket INTERFACE)
    target_link_libraries(og_ext_ixwebsocket INTERFACE ${OG_IXWEBSOCKET_TARGET})
    if(OG_IXWEBSOCKET_FROM_PACKAGE)
        message(STATUS "OpenGlad: using packaged IXWebSocket (${OG_IXWEBSOCKET_TARGET})")
    elseif(OG_IXWEBSOCKET_FROM_FETCH)
        message(STATUS "OpenGlad: using fetched IXWebSocket ${OPENGLAD_IXWEBSOCKET_GIT_TAG}")
    endif()
endif()

set(OG_IO_EXTERNAL_LIBS
    og_ext_yaml
    og_ext_zlib
    og_ext_physfs
    og_ext_libzip
    og_ext_lodepng
    og_lua
)
