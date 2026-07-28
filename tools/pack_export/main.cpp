/* Class-pack descriptor exporter.
 *
 * Emits packs/core/classpack.yaml from the compiled-in family registries:
 * every static data field of all five descriptor orders (living / weapon /
 * effect / treasure / generator), per the schema in
 * docs/lua-classpacks-design.md §4. Core families pin `wire_id` to their
 * legacy numeric ids (§5). Behavior callbacks are never exported — behavior
 * lives in the pack's Lua scripts.
 *
 * The output is deterministic (pure static data, ids ascending), so a
 * regenerated file diffs clean when the C++ descriptors are unchanged.
 *
 * NOTE (quality plan Stage 4): the shipped core pack now uses the SPLIT
 * layout — a header-only classpack.yaml plus one families/<order>-<NN>-
 * <slug>.yaml per entry (scripts/refactor/split_classpack.py partitions a
 * monolith), and the families/ files carry hand-maintained `tuning:`
 * blocks this exporter knows nothing about. Point the tool at a scratch
 * path for bootstrap/debug export; do NOT overwrite the shipped layout.
 *
 * Usage: pack_export [output.yaml]   (default: packs/core/classpack.yaml)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/core_pack_presentation.h>
#include <openglad/core/family_presentation.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/interface/session_state.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <string>
#include <vector>

// --- Headless process globals (same shape as tools/concept_mapgen). ---
namespace og::runtime {
static SessionState s_export_session{};
thread_local SessionState* current_session = &s_export_session;
std::atomic<SessionState*> primary_session{&s_export_session};
std::atomic<GameplayContext*> primary_game{&s_export_session.game_};
} // namespace og::runtime

void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 20260726u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace {

int g_errors = 0;

void fail(const std::string& message)
{
    std::fprintf(stderr, "pack_export: ERROR: %s\n", message.c_str());
    ++g_errors;
}

// Double-quoted YAML scalar with escaping. Descriptions carry embedded
// newlines and significant trailing spaces, so every free-text string is
// emitted in double-quoted style where "\n" is unambiguous.
std::string quoted(const char* s)
{
    std::string out = "\"";
    for (const char* p = s; *p != '\0'; p++) {
        const unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 0x20 || c == 0x7F)
                    out += std::format("\\x{:02X}", c);
                else
                    out.push_back(static_cast<char>(c));
                break;
        }
    }
    out.push_back('"');
    return out;
}

// `key: "value"` for strings, `key: ~` for null (e.g. short_name,
// description on families that have none).
std::string opt_quoted(const char* s)
{
    return s != nullptr ? quoted(s) : std::string("~");
}

std::string flow_ints(const std::int32_t* values, std::size_t n)
{
    std::string out = "[";
    for (std::size_t i = 0; i < n; i++) {
        if (i > 0)
            out += ", ";
        out += std::to_string(values[i]);
    }
    out.push_back(']');
    return out;
}

// std::format("{}", float) emits the shortest representation that parses
// back to the exact same float — the reader's strtof round-trips it.
std::string fmt_float(float v)
{
    return std::format("{}", v);
}

std::string flow_floats(const float* values, std::size_t n)
{
    std::string out = "[";
    for (std::size_t i = 0; i < n; i++) {
        if (i > 0)
            out += ", ";
        out += fmt_float(values[i]);
    }
    out.push_back(']');
    return out;
}

std::string flow_quoted(const char* const* values, std::size_t n)
{
    std::string out = "[";
    for (std::size_t i = 0; i < n; i++) {
        if (i > 0)
            out += ", ";
        out += quoted(values[i]);
    }
    out.push_back(']');
    return out;
}

// init_bit_flags as a flow list of BIT_* names ("[FLYING, SWIMMING]").
// A registry value with bits outside the shared vocabulary would silently
// lose data in a round-trip, so that is a hard export error.
std::string flow_bit_flags(std::int32_t flags, const char* context)
{
    std::int32_t unknown = 0;
    const std::vector<std::string> names =
        og::families::bit_flag_names(flags, &unknown);
    if (unknown != 0)
        fail(std::format("{}: init_bit_flags has unnamed bits {:#x}",
                         context, static_cast<std::uint32_t>(unknown)));
    std::string out = "[";
    for (std::size_t i = 0; i < names.size(); i++) {
        if (i > 0)
            out += ", ";
        out += names[i];
    }
    out.push_back(']');
    return out;
}

// --- presentation block ---------------------------------------------------
//
// The UI presentation data (curses glyph, radar blip, editor label) is
// seeded from the core-pack transcription tables in
// include/openglad/core/core_pack_presentation.h, because the C++ family
// registries do not populate those descriptor fields — the pack does, at
// mount time. A registry that DOES set one (anything other than the struct's
// default) wins, so this stays correct once the registries are fed from YAML.

std::string radar_color_scalar(int color)
{
    if (color == og::kRadarColorNone)
        return og::kRadarColorNoneName;
    if (color == og::kRadarColorTeam)
        return og::kRadarColorTeamName;
    return std::to_string(color);
}

std::string presentation_block(const og::FamilyGlyph& g,
                               const og::RadarBlip& r, const char* context)
{
    const std::string utf8 = og::glyph_to_utf8(g.codepoint);
    if (utf8.empty())
        fail(std::format("{}: glyph codepoint U+{:04X} is not encodable",
                         context, static_cast<std::uint32_t>(g.codepoint)));
    const char ascii[2] = {g.ascii, '\0'};
    std::string y;
    y += std::format("      glyph: {}\n", quoted(utf8.c_str()));
    y += std::format("      glyph_ascii: {}\n", quoted(ascii));
    y += std::format("      glyph_color: {}\n", og::glyph_color_name(g.color));
    y += std::format("      glyph_bold: {}\n", g.bold);
    y += std::format("      glyph_transparent: {}\n", g.transparent);
    y += std::format("      radar_color: {}\n", radar_color_scalar(r.color));
    y += std::format("      radar_jitter: {}\n", r.jitter);
    return y;
}

// The four non-living orders have no built-in `animation:` vocabulary: their
// tables come from the gloader EntityDef rows, and an installed pack table
// cannot be named back from its pointer. The exporter runs on pristine
// registries, so this is always null — a non-null table means someone
// exported after installing a pack, which would silently lose data.
std::string anim_set_scalar(const signed char* const* table,
                            const char* context)
{
    if (table != nullptr)
        fail(std::format("{}: an installed animation table cannot be exported",
                         context));
    return "~";
}

// "the descriptor's value if a registry set one, else the core-pack seed".
og::FamilyGlyph seeded_glyph(const og::FamilyGlyph& from_registry,
                             const og::FamilyGlyph& registry_default,
                             const og::FamilyGlyph& seed)
{
    return from_registry == registry_default ? seed : from_registry;
}

og::RadarBlip seeded_radar(const og::RadarBlip& from_registry,
                           const og::RadarBlip& seed)
{
    return from_registry == og::RadarBlip{} ? seed : from_registry;
}

// Same rule for the level editor's generator caption.
const char* seeded_editor_label(const char* from_registry, int family_id)
{
    const char* blank = GeneratorFamilyDescriptor{}.editor_label;
    if (from_registry != nullptr && std::strcmp(from_registry, blank) != 0)
        return from_registry;
    return og::corepack::generator_editor_label(family_id);
}

// Canonical string id ("core:knife" / positional "core:#19") for a family
// reference; export fails when the target family does not exist.
std::string family_ref(Order order, int family_id, const char* context)
{
    std::string id = og::families::family_string_id(order, family_id);
    if (id.empty())
        fail(std::format("{}: no family {} in order {}", context, family_id,
                         static_cast<int>(order)));
    return id;
}

void export_living(std::string& y)
{
    y += "  living:\n";
    for (int id = 0; get_family_descriptor(id) != nullptr; id++) {
        const FamilyDescriptor* d = get_family_descriptor(id);
        const std::string sid = family_ref(Order::Living, id, "living");
        y += std::format("    - id: {}\n", sid);
        y += std::format("      wire_id: {}\n", id);
        y += std::format("      name: {}\n", quoted(d->name));
        y += std::format("      short_name: {}\n", opt_quoted(d->short_name));
        y += std::format("      base_stats: {}\n", flow_ints(d->base_stats, 6));
        y += std::format("      hiring_cost: {}\n", d->hiring_cost);
        y += std::format("      derived_bonuses: {}\n",
                         flow_floats(d->derived_bonuses, 8));
        y += std::format("      stat_costs: {}\n", flow_ints(d->stat_costs, 6));
        {
            std::int32_t costs[FD_NUM_SPECIALS];
            for (int i = 0; i < FD_NUM_SPECIALS; i++)
                costs[i] = d->special_cost[i];
            y += std::format("      special_costs: {}\n",
                             flow_ints(costs, FD_NUM_SPECIALS));
        }
        y += std::format("      weapon_cost: {}\n", d->weapon_cost);
        y += std::format("      default_weapon: {}\n",
                         family_ref(Order::Weapon, d->default_weapon, sid.c_str()));
        y += std::format("      init_bit_flags: {}\n",
                         flow_bit_flags(d->init_bit_flags, sid.c_str()));
        y += std::format("      init_ani_type: {}\n",
                         static_cast<int>(d->init_ani_type));
        y += std::format("      init_max_magicpoints: {}\n",
                         fmt_float(d->init_max_magicpoints));
        y += std::format("      special_names: {}\n",
                         flow_quoted(d->special_names, FD_NUM_SPECIALS));
        y += std::format("      alternate_names: {}\n",
                         flow_quoted(d->alternate_names, FD_NUM_SPECIALS));
        y += std::format("      leaves_bloodspot: {}\n", d->leaves_bloodspot);
        y += std::format("      magic_damage_modifier: {}\n",
                         fmt_float(d->magic_damage_modifier));
        y += std::format("      is_stationary: {}\n", d->is_stationary);
        y += std::format("      has_returning_weapon: {}\n",
                         d->has_returning_weapon);
        y += std::format("      is_undead: {}\n", d->is_undead);
        if (d->promotes_to >= 0)
            y += std::format("      promotes_to: {}\n",
                             family_ref(Order::Living, d->promotes_to,
                                        sid.c_str()));
        else
            y += "      promotes_to: ~\n";
        y += std::format("      promotion_level_req: {}\n",
                         d->promotion_level_req);
        y += std::format("      death_message: {}\n",
                         opt_quoted(d->death_message));
        y += std::format("      sprite: {}\n", opt_quoted(d->pix_filename));
        y += std::format("      animation: {}\n",
                         og::families::animation_type_name(d->animation_type));
        y += std::format("      ai_line_of_sight: {}\n", d->ai_line_of_sight);
        y += std::format("      description: {}\n",
                         opt_quoted(d->description));
        y += std::format("      names: {}\n",
                         flow_quoted(d->name_pool,
                                     d->name_pool != nullptr
                                         ? static_cast<std::size_t>(
                                               d->name_pool_size)
                                         : 0));
        y += std::format("      playable: {}\n", d->is_playable);
        y += std::format("      playable_order: {}\n", d->playable_order);
        y += presentation_block(
            seeded_glyph(d->glyph, FamilyDescriptor{}.glyph,
                         og::corepack::living_glyph(id)),
            seeded_radar(d->radar, og::RadarBlip{}), sid.c_str());
    }
}

void export_weapons(std::string& y)
{
    y += "  weapon:\n";
    for (int id = 0; get_weapon_family_descriptor(id) != nullptr; id++) {
        const WeaponFamilyDescriptor* d = get_weapon_family_descriptor(id);
        const std::string sid = family_ref(Order::Weapon, id, "weapon");
        y += std::format("    - id: {}\n", sid);
        y += std::format("      wire_id: {}\n", id);
        y += std::format("      name: {}\n", quoted(d->name));
        y += std::format("      fire_sound: {}\n", d->fire_sound);
        y += std::format("      skip_sit_notify: {}\n", d->skip_sit_notify);
        y += std::format("      is_auto_attackable: {}\n",
                         d->is_auto_attackable);
        y += std::format("      init_bit_flags: {}\n",
                         flow_bit_flags(d->init_bit_flags, sid.c_str()));
        y += std::format("      init_lifetime: {}\n", d->init_lifetime);
        y += std::format("      init_ani_type: {}\n",
                         static_cast<int>(d->init_ani_type));
        y += std::format("      vz: {}\n", fmt_float(d->init_vz));
        y += std::format("      gravity: {}\n", fmt_float(d->gravity));
        y += std::format("      sizez: {}\n", d->init_sizez);
        y += std::format("      can_drop_floors: {}\n", d->can_drop_floors);
        y += std::format("      sprite: {}\n", opt_quoted(d->pix_filename));
        y += std::format("      animation: {}\n",
                         anim_set_scalar(d->anim_table, sid.c_str()));
        y += presentation_block(
            seeded_glyph(d->glyph, WeaponFamilyDescriptor{}.glyph,
                         og::corepack::weapon_glyph(id)),
            seeded_radar(d->radar, og::RadarBlip{}), sid.c_str());
    }
}

void export_effects(std::string& y)
{
    y += "  effect:\n";
    for (int id = 0; get_effect_family_descriptor(id) != nullptr; id++) {
        const EffectFamilyDescriptor* d = get_effect_family_descriptor(id);
        const std::string sid = family_ref(Order::FX, id, "effect");
        y += std::format("    - id: {}\n", sid);
        y += std::format("      wire_id: {}\n", id);
        y += std::format("      name: {}\n", quoted(d->name));
        y += std::format("      loops_animation: {}\n", d->loops_animation);
        y += std::format("      creates_hit_effect: {}\n",
                         d->creates_hit_effect);
        y += std::format("      init_bit_flags: {}\n",
                         flow_bit_flags(d->init_bit_flags, sid.c_str()));
        y += std::format("      sprite: {}\n", opt_quoted(d->pix_filename));
        y += std::format("      animation: {}\n",
                         anim_set_scalar(d->anim_table, sid.c_str()));
        y += presentation_block(
            seeded_glyph(d->glyph, EffectFamilyDescriptor{}.glyph,
                         og::corepack::effect_glyph(id)),
            seeded_radar(d->radar, og::RadarBlip{}), sid.c_str());
    }
}

void export_treasures(std::string& y)
{
    y += "  treasure:\n";
    for (int id = 0; get_treasure_family_descriptor(id) != nullptr; id++) {
        const TreasureFamilyDescriptor* d = get_treasure_family_descriptor(id);
        const std::string sid = family_ref(Order::Treasure, id, "treasure");
        y += std::format("    - id: {}\n", sid);
        y += std::format("      wire_id: {}\n", id);
        y += std::format("      name: {}\n", quoted(d->name));
        y += std::format("      init_ignore: {}\n", d->init_ignore);
        y += std::format("      init_frame: {}\n", d->init_frame);
        y += std::format("      sprite: {}\n", opt_quoted(d->pix_filename));
        y += std::format("      animation: {}\n",
                         anim_set_scalar(d->anim_table, sid.c_str()));
        y += presentation_block(
            seeded_glyph(d->glyph, TreasureFamilyDescriptor{}.glyph,
                         og::corepack::treasure_glyph(id)),
            seeded_radar(d->radar, og::corepack::treasure_radar(id)),
            sid.c_str());
    }
}

void export_generators(std::string& y)
{
    y += "  generator:\n";
    for (int id = 0; get_generator_family_descriptor(id) != nullptr; id++) {
        const GeneratorFamilyDescriptor* d =
            get_generator_family_descriptor(id);
        const std::string sid = family_ref(Order::Generator, id, "generator");
        y += std::format("    - id: {}\n", sid);
        y += std::format("      wire_id: {}\n", id);
        y += std::format("      name: {}\n", quoted(d->name));
        // GeneratorFamilyDescriptor.default_weapon names the LIVING family
        // the generator produces (tent → skeleton), so the reference
        // resolves through the living registry.
        y += std::format("      default_weapon: {}\n",
                         family_ref(Order::Living, d->default_weapon,
                                    sid.c_str()));
        y += std::format("      has_lifetime: {}\n", d->has_lifetime);
        y += std::format("      spawn_ani_type: {}\n",
                         static_cast<int>(d->spawn_ani_type));
        y += std::format("      clear_owner: {}\n", d->clear_owner);
        y += std::format("      sprite: {}\n", opt_quoted(d->pix_filename));
        y += std::format("      animation: {}\n",
                         anim_set_scalar(d->anim_table, sid.c_str()));
        y += std::format("      editor_label: {}\n",
                         quoted(seeded_editor_label(d->editor_label, id)));
        y += presentation_block(
            seeded_glyph(d->glyph, GeneratorFamilyDescriptor{}.glyph,
                         og::corepack::generator_glyph(id)),
            seeded_radar(d->radar, og::RadarBlip{}), sid.c_str());
    }
}

} // namespace

int main(int argc, char* argv[])
{
    const char* out_path = argc > 1 ? argv[1] : "packs/core/classpack.yaml";

    init_all_registries();

    std::string y;
    y += "# Core class-pack descriptor data (docs/lua-classpacks-design.md §4).\n";
    y += "# GENERATED by tools/pack_export from the C++ family registries —\n";
    y += "# do not hand-edit. Regenerate:\n";
    y += "#   cmake --build --preset ci-test --target pack_export\n";
    y += "#   ./build/ci-test/pack_export packs/core/classpack.yaml\n";
    y += "#\n";
    y += "# The core pack ships no `anims:` section: every core family uses a\n";
    y += "# built-in animation table (animation: standard|mage|skeleton|\n";
    y += "# giant_skeleton|slime|small_slime|static for livings, the gloader\n";
    y += "# EntityDef row for the other orders). A mod pack declares its own\n";
    y += "# frame sets under a top-level `anims:` mapping and names them from a\n";
    y += "# family's `animation:` key — see docs/lua-classpacks-design.md §4.\n";
    y += "pack: core\n";
    y += "version: 1\n";
    y += "title: \"OpenGlad Core Families\"\n";
    y += "authors: \"FSGames / the OpenGlad project\"\n";
    y += "families:\n";
    export_living(y);
    export_weapons(y);
    export_effects(y);
    export_treasures(y);
    export_generators(y);

    if (g_errors > 0) {
        std::fprintf(stderr, "pack_export: %d error(s); not writing %s\n",
                     g_errors, out_path);
        return 1;
    }

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "pack_export: cannot open %s for writing\n",
                     out_path);
        return 1;
    }
    out << y;
    out.close();
    if (!out) {
        std::fprintf(stderr, "pack_export: write to %s failed\n", out_path);
        return 1;
    }
    std::fprintf(stderr, "pack_export: wrote %s (%zu bytes)\n", out_path,
                 y.size());
    return 0;
}
