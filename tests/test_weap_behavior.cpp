#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/weap.h>
#include <openglad/entities/family_registries.h>
#include <openglad/entities/weapon_family_descriptor.h>
#include <openglad/core/constants.h>
#include <openglad/core/terrain_types.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"
#include <memory>
#include <cstdlib>

extern screen* myscreen;

static walker* make_weapon(char family)
{
    walker* w = myscreen->level_data.add_weap_ob(Order::Weapon, family);
    if (w) {
        w->setxy(100, 100);
        w->owner = w;
    }
    return w;
}

static std::unique_ptr<walker> make_living(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w)
        w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// weap::act - various act types
// ---------------------------------------------------------------------------

void test_weap_act_fire()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_FIRE);
    w->lastx = 1;
    w->lasty = 0;
    w->act();
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_fire);

void test_weap_act_sit_tree()
{
    walker* w = make_weapon(FAMILY_TREE);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    TEST_ASSERT(result, "tree sit returns 1");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_sit_tree);

void test_weap_act_sit_blood()
{
    walker* w = make_weapon(FAMILY_BLOOD);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    TEST_ASSERT(result, "blood sit returns 1");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_sit_blood);

void test_weap_act_sit_door()
{
    walker* w = make_weapon(FAMILY_DOOR);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    TEST_ASSERT(result, "door sit returns 1");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_sit_door);

void test_weap_act_die()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_DIE);
    w->act();
    TEST_ASSERT(w->dead == 1, "weap act die sets dead");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_die);

void test_weap_act_random()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_RANDOM);
    w->act();
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_random);

// ---------------------------------------------------------------------------
// weap::death - various weapon families
// ---------------------------------------------------------------------------

void test_weap_death_knife_soldier_owner()
{
    auto owner = make_living(FAMILY_SOLDIER, 0);
    if (!owner) return;

    walker* knife = make_weapon(FAMILY_KNIFE);
    if (!knife) return;
    knife->owner = owner.get();
    knife->dead = 1;
    knife->death();
    // Should create a KNIFE_BACK effect

    myscreen->level_data.remove_ob(knife);
}
REGISTER_TEST(test_weap_death_knife_soldier_owner);

void test_weap_death_knife_non_soldier()
{
    auto owner = make_living(FAMILY_ARCHER, 0);
    if (!owner) return;

    walker* knife = make_weapon(FAMILY_KNIFE);
    if (!knife) return;
    knife->owner = owner.get();
    knife->dead = 1;
    knife->death();
    // Should NOT create a KNIFE_BACK since owner is not soldier

    myscreen->level_data.remove_ob(knife);
}
REGISTER_TEST(test_weap_death_knife_non_soldier);

void test_weap_death_fire_arrow_exploding()
{
    auto owner = make_living(FAMILY_ARCHER, 0);
	    if (!owner) return;
	    
	    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
	    if (!arrow) return;
	    arrow->owner = owner.get();
	    arrow->skip_exit = 1; // means it's supposed to explode
	    arrow->dead = 1;
	    arrow->death();

    myscreen->level_data.remove_ob(arrow);
}
REGISTER_TEST(test_weap_death_fire_arrow_exploding);

void test_weap_death_fire_arrow_no_explode()
{
    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
    if (!arrow) return;
    arrow->skip_exit = 0; // not supposed to explode
    arrow->dead = 1;
    arrow->death();
    myscreen->level_data.remove_ob(arrow);
}
REGISTER_TEST(test_weap_death_fire_arrow_no_explode);

void test_weap_death_wave_transforms()
{
    walker* wave = make_weapon(FAMILY_WAVE);
    if (!wave) return;
    wave->dead = 1;
    wave->death();
    // Should transform to WAVE2 and un-dead
    TEST_ASSERT(wave->dead == 0, "wave should un-dead on transform");
    myscreen->level_data.remove_ob(wave);
}
REGISTER_TEST(test_weap_death_wave_transforms);

void test_weap_death_wave2_transforms()
{
    walker* wave = make_weapon(FAMILY_WAVE2);
    if (!wave) return;
    wave->dead = 1;
    wave->death();
    TEST_ASSERT(wave->dead == 0, "wave2 should un-dead on transform");
    myscreen->level_data.remove_ob(wave);
}
REGISTER_TEST(test_weap_death_wave2_transforms);

void test_weap_death_door()
{
    walker* door = make_weapon(FAMILY_DOOR);
    if (!door) return;
    door->dead = 1;
    door->death();
    myscreen->level_data.remove_ob(door);
}
REGISTER_TEST(test_weap_death_door);

void test_weap_death_rock_no_bounce()
{
    walker* rock = make_weapon(FAMILY_ROCK);
    if (!rock) return;
    // do_bounce is a member of weap, not walker base
    // Just test death with default state
    rock->dead = 1;
    rock->death();
    myscreen->level_data.remove_ob(rock);
}
REGISTER_TEST(test_weap_death_rock_no_bounce);

void test_weap_death_boulder_exploding()
{
    walker* boulder = make_weapon(FAMILY_BOULDER);
    if (!boulder) return;
    boulder->skip_exit = 1;
    boulder->dead = 1;
    boulder->death();
    myscreen->level_data.remove_ob(boulder);
}
REGISTER_TEST(test_weap_death_boulder_exploding);

// ---------------------------------------------------------------------------
// weap::animate
// ---------------------------------------------------------------------------

void test_weap_animate_knife()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->ani_type = ANI_ATTACK;
    w->animate();
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_animate_knife);

void test_weap_animate_arrow()
{
    walker* w = make_weapon(FAMILY_ARROW);
    if (!w) return;
    w->ani_type = ANI_ATTACK;
    w->animate();
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_animate_arrow);

void test_weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight()
{
    myscreen->level_data.create_new_grid();
    walker* w = make_weapon(FAMILY_KNIFE);
    auto dead_living = make_living(FAMILY_SOLDIER, 1);
    TEST_ASSERT(w && dead_living, "weapon and dead living created");
    if (!(w && dead_living))
        return;

    dead_living->dead = 1;
    w->foe = dead_living.get();
    w->leader = dead_living.get();
    w->owner = dead_living.get();
    w->setxy(0, 0);
    w->lineofsight = 5;
    myscreen->level_data.grid.data[0] = PIX_TREE_M1;
    w->set_act_type(ACT_RANDOM);

    (void)w->act();
    TEST_ASSERT(w->foe == nullptr && w->leader == nullptr, "dead foe/leader should be cleared");
    TEST_ASSERT(w->owner == w, "dead owner should be cleared then default to self");
    TEST_ASSERT_EQ(4, (int)w->lineofsight, "trees tile should decrement lineofsight");

    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight);

void test_weap_act_control_generate_guard_and_default_paths()
{
    walker* control = make_weapon(FAMILY_KNIFE);
    walker* gen = make_weapon(FAMILY_KNIFE);
    walker* guard = make_weapon(FAMILY_KNIFE);
    walker* unknown = make_weapon(FAMILY_KNIFE);
    TEST_ASSERT(control && gen && guard && unknown, "weapons created");
    if (!(control && gen && guard && unknown))
        return;

    control->set_act_type(ACT_CONTROL);
    TEST_ASSERT(control->act(), "ACT_CONTROL should return true");

    gen->set_act_type(ACT_GENERATE);
    TEST_ASSERT(!gen->act(), "ACT_GENERATE path should fall through to return false");

    guard->set_act_type(ACT_GUARD);
    TEST_ASSERT(!guard->act(), "ACT_GUARD path should fall through to return false");

    unknown->set_act_type(123);
    TEST_ASSERT(!unknown->act(), "unknown act should return false");

    myscreen->level_data.remove_ob(control);
    myscreen->level_data.remove_ob(gen);
    myscreen->level_data.remove_ob(guard);
    myscreen->level_data.remove_ob(unknown);
}
REGISTER_TEST(test_weap_act_control_generate_guard_and_default_paths);

void test_weap_death_is_idempotent()
{
    walker* w = make_weapon(FAMILY_KNIFE);
    TEST_ASSERT(w != nullptr, "weapon created");
    if (!w)
        return;
    w->dead = 1;
    TEST_ASSERT(w->death(), "first death() call should succeed");
    TEST_ASSERT(!w->death(), "second death() call should short-circuit");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_weap_death_is_idempotent);

void test_weap_headless_default_ctor_and_setxy_path()
{
    weap headless;
    TEST_ASSERT_EQ(0, (int)headless.do_bounce, "default weap ctor should initialize do_bounce=0");
    TEST_ASSERT_EQ((int)Order::Weapon, (int)headless.query_order(), "headless weap should report weapon order");

    headless.setxy(12, 34);
    TEST_ASSERT_EQ(12, (int)headless.xpos, "weap::setxy override should update xpos");
    TEST_ASSERT_EQ(34, (int)headless.ypos, "weap::setxy override should update ypos");
}
REGISTER_TEST(test_weap_headless_default_ctor_and_setxy_path);

static void set_world_tile(short world_x, short world_y, unsigned char tile)
{
    auto& level = myscreen->level_data;
    const int gx = world_x / GRID_SIZE;
    const int gy = world_y / GRID_SIZE;
    if (gx < 0 || gy < 0 || gx >= level.grid.w || gy >= level.grid.h)
        return;
    level.grid.data[gx + level.grid.w * gy] = tile;
}

void test_weapon_family_rock_death_bounce_matrix()
{
    myscreen->level_data.create_new_grid();
    walker* rock_w = make_weapon(FAMILY_ROCK);
    TEST_ASSERT(rock_w != nullptr, "rock weapon created");
    if (!rock_w)
        return;
    auto* rock = static_cast<weap*>(rock_w);

    const WeaponFamilyDescriptor* rock_desc = get_weapon_family_descriptor(FAMILY_ROCK);
    TEST_ASSERT(rock_desc != nullptr && rock_desc->on_death != nullptr, "rock descriptor callback exists");
    if (!(rock_desc && rock_desc->on_death))
    {
        myscreen->level_data.remove_ob(rock_w);
        return;
    }

    rock->setxy(64, 64);
    rock->lastx = GRID_SIZE;
    rock->lasty = GRID_SIZE;
    rock->collide_ob = nullptr;
    rock->lineofsight = 1;

    // Guard: do_bounce disabled.
    rock->dead = 1;
    rock->do_bounce = 0;
    TEST_ASSERT(!rock_desc->on_death(rock), "rock on_death should short-circuit when do_bounce=0");

    // First probe passable => no bounce, die normally.
    rock->do_bounce = 1;
    rock->dead = 1;
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_GRASS1);
    TEST_ASSERT(!rock_desc->on_death(rock), "rock on_death should return false when forward tile is passable");
    TEST_ASSERT_EQ(1, (int)rock->dead, "forward-passable path should leave rock dead");

    // First blocked, second passable => bounce down-left (flip X only).
    rock->setxy(64, 64);
    rock->lastx = GRID_SIZE;
    rock->lasty = GRID_SIZE;
    rock->dead = 1;
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_GRASS1);
    TEST_ASSERT(rock_desc->on_death(rock), "rock on_death should bounce down-left");
    TEST_ASSERT_EQ(-GRID_SIZE, (int)rock->lastx, "down-left bounce should invert X velocity");
    TEST_ASSERT_EQ(GRID_SIZE, (int)rock->lasty, "down-left bounce should preserve Y velocity");

    // First+second blocked, third passable => bounce up-right (flip Y only).
    rock->setxy(64, 64);
    rock->lastx = GRID_SIZE;
    rock->lasty = GRID_SIZE;
    rock->dead = 1;
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_GRASS1);
    TEST_ASSERT(rock_desc->on_death(rock), "rock on_death should bounce up-right");
    TEST_ASSERT_EQ(GRID_SIZE, (int)rock->lastx, "up-right bounce should preserve X velocity");
    TEST_ASSERT_EQ(-GRID_SIZE, (int)rock->lasty, "up-right bounce should invert Y velocity");

    // First+second+third blocked, fourth passable => bounce up-left (flip both).
    rock->setxy(64, 64);
    rock->lastx = GRID_SIZE;
    rock->lasty = GRID_SIZE;
    rock->dead = 1;
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 - GRID_SIZE, PIX_GRASS1);
    TEST_ASSERT(rock_desc->on_death(rock), "rock on_death should bounce up-left");
    TEST_ASSERT_EQ(-GRID_SIZE, (int)rock->lastx, "up-left bounce should invert X velocity");
    TEST_ASSERT_EQ(-GRID_SIZE, (int)rock->lasty, "up-left bounce should invert Y velocity");

    // All blocked => remain dead.
    rock->setxy(64, 64);
    rock->lastx = GRID_SIZE;
    rock->lasty = GRID_SIZE;
    rock->dead = 1;
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    TEST_ASSERT(!rock_desc->on_death(rock), "rock on_death should fail when all bounce probes are blocked");
    TEST_ASSERT_EQ(1, (int)rock->dead, "all-blocked path should leave rock dead");

    myscreen->level_data.remove_ob(rock_w);
}
REGISTER_TEST(test_weapon_family_rock_death_bounce_matrix);

void test_weapon_family_animate_callbacks_and_sprinkle_hit_paths()
{
    myscreen->level_data.create_new_grid();

    walker* tree_w = make_weapon(FAMILY_TREE);
    walker* glow_w = make_weapon(FAMILY_GLOW);
    walker* circle_w = make_weapon(FAMILY_CIRCLE_PROTECTION);
    auto owner = make_living(FAMILY_SOLDIER, 0);
    TEST_ASSERT(tree_w && glow_w && circle_w && owner, "tree/glow/circle and owner created");
    if (!(tree_w && glow_w && circle_w && owner))
        return;

    // TREE/BLOOD callback: ani_type clamp + sentinel reset.
    tree_w->curdir = 0;
    tree_w->ani_type = 5;
    tree_w->cycle = 0;
    tree_w->ani[0][0] = 10;
    tree_w->ani[0][1] = -1;
    TEST_ASSERT(tree_w->animate(), "tree animate should succeed");
    TEST_ASSERT_EQ(0, (int)tree_w->ani_type, "tree animate should clamp ani_type >1 to 0");
    TEST_ASSERT_EQ(0, (int)tree_w->cycle, "tree animate should reset cycle at -1 sentinel");

    // CIRCLE_PROTECTION callback: no owner/invalid owner path.
    circle_w->owner = nullptr;
    circle_w->dead = 0;
    TEST_ASSERT(circle_w->animate(), "circle animate should still return via death handling");
    TEST_ASSERT_EQ(1, (int)circle_w->dead, "circle animate should mark dead when owner is missing");

    // CIRCLE_PROTECTION callback: valid owner centers on owner.
    circle_w->dead = 0;
    circle_w->death_called = 0;
    circle_w->owner = owner.get();
    owner->dead = 0;
    circle_w->stats()->hitpoints = 5;
    owner->setxy(180, 188);
    TEST_ASSERT(circle_w->animate(), "circle animate should succeed with valid owner");
    TEST_ASSERT(std::abs((int)circle_w->xpos - (int)owner->xpos) <= GRID_SIZE,
                "circle animate should center near owner on X");
    TEST_ASSERT(std::abs((int)circle_w->ypos - (int)owner->ypos) <= GRID_SIZE,
                "circle animate should center near owner on Y");

    // GLOW callback: illegal ani_type clamp + sentinel reset + lifetime death.
    glow_w->curdir = 0;
    glow_w->ani_type = 9;
    glow_w->cycle = 0;
    glow_w->lifetime = 0;
    glow_w->dead = 0;
    glow_w->death_called = 0;
    glow_w->ani[NUM_FACINGS * 2][0] = 12;
    glow_w->ani[NUM_FACINGS * 2][1] = -1;
    TEST_ASSERT(glow_w->animate(), "glow animate should succeed");
    TEST_ASSERT_EQ(2, (int)glow_w->ani_type, "glow animate should clamp ani_type >2 to pulse state");
    TEST_ASSERT_EQ(0, (int)glow_w->cycle, "glow animate should reset cycle at sentinel");
    TEST_ASSERT_EQ(1, (int)glow_w->dead, "glow animate should mark dead when lifetime expires");

    // SPRINKLE callback: non-living target no-op; living target with null myguy uses con=0.
    const WeaponFamilyDescriptor* sprinkle_desc = get_weapon_family_descriptor(FAMILY_SPRINKLE);
    TEST_ASSERT(sprinkle_desc != nullptr && sprinkle_desc->on_hit_target != nullptr,
                "sprinkle descriptor callback exists");
    walker* non_living_target = myscreen->level_data.add_ob(Order::Treasure, FAMILY_STAIN);
    walker* living_target = myscreen->level_data.add_ob(Order::Living, FAMILY_SLIME);
    TEST_ASSERT(non_living_target != nullptr && living_target != nullptr, "sprinkle targets created");
    if (sprinkle_desc && sprinkle_desc->on_hit_target && non_living_target && living_target)
    {
        short before_non_living = non_living_target->stats()->frozen_delay;
        TEST_ASSERT(sprinkle_desc->on_hit_target(tree_w, non_living_target, owner.get()),
                    "sprinkle hit callback should return true for non-living targets");
        TEST_ASSERT_EQ((int)before_non_living, (int)non_living_target->stats()->frozen_delay,
                       "sprinkle should not change frozen_delay for non-living targets");

        living_target->myguy = nullptr;
        living_target->stats()->frozen_delay = 0;
        owner->stats()->level = 6;
        TEST_ASSERT(sprinkle_desc->on_hit_target(tree_w, living_target, owner.get()),
                    "sprinkle hit callback should return true for living targets");
        TEST_ASSERT(living_target->stats()->frozen_delay >= 0,
                    "sprinkle should set a deterministic frozen_delay for living targets");
    }

    myscreen->level_data.remove_ob(non_living_target);
    myscreen->level_data.remove_ob(living_target);
    myscreen->level_data.remove_ob(tree_w);
    myscreen->level_data.remove_ob(glow_w);
    myscreen->level_data.remove_ob(circle_w);
}
REGISTER_TEST(test_weapon_family_animate_callbacks_and_sprinkle_hit_paths);

void test_weap_act_sit_with_non_skipping_family_and_act_animate_shortcut()
{
    walker* sit_weapon = make_weapon(FAMILY_KNIFE);
    walker* anim_weapon = make_weapon(FAMILY_ARROW);
    TEST_ASSERT(sit_weapon && anim_weapon, "weapons created");
    if (!(sit_weapon && anim_weapon))
        return;

    sit_weapon->set_act_type(ACT_SIT);
    TEST_ASSERT(sit_weapon->act(), "non-skip sit family should still return true");

    // Cover act() early return path when previous animation is still active.
    anim_weapon->ani_type = ANI_ATTACK;
    anim_weapon->cycle = 0;
    anim_weapon->curdir = 0;
    TEST_ASSERT(anim_weapon->act(), "non-walk ani_type should route through animate() and return true");

    myscreen->level_data.remove_ob(sit_weapon);
    myscreen->level_data.remove_ob(anim_weapon);
}
REGISTER_TEST(test_weap_act_sit_with_non_skipping_family_and_act_animate_shortcut);
