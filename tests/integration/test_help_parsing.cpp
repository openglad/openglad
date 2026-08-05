#include <openglad/legacy/base.h>
#include <openglad/resources/og_file.h>
#include <gtest/gtest.h>

#include <cstring>
#include <limits>

// Simple memory-backed OgFile for testing.
class MemOgFile : public og::io::OgFile {
public:
    MemOgFile(const void* data, size_t len)
        : data_(static_cast<const unsigned char*>(data)), len_(len), pos_(0) {}

    std::size_t read(void* buf, std::size_t size, std::size_t count) override {
        std::size_t total = size * count;
        std::size_t avail = (pos_ < len_) ? len_ - pos_ : 0;
        if (total > avail) total = avail;
        std::memcpy(buf, data_ + pos_, total);
        pos_ += total;
        return (size > 0) ? total / size : 0;
    }
    std::size_t write(const void*, std::size_t, std::size_t) override { return 0; }
    // Mirrors the real OgFile::seek contract (og_file.h): "Returns new
    // position or -1 on error". `offset` is legitimately negative for
    // SEEK_CUR/SEEK_END, so the arithmetic is signed and range-checked --
    // both production backends reject a seek before the start (PHYSFS_seek
    // fails; fseek returns nonzero) and leave the position alone, and so
    // does this stand-in. An unsigned `pos_ += offset` instead wrapped to a
    // gigantic position and reported it as success. Seeking past the end
    // stays legal (stdio semantics; read() then returns 0 objects).
    std::int64_t seek(std::int64_t offset, int whence) override {
        std::int64_t base = 0;
        if (whence == 0) base = 0;                                   // SEEK_SET
        else if (whence == 1) base = static_cast<std::int64_t>(pos_);// SEEK_CUR
        else if (whence == 2) base = static_cast<std::int64_t>(len_);// SEEK_END
        else return -1;
        if (offset > std::numeric_limits<std::int64_t>::max() - base)
            return -1;
        const std::int64_t target = base + offset;
        if (target < 0) return -1;
        pos_ = static_cast<std::size_t>(target);
        return target;
    }
    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }
private:
    const unsigned char* data_;
    std::size_t len_;
    std::size_t pos_;
};

// The stand-in must honor the OgFile::seek contract, negative offsets
// included: in-range seeks return the new position, an out-of-range one
// (before the start) returns -1 and leaves the position where it was.
TEST(HelpParsing, mem_ogfile_seek_honors_the_ogfile_contract)
{
    const char* text = "0123456789";
    MemOgFile file(text, strlen(text));

    // In-range: SET, CUR forward, CUR backward, END backward.
    EXPECT_EQ(4, file.seek(4, 0));
    EXPECT_EQ(4, file.tell());
    EXPECT_EQ(7, file.seek(3, 1));
    EXPECT_EQ(5, file.seek(-2, 1));
    EXPECT_EQ(9, file.seek(-1, 2));
    EXPECT_EQ(10, file.seek(0, 2));
    EXPECT_EQ(0, file.seek(0, 0));

    // The byte at the sought position is the one that gets read.
    char c = 0;
    ASSERT_EQ(6, file.seek(-4, 2));
    ASSERT_EQ(1u, file.read(&c, 1, 1));
    EXPECT_EQ('6', c);

    // Out of range (before the start): -1, position untouched -- never a
    // wrapped-around gigantic offset reported as success.
    ASSERT_EQ(3, file.seek(3, 0));
    EXPECT_EQ(-1, file.seek(-1, 0));
    EXPECT_EQ(3, file.tell());
    EXPECT_EQ(-1, file.seek(-4, 1));
    EXPECT_EQ(3, file.tell());
    EXPECT_EQ(-1, file.seek(-11, 2));
    EXPECT_EQ(3, file.tell());
    EXPECT_EQ(-1, file.seek(std::numeric_limits<std::int64_t>::min(), 1));
    EXPECT_EQ(3, file.tell());

    // Unknown whence is an error too, and reads after a rejected seek still
    // come from the untouched position.
    EXPECT_EQ(-1, file.seek(0, 42));
    ASSERT_EQ(1u, file.read(&c, 1, 1));
    EXPECT_EQ('3', c);
}

TEST(HelpParsing, help_read_one_line_stops_on_newline_and_sets_eof)
{
    const char* text = "abc\ndef";
    MemOgFile file(text, strlen(text));

    og::runtime::current_session->help_end_of_file_ = 0;
    std::string l1 = read_one_line(file, HELP_WIDTH);
    ASSERT_STREQ("abc", l1.c_str()) << "first line should be 'abc'";

    std::string l2 = read_one_line(file, HELP_WIDTH);
    ASSERT_STREQ("def", l2.c_str()) << "second line should be 'def'";

    // Reading again should hit EOF and set end_of_file.
    (void)read_one_line(file, HELP_WIDTH);
    ASSERT_TRUE(og::runtime::current_session->help_end_of_file_ == 1) << "help_end_of_file_ should be set after EOF";
}


TEST(HelpParsing, help_fill_help_array_reads_multiple_lines)
{
    const char* text = "line1\nline2\nline3\n";
    MemOgFile file(text, strlen(text));

    char arr[HELP_WIDTH][MAX_LINES];
    memset(arr, 0, sizeof(arr));
    og::runtime::current_session->help_end_of_file_ = 0;

    short n = fill_help_array(arr, file);
    ASSERT_TRUE(n >= 2) << "fill_help_array should read at least 2 lines";
    ASSERT_STREQ("line1", arr[0]) << "line 0 should match";
    ASSERT_STREQ("line2", arr[1]) << "line 1 should match";
}

TEST(HelpParsing, help_read_one_line_returns_an_exact_width_record)
{
    std::string text(static_cast<std::size_t>(HELP_WIDTH), 'x');
    MemOgFile file(text.data(), text.size());

    og::runtime::current_session->help_end_of_file_ = 0;
    const std::string line = read_one_line(file, HELP_WIDTH);
    EXPECT_EQ(static_cast<std::size_t>(HELP_WIDTH), line.size());
    EXPECT_EQ(text, line);
    EXPECT_EQ(0, og::runtime::current_session->help_end_of_file_)
        << "consuming exactly the requested width is not an early EOF";

    EXPECT_TRUE(read_one_line(file, HELP_WIDTH).empty());
    EXPECT_EQ(1, og::runtime::current_session->help_end_of_file_);
}
