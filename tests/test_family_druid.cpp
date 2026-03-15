#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <gtest/gtest.h>

const FamilyDescriptor& describe_family_druid();

TEST(FamilyDruid, descriptor_level_up_and_difficulty)
{
    const FamilyDescriptor& desc = describe_family_druid();
    ASSERT_TRUE(desc.family_id == FAMILY_DRUID);
    ASSERT_TRUE(desc.do_special != nullptr);
    ASSERT_TRUE(desc.set_difficulty != nullptr);
    ASSERT_TRUE(desc.level_up != nullptr);

    guy g(FAMILY_DRUID);
    const short old_str = g.strength;
    const short old_int = g.intelligence;
    desc.level_up(&g, 2);
    ASSERT_TRUE(g.strength > old_str);
    ASSERT_TRUE(g.intelligence > old_int);

    living self;
    self.set_damage(0.0f);
    const float old_hp = self.stats()->max_hitpoints();
    const float old_mp = self.stats()->max_magicpoints();
    const float old_damage = self.damage();
    desc.set_difficulty(&self, 3);
    ASSERT_TRUE(self.stats()->max_hitpoints() > old_hp);
    ASSERT_TRUE(self.stats()->max_magicpoints() > old_mp);
    ASSERT_TRUE(self.damage() > old_damage);
}

TEST(FamilyDruid, do_special_reveal_and_busy_paths)
{
    const FamilyDescriptor& desc = describe_family_druid();
    living self;
    self.stats()->set_level(4);
    self.set_fire_frequency(2.0f);

    self.set_current_special(3); // reveal items
    self.set_busy(0);
    const short old_view = self.view_all();
    ASSERT_TRUE(desc.do_special(&self));
    ASSERT_TRUE(self.view_all() > old_view);
    ASSERT_TRUE(self.busy() > 0.0f);

    self.set_current_special(1); // plant tree
    self.set_busy(1);
    ASSERT_TRUE(!desc.do_special(&self));

    self.set_current_special(2); // summon faerie
    self.set_busy(1);
    ASSERT_TRUE(!desc.do_special(&self));

    self.set_current_special(4); // protection/default
    self.set_busy(1);
    ASSERT_TRUE(!desc.do_special(&self));
}
