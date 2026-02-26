#include <openglad/legacy/base.h>
#include <openglad/resources/og_file.h>
#include "test_framework.h"

#include <cstring>

extern short end_of_file;

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

void test_help_read_one_line_stops_on_newline_and_sets_eof()
{
    const char* text = "abc\ndef";
    MemOgFile file(text, strlen(text));

    end_of_file = 0;
    std::string l1 = read_one_line(file, HELP_WIDTH);
    TEST_ASSERT_STR_EQ("abc", l1.c_str(), "first line should be 'abc'");

    std::string l2 = read_one_line(file, HELP_WIDTH);
    TEST_ASSERT_STR_EQ("def", l2.c_str(), "second line should be 'def'");

    // Reading again should hit EOF and set end_of_file.
    (void)read_one_line(file, HELP_WIDTH);
    TEST_ASSERT(end_of_file == 1, "end_of_file should be set after EOF");
}
REGISTER_TEST(test_help_read_one_line_stops_on_newline_and_sets_eof);

void test_help_fill_help_array_reads_multiple_lines()
{
    const char* text = "line1\nline2\nline3\n";
    MemOgFile file(text, strlen(text));

    char arr[HELP_WIDTH][MAX_LINES];
    memset(arr, 0, sizeof(arr));
    end_of_file = 0;

    short n = fill_help_array(arr, file);
    TEST_ASSERT(n >= 2, "fill_help_array should read at least 2 lines");
    TEST_ASSERT_STR_EQ("line1", arr[0], "line 0 should match");
    TEST_ASSERT_STR_EQ("line2", arr[1], "line 1 should match");
}
REGISTER_TEST(test_help_fill_help_array_reads_multiple_lines);
