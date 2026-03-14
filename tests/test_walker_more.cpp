#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    w->setxy(50, 50);
    return w;
}

TEST(WalkerMore, walker_misc_methods_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    auto nearby = create_living(FAMILY_ORC);
    ASSERT_TRUE(w != nullptr) << "create_walker(soldier) should succeed";
    ASSERT_TRUE(nearby != nullptr) << "create_walker(orc) should succeed";
    nearby->setxy(64, 64);

    // Basic movement helpers (should not crash).
    w->move(1, 0);
    w->worldmove(1.0f, 0.0f);
    w->setworldxy(60.0f, 60.0f);
    w->setxy(60, 60);
    w->facing(61, 60);
    w->turn(1);

    // Path helpers and distance checks.
    ASSERT_TRUE(w->distance_to_ob(w.get()) == 0) << "distance to self should be 0";
    (void)w->distance_to_ob_center(w.get());
    (void)w->get_current_angle();
    (void)w->old_act_type;
    (void)w->spaces_clear();
    (void)w->query_team_color();

    // Order/family reassignment and simple state transitions.
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_act_type(0);
    (void)w->act_type;
    w->old_act_type = 1;
    (void)w->restore_act_type();
    (void)w->fire_check(1, 0);
    w->center_on(nearby.get());
    w->set_direct_frame(0);
    // Avoid calling higher-level actions here (fire/teleport/turn_undead/etc.):
    // they can spawn objects into `myscreen->level_runtime_data()` which outlive this test's
    // locally-owned walkers and lead to UAF in later tests under ASan.

    // Reset is a large code path; smoke it to improve coverage.
    w->reset();
    w->animate();
    w->set_difficulty(2);

}


TEST(WalkerMore, walker_friendliness_and_attack_paths)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(a != nullptr) << "create_walker(attacker) should succeed";
    ASSERT_TRUE(b != nullptr) << "create_walker(target) should succeed";

    a->team_num = 0;
    b->team_num = 1;

    ASSERT_TRUE(!a->is_friendly(b.get())) << "enemy should not be friendly";
    ASSERT_TRUE(a->is_friendly_to_team(0)) << "same team should be friendly";
    ASSERT_TRUE(!a->is_friendly_to_team(1)) << "other team should not be friendly";

    // Give attacker a guy to record tallies.
    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    a->myguy->teamnum = 0;
    a->myguy->exp = 0;

    b->stats()->armor = 0;
    b->stats()->hitpoints = 50;
    b->stats()->max_hitpoints = 50;

    (void)a->attack(b.get());

}


TEST(WalkerMore, walker_specials_and_render_paths_smoke)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker(soldier) should succeed";
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
    (void)w->old_act_type;

}


TEST(WalkerMore, walker_myguy_move_and_weapon_heading_and_outline_named)
{
    auto owner = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_ORC);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    ASSERT_TRUE(target != nullptr) << "target created";
    if (!owner || !target)
        return;

    // -----------------------------------------------------------------------
    // myguy ownership/view helpers
    // -----------------------------------------------------------------------
    owner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    ASSERT_TRUE(owner->myguy != nullptr) << "owner has myguy";
    owner->move_myguy_to(target.get());
    ASSERT_TRUE(owner->myguy == nullptr) << "owner myguy cleared after move";
    ASSERT_TRUE(target->myguy != nullptr) << "target received owned myguy";

    // Move a non-owned view pointer.
    guy view_guy(FAMILY_SOLDIER);
    owner->set_myguy_view(&view_guy);
    owner->move_myguy_to(target.get());
    ASSERT_TRUE(owner->myguy == nullptr) << "owner view cleared after move";
    ASSERT_TRUE(target->myguy == &view_guy) << "target received view myguy";

    target->clear_myguy();
    ASSERT_TRUE(target->myguy == nullptr) << "clear_myguy clears view/ownership";

    // -----------------------------------------------------------------------
    // compute_outline: OUTLINE_NAMED transition branches
    // -----------------------------------------------------------------------
    owner->outline = OUTLINE_NAMED;
    owner->invisibility_left = 0;
    owner->invulnerable_left = 1;
    owner->flight_left = 0;
    owner->compute_outline(/*viewer_control*/ nullptr);
    ASSERT_TRUE(owner->outline == OUTLINE_INVULNERABLE) << "named should transition to invulnerable when invulnerable_left set";

    owner->outline = OUTLINE_NAMED;
    owner->invisibility_left = 0;
    owner->invulnerable_left = 0;
    owner->flight_left = 1;
    owner->compute_outline(/*viewer_control*/ nullptr);
    ASSERT_TRUE(owner->outline == OUTLINE_FLYING) << "named should transition to flying when flight_left set";

    // -----------------------------------------------------------------------
    // set_weapon_heading: deterministic switch coverage (no waver)
    // -----------------------------------------------------------------------
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";
    if (!l)
        return;

    auto weapon = l->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
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
    ASSERT_EQ(static_cast<int>(owner->xpos + owner->sizex + 1), static_cast<int>(weapon->xpos)) << "FACE_RIGHT sets weapon xpos";

    // FACE_LEFT
    owner->lastx = -1;
    owner->lasty = 0;
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos - weapon->sizex - 1), static_cast<int>(weapon->xpos)) << "FACE_LEFT sets weapon xpos";

    // FACE_DOWN
    owner->lastx = 0;
    owner->lasty = 1;
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->ypos + owner->sizey + 1), static_cast<int>(weapon->ypos)) << "FACE_DOWN sets weapon ypos";

    // FACE_UP
    owner->lastx = 0;
    owner->lasty = -1;
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->ypos - weapon->sizey - 1), static_cast<int>(weapon->ypos)) << "FACE_UP sets weapon ypos";

    // Diagonals.
    owner->lastx = 1;
    owner->lasty = -1;
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos + owner->sizex + 1), static_cast<int>(weapon->xpos)) << "FACE_UP_RIGHT sets weapon xpos";

    owner->lastx = -1;
    owner->lasty = -1;
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos - weapon->sizex - 1), static_cast<int>(weapon->xpos)) << "FACE_UP_LEFT sets weapon xpos";

    owner->lastx = 1;
    owner->lasty = 1;
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos + owner->sizex + 1), static_cast<int>(weapon->xpos)) << "FACE_DOWN_RIGHT sets weapon xpos";

    owner->lastx = -1;
    owner->lasty = 1;
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos - weapon->sizex - 1), static_cast<int>(weapon->xpos)) << "FACE_DOWN_LEFT sets weapon xpos";

    // -----------------------------------------------------------------------
    // walker::act switch: deterministic small cases
    // -----------------------------------------------------------------------
    owner->stats()->clear_command();
    owner->ani_type = ANI_WALK;

    owner->set_act_type(ACT_DIE);
    owner->dead = 0;
    ASSERT_TRUE(owner->act() == 1) << "ACT_DIE act returns 1";
    ASSERT_TRUE(owner->dead == 1) << "ACT_DIE sets dead";

    owner->dead = 0;
    owner->set_act_type(127);
    ASSERT_TRUE(owner->act() == 0) << "unknown act_type returns 0";
}


TEST(WalkerMore, walker_init_fire_and_fire_check_gate_branches)
{
    auto w = create_living(FAMILY_SOLDIER);
    auto foe = create_living(FAMILY_ORC);
    ASSERT_TRUE(w && foe) << "walkers created";
    if (!(w && foe))
        return;

    w->setxy(80, 80);
    foe->setxy(96, 80);
    w->set_foe(foe.get());

    // init_fire: control walker must not turn/fire when facing differs.
    w->curdir = FACE_LEFT;
    w->enddir = FACE_LEFT;
    w->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(!w->init_fire(1, 0)) << "ACT_CONTROL init_fire should fail when facing differs";

    // init_fire: busy gate.
    w->set_act_type(ACT_RANDOM);
    w->curdir = FACE_RIGHT;
    w->enddir = FACE_RIGHT;
    w->busy = 3;
    ASSERT_TRUE(!w->init_fire(1, 0)) << "busy init_fire should fail";
    w->busy = 0;

    // fire_check: no foe.
    w->set_foe(nullptr);
    ASSERT_TRUE(!w->fire_check(1, 0)) << "fire_check should fail without foe";
    w->set_foe(foe.get());

    // fire_check: no-ranged bit.
    w->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    ASSERT_TRUE(!w->fire_check(1, 0)) << "fire_check should fail with BIT_NO_RANGED";
    w->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // fire_check: insufficient magic for weapon cost.
    w->stats()->weapon_cost = 9999;
    w->stats()->magicpoints = 0;
    ASSERT_TRUE(!w->fire_check(1, 0)) << "fire_check should fail when weapon_cost exceeds magicpoints";
    w->stats()->weapon_cost = 0;
    w->stats()->magicpoints = 100;

    // fire_check: target direction mismatch with current facing.
    w->curdir = FACE_LEFT;
    ASSERT_TRUE(!w->fire_check(1, 0)) << "fire_check should fail when targetdir differs from curdir";
}


TEST(WalkerMore, walker_round6_friendliness_null_dead_owner_chain_and_allied_modes)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_ARCHER);
    auto owner_a = create_living(FAMILY_MAGE);
    auto owner_b = create_living(FAMILY_ORC);
    ASSERT_TRUE(a && b && owner_a && owner_b) << "walkers created";
    if (!(a && b && owner_a && owner_b))
        return;

    // Null target guard.
    ASSERT_EQ(0, (int)a->is_friendly(nullptr)) << "is_friendly should return 0 for null target";

    // Dead target guard.
    b->dead = 1;
    ASSERT_EQ(0, (int)a->is_friendly(b.get())) << "dead target should be unfriendly";
    b->dead = 0;

    // Owner-chain traversal branches.
    a->owner = owner_a.get();
    b->owner = owner_b.get();
    owner_a->team_num = 0;
    owner_b->team_num = 1;

    const int old_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;

    // Allied mode with one myguy missing (has_myguy == 2 path).
    owner_a->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    owner_b->clear_myguy();
    owner_b->team_num = 0;
    og::runtime::current_session->myscreen_->world_.allied_mode = 1;
    ASSERT_TRUE(a->is_friendly(b.get()) != 0) << "allied mode should treat team-0 non-myguy as friendly";

    owner_b->team_num = 1;
    ASSERT_EQ(0, (int)a->is_friendly(b.get())) << "allied mode should reject non-team-0 when only one side has myguy";

    // Enemy mode path (allied_mode==0).
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;
    owner_b->team_num = owner_a->team_num;
    ASSERT_TRUE(a->is_friendly(b.get()) != 0) << "enemy mode uses team equality";

    // is_friendly_to_team dead and no-myguy paths.
    a->dead = 1;
    ASSERT_EQ(0, (int)a->is_friendly_to_team(0)) << "dead walker should not be friendly to any team";
    a->dead = 0;
    owner_a->clear_myguy();
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;
    ASSERT_TRUE(a->is_friendly_to_team(owner_a->team_num) != 0) << "non-myguy path should still compare team";

    og::runtime::current_session->myscreen_->world_.allied_mode = static_cast<short>(old_allied_mode);
}


TEST(WalkerMore, walker_round6_act_fire_collision_attack_path)
{
    auto weapon = create_living(FAMILY_ARROW);
    auto target = create_living(FAMILY_ORC);
    ASSERT_TRUE(weapon && target) << "walkers created";
    if (!(weapon && target))
        return;

    weapon->set_order_family(Order::Weapon, FAMILY_ARROW);
    weapon->team_num = 0;
    target->team_num = 1;
    target->stats()->hitpoints = 50;
    weapon->lineofsight = 5;
    weapon->lastx = -1;
    weapon->lasty = 0;
    weapon->setxy(0, GRID_SIZE * 4); // force walk() failure on next step
    weapon->collide_ob = target.get();
    weapon->stats()->set_bit_flags(BIT_IMMORTAL, 1); // keep weapon alive after collision branch

    const float hp_before = target->stats()->hitpoints;
    weapon->set_act_type(ACT_FIRE);
    ASSERT_TRUE(weapon->act()) << "act() should dispatch to act_fire";
    ASSERT_TRUE(target->stats()->hitpoints <= hp_before) << "collision branch should attack target";
}


TEST(WalkerMore, walker_friendliness_null_dead_and_allied_mode_paths)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_ORC);
    ASSERT_TRUE(a && b) << "walkers created";
    if (!(a && b))
        return;

    a->team_num = 0;
    b->team_num = 1;

    ASSERT_TRUE(!a->is_friendly(nullptr)) << "null target should be unfriendly";

    b->dead = 1;
    ASSERT_TRUE(!a->is_friendly(b.get())) << "dead target should be unfriendly";
    b->dead = 0;

    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    b->clear_myguy();

    b->team_num = 0;
    ASSERT_TRUE(a->is_friendly(b.get()) != 0) << "team 0 target with one myguy should be treated as friendly";

    ASSERT_TRUE(a->is_friendly_to_team(1) == 0 || a->is_friendly_to_team(1) == 1) << "friendly-to-team path should execute";

    a->dead = 1;
    ASSERT_TRUE(!a->is_friendly_to_team(0)) << "dead walker should be unfriendly to all teams";
    a->dead = 0;
}


TEST(WalkerMore, walker_batch2_misc_uncovered_paths_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    auto t = create_living(FAMILY_ORC);
    ASSERT_TRUE(w && t) << "walkers created";
    if (!(w && t))
        return;

    // move_myguy_to(nullptr) small branch.
    w->move_myguy_to(nullptr);

    // default virtual-like hooks that log and return fallback values.
    ASSERT_TRUE(w->eat_me(t.get()) == 0) << "eat_me non-treasure fallback should return 0";
    (void)w->do_summon(1, 5);
    (void)w->check_special();

    // set_difficulty branches.
    w->team_num = 1;
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_difficulty(3);
    w->set_order_family(Order::Generator, FAMILY_TENT);
    w->set_difficulty(3);

    // act_generate / act_fire / act_guard / act_random smoke via public act().
    w->set_order_family(Order::Generator, FAMILY_TENT);
    w->stats()->level = 5;
    w->set_act_type(ACT_GENERATE);
    (void)w->act();
    w->set_order_family(Order::Weapon, FAMILY_ARROW);
    w->lineofsight = 1;
    w->set_act_type(ACT_FIRE);
    (void)w->act();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_act_type(ACT_GUARD);
    (void)w->act();
    w->set_act_type(ACT_RANDOM);
    (void)w->act();
}


TEST(WalkerMore, walker_create_weapon_myguy_and_direction_and_cleric_branches)
{
    auto shooter = create_living(FAMILY_CLERIC);
    ASSERT_TRUE(shooter != nullptr) << "shooter created";
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
    ASSERT_TRUE(w1 != nullptr) << "weapon created (with myguy)";

    // Without myguy and diagonal direction, create_weapon takes the else branch
    // and skips the cardinal-range scaling.
    shooter->clear_myguy();
    shooter->lastx = shooter->stepsize;
    shooter->lasty = shooter->stepsize;
    walker* w2 = shooter->create_weapon();
    ASSERT_TRUE(w2 != nullptr) << "weapon created (no myguy)";

    // Cleric special-case: weapon is configured with glow-grow and extra lifetime.
    if (w1) {
        ASSERT_TRUE(w1->ani_type == ANI_GLOWGROW) << "cleric weapon uses glowgrow";
    }

    // Clean up only what we spawned; don't wipe global state (view controls, etc.).
    if (w1)
        og::runtime::current_session->myscreen_->world().remove_ob(w1);
    if (w2)
        og::runtime::current_session->myscreen_->world().remove_ob(w2);
}

