#include "base.h"
#include "test_framework.h"

#include <cstring>

extern short end_of_file;

// From help.cpp
char* read_one_line(SDL_RWops* infile, short length);
short fill_help_array(char somearray[HELP_WIDTH][MAX_LINES], SDL_RWops* infile);

void test_help_read_one_line_stops_on_newline_and_sets_eof()
{
    const char* text = "abc\ndef";
    SDL_RWops* rw = SDL_RWFromConstMem(text, (int)strlen(text));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");

    end_of_file = 0;
    char* l1 = read_one_line(rw, HELP_WIDTH);
    TEST_ASSERT(l1 != nullptr, "read_one_line should return buffer");
    TEST_ASSERT_STR_EQ("abc", l1, "first line should be 'abc'");
    delete[] l1;

    char* l2 = read_one_line(rw, HELP_WIDTH);
    TEST_ASSERT_STR_EQ("def", l2, "second line should be 'def'");
    delete[] l2;

    // Reading again should hit EOF and set end_of_file.
    char* l3 = read_one_line(rw, HELP_WIDTH);
    TEST_ASSERT(end_of_file == 1, "end_of_file should be set after EOF");
    delete[] l3;

    SDL_RWclose(rw);
}
REGISTER_TEST(test_help_read_one_line_stops_on_newline_and_sets_eof);

void test_help_fill_help_array_reads_multiple_lines()
{
    const char* text = "line1\nline2\nline3\n";
    SDL_RWops* rw = SDL_RWFromConstMem(text, (int)strlen(text));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");

    char arr[HELP_WIDTH][MAX_LINES];
    memset(arr, 0, sizeof(arr));
    end_of_file = 0;

    short n = fill_help_array(arr, rw);
    TEST_ASSERT(n >= 2, "fill_help_array should read at least 2 lines");
    TEST_ASSERT_STR_EQ("line1", arr[0], "line 0 should match");
    TEST_ASSERT_STR_EQ("line2", arr[1], "line 1 should match");

    SDL_RWclose(rw);
}
REGISTER_TEST(test_help_fill_help_array_reads_multiple_lines);
