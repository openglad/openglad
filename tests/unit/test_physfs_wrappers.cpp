#include "unit.h"

#include <openglad/resources/physfs_api.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/og_file.h>

#include <filesystem>
#include <fstream>
#include <vector>

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

OG_UNIT_TEST(test_physfs_filesystem_wrappers_read_write_exists_enumerate)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "openglad_unit_fs_wrappers";
    std::error_code ec;
    fs::create_directories(base, ec);
    OG_ASSERT(!ec);

    if (!og::resources::init("og_unit_tests")) {
        OG_ASSERT(og::resources::deinit());
        OG_ASSERT(og::resources::init("og_unit_tests"));
    }
    OG_ASSERT(og::resources::set_write_dir(base.string()));
    OG_ASSERT(og::resources::mount(base.string(), nullptr, 1));

    const std::vector<std::uint8_t> payload{1, 2, 3, 4};
    OG_ASSERT(og::resources::write_file("alpha.bin", payload.data(), payload.size()));
    OG_ASSERT(!og::resources::write_file(nullptr, payload.data(), payload.size()));
    OG_ASSERT(!og::resources::write_file("null_data.bin", nullptr, 1));

    const std::vector<std::uint8_t> read_back = og::resources::read_file("alpha.bin");
    OG_ASSERT(read_back == payload);
    OG_ASSERT(og::resources::read_file(nullptr).empty());
    OG_ASSERT(og::resources::read_file("missing.bin").empty());

    OG_ASSERT(og::resources::exists("alpha.bin"));
    OG_ASSERT(!og::resources::exists(nullptr));

    std::ofstream(base / "zeta.bin").put('z');
    std::ofstream(base / "beta.bin").put('b');
    const std::list<std::string> names = og::resources::enumerate_files_sorted("");
    OG_ASSERT(!names.empty());
    OG_ASSERT(names.front() == "alpha.bin");

    fs::remove_all(base, ec);
}

OG_UNIT_TEST(test_physfs_filesystem_wrappers_error_helpers)
{
    if (!og::resources::init("og_unit_tests")) {
        OG_ASSERT(og::resources::deinit());
        OG_ASSERT(og::resources::init("og_unit_tests"));
    }

    // Unmounting a path that was never mounted should set a PhysFS error code.
    OG_ASSERT(!og::resources::unmount("definitely_not_mounted.zip"));
    (void)og::resources::last_error();
    (void)og::resources::last_error_code();
    (void)og::resources::last_error_is_not_mounted();
    (void)og::resources::last_error_is_files_still_open();
}
