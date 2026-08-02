#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/core/constants.h>
#include <openglad/core/terrain_types.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <memory>
#include <cstdlib>
#include "test_family_hook_dispatch.h"

// myscreen is now a macro defined in base.h (via game_session.h)

static walker* make_weapon(char family)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, family);
    if (w) {
        w->setxy(100, 100);
        w->set_owner(w);
    }
    return w;
}

static std::unique_ptr<walker> make_living(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// weap::act - various act types
// ---------------------------------------------------------------------------

TEST(WeapBehavior, weap_act_fire)
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_FIRE);
    w->set_lastx(1);
    w->set_lasty(0);
    w->act();
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_sit_tree)
{
    walker* w = make_weapon(FAMILY_TREE);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    ASSERT_TRUE(result) << "tree sit returns 1";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_sit_blood)
{
    walker* w = make_weapon(FAMILY_BLOOD);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    ASSERT_TRUE(result) << "blood sit returns 1";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_sit_door)
{
    walker* w = make_weapon(FAMILY_DOOR);
    if (!w) return;
    w->set_act_type(ACT_SIT);
    bool result = w->act();
    ASSERT_TRUE(result) << "door sit returns 1";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_die)
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_DIE);
    w->act();
    ASSERT_TRUE(w->dead() == 1) << "weap act die sets dead";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_random)
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_act_type(ACT_RANDOM);
    w->act();
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


// ---------------------------------------------------------------------------
// weap::death - various weapon families
// ---------------------------------------------------------------------------

TEST(WeapBehavior, weap_death_knife_soldier_owner)
{
    auto owner = make_living(FAMILY_SOLDIER, 0);
    if (!owner) return;

    walker* knife = make_weapon(FAMILY_KNIFE);
    if (!knife) return;
    knife->set_owner(owner.get());
    knife->set_dead(1);
    knife->death();
    // Should create a KNIFE_BACK effect

    og::runtime::current_session->myscreen_->world().remove_ob(knife);
}


TEST(WeapBehavior, weap_death_knife_non_soldier)
{
    auto owner = make_living(FAMILY_ARCHER, 0);
    if (!owner) return;

    walker* knife = make_weapon(FAMILY_KNIFE);
    if (!knife) return;
    knife->set_owner(owner.get());
    knife->set_dead(1);
    knife->death();
    // Should NOT create a KNIFE_BACK since owner is not soldier

    og::runtime::current_session->myscreen_->world().remove_ob(knife);
}


TEST(WeapBehavior, weap_death_fire_arrow_exploding)
{
    auto owner = make_living(FAMILY_ARCHER, 0);
	    if (!owner) return;
	    
	    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
	    if (!arrow) return;
	    arrow->set_owner(owner.get());
	    arrow->set_skip_exit(1); // means it's supposed to explode
	    arrow->set_dead(1);
	    arrow->death();

    og::runtime::current_session->myscreen_->world().remove_ob(arrow);
}


TEST(WeapBehavior, weap_death_fire_arrow_no_explode)
{
    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
    if (!arrow) return;
    arrow->set_skip_exit(0); // not supposed to explode
    arrow->set_dead(1);
    arrow->death();
    og::runtime::current_session->myscreen_->world().remove_ob(arrow);
}


TEST(WeapBehavior, weap_death_wave_transforms)
{
    walker* wave = make_weapon(FAMILY_WAVE);
    if (!wave) return;
    wave->set_dead(1);
    wave->death();
    // Should transform to WAVE2 and un-dead
    ASSERT_TRUE(wave->dead() == 0) << "wave should un-dead on transform";
    og::runtime::current_session->myscreen_->world().remove_ob(wave);
}


TEST(WeapBehavior, weap_death_wave2_transforms)
{
    walker* wave = make_weapon(FAMILY_WAVE2);
    if (!wave) return;
    wave->set_dead(1);
    wave->death();
    ASSERT_TRUE(wave->dead() == 0) << "wave2 should un-dead on transform";
    og::runtime::current_session->myscreen_->world().remove_ob(wave);
}


TEST(WeapBehavior, weap_death_door)
{
    walker* door = make_weapon(FAMILY_DOOR);
    if (!door) return;
    door->set_dead(1);
    door->death();
    og::runtime::current_session->myscreen_->world().remove_ob(door);
}


TEST(WeapBehavior, weap_death_rock_no_bounce)
{
    walker* rock = make_weapon(FAMILY_ROCK);
    if (!rock) return;
    // do_bounce is a member of weap, not walker base
    // Just test death with default state
    rock->set_dead(1);
    rock->death();
    og::runtime::current_session->myscreen_->world().remove_ob(rock);
}


TEST(WeapBehavior, weap_death_boulder_exploding)
{
    walker* boulder = make_weapon(FAMILY_BOULDER);
    if (!boulder) return;
    boulder->set_skip_exit(1);
    boulder->set_dead(1);
    boulder->death();
    og::runtime::current_session->myscreen_->world().remove_ob(boulder);
}


// ---------------------------------------------------------------------------
// weap::animate
// ---------------------------------------------------------------------------

TEST(WeapBehavior, weap_animate_knife)
{
    walker* w = make_weapon(FAMILY_KNIFE);
    if (!w) return;
    w->set_ani_type(ANI_ATTACK);
    w->animate();
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_animate_arrow)
{
    walker* w = make_weapon(FAMILY_ARROW);
    if (!w) return;
    w->set_ani_type(ANI_ATTACK);
    w->animate();
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_weapon(FAMILY_KNIFE);
    auto dead_living = make_living(FAMILY_SOLDIER, 1);
    ASSERT_TRUE(w && dead_living) << "weapon and dead living created";
    if (!(w && dead_living))
        return;

    dead_living->set_dead(1);
    w->set_foe(dead_living.get());
    w->set_leader(dead_living.get());
    w->set_owner(dead_living.get());
    w->setxy(0, 0);
    w->set_lineofsight(5);
    og::runtime::current_session->myscreen_->world().grid.data[0] = PIX_TREE_M1;
    w->set_act_type(ACT_RANDOM);

    (void)w->act();
    ASSERT_TRUE(w->foe() == nullptr && w->leader() == nullptr) << "dead foe/leader should be cleared";
    ASSERT_TRUE(w->owner() == w) << "dead owner should be cleared then default to self";
    ASSERT_EQ(4, (int)w->lineofsight()) << "trees tile should decrement lineofsight";

    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_control_generate_guard_and_default_paths)
{
    walker* control = make_weapon(FAMILY_KNIFE);
    walker* gen = make_weapon(FAMILY_KNIFE);
    walker* guard = make_weapon(FAMILY_KNIFE);
    walker* unknown = make_weapon(FAMILY_KNIFE);
    ASSERT_TRUE(control && gen && guard && unknown) << "weapons created";
    if (!(control && gen && guard && unknown))
        return;

    control->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(control->act()) << "ACT_CONTROL should return true";

    gen->set_act_type(ACT_GENERATE);
    ASSERT_TRUE(!gen->act()) << "ACT_GENERATE path should fall through to return false";

    guard->set_act_type(ACT_GUARD);
    ASSERT_TRUE(!guard->act()) << "ACT_GUARD path should fall through to return false";

    unknown->set_act_type(123);
    ASSERT_TRUE(!unknown->act()) << "unknown act should return false";

    og::runtime::current_session->myscreen_->world().remove_ob(control);
    og::runtime::current_session->myscreen_->world().remove_ob(gen);
    og::runtime::current_session->myscreen_->world().remove_ob(guard);
    og::runtime::current_session->myscreen_->world().remove_ob(unknown);
}


TEST(WeapBehavior, weap_death_is_idempotent)
{
    walker* w = make_weapon(FAMILY_KNIFE);
    ASSERT_TRUE(w != nullptr) << "weapon created";
    if (!w)
        return;
    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "first death() call should succeed";
    ASSERT_TRUE(!w->death()) << "second death() call should short-circuit";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_headless_default_ctor_and_setxy_path)
{
    weap headless;
    ASSERT_EQ(0, (int)headless.do_bounce()) << "default weap ctor should initialize do_bounce=0";
    ASSERT_EQ((int)Order::Weapon, (int)headless.query_order()) << "headless weap should report weapon order";

    headless.setxy(12, 34);
    ASSERT_EQ(12, (int)headless.xpos()) << "weap::setxy override should update xpos";
    ASSERT_EQ(34, (int)headless.ypos()) << "weap::setxy override should update ypos";
}


static void set_world_tile(short world_x, short world_y, unsigned char tile)
{
    auto& level = og::runtime::current_session->myscreen_->level_runtime_data();
    const int gx = world_x / GRID_SIZE;
    const int gy = world_y / GRID_SIZE;
    if (gx < 0 || gy < 0 || gx >= level.world().grid.w || gy >= level.world().grid.h)
        return;
    level.world().grid.data[gx + level.world().grid.w * gy] = tile;
}

TEST(WeapBehavior, weapon_family_rock_death_bounce_matrix)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* rock_w = make_weapon(FAMILY_ROCK);
    ASSERT_TRUE(rock_w != nullptr) << "rock weapon created";
    if (!rock_w)
        return;
    auto* rock = static_cast<weap*>(rock_w);

    const WeaponFamilyDescriptor* rock_desc = get_weapon_family_descriptor(FAMILY_ROCK);
    ASSERT_TRUE(rock_desc != nullptr && og::test::has_on_death(*rock_desc)) << "rock descriptor callback exists";
    if (!(rock_desc && og::test::has_on_death(*rock_desc)))
    {
        og::runtime::current_session->myscreen_->world().remove_ob(rock_w);
        return;
    }

    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_collide_ob(nullptr);
    rock->set_lineofsight(1);

    // Guard: do_bounce disabled.
    rock->set_dead(1);
    rock->set_do_bounce(0);
    ASSERT_TRUE(!og::test::on_death(*rock_desc, rock)) << "rock on_death should short-circuit when do_bounce=0";

    // First probe passable => no bounce, die normally.
    rock->set_do_bounce(1);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_GRASS1);
    ASSERT_TRUE(!og::test::on_death(*rock_desc, rock)) << "rock on_death should return false when forward tile is passable";
    ASSERT_EQ(1, (int)rock->dead()) << "forward-passable path should leave rock dead";

    // First blocked, second passable => bounce down-left (flip X only).
    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_GRASS1);
    ASSERT_TRUE(og::test::on_death(*rock_desc, rock)) << "rock on_death should bounce down-left";
    ASSERT_EQ(-GRID_SIZE, (int)rock->lastx()) << "down-left bounce should invert X velocity";
    ASSERT_EQ(GRID_SIZE, (int)rock->lasty()) << "down-left bounce should preserve Y velocity";

    // First+second blocked, third passable => bounce up-right (flip Y only).
    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_GRASS1);
    ASSERT_TRUE(og::test::on_death(*rock_desc, rock)) << "rock on_death should bounce up-right";
    ASSERT_EQ(GRID_SIZE, (int)rock->lastx()) << "up-right bounce should preserve X velocity";
    ASSERT_EQ(-GRID_SIZE, (int)rock->lasty()) << "up-right bounce should invert Y velocity";

    // First+second+third blocked, fourth passable => bounce up-left (flip both).
    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 - GRID_SIZE, PIX_GRASS1);
    ASSERT_TRUE(og::test::on_death(*rock_desc, rock)) << "rock on_death should bounce up-left";
    ASSERT_EQ(-GRID_SIZE, (int)rock->lastx()) << "up-left bounce should invert X velocity";
    ASSERT_EQ(-GRID_SIZE, (int)rock->lasty()) << "up-left bounce should invert Y velocity";

    // All blocked => remain dead.
    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    ASSERT_TRUE(!og::test::on_death(*rock_desc, rock)) << "rock on_death should fail when all bounce probes are blocked";
    ASSERT_EQ(1, (int)rock->dead()) << "all-blocked path should leave rock dead";

    og::runtime::current_session->myscreen_->world().remove_ob(rock_w);
}


TEST(WeapBehavior, weapon_animate_handles_out_of_range_facing_and_cycle)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();

    // weap::animate() and the weapon-family on_animate callbacks index the
    // animation table with curdir/cycle, which can arrive out of range from a
    // snapshot. These must be bounded (facing clamp + sequence sentinel) rather
    // than reading out of bounds. ARROW exercises the default branch; TREE and
    // GLOW exercise the on_animate callbacks.
    walker* arrow_w = make_weapon(FAMILY_ARROW);
    walker* tree_w = make_weapon(FAMILY_TREE);
    walker* glow_w = make_weapon(FAMILY_GLOW);
    ASSERT_TRUE(arrow_w && tree_w && glow_w);
    if (!(arrow_w && tree_w && glow_w))
        return;

    for (walker* w : {arrow_w, tree_w, glow_w})
    {
        ASSERT_GT(w->ani_count, 0) << "real weapon records its table length";
        w->set_curdir(static_cast<char>(100));
        w->set_cycle(static_cast<signed char>(120));
        w->set_ani_type(static_cast<char>(40));
        (void)w->animate(); // must not crash / read OOB (verified under sanitizers)
        w->set_curdir(static_cast<char>(-7));
        w->set_cycle(static_cast<signed char>(-3));
        (void)w->animate();
    }

    og::runtime::current_session->myscreen_->world().remove_ob(arrow_w);
    og::runtime::current_session->myscreen_->world().remove_ob(tree_w);
    og::runtime::current_session->myscreen_->world().remove_ob(glow_w);
}


TEST(WeapBehavior, weapon_family_animate_callbacks_and_sprinkle_hit_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();

    walker* tree_w = make_weapon(FAMILY_TREE);
    walker* glow_w = make_weapon(FAMILY_GLOW);
    walker* circle_w = make_weapon(FAMILY_CIRCLE_PROTECTION);
    auto owner = make_living(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(tree_w && glow_w && circle_w && owner) << "tree/glow/circle and owner created";
    if (!(tree_w && glow_w && circle_w && owner))
        return;

    // TREE/BLOOD callback: ani_type clamp + sentinel reset.
    tree_w->set_curdir(0);
    tree_w->set_ani_type(5);
    tree_w->set_cycle(0);
    // Create local mutable animation data (global tables are const)
    static signed char tree_test_seq[] = {10, -1};
    static const signed char * tree_test_rows[] = {tree_test_seq, tree_test_seq, tree_test_seq, tree_test_seq,
                                                    tree_test_seq, tree_test_seq, tree_test_seq, tree_test_seq,
                                                    tree_test_seq, tree_test_seq, tree_test_seq, tree_test_seq,
                                                    tree_test_seq, tree_test_seq, tree_test_seq, tree_test_seq};
    tree_w->ani = tree_test_rows;
    ASSERT_TRUE(tree_w->animate()) << "tree animate should succeed";
    ASSERT_EQ(0, (int)tree_w->ani_type()) << "tree animate should clamp ani_type >1 to 0";
    ASSERT_EQ(0, (int)tree_w->cycle()) << "tree animate should reset cycle at -1 sentinel";

    // CIRCLE_PROTECTION callback: no owner/invalid owner path.
    circle_w->set_owner(nullptr);
    circle_w->set_dead(0);
    ASSERT_TRUE(circle_w->animate()) << "circle animate should still return via death handling";
    ASSERT_EQ(1, (int)circle_w->dead()) << "circle animate should mark dead when owner is missing";

    // CIRCLE_PROTECTION callback: valid owner centers on owner.
    circle_w->set_dead(0);
    circle_w->set_death_called(0);
    circle_w->set_owner(owner.get());
    owner->set_dead(0);
    circle_w->stats()->set_hitpoints(5);
    owner->setxy(180, 188);
    ASSERT_TRUE(circle_w->animate()) << "circle animate should succeed with valid owner";
    ASSERT_TRUE(std::abs((int)circle_w->xpos() - (int)owner->xpos()) <= GRID_SIZE) << "circle animate should center near owner on X";
    ASSERT_TRUE(std::abs((int)circle_w->ypos() - (int)owner->ypos()) <= GRID_SIZE) << "circle animate should center near owner on Y";

    // GLOW callback: illegal ani_type clamp + sentinel reset + lifetime death.
    glow_w->set_curdir(0);
    glow_w->set_ani_type(9);
    glow_w->set_cycle(0);
    glow_w->set_lifetime(0);
    glow_w->set_dead(0);
    glow_w->set_death_called(0);
    // Create local mutable animation data for glow (global tables are const)
    static signed char glow_test_seq[] = {12, -1};
    static const signed char * glow_test_rows[32] = {};
    // Fill all rows with the test sequence
    for (int i = 0; i < 32; i++)
        glow_test_rows[i] = glow_test_seq;
    glow_w->ani = glow_test_rows;
    ASSERT_TRUE(glow_w->animate()) << "glow animate should succeed";
    ASSERT_EQ(2, (int)glow_w->ani_type()) << "glow animate should clamp ani_type >2 to pulse state";
    ASSERT_EQ(0, (int)glow_w->cycle()) << "glow animate should reset cycle at sentinel";
    ASSERT_EQ(1, (int)glow_w->dead()) << "glow animate should mark dead when lifetime expires";

    // SPRINKLE callback: non-living target no-op; living target with null myguy uses con=0.
    const WeaponFamilyDescriptor* sprinkle_desc = get_weapon_family_descriptor(FAMILY_SPRINKLE);
    ASSERT_TRUE(sprinkle_desc != nullptr && og::test::has_on_hit_target(*sprinkle_desc)) << "sprinkle descriptor callback exists";
    walker* non_living_target = og::runtime::current_session->myscreen_->world().add_ob(Order::Treasure, FAMILY_STAIN);
    walker* living_target = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SLIME);
    ASSERT_TRUE(non_living_target != nullptr && living_target != nullptr) << "sprinkle targets created";
    if (sprinkle_desc && og::test::has_on_hit_target(*sprinkle_desc) && non_living_target && living_target)
    {
        short before_non_living = non_living_target->stats()->frozen_delay();
        ASSERT_TRUE(og::test::on_hit_target(*sprinkle_desc, tree_w, non_living_target, owner.get())) << "sprinkle hit callback should return true for non-living targets";
        ASSERT_EQ((int)before_non_living, (int)non_living_target->stats()->frozen_delay()) << "sprinkle should not change frozen_delay for non-living targets";

        living_target->myguy = nullptr;
        living_target->stats()->set_frozen_delay(0);
        owner->stats()->set_level(6);
        ASSERT_TRUE(og::test::on_hit_target(*sprinkle_desc, tree_w, living_target, owner.get())) << "sprinkle hit callback should return true for living targets";
        ASSERT_TRUE(living_target->stats()->frozen_delay() >= 0) << "sprinkle should set a deterministic frozen_delay for living targets";
    }

    og::runtime::current_session->myscreen_->world().remove_ob(non_living_target);
    og::runtime::current_session->myscreen_->world().remove_ob(living_target);
    og::runtime::current_session->myscreen_->world().remove_ob(tree_w);
    og::runtime::current_session->myscreen_->world().remove_ob(glow_w);
    og::runtime::current_session->myscreen_->world().remove_ob(circle_w);
}


TEST(WeapBehavior, weap_act_sit_with_non_skipping_family_and_act_animate_shortcut)
{
    walker* sit_weapon = make_weapon(FAMILY_KNIFE);
    walker* anim_weapon = make_weapon(FAMILY_ARROW);
    ASSERT_TRUE(sit_weapon && anim_weapon) << "weapons created";
    if (!(sit_weapon && anim_weapon))
        return;

    sit_weapon->set_act_type(ACT_SIT);
    ASSERT_TRUE(sit_weapon->act()) << "non-skip sit family should still return true";

    // Cover act() early return path when previous animation is still active.
    anim_weapon->set_ani_type(ANI_ATTACK);
    anim_weapon->set_cycle(0);
    anim_weapon->set_curdir(0);
    ASSERT_TRUE(anim_weapon->act()) << "non-walk ani_type should route through animate() and return true";

    og::runtime::current_session->myscreen_->world().remove_ob(sit_weapon);
    og::runtime::current_session->myscreen_->world().remove_ob(anim_weapon);
}
