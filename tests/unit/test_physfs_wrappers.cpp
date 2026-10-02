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
#include <system_error>

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

namespace {

// The process working directory for one scope, restored on every exit path
// (an ASSERT included). og_unit_data runs its tests on one thread.
struct ScopedCurrentPath
{
    explicit ScopedCurrentPath(const std::filesystem::path& dir)
        : saved(std::filesystem::current_path())
    {
        std::filesystem::current_path(dir);
    }
    ~ScopedCurrentPath()
    {
        std::error_code ec;
        std::filesystem::current_path(saved, ec);
    }
    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;
    std::filesystem::path saved;
};

// Writes one byte through `file` and closes it; false when there is no file.
bool write_one_byte(og::io::OgFilePtr file, unsigned char byte)
{
    if (!file)
        return false;
    return og::io::og_write_exact(*file, &byte, 1, 1);
}

} // namespace

// #332, the editor-save product state: PhysFS writes to the user dir (A),
// the process CWD is somewhere else (B, the install or the checkout), and a
// relative write names a parent that A lacks but B has. PhysFS refuses; the
// open must FAIL instead of landing the bytes under the CWD, where the save
// would be silently lost. The controls pin every arm where the stdio
// fallback stays: a write PhysFS accepts, an absolute path, no write dir at
// all, and a relative path that resolves under the write dir.
TEST(PhysfsWrappers, og_open_write_never_lands_a_refused_relative_path_in_the_cwd)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "openglad_unit_refused_write";
    const fs::path write_dir = root / "A";
    const fs::path cwd = root / "B";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(cwd / "nosuchdir");
    fs::create_directories(cwd / "present");
    fs::create_directories(write_dir / "present");
    fs::create_directories(write_dir / "inner" / "sub");
    struct Cleanup
    {
        fs::path root;
        ~Cleanup()
        {
            restore_unit_filesystem();
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }
    } cleanup{root};

    if (!og::io::physfs_init("og_unit_tests"))
    {
        ASSERT_TRUE(og::io::physfs_deinit());
        ASSERT_TRUE(og::io::physfs_init("og_unit_tests"));
    }
    ASSERT_TRUE(og::io::physfs_set_write_dir(write_dir.string()));

    {
        ScopedCurrentPath in_b(cwd);

        // The rule: PhysFS refused (A has no nosuchdir/), so nothing opens.
        EXPECT_FALSE(write_one_byte(og::io::og_open_write("nosuchdir/x.bin"), 1))
            << "a relative write PhysFS refused must fail, not fall back to the CWD";
        EXPECT_FALSE(fs::exists(cwd / "nosuchdir" / "x.bin"))
            << "the refused editor save landed in the process CWD";
        EXPECT_FALSE(fs::exists(write_dir / "nosuchdir"));

        // A "../" spelling that climbs out of the CWD is resolved first and
        // refused the same way: it does not resolve under the write dir.
        EXPECT_FALSE(write_one_byte(og::io::og_open_write("../B/nosuchdir/y.bin"), 2))
            << "a dot-dot relative path outside the write dir must fail";
        EXPECT_FALSE(fs::exists(cwd / "nosuchdir" / "y.bin"));

        // Control: a write PhysFS accepts lands in the write dir, not in B.
        EXPECT_TRUE(write_one_byte(og::io::og_open_write("present/x.bin"), 3));
        EXPECT_TRUE(fs::exists(write_dir / "present" / "x.bin"))
            << "PhysFS still serves a relative write whose parent exists";
        EXPECT_FALSE(fs::exists(cwd / "present" / "x.bin"));

        // Control: PhysFS rejects an absolute path; stdio writes it.
        EXPECT_TRUE(write_one_byte(og::io::og_open_write((cwd / "abs.bin").string().c_str()), 4))
            << "an absolute path keeps the stdio fallback";
        EXPECT_TRUE(fs::exists(cwd / "abs.bin"));
    }

    {
        // Control: a relative path that resolves under the write dir keeps
        // the fallback (a relative user dir, or HOME unset => "./"). With the
        // CWD at A/inner, PhysFS looks for A/sub (absent) and refuses; stdio
        // then lands the file at A/inner/sub/x.bin, which only stdio reaches.
        ScopedCurrentPath in_inner(write_dir / "inner");
        EXPECT_TRUE(write_one_byte(og::io::og_open_write("sub/x.bin"), 5))
            << "a relative path under the write dir keeps the stdio fallback";
        EXPECT_TRUE(fs::exists(write_dir / "inner" / "sub" / "x.bin"));
        EXPECT_FALSE(fs::exists(write_dir / "sub"));
    }

    // Control: PhysFS running with no write dir at all; the relative write
    // goes to the CWD, the case company.cpp's rename fallbacks handle.
    ASSERT_TRUE(og::io::physfs_deinit());
    ASSERT_TRUE(og::io::physfs_init("og_unit_tests"));
    {
        ScopedCurrentPath in_b(cwd);
        EXPECT_TRUE(write_one_byte(og::io::og_open_write("nosuchdir/z.bin"), 6))
            << "with no write dir a relative path keeps the stdio fallback";
        EXPECT_TRUE(fs::exists(cwd / "nosuchdir" / "z.bin"));
    }
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
