#include <gtest/gtest.h>

#include <openglad/resources/physfs_api.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <list>
#include <string>

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

// og::io::og_open_write picks its backend by what PhysFS will accept: a
// VFS-relative name opens a PhysFS write handle under the current write dir,
// and a path PhysFS refuses (an absolute filesystem path) falls back to an
// unbuffered stdio FILE*. Each branch is proven by WHERE the byte landed:
// only a PhysFS-backed handle can create a file inside the write dir, and
// only the stdio fallback can create one outside it.
TEST(PhysfsWrappers, og_open_write_uses_physfs_inside_the_write_dir_and_stdio_outside)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "openglad_unit_ogfile_ctor";
    const fs::path abs_stdio = fs::temp_directory_path() / "openglad_unit_ctor_stdio.bin";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::remove(abs_stdio, ec);
    fs::create_directories(base, ec);
    ASSERT_TRUE(!ec);

    if (!og::io::physfs_init("og_unit_tests"))
    {
        ASSERT_TRUE(og::io::physfs_deinit());
        ASSERT_TRUE(og::io::physfs_init("og_unit_tests"));
    }
    ASSERT_TRUE(og::io::physfs_set_write_dir(base.string()));
    // Mounted as well as written to, so physfs_enumerate_files_sorted has a
    // search path to answer from.
    ASSERT_TRUE(og::resources::mount(base.string().c_str(), nullptr, 1));

    // (a) PhysFS branch: a relative name resolves against the write dir.
    auto physfs_file = og::io::og_open_write("unit_ctor_physfs.bin");
    ASSERT_NE(nullptr, physfs_file) << "a VFS-relative name opens a PhysFS write handle";
    const unsigned char physfs_byte = 11;
    ASSERT_TRUE(og::io::og_write_exact(*physfs_file, &physfs_byte, 1, 1));
    physfs_file.reset();

    ASSERT_TRUE(fs::exists(base / "unit_ctor_physfs.bin"))
        << "the PhysFS handle wrote INSIDE the write dir";
    EXPECT_EQ(std::uintmax_t{1}, fs::file_size(base / "unit_ctor_physfs.bin"))
        << "og_write_exact wrote exactly one byte through the PhysFS backend";
    {
        std::ifstream in(base / "unit_ctor_physfs.bin", std::ios::binary);
        ASSERT_TRUE(in.good());
        const int got = in.get();
        EXPECT_EQ(11, got) << "the byte handed to og_write_exact";
    }

    const std::list<std::string> listing = og::io::physfs_enumerate_files_sorted("");
    EXPECT_NE(listing.end(),
              std::find(listing.begin(), listing.end(), std::string("unit_ctor_physfs.bin")))
        << "the enumerator answers from the mounted search path";

    // (b) stdio fallback: PhysFS refuses an absolute filesystem path, and
    // abs_stdio lives OUTSIDE the write dir, so only stdio can have made it.
    auto stdio_file = og::io::og_open_write(abs_stdio.string().c_str());
    ASSERT_NE(nullptr, stdio_file) << "an absolute path falls back to stdio";
    const unsigned char stdio_byte = 22;
    ASSERT_TRUE(og::io::og_write_exact(*stdio_file, &stdio_byte, 1, 1));
    stdio_file.reset();

    ASSERT_TRUE(fs::exists(abs_stdio))
        << "the stdio fallback wrote OUTSIDE the PhysFS write dir";
    EXPECT_EQ(std::uintmax_t{1}, fs::file_size(abs_stdio));
    {
        std::ifstream in(abs_stdio, std::ios::binary);
        ASSERT_TRUE(in.good());
        const int got = in.get();
        EXPECT_EQ(22, got) << "the byte handed to og_write_exact";
    }

    EXPECT_TRUE(og::resources::unmount(base.string().c_str()));
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
