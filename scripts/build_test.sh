#!/bin/bash
#
# Build script for test binary (headless testing)
# Outputs openglad_test binary to the project root
#
# Based on build_native.sh - same source lists, same external libs.
# Adds -DTESTING to all compiler flags and compiles test sources.
# Uses build-test/ output dir (separate from build-native/).
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# ----------------------------------------------------------------------------
# 1. Check dependencies
# ----------------------------------------------------------------------------
if ! pkg-config --exists sdl2 SDL2_mixer; then
    echo "ERROR: Missing dependencies. Install with:"
    echo "  sudo apt-get install libsdl2-dev libsdl2-mixer-dev"
    exit 1
fi

SDL_CFLAGS=$(pkg-config --cflags sdl2 SDL2_mixer)
SDL_LIBS=$(pkg-config --libs sdl2 SDL2_mixer)

echo "Building test binary..."
echo "Using SDL2: $(pkg-config --modversion sdl2)"

# ----------------------------------------------------------------------------
# 2. Setup build directory
# ----------------------------------------------------------------------------
BUILD_DIR="$PROJECT_ROOT/build-test"
mkdir -p "$BUILD_DIR"

SRC_DIR="$PROJECT_ROOT/src"
EXT_DIR="$SRC_DIR/external"
TEST_DIR="$PROJECT_ROOT/tests"

# ----------------------------------------------------------------------------
# 3. Common compiler flags (same as native + TESTING)
# ----------------------------------------------------------------------------
COMMON_FLAGS=(
    -O2 -g
    -DTESTING
    -I"$SRC_DIR"
    -I"$TEST_DIR"
    -I"$EXT_DIR/micropather"
    -I"$EXT_DIR/yam"
    -I"$EXT_DIR/libyaml/include"
    -I"$EXT_DIR/physfs"
    -I"$EXT_DIR/physfs/extras"
    -I"$EXT_DIR/physfs/zlib123"
    -I"$EXT_DIR/libzip"
    $SDL_CFLAGS
    -DUSE_BMP_SCREENSHOT=1
    -DPHYSFS_NO_CDROM_SUPPORT=1
    -DPHYSFS_SUPPORTS_ZIP=1
    -DPHYSFS_SUPPORTS_GRP=0
    -DPHYSFS_SUPPORTS_WAD=0
    -DPHYSFS_SUPPORTS_HOG=0
    -DPHYSFS_SUPPORTS_MVL=0
    -DPHYSFS_SUPPORTS_QPAK=0
    -DPHYSFS_SUPPORTS_LZMA=0
    -DYAML_DECLARE_STATIC
    -Wno-parentheses
)

CFLAGS_ARR=("${COMMON_FLAGS[@]}")
CXXFLAGS_ARR=("${COMMON_FLAGS[@]}" -std=c++11)

CC="${CC:-gcc}"
CXX="${CXX:-g++}"

# ----------------------------------------------------------------------------
# 4. Define source files (same as build_native.sh)
# ----------------------------------------------------------------------------

# Main game C++ sources (glad.cpp included - TESTING guard excludes its main())
CXX_SOURCES=(
    glad.cpp
    button.cpp
    effect.cpp
    game.cpp
    graphlib.cpp
    guy.cpp
    help.cpp
    input.cpp
    intro.cpp
    living.cpp
    obmap.cpp
    pal32.cpp
    picker.cpp
    pixie.cpp
    pixien.cpp
    radar.cpp
    screen.cpp
    smooth.cpp
    sound.cpp
    stats.cpp
    text.cpp
    treasure.cpp
    video.cpp
    view.cpp
    walker.cpp
    weap.cpp
    sai2x.cpp
    util.cpp
    io.cpp
    gparser.cpp
    gloader.cpp
    pixie_data.cpp
    level_data.cpp
    level_picker.cpp
    level_editor.cpp
    campaign_picker.cpp
    results_screen.cpp
    save_data.cpp
    test_trace.cpp
)

# External C++ sources
EXT_CXX_SOURCES=(
    external/micropather/micropather.cpp
    external/yam/yam.cpp
)

# External C sources - libyaml
LIBYAML_SOURCES=(
    external/libyaml/src/api.c
    external/libyaml/src/loader.c
    external/libyaml/src/parser.c
    external/libyaml/src/reader.c
    external/libyaml/src/scanner.c
    external/libyaml/src/emitter.c
    external/libyaml/src/dumper.c
    external/libyaml/src/writer.c
)

# External C sources - physfs
PHYSFS_SOURCES=(
    external/physfs/physfs.c
    external/physfs/physfs_byteorder.c
    external/physfs/physfs_unicode.c
    external/physfs/archivers/dir.c
    external/physfs/archivers/zip.c
    external/physfs/archivers/grp.c
    external/physfs/archivers/qpak.c
    external/physfs/archivers/hog.c
    external/physfs/archivers/mvl.c
    external/physfs/archivers/wad.c
    external/physfs/platform/posix.c
    external/physfs/platform/unix.c
    external/physfs/extras/physfsrwops.c
)

# External C sources - zlib
ZLIB_SOURCES=(
    external/physfs/zlib123/adler32.c
    external/physfs/zlib123/compress.c
    external/physfs/zlib123/crc32.c
    external/physfs/zlib123/deflate.c
    external/physfs/zlib123/inffast.c
    external/physfs/zlib123/inflate.c
    external/physfs/zlib123/inftrees.c
    external/physfs/zlib123/trees.c
    external/physfs/zlib123/zutil.c
)

# External C sources - libzip
LIBZIP_SOURCES=(
    external/libzip/zip_add.c
    external/libzip/zip_add_dir.c
    external/libzip/zip_add_entry.c
    external/libzip/zip_close.c
    external/libzip/zip_delete.c
    external/libzip/zip_dir_add.c
    external/libzip/zip_dirent.c
    external/libzip/zip_discard.c
    external/libzip/zip_entry.c
    external/libzip/zip_error.c
    external/libzip/zip_error_clear.c
    external/libzip/zip_error_get.c
    external/libzip/zip_error_get_sys_type.c
    external/libzip/zip_error_strerror.c
    external/libzip/zip_error_to_str.c
    external/libzip/zip_extra_field.c
    external/libzip/zip_extra_field_api.c
    external/libzip/zip_fclose.c
    external/libzip/zip_fdopen.c
    external/libzip/zip_file_add.c
    external/libzip/zip_file_error_clear.c
    external/libzip/zip_file_error_get.c
    external/libzip/zip_file_get_comment.c
    external/libzip/zip_file_get_offset.c
    external/libzip/zip_file_rename.c
    external/libzip/zip_file_replace.c
    external/libzip/zip_file_set_comment.c
    external/libzip/zip_file_strerror.c
    external/libzip/zip_filerange_crc.c
    external/libzip/zip_fopen.c
    external/libzip/zip_fopen_encrypted.c
    external/libzip/zip_fopen_index.c
    external/libzip/zip_fopen_index_encrypted.c
    external/libzip/zip_fread.c
    external/libzip/zip_get_archive_comment.c
    external/libzip/zip_get_archive_flag.c
    external/libzip/zip_get_compression_implementation.c
    external/libzip/zip_get_encryption_implementation.c
    external/libzip/zip_get_file_comment.c
    external/libzip/zip_get_name.c
    external/libzip/zip_get_num_entries.c
    external/libzip/zip_get_num_files.c
    external/libzip/zip_memdup.c
    external/libzip/zip_name_locate.c
    external/libzip/zip_new.c
    external/libzip/zip_open.c
    external/libzip/zip_rename.c
    external/libzip/zip_replace.c
    external/libzip/zip_set_archive_comment.c
    external/libzip/zip_set_archive_flag.c
    external/libzip/zip_set_default_password.c
    external/libzip/zip_set_file_comment.c
    external/libzip/zip_set_file_compression.c
    external/libzip/zip_set_name.c
    external/libzip/zip_source_buffer.c
    external/libzip/zip_source_close.c
    external/libzip/zip_source_crc.c
    external/libzip/zip_source_deflate.c
    external/libzip/zip_source_error.c
    external/libzip/zip_source_file.c
    external/libzip/zip_source_filep.c
    external/libzip/zip_source_free.c
    external/libzip/zip_source_function.c
    external/libzip/zip_source_layered.c
    external/libzip/zip_source_open.c
    external/libzip/zip_source_pkware.c
    external/libzip/zip_source_pop.c
    external/libzip/zip_source_read.c
    external/libzip/zip_source_stat.c
    external/libzip/zip_source_window.c
    external/libzip/zip_source_zip.c
    external/libzip/zip_source_zip_new.c
    external/libzip/zip_stat.c
    external/libzip/zip_stat_index.c
    external/libzip/zip_stat_init.c
    external/libzip/zip_strerror.c
    external/libzip/zip_string.c
    external/libzip/zip_unchange.c
    external/libzip/zip_unchange_all.c
    external/libzip/zip_unchange_archive.c
    external/libzip/zip_unchange_data.c
    external/libzip/zip_utf-8.c
    external/libzip/zip_err_str.c
    external/libzip/mkstemp.c
)

# Test sources
TEST_SOURCES=(
    test_framework.cpp
    test_main.cpp
    test_startup.cpp
    test_save_load.cpp
    test_menu.cpp
    test_game_launch.cpp
    test_level_progress.cpp
    test_back_to_mainmenu.cpp
    test_new_game.cpp
    test_hire_team.cpp
    test_train_team.cpp
    test_save_load_team.cpp
    test_view_team.cpp
    test_options_menu.cpp
    test_help.cpp
    test_load_levels.cpp
    test_difficulty.cpp
    test_guy.cpp
    test_go_no_team.cpp
    test_save_menu.cpp
)

# ----------------------------------------------------------------------------
# 5. Compile source files
# ----------------------------------------------------------------------------
echo "Compiling source files..."

OBJ_FILES=()

compile_cxx() {
    local src="$1"
    local src_dir="$2"
    local obj_name=$(basename "${src%.cpp}.o")
    local obj="$BUILD_DIR/$obj_name"
    OBJ_FILES+=("$obj")

    if [ ! -f "$obj" ] || [ "$src_dir/$src" -nt "$obj" ]; then
        echo "  [C++] $src"
        $CXX "${CXXFLAGS_ARR[@]}" -c "$src_dir/$src" -o "$obj"
    fi
}

compile_c() {
    local src="$1"
    local obj_name=$(basename "${src%.c}.o")
    local obj="$BUILD_DIR/$obj_name"
    OBJ_FILES+=("$obj")

    if [ ! -f "$obj" ] || [ "$SRC_DIR/$src" -nt "$obj" ]; then
        echo "  [C]   $src"
        $CC "${CFLAGS_ARR[@]}" -c "$SRC_DIR/$src" -o "$obj"
    fi
}

# Compile main game sources
for src in "${CXX_SOURCES[@]}"; do
    compile_cxx "$src" "$SRC_DIR"
done

# Compile external C++ sources
for src in "${EXT_CXX_SOURCES[@]}"; do
    compile_cxx "$src" "$SRC_DIR"
done

# Compile libyaml
for src in "${LIBYAML_SOURCES[@]}"; do
    compile_c "$src"
done

# Compile physfs
for src in "${PHYSFS_SOURCES[@]}"; do
    compile_c "$src"
done

# Compile zlib
for src in "${ZLIB_SOURCES[@]}"; do
    compile_c "$src"
done

# Compile libzip
for src in "${LIBZIP_SOURCES[@]}"; do
    compile_c "$src"
done

# Compile test sources
for src in "${TEST_SOURCES[@]}"; do
    compile_cxx "$src" "$TEST_DIR"
done

echo "  Compiled ${#OBJ_FILES[@]} object files"

# ----------------------------------------------------------------------------
# 6. Link
# ----------------------------------------------------------------------------
echo ""
echo "Linking openglad_test..."
$CXX "${OBJ_FILES[@]}" -o "$PROJECT_ROOT/openglad_test" $SDL_LIBS -lm -lpthread

echo ""
echo "Build complete!"
ls -lh "$PROJECT_ROOT/openglad_test"
echo ""
echo "Run with: ./openglad_test"
