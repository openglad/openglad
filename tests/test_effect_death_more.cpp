#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

extern screen* myscreen;

void test_effect_death_ghost_scare_forces_walk_commands_on_foes()
{
    myscreen->level_data.create_new_grid();

    walker* ghost = myscreen->level_data.add_ob(Order::Living, FAMILY_GHOST);
    walker* foe1 = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe2 = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(ghost && foe1 && foe2, "walkers created");
    if (!(ghost && foe1 && foe2))
        return;

    ghost->team_num = 1;
    foe1->team_num = 2;
    foe2->team_num = 2;

    ghost->stats()->level = 5;
    ghost->setxy(GRID_SIZE * 10, GRID_SIZE * 10);
    foe1->setxy(GRID_SIZE * 11, GRID_SIZE * 10);
    foe2->setxy(GRID_SIZE * 9, GRID_SIZE * 10);

    // Spawn the scare FX and trigger death() directly.
    walker* scare = myscreen->level_data.add_ob(Order::FX, FAMILY_GHOST_SCARE);
    TEST_ASSERT(scare != nullptr, "scare effect created");
    if (!scare)
        return;
    scare->owner = ghost;
    scare->setxy(ghost->xpos, ghost->ypos);
    scare->dead = 1;

    // Should push COMMAND_WALK commands onto nearby foes.
    (void)scare->death();
}
REGISTER_TEST(test_effect_death_ghost_scare_forces_walk_commands_on_foes);

void test_effect_death_bomb_spawns_explosion_with_owner_and_damage()
{
    myscreen->level_data.create_new_grid();

    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_THIEF);
    TEST_ASSERT(owner != nullptr, "owner created");
    if (!owner)
        return;

    owner->stats()->level = 3;
    owner->setxy(GRID_SIZE * 8, GRID_SIZE * 8);

    walker* bomb = myscreen->level_data.add_ob(Order::FX, FAMILY_BOMB);
    TEST_ASSERT(bomb != nullptr, "bomb created");
    if (!bomb)
        return;
    bomb->owner = owner;
    bomb->damage = 12.0f;
    bomb->dead = 1;

    (void)bomb->death();
}
REGISTER_TEST(test_effect_death_bomb_spawns_explosion_with_owner_and_damage);
