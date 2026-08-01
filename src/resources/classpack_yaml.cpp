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

// --- generic entry walker ----------------------------------------------

// Walks the key/value pairs of one family entry (MAPPING_START already
// consumed). on_scalar / on_list return false to abort (cursor error set).
// `tuning` receives the entry's `tuning:` map (every order has one).
template <typename SetScalarFn, typename SetListFn>
bool parse_entry_fields(Cursor& c, std::vector<ClasspackTuningPair>& tuning,
                        SetScalarFn&& on_scalar, SetListFn&& on_list)
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
                std::vector<Scalar> items;
                if (!collect_scalars(c, items))
                    return false;
                if (!on_list(key, items))
                    return false;
                break;
            }
            case YAML_MAPPING_START_EVENT:
                yaml_event_delete(&vev);
                if (key == "tuning") {
                    if (!parse_tuning_map(c, tuning))
                        return false;
                    break;
                }
                // Unknown nested mapping: skip (forward compatibility).
                if (!skip_node(c, YAML_MAPPING_START_EVENT))
                    return false;
                break;
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

bool parse_living_entry(Cursor& c, ClasspackLivingEntry& e)
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
        });
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
