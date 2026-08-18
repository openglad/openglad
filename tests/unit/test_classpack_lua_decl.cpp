/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The DECLARATION pass: `og.family` / `og.anims` / `og.pack` read for their
// DATA, in a throwaway VM, once per content change.
//
// A family file is evaluated twice in two different contexts, and this is
// the first: a fresh VM whose og.family harvests into the interchange
// structs the installer consumes, whose world bindings all refuse to run,
// and which is thrown away the moment the harvest is done. The
// second context (binding hooks against the installed descriptors) is
// tests/unit/test_classpack_lua_bind.cpp.
//
// Two things are load-bearing here.
//
// The first is the ERROR SURFACE. A declaration arrives over the network
// from another player's machine, and it is a program, not a document: it
// can loop, it can lie about a type, it can name a special that does not
// exist. The v3 rules say what a bad declaration does — reject the whole
// pack, by name, with the reason — and a rule with no test is a comment.
// Every load error the format spec calls for has a case below, asserted on
// the MESSAGE, because the message is the entire user interface of a pack
// that will not load.
//
// The second is that harvesting is a pure function of the chunk's bytes.
// The declaration pass runs behind an exact-bytes memo, so if it could read
// the world, the clock or the RNG, two peers would install different
// registries from the same pack.

#include <gtest/gtest.h>

#include <openglad/core/family_presentation.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/pack_scripts.h>

#include <cstdint>
#include <string>
#include <vector>

using og::data::ClasspackData;
using og::script::DeclareResult;

namespace {

constexpr const char* kPack = "v3decl";

class LuaFamilyDeclTest : public ::testing::Test {
protected:
    void SetUp() override { og::script::clear_pack_family_chunks(); }
    void TearDown() override { og::script::clear_pack_family_chunks(); }

    // One chunk, declared and evaluated. This is the whole pass: chunk
    // registration is what the mounted-tree walk does for a real pack, and
    // declare_pack_families is what the installer calls.
    //
    // The chunk name deliberately does NOT start with `packs/`. That prefix
    // is a claim that the bytes are pack content at that virtual path, and
    // registering a chunk that makes it declares those bytes to the pack-Lua
    // coverage inventory (pack_scripts.h) — which is right for a mount and a
    // lie for the throwaway declarations below, none of which exist anywhere
    // in the repository. Named this way they are what they are: test Lua,
    // which the coverage report leaves unmeasured.
    static DeclareResult declare(const std::string& lua, ClasspackData& out)
    {
        og::script::clear_pack_family_chunks();
        og::script::register_pack_family_chunk(
            {kPack, "v3decl/families/a.lua", lua});
        return og::script::declare_pack_families(kPack, out);
    }

    static DeclareResult declare(const std::string& lua)
    {
        ClasspackData discarded;
        return declare(lua, discarded);
    }
};

// A declaration must FAIL, and the message must contain `needle` — the
// reason is the whole point, so a test that only checked the boolean would
// pass on the wrong error.
void expect_rejected(const DeclareResult& r, const std::string& needle)
{
    EXPECT_FALSE(r.ok) << "expected a rejection, got a clean declaration";
    EXPECT_NE(std::string::npos, r.error.find(needle))
        << "wanted \"" << needle << "\" in:\n  " << r.error;
}

// The soldier, whole, in the v3 spelling. Every named block appears, so the
// harvest test below can check that each one landed and the error tests can
// patch one line of a declaration that is otherwise known good.
const char* kSoldier = R"LUA(
og.family("living", {
  id = "v3:soldier",
  wire_id = 61,
  name = "SOLDIER",
  short_name = og.NIL,
  stats  = { strength = 12, dexterity = 6, constitution = 12,
             intelligence = 8, armor = 9, level = 1 },
  combat = { hp = 120, melee_damage = 20, stepsize = 4,
             fire_delay = 6, fire_mp_cost = 2 },
  costs  = { hire = 250,
             train = { strength = 6, dexterity = 10, constitution = 6,
                       intelligence = 25, armor = 50, level = 200 } },
  specials = {
    { id = "charge",    name = "CHARGE",    mp_cost = 25 },
    { id = "boomerang", name = "BOOMERANG", mp_cost = 100 },
    { id = "whirlwind", name = "WHIRLWIND", mp_cost = 120,
      alternate = { name = "MYSTIC MACE" } },
    { id = "disarm",    name = "DISARM",    mp_cost = 150, slot = 5 },
  },
  default_weapon = "core:knife",
  flags = {},
  leaves_bloodspot = true,
  has_returning_weapon = true,
  is_undead = false,
  death_message = "SOLDIER SLAIN",
  sprite = "footman.png",
  animation = "standard",
  ai_line_of_sight = 7,
  description = "Your basic grunt.   \nTwo lines.",
  names = { "Lothar", "Gunther" },
  playable = true, playable_order = 0,
  glyph = "S", glyph_ascii = "S", glyph_color = "default",
  glyph_bold = false, glyph_transparent = false,
  radar_color = "none", radar_jitter = 0, radar_ping = true,
  tuning = { charge_speed = 4, whirl_scale = 1.5, chatty = true,
             flavour = "gruff" },
})
)LUA";

// The same declaration with one line replaced — the error tests all work
// this way, so the thing under test is the one line and not the 40 around
// it.
std::string soldier_with(const std::string& original,
                         const std::string& replacement)
{
    std::string text = kSoldier;
    const size_t at = text.find(original);
    EXPECT_NE(std::string::npos, at) << "patch anchor gone: " << original;
    if (at == std::string::npos)
        return text;
    return text.replace(at, original.size(), replacement);
}

}  // namespace

// ---------------------------------------------------------------------------
// The harvest
// ---------------------------------------------------------------------------

TEST_F(LuaFamilyDeclTest, a_full_living_declaration_lands_every_block)
{
    ClasspackData data;
    const DeclareResult r = declare(kSoldier, data);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(1u, data.living.size());
    const auto& e = data.living[0];

    EXPECT_EQ("v3:soldier", e.id);
    EXPECT_EQ("61", e.wire_id) << "an integer wire_id arrives as its text";
    ASSERT_TRUE(e.name.has_value());
    EXPECT_EQ("SOLDIER", *e.name);

    ASSERT_TRUE(e.stats.has_value());
    EXPECT_EQ(12, e.stats->strength);
    EXPECT_EQ(6, e.stats->dexterity);
    EXPECT_EQ(12, e.stats->constitution);
    EXPECT_EQ(8, e.stats->intelligence);
    EXPECT_EQ(9, e.stats->armor);
    EXPECT_EQ(1, e.stats->level);

    ASSERT_TRUE(e.combat.has_value());
    EXPECT_FLOAT_EQ(120.0f, e.combat->hp);
    EXPECT_FLOAT_EQ(20.0f, e.combat->melee_damage);
    EXPECT_FLOAT_EQ(4.0f, e.combat->stepsize);
    EXPECT_FLOAT_EQ(6.0f, e.combat->fire_delay);
    EXPECT_EQ(2, e.combat->fire_mp_cost);

    ASSERT_TRUE(e.costs.has_value());
    EXPECT_EQ(250, e.costs->hire);
    ASSERT_TRUE(e.costs->train.has_value());
    EXPECT_EQ(6, e.costs->train->strength);
    EXPECT_EQ(200, e.costs->train->level);

    ASSERT_TRUE(e.specials.has_value());
    ASSERT_EQ(4u, e.specials->size());
    EXPECT_EQ("charge", (*e.specials)[0].id);
    EXPECT_EQ(1, (*e.specials)[0].slot) << "list position gives the slot";
    EXPECT_EQ(25, (*e.specials)[0].mp_cost);
    EXPECT_EQ(3, (*e.specials)[2].slot);
    ASSERT_TRUE((*e.specials)[2].alternate_name.has_value());
    EXPECT_EQ("MYSTIC MACE", *(*e.specials)[2].alternate_name);
    EXPECT_EQ(5, (*e.specials)[3].slot) << "an explicit slot leaves a hole";

    ASSERT_TRUE(e.default_weapon.has_value());
    EXPECT_EQ("core:knife", *e.default_weapon);
    ASSERT_TRUE(e.init_bit_flags.has_value());
    EXPECT_TRUE(e.init_bit_flags->empty())
        << "an empty flags list is a declaration of no flags, not an "
           "omission";
    ASSERT_TRUE(e.leaves_bloodspot.has_value());
    EXPECT_TRUE(*e.leaves_bloodspot);
    ASSERT_TRUE(e.is_undead.has_value());
    EXPECT_FALSE(*e.is_undead);

    // Trailing spaces inside a description are significant: the picker pads
    // its lines by hand, so a stripped string re-wraps the class blurb.
    ASSERT_TRUE(e.description.present);
    EXPECT_FALSE(e.description.is_null);
    EXPECT_EQ("Your basic grunt.   \nTwo lines.", e.description.value);

    ASSERT_TRUE(e.names.has_value());
    ASSERT_EQ(2u, e.names->size());
    EXPECT_EQ("Lothar", (*e.names)[0]);

    ASSERT_TRUE(e.presentation.glyph.has_value());
    EXPECT_EQ("S", *e.presentation.glyph);
    ASSERT_TRUE(e.presentation.radar_color.has_value());
    EXPECT_EQ(og::kRadarColorNone, *e.presentation.radar_color)
        << "\"none\" is the sentinel spelling, folded in at harvest";
    ASSERT_TRUE(e.presentation.radar_ping.has_value())
        << "radar_ping (#209) rides the shared presentation harvest";
    EXPECT_TRUE(*e.presentation.radar_ping);

    // Tuning keeps the author's scalar SUBTYPE: 4 stays an integer, 1.5 a
    // float, so Lua-side arithmetic rounds the way the author wrote it.
    ASSERT_EQ(4u, e.tuning.size());
    bool saw_int = false, saw_num = false, saw_bool = false, saw_str = false;
    for (const auto& p : e.tuning) {
        if (p.key == "charge_speed") {
            saw_int = p.value.kind ==
                      og::data::ClasspackTuningValue::Kind::Integer;
            EXPECT_EQ(4, p.value.integer);
        } else if (p.key == "whirl_scale") {
            saw_num = p.value.kind ==
                      og::data::ClasspackTuningValue::Kind::Number;
            EXPECT_DOUBLE_EQ(1.5, p.value.number);
        } else if (p.key == "chatty") {
            saw_bool = p.value.kind ==
                       og::data::ClasspackTuningValue::Kind::Boolean;
            EXPECT_TRUE(p.value.boolean);
        } else if (p.key == "flavour") {
            saw_str = p.value.kind ==
                      og::data::ClasspackTuningValue::Kind::String;
            EXPECT_EQ("gruff", p.value.string);
        }
    }
    EXPECT_TRUE(saw_int && saw_num && saw_bool && saw_str)
        << "a tuning value must keep its Lua subtype";

    // Provenance: the rules that hold a declared slot to a higher
    // standard than one filled any other way read this back after install.
    ASSERT_EQ(1u, data.lua_declarations.size());
    EXPECT_EQ(Order::Living, data.lua_declarations[0].order);
    EXPECT_EQ("v3:soldier", data.lua_declarations[0].id);
}

TEST_F(LuaFamilyDeclTest, tuning_harvests_in_a_content_determined_order)
{
    // A Lua table has no source order to preserve, so the harvest sorts by
    // key. Two peers whose files differ only in how the author typed the
    // table must install the same tuning.
    ClasspackData a;
    ASSERT_TRUE(declare("og.family('effect', { id = 'v3:fx', tuning = "
                        "{ zeta = 1, alpha = 2, mid = 3 } })", a).ok);
    ClasspackData b;
    ASSERT_TRUE(declare("og.family('effect', { id = 'v3:fx', tuning = "
                        "{ mid = 3, alpha = 2, zeta = 1 } })", b).ok);
    ASSERT_EQ(3u, a.effects[0].tuning.size());
    ASSERT_EQ(a.effects[0].tuning.size(), b.effects[0].tuning.size());
    for (size_t i = 0; i < a.effects[0].tuning.size(); i++)
        EXPECT_EQ(a.effects[0].tuning[i].key, b.effects[0].tuning[i].key);
    EXPECT_EQ("alpha", a.effects[0].tuning[0].key);
    EXPECT_EQ("zeta", a.effects[0].tuning[2].key);
}

// V6. og.NIL is a PRESENT null, and the installer copies and patches: an
// absent key keeps whatever the slot held, an explicit null clears it. Both
// spellings have to stay expressible or a sparse declaration could not say
// "clear this" at all.
TEST_F(LuaFamilyDeclTest, og_nil_is_the_present_null_and_absence_is_not)
{
    ClasspackData nil_data;
    ASSERT_TRUE(declare(kSoldier, nil_data).ok);
    EXPECT_TRUE(nil_data.living[0].short_name.present);
    EXPECT_TRUE(nil_data.living[0].short_name.is_null);

    ClasspackData absent;
    ASSERT_TRUE(declare(soldier_with("short_name = og.NIL,", ""), absent).ok);
    EXPECT_FALSE(absent.living[0].short_name.present)
        << "an omitted key must not read as an explicit null";

    ClasspackData valued;
    ASSERT_TRUE(
        declare(soldier_with("short_name = og.NIL,", "short_name = \"SOLD\","),
                valued).ok);
    EXPECT_TRUE(valued.living[0].short_name.present);
    EXPECT_FALSE(valued.living[0].short_name.is_null);
    EXPECT_EQ("SOLD", valued.living[0].short_name.value);
}

TEST_F(LuaFamilyDeclTest, og_nil_where_no_null_can_be_read_is_a_load_error)
{
    // Only the fields the descriptor can actually hold a null in take
    // og.NIL. Anywhere else it is a typo with a plausible-looking spelling,
    // which is exactly what strict keys exist to catch.
    expect_rejected(declare(soldier_with("name = \"SOLDIER\",",
                                         "name = og.NIL,")),
                    "og.NIL means \"explicitly none\"");
}

TEST_F(LuaFamilyDeclTest, every_order_declares)
{
    ClasspackData data;
    const DeclareResult r = declare(R"LUA(
og.family("living",    { id = "v3:one",   name = "ONE" })
og.family("weapon",    { id = "v3:two",   name = "TWO", gravity = 0.5 })
og.family("effect",    { id = "v3:three", name = "THREE", loops_animation = true })
og.family("treasure",  { id = "v3:four",  name = "FOUR", init_frame = -1 })
og.family("generator", { id = "v3:five",  name = "FIVE", editor_label = "FIVE" })
)LUA", data);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(1u, data.living.size());
    EXPECT_EQ(1u, data.weapons.size());
    EXPECT_EQ(1u, data.effects.size());
    EXPECT_EQ(1u, data.treasures.size());
    EXPECT_EQ(1u, data.generators.size());
    EXPECT_EQ(5u, data.lua_declarations.size())
        << "each declaration is provenance, whatever its order";
}

// Non-living families have no stats block, so `hp` is where a pack declares
// durability for scenery it ships -- weapon HP is what makes a TREE take 50
// damage and a DOOR 5000 before it breaks. Before this existed, every
// pack-shipped non-living arrived with base HP 0 and broke on the first hit.
TEST_F(LuaFamilyDeclTest, every_non_living_order_declares_hp)
{
    ClasspackData data;
    const DeclareResult r = declare(R"LUA(
og.family("weapon",    { id = "v3:plank", name = "PLANK", hp = 50 })
og.family("effect",    { id = "v3:puff",  name = "PUFF",  hp = 1 })
og.family("treasure",  { id = "v3:chest", name = "CHEST", hp = 12 })
og.family("generator", { id = "v3:kiln",  name = "KILN",  hp = 300 })
)LUA", data);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(1u, data.weapons.size());
    ASSERT_EQ(1u, data.effects.size());
    ASSERT_EQ(1u, data.treasures.size());
    ASSERT_EQ(1u, data.generators.size());
    ASSERT_TRUE(data.weapons[0].hp.has_value());
    EXPECT_FLOAT_EQ(50.0f, *data.weapons[0].hp);
    ASSERT_TRUE(data.effects[0].hp.has_value());
    EXPECT_FLOAT_EQ(1.0f, *data.effects[0].hp);
    ASSERT_TRUE(data.treasures[0].hp.has_value());
    EXPECT_FLOAT_EQ(12.0f, *data.treasures[0].hp);
    ASSERT_TRUE(data.generators[0].hp.has_value());
    EXPECT_FLOAT_EQ(300.0f, *data.generators[0].hp);
}

// Absent means "keep whatever the core row supplies", which is what makes the
// field safe to add: a pack that says nothing changes nothing.
TEST_F(LuaFamilyDeclTest, omitted_hp_stays_unset)
{
    ClasspackData data;
    ASSERT_TRUE(declare(R"LUA(
og.family("weapon", { id = "v3:plank", name = "PLANK" })
)LUA", data).ok);
    ASSERT_EQ(1u, data.weapons.size());
    EXPECT_FALSE(data.weapons[0].hp.has_value());
}

// V1's other half: one file may declare a whole family GROUP. The slime
// trio shares closures and a flagged RNG ordering comment, and splitting it
// into three files to satisfy a one-per-file rule would split the comment
// away from the code it explains.
TEST_F(LuaFamilyDeclTest, one_chunk_may_declare_many_families)
{
    ClasspackData data;
    ASSERT_TRUE(declare(R"LUA(
local shared = function() return true end
for i, id in ipairs({ "v3:slime", "v3:mediumslime", "v3:smallslime" }) do
  og.family("living", { id = id, wire_id = 60 + i, on_death = shared })
end
)LUA", data).ok);
    ASSERT_EQ(3u, data.living.size());
    EXPECT_EQ("v3:slime", data.living[0].id);
    EXPECT_EQ("63", data.living[2].wire_id);
}

// ---------------------------------------------------------------------------
// V5 — strict keys
// ---------------------------------------------------------------------------

TEST_F(LuaFamilyDeclTest, an_unknown_top_level_key_is_a_load_error)
{
    expect_rejected(declare(soldier_with("name = \"SOLDIER\",",
                                         "nmae = \"SOLDIER\",")),
                    "unknown key 'nmae' (did you mean 'name'?)");
}

TEST_F(LuaFamilyDeclTest, unknown_keys_inside_the_named_blocks_are_errors)
{
    expect_rejected(declare(soldier_with("strength = 12,", "strenght = 12,")),
                    "unknown key 'strenght' (did you mean 'strength'?)");
    expect_rejected(declare(soldier_with("hp = 120,", "hpp = 120,")),
                    "unknown key 'hpp' (did you mean 'hp'?)");
    expect_rejected(declare(soldier_with("hire = 250,", "hier = 250,")),
                    "unknown key 'hier' (did you mean 'hire'?)");
    expect_rejected(
        declare(soldier_with("{ id = \"charge\",    name = \"CHARGE\",    "
                             "mp_cost = 25 },",
                             "{ id = \"charge\", name = \"CHARGE\", "
                             "mp_cost = 25, mpcost = 5 },")),
        "unknown key 'mpcost'");
}

// The typo this rule was written for. `default_case` inside the specials
// list would leave every slot unhandled, and without a strict check over
// the LIST itself it would only surface much later as the unhandled-castable
// error at the end of the load.
TEST_F(LuaFamilyDeclTest, a_misspelled_default_cast_is_caught_where_it_is_written)
{
    expect_rejected(
        declare(soldier_with("specials = {",
                             "specials = { default_case = function() end,")),
        "unknown key 'default_case' (did you mean 'default_cast'?)");
}

TEST_F(LuaFamilyDeclTest, a_wrong_word_gets_no_suggestion)
{
    // The suggestion budget is half the candidate's length. "wibble" is not
    // a misspelling of anything, and guessing at it would be worse than
    // saying nothing.
    ClasspackData data;
    const DeclareResult r =
        declare(soldier_with("name = \"SOLDIER\",", "wibble = 3,"), data);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(std::string::npos, r.error.find("unknown key 'wibble'"));
    EXPECT_EQ(std::string::npos, r.error.find("did you mean"));
}

// A named block is all-or-nothing: defaulting one member in silence would
// ship, say, a class with no armour and no complaint. The strict-key check
// runs FIRST inside a block, so a misspelled axis reads as the typo it is
// rather than as the member it displaced.
TEST_F(LuaFamilyDeclTest, a_named_block_must_spell_every_member)
{
    expect_rejected(declare("og.family('living', { id = 'v3:x', stats = "
                            "{ strength = 1 } })"),
                    "'v3:x'.stats: missing 'dexterity'");
    expect_rejected(declare("og.family('living', { id = 'v3:x', combat = "
                            "{ hp = 1 } })"),
                    "'v3:x'.combat: missing 'melee_damage'");
    expect_rejected(declare("og.family('living', { id = 'v3:x', costs = "
                            "{ train = { armor = 1 } } })"),
                    "'v3:x'.costs: missing 'hire'");
    // costs.train is the exception, and deliberately: an unpriced axis
    // costs 0, which is what the shipped families have always meant.
    ClasspackData data;
    ASSERT_TRUE(declare("og.family('living', { id = 'v3:x', costs = "
                        "{ hire = 10, train = { armor = 1 } } })", data).ok);
    ASSERT_TRUE(data.living[0].costs->train.has_value());
    EXPECT_EQ(1, data.living[0].costs->train->armor);
    EXPECT_EQ(0, data.living[0].costs->train->strength);
}

TEST_F(LuaFamilyDeclTest, a_block_that_is_not_a_table_says_so)
{
    expect_rejected(declare("og.family('living', { id = 'v3:x', "
                            "stats = 7 })"),
                    "stats: expected a table, got number");
}

// V5's escape hatch. A pack that has to load on two engine releases needs
// SOMEWHERE to put a key this one does not know, or strict keys make
// straddling impossible.
TEST_F(LuaFamilyDeclTest, ext_is_reserved_and_ignored)
{
    ClasspackData data;
    ASSERT_TRUE(declare(soldier_with("name = \"SOLDIER\",",
                                     "name = \"SOLDIER\", ext = "
                                     "{ future = { 1, 2, 3 } },"),
                        data).ok);
    ASSERT_EQ(1u, data.living.size());
    EXPECT_EQ("SOLDIER", *data.living[0].name);
}

// ---------------------------------------------------------------------------
// The shape of a declaration
// ---------------------------------------------------------------------------

TEST_F(LuaFamilyDeclTest, a_declaration_needs_a_qualified_id)
{
    expect_rejected(declare("og.family('living', { name = 'X' })"),
                    "a declaration needs an id");
    expect_rejected(declare("og.family('living', { id = 'soldier' })"),
                    "a family id is <pack>:<family>");
}

TEST_F(LuaFamilyDeclTest, an_unknown_order_names_the_five_that_exist)
{
    expect_rejected(declare("og.family('monster', { id = 'v3:x' })"),
                    "unknown order 'monster' (living, weapon, effect, "
                    "treasure, generator)");
}

TEST_F(LuaFamilyDeclTest, a_field_of_the_wrong_type_names_the_field)
{
    expect_rejected(declare(soldier_with("hp = 120,", "hp = \"lots\",")),
                    "'v3:soldier'.combat.hp: expected a number, got string");
    expect_rejected(declare(soldier_with("names = { \"Lothar\", \"Gunther\" },",
                                         "names = \"Lothar\",")),
                    "names: expected a list, got string");
    expect_rejected(declare(soldier_with("wire_id = 61,", "wire_id = {},")),
                    "wire_id: expected a number 0..255 or \"auto\", got "
                    "table");
    expect_rejected(declare(soldier_with("leaves_bloodspot = true,",
                                         "leaves_bloodspot = 1,")),
                    "leaves_bloodspot: expected true or false, got number");
}

TEST_F(LuaFamilyDeclTest, wire_id_auto_is_a_string_the_installer_reads)
{
    ClasspackData data;
    ASSERT_TRUE(declare(soldier_with("wire_id = 61,", "wire_id = \"auto\","),
                        data).ok);
    EXPECT_EQ("auto", data.living[0].wire_id);
}

TEST_F(LuaFamilyDeclTest, an_inline_hook_of_the_wrong_type_is_a_load_error)
{
    expect_rejected(declare(soldier_with("playable = true,",
                                         "on_death = 7,")),
                    "on_death: a hook is a function, got number");
}

// ---------------------------------------------------------------------------
// V2 — specials
// ---------------------------------------------------------------------------

TEST_F(LuaFamilyDeclTest, the_disabled_sentinel_is_refused_by_name)
{
    // 5000 is the registry's own "this slot holds no special" marker, so a
    // declaration cannot spell it as a price. A list says the same thing by
    // leaving the special out, and the refusal points at that.
    const DeclareResult r =
        declare(soldier_with("mp_cost = 25 },", "mp_cost = 5000 },"));
    expect_rejected(r, "5000 and up is the registry's own \"disabled\"");
    EXPECT_NE(std::string::npos, r.error.find("leave the special out"));
}

TEST_F(LuaFamilyDeclTest, a_negative_special_cost_is_a_load_error)
{
    expect_rejected(declare(soldier_with("mp_cost = 25 },",
                                         "mp_cost = -1 },")),
                    "cannot cost negative magic");
}

TEST_F(LuaFamilyDeclTest, a_special_needs_its_three_required_fields)
{
    expect_rejected(declare("og.family('living', { id = 'v3:x', specials = "
                            "{ { name = 'A', mp_cost = 1 } } })"),
                    "a special needs an id");
    expect_rejected(declare("og.family('living', { id = 'v3:x', specials = "
                            "{ { id = 'a', mp_cost = 1 } } })"),
                    "a special needs a name (the HUD string)");
    expect_rejected(declare("og.family('living', { id = 'v3:x', specials = "
                            "{ { id = 'a', name = 'A' } } })"),
                    "a special needs an mp_cost");
}

TEST_F(LuaFamilyDeclTest, special_ids_are_unique_and_never_the_catch_all)
{
    expect_rejected(
        declare("og.family('living', { id = 'v3:x', specials = {"
                "{ id = 'a', name = 'A', mp_cost = 1 },"
                "{ id = 'a', name = 'B', mp_cost = 2 } } })"),
        "already used by an earlier special of this family");
    // "default" is the specials table's catch-all key, so a special named
    // for it could never be reached by name.
    expect_rejected(
        declare("og.family('living', { id = 'v3:x', specials = "
                "{ { id = 'default', name = 'A', mp_cost = 1 } } })"),
        "'default' is reserved");
    expect_rejected(
        declare("og.family('living', { id = 'v3:x', specials = "
                "{ { id = 'Charge', name = 'A', mp_cost = 1 } } })"),
        "a special id is lowercase letters, digits and underscores");
}

TEST_F(LuaFamilyDeclTest, slots_run_forward_and_stop_at_five)
{
    expect_rejected(
        declare("og.family('living', { id = 'v3:x', specials = {"
                "{ id = 'a', name = 'A', mp_cost = 1, slot = 3 },"
                "{ id = 'b', name = 'B', mp_cost = 1, slot = 2 } } })"),
        "must be greater than the previous entry's slot 3");
    expect_rejected(
        declare("og.family('living', { id = 'v3:x', specials = "
                "{ { id = 'a', name = 'A', mp_cost = 1, slot = 6 } } })"),
        "slot 6 does not exist (a family has slots 1..5)");
}

TEST_F(LuaFamilyDeclTest, alternate_takes_a_named_table)
{
    expect_rejected(declare("og.family('living', { id = 'v3:x', specials = "
                            "{ { id = 'a', name = 'A', mp_cost = 1, "
                            "alternate = 'MACE' } } })"),
                    "alternate takes a table");
    expect_rejected(declare("og.family('living', { id = 'v3:x', specials = "
                            "{ { id = 'a', name = 'A', mp_cost = 1, "
                            "alternate = {} } } })"),
                    "alternate: needs a name");
}

TEST_F(LuaFamilyDeclTest, a_cast_is_a_function_or_the_explicit_no_op)
{
    // `cast = false` is the charged no-op said out loud: the slot spends its
    // MP and does nothing, on purpose. Anything else that is not a function
    // is a mistake.
    ClasspackData data;
    EXPECT_TRUE(declare("og.family('living', { id = 'v3:x', specials = "
                        "{ { id = 'a', name = 'A', mp_cost = 1, "
                        "cast = false } } })", data).ok);
    expect_rejected(declare("og.family('living', { id = 'v3:x', specials = "
                            "{ { id = 'a', name = 'A', mp_cost = 1, "
                            "cast = 3 } } })"),
                    "cast: expected a function, got number");
    expect_rejected(declare("og.family('living', { id = 'v3:x', specials = "
                            "{ { id = 'a', name = 'A', mp_cost = 1, "
                            "cast = true } } })"),
                    "cast: expected a function, got boolean");
    expect_rejected(declare("og.family('living', { id = 'v3:x', specials = "
                            "{ default_cast = 5 } })"),
                    "default_cast: expected a function, got number");
}

// Two spellings answering one question is not a merge — it is an ambiguity,
// and the engine refuses to pick a winner.
TEST_F(LuaFamilyDeclTest, mixing_the_two_spellings_of_one_hook_is_an_error)
{
    expect_rejected(
        declare("og.family('living', { id = 'v3:x',"
                " check_special_ai = function() return true end,"
                " specials = { { id = 'a', name = 'A', mp_cost = 1,"
                " ai = function() return true end } } })"),
        "two answers to the same question");
    expect_rejected(
        declare("og.family('living', { id = 'v3:x',"
                " do_special = function() return true end,"
                " specials = { { id = 'a', name = 'A', mp_cost = 1,"
                " cast = function() return true end } } })"),
        "both answer the cast");
    // The default_cast half of the same rule.
    expect_rejected(
        declare("og.family('living', { id = 'v3:x',"
                " do_special = function() return true end,"
                " specials = { default_cast = function() return true end } })"),
        "both answer the cast");
}

// ---------------------------------------------------------------------------
// V8 — og.anims
// ---------------------------------------------------------------------------

TEST_F(LuaFamilyDeclTest, an_anim_set_harvests_rows_holes_and_frames)
{
    ClasspackData data;
    ASSERT_TRUE(declare(R"LUA(
og.anims("slime", { rows = 4, frames = { {0,1,2,1}, false, {3}, false } })
)LUA", data).ok);
    ASSERT_EQ(1u, data.anims.size());
    const auto& set = data.anims[0];
    EXPECT_EQ("slime", set.name);
    ASSERT_TRUE(set.rows.has_value());
    EXPECT_EQ(4, *set.rows);
    ASSERT_EQ(4u, set.frames.size());
    EXPECT_FALSE(set.frames[0].is_null);
    EXPECT_EQ(std::vector<std::int32_t>({0, 1, 2, 1}), set.frames[0].frames);
    // `false`, not nil: a nil in a Lua array punches a hole the length
    // operator cannot see, so the null row has to be a value.
    EXPECT_TRUE(set.frames[1].is_null);
    EXPECT_TRUE(set.frames[1].frames.empty());
    EXPECT_TRUE(set.frames[3].is_null);
}

TEST_F(LuaFamilyDeclTest, a_malformed_anim_set_is_a_load_error)
{
    expect_rejected(declare("og.anims('x', { rows = 2 })"),
                    "needs a frames list");
    expect_rejected(declare("og.anims('x', { frames = 3 })"),
                    "frames: expected a list of rows, got number");
    expect_rejected(declare("og.anims('x', { frames = { 3 } })"),
                    "a frame row is a list of frame indices, or false for a "
                    "null row, got number");
    expect_rejected(declare("og.anims('x', { frames = { {0} }, rowz = 1 })"),
                    "unknown key 'rowz' (did you mean 'rows'?)");
    expect_rejected(declare("og.anims('x', { frames = { {'a'} } })"),
                    "expected a whole number, got string");
}

// ---------------------------------------------------------------------------
// V9 — og.pack
// ---------------------------------------------------------------------------

TEST_F(LuaFamilyDeclTest, the_pack_header_fills_the_manifest_scalars)
{
    ClasspackData data;
    ASSERT_TRUE(declare(R"LUA(
og.pack{ id = "v3decl", version = "2.0", title = "Test Pack",
         authors = { "Fable", "Yan" } }
)LUA", data).ok);
    EXPECT_EQ("v3decl", data.pack);
    EXPECT_EQ("2.0", data.version);
    EXPECT_EQ("Test Pack", data.title);
    // The interchange carries authors as one free-text line, because that is
    // what the multiplayer manifest shows.
    EXPECT_EQ("Fable, Yan", data.authors);
}

TEST_F(LuaFamilyDeclTest, the_pack_header_is_strict_too)
{
    expect_rejected(declare("og.pack{ titel = 'X' }"),
                    "unknown key 'titel' (did you mean 'title'?)");
    expect_rejected(declare("og.pack{ authors = 'Fable' }"),
                    "authors: expected a list, got string");
}

// ---------------------------------------------------------------------------
// What the declaration VM will NOT do
// ---------------------------------------------------------------------------

// The determinism rule. The declaration pass runs behind a memo on exact
// bytes and its output is installed on every peer, so a declaration that
// could read the RNG, the clock or the world would make the registry a
// function of one machine's game state.
TEST_F(LuaFamilyDeclTest, the_world_api_is_closed_during_a_declaration)
{
    for (const char* call : {"og.rand(5)", "og.oblist()", "og.my_team()",
                             "og.add_ob('living', 0)"}) {
        const DeclareResult r = declare(std::string("local x = ") + call);
        EXPECT_FALSE(r.ok) << call << " ran at declaration time";
        EXPECT_NE(std::string::npos,
                  r.error.find("the world API is dispatch-time only"))
            << call << " → " << r.error;
    }
}

// og.use is the exception, and deliberately: lib modules are pure, and a
// declaration that cannot call one has to inline every shared table.
TEST_F(LuaFamilyDeclTest, og_use_resolves_inside_a_declaration)
{
    // Registered ALONGSIDE the shipped pack's modules and withdrawn by pack
    // id: clearing the registry outright would leave every later test in
    // this binary with a core pack whose scripts cannot og.use anything,
    // and nothing puts them back.
    og::script::register_pack_lib_module(
        {kPack, "shared", "v3decl/lib/shared.lua",
         "return { hp = 99 }\n"});
    ClasspackData data;
    const DeclareResult r = declare(
        "local shared = og.use('shared')\n"
        "og.family('living', { id = 'v3:x', ai_line_of_sight = shared.hp })\n",
        data);
    og::script::unregister_pack_lib_modules(kPack);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(1u, data.living.size());
    ASSERT_TRUE(data.living[0].ai_line_of_sight.has_value());
    EXPECT_EQ(99, *data.living[0].ai_line_of_sight);
}

// A same-pack forward reference cannot resolve mid-declaration: the family
// it names may be three lines further down the file. og.family_id answers a
// deferred token that is TRUTHY — the shipped idiom is
// `assert(og.family_id(...))` — and errors the moment anything tries to use
// it as a number.
TEST_F(LuaFamilyDeclTest, family_id_defers_during_a_declaration)
{
    ClasspackData data;
    ASSERT_TRUE(declare(
        "local ref = og.family_id('living', 'core:soldier')\n"
        "assert(ref, 'a deferred reference must be truthy')\n"
        "og.family('living', { id = 'v3:x' })\n", data).ok);
    const DeclareResult used = declare(
        "local ref = og.family_id('living', 'core:soldier')\n"
        "og.family('living', { id = 'v3:x', init_ani_type = ref + 1 })\n");
    EXPECT_FALSE(used.ok) << "a deferred reference must refuse to be a number";
}

TEST_F(LuaFamilyDeclTest, the_api_version_is_readable_for_feature_detection)
{
    ClasspackData data;
    ASSERT_TRUE(declare(
        "assert(og.api.version == " + std::to_string(og::script::kPackApiVersion) +
        ")\nog.family('living', { id = 'v3:x' })\n", data).ok);
    EXPECT_EQ(1u, data.living.size());
    // Frozen: a pack cannot rewrite the version it is being told about.
    EXPECT_FALSE(declare("og.api.version = 99").ok);
}

// ---------------------------------------------------------------------------
// Rejection is per PACK, not per family
// ---------------------------------------------------------------------------

TEST_F(LuaFamilyDeclTest, one_bad_chunk_rejects_the_whole_pack)
{
    og::script::clear_pack_family_chunks();
    og::script::register_pack_family_chunk(
        {kPack, "v3decl/families/a-good.lua",
         "og.family('living', { id = 'v3:good' })\n"});
    og::script::register_pack_family_chunk(
        {kPack, "v3decl/families/b-bad.lua",
         "og.family('living', { id = 'v3:bad', nmae = 'X' })\n"});
    og::script::register_pack_family_chunk(
        {kPack, "v3decl/families/c-never.lua",
         "og.family('living', { id = 'v3:never' })\n"});
    ClasspackData data;
    const DeclareResult r = og::script::declare_pack_families(kPack, data);
    expect_rejected(r, "unknown key 'nmae'");
    // The caller throws the harvest away, so what matters is that the pass
    // STOPPED: a half-declared pack must never reach the installer.
    for (const auto& e : data.living)
        EXPECT_NE("v3:never", e.id) << "the pass ran on past a rejection";
}

TEST_F(LuaFamilyDeclTest, a_chunk_that_does_not_compile_is_named)
{
    const DeclareResult r = declare("og.family('living', { id = ");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(std::string::npos, r.error.find("v3decl/families/a.lua"))
        << r.error;
}

TEST_F(LuaFamilyDeclTest, a_pack_with_no_family_chunks_costs_nothing)
{
    og::script::clear_pack_family_chunks();
    ClasspackData data;
    const DeclareResult r =
        og::script::declare_pack_families("no-such-pack", data);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.error.empty());
    EXPECT_TRUE(data.living.empty());
}

// The failed-pack ledger the bind replay reads: a rejected pack's chunks
// are still registered (registration precedes install), and replaying them
// would report a second, worse error in every VM the process builds.
TEST_F(LuaFamilyDeclTest, a_failed_declaration_can_be_remembered_and_cleared)
{
    og::script::clear_failed_pack_declarations();
    EXPECT_FALSE(og::script::pack_declaration_failed(kPack));
    og::script::note_failed_pack_declaration(kPack);
    EXPECT_TRUE(og::script::pack_declaration_failed(kPack));
    EXPECT_FALSE(og::script::pack_declaration_failed("someone-else"));
    og::script::clear_failed_pack_declarations();
    EXPECT_FALSE(og::script::pack_declaration_failed(kPack))
        << "the ledger describes the install pass that just ran, so a "
           "reinstall starts it empty";
}
