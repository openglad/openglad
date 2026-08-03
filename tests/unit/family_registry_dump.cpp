/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "family_registry_dump.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/script/family_tuning.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace og::testing {

namespace {

// --- scalars ---------------------------------------------------------------

void field(std::string& out, const char* key, const std::string& value)
{
    out += "  ";
    out += key;
    out += " = ";
    out += value;
    out += '\n';
}

std::string quoted(const char* text)
{
    if (text == nullptr)
        return "~";  // the descriptor's own "none"; never an empty string
    std::string out = "\"";
    for (const char* p = text; *p != '\0'; p++) {
        const unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            // Trailing spaces in a description are significant and must
            // survive a round trip through this text, so anything outside
            // plain printable ASCII is escaped rather than emitted raw.
            if (c < 0x20 || c >= 0x7f) {
                char esc[8];
                std::snprintf(esc, sizeof(esc), "\\x%02x", c);
                out += esc;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

std::string number(long long v)
{
    return std::to_string(v);
}

// %.9g round-trips a float exactly, so a value that changed type on its way
// into the descriptor shows as a changed line instead of hiding behind a
// short print.
std::string real(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

std::string yes_no(bool v)
{
    return v ? "true" : "false";
}

// A behavior callback is compared for PRESENCE only: its address is a link
// artifact, and what matters is that a pack-installed family has none.
template <typename Fn>
std::string callback(Fn fn)
{
    return fn != nullptr ? "set" : "none";
}

// --- shared descriptor parts ----------------------------------------------

void dump_glyph(std::string& out, const og::FamilyGlyph& g)
{
    field(out, "glyph.codepoint", number(static_cast<long long>(g.codepoint)));
    field(out, "glyph.ascii", number(static_cast<long long>(g.ascii)));
    field(out, "glyph.color", number(static_cast<long long>(g.color)));
    field(out, "glyph.bold", yes_no(g.bold));
    field(out, "glyph.transparent", yes_no(g.transparent));
}

void dump_radar(std::string& out, const og::RadarBlip& r)
{
    field(out, "radar.color", number(r.color));
    field(out, "radar.jitter", number(r.jitter));
}

// The pack-shipped frame table: row count, then each row's frames up to the
// -1 sentinel. A null row is a hole the pack punched on purpose.
void dump_anims(std::string& out, const signed char* const* table, int rows)
{
    field(out, "anim_row_count", number(rows));
    if (table == nullptr) {
        field(out, "anim_table", "~");
        return;
    }
    for (int r = 0; r < rows; r++) {
        const std::string key = "anim_table[" + std::to_string(r) + "]";
        if (table[r] == nullptr) {
            field(out, key.c_str(), "~");
            continue;
        }
        std::string row;
        // The rows are -1 terminated by contract. The cap is a guard, not a
        // limit: a row that hits it is malformed and the dump says so
        // rather than walking off the end.
        for (int i = 0; i < 256; i++) {
            if (table[r][i] < 0)
                break;
            if (!row.empty())
                row += ' ';
            row += std::to_string(static_cast<int>(table[r][i]));
            if (i == 255)
                row += " ...UNTERMINATED";
        }
        field(out, key.c_str(), row.empty() ? std::string("(empty)") : row);
    }
}

// Tuning, sorted by key — see the header for why order is not compared.
void dump_tuning(std::string& out, Order order, int family_id)
{
    const og::script::TuningMap* map =
        og::script::family_tuning(order, family_id);
    if (map == nullptr) {
        field(out, "tuning", "~");
        return;
    }
    std::vector<const og::script::TuningPair*> pairs;
    pairs.reserve(map->size());
    for (const og::script::TuningPair& p : *map)
        pairs.push_back(&p);
    std::sort(pairs.begin(), pairs.end(),
              [](const og::script::TuningPair* a,
                 const og::script::TuningPair* b) { return a->key < b->key; });
    field(out, "tuning.count", number(static_cast<long long>(pairs.size())));
    for (const og::script::TuningPair* p : pairs) {
        const std::string key = "tuning[" + p->key + "]";
        switch (p->value.kind) {
        case og::script::TuningValue::Kind::Integer:
            field(out, key.c_str(), "int " + number(p->value.integer));
            break;
        case og::script::TuningValue::Kind::Number:
            field(out, key.c_str(), "num " + real(p->value.number));
            break;
        case og::script::TuningValue::Kind::Boolean:
            field(out, key.c_str(), "bool " + yes_no(p->value.boolean));
            break;
        case og::script::TuningValue::Kind::String:
            field(out, key.c_str(),
                  "str " + quoted(p->value.string.c_str()));
            break;
        }
    }
}

void block(std::string& out, const char* order_name, int family_id)
{
    out += "[";
    out += order_name;
    out += ' ';
    out += std::to_string(family_id);
    out += "]\n";
}

// --- the five orders -------------------------------------------------------

void dump_living(std::string& out)
{
    static const char* const kStatAxis[StatAxis::Count] = {
        "strength", "dexterity", "constitution", "intelligence", "armor",
        "level"};
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const FamilyDescriptor* fd = get_family_descriptor(id);
        if (fd == nullptr)
            continue;
        block(out, "living", id);
        field(out, "declared_id", quoted(fd->declared_id));
        field(out, "family_id", number(fd->family_id));
        field(out, "name", quoted(fd->name));
        field(out, "short_name", quoted(fd->short_name));
        for (int a = 0; a < StatAxis::Count; a++) {
            field(out, (std::string("base_stats.") + kStatAxis[a]).c_str(),
                  number(fd->base_stats[a]));
            field(out, (std::string("stat_costs.") + kStatAxis[a]).c_str(),
                  number(fd->stat_costs[a]));
        }
        field(out, "hiring_cost", number(fd->hiring_cost));
        field(out, "combat.hp", real(fd->combat.hp));
        field(out, "combat.melee_damage", real(fd->combat.melee_damage));
        field(out, "combat.stepsize", real(fd->combat.stepsize));
        field(out, "combat.fire_delay", real(fd->combat.fire_delay));
        field(out, "combat.fire_mp_cost", number(fd->combat.fire_mp_cost));
        for (int s = 0; s < FD_NUM_SPECIALS; s++) {
            const std::string prefix = "special[" + std::to_string(s) + "].";
            field(out, (prefix + "id").c_str(), quoted(fd->special_ids[s]));
            field(out, (prefix + "name").c_str(),
                  quoted(fd->special_names[s]));
            field(out, (prefix + "alternate").c_str(),
                  quoted(fd->alternate_names[s]));
            field(out, (prefix + "cost").c_str(),
                  number(fd->special_cost[s]));
        }
        field(out, "default_weapon", number(fd->default_weapon));
        field(out, "init_bit_flags", number(fd->init_bit_flags));
        field(out, "init_ani_type",
              number(static_cast<long long>(fd->init_ani_type)));
        field(out, "init_max_magicpoints", real(fd->init_max_magicpoints));
        field(out, "leaves_bloodspot", yes_no(fd->leaves_bloodspot));
        field(out, "magic_damage_modifier", real(fd->magic_damage_modifier));
        field(out, "is_stationary", yes_no(fd->is_stationary));
        field(out, "has_returning_weapon", yes_no(fd->has_returning_weapon));
        field(out, "is_undead", yes_no(fd->is_undead));
        field(out, "promotes_to", number(fd->promotes_to));
        field(out, "promotion_level_req", number(fd->promotion_level_req));
        field(out, "promotion_new_level", callback(fd->promotion_new_level));
        field(out, "death_message", quoted(fd->death_message));
        field(out, "cb.do_special", callback(fd->do_special));
        field(out, "cb.check_special_ai", callback(fd->check_special_ai));
        field(out, "cb.hit_response", callback(fd->hit_response));
        field(out, "cb.set_difficulty", callback(fd->set_difficulty));
        field(out, "cb.level_up", callback(fd->level_up));
        field(out, "cb.on_death", callback(fd->on_death));
        field(out, "cb.on_act_living", callback(fd->on_act_living));
        field(out, "cb.on_shoved", callback(fd->on_shoved));
        field(out, "cb.on_fire_weapon", callback(fd->on_fire_weapon));
        field(out, "cb.handle_teleport", callback(fd->handle_teleport));
        field(out, "cb.on_create", callback(fd->on_create));
        field(out, "cb.customize_weapon", callback(fd->customize_weapon));
        field(out, "cb.on_ani_complete", callback(fd->on_ani_complete));
        field(out, "cb.on_melee_hit", callback(fd->on_melee_hit));
        field(out, "pix_filename", quoted(fd->pix_filename));
        field(out, "animation_type",
              number(static_cast<long long>(fd->animation_type)));
        field(out, "ai_line_of_sight", number(fd->ai_line_of_sight));
        dump_anims(out, fd->anim_table, fd->anim_row_count);
        field(out, "description", quoted(fd->description));
        field(out, "name_pool_size", number(fd->name_pool_size));
        for (int n = 0; n < fd->name_pool_size; n++) {
            const std::string key = "name_pool[" + std::to_string(n) + "]";
            field(out, key.c_str(),
                  quoted(fd->name_pool == nullptr ? nullptr
                                                  : fd->name_pool[n]));
        }
        field(out, "is_playable", yes_no(fd->is_playable));
        field(out, "playable_order", number(fd->playable_order));
        dump_glyph(out, fd->glyph);
        dump_radar(out, fd->radar);
        dump_tuning(out, Order::Living, id);
    }
}

void dump_weapons(std::string& out)
{
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const WeaponFamilyDescriptor* fd = get_weapon_family_descriptor(id);
        if (fd == nullptr)
            continue;
        block(out, "weapon", id);
        field(out, "declared_id", quoted(fd->declared_id));
        field(out, "family_id", number(fd->family_id));
        field(out, "name", quoted(fd->name));
        field(out, "fire_sound", number(fd->fire_sound));
        field(out, "skip_sit_notify", yes_no(fd->skip_sit_notify));
        field(out, "is_auto_attackable", yes_no(fd->is_auto_attackable));
        field(out, "init_bit_flags", number(fd->init_bit_flags));
        field(out, "init_lifetime", number(fd->init_lifetime));
        field(out, "init_ani_type",
              number(static_cast<long long>(fd->init_ani_type)));
        field(out, "init_vz", real(fd->init_vz));
        field(out, "gravity", real(fd->gravity));
        field(out, "init_sizez", number(fd->init_sizez));
        field(out, "can_drop_floors", yes_no(fd->can_drop_floors));
        field(out, "pix_filename", quoted(fd->pix_filename));
        dump_anims(out, fd->anim_table, fd->anim_row_count);
        dump_glyph(out, fd->glyph);
        dump_radar(out, fd->radar);
        field(out, "cb.on_death", callback(fd->on_death));
        field(out, "cb.on_animate", callback(fd->on_animate));
        field(out, "cb.on_hit_target", callback(fd->on_hit_target));
        dump_tuning(out, Order::Weapon, id);
    }
}

void dump_effects(std::string& out)
{
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const EffectFamilyDescriptor* fd = get_effect_family_descriptor(id);
        if (fd == nullptr)
            continue;
        block(out, "effect", id);
        field(out, "declared_id", quoted(fd->declared_id));
        field(out, "family_id", number(fd->family_id));
        field(out, "name", quoted(fd->name));
        field(out, "loops_animation", yes_no(fd->loops_animation));
        field(out, "creates_hit_effect", yes_no(fd->creates_hit_effect));
        field(out, "init_bit_flags", number(fd->init_bit_flags));
        field(out, "pix_filename", quoted(fd->pix_filename));
        dump_anims(out, fd->anim_table, fd->anim_row_count);
        dump_glyph(out, fd->glyph);
        dump_radar(out, fd->radar);
        field(out, "cb.on_act", callback(fd->on_act));
        field(out, "cb.on_death", callback(fd->on_death));
        dump_tuning(out, Order::FX, id);
    }
}

void dump_treasures(std::string& out)
{
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const TreasureFamilyDescriptor* fd =
            get_treasure_family_descriptor(id);
        if (fd == nullptr)
            continue;
        block(out, "treasure", id);
        field(out, "declared_id", quoted(fd->declared_id));
        field(out, "family_id", number(fd->family_id));
        field(out, "name", quoted(fd->name));
        field(out, "init_ignore", yes_no(fd->init_ignore));
        field(out, "init_frame", number(fd->init_frame));
        field(out, "pix_filename", quoted(fd->pix_filename));
        dump_anims(out, fd->anim_table, fd->anim_row_count);
        dump_glyph(out, fd->glyph);
        dump_radar(out, fd->radar);
        field(out, "cb.on_eat", callback(fd->on_eat));
        dump_tuning(out, Order::Treasure, id);
    }
}

void dump_generators(std::string& out)
{
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const GeneratorFamilyDescriptor* fd =
            get_generator_family_descriptor(id);
        if (fd == nullptr)
            continue;
        block(out, "generator", id);
        field(out, "declared_id", quoted(fd->declared_id));
        field(out, "family_id", number(fd->family_id));
        field(out, "name", quoted(fd->name));
        field(out, "default_weapon", number(fd->default_weapon));
        field(out, "has_lifetime", yes_no(fd->has_lifetime));
        field(out, "spawn_ani_type",
              number(static_cast<long long>(fd->spawn_ani_type)));
        field(out, "clear_owner", yes_no(fd->clear_owner));
        field(out, "pix_filename", quoted(fd->pix_filename));
        dump_anims(out, fd->anim_table, fd->anim_row_count);
        dump_glyph(out, fd->glyph);
        dump_radar(out, fd->radar);
        field(out, "editor_label", quoted(fd->editor_label));
        dump_tuning(out, Order::Generator, id);
    }
}

}  // namespace

std::string dump_installed_families()
{
    std::string out;
    out.reserve(1 << 17);
    dump_living(out);
    dump_weapons(out);
    dump_effects(out);
    dump_treasures(out);
    dump_generators(out);
    return out;
}

}  // namespace og::testing
