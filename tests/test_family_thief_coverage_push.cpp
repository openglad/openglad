#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/sim/irandom.h>
#include <openglad/core/constants.h>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_thief();

OG_UNIT_TEST(test_family_thief_descriptor_shape_and_level_up)
{
    const FamilyDescriptor& desc = describe_family_thief();
    OG_ASSERT(desc.family_id == FAMILY_THIEF);
    OG_ASSERT(desc.do_special != nullptr);
    OG_ASSERT(desc.check_special_ai != nullptr);
    OG_ASSERT(desc.level_up != nullptr);

    guy g(FAMILY_THIEF);
    const short old_str = g.strength;
    const short old_dex = g.dexterity;
    const short old_con = g.constitution;
    const short old_int = g.intelligence;
    const short old_armor = g.armor;

    desc.level_up(&g, 2);
    OG_ASSERT(g.strength > old_str);
    OG_ASSERT(g.dexterity > old_dex);
    OG_ASSERT(g.constitution > old_con);
    OG_ASSERT(g.intelligence > old_int);
    OG_ASSERT(g.armor > old_armor);
}

OG_UNIT_TEST(test_family_thief_check_special_ai_foe_distance_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    living self;
    living foe;
    self.current_special = 1;
    self.foe = &foe;

    self.setxy(0, 0);
    foe.setxy(60, 0); // >35 and <130 => false
    OG_ASSERT(!desc.check_special_ai(&self));

    foe.setxy(10, 0); // <=35 => true
    OG_ASSERT(desc.check_special_ai(&self));

    self.current_special = 2; // default branch => true
    OG_ASSERT(desc.check_special_ai(&self));
}

OG_UNIT_TEST(test_family_thief_do_special_busy_and_cloak_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    living self;
    FixedRandom rng(7);
    self.sim_rng = &rng;
    self.stats()->level = 3;

    self.current_special = 2; // cloak
    self.invisibility_left = 0;
    OG_ASSERT(desc.do_special(&self));
    OG_ASSERT(self.invisibility_left > 0);

    self.current_special = 3; // taunt/charm
    self.busy = 1;
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 4; // poison cloud/default
    self.busy = 1;
    OG_ASSERT(!desc.do_special(&self));
}
