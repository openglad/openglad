#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SRC_DIR="${PROJECT_ROOT}/src"
BUILD_DIR="${PROJECT_ROOT}/build/ci-test"
OBJ_DIR="${BUILD_DIR}/obj"
mkdir -p "${OBJ_DIR}"

if ! pkg-config --exists sdl2 SDL2_mixer; then
    echo "ERROR: Missing dependencies. Install libsdl2-dev and libsdl2-mixer-dev." >&2
    exit 1
fi

SDL_CFLAGS=($(pkg-config --cflags sdl2 SDL2_mixer))
SDL_LIBS=($(pkg-config --libs sdl2 SDL2_mixer))

COMMON_FLAGS=(
    -O2
    -DOG_PARITY_RECORDER=1
    -I"${PROJECT_ROOT}/tools"
    -I"${SRC_DIR}"
    -I"${SRC_DIR}/external/micropather"
    -I"${SRC_DIR}/external/yam"
    -I"${SRC_DIR}/external/libyaml/include"
    -I"${SRC_DIR}/external/physfs"
    -I"${SRC_DIR}/external/physfs/extras"
    -I"${SRC_DIR}/external/physfs/zlib123"
    -I"${SRC_DIR}/external/libzip"
    "${SDL_CFLAGS[@]}"
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
    -Wno-write-strings
)
CFLAGS_ARR=("${COMMON_FLAGS[@]}")
CXXFLAGS_ARR=("${COMMON_FLAGS[@]}" -std=c++17)

CC="${CC:-gcc}"
CXX="${CXX:-g++}"

CXX_SOURCES=(
    src/button.cpp
    src/effect.cpp
    src/graphlib.cpp
    src/guy.cpp
    src/help.cpp
    src/input.cpp
    src/living.cpp
    src/obmap.cpp
    src/pal32.cpp
    src/pixie.cpp
    src/pixien.cpp
    src/radar.cpp
    src/screen.cpp
    src/smooth.cpp
    src/sound.cpp
    src/stats.cpp
    src/text.cpp
    src/treasure.cpp
    src/video.cpp
    src/view.cpp
    src/walker.cpp
    src/weap.cpp
    src/sai2x.cpp
    src/util.cpp
    src/io.cpp
    src/gparser.cpp
    src/gloader.cpp
    src/pixie_data.cpp
    src/level_data.cpp
    src/save_data.cpp
    src/external/micropather/micropather.cpp
    src/external/yam/yam.cpp
    tools/parity_event_log.cpp
    tools/parity_dump_state.cpp
    tools/parity_stubs.cpp
    tools/parity_dump_master.cpp
)

LIBYAML_SOURCES=(
    src/external/libyaml/src/api.c
    src/external/libyaml/src/loader.c
    src/external/libyaml/src/parser.c
    src/external/libyaml/src/reader.c
    src/external/libyaml/src/scanner.c
    src/external/libyaml/src/emitter.c
    src/external/libyaml/src/dumper.c
    src/external/libyaml/src/writer.c
)

PHYSFS_SOURCES=(
    src/external/physfs/physfs.c
    src/external/physfs/physfs_byteorder.c
    src/external/physfs/physfs_unicode.c
    src/external/physfs/archivers/dir.c
    src/external/physfs/archivers/zip.c
    src/external/physfs/archivers/grp.c
    src/external/physfs/archivers/qpak.c
    src/external/physfs/archivers/hog.c
    src/external/physfs/archivers/mvl.c
    src/external/physfs/archivers/wad.c
    src/external/physfs/platform/posix.c
    src/external/physfs/platform/unix.c
    src/external/physfs/extras/physfsrwops.c
)

ZLIB_SOURCES=(
    src/external/physfs/zlib123/adler32.c
    src/external/physfs/zlib123/compress.c
    src/external/physfs/zlib123/crc32.c
    src/external/physfs/zlib123/deflate.c
    src/external/physfs/zlib123/inffast.c
    src/external/physfs/zlib123/inflate.c
    src/external/physfs/zlib123/inftrees.c
    src/external/physfs/zlib123/trees.c
    src/external/physfs/zlib123/zutil.c
)

LIBZIP_SOURCES=(
    src/external/libzip/zip_add.c
    src/external/libzip/zip_add_dir.c
    src/external/libzip/zip_add_entry.c
    src/external/libzip/zip_close.c
    src/external/libzip/zip_delete.c
    src/external/libzip/zip_dir_add.c
    src/external/libzip/zip_dirent.c
    src/external/libzip/zip_discard.c
    src/external/libzip/zip_entry.c
    src/external/libzip/zip_error.c
    src/external/libzip/zip_error_clear.c
    src/external/libzip/zip_error_get.c
    src/external/libzip/zip_error_get_sys_type.c
    src/external/libzip/zip_error_strerror.c
    src/external/libzip/zip_error_to_str.c
    src/external/libzip/zip_extra_field.c
    src/external/libzip/zip_extra_field_api.c
    src/external/libzip/zip_fclose.c
    src/external/libzip/zip_fdopen.c
    src/external/libzip/zip_file_add.c
    src/external/libzip/zip_file_error_clear.c
    src/external/libzip/zip_file_error_get.c
    src/external/libzip/zip_file_get_comment.c
    src/external/libzip/zip_file_get_offset.c
    src/external/libzip/zip_file_rename.c
    src/external/libzip/zip_file_replace.c
    src/external/libzip/zip_file_set_comment.c
    src/external/libzip/zip_file_strerror.c
    src/external/libzip/zip_filerange_crc.c
    src/external/libzip/zip_fopen.c
    src/external/libzip/zip_fopen_encrypted.c
    src/external/libzip/zip_fopen_index.c
    src/external/libzip/zip_fopen_index_encrypted.c
    src/external/libzip/zip_fread.c
    src/external/libzip/zip_get_archive_comment.c
    src/external/libzip/zip_get_archive_flag.c
    src/external/libzip/zip_get_compression_implementation.c
    src/external/libzip/zip_get_encryption_implementation.c
    src/external/libzip/zip_get_file_comment.c
    src/external/libzip/zip_get_name.c
    src/external/libzip/zip_get_num_entries.c
    src/external/libzip/zip_get_num_files.c
    src/external/libzip/zip_memdup.c
    src/external/libzip/zip_name_locate.c
    src/external/libzip/zip_new.c
    src/external/libzip/zip_open.c
    src/external/libzip/zip_rename.c
    src/external/libzip/zip_replace.c
    src/external/libzip/zip_set_archive_comment.c
    src/external/libzip/zip_set_archive_flag.c
    src/external/libzip/zip_set_default_password.c
    src/external/libzip/zip_set_file_comment.c
    src/external/libzip/zip_set_file_compression.c
    src/external/libzip/zip_set_name.c
    src/external/libzip/zip_source_buffer.c
    src/external/libzip/zip_source_close.c
    src/external/libzip/zip_source_crc.c
    src/external/libzip/zip_source_deflate.c
    src/external/libzip/zip_source_error.c
    src/external/libzip/zip_source_file.c
    src/external/libzip/zip_source_filep.c
    src/external/libzip/zip_source_free.c
    src/external/libzip/zip_source_function.c
    src/external/libzip/zip_source_layered.c
    src/external/libzip/zip_source_open.c
    src/external/libzip/zip_source_pkware.c
    src/external/libzip/zip_source_pop.c
    src/external/libzip/zip_source_read.c
    src/external/libzip/zip_source_stat.c
    src/external/libzip/zip_source_window.c
    src/external/libzip/zip_source_zip.c
    src/external/libzip/zip_source_zip_new.c
    src/external/libzip/zip_stat.c
    src/external/libzip/zip_stat_index.c
    src/external/libzip/zip_stat_init.c
    src/external/libzip/zip_strerror.c
    src/external/libzip/zip_string.c
    src/external/libzip/zip_unchange.c
    src/external/libzip/zip_unchange_all.c
    src/external/libzip/zip_unchange_archive.c
    src/external/libzip/zip_unchange_data.c
    src/external/libzip/zip_utf-8.c
    src/external/libzip/zip_err_str.c
    src/external/libzip/mkstemp.c
)

OBJ_FILES=()

compile_cxx() {
    local rel="$1"
    local obj="${OBJ_DIR}/${rel//\//_}.o"
    OBJ_FILES+=("${obj}")
    if [[ ! -f "${obj}" || "${PROJECT_ROOT}/${rel}" -nt "${obj}" ]]; then
        echo "  [C++] ${rel}"
        "${CXX}" "${CXXFLAGS_ARR[@]}" -c "${PROJECT_ROOT}/${rel}" -o "${obj}"
    fi
}

compile_c() {
    local rel="$1"
    local obj="${OBJ_DIR}/${rel//\//_}.o"
    OBJ_FILES+=("${obj}")
    if [[ ! -f "${obj}" || "${PROJECT_ROOT}/${rel}" -nt "${obj}" ]]; then
        echo "  [C]   ${rel}"
        "${CC}" "${CFLAGS_ARR[@]}" -c "${PROJECT_ROOT}/${rel}" -o "${obj}"
    fi
}

for src in "${CXX_SOURCES[@]}"; do compile_cxx "${src}"; done
for src in "${LIBYAML_SOURCES[@]}"; do compile_c "${src}"; done
for src in "${PHYSFS_SOURCES[@]}"; do compile_c "${src}"; done
for src in "${ZLIB_SOURCES[@]}"; do compile_c "${src}"; done
for src in "${LIBZIP_SOURCES[@]}"; do compile_c "${src}"; done

ln -sfn ../../builtin "${BUILD_DIR}/builtin"
ln -sfn ../../pix "${BUILD_DIR}/pix"
ln -sfn ../../sound "${BUILD_DIR}/sound"
ln -sfn ../../cfg "${BUILD_DIR}/cfg"
ln -sfn ../../glad.hlp "${BUILD_DIR}/glad.hlp"

echo "Linking parity_dump_master..."
"${CXX}" "${OBJ_FILES[@]}" -o "${BUILD_DIR}/parity_dump_master" \
    "${SDL_LIBS[@]}" -lm -lpthread
chmod +x "${BUILD_DIR}/parity_dump_master"
echo "Built ${BUILD_DIR}/parity_dump_master"
