/* Unit tests for the pure Kitty keyboard protocol decoder.
 *
 * These feed the exact byte sequences a supporting terminal emits and assert the
 * decoded Key events — press/repeat/release, modifiers, standalone modifier keys,
 * functional keys, partial reads, and the capability handshake. No TTY needed, so
 * the fiddly parsing the real client depends on is fully covered in CI.
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/kitty_keys.h>

#include <vector>

using namespace og::curses;
using og::curses::kitty::Decoder;

namespace og::curses {
int curses_terminal_testing_exercise_internal_helpers();
}

namespace {

// Decode exactly one key from a byte string.
Key decode1(std::string_view bytes)
{
    Decoder d;
    d.feed(bytes);
    Key k;
    EXPECT_TRUE(d.next(k)) << "expected a key event from the byte sequence";
    return k;
}

// Decode every key from a byte string.
std::vector<Key> decode_all(std::string_view bytes)
{
    Decoder d;
    d.feed(bytes);
    std::vector<Key> out;
    Key k;
    while (d.next(k))
        out.push_back(k);
    return out;
}

} // namespace

TEST(KittyKeys, printable_key_press)
{
    const Key k = decode1("\x1b[113u"); // 'q' (113)
    EXPECT_EQ(k.code, KeyCode::Char);
    EXPECT_EQ(k.ch, U'q');
    EXPECT_EQ(k.event, KeyEvent::Press);
    EXPECT_FALSE(k.mods.any());
}

TEST(KittyKeys, event_types_press_repeat_release)
{
    EXPECT_EQ(decode1("\x1b[113;1:1u").event, KeyEvent::Press);
    EXPECT_EQ(decode1("\x1b[113;1:2u").event, KeyEvent::Repeat);
    EXPECT_EQ(decode1("\x1b[113;1:3u").event, KeyEvent::Release);
    // A bare "CSI codepoint u" with no params is a press.
    EXPECT_EQ(decode1("\x1b[113u").event, KeyEvent::Press);
}

TEST(KittyKeys, modifiers_decode)
{
    // modifier field = bitfield + 1: shift=2, alt=3, ctrl=5, ctrl+shift=6.
    const Key shift = decode1("\x1b[97;2u");
    EXPECT_TRUE(shift.mods.shift);
    EXPECT_FALSE(shift.mods.ctrl);

    const Key ctrl = decode1("\x1b[100;5u"); // Ctrl+d
    EXPECT_EQ(ctrl.ch, U'd');
    EXPECT_TRUE(ctrl.mods.ctrl);
    EXPECT_FALSE(ctrl.mods.shift);

    const Key alt = decode1("\x1b[100;3u");
    EXPECT_TRUE(alt.mods.alt);

    const Key both = decode1("\x1b[100;6u"); // ctrl+shift
    EXPECT_TRUE(both.mods.ctrl);
    EXPECT_TRUE(both.mods.shift);
}

TEST(KittyKeys, named_control_keys)
{
    EXPECT_EQ(decode1("\x1b[27u").code, KeyCode::Escape);
    EXPECT_EQ(decode1("\x1b[13u").code, KeyCode::Enter);
    EXPECT_EQ(decode1("\x1b[9u").code, KeyCode::Tab);
    EXPECT_EQ(decode1("\x1b[127u").code, KeyCode::Backspace);
    // Space stays a printable character.
    const Key space = decode1("\x1b[32u");
    EXPECT_EQ(space.code, KeyCode::Char);
    EXPECT_EQ(space.ch, U' ');
}

// The headline capability: standalone modifier keys arrive as their own events,
// so Fire = LeftCtrl / Special = LeftAlt become deliverable.
TEST(KittyKeys, standalone_modifier_keys)
{
    EXPECT_EQ(decode1("\x1b[57441u").code, KeyCode::LeftShift);
    EXPECT_EQ(decode1("\x1b[57442u").code, KeyCode::LeftCtrl);
    EXPECT_EQ(decode1("\x1b[57443u").code, KeyCode::LeftAlt);
    EXPECT_EQ(decode1("\x1b[57444u").code, KeyCode::LeftSuper);
    EXPECT_EQ(decode1("\x1b[57447u").code, KeyCode::RightShift);
    EXPECT_EQ(decode1("\x1b[57448u").code, KeyCode::RightCtrl);
    EXPECT_EQ(decode1("\x1b[57449u").code, KeyCode::RightAlt);
    EXPECT_EQ(decode1("\x1b[57450u").code, KeyCode::RightSuper);

    // And they carry press/release, so the engine can track them held.
    EXPECT_EQ(decode1("\x1b[57442u").event, KeyEvent::Press);
    EXPECT_EQ(decode1("\x1b[57442;1:3u").event, KeyEvent::Release);
}

TEST(KittyKeys, arrow_keys_letter_form)
{
    EXPECT_EQ(decode1("\x1b[A").code, KeyCode::Up);
    EXPECT_EQ(decode1("\x1b[B").code, KeyCode::Down);
    EXPECT_EQ(decode1("\x1b[C").code, KeyCode::Right);
    EXPECT_EQ(decode1("\x1b[D").code, KeyCode::Left);
    // With modifiers / event types: CSI 1 ; mods : event {A..D}.
    const Key ctrl_up = decode1("\x1b[1;5A");
    EXPECT_EQ(ctrl_up.code, KeyCode::Up);
    EXPECT_TRUE(ctrl_up.mods.ctrl);
    EXPECT_EQ(decode1("\x1b[1;1:3A").event, KeyEvent::Release);
}

TEST(KittyKeys, tilde_and_ss3_functional_keys)
{
    EXPECT_EQ(decode1("\x1b[1~").code, KeyCode::Home);
    EXPECT_EQ(decode1("\x1b[2~").code, KeyCode::Insert);
    EXPECT_EQ(decode1("\x1b[3~").code, KeyCode::Delete);
    EXPECT_EQ(decode1("\x1b[4~").code, KeyCode::End);
    EXPECT_EQ(decode1("\x1b[5~").code, KeyCode::PageUp);
    EXPECT_EQ(decode1("\x1b[6~").code, KeyCode::PageDown);
    EXPECT_EQ(decode1("\x1b[7~").code, KeyCode::Home);
    EXPECT_EQ(decode1("\x1b[8~").code, KeyCode::End);
    EXPECT_EQ(decode1("\x1b[15~").code, KeyCode::F5);
    EXPECT_EQ(decode1("\x1b[17~").code, KeyCode::F6);
    EXPECT_EQ(decode1("\x1b[18~").code, KeyCode::F7);
    EXPECT_EQ(decode1("\x1b[19~").code, KeyCode::F8);
    EXPECT_EQ(decode1("\x1b[20~").code, KeyCode::F9);
    EXPECT_EQ(decode1("\x1b[21~").code, KeyCode::F10);
    EXPECT_EQ(decode1("\x1b[23~").code, KeyCode::F11);
    EXPECT_EQ(decode1("\x1b[24~").code, KeyCode::F12);
    // SS3 form for F1..F4.
    EXPECT_EQ(decode1("\x1bOP").code, KeyCode::F1);
    EXPECT_EQ(decode1("\x1bOQ").code, KeyCode::F2);
    EXPECT_EQ(decode1("\x1bOR").code, KeyCode::F3);
    EXPECT_EQ(decode1("\x1bOS").code, KeyCode::F4);
    // CSI letter form for F1..F4.
    EXPECT_EQ(decode1("\x1b[1;2P").code, KeyCode::F1);
    EXPECT_EQ(decode1("\x1b[1;2Q").code, KeyCode::F2);
    EXPECT_EQ(decode1("\x1b[1;2S").code, KeyCode::F4);
    EXPECT_EQ(decode1("\x1b[1;2H").code, KeyCode::Home);
    EXPECT_EQ(decode1("\x1b[1;2F").code, KeyCode::End);
}

TEST(KittyKeys, focus_events)
{
    EXPECT_EQ(decode1("\x1b[I").code, KeyCode::FocusIn);
    EXPECT_EQ(decode1("\x1b[O").code, KeyCode::FocusOut);
}

TEST(KittyKeys, multiple_events_in_one_chunk)
{
    const std::vector<Key> keys = decode_all("\x1b[97u\x1b[98u\x1b[99u");
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0].ch, U'a');
    EXPECT_EQ(keys[1].ch, U'b');
    EXPECT_EQ(keys[2].ch, U'c');
}

TEST(KittyKeys, partial_sequence_waits_for_rest)
{
    Decoder d;
    Key k;
    d.feed("\x1b[11"); // partial "CSI 11" — could become 113u, 112u, ...
    EXPECT_FALSE(d.next(k)) << "an incomplete sequence yields nothing yet";
    d.feed("3u"); // completes to "\x1b[113u"
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'q');
    EXPECT_FALSE(d.next(k)) << "buffer fully consumed";
}

TEST(KittyKeys, capability_response_is_skipped_not_emitted)
{
    Decoder d;
    Key k;
    d.feed("\x1b[?11u"); // a kitty capability reply, not a key
    EXPECT_FALSE(d.next(k)) << "the protocol response must not surface as a key";
    // A real key after the response still decodes.
    d.feed("\x1b[97u");
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'a');
}

TEST(KittyKeys, raw_byte_fallback_decodes_as_char)
{
    // Defensive path: a bare byte (no escape) is decoded as a character press.
    const Key k = decode1("a");
    EXPECT_EQ(k.code, KeyCode::Char);
    EXPECT_EQ(k.ch, U'a');
    EXPECT_EQ(k.event, KeyEvent::Press);
}

TEST(KittyKeys, raw_utf8_fallback_decodes_multibyte_and_control_keys)
{
    EXPECT_EQ(decode1("\xc3\xa9").ch, static_cast<char32_t>(0x00e9));
    EXPECT_EQ(decode1("\xe2\x82\xac").ch, static_cast<char32_t>(0x20ac));
    EXPECT_EQ(decode1("\xf0\x9f\x98\x80").ch, static_cast<char32_t>(0x1f600));
    EXPECT_EQ(decode1("\r").code, KeyCode::Enter);
    EXPECT_EQ(decode1("\n").code, KeyCode::Enter);
    EXPECT_EQ(decode1("\t").code, KeyCode::Tab);
    EXPECT_EQ(decode1("\b").code, KeyCode::Backspace);
    EXPECT_EQ(decode1("\x7f").code, KeyCode::Backspace);
}

TEST(KittyKeys, malformed_or_unknown_sequences_are_skipped_and_resync)
{
    Decoder d;
    Key k;

    d.feed("\x1b[1114112u\x1b[97u"); // invalid Unicode codepoint, then 'a'
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'a');
    EXPECT_FALSE(d.next(k));

    d.clear();
    d.feed("\x1b[99~\x1b[98u"); // unknown tilde code, then 'b'
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'b');

    d.clear();
    d.feed("\x1b[1;2Z\x1b[99u"); // unknown CSI final, then 'c'
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'c');

    d.clear();
    d.feed("\x1bOT\x1b[100u"); // unknown SS3 final, then 'd'
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'd');

    d.clear();
    d.feed("\xff" "e"); // invalid UTF-8 lead byte, then raw 'e'
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'e');

    d.clear();
    d.feed("\xc3x" "f"); // malformed UTF-8 continuation, then raw 'x'
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'x');
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'f');

    d.clear();
    d.feed("\xe2\x82"); // incomplete UTF-8 stays buffered
    EXPECT_FALSE(d.next(k));
    d.feed("\xac");
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, static_cast<char32_t>(0x20ac));

    d.clear();
    d.feed("\x1b[?"); // incomplete private CSI response stays buffered
    EXPECT_FALSE(d.next(k));
    d.feed("11u\x1b[103u");
    ASSERT_TRUE(d.next(k));
    EXPECT_EQ(k.ch, U'g');
}

TEST(KittyKeys, oversized_params_garbage_final_and_bare_escape_resync)
{
    kitty::Decoder decoder;
    Key key;

    decoder.feed("\x1b[999999999999999999999999u\x1b[103u");
    ASSERT_TRUE(decoder.next(key));
    EXPECT_TRUE(key.is_char(U'g'));

    decoder.clear();
    decoder.feed(std::string("\x1b[1\x01\x1b[104u", 10));
    bool saw_h = false;
    while (decoder.next(key))
        saw_h = saw_h || key.is_char(U'h');
    EXPECT_TRUE(saw_h);

    decoder.clear();
    decoder.feed("\x1bx");
    ASSERT_TRUE(decoder.next(key));
    EXPECT_EQ(key.code, KeyCode::Escape);
    ASSERT_TRUE(decoder.next(key));
    EXPECT_TRUE(key.is_char(U'x'));
}

TEST(KittyKeys, response_indicates_support_detects_kitty_reply)
{
    bool done = false;
    // Kitty reply (CSI ? flags u) followed by the DA reply (CSI ? ... c).
    EXPECT_TRUE(kitty::response_indicates_support("\x1b[?11u\x1b[?62;1c", done));
    EXPECT_TRUE(done) << "the DA reply ends the handshake";
}

TEST(KittyKeys, response_indicates_support_unsupported_terminal)
{
    bool done = false;
    // Only the DA reply, no kitty 'u' reply -> unsupported, but handshake done.
    EXPECT_FALSE(kitty::response_indicates_support("\x1b[?62;1c", done));
    EXPECT_TRUE(done);
}

TEST(KittyKeys, response_indicates_support_partial_not_done)
{
    bool done = false;
    // The kitty reply arrived but the DA reply has not yet -> supported, not done.
    EXPECT_TRUE(kitty::response_indicates_support("\x1b[?0u", done));
    EXPECT_FALSE(done);
}

TEST(CursesTerminalInternals, pure_helpers_cover_color_write_and_capability_probe)
{
    EXPECT_EQ(0,
              curses_terminal_testing_exercise_internal_helpers());
}
