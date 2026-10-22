/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Ability Select: the belt model (og::sim::belt).
//
// The class defaults are pinned as LITERAL names, for every core family with
// the New Specials setting on and off, read through the real core pack: a
// pack edit that moves a name, a slot or a new_kit flag shows up here as a
// changed cell, and so does any change to the pair-first rule. The byte
// tables (at rest and on the input wire) are walked over all 256 values.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace belt = og::sim::belt;
using belt::Entry;
using belt::Layout;
using belt::Refusal;
using belt::SlotState;

namespace {

constexpr std::size_t kN = static_cast<std::size_t>(belt::kSlots);

const FamilyDescriptor* fd_of(int family)
{
    init_family_registry();
    return get_family_descriptor(family);
}

std::string entry_text(const FamilyDescriptor* fd, const Entry& e, short ns)
{
    // Read the names back the way the HUD does, through resolve(): an entry
    // in slot 0 of an otherwise empty belt.
    belt::Bytes bytes;
    bytes.fill(belt::kEmptyByte);
    bytes[0] = belt::encode(e);
    const Layout l = belt::resolve(fd, 99, ns, bytes);
    return std::to_string(e.slot) + (e.alt ? "+ " : " ") +
        (l[0].entry ? l[0].name : "?");
}

std::string catalog_text(int family, short ns)
{
    const FamilyDescriptor* fd = fd_of(family);
    std::string out;
    for (const Entry& e : belt::catalog(fd, ns)) {
        if (!out.empty())
            out += ", ";
        out += entry_text(fd, e, ns);
    }
    return out;
}

// "1 | 2 | 3 | S1 | S2 | S3 || bench", "-" for an empty slot.
std::string layout_text(const FamilyDescriptor* fd, short ns, const Layout& l)
{
    std::string out;
    for (std::size_t b = 0; b < kN; ++b) {
        if (b > 0)
            out += " | ";
        out += l[b].state == SlotState::Empty ? "-" : l[b].name;
    }
    out += " ||";
    for (const Entry& e : belt::bench(fd, ns, l)) {
        belt::Bytes bytes;
        bytes.fill(belt::kEmptyByte);
        bytes[0] = belt::encode(e);
        out += " ";
        out += belt::resolve(fd, 99, ns, bytes)[0].name;
    }
    return out;
}

std::string default_text(int family, short ns)
{
    const FamilyDescriptor* fd = fd_of(family);
    return layout_text(fd, ns, belt::default_layout(fd, ns));
}

struct FamilyRow {
    int family;
    const char* label;
    const char* on;   // New Specials on
    const char* off;  // New Specials off
};

}  // namespace

// The catalog: every named, in-play entry, slot order, primary before its
// alternate. Proof it can fail: listing the alternate before the primary in
// catalog() turned the mage row into "1+ TELEPORT MARKER, 1 TELEPORT, ...".
TEST(Belt, catalog_lists_every_named_in_play_entry_in_slot_order)
{
    const FamilyRow rows[] = {
        {FAMILY_SOLDIER, "soldier",
         "1 CHARGE, 2 BOOMERANG, 3 WHIRLWIND, 4 DISARM",
         "1 CHARGE, 2 BOOMERANG, 3 WHIRLWIND, 4 DISARM"},
        {FAMILY_ELF, "elf",
         "1 ROCKS, 2 BOUNCING ROCKS, 3 LOTS OF ROCKS, 4 MEGA ROCKS",
         "1 ROCKS, 2 BOUNCING ROCKS, 3 LOTS OF ROCKS, 4 MEGA ROCKS"},
        {FAMILY_ARCHER, "archer", "1 FIRE ARROWS, 2 BARRAGE, 3 EXPLODING BOLT",
         "1 FIRE ARROWS, 2 BARRAGE, 3 EXPLODING BOLT"},
        {FAMILY_MAGE, "mage",
         "1 TELEPORT, 1+ TELEPORT MARKER, 2 WARP SPACE, 3 FREEZE TIME, "
         "4 ENERGY WAVE, 5 HEARTBURST",
         "1 TELEPORT, 1+ TELEPORT MARKER, 2 WARP SPACE, 3 FREEZE TIME, "
         "4 ENERGY WAVE, 5 HEARTBURST"},
        {FAMILY_SKELETON, "skeleton",
         "1 TUNNEL, 2 DIG IN, 3 BONE WALL, 3+ BONE STORM, 4 REASSEMBLE, "
         "4+ LEGION",
         "1 TUNNEL"},
        {FAMILY_CLERIC, "cleric",
         "1 HEAL, 1+ MYSTIC MACE, 2 RAISE UNDEAD, 2+ TURN UNDEAD, "
         "3 RAISE GHOST, 4 RESURRECT",
         "1 HEAL, 1+ MYSTIC MACE, 2 RAISE UNDEAD, 2+ TURN UNDEAD, "
         "3 RAISE GHOST, 4 RESURRECT"},
        {FAMILY_FIREELEMENTAL, "elemental",
         "1 STARBURST, 2 IMMOLATE, 3 METEOR RAIN, 4 REKINDLE, 4+ SUPERNOVA",
         "1 STARBURST"},
        {FAMILY_FAERIE, "faerie",
         "1 BLINK, 1+ SWAP, 2 GLIMMER, 3 HASTEN, 3+ HASTE SELF, 4 WISH", ""},
        {FAMILY_SLIME, "slime", "1 SPLIT", "1 SPLIT"},
        {FAMILY_SMALL_SLIME, "small slime", "1 GROW", "1 GROW"},
        {FAMILY_MEDIUM_SLIME, "medium slime", "1 GROW", "1 GROW"},
        {FAMILY_THIEF, "thief",
         "1 DROP BOMB, 1+ MINE, 2 CLOAK, 3 TAUNT ENEMY, 3+ CHARM OPPONENT, "
         "4 POISON CLOUD",
         "1 DROP BOMB, 2 CLOAK, 3 TAUNT ENEMY, 3+ CHARM OPPONENT, "
         "4 POISON CLOUD"},
        {FAMILY_GHOST, "ghost",
         "1 SCARE, 1+ WAIL, 2 SIPHON, 3 POSSESS, 4 PHASE", "1 SCARE"},
        {FAMILY_DRUID, "druid",
         "1 GROW TREE, 2 SUMMON FAERIE, 3 REVEAL, 4 PROTECTION",
         "1 GROW TREE, 2 SUMMON FAERIE, 3 REVEAL, 4 PROTECTION"},
        {FAMILY_ORC, "orc", "1 HOWL, 2 EAT CORPSE", "1 HOWL, 2 EAT CORPSE"},
        {FAMILY_BIG_ORC, "orc captain",
         "1 HOWL, 2 EAT CORPSE, 3 HOOK BLADE, 3+ KNIFE FAN, 4 WAR BANNER, "
         "4+ WARBAND",
         ""},
        {FAMILY_BARBARIAN, "barbarian", "1 HURL BOULDER, 2 EXPLODING BOULDER",
         "1 HURL BOULDER, 2 EXPLODING BOULDER"},
        {FAMILY_ARCHMAGE, "archmage",
         "1 TELEPORT, 1+ TELEPORT MARKER, 2 HEARTBURST, 2+ CHAIN LIGHTNING, "
         "3 SUMMON IMAGE, 3+ SUMMON ELEMENTAL, 4 MIND CONTROL",
         "1 TELEPORT, 1+ TELEPORT MARKER, 2 HEARTBURST, 2+ CHAIN LIGHTNING, "
         "3 SUMMON IMAGE, 3+ SUMMON ELEMENTAL, 4 MIND CONTROL"},
        {FAMILY_GOLEM, "golem", "", ""},
        {FAMILY_GIANT_SKELETON, "giant skeleton", "", ""},
        {FAMILY_TOWER1, "tower", "", ""},
    };
    static_assert(sizeof(rows) / sizeof(rows[0]) == NUM_FAMILIES,
                  "every core family has a row");
    for (const FamilyRow& row : rows) {
        EXPECT_EQ(row.on, catalog_text(row.family, 1)) << row.label;
        EXPECT_EQ(row.off, catalog_text(row.family, 0)) << row.label;
    }
}

// Decision 10: the cleric's TURN UNDEAD is the alternate of both RAISE
// UNDEAD and RAISE GHOST; it is listed once, under RAISE UNDEAD. Proof it
// can fail: dropping the name check in catalog() listed (3,1) as a seventh
// entry.
TEST(Belt, catalog_drops_the_clerics_second_turn_undead)
{
    const std::vector<Entry> c = belt::catalog(fd_of(FAMILY_CLERIC), 1);
    ASSERT_EQ(6u, c.size());
    const auto has = [&c](Entry e) {
        for (const Entry& x : c)
            if (x == e)
                return true;
        return false;
    };
    EXPECT_TRUE(has(Entry{2, true}));
    EXPECT_FALSE(has(Entry{3, true}));
    EXPECT_TRUE(has(Entry{3, false}));
}

// The design's A.5 table, cell by cell, both settings. Proof it can fail:
// sending an alternate straight to the lowest free SHIFTER slot (no "same
// key" preference) moved the thief's CHARM OPPONENT from S3 to S2.
TEST(Belt, default_layout_matches_the_table)
{
    const FamilyRow rows[] = {
        {FAMILY_SOLDIER, "soldier",
         "CHARGE | BOOMERANG | WHIRLWIND | DISARM | - | - ||",
         "CHARGE | BOOMERANG | WHIRLWIND | DISARM | - | - ||"},
        {FAMILY_ELF, "elf",
         "ROCKS | BOUNCING ROCKS | LOTS OF ROCKS | MEGA ROCKS | - | - ||",
         "ROCKS | BOUNCING ROCKS | LOTS OF ROCKS | MEGA ROCKS | - | - ||"},
        {FAMILY_ARCHER, "archer",
         "FIRE ARROWS | BARRAGE | EXPLODING BOLT | - | - | - ||",
         "FIRE ARROWS | BARRAGE | EXPLODING BOLT | - | - | - ||"},
        {FAMILY_MAGE, "mage",
         "TELEPORT | WARP SPACE | FREEZE TIME | TELEPORT MARKER | "
         "ENERGY WAVE | HEARTBURST ||",
         "TELEPORT | WARP SPACE | FREEZE TIME | TELEPORT MARKER | "
         "ENERGY WAVE | HEARTBURST ||"},
        {FAMILY_SKELETON, "skeleton",
         "TUNNEL | DIG IN | BONE WALL | REASSEMBLE | LEGION | BONE STORM ||",
         "TUNNEL | - | - | - | - | - ||"},
        {FAMILY_CLERIC, "cleric",
         "HEAL | RAISE UNDEAD | RAISE GHOST | MYSTIC MACE | TURN UNDEAD | "
         "RESURRECT ||",
         "HEAL | RAISE UNDEAD | RAISE GHOST | MYSTIC MACE | TURN UNDEAD | "
         "RESURRECT ||"},
        {FAMILY_FIREELEMENTAL, "elemental",
         "STARBURST | IMMOLATE | METEOR RAIN | REKINDLE | SUPERNOVA | - ||",
         "STARBURST | - | - | - | - | - ||"},
        {FAMILY_FAERIE, "faerie",
         "BLINK | GLIMMER | HASTEN | SWAP | WISH | HASTE SELF ||",
         "- | - | - | - | - | - ||"},
        {FAMILY_SLIME, "slime", "SPLIT | - | - | - | - | - ||",
         "SPLIT | - | - | - | - | - ||"},
        {FAMILY_SMALL_SLIME, "small slime", "GROW | - | - | - | - | - ||",
         "GROW | - | - | - | - | - ||"},
        {FAMILY_MEDIUM_SLIME, "medium slime", "GROW | - | - | - | - | - ||",
         "GROW | - | - | - | - | - ||"},
        {FAMILY_THIEF, "thief",
         "DROP BOMB | CLOAK | TAUNT ENEMY | MINE | POISON CLOUD | "
         "CHARM OPPONENT ||",
         "DROP BOMB | CLOAK | TAUNT ENEMY | POISON CLOUD | - | "
         "CHARM OPPONENT ||"},
        {FAMILY_GHOST, "ghost",
         "SCARE | SIPHON | POSSESS | WAIL | PHASE | - ||",
         "SCARE | - | - | - | - | - ||"},
        {FAMILY_DRUID, "druid",
         "GROW TREE | SUMMON FAERIE | REVEAL | PROTECTION | - | - ||",
         "GROW TREE | SUMMON FAERIE | REVEAL | PROTECTION | - | - ||"},
        {FAMILY_ORC, "orc", "HOWL | EAT CORPSE | - | - | - | - ||",
         "HOWL | EAT CORPSE | - | - | - | - ||"},
        {FAMILY_BIG_ORC, "orc captain",
         "HOWL | EAT CORPSE | HOOK BLADE | WAR BANNER | WARBAND | "
         "KNIFE FAN ||",
         "- | - | - | - | - | - ||"},
        {FAMILY_BARBARIAN, "barbarian",
         "HURL BOULDER | EXPLODING BOULDER | - | - | - | - ||",
         "HURL BOULDER | EXPLODING BOULDER | - | - | - | - ||"},
        {FAMILY_ARCHMAGE, "archmage",
         "TELEPORT | HEARTBURST | SUMMON IMAGE | TELEPORT MARKER | "
         "CHAIN LIGHTNING | SUMMON ELEMENTAL || MIND CONTROL",
         "TELEPORT | HEARTBURST | SUMMON IMAGE | TELEPORT MARKER | "
         "CHAIN LIGHTNING | SUMMON ELEMENTAL || MIND CONTROL"},
        {FAMILY_GOLEM, "golem", "- | - | - | - | - | - ||",
         "- | - | - | - | - | - ||"},
        {FAMILY_GIANT_SKELETON, "giant skeleton", "- | - | - | - | - | - ||",
         "- | - | - | - | - | - ||"},
        {FAMILY_TOWER1, "tower", "- | - | - | - | - | - ||",
         "- | - | - | - | - | - ||"},
    };
    static_assert(sizeof(rows) / sizeof(rows[0]) == NUM_FAMILIES,
                  "every core family has a row");
    for (const FamilyRow& row : rows) {
        EXPECT_EQ(row.on, default_text(row.family, 1)) << row.label;
        EXPECT_EQ(row.off, default_text(row.family, 0)) << row.label;
        // Six DEFAULT bytes resolve to exactly the default layout.
        const FamilyDescriptor* fd = fd_of(row.family);
        for (const short ns : {short{0}, short{1}}) {
            const Layout d = belt::default_layout(fd, ns);
            const Layout r = belt::resolve(fd, 99, ns, belt::default_bytes());
            for (std::size_t b = 0; b < kN; ++b) {
                EXPECT_EQ(d[b].entry, r[b].entry) << row.label << " slot " << b;
                if (d[b].entry) {
                    EXPECT_STREQ(d[b].name, r[b].name) << row.label;
                }
            }
        }
    }
}

// Decision 9: the default never reads a level. Proof it can fail: letting
// resolve() take a DEFAULT slot only when the level opens it left a level-1
// mage with TELEPORT and TELEPORT MARKER alone.
TEST(Belt, default_layout_ignores_level)
{
    const FamilyDescriptor* mage = fd_of(FAMILY_MAGE);
    const Layout low = belt::resolve(mage, 1, 1, belt::default_bytes());
    const Layout high = belt::resolve(mage, 13, 1, belt::default_bytes());
    for (std::size_t b = 0; b < kN; ++b) {
        EXPECT_EQ(low[b].entry, high[b].entry) << b;
        EXPECT_STREQ(low[b].name, high[b].name) << b;
        EXPECT_EQ(low[b].opens_at, high[b].opens_at) << b;
        EXPECT_EQ(SlotState::Castable, high[b].state) << b;
    }
}

TEST(Belt, default_bytes_are_six_zeros)
{
    const belt::Bytes bytes = belt::default_bytes();
    for (const std::uint8_t b : bytes)
        EXPECT_EQ(0, b);
    EXPECT_EQ(6, belt::kSlots);
    EXPECT_EQ(0x00, belt::kDefaultByte);
    EXPECT_EQ(0xFF, belt::kEmptyByte);
    EXPECT_EQ(0x80, belt::kItemBit);
    EXPECT_EQ(0x07, belt::kCastNothing);
}

// Proof it can fail: encoding the alternate as bit 4 (alt << 4) broke the
// round trip for every alternate.
TEST(Belt, encode_decode_round_trip_every_entry)
{
    for (int slot = 1; slot <= 5; ++slot) {
        for (const bool alt : {false, true}) {
            const Entry e{static_cast<std::uint8_t>(slot), alt};
            const std::uint8_t byte = belt::encode(e);
            EXPECT_EQ(slot | (alt ? 0x08 : 0), byte);
            const std::optional<Entry> back = belt::decode_stored(byte);
            ASSERT_TRUE(back.has_value()) << int(byte);
            EXPECT_EQ(e, *back);
            EXPECT_EQ(byte, belt::normalize_stored(byte));
        }
    }
}

// Proof it can fail: dropping the bits 4-6 check in the decoder let 0x11
// decode as slot 1.
TEST(Belt, decode_rejects_slot_0_6_7_item_bit_and_junk)
{
    int accepted = 0;
    for (int v = 0; v < 256; ++v) {
        const std::uint8_t byte = static_cast<std::uint8_t>(v);
        const int slot = v & 0x07;
        const bool well_formed = (v & 0xF0) == 0 && slot >= 1 && slot <= 5;
        EXPECT_EQ(well_formed, belt::decode_stored(byte).has_value()) << v;
        accepted += well_formed ? 1 : 0;
    }
    EXPECT_EQ(10, accepted);
    for (const int v : {0x00, 0x06, 0x07, 0x08, 0x0E, 0x0F, 0x11, 0x80, 0x81,
                        0xFF})
        EXPECT_FALSE(belt::decode_stored(static_cast<std::uint8_t>(v))) << v;
}

// All 256 values against the at-rest table. Proof it can fail: returning
// kDefaultByte for every byte with the item bit (instead of keeping it)
// turned 0x81 into 0x00.
TEST(Belt, normalize_stored_maps_junk_to_default_and_keeps_empty_item_and_explicit)
{
    int junk = 0;
    for (int v = 0; v < 256; ++v) {
        const std::uint8_t byte = static_cast<std::uint8_t>(v);
        const int slot = v & 0x07;
        const bool explicit_entry = (v & 0xF0) == 0 && slot >= 1 && slot <= 5;
        const bool kept = v == 0x00 || v == 0xFF || (v & 0x80) != 0 ||
            explicit_entry;
        EXPECT_EQ(kept ? byte : std::uint8_t{0}, belt::normalize_stored(byte))
            << v;
        junk += kept ? 0 : 1;
        // The reader and resolve agree: a junk byte resolves as DEFAULT.
        if (!kept) {
            belt::Bytes stored = belt::default_bytes();
            stored[2] = byte;
            const Layout l =
                belt::resolve(fd_of(FAMILY_MAGE), 13, 1, stored);
            ASSERT_TRUE(l[2].entry.has_value()) << v;
            EXPECT_STREQ("FREEZE TIME", l[2].name) << v;
        }
    }
    // 128 values below the item bit, minus DEFAULT, minus 10 explicit.
    EXPECT_EQ(117, junk);
}

// Every nonzero wire byte that is not a well-formed entry casts nothing.
// Proof it can fail: masking the byte with & 0x0F before decoding let 0x11
// cast slot 1.
TEST(Belt, validate_cast_refuses_every_malformed_byte_as_nothing_there)
{
    const FamilyDescriptor* archmage = fd_of(FAMILY_ARCHMAGE);
    for (const int v : {0x06, 0x07, 0x08, 0x0E, 0x0F, 0x10, 0x11, 0x19, 0x40,
                        0x80, 0x81, 0xF7, 0xFF}) {
        Entry out{9, true};
        int opens = -1;
        EXPECT_EQ(Refusal::NothingThere,
                  belt::validate_cast(archmage, 99, 1,
                                      static_cast<std::uint8_t>(v), &out,
                                      &opens))
            << v;
        EXPECT_EQ((Entry{9, true}), out) << "untouched on refusal, " << v;
        EXPECT_EQ(-1, opens) << v;
    }
    // Exhaustively: of the 255 nonzero bytes only the ten well-formed
    // entries pass the shape check (the archmage at 99 names slots 1-4 and
    // their first three alternates, so 7 cast and 3 are unnamed).
    int cast = 0;
    for (int v = 1; v < 256; ++v)
        if (belt::validate_cast(archmage, 99, 1, static_cast<std::uint8_t>(v),
                                nullptr, nullptr) == Refusal::None)
            ++cast;
    EXPECT_EQ(7, cast);
}

// Proof it can fail: swapping the DEFAULT and EMPTY branches in resolve()
// emptied every untouched slot.
TEST(Belt, resolve_zero_is_default_ff_is_empty_explicit_is_explicit)
{
    const FamilyDescriptor* mage = fd_of(FAMILY_MAGE);
    belt::Bytes stored = belt::default_bytes();
    stored[1] = belt::kEmptyByte;
    stored[2] = belt::encode(Entry{5, false});  // HEARTBURST on key 3
    stored[3] = 0x81;                           // an item byte (#177)
    stored[5] = belt::encode(Entry{3, false});  // FREEZE TIME on S3
    const Layout l = belt::resolve(mage, 13, 1, stored);

    EXPECT_STREQ("TELEPORT", l[0].name);
    EXPECT_EQ((Entry{1, false}), *l[0].entry);
    EXPECT_EQ(SlotState::Castable, l[0].state);

    EXPECT_EQ(SlotState::Empty, l[1].state);
    EXPECT_FALSE(l[1].entry.has_value());
    EXPECT_STREQ(kSpecialNameNone, l[1].name);
    EXPECT_EQ(0, l[1].opens_at);

    EXPECT_STREQ("HEARTBURST", l[2].name);
    EXPECT_EQ((Entry{5, false}), *l[2].entry);
    EXPECT_EQ(13, l[2].opens_at);

    EXPECT_EQ(SlotState::Empty, l[3].state) << "an item byte reads empty";
    EXPECT_STREQ("ENERGY WAVE", l[4].name) << "DEFAULT";
    EXPECT_STREQ("FREEZE TIME", l[5].name);

    // An explicit entry the family does not name reads empty.
    stored[0] = belt::encode(Entry{2, true});  // the mage's WARP SPACE has no alternate
    EXPECT_EQ(SlotState::Empty, belt::resolve(mage, 13, 1, stored)[0].state);
    // So does the cleric's dropped duplicate: (3, alt) is not in the catalog.
    belt::Bytes cleric = belt::default_bytes();
    cleric[0] = belt::encode(Entry{3, true});
    EXPECT_EQ(SlotState::Empty,
              belt::resolve(fd_of(FAMILY_CLERIC), 13, 1, cleric)[0].state);
    // The same entry on two keys is allowed: both read it.
    stored[0] = belt::encode(Entry{5, false});
    const Layout twice = belt::resolve(mage, 13, 1, stored);
    EXPECT_STREQ("HEARTBURST", twice[0].name);
    EXPECT_STREQ("HEARTBURST", twice[2].name);
}

// The thief's MINE is a New Specials alternate: with the setting off an
// explicit MINE byte reads empty, the byte is left as it was, and MINE is
// back when the setting is. Proof it can fail: resolving explicit bytes
// without the catalog check showed "NONE" as a Castable slot.
TEST(Belt, resolve_hides_an_entry_the_setting_hides_without_rewriting_the_byte)
{
    const FamilyDescriptor* thief = fd_of(FAMILY_THIEF);
    belt::Bytes stored = belt::default_bytes();
    stored[0] = belt::encode(Entry{1, true});
    const belt::Bytes before = stored;

    const Layout off = belt::resolve(thief, 13, 0, stored);
    EXPECT_EQ(SlotState::Empty, off[0].state);
    EXPECT_FALSE(off[0].entry.has_value());
    EXPECT_EQ(before, stored);
    EXPECT_EQ(belt::kCastNothing, belt::cast_byte(off[0]));

    const Layout on = belt::resolve(thief, 13, 1, stored);
    EXPECT_EQ(SlotState::Castable, on[0].state);
    EXPECT_STREQ("MINE", on[0].name);
}

// A level-1 mage: TELEPORT and TELEPORT MARKER cast, everything else is on
// its key, locked, with the level its slot opens at. Proof it can fail:
// computing opens_at as 3 * slot (dropping the - 2) read 6/9/12/15.
TEST(Belt, resolve_marks_locked_with_opens_at)
{
    const FamilyDescriptor* mage = fd_of(FAMILY_MAGE);
    const Layout l = belt::resolve(mage, 1, 1, belt::default_bytes());
    EXPECT_EQ(SlotState::Castable, l[0].state);  // TELEPORT
    EXPECT_EQ(1, l[0].opens_at);
    EXPECT_EQ(SlotState::Castable, l[3].state);  // TELEPORT MARKER
    EXPECT_EQ(1, l[3].opens_at);
    struct Want { std::size_t b; const char* name; int opens_at; };
    const Want locked[] = {{1, "WARP SPACE", 4},
                           {2, "FREEZE TIME", 7},
                           {4, "ENERGY WAVE", 10},
                           {5, "HEARTBURST", 13}};
    for (const Want& w : locked) {
        EXPECT_EQ(SlotState::Locked, l[w.b].state) << w.name;
        EXPECT_STREQ(w.name, l[w.b].name);
        EXPECT_EQ(w.opens_at, l[w.b].opens_at) << w.name;
        // A locked slot still sends its entry; the server says why.
        EXPECT_EQ(belt::encode(*l[w.b].entry), belt::cast_byte(l[w.b]));
    }
    // The boundary: level 4 opens WARP SPACE, level 3 does not.
    EXPECT_EQ(SlotState::Locked,
              belt::resolve(mage, 3, 1, belt::default_bytes())[1].state);
    EXPECT_EQ(SlotState::Castable,
              belt::resolve(mage, 4, 1, belt::default_bytes())[1].state);
}

// Proof it can fail: building the bench from the catalog without the
// on-belt check listed all seven archmage spells with the default belt.
TEST(Belt, bench_is_catalog_minus_belt)
{
    const FamilyDescriptor* archmage = fd_of(FAMILY_ARCHMAGE);
    const Layout d = belt::resolve(archmage, 1, 1, belt::default_bytes());
    const std::vector<Entry> b = belt::bench(archmage, 1, d);
    ASSERT_EQ(1u, b.size());
    EXPECT_EQ((Entry{4, false}), b[0]);  // MIND CONTROL

    belt::Bytes cleared;
    cleared.fill(belt::kEmptyByte);
    const Layout empty = belt::resolve(archmage, 1, 1, cleared);
    EXPECT_EQ(belt::catalog(archmage, 1), belt::bench(archmage, 1, empty));
    EXPECT_EQ(7u, belt::bench(archmage, 1, empty).size());

    // An entry on two keys leaves the bench once, not twice.
    belt::Bytes doubled = belt::default_bytes();
    doubled[0] = belt::encode(Entry{4, false});
    doubled[1] = belt::encode(Entry{4, false});
    const std::vector<Entry> after =
        belt::bench(archmage, 1, belt::resolve(archmage, 1, 1, doubled));
    ASSERT_EQ(2u, after.size());
    EXPECT_EQ((Entry{1, false}), after[0]);  // TELEPORT
    EXPECT_EQ((Entry{2, false}), after[1]);  // HEARTBURST
}

// The server's gate. Proof it can fail: dropping the name check let the
// archer's unnamed slot 5 cast at level 99 (and read "Locked" at level 1).
TEST(Belt, validate_cast_refuses_locked_unnamed_hidden_and_sentinel)
{
    const FamilyDescriptor* mage = fd_of(FAMILY_MAGE);
    Entry out;
    int opens = 0;

    // Castable.
    EXPECT_EQ(Refusal::None, belt::validate_cast(mage, 1, 1, 0x09, &out, &opens));
    EXPECT_EQ((Entry{1, true}), out);
    EXPECT_EQ(0, opens) << "opens_at is written only on Locked";

    // Locked: WARP SPACE at level 1.
    out = Entry{};
    EXPECT_EQ(Refusal::Locked, belt::validate_cast(mage, 1, 1, 0x02, &out, &opens));
    EXPECT_EQ(4, opens);
    EXPECT_EQ(Entry{}, out) << "out is written only on None";
    EXPECT_EQ(Refusal::Locked,
              belt::validate_cast(mage, 12, 1, 0x05, nullptr, nullptr));
    EXPECT_EQ(Refusal::None,
              belt::validate_cast(mage, 13, 1, 0x05, nullptr, nullptr));

    // Unnamed: the mage's WARP SPACE has no alternate; the archer has no
    // slot 5, even at a level that would open it.
    EXPECT_EQ(Refusal::NothingThere,
              belt::validate_cast(mage, 99, 1, 0x0A, &out, &opens));
    EXPECT_EQ(Refusal::NothingThere,
              belt::validate_cast(fd_of(FAMILY_ARCHER), 1, 1, 0x05, &out,
                                  &opens));
    EXPECT_EQ(Refusal::NothingThere,
              belt::validate_cast(fd_of(FAMILY_ARCHER), 99, 1, 0x05, &out,
                                  &opens));

    // Hidden by the setting: the thief's MINE, the skeleton's DIG IN.
    const FamilyDescriptor* thief = fd_of(FAMILY_THIEF);
    EXPECT_EQ(Refusal::None, belt::validate_cast(thief, 1, 1, 0x09, &out, &opens));
    EXPECT_EQ(Refusal::NothingThere,
              belt::validate_cast(thief, 1, 0, 0x09, &out, &opens));
    EXPECT_EQ(Refusal::NothingThere,
              belt::validate_cast(fd_of(FAMILY_SKELETON), 99, 0, 0x02, &out,
                                  &opens));

    // The sentinel, and 0 (never passed by the server: 0 is classic).
    EXPECT_EQ(Refusal::NothingThere,
              belt::validate_cast(mage, 99, 1, belt::kCastNothing, &out, &opens));
    EXPECT_EQ(Refusal::NothingThere,
              belt::validate_cast(mage, 99, 1, 0x00, &out, &opens));

    // The cleric's second TURN UNDEAD still casts from a byte that names it,
    // at RAISE GHOST's level.
    const FamilyDescriptor* cleric = fd_of(FAMILY_CLERIC);
    EXPECT_EQ(Refusal::None,
              belt::validate_cast(cleric, 7, 1, 0x0B, &out, &opens));
    EXPECT_EQ((Entry{3, true}), out);
    EXPECT_EQ(Refusal::Locked,
              belt::validate_cast(cleric, 6, 1, 0x0B, &out, &opens));
    EXPECT_EQ(7, opens);
}

// Proof it can fail: cast_byte returning 0 for an Empty slot would send a
// classic cast of the walker's current slot.
TEST(Belt, cast_byte_and_slot_index)
{
    belt::Resolved empty;
    EXPECT_EQ(SlotState::Empty, empty.state);
    EXPECT_EQ(belt::kCastNothing, belt::cast_byte(empty));
    belt::Resolved r;
    r.entry = Entry{3, true};
    r.state = SlotState::Castable;
    EXPECT_EQ(0x0B, belt::cast_byte(r));
    r.state = SlotState::Locked;
    EXPECT_EQ(0x0B, belt::cast_byte(r));

    EXPECT_EQ(0, belt::slot_index(1, false));
    EXPECT_EQ(1, belt::slot_index(2, false));
    EXPECT_EQ(2, belt::slot_index(3, false));
    EXPECT_EQ(3, belt::slot_index(1, true));
    EXPECT_EQ(4, belt::slot_index(2, true));
    EXPECT_EQ(5, belt::slot_index(3, true));
}

// A body with no guy (a summoned ally, a raised skeleton, a possessed host)
// resolves the class default. The walker overload (W1-A) is a thin call of
// this one. Proof it can fail: returning a value-initialised array of 0xFF
// for null emptied every guyless belt.
TEST(Belt, bytes_for_walker_without_guy_is_default)
{
    EXPECT_EQ(belt::default_bytes(), belt::bytes_for(nullptr));
    belt::Bytes mine = belt::default_bytes();
    mine[4] = belt::kEmptyByte;
    EXPECT_EQ(mine, belt::bytes_for(&mine));
}

// A class pack may declare 5 specials + 5 alternates = 10 entries: the belt
// holds six, the other four sit on the bench. An alternate whose primary
// went to the SHIFTER row takes the next free SHIFTER slot; with none left
// it is benched. A null descriptor reads as a family with nothing.
TEST(Belt, a_ten_entry_pack_benches_four_and_null_has_nothing)
{
    init_family_registry();
    FamilyDescriptor fd = *get_family_descriptor(FAMILY_SOLDIER);
    const char* primaries[] = {nullptr, "P1", "P2", "P3", "P4", "P5"};
    const char* alternates[] = {nullptr, "A1", "A2", "A3", "A4", "A5"};
    for (int s = 1; s <= 5; ++s) {
        fd.special_names[s] = primaries[s];
        fd.alternate_names[s] = alternates[s];
        fd.special_new_kit[s] = false;
        fd.alternate_new_kit[s] = false;
        fd.special_level[s] = 1;
    }
    EXPECT_EQ(10u, belt::catalog(&fd, 0).size());
    EXPECT_EQ("P1 | P2 | P3 | A1 | A2 | A3 || P4 A4 P5 A5",
              layout_text(&fd, 0, belt::default_layout(&fd, 0)));

    // Primaries 1 and 2 unnamed: A1/A2 (no primary placed) take the lowest
    // free SHIFTER slots; P3's own SHIFTER slot is then taken by A1, so A3
    // takes the next free one, and A4/A5 find none left.
    fd.special_names[1] = kSpecialNameNone;
    fd.special_names[2] = nullptr;
    EXPECT_EQ("P3 | P4 | P5 | A1 | A2 | A3 || A4 A5",
              layout_text(&fd, 0, belt::default_layout(&fd, 0)));

    EXPECT_TRUE(belt::catalog(nullptr, 1).empty());
    const Layout none = belt::resolve(nullptr, 99, 1, belt::default_bytes());
    for (const belt::Resolved& r : none)
        EXPECT_EQ(SlotState::Empty, r.state);
    EXPECT_EQ(Refusal::NothingThere,
              belt::validate_cast(nullptr, 99, 1, 0x01, nullptr, nullptr));
}
