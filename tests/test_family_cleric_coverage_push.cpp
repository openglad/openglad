#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_cleric();

OG_UNIT_TEST(test_family_cleric_descriptor_difficulty_and_customize_weapon)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    OG_ASSERT(desc.family_id == FAMILY_CLERIC);
    OG_ASSERT(desc.do_special != nullptr);
    OG_ASSERT(desc.check_special_ai != nullptr);
    OG_ASSERT(desc.set_difficulty != nullptr);
    OG_ASSERT(desc.customize_weapon != nullptr);

    living self;
    living weapon;
    self.stats()->level = 3;
    weapon.lifetime = 10;
    desc.customize_weapon(&self, &weapon);
    OG_ASSERT(weapon.ani_type == ANI_GLOWGROW);
    OG_ASSERT(weapon.lifetime == 340);

    const float old_hp = self.stats()->max_hitpoints;
    const float old_mp = self.stats()->max_magicpoints;
    const float old_damage = self.damage;
    desc.set_difficulty(&self, 2);
    OG_ASSERT(self.stats()->max_hitpoints > old_hp);
    OG_ASSERT(self.stats()->max_magicpoints > old_mp);
    OG_ASSERT(self.damage > old_damage);
}

OG_UNIT_TEST(test_family_cleric_check_ai_default_and_do_special_busy_returns)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    living self;

    self.current_special = 0; // default true branch in check_special_ai
    OG_ASSERT(desc.check_special_ai(&self));

    self.shifter_down = 1;
    self.busy = 1;

    self.current_special = 1; // mystic mace branch guarded by busy
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 2; // turn undead branch guarded by busy
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 3; // turn undead high branch guarded by busy
    OG_ASSERT(!desc.do_special(&self));
}
