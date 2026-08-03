/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The class-pack INTERCHANGE structs: one pack's family descriptor data in
// plain structs with OWNED std::string storage, independent of whatever
// text produced it.
//
// The Lua declaration pass fills these (gameplay/script/family_decl.h,
// `og.family`), and exactly one back end consumes them:
// install_pack_families() in src/resources/packs.cpp. The split is
// deliberate: these structs are the seam a tool can fill directly, with no
// Lua anywhere in sight (install_classpack_data; tools/concept_mapgen
// builds one in C++), and the shape a pack takes on the wire.
//
// They live under gameplay rather than resources because the declaration
// pass runs a Lua VM, and Lua headers may only be included from
// src/gameplay/script/ (check_vendor_leaks.sh); gameplay cannot see
// resources headers, so the shared vocabulary has to sit on the gameplay
// side of the boundary.
//
// Every field is optional-by-presence: the registry installer overwrites
// only the fields a pack actually declares, so a sparse mod entry inherits
// the rest of the current descriptor. Free-text fields that can carry an
// explicit null (`og.NIL`) — e.g. short_name, promotes_to —
// use NullableString so "absent", "null", and "value" stay
// distinguishable.

#include <openglad/core/order.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace og::data {

// A string field that distinguishes absent / explicit null / value.
struct NullableString {
    bool present = false;
    bool is_null = false;    // meaningful only when present
    std::string value;       // meaningful only when present && !is_null
};

// The presentation block every order shares (docs/lua-classpacks-design.md
// §4). Absent sub-keys leave the descriptor's current values alone.
//   glyph:            one UTF-8 codepoint, the terminal client's shape
//   glyph_ascii:      single-byte fallback for non-Unicode terminals
//   glyph_color:      default|black|red|green|yellow|blue|magenta|cyan|
//                     white|team ("team" = paint with the entity's team)
//   glyph_bold:       bright attribute
//   glyph_transparent: draw nothing (the curses Glyph::skip flag)
//   radar_color:      palette index, or "team" / "none"
//   radar_jitter:     rng(N) span added to radar_color (0 = no rng call)
//   radar_landmark:   blip without treasure sight (treasure/fx orders only —
//                     the EXIT/TELEPORTER class of map furniture)
struct ClasspackPresentation {
    std::optional<std::string> glyph;        // UTF-8, exactly one codepoint
    std::optional<std::string> glyph_ascii;  // exactly one byte
    std::optional<std::string> glyph_color;
    std::optional<bool> glyph_bold;
    std::optional<bool> glyph_transparent;
    std::optional<std::int32_t> radar_color;  // sentinels already folded in
    std::optional<std::int32_t> radar_jitter;
    std::optional<bool> radar_landmark;       // treasure/fx declarations only
};

// One scalar value of a family entry's `tuning` map. The kind preserves
// the author's spelling — `5` is Integer, `5.0` Number, `true` Boolean, and
// a quoted `"5"` is String even though it looks numeric. Scripts read the
// map through `og.tuning(self)` as a frozen table, so the Integer/Number split
// decides nothing here beyond which Lua number subtype the value carries.
struct ClasspackTuningValue {
    enum class Kind : std::uint8_t { Integer, Number, Boolean, String };
    Kind kind = Kind::Integer;
    std::int64_t integer = 0;  // Kind::Integer
    double number = 0.0;       // Kind::Number
    bool boolean = false;      // Kind::Boolean
    std::string string;        // Kind::String
};

// One key/value pair of a `tuning` map. The harvest sorts them by key.
struct ClasspackTuningPair {
    std::string key;
    ClasspackTuningValue value;
};

// One row of an `anims` frame set. `is_null` is a null row (`false` in the
// frames list), which becomes a nullptr entry in the built table (the legacy
// anislime table has eight of them).
struct ClasspackAnimRow {
    bool is_null = false;
    std::vector<std::int32_t> frames;  // sprite frame indices, 0..127
};

// One named set declared by `og.anims`. `rows` is the optional total row
// count; when it exceeds the declared rows the declared rows repeat
// cyclically, so `rows = 16` over a single row reproduces the legacy
// 16x-same-row tables.
struct ClasspackAnimSet {
    std::string name;
    std::optional<std::int32_t> rows;
    std::vector<ClasspackAnimRow> frames;
};

// --- the living blocks ---------------------------------------------------
//
// A living entry names its axes. The six positional arrays the DOS data
// files had (base_stats, derived_bonuses, stat_costs, special_costs,
// special_names, alternate_names) survive only inside the descriptor, where
// the installer folds these blocks into them.

// `stats:` — the attribute scores a recruit starts with, and the base the
// picker prices training deltas against. Every member is required when the
// block appears: defaulting one silently would ship, say, a 0-armor class.
struct ClasspackStatsBlock {
    std::int32_t strength = 0;
    std::int32_t dexterity = 0;
    std::int32_t constitution = 0;
    std::int32_t intelligence = 0;
    std::int32_t armor = 0;
    std::int32_t level = 0;   // starting level (1 for every core family)
};

// `combat:` — what one of these is in the field. All members required,
// same argument as stats:. fire_delay is busy ticks added after each
// attack (lower = faster); fire_mp_cost is MAGIC POINTS per ranged shot.
struct ClasspackCombatBlock {
    float hp = 0.0f;
    float melee_damage = 0.0f;
    float stepsize = 0.0f;
    float fire_delay = 0.0f;
    std::int32_t fire_mp_cost = 0;
};

// `costs.train` — gold per training point on each axis. Axes are optional
// here: an axis a pack does not price is 0. `level` is vestigial — levels
// are priced by the exp curve — and exists only so a pack can install the
// byte the descriptor has always carried.
struct ClasspackTrainCosts {
    std::int32_t strength = 0;
    std::int32_t dexterity = 0;
    std::int32_t constitution = 0;
    std::int32_t intelligence = 0;
    std::int32_t armor = 0;
    std::int32_t level = 0;
};

// `costs:` — gold, and only gold. `hire` is required.
struct ClasspackCostsBlock {
    std::int32_t hire = 0;
    std::optional<ClasspackTrainCosts> train;
};

// The highest special slot a family has. Slot 0 is an engine artifact the
// loader zeroes and no declaration can spell; the reachable slots are 1..5,
// the fifth becoming selectable at level 13. (== FD_NUM_SPECIALS - 1,
// checked where the two meet in the installer.)
inline constexpr int kMaxSpecialSlot = 5;

// One entry of a `specials` list. List order gives slots 1..5; an entry
// may name its own `slot` to leave a hole, and slots must strictly
// increase. `id` is the key a pack script's specials table uses for the
// slot.
struct ClasspackSpecialEntry {
    std::string id;                // required, [a-z0-9_]+, unique per family
    std::string name;              // required, the HUD string
    std::int32_t mp_cost = 0;      // required
    std::optional<std::string> alternate_name;  // alternate = { name = }
    std::int32_t slot = 0;         // 1..5, from list order or `slot`
};

// One entry declared by og.family("living", ...). Keys per design doc §4;
// field names match the keys, values mirror FamilyDescriptor's data fields.
struct ClasspackLivingEntry {
    std::string id;                        // required ("core:soldier")
    std::string wire_id;                   // "0".."255", "auto", or "" = absent (auto)
    std::optional<std::string> name;
    NullableString short_name;
    // the named blocks
    std::optional<ClasspackStatsBlock> stats;
    std::optional<ClasspackCombatBlock> combat;
    std::optional<ClasspackCostsBlock> costs;
    std::optional<std::vector<ClasspackSpecialEntry>> specials;
    std::optional<std::string> default_weapon;   // weapon family string id
    std::optional<std::vector<std::string>> init_bit_flags;    // BIT_* names
    std::optional<std::int32_t> init_ani_type;
    std::optional<float> init_max_magicpoints;
    std::optional<bool> leaves_bloodspot;
    std::optional<float> magic_damage_modifier;
    std::optional<bool> is_stationary;
    std::optional<bool> has_returning_weapon;
    std::optional<bool> is_undead;
    NullableString promotes_to;            // living family string id or ~
    std::optional<std::int32_t> promotion_level_req;
    NullableString death_message;
    NullableString sprite;                 // pix filename or pack-relative path
    std::optional<std::string> animation;  // built-in name or og.anims set
    std::optional<std::int32_t> ai_line_of_sight;
    NullableString description;
    std::optional<std::vector<std::string>> names;  // random-name pool
    std::optional<bool> playable;
    std::optional<std::int32_t> playable_order;
    ClasspackPresentation presentation;
    std::vector<ClasspackTuningPair> tuning;  // `tuning` map, by key
};

// One entry declared by og.family("weapon", ...) (WeaponFamilyDescriptor
// data fields).
struct ClasspackWeaponEntry {
    std::string id;
    std::string wire_id;
    std::optional<std::string> name;
    std::optional<std::int32_t> fire_sound;
    std::optional<bool> skip_sit_notify;
    std::optional<bool> is_auto_attackable;
    std::optional<std::vector<std::string>> init_bit_flags;
    std::optional<std::int32_t> init_lifetime;
    std::optional<std::int32_t> init_ani_type;
    std::optional<float> vz;               // WeaponFamilyDescriptor.init_vz
    std::optional<float> gravity;
    std::optional<std::int32_t> sizez;     // WeaponFamilyDescriptor.init_sizez
    std::optional<bool> can_drop_floors;
    NullableString sprite;
    std::optional<std::string> animation;  // og.anims set name
    ClasspackPresentation presentation;
    std::vector<ClasspackTuningPair> tuning;  // `tuning` map, by key
};

// One entry declared by og.family("effect", ...) (EffectFamilyDescriptor
// data fields).
struct ClasspackEffectEntry {
    std::string id;
    std::string wire_id;
    std::optional<std::string> name;
    std::optional<bool> loops_animation;
    std::optional<bool> creates_hit_effect;
    std::optional<std::vector<std::string>> init_bit_flags;
    NullableString sprite;
    std::optional<std::string> animation;  // og.anims set name
    ClasspackPresentation presentation;
    std::vector<ClasspackTuningPair> tuning;  // `tuning` map, by key
};

// One entry declared by og.family("treasure", ...)
// (TreasureFamilyDescriptor data fields).
struct ClasspackTreasureEntry {
    std::string id;
    std::string wire_id;
    std::optional<std::string> name;
    std::optional<bool> init_ignore;
    std::optional<std::int32_t> init_frame;
    NullableString sprite;
    std::optional<std::string> animation;  // og.anims set name
    ClasspackPresentation presentation;
    std::vector<ClasspackTuningPair> tuning;  // `tuning` map, by key
};

// One entry declared by og.family("generator", ...) (GeneratorFamilyDescriptor
// data
// fields). default_weapon names the LIVING family the generator produces.
struct ClasspackGeneratorEntry {
    std::string id;
    std::string wire_id;
    std::optional<std::string> name;
    std::optional<std::string> default_weapon;  // living family string id
    std::optional<bool> has_lifetime;
    std::optional<std::int32_t> spawn_ani_type;
    std::optional<bool> clear_owner;
    NullableString sprite;
    std::optional<std::string> animation;  // og.anims set name
    std::optional<std::string> editor_label;  // level-editor palette caption
    ClasspackPresentation presentation;
    std::vector<ClasspackTuningPair> tuning;  // `tuning` map, by key
};

// One family the Lua declaration pass produced, by order and declared id.
//
// Provenance only a declaration can claim. Two rules read it: a castable
// special that nothing handles is a WARNING for a slot filled some other
// way and a pack ERROR for one an og.family declared (format spec V3), and
// diagnostics name the pack that declared the slot. It rides in the parsed
// data rather than a side channel because the parse is memoized on exact
// bytes — a side channel would be empty on every memo hit, which is most
// installs.
struct ClasspackLuaDeclaration {
    ::Order order = ::Order::Living;
    std::string id;   // the entry's declared `id`, e.g. "core:soldier"
};

struct ClasspackData {
    std::string pack;      // og.pack id (informative; mount dir is the id)
    std::string version;
    std::string title;
    std::string authors;
    std::vector<ClasspackLivingEntry> living;
    std::vector<ClasspackWeaponEntry> weapons;
    std::vector<ClasspackEffectEntry> effects;
    std::vector<ClasspackTreasureEntry> treasures;
    std::vector<ClasspackGeneratorEntry> generators;
    std::vector<ClasspackAnimSet> anims;  // og.anims sets, declaration order
    std::vector<ClasspackLuaDeclaration> lua_declarations;
};

}  // namespace og::data
