#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)
bool walkerIsAutoAttackable(walker* ob);
short collide(short x, short y, short xsize, short ysize,
              short x2, short y2, short xsize2, short ysize2);

static std::unique_ptr<walker> make_living(char family, short level = 3)
{
    guy g(family);
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// set_difficulty for all families - exercises the big switch (living.cpp)
// ---------------------------------------------------------------------------

TEST(LivingCombat, living_set_difficulty_raises_max_hp_and_refills_to_full)
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_NE(nullptr, l) << "loader is required to build the per-family walkers";

    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const short saved_difficulty = world.difficulty;
    // 100% is the identity difficulty: living::set_difficulty skips the percent
    // rescale entirely for a team-0 walker, so the family scaling is the only
    // thing moving max_hitpoints below.
    world.difficulty = 100;

    for (int i = 0; i < 14; i++) {
        for (int level = 1; level <= 5; level++) {
            auto w = l->create_walker_owned(Order::Living, families[i]);
            ASSERT_NE(nullptr, w.get()) << "family " << (int)families[i] << " walker created";
            w->set_team_num(0);
            const float before = w->stats()->max_hitpoints();
            static_cast<living*>(w.get())->set_difficulty(static_cast<std::uint32_t>(level));
            EXPECT_GT(w->stats()->max_hitpoints(), before)
                << "set_difficulty must add hp*level^2 (family " << (int)families[i]
                << ", level " << level << ")";
            EXPECT_FLOAT_EQ(w->stats()->max_hitpoints(), w->stats()->hitpoints())
                << "set_difficulty refills to full (family " << (int)families[i]
                << ", level " << level << ")";
        }
    }

    // Exact anchor: the soldier family hook is
    // og.apply_difficulty_scaling(self, level, 13.0, ...) (living-00-soldier.lua),
    // and guy.cpp apply_difficulty_scaling adds hp * level^2 == 13 * 9 at level 3.
    auto soldier = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier.get()) << "soldier anchor walker created";
    soldier->set_team_num(0);
    const float soldier_before = soldier->stats()->max_hitpoints();
    static_cast<living*>(soldier.get())->set_difficulty(3u);
    EXPECT_FLOAT_EQ(soldier_before + 13.0f * 9.0f, soldier->stats()->max_hitpoints())
        << "soldier difficulty-3 scaling is exactly +13*3^2 max hitpoints";
    EXPECT_FLOAT_EQ(soldier->stats()->max_hitpoints(), soldier->stats()->hitpoints())
        << "soldier is refilled to its new maximum";
    EXPECT_EQ(2, (int)soldier->weapons_left())
        << "soldier set_difficulty resets weapons_left to (level+1)/2";

    world.difficulty = saved_difficulty;
}


// ---------------------------------------------------------------------------
// check_special for all families - exercises the big switch (~143 lines)
// ---------------------------------------------------------------------------

TEST(LivingCombat, living_check_special_denies_disabled_specials_and_falls_back_to_special_one)
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    for (int i = 0; i < 14; i++) {
        auto w = make_living(families[i]);
        ASSERT_NE(nullptr, w.get()) << "family " << (int)families[i] << " walker created";
        living* lv = static_cast<living*>(w.get());
        w->stats()->set_magicpoints(100);
        w->stats()->set_max_magicpoints(100);

        // Gate 1: the placed-NPC specials_disabled flag is an unconditional no,
        // for every family, before any hook is consulted.
        w->set_specials_disabled(true);
        EXPECT_FALSE(lv->check_special())
            << "specials_disabled must deny the AI a special (family " << (int)families[i] << ")";
        w->set_specials_disabled(false);

        // Gate 2: an unaffordable current_special is reset to the default 1.
        w->set_current_special(4);
        w->stats()->set_special_cost(4, 200);
        w->stats()->set_magicpoints(0);
        (void)lv->check_special();  // the family AI hook's verdict is world state dependent
        EXPECT_EQ(1, (int)w->current_special())
            << "check_special resets an unaffordable special to 1 (family "
            << (int)families[i] << ")";
    }
}


// ---------------------------------------------------------------------------
// living::act smoke test
// ---------------------------------------------------------------------------

TEST(LivingCombat, living_act_control)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_act_type(ACT_CONTROL);
    bool result = w->act();
    ASSERT_TRUE(result) << "ACT_CONTROL should return true";
}


TEST(LivingCombat, living_act_guard_wakes_and_fires_only_when_a_foe_is_in_sight)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();

    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "guard created";

    w->set_team_num(0);
    w->set_act_type(ACT_GUARD);
    w->set_ani_type(ANI_WALK);
    w->set_action(0);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));
    w->set_lineofsight(4);
    w->setxy(100, 100);
    w->set_foe(nullptr);
    w->stats()->clear_command();

    // No foe in the level: walker::act_guard returns 0 and living::act's
    // ACT_GUARD arm falls through the switch to `return 0`.
    EXPECT_FALSE(w->act()) << "living::act returns 0 on the ACT_GUARD arm";
    EXPECT_EQ(nullptr, w->foe()) << "an empty level leaves the guard with no foe";
    EXPECT_EQ(ACT_GUARD, (int)w->act_type()) << "no sighting => no wake";
    EXPECT_FALSE(w->stats()->has_commands()) << "no foe => act_guard queues nothing";

    walker* orc = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, orc) << "hostile orc created";
    orc->set_team_num(1);
    orc->setxy(120, 100);

    w->stats()->clear_command();
    EXPECT_FALSE(w->act()) << "living::act still returns 0 on the ACT_GUARD arm";
    EXPECT_EQ(orc, w->foe()) << "act_guard latches find_near_foe's result";
    EXPECT_EQ(FACE_RIGHT, (int)w->curdir())
        << "act_guard face_delta()s toward a foe inside lineofsight*GRID_SIZE";
    EXPECT_EQ(ACT_RANDOM, (int)w->act_type())
        << "the 2026-07-11 wake rule converts a sighted non-hold-post guard to ACT_RANDOM";
    ASSERT_TRUE(w->stats()->has_commands()) << "act_guard queues a parting COMMAND_FIRE";
    EXPECT_EQ(COMMAND_FIRE, (int)w->stats()->commands.front().commandtype)
        << "the queued command is a fire, not a walk";
    EXPECT_EQ(20, (int)w->stats()->commands.front().com1)
        << "directional guard fire carries the foe x-delta";
    EXPECT_EQ(0, (int)w->stats()->commands.front().com2)
        << "directional guard fire carries the foe y-delta";

    // A hold-post guard sights the same foe, fires, and stays posted.
    w->set_act_type(ACT_GUARD);
    w->set_guard_hold_post(true);
    w->set_foe(nullptr);
    w->stats()->clear_command();
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));
    EXPECT_FALSE(w->act()) << "living::act returns 0 on the ACT_GUARD arm";
    EXPECT_EQ(orc, w->foe()) << "hold-post guards still acquire foes";
    EXPECT_EQ(ACT_GUARD, (int)w->act_type()) << "npc_flags bit 1 (hold post) blocks the wake";
    ASSERT_TRUE(w->stats()->has_commands()) << "hold-post guards still fire";
    EXPECT_EQ(COMMAND_FIRE, (int)w->stats()->commands.front().commandtype)
        << "hold-post guard queues COMMAND_FIRE";
    w->set_guard_hold_post(false);
}


TEST(LivingCombat, living_act_owner_dead_kills_summon)
{
    auto owner = make_living(FAMILY_MAGE);
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(owner && summoned) << "walkers created";
    if (!(owner && summoned))
        return;

    // Summoned living with an owner that is dead should die immediately.
    summoned->set_owner(owner.get());
    owner->set_dead(1);
    summoned->set_dead(0);

    bool r = summoned->act();
    (void)r;
    ASSERT_TRUE(summoned->dead()) << "summon should die when owner is dead";
}


TEST(LivingCombat, living_act_lifetime_expires_without_owner)
{
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(summoned) << "walker created";
    if (!summoned)
        return;

    // When lifetime is set and owner is missing, it should die.
    summoned->set_lifetime(1);
    summoned->set_owner(nullptr);
    summoned->set_dead(0);

    bool r = summoned->act();
    (void)r;
    ASSERT_TRUE(summoned->dead()) << "living with lifetime but no owner should die";
}


TEST(LivingCombat, living_act_fire_elemental_drain_charges_owner_and_heals_only_on_a_full_toll)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();

    auto owner = make_living(FAMILY_MAGE);
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    ASSERT_NE(nullptr, owner.get()) << "owner created";
    ASSERT_NE(nullptr, summoned.get()) << "summoned elemental created";

    summoned->set_owner(owner.get());
    summoned->set_dead(0);
    summoned->set_ani_type(ANI_WALK);
    // ACT_CONTROL returns straight out of living::act's switch, so nothing
    // after the on_act_living hook can touch these numbers.
    summoned->set_act_type(ACT_CONTROL);

    // Paid toll: owner at 20/30 hp (>= max/3) and 10 mp (>= 3).
    summoned->set_lifetime(5);
    summoned->stats()->set_max_hitpoints(10);
    summoned->stats()->set_hitpoints(5);
    summoned->set_regen_delay(100);  // freeze the elemental's own hp regen tick
    owner->stats()->set_max_hitpoints(30);
    owner->stats()->set_hitpoints(20);
    owner->stats()->set_magicpoints(10);

    (void)summoned->act();

    EXPECT_FLOAT_EQ(19.0f, owner->stats()->hitpoints())
        << "the drain charges the living owner exactly 1 hp";
    EXPECT_FLOAT_EQ(7.0f, owner->stats()->magicpoints())
        << "the drain charges the living owner exactly 3 mp";
    EXPECT_FLOAT_EQ(6.0f, summoned->stats()->hitpoints())
        << "both tolls paid => the elemental heals exactly 1 hp";
    EXPECT_EQ(4, (int)summoned->lifetime())
        << "a paid toll burns only living::act's own lifetime tick";

    // Unpaid mp toll: the hp toll is still taken, no heal, extra lifetime burn.
    summoned->set_lifetime(5);
    summoned->stats()->set_hitpoints(5);
    summoned->set_regen_delay(100);
    owner->stats()->set_hitpoints(20);
    owner->stats()->set_magicpoints(2);  // < 3 => mp toll cannot be paid

    (void)summoned->act();

    EXPECT_FLOAT_EQ(19.0f, owner->stats()->hitpoints())
        << "the hp toll is taken before the mp toll is even tested";
    EXPECT_FLOAT_EQ(2.0f, owner->stats()->magicpoints())
        << "an owner below 3 mp is not charged mp";
    EXPECT_FLOAT_EQ(5.0f, summoned->stats()->hitpoints())
        << "an unpaid toll heals the elemental nothing";
    EXPECT_EQ(3, (int)summoned->lifetime())
        << "an unpaid toll burns an extra lifetime tick on top of act's own";
}


// ---------------------------------------------------------------------------
// living::facing for all 8 directions
// ---------------------------------------------------------------------------

TEST(LivingCombat, living_facing_all_directions)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";

    // right
    ASSERT_EQ(FACE_RIGHT, (int)static_cast<living*>(w.get())->facing(10, 0)) << "right";
    // left
    ASSERT_EQ(FACE_LEFT, (int)static_cast<living*>(w.get())->facing(-10, 0)) << "left";
    // up
    ASSERT_EQ(FACE_UP, (int)static_cast<living*>(w.get())->facing(0, -10)) << "up";
    // down
    ASSERT_EQ(FACE_DOWN, (int)static_cast<living*>(w.get())->facing(0, 10)) << "down";
    // diagonals
    static_cast<living*>(w.get())->facing(10, -10);
    static_cast<living*>(w.get())->facing(-10, -10);
    static_cast<living*>(w.get())->facing(10, 10);
    static_cast<living*>(w.get())->facing(-10, 10);

}


// ---------------------------------------------------------------------------
// shove between allies
// ---------------------------------------------------------------------------

TEST(LivingCombat, living_shove_injects_walk_only_when_the_baby_step_is_passable)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();

    auto a = make_living(FAMILY_SOLDIER);
    auto b = make_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, a.get()) << "shover created";
    ASSERT_NE(nullptr, b.get()) << "shovee created";

    living* shover = static_cast<living*>(a.get());
    a->set_team_num(0);
    b->set_team_num(0);
    a->setxy(100, 100);
    b->setxy(105, 100);
    b->stats()->clear_command();

    // LCG seed 1: the first next(3) draw is 2, so the "make sure WE don't get
    // shoved" 1-in-3 gate lets the shove through.
    world.rng_.state_ = 1;
    EXPECT_EQ(1, (int)shover->shove(b.get(), 1, 0))
        << "an allied, non-ACT_CONTROL target on passable ground is shoved";
    ASSERT_TRUE(b->stats()->has_commands()) << "shove force-queues a walk on the target";
    EXPECT_EQ(COMMAND_WALK, (int)b->stats()->commands.front().commandtype)
        << "the injected command is COMMAND_WALK";
    EXPECT_EQ(4, (int)b->stats()->commands.front().commandcount)
        << "shove injects exactly 4 walk iterations";
    EXPECT_EQ(1, (int)b->stats()->commands.front().com1) << "shove carries the x delta";
    EXPECT_EQ(0, (int)b->stats()->commands.front().com2) << "shove carries the y delta";

    // 2026-07-10 livelock fix: when the injected baby step is terrain-blocked
    // the target's queue is left alone instead of being stolen every tick.
    b->stats()->clear_command();
    b->setxy(0, 100);
    world.rng_.state_ = 1;
    EXPECT_EQ(0, (int)shover->shove(b.get(), -1, 0))
        << "a shove whose baby step leaves the map is refused";
    EXPECT_FALSE(b->stats()->has_commands())
        << "a refused shove must not clear or replace the target's own command queue";
}


// ---------------------------------------------------------------------------
// living walk with multiple families
// ---------------------------------------------------------------------------

TEST(LivingCombat, living_walk_moves_on_matched_facing_and_only_turns_otherwise)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();

    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC };

    for (int i = 0; i < 6; i++) {
        auto w = make_living(families[i]);
        ASSERT_NE(nullptr, w.get()) << "family " << (int)families[i] << " walker created";
        living* lv = static_cast<living*>(w.get());
        w->set_act_type(ACT_RANDOM);
        w->setxy(100, 100);

        // Facing already matches the requested step: living::walk worldmove()s.
        w->set_curdir(static_cast<signed char>(lv->facing(1, 0)));
        w->set_enddir(static_cast<char>(lv->facing(1, 0)));
        const int x0 = (int)w->xpos();
        const int y0 = (int)w->ypos();
        ASSERT_TRUE(lv->walk(1.0f, 0.0f)) << "family " << (int)families[i] << " walks east";
        EXPECT_EQ(x0 + 1, (int)w->xpos())
            << "matched-facing walk moves one pixel east (family " << (int)families[i] << ")";
        EXPECT_EQ(y0, (int)w->ypos())
            << "an east step must not move y (family " << (int)families[i] << ")";

        // Facing differs: living::walk only records the new enddir and turns.
        w->set_curdir(static_cast<signed char>(FACE_UP));
        const int x1 = (int)w->xpos();
        EXPECT_TRUE(lv->walk(1.0f, 0.0f)) << "family " << (int)families[i] << " turns east";
        EXPECT_EQ(FACE_RIGHT, (int)w->enddir())
            << "a mismatched walk records the requested facing (family "
            << (int)families[i] << ")";
        EXPECT_EQ(x1, (int)w->xpos())
            << "a turn-only walk must not move (family " << (int)families[i] << ")";
    }
}


TEST(LivingCombat, living_headless_ctor_defaults)
{
    living w;
    ASSERT_EQ(1, (int)w.current_special()) << "headless living ctor should set current_special=1";
    ASSERT_EQ(0, (int)w.lifetime()) << "headless living ctor should set lifetime=0";
}


TEST(LivingCombat, living_act_bonus_rounds_and_dead_gate)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";

    w->set_act_type(ACT_CONTROL);
    w->set_bonus_rounds(1);
    bool r = w->act();
    ASSERT_TRUE(r) << "act should still succeed when bonus_rounds recurse";
    ASSERT_EQ(0, (int)w->bonus_rounds()) << "bonus_rounds should decrement to zero";

    w->set_dead(1);
    r = w->act();
    ASSERT_TRUE(!r) << "dead living should return false from act";
}


TEST(LivingCombat, living_act_lifetime_expiry_with_owner)
{
    auto owner = make_living(FAMILY_MAGE);
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(owner && summoned) << "walkers created";
    if (!(owner && summoned))
        return;

    summoned->set_owner(owner.get());
    summoned->set_lifetime(1);
    summoned->set_dead(0);
    bool r = summoned->act();
    (void)r;
    ASSERT_TRUE(summoned->dead()) << "summoned living should die when lifetime reaches zero";
}


TEST(LivingCombat, living_act_timers_charm_and_recoil_clamps)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";

    w->set_act_type(ACT_CONTROL);
    w->set_view_all(1);
    w->set_invulnerable_left(1);
    w->set_invisibility_left(0);
    w->set_outline(5);
    w->set_charm_left((1));
    w->set_team_num(4);
    w->set_real_team_num(2);
    w->set_speed_bonus_left(2);
    w->set_speed_bonus(3.0f);
    w->set_attack_lunge(0.2f);
    w->set_hit_recoil(0.3f);

    bool r = w->act();
    ASSERT_TRUE(r) << "ACT_CONTROL should return true";
    ASSERT_EQ(0, (int)w->view_all()) << "view_all should decrement";
    ASSERT_EQ(0, (int)w->invulnerable_left()) << "invulnerable_left should decrement";
    ASSERT_EQ(0, (int)w->outline()) << "outline should clear when not invisible";
    ASSERT_EQ(2, (int)w->team_num()) << "team should restore from real_team_num after charm expires";
    ASSERT_EQ(255, (int)w->real_team_num()) << "real_team_num should reset after charm expires";
    ASSERT_EQ(1, (int)w->speed_bonus_left()) << "speed bonus timer should decrement";
    ASSERT_EQ(0, (int)w->attack_lunge()) << "attack_lunge should clamp to zero";
    ASSERT_EQ(0, (int)w->hit_recoil()) << "hit_recoil should clamp to zero";
}


TEST(LivingCombat, living_act_nonpassable_tile_damage_kills)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";

    cfg.apply_setting("effects", "damage_numbers", "on");
    w->set_act_type(ACT_CONTROL);
    w->set_xpos(-100);
    w->set_ypos(-100);
    w->set_flight_left(0);
    w->stats()->set_hitpoints(1);
    w->stats()->set_magicpoints(0);
    w->stats()->set_max_magicpoints(0);

    (void)w->act();
    ASSERT_TRUE(w->dead()) << "non-flying living on an impassable tile should die at 1 HP";
}


TEST(LivingCombat, living_walk_and_do_action_edge_branches)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(10, 10);
    w->set_curdir(FACE_LEFT);

    bool moved = static_cast<living*>(w.get())->walk(-1000, 0);
    ASSERT_TRUE(!moved) << "walk should fail when target would be outside map bounds";

    w->set_action(0);
    bool a = static_cast<living*>(w.get())->do_action();
    ASSERT_TRUE(!a) << "do_action should return false when action=0";

    w->set_action(ACTION_FOLLOW);
    w->set_foe(w.get());
    a = static_cast<living*>(w.get())->do_action();
    ASSERT_TRUE(!a) << "ACTION_FOLLOW with existing foe should return false";

    w->set_foe(nullptr);
    w->set_leader(w.get());
    w->leader()->set_foe(w.get());
    a = static_cast<living*>(w.get())->do_action();
    ASSERT_TRUE(!a) << "ACTION_FOLLOW should copy leader foe then return false";
    ASSERT_TRUE(w->foe() == w.get()) << "foe should be copied from leader";
}


TEST(LivingCombat, living_facing_threshold_edges_and_summon_and_autoattackable)
{
    auto w = make_living(FAMILY_CLERIC);
    ASSERT_TRUE(w != nullptr) << "walker created";
    living* lv = static_cast<living*>(w.get());

    ASSERT_EQ(FACE_DOWN, (int)lv->facing(1, 3)) << "x>0 slope>2414 => down";
    ASSERT_EQ(FACE_DOWN_RIGHT, (int)lv->facing(1, 1)) << "x>0 slope>414 => down-right";
    ASSERT_EQ(FACE_RIGHT, (int)lv->facing(1, 0)) << "x>0 slope>-414 => right";
    ASSERT_EQ(FACE_UP_RIGHT, (int)lv->facing(1, -1)) << "x>0 slope>-2414 => up-right";
    ASSERT_EQ(FACE_UP, (int)lv->facing(1, -3)) << "x>0 steep negative => up";

    ASSERT_EQ(FACE_UP, (int)lv->facing(-1, -3)) << "x<0 slope>2414 => up";
    ASSERT_EQ(FACE_UP_LEFT, (int)lv->facing(-1, -1)) << "x<0 slope>414 => up-left";
    ASSERT_EQ(FACE_LEFT, (int)lv->facing(-1, 0)) << "x<0 slope>-414 => left";
    ASSERT_EQ(FACE_DOWN_LEFT, (int)lv->facing(-1, 1)) << "x<0 slope>-2414 => down-left";
    ASSERT_EQ(FACE_DOWN, (int)lv->facing(-1, 3)) << "x<0 steep negative => down";

    walker* summoned = lv->do_summon(FAMILY_GHOST, 123);
    ASSERT_TRUE(summoned != nullptr) << "do_summon should create a living walker";
    if (!summoned)
        return;
    ASSERT_TRUE(summoned->owner() == w.get()) << "summoned owner should be summoner";
    ASSERT_EQ(123, (int)summoned->lifetime()) << "summoned lifetime should match input";

    walker* fx = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_BLOOD);
    ASSERT_TRUE(fx != nullptr) << "fx object created";
    bool aa = walkerIsAutoAttackable(fx);
    ASSERT_TRUE(!aa) << "FX should not be auto-attackable";
}


TEST(LivingCombat, living_set_difficulty_delay_loops_and_clamps)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_team_num(0);
    static_cast<living*>(w.get())->set_difficulty(200);
    ASSERT_TRUE(w->stats()->heal_per_round() > 0) << "high level should force heal-per-round loop increments";
    ASSERT_TRUE(w->stats()->magic_per_round() > 0) << "high level should force magic-per-round loop increments";
}


TEST(LivingCombat, living_do_action_follow_leader_null_and_command_paths)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    lv->set_action(ACTION_FOLLOW);
    lv->set_foe(nullptr);
    lv->set_leader(nullptr);

    bool r = lv->do_action();
    ASSERT_TRUE(!r) << "ACTION_FOLLOW without leader should return false";

    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_TRUE(leader != nullptr) << "leader created";
    if (!leader)
        return;
    leader->set_team_num(lv->team_num());
    leader->set_user(0);
    leader->setxy(static_cast<short>(lv->xpos() + 8), lv->ypos());
    leader->set_foe(nullptr);

    r = lv->do_action();
    ASSERT_TRUE(r) << "ACTION_FOLLOW with leader and no foe should return true";
    ASSERT_TRUE(lv->stats()->has_commands()) << "ACTION_FOLLOW should enqueue follow command";
}


TEST(LivingCombat, living_facing_vertical_and_boundary_cases)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    ASSERT_EQ(FACE_DOWN, (int)lv->facing(0, 5)) << "x==0 positive y should face down";
    ASSERT_EQ(FACE_UP, (int)lv->facing(0, -5)) << "x==0 negative y should face up";
    ASSERT_EQ(FACE_DOWN_RIGHT, (int)lv->facing(5, 3)) << "positive boundary slope should face down-right";
    ASSERT_EQ(FACE_UP_LEFT, (int)lv->facing(-5, -3)) << "negative boundary slope should face up-left";
}


TEST(LivingCombat, obmap_guard_and_hash_and_move_branches)
{
    obmap map;

    ASSERT_EQ(1, (int)map.query_list(nullptr, 0, 0)) << "query_list null should return pass";

    walker dead_w;
    dead_w.set_dead(1);
    dead_w.set_sizex(4);
    dead_w.set_sizey(4);
    ASSERT_EQ(1, (int)map.query_list(&dead_w, 0, 0)) << "query_list dead walker should return pass";

    ASSERT_EQ(0, (int)map.hash(-1)) << "negative small values hash to 0 with integer truncation";
    ASSERT_EQ(199, (int)map.hash(-1000)) << "large negative hash should clamp to 199";
    ASSERT_EQ(199, (int)map.hash(10000)) << "large hash should clamp to 199";

    walker orphan;
    orphan.set_sizex(8);
    orphan.set_sizey(8);
    orphan.setxy(-1, -1);
    ASSERT_EQ(0, (int)map.remove(&orphan)) << "remove unknown negative-position walker should fail";

    walker* live = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(live != nullptr) << "live walker created";
    if (!live)
        return;
    live->set_sizex(8);
    live->set_sizey(8);
    live->setxy(100, 100);

    ASSERT_EQ(1, (int)map.add(live, 100, 100)) << "add should succeed";
    ASSERT_EQ(1, (int)map.move(live, 100, 100)) << "move same pos should succeed";
    ASSERT_EQ(1, (int)map.remove(live)) << "remove tracked walker should succeed";
}


TEST(LivingCombat, living_facing_zero_vector_and_obmap_door_paths)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    ASSERT_EQ(FACE_UP, (int)lv->facing(0, 0)) << "facing with zero vector should use x==0/y<=0 branch";
}


TEST(LivingCombat, obmap_remove_stale_and_collide_axis_reject_paths)
{
    obmap map;

    walker stale;
    stale.set_order_family(Order::Living, FAMILY_SOLDIER);
    stale.set_sizex(12);
    stale.set_sizey(12);
    stale.setxy(96, 96);

    // Simulate stale bookkeeping: present in pile map, absent in walker_to_pos.
    auto cell = std::make_pair(obmap::hash(stale.xpos()), obmap::hash(stale.ypos()));
    map.pos_to_walker[cell].push_back(&stale);
    ASSERT_EQ(1, (int)map.remove(&stale)) << "remove should clean stale pointer via bounded fallback";
    ASSERT_TRUE(map.pos_to_walker.find(cell) == map.pos_to_walker.end()) << "fallback remove should erase empty cell pile";

    ASSERT_EQ(0, (int)collide(100, 100, 10, 10, 200, 100, 10, 10)) << "collide should reject separated x-right case";
    ASSERT_EQ(0, (int)collide(200, 100, 10, 10, 100, 100, 10, 10)) << "collide should reject separated x-left case";
    ASSERT_EQ(0, (int)collide(100, 200, 10, 10, 100, 100, 10, 10)) << "collide should reject separated y-up case";
    ASSERT_EQ(0, (int)collide(100, 100, 10, 10, 100, 200, 10, 10)) << "collide should reject separated y-down case";
}


TEST(LivingCombat, obmap_query_list_door_unlock_and_lock_branches)
{
    obmap map;

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr) << "actor created";
    if (!actor)
        return;
    actor->set_sizex(12);
    actor->set_sizey(12);
    actor->setxy(64, 64);
    actor->set_user(0);
    actor->set_skip_exit(0);
    actor->set_team_num(1);

    // Locked door branch: missing key should block and set skip_exit.
    walker* locked_door = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_TRUE(locked_door != nullptr) << "locked door created";
    if (!locked_door)
        return;
    locked_door->stats()->set_level(3);
    locked_door->set_sizex(12);
    locked_door->set_sizey(12);
    locked_door->setxy(64, 64);
    locked_door->set_team_num(2);
    ASSERT_EQ(1, (int)map.add(locked_door, 64, 64)) << "add locked door";

    actor->set_keys(0);
    short pass = map.query_list(actor, 64, 64);
    ASSERT_EQ(0, (int)pass) << "locked door without key should block movement";
    ASSERT_TRUE(actor->skip_exit() >= 10) << "locked door branch should set skip_exit cooldown";
    (void)map.remove(locked_door);

    // Unlocked door path with normal collision: should return blocked for this round.
    walker* unlocked_door = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_TRUE(unlocked_door != nullptr) << "unlocked door created";
    if (!unlocked_door)
        return;
    unlocked_door->stats()->set_level(1);
    unlocked_door->set_sizex(12);
    unlocked_door->set_sizey(12);
    unlocked_door->setxy(64, 64);
    unlocked_door->set_team_num(2);
    ASSERT_EQ(1, (int)map.add(unlocked_door, 64, 64)) << "add unlocked door";

    actor->set_keys(2); // 2^level where level=1
    actor->stats()->set_bit_flags(BIT_NO_COLLIDE, 0);
    pass = map.query_list(actor, 64, 64);
    ASSERT_EQ(0, (int)pass) << "unlocked door should still block for current query tick";
    ASSERT_TRUE(unlocked_door->dead() == 1) << "unlocked door should be marked dead";
    (void)map.remove(unlocked_door);

    // Unlocked door + BIT_NO_COLLIDE path should return pass-through.
    walker* nocollide_door = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_TRUE(nocollide_door != nullptr) << "nocollide door created";
    if (!nocollide_door)
        return;
    nocollide_door->stats()->set_level(1);
    nocollide_door->set_sizex(12);
    nocollide_door->set_sizey(12);
    nocollide_door->setxy(64, 64);
    nocollide_door->set_team_num(2);
    ASSERT_EQ(1, (int)map.add(nocollide_door, 64, 64)) << "add nocollide door";
    actor->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    pass = map.query_list(actor, 64, 64);
    ASSERT_EQ(1, (int)pass) << "BIT_NO_COLLIDE should pass through opened door";
    (void)map.remove(nocollide_door);
}


TEST(LivingCombat, living_act_invisibility_skip_exit_and_action_command_paths)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->set_act_type(ACT_GUARD);
    w->set_invisibility_left(2);
    w->set_outline(7);
    w->set_skip_exit(2);
    w->set_action(ACTION_FOLLOW);
    w->set_user(-1);
    w->set_foe(nullptr);
    w->set_leader(nullptr);
    bool r = w->act();
    (void)r;
    ASSERT_EQ(1, (int)w->invisibility_left()) << "invisibility should decrement when active";
    ASSERT_EQ(7, (int)w->outline()) << "outline should remain while invisibility is active";
    ASSERT_EQ(1, (int)w->skip_exit()) << "skip_exit should decrement";

    // Unknown act_type should take default branch and return false.
    w->set_act_type(static_cast<char>(99));
    r = w->act();
    ASSERT_TRUE(!r) << "unknown act type should return false";
}


TEST(LivingCombat, living_act_command_execution_and_autoattackable_edges)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    lv->set_act_type(ACT_GUARD);
    lv->set_ani_type(ANI_WALK);
    lv->stats()->set_command(COMMAND_WALK, 2, 1, 0);
    bool r = lv->act();
    ASSERT_TRUE(r) << "act should return true when command execution returns non-zero";

    walker* non_auto_weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    walker* auto_weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_DOOR);
    walker* non_attackable = og::runtime::current_session->myscreen_->world().add_ob(Order::Treasure, FAMILY_GOLD_BAR);
    ASSERT_NE(nullptr, non_auto_weap) << "knife weapon created";
    ASSERT_NE(nullptr, auto_weap) << "door weapon created";
    ASSERT_NE(nullptr, non_attackable) << "treasure created";
    // Order::Weapon defers to the weapon family descriptor, both ways round.
    EXPECT_FALSE(walkerIsAutoAttackable(non_auto_weap))
        << "knife declares is_auto_attackable=false (weapon-00-knife.lua)";
    EXPECT_TRUE(walkerIsAutoAttackable(auto_weap))
        << "door declares is_auto_attackable=true (weapon-18-door.lua)";
    EXPECT_FALSE(walkerIsAutoAttackable(non_attackable))
        << "non-living non-generator non-weapon should not be auto-attackable";
}


TEST(LivingCombat, living_round7_act_random_and_do_action_targeted_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    auto actor = make_living(FAMILY_SOLDIER);
    auto foe = make_living(FAMILY_ORC);
    ASSERT_TRUE(actor && foe) << "actor and foe created";
    if (!(actor && foe))
        return;

    actor->set_team_num(0);
    foe->set_team_num(1);
    actor->setxy(100, 100);
    foe->setxy(120, 100);
    actor->set_lineofsight(40);
    actor->set_foe(foe.get());
    actor->set_ani_type(ANI_WALK);
    actor->set_action(0);
    // fire_check aims via lastx/lasty and denies on a facing mismatch, so keep
    // the heading and the facing pointing at the foe (due east).
    actor->set_curdir(static_cast<signed char>(FACE_RIGHT));
    actor->set_enddir(static_cast<char>(FACE_RIGHT));
    actor->set_lastx(1.0f);
    actor->set_lasty(0.0f);
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    // The ACT_RANDOM arm of living::act draws next(5) twice before it decides.
    // LCG seed 6 yields 4 then 0, which is the one seed arm that reaches
    // living::act_random; seed 1 yields 3 then 1 and takes the 4-of-5
    // "snap facing and search" arm instead. Both are pinned below.

    // living::act_random, fire_check true path through act() dispatch.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    actor->set_act_type(ACT_RANDOM);
    actor->stats()->clear_command();
    actor->set_foe(foe.get());
    world.rng_.state_ = 6;
    bool ok = actor->act();
    // living::act's `act_random(); break;` arm falls out of the switch to
    // `return 0` — the queued command, not the return value, is the observable.
    EXPECT_FALSE(ok) << "living::act discards act_random's 1 on the 1-in-5 arm";
    ASSERT_TRUE(actor->stats()->has_commands()) << "act_random must queue something";
    EXPECT_EQ(COMMAND_FIRE, (int)actor->stats()->commands.front().commandtype)
        << "an allowed fire_check makes act_random set_command(COMMAND_FIRE)";
    EXPECT_EQ(20, (int)actor->stats()->commands.front().com1)
        << "COMMAND_FIRE carries the foe x-delta";
    EXPECT_EQ(0, (int)actor->stats()->commands.front().com2)
        << "COMMAND_FIRE carries the foe y-delta";

    // living::act_random fire_check false path -> turn + COMMAND_SEARCH.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    actor->set_act_type(ACT_RANDOM);
    actor->stats()->clear_command();
    actor->set_foe(foe.get());
    actor->set_ani_type(ANI_WALK);  // the fire arm above left us mid-attack animation
    actor->set_curdir(static_cast<signed char>(FACE_RIGHT));
    actor->set_enddir(static_cast<char>(FACE_RIGHT));
    world.rng_.state_ = 6;
    ok = actor->act();
    EXPECT_FALSE(ok) << "living::act discards act_random's 1 on the 1-in-5 arm";
    ASSERT_TRUE(actor->stats()->has_commands()) << "the blocked arm must queue something";
    EXPECT_EQ(COMMAND_SEARCH, (int)actor->stats()->commands.front().commandtype)
        << "a denied fire_check makes act_random fall through to COMMAND_SEARCH";
    EXPECT_EQ(200, (int)actor->stats()->commands.front().commandcount)
        << "the act_random search is queued for 200 iterations";

    // The 4-of-5 arm of living::act's ACT_RANDOM case: snap the facing to an
    // even direction and search for 300.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    actor->set_act_type(ACT_RANDOM);
    actor->stats()->clear_command();
    actor->set_foe(foe.get());
    actor->set_ani_type(ANI_WALK);
    actor->set_curdir(static_cast<signed char>(FACE_DOWN_RIGHT));
    actor->set_enddir(static_cast<char>(FACE_DOWN_RIGHT));
    world.rng_.state_ = 1;
    ok = actor->act();
    ASSERT_TRUE(ok) << "the 4-of-5 ACT_RANDOM arm returns 1";
    EXPECT_EQ(FACE_RIGHT, (int)actor->curdir())
        << "the 4-of-5 arm snaps curdir to (enddir/2)*2 (FACE_DOWN_RIGHT -> FACE_RIGHT)";
    EXPECT_EQ(FACE_RIGHT, (int)actor->enddir())
        << "the 4-of-5 arm snaps enddir to the same even facing";
    ASSERT_TRUE(actor->stats()->has_commands()) << "the 4-of-5 arm queues a search";
    EXPECT_EQ(COMMAND_SEARCH, (int)actor->stats()->commands.front().commandtype)
        << "the 4-of-5 arm queues COMMAND_SEARCH";
    EXPECT_EQ(300, (int)actor->stats()->commands.front().commandcount)
        << "the 4-of-5 search is queued for 300 iterations";

    // Same arm with no foe at all: seed 1's third draw is odd, so it takes the
    // random-walk branch rather than the find_far_foe branch.
    actor->set_act_type(ACT_RANDOM);
    actor->stats()->clear_command();
    actor->set_foe(nullptr);
    actor->set_ani_type(ANI_WALK);
    foe->set_team_num(0);  // friendly => find_near_foe/find_far_foe find nobody
    world.rng_.state_ = 1;
    ok = actor->act();
    ASSERT_TRUE(ok) << "the foeless 4-of-5 arm returns 1";
    EXPECT_EQ(nullptr, actor->foe()) << "no hostile in the level => no foe is latched";
    ASSERT_TRUE(actor->stats()->has_commands()) << "the foeless arm queues a random walk";
    EXPECT_EQ(COMMAND_WALK, (int)actor->stats()->commands.front().commandtype)
        << "COMMAND_RANDOM_WALK is rewritten into a COMMAND_WALK with random deltas";
    EXPECT_EQ(20, (int)actor->stats()->commands.front().commandcount)
        << "the foeless random walk is queued for 20 iterations";
    foe->set_team_num(1);

    // living::do_action default branch.
    actor->set_action(static_cast<char>(99));
    ASSERT_TRUE(!static_cast<living*>(actor.get())->do_action()) << "unknown action should return false";
}


TEST(LivingCombat, living_round8_dead_outline_forestwalk_and_offmap_walk_paths)
{
    // dead gate: line-early return path.
    auto dead_w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(dead_w != nullptr) << "dead gate walker created";
    if (!dead_w)
        return;
    dead_w->set_dead(1);
    ASSERT_TRUE(!dead_w->act()) << "dead living should return false immediately";

    auto w = make_living(FAMILY_DRUID);
    ASSERT_TRUE(w != nullptr) << "forestwalk walker created";
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    w->set_act_type(ACT_CONTROL);

    // invisibility expiry should clear outline.
    w->set_invisibility_left(0);
    w->set_outline(7);
    (void)w->act();
    ASSERT_EQ(0, (int)w->outline()) << "outline should clear when invisibility is exhausted";

    // Forestwalk myguy dex branch with clamp-to-zero temp.
    w->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    if (w->myguy)
        w->myguy->dexterity = 120;
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().grid.data[static_cast<std::size_t>((w->ypos() / GRID_SIZE) * og::runtime::current_session->myscreen_->world().grid.w + (w->xpos() / GRID_SIZE))] = PIX_TREE_B1;
    const float normal = lv->normal_stepsize();
    (void)w->act();
    ASSERT_TRUE(lv->stepsize() >= 1.0f) << "forestwalk path should keep stepsize >= 1";
    ASSERT_TRUE(lv->stepsize() <= normal + 0.1f) << "high dex forestwalk branch should clamp temp and avoid negative speed penalty";

    // Off-map walk rejection branch.
    w->set_curdir(static_cast<signed char>(lv->facing(-1000, 0)));
    ASSERT_TRUE(!lv->walk(-1000, 0)) << "walk should reject off-map target coordinates";
}


TEST(LivingCombat, obmap_round8_hash_negative_and_large_clamp_paths)
{
    ASSERT_EQ(199, (int)obmap::hash(-100)) << "hash should clamp negative coordinates to 199 bucket";
    ASSERT_EQ(199, (int)obmap::hash(9999)) << "hash should clamp very large coordinates to 199 bucket";
}


TEST(LivingCombat, living_round10_facing_and_action_follow_branch_matrix)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    auto actor = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr) << "actor created";
    if (!actor)
        return;

    living* lv = static_cast<living*>(actor.get());

    // Explicitly cover x==0 vertical branch and x<0 slope ladder.
    ASSERT_EQ(FACE_DOWN, (int)lv->facing(0, 1)) << "facing with x==0 and positive y should be down";
    ASSERT_EQ(FACE_UP, (int)lv->facing(0, -1)) << "facing with x==0 and non-positive y should be up";
    ASSERT_EQ(FACE_UP_LEFT, (int)lv->facing(-2, -1)) << "x<0 with positive slope should be up-left";
    ASSERT_EQ(FACE_LEFT, (int)lv->facing(-3, 0)) << "x<0 with flat slope should be left";
    ASSERT_EQ(FACE_DOWN_LEFT, (int)lv->facing(-2, 1)) << "x<0 with small negative slope should be down-left";

    // do_action: action==0 guard.
    actor->set_action(0);
    ASSERT_TRUE(!lv->do_action()) << "do_action should return false when action is unset";

    // do_action ACTION_FOLLOW with existing foe returns false immediately.
    auto foe = make_living(FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (foe)
    {
        actor->set_action(ACTION_FOLLOW);
        actor->set_foe(foe.get());
        ASSERT_TRUE(!lv->do_action()) << "ACTION_FOLLOW should return false when actor already has a foe";
    }

    // do_action ACTION_FOLLOW with no leader in level should return false.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    actor = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr) << "actor recreated";
    if (!actor)
        return;
    lv = static_cast<living*>(actor.get());
    actor->set_action(ACTION_FOLLOW);
    actor->set_foe(nullptr);
    actor->set_team_num(1); // no team-1 players in level
    ASSERT_TRUE(!lv->do_action()) << "ACTION_FOLLOW should return false when no nearest player is found";

    // do_action ACTION_FOLLOW with leader->foe() copies foe and returns false.
    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* leader_foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(leader && leader_foe) << "leader and leader foe created";
    if (leader && leader_foe)
    {
        actor->set_team_num(2);
        leader->set_team_num(0);
        leader->set_user(0);
        leader_foe->set_team_num(1);
        leader->set_foe(leader_foe);
        leader->setxy(actor->xpos() + 8, actor->ypos() + 8);
        ASSERT_TRUE(!lv->do_action()) << "ACTION_FOLLOW should return false after adopting leader foe";
        ASSERT_EQ(leader, actor->leader()) << "ACTION_FOLLOW latches find_nearest_player as the leader";
        ASSERT_EQ(leader_foe, actor->foe()) << "ACTION_FOLLOW adopts the leader's foe";
    }

    // do_action ACTION_FOLLOW with leader and no foe enqueues follow command.
    if (leader)
    {
        actor->set_foe(nullptr);
        leader->set_foe(nullptr);
        const bool follow_res = lv->do_action();
        ASSERT_TRUE(follow_res) << "ACTION_FOLLOW with leader and no foe should enqueue follow command";
        ASSERT_EQ(leader, actor->leader());
        ASSERT_TRUE(actor->stats()->has_commands());
    }
}


TEST(LivingCombat, obmap_round10_add_remove_move_and_fallback_paths)
{
    obmap map;

    ASSERT_EQ(0, (int)map.remove(nullptr)) << "remove(nullptr) should fail";
    ASSERT_EQ(0, (int)map.add(nullptr, 0, 0)) << "add(nullptr) should fail";

    walker a;
    a.set_sizex(12);
    a.set_sizey(12);
    a.setxy(32, 32);

    ASSERT_EQ(0, (int)map.add(&a, -1, 0)) << "add should reject negative x";
    ASSERT_EQ(1, (int)map.move(&a, a.xpos(), a.ypos())) << "move no-op should succeed";

    ASSERT_EQ(1, (int)map.add(&a, 32, 32)) << "add should succeed for valid object";
    ASSERT_EQ(1, (int)map.add(&a, 32, 32)) << "add duplicate should remove old occupancy then re-add";
    ASSERT_EQ(1, (int)map.remove(&a)) << "remove should succeed for mapped object";

    // Fallback remove path: object not in walker_to_pos but present in nearby pile cells.
    walker b;
    b.set_sizex(12);
    b.set_sizey(12);
    b.setxy(64, 64);
    auto cell = std::make_pair(obmap::hash(b.xpos()), obmap::hash(b.ypos()));
    map.pos_to_walker[cell].push_back(&b);
    ASSERT_EQ(1, (int)map.remove(&b)) << "remove fallback should clear stale pile entry";
    ASSERT_TRUE(map.pos_to_walker.find(cell) == map.pos_to_walker.end()) << "fallback remove should erase emptied cell";

    // Fallback remove early-return path when stale object has negative coordinates.
    walker c;
    c.set_sizex(12);
    c.set_sizey(12);
    c.setxy(-5, -5);
    ASSERT_EQ(0, (int)map.remove(&c)) << "remove fallback should reject negative-position stale object";
}


TEST(LivingCombat, obmap_round11_stale_query_and_helper_accessors_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    obmap map;

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr) << "actor created";
    if (!actor)
        return;
    actor->set_team_num(0);
    actor->set_sizex(12);
    actor->set_sizey(12);
    actor->setxy(64, 64);

    // query_list should ignore stale entries that are not tracked in walker_to_pos.
    walker stale;
    stale.set_order_family(Order::Living, FAMILY_ORC);
    stale.set_team_num(1);
    stale.set_sizex(12);
    stale.set_sizey(12);
    stale.setxy(64, 64);
    auto cell = std::make_pair(obmap::hash(stale.xpos()), obmap::hash(stale.ypos()));
    map.pos_to_walker[cell].push_back(&stale); // intentionally stale (not added/tracked)
    ASSERT_EQ(1, (int)map.query_list(actor, actor->xpos(), actor->ypos())) << "query_list should skip stale pile entries safely";

    // weapon-vs-weapon "miss" branch in ob_pass_check.
    walker* w1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_ARROW);
    walker* w2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(w1 != nullptr && w2 != nullptr) << "weapon fixtures created";
    if (!(w1 && w2))
        return;
    w1->set_team_num(1);
    w2->set_team_num(2);
    w1->set_sizey(12);
    w1->set_sizex(12);
    w2->set_sizey(12);
    w2->set_sizex(12);
    w1->setxy(64, 64);
    w2->setxy(64, 64);
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1; // next(10)=8 (>3) for miss branch
    ASSERT_EQ(1, (int)map.add(w2, w2->xpos(), w2->ypos())) << "track second weapon";
    const int weapon_miss_pass = map.query_list(w1, w1->xpos(), w1->ypos());
    ASSERT_EQ(1, weapon_miss_pass) << "weapon should pass when colliding weapon-miss branch executes";

    // obmap_get_list/unhash helpers.
    auto& pile = map.obmap_get_list(64, 64);
    ASSERT_TRUE(!pile.empty()) << "obmap_get_list should expose hashed pile";
    ASSERT_EQ(64, (int)obmap::unhash(obmap::hash(64))) << "unhash(hash(x)) should map to cell origin";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


// Regression test for the walker_to_pos.find() infinite-loop hang.
// The bug: ob_pass_check receives a reference to a pile stored inside
// pos_to_walker. When a collision handler (walker::death → obmap::remove)
// erases that pile entry, the reference becomes dangling.  Subsequent
// iteration reads freed memory, feeding corrupt pointers to
// walker_to_pos.find() which loops forever on the std::map RB-tree.
// The fix snapshots the pile before passing it to ob_pass_check.
TEST(LivingCombat, obmap_query_list_no_hang_when_pile_erased_during_collision)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Actor: a soldier with the key to open a door (key bit 2 = level 1).
    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr) << "actor created";
    if (!actor) return;
    actor->set_sizex(12);
    actor->set_sizey(12);
    actor->setxy(64, 64);
    actor->set_keys(2); // 2^1 → unlocks level-1 doors
    actor->set_team_num(0);
    actor->set_user(0);

    // Door: a FAMILY_DOOR weapon whose death() calls obmap::remove(this),
    // which may erase the pos_to_walker pile that query_list is iterating.
    walker* door = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_TRUE(door != nullptr) << "door created";
    if (!door) return;
    door->stats()->set_level(1);
    door->set_sizex(12);
    door->set_sizey(12);
    door->setxy(64, 64);
    door->set_team_num(1); // different team from actor

    // Use the level's own obmap so that death() → remove() hits the same
    // map that query_list is iterating.
    obmap* map = og::runtime::current_session->myscreen_->world().myobmap.get();
    map->add(actor, actor->xpos(), actor->ypos());
    map->add(door, door->xpos(), door->ypos());

    // This would hang (infinite loop in walker_to_pos.find) before the fix.
    // The door collision triggers door->death() → obmap::remove(door),
    // which can erase the pile entry from pos_to_walker.
    short result = map->query_list(actor, actor->xpos(), actor->ypos());
    // Door should block movement for this frame (result 0) but not hang.
    ASSERT_EQ(0, (int)result) << "door collision should block without hanging";
    ASSERT_TRUE(door->dead() == 1) << "door should be dead after unlock collision";

    og::runtime::current_session->myscreen_->world().delete_objects();
}
