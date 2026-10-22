/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/families/belt.h>

#include <openglad/gameplay/families/specials_view.h>

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
    return entry.alt ? alternate_name(fd, entry.slot, new_specials)
                     : special_name(fd, entry.slot, new_specials);
}

// Slot 1..5 in bits 0-2, alternate in bit 3, nothing in bits 4-7.
std::optional<Entry> decode_well_formed(std::uint8_t byte) noexcept
{
    const int slot = byte & kSlotMask;
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
    for (int b = from; b < to; ++b)
        if (!at(layout, b).entry)
            return b;
    return -1;
}

}  // namespace

std::vector<Entry> catalog(const FamilyDescriptor* fd, short new_specials)
{
    std::vector<Entry> out;
    std::vector<const char*> seen;
    for (int slot = kFirstSpecial; slot <= kLastSpecial; ++slot) {
        for (const bool alt : {false, true}) {
            const Entry entry{static_cast<std::uint8_t>(slot), alt};
            const char* name = entry_name(fd, entry, new_specials);
            if (!is_named(name))
                continue;
            const bool repeat = std::any_of(
                seen.begin(), seen.end(),
                [name](const char* s) { return std::strcmp(s, name) == 0; });
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
    for (const Entry& entry : catalog(fd, new_specials)) {
        int b = -1;
        if (!entry.alt) {
            b = lowest_free(layout, 0, kPlainSlots);
            if (b >= 0)
                primary_at[entry.slot] = b;
        } else if (primary_at[entry.slot] >= 0 &&
                   !at(layout, primary_at[entry.slot] + kPlainSlots).entry) {
            b = primary_at[entry.slot] + kPlainSlots;
        }
        if (b < 0)
            b = lowest_free(layout, kPlainSlots, kSlots);
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
    for (std::size_t b = 0; b < layout.size(); ++b) {
        const std::uint8_t byte = normalize_stored(stored[b]);
        if (byte == kDefaultByte) {
            layout[b] = defaults[b];
        } else if (const std::optional<Entry> entry = decode_stored(byte)) {
            if (std::find(known.begin(), known.end(), *entry) != known.end())
                layout[b] = filled(fd, *entry, new_specials);
        }
        // EMPTY, an item byte, or an entry not in the catalog: Empty.
        if (layout[b].entry && !slot_open(fd, layout[b].entry->slot, level))
            layout[b].state = SlotState::Locked;
    }
    return layout;
}

std::vector<Entry> bench(const FamilyDescriptor* fd, short new_specials,
                         const Layout& layout)
{
    std::vector<Entry> out;
    for (const Entry& entry : catalog(fd, new_specials)) {
        const bool on_belt = std::any_of(
            layout.begin(), layout.end(),
            [&entry](const Resolved& r) { return r.entry == entry; });
        if (!on_belt)
            out.push_back(entry);
    }
    return out;
}

std::uint8_t encode(const Entry& entry) noexcept
{
    return static_cast<std::uint8_t>((entry.slot & kSlotMask) |
                                     (entry.alt ? kAltBit : 0));
}

std::uint8_t normalize_stored(std::uint8_t stored) noexcept
{
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
    if (resolved.state == SlotState::Empty || !resolved.entry)
        return kCastNothing;
    return encode(*resolved.entry);
}

int slot_index(int key, bool shift) noexcept
{
    return (key - 1) + (shift ? kPlainSlots : 0);
}

Bytes bytes_for(const Bytes* stored) noexcept
{
    return stored != nullptr ? *stored : default_bytes();
}

Refusal validate_cast(const FamilyDescriptor* fd, int level,
                      short new_specials, std::uint8_t belt_cast, Entry* out,
                      int* opens_at)
{
    const std::optional<Entry> entry = decode_well_formed(belt_cast);
    if (!entry || !is_named(entry_name(fd, *entry, new_specials)))
        return Refusal::NothingThere;
    if (!slot_open(fd, entry->slot, level)) {
        if (opens_at != nullptr)
            *opens_at = slot_open_level(fd, entry->slot);
        return Refusal::Locked;
    }
    if (out != nullptr)
        *out = *entry;
    return Refusal::None;
}

}  // namespace og::sim::belt
