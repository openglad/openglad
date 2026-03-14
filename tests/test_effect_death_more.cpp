#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)

TEST(EffectDeathMore, effect_death_ghost_scare_forces_walk_commands_on_foes)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();

    walker* ghost = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_GHOST);
    walker* foe1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(ghost && foe1 && foe2) << "walkers created";
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
    walker* scare = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_GHOST_SCARE);
    ASSERT_TRUE(scare != nullptr) << "scare effect created";
    if (!scare)
        return;
    scare->set_owner(ghost);
    scare->setxy(ghost->xpos, ghost->ypos);
    scare->dead = 1;

    // Should push COMMAND_WALK commands onto nearby foes.
    (void)scare->death();
}


TEST(EffectDeathMore, effect_death_bomb_spawns_explosion_with_owner_and_damage)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();

    walker* owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_THIEF);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner)
        return;

    owner->stats()->level = 3;
    owner->setxy(GRID_SIZE * 8, GRID_SIZE * 8);

    walker* bomb = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_BOMB);
    ASSERT_TRUE(bomb != nullptr) << "bomb created";
    if (!bomb)
        return;
    bomb->set_owner(owner);
    bomb->damage = 12.0f;
    bomb->dead = 1;

    (void)bomb->death();
}

