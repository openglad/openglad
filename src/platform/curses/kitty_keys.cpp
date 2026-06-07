/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Pure Kitty keyboard protocol decoder. See kitty_keys.h.
 */
#include <openglad/platform/curses/kitty_keys.h>

#include <cstddef>

namespace og::curses::kitty {

namespace {

constexpr unsigned char kEsc = 0x1b;

// One parse step's outcome.
enum class Status {
    NeedMore, // not enough bytes buffered yet for a complete sequence
    Skip,     // a complete sequence that is not a key event (consumed, no Key)
    Emit,     // a complete key event (consumed, Key produced)
};

KeyEvent decode_event(long e)
{
    switch (e) {
    case 2:  return KeyEvent::Repeat;
    case 3:  return KeyEvent::Release;
    case 1:
    default: return KeyEvent::Press;
    }
}

// Kitty encodes modifiers as (bitfield + 1); bits: shift=1, alt=2, ctrl=4, super=8.
KeyMods decode_mods(long m)
{
    KeyMods mods;
    if (m <= 1)
        return mods;
    const long bits = m - 1;
    mods.shift = (bits & 1) != 0;
    mods.alt   = (bits & 2) != 0;
    mods.ctrl  = (bits & 4) != 0;
    mods.super = (bits & 8) != 0;
    return mods;
}

// Split a CSI parameter run ("113;1:3") into the fields we care about:
//   group 0, sub 0  -> p0        (key codepoint for 'u', number for '~')
//   group 1, sub 0  -> modifiers
//   group 1, sub 1  -> event type
// Absent fields keep their defaults.
void parse_params(std::string_view params, long& p0, long& modifiers, long& event)
{
    int group = 0;
    int sub = 0;
    long acc = 0;
    bool have_digit = false;
    auto commit = [&]() {
        if (have_digit) {
            if (group == 0 && sub == 0)
                p0 = acc;
            else if (group == 1 && sub == 0)
                modifiers = acc;
            else if (group == 1 && sub == 1)
                event = acc;
        }
        acc = 0;
        have_digit = false;
    };
    for (char c : params) {
        if (c >= '0' && c <= '9') {
            acc = acc * 10 + (c - '0');
            have_digit = true;
        } else if (c == ':') {
            commit();
            ++sub;
        } else if (c == ';') {
            commit();
            ++group;
            sub = 0;
        }
    }
    commit();
}

// Decode a "CSI codepoint ; mods:event u" key event.
Status decode_u(long cp, KeyMods mods, KeyEvent ev, Key& key)
{
    if (cp < 0 || cp >= 0x110000)
        return Status::Skip;
    switch (cp) {
    case 13: key = Key::special(KeyCode::Enter, ev, mods);     return Status::Emit;
    case 9:  key = Key::special(KeyCode::Tab, ev, mods);       return Status::Emit;
    case 27: key = Key::special(KeyCode::Escape, ev, mods);    return Status::Emit;
    case 8:
    case 127: key = Key::special(KeyCode::Backspace, ev, mods); return Status::Emit;
    // Standalone modifier keys (Kitty functional codepoints in the PUA).
    case 57441: key = Key::special(KeyCode::LeftShift, ev, mods);  return Status::Emit;
    case 57442: key = Key::special(KeyCode::LeftCtrl, ev, mods);   return Status::Emit;
    case 57443: key = Key::special(KeyCode::LeftAlt, ev, mods);    return Status::Emit;
    case 57444: key = Key::special(KeyCode::LeftSuper, ev, mods);  return Status::Emit;
    case 57447: key = Key::special(KeyCode::RightShift, ev, mods); return Status::Emit;
    case 57448: key = Key::special(KeyCode::RightCtrl, ev, mods);  return Status::Emit;
    case 57449: key = Key::special(KeyCode::RightAlt, ev, mods);   return Status::Emit;
    case 57450: key = Key::special(KeyCode::RightSuper, ev, mods); return Status::Emit;
    default: break;
    }
    // Anything else with a real Unicode value is a printable character. The base
    // layout key is reported (lowercase for letters); curses_input folds case.
    key = Key::character(static_cast<char32_t>(cp), ev, mods);
    return Status::Emit;
}

// Decode a "CSI number ; mods:event ~" functional key.
Status decode_tilde(long n, KeyMods mods, KeyEvent ev, Key& key)
{
    KeyCode code;
    switch (n) {
    case 2:  code = KeyCode::Insert;   break;
    case 3:  code = KeyCode::Delete;   break;
    case 5:  code = KeyCode::PageUp;   break;
    case 6:  code = KeyCode::PageDown; break;
    case 1:
    case 7:  code = KeyCode::Home;     break;
    case 4:
    case 8:  code = KeyCode::End;      break;
    case 15: code = KeyCode::F5;       break;
    case 17: code = KeyCode::F6;       break;
    case 18: code = KeyCode::F7;       break;
    case 19: code = KeyCode::F8;       break;
    case 20: code = KeyCode::F9;       break;
    case 21: code = KeyCode::F10;      break;
    case 23: code = KeyCode::F11;      break;
    case 24: code = KeyCode::F12;      break;
    default: return Status::Skip;
    }
    key = Key::special(code, ev, mods);
    return Status::Emit;
}

// Decode a "CSI 1 ; mods:event <letter>" key (arrows, Home/End, F1-F4) or a focus
// event (CSI I / CSI O).
Status decode_letter(char final, KeyMods mods, KeyEvent ev, Key& key)
{
    switch (final) {
    case 'A': key = Key::special(KeyCode::Up, ev, mods);    return Status::Emit;
    case 'B': key = Key::special(KeyCode::Down, ev, mods);  return Status::Emit;
    case 'C': key = Key::special(KeyCode::Right, ev, mods); return Status::Emit;
    case 'D': key = Key::special(KeyCode::Left, ev, mods);  return Status::Emit;
    case 'H': key = Key::special(KeyCode::Home, ev, mods);  return Status::Emit;
    case 'F': key = Key::special(KeyCode::End, ev, mods);   return Status::Emit;
    case 'P': key = Key::special(KeyCode::F1, ev, mods);    return Status::Emit;
    case 'Q': key = Key::special(KeyCode::F2, ev, mods);    return Status::Emit;
    case 'S': key = Key::special(KeyCode::F4, ev, mods);    return Status::Emit;
    case 'I': key = Key::special(KeyCode::FocusIn);         return Status::Emit;
    case 'O': key = Key::special(KeyCode::FocusOut);        return Status::Emit;
    default:  return Status::Skip; // unknown CSI final byte: ignore the sequence
    }
}

bool is_csi_final(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x40 && u <= 0x7e;
}

// Parse the CSI sequence beginning at b[0..] (b starts with ESC '[').
Status parse_csi(std::string_view b, std::size_t& consumed, Key& key)
{
    std::size_t i = 2;
    bool priv = false;
    if (i < b.size() && b[i] == '?') {
        priv = true;
        ++i;
    }
    const std::size_t params_start = i;
    while (i < b.size()) {
        const char c = b[i];
        if ((c >= '0' && c <= '9') || c == ';' || c == ':') {
            ++i;
            continue;
        }
        break;
    }
    if (i >= b.size())
        return Status::NeedMore; // final byte not yet buffered
    const char final = b[i];
    if (!is_csi_final(final)) {
        // Garbage byte where a final byte should be; drop the ESC and resync.
        consumed = 1;
        return Status::Skip;
    }
    const std::string_view params = b.substr(params_start, i - params_start);
    consumed = i + 1;

    // Private sequences (CSI ? ... u/c, DEC responses) are not key events.
    if (priv)
        return Status::Skip;

    long p0 = -1;
    long modifiers = 1;
    long event = 1;
    parse_params(params, p0, modifiers, event);
    const KeyMods mods = decode_mods(modifiers);
    const KeyEvent ev = decode_event(event);

    switch (final) {
    case 'u': return decode_u(p0, mods, ev, key);
    case '~': return decode_tilde(p0, mods, ev, key);
    default:  return decode_letter(final, mods, ev, key);
    }
}

// Decode one raw (non-escape) byte as a UTF-8 character. This is a defensive
// fallback — under the protocol every key arrives as an escape sequence — but it
// keeps stray bytes from wedging the buffer.
Status parse_raw_char(std::string_view b, std::size_t& consumed, Key& key)
{
    const unsigned char c0 = static_cast<unsigned char>(b[0]);
    std::size_t len = 1;
    char32_t cp = 0;
    if (c0 < 0x80) {
        len = 1;
        cp = c0;
    } else if ((c0 >> 5) == 0x6) {
        len = 2;
        cp = c0 & 0x1fu;
    } else if ((c0 >> 4) == 0xe) {
        len = 3;
        cp = c0 & 0x0fu;
    } else if ((c0 >> 3) == 0x1e) {
        len = 4;
        cp = c0 & 0x07u;
    } else {
        consumed = 1; // invalid lead byte
        return Status::Skip;
    }
    if (b.size() < len)
        return Status::NeedMore;
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned char cc = static_cast<unsigned char>(b[k]);
        if ((cc >> 6) != 0x2) {
            consumed = 1; // malformed continuation
            return Status::Skip;
        }
        cp = (cp << 6) | (cc & 0x3fu);
    }
    consumed = len;
    if (cp == 13 || cp == 10) { key = Key::special(KeyCode::Enter);     return Status::Emit; }
    if (cp == 9)              { key = Key::special(KeyCode::Tab);       return Status::Emit; }
    if (cp == 8 || cp == 127) { key = Key::special(KeyCode::Backspace); return Status::Emit; }
    if (cp == 27)             { key = Key::special(KeyCode::Escape);    return Status::Emit; }
    key = Key::character(cp);
    return Status::Emit;
}

Status parse_one(std::string_view b, std::size_t& consumed, Key& key)
{
    if (b.empty())
        return Status::NeedMore;
    if (static_cast<unsigned char>(b[0]) != kEsc)
        return parse_raw_char(b, consumed, key);
    if (b.size() < 2)
        return Status::NeedMore;
    const char b1 = b[1];
    if (b1 == '[')
        return parse_csi(b, consumed, key);
    if (b1 == 'O') {
        // SS3: ESC O {P,Q,R,S} -> F1..F4 (some terminals still use this form).
        if (b.size() < 3)
            return Status::NeedMore;
        consumed = 3;
        switch (b[2]) {
        case 'P': key = Key::special(KeyCode::F1); return Status::Emit;
        case 'Q': key = Key::special(KeyCode::F2); return Status::Emit;
        case 'R': key = Key::special(KeyCode::F3); return Status::Emit;
        case 'S': key = Key::special(KeyCode::F4); return Status::Emit;
        default:  return Status::Skip;
        }
    }
    // A bare ESC (or ESC + an unmodelled byte). Under the protocol Escape arrives
    // as "CSI 27 u", so this is rare; emit Escape and let the next byte resync.
    consumed = 1;
    key = Key::special(KeyCode::Escape);
    return Status::Emit;
}

} // namespace

bool Decoder::next(Key& out)
{
    while (!buf_.empty()) {
        std::size_t consumed = 0;
        Key key;
        const Status st = parse_one(buf_, consumed, key);
        if (st == Status::NeedMore)
            return false;
        buf_.erase(0, consumed);
        if (st == Status::Emit) {
            out = key;
            return true;
        }
        // Status::Skip: drop the consumed bytes and try the next sequence.
    }
    return false;
}

bool response_indicates_support(std::string_view reply, bool& done)
{
    done = false;
    bool support = false;
    std::size_t i = 0;
    while (i + 1 < reply.size()) {
        if (static_cast<unsigned char>(reply[i]) == kEsc && reply[i + 1] == '[') {
            const bool priv = (i + 2 < reply.size() && reply[i + 2] == '?');
            std::size_t j = i + 2;
            while (j < reply.size() && !is_csi_final(reply[j]))
                ++j;
            if (j >= reply.size())
                break; // incomplete trailing sequence
            const char final = reply[j];
            if (priv && final == 'u')
                support = true;
            if (priv && final == 'c')
                done = true; // Primary Device Attributes reply: handshake over
            i = j + 1;
            continue;
        }
        ++i;
    }
    return support;
}

} // namespace og::curses::kitty
