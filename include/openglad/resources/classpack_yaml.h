/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// classpack.yaml reader (docs/lua-classpacks-design.md §4). Parses a pack's
// family descriptor data into plain structs with OWNED std::string storage:
// a ClasspackData survives on its own, independent of the YAML text buffer.
//
// Every field is optional-by-presence: the registry installer overwrites
// only the fields a pack actually declares, so a sparse mod entry inherits
// the rest of the current descriptor. Free-text fields that can carry an
// explicit YAML null (`~`) — e.g. short_name, promotes_to — use
// NullableString so "absent", "null", and "value" stay distinguishable.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace og::data {

// A string field that distinguishes absent / explicit null (~) / value.
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
struct ClasspackPresentation {
    std::optional<std::string> glyph;        // UTF-8, exactly one codepoint
    std::optional<std::string> glyph_ascii;  // exactly one byte
    std::optional<std::string> glyph_color;
    std::optional<bool> glyph_bold;
    std::optional<bool> glyph_transparent;
    std::optional<std::int32_t> radar_color;  // sentinels already folded in
    std::optional<std::int32_t> radar_jitter;
};

// One row of an `anims:` frame set. `is_null` is the YAML `~` row, which
// becomes a nullptr entry in the built table (the legacy anislime table has
// eight of them).
struct ClasspackAnimRow {
    bool is_null = false;
    std::vector<std::int32_t> frames;  // sprite frame indices, 0..127
};

// One named set under `anims:`. `rows` is the optional total row count; when
// it exceeds the declared rows the declared rows repeat cyclically, so
// `rows: 16` over a single row reproduces the legacy 16x-same-row tables.
struct ClasspackAnimSet {
    std::string name;
    std::optional<std::int32_t> rows;
    std::vector<ClasspackAnimRow> frames;
};

// One entry under families.living. YAML keys per design doc §4; field
// names match the keys, values mirror FamilyDescriptor's data fields.
struct ClasspackLivingEntry {
    std::string id;                        // required ("core:soldier")
    std::string wire_id;                   // "0".."255", "auto", or "" = absent (auto)
    std::optional<std::string> name;
    NullableString short_name;
    std::optional<std::vector<std::int32_t>> base_stats;       // 6
    std::optional<std::int32_t> hiring_cost;
    std::optional<std::vector<float>> derived_bonuses;         // 8
    std::optional<std::vector<std::int32_t>> stat_costs;       // 6
    std::optional<std::vector<std::int32_t>> special_costs;    // 6
    std::optional<std::int32_t> weapon_cost;
    std::optional<std::string> default_weapon;   // weapon family string id
    std::optional<std::vector<std::string>> init_bit_flags;    // BIT_* names
    std::optional<std::int32_t> init_ani_type;
    std::optional<float> init_max_magicpoints;
    std::optional<std::vector<std::string>> special_names;     // 6
    std::optional<std::vector<std::string>> alternate_names;   // 6
    std::optional<bool> leaves_bloodspot;
    std::optional<float> magic_damage_modifier;
    std::optional<bool> is_stationary;
    std::optional<bool> has_returning_weapon;
    std::optional<bool> is_undead;
    NullableString promotes_to;            // living family string id or ~
    std::optional<std::int32_t> promotion_level_req;
    NullableString death_message;
    NullableString sprite;                 // pix filename or pack-relative path
    std::optional<std::string> animation;  // built-in name or an anims: set
    std::optional<std::int32_t> ai_line_of_sight;
    NullableString description;
    std::optional<std::vector<std::string>> names;  // random-name pool
    std::optional<bool> playable;
    std::optional<std::int32_t> playable_order;
    ClasspackPresentation presentation;
};

// One entry under families.weapon (WeaponFamilyDescriptor data fields).
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
    std::optional<std::string> animation;  // anims: set name
    ClasspackPresentation presentation;
};

// One entry under families.effect (EffectFamilyDescriptor data fields).
struct ClasspackEffectEntry {
    std::string id;
    std::string wire_id;
    std::optional<std::string> name;
    std::optional<bool> loops_animation;
    std::optional<bool> creates_hit_effect;
    std::optional<std::vector<std::string>> init_bit_flags;
    NullableString sprite;
    std::optional<std::string> animation;  // anims: set name
    ClasspackPresentation presentation;
};

// One entry under families.treasure (TreasureFamilyDescriptor data fields).
struct ClasspackTreasureEntry {
    std::string id;
    std::string wire_id;
    std::optional<std::string> name;
    std::optional<bool> init_ignore;
    std::optional<std::int32_t> init_frame;
    NullableString sprite;
    std::optional<std::string> animation;  // anims: set name
    ClasspackPresentation presentation;
};

// One entry under families.generator (GeneratorFamilyDescriptor data
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
    std::optional<std::string> animation;  // anims: set name
    std::optional<std::string> editor_label;  // level-editor palette caption
    ClasspackPresentation presentation;
};

struct ClasspackData {
    std::string pack;      // `pack:` header (informative; mount dir is the id)
    std::string version;
    std::string title;
    std::string authors;
    std::vector<ClasspackLivingEntry> living;
    std::vector<ClasspackWeaponEntry> weapons;
    std::vector<ClasspackEffectEntry> effects;
    std::vector<ClasspackTreasureEntry> treasures;
    std::vector<ClasspackGeneratorEntry> generators;
    std::vector<ClasspackAnimSet> anims;  // `anims:` section, YAML order
};

// Parses classpack.yaml text into out. Strict: malformed YAML, a bad
// number/bool, or a family entry without an id fails the whole pack
// (returns false after LogWarn; out is partially filled and must be
// discarded). Unknown keys and unknown families sections are skipped for
// forward compatibility. Never throws.
bool parse_classpack_yaml(std::string_view text, ClasspackData& out,
                          const char* source_name);

} // namespace og::data
