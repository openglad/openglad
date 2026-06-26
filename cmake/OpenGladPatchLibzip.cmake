if(NOT DEFINED OPENGLAD_LIBZIP_SOURCE_DIR)
    message(FATAL_ERROR "OPENGLAD_LIBZIP_SOURCE_DIR is required")
endif()

file(READ "${OPENGLAD_LIBZIP_SOURCE_DIR}/CMakeLists.txt" OG_LIBZIP_CMAKELISTS)
string(REPLACE
    "find_package(ZLIB 1.1.2 REQUIRED)"
    "if(NOT TARGET ZLIB::ZLIB)\n  find_package(ZLIB 1.1.2 REQUIRED)\nendif()"
    OG_LIBZIP_CMAKELISTS
    "${OG_LIBZIP_CMAKELISTS}"
)
file(WRITE "${OPENGLAD_LIBZIP_SOURCE_DIR}/CMakeLists.txt" "${OG_LIBZIP_CMAKELISTS}")

if(OPENGLAD_LIBZIP_USE_FETCHED_ZLIB)
    file(READ "${OPENGLAD_LIBZIP_SOURCE_DIR}/lib/CMakeLists.txt" OG_LIBZIP_LIB_CMAKELISTS)
    string(REPLACE
        "target_link_libraries(zip PRIVATE ZLIB::ZLIB)"
        "target_link_libraries(zip PRIVATE zlibstatic)"
        OG_LIBZIP_LIB_CMAKELISTS
        "${OG_LIBZIP_LIB_CMAKELISTS}"
    )
    file(WRITE "${OPENGLAD_LIBZIP_SOURCE_DIR}/lib/CMakeLists.txt" "${OG_LIBZIP_LIB_CMAKELISTS}")
endif()
