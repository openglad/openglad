// Pins for og::core::wrap_text / wrap_lines (issue #152): the single wrap
// primitive every prose render site now feeds through. The contract here is
// load-bearing — HIRE descriptions, campaign blurbs, scenario briefings, the
// help viewer, dialogs and the curses client all rely on these exact rules.
#include <gtest/gtest.h>

#include <openglad/core/text_wrap.h>

using og::core::wrap_lines;
using og::core::wrap_text;
using og::core::WrapMode;

namespace {

std::vector<std::string> v(std::initializer_list<const char*> lines)
{
    return std::vector<std::string>(lines.begin(), lines.end());
}

} // namespace

TEST(TextWrap, greedy_break_before_overflowing_word)
{
    EXPECT_EQ(v({"aaa bbb", "ccc"}), wrap_text("aaa bbb ccc", 7));
    EXPECT_EQ(v({"aaa", "bbb", "ccc"}), wrap_text("aaa bbb ccc", 3));
}

TEST(TextWrap, exact_fit_is_not_broken)
{
    EXPECT_EQ(v({"abcdefg"}), wrap_text("abcdefg", 7));
    EXPECT_EQ(v({"aaa bbb"}), wrap_text("aaa bbb", 7));
}

TEST(TextWrap, trailing_whitespace_padding_is_stripped)
{
    // The shipped packs pad every description line with trailing spaces to a
    // fixed column; the wrap must erase that padding.
    EXPECT_EQ(v({"abc", "def"}), wrap_text("abc   \ndef", 10));
    EXPECT_EQ(v({"abc", "def"}), wrap_text("abc\t\ndef  ", 10));
}

TEST(TextWrap, carriage_returns_are_stripped)
{
    EXPECT_EQ(v({"abc", "def"}), wrap_text("abc\r\ndef\r", 10));
}

TEST(TextWrap, overlong_word_is_hard_split_never_dropped)
{
    const std::string token(40, 'x');
    const std::vector<std::string> out = wrap_text(token, 10);
    ASSERT_EQ(4u, out.size());
    std::size_t total = 0;
    for (const std::string& line : out)
    {
        EXPECT_LE(line.size(), 10u);
        total += line.size();
    }
    EXPECT_EQ(40u, total);
}

TEST(TextWrap, overlong_word_mid_line_starts_fresh_then_splits)
{
    // "aa" fits, then the 12-char token is split onto following lines.
    const std::vector<std::string> out = wrap_text("aa bbbbbbbbbbbb", 8);
    EXPECT_EQ(v({"aa", "bbbbbbbb", "bbbb"}), out);
}

TEST(TextWrap, hard_breaks_preserves_every_newline_and_indentation)
{
    const std::vector<std::string> out = wrap_text(
        "Shifter + Yell: Summon NPCs to\n  come fight with or protect you",
        39);
    EXPECT_EQ(v({"Shifter + Yell: Summon NPCs to",
                 "  come fight with or protect you"}),
              out);
}

TEST(TextWrap, hard_breaks_keeps_ascii_table_interior_spacing)
{
    // Fitting lines pass through verbatim: the Controls help tab's tables
    // must not be reflowed.
    const char* table = "Fire:    LCtrl   .       Space   5";
    EXPECT_EQ(v({"Fire:    LCtrl   .       Space   5"}),
              wrap_text(table, 39));
}

TEST(TextWrap, hard_breaks_rewraps_only_overlong_lines_keeping_indent)
{
    const std::vector<std::string> out =
        wrap_text("  alpha beta gamma delta", 12);
    EXPECT_EQ(v({"  alpha beta", "  gamma", "  delta"}), out);
}

TEST(TextWrap, degenerate_indent_wider_than_budget_is_dropped)
{
    // Indent alone would fill the line; it is dropped so wrapping terminates.
    const std::vector<std::string> out =
        wrap_text("          longword more words", 8);
    ASSERT_FALSE(out.empty());
    for (const std::string& line : out)
        EXPECT_LE(line.size(), 8u);
}

TEST(TextWrap, blank_line_survives_as_one_empty_output_line)
{
    EXPECT_EQ(v({"a", "", "b"}), wrap_text("a\n\nb", 10));
}

TEST(TextWrap, empty_input_yields_empty_vector)
{
    EXPECT_TRUE(wrap_text("", 10).empty());
    EXPECT_TRUE(wrap_text("", 10, WrapMode::Paragraphs).empty());
}

TEST(TextWrap, whitespace_only_input_yields_no_nonempty_lines)
{
    for (const std::string& line : wrap_text("   \n\t\n ", 10))
        EXPECT_TRUE(line.empty());
    EXPECT_TRUE(wrap_text("   \n\t\n ", 10, WrapMode::Paragraphs).empty());
}

TEST(TextWrap, nonpositive_budget_returns_input_per_source_line)
{
    EXPECT_EQ(v({"abc def", "ghi"}), wrap_text("abc def\nghi", 0));
    EXPECT_EQ(v({"abc def", "ghi"}), wrap_text("abc def\nghi", -5));
}

TEST(TextWrap, paragraphs_single_newline_joins_with_one_space)
{
    EXPECT_EQ(v({"one two three"}),
              wrap_text("one\ntwo\nthree", 20, WrapMode::Paragraphs));
    // Trailing padding on the joined fragments collapses to single spaces.
    EXPECT_EQ(v({"one two three"}),
              wrap_text("one   \ntwo  \nthree", 20, WrapMode::Paragraphs));
}

TEST(TextWrap, paragraphs_blank_line_emits_exactly_one_empty_line)
{
    EXPECT_EQ(v({"one two", "", "three"}),
              wrap_text("one\ntwo\n\nthree", 20, WrapMode::Paragraphs));
}

TEST(TextWrap, paragraphs_many_newlines_still_emit_one_empty_line)
{
    EXPECT_EQ(v({"one", "", "two"}),
              wrap_text("one\n\n\n\ntwo", 20, WrapMode::Paragraphs));
    // Whitespace-only lines count as blank separators too.
    EXPECT_EQ(v({"one", "", "two"}),
              wrap_text("one\n  \n\t\ntwo", 20, WrapMode::Paragraphs));
}

TEST(TextWrap, paragraphs_reflow_of_a_padded_pack_description)
{
    // The exact shape the shipped class packs used before #152: 26-char
    // hand-wrap, trailing-space padding, '\n\n' before the Special line.
    const char* padded =
        "Your basic grunt, can     \n"
        "absorb and deal damage and\n"
        "move moderately fast. A   \n"
        "good all-around fighter. A\n"
        "soldier's normal weapon is\n"
        "a magical returning blade.\n"
        "\n"
        "Special: Charge";
    const std::vector<std::string> out =
        wrap_text(padded, 27, WrapMode::Paragraphs);
    ASSERT_LE(out.size(), 8u) << "must fit the HIRE box at 10px pitch";
    for (const std::string& line : out)
        EXPECT_LE(line.size(), 27u);
    // The blank separator survives and the Special line is its own
    // paragraph.
    ASSERT_GE(out.size(), 2u);
    EXPECT_EQ("Special: Charge", out.back());
    EXPECT_TRUE(out[out.size() - 2].empty());
}

TEST(TextWrap, wrap_lines_joins_list_entries)
{
    const std::list<std::string> lines = {"aaa bbb", "ccc"};
    EXPECT_EQ(v({"aaa bbb", "ccc"}), wrap_lines(lines, 10));
    EXPECT_EQ(v({"aaa", "bbb", "ccc"}), wrap_lines(lines, 3));
    EXPECT_EQ(v({"aaa bbb ccc"}), wrap_lines(lines, 20, WrapMode::Paragraphs));
}

TEST(TextWrap, wrap_lines_empty_list_yields_empty_vector)
{
    EXPECT_TRUE(wrap_lines({}, 10).empty());
}

TEST(TextWrap, wrap_lines_preserves_blank_list_entries_in_hard_breaks)
{
    const std::list<std::string> lines = {"a", "", "b"};
    EXPECT_EQ(v({"a", "", "b"}), wrap_lines(lines, 10));
}
