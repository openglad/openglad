#include <openglad/interface/ui/results_screen.h>
#include <gtest/gtest.h>

TEST(ResultsScreenInternalHelper, exercises_core_paths)
{
    ASSERT_EQ(0, results_screen_test_exercise_internal())
        << "internal helper should report the first failed check as a negative index";
}

#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
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
