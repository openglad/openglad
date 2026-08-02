/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/classpack_yaml.h>

#include <openglad/core/family_presentation.h>
#include <openglad/core/util.h>

#include <yaml.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace og::data {

namespace {

// Pull-cursor over libyaml events. Any parser error or structural error
// latches failed; callers bail out and parse_classpack_yaml reports false
// (LogWarn, pack skipped) — never a crash.
struct Cursor {
    yaml_parser_t parser{};
    const char* source;
    bool initialized = false;
    bool failed = false;

    Cursor(std::string_view text, const char* source_name)
        : source(source_name)
    {
        std::memset(&parser, 0, sizeof(parser));
        if (!yaml_parser_initialize(&parser)) {
            failed = true;
            return;
        }
        initialized = true;
        yaml_parser_set_input_string(
            &parser,
            reinterpret_cast<const unsigned char*>(text.data()),
            text.size());
    }

    ~Cursor()
    {
        if (initialized)
            yaml_parser_delete(&parser);
    }

    Cursor(const Cursor&) = delete;
    Cursor& operator=(const Cursor&) = delete;

    bool next(yaml_event_t& ev)
    {
        if (failed)
            return false;
        std::memset(&ev, 0, sizeof(ev));
        if (!yaml_parser_parse(&parser, &ev)) {
            LogWarn("classpack.yaml parse error in {}: {} {}\n", source,
                    parser.context ? parser.context : "",
                    parser.problem ? parser.problem : "");
            failed = true;
            return false;
        }
        return true;
    }

    void error(const std::string& msg)
    {
        LogWarn("classpack.yaml error in {}: {}\n", source, msg);
        failed = true;
    }

    // Warn tier: the pack still loads, but the author hears that one of
    // their keys did nothing.
    void warn(const std::string& msg) const
    {
        LogWarn("classpack.yaml in {}: {}\n", source, msg);
    }
};

// What a key handler did with the value it was offered.
enum class KeyResult : std::uint8_t {
    Taken,    // recognized and consumed
    Unknown,  // not one of ours — the caller decides (warn or skip)
    Failed,   // malformed: the cursor already carries the error
};

// One scalar value; `null` marks an explicit YAML null (plain ~ / null /
// empty), which nullable fields keep and every other field treats as
// "not declared". `plain` records the style: tuning values use it to keep
// a QUOTED "5" a string while a plain 5 becomes a number.
struct Scalar {
    std::string text;
    bool null = false;
    bool plain = false;
};

Scalar scalar_of(const yaml_event_t& ev)
{
    Scalar s;
    const auto* value = reinterpret_cast<const char*>(ev.data.scalar.value);
    const auto length = static_cast<std::size_t>(ev.data.scalar.length);
    if (value != nullptr)
        s.text.assign(value, length);
    s.plain = ev.data.scalar.style == YAML_PLAIN_SCALAR_STYLE;
    if (s.plain &&
        (s.text.empty() || s.text == "~" || s.text == "null" ||
         s.text == "Null" || s.text == "NULL"))
        s.null = true;
    return s;
}

// Consumes a whole node whose FIRST event had the given type (used for
// unknown keys / sections — forward compatibility).
bool skip_node(Cursor& c, yaml_event_type_t first)
{
    int depth = (first == YAML_SEQUENCE_START_EVENT ||
                 first == YAML_MAPPING_START_EVENT)
                    ? 1
                    : 0;
    while (depth > 0) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        switch (ev.type) {
            case YAML_SEQUENCE_START_EVENT:
            case YAML_MAPPING_START_EVENT:
                depth++;
                break;
            case YAML_SEQUENCE_END_EVENT:
            case YAML_MAPPING_END_EVENT:
                depth--;
                break;
            case YAML_STREAM_END_EVENT:
                yaml_event_delete(&ev);
                c.error("unexpected end of stream");
                return false;
            default:
                break;
        }
        yaml_event_delete(&ev);
    }
    return true;
}

// Collects the scalar items of a flow/block sequence (SEQUENCE_START
// already consumed). Nested composites are a structural error.
bool collect_scalars(Cursor& c, std::vector<Scalar>& items)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            c.error("field lists must contain scalars");
            return false;
        }
        items.push_back(scalar_of(ev));
        yaml_event_delete(&ev);
    }
}

// --- typed field setters (explicit null counts as "not declared" except
// --- for NullableString fields, which remember it) --------------------

bool set_int(Cursor& c, const std::string& key, const Scalar& v,
             std::optional<std::int32_t>& out)
{
    if (v.null)
        return true;
    const auto parsed = parse_int_strict(v.text);
    if (!parsed) {
        c.error("bad integer for '" + key + "': " + v.text);
        return false;
    }
    out = *parsed;
    return true;
}

bool parse_float_strict(const std::string& text, float& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0')
        return false;
    out = value;
    return true;
}

// Tuning-value parses. Tuning integers are int64 (they land in Lua
// integers, which are int64) and tuning floats are double (Lua numbers),
// so neither narrows on the way into og.tuning.
bool parse_int64_strict(const std::string& text, std::int64_t& out)
{
    if (text.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE)
        return false;
    out = static_cast<std::int64_t>(value);
    return true;
}

bool parse_double_strict(const std::string& text, double& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0')
        return false;
    out = value;
    return true;
}

bool set_float(Cursor& c, const std::string& key, const Scalar& v,
               std::optional<float>& out)
{
    if (v.null)
        return true;
    float value = 0.0f;
    if (!parse_float_strict(v.text, value)) {
        c.error("bad number for '" + key + "': " + v.text);
        return false;
    }
    out = value;
    return true;
}

bool set_bool(Cursor& c, const std::string& key, const Scalar& v,
              std::optional<bool>& out)
{
    if (v.null)
        return true;
    if (v.text == "true")
        out = true;
    else if (v.text == "false")
        out = false;
    else {
        c.error("bad boolean for '" + key + "': " + v.text);
        return false;
    }
    return true;
}

void set_string(const Scalar& v, std::optional<std::string>& out)
{
    if (!v.null)
        out = v.text;
}

void set_nullable(const Scalar& v, NullableString& out)
{
    out.present = true;
    out.is_null = v.null;
    if (!v.null)
        out.value = v.text;
}

// `radar_color:` accepts the two sentinel spellings on top of a palette
// index, so a pack can say "no blip" / "the entity's team colour" without
// knowing the sentinel numbers.
bool set_radar_color(Cursor& c, const std::string& key, const Scalar& v,
                     std::optional<std::int32_t>& out)
{
    if (v.null)
        return true;
    if (v.text == og::kRadarColorNoneName) {
        out = og::kRadarColorNone;
        return true;
    }
    if (v.text == og::kRadarColorTeamName) {
        out = og::kRadarColorTeam;
        return true;
    }
    return set_int(c, key, v, out);
}

// The presentation sub-keys every order shares. A key that is not one of
// them falls through untouched (unknown keys are skipped for forward
// compatibility); the return value is the usual "false = malformed value,
// abort the pack".
bool set_presentation_field(Cursor& c, const std::string& key,
                            const Scalar& v, ClasspackPresentation& p)
{
    if (key == "glyph")
        set_string(v, p.glyph);
    else if (key == "glyph_ascii")
        set_string(v, p.glyph_ascii);
    else if (key == "glyph_color")
        set_string(v, p.glyph_color);
    else if (key == "glyph_bold")
        return set_bool(c, key, v, p.glyph_bold);
    else if (key == "glyph_transparent")
        return set_bool(c, key, v, p.glyph_transparent);
    else if (key == "radar_color")
        return set_radar_color(c, key, v, p.radar_color);
    else if (key == "radar_jitter")
        return set_int(c, key, v, p.radar_jitter);
    return true;
}

// --- typed list setters ------------------------------------------------

bool set_int_list(Cursor& c, const std::string& key,
                  const std::vector<Scalar>& items,
                  std::optional<std::vector<std::int32_t>>& out)
{
    std::vector<std::int32_t> values;
    values.reserve(items.size());
    for (const Scalar& item : items) {
        const auto parsed = parse_int_strict(item.text);
        if (item.null || !parsed) {
            c.error("bad integer in list '" + key + "': " + item.text);
            return false;
        }
        values.push_back(*parsed);
    }
    out = std::move(values);
    return true;
}

bool set_float_list(Cursor& c, const std::string& key,
                    const std::vector<Scalar>& items,
                    std::optional<std::vector<float>>& out)
{
    std::vector<float> values;
    values.reserve(items.size());
    for (const Scalar& item : items) {
        float value = 0.0f;
        if (item.null || !parse_float_strict(item.text, value)) {
            c.error("bad number in list '" + key + "': " + item.text);
            return false;
        }
        values.push_back(value);
    }
    out = std::move(values);
    return true;
}

bool set_string_list(Cursor& c, const std::string& key,
                     const std::vector<Scalar>& items,
                     std::optional<std::vector<std::string>>& out)
{
    std::vector<std::string> values;
    values.reserve(items.size());
    for (const Scalar& item : items) {
        if (item.null) {
            c.error("null entry in list '" + key + "'");
            return false;
        }
        values.push_back(item.text);
    }
    out = std::move(values);
    return true;
}

// --- tuning: map (read via og.tuning) -----------------------------------

// Classifies one tuning scalar. Quoted scalars stay strings whatever they
// spell; plain scalars try int64, then boolean, then double, then fall
// back to a plain string. A null is an error — tuning keys carry values.
bool classify_tuning_value(Cursor& c, const std::string& key,
                           const Scalar& v, ClasspackTuningValue& out)
{
    if (v.null) {
        c.error("tuning '" + key + "' must not be null");
        return false;
    }
    if (!v.plain) {
        out.kind = ClasspackTuningValue::Kind::String;
        out.string = v.text;
        return true;
    }
    if (parse_int64_strict(v.text, out.integer)) {
        out.kind = ClasspackTuningValue::Kind::Integer;
        return true;
    }
    if (v.text == "true" || v.text == "false") {
        out.kind = ClasspackTuningValue::Kind::Boolean;
        out.boolean = v.text == "true";
        return true;
    }
    if (parse_double_strict(v.text, out.number)) {
        out.kind = ClasspackTuningValue::Kind::Number;
        return true;
    }
    out.kind = ClasspackTuningValue::Kind::String;
    out.string = v.text;
    return true;
}

// The body of one `tuning:` mapping (MAPPING_START already consumed).
// Scalar values only — a nested list or mapping fails the pack, strict
// like every other malformed value.
bool parse_tuning_map(Cursor& c, std::vector<ClasspackTuningPair>& out)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            c.error("tuning keys must be scalars");
            return false;
        }
        ClasspackTuningPair pair;
        pair.key = scalar_of(ev).text;
        yaml_event_delete(&ev);
        if (pair.key.empty()) {
            c.error("tuning: a value without a key");
            return false;
        }

        yaml_event_t vev;
        if (!c.next(vev))
            return false;
        if (vev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&vev);
            c.error("tuning '" + pair.key + "' must be a scalar");
            return false;
        }
        const Scalar value = scalar_of(vev);
        yaml_event_delete(&vev);
        if (!classify_tuning_value(c, pair.key, value, pair.value))
            return false;
        out.push_back(std::move(pair));
    }
}

// --- schema v2 living blocks -------------------------------------------
//
// The named blocks that replace the positional arrays. Two tiers differ
// from the entry level on purpose:
//   * an unknown key INSIDE a block warns instead of vanishing. Forward
//     compatibility is why unknown ENTRY keys are silent, and that argument
//     holds for a future engine adding a section; inside `combat:` the
//     realistic unknown key is `step_size`, and swallowing it installs a
//     default the author never wrote.
//   * a member of stats:/combat: that the block leaves out is fatal. There
//     is no honest default for armor or hitpoints, so the file has to say.

// Walks one v2 block's key/value pairs (MAPPING_START already consumed).
// `where` names the block in diagnostics: "'core:soldier' combat".
template <typename OnScalar, typename OnNode>
bool parse_v2_block(Cursor& c, const std::string& where, OnScalar&& on_scalar,
                    OnNode&& on_node)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            c.error(where + ": expected a field key");
            return false;
        }
        const std::string key = scalar_of(ev).text;
        yaml_event_delete(&ev);

        yaml_event_t vev;
        if (!c.next(vev))
            return false;
        if (vev.type == YAML_SCALAR_EVENT) {
            const Scalar value = scalar_of(vev);
            yaml_event_delete(&vev);
            const KeyResult r = on_scalar(key, value);
            if (r == KeyResult::Failed)
                return false;
            if (r == KeyResult::Unknown)
                c.warn(where + ": unknown key '" + key + "' — ignored");
            continue;
        }
        const yaml_event_type_t type = vev.type;
        yaml_event_delete(&vev);
        const KeyResult r = on_node(key, type);
        if (r == KeyResult::Failed)
            return false;
        if (r == KeyResult::Taken)
            continue;
        c.warn(where + ": unknown key '" + key + "' — ignored");
        if (!skip_node(c, type))
            return false;
    }
}

// A block key with no nested nodes of its own.
KeyResult no_nested_nodes(const std::string&, yaml_event_type_t)
{
    return KeyResult::Unknown;
}

// One numeric member of a v2 block. An explicit `~` leaves `seen` false,
// so a nulled-out required key trips the missing-key error instead of
// installing 0.
KeyResult take_int(Cursor& c, const std::string& where, const std::string& key,
                   const Scalar& v, std::int32_t& out, bool& seen)
{
    if (v.null)
        return KeyResult::Taken;
    const auto parsed = parse_int_strict(v.text);
    if (!parsed) {
        c.error(where + "." + key + ": bad integer '" + v.text + "'");
        return KeyResult::Failed;
    }
    out = *parsed;
    seen = true;
    return KeyResult::Taken;
}

KeyResult take_float(Cursor& c, const std::string& where,
                     const std::string& key, const Scalar& v, float& out,
                     bool& seen)
{
    if (v.null)
        return KeyResult::Taken;
    float value = 0.0f;
    if (!parse_float_strict(v.text, value)) {
        c.error(where + "." + key + ": bad number '" + v.text + "'");
        return KeyResult::Failed;
    }
    out = value;
    seen = true;
    return KeyResult::Taken;
}

bool require_key(Cursor& c, const std::string& where, const char* key,
                 bool seen)
{
    if (seen)
        return true;
    c.error(where + ": missing required key '" + std::string(key) + "'");
    return false;
}

bool parse_stats_block(Cursor& c, const std::string& where,
                       ClasspackStatsBlock& out)
{
    bool str = false, dex = false, con = false, intel = false, arm = false,
         lvl = false;
    const bool ok = parse_v2_block(
        c, where,
        [&](const std::string& key, const Scalar& v) {
            if (key == "strength")
                return take_int(c, where, key, v, out.strength, str);
            if (key == "dexterity")
                return take_int(c, where, key, v, out.dexterity, dex);
            if (key == "constitution")
                return take_int(c, where, key, v, out.constitution, con);
            if (key == "intelligence")
                return take_int(c, where, key, v, out.intelligence, intel);
            if (key == "armor")
                return take_int(c, where, key, v, out.armor, arm);
            if (key == "level")
                return take_int(c, where, key, v, out.level, lvl);
            return KeyResult::Unknown;
        },
        no_nested_nodes);
    if (!ok)
        return false;
    return require_key(c, where, "strength", str) &&
           require_key(c, where, "dexterity", dex) &&
           require_key(c, where, "constitution", con) &&
           require_key(c, where, "intelligence", intel) &&
           require_key(c, where, "armor", arm) &&
           require_key(c, where, "level", lvl);
}

// The four columns of the old derived_bonuses[8] that never had a reader.
// They are recognized-and-fatal rather than unknown-and-warned: a modder
// writing `mp: 40` has a concrete belief about the engine, and the answer
// is a sentence, not a shrug. Returns nullptr for a live key.
const char* dead_combat_axis(const std::string& key)
{
    if (key == "mp")
        return "max MP is derived (10 + INT*3); set init_max_magicpoints to "
               "override it";
    if (key == "ranged_damage")
        return "a ranged attack's damage belongs to the weapon family";
    if (key == "range")
        return "ranged reach is ai_line_of_sight";
    if (key == "defense")
        return "armor is stats.armor";
    return nullptr;
}

bool parse_combat_block(Cursor& c, const std::string& where,
                        ClasspackCombatBlock& out)
{
    bool hp = false, dmg = false, step = false, delay = false, mp = false;
    const bool ok = parse_v2_block(
        c, where,
        [&](const std::string& key, const Scalar& v) {
            if (key == "hp")
                return take_float(c, where, key, v, out.hp, hp);
            if (key == "melee_damage")
                return take_float(c, where, key, v, out.melee_damage, dmg);
            if (key == "stepsize")
                return take_float(c, where, key, v, out.stepsize, step);
            if (key == "fire_delay")
                return take_float(c, where, key, v, out.fire_delay, delay);
            if (key == "fire_mp_cost")
                return take_int(c, where, key, v, out.fire_mp_cost, mp);
            if (const char* why = dead_combat_axis(key)) {
                c.error(where + "." + key + " is not a mechanism: " + why);
                return KeyResult::Failed;
            }
            return KeyResult::Unknown;
        },
        no_nested_nodes);
    if (!ok)
        return false;
    return require_key(c, where, "hp", hp) &&
           require_key(c, where, "melee_damage", dmg) &&
           require_key(c, where, "stepsize", step) &&
           require_key(c, where, "fire_delay", delay) &&
           require_key(c, where, "fire_mp_cost", mp);
}

// costs.train — gold per training point. Every axis is optional here: an
// omitted one is 0, which is exactly what an unpriced axis shipped in v1.
bool parse_train_costs(Cursor& c, const std::string& where,
                       ClasspackTrainCosts& out)
{
    bool seen = false;
    return parse_v2_block(
        c, where,
        [&](const std::string& key, const Scalar& v) {
            if (key == "strength")
                return take_int(c, where, key, v, out.strength, seen);
            if (key == "dexterity")
                return take_int(c, where, key, v, out.dexterity, seen);
            if (key == "constitution")
                return take_int(c, where, key, v, out.constitution, seen);
            if (key == "intelligence")
                return take_int(c, where, key, v, out.intelligence, seen);
            if (key == "armor")
                return take_int(c, where, key, v, out.armor, seen);
            if (key == "level")
                return take_int(c, where, key, v, out.level, seen);
            return KeyResult::Unknown;
        },
        no_nested_nodes);
}

bool parse_costs_block(Cursor& c, const std::string& where,
                       ClasspackCostsBlock& out)
{
    bool hire = false;
    const bool ok = parse_v2_block(
        c, where,
        [&](const std::string& key, const Scalar& v) {
            if (key == "hire")
                return take_int(c, where, key, v, out.hire, hire);
            return KeyResult::Unknown;
        },
        [&](const std::string& key, yaml_event_type_t type) {
            if (key != "train" || type != YAML_MAPPING_START_EVENT)
                return KeyResult::Unknown;
            ClasspackTrainCosts train;
            if (!parse_train_costs(c, where + ".train", train))
                return KeyResult::Failed;
            out.train = train;
            return KeyResult::Taken;
        });
    if (!ok)
        return false;
    return require_key(c, where, "hire", hire);
}

// An id is a bare Lua table key on the script side, so keep it to the
// characters that can be spelled without brackets.
bool is_special_id(const std::string& id)
{
    if (id.empty())
        return false;
    for (const char ch : id) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                        ch == '_';
        if (!ok)
            return false;
    }
    return true;
}

bool parse_special_entry(Cursor& c, const std::string& where,
                         ClasspackSpecialEntry& out)
{
    bool has_id = false, has_name = false, has_cost = false,
         has_slot = false;
    const bool ok = parse_v2_block(
        c, where,
        [&](const std::string& key, const Scalar& v) {
            if (key == "id") {
                if (!v.null) {
                    out.id = v.text;
                    has_id = true;
                }
                return KeyResult::Taken;
            }
            if (key == "name") {
                if (!v.null) {
                    out.name = v.text;
                    has_name = true;
                }
                return KeyResult::Taken;
            }
            if (key == "mp_cost")
                return take_int(c, where, key, v, out.mp_cost, has_cost);
            if (key == "slot")
                return take_int(c, where, key, v, out.slot, has_slot);
            if (key == "alternate") {
                // The shorthand a modder reaches for first. Say the shape
                // rather than filing it under unknown keys.
                c.error(where +
                        ".alternate takes a mapping: alternate: {name: ...}");
                return KeyResult::Failed;
            }
            return KeyResult::Unknown;
        },
        [&](const std::string& key, yaml_event_type_t type) {
            if (key != "alternate" || type != YAML_MAPPING_START_EVENT)
                return KeyResult::Unknown;
            const std::string alt_where = where + ".alternate";
            std::string alt_name;
            bool alt_seen = false;
            const bool alt_ok = parse_v2_block(
                c, alt_where,
                [&](const std::string& alt_key, const Scalar& v) {
                    if (alt_key == "name") {
                        if (!v.null) {
                            alt_name = v.text;
                            alt_seen = true;
                        }
                        return KeyResult::Taken;
                    }
                    if (alt_key == "mp_cost") {
                        // Foreseeable and wrong: the HUD prices the
                        // alternate at the special's own mp_cost.
                        c.warn(alt_where +
                               ": an alternate shares the special's mp_cost "
                               "— ignored");
                        return KeyResult::Taken;
                    }
                    return KeyResult::Unknown;
                },
                no_nested_nodes);
            if (!alt_ok)
                return KeyResult::Failed;
            if (!require_key(c, alt_where, "name", alt_seen))
                return KeyResult::Failed;
            out.alternate_name = alt_name;
            return KeyResult::Taken;
        });
    if (!ok)
        return false;
    if (!require_key(c, where, "id", has_id) ||
        !require_key(c, where, "name", has_name) ||
        !require_key(c, where, "mp_cost", has_cost))
        return false;
    if (!is_special_id(out.id)) {
        c.error(where + ".id '" + out.id +
                "': a special id is lowercase letters, digits and "
                "underscores");
        return false;
    }
    if (out.id == "default") {
        c.error(where +
                ".id: 'default' is reserved — it is the specials table's "
                "catch-all key, so a special could never be handled by "
                "name");
        return false;
    }
    // An undeclared slot stays 0, which the list reads as "the next one".
    // `slot: 0` written out is a different claim — the engine artifact,
    // which is not a special — so it does not get that reading.
    if (has_slot && (out.slot < 1 || out.slot > kMaxSpecialSlot)) {
        c.error(where + ".slot: " + std::to_string(out.slot) +
                " does not exist (a family has slots 1.." +
                std::to_string(kMaxSpecialSlot) + ")");
        return false;
    }
    return true;
}

// `specials:` — up to five entries, list order giving slots 1..5.
// SEQUENCE_START already consumed.
bool parse_specials_list(Cursor& c, const std::string& entry_where,
                         std::vector<ClasspackSpecialEntry>& out)
{
    int previous_slot = 0;
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            c.error(entry_where + " specials: entries must be mappings");
            return false;
        }
        yaml_event_delete(&ev);

        const std::string where =
            entry_where + " specials[" + std::to_string(out.size()) + "]";
        ClasspackSpecialEntry entry;
        if (!parse_special_entry(c, where, entry))
            return false;
        for (const ClasspackSpecialEntry& seen : out) {
            if (seen.id != entry.id)
                continue;
            c.error(where + ".id '" + entry.id +
                    "': already used by an earlier special of this family");
            return false;
        }
        if (entry.slot == 0)
            entry.slot = previous_slot + 1;
        if (entry.slot > kMaxSpecialSlot) {
            c.error(where + ": slot " + std::to_string(entry.slot) +
                    " does not exist (a family has slots 1.." +
                    std::to_string(kMaxSpecialSlot) + ")");
            return false;
        }
        if (entry.slot <= previous_slot) {
            c.error(where + ": slot " + std::to_string(entry.slot) +
                    " must be greater than the previous entry's slot " +
                    std::to_string(previous_slot) +
                    " (specials are listed in slot order)");
            return false;
        }
        previous_slot = entry.slot;
        out.push_back(std::move(entry));
    }
}

// --- generic entry walker ----------------------------------------------

// A family order with no nested structures beyond `tuning:`.
struct NoRawNodes {
    KeyResult operator()(const std::string&, yaml_event_type_t) const
    {
        return KeyResult::Unknown;
    }
};

// Walks the key/value pairs of one family entry (MAPPING_START already
// consumed). on_scalar / on_list return false to abort (cursor error set);
// on_raw_node gets first refusal on a nested list or mapping and consumes
// the node itself when it claims one. `tuning` receives the entry's
// `tuning:` map (every order has one).
template <typename SetScalarFn, typename SetListFn,
          typename RawNodeFn = NoRawNodes>
bool parse_entry_fields(Cursor& c, std::vector<ClasspackTuningPair>& tuning,
                        SetScalarFn&& on_scalar, SetListFn&& on_list,
                        RawNodeFn&& on_raw_node = RawNodeFn{})
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            c.error("expected a field key");
            return false;
        }
        const std::string key = scalar_of(ev).text;
        yaml_event_delete(&ev);

        yaml_event_t vev;
        if (!c.next(vev))
            return false;
        switch (vev.type) {
            case YAML_SCALAR_EVENT: {
                const Scalar value = scalar_of(vev);
                yaml_event_delete(&vev);
                if (!on_scalar(key, value))
                    return false;
                break;
            }
            case YAML_SEQUENCE_START_EVENT: {
                yaml_event_delete(&vev);
                const KeyResult claimed =
                    on_raw_node(key, YAML_SEQUENCE_START_EVENT);
                if (claimed == KeyResult::Failed)
                    return false;
                if (claimed == KeyResult::Taken)
                    break;
                std::vector<Scalar> items;
                if (!collect_scalars(c, items))
                    return false;
                if (!on_list(key, items))
                    return false;
                break;
            }
            case YAML_MAPPING_START_EVENT: {
                yaml_event_delete(&vev);
                if (key == "tuning") {
                    if (!parse_tuning_map(c, tuning))
                        return false;
                    break;
                }
                const KeyResult claimed =
                    on_raw_node(key, YAML_MAPPING_START_EVENT);
                if (claimed == KeyResult::Failed)
                    return false;
                if (claimed == KeyResult::Taken)
                    break;
                // Unknown nested mapping: skip (forward compatibility).
                if (!skip_node(c, YAML_MAPPING_START_EVENT))
                    return false;
                break;
            }
            case YAML_ALIAS_EVENT:
                yaml_event_delete(&vev);
                break;
            default:
                yaml_event_delete(&vev);
                c.error("unexpected value for '" + key + "'");
                return false;
        }
    }
}

// --- per-order entry parsers -------------------------------------------

// The shape each v2 block takes, or nullptr when the key is not one of
// them. A block written in the wrong shape — `stats: [12, 6, 12, ...]`,
// the half-remembered v1 array — is fatal rather than unknown-and-
// skipped: skipping it would install a family with no attributes at all
// and say nothing.
const char* v2_block_shape(const std::string& key)
{
    if (key == "stats" || key == "combat" || key == "costs")
        return "a mapping of named keys";
    if (key == "specials")
        return "a list of specials";
    return nullptr;
}

// True when the entry declares any of the six positional arrays (or the
// two loose scalars) v2 replaced.
bool declares_schema_v1(const ClasspackLivingEntry& e)
{
    return e.base_stats || e.hiring_cost || e.derived_bonuses ||
           e.stat_costs || e.special_costs || e.weapon_cost ||
           e.special_names || e.alternate_names;
}

bool declares_schema_v2(const ClasspackLivingEntry& e)
{
    return e.stats || e.combat || e.costs || e.specials;
}

bool parse_living_entry(Cursor& c, ClasspackLivingEntry& e)
{
    // Blocks can appear before `id:`, so name the entry as best we can.
    const auto entry_where = [&e]() {
        return e.id.empty() ? std::string("a living entry")
                            : "'" + e.id + "'";
    };
    const bool ok = parse_entry_fields(
        c, e.tuning,
        [&](const std::string& key, const Scalar& v) {
            if (key == "id") {
                if (!v.null)
                    e.id = v.text;
            } else if (key == "wire_id") {
                if (!v.null)
                    e.wire_id = v.text;
            } else if (key == "name")
                set_string(v, e.name);
            else if (key == "short_name")
                set_nullable(v, e.short_name);
            else if (key == "hiring_cost")
                return set_int(c, key, v, e.hiring_cost);
            else if (key == "weapon_cost")
                return set_int(c, key, v, e.weapon_cost);
            else if (key == "default_weapon")
                set_string(v, e.default_weapon);
            else if (key == "init_ani_type")
                return set_int(c, key, v, e.init_ani_type);
            else if (key == "init_max_magicpoints")
                return set_float(c, key, v, e.init_max_magicpoints);
            else if (key == "leaves_bloodspot")
                return set_bool(c, key, v, e.leaves_bloodspot);
            else if (key == "magic_damage_modifier")
                return set_float(c, key, v, e.magic_damage_modifier);
            else if (key == "is_stationary")
                return set_bool(c, key, v, e.is_stationary);
            else if (key == "has_returning_weapon")
                return set_bool(c, key, v, e.has_returning_weapon);
            else if (key == "is_undead")
                return set_bool(c, key, v, e.is_undead);
            else if (key == "promotes_to")
                set_nullable(v, e.promotes_to);
            else if (key == "promotion_level_req")
                return set_int(c, key, v, e.promotion_level_req);
            else if (key == "death_message")
                set_nullable(v, e.death_message);
            else if (key == "sprite")
                set_nullable(v, e.sprite);
            else if (key == "animation")
                set_string(v, e.animation);
            else if (key == "ai_line_of_sight")
                return set_int(c, key, v, e.ai_line_of_sight);
            else if (key == "description")
                set_nullable(v, e.description);
            else if (key == "playable")
                return set_bool(c, key, v, e.playable);
            else if (key == "playable_order")
                return set_int(c, key, v, e.playable_order);
            else if (const char* shape = v2_block_shape(key)) {
                if (v.null)
                    return true;  // `~` is "not declared", as everywhere
                c.error(entry_where() + " " + key + " takes " + shape);
                return false;
            }
            else {
                // Presentation block; unknown keys are skipped (forward
                // compatibility).
                return set_presentation_field(c, key, v, e.presentation);
            }
            return true;
        },
        [&](const std::string& key, const std::vector<Scalar>& items) {
            if (key == "base_stats")
                return set_int_list(c, key, items, e.base_stats);
            if (key == "derived_bonuses")
                return set_float_list(c, key, items, e.derived_bonuses);
            if (key == "stat_costs")
                return set_int_list(c, key, items, e.stat_costs);
            if (key == "special_costs")
                return set_int_list(c, key, items, e.special_costs);
            if (key == "init_bit_flags")
                return set_string_list(c, key, items, e.init_bit_flags);
            if (key == "special_names")
                return set_string_list(c, key, items, e.special_names);
            if (key == "alternate_names")
                return set_string_list(c, key, items, e.alternate_names);
            if (key == "names")
                return set_string_list(c, key, items, e.names);
            return true;
        },
        [&](const std::string& key, yaml_event_type_t type) {
            const char* shape = v2_block_shape(key);
            if (shape == nullptr)
                return KeyResult::Unknown;
            const yaml_event_type_t wanted =
                (key == "specials") ? YAML_SEQUENCE_START_EVENT
                                    : YAML_MAPPING_START_EVENT;
            if (type != wanted) {
                c.error(entry_where() + " " + key + " takes " + shape);
                return KeyResult::Failed;
            }
            if (key == "stats") {
                ClasspackStatsBlock block;
                if (!parse_stats_block(c, entry_where() + " stats", block))
                    return KeyResult::Failed;
                e.stats = block;
            } else if (key == "combat") {
                ClasspackCombatBlock block;
                if (!parse_combat_block(c, entry_where() + " combat", block))
                    return KeyResult::Failed;
                e.combat = block;
            } else if (key == "costs") {
                ClasspackCostsBlock block;
                if (!parse_costs_block(c, entry_where() + " costs", block))
                    return KeyResult::Failed;
                e.costs = block;
            } else {
                std::vector<ClasspackSpecialEntry> list;
                if (!parse_specials_list(c, entry_where(), list))
                    return KeyResult::Failed;
                e.specials = std::move(list);
            }
            return KeyResult::Taken;
        });
    if (!ok)
        return false;
    // Half-migrated entries are the one thing dual-accept must not swallow:
    // with both spellings present, which one wins is a coin toss the author
    // never sees. Fatal, like any other unusable value.
    if (declares_schema_v1(e) && declares_schema_v2(e)) {
        c.error(entry_where() +
                ": mixes the old positional keys (base_stats, "
                "derived_bonuses, stat_costs, special_costs, special_names, "
                "alternate_names, hiring_cost, weapon_cost) with the named "
                "blocks that replaced them (stats, combat, costs, specials) "
                "— convert the whole entry");
        return false;
    }
    return true;
}

bool parse_weapon_entry(Cursor& c, ClasspackWeaponEntry& e)
{
    return parse_entry_fields(
        c, e.tuning,
        [&](const std::string& key, const Scalar& v) {
            if (key == "id") {
                if (!v.null)
                    e.id = v.text;
            } else if (key == "wire_id") {
                if (!v.null)
                    e.wire_id = v.text;
            } else if (key == "name")
                set_string(v, e.name);
            else if (key == "fire_sound")
                return set_int(c, key, v, e.fire_sound);
            else if (key == "skip_sit_notify")
                return set_bool(c, key, v, e.skip_sit_notify);
            else if (key == "is_auto_attackable")
                return set_bool(c, key, v, e.is_auto_attackable);
            else if (key == "init_lifetime")
                return set_int(c, key, v, e.init_lifetime);
            else if (key == "init_ani_type")
                return set_int(c, key, v, e.init_ani_type);
            else if (key == "vz")
                return set_float(c, key, v, e.vz);
            else if (key == "gravity")
                return set_float(c, key, v, e.gravity);
            else if (key == "sizez")
                return set_int(c, key, v, e.sizez);
            else if (key == "can_drop_floors")
                return set_bool(c, key, v, e.can_drop_floors);
            else if (key == "sprite")
                set_nullable(v, e.sprite);
            else if (key == "animation")
                set_string(v, e.animation);
            else {
                return set_presentation_field(c, key, v, e.presentation);
            }
            return true;
        },
        [&](const std::string& key, const std::vector<Scalar>& items) {
            if (key == "init_bit_flags")
                return set_string_list(c, key, items, e.init_bit_flags);
            return true;
        });
}

bool parse_effect_entry(Cursor& c, ClasspackEffectEntry& e)
{
    return parse_entry_fields(
        c, e.tuning,
        [&](const std::string& key, const Scalar& v) {
            if (key == "id") {
                if (!v.null)
                    e.id = v.text;
            } else if (key == "wire_id") {
                if (!v.null)
                    e.wire_id = v.text;
            } else if (key == "name")
                set_string(v, e.name);
            else if (key == "loops_animation")
                return set_bool(c, key, v, e.loops_animation);
            else if (key == "creates_hit_effect")
                return set_bool(c, key, v, e.creates_hit_effect);
            else if (key == "sprite")
                set_nullable(v, e.sprite);
            else if (key == "animation")
                set_string(v, e.animation);
            else {
                return set_presentation_field(c, key, v, e.presentation);
            }
            return true;
        },
        [&](const std::string& key, const std::vector<Scalar>& items) {
            if (key == "init_bit_flags")
                return set_string_list(c, key, items, e.init_bit_flags);
            return true;
        });
}

bool parse_treasure_entry(Cursor& c, ClasspackTreasureEntry& e)
{
    return parse_entry_fields(
        c, e.tuning,
        [&](const std::string& key, const Scalar& v) {
            if (key == "id") {
                if (!v.null)
                    e.id = v.text;
            } else if (key == "wire_id") {
                if (!v.null)
                    e.wire_id = v.text;
            } else if (key == "name")
                set_string(v, e.name);
            else if (key == "init_ignore")
                return set_bool(c, key, v, e.init_ignore);
            else if (key == "init_frame")
                return set_int(c, key, v, e.init_frame);
            else if (key == "sprite")
                set_nullable(v, e.sprite);
            else if (key == "animation")
                set_string(v, e.animation);
            else {
                return set_presentation_field(c, key, v, e.presentation);
            }
            return true;
        },
        [&](const std::string&, const std::vector<Scalar>&) { return true; });
}

bool parse_generator_entry(Cursor& c, ClasspackGeneratorEntry& e)
{
    return parse_entry_fields(
        c, e.tuning,
        [&](const std::string& key, const Scalar& v) {
            if (key == "id") {
                if (!v.null)
                    e.id = v.text;
            } else if (key == "wire_id") {
                if (!v.null)
                    e.wire_id = v.text;
            } else if (key == "name")
                set_string(v, e.name);
            else if (key == "default_weapon")
                set_string(v, e.default_weapon);
            else if (key == "has_lifetime")
                return set_bool(c, key, v, e.has_lifetime);
            else if (key == "spawn_ani_type")
                return set_int(c, key, v, e.spawn_ani_type);
            else if (key == "clear_owner")
                return set_bool(c, key, v, e.clear_owner);
            else if (key == "sprite")
                set_nullable(v, e.sprite);
            else if (key == "animation")
                set_string(v, e.animation);
            else if (key == "editor_label")
                set_string(v, e.editor_label);
            else {
                return set_presentation_field(c, key, v, e.presentation);
            }
            return true;
        },
        [&](const std::string&, const std::vector<Scalar>&) { return true; });
}

// --- anims: named frame sets -------------------------------------------

// One row of a set: a nested sequence of frame indices, or a plain `~` for
// a null row (the legacy anislime table has eight of those).
bool parse_anim_rows(Cursor& c, const std::string& set_name,
                     std::vector<ClasspackAnimRow>& out)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type == YAML_SCALAR_EVENT) {
            const Scalar s = scalar_of(ev);
            yaml_event_delete(&ev);
            if (!s.null) {
                c.error("anims." + set_name +
                        ": a frame row must be a list or ~");
                return false;
            }
            ClasspackAnimRow row;
            row.is_null = true;
            out.push_back(std::move(row));
            continue;
        }
        if (ev.type != YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            c.error("anims." + set_name + ": a frame row must be a list or ~");
            return false;
        }
        yaml_event_delete(&ev);
        std::vector<Scalar> items;
        if (!collect_scalars(c, items))
            return false;
        ClasspackAnimRow row;
        row.frames.reserve(items.size());
        for (const Scalar& item : items) {
            const auto parsed = parse_int_strict(item.text);
            if (item.null || !parsed) {
                c.error("anims." + set_name + ": bad frame index '" +
                        item.text + "'");
                return false;
            }
            row.frames.push_back(*parsed);
        }
        out.push_back(std::move(row));
    }
}

// The body of one named set (MAPPING_START already consumed).
bool parse_anim_set(Cursor& c, ClasspackAnimSet& set)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            c.error("anims." + set.name + ": expected a field key");
            return false;
        }
        const std::string key = scalar_of(ev).text;
        yaml_event_delete(&ev);

        yaml_event_t vev;
        if (!c.next(vev))
            return false;
        if (vev.type == YAML_SCALAR_EVENT) {
            const Scalar value = scalar_of(vev);
            yaml_event_delete(&vev);
            if (key == "rows" &&
                !set_int(c, "anims." + set.name + ".rows", value, set.rows))
                return false;
            continue;
        }
        if (vev.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&vev);
            if (key == "frames") {
                if (!parse_anim_rows(c, set.name, set.frames))
                    return false;
            } else if (!skip_node(c, YAML_SEQUENCE_START_EVENT))
                return false;
            continue;
        }
        const yaml_event_type_t type = vev.type;
        yaml_event_delete(&vev);
        if (!skip_node(c, type))
            return false;
    }
}

// `anims:` — a mapping of set name → { rows, frames } (MAPPING_START
// already consumed).
bool parse_anims(Cursor& c, ClasspackData& out)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            c.error("anims keys must be set names");
            return false;
        }
        ClasspackAnimSet set;
        set.name = scalar_of(ev).text;
        yaml_event_delete(&ev);
        if (set.name.empty()) {
            c.error("anims: a set without a name");
            return false;
        }

        yaml_event_t vev;
        if (!c.next(vev))
            return false;
        if (vev.type != YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&vev);
            c.error("anims." + set.name + " must be a mapping");
            return false;
        }
        yaml_event_delete(&vev);
        if (!parse_anim_set(c, set))
            return false;
        out.anims.push_back(std::move(set));
    }
}

// Parses one families.<order> sequence of entry mappings.
template <typename Entry, typename ParseEntryFn>
bool parse_entry_sequence(Cursor& c, std::vector<Entry>& out,
                          ParseEntryFn&& parse_entry, const char* order_name)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            c.error(std::string("families.") + order_name +
                    " entries must be mappings");
            return false;
        }
        yaml_event_delete(&ev);
        Entry entry;
        if (!parse_entry(c, entry))
            return false;
        if (entry.id.empty()) {
            c.error(std::string("families.") + order_name +
                    " entry without an id");
            return false;
        }
        out.push_back(std::move(entry));
    }
}

bool parse_families(Cursor& c, ClasspackData& out)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            c.error("families keys must be order names");
            return false;
        }
        const std::string order = scalar_of(ev).text;
        yaml_event_delete(&ev);

        yaml_event_t vev;
        if (!c.next(vev))
            return false;
        if (vev.type == YAML_SCALAR_EVENT) {
            // e.g. `weapon: ~` — an empty section.
            yaml_event_delete(&vev);
            continue;
        }
        if (vev.type != YAML_SEQUENCE_START_EVENT) {
            const yaml_event_type_t type = vev.type;
            yaml_event_delete(&vev);
            if (!skip_node(c, type))
                return false;
            continue;
        }
        yaml_event_delete(&vev);

        bool ok;
        if (order == "living")
            ok = parse_entry_sequence(c, out.living, parse_living_entry,
                                      "living");
        else if (order == "weapon")
            ok = parse_entry_sequence(c, out.weapons, parse_weapon_entry,
                                      "weapon");
        else if (order == "effect" || order == "fx")
            ok = parse_entry_sequence(c, out.effects, parse_effect_entry,
                                      "effect");
        else if (order == "treasure")
            ok = parse_entry_sequence(c, out.treasures, parse_treasure_entry,
                                      "treasure");
        else if (order == "generator")
            ok = parse_entry_sequence(c, out.generators,
                                      parse_generator_entry, "generator");
        else
            // Unknown order section: consume its sequence.
            ok = skip_node(c, YAML_SEQUENCE_START_EVENT);
        if (!ok)
            return false;
    }
}

bool parse_root(Cursor& c, ClasspackData& out)
{
    while (true) {
        yaml_event_t ev;
        if (!c.next(ev))
            return false;
        if (ev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&ev);
            return true;
        }
        if (ev.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&ev);
            c.error("root keys must be scalars");
            return false;
        }
        const std::string key = scalar_of(ev).text;
        yaml_event_delete(&ev);

        yaml_event_t vev;
        if (!c.next(vev))
            return false;
        if (key == "families") {
            if (vev.type != YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&vev);
                c.error("families must be a mapping of order names");
                return false;
            }
            yaml_event_delete(&vev);
            if (!parse_families(c, out))
                return false;
        } else if (key == "anims") {
            if (vev.type == YAML_SCALAR_EVENT) {
                // `anims: ~` — an empty section.
                yaml_event_delete(&vev);
                continue;
            }
            if (vev.type != YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&vev);
                c.error("anims must be a mapping of set names");
                return false;
            }
            yaml_event_delete(&vev);
            if (!parse_anims(c, out))
                return false;
        } else if (vev.type == YAML_SCALAR_EVENT) {
            const Scalar value = scalar_of(vev);
            yaml_event_delete(&vev);
            if (!value.null) {
                if (key == "pack")
                    out.pack = value.text;
                else if (key == "version")
                    out.version = value.text;
                else if (key == "title")
                    out.title = value.text;
                else if (key == "authors")
                    out.authors = value.text;
            }
        } else {
            const yaml_event_type_t type = vev.type;
            yaml_event_delete(&vev);
            if (!skip_node(c, type))
                return false;
        }
    }
}

} // namespace

bool parse_classpack_yaml(std::string_view text, ClasspackData& out,
                          const char* source_name)
{
    Cursor c(text, source_name != nullptr ? source_name : "(classpack)");
    if (c.failed)
        return false;

    yaml_event_t ev;
    if (!c.next(ev))
        return false;
    if (ev.type != YAML_STREAM_START_EVENT) {
        yaml_event_delete(&ev);
        c.error("expected YAML stream");
        return false;
    }
    yaml_event_delete(&ev);

    if (!c.next(ev))
        return false;
    if (ev.type != YAML_DOCUMENT_START_EVENT) {
        yaml_event_delete(&ev);
        c.error("empty classpack.yaml");
        return false;
    }
    yaml_event_delete(&ev);

    if (!c.next(ev))
        return false;
    if (ev.type != YAML_MAPPING_START_EVENT) {
        yaml_event_delete(&ev);
        c.error("classpack.yaml root must be a mapping");
        return false;
    }
    yaml_event_delete(&ev);

    if (!parse_root(c, out))
        return false;
    // Trailing DOCUMENT_END / STREAM_END events are irrelevant.
    return !c.failed;
}

} // namespace og::data
