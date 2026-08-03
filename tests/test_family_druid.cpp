#include <openglad/gameplay/family_descriptor.h>

#include "test_family_lookup.h"
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <gtest/gtest.h>

#include "test_family_hook_dispatch.h"


// Druid behavior lives in packs/core/families/living-13-druid.lua, in the
// same chunk that declares its data. Everything here dispatches the way the
// sim does.

TEST(FamilyDruid, descriptor_level_up_and_difficulty)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_DRUID);
    ASSERT_TRUE(desc.family_id == FAMILY_DRUID);
    ASSERT_TRUE(og::test::has_do_special(desc));
    ASSERT_TRUE(og::test::has_set_difficulty(desc));
    ASSERT_TRUE(og::test::has_level_up(desc));
    ASSERT_EQ(nullptr, desc.do_special)
        << "pack-installed family behavior must have no C++ callback";

    og::test::ScopedHookFailureGuard guard;

    guy g(FAMILY_DRUID);
    const short old_str = g.strength;
    const short old_int = g.intelligence;
    og::test::level_up(desc, &g, 2);
    ASSERT_TRUE(g.strength > old_str);
    ASSERT_TRUE(g.intelligence > old_int);

    living self;
    self.set_damage(0.0f);
    const float old_hp = self.stats()->max_hitpoints();
    const float old_mp = self.stats()->max_magicpoints();
    const float old_damage = self.damage();
    og::test::set_difficulty(desc, &self, 3);
    ASSERT_TRUE(self.stats()->max_hitpoints() > old_hp);
    ASSERT_TRUE(self.stats()->max_magicpoints() > old_mp);
    ASSERT_TRUE(self.damage() > old_damage);

    ASSERT_EQ(0u, guard.count()) << guard.message();
}

TEST(FamilyDruid, do_special_reveal_and_busy_paths)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_DRUID);
    og::test::ScopedHookFailureGuard guard;
    living self;
    self.stats()->set_level(4);
    self.set_fire_frequency(2.0f);

    self.set_current_special(3); // reveal items
    self.set_busy(0);
    const short old_view = self.view_all();
    ASSERT_TRUE(og::test::do_special(desc, &self));
    ASSERT_TRUE(self.view_all() > old_view);
    ASSERT_TRUE(self.busy() > 0.0f);

    self.set_current_special(1); // plant tree
    self.set_busy(1);
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_current_special(2); // summon faerie
    self.set_busy(1);
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_current_special(4); // protection/default
    self.set_busy(1);
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    ASSERT_EQ(0u, guard.count()) << guard.message();
}
