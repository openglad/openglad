#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_druid();

OG_UNIT_TEST(test_family_druid_descriptor_level_up_and_difficulty)
{
    const FamilyDescriptor& desc = describe_family_druid();
    OG_ASSERT(desc.family_id == FAMILY_DRUID);
    OG_ASSERT(desc.do_special != nullptr);
    OG_ASSERT(desc.set_difficulty != nullptr);
    OG_ASSERT(desc.level_up != nullptr);

    guy g(FAMILY_DRUID);
    const short old_str = g.strength;
    const short old_int = g.intelligence;
    desc.level_up(&g, 2);
    OG_ASSERT(g.strength > old_str);
    OG_ASSERT(g.intelligence > old_int);

    living self;
    self.damage = 0.0f;
    const float old_hp = self.stats()->max_hitpoints;
    const float old_mp = self.stats()->max_magicpoints;
    const float old_damage = self.damage;
    desc.set_difficulty(&self, 3);
    OG_ASSERT(self.stats()->max_hitpoints > old_hp);
    OG_ASSERT(self.stats()->max_magicpoints > old_mp);
    OG_ASSERT(self.damage > old_damage);
}

OG_UNIT_TEST(test_family_druid_do_special_reveal_and_busy_paths)
{
    const FamilyDescriptor& desc = describe_family_druid();
    living self;
    self.stats()->level = 4;
    self.fire_frequency = 2.0f;

    self.current_special = 3; // reveal items
    self.busy = 0;
    const short old_view = self.view_all;
    OG_ASSERT(desc.do_special(&self));
    OG_ASSERT(self.view_all > old_view);
    OG_ASSERT(self.busy > 0.0f);

    self.current_special = 1; // plant tree
    self.busy = 1;
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 2; // summon faerie
    self.busy = 1;
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 4; // protection/default
    self.busy = 1;
    OG_ASSERT(!desc.do_special(&self));
}
