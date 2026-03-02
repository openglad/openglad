#include "unit.h"

#include <openglad/resources/physfs_api.h>
#include <openglad/resources/og_file.h>

#include <filesystem>

OG_UNIT_TEST(test_physfs_wrapper_init_deinit_roundtrip_restore_state)
{
    const bool init_ok = og::io::physfs_init("og_unit_tests");
    (void)init_ok; // false is acceptable when already initialized

    OG_ASSERT(og::io::physfs_deinit());
    OG_ASSERT(og::io::physfs_init("og_unit_tests"));
}

OG_UNIT_TEST(test_og_file_physfs_and_stdio_constructor_paths)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "openglad_unit_ogfile_ctor";
    const fs::path abs_stdio = fs::temp_directory_path() / "openglad_unit_ctor_stdio.bin";
    std::error_code ec;
    fs::create_directories(base, ec);
    OG_ASSERT(!ec);

    if (!og::io::physfs_init("og_unit_tests"))
    {
        OG_ASSERT(og::io::physfs_deinit());
        OG_ASSERT(og::io::physfs_init("og_unit_tests"));
    }
    OG_ASSERT(og::io::physfs_set_write_dir(base.string()));

    auto physfs_file = og::io::og_open_write("unit_ctor_physfs.bin");
    OG_ASSERT(physfs_file != nullptr);
    if (physfs_file)
    {
        const unsigned char b = 11;
        OG_ASSERT(og::io::og_write_exact(*physfs_file, &b, 1, 1));
    }
    (void)og::io::physfs_enumerate_files_sorted("");

    // Absolute filesystem path should bypass PhysFS openWrite and hit stdio fallback.
    auto stdio_file = og::io::og_open_write(abs_stdio.string().c_str());
    OG_ASSERT(stdio_file != nullptr);
    if (stdio_file)
    {
        const unsigned char b = 22;
        OG_ASSERT(og::io::og_write_exact(*stdio_file, &b, 1, 1));
    }

    fs::remove(base / "unit_ctor_physfs.bin", ec);
    fs::remove(abs_stdio, ec);
    fs::remove_all(base, ec);
}
