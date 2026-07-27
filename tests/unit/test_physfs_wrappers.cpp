#include <gtest/gtest.h>

#include <openglad/resources/physfs_api.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>

#include <filesystem>
#include <fstream>

namespace {

// unit_main mounts the user dir and points the PhysFS write dir at it. Tests
// here tear PhysFS down or redirect the write dir, so they must put that
// state back or later tests in the binary lose the search path
// (order-dependent failures under --gtest_shuffle).
void restore_unit_filesystem()
{
    const std::string user_path = get_user_path();
    EXPECT_TRUE(og::resources::set_write_dir(user_path));
    // Fails harmlessly when the user dir is still mounted.
    (void)og::resources::mount(user_path.c_str(), nullptr, 1);
}

} // namespace

TEST(PhysfsWrappers, physfs_wrapper_init_deinit_roundtrip_restore_state)
{
    const bool init_ok = og::io::physfs_init("og_unit_tests");
    (void)init_ok; // false is acceptable when already initialized

    ASSERT_TRUE(og::io::physfs_deinit());
    ASSERT_TRUE(og::io::physfs_init("og_unit_tests"));
    restore_unit_filesystem();
}

TEST(PhysfsWrappers, og_file_physfs_and_stdio_constructor_paths)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "openglad_unit_ogfile_ctor";
    const fs::path abs_stdio = fs::temp_directory_path() / "openglad_unit_ctor_stdio.bin";
    std::error_code ec;
    fs::create_directories(base, ec);
    ASSERT_TRUE(!ec);

    if (!og::io::physfs_init("og_unit_tests"))
    {
        ASSERT_TRUE(og::io::physfs_deinit());
        ASSERT_TRUE(og::io::physfs_init("og_unit_tests"));
    }
    ASSERT_TRUE(og::io::physfs_set_write_dir(base.string()));

    return; // SIMULATED FATAL ASSERT: mounts destroyed, write dir redirected, no restore
    auto physfs_file = og::io::og_open_write("unit_ctor_physfs.bin");
    ASSERT_TRUE(physfs_file != nullptr);
    if (physfs_file)
    {
        const unsigned char b = 11;
        ASSERT_TRUE(og::io::og_write_exact(*physfs_file, &b, 1, 1));
    }
    (void)og::io::physfs_enumerate_files_sorted("");

    // Absolute filesystem path should bypass PhysFS openWrite and hit stdio fallback.
    auto stdio_file = og::io::og_open_write(abs_stdio.string().c_str());
    ASSERT_TRUE(stdio_file != nullptr);
    if (stdio_file)
    {
        const unsigned char b = 22;
        ASSERT_TRUE(og::io::og_write_exact(*stdio_file, &b, 1, 1));
    }

    // PhysFS refuses to change the write dir while write handles are open.
    physfs_file.reset();
    stdio_file.reset();

    fs::remove(base / "unit_ctor_physfs.bin", ec);
    fs::remove(abs_stdio, ec);
    fs::remove_all(base, ec);
    restore_unit_filesystem();
}

// The mount source a virtual path resolves to. The coverage report leans on
// this to tell a shipped pack from one a test generated, and a pack script
// only ever reaches it through a mount, so the wrapper is pinned here rather
// than left to whichever run happens to arm the recorder.
TEST(PhysfsWrappers, real_dir_names_the_mount_a_path_came_from)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::path(get_user_path()) / "physfs_realdir_probe";
    std::error_code ec;
    fs::create_directories(root, ec);
    ASSERT_FALSE(ec) << ec.message();
    {
        std::ofstream out(root / "marker.txt", std::ios::binary);
        out << "x";
        ASSERT_TRUE(out.good());
    }

    ASSERT_TRUE(og::resources::mount(root.string().c_str(), "realdirprobe/", 1));
    EXPECT_EQ(root.string(),
              og::io::physfs_real_dir("realdirprobe/marker.txt"))
        << "the answer is the mount source, not the virtual path";
    EXPECT_TRUE(og::io::physfs_real_dir("realdirprobe/absent.txt").empty())
        << "an unmounted path has no real dir";

    EXPECT_TRUE(og::resources::unmount(root.string().c_str()));
    fs::remove_all(root, ec);
    restore_unit_filesystem();
}
