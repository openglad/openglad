#include <openglad/legacy/base.h>
#include <openglad/resources/og_file.h>
#include <gtest/gtest.h>

#include <cstring>

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
    std::int64_t seek(std::int64_t offset, int whence) override {
        if (whence == 0) pos_ = offset;
        else if (whence == 1) pos_ += offset;
        else if (whence == 2) pos_ = len_ + offset;
        return static_cast<std::int64_t>(pos_);
    }
    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }
private:
    const unsigned char* data_;
    std::size_t len_;
    std::size_t pos_;
};

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

