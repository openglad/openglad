#include <openglad/resources/zip_api.h>
#include <openglad/resources/io.h>
#include <openglad/resources/io_common.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

// Scratch lives under the user dir's temp/, spelled absolute (the
// test_io_platform_coverage.cpp user_temp() pattern), never relative to the
// checkout the binary runs from. And in a subdirectory of its OWN, removed
// when the test ends: <user>/temp/ is also the campaign unpack dir
// (cleanup_unpacked_campaign() remove_all's it, repack_campaign() zips all
// of it), so scratch left loose there rides into the next editor save made
// in the same binary.
fs::path zip_r15_root()
{
    return fs::path(get_user_path()) / "temp" / "zip_r15";
}

// One test's scratch dir, <user>/temp/zip_r15/<tag>, fresh on entry and
// removed (with zip_r15/ itself) on every exit path, ASSERT included.
class R15Dir
{
public:
    explicit R15Dir(const std::string& tag) : path_(zip_r15_root() / tag)
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }
    ~R15Dir()
    {
        std::error_code ec;
        fs::remove_all(zip_r15_root(), ec);
    }
    R15Dir(const R15Dir&) = delete;
    R15Dir& operator=(const R15Dir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

} // namespace

TEST(ZipApi, r15_zip_and_unzip_success_paths)
{
    const R15Dir scratch("ok");
    const fs::path base = scratch.path();
    const fs::path in = base / "in";
    const fs::path out = base / "out";
    const fs::path zipfile = base / "archive.zip";

    std::error_code ec;
    fs::create_directories(in / "subdir" / "emptydir", ec);
    fs::create_directories(in / "subdir2", ec);
    {
        std::ofstream f1((in / "root.txt").string(), std::ios::binary);
        f1 << "root-r15";
    }
    {
        std::ofstream f2((in / "subdir" / "nested.txt").string(), std::ios::binary);
        f2 << "nested-r15";
    }

    const ArchiveIoError zip_err = og::io::zip_contents_with_error(in.string(), zipfile.string());
    ASSERT_EQ(ArchiveIoError::None, zip_err);

    const ArchiveIoError unzip_err = og::io::unzip_into_with_error(zipfile.string(), out.string());
    ASSERT_EQ(ArchiveIoError::None, unzip_err);

    ASSERT_TRUE(fs::exists(out / "root.txt"));
    ASSERT_TRUE(fs::exists(out / "subdir" / "nested.txt"));
}

TEST(ZipApi, r15_error_paths_for_open_archive_and_output)
{
    const R15Dir scratch("errors");
    const fs::path base = scratch.path();
    const fs::path in = base / "in";
    const fs::path zipfile = base / "archive.zip";
    std::error_code ec;
    fs::create_directories(in, ec);
    {
        std::ofstream f((in / "a.txt").string(), std::ios::binary);
        f << "a";
    }

    // Directory as output archive path -> open archive failure.
    const ArchiveIoError zip_open_fail = og::io::zip_contents_with_error(in.string(), in.string());
    ASSERT_TRUE(zip_open_fail == ArchiveIoError::OpenArchiveFailed);

    ASSERT_TRUE(og::io::zip_contents_with_error(in.string(), zipfile.string()) == ArchiveIoError::None);

    // Missing archive -> open archive failure.
    const ArchiveIoError missing = og::io::unzip_into_with_error((base / "missing.zip").string(), (base / "missing_out").string());
    ASSERT_TRUE(missing == ArchiveIoError::OpenArchiveFailed);

    // Output file blocks extracted file creation -> open output failure.
    const fs::path blocked = base / "blocked";
    {
        std::ofstream out_file(blocked.string(), std::ios::binary);
        out_file << "block";
    }
    ASSERT_TRUE(og::io::unzip_into_with_error(zipfile.string(), blocked.string()) == ArchiveIoError::OpenOutputFailed);
}

namespace {

std::uint32_t crc32_of(const std::string& bytes)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char ch : bytes)
    {
        crc ^= static_cast<unsigned char>(ch);
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void put16(std::string& out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

void put32(std::string& out, std::uint32_t v)
{
    put16(out, v & 0xFFFFu);
    put16(out, (v >> 16) & 0xFFFFu);
}

// A minimal STORED zip (no compression, no extra fields) whose entries are
// written verbatim — including names libzip's own writer would never emit.
std::string stored_zip(
    const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::string body;
    std::string central;
    for (const auto& [name, data] : entries)
    {
        const auto offset = static_cast<std::uint32_t>(body.size());
        const std::uint32_t crc = crc32_of(data);
        const auto size = static_cast<std::uint32_t>(data.size());
        const auto name_len = static_cast<std::uint32_t>(name.size());
        put32(body, 0x04034b50u);
        put16(body, 20); put16(body, 0); put16(body, 0);  // version, flags, stored
        put16(body, 0); put16(body, 0x21);                // time, date
        put32(body, crc); put32(body, size); put32(body, size);
        put16(body, name_len); put16(body, 0);
        body += name;
        body += data;

        put32(central, 0x02014b50u);
        put16(central, 20); put16(central, 20); put16(central, 0);
        put16(central, 0); put16(central, 0); put16(central, 0x21);
        put32(central, crc); put32(central, size); put32(central, size);
        put16(central, name_len); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0);
        put32(central, offset);
        central += name;
    }
    std::string out = body + central;
    put32(out, 0x06054b50u);
    put16(out, 0); put16(out, 0);
    put16(out, static_cast<std::uint32_t>(entries.size()));
    put16(out, static_cast<std::uint32_t>(entries.size()));
    put32(out, static_cast<std::uint32_t>(central.size()));
    put32(out, static_cast<std::uint32_t>(body.size()));
    put16(out, 0);
    return out;
}

} // namespace

// An archive entry with an EMPTY name names no file: extraction skips it and
// carries on, so the rest of the package still unpacks and the archive is
// not reported broken. Control: the same archive without the nameless entry
// extracts the same file with the same result.
TEST(ZipApi, unzip_skips_an_entry_with_an_empty_name)
{
    const R15Dir scratch("empty_name");
    const fs::path base = scratch.path();
    const fs::path zipfile = base / "nameless.zip";
    const fs::path control_zip = base / "control.zip";
    {
        std::ofstream f(zipfile, std::ios::binary);
        f << stored_zip({{"", ""}, {"keep.txt", "keep"}});
    }
    {
        std::ofstream f(control_zip, std::ios::binary);
        f << stored_zip({{"keep.txt", "keep"}});
    }

    ASSERT_EQ(ArchiveIoError::None,
              og::io::unzip_into_with_error(control_zip.string(),
                                            (base / "control_out").string()))
        << "control: the hand-built archive format is one libzip reads";
    ASSERT_TRUE(fs::exists(base / "control_out" / "keep.txt"));

    const fs::path out = base / "out";
    EXPECT_EQ(ArchiveIoError::None,
              og::io::unzip_into_with_error(zipfile.string(), out.string()))
        << "a nameless entry must be skipped, not fail the unpack";
    std::ifstream kept(out / "keep.txt", std::ios::binary);
    const std::string kept_bytes{std::istreambuf_iterator<char>(kept),
                                 std::istreambuf_iterator<char>()};
    EXPECT_EQ("keep", kept_bytes) << "the named entry after it still extracts";
    std::size_t files = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(out, ec))
        if (entry.is_regular_file())
            ++files;
    EXPECT_EQ(1u, files) << "the nameless entry must produce no file";
}

#if !defined(_WIN32)
namespace {

// Lowers the soft RLIMIT_NOFILE so exactly `free_fds` descriptor slots stay
// unused below it, and restores the old limit on scope exit (gcov and the
// sanitizers need descriptors at process exit, and an ASSERT may leave the
// scope early).
class FreeFdBudget
{
public:
    explicit FreeFdBudget(int free_fds)
    {
        if (::getrlimit(RLIMIT_NOFILE, &old_) != 0)
            return;
        rlim_t limit = 0;
        int free_seen = 0;
        while (free_seen < free_fds && limit < old_.rlim_cur)
        {
            if (::fcntl(static_cast<int>(limit), F_GETFD) == -1 && errno == EBADF)
                ++free_seen;
            ++limit;
        }
        if (free_seen != free_fds)
            return;
        rlimit lowered = old_;
        lowered.rlim_cur = limit;
        ready_ = ::setrlimit(RLIMIT_NOFILE, &lowered) == 0;
    }
    ~FreeFdBudget()
    {
        if (ready_)
            (void)::setrlimit(RLIMIT_NOFILE, &old_);
    }
    FreeFdBudget(const FreeFdBudget&) = delete;
    FreeFdBudget& operator=(const FreeFdBudget&) = delete;

    bool ready() const { return ready_; }

    // Counts the descriptors still available by dup()ing stderr until EMFILE,
    // then closes every probe.
    static int probe_free_fds()
    {
        std::vector<int> probes;
        for (;;)
        {
            const int fd = ::dup(STDERR_FILENO);
            if (fd < 0)
                break;
            probes.push_back(fd);
        }
        const int saw_emfile = errno == EMFILE;
        for (const int fd : probes)
            ::close(fd);
        return saw_emfile ? static_cast<int>(probes.size()) : -1;
    }

private:
    rlimit old_{};
    bool ready_ = false;
};

void write_text(const fs::path& path, const std::string& text)
{
    std::ofstream f(path.string(), std::ios::binary);
    f << text;
}

std::vector<std::string> regular_files_under(const fs::path& root)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec))
        if (entry.is_regular_file())
            out.push_back(fs::relative(entry.path(), root).generic_string());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

// Rule (#330): zip_contents_with_error never writes an archive it knows is
// incomplete. A directory walk that runs out of descriptors part way down a
// 64-deep tree cannot finish, so the call reports ReadInputFailed and leaves
// no archive, instead of reporting None over a silently truncated one.
// Control under the SAME descriptor budget: a 2-level tree zips to None and
// unzips to exactly its files, so the budget is enough to write and close an
// archive and the red cannot be a zip_close failure.
TEST(ZipApi, a_walk_that_runs_out_of_descriptors_reports_ReadInputFailed_and_writes_no_archive)
{
    constexpr int kFreeFds = 8;
    constexpr int kDepth = 64;
    const R15Dir scratch("fd_budget_walk");
    const fs::path base = scratch.path();
    const fs::path deep_in = base / "deep";
    const fs::path shallow_in = base / "shallow";
    const fs::path deep_zip = base / "deep.zip";
    const fs::path shallow_zip = base / "shallow.zip";
    const fs::path shallow_out = base / "shallow_out";

    std::error_code ec;
    fs::path level = deep_in;
    for (int depth = 0; depth < kDepth; ++depth)
    {
        level /= "d" + std::to_string(depth);
        fs::create_directories(level, ec);
        write_text(level / "f.txt", std::to_string(depth));
    }
    fs::create_directories(shallow_in / "sub", ec);
    write_text(shallow_in / "top.txt", "top");
    write_text(shallow_in / "sub" / "inner.txt", "inner");

    int measured_free = -1;
    ArchiveIoError shallow_zip_r = ArchiveIoError::OpenArchiveFailed;
    ArchiveIoError shallow_unzip_r = ArchiveIoError::OpenArchiveFailed;
    ArchiveIoError deep_zip_r = ArchiveIoError::OpenArchiveFailed;
    {
        FreeFdBudget budget(kFreeFds);
        ASSERT_TRUE(budget.ready()) << "could not lower RLIMIT_NOFILE";
        measured_free = FreeFdBudget::probe_free_fds();
        shallow_zip_r = og::io::zip_contents_with_error(shallow_in.string(), shallow_zip.string());
        shallow_unzip_r = og::io::unzip_into_with_error(shallow_zip.string(), shallow_out.string());
        deep_zip_r = og::io::zip_contents_with_error(deep_in.string(), deep_zip.string());
    }

    ASSERT_EQ(kFreeFds, measured_free) << "the descriptor budget must be exact";
    ASSERT_EQ(ArchiveIoError::None, shallow_zip_r)
        << "control: the budget is enough to walk, write and close a small archive";
    ASSERT_EQ(ArchiveIoError::None, shallow_unzip_r) << "control: the archive unpacks";
    ASSERT_EQ((std::vector<std::string>{"sub/inner.txt", "top.txt"}), regular_files_under(shallow_out))
        << "control: the archive holds exactly the tree's files";

    EXPECT_EQ(ArchiveIoError::ReadInputFailed, deep_zip_r)
        << "a walk that could not finish must not report success";
    EXPECT_FALSE(fs::exists(deep_zip)) << "a failed walk must leave no (truncated) archive";
}
#endif
