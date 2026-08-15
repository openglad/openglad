/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The INSTALLER, and the string-id table it fills (headless).
//
// Everything downstream of the declaration pass: a harvested ClasspackData
// goes into install_classpack_data or arrives from a real mount, and these
// tests read the registries back. Wire-id assignment (pinned, auto, out of
// range, freed on unmount), string-id resolution for all five orders, the
// sparse copy-and-patch override rule, presentation, pack-shipped animation
// tables and the tuning store are all installer contracts, independent of
// how the data was written.
//
// The front end that writes it is `og.family` — see
// tests/unit/test_classpack_lua_decl.cpp for the declaration pass and its
// error surface. Where a test here needs data, it declares it, because a
// hand-built ClasspackData can hold shapes no author can write.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/campaign_ids.h>
#include <openglad/core/family_presentation.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/pack_transfer_io.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>  // mkdtemp lives here on macOS (stdlib.h on glibc)
#endif

using og::data::ClasspackData;

namespace {

// One family chunk, harvested. This is the front end: registering the chunk
// is what a mount does, and declare_pack_families is what the installer
// calls before it touches a registry. The chunk name stays clear of the
// `packs/` prefix on purpose — that prefix declares the bytes to the
// pack-Lua coverage inventory (pack_scripts.h), which is right for the
// mounted scratch packs further down this file and wrong for a literal.
void declare_or_die(const std::string& lua, ClasspackData& out,
                    const char* pack_id = "testpack")
{
    og::script::clear_pack_family_chunks();
    og::script::register_pack_family_chunk(
        {pack_id, std::string(pack_id) + "/families/a.lua", lua});
    const og::script::DeclareResult r =
        og::script::declare_pack_families(pack_id, out);
    og::script::clear_pack_family_chunks();
    ASSERT_TRUE(r.ok) << r.error;
}

// A costs block with a hire price and no training table — the smallest
// declaration that gives an entry one value worth checking after an install.
og::data::ClasspackCostsBlock hire_only(std::int32_t gold)
{
    og::data::ClasspackCostsBlock costs;
    costs.hire = gold;
    return costs;
}

} // namespace

// ---------------------------------------------------------------------------
// Family string-id resolution + the installer's name vocabulary
// ---------------------------------------------------------------------------

TEST(FamilyStringIds, resolution)
{
    init_all_registries();

    ASSERT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:soldier"),
              FAMILY_SOLDIER);
    ASSERT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "soldier"),
              FAMILY_SOLDIER)
        << "bare names resolve too";
    ASSERT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:#19"),
              FAMILY_GIANT_SKELETON);
    ASSERT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:no_such"),
              -1);
}

TEST(FamilyStringIds, reader_vocabulary)
{
    const std::pair<const char*, FamilyAnimationType> animation_types[] = {
        {"standard", FamilyAnimationType::FAMILY_ANIM_STANDARD},
        {"mage", FamilyAnimationType::FAMILY_ANIM_MAGE},
        {"skeleton", FamilyAnimationType::FAMILY_ANIM_SKELETON},
        {"giant_skeleton", FamilyAnimationType::FAMILY_ANIM_GIANT_SKELETON},
        {"slime", FamilyAnimationType::FAMILY_ANIM_SLIME},
        {"small_slime", FamilyAnimationType::FAMILY_ANIM_SMALL_SLIME},
        {"static", FamilyAnimationType::FAMILY_ANIM_STATIC},
    };
    for (const auto& [name, expected] : animation_types) {
        FamilyAnimationType actual{};
        ASSERT_TRUE(og::families::animation_type_from_name(name, actual));
        ASSERT_EQ(actual, expected);
    }
    FamilyAnimationType unused{};
    ASSERT_FALSE(og::families::animation_type_from_name("moonwalk", unused));

    ASSERT_EQ(og::families::bit_flag_from_name("FLYING"), BIT_FLYING);
    ASSERT_EQ(og::families::bit_flag_from_name("ETHEREAL"), BIT_ETHEREAL);
    ASSERT_EQ(og::families::bit_flag_from_name("SPELUNKING"), 0);
}

// ---------------------------------------------------------------------------
// The committed core pack, declared
// ---------------------------------------------------------------------------

namespace {

// Loads the committed core pack the way the loader does: every
// families/*.lua in sorted filename order, evaluated by the declaration
// pass into ONE ClasspackData. The pack header is og.pack, declared in
// families/00-pack.lua like everything else. Every committed-core test
// goes through it.
void load_committed_core_pack(ClasspackData& data)
{
    const auto read_all = [](const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    };
    std::vector<std::filesystem::path> family_chunks;
    for (const auto& entry :
         std::filesystem::directory_iterator("packs/core/families"))
    {
        if (entry.path().extension() == ".lua")
            family_chunks.push_back(entry.path());
    }
    std::sort(family_chunks.begin(), family_chunks.end());
    // 69 declaration files (the slime trio shares one; the CTF flag/
    // waypoint pair left with the CTF retirement) plus the header.
    ASSERT_EQ(family_chunks.size(), 70u);
    for (const std::filesystem::path& p : family_chunks) {
        og::script::register_pack_family_chunk(
            {"core", "packs/core/families/" + p.filename().string(),
             read_all(p)});
    }
    const og::script::DeclareResult declared =
        og::script::declare_pack_families("core", data);
    ASSERT_TRUE(declared.ok) << declared.error;
}

}  // namespace

TEST(CommittedCorePack, matches_the_built_in_registries)
{
    ClasspackData data;
    load_committed_core_pack(data);
    if (::testing::Test::HasFatalFailure())
        return;
    // The og.pack header, which is all families/00-pack.lua declares.
    ASSERT_EQ(data.pack, "core");
    ASSERT_EQ(data.version, "1");
    ASSERT_EQ(data.title, "OpenGlad Core Families");
    ASSERT_EQ(data.authors, "FSGames / the OpenGlad project");
    ASSERT_EQ(data.living.size(), static_cast<std::size_t>(NUM_FAMILIES));
    ASSERT_EQ(data.weapons.size(), 20u);
    ASSERT_EQ(data.effects.size(), 13u);
    ASSERT_EQ(data.treasures.size(), 13u);
    ASSERT_EQ(data.generators.size(), 4u);

    init_all_registries();
    // Spot-check the soldier entry against the live registry: the
    // committed declaration must mirror the C++ descriptor data exactly.
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(fd, nullptr);
    const auto& e = data.living[0];
    ASSERT_EQ(e.id, "core:soldier");
    ASSERT_EQ(e.wire_id, "0");
    ASSERT_EQ(*e.name, fd->name);
    ASSERT_TRUE(e.stats.has_value());
    ASSERT_EQ(e.stats->strength, fd->base_stats[StatAxis::Strength]);
    ASSERT_EQ(e.stats->dexterity, fd->base_stats[StatAxis::Dexterity]);
    ASSERT_EQ(e.stats->constitution, fd->base_stats[StatAxis::Constitution]);
    ASSERT_EQ(e.stats->intelligence, fd->base_stats[StatAxis::Intelligence]);
    ASSERT_EQ(e.stats->armor, fd->base_stats[StatAxis::Armor]);
    ASSERT_EQ(e.stats->level, fd->base_stats[StatAxis::Level]);
    ASSERT_TRUE(e.combat.has_value());
    ASSERT_EQ(e.combat->hp, fd->combat.hp);
    ASSERT_EQ(e.combat->melee_damage, fd->combat.melee_damage);
    ASSERT_EQ(e.combat->stepsize, fd->combat.stepsize);
    ASSERT_EQ(e.combat->fire_delay, fd->combat.fire_delay);
    ASSERT_EQ(e.combat->fire_mp_cost, fd->combat.fire_mp_cost);
    ASSERT_TRUE(e.costs.has_value());
    ASSERT_EQ(e.costs->hire, fd->hiring_cost);
    ASSERT_TRUE(e.costs->train.has_value());
    ASSERT_EQ(e.costs->train->strength, fd->stat_costs[StatAxis::Strength]);
    ASSERT_EQ(e.costs->train->armor, fd->stat_costs[StatAxis::Armor]);
    // The specials list is slot-ordered and its ids are what soldier.lua
    // keys its handlers by; the disabled slots it omits are the registry
    // defaults (index 0 is the engine artifact, index 5 is unused here).
    ASSERT_TRUE(e.specials.has_value());
    ASSERT_EQ(e.specials->size(), 4u);
    for (const og::data::ClasspackSpecialEntry& s : *e.specials) {
        ASSERT_STREQ(s.name.c_str(), fd->special_names[s.slot]);
        ASSERT_EQ(s.mp_cost, fd->special_cost[s.slot]);
        ASSERT_STREQ(s.id.c_str(), fd->special_ids[s.slot]);
    }
    ASSERT_EQ((*e.specials)[0].id, "charge");
    ASSERT_EQ((*e.specials)[0].slot, 1);
    ASSERT_EQ((*e.specials)[3].id, "disarm");
    ASSERT_EQ((*e.specials)[3].slot, 4);
    ASSERT_EQ(e.default_weapon.value_or(""), "core:knife");
    ASSERT_EQ(e.description.value, fd->description);
    ASSERT_EQ(e.names->size(),
              static_cast<std::size_t>(fd->name_pool_size));
    ASSERT_EQ(e.animation.value_or(""), "standard");

    // Every living entry's wire_id is the pinned legacy byte, and every one
    // of them declares all four blocks. A family that omitted one would
    // install whatever the registry slot happened to hold, which for a mod
    // slot is nothing at all.
    for (std::size_t i = 0; i < data.living.size(); i++) {
        ASSERT_EQ(data.living[i].wire_id, std::to_string(i));
        ASSERT_TRUE(data.living[i].stats.has_value()) << data.living[i].id;
        ASSERT_TRUE(data.living[i].combat.has_value()) << data.living[i].id;
        ASSERT_TRUE(data.living[i].costs.has_value()) << data.living[i].id;
        ASSERT_TRUE(data.living[i].specials.has_value()) << data.living[i].id;
    }
}

// ---------------------------------------------------------------------------
// Registry install: declared data overwrites, callbacks preserved
// ---------------------------------------------------------------------------

TEST(ClasspackInstall, overrides_data_preserves_callbacks)
{
    init_all_registries();
    const FamilyDescriptor before = *get_family_descriptor(FAMILY_SOLDIER);

    og::data::ClasspackData data;
    data.pack = "test";
    og::data::ClasspackLivingEntry e;
    e.id = "test:soldier_override";
    e.wire_id = "0"; // pins the soldier slot
    e.stats = og::data::ClasspackStatsBlock{99, 6, 12, 8, 9, 1};
    e.death_message.present = true;
    e.death_message.value = "SOLDIER TESTED";
    e.names = std::vector<std::string>{"Alpha", "Beta"};
    data.living.push_back(std::move(e));

    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* after = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(after, nullptr);
    // Declared fields reflect the override...
    ASSERT_EQ(after->base_stats[0], 99);
    ASSERT_STREQ(after->death_message, "SOLDIER TESTED");
    ASSERT_EQ(after->name_pool_size, 2);
    ASSERT_STREQ(after->name_pool[0], "Alpha");
    ASSERT_STREQ(after->name_pool[1], "Beta");
    // ...undeclared data fields keep their current values...
    ASSERT_EQ(after->hiring_cost, before.hiring_cost);
    ASSERT_STREQ(after->name, "SOLDIER");
    ASSERT_EQ(after->description, before.description)
        << "absent description must keep the exact current pointer";
    ASSERT_EQ(after->default_weapon, before.default_weapon);
    // ...and EVERY behavior callback pointer is preserved unchanged.
    ASSERT_EQ(after->promotion_new_level, before.promotion_new_level);
    ASSERT_EQ(after->do_special, before.do_special);
    ASSERT_EQ(after->check_special_ai, before.check_special_ai);
    ASSERT_EQ(after->hit_response, before.hit_response);
    ASSERT_EQ(after->set_difficulty, before.set_difficulty);
    ASSERT_EQ(after->level_up, before.level_up);
    ASSERT_EQ(after->on_death, before.on_death);
    ASSERT_EQ(after->on_act_living, before.on_act_living);
    ASSERT_EQ(after->on_shoved, before.on_shoved);
    ASSERT_EQ(after->on_fire_weapon, before.on_fire_weapon);
    ASSERT_EQ(after->handle_teleport, before.handle_teleport);
    ASSERT_EQ(after->on_create, before.on_create);
    ASSERT_EQ(after->customize_weapon, before.customize_weapon);
    ASSERT_EQ(after->on_ani_complete, before.on_ani_complete);
    ASSERT_EQ(after->on_melee_hit, before.on_melee_hit);

    // Restore the pristine descriptor for the rest of the process.
    ASSERT_TRUE(set_family_descriptor(FAMILY_SOLDIER, before));
}

// #209: `radar_ping = true` rides the presentation fold onto the
// descriptor's RadarBlip; an absent key keeps the current (false) value.
TEST(ClasspackInstall, radar_ping_installs_onto_the_descriptor)
{
    init_all_registries();
    const FamilyDescriptor before = *get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_FALSE(before.radar.ping) << "core families ship no ping";

    og::data::ClasspackData data;
    data.pack = "test";
    og::data::ClasspackLivingEntry e;
    e.id = "test:soldier_ping";
    e.wire_id = "0"; // pins the soldier slot
    e.presentation.radar_ping = true;
    data.living.push_back(std::move(e));

    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    const FamilyDescriptor* after = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(after, nullptr);
    EXPECT_TRUE(after->radar.ping);
    EXPECT_EQ(after->radar.color, before.radar.color)
        << "ping never disturbs the blip colour";

    // A second declaration that omits the key keeps the current value.
    og::data::ClasspackData keep;
    keep.pack = "test";
    og::data::ClasspackLivingEntry e2;
    e2.id = "test:soldier_ping2";
    e2.wire_id = "0";
    keep.living.push_back(std::move(e2));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(keep)), 1);
    EXPECT_TRUE(get_family_descriptor(FAMILY_SOLDIER)->radar.ping)
        << "omitting radar_ping keeps whatever the slot holds";

    // Restore the pristine descriptor for the rest of the process.
    ASSERT_TRUE(set_family_descriptor(FAMILY_SOLDIER, before));
}

TEST(ClasspackInstall, wire_id_pins_and_references_resolve)
{
    init_all_registries();
    const FamilyDescriptor before_mage = *get_family_descriptor(FAMILY_MAGE);
    const GeneratorFamilyDescriptor before_tent =
        *get_generator_family_descriptor(FAMILY_TENT);

    og::data::ClasspackData data;
    {
        og::data::ClasspackLivingEntry e;
        e.id = "test:mage_override";
        e.wire_id = "3"; // FAMILY_MAGE
        e.default_weapon = "core:rock";
        e.promotes_to.present = true; // explicit ~ → no promotion
        e.promotes_to.is_null = true;
        data.living.push_back(std::move(e));
    }
    {
        og::data::ClasspackGeneratorEntry g;
        g.id = "test:tent_override";
        g.wire_id = "0"; // FAMILY_TENT
        g.default_weapon = "core:ghost"; // living family reference
        data.generators.push_back(std::move(g));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 2);

    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    ASSERT_EQ(mage->default_weapon, FAMILY_ROCK)
        << "default_weapon resolves through the weapon registry";
    ASSERT_EQ(mage->promotes_to, -1) << "explicit ~ clears the promotion";
    ASSERT_EQ(mage->do_special, before_mage.do_special);

    const GeneratorFamilyDescriptor* tent =
        get_generator_family_descriptor(FAMILY_TENT);
    ASSERT_EQ(tent->default_weapon, FAMILY_GHOST)
        << "generator default_weapon resolves through the living registry";

    ASSERT_TRUE(set_family_descriptor(FAMILY_MAGE, before_mage));
    ASSERT_TRUE(set_generator_family_descriptor(FAMILY_TENT, before_tent));
}

TEST(ClasspackInstall, a_declaration_installs_and_skips_bad_refs)
{
    init_all_registries();
    const WeaponFamilyDescriptor before_rock =
        *get_weapon_family_descriptor(FAMILY_ROCK);
    const FamilyDescriptor before_elf = *get_family_descriptor(FAMILY_ELF);

    og::data::ClasspackData data;
    declare_or_die(
        "og.family('weapon', { id = 'test:rock', wire_id = 1,\n"
        "                      fire_sound = 42,\n"
        "                      flags = { 'MAGICAL', 'FIRE' } })\n"
        "og.family('living', { id = 'test:elf', wire_id = 1,\n"
        "                      default_weapon = 'test:no_such_weapon',\n"
        "                      animation = 'moonwalk',\n"
        "                      costs = { hire = 7 } })\n",
        data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 2);

    const WeaponFamilyDescriptor* rock =
        get_weapon_family_descriptor(FAMILY_ROCK);
    ASSERT_EQ(rock->fire_sound, 42);
    ASSERT_EQ(rock->init_bit_flags, BIT_MAGICAL | BIT_FIRE);
    ASSERT_EQ(rock->on_death, before_rock.on_death);
    ASSERT_EQ(rock->on_animate, before_rock.on_animate);
    ASSERT_EQ(rock->on_hit_target, before_rock.on_hit_target);

    const FamilyDescriptor* elf = get_family_descriptor(FAMILY_ELF);
    ASSERT_EQ(elf->hiring_cost, 7) << "good fields apply";
    ASSERT_EQ(elf->default_weapon, before_elf.default_weapon)
        << "unresolved reference keeps the current value";
    ASSERT_EQ(elf->animation_type, before_elf.animation_type)
        << "unknown animation name keeps the current value";

    ASSERT_TRUE(set_weapon_family_descriptor(FAMILY_ROCK, before_rock));
    ASSERT_TRUE(set_family_descriptor(FAMILY_ELF, before_elf));
}

// ---------------------------------------------------------------------------
// The named blocks → the descriptor's parallel arrays
// ---------------------------------------------------------------------------
//
// A living descriptor still holds the shapes the DOS data files had: six
// base_stats columns, six stat_costs columns, and three parallel
// special_* arrays with a dead slot 0 and a disabled-cost sentinel. The
// declaration spells none of that — it names its axes and lists its
// specials — so the fold is the installer's, and these four cases are what
// says it folds the way the arrays were written by hand.

namespace {

// The soldier's shipped numbers, on a free wire slot. Every value is one
// the original arrays carried:
//   base_stats: [12, 6, 12, 8, 9, 1]      hiring_cost: 250
//   derived_bonuses: [120, 0, 20, 0, 0, 0, 4, 6]   weapon_cost: 2
//   stat_costs: [6, 10, 6, 25, 50, 200]
//   special_costs: [5000, 25, 100, 120, 150, 5000]
//   special_names: ["NONE", "CHARGE", "BOOMERANG", "WHIRLWIND", "DISARM",
//                   "NONE"]
//   alternate_names: ["NONE", "NONE", "NONE", "MYSTIC MACE", "NONE", "NONE"]
constexpr const char* kSoldierDecl = R"LUA(
og.family('living', {
  id = 'decl:soldier',
  wire_id = 61,
  name = 'SOLDIER',
  stats  = { strength = 12, dexterity = 6, constitution = 12,
             intelligence = 8, armor = 9, level = 1 },
  combat = { hp = 120, melee_damage = 20, stepsize = 4,
             fire_delay = 6, fire_mp_cost = 2 },
  costs  = { hire = 250,
             train = { strength = 6, dexterity = 10, constitution = 6,
                       intelligence = 25, armor = 50, level = 200 } },
  specials = {
    { id = 'charge',    name = 'CHARGE',    mp_cost = 25 },
    { id = 'boomerang', name = 'BOOMERANG', mp_cost = 100 },
    { id = 'whirlwind', name = 'WHIRLWIND', mp_cost = 120,
      alternate = { name = 'MYSTIC MACE' } },
    { id = 'disarm',    name = 'DISARM',    mp_cost = 150 },
  },
})
)LUA";

}  // namespace

TEST(ClasspackInstall, a_declaration_installs_the_shipped_soldier_bytes)
{
    init_all_registries();

    ClasspackData data;
    declare_or_die(kSoldierDecl, data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* d = get_family_descriptor(61);
    ASSERT_NE(d, nullptr);

    // The six base_stats columns, in the order the array had them.
    const std::int32_t stats[StatAxis::Count] = {12, 6, 12, 8, 9, 1};
    const std::int32_t train[StatAxis::Count] = {6, 10, 6, 25, 50, 200};
    for (int i = 0; i < StatAxis::Count; i++) {
        ASSERT_EQ(d->base_stats[i], stats[i]) << "base_stats " << i;
        ASSERT_EQ(d->stat_costs[i], train[i]) << "stat_costs " << i;
    }
    ASSERT_EQ(d->hiring_cost, 250);
    // The four live derived_bonuses columns: 0, 2, 6, 7.
    ASSERT_EQ(d->combat.hp, 120.0f);
    ASSERT_EQ(d->combat.melee_damage, 20.0f);
    ASSERT_EQ(d->combat.stepsize, 4.0f);
    ASSERT_EQ(d->combat.fire_delay, 6.0f);
    ASSERT_EQ(d->combat.fire_mp_cost, 2);

    // The three parallel special arrays, folded out of one list. Slot 0 is
    // the engine artifact and slot 5 was never declared; both are the
    // disabled pair, which is what the arrays spelled out by hand.
    const char* names[FD_NUM_SPECIALS] = {"NONE",      "CHARGE",   "BOOMERANG",
                                          "WHIRLWIND", "DISARM",   "NONE"};
    const unsigned short costs[FD_NUM_SPECIALS] = {5000, 25, 100, 120, 150,
                                                   5000};
    for (int i = 0; i < FD_NUM_SPECIALS; i++) {
        ASSERT_STREQ(d->special_names[i], names[i]) << "slot " << i;
        ASSERT_EQ(d->special_cost[i], costs[i]) << "slot " << i;
    }
    ASSERT_STREQ(d->alternate_names[3], "MYSTIC MACE");
    ASSERT_STREQ(d->alternate_names[1], "NONE");

    // ...and the ids the arrays could not spell: a script keys its handler
    // by these, and og.family binds its casts through them.
    ASSERT_STREQ(d->special_ids[1], "charge");
    ASSERT_STREQ(d->special_ids[4], "disarm");
    ASSERT_EQ(d->special_ids[0], nullptr) << "slot 0 is not a special";
    ASSERT_EQ(d->special_ids[5], nullptr) << "undeclared slot has no id";
}

// A specials list is not sparse: it rewrites all five slots. Anything else
// and a mod that renamed one special would silently inherit four.
TEST(ClasspackInstall, a_specials_list_rewrites_every_slot)
{
    init_all_registries();
    ClasspackData full;
    declare_or_die(kSoldierDecl, full);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(full)), 1);
    ASSERT_STREQ(get_family_descriptor(61)->special_names[4], "DISARM");

    ClasspackData thin;
    declare_or_die(
        "og.family('living', { id = 'decl:soldier', wire_id = 61,\n"
        "  specials = { { id = 'charge', name = 'CHARGE',\n"
        "                 mp_cost = 25 } } })\n",
        thin);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(thin)), 1);

    const FamilyDescriptor* d = get_family_descriptor(61);
    ASSERT_STREQ(d->special_names[1], "CHARGE");
    ASSERT_STREQ(d->special_names[4], "NONE");
    ASSERT_EQ(d->special_cost[4], kSpecialCostDisabled);
    ASSERT_EQ(d->special_ids[4], nullptr);
    // Everything outside the list is untouched by a sparse entry.
    ASSERT_EQ(d->base_stats[StatAxis::Strength], 12);
}

// `slot = N` jumps the cursor and the list resumes after it, so a family
// can leave a hole where an older one had a special and still fill slot 5.
TEST(ClasspackInstall, an_explicit_slot_leaves_a_hole_behind_it)
{
    init_all_registries();
    ClasspackData data;
    declare_or_die(
        "og.family('living', { id = 'decl:slots', wire_id = 62,\n"
        "  specials = {\n"
        "    { id = 'a', name = 'A', mp_cost = 1 },\n"
        "    { id = 'b', name = 'B', mp_cost = 2, slot = 4 },\n"
        "    { id = 'c', name = 'C', mp_cost = 3 } } })\n",
        data);
    const auto& list = *data.living[0].specials;
    ASSERT_EQ(list[0].slot, 1);
    ASSERT_EQ(list[1].slot, 4);
    ASSERT_EQ(list[2].slot, 5) << "the list resumes after an explicit slot";

    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    const FamilyDescriptor* d = get_family_descriptor(62);
    ASSERT_NE(d, nullptr);
    ASSERT_STREQ(d->special_names[1], "A");
    ASSERT_STREQ(d->special_names[2], "NONE") << "the hole is a hole";
    ASSERT_STREQ(d->special_names[3], "NONE");
    ASSERT_STREQ(d->special_names[4], "B");
    ASSERT_STREQ(d->special_names[5], "C")
        << "slot 5 is real: a level-13 caster cycles into it";
    ASSERT_EQ(d->special_cost[5], 3);
    ASSERT_STREQ(d->special_ids[5], "c");
}

// A training table names only the axes it prices. The rest install 0 —
// free to train, exactly as the original tables shipped it.
TEST(ClasspackInstall, absent_train_axes_install_zero)
{
    init_all_registries();
    ClasspackData data;
    declare_or_die(
        "og.family('living', { id = 'decl:sparse', wire_id = 63,\n"
        "  costs = { hire = 40, train = { strength = 7 } } })\n",
        data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    const FamilyDescriptor* d = get_family_descriptor(63);
    ASSERT_NE(d, nullptr);
    ASSERT_EQ(d->hiring_cost, 40);
    ASSERT_EQ(d->stat_costs[StatAxis::Strength], 7);
    ASSERT_EQ(d->stat_costs[StatAxis::Armor], 0)
        << "an unpriced axis is 0, exactly as v1 shipped it";
}

// ---------------------------------------------------------------------------
// Mod families above the core pins
// ---------------------------------------------------------------------------

namespace {

// The five registries are process-global and every install test shares
// them. Frees the pack-installed slots on the way IN and OUT, so a
// shuffled run order can never leak a mod family into a test that counts
// core families (and so each test's `auto` ids start from the same place).
// Core pins are never touched by reset_all_registry_mod_slots().
class ModSlotGuard {
public:
    ModSlotGuard()
    {
        init_all_registries();
        reset_all_registry_mod_slots();
    }
    ~ModSlotGuard() { reset_all_registry_mod_slots(); }

    ModSlotGuard(const ModSlotGuard&) = delete;
    ModSlotGuard& operator=(const ModSlotGuard&) = delete;
};

// Which ids of one order currently answer a descriptor. Used to prove an
// install changes exactly one slot and leaves every never-populated id
// answering nullptr.
template <typename GetFn>
std::vector<bool> populated_map(GetFn get)
{
    std::vector<bool> out(static_cast<std::size_t>(NUM_FAMILY_SLOTS), false);
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++)
        out[static_cast<std::size_t>(id)] = get(id) != nullptr;
    return out;
}

}  // namespace

// The headline third-party-class case: a pack entry with `wire_id: auto`
// must land in a real slot above the core pins, become visible through the
// ordinary getter, and resolve by string id — while every id the install
// did NOT claim keeps answering nullptr.
TEST(ClasspackInstall, auto_wire_id_lands_above_core_pins_and_resolves)
{
    ModSlotGuard guard;

    const std::vector<bool> living_before =
        populated_map(get_family_descriptor);
    const std::vector<bool> weapon_before =
        populated_map(get_weapon_family_descriptor);
    const std::vector<bool> effect_before =
        populated_map(get_effect_family_descriptor);
    ASSERT_EQ(get_family_descriptor(NUM_FAMILIES), nullptr)
        << "the first living mod slot starts free";

    og::data::ClasspackData data;
    data.pack = "mod";
    {
        og::data::ClasspackLivingEntry e;
        e.id = "mod:warlock";
        e.wire_id = "auto";
        e.name = "WARLOCK";
        e.costs = hire_only(250);
        e.default_weapon = "core:knife";
        e.death_message.present = true;
        e.death_message.value = "WARLOCK UNDONE";
        data.living.push_back(std::move(e));
    }
    {
        // The weapon core count is 20 and the auto counter starts at 21, so
        // this lands ABOVE a free slot 20 — a hole the id scans must walk
        // past rather than mistake for the end of the registry.
        og::data::ClasspackWeaponEntry w;
        w.id = "mod:hexbolt";
        w.wire_id = "auto";
        w.name = "HEXBOLT";
        w.fire_sound = 42;
        data.weapons.push_back(std::move(w));
    }
    {
        // Effects pin only 13 core ids, so this one sits above EIGHT
        // consecutive free slots (13..20).
        og::data::ClasspackEffectEntry fx;
        fx.id = "mod:hexburst";
        fx.wire_id = "auto";
        fx.name = "HEXBURST";
        fx.loops_animation = true;
        data.effects.push_back(std::move(fx));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 3);

    // (a) the auto ids land above the core pins.
    const int living_id =
        og::families::resolve_family_string_id(Order::Living, "mod:warlock");
    const int weapon_id =
        og::families::resolve_family_string_id(Order::Weapon, "mod:hexbolt");
    const int effect_id =
        og::families::resolve_family_string_id(Order::FX, "mod:hexburst");
    ASSERT_GE(living_id, NUM_FAMILIES) << "mod living family below the pins";
    ASSERT_GE(weapon_id, NUM_FAMILIES) << "mod weapon family below the pins";
    ASSERT_GE(effect_id, NUM_FAMILIES) << "mod effect family below the pins";
    // A fresh auto counter starts each order at the first id past the
    // living pins, so the assignment is exactly reproducible.
    EXPECT_EQ(living_id, NUM_FAMILIES);
    EXPECT_EQ(weapon_id, NUM_FAMILIES);
    EXPECT_EQ(effect_id, NUM_FAMILIES);

    // (b) the ordinary getters hand the mod descriptors back.
    const FamilyDescriptor* warlock = get_family_descriptor(living_id);
    ASSERT_NE(warlock, nullptr);
    EXPECT_EQ(warlock->family_id, living_id);
    EXPECT_STREQ(warlock->name, "WARLOCK");
    EXPECT_EQ(warlock->hiring_cost, 250);
    EXPECT_EQ(warlock->default_weapon, FAMILY_KNIFE)
        << "a mod family's references resolve against the core registries";
    EXPECT_STREQ(warlock->death_message, "WARLOCK UNDONE");

    const WeaponFamilyDescriptor* hexbolt =
        get_weapon_family_descriptor(weapon_id);
    ASSERT_NE(hexbolt, nullptr);
    EXPECT_STREQ(hexbolt->name, "HEXBOLT");
    EXPECT_EQ(hexbolt->fire_sound, 42);

    const EffectFamilyDescriptor* hexburst =
        get_effect_family_descriptor(effect_id);
    ASSERT_NE(hexburst, nullptr);
    EXPECT_STREQ(hexburst->name, "HEXBURST");
    EXPECT_TRUE(hexburst->loops_animation);

    // (c) exactly one slot per order changed; every id in between — and
    // every id above — still answers nullptr.
    const std::vector<bool> living_after =
        populated_map(get_family_descriptor);
    const std::vector<bool> weapon_after =
        populated_map(get_weapon_family_descriptor);
    const std::vector<bool> effect_after =
        populated_map(get_effect_family_descriptor);
    for (int id = 0; id < NUM_FAMILY_SLOTS; id++) {
        const auto i = static_cast<std::size_t>(id);
        EXPECT_EQ(living_after[i], living_before[i] || id == living_id)
            << "living slot " << id;
        EXPECT_EQ(weapon_after[i], weapon_before[i] || id == weapon_id)
            << "weapon slot " << id;
        EXPECT_EQ(effect_after[i], effect_before[i] || id == effect_id)
            << "effect slot " << id;
    }
    // Spelled out for the specific holes the scans have to survive.
    EXPECT_EQ(get_weapon_family_descriptor(weapon_id - 1), nullptr)
        << "weapon slot 20 is free and must stay invisible";
    for (int id = 13; id < effect_id; id++)
        EXPECT_EQ(get_effect_family_descriptor(id), nullptr)
            << "effect slot " << id << " was never populated";
}

// An explicitly pinned mod id far above the pins: the id scans must walk
// the whole byte range, not stop at the first free slot.
TEST(ClasspackInstall, pinned_mod_id_resolves_across_a_gap)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:lich";
    e.wire_id = "40";  // deliberate gap: 21..39 stay free
    e.name = "LICH";
    e.costs = hire_only(900);
    data.living.push_back(std::move(e));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* lich = get_family_descriptor(40);
    ASSERT_NE(lich, nullptr);
    EXPECT_STREQ(lich->name, "LICH");
    EXPECT_EQ(lich->family_id, 40);
    EXPECT_EQ(lich->hiring_cost, 900);

    for (int id = NUM_FAMILIES; id < 40; id++)
        EXPECT_EQ(get_family_descriptor(id), nullptr) << "gap slot " << id;

    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "mod:lich"),
              40)
        << "the name scan must walk past the 21..39 gap";
    // A gap slot has no positional escape.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:#30"),
              -1)
        << "an unpopulated id must not become resolvable";
}

// The top of the byte range is a usable slot; anything outside it is not.
TEST(ClasspackInstall, wire_ids_outside_the_byte_range_are_skipped)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    for (const char* bad : {"256", "-1", "banana", "12x"}) {
        og::data::ClasspackLivingEntry e;
        e.id = std::string("mod:bad_") + bad;
        e.wire_id = bad;
        e.costs = hire_only(1);
        data.living.push_back(std::move(e));
    }
    {
        og::data::ClasspackLivingEntry e;
        e.id = "mod:edge";
        e.wire_id = "255";  // the last slot a family byte can name
        e.name = "EDGE";
        data.living.push_back(std::move(e));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1)
        << "only the in-range pin installs";

    const FamilyDescriptor* edge =
        get_family_descriptor(NUM_FAMILY_SLOTS - 1);
    ASSERT_NE(edge, nullptr);
    EXPECT_STREQ(edge->name, "EDGE");
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "mod:edge"),
              NUM_FAMILY_SLOTS - 1);
}

// reset_all_registry_mod_slots() is what makes a re-install mirror exactly
// the packs mounted now: an unmounted pack must leave no family behind,
// and the core pins must survive untouched.
TEST(ClasspackInstall, reset_mod_slots_frees_pack_families_and_keeps_core)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:transient";
    e.wire_id = "auto";
    e.name = "TRANSIENT";
    data.living.push_back(std::move(e));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    ASSERT_NE(get_family_descriptor(NUM_FAMILIES), nullptr);

    reset_all_registry_mod_slots();

    EXPECT_EQ(get_family_descriptor(NUM_FAMILIES), nullptr)
        << "the pack family is gone";
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "mod:transient"),
              -1)
        << "and is no longer resolvable by string id";
    for (int id = 0; id < NUM_FAMILIES; id++)
        EXPECT_NE(get_family_descriptor(id), nullptr)
            << "core pin " << id << " must survive the reset";
    EXPECT_STREQ(get_family_descriptor(FAMILY_SOLDIER)->name, "SOLDIER");
}

// ---------------------------------------------------------------------------
// Pack namespaces are a real scope
// ---------------------------------------------------------------------------

namespace {

// One living entry, spelled out because these tests care about exactly two
// fields: the declared id and the display name.
og::data::ClasspackData one_living(const char* pack, const char* id,
                                   const char* wire_id, const char* name,
                                   std::int32_t hiring_cost)
{
    og::data::ClasspackData data;
    data.pack = pack;
    og::data::ClasspackLivingEntry e;
    e.id = id;
    e.wire_id = wire_id;
    e.name = name;
    e.costs = hire_only(hiring_cost);
    data.living.push_back(std::move(e));
    return data;
}

// Restores every populated slot of all five registries verbatim. Same
// reason as CorePinGuard below, for a test that installs a whole pack over
// the pins.
class RegistrySnapshotGuard {
public:
    RegistrySnapshotGuard()
        : living_(grab<FamilyDescriptor>(get_family_descriptor)),
          weapons_(grab<WeaponFamilyDescriptor>(get_weapon_family_descriptor)),
          effects_(grab<EffectFamilyDescriptor>(get_effect_family_descriptor)),
          treasures_(
              grab<TreasureFamilyDescriptor>(get_treasure_family_descriptor)),
          generators_(grab<GeneratorFamilyDescriptor>(
              get_generator_family_descriptor))
    {
    }

    ~RegistrySnapshotGuard()
    {
        put(living_, set_family_descriptor);
        put(weapons_, set_weapon_family_descriptor);
        put(effects_, set_effect_family_descriptor);
        put(treasures_, set_treasure_family_descriptor);
        put(generators_, set_generator_family_descriptor);
    }

    RegistrySnapshotGuard(const RegistrySnapshotGuard&) = delete;
    RegistrySnapshotGuard& operator=(const RegistrySnapshotGuard&) = delete;

private:
    template <typename Desc, typename Get>
    static std::vector<std::pair<int, Desc>> grab(Get get)
    {
        std::vector<std::pair<int, Desc>> out;
        for (int id = 0; id < NUM_FAMILY_SLOTS; id++)
            if (const Desc* d = get(id))
                out.emplace_back(id, *d);
        return out;
    }

    template <typename Desc, typename Set>
    static void put(const std::vector<std::pair<int, Desc>>& saved, Set set)
    {
        for (const auto& entry : saved)
            (void)set(entry.first, entry.second);
    }

    std::vector<std::pair<int, FamilyDescriptor>> living_;
    std::vector<std::pair<int, WeaponFamilyDescriptor>> weapons_;
    std::vector<std::pair<int, EffectFamilyDescriptor>> effects_;
    std::vector<std::pair<int, TreasureFamilyDescriptor>> treasures_;
    std::vector<std::pair<int, GeneratorFamilyDescriptor>> generators_;
};

// Restores one core pin verbatim. The mod-slot reset deliberately leaves
// the pins alone, so a test that installs over one has to put it back or
// every later test (in any --gtest_shuffle order) inherits the edit.
class CorePinGuard {
public:
    explicit CorePinGuard(int family_id)
        : family_id_(family_id), saved_(*get_family_descriptor(family_id))
    {
    }
    ~CorePinGuard() { set_family_descriptor(family_id_, saved_); }

    CorePinGuard(const CorePinGuard&) = delete;
    CorePinGuard& operator=(const CorePinGuard&) = delete;

private:
    int family_id_;
    FamilyDescriptor saved_;
};

}  // namespace

// THE pluggability case: two independent packs that each ship a "WARLOCK".
// The fully-qualified declared id keeps each one separately addressable.
TEST(FamilyStringIds, two_packs_may_ship_the_same_family_name)
{
    ModSlotGuard guard;

    // Explicit wire ids: each install_classpack_data call starts its own
    // auto counter (a real install pass shares one across all packs), so
    // two separate calls would both claim the first auto slot.
    ASSERT_EQ(og::resources::install_classpack_data(
                  one_living("alpha", "alpha:warlock", "30", "WARLOCK", 111)),
              1);
    ASSERT_EQ(og::resources::install_classpack_data(
                  one_living("beta", "beta:warlock", "31", "WARLOCK", 222)),
              1);

    const int alpha =
        og::families::resolve_family_string_id(Order::Living, "alpha:warlock");
    const int beta =
        og::families::resolve_family_string_id(Order::Living, "beta:warlock");
    ASSERT_EQ(alpha, 30);
    ASSERT_EQ(beta, 31);
    ASSERT_NE(alpha, beta) << "the namespace is what tells them apart";

    // Each id reaches its OWN descriptor, not merely a valid one.
    ASSERT_NE(get_family_descriptor(alpha), nullptr);
    ASSERT_NE(get_family_descriptor(beta), nullptr);
    EXPECT_EQ(get_family_descriptor(alpha)->hiring_cost, 111);
    EXPECT_EQ(get_family_descriptor(beta)->hiring_cost, 222);
    EXPECT_STREQ(get_family_descriptor(alpha)->declared_id, "alpha:warlock");
    EXPECT_STREQ(get_family_descriptor(beta)->declared_id, "beta:warlock");

    // Case and spaces normalize on both sides, as for bare names.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "Alpha:Warlock"),
              alpha);

    // The documented fallbacks: a bare name, or a namespace nobody
    // declared, still resolve — to the lowest matching byte.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "warlock"),
              alpha)
        << "bare names keep working; first match wins";
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "gamma:warlock"),
              alpha)
        << "an undeclared namespace falls back to the bare-name match";
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "alpha:no_such"),
              -1)
        << "a namespace that exists does not invent families";
}

// With the real core pack installed, EVERY id it declares — all 71 across
// the five orders, positional escapes included — must resolve to the byte
// the pack pinned. This is what every `og.family_id(order, "core:...")` in
// packs/core/families/*.lua depends on.
TEST(FamilyStringIds, every_committed_core_pack_id_resolves_to_its_wire_id)
{
    ModSlotGuard mods;
    RegistrySnapshotGuard pins;

    ClasspackData data;
    load_committed_core_pack(data);
    if (::testing::Test::HasFatalFailure())
        return;

    // (order, declared id, pinned wire id) for every entry, kept before the
    // install moves the parsed data into the process-lifetime store.
    std::vector<std::tuple<Order, std::string, int>> expected;
    const auto collect = [&expected](Order order, const auto& entries) {
        for (const auto& e : entries)
            expected.emplace_back(order, e.id, std::stoi(e.wire_id));
    };
    collect(Order::Living, data.living);
    collect(Order::Weapon, data.weapons);
    collect(Order::FX, data.effects);
    collect(Order::Treasure, data.treasures);
    collect(Order::Generator, data.generators);
    ASSERT_EQ(expected.size(), 71u) << "the whole core pack";

    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 71);

    for (const auto& [order, id, wire] : expected) {
        EXPECT_EQ(og::families::resolve_family_string_id(order, id.c_str()),
                  wire)
            << id << " must resolve to its pinned byte";
    }
    EXPECT_STREQ(get_family_descriptor(FAMILY_SOLDIER)->declared_id,
                 "core:soldier");
    // The escapes the core pack ships for its own name collisions.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "core:#19"),
              FAMILY_GIANT_SKELETON);
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "beast"),
              FAMILY_GOLEM)
        << "the shared display name still lands on the lowest byte";
}

// A mod family may reuse a CORE display name without shadowing the core
// family: "core:soldier" keeps its byte, the mod answers to its own id.
TEST(FamilyStringIds, a_mod_family_reusing_a_core_name_does_not_shadow_it)
{
    ModSlotGuard guard;

    ASSERT_EQ(og::resources::install_classpack_data(one_living(
                  "mod", "mod:soldier", "30", "SOLDIER", 777)),
              1);

    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:soldier"),
              FAMILY_SOLDIER);
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "mod:soldier"),
              30);
    EXPECT_EQ(get_family_descriptor(FAMILY_SOLDIER)->hiring_cost, 250)
        << "the core soldier is untouched";
    EXPECT_EQ(get_family_descriptor(30)->hiring_cost, 777);

    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "soldier"),
              FAMILY_SOLDIER)
        << "the bare name still means the core family (lowest byte)";
}

// Two slots that declare the SAME id are ambiguous by name, but each remains
// addressable through the positional escape.
TEST(FamilyStringIds, duplicate_declared_ids_keep_positional_resolution)
{
    ModSlotGuard guard;

    og::data::ClasspackData data =
        one_living("dup", "dup:twin", "30", "FIRST TWIN", 1);
    {
        og::data::ClasspackLivingEntry e;
        e.id = "dup:twin";  // same declared id, different slot
        e.wire_id = "31";
        e.name = "SECOND TWIN";
        e.costs = hire_only(2);
        data.living.push_back(std::move(e));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 2);

    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "dup:#30"),
              30);
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "dup:#31"),
              31);
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living, "dup:twin"),
              30)
        << "the ambiguous id itself resolves to the lowest byte";
    // Their display names differ, so the bare-name path still separates them.
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "second_twin"),
              31);
}

// Overriding a stock family in place — the supported reskin. Pin the core
// family's wire_id, KEEP its id, and every reference to "core:soldier"
// (Lua hook registrations included) keeps landing on the same byte.
TEST(ClasspackInstall, a_pack_may_override_a_core_family_in_place)
{
    ModSlotGuard guard;
    CorePinGuard pin(FAMILY_SOLDIER);

    ASSERT_EQ(og::resources::install_classpack_data(one_living(
                  "mod", "core:soldier", "0", "SOLDIER", 1234)),
              1);

    const FamilyDescriptor* soldier = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(soldier, nullptr);
    EXPECT_EQ(soldier->hiring_cost, 1234) << "the pack's data won";
    EXPECT_STREQ(soldier->declared_id, "core:soldier");
    EXPECT_EQ(og::families::resolve_family_string_id(Order::Living,
                                                     "core:soldier"),
              FAMILY_SOLDIER)
        << "the id an overriding pack keeps is the id that still resolves";
}

// ---------------------------------------------------------------------------
// Presentation fields (glyph + radar + editor label)
// ---------------------------------------------------------------------------

// The committed core pack must carry an exact transcription of the UI
// switch tables, so the sweep that deletes them changes no pixel.
TEST(CommittedCorePack, carries_ui_presentation)
{
    ClasspackData data;
    load_committed_core_pack(data);
    if (::testing::Test::HasFatalFailure())
        return;

    // Every core family of every order declares the whole block: the sweep
    // agent may read the descriptor unconditionally.
    auto complete = [](const og::data::ClasspackPresentation& p) {
        return p.glyph && p.glyph_ascii && p.glyph_color && p.glyph_bold &&
               p.glyph_transparent && p.radar_color && p.radar_jitter;
    };
    for (const auto& e : data.living)
        EXPECT_TRUE(complete(e.presentation)) << e.id;
    for (const auto& e : data.weapons)
        EXPECT_TRUE(complete(e.presentation)) << e.id;
    for (const auto& e : data.effects)
        EXPECT_TRUE(complete(e.presentation)) << e.id;
    for (const auto& e : data.treasures)
        EXPECT_TRUE(complete(e.presentation)) << e.id;
    for (const auto& e : data.generators) {
        EXPECT_TRUE(complete(e.presentation)) << e.id;
        EXPECT_TRUE(e.editor_label.has_value()) << e.id;
    }

    // Spot pins against the legacy switch bodies.
    EXPECT_EQ(data.living[FAMILY_SOLDIER].presentation.glyph.value_or(""),
              "S");
    EXPECT_EQ(data.living[FAMILY_TOWER1].presentation.glyph.value_or(""), "Y");
    EXPECT_EQ(data.weapons[FAMILY_TREE].presentation.glyph.value_or(""),
              "\xe2\x99\xa3");
    EXPECT_EQ(data.weapons[FAMILY_TREE].presentation.glyph_ascii.value_or(""),
              "&");
    EXPECT_EQ(data.weapons[FAMILY_TREE].presentation.glyph_color.value_or(""),
              "green");
    EXPECT_EQ(data.treasures[FAMILY_STAIN]
                  .presentation.glyph_transparent.value_or(false),
              true);
    EXPECT_EQ(data.treasures[FAMILY_GOLD_BAR].presentation.radar_color.value_or(0),
              88);
    EXPECT_EQ(
        data.treasures[FAMILY_GOLD_BAR].presentation.radar_jitter.value_or(0),
        5);
    EXPECT_EQ(
        data.treasures[FAMILY_SILVER_BAR].presentation.radar_color.value_or(0),
        23);
    EXPECT_EQ(
        data.treasures[FAMILY_DRUMSTICK].presentation.radar_color.value_or(0),
        136);
    EXPECT_EQ(
        data.treasures[FAMILY_DRUMSTICK].presentation.radar_jitter.value_or(0),
        2);
    EXPECT_EQ(data.treasures[FAMILY_EXIT].presentation.radar_color.value_or(0),
              120);
    EXPECT_EQ(data.treasures[FAMILY_EXIT].presentation.radar_jitter.value_or(0),
              7);
    // SPEED_POTION is deliberately absent from the radar potion case list.
    EXPECT_EQ(
        data.treasures[FAMILY_SPEED_POTION].presentation.radar_color.value_or(0),
        og::kRadarColorNone);
    EXPECT_EQ(data.generators[FAMILY_TOWER].editor_label.value_or(""),
              "MAGE TOWER");
    EXPECT_EQ(data.generators[FAMILY_BONES].editor_label.value_or(""),
              "BONEPILE");
}

TEST(ClasspackInstall, presentation_lands_on_every_order)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    {
        og::data::ClasspackLivingEntry e;
        e.id = "mod:wisp";
        e.wire_id = "auto";
        e.presentation.glyph = "\xe2\x86\xaf";  // U+21AF
        e.presentation.glyph_ascii = "/";
        e.presentation.glyph_color = "team";
        e.presentation.glyph_bold = true;
        e.presentation.radar_color = og::kRadarColorTeam;
        e.presentation.radar_jitter = 7;
        data.living.push_back(std::move(e));
    }
    {
        og::data::ClasspackGeneratorEntry g;
        g.id = "mod:hut";
        g.wire_id = "auto";
        g.editor_label = "HUT";
        g.presentation.glyph = "H";
        g.presentation.glyph_transparent = true;
        data.generators.push_back(std::move(g));
    }
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 2);

    const FamilyDescriptor* living = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(living, nullptr);
    EXPECT_EQ(living->glyph.codepoint, U'↯');
    EXPECT_EQ(living->glyph.ascii, '/');
    EXPECT_EQ(living->glyph.color, og::GlyphColor::Team);
    EXPECT_TRUE(living->glyph.bold);
    EXPECT_FALSE(living->glyph.transparent) << "undeclared keeps the default";
    EXPECT_EQ(living->radar.color, og::kRadarColorTeam);
    EXPECT_EQ(living->radar.jitter, 7);

    const GeneratorFamilyDescriptor* gen =
        get_generator_family_descriptor(4);  // 4 core generators, auto = 21
    (void)gen;
    const GeneratorFamilyDescriptor* installed =
        get_generator_family_descriptor(21);
    ASSERT_NE(installed, nullptr);
    EXPECT_STREQ(installed->editor_label, "HUT");
    EXPECT_EQ(installed->glyph.codepoint, U'H');
    EXPECT_TRUE(installed->glyph.transparent);
    EXPECT_EQ(installed->glyph.color, og::GlyphColor::Default)
        << "an undeclared colour keeps the generator default";
}

// A mod family that declares no presentation renders exactly like an
// unknown family always did: the order's legacy `default:` branch.
TEST(ClasspackInstall, undeclared_presentation_is_the_legacy_default)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:blob";
    e.wire_id = "auto";
    data.living.push_back(std::move(e));
    og::data::ClasspackWeaponEntry w;
    w.id = "mod:shard";
    w.wire_id = "auto";
    data.weapons.push_back(std::move(w));
    og::data::ClasspackTreasureEntry t;
    t.id = "mod:coin";
    t.wire_id = "auto";
    data.treasures.push_back(std::move(t));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 3);

    EXPECT_EQ(get_family_descriptor(NUM_FAMILIES)->glyph.codepoint, U'?');
    EXPECT_EQ(get_weapon_family_descriptor(21)->glyph.codepoint, U'*');
    EXPECT_EQ(get_weapon_family_descriptor(21)->glyph.color,
              og::GlyphColor::White);
    EXPECT_EQ(get_treasure_family_descriptor(21)->glyph.codepoint, U'$');
    EXPECT_EQ(get_treasure_family_descriptor(21)->glyph.color,
              og::GlyphColor::Yellow);
    EXPECT_EQ(get_treasure_family_descriptor(21)->radar.color,
              og::kRadarColorNone);
}

// A malformed cosmetic value warns and keeps the current setting — it must
// never sink the pack (behaviour data is what a pack really ships).
TEST(ClasspackInstall, malformed_presentation_keeps_the_current_value)
{
    ModSlotGuard guard;

    og::data::ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:garbled";
    e.wire_id = "auto";
    e.presentation.glyph = "ab";           // two characters
    e.presentation.glyph_ascii = "";       // zero bytes
    e.presentation.glyph_color = "puce";   // not in the vocabulary
    e.presentation.radar_jitter = -3;      // negative span
    data.living.push_back(std::move(e));
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* d = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->glyph.codepoint, U'?');
    EXPECT_EQ(d->glyph.ascii, '?');
    EXPECT_EQ(d->glyph.color, og::GlyphColor::Default);
    EXPECT_EQ(d->radar.jitter, 0);
}

// ---------------------------------------------------------------------------
// anims: pack-defined animation tables
// ---------------------------------------------------------------------------

// The headline Feature-2 case: a pack-defined set survives parse → install
// → descriptor, with the row pointers, the -1 sentinels and — critically —
// the explicit row count that bounds walker::ani_count.
TEST(ClasspackInstall, pack_animation_set_reaches_the_descriptor)
{
    ModSlotGuard guard;

    ClasspackData data;
    declare_or_die(
        "og.pack{ id = 'mod' }\n"
        "og.anims('wisp_walk', { rows = 16,\n"
        "                        frames = { {0, 1, 2}, false } })\n"
        "og.family('living', { id = 'mod:wisp', wire_id = 'auto',\n"
        "                      animation = 'wisp_walk',\n"
        "                      sprite = 'packs/mod/sprites/wisp.png' })\n",
        data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* d = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(d, nullptr);
    ASSERT_NE(d->anim_table, nullptr);
    EXPECT_EQ(d->anim_row_count, 16)
        << "the row count must be explicit: gloader's anim_table_count() "
           "registry lookup answers 0 for a pack table, and ani_count == 0 "
           "disables the animate() bounds checks";

    // Row 0 is the declared frame list plus the -1 end sentinel; row 1 is
    // the explicit null row; the remaining rows repeat cyclically.
    ASSERT_NE(d->anim_table[0], nullptr);
    EXPECT_EQ(d->anim_table[0][0], 0);
    EXPECT_EQ(d->anim_table[0][1], 1);
    EXPECT_EQ(d->anim_table[0][2], 2);
    EXPECT_EQ(d->anim_table[0][3], -1);
    EXPECT_EQ(d->anim_table[1], nullptr);
    EXPECT_EQ(d->anim_table[2], d->anim_table[0]);
    EXPECT_EQ(d->anim_table[15], d->anim_table[1]);

    // The pack-relative sprite path is passed through untouched:
    // read_pixie_file tries "pix/<name>" first and then the raw name.
    ASSERT_NE(d->pix_filename, nullptr);
    EXPECT_STREQ(d->pix_filename, "packs/mod/sprites/wisp.png");
}

// Built-in animation names stay reserved and keep working; naming one
// clears any pack table the slot was carrying.
TEST(ClasspackInstall, builtin_animation_names_still_win)
{
    ModSlotGuard guard;

    ClasspackData first;
    declare_or_die(
        "og.anims('custom', { frames = { {0} } })\n"
        "og.family('living', { id = 'mod:shifter', wire_id = 'auto',\n"
        "                      animation = 'custom' })\n",
        first);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(first)), 1);
    ASSERT_NE(get_family_descriptor(NUM_FAMILIES)->anim_table, nullptr);

    ClasspackData second;
    declare_or_die(
        "og.family('living', { id = 'mod:shifter', wire_id = 21,\n"
        "                      animation = 'skeleton' })\n",
        second);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(second)), 1);

    const FamilyDescriptor* d = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->animation_type, FamilyAnimationType::FAMILY_ANIM_SKELETON);
    EXPECT_EQ(d->anim_table, nullptr)
        << "a built-in name returns the family to the gloader table";
    EXPECT_EQ(d->anim_row_count, 0);
}

// A set the installer rejects leaves the family's animation alone rather
// than planting a table with a bogus row count.
TEST(ClasspackInstall, rejected_animation_sets_leave_the_family_alone)
{
    ModSlotGuard guard;

    ClasspackData data;
    declare_or_die(
        // rows < the declared row count
        "og.anims('too_short', { rows = 1, frames = { {0}, {1} } })\n"
        // a frame index outside 0..127
        "og.anims('bad_frame', { frames = { {999} } })\n"
        "og.anims('empty_row',  { frames = { {} } })\n"
        "og.family('living', { id = 'mod:a', wire_id = 21,\n"
        "                      animation = 'too_short' })\n"
        "og.family('living', { id = 'mod:b', wire_id = 22,\n"
        "                      animation = 'bad_frame' })\n"
        "og.family('living', { id = 'mod:c', wire_id = 23,\n"
        "                      animation = 'empty_row' })\n"
        "og.family('living', { id = 'mod:d', wire_id = 24,\n"
        "                      animation = 'nope' })\n",
        data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 4);

    for (int id = 21; id <= 24; id++) {
        const FamilyDescriptor* d = get_family_descriptor(id);
        ASSERT_NE(d, nullptr) << id;
        EXPECT_EQ(d->anim_table, nullptr) << id;
        EXPECT_EQ(d->anim_row_count, 0) << id;
    }
}

// The remaining set-level rejections: no frames at all, a row count past
// the cap, an over-long row, and a duplicate set name (first wins).
TEST(ClasspackInstall, animation_set_bounds_are_enforced)
{
    ModSlotGuard guard;

    std::string lua =
        "og.anims('no_frames', { rows = 4, frames = {} })\n"
        "og.anims('too_many_rows', { rows = 257, frames = { {0} } })\n"
        "og.anims('long_row', { frames = { {";
    for (int i = 0; i < 256; i++)
        lua += (i > 0 ? ", 1" : "1");
    lua +=
        "} } })\n"
        "og.anims('twice', { frames = { {1} } })\n"
        "og.anims('twice', { frames = { {2} } })\n"
        "og.family('living', { id = 'mod:a', wire_id = 21,\n"
        "                      animation = 'no_frames' })\n"
        "og.family('living', { id = 'mod:b', wire_id = 22,\n"
        "                      animation = 'too_many_rows' })\n"
        "og.family('living', { id = 'mod:c', wire_id = 23,\n"
        "                      animation = 'long_row' })\n"
        "og.family('living', { id = 'mod:d', wire_id = 24,\n"
        "                      animation = 'twice' })\n";
    ClasspackData data;
    declare_or_die(lua, data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 4);

    for (int id = 21; id <= 23; id++) {
        const FamilyDescriptor* d = get_family_descriptor(id);
        ASSERT_NE(d, nullptr) << id;
        EXPECT_EQ(d->anim_table, nullptr) << id;
    }
    // The duplicate name keeps the FIRST definition.
    const FamilyDescriptor* dup = get_family_descriptor(24);
    ASSERT_NE(dup, nullptr);
    ASSERT_NE(dup->anim_table, nullptr);
    EXPECT_EQ(dup->anim_table[0][0], 1);
    EXPECT_EQ(dup->anim_row_count, 1);
}

// A non-living family naming a set that does not exist keeps its table.
TEST(ClasspackInstall, unknown_animation_name_on_a_weapon_keeps_the_table)
{
    ModSlotGuard guard;

    ClasspackData data;
    declare_or_die(
        "og.family('weapon', { id = 'mod:shard', wire_id = 'auto',\n"
        "                      animation = 'nope' })\n",
        data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
    ASSERT_NE(get_weapon_family_descriptor(21), nullptr);
    EXPECT_EQ(get_weapon_family_descriptor(21)->anim_table, nullptr);
    EXPECT_EQ(get_weapon_family_descriptor(21)->anim_row_count, 0);
}

// Every non-living order can carry a pack table too, and the row count
// travels with it.
TEST(ClasspackInstall, pack_animation_sets_reach_the_other_orders)
{
    ModSlotGuard guard;

    ClasspackData data;
    declare_or_die(
        "og.anims('spin', { rows = 24, frames = { {3, 4} } })\n"
        "og.family('weapon', { id = 'mod:shard', wire_id = 'auto',\n"
        "                      animation = 'spin',\n"
        "                      sprite = 'packs/mod/sprites/shard.png' })\n"
        "og.family('effect', { id = 'mod:spark', wire_id = 'auto',\n"
        "                      animation = 'spin' })\n"
        "og.family('treasure', { id = 'mod:relic', wire_id = 'auto',\n"
        "                        animation = 'spin' })\n"
        "og.family('generator', { id = 'mod:hut', wire_id = 'auto',\n"
        "                         animation = 'spin' })\n",
        data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 4);

    const WeaponFamilyDescriptor* w = get_weapon_family_descriptor(21);
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->anim_table, nullptr);
    EXPECT_EQ(w->anim_row_count, 24);
    EXPECT_EQ(w->anim_table[0][0], 3);
    EXPECT_EQ(w->anim_table[0][2], -1);
    EXPECT_STREQ(w->pix_filename, "packs/mod/sprites/shard.png");

    ASSERT_NE(get_effect_family_descriptor(21), nullptr);
    EXPECT_EQ(get_effect_family_descriptor(21)->anim_row_count, 24);
    ASSERT_NE(get_treasure_family_descriptor(21), nullptr);
    EXPECT_EQ(get_treasure_family_descriptor(21)->anim_row_count, 24);
    ASSERT_NE(get_generator_family_descriptor(21), nullptr);
    EXPECT_EQ(get_generator_family_descriptor(21)->anim_row_count, 24);
    // All four share the one materialized table.
    EXPECT_EQ(get_effect_family_descriptor(21)->anim_table, w->anim_table);
}

// Animation sets are pack-local: pack B cannot name pack A's set.
TEST(ClasspackInstall, animation_sets_do_not_leak_between_packs)
{
    ModSlotGuard guard;

    ClasspackData a;
    declare_or_die("og.anims('shared', { frames = { {0} } })\n", a, "packa");
    ASSERT_EQ(og::resources::install_classpack_data(std::move(a)), 0);

    ClasspackData b;
    declare_or_die(
        "og.family('living', { id = 'modb:thief', wire_id = 'auto',\n"
        "                      animation = 'shared' })\n",
        b, "packb");
    ASSERT_EQ(og::resources::install_classpack_data(std::move(b)), 1);

    const FamilyDescriptor* d = get_family_descriptor(NUM_FAMILIES);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->anim_table, nullptr);
    EXPECT_EQ(d->anim_row_count, 0);
}

// ---------------------------------------------------------------------------
// UTF-8 helpers behind the `glyph:` scalar
// ---------------------------------------------------------------------------

TEST(FamilyPresentation, glyph_utf8_round_trips)
{
    for (const char32_t cp : {U'S', U'$', U' ', U'é', U'♣', U'↯',
                              U'✺', U'◍', U'\U0001F600'}) {
        const std::string encoded = og::glyph_to_utf8(cp);
        ASSERT_FALSE(encoded.empty()) << static_cast<std::uint32_t>(cp);
        char32_t back = U'\0';
        ASSERT_TRUE(og::glyph_from_utf8(encoded, back))
            << static_cast<std::uint32_t>(cp);
        EXPECT_EQ(back, cp);
    }

    char32_t out = U'\0';
    EXPECT_FALSE(og::glyph_from_utf8("", out));
    EXPECT_FALSE(og::glyph_from_utf8("ab", out)) << "two characters";
    EXPECT_FALSE(og::glyph_from_utf8("\xC0\x80", out)) << "overlong NUL";
    EXPECT_FALSE(og::glyph_from_utf8("\xE2\x99", out)) << "truncated";
    EXPECT_FALSE(og::glyph_from_utf8("\x80", out)) << "lone continuation";
    EXPECT_FALSE(og::glyph_from_utf8("\xED\xA0\x80", out)) << "surrogate";
    EXPECT_TRUE(og::glyph_to_utf8(static_cast<char32_t>(0x110000u)).empty());

    // The colour vocabulary round-trips both ways.
    for (std::size_t i = 0; i < og::kGlyphColorNames.size(); i++) {
        og::GlyphColor c{};
        ASSERT_TRUE(og::glyph_color_from_name(og::kGlyphColorNames[i], c));
        EXPECT_EQ(static_cast<std::size_t>(c), i);
        EXPECT_STREQ(og::glyph_color_name(c), og::kGlyphColorNames[i]);
    }
    og::GlyphColor unknown{};
    EXPECT_FALSE(og::glyph_color_from_name("puce", unknown));
}

// ---------------------------------------------------------------------------
// tuning: maps (read by scripts via og.tuning)
// ---------------------------------------------------------------------------

namespace {

using og::data::ClasspackTuningValue;

// The tuning store is process-global (like the registries); every test
// that touches it restores emptiness on both sides of itself.
class TuningStoreGuard {
public:
    TuningStoreGuard() { og::script::clear_all_family_tuning(); }
    ~TuningStoreGuard() { og::script::clear_all_family_tuning(); }
};

}  // namespace

// ---------------------------------------------------------------------------
// tuning install — declared data lands in the gameplay-side store
// ---------------------------------------------------------------------------

// install_classpack_data carries each entry's tuning into
// og::script::set_family_tuning under the entry's (order, wire id), with
// every value kind converted; an entry that declares nothing installs
// nothing.
TEST(ClasspackInstall, tuning_reaches_the_gameplay_store)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;

    ClasspackData data;
    declare_or_die(
        "og.pack{ id = 'mod' }\n"
        "og.family('living', { id = 'mod:tuned', wire_id = 'auto',\n"
        "                      name = 'TUNED',\n"
        "                      tuning = { stun = 7, scale = 1.5,\n"
        "                                 brave = true, tag = 'knife' } })\n"
        "og.family('living', { id = 'mod:plain', wire_id = 'auto',\n"
        "                      name = 'PLAIN' })\n"
        "og.family('weapon', { id = 'mod:zap', wire_id = 'auto',\n"
        "                      name = 'ZAP', tuning = { speed = 9 } })\n",
        data);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 3);

    const int tuned_id =
        og::families::resolve_family_string_id(Order::Living, "mod:tuned");
    const int plain_id =
        og::families::resolve_family_string_id(Order::Living, "mod:plain");
    const int zap_id =
        og::families::resolve_family_string_id(Order::Weapon, "mod:zap");
    ASSERT_GE(tuned_id, 0);
    ASSERT_GE(plain_id, 0);
    ASSERT_GE(zap_id, 0);

    // Key order: sorted, not as typed — a Lua table has no source order,
    // so the harvest imposes one (brave, scale, stun, tag).
    const og::script::TuningMap* tuned =
        og::script::family_tuning(Order::Living, tuned_id);
    ASSERT_NE(tuned, nullptr);
    ASSERT_EQ(tuned->size(), 4u);
    EXPECT_EQ((*tuned)[0].key, "brave");
    ASSERT_EQ((*tuned)[0].value.kind,
              og::script::TuningValue::Kind::Boolean);
    EXPECT_TRUE((*tuned)[0].value.boolean);
    EXPECT_EQ((*tuned)[1].key, "scale");
    ASSERT_EQ((*tuned)[1].value.kind, og::script::TuningValue::Kind::Number);
    EXPECT_EQ((*tuned)[1].value.number, 1.5);
    EXPECT_EQ((*tuned)[2].key, "stun");
    ASSERT_EQ((*tuned)[2].value.kind,
              og::script::TuningValue::Kind::Integer);
    EXPECT_EQ((*tuned)[2].value.integer, 7);
    EXPECT_EQ((*tuned)[3].key, "tag");
    ASSERT_EQ((*tuned)[3].value.kind, og::script::TuningValue::Kind::String);
    EXPECT_EQ((*tuned)[3].value.string, "knife");

    EXPECT_EQ(og::script::family_tuning(Order::Living, plain_id), nullptr)
        << "an entry without tuning installs nothing";

    const og::script::TuningMap* zap =
        og::script::family_tuning(Order::Weapon, zap_id);
    ASSERT_NE(zap, nullptr);
    ASSERT_EQ(zap->size(), 1u);
    EXPECT_EQ((*zap)[0].key, "speed");
    EXPECT_EQ((*zap)[0].value.integer, 9);
}

// Tuning belongs to whoever occupies the slot NOW: a reinstall of the same
// wire id without tuning erases the previous occupant's map (the
// set_family_tuning empty-map contract the installer relies on between
// full clear_all passes).
TEST(ClasspackInstall, reinstalling_a_slot_without_tuning_clears_it)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;

    ClasspackData first;
    declare_or_die(
        "og.pack{ id = 'mod' }\n"
        "og.family('living', { id = 'mod:v1', wire_id = 255,\n"
        "                      name = 'V1', tuning = { cap = 420 } })\n",
        first);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(first)), 1);
    ASSERT_NE(og::script::family_tuning(Order::Living, 255), nullptr);

    ClasspackData second;
    declare_or_die(
        "og.pack{ id = 'mod' }\n"
        "og.family('living', { id = 'mod:v2', wire_id = 255,\n"
        "                      name = 'V2' })\n",
        second);
    ASSERT_EQ(og::resources::install_classpack_data(std::move(second)), 1);
    EXPECT_EQ(og::script::family_tuning(Order::Living, 255), nullptr)
        << "the replacing entry declared no tuning, so the slot is bare";
}

// ---------------------------------------------------------------------------
// lib/ end to end — the REAL enumeration path (packs.cpp), mount to unmount
// ---------------------------------------------------------------------------

namespace {

// The three Lua sources this fixture writes to disk. Their exact bytes are
// declared in scripts/coverage/runtime_only_lua.txt (the engine compiles
// them under packs/-prefixed chunk names, so an armed coverage run must
// recognize the digests) — CHANGING A BYTE HERE MEANS UPDATING THAT LEDGER.
constexpr const char* kLibAaa = "return { tag = 'aaa' }\n";
constexpr const char* kLibHelper = "return { bonus = 5 }\n";
constexpr const char* kLibProbeScript =
    "local aaa = og.use('aaa')\n"
    "local helper = og.use('helper')\n"
    "og.log('libpack', aaa.tag, helper.bonus)\n";

std::filesystem::path make_scratch_lib_pack()
{
    std::string templ =
        (std::filesystem::temp_directory_path() / "og_libpack_XXXXXX")
            .string();
    char* made = ::mkdtemp(templ.data());
    if (made == nullptr)
        return {};
    const std::filesystem::path root(made);
    std::error_code ec;
    std::filesystem::create_directories(root / "lib", ec);
    std::filesystem::create_directories(root / "scripts", ec);
    if (ec)
        return {};
    const auto put = [&](const std::filesystem::path& p,
                         const std::string& bytes) {
        std::ofstream out(p, std::ios::binary);
        out << bytes;
    };
    put(root / "lib" / "aaa.lua", kLibAaa);
    put(root / "lib" / "helper.lua", kLibHelper);
    put(root / "lib" / "notes.txt", "not lua; must be ignored\n");
    put(root / "lib" / "empty.lua", "");  // unreadable/empty: warn + skip
    put(root / "scripts" / "probe.lua", kLibProbeScript);
    std::filesystem::create_directories(root / "families", ec);
    put(root / "families" / "tuned.lua",
        "og.pack{ id = 'org.test.libpack' }\n"
        "og.family('living', { id = 'libpack:tuned', wire_id = 'auto',\n"
        "                      name = 'TUNED', tuning = { cap = 11 } })\n");
    return root;
}

}  // namespace

// One mount drives the whole class-pack resources contract: lib/*.lua (and
// only *.lua with content) registers under deterministic packs/<id>/lib/
// chunk names, the pack's script og.use-binds the exports when the shared
// VM rebuilds, the declared tuning reaches the gameplay store, the MP
// transfer manifest carries the lib files (they are pack CONTENT — the
// same walk that ships scripts ships lib), and unmount+refresh removes all
// of it.
TEST(ClasspackLibE2e, mount_registers_loads_transfers_and_unmounts)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;

    const std::filesystem::path root = make_scratch_lib_pack();
    ASSERT_FALSE(root.empty()) << "scratch pack creation failed";
    ASSERT_TRUE(og::resources::mount(root.string().c_str(),
                                     "packs/org.test.libpack", 1));
    og::resources::refresh_pack_scripts();

    // Registration: exactly the two real modules, filename-lexicographic,
    // with the virtual-tree chunk names the loader will report.
    std::vector<const og::script::PackLibModule*> mine;
    for (const og::script::PackLibModule& m : og::script::pack_lib_modules())
    {
        if (m.pack_id == "org.test.libpack")
            mine.push_back(&m);
    }
    ASSERT_EQ(mine.size(), 2u)
        << "notes.txt and empty.lua must not have registered";
    EXPECT_EQ(mine[0]->name, "aaa");
    EXPECT_EQ(mine[0]->chunk_name, "packs/org.test.libpack/lib/aaa.lua");
    EXPECT_EQ(mine[1]->name, "helper");
    EXPECT_EQ(mine[1]->source, kLibHelper);

    // Load: the rebuilt shared VM ran the modules and the pack script.
    const std::vector<std::string>& log =
        og::script::active_world_scripts().host().log();
    bool saw_probe = false;
    for (const std::string& line : log)
        saw_probe = saw_probe || line == "libpack\taaa\t5";
    EXPECT_TRUE(saw_probe)
        << "probe.lua must have read both lib exports through og.use";

    // Tuning: the declaration's `tuning` map reached the gameplay store
    // through the same install pass.
    const int tuned_id = og::families::resolve_family_string_id(
        Order::Living, "libpack:tuned");
    ASSERT_GE(tuned_id, 0);
    const og::script::TuningMap* tuned =
        og::script::family_tuning(Order::Living, tuned_id);
    ASSERT_NE(tuned, nullptr);
    ASSERT_EQ(tuned->size(), 1u);
    EXPECT_EQ((*tuned)[0].key, "cap");
    EXPECT_EQ((*tuned)[0].value.integer, 11);

    // Transfer: the host-side manifest walk ships lib/ (and families/) as
    // ordinary pack content. Tuning rides INSIDE the declaration — it is
    // load-time pack data, never wire state.
    bool found_pack = false;
    for (const og::sim::HostedPack& hp :
         og::resources::build_transferable_packs())
    {
        if (hp.manifest.pack_id != "org.test.libpack")
            continue;
        found_pack = true;
        std::vector<std::string> paths;
        for (const auto& f : hp.manifest.files)
            paths.push_back(f.path);
        EXPECT_NE(paths.end(),
                  std::find(paths.begin(), paths.end(), "lib/aaa.lua"));
        EXPECT_NE(paths.end(),
                  std::find(paths.begin(), paths.end(), "lib/helper.lua"));
        EXPECT_NE(paths.end(),
                  std::find(paths.begin(), paths.end(), "scripts/probe.lua"));
        EXPECT_NE(paths.end(),
                  std::find(paths.begin(), paths.end(),
                            "families/tuned.lua"));
    }
    EXPECT_TRUE(found_pack)
        << "the mounted scratch pack must be transferable";

    // Unmount: modules, scripts and tuning all follow the mount out.
    ASSERT_TRUE(og::resources::unmount(root.string().c_str()));
    og::resources::refresh_pack_scripts();
    for (const og::script::PackLibModule& m : og::script::pack_lib_modules())
        EXPECT_NE(m.pack_id, "org.test.libpack");
    EXPECT_EQ(og::script::family_tuning(Order::Living, tuned_id), nullptr)
        << "an unmounted pack must leave no tuning behind";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// ---------------------------------------------------------------------------
// packs/<id>/families/ — many files, one pack
// ---------------------------------------------------------------------------

namespace {

// A scratch pack whose families/ holds two declaration files in sorted
// order, a non-Lua note and a subdirectory that must both be ignored.
// bb.lua re-declares aa.lua's slot with one field, proving the documented
// precedence: later files overwrite exactly the fields they declare.
std::filesystem::path make_scratch_split_pack(bool second_family,
                                              bool broken_family_file)
{
    std::string templ =
        (std::filesystem::temp_directory_path() / "og_split_XXXXXX")
            .string();
    char* made = ::mkdtemp(templ.data());
    if (made == nullptr)
        return {};
    const std::filesystem::path root(made);
    std::error_code ec;
    std::filesystem::create_directories(root / "families" / "nested", ec);
    if (ec)
        return {};
    const auto put = [&](const std::filesystem::path& p,
                         const std::string& bytes) {
        std::ofstream out(p, std::ios::binary);
        out << bytes;
    };
    put(root / "families" / "aa.lua",
        "og.pack{ id = 'org.test.splitpack' }\n"
        "og.family('living', { id = 'splitpack:alpha', wire_id = 30,\n"
        "                      name = 'ALPHA', costs = { hire = 111 },\n"
        "                      tuning = { split_key = 7 } })\n");
    if (second_family)
        put(root / "families" / "bb.lua",
            broken_family_file
                ? "og.family('living', { id = 'splitpack:alpha',\n"
                  "                      wire_id = 30, nmae = 'BETA' })\n"
                : "og.family('living', { id = 'splitpack:alpha',\n"
                  "                      wire_id = 30,\n"
                  "                      costs = { hire = 222 },\n"
                  "                      tuning = { split_key = 9 } })\n");
    put(root / "families" / "notes.txt", "not lua; must be ignored\n");
    return root;
}

class SplitPackMount {
public:
    explicit SplitPackMount(const std::filesystem::path& root) : root_(root)
    {
        mounted_ = og::resources::mount(root.string().c_str(),
                                        "packs/org.test.splitpack", 1);
        if (mounted_)
            og::resources::refresh_pack_scripts();
    }
    ~SplitPackMount()
    {
        if (mounted_) {
            (void)og::resources::unmount(root_.string().c_str());
            og::resources::refresh_pack_scripts();
        }
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    [[nodiscard]] bool ok() const { return mounted_; }

private:
    std::filesystem::path root_;
    bool mounted_ = false;
};

}  // namespace

// The two families/ files declare into one pack in sorted filename order,
// so bb.lua's sparse re-declaration of aa.lua's WIRE slot overwrites
// exactly the data field it declares (costs.hire) while undeclared fields
// (name) keep aa.lua's values — and the tuning map follows the
// install-always-replaces rule (bb.lua's map wins whole). notes.txt and the
// nested directory are skipped.
TEST(ClasspackSplitLayout, many_files_install_as_one_pack)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;
    const std::filesystem::path root = make_scratch_split_pack(
        /*second_family=*/true, /*broken_family_file=*/false);
    ASSERT_FALSE(root.empty());
    SplitPackMount mount(root);
    ASSERT_TRUE(mount.ok());

    const int alpha_id = og::families::resolve_family_string_id(
        Order::Living, "splitpack:alpha");
    ASSERT_GE(alpha_id, 0) << "families/*.lua entries must install";

    EXPECT_EQ(alpha_id, 30) << "wire_id pins the slot across both files";
    const FamilyDescriptor* alpha = get_family_descriptor(alpha_id);
    ASSERT_NE(alpha, nullptr);
    EXPECT_STREQ(alpha->name, "ALPHA") << "aa.lua's undeclared-in-bb "
                                          "fields must survive";
    EXPECT_EQ(alpha->hiring_cost, 222)
        << "bb.lua loads after aa.lua (sorted) and overwrites the one "
           "field it declares";

    const og::script::TuningMap* tuned =
        og::script::family_tuning(Order::Living, alpha_id);
    ASSERT_NE(tuned, nullptr) << "tuning from a families/ file must reach "
                                 "the gameplay store";
    ASSERT_EQ(tuned->size(), 1u);
    EXPECT_EQ((*tuned)[0].key, "split_key");
    EXPECT_EQ((*tuned)[0].value.integer, 9)
        << "an installed entry always installs its tuning map, so the "
           "later file's map replaces the earlier one whole";
}

// One file is a complete pack.
TEST(ClasspackSplitLayout, a_single_family_file_installs)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;
    const std::filesystem::path root = make_scratch_split_pack(
        /*second_family=*/false, /*broken_family_file=*/false);
    ASSERT_FALSE(root.empty());
    SplitPackMount mount(root);
    ASSERT_TRUE(mount.ok());

    const int alpha_id = og::families::resolve_family_string_id(
        Order::Living, "splitpack:alpha");
    ASSERT_GE(alpha_id, 0);
    EXPECT_EQ(get_family_descriptor(alpha_id)->hiring_cost, 111)
        << "nothing overwrote aa.lua";
}

// One unusable families/ file rejects the WHOLE pack — including the
// entries its well-formed neighbour declared. All or nothing: a pack that
// half-installed would be a game whose families depend on load order.
TEST(ClasspackSplitLayout, bad_family_file_rejects_the_whole_pack)
{
    ModSlotGuard guard;
    TuningStoreGuard tuning_guard;
    const std::filesystem::path root = make_scratch_split_pack(
        /*second_family=*/true, /*broken_family_file=*/true);
    ASSERT_FALSE(root.empty());
    SplitPackMount mount(root);
    ASSERT_TRUE(mount.ok());

    EXPECT_LT(og::families::resolve_family_string_id(Order::Living,
                                                     "splitpack:alpha"),
              0);
    EXPECT_EQ(og::script::family_tuning(Order::Living, 30), nullptr)
        << "no tuning may leak from a rejected pack";
}

// ---------------------------------------------------------------------------
// Hostile data, straight into the installer
// ---------------------------------------------------------------------------
//
// install_classpack_data takes a ClasspackData from anywhere — a
// declaration, or C++ (tools/concept_mapgen builds one directly) — and the
// declaration itself arrives over the network from another player's machine
// (src/gameplay/pack_transfer.cpp). So these are shapes a peer can hand this
// process, asserted against the uniform contract: a bad entry is SKIPPED,
// never obeyed and never fatal, the rest of the pack installs, and nothing
// reads out of bounds or is left half-written.

// ---------------------------------------------------------------------------
// Install: asking for more than the registries have
// ---------------------------------------------------------------------------

// Wire ids are one byte, but the auto counter is not: it runs 21, 22, ...
// per order and simply walks off the end of the 256-slot registry. The
// install-slot lookup is the only thing standing between a pack that
// declares more families than the engine can hold and an out-of-bounds
// registry write, so exercise it on EVERY order — each has its own
// installer with its own copy of the guard.
TEST(ClasspackInstallErrors, an_oversized_pack_stops_at_every_registry_end)
{
    ModSlotGuard guard;

    constexpr int kFirstModId = 21;
    constexpr int kFits = NUM_FAMILY_SLOTS - kFirstModId;
    constexpr int kOverflow = 8;

    ClasspackData data;
    data.pack = "mod";
    for (int i = 0; i < kFits + kOverflow; i++) {
        const std::string suffix = "_" + std::to_string(i);
        {
            og::data::ClasspackLivingEntry e;
            e.id = "mod:bulk_living" + suffix;
            e.costs = hire_only(1);
            data.living.push_back(std::move(e));
        }
        {
            og::data::ClasspackWeaponEntry e;
            e.id = "mod:bulk_weapon" + suffix;
            e.init_lifetime = 1;
            data.weapons.push_back(std::move(e));
        }
        {
            og::data::ClasspackEffectEntry e;
            e.id = "mod:bulk_effect" + suffix;
            e.loops_animation = true;
            data.effects.push_back(std::move(e));
        }
        {
            og::data::ClasspackTreasureEntry e;
            e.id = "mod:bulk_treasure" + suffix;
            e.init_frame = 1;
            data.treasures.push_back(std::move(e));
        }
        {
            og::data::ClasspackGeneratorEntry e;
            e.id = "mod:bulk_generator" + suffix;
            e.has_lifetime = true;
            data.generators.push_back(std::move(e));
        }
    }

    EXPECT_EQ(og::resources::install_classpack_data(std::move(data)),
              5 * kFits)
        << "each order installs exactly its free slots and rejects the rest";

    // Per order: the last id that fits is populated, the first past the end
    // is not, and nothing wrapped around onto a core pin.
    const std::pair<Order, const char*> orders[] = {
        {Order::Living, "mod:bulk_living_"},
        {Order::Weapon, "mod:bulk_weapon_"},
        {Order::FX, "mod:bulk_effect_"},
        {Order::Treasure, "mod:bulk_treasure_"},
        {Order::Generator, "mod:bulk_generator_"},
    };
    for (const auto& [order, prefix] : orders) {
        const std::string last = prefix + std::to_string(kFits - 1);
        const std::string past = prefix + std::to_string(kFits);
        EXPECT_EQ(og::families::resolve_family_string_id(order, last.c_str()),
                  NUM_FAMILY_SLOTS - 1)
            << last;
        EXPECT_EQ(og::families::resolve_family_string_id(order, past.c_str()),
                  -1)
            << past;
    }

    const FamilyDescriptor* soldier = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(soldier, nullptr);
    EXPECT_STREQ(soldier->name, "SOLDIER") << "core pin untouched";
}

// init_bit_flags is a list of NAMES. One unknown name must keep the whole
// mask the descriptor already had rather than folding a partial mask — a
// partial fold would silently drop, say, BIT_IMMORTAL off a family and turn
// an immortal boss mortal. The rest of the entry still installs: an
// unrecognised flag is a forward-compatibility miss, not a corrupt pack.
TEST(ClasspackInstallErrors, an_unknown_bit_flag_name_keeps_the_whole_mask)
{
    ModSlotGuard guard;

    // Pin onto a core family so there is a non-zero mask to preserve.
    const FamilyDescriptor* ghost_before =
        get_family_descriptor(FAMILY_GHOST);
    ASSERT_NE(ghost_before, nullptr);
    const std::int32_t mask_before = ghost_before->init_bit_flags;
    ASSERT_NE(mask_before, 0) << "the ghost is the family with flags to lose";

    ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id = "mod:bad_flags";
    e.wire_id = std::to_string(FAMILY_GHOST);
    e.costs = hire_only(77);
    // FLYING is real and would fold if the loop folded partially.
    e.init_bit_flags =
        std::vector<std::string>{"FLYING", "NOT_A_REAL_FLAG"};
    data.living.push_back(std::move(e));

    EXPECT_EQ(og::resources::install_classpack_data(std::move(data)), 1)
        << "an unknown flag name does not sink the entry";

    const FamilyDescriptor* after = get_family_descriptor(FAMILY_GHOST);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->hiring_cost, 77) << "the good fields still applied";
    EXPECT_EQ(after->init_bit_flags, mask_before)
        << "an unknown flag name must keep the mask whole, not fold a prefix";
}

// An entry with no `id:` at all still resolves a slot, and the slot keeps
// whatever declared id it already had. Nothing may write a null name.
TEST(ClasspackInstallErrors, an_entry_without_a_declared_id_keeps_the_slot_id)
{
    ModSlotGuard guard;

    const FamilyDescriptor* before = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(before, nullptr);
    const char* declared_before = before->declared_id;

    ClasspackData data;
    data.pack = "mod";
    og::data::ClasspackLivingEntry e;
    e.id.clear();      // no id declared
    e.wire_id = "0";   // but pinned onto the soldier slot
    e.costs = hire_only(4242);
    data.living.push_back(std::move(e));

    EXPECT_EQ(og::resources::install_classpack_data(std::move(data)), 1);

    const FamilyDescriptor* after = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->hiring_cost, 4242) << "declared field applied";
    EXPECT_EQ(after->declared_id, declared_before)
        << "an id-less entry must not clear the slot's string id";
}
