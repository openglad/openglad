#include <openglad/interface/ui/results_screen.h>
#include <gtest/gtest.h>

TEST(ResultsScreenInternalHelper, exercises_core_paths)
{
    ASSERT_EQ(25, results_screen_test_exercise_internal())
        << "every internal postcondition must run; failures report a negative check index";
}

#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/packs.h>
#include "test_ability_select_scope.h"

#include <memory>
#include <string>
#include <vector>

// results_screen_internal.inc
std::vector<std::string> results_screen_testing_gained_specials(guy* before,
                                                                walker* after);

namespace {

// A troop of `family` that went from `from` to `to` this mission: the guy
// as it entered and the walker that carries its guy out.
struct LevelledTroop
{
    guy before;
    std::unique_ptr<walker> after;

    LevelledTroop(unsigned char family, short from, short to)
        : before(family)
    {
        before.name = "Troop";
        before.upgrade_to_level(from, true);
        guy out(family);
        out.name = "Troop";
        out.upgrade_to_level(to, true);
        after = guy_create_walker_owned(out,
                                        og::runtime::current_session->myscreen_);
    }

    std::vector<std::string> gained()
    {
        return results_screen_testing_gained_specials(&before, after.get());
    }
};

} // namespace

// New Ability Select: a special that opens while it is not on the hero's
// belt is announced with " (ON THE BENCH)" so the player knows to put it on
// a key; by default only the Archmage's MIND CONTROL (level 10) is benched.
// With MIND CONTROL placed on key 3, or with the setting off, the plain
// name. The loop reads every slot, 1..5: the mage's level-13 HEARTBURST
// (slot 5) is announced.
//
// RED: the bench test dropped (no suffix); the slot loop back at `<= 4`
// (no HEARTBURST).
TEST(ResultsScreenInternalHelper, bench_note_when_a_new_special_is_off_the_belt)
{
    {
        ScopedAbilitySelect on(true);
        LevelledTroop archmage(FAMILY_ARCHMAGE, 9, 10);
        ASSERT_NE(nullptr, archmage.after);
        EXPECT_EQ((std::vector<std::string>{"MIND CONTROL (ON THE BENCH)"}),
                  archmage.gained());

        archmage.after->myguy->belt[2] =
            og::sim::belt::encode(og::sim::belt::Entry{4, false});
        EXPECT_EQ((std::vector<std::string>{"MIND CONTROL"}), archmage.gained())
            << "MIND CONTROL on key 3 is not on the bench";
    }
    {
        ScopedAbilitySelect off;
        LevelledTroop archmage(FAMILY_ARCHMAGE, 9, 10);
        EXPECT_EQ((std::vector<std::string>{"MIND CONTROL"}), archmage.gained())
            << "no bench with the setting off";
    }
    {
        ScopedAbilitySelect on(true);
        LevelledTroop mage(FAMILY_MAGE, 12, 13);
        EXPECT_EQ((std::vector<std::string>{"HEARTBURST"}), mage.gained())
            << "slot 5 opens at level 13 and is announced";
    }
}

namespace {

// The five family registries are process-global: the pack class this test
// installs is freed on the way in and on every way out.
struct HelperPackFamilyGuard
{
    HelperPackFamilyGuard() { reset_all_registry_mod_slots(); }
    ~HelperPackFamilyGuard() { reset_all_registry_mod_slots(); }
    HelperPackFamilyGuard(const HelperPackFamilyGuard&) = delete;
    HelperPackFamilyGuard& operator=(const HelperPackFamilyGuard&) = delete;
};

} // namespace

// #500: a troop of a pack class from 128 up announces its own class's special
// when it reaches the slot's level. TroopResult kept the class in a signed
// byte until #500, so class 200 read -56, found no family and announced
// nothing.
TEST(ResultsScreenInternalHelper, a_class_200_troop_gains_its_own_special)
{
    HelperPackFamilyGuard pack_guard;
    {
        og::data::ClasspackData data;
        data.pack = "resultshelper";
        og::data::ClasspackLivingEntry e;
        e.id = "test:highborn";
        e.wire_id = "200";
        e.name = "HIGHBORN";
        og::data::ClasspackCostsBlock costs;
        costs.hire = 250;
        e.costs = costs;
        e.default_weapon = "core:knife";
        og::data::ClasspackSpecialEntry fire;
        fire.id = "highfire";
        fire.name = "HIGHFIRE";
        fire.mp_cost = 20;
        fire.slot = 2;  // C++ harvests spell the slot; the Lua parser derives it
        e.specials = std::vector<og::data::ClasspackSpecialEntry>{fire};
        data.living.push_back(std::move(e));
        ASSERT_EQ(1, og::resources::install_classpack_data(std::move(data)));
    }
    ASSERT_NE(nullptr, get_family_descriptor(200));

    ScopedAbilitySelect off;
    guy before(FAMILY_SOLDIER);
    before.name = "High";
    before.family = 200;
    before.upgrade_to_level(3, true);
    guy out(before);
    out.upgrade_to_level(4, true);
    // The body stands as a soldier (no art is loaded for 200 here); its guy
    // is a copy of `out`, class 200.
    const std::unique_ptr<walker> after =
        guy_create_walker_owned(out, og::runtime::current_session->myscreen_);
    ASSERT_NE(nullptr, after);
    ASSERT_NE(nullptr, after->myguy);
    EXPECT_EQ((std::vector<std::string>{"HIGHFIRE"}),
              results_screen_testing_gained_specials(&before, after.get()))
        << "slot 2 opens at level 4 (3*2-2) and names the class's own special";
}
