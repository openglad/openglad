/* Unit tests for the HeadlessTerminal test double. */
#include <gtest/gtest.h>

#include <openglad/platform/curses/headless_terminal.h>

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
