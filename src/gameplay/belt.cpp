/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/families/belt.h>

#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace og::sim::belt {

namespace {

inline constexpr std::uint8_t kSlotMask = 0x07;
inline constexpr std::uint8_t kAltBit = 0x08;
inline constexpr std::uint8_t kZeroBits = 0x70;  // bits 4-6
inline constexpr int kFirstSpecial = 1;
inline constexpr int kLastSpecial = FD_NUM_SPECIALS - 1;  // 5
inline constexpr int kPlainSlots = 3;

bool is_named(const char* name) noexcept
{
    return name != nullptr && std::strcmp(name, kSpecialNameNone) != 0;
}

const char* entry_name(const FamilyDescriptor* fd, const Entry& entry,
                       short new_specials) noexcept
{
    // rule: A belt entry is its slot's alternate special when it is the alternate (SHIFT) arm, else
    //   the slot's primary special.
    // tests: Belt.catalog_lists_every_named_in_play_entry_in_slot_order
    return entry.alt ? alternate_name(fd, entry.slot, new_specials)
                     : special_name(fd, entry.slot, new_specials);
}

// Slot 1..5 in bits 0-2, alternate in bit 3, nothing in bits 4-7.
std::optional<Entry> decode_well_formed(std::uint8_t byte) noexcept
{
    const int slot = byte & kSlotMask;
    // rule: A belt byte names a special only when it is slot 1..5 plus an optional alternate bit
    //   and nothing else; slot 0/6/7, the item bit or any high bit means 'not a special'.
    // tests: Belt.decode_rejects_slot_0_6_7_item_bit_and_junk
    // tests: Belt.encode_decode_round_trip_every_entry
    // tests: Belt.validate_cast_refuses_every_malformed_byte_as_nothing_there
    // tests: Belt.validate_cast_refuses_locked_unnamed_hidden_and_sentinel
    if ((byte & (kZeroBits | kItemBit)) != 0 || slot < kFirstSpecial ||
        slot > kLastSpecial)
        return std::nullopt;
    return Entry{static_cast<std::uint8_t>(slot), (byte & kAltBit) != 0};
}

Resolved filled(const FamilyDescriptor* fd, const Entry& entry,
                short new_specials)
{
    Resolved r;
    r.entry = entry;
    r.state = SlotState::Castable;
    r.name = entry_name(fd, entry, new_specials);
    r.opens_at = slot_open_level(fd, entry.slot);
    return r;
}

Resolved& at(Layout& layout, int b) noexcept
{
    return layout[static_cast<std::size_t>(b)];
}

const Resolved& at(const Layout& layout, int b) noexcept
{
    return layout[static_cast<std::size_t>(b)];
}

int lowest_free(const Layout& layout, int from, int to) noexcept
{
    // rule: Default belt: primaries take keys 1-3 in catalog order; an alternate sits on SHIFT +
    //   its primary's key; anything that did not fit goes to the lowest free shifted key; more than
    //   six abilities leaves the rest on the bench.
    // tests: Belt.default_layout_matches_the_table
    // tests: Belt.a_ten_entry_pack_benches_four_and_null_has_nothing
    // tests: Belt.default_layout_ignores_level
    for (int b = from; b < to; ++b)
        // rule: Default belt: primaries take keys 1-3 in catalog order.
        // tests: Belt.default_layout_matches_the_table
        // tests: Belt.a_ten_entry_pack_benches_four_and_null_has_nothing
        // tests: Belt.default_layout_ignores_level
        if (!at(layout, b).entry)
            return b;
    return -1;
}

}  // namespace

std::vector<Entry> catalog(const FamilyDescriptor* fd, short new_specials)
{
    std::vector<Entry> out;
    std::vector<const char*> seen;
    // rule: A class's ability catalog is every named, in-play special and alternate in slot order
    //   (primary before alternate), each distinct ability name listed once (the cleric's alternate
    //   TURN UNDEAD duplicate is dropped); unnamed or setting-hidden entries are not abilities.
    // tests: Belt.catalog_lists_every_named_in_play_entry_in_slot_order
    // tests: Belt.catalog_drops_the_clerics_second_turn_undead
    for (int slot = kFirstSpecial; slot <= kLastSpecial; ++slot) {
        // rule: A class's ability catalog is every named, in-play special and alternate in slot
        //   order (primary before alternate), each distinct ability name listed once (the cleric's
        //   alternate TURN UNDEAD duplicate is dropped).
        // tests: Belt.catalog_lists_every_named_in_play_entry_in_slot_order
        // tests: Belt.catalog_drops_the_clerics_second_turn_undead
        for (const bool alt : {false, true}) {
            const Entry entry{static_cast<std::uint8_t>(slot), alt};
            const char* name = entry_name(fd, entry, new_specials);
            // rule: A class's ability catalog is every named, in-play special and alternate in slot
            //   order (primary before alternate), each distinct ability name listed once (the
            //   cleric's alternate TURN UNDEAD duplicate is dropped).
            // tests: Belt.catalog_lists_every_named_in_play_entry_in_slot_order
            // tests: Belt.catalog_drops_the_clerics_second_turn_undead
            if (!is_named(name))
                continue;
            const bool repeat = std::any_of(
                seen.begin(), seen.end(),
                [name](const char* s) { return std::strcmp(s, name) == 0; });
            // rule: A class's ability catalog is every named, in-play special and alternate in slot
            //   order (primary before alternate), each distinct ability name listed once (the
            //   cleric's alternate TURN UNDEAD duplicate is dropped).
            // tests: Belt.catalog_lists_every_named_in_play_entry_in_slot_order
            // tests: Belt.catalog_drops_the_clerics_second_turn_undead
            if (repeat)
                continue;
            seen.push_back(name);
            out.push_back(entry);
        }
    }
    return out;
}

Bytes default_bytes() noexcept
{
    Bytes bytes;
    bytes.fill(kDefaultByte);
    return bytes;
}

Layout default_layout(const FamilyDescriptor* fd, short new_specials)
{
    Layout layout{};
    // The plain slot each special slot's primary took, so its alternate can
    // sit on SHIFTER + the same key.
    int primary_at[FD_NUM_SPECIALS];
    std::fill(std::begin(primary_at), std::end(primary_at), -1);
    // rule: Default belt: primaries take keys 1-3 in catalog order.
    // tests: Belt.default_layout_matches_the_table
    // tests: Belt.a_ten_entry_pack_benches_four_and_null_has_nothing
    // tests: Belt.default_layout_ignores_level
    for (const Entry& entry : catalog(fd, new_specials)) {
        int b = -1;
        // rule: Default belt: primaries take keys 1-3 in catalog order.
        // tests: Belt.default_layout_matches_the_table
        // tests: Belt.a_ten_entry_pack_benches_four_and_null_has_nothing
        // tests: Belt.default_layout_ignores_level
        if (!entry.alt) {
            b = lowest_free(layout, 0, kPlainSlots);
            // rule: Default belt: primaries take keys 1-3 in catalog order.
            // tests: Belt.default_layout_matches_the_table
            // tests: Belt.a_ten_entry_pack_benches_four_and_null_has_nothing
            // tests: Belt.default_layout_ignores_level
            if (b >= 0)
                primary_at[entry.slot] = b;
        } else if (primary_at[entry.slot] >= 0 &&
                   !at(layout, primary_at[entry.slot] + kPlainSlots).entry) {
            b = primary_at[entry.slot] + kPlainSlots;
        }
        // rule: Default belt: primaries take keys 1-3 in catalog order.
        // tests: Belt.default_layout_matches_the_table
        // tests: Belt.a_ten_entry_pack_benches_four_and_null_has_nothing
        // tests: Belt.default_layout_ignores_level
        if (b < 0)
            b = lowest_free(layout, kPlainSlots, kSlots);
        // rule: Default belt: primaries take keys 1-3 in catalog order.
        // tests: Belt.default_layout_matches_the_table
        // tests: Belt.a_ten_entry_pack_benches_four_and_null_has_nothing
        // tests: Belt.default_layout_ignores_level
        if (b >= 0)
            at(layout, b) = filled(fd, entry, new_specials);
    }
    return layout;
}

Layout resolve(const FamilyDescriptor* fd, int level, short new_specials,
               const Bytes& stored)
{
    const Layout defaults = default_layout(fd, new_specials);
    const std::vector<Entry> known = catalog(fd, new_specials);
    Layout layout{};
    // rule: A stored belt key means: 0 = 'the default ability for this key', an explicit entry =
    //   that ability if the class still has it under the current setting, anything else (FF empty,
    //   an item byte, an entry not in the catalog) = an empty key; the stored byte is never
    //   rewritten.
    // tests: Belt.resolve_zero_is_default_ff_is_empty_explicit_is_explicit
    // tests: Belt.resolve_hides_an_entry_the_setting_hides_without_rewriting_the_byte
    // tests: Belt.default_layout_matches_the_table
    for (std::size_t b = 0; b < layout.size(); ++b) {
        const std::uint8_t byte = normalize_stored(stored[b]);
        // rule: A stored belt key means: 0 = 'the default ability for this key', an explicit entry
        //   = that ability if the class still has it under the current setting, anything else (FF
        //   empty, an item byte, an entry not in the catalog) = an empty key.
        // tests: Belt.resolve_zero_is_default_ff_is_empty_explicit_is_explicit
        // tests: Belt.resolve_hides_an_entry_the_setting_hides_without_rewriting_the_byte
        // tests: Belt.default_layout_matches_the_table
        if (byte == kDefaultByte) {
            layout[b] = defaults[b];
        } else if (const std::optional<Entry> entry = decode_stored(byte)) {
            // rule: A stored belt key means: 0 = 'the default ability for this key', an explicit
            //   entry = that ability if the class still has it under the current setting, anything
            //   else (FF empty, an item byte, an entry not in the catalog) = an empty key.
            // tests: Belt.resolve_zero_is_default_ff_is_empty_explicit_is_explicit
            // tests: Belt.resolve_hides_an_entry_the_setting_hides_without_rewriting_the_byte
            // tests: Belt.default_layout_matches_the_table
            if (std::find(known.begin(), known.end(), *entry) != known.end())
                layout[b] = filled(fd, *entry, new_specials);
        }
        // EMPTY, an item byte, or an entry not in the catalog: Empty.
        // rule: An ability on the belt whose slot the hero's level has not opened yet shows as
        //   Locked (with the level it opens at) rather than vanishing.
        // tests: Belt.resolve_marks_locked_with_opens_at
        if (layout[b].entry && !slot_open(fd, layout[b].entry->slot, level))
            layout[b].state = SlotState::Locked;
    }
    return layout;
}

std::vector<Entry> bench(const FamilyDescriptor* fd, short new_specials,
                         const Layout& layout)
{
    std::vector<Entry> out;
    // rule: The bench is exactly the class's catalog minus whatever is on the belt.
    // tests: Belt.bench_is_catalog_minus_belt
    for (const Entry& entry : catalog(fd, new_specials)) {
        const bool on_belt = std::any_of(
            layout.begin(), layout.end(),
            [&entry](const Resolved& r) { return r.entry == entry; });
        // rule: The bench is exactly the class's catalog minus whatever is on the belt.
        // tests: Belt.bench_is_catalog_minus_belt
        if (!on_belt)
            out.push_back(entry);
    }
    return out;
}

std::uint8_t encode(const Entry& entry) noexcept
{
    // rule: An alternate is encoded as its slot with the alternate bit; encode/decode round-trip
    //   every entry.
    // tests: Belt.encode_decode_round_trip_every_entry
    return static_cast<std::uint8_t>((entry.slot & kSlotMask) |
                                     (entry.alt ? kAltBit : 0));
}

std::uint8_t normalize_stored(std::uint8_t stored) noexcept
{
    // rule: A save's belt byte that is junk is read as 'default'; default, empty, item and
    //   well-formed entry bytes are kept as written.
    // tests: Belt.normalize_stored_maps_junk_to_default_and_keeps_empty_item_and_explicit
    // tests: SaveDataVersions.v21_belt_reader_normalises_junk_to_default
    if (stored == kDefaultByte || stored == kEmptyByte ||
        (stored & kItemBit) != 0 || decode_well_formed(stored))
        return stored;
    return kDefaultByte;
}

std::optional<Entry> decode_stored(std::uint8_t stored) noexcept
{
    return decode_well_formed(stored);
}

std::uint8_t cast_byte(const Resolved& resolved) noexcept
{
    // rule: Pressing an empty belt key sends 'cast nothing'; a Locked key still sends its entry so
    //   the sim can refuse it with the opens-at message.
    // tests: Belt.cast_byte_and_slot_index, BeltInput.empty_slot_latches_cast_nothing
    // tests: BeltInput.locked_slot_latches_its_entry
    // tests: Belt.resolve_hides_an_entry_the_setting_hides_without_rewriting_the_byte
    // tests: Belt.resolve_marks_locked_with_opens_at
    if (resolved.state == SlotState::Empty || !resolved.entry)
        return kCastNothing;
    return encode(*resolved.entry);
}

int slot_index(int key, bool shift) noexcept
{
    // rule: SPEC key k reaches belt position k-1, or k+2 with SHIFT held.
    // tests: Belt.cast_byte_and_slot_index
    return (key - 1) + (shift ? kPlainSlots : 0);
}

Bytes bytes_for(const Bytes* stored) noexcept
{
    // rule: A walker with no hero record (a bot, a summon) uses the all-default belt; a hero uses
    //   its saved belt.
    // tests: Belt.bytes_for_walker_without_guy_is_default
    // tests: Belt.bytes_for_walker_reads_the_guys_belt_or_the_default
    return stored != nullptr ? *stored : default_bytes();
}

Bytes bytes_for(const walker& w) noexcept
{
    // rule: A walker with no hero record (a bot, a summon) uses the all-default belt; a hero uses
    //   its saved belt.
    // tests: Belt.bytes_for_walker_without_guy_is_default
    // tests: Belt.bytes_for_walker_reads_the_guys_belt_or_the_default
    return bytes_for(w.myguy != nullptr ? &w.myguy->belt : nullptr);
}

Refusal validate_cast(const FamilyDescriptor* fd, int level,
                      short new_specials, std::uint8_t belt_cast, Entry* out,
                      int* opens_at)
{
    const std::optional<Entry> entry = decode_well_formed(belt_cast);
    // rule: A belt cast is refused NOTHING THERE when the byte names no ability the class has under
    //   the current setting, refused LOCKED (reporting the opening level) when the hero's level has
    //   not opened the slot, and otherwise accepted.
    // tests: Belt.validate_cast_refuses_every_malformed_byte_as_nothing_there
    // tests: Belt.validate_cast_refuses_locked_unnamed_hidden_and_sentinel
    if (!entry || !is_named(entry_name(fd, *entry, new_specials)))
        return Refusal::NothingThere;
    if (!slot_open(fd, entry->slot, level)) {
        // rule: A belt cast is refused NOTHING THERE when the byte names no ability the class has
        //   under the current setting, refused LOCKED (reporting the opening level) when the hero's
        //   level has not opened the slot, and otherwise accepted.
        // tests: Belt.validate_cast_refuses_every_malformed_byte_as_nothing_there
        // tests: Belt.validate_cast_refuses_locked_unnamed_hidden_and_sentinel
        if (opens_at != nullptr)
            *opens_at = slot_open_level(fd, entry->slot);
        return Refusal::Locked;
    }
    if (out != nullptr)
        *out = *entry;
    return Refusal::None;
}

}  // namespace og::sim::belt
