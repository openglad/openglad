/* Unit tests for the HeadlessTerminal test double. */
#include <gtest/gtest.h>

#include <openglad/platform/curses/headless_terminal.h>

#include "transcript_capture.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

using namespace og::curses;

TEST(HeadlessTerminal, dimensions_and_clear)
{
    HeadlessTerminal term(10, 20);
    EXPECT_EQ(term.rows(), 10);
    EXPECT_EQ(term.cols(), 20);
    term.put(1, 2, U'X', Color::Red, Color::Default, false);
    EXPECT_EQ(term.char_at(1, 2), U'X');
    term.clear();
    EXPECT_EQ(term.char_at(1, 2), U' ');
}

TEST(HeadlessTerminal, put_records_color_and_bold)
{
    HeadlessTerminal term(5, 5);
    term.put(0, 0, U'A', Color::Cyan, Color::Blue, true);
    const Cell& c = term.cell_at(0, 0);
    EXPECT_EQ(c.ch, U'A');
    EXPECT_EQ(c.fg, Color::Cyan);
    EXPECT_EQ(c.bg, Color::Blue);
    EXPECT_TRUE(c.bold);
}

TEST(HeadlessTerminal, put_str_lays_out_cells)
{
    HeadlessTerminal term(3, 10);
    term.put_str(1, 2, "Hi!", Color::White, Color::Default, false);
    EXPECT_EQ(term.char_at(1, 2), U'H');
    EXPECT_EQ(term.char_at(1, 3), U'i');
    EXPECT_EQ(term.char_at(1, 4), U'!');
    EXPECT_EQ(term.text_row(1).substr(2, 3), "Hi!");
}

TEST(HeadlessTerminal, out_of_bounds_put_is_ignored)
{
    HeadlessTerminal term(3, 3);
    term.put(-1, 0, U'X', Color::Red, Color::Default, false);
    term.put(0, 99, U'Y', Color::Red, Color::Default, false);
    EXPECT_EQ(term.find_char(U'X').first, -1);
    EXPECT_EQ(term.find_char(U'Y').first, -1);
}

TEST(HeadlessTerminal, scripted_keys_are_returned_in_order)
{
    HeadlessTerminal term(3, 3);
    term.push_char(U'a');
    term.push_special(KeyCode::Up);
    term.push_string("bc");
    EXPECT_TRUE(term.poll_key(false).is_char(U'a'));
    EXPECT_EQ(term.poll_key(false).code, KeyCode::Up);
    EXPECT_TRUE(term.poll_key(false).is_char(U'b'));
    EXPECT_TRUE(term.poll_key(false).is_char(U'c'));
    EXPECT_TRUE(term.poll_key(false).is_none()) << "exhausted queue yields None";
}

TEST(HeadlessTerminal, present_snapshots_and_counts)
{
    HeadlessTerminal term(2, 2);
    term.put(0, 0, U'Z', Color::Red, Color::Default, false);
    EXPECT_EQ(term.present_count(), 0);
    term.present();
    EXPECT_EQ(term.present_count(), 1);
    EXPECT_EQ(term.presented_char_at(0, 0), U'Z');
    // Mutating the back buffer after present does not change the snapshot.
    term.put(0, 0, U'Q', Color::Red, Color::Default, false);
    EXPECT_EQ(term.presented_char_at(0, 0), U'Z');
    EXPECT_EQ(term.char_at(0, 0), U'Q');
}

TEST(HeadlessTerminal, find_and_count_char)
{
    HeadlessTerminal term(2, 3);
    term.put(0, 0, U'*', Color::Red, Color::Default, false);
    term.put(1, 2, U'*', Color::Red, Color::Default, false);
    EXPECT_EQ(term.count_char(U'*'), 2);
    EXPECT_EQ(term.find_char(U'*'), std::make_pair(0, 0));
}

TEST(HeadlessTerminal, resize_dump_and_beep_cover_remaining_helpers)
{
    HeadlessTerminal term(0, -5);
    EXPECT_EQ(term.rows(), 1);
    EXPECT_EQ(term.cols(), 1);

    term.resize(3, 4);
    EXPECT_EQ(term.rows(), 3);
    EXPECT_EQ(term.cols(), 4);
    EXPECT_EQ(term.dump(), "    \n    \n    \n");

    term.put_str(1, 1, "ab", Color::Green, Color::Default, false);
    EXPECT_EQ(term.dump(), "    \n ab \n    \n");
    term.beep();
    term.beep();
    EXPECT_EQ(term.beep_count(), 2);

    term.set_unicode(false);
    term.set_color(false);
    EXPECT_FALSE(term.supports_unicode());
    EXPECT_FALSE(term.supports_color());
    EXPECT_EQ(term.presented_char_at(99, 99), 0);

    term.clear();
    term.put(0, 0, U'\u03a9', Color::Default, Color::Default, false);
    EXPECT_EQ(term.text_row(0), "?   ")
        << "the diagnostic text view preserves blank cells and marks Unicode";
    EXPECT_TRUE(term.text_row(-1).empty());
}

// --- transcript capture (PR #292 P8 media helper) -------------------------

TEST(HeadlessTerminalTranscript, capture_writes_the_full_dump_only_when_env_set)
{
    namespace fs = std::filesystem;

    HeadlessTerminal term(3, 12);
    term.put_str(0, 0, "row-zero", Color::White, Color::Default, false);
    term.put_str(1, 0, "row-one", Color::White, Color::Default, false);
    term.put_str(2, 0, "row-two-END", Color::White, Color::Default, false);
    // Every row, padded to the terminal width, in order, newline-terminated.
    const std::string expected =
        "row-zero    \n"
        "row-one     \n"
        "row-two-END \n";
    ASSERT_EQ(expected, term.dump())
        << "the fixture row set must be the exact transcript the capture ships";

    const char* const previous_dir = std::getenv("OG_FX_CAPTURE_DIR");
    const std::string saved_dir = previous_dir != nullptr ? previous_dir : "";
    const fs::path scratch =
        fs::temp_directory_path() /
        ("og_transcript_capture_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(scratch, ec);
    ASSERT_TRUE(fs::create_directories(scratch, ec)) << "scratch dir: " << ec.message();

    // Arm 1: with the env set, the whole dump lands in <dir>/<name>.txt.
    ASSERT_EQ(0, ::setenv("OG_FX_CAPTURE_DIR", scratch.c_str(), 1));
    EXPECT_TRUE(capture_transcript(term, "armed"))
        << "capture_transcript reports the write it performed";
    const fs::path armed = scratch / "armed.txt";
    ASSERT_TRUE(fs::exists(armed)) << "the capture must create <name>.txt";
    std::string written;
    {
        std::ifstream in(armed, std::ios::binary);
        ASSERT_TRUE(in.good()) << "the written transcript must be readable";
        written.assign(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(expected, written)
        << "every row of the terminal, in order, must reach the file";
    EXPECT_EQ(expected.size(), static_cast<std::size_t>(fs::file_size(armed)))
        << "no row may be dropped from the tail of the transcript";

    // Arm 2: with the env unset the helper is inert -- no file, no throw, so
    // the capture calls inside a regression test change nothing in a plain run.
    ASSERT_EQ(0, ::unsetenv("OG_FX_CAPTURE_DIR"));
    EXPECT_FALSE(capture_transcript(term, "unarmed"))
        << "an unset OG_FX_CAPTURE_DIR must write nothing";
    EXPECT_FALSE(fs::exists(scratch / "unarmed.txt"))
        << "no transcript file may appear when the capture is not armed";
    EXPECT_EQ(1u, std::distance(fs::directory_iterator(scratch),
                                fs::directory_iterator()))
        << "the armed capture is the only file the helper ever created";

    // Arm 3: an empty value counts as unset.
    ASSERT_EQ(0, ::setenv("OG_FX_CAPTURE_DIR", "", 1));
    EXPECT_FALSE(capture_transcript(term, "empty-env"))
        << "an empty OG_FX_CAPTURE_DIR is not a directory";
    EXPECT_FALSE(fs::exists(scratch / "empty-env.txt"))
        << "an empty OG_FX_CAPTURE_DIR must not write a file";

    if (saved_dir.empty())
        ASSERT_EQ(0, ::unsetenv("OG_FX_CAPTURE_DIR"));
    else
        ASSERT_EQ(0, ::setenv("OG_FX_CAPTURE_DIR", saved_dir.c_str(), 1));
    fs::remove_all(scratch, ec);
}

TEST(HeadlessTerminalTranscript, phase_tag_defaults_to_run)
{
    const char* const previous = std::getenv("OG_FX_PHASE");
    const std::string saved = previous != nullptr ? previous : "";

    ASSERT_EQ(0, ::unsetenv("OG_FX_PHASE"));
    EXPECT_EQ("run", transcript_phase())
        << "an unset phase tags the transcript 'run'";
    ASSERT_EQ(0, ::setenv("OG_FX_PHASE", "", 1));
    EXPECT_EQ("run", transcript_phase())
        << "an empty phase tags the transcript 'run'";
    ASSERT_EQ(0, ::setenv("OG_FX_PHASE", "before", 1));
    EXPECT_EQ("before", transcript_phase())
        << "the capture names carry the phase the run asked for";

    if (saved.empty())
        ASSERT_EQ(0, ::unsetenv("OG_FX_PHASE"));
    else
        ASSERT_EQ(0, ::setenv("OG_FX_PHASE", saved.c_str(), 1));
}
