#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/render/walker_draw.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    w->setxy(50, 50);
    return w;
}

void test_walker_misc_methods_smoke()
{
    auto w = create_living(FAMILY_SOLDIER);
    auto nearby = create_living(FAMILY_ORC);
    TEST_ASSERT(w != nullptr, "create_walker(soldier) should succeed");
    TEST_ASSERT(nearby != nullptr, "create_walker(orc) should succeed");
    nearby->setxy(64, 64);

    // Basic movement helpers (should not crash).
    w->move(1, 0);
    w->worldmove(1.0f, 0.0f);
    w->setworldxy(60.0f, 60.0f);
    w->setxy(60, 60);
    w->facing(61, 60);
    w->turn(1);

    // Path helpers and distance checks.
    TEST_ASSERT(w->distance_to_ob(w.get()) == 0, "distance to self should be 0");
    (void)w->distance_to_ob_center(w.get());
    (void)w->get_current_angle();
    (void)w->query_old_act_type();
    (void)w->spaces_clear();
    (void)w->query_team_color();

    // Order/family reassignment and simple state transitions.
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_act_type(0);
    (void)w->query_act_type();
    w->set_old_act_type(1);
    (void)w->restore_act_type();
    (void)w->fire_check(1, 0);
    w->center_on(nearby.get());
    w->set_direct_frame(0);
    // Avoid calling higher-level actions here (fire/teleport/turn_undead/etc.):
    // they can spawn objects into `myscreen->level_data` which outlive this test's
    // locally-owned walkers and lead to UAF in later tests under ASan.

    // Reset is a large code path; smoke it to improve coverage.
    w->reset();
    w->animate();
    w->set_difficulty(2);

}
REGISTER_TEST(test_walker_misc_methods_smoke);

void test_walker_friendliness_and_attack_paths()
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_SMALL_SLIME);
    TEST_ASSERT(a != nullptr, "create_walker(attacker) should succeed");
    TEST_ASSERT(b != nullptr, "create_walker(target) should succeed");

    a->team_num = 0;
    b->team_num = 1;

    TEST_ASSERT(!a->is_friendly(b.get()), "enemy should not be friendly");
    TEST_ASSERT(a->is_friendly_to_team(0), "same team should be friendly");
    TEST_ASSERT(!a->is_friendly_to_team(1), "other team should not be friendly");

    // Give attacker a guy to record tallies.
    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    a->myguy->teamnum = 0;
    a->myguy->exp = 0;

    b->stats()->armor = 0;
    b->stats()->hitpoints = 50;
    b->stats()->max_hitpoints = 50;

    (void)a->attack(b.get());

}
REGISTER_TEST(test_walker_friendliness_and_attack_paths);

void test_walker_specials_and_render_paths_smoke()
{
    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker(soldier) should succeed");
    w->team_num = 0;

    // Give the walker a guy so specials/XP paths have something to update.
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->myguy->teamnum = 0;
    w->stats()->magicpoints = 999;
    w->stats()->max_magicpoints = 999;

    // Keep to deterministic, non-blocking paths in unit-test mode.
    (void)w->query_next_to();
    (void)draw_walker(*w, v);
    (void)draw_walker_tile(*w, v);
    w->animate();
    w->set_difficulty(1);
    w->set_direct_frame(0);
    (void)w->query_team_color();
    (void)w->query_old_act_type();

}
REGISTER_TEST(test_walker_specials_and_render_paths_smoke);

void test_walker_myguy_move_and_weapon_heading_and_outline_named()
{
    auto owner = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_ORC);
    TEST_ASSERT(owner != nullptr, "owner created");
    TEST_ASSERT(target != nullptr, "target created");
    if (!owner || !target)
        return;

    // -----------------------------------------------------------------------
    // myguy ownership/view helpers
    // -----------------------------------------------------------------------
    owner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    TEST_ASSERT(owner->myguy != nullptr, "owner has myguy");
    owner->move_myguy_to(target.get());
    TEST_ASSERT(owner->myguy == nullptr, "owner myguy cleared after move");
    TEST_ASSERT(target->myguy != nullptr, "target received owned myguy");

    // Move a non-owned view pointer.
    guy view_guy(FAMILY_SOLDIER);
    owner->set_myguy_view(&view_guy);
    owner->move_myguy_to(target.get());
    TEST_ASSERT(owner->myguy == nullptr, "owner view cleared after move");
    TEST_ASSERT(target->myguy == &view_guy, "target received view myguy");

    target->clear_myguy();
    TEST_ASSERT(target->myguy == nullptr, "clear_myguy clears view/ownership");

    // -----------------------------------------------------------------------
    // compute_outline: OUTLINE_NAMED transition branches
    // -----------------------------------------------------------------------
    owner->outline = OUTLINE_NAMED;
    owner->invisibility_left = 0;
    owner->invulnerable_left = 1;
    owner->flight_left = 0;
    owner->compute_outline(/*viewer_control*/ nullptr);
    TEST_ASSERT(owner->outline == OUTLINE_INVULNERABLE,
                "named should transition to invulnerable when invulnerable_left set");

    owner->outline = OUTLINE_NAMED;
    owner->invisibility_left = 0;
    owner->invulnerable_left = 0;
    owner->flight_left = 1;
    owner->compute_outline(/*viewer_control*/ nullptr);
    TEST_ASSERT(owner->outline == OUTLINE_FLYING,
                "named should transition to flying when flight_left set");

    // -----------------------------------------------------------------------
    // set_weapon_heading: deterministic switch coverage (no waver)
    // -----------------------------------------------------------------------
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");
    if (!l)
        return;

    auto weapon = l->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(weapon != nullptr, "weapon created");
    if (!weapon)
        return;
    weapon->stepsize = 0; // waver becomes 0

    // Use explicit xpos/ypos because set_weapon_heading uses them directly.
    owner->xpos = 100;
    owner->ypos = 120;

    // FACE_RIGHT
    owner->lastx = 1;
    owner->lasty = 0;
    owner->set_weapon_heading(weapon.get());
    TEST_ASSERT_EQ(static_cast<int>(owner->xpos + owner->sizex + 1), static_cast<int>(weapon->xpos),
                   "FACE_RIGHT sets weapon xpos");

    // FACE_LEFT
    owner->lastx = -1;
    owner->lasty = 0;
    owner->set_weapon_heading(weapon.get());
    TEST_ASSERT_EQ(static_cast<int>(owner->xpos - weapon->sizex - 1), static_cast<int>(weapon->xpos),
                   "FACE_LEFT sets weapon xpos");

    // FACE_DOWN
    owner->lastx = 0;
    owner->lasty = 1;
    owner->set_weapon_heading(weapon.get());
    TEST_ASSERT_EQ(static_cast<int>(owner->ypos + owner->sizey + 1), static_cast<int>(weapon->ypos),
                   "FACE_DOWN sets weapon ypos");

    // FACE_UP
    owner->lastx = 0;
    owner->lasty = -1;
    owner->set_weapon_heading(weapon.get());
    TEST_ASSERT_EQ(static_cast<int>(owner->ypos - weapon->sizey - 1), static_cast<int>(weapon->ypos),
                   "FACE_UP sets weapon ypos");

    // Diagonals.
    owner->lastx = 1;
    owner->lasty = -1;
    owner->set_weapon_heading(weapon.get());
    TEST_ASSERT_EQ(static_cast<int>(owner->xpos + owner->sizex + 1), static_cast<int>(weapon->xpos),
                   "FACE_UP_RIGHT sets weapon xpos");

    owner->lastx = -1;
    owner->lasty = -1;
    owner->set_weapon_heading(weapon.get());
    TEST_ASSERT_EQ(static_cast<int>(owner->xpos - weapon->sizex - 1), static_cast<int>(weapon->xpos),
                   "FACE_UP_LEFT sets weapon xpos");

    owner->lastx = 1;
    owner->lasty = 1;
    owner->set_weapon_heading(weapon.get());
    TEST_ASSERT_EQ(static_cast<int>(owner->xpos + owner->sizex + 1), static_cast<int>(weapon->xpos),
                   "FACE_DOWN_RIGHT sets weapon xpos");

    owner->lastx = -1;
    owner->lasty = 1;
    owner->set_weapon_heading(weapon.get());
    TEST_ASSERT_EQ(static_cast<int>(owner->xpos - weapon->sizex - 1), static_cast<int>(weapon->xpos),
                   "FACE_DOWN_LEFT sets weapon xpos");

    // -----------------------------------------------------------------------
    // walker::act switch: deterministic small cases
    // -----------------------------------------------------------------------
    owner->stats()->clear_command();
    owner->ani_type = ANI_WALK;

    owner->set_act_type(ACT_DIE);
    owner->dead = 0;
    TEST_ASSERT(owner->act() == 1, "ACT_DIE act returns 1");
    TEST_ASSERT(owner->dead == 1, "ACT_DIE sets dead");

    owner->dead = 0;
    owner->set_act_type(127);
    TEST_ASSERT(owner->act() == 0, "unknown act_type returns 0");
}
REGISTER_TEST(test_walker_myguy_move_and_weapon_heading_and_outline_named);

void test_walker_create_weapon_myguy_and_direction_and_cleric_branches()
{
    auto shooter = create_living(FAMILY_CLERIC);
    TEST_ASSERT(shooter != nullptr, "shooter created");
    if (!shooter)
        return;

    shooter->team_num = 0;
    shooter->stats()->level = 4;
    shooter->current_weapon = FAMILY_ARROW;
    shooter->default_weapon = shooter->current_weapon;

    // With myguy and cardinal direction, create_weapon takes the myguy stat branch
    // and applies the cardinal-range scaling.
    shooter->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    shooter->lastx = shooter->stepsize;
    shooter->lasty = 0;
    walker* w1 = shooter->create_weapon();
    TEST_ASSERT(w1 != nullptr, "weapon created (with myguy)");

    // Without myguy and diagonal direction, create_weapon takes the else branch
    // and skips the cardinal-range scaling.
    shooter->clear_myguy();
    shooter->lastx = shooter->stepsize;
    shooter->lasty = shooter->stepsize;
    walker* w2 = shooter->create_weapon();
    TEST_ASSERT(w2 != nullptr, "weapon created (no myguy)");

    // Cleric special-case: weapon is configured with glow-grow and extra lifetime.
    if (w1) {
        TEST_ASSERT(w1->ani_type == ANI_GLOWGROW, "cleric weapon uses glowgrow");
    }

    // Clean up only what we spawned; don't wipe global state (view controls, etc.).
    if (w1)
        myscreen->level_data.remove_ob(w1);
    if (w2)
        myscreen->level_data.remove_ob(w2);
}
REGISTER_TEST(test_walker_create_weapon_myguy_and_direction_and_cleric_branches);
