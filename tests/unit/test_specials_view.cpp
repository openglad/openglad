/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The New Specials setting as the sim reads it: og::sim::specials_view.
//
// With the setting off, every answer must equal the registry as it was
// before the setting existed: a special or alternate declared `new_kit =
// true` reads "NONE" and costs kSpecialCostDisabled, so no player, HUD or
// bot can tell it is there. No shipped family declares a new special yet, so
// the view is exercised through a test-only declaration installed over the
// golem's slot (a family no kit uses), the in-place override the class-pack
// install tests use, and put back afterwards.
//
// Also here: the declaration keys' refusals, the special-cycling rule, and
// the Lua bindings that read the setting and the per-walker kit state
// (walker:kit_state() and friends, walker:alternate_down(),
// og.match_setting("new_specials"), og.C.KIT_*), plus the kit marker
// helper module every later kit hangs its timers on.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "kit_probe_family.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/placement.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/packs.h>

#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using og::data::ClasspackData;
using og::test::kProbeDecl;
using og::test::ProbeFamily;
using og::test::declare;

std::string name_or_empty(const char* name)
{
    return name == nullptr ? std::string() : std::string(name);
}

walker* add_caster(TestGameWorld& tw, int family, short level = 10)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w != nullptr && w->stats() != nullptr) {
        w->stats()->set_level(level);
        w->stats()->set_max_magicpoints(500);
        w->stats()->set_magicpoints(500);
    }
    return w;
}

// The HUD's special box is 11 characters (score_panel.cpp's SPC line); a
// new name longer than that is cut off on every HUD.
constexpr std::size_t kHudSpecialBox = 11;

bool fits_hud_box(const char* name)
{
    return name != nullptr && std::strlen(name) <= kHudSpecialBox;
}

// Installs a chosen gameplay context (none at all, here) and restores the
// previous one, even when an ASSERT unwinds the test early.
class ScopedNoGameplayContext {
public:
    ScopedNoGameplayContext() : previous_(current_game) { current_game = nullptr; }
    ~ScopedNoGameplayContext() { current_game = previous_; }
    ScopedNoGameplayContext(const ScopedNoGameplayContext&) = delete;
    ScopedNoGameplayContext& operator=(const ScopedNoGameplayContext&) = delete;

private:
    GameplayContext* previous_;
};

}  // namespace

// For every installed living family and every slot: with the setting off
// (and with no world at all), the view answers the registry before the
// feature (NONE / 5000 wherever new_kit is declared, the declared values
// elsewhere), for the shifted column too; with the setting on it answers the
// declared values.
TEST(SpecialsView, off_equals_registry_before_the_feature)
{
    og::test::ScopedHookFailureGuard guard;
    ProbeFamily probe;
    ASSERT_EQ(1, probe.installed());
    TestGameWorld tw;

    int families_checked = 0;
    int hidden_slots_seen = 0;
    for (int family = 0; family < NUM_FAMILY_SLOTS; ++family) {
        const FamilyDescriptor* fd = get_family_descriptor(family);
        if (fd == nullptr)
            continue;
        walker* w = add_caster(tw, family);
        ASSERT_NE(nullptr, w) << "family " << family;
        ++families_checked;
        for (int slot = 0; slot < FD_NUM_SPECIALS; ++slot) {
            const bool slot_new = fd->special_new_kit[slot];
            const bool alt_new = fd->alternate_new_kit[slot];
            const unsigned short stamped = w->stats()->special_cost(slot);
            ASSERT_EQ(fd->special_cost[slot], stamped)
                << "the loader stamps the declared cost, family " << family;
            if (slot_new)
                ++hidden_slots_seen;
            for (const short flag : {short{0}, short{1}}) {
                tw.world().new_specials = flag;
                const bool slot_hidden = slot_new && flag == 0;
                const bool alt_hidden = slot_hidden || (alt_new && flag == 0);
                EXPECT_EQ(slot_hidden ? std::string(kSpecialNameNone)
                                      : name_or_empty(fd->special_names[slot]),
                          name_or_empty(og::sim::special_name(fd, slot, flag)))
                    << "family " << family << " slot " << slot
                    << " flag " << flag;
                EXPECT_EQ(alt_hidden ? std::string(kSpecialNameNone)
                                     : name_or_empty(fd->alternate_names[slot]),
                          name_or_empty(og::sim::alternate_name(fd, slot, flag)))
                    << "family " << family << " slot " << slot
                    << " flag " << flag;
                const unsigned short unshifted =
                    slot_hidden ? kSpecialCostDisabled : stamped;
                const unsigned short shifted =
                    slot_hidden ? kSpecialCostDisabled
                    : (!alt_hidden && fd->alternate_cost[slot] > 0)
                        ? fd->alternate_cost[slot]
                        : stamped;
                EXPECT_EQ(unshifted, og::sim::cast_cost(*w, slot, false))
                    << "family " << family << " slot " << slot
                    << " flag " << flag;
                EXPECT_EQ(shifted, og::sim::cast_cost(*w, slot, true))
                    << "family " << family << " slot " << slot
                    << " flag " << flag;
            }
            // No world at all reads as off.
            {
                ScopedNoGameplayContext no_world;
                EXPECT_EQ(0, og::sim::session_new_specials());
                EXPECT_EQ(slot_new ? kSpecialCostDisabled : stamped,
                          og::sim::cast_cost(*w, slot, false))
                    << "family " << family << " slot " << slot
                    << " with no world";
            }
        }
    }
    EXPECT_GE(families_checked, NUM_FAMILIES) << "every core family installed";
    EXPECT_EQ(2, hidden_slots_seen)
        << "the probe's two new-kit slots were really in the sweep";

    // The rule above, spelled out on the probe so the sweep cannot agree
    // with a broken rule by computing it the same way.
    const FamilyDescriptor* fd = ProbeFamily::fd();
    walker* golem = add_caster(tw, FAMILY_GOLEM);
    ASSERT_NE(nullptr, golem);
    tw.world().new_specials = 0;
    EXPECT_STREQ("NONE", og::sim::special_name(fd, 2, 0));
    EXPECT_STREQ("NONE", og::sim::alternate_name(fd, 1, 0));
    EXPECT_STREQ("DROP BOMB", og::sim::special_name(fd, 1, 0));
    EXPECT_EQ(5000, og::sim::cast_cost(*golem, 2, false));
    EXPECT_EQ(5000, og::sim::cast_cost(*golem, 4, true));
    EXPECT_EQ(35, og::sim::cast_cost(*golem, 1, true));
    tw.world().new_specials = 1;
    EXPECT_STREQ("DIG IN", og::sim::special_name(fd, 2, 1));
    EXPECT_STREQ("MINE", og::sim::alternate_name(fd, 1, 1));
    EXPECT_EQ(40, og::sim::cast_cost(*golem, 2, false));
    EXPECT_EQ(60, og::sim::cast_cost(*golem, 4, true))
        << "a slot with no alternate costs its own price shifted";
    EXPECT_EQ(70, og::sim::cast_cost(*golem, 1, true));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The proof that does not lean on the registry golden: the six classic kits
// the feature extends, written out by hand, as the setting-off view shows
// them. (The faerie and the orc captain have no specials at all.)
TEST(SpecialsView, off_view_matches_the_classic_six_literally)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 0;

    struct Row {
        int family;
        int slot;
        const char* name;
        const char* alternate;
        unsigned short cost;
    };
    const Row rows[] = {
        {FAMILY_THIEF, 1, "DROP BOMB", "NONE", 35},
        {FAMILY_THIEF, 2, "CLOAK", "NONE", 125},
        {FAMILY_THIEF, 3, "TAUNT ENEMY", "CHARM OPPONENT", 100},
        {FAMILY_THIEF, 4, "POISON CLOUD", "NONE", 150},
        {FAMILY_THIEF, 5, "NONE", "NONE", 5000},
        {FAMILY_SKELETON, 1, "TUNNEL", "NONE", 10},
        {FAMILY_SKELETON, 2, "NONE", "NONE", 5000},
        {FAMILY_SKELETON, 3, "NONE", "NONE", 5000},
        {FAMILY_SKELETON, 4, "NONE", "NONE", 5000},
        {FAMILY_FIREELEMENTAL, 1, "STARBURST", "NONE", 50},
        {FAMILY_FIREELEMENTAL, 2, "NONE", "NONE", 5000},
        {FAMILY_FIREELEMENTAL, 3, "NONE", "NONE", 5000},
        {FAMILY_FIREELEMENTAL, 4, "NONE", "NONE", 5000},
        {FAMILY_GHOST, 1, "SCARE", "NONE", 30},
        {FAMILY_GHOST, 2, "NONE", "NONE", 5000},
        {FAMILY_GHOST, 3, "NONE", "NONE", 5000},
        {FAMILY_GHOST, 4, "NONE", "NONE", 5000},
        {FAMILY_ORC, 1, "HOWL", "NONE", 25},
        {FAMILY_ORC, 2, "EAT CORPSE", "NONE", 20},
        {FAMILY_ORC, 3, "NONE", "NONE", 5000},
        {FAMILY_ORC, 4, "NONE", "NONE", 5000},
        {FAMILY_FAERIE, 1, "NONE", "NONE", 5000},
        {FAMILY_FAERIE, 2, "NONE", "NONE", 5000},
        {FAMILY_FAERIE, 3, "NONE", "NONE", 5000},
        {FAMILY_FAERIE, 4, "NONE", "NONE", 5000},
        {FAMILY_BIG_ORC, 1, "NONE", "NONE", 5000},
        {FAMILY_BIG_ORC, 2, "NONE", "NONE", 5000},
        {FAMILY_BIG_ORC, 3, "NONE", "NONE", 5000},
        {FAMILY_BIG_ORC, 4, "NONE", "NONE", 5000},
    };
    for (const Row& row : rows) {
        const FamilyDescriptor* fd = get_family_descriptor(row.family);
        ASSERT_NE(nullptr, fd) << row.family;
        walker* w = add_caster(tw, row.family);
        ASSERT_NE(nullptr, w) << row.family;
        EXPECT_STREQ(row.name, og::sim::special_name(fd, row.slot, 0))
            << "family " << row.family << " slot " << row.slot;
        EXPECT_STREQ(row.alternate, og::sim::alternate_name(fd, row.slot, 0))
            << "family " << row.family << " slot " << row.slot;
        EXPECT_EQ(row.cost, og::sim::cast_cost(*w, row.slot, false))
            << "family " << row.family << " slot " << row.slot;
        EXPECT_EQ(row.cost, og::sim::cast_cost(*w, row.slot, true))
            << "no classic alternate carries its own price: family "
            << row.family << " slot " << row.slot;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A priced alternate is charged only when the shift is down AND the
// alternate is in play; an unpriced (classic) alternate costs the primary;
// a hidden slot hides its alternate too (D21); a HIDDEN or CHANNEL walker
// pays nothing for an in-play slot and is still refused a hidden one (D22).
TEST(SpecialsView, alternate_cost_gates_only_when_shifted_and_in_play)
{
    og::test::ScopedHookFailureGuard guard;
    ProbeFamily probe;
    ASSERT_EQ(1, probe.installed());
    TestGameWorld tw;
    walker* w = add_caster(tw, FAMILY_GOLEM);
    ASSERT_NE(nullptr, w);
    const FamilyDescriptor* fd = ProbeFamily::fd();

    tw.world().new_specials = 1;
    EXPECT_EQ(35, og::sim::cast_cost(*w, 1, false));
    EXPECT_EQ(70, og::sim::cast_cost(*w, 1, true)) << "MINE costs its own 70";
    EXPECT_EQ(70, og::sim::alternate_cost_in_play(fd, 1, 1));
    EXPECT_EQ(2, og::sim::cast_cost(*w, 3, true))
        << "an unpriced alternate costs the primary";
    EXPECT_EQ(0, og::sim::alternate_cost_in_play(fd, 3, 1));
    EXPECT_EQ(90, og::sim::cast_cost(*w, 2, true));

    tw.world().new_specials = 0;
    EXPECT_EQ(35, og::sim::cast_cost(*w, 1, true))
        << "with the setting off the new alternate is not in play";
    EXPECT_EQ(0, og::sim::alternate_cost_in_play(fd, 1, 0));
    EXPECT_EQ(5000, og::sim::cast_cost(*w, 2, true))
        << "a hidden slot stays 5000 whatever the shift (D21)";
    EXPECT_STREQ("NONE", og::sim::alternate_name(fd, 2, 0))
        << "the alternate is subordinate to its slot";
    EXPECT_FALSE(og::sim::alternate_in_play(fd, 2, 0));
    EXPECT_FALSE(og::sim::special_in_play(fd, 2, 0));
    EXPECT_TRUE(og::sim::special_in_play(fd, 1, 0));
    EXPECT_FALSE(og::sim::alternate_in_play(fd, 1, 0));

    // Second presses (D22): a HIDDEN or CHANNEL walker pays nothing.
    for (const std::uint8_t mark : {KIT_HIDDEN, KIT_CHANNEL}) {
        w->set_kit_state(mark);
        for (const short flag : {short{0}, short{1}}) {
            tw.world().new_specials = flag;
            for (int slot = 1; slot <= 4; ++slot) {
                const bool hidden_slot = flag == 0 && (slot == 2 || slot == 4);
                for (const bool shifted : {false, true})
                    EXPECT_EQ(hidden_slot ? 5000 : 0,
                              og::sim::cast_cost(*w, slot, shifted))
                        << "mark " << int{mark} << " flag " << flag
                        << " slot " << slot << " shifted " << shifted;
            }
        }
        w->set_kit_state(0);
    }
    // A mark that is neither (FEARLESS) pays the ordinary price.
    tw.world().new_specials = 1;
    w->set_kit_state(KIT_FEARLESS);
    EXPECT_EQ(35, og::sim::cast_cost(*w, 1, false));
    w->set_kit_state(0);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Every name the setting can reveal fits the HUD's special box. The rule's
// own teeth are checked first (METEOR SHOWER is why the elemental's special
// is spelled METEOR RAIN), then every installed family's new-kit names.
TEST(SpecialsView, special_names_fit_the_hud_box)
{
    EXPECT_FALSE(fits_hud_box("METEOR SHOWER"));
    EXPECT_TRUE(fits_hud_box("METEOR RAIN"));
    for (int family = 0; family < NUM_FAMILY_SLOTS; ++family) {
        const FamilyDescriptor* fd = get_family_descriptor(family);
        if (fd == nullptr)
            continue;
        for (int slot = 1; slot < FD_NUM_SPECIALS; ++slot) {
            if (fd->special_new_kit[slot]) {
                EXPECT_TRUE(fits_hud_box(fd->special_names[slot]))
                    << "family " << family << " slot " << slot << ": "
                    << name_or_empty(fd->special_names[slot]);
            }
            if (fd->special_new_kit[slot] || fd->alternate_new_kit[slot]) {
                EXPECT_TRUE(fits_hud_box(fd->alternate_names[slot]))
                    << "family " << family << " slot " << slot << ": "
                    << name_or_empty(fd->alternate_names[slot]);
            }
        }
    }
}

// The declaration keys refuse junk with an error that names the entry.
TEST(SpecialsView, declaration_keys_reject_junk)
{
    og::test::ScopedPackStoreState pack_store;
    const auto expect_refused = [](const std::string& entry,
                                   const std::string& fragment) {
        ClasspackData data;
        const og::script::DeclareResult r = declare(
            "og.family('living', { id = 'kitprobe:junk', wire_id = 'auto',\n"
            "  specials = { " + entry + " } })\n",
            data);
        EXPECT_FALSE(r.ok) << entry;
        EXPECT_NE(std::string::npos, r.error.find("'x'"))
            << "the error names the entry: " << r.error;
        EXPECT_NE(std::string::npos, r.error.find(fragment))
            << "the error says what is wrong: " << r.error;
    };
    expect_refused("{ id = 'x', name = 'X', mp_cost = 1, new_kit = 'yes' }",
                   "new_kit");
    expect_refused("{ id = 'x', name = 'X', mp_cost = 1,"
                   " alternate = { name = 'Y', mp_cost = 5000 } }",
                   "alternate.mp_cost 5000");
    expect_refused("{ id = 'x', name = 'X', mp_cost = 1,"
                   " alternate = { name = 'Y', mp_cost = -1 } }",
                   "cannot cost negative magic");
    expect_refused("{ id = 'x', name = 'X', mp_cost = 1,"
                   " alternate = { name = 'Y', new_kit = 3 } }",
                   "new_kit");
    expect_refused("{ id = 'x', name = 'X', mp_cost = 1,"
                   " alternate = { name = 'Y', mp_cot = 3 } }",
                   "unknown key 'mp_cot'");

    // The accepted spellings land on the entry.
    ClasspackData good;
    const og::script::DeclareResult ok = declare(kProbeDecl, good);
    ASSERT_TRUE(ok.ok) << ok.error;
    ASSERT_EQ(1u, good.living.size());
    const auto& list = *good.living[0].specials;
    ASSERT_EQ(4u, list.size());
    EXPECT_FALSE(list[0].new_kit);
    EXPECT_TRUE(list[0].alternate_new_kit);
    EXPECT_EQ(70, list[0].alternate_cost);
    EXPECT_TRUE(list[1].new_kit);
    EXPECT_FALSE(list[1].alternate_new_kit);
    EXPECT_EQ(90, list[1].alternate_cost);
    EXPECT_EQ(0, list[2].alternate_cost) << "no mp_cost = the primary's";

    // Weapon scenery keys: a negative rally radius is refused; both keys
    // reach the entry.
    ClasspackData bad_weapon;
    const og::script::DeclareResult bad = declare(
        "og.family('weapon', { id = 'kitprobe:pole', wire_id = 'auto',\n"
        "  rally_radius = -4 })\n",
        bad_weapon);
    EXPECT_FALSE(bad.ok);
    EXPECT_NE(std::string::npos, bad.error.find("rally_radius"))
        << bad.error;
    ClasspackData weapon;
    const og::script::DeclareResult wok = declare(
        "og.family('weapon', { id = 'kitprobe:pole', wire_id = 'auto',\n"
        "  rally_radius = 96, blocks_placement = true })\n",
        weapon);
    ASSERT_TRUE(wok.ok) << wok.error;
    ASSERT_EQ(1u, weapon.weapons.size());
    EXPECT_EQ(96, weapon.weapons[0].rally_radius.value_or(-1));
    EXPECT_TRUE(weapon.weapons[0].blocks_placement.value_or(false));
}

// Special cycling (SwitchSpecial) treats a slot the setting hides exactly
// as a NONE slot: the cycling rule wraps to slot 1 on reaching one. With
// the setting off a level-10 caster of the probe (slot 2 hidden) never
// leaves slot 1; with it on it cycles 1 -> 2 -> 3 -> 4 -> 1.
TEST(SpecialsView, cycle_skips_hidden_new_kit_slots)
{
    og::test::ScopedHookFailureGuard guard;
    ProbeFamily probe;
    ASSERT_EQ(1, probe.installed());
    TestGameWorld tw;
    walker* w = add_caster(tw, FAMILY_GOLEM, 10);
    ASSERT_NE(nullptr, w);

    const auto cycle = [&](short flag, int presses) {
        tw.world().new_specials = flag;
        w->set_current_special(1);
        std::vector<int> seen;
        for (int i = 0; i < presses; ++i) {
            sim_advance_current_special(tw.world(), *w);
            seen.push_back(static_cast<int>(w->current_special()));
        }
        return seen;
    };
    EXPECT_EQ((std::vector<int>{1, 1, 1, 1}), cycle(0, 4));
    EXPECT_EQ((std::vector<int>{2, 3, 4, 1}), cycle(1, 4));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The weapon declaration's blocks_placement reaches the descriptor and the
// placement predicate; a classic weapon and a non-weapon never block.
TEST(SpecialsView, blocks_placement_reaches_the_predicate)
{
    og::test::ScopedPackStoreState pack_store;
    ClasspackData data;
    const og::script::DeclareResult r = declare(
        "og.family('weapon', { id = 'kitprobe:wall', wire_id = 'auto',\n"
        "  blocks_placement = true, rally_radius = 64 })\n",
        data);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(1, og::resources::install_classpack_data(std::move(data)));
    const int wall_id =
        og::families::resolve_family_string_id(Order::Weapon, "kitprobe:wall");
    ASSERT_GE(wall_id, 22) << "a pack weapon takes the weapon auto floor";
    const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(wall_id);
    ASSERT_NE(nullptr, wfd);
    EXPECT_TRUE(wfd->blocks_placement);
    EXPECT_EQ(64, wfd->rally_radius);
    EXPECT_FALSE(get_weapon_family_descriptor(FAMILY_KNIFE)->blocks_placement);

    TestGameWorld tw;
    // A pack weapon with no art of its own: the world's loader only knows
    // the families that were installed when it was built, so the walker is
    // made as a knife and re-tagged, which is all the predicate reads.
    walker* wall = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, wall);
    wall->set_order_family(Order::Weapon, static_cast<char>(wall_id));
    EXPECT_EQ(Order::Weapon, wall->query_order());
    EXPECT_EQ(wall_id, static_cast<int>(static_cast<unsigned char>(wall->family())));
    EXPECT_TRUE(og::sim::declares_blocks_placement(*wall));
    walker* knife = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    EXPECT_FALSE(og::sim::declares_blocks_placement(*knife));
    walker* soldier = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    EXPECT_FALSE(og::sim::declares_blocks_placement(*soldier));
    reset_all_registry_mod_slots();
}

// ---------------------------------------------------------------------------
// Lua: the setting, the kit state and the kit marker helper module
// ---------------------------------------------------------------------------

namespace {

// Registers `body` as core:soldier's do_special (the binding tests' shape:
// a pack script whose hook runs the Lua under test) and dispatches it once.
class KitLuaTest : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override { og::script::clear_pack_scripts(); }

    static std::optional<SpecialResult> run(const std::string& prelude,
                                            const std::string& body,
                                            walker* self)
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.kit", "kit_probe.lua",
             prelude +
                 "\nog.register_hooks('living', 'core:soldier', {\n"
                 "  do_special = function(self)\n" +
                 body +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
        return og::script::hooks::do_special(
            get_family_descriptor(FAMILY_SOLDIER), self);
    }

    static std::string last_error()
    {
        const auto& errors = og::script::active_world_scripts().host().errors();
        return errors.empty() ? std::string("(no script error recorded)")
                              : errors.back().message;
    }
};

}  // namespace

// walker:kit_state()/set_kit_state(), hidden()/set_hidden(),
// possess_link(), possess_ticks() and og.C.KIT_* read and write the same
// C++ fields; set_kit_state routes a HIDDEN flip through set_hidden so the
// collision table follows.
TEST_F(KitLuaTest, kit_state_bindings_read_and_write_the_walker)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->setxy(64, 64);
    self->set_possess_link(77u);
    self->set_possess_ticks(12);
    ASSERT_EQ(1u, tw.world().myobmap->walker_to_pos.count(self));

    const auto handled = run(
        "",
        "    if og.C.KIT_HIDDEN ~= 1 or og.C.KIT_FEARLESS ~= 2\n"
        "        or og.C.KIT_WARD ~= 4 or og.C.KIT_CHANNEL ~= 8 then\n"
        "      error('kit constants')\n"
        "    end\n"
        "    if self:possess_link() ~= 77 or self:possess_ticks() ~= 12 then\n"
        "      error('possession reads')\n"
        "    end\n"
        "    if self:hidden() then\n"
        "      error('starts visible')\n"
        "    end\n"
        "    self:set_kit_state(og.C.KIT_FEARLESS + og.C.KIT_HIDDEN)\n"
        "    if not self:hidden() or self:kit_state() ~= 3 then\n"
        "      error('set_kit_state')\n"
        "    end\n"
        "    self:set_hidden(false)\n"
        "    if self:hidden() or self:kit_state() ~= 2 then\n"
        "      error('set_hidden false keeps the other bits')\n"
        "    end\n"
        "    self:set_hidden(1)",
        self);
    ASSERT_TRUE(handled.has_value()) << last_error();
    EXPECT_TRUE(*handled);
    EXPECT_TRUE(self->hidden());
    EXPECT_EQ(KIT_HIDDEN | KIT_FEARLESS, self->kit_state());
    EXPECT_EQ(0u, tw.world().myobmap->walker_to_pos.count(self))
        << "a walker hidden from Lua leaves the collision table";
    self->set_hidden(false);
    EXPECT_EQ(1u, tw.world().myobmap->walker_to_pos.count(self))
        << "and re-enters it when revealed";
    // set_kit_state from Lua reveals through set_hidden too.
    self->set_hidden(true);
    const auto reveal = run("", "    self:set_kit_state(0)", self);
    ASSERT_TRUE(reveal.has_value()) << last_error();
    EXPECT_FALSE(self->hidden());
    EXPECT_EQ(1u, tw.world().myobmap->walker_to_pos.count(self));
}

// set_kit_state cannot hide a possessed body. walker::set_hidden turns away
// the hide of a host whose rider is riding it, and the raw byte the binding
// writes afterwards must land the HIDDEN bit as set_hidden left it, or a
// kit could hide the host through the back door. The other bits still land.
TEST_F(KitLuaTest, set_kit_state_cannot_hide_a_possessed_body)
{
    TestGameWorld tw;
    walker* host = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* rider = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, host);
    ASSERT_NE(nullptr, rider);
    host->setxy(64, 64);
    rider->setxy(64, 64);
    rider->set_hidden(true);
    host->set_possess_link(rider->entity_id());
    rider->set_possess_link(host->entity_id());
    ASSERT_TRUE(rider->hidden());
    ASSERT_EQ(1u, tw.world().myobmap->walker_to_pos.count(host));

    const std::string body =
        "    self:set_kit_state(og.C.KIT_FEARLESS + og.C.KIT_HIDDEN)";
    const auto linked = run("", body, host);
    ASSERT_TRUE(linked.has_value()) << last_error();
    EXPECT_FALSE(host->hidden()) << "the possessed body stays visible";
    EXPECT_EQ(KIT_FEARLESS, host->kit_state())
        << "the refused HIDDEN bit is not written, the other bits are";
    EXPECT_EQ(1u, tw.world().myobmap->walker_to_pos.count(host))
        << "and the body stays in the collision table";

    // Once the possession is over the same call hides it as usual.
    host->set_possess_link(0);
    rider->set_possess_link(0);
    const auto free_body = run("", body, host);
    ASSERT_TRUE(free_body.has_value()) << last_error();
    EXPECT_TRUE(host->hidden());
    EXPECT_EQ(KIT_HIDDEN | KIT_FEARLESS, host->kit_state());
    EXPECT_EQ(0u, tw.world().myobmap->walker_to_pos.count(host));
}

// og.match_setting("new_specials") answers the world's flag, and
// walker:alternate_down() is the shift AND the current slot's alternate in
// play: false while the setting hides a new-kit alternate, true for a
// classic one (the cleric's MACE shape) either way.
TEST_F(KitLuaTest, match_setting_and_alternate_down_follow_the_setting)
{
    ProbeFamily probe;
    ASSERT_EQ(1, probe.installed());
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    walker* golem = add_caster(tw, FAMILY_GOLEM);
    ASSERT_NE(nullptr, golem);
    self->set_foe(golem);

    const std::string body =
        "    local g = self:foe()\n"
        "    self.level = og.match_setting('new_specials')\n"
        "    g:set_current_special(1)\n"
        "    if g:alternate_down() then\n"
        "      self:set_lineofsight(1)\n"
        "    else\n"
        "      self:set_lineofsight(0)\n"
        "    end\n"
        "    g:set_current_special(3)\n"
        "    if g:alternate_down() then\n"
        "      self:set_weapons_left(1)\n"
        "    else\n"
        "      self:set_weapons_left(0)\n"
        "    end";
    for (const short flag : {short{0}, short{1}}) {
        tw.world().new_specials = flag;
        golem->set_shifter_down(1);
        const auto handled = run("", body, self);
        ASSERT_TRUE(handled.has_value()) << last_error();
        EXPECT_EQ(flag, self->stats()->level())
            << "og.match_setting('new_specials')";
        EXPECT_EQ(flag, self->lineofsight())
            << "MINE (new-kit alternate) is down only with the setting on";
        EXPECT_EQ(1, self->weapons_left())
            << "a classic alternate is down whenever the shift is";
    }
    golem->set_shifter_down(0);
    tw.world().new_specials = 1;
    const auto unshifted = run("", body, self);
    ASSERT_TRUE(unshifted.has_value()) << last_error();
    EXPECT_EQ(0, self->lineofsight()) << "no shift, no alternate";
    EXPECT_EQ(0, self->weapons_left());
}

// lib/kit_marker.lua: spawn makes an invisible helper owned by its caster,
// carrying its role in ani_type; find answers the caster's live marker of a
// role; age counts ticks since the spawn; the marker's on_act hands each
// tick to its role's handler, and a marker whose role has no handler dies.
TEST_F(KitLuaTest, kit_marker_spawns_finds_ages_and_dispatches_by_role)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().tick_count_ = 100;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->setxy(64, 64);
    const int marker_family = og::families::resolve_family_string_id(
        Order::FX, "core:kit_marker");
    ASSERT_EQ(17, marker_family) << "core:kit_marker is effect wire id 17";

    // Role 9: count down the marker's own lifetime, die at zero (the
    // shape every real role handler follows). Role 8: no handler.
    const std::string prelude =
        "local km = og.use('core:kit_marker')\n"
        "km.register(9, function(marker)\n"
        "  marker.lifetime = marker:lifetime() - 1\n"
        "  if marker:lifetime() <= 0 then\n"
        "    marker.dead = 1\n"
        "    marker:death()\n"
        "  end\n"
        "end)\n";
    const auto spawned = run(
        prelude,
        "    if km.find(self, 9) ~= nil then\n"
        "      error('no marker yet')\n"
        "    end\n"
        "    local m = km.spawn(self, 9, 3)\n"
        "    if km.find(self, 9) ~= m or km.find(self, 8) ~= nil then\n"
        "      error('find by owner and role')\n"
        "    end\n"
        "    if km.age(m) ~= 0 or km.BURROW ~= 1 or km.METEOR_RAIN ~= 5 then\n"
        "      error('age and roles')\n"
        "    end\n"
        "    km.spawn(self, 8, 0)",
        self);
    ASSERT_TRUE(spawned.has_value()) << last_error();

    std::vector<walker*> markers;
    for (auto& uptr : tw.world().oblist)
        if (uptr && uptr->query_order() == Order::FX &&
            uptr->family() == marker_family)
            markers.push_back(uptr.get());
    ASSERT_EQ(2u, markers.size()) << "spawn puts markers in the oblist";
    walker* timed = markers[0]->ani_type() == 9 ? markers[0] : markers[1];
    walker* orphan = timed == markers[0] ? markers[1] : markers[0];
    EXPECT_EQ(9, timed->ani_type());
    EXPECT_EQ(8, orphan->ani_type());
    EXPECT_EQ(self, timed->owner());
    EXPECT_EQ(self->team_num(), timed->team_num());
    EXPECT_EQ(3, timed->lifetime());
    EXPECT_TRUE(timed->ignore());

    // The age reads the world tick.
    tw.world().tick_count_ = 107;
    const auto aged = run(prelude,
                          "    self:set_lineofsight(km.age(km.find(self, 9)))",
                          self);
    ASSERT_TRUE(aged.has_value()) << last_error();
    EXPECT_EQ(7, self->lineofsight());

    // Tick both markers through the engine's own effect act.
    EXPECT_TRUE(orphan->act());
    EXPECT_TRUE(orphan->dead()) << "a role with no handler dies at once";
    for (int t = 0; t < 2; ++t) {
        EXPECT_TRUE(timed->act());
        EXPECT_FALSE(timed->dead()) << "tick " << t;
    }
    EXPECT_TRUE(timed->act());
    EXPECT_TRUE(timed->dead()) << "the handler ran its countdown out";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
