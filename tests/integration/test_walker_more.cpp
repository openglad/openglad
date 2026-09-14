#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/gameplay_context.h>
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

namespace
{
// compute_base_damage is `d - sqrt(d)/2 + random(floor(sqrt(d)))`. Pinning an
// exact damage number means pinning that draw, and the cheapest way to do it
// without owning the RNG seam is a damage value whose sqrt floors to 1:
// random(1) is 0 for every IRandom implementation. 2.25 -> 2.25 - 0.75 = 1.5,
// which damage_to_hit_points rounds to exactly 2.
inline constexpr float kRngFreeDamage = 2.25f;
inline constexpr int kRngFreeHitPoints = 2;

GameWorld& test_world()
{
    return og::runtime::current_session->myscreen_->world();
}

// An empty, fully passable 40x60 grass grid with nothing in the obmap.
void open_empty_world()
{
    test_world().create_new_grid();
    test_world().delete_objects();
}
} // namespace

TEST(WalkerMore, walker_center_on_reset_and_small_state_accessors)
{
    open_empty_world();

    auto w = create_living(FAMILY_SOLDIER);
    auto nearby = create_living(FAMILY_ORC);
    ASSERT_TRUE(w != nullptr) << "create_walker(soldier) should succeed";
    ASSERT_TRUE(nearby != nullptr) << "create_walker(orc) should succeed";
    // Park the orc far away so the eight-neighbour sweep below sees open ground.
    nearby->setxy(320, 400);
    w->setxy(80, 80);

    // -------------------------------------------------------------------
    // spaces_clear(): all eight neighbouring body-sized offsets on open ground.
    // -------------------------------------------------------------------
    ASSERT_EQ(8, w->spaces_clear())
        << "an open grass grid with nothing else in the obmap leaves all 8 spaces clear";

    // team 0's palette ramp base; the outline/render paths key off this value.
    ASSERT_EQ(40, static_cast<int>(w->query_team_color()))
        << "team 0 ramp base is 16*team + 40";

    // -------------------------------------------------------------------
    // distance_to_ob is the SQUARED centre-corrected distance.
    // -------------------------------------------------------------------
    ASSERT_EQ(0, w->distance_to_ob(w.get())) << "distance to self should be 0";

    // -------------------------------------------------------------------
    // set_act_type/restore_act_type shuffle exactly one slot.
    // -------------------------------------------------------------------
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_act_type(ACT_CONTROL);
    w->set_act_type(0);
    ASSERT_EQ(0, static_cast<int>(w->act_type())) << "set_act_type writes the live slot";
    ASSERT_EQ(ACT_CONTROL, static_cast<int>(w->old_act_type()))
        << "set_act_type parks the previous act_type in old_act_type";
    w->set_old_act_type(1);
    ASSERT_EQ(1, static_cast<int>(w->restore_act_type()))
        << "restore_act_type returns the parked act_type";
    ASSERT_EQ(1, static_cast<int>(w->act_type()))
        << "restore_act_type copies the parked act_type back into the live slot";

    // -------------------------------------------------------------------
    // center_on: our centre lands on the target's centre, exactly.
    // -------------------------------------------------------------------
    nearby->setxy(320, 400);
    const int expected_x = nearby->xpos() + nearby->sizex() / 2 - w->sizex() / 2;
    const int expected_y = nearby->ypos() + nearby->sizey() / 2 - w->sizey() / 2;
    w->center_on(nearby.get());
    ASSERT_EQ(expected_x, static_cast<int>(w->xpos()))
        << "center_on: target.x + target.sizex/2 - sizex/2";
    ASSERT_EQ(expected_y, static_cast<int>(w->ypos()))
        << "center_on: target.y + target.sizey/2 - sizey/2";

    // -------------------------------------------------------------------
    // reset(): the per-walker fields walker::reset actually writes.
    // -------------------------------------------------------------------
    w->set_dead(1);
    w->set_death_called(1);
    w->set_ignore(1);
    w->set_flight_left(9);
    w->set_regen_delay(37);
    w->set_hurt_flash(true);
    w->set_attack_lunge(1.0f);
    w->set_hit_recoil(1.0f);
    w->set_last_hitpoints(42.0f);
    w->stats()->set_bit_flags(BIT_IMMORTAL, 1);
    ASSERT_TRUE(w->reset()) << "reset reports success";
    EXPECT_EQ(0, static_cast<int>(w->dead())) << "reset revives";
    EXPECT_EQ(0, static_cast<int>(w->death_called())) << "reset clears death_called";
    EXPECT_EQ(0, static_cast<int>(w->ignore())) << "reset makes us collidable again";
    EXPECT_EQ(0, static_cast<int>(w->flight_left())) << "reset grounds us";
    EXPECT_EQ(0, static_cast<int>(w->regen_delay())) << "reset clears the regen delay";
    EXPECT_FALSE(w->hurt_flash()) << "reset clears the hurt flash";
    EXPECT_FLOAT_EQ(0.0f, w->attack_lunge()) << "reset clears the lunge offset";
    EXPECT_FLOAT_EQ(0.0f, w->hit_recoil()) << "reset clears the recoil offset";
    EXPECT_FLOAT_EQ(0.0f, w->last_hitpoints()) << "reset clears last_hitpoints";
    EXPECT_EQ(0u, static_cast<unsigned>(w->stats()->bit_flags()))
        << "reset wipes every stat bit flag";
}


TEST(WalkerMore, walker_friendliness_and_a_hostile_attack_lands_its_damage)
{
    open_empty_world();

    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(a != nullptr) << "create_walker(attacker) should succeed";
    ASSERT_TRUE(b != nullptr) << "create_walker(target) should succeed";

    a->set_team_num(0);
    b->set_team_num(1);

    ASSERT_TRUE(!a->is_friendly(b.get())) << "enemy should not be friendly";
    ASSERT_TRUE(a->is_friendly_to_team(0)) << "same team should be friendly";
    ASSERT_TRUE(!a->is_friendly_to_team(1)) << "other team should not be friendly";

    // Give attacker a guy to record tallies.
    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    a->myguy->teamnum = 0;
    a->myguy->exp = 0;
    a->myguy->total_hits = 0;
    a->myguy->scen_hits = 0;

    a->stats()->set_level(1);
    b->stats()->set_level(1);
    b->stats()->set_armor(0);
    b->stats()->set_hitpoints(50);
    b->stats()->set_max_hitpoints(50);

    a->set_damage(kRngFreeDamage);

    ASSERT_TRUE(a->attack(b.get())) << "a hostile living is a legal target";
    EXPECT_FLOAT_EQ(48.0f, b->stats()->hitpoints())
        << "50 hp - the 2-point post-reduction blow";
    EXPECT_FLOAT_EQ(50.0f, b->last_hitpoints())
        << "do_combat_damage stamps the pre-hit hp for the render/HUD delta";
    EXPECT_EQ(50, b->regen_delay())
        << "a landed blow delays the victim's hp regeneration by 50 ticks";
    EXPECT_EQ(1, static_cast<int>(a->myguy->scen_hits))
        << "a living target counts as one scenario hit";
    EXPECT_EQ(1, static_cast<int>(a->myguy->total_hits))
        << "...and one lifetime hit";
    EXPECT_EQ(kRngFreeHitPoints, static_cast<int>(a->myguy->scen_damage))
        << "the tally records the damage actually dealt";
    EXPECT_EQ(18u, a->myguy->exp)
        << "melee xp: 6 * 2 * poly(level_diff 0) / 20 = 18";
}


TEST(WalkerMore, walker_draw_paths_animate_step_and_query_next_to_probe)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    open_empty_world();

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker(soldier) should succeed";
    w->set_team_num(0);
    w->setxy(160, 160);

    // Give the walker a guy so specials/XP paths have something to update.
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->myguy->teamnum = 0;
    w->stats()->set_magicpoints(999);
    w->stats()->set_max_magicpoints(999);

    // Both render entry points report that they drew a live walker.
    EXPECT_TRUE(draw_walker(*w, v)) << "a live, non-dormant walker draws";
    EXPECT_TRUE(draw_walker_tile(*w, v)) << "the tile-mode blit draws too";
    EXPECT_EQ(40, static_cast<int>(w->query_team_color()))
        << "team 0 ramp base is 16*team + 40";

    // animate() consumes exactly one frame of the current sequence.
    w->set_ani_type(ANI_WALK);
    w->set_curdir(FACE_RIGHT);
    w->set_cycle(0);
    ASSERT_TRUE(w->animate()) << "the walk sequence has frames to spend";
    EXPECT_EQ(1, static_cast<int>(w->cycle()))
        << "animate advances the cycle by exactly one";

    // query_next_to probes ONE point derived from lastx/lasty. Note the quirk
    // it pins: the y arm's `else` catches lasty == 0, so a walker facing due
    // east probes (x + sizex, y - sizey) -- up AND right, not straight right.
    w->set_lastx(1);
    w->set_lasty(0);
    ASSERT_FALSE(w->query_next_to()) << "nothing stands on the probe point yet";

    auto blocker = create_living(FAMILY_ORC);
    ASSERT_TRUE(blocker != nullptr) << "create_walker(orc) should succeed";
    blocker->set_team_num(1);
    blocker->setxy(static_cast<short>(w->xpos() + w->sizex()),
                   static_cast<short>(w->ypos() - w->sizey()));
    EXPECT_TRUE(w->query_next_to())
        << "a body on the (x + sizex, y - sizey) probe point reads as next-to";

    blocker->setxy(320, 400);
    EXPECT_FALSE(w->query_next_to()) << "moving the body away clears the probe";
}


TEST(WalkerMore, walker_myguy_move_and_weapon_heading_and_outline_named)
{
    auto owner = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_ORC);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    ASSERT_TRUE(target != nullptr) << "target created";

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
    owner->set_outline(OUTLINE_NAMED);
    owner->set_invisibility_left(0);
    owner->set_invulnerable_left(1);
    owner->set_flight_left(0);
    owner->compute_outline(/*viewer_control*/ nullptr);
    ASSERT_TRUE(owner->outline() == OUTLINE_INVULNERABLE) << "named should transition to invulnerable when invulnerable_left set";

    owner->set_outline(OUTLINE_NAMED);
    owner->set_invisibility_left(0);
    owner->set_invulnerable_left(0);
    owner->set_flight_left(1);
    owner->compute_outline(/*viewer_control*/ nullptr);
    ASSERT_TRUE(owner->outline() == OUTLINE_FLYING) << "named should transition to flying when flight_left set";

    // -----------------------------------------------------------------------
    // set_weapon_heading: deterministic switch coverage (no waver)
    // -----------------------------------------------------------------------
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    auto weapon = l->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
    weapon->set_stepsize(0); // waver becomes 0

    // Use explicit xpos/ypos because set_weapon_heading uses them directly.
    owner->set_xpos(100);
    owner->set_ypos(120);

    // FACE_RIGHT
    owner->set_lastx(1);
    owner->set_lasty(0);
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos() + owner->sizex() + 1), static_cast<int>(weapon->xpos())) << "FACE_RIGHT sets weapon xpos";

    // FACE_LEFT
    owner->set_lastx(-1);
    owner->set_lasty(0);
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos() - weapon->sizex() - 1), static_cast<int>(weapon->xpos())) << "FACE_LEFT sets weapon xpos";

    // FACE_DOWN
    owner->set_lastx(0);
    owner->set_lasty(1);
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->ypos() + owner->sizey() + 1), static_cast<int>(weapon->ypos())) << "FACE_DOWN sets weapon ypos";

    // FACE_UP
    owner->set_lastx(0);
    owner->set_lasty(-1);
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->ypos() - weapon->sizey() - 1), static_cast<int>(weapon->ypos())) << "FACE_UP sets weapon ypos";

    // Diagonals.
    owner->set_lastx(1);
    owner->set_lasty(-1);
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos() + owner->sizex() + 1), static_cast<int>(weapon->xpos())) << "FACE_UP_RIGHT sets weapon xpos";

    owner->set_lastx(-1);
    owner->set_lasty(-1);
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos() - weapon->sizex() - 1), static_cast<int>(weapon->xpos())) << "FACE_UP_LEFT sets weapon xpos";

    owner->set_lastx(1);
    owner->set_lasty(1);
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos() + owner->sizex() + 1), static_cast<int>(weapon->xpos())) << "FACE_DOWN_RIGHT sets weapon xpos";

    owner->set_lastx(-1);
    owner->set_lasty(1);
    owner->set_weapon_heading(weapon.get());
    ASSERT_EQ(static_cast<int>(owner->xpos() - weapon->sizex() - 1), static_cast<int>(weapon->xpos())) << "FACE_DOWN_LEFT sets weapon xpos";

    // -----------------------------------------------------------------------
    // walker::act switch: deterministic small cases
    // -----------------------------------------------------------------------
    owner->stats()->clear_command();
    owner->set_ani_type(ANI_WALK);

    owner->set_act_type(ACT_DIE);
    owner->set_dead(0);
    ASSERT_TRUE(owner->act() == 1) << "ACT_DIE act returns 1";
    ASSERT_TRUE(owner->dead() == 1) << "ACT_DIE sets dead";

    owner->set_dead(0);
    owner->set_act_type(127);
    ASSERT_TRUE(owner->act() == 0) << "unknown act_type returns 0";
}


TEST(WalkerMore, walker_init_fire_and_fire_check_gate_branches)
{
    // The open grid has to exist BEFORE the walkers do: obmap::move short-
    // circuits when the coordinates are unchanged, so re-placing a walker at
    // the coordinates it already holds would leave it out of the fresh obmap.
    open_empty_world();

    auto w = create_living(FAMILY_SOLDIER);
    auto foe = create_living(FAMILY_ORC);
    ASSERT_TRUE(w && foe) << "walkers created";

    w->setxy(80, 80);
    foe->setxy(96, 80);
    w->set_team_num(0);
    foe->set_team_num(1);
    w->set_foe(foe.get());

    // init_fire: control walker must not turn/fire when facing differs.
    w->set_curdir(FACE_LEFT);
    w->set_enddir(FACE_LEFT);
    w->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(!w->init_fire(1, 0)) << "ACT_CONTROL init_fire should fail when facing differs";

    // init_fire: busy gate.
    w->set_act_type(ACT_RANDOM);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    w->set_busy(3);
    ASSERT_TRUE(!w->init_fire(1, 0)) << "busy init_fire should fail";
    w->set_busy(0);

    // fire_check: no foe.
    w->set_foe(nullptr);
    ASSERT_TRUE(!w->fire_check(1, 0)) << "fire_check should fail without foe";
    w->set_foe(foe.get());

    // fire_check: no-ranged bit.
    w->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    ASSERT_TRUE(!w->fire_check(1, 0)) << "fire_check should fail with BIT_NO_RANGED";
    w->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // fire_check: insufficient magic for weapon cost.
    w->stats()->set_weapon_cost(9999);
    w->stats()->set_magicpoints(0);
    ASSERT_TRUE(!w->fire_check(1, 0)) << "fire_check should fail when weapon_cost exceeds magicpoints";
    w->stats()->set_weapon_cost(0);
    w->stats()->set_magicpoints(100);

    // fire_check: target direction mismatch with current facing.
    w->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!w->fire_check(1, 0)) << "fire_check should fail when targetdir differs from curdir";

    // -------------------------------------------------------------------
    // The positive control. Six refusals alone stay green for a regression
    // that makes fire_check/init_fire ALWAYS deny (the guard-standoff class
    // of bug), so pin the arm that must still say yes.
    // -------------------------------------------------------------------
    w->set_foe(foe.get());
    // set_weapon_heading reads lastx/lasty, not curdir; keep them consistent
    // or every probe shot flies due north off the map.
    w->set_lastx(1);
    w->set_lasty(0);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    w->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    w->stats()->set_weapon_cost(0);
    w->stats()->set_magicpoints(9999);
    w->set_busy(0);
    w->set_ani_type(ANI_WALK);
    w->set_act_type(ACT_RANDOM);
    w->set_dead(0);

    walker::FireCheckDenial why = walker::FireCheckDenial::WallBlocked;
    ASSERT_TRUE(w->fire_check(1, 0, &why))
        << "a hostile foe one body-width east, in reach, faced and unobstructed, IS firable";
    EXPECT_EQ(static_cast<int>(walker::FireCheckDenial::None), static_cast<int>(why))
        << "a passing check reports no denial stage";
    ASSERT_TRUE(w->init_fire(1, 0))
        << "init_fire's ANI_WALK arm starts the attack animation";
    EXPECT_EQ(ANI_ATTACK, static_cast<int>(w->ani_type()))
        << "init_fire switches the walker into its attack animation";
    // busy was zeroed above and init_fire's charge is `busy += fire_frequency()`
    // (walker.cpp), so the delay it leaves is knowable to the frame.
    EXPECT_FLOAT_EQ(w->fire_frequency(), w->busy())
        << "init_fire charges exactly one fire_frequency of delay";
}


TEST(WalkerMore, walker_round6_friendliness_null_dead_owner_chain_and_allied_modes)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_ARCHER);
    auto owner_a = create_living(FAMILY_MAGE);
    auto owner_b = create_living(FAMILY_ORC);
    ASSERT_TRUE(a && b && owner_a && owner_b) << "walkers created";

    // Null target guard.
    ASSERT_EQ(0, (int)a->is_friendly(nullptr)) << "is_friendly should return 0 for null target";

    // Dead target guard.
    b->set_dead(1);
    ASSERT_EQ(0, (int)a->is_friendly(b.get())) << "dead target should be unfriendly";
    b->set_dead(0);

    // Owner-chain traversal branches.
    a->set_owner(owner_a.get());
    b->set_owner(owner_b.get());
    owner_a->set_team_num(0);
    owner_b->set_team_num(1);

    const int old_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;

    // Ownership does not affect same-team friendliness.
    owner_a->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    owner_b->clear_myguy();
    owner_b->set_team_num(0);
    og::runtime::current_session->myscreen_->world_.allied_mode = 1;
    ASSERT_TRUE(a->is_friendly(b.get()) != 0)
        << "same-team walkers should be friendly";

    owner_b->set_team_num(1);
    ASSERT_EQ(0, (int)a->is_friendly(b.get()))
        << "different teams should be hostile regardless of ownership";

    // Enemy mode path (allied_mode==0).
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;
    owner_b->set_team_num(owner_a->team_num());
    ASSERT_TRUE(a->is_friendly(b.get()) != 0) << "enemy mode uses team equality";

    // is_friendly_to_team dead and no-myguy paths.
    a->set_dead(1);
    ASSERT_EQ(0, (int)a->is_friendly_to_team(0)) << "dead walker should not be friendly to any team";
    a->set_dead(0);
    owner_a->clear_myguy();
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;
    ASSERT_TRUE(a->is_friendly_to_team(owner_a->team_num()) != 0) << "non-myguy path should still compare team";

    og::runtime::current_session->myscreen_->world_.allied_mode = static_cast<short>(old_allied_mode);
}


// act_fire's collision arm, reached the way the sim reaches it. Note that a
// test CANNOT pre-seed collide_ob: both walker::act and weap::act clear it
// before dispatching, so the only way into `if (collide_ob && !dead)
// attack(collide_ob)` is for act_fire's own walk() to be blocked by a body --
// the obmap calls collide() on the mover as it refuses the step.
TEST(WalkerMore, walker_act_fire_collision_arm_attacks_the_blocking_body)
{
    open_empty_world();
    GameWorld& world = test_world();

    walker* target = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(target != nullptr) << "target created";
    target->set_team_num(1);
    target->setxy(200, 160);
    target->stats()->set_armor(0);
    target->stats()->set_hitpoints(50);
    target->stats()->set_max_hitpoints(50);

    // An IMMORTAL weapon parked one step west of the target body.
    walker* weapon = world.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
    ASSERT_EQ(Order::Weapon, weapon->query_order())
        << "the collide arm lives on weap::act's ACT_FIRE case";
    weapon->set_team_num(0);
    weapon->set_lineofsight(5);       // remaining_range != 0, so we reach the walk
    weapon->set_curdir(FACE_RIGHT);
    weapon->set_lastx(2);
    weapon->set_lasty(0);
    weapon->set_ani_type(ANI_WALK);   // otherwise weap::act just animates
    weapon->set_act_type(ACT_FIRE);
    weapon->stats()->set_hitpoints(100);
    weapon->set_damage(kRngFreeDamage);  // an exact 2 hit points
    weapon->stats()->set_bit_flags(BIT_IMMORTAL, 1);
    // One pixel short of the target's box: the +2 step is what overlaps it.
    ASSERT_TRUE(weapon->setxy(
        static_cast<short>(target->xpos() - weapon->sizex() - 1),
        static_cast<short>(160)));
    weapon->set_lastx(6);

    const short wx_before = weapon->xpos();
    ASSERT_TRUE(weapon->act()) << "weap::act dispatches ACT_FIRE to act_fire";
    EXPECT_EQ(wx_before, weapon->xpos())
        << "the body refused the step: the weapon is still where it started";
    EXPECT_FLOAT_EQ(48.0f, target->stats()->hitpoints())
        << "50 hp - the 2-point post-reduction blow the collision arm deals";
    EXPECT_FLOAT_EQ(50.0f, target->last_hitpoints())
        << "the victim's pre-hit hp is stamped for the HUD delta";
    EXPECT_EQ(0, static_cast<int>(weapon->dead()))
        << "BIT_IMMORTAL keeps the weapon alive through its own collision";
    EXPECT_EQ(4, static_cast<int>(weapon->lineofsight()))
        << "act_fire spends one point of range per tick";

    // The mortal twin: same collision, but the weapon dies on impact.
    walker* mortal = world.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(mortal != nullptr) << "second weapon created";
    mortal->set_team_num(0);
    mortal->set_lineofsight(5);
    mortal->set_curdir(FACE_RIGHT);
    mortal->set_lastx(2);
    mortal->set_lasty(0);
    mortal->set_ani_type(ANI_WALK);
    mortal->set_act_type(ACT_FIRE);
    mortal->stats()->set_hitpoints(100);
    mortal->set_damage(kRngFreeDamage);
    mortal->stats()->set_bit_flags(BIT_IMMORTAL, 0);
    ASSERT_TRUE(mortal->setxy(
        static_cast<short>(target->xpos() - mortal->sizex() - 1),
        static_cast<short>(160)));
    mortal->set_lastx(6);

    ASSERT_TRUE(mortal->act()) << "weap::act dispatches ACT_FIRE to act_fire";
    EXPECT_FLOAT_EQ(46.0f, target->stats()->hitpoints())
        << "a second 2-point blow";
    EXPECT_EQ(1, static_cast<int>(mortal->dead()))
        << "a mortal weapon dies in the body it hit";

    world.delete_objects();
}


TEST(WalkerMore, walker_friendliness_null_dead_and_strict_team_paths)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_ORC);
    ASSERT_TRUE(a && b) << "walkers created";

    a->set_team_num(0);
    b->set_team_num(1);

    ASSERT_TRUE(!a->is_friendly(nullptr)) << "null target should be unfriendly";

    b->set_dead(1);
    ASSERT_TRUE(!a->is_friendly(b.get())) << "dead target should be unfriendly";
    b->set_dead(0);

    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    b->clear_myguy();

    b->set_team_num(0);
    ASSERT_TRUE(a->is_friendly(b.get()) != 0) << "team 0 target with one myguy should be treated as friendly";

    ASSERT_FALSE(a->is_friendly_to_team(1))
        << "team 0 walker should not be friendly to team 1";

    a->set_dead(1);
    ASSERT_TRUE(!a->is_friendly_to_team(0)) << "dead walker should be unfriendly to all teams";
    a->set_dead(0);
}


TEST(WalkerMore, walker_base_hooks_and_set_difficulty_scaling)
{
    open_empty_world();

    auto w = create_living(FAMILY_SOLDIER);
    auto t = create_living(FAMILY_ORC);
    ASSERT_TRUE(w && t) << "walkers created";

    // move_myguy_to(nullptr) small branch.
    w->move_myguy_to(nullptr);
    EXPECT_EQ(nullptr, w->myguy) << "moving a myguy nowhere still clears ours";

    // The base class's "this family cannot do that" hook.
    EXPECT_EQ(0, static_cast<int>(w->eat_me(t.get())))
        << "eat_me non-treasure fallback should return 0";

    // -------------------------------------------------------------------
    // walker::set_difficulty at a known difficulty percent. This is the BASE
    // implementation, so the subject has to be a walker that does not override
    // it: living::set_difficulty is a different rule (family Lua scaling, plus
    // an A12a arm that scales myguy-less team-0 NPCs too).
    // -------------------------------------------------------------------
    GameWorld& world = test_world();
    const auto old_difficulty = world.difficulty;
    world.difficulty = 200;  // query_difficulty_percent() == 200

    walker* subject = world.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(subject != nullptr) << "difficulty subject created";

    // The base class's other two "this family cannot do that" hooks. living
    // overrides both (a cleric really does raise skeletons), so the no-op
    // contract has to be read off a walker that does not.
    EXPECT_EQ(nullptr, subject->do_summon(FAMILY_SKELETON, 5))
        << "walker::do_summon is the base no-op: nothing is summoned";
    EXPECT_FALSE(subject->check_special())
        << "walker::check_special is the base no-op: no special is available";

    // Player characters (team 0) are never scaled.
    subject->set_team_num(0);
    subject->stats()->set_max_hitpoints(70.0f);
    subject->stats()->set_max_magicpoints(30.0f);
    subject->set_damage(9.0f);
    subject->set_difficulty(3);
    EXPECT_FLOAT_EQ(70.0f, subject->stats()->max_hitpoints()) << "team 0 is exempt";
    EXPECT_FLOAT_EQ(30.0f, subject->stats()->max_magicpoints()) << "team 0 is exempt";
    EXPECT_FLOAT_EQ(9.0f, subject->damage()) << "team 0 is exempt";

    // Every other team scales max hp / max mp / damage by the percent.
    subject->set_team_num(1);
    subject->set_difficulty(3);
    EXPECT_FLOAT_EQ(140.0f, subject->stats()->max_hitpoints()) << "70 * 200 / 100";
    EXPECT_FLOAT_EQ(60.0f, subject->stats()->max_magicpoints()) << "30 * 200 / 100";
    EXPECT_FLOAT_EQ(18.0f, subject->damage()) << "9 * 200 / 100";

    // A generator's hp is derived from scratch: 100 * level * percent / 100,
    // written to BOTH hitpoints and max_hitpoints (the post is its own
    // denominator, so act_generate's per-spawn regen has room to take).
    subject->set_order_family(Order::Generator, FAMILY_TENT);
    subject->set_difficulty(3);
    EXPECT_FLOAT_EQ(600.0f, subject->stats()->hitpoints()) << "100 * 3 * 200 / 100";
    EXPECT_FLOAT_EQ(600.0f, subject->stats()->max_hitpoints())
        << "a generator's max_hitpoints tracks its hitpoints";

    world.difficulty = old_difficulty;
    world.delete_objects();
}


TEST(WalkerMore, walker_create_weapon_myguy_branch_vs_level_branch_and_cardinal_scaling)
{
    open_empty_world();
    GameWorld& world = test_world();

    // The unconfigured baseline this family's weapon starts from.
    walker* probe = world.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(probe != nullptr) << "baseline arrow created";
    const float base_stepsize = probe->stepsize();
    const int base_los = probe->lineofsight();
    const float base_damage = probe->damage();
    world.remove_ob(probe);

    auto shooter = create_living(FAMILY_CLERIC);
    ASSERT_TRUE(shooter != nullptr) << "shooter created";

    shooter->set_team_num(0);
    shooter->stats()->set_level(4);
    shooter->set_current_weapon(FAMILY_ARROW);
    shooter->set_default_weapon(shooter->current_weapon());

    // Every weapon first takes damage * (level + 3) / 4.
    const float leveled_damage = (base_damage * (4.0f + 3.0f)) / 4.0f;
    // ... and then lineofsight += level / 3.
    const int leveled_los = base_los + 4 / 3;

    // With myguy and a CARDINAL heading, create_weapon takes the myguy stat
    // branch (lineofsight += str/23 + dex/31, damage += str/7) and then applies
    // the circular-range scaling (309/256) and the diagonal stepsize scaling
    // (362/256).
    shooter->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    shooter->myguy->strength = 46;    // 46/23 = 2 sight, 46/7 = 6.571 damage
    shooter->myguy->dexterity = 62;   // 62/31 = 2 sight
    shooter->set_lastx(shooter->stepsize());
    shooter->set_lasty(0);
    walker* w1 = shooter->create_weapon();
    ASSERT_TRUE(w1 != nullptr) << "weapon created (with myguy)";

    // Without myguy and a DIAGONAL heading, create_weapon takes the else branch
    // (damage *= level) and skips both cardinal scalings.
    shooter->clear_myguy();
    shooter->set_lastx(shooter->stepsize());
    shooter->set_lasty(shooter->stepsize());
    walker* w2 = shooter->create_weapon();
    ASSERT_TRUE(w2 != nullptr) << "weapon created (no myguy)";

    // --- the two branches must NOT agree ---------------------------------
    const int myguy_los = leveled_los + 46 / 23 + 62 / 31;
    EXPECT_EQ((myguy_los * 309) / 256, w1->lineofsight())
        << "myguy sight bonus, then the cardinal circular-range scaling";
    EXPECT_EQ(leveled_los, w2->lineofsight())
        << "no myguy bonus and no cardinal scaling on a diagonal shot";

    EXPECT_FLOAT_EQ((base_stepsize * 362.0f) / 256.0f, w1->stepsize())
        << "a cardinal shot's stepsize is scaled by 362/256";
    EXPECT_FLOAT_EQ(base_stepsize, w2->stepsize())
        << "a diagonal shot keeps the family stepsize";

    EXPECT_FLOAT_EQ(leveled_damage + 46.0f / 7.0f, w1->damage())
        << "the myguy branch adds strength/7 to damage";
    EXPECT_FLOAT_EQ(leveled_damage * 4.0f, w2->damage())
        << "the no-myguy branch multiplies damage by the caster level instead";

    EXPECT_EQ(shooter.get(), w1->owner()) << "the weapon belongs to its caster";
    EXPECT_EQ(0, static_cast<int>(w1->team_num())) << "and inherits its team";

    // Cleric special-case: the family hook configures glow-grow (unguarded).
    EXPECT_EQ(ANI_GLOWGROW, static_cast<int>(w1->ani_type()))
        << "cleric weapon uses glowgrow";
    EXPECT_EQ(ANI_GLOWGROW, static_cast<int>(w2->ani_type()))
        << "the hook runs on the no-myguy branch too";

    // Clean up only what we spawned; don't wipe global state (view controls, etc.).
    world.remove_ob(w1);
    world.remove_ob(w2);
}
