/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/resources/packs.h>

#include <openglad/core/order.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/classpack_yaml.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/physfs_api.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace og::resources {

int refresh_pack_scripts()
{
    og::script::clear_pack_scripts();
    const int scripts = register_mounted_pack_scripts();
    // Mount changes can add/remove classpack.yaml data too (campaign-
    // embedded packs); reinstall keeps registries in step with scripts.
    install_classpacks();
    return scripts;
}

int register_mounted_pack_scripts()
{
    int registered = 0;
    // Sorted enumeration keeps the replay order identical on every peer.
    for (const std::string& pack_id :
         og::io::physfs_enumerate_files_sorted("packs")) {
        const std::string scripts_dir = "packs/" + pack_id + "/scripts";
        for (const std::string& file :
             og::io::physfs_enumerate_files_sorted(scripts_dir)) {
            if (file.size() < 4 ||
                file.compare(file.size() - 4, 4, ".lua") != 0)
                continue;
            const std::string vpath = scripts_dir + "/" + file;
            std::vector<std::uint8_t> bytes = read_file(vpath.c_str());
            if (bytes.empty()) {
                LogWarn("class pack script unreadable: {}\n", vpath);
                continue;
            }
            og::script::register_pack_script(
                {pack_id, vpath,
                 std::string(reinterpret_cast<const char*>(bytes.data()),
                             bytes.size())});
            registered++;
        }
    }
    if (registered > 0)
        Log("Registered {} class pack script(s)\n", registered);
    return registered;
}

// ---------------------------------------------------------------------------
// classpack.yaml → registry install
// ---------------------------------------------------------------------------

namespace {

// Owns every installed ClasspackData for the life of the process, plus the
// name-pool pointer arrays the FamilyDescriptor name_pool field borrows.
// Descriptor const char* fields point straight into the stored structs'
// std::strings (stable: the unique_ptr fixes each ClasspackData's address
// and entries are never mutated after install; the deque never moves
// existing pools). DELIBERATELY leaked — never destroyed — because the
// gameplay registries keep these borrows through static teardown.
// (LeakSanitizer counts a reachable singleton as "still reachable", not a
// leak.)
struct ClasspackStore {
    std::vector<std::unique_ptr<og::data::ClasspackData>> packs;
    std::deque<std::vector<const char*>> name_pools;
};

ClasspackStore& classpack_store()
{
    static ClasspackStore* store = new ClasspackStore();
    return *store;
}

// Deterministic wire-id assignment for one install run: core packs pin
// explicit ids; wire_id auto/absent takes the next id >= 21 per order in
// encounter order (packs are visited pack-id-lexicographically, entries in
// YAML order). Ids 0..20 are reserved for the core pins, and every install
// pass starts from a registry whose mod slots were just freed, so the
// counter reproduces the same assignment on every peer. A pack that pins an
// id >= 21 explicitly can still collide with an auto id — pin the whole
// pack or none of it. Entries past a registry's capacity (256 ids per
// order) are rejected by the slot lookup.
struct AutoWireIds {
    std::int32_t next[8] = {21, 21, 21, 21, 21, 21, 21, 21};

    int take(Order order)
    {
        return next[static_cast<int>(order)]++;
    }
};

int resolve_wire_id(const std::string& wire_id, Order order,
                    AutoWireIds& autos, const std::string& id)
{
    if (wire_id.empty() || wire_id == "auto")
        return autos.take(order);
    const auto parsed = parse_int_strict(wire_id);
    if (!parsed || *parsed < 0 || *parsed > 255) {
        LogWarn("classpack {}: bad wire_id '{}' — entry skipped\n", id,
                wire_id);
        return -1;
    }
    return *parsed;
}

// Resolves a family reference ("core:knife") against the given order's
// registry. Unresolved references keep the current descriptor value.
int resolve_ref(Order order, const std::string& ref, const char* field,
                const std::string& id)
{
    const int target =
        og::families::resolve_family_string_id(order, ref.c_str());
    if (target < 0)
        LogWarn("classpack {}: unresolved {} '{}' — field kept\n", id, field,
                ref);
    return target;
}

// Folds init_bit_flags names into a mask. An unknown name keeps the
// current descriptor mask (warn, no partial fold).
bool fold_bit_flags(const std::vector<std::string>& names,
                    std::int32_t& out, const std::string& id)
{
    std::int32_t flags = 0;
    for (const std::string& name : names) {
        const std::int32_t bit = og::families::bit_flag_from_name(name);
        if (bit == 0) {
            LogWarn(
                "classpack {}: unknown bit flag '{}' — init_bit_flags "
                "kept\n",
                id, name);
            return false;
        }
        flags |= bit;
    }
    out = flags;
    return true;
}

// The stored entry's string (owned by the ClasspackStore) or nullptr for
// an explicit YAML null.
const char* nullable_cstr(const og::data::NullableString& s)
{
    return s.is_null ? nullptr : s.value.c_str();
}

// Builds the name_pool pointer array for one entry in the store; the
// pointers reference the stored entry's names vector.
void apply_name_pool(const std::vector<std::string>& names,
                     FamilyDescriptor& d)
{
    if (names.empty()) {
        d.name_pool = nullptr;
        d.name_pool_size = 0;
        return;
    }
    std::vector<const char*> pool;
    pool.reserve(names.size());
    for (const std::string& name : names)
        pool.push_back(name.c_str());
    classpack_store().name_pools.push_back(std::move(pool));
    d.name_pool = classpack_store().name_pools.back().data();
    d.name_pool_size =
        static_cast<int>(classpack_store().name_pools.back().size());
}

// --- per-order installers (entries must already live in the store) ------

bool install_living(const og::data::ClasspackLivingEntry& e,
                    AutoWireIds& autos)
{
    const int id = resolve_wire_id(e.wire_id, Order::Living, autos, e.id);
    if (id < 0)
        return false;
    // The install slot: an occupied slot hands back its live descriptor, a
    // free one the order's defaults. nullptr means the id is past capacity.
    const FamilyDescriptor* current = get_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no living registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    // Copying the live descriptor preserves every behavior callback
    // pointer; only the data fields the YAML declares are overwritten.
    FamilyDescriptor d = *current;
    if (e.name)
        d.name = e.name->c_str();
    if (e.short_name.present)
        d.short_name = nullable_cstr(e.short_name);
    if (e.base_stats) {
        const std::size_t n = std::min<std::size_t>(6, e.base_stats->size());
        for (std::size_t i = 0; i < n; i++)
            d.base_stats[i] = (*e.base_stats)[i];
    }
    if (e.hiring_cost)
        d.hiring_cost = *e.hiring_cost;
    if (e.derived_bonuses) {
        const std::size_t n =
            std::min<std::size_t>(8, e.derived_bonuses->size());
        for (std::size_t i = 0; i < n; i++)
            d.derived_bonuses[i] = (*e.derived_bonuses)[i];
    }
    if (e.stat_costs) {
        const std::size_t n = std::min<std::size_t>(6, e.stat_costs->size());
        for (std::size_t i = 0; i < n; i++)
            d.stat_costs[i] = (*e.stat_costs)[i];
    }
    if (e.special_costs) {
        const std::size_t n = std::min<std::size_t>(
            FD_NUM_SPECIALS, e.special_costs->size());
        for (std::size_t i = 0; i < n; i++)
            d.special_cost[i] =
                static_cast<unsigned short>((*e.special_costs)[i]);
    }
    if (e.weapon_cost)
        d.weapon_cost = static_cast<short>(*e.weapon_cost);
    if (e.default_weapon) {
        const int weapon = resolve_ref(Order::Weapon, *e.default_weapon,
                                       "default_weapon", e.id);
        if (weapon >= 0)
            d.default_weapon = weapon;
    }
    if (e.init_bit_flags) {
        std::int32_t flags = 0;
        if (fold_bit_flags(*e.init_bit_flags, flags, e.id))
            d.init_bit_flags = flags;
    }
    if (e.init_ani_type)
        d.init_ani_type = static_cast<char>(*e.init_ani_type);
    if (e.init_max_magicpoints)
        d.init_max_magicpoints = *e.init_max_magicpoints;
    if (e.special_names) {
        const std::size_t n = std::min<std::size_t>(
            FD_NUM_SPECIALS, e.special_names->size());
        for (std::size_t i = 0; i < n; i++)
            d.special_names[i] = (*e.special_names)[i].c_str();
    }
    if (e.alternate_names) {
        const std::size_t n = std::min<std::size_t>(
            FD_NUM_SPECIALS, e.alternate_names->size());
        for (std::size_t i = 0; i < n; i++)
            d.alternate_names[i] = (*e.alternate_names)[i].c_str();
    }
    if (e.leaves_bloodspot)
        d.leaves_bloodspot = *e.leaves_bloodspot;
    if (e.magic_damage_modifier)
        d.magic_damage_modifier = *e.magic_damage_modifier;
    if (e.is_stationary)
        d.is_stationary = *e.is_stationary;
    if (e.has_returning_weapon)
        d.has_returning_weapon = *e.has_returning_weapon;
    if (e.is_undead)
        d.is_undead = *e.is_undead;
    if (e.promotes_to.present) {
        if (e.promotes_to.is_null)
            d.promotes_to = -1;
        else {
            const int target = resolve_ref(
                Order::Living, e.promotes_to.value, "promotes_to", e.id);
            if (target >= 0)
                d.promotes_to = target;
        }
    }
    if (e.promotion_level_req)
        d.promotion_level_req = *e.promotion_level_req;
    if (e.death_message.present)
        d.death_message = nullable_cstr(e.death_message);
    if (e.sprite.present)
        d.pix_filename = nullable_cstr(e.sprite);
    if (e.animation) {
        FamilyAnimationType type{};
        if (og::families::animation_type_from_name(*e.animation, type))
            d.animation_type = type;
        else
            LogWarn("classpack {}: unknown animation '{}' — kept\n", e.id,
                    *e.animation);
    }
    if (e.ai_line_of_sight)
        d.ai_line_of_sight = *e.ai_line_of_sight;
    if (e.description.present)
        d.description = nullable_cstr(e.description);
    if (e.names)
        apply_name_pool(*e.names, d);
    if (e.playable)
        d.is_playable = *e.playable;
    if (e.playable_order)
        d.playable_order = *e.playable_order;

    return set_family_descriptor(id, d);
}

bool install_weapon(const og::data::ClasspackWeaponEntry& e,
                    AutoWireIds& autos)
{
    const int id = resolve_wire_id(e.wire_id, Order::Weapon, autos, e.id);
    if (id < 0)
        return false;
    const WeaponFamilyDescriptor* current =
        get_weapon_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no weapon registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    WeaponFamilyDescriptor d = *current;
    if (e.name)
        d.name = e.name->c_str();
    if (e.fire_sound)
        d.fire_sound = *e.fire_sound;
    if (e.skip_sit_notify)
        d.skip_sit_notify = *e.skip_sit_notify;
    if (e.is_auto_attackable)
        d.is_auto_attackable = *e.is_auto_attackable;
    if (e.init_bit_flags) {
        std::int32_t flags = 0;
        if (fold_bit_flags(*e.init_bit_flags, flags, e.id))
            d.init_bit_flags = flags;
    }
    if (e.init_lifetime)
        d.init_lifetime = static_cast<short>(*e.init_lifetime);
    if (e.init_ani_type)
        d.init_ani_type = static_cast<char>(*e.init_ani_type);
    if (e.vz)
        d.init_vz = *e.vz;
    if (e.gravity)
        d.gravity = *e.gravity;
    if (e.sizez)
        d.init_sizez = static_cast<short>(*e.sizez);
    if (e.can_drop_floors)
        d.can_drop_floors = *e.can_drop_floors;

    return set_weapon_family_descriptor(id, d);
}

bool install_effect(const og::data::ClasspackEffectEntry& e,
                    AutoWireIds& autos)
{
    const int id = resolve_wire_id(e.wire_id, Order::FX, autos, e.id);
    if (id < 0)
        return false;
    const EffectFamilyDescriptor* current =
        get_effect_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no effect registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    EffectFamilyDescriptor d = *current;
    if (e.name)
        d.name = e.name->c_str();
    if (e.loops_animation)
        d.loops_animation = *e.loops_animation;
    if (e.creates_hit_effect)
        d.creates_hit_effect = *e.creates_hit_effect;
    if (e.init_bit_flags) {
        std::int32_t flags = 0;
        if (fold_bit_flags(*e.init_bit_flags, flags, e.id))
            d.init_bit_flags = flags;
    }

    return set_effect_family_descriptor(id, d);
}

bool install_treasure(const og::data::ClasspackTreasureEntry& e,
                      AutoWireIds& autos)
{
    const int id = resolve_wire_id(e.wire_id, Order::Treasure, autos, e.id);
    if (id < 0)
        return false;
    const TreasureFamilyDescriptor* current =
        get_treasure_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no treasure registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    TreasureFamilyDescriptor d = *current;
    if (e.name)
        d.name = e.name->c_str();
    if (e.init_ignore)
        d.init_ignore = *e.init_ignore;
    if (e.init_frame)
        d.init_frame = static_cast<short>(*e.init_frame);

    return set_treasure_family_descriptor(id, d);
}

bool install_generator(const og::data::ClasspackGeneratorEntry& e,
                       AutoWireIds& autos)
{
    const int id = resolve_wire_id(e.wire_id, Order::Generator, autos, e.id);
    if (id < 0)
        return false;
    const GeneratorFamilyDescriptor* current =
        get_generator_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no generator registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    GeneratorFamilyDescriptor d = *current;
    if (e.name)
        d.name = e.name->c_str();
    if (e.default_weapon) {
        // Generator default_weapon names the LIVING family produced.
        const int living = resolve_ref(Order::Living, *e.default_weapon,
                                       "default_weapon", e.id);
        if (living >= 0)
            d.default_weapon = living;
    }
    if (e.has_lifetime)
        d.has_lifetime = *e.has_lifetime;
    if (e.spawn_ani_type)
        d.spawn_ani_type = static_cast<char>(*e.spawn_ani_type);
    if (e.clear_owner)
        d.clear_owner = *e.clear_owner;

    return set_generator_family_descriptor(id, d);
}

// Installs every entry of a STORED pack. Order: weapons and the other
// non-living orders first, then livings (whose default_weapon may name a
// weapon this pack just installed), then generators (whose default_weapon
// may name a living this pack just installed).
int install_pack_families(const og::data::ClasspackData& pack,
                          AutoWireIds& autos)
{
    int installed = 0;
    for (const auto& e : pack.weapons)
        installed += install_weapon(e, autos) ? 1 : 0;
    for (const auto& e : pack.effects)
        installed += install_effect(e, autos) ? 1 : 0;
    for (const auto& e : pack.treasures)
        installed += install_treasure(e, autos) ? 1 : 0;
    for (const auto& e : pack.living)
        installed += install_living(e, autos) ? 1 : 0;
    for (const auto& e : pack.generators)
        installed += install_generator(e, autos) ? 1 : 0;
    return installed;
}

} // namespace

int install_classpacks()
{
    int packs_seen = 0;
    int installed = 0;
    AutoWireIds autos;
    // A full re-install: free the slots the previous pass claimed so the
    // registries end up describing exactly the packs mounted right now (an
    // unmounted pack must not leave a family behind, and auto ids must
    // restart from the same place on every peer). Core pins are untouched.
    reset_all_registry_mod_slots();
    // Same deterministic pack order as script registration.
    for (const std::string& pack_id :
         og::io::physfs_enumerate_files_sorted("packs")) {
        const std::string vpath = "packs/" + pack_id + "/classpack.yaml";
        std::vector<std::uint8_t> bytes = read_file(vpath.c_str());
        if (bytes.empty())
            continue; // pack without classpack.yaml (scripts-only) is fine
        auto parsed = std::make_unique<og::data::ClasspackData>();
        if (!og::data::parse_classpack_yaml(
                std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                 bytes.size()),
                *parsed, vpath.c_str())) {
            LogWarn("class pack '{}' rejected: classpack.yaml unusable\n",
                    pack_id);
            continue;
        }
        // Move into the process-lifetime store BEFORE installing:
        // descriptors borrow the stored strings.
        og::data::ClasspackData& stored = *parsed;
        classpack_store().packs.push_back(std::move(parsed));
        installed += install_pack_families(stored, autos);
        packs_seen++;
    }
    if (packs_seen > 0)
        Log("Installed {} class pack(s): {} family descriptor(s)\n",
            packs_seen, installed);
    return installed;
}

int install_classpack_data(og::data::ClasspackData&& data)
{
    auto parsed =
        std::make_unique<og::data::ClasspackData>(std::move(data));
    og::data::ClasspackData& stored = *parsed;
    classpack_store().packs.push_back(std::move(parsed));
    AutoWireIds autos;
    return install_pack_families(stored, autos);
}

}  // namespace og::resources
