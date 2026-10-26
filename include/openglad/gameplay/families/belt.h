/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// New Ability Select: the belt model. SDL-free, no RNG, no side effects.
//
// Words (docs/ability-select-design.md):
//   ability  one castable thing of a class: a special slot's primary, or
//            that slot's alternate.
//   entry    how an ability is named: (special slot 1..5, alternate yes/no).
//   catalog  the entries a family has this session, in slot order:
//            (1,0) (1,1) (2,0) (2,1) ... (5,1), keeping only the named ones
//            the New Specials setting leaves in play, and dropping an entry
//            whose name repeats an earlier one (the cleric's TURN UNDEAD is
//            the alternate of both RAISE UNDEAD and RAISE GHOST; it is
//            listed once, under RAISE UNDEAD). Locked entries stay listed.
//   belt     six slots: 0,1,2 = SPEC 1/2/3; 3,4,5 = SPEC 1/2/3 with
//            SHIFTER held. A slot holds one entry or is empty.
//   bench    every catalog entry not on the resolved belt. Never stored.
//   locked   an entry whose special slot the hero's level has not opened.
//
// Every fact about a family is read through og::sim::specials_view
// (special_name, alternate_name, slot_open, slot_open_level), so the belt
// sees exactly what the HUD, the cycle key and the bots see.

#include <openglad/gameplay/families/family_descriptor.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

class walker;

namespace og::sim::belt {

inline constexpr int kSlots = 6;

// A belt byte at rest (the guy, the save record, the guy's wire structs):
//   0x00          DEFAULT: whatever the class default puts in this slot (an
//                 untouched guy is six zeros)
//   0xFF          EMPTY: the player cleared this slot
//   bits 0-2      special slot 1..5; bit 3 = alternate; bits 4-6 zero
//   bit 7 (0x80)  reserved for item abilities (#177); never produced today,
//                 shown as an empty slot
//   anything else junk, read as DEFAULT (normalize_stored)
inline constexpr std::uint8_t kDefaultByte = 0x00;
inline constexpr std::uint8_t kEmptyByte = 0xFF;
inline constexpr std::uint8_t kItemBit = 0x80;
// PlayerInput::belt_cast for "nothing on this key": the server says NO
// ABILITY THERE and casts nothing. On the input wire 0 means the classic
// cast (the walker's current slot); every other value is an entry and goes
// through validate_cast.
inline constexpr std::uint8_t kCastNothing = 0x07;

struct Entry {
    std::uint8_t slot = 0;  // 1..5
    bool alt = false;
    friend bool operator==(const Entry&, const Entry&) = default;
};

using Bytes = std::array<std::uint8_t, kSlots>;

enum class SlotState : std::uint8_t { Castable, Locked, Empty };

// One belt slot as a player sees it. An Empty slot has no entry, the name
// kSpecialNameNone and opens_at 0; a Castable or Locked slot names its
// ability and the level its special slot opens at.
struct Resolved {
    std::optional<Entry> entry;
    SlotState state = SlotState::Empty;
    const char* name = kSpecialNameNone;
    int opens_at = 0;
};

using Layout = std::array<Resolved, kSlots>;

// The family's entries this session (see "catalog" above).
[[nodiscard]] std::vector<Entry> catalog(const FamilyDescriptor* fd,
                                         short new_specials);
// Six DEFAULT bytes: an untouched guy.
[[nodiscard]] Bytes default_bytes() noexcept;
// The class default, "pair-first": walking the catalog in slot order, a
// primary takes the lowest free plain slot (0..2); its alternate takes the
// same key's SHIFTER slot (b + 3) when free, else the lowest free SHIFTER
// slot (3..5); a primary with no plain slot left takes the lowest free
// SHIFTER slot; what is left stays on the bench. Never reads a level: every
// filled slot is Castable here, and resolve() marks the locked ones.
[[nodiscard]] Layout default_layout(const FamilyDescriptor* fd,
                                    short new_specials);
// The belt a hero of `level` has from `stored`: per slot, the byte is
// normalised; DEFAULT takes the class default's slot, EMPTY and an item
// byte read Empty, an explicit entry reads as itself when it is in the
// catalog and Empty otherwise (the byte is never rewritten, so an entry the
// New Specials setting hides comes back when the setting does). A filled
// slot is Locked below its slot's opening level, else Castable.
[[nodiscard]] Layout resolve(const FamilyDescriptor* fd, int level,
                             short new_specials, const Bytes& stored);
// Catalog entries not on `layout`, in catalog order.
[[nodiscard]] std::vector<Entry> bench(const FamilyDescriptor* fd,
                                       short new_specials,
                                       const Layout& layout);
// slot | alt << 3.
[[nodiscard]] std::uint8_t encode(const Entry& entry) noexcept;
// The at-rest table above: junk -> kDefaultByte; DEFAULT, EMPTY, an item
// byte and a well-formed explicit entry are returned as they are.
[[nodiscard]] std::uint8_t normalize_stored(std::uint8_t stored) noexcept;
// The entry a stored byte names: only a well-formed explicit entry;
// DEFAULT, EMPTY, item bytes and junk -> nullopt.
[[nodiscard]] std::optional<Entry> decode_stored(std::uint8_t stored) noexcept;
// What a key press on this slot puts on the input wire: the entry's byte
// for a Castable or Locked slot (the server refuses the locked one and says
// why), kCastNothing for an Empty one.
[[nodiscard]] std::uint8_t cast_byte(const Resolved& resolved) noexcept;
// The belt slot of SPEC `key` (1..3), on the SHIFTER row when `shift`.
[[nodiscard]] int slot_index(int key, bool shift) noexcept;
// The stored bytes for a hero: `stored` when it has some (the guy's belt),
// DEFAULT bytes when it has none (a summoned ally, a raised skeleton, a
// possessed host, a cross-controlled bot). The walker overload is a thin
// call of this one.
[[nodiscard]] Bytes bytes_for(const Bytes* stored) noexcept;
// The walker's stored bytes: its guy's belt when it has a guy, DEFAULT bytes
// otherwise.
[[nodiscard]] Bytes bytes_for(const walker& w) noexcept;

// The server's gate for every nonzero PlayerInput::belt_cast: the byte is
// well-formed (slot 1..5 in bits 0-2, alternate in bit 3, bits 4-7 zero),
// its ability is named and in play, and its special slot is open at
// `level`. kCastNothing and every malformed byte -> NothingThere; a named
// entry the level has not opened -> Locked with *opens_at set. On None,
// *out holds the entry. Either pointer may be null. Unlike resolve(), this
// does not ask the catalog: the cleric's second TURN UNDEAD, (3, alt),
// still casts from a byte that names it.
enum class Refusal { None, NothingThere, Locked };
[[nodiscard]] Refusal validate_cast(const FamilyDescriptor* fd, int level,
                                    short new_specials,
                                    std::uint8_t belt_cast, Entry* out,
                                    int* opens_at);

}  // namespace og::sim::belt
