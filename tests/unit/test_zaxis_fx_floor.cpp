/* A8 regression tests: FX / child-entity spawns inherit their source's floor,
 * and explosion blast damage never crosses floors.
 *
 * Before the fix, nearly every FX spawn site left the child on the default
 * floor 0 regardless of the source's floor, so explosions rendered (and
 * damaged!) on the wrong floor. All of this is gated by data (floor fields),
 * so single-floor levels are byte-identical — covered by og_test_parity.
 */
#include "../test_game_world_fixture.h"

#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/summon.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/constants.h>

#include <gtest/gtest.h>
#include "test_family_hook_dispatch.h"

namespace {

// Count live oblist walkers of an order+family, returning the last one seen.
walker* find_fx(GameWorld& w, int family, int* count = nullptr)
{
    walker* found = nullptr;
    int n = 0;
    for (auto& uptr : w.oblist)
    {
        walker* ob = uptr.get();
        if (ob && !ob->dead() && ob->family() == family)
        {
            found = ob;
            ++n;
        }
    }
    if (count)
        *count = n;
    return found;
}

} // namespace

TEST(ZAxisFxFloor, summon_entity_inherits_summoner_floor)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(3);

    walker* caster = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(caster, nullptr);
    caster->set_floor(2);
    caster->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);

    walker* fx = summon_entity(caster, Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(fx, nullptr);
    EXPECT_EQ(2, fx->floor())
        << "summon_entity must place the child on the summoner's floor";
    EXPECT_EQ(caster->team_num(), fx->team_num());
}

TEST(ZAxisFxFloor, bomb_detonation_spawns_explosion_on_bomb_floor)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    // A thief's armed bomb sits on floor 1 (as family_thief now authors it).
    walker* owner = w.add_ob(Order::Living, FAMILY_THIEF);
    ASSERT_NE(owner, nullptr);
    owner->set_floor(1);
    owner->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);
    owner->stats()->set_level(3);

    walker* bomb = w.add_ob(Order::FX, FAMILY_BOMB);
    ASSERT_NE(bomb, nullptr);
    bomb->set_owner(owner);
    bomb->set_floor(1);
    bomb->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);
    bomb->set_damage(30.0f);

    const auto* fd = get_effect_family_descriptor(FAMILY_BOMB);
    ASSERT_NE(fd, nullptr);
    ASSERT_TRUE(og::test::has_on_death(*fd));
    ASSERT_TRUE(og::test::on_death(*fd, static_cast<effect*>(bomb)));

    walker* boom = find_fx(w, FAMILY_EXPLOSION);
    ASSERT_NE(boom, nullptr) << "detonation must spawn a FAMILY_EXPLOSION";
    EXPECT_EQ(1, boom->floor())
        << "the explosion child must inherit the bomb's floor";
}

TEST(ZAxisFxFloor, explosion_damage_never_crosses_floors)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* owner = w.add_ob(Order::Living, FAMILY_THIEF);
    ASSERT_NE(owner, nullptr);
    owner->set_floor(1);
    owner->setxy(12 * GRID_SIZE, 12 * GRID_SIZE); // far from the blast
    owner->stats()->set_level(3);
    owner->set_team_num(0);

    // Victim on the blast floor and a bystander directly below it.
    walker* same_floor = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* below = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(same_floor, nullptr);
    ASSERT_NE(below, nullptr);
    same_floor->set_floor(1);
    same_floor->setxy(6 * GRID_SIZE + 4, 6 * GRID_SIZE);
    same_floor->set_team_num(1);
    below->set_floor(0);
    below->setxy(6 * GRID_SIZE + 4, 6 * GRID_SIZE); // same x/y, floor below
    below->set_team_num(1);

    const float same_hp = same_floor->stats()->hitpoints();
    const float below_hp = below->stats()->hitpoints();

    walker* boom = w.add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(boom, nullptr);
    boom->set_owner(owner);
    boom->set_floor(1);
    boom->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);
    boom->set_damage(20.0f);
    boom->set_team_num(0);

    const auto* fd = get_effect_family_descriptor(FAMILY_EXPLOSION);
    ASSERT_NE(fd, nullptr);
    ASSERT_TRUE(og::test::has_on_death(*fd));
    og::test::on_death(*fd, static_cast<effect*>(boom));

    EXPECT_LT(same_floor->stats()->hitpoints(), same_hp)
        << "a same-floor victim inside the blast radius must take damage";
    EXPECT_EQ(below_hp, below->stats()->hitpoints())
        << "a walker on another floor at the same x/y must take NO damage";
}

TEST(ZAxisFxFloor, generator_death_explosions_inherit_generator_floor)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* gen = w.add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_NE(gen, nullptr);
    gen->set_floor(1);
    gen->set_sizex(32);
    gen->set_sizey(32);
    gen->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);
    gen->stats()->set_level(2);

    gen->set_dead(1);
    gen->death();

    int count = 0;
    walker* boom = find_fx(w, FAMILY_EXPLOSION, &count);
    ASSERT_GE(count, 1) << "generator death must go up in flames";
    for (auto& uptr : w.oblist)
    {
        walker* ob = uptr.get();
        if (ob && !ob->dead() && ob->family() == FAMILY_EXPLOSION)
        {
            EXPECT_EQ(1, ob->floor())
                << "generator death explosion spawned on the wrong floor";
        }
    }
    (void)boom;
}

TEST(ZAxisFxFloor, delayed_spawn_wake_flash_inherits_wakers_floor)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    // Keep the level alive: one player-team walker.
    walker* hero = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(hero, nullptr);
    hero->setxy(2 * GRID_SIZE, 2 * GRID_SIZE);
    hero->set_team_num(0);

    walker* invader = w.add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(invader, nullptr);
    invader->set_floor(1);
    invader->setxy(10 * GRID_SIZE, 10 * GRID_SIZE);
    invader->set_team_num(1);
    invader->set_spawn_delay(1);
    invader->set_dormant(true);

    w.tick(); // tick 1: still within the delay
    w.tick(); // tick 2 > delay 1: activation + flash

    ASSERT_FALSE(invader->dormant());
    walker* flash = find_fx(w, FAMILY_FLASH);
    ASSERT_NE(flash, nullptr) << "activation must emit the teleport-in flash";
    EXPECT_EQ(1, flash->floor())
        << "the wake flash must appear on the waking walker's floor";
}
