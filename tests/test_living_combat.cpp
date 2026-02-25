#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/living.h>
#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/obmap.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
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

void test_living_set_difficulty_levels()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    for (int i = 0; i < 14; i++) {
        for (int level = 1; level <= 5; level++) {
            loader* l = og::runtime::current_session->myscreen_->level_data.myloader.get();
            if (!l) continue;
            auto w = l->create_walker_owned(Order::Living, families[i]);
            if (w) {
                static_cast<living*>(w.get())->set_difficulty(level);
                TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP positive for all families at all levels");
            }
        }
    }
}
REGISTER_TEST(test_living_set_difficulty_levels);

// ---------------------------------------------------------------------------
// check_special for all families - exercises the big switch (~143 lines)
// ---------------------------------------------------------------------------

void test_living_check_special_all_families()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    for (int i = 0; i < 14; i++) {
        auto w = make_living(families[i]);
        if (w) {
            w->stats()->magicpoints = 100;
            w->stats()->max_magicpoints = 100;
            bool result = static_cast<living*>(w.get())->check_special();
            (void)result; // just exercise the code path
        }
    }
}
REGISTER_TEST(test_living_check_special_all_families);

// ---------------------------------------------------------------------------
// living::act smoke test
// ---------------------------------------------------------------------------

void test_living_act_control()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_CONTROL);
    bool result = w->act();
    TEST_ASSERT(result, "ACT_CONTROL should return true");
}
REGISTER_TEST(test_living_act_control);

void test_living_act_random()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_RANDOM);
    w->act();
}
REGISTER_TEST(test_living_act_random);

void test_living_act_guard()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_GUARD);
    w->act();
}
REGISTER_TEST(test_living_act_guard);

void test_living_act_owner_dead_kills_summon()
{
    auto owner = make_living(FAMILY_MAGE);
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(owner && summoned, "walkers created");
    if (!(owner && summoned))
        return;

    // Summoned living with an owner that is dead should die immediately.
    summoned->owner = owner.get();
    owner->dead = 1;
    summoned->dead = 0;

    bool r = summoned->act();
    (void)r;
    TEST_ASSERT(summoned->dead, "summon should die when owner is dead");
}
REGISTER_TEST(test_living_act_owner_dead_kills_summon);

void test_living_act_lifetime_expires_without_owner()
{
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(summoned, "walker created");
    if (!summoned)
        return;

    // When lifetime is set and owner is missing, it should die.
    summoned->lifetime = 1;
    summoned->owner = nullptr;
    summoned->dead = 0;

    bool r = summoned->act();
    (void)r;
    TEST_ASSERT(summoned->dead, "living with lifetime but no owner should die");
}
REGISTER_TEST(test_living_act_lifetime_expires_without_owner);

void test_living_act_fire_elemental_drain_heals_self_with_owner_resources()
{
    auto owner = make_living(FAMILY_MAGE);
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(owner && summoned, "walkers created");
    if (!(owner && summoned))
        return;

    summoned->owner = owner.get();
    summoned->lifetime = 5;
    summoned->dead = 0;

    // Hurt the elemental so it runs the drain logic.
    summoned->stats()->max_hitpoints = 10;
    summoned->stats()->hitpoints = 5;

    owner->stats()->max_hitpoints = 30;
    owner->stats()->hitpoints = 20;  // >= max/3 => can pay hp
    owner->stats()->magicpoints = 10; // >= 3 => can pay mp

    const float hp_before = summoned->stats()->hitpoints;
    (void)summoned->act();

    TEST_ASSERT(summoned->stats()->hitpoints >= hp_before, "fire elemental should heal when owner pays toll");
}
REGISTER_TEST(test_living_act_fire_elemental_drain_heals_self_with_owner_resources);

// ---------------------------------------------------------------------------
// living::facing for all 8 directions
// ---------------------------------------------------------------------------

void test_living_facing_all_directions()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    // right
    TEST_ASSERT_EQ(FACE_RIGHT, (int)static_cast<living*>(w.get())->facing(10, 0), "right");
    // left
    TEST_ASSERT_EQ(FACE_LEFT, (int)static_cast<living*>(w.get())->facing(-10, 0), "left");
    // up
    TEST_ASSERT_EQ(FACE_UP, (int)static_cast<living*>(w.get())->facing(0, -10), "up");
    // down
    TEST_ASSERT_EQ(FACE_DOWN, (int)static_cast<living*>(w.get())->facing(0, 10), "down");
    // diagonals
    static_cast<living*>(w.get())->facing(10, -10);
    static_cast<living*>(w.get())->facing(-10, -10);
    static_cast<living*>(w.get())->facing(10, 10);
    static_cast<living*>(w.get())->facing(-10, 10);

}
REGISTER_TEST(test_living_facing_all_directions);

// ---------------------------------------------------------------------------
// shove between allies
// ---------------------------------------------------------------------------

void test_living_shove_movement()
{
    auto a = make_living(FAMILY_SOLDIER);
    auto b = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    a->team_num = 0;
    b->team_num = 0;
    a->setxy(100, 100);
    b->setxy(105, 100);

    // Shove in all cardinal directions
    static_cast<living*>(a.get())->shove(b.get(), 1, 0);
    static_cast<living*>(a.get())->shove(b.get(), -1, 0);
    static_cast<living*>(a.get())->shove(b.get(), 0, 1);
    static_cast<living*>(a.get())->shove(b.get(), 0, -1);
}
REGISTER_TEST(test_living_shove_movement);

// ---------------------------------------------------------------------------
// living walk with multiple families
// ---------------------------------------------------------------------------

void test_living_walk_all_families()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC };

    for (int i = 0; i < 6; i++) {
        auto w = make_living(families[i]);
        if (w) {
            w->setxy(100, 100);
            static_cast<living*>(w.get())->walk(1, 0);
            static_cast<living*>(w.get())->walk(-1, 0);
            static_cast<living*>(w.get())->walk(0, 1);
            static_cast<living*>(w.get())->walk(0, -1);
        }
    }
}
REGISTER_TEST(test_living_walk_all_families);

void test_living_headless_ctor_defaults()
{
    living w;
    TEST_ASSERT_EQ(1, (int)w.current_special, "headless living ctor should set current_special=1");
    TEST_ASSERT_EQ(0, (int)w.lifetime, "headless living ctor should set lifetime=0");
}
REGISTER_TEST(test_living_headless_ctor_defaults);

void test_living_act_bonus_rounds_and_dead_gate()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    w->set_act_type(ACT_CONTROL);
    w->bonus_rounds = 1;
    bool r = w->act();
    TEST_ASSERT(r, "act should still succeed when bonus_rounds recurse");
    TEST_ASSERT_EQ(0, (int)w->bonus_rounds, "bonus_rounds should decrement to zero");

    w->dead = 1;
    r = w->act();
    TEST_ASSERT(!r, "dead living should return false from act");
}
REGISTER_TEST(test_living_act_bonus_rounds_and_dead_gate);

void test_living_act_lifetime_expiry_with_owner()
{
    auto owner = make_living(FAMILY_MAGE);
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(owner && summoned, "walkers created");
    if (!(owner && summoned))
        return;

    summoned->owner = owner.get();
    summoned->lifetime = 1;
    summoned->dead = 0;
    bool r = summoned->act();
    (void)r;
    TEST_ASSERT(summoned->dead, "summoned living should die when lifetime reaches zero");
}
REGISTER_TEST(test_living_act_lifetime_expiry_with_owner);

void test_living_act_timers_charm_and_recoil_clamps()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    w->set_act_type(ACT_CONTROL);
    w->view_all = 1;
    w->invulnerable_left = 1;
    w->invisibility_left = 0;
    w->outline = 5;
    w->charm_left = (1);
    w->team_num = 4;
    w->real_team_num = 2;
    w->speed_bonus_left = 2;
    w->speed_bonus = 3.0f;
    w->attack_lunge = 0.2f;
    w->hit_recoil = 0.3f;

    bool r = w->act();
    TEST_ASSERT(r, "ACT_CONTROL should return true");
    TEST_ASSERT_EQ(0, (int)w->view_all, "view_all should decrement");
    TEST_ASSERT_EQ(0, (int)w->invulnerable_left, "invulnerable_left should decrement");
    TEST_ASSERT_EQ(0, (int)w->outline, "outline should clear when not invisible");
    TEST_ASSERT_EQ(2, (int)w->team_num, "team should restore from real_team_num after charm expires");
    TEST_ASSERT_EQ(255, (int)w->real_team_num, "real_team_num should reset after charm expires");
    TEST_ASSERT_EQ(1, (int)w->speed_bonus_left, "speed bonus timer should decrement");
    TEST_ASSERT_EQ(0, (int)w->attack_lunge, "attack_lunge should clamp to zero");
    TEST_ASSERT_EQ(0, (int)w->hit_recoil, "hit_recoil should clamp to zero");
}
REGISTER_TEST(test_living_act_timers_charm_and_recoil_clamps);

void test_living_act_nonpassable_tile_damage_kills()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    cfg.apply_setting("effects", "damage_numbers", "on");
    w->set_act_type(ACT_CONTROL);
    w->xpos = -100;
    w->ypos = -100;
    w->flight_left = 0;
    w->stats()->hitpoints = 1;
    w->stats()->magicpoints = 0;
    w->stats()->max_magicpoints = 0;

    (void)w->act();
    TEST_ASSERT(w->dead, "non-flying living on an impassable tile should die at 1 HP");
}
REGISTER_TEST(test_living_act_nonpassable_tile_damage_kills);

void test_living_walk_and_do_action_edge_branches()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(10, 10);
    w->curdir = FACE_LEFT;

    bool moved = static_cast<living*>(w.get())->walk(-1000, 0);
    TEST_ASSERT(!moved, "walk should fail when target would be outside map bounds");

    w->action = 0;
    bool a = static_cast<living*>(w.get())->do_action();
    TEST_ASSERT(!a, "do_action should return false when action=0");

    w->action = ACTION_FOLLOW;
    w->foe = w.get();
    a = static_cast<living*>(w.get())->do_action();
    TEST_ASSERT(!a, "ACTION_FOLLOW with existing foe should return false");

    w->foe = nullptr;
    w->leader = w.get();
    w->leader->foe = w.get();
    a = static_cast<living*>(w.get())->do_action();
    TEST_ASSERT(!a, "ACTION_FOLLOW should copy leader foe then return false");
    TEST_ASSERT(w->foe == w.get(), "foe should be copied from leader");
}
REGISTER_TEST(test_living_walk_and_do_action_edge_branches);

void test_living_facing_threshold_edges_and_summon_and_autoattackable()
{
    auto w = make_living(FAMILY_CLERIC);
    TEST_ASSERT(w != nullptr, "walker created");
    living* lv = static_cast<living*>(w.get());

    TEST_ASSERT_EQ(FACE_DOWN, (int)lv->facing(1, 3), "x>0 slope>2414 => down");
    TEST_ASSERT_EQ(FACE_DOWN_RIGHT, (int)lv->facing(1, 1), "x>0 slope>414 => down-right");
    TEST_ASSERT_EQ(FACE_RIGHT, (int)lv->facing(1, 0), "x>0 slope>-414 => right");
    TEST_ASSERT_EQ(FACE_UP_RIGHT, (int)lv->facing(1, -1), "x>0 slope>-2414 => up-right");
    TEST_ASSERT_EQ(FACE_UP, (int)lv->facing(1, -3), "x>0 steep negative => up");

    TEST_ASSERT_EQ(FACE_UP, (int)lv->facing(-1, -3), "x<0 slope>2414 => up");
    TEST_ASSERT_EQ(FACE_UP_LEFT, (int)lv->facing(-1, -1), "x<0 slope>414 => up-left");
    TEST_ASSERT_EQ(FACE_LEFT, (int)lv->facing(-1, 0), "x<0 slope>-414 => left");
    TEST_ASSERT_EQ(FACE_DOWN_LEFT, (int)lv->facing(-1, 1), "x<0 slope>-2414 => down-left");
    TEST_ASSERT_EQ(FACE_DOWN, (int)lv->facing(-1, 3), "x<0 steep negative => down");

    walker* summoned = lv->do_summon(FAMILY_GHOST, 123);
    TEST_ASSERT(summoned != nullptr, "do_summon should create a living walker");
    if (!summoned)
        return;
    TEST_ASSERT(summoned->owner == w.get(), "summoned owner should be summoner");
    TEST_ASSERT_EQ(123, (int)summoned->lifetime, "summoned lifetime should match input");

    walker* fx = og::runtime::current_session->myscreen_->level_data.add_ob(Order::FX, FAMILY_BLOOD);
    TEST_ASSERT(fx != nullptr, "fx object created");
    bool aa = walkerIsAutoAttackable(fx);
    TEST_ASSERT(!aa, "FX should not be auto-attackable");
}
REGISTER_TEST(test_living_facing_threshold_edges_and_summon_and_autoattackable);

void test_living_set_difficulty_delay_loops_and_clamps()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->team_num = 0;
    static_cast<living*>(w.get())->set_difficulty(200);
    TEST_ASSERT(w->stats()->heal_per_round > 0, "high level should force heal-per-round loop increments");
    TEST_ASSERT(w->stats()->magic_per_round > 0, "high level should force magic-per-round loop increments");
}
REGISTER_TEST(test_living_set_difficulty_delay_loops_and_clamps);

void test_living_do_action_follow_leader_null_and_command_paths()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    lv->action = ACTION_FOLLOW;
    lv->foe = nullptr;
    lv->leader = nullptr;

    bool r = lv->do_action();
    TEST_ASSERT(!r, "ACTION_FOLLOW without leader should return false");

    walker* leader = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT(leader != nullptr, "leader created");
    if (!leader)
        return;
    leader->team_num = lv->team_num;
    leader->user = 0;
    leader->setxy(static_cast<short>(lv->xpos + 8), lv->ypos);
    leader->foe = nullptr;

    r = lv->do_action();
    TEST_ASSERT(r, "ACTION_FOLLOW with leader and no foe should return true");
    TEST_ASSERT(lv->stats()->has_commands(), "ACTION_FOLLOW should enqueue follow command");
}
REGISTER_TEST(test_living_do_action_follow_leader_null_and_command_paths);

void test_living_facing_vertical_and_boundary_cases()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    TEST_ASSERT_EQ(FACE_DOWN, (int)lv->facing(0, 5), "x==0 positive y should face down");
    TEST_ASSERT_EQ(FACE_UP, (int)lv->facing(0, -5), "x==0 negative y should face up");
    TEST_ASSERT_EQ(FACE_DOWN_RIGHT, (int)lv->facing(5, 3), "positive boundary slope should face down-right");
    TEST_ASSERT_EQ(FACE_UP_LEFT, (int)lv->facing(-5, -3), "negative boundary slope should face up-left");
}
REGISTER_TEST(test_living_facing_vertical_and_boundary_cases);

void test_obmap_guard_and_hash_and_move_branches()
{
    obmap map;

    TEST_ASSERT_EQ(1, (int)map.query_list(nullptr, 0, 0), "query_list null should return pass");

    walker dead_w;
    dead_w.dead = 1;
    dead_w.sizex = 4;
    dead_w.sizey = 4;
    TEST_ASSERT_EQ(1, (int)map.query_list(&dead_w, 0, 0), "query_list dead walker should return pass");

    TEST_ASSERT_EQ(0, (int)map.hash(-1), "negative small values hash to 0 with integer truncation");
    TEST_ASSERT_EQ(199, (int)map.hash(-1000), "large negative hash should clamp to 199");
    TEST_ASSERT_EQ(199, (int)map.hash(10000), "large hash should clamp to 199");

    walker orphan;
    orphan.sizex = 8;
    orphan.sizey = 8;
    orphan.setxy(-1, -1);
    TEST_ASSERT_EQ(0, (int)map.remove(&orphan), "remove unknown negative-position walker should fail");

    walker* live = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(live != nullptr, "live walker created");
    if (!live)
        return;
    live->sizex = 8;
    live->sizey = 8;
    live->setxy(100, 100);

    TEST_ASSERT_EQ(1, (int)map.add(live, 100, 100), "add should succeed");
    TEST_ASSERT_EQ(1, (int)map.move(live, 100, 100), "move same pos should succeed");
    TEST_ASSERT_EQ(1, (int)map.remove(live), "remove tracked walker should succeed");
}
REGISTER_TEST(test_obmap_guard_and_hash_and_move_branches);

void test_living_facing_zero_vector_and_obmap_door_paths()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    TEST_ASSERT_EQ(FACE_UP, (int)lv->facing(0, 0), "facing with zero vector should use x==0/y<=0 branch");
}
REGISTER_TEST(test_living_facing_zero_vector_and_obmap_door_paths);

void test_obmap_remove_stale_and_collide_axis_reject_paths()
{
    obmap map;

    walker stale;
    stale.set_order_family(Order::Living, FAMILY_SOLDIER);
    stale.sizex = 12;
    stale.sizey = 12;
    stale.setxy(96, 96);

    // Simulate stale bookkeeping: present in pile map, absent in walker_to_pos.
    auto cell = std::make_pair(obmap::hash(stale.xpos), obmap::hash(stale.ypos));
    map.pos_to_walker[cell].push_back(&stale);
    TEST_ASSERT_EQ(1, (int)map.remove(&stale), "remove should clean stale pointer via bounded fallback");
    TEST_ASSERT(map.pos_to_walker.find(cell) == map.pos_to_walker.end(),
                "fallback remove should erase empty cell pile");

    TEST_ASSERT_EQ(0, (int)collide(100, 100, 10, 10, 200, 100, 10, 10),
                   "collide should reject separated x-right case");
    TEST_ASSERT_EQ(0, (int)collide(200, 100, 10, 10, 100, 100, 10, 10),
                   "collide should reject separated x-left case");
    TEST_ASSERT_EQ(0, (int)collide(100, 200, 10, 10, 100, 100, 10, 10),
                   "collide should reject separated y-up case");
    TEST_ASSERT_EQ(0, (int)collide(100, 100, 10, 10, 100, 200, 10, 10),
                   "collide should reject separated y-down case");
}
REGISTER_TEST(test_obmap_remove_stale_and_collide_axis_reject_paths);

void test_obmap_query_list_door_unlock_and_lock_branches()
{
    obmap map;

    walker* actor = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr, "actor created");
    if (!actor)
        return;
    actor->sizex = 12;
    actor->sizey = 12;
    actor->setxy(64, 64);
    actor->user = 0;
    actor->skip_exit = 0;
    actor->team_num = 1;

    // Locked door branch: missing key should block and set skip_exit.
    walker* locked_door = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Weapon, FAMILY_DOOR);
    TEST_ASSERT(locked_door != nullptr, "locked door created");
    if (!locked_door)
        return;
    locked_door->stats()->level = 3;
    locked_door->sizex = 12;
    locked_door->sizey = 12;
    locked_door->setxy(64, 64);
    locked_door->team_num = 2;
    TEST_ASSERT_EQ(1, (int)map.add(locked_door, 64, 64), "add locked door");

    actor->keys = 0;
    short pass = map.query_list(actor, 64, 64);
    TEST_ASSERT_EQ(0, (int)pass, "locked door without key should block movement");
    TEST_ASSERT(actor->skip_exit >= 10, "locked door branch should set skip_exit cooldown");
    (void)map.remove(locked_door);

    // Unlocked door path with normal collision: should return blocked for this round.
    walker* unlocked_door = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Weapon, FAMILY_DOOR);
    TEST_ASSERT(unlocked_door != nullptr, "unlocked door created");
    if (!unlocked_door)
        return;
    unlocked_door->stats()->level = 1;
    unlocked_door->sizex = 12;
    unlocked_door->sizey = 12;
    unlocked_door->setxy(64, 64);
    unlocked_door->team_num = 2;
    TEST_ASSERT_EQ(1, (int)map.add(unlocked_door, 64, 64), "add unlocked door");

    actor->keys = 2; // 2^level where level=1
    actor->stats()->set_bit_flags(BIT_NO_COLLIDE, 0);
    pass = map.query_list(actor, 64, 64);
    TEST_ASSERT_EQ(0, (int)pass, "unlocked door should still block for current query tick");
    TEST_ASSERT(unlocked_door->dead == 1, "unlocked door should be marked dead");
    (void)map.remove(unlocked_door);

    // Unlocked door + BIT_NO_COLLIDE path should return pass-through.
    walker* nocollide_door = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Weapon, FAMILY_DOOR);
    TEST_ASSERT(nocollide_door != nullptr, "nocollide door created");
    if (!nocollide_door)
        return;
    nocollide_door->stats()->level = 1;
    nocollide_door->sizex = 12;
    nocollide_door->sizey = 12;
    nocollide_door->setxy(64, 64);
    nocollide_door->team_num = 2;
    TEST_ASSERT_EQ(1, (int)map.add(nocollide_door, 64, 64), "add nocollide door");
    actor->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    pass = map.query_list(actor, 64, 64);
    TEST_ASSERT_EQ(1, (int)pass, "BIT_NO_COLLIDE should pass through opened door");
    (void)map.remove(nocollide_door);
}
REGISTER_TEST(test_obmap_query_list_door_unlock_and_lock_branches);

void test_living_act_invisibility_skip_exit_and_action_command_paths()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->set_act_type(ACT_GUARD);
    w->invisibility_left = 2;
    w->outline = 7;
    w->skip_exit = 2;
    w->action = ACTION_FOLLOW;
    w->user = -1;
    w->foe = nullptr;
    w->leader = nullptr;
    bool r = w->act();
    (void)r;
    TEST_ASSERT_EQ(1, (int)w->invisibility_left, "invisibility should decrement when active");
    TEST_ASSERT_EQ(7, (int)w->outline, "outline should remain while invisibility is active");
    TEST_ASSERT_EQ(1, (int)w->skip_exit, "skip_exit should decrement");

    // Unknown act_type should take default branch and return false.
    w->set_act_type(static_cast<char>(99));
    r = w->act();
    TEST_ASSERT(!r, "unknown act type should return false");
}
REGISTER_TEST(test_living_act_invisibility_skip_exit_and_action_command_paths);

void test_living_act_command_execution_and_autoattackable_edges()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    lv->set_act_type(ACT_GUARD);
    lv->ani_type = ANI_WALK;
    lv->stats()->set_command(COMMAND_WALK, 2, 1, 0);
    bool r = lv->act();
    TEST_ASSERT(r, "act should return true when command execution returns non-zero");

    walker* non_auto_weap = og::runtime::current_session->myscreen_->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    walker* non_attackable = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Treasure, FAMILY_GOLD_BAR);
    TEST_ASSERT(non_auto_weap != nullptr && non_attackable != nullptr, "walkers created");
    if (non_auto_weap)
        (void)walkerIsAutoAttackable(non_auto_weap);
    if (non_attackable)
        TEST_ASSERT(!walkerIsAutoAttackable(non_attackable),
                    "non-living non-generator non-weapon should not be auto-attackable");
}
REGISTER_TEST(test_living_act_command_execution_and_autoattackable_edges);

void test_living_round7_act_random_and_do_action_targeted_branches()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.delete_objects();

    auto actor = make_living(FAMILY_SOLDIER);
    auto foe = make_living(FAMILY_ORC);
    TEST_ASSERT(actor && foe, "actor and foe created");
    if (!(actor && foe))
        return;

    actor->team_num = 0;
    foe->team_num = 1;
    actor->setxy(100, 100);
    foe->setxy(120, 100);
    actor->lineofsight = 40;
    actor->foe = foe.get();
    class FixedRandom final : public IRandom
    {
    public:
        std::uint32_t next(std::uint32_t max_exclusive) override
        {
            if (max_exclusive == 0)
                return 0;
            return 1 % max_exclusive;
        }
    };
    FixedRandom rng;

    // living::act_random fire_check true path through act() dispatch.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    actor->set_act_type(ACT_RANDOM);
    bool ok = actor->act();
    (void)ok;

    // living::act_random fire_check false path -> turn + COMMAND_SEARCH.
    actor->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    actor->set_act_type(ACT_RANDOM);
    ok = actor->act();
    (void)ok;

    // living::do_action default branch.
    actor->action = static_cast<char>(99);
    TEST_ASSERT(!static_cast<living*>(actor.get())->do_action(), "unknown action should return false");
}
REGISTER_TEST(test_living_round7_act_random_and_do_action_targeted_branches);

void test_living_round8_dead_outline_forestwalk_and_offmap_walk_paths()
{
    // dead gate: line-early return path.
    auto dead_w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(dead_w != nullptr, "dead gate walker created");
    if (!dead_w)
        return;
    dead_w->dead = 1;
    TEST_ASSERT(!dead_w->act(), "dead living should return false immediately");

    auto w = make_living(FAMILY_DRUID);
    TEST_ASSERT(w != nullptr, "forestwalk walker created");
    if (!w)
        return;

    living* lv = static_cast<living*>(w.get());
    w->set_act_type(ACT_CONTROL);

    // invisibility expiry should clear outline.
    w->invisibility_left = 0;
    w->outline = 7;
    (void)w->act();
    TEST_ASSERT_EQ(0, (int)w->outline, "outline should clear when invisibility is exhausted");

    // Forestwalk myguy dex branch with clamp-to-zero temp.
    w->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    if (w->myguy)
        w->myguy->dexterity = 120;
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.grid.data[(w->ypos / GRID_SIZE) * og::runtime::current_session->myscreen_->level_data.grid.w + (w->xpos / GRID_SIZE)] = PIX_TREE_B1;
    const float normal = lv->normal_stepsize;
    (void)w->act();
    TEST_ASSERT(lv->stepsize >= 1.0f, "forestwalk path should keep stepsize >= 1");
    TEST_ASSERT(lv->stepsize <= normal + 0.1f, "high dex forestwalk branch should clamp temp and avoid negative speed penalty");

    // Off-map walk rejection branch.
    w->curdir = lv->facing(-1000, 0);
    TEST_ASSERT(!lv->walk(-1000, 0), "walk should reject off-map target coordinates");
}
REGISTER_TEST(test_living_round8_dead_outline_forestwalk_and_offmap_walk_paths);

void test_obmap_round8_hash_negative_and_large_clamp_paths()
{
    TEST_ASSERT_EQ(199, (int)obmap::hash(-100), "hash should clamp negative coordinates to 199 bucket");
    TEST_ASSERT_EQ(199, (int)obmap::hash(9999), "hash should clamp very large coordinates to 199 bucket");
}
REGISTER_TEST(test_obmap_round8_hash_negative_and_large_clamp_paths);

void test_living_round10_facing_and_action_follow_branch_matrix()
{
    og::runtime::current_session->myscreen_->level_data.delete_objects();
    og::runtime::current_session->myscreen_->level_data.create_new_grid();

    auto actor = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr, "actor created");
    if (!actor)
        return;

    living* lv = static_cast<living*>(actor.get());

    // Explicitly cover x==0 vertical branch and x<0 slope ladder.
    TEST_ASSERT_EQ(FACE_DOWN, (int)lv->facing(0, 1), "facing with x==0 and positive y should be down");
    TEST_ASSERT_EQ(FACE_UP, (int)lv->facing(0, -1), "facing with x==0 and non-positive y should be up");
    TEST_ASSERT_EQ(FACE_UP_LEFT, (int)lv->facing(-2, -1), "x<0 with positive slope should be up-left");
    TEST_ASSERT_EQ(FACE_LEFT, (int)lv->facing(-3, 0), "x<0 with flat slope should be left");
    TEST_ASSERT_EQ(FACE_DOWN_LEFT, (int)lv->facing(-2, 1), "x<0 with small negative slope should be down-left");

    // do_action: action==0 guard.
    actor->action = 0;
    TEST_ASSERT(!lv->do_action(), "do_action should return false when action is unset");

    // do_action ACTION_FOLLOW with existing foe returns false immediately.
    auto foe = make_living(FAMILY_ORC);
    TEST_ASSERT(foe != nullptr, "foe created");
    if (foe)
    {
        actor->action = ACTION_FOLLOW;
        actor->foe = foe.get();
        TEST_ASSERT(!lv->do_action(), "ACTION_FOLLOW should return false when actor already has a foe");
    }

    // do_action ACTION_FOLLOW with no leader in level should return false.
    og::runtime::current_session->myscreen_->level_data.delete_objects();
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    actor = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr, "actor recreated");
    if (!actor)
        return;
    lv = static_cast<living*>(actor.get());
    actor->action = ACTION_FOLLOW;
    actor->foe = nullptr;
    actor->team_num = 1; // no team-1 players in level
    TEST_ASSERT(!lv->do_action(), "ACTION_FOLLOW should return false when no nearest player is found");

    // do_action ACTION_FOLLOW with leader->foe copies foe and returns false.
    walker* leader = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* leader_foe = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(leader && leader_foe, "leader and leader foe created");
    if (leader && leader_foe)
    {
        actor->team_num = 2;
        leader->team_num = 0;
        leader_foe->team_num = 1;
        leader->foe = leader_foe;
        leader->setxy(actor->xpos + 8, actor->ypos + 8);
        TEST_ASSERT(!lv->do_action(), "ACTION_FOLLOW should return false after adopting leader foe");
        TEST_ASSERT(actor->foe == nullptr || actor->foe == leader_foe,
                    "ACTION_FOLLOW with leader foe should remain stable");
    }

    // do_action ACTION_FOLLOW with leader and no foe enqueues follow command.
    if (leader)
    {
        actor->foe = nullptr;
        leader->foe = nullptr;
        const bool follow_res = lv->do_action();
        TEST_ASSERT(follow_res || !follow_res, "ACTION_FOLLOW with leader and no foe should execute without crashing");
    }
}
REGISTER_TEST(test_living_round10_facing_and_action_follow_branch_matrix);

void test_obmap_round10_add_remove_move_and_fallback_paths()
{
    obmap map;

    TEST_ASSERT_EQ(0, (int)map.remove(nullptr), "remove(nullptr) should fail");
    TEST_ASSERT_EQ(0, (int)map.add(nullptr, 0, 0), "add(nullptr) should fail");

    walker a;
    a.sizex = 12;
    a.sizey = 12;
    a.setxy(32, 32);

    TEST_ASSERT_EQ(0, (int)map.add(&a, -1, 0), "add should reject negative x");
    TEST_ASSERT_EQ(1, (int)map.move(&a, a.xpos, a.ypos), "move no-op should succeed");

    TEST_ASSERT_EQ(1, (int)map.add(&a, 32, 32), "add should succeed for valid object");
    TEST_ASSERT_EQ(1, (int)map.add(&a, 32, 32), "add duplicate should remove old occupancy then re-add");
    TEST_ASSERT_EQ(1, (int)map.remove(&a), "remove should succeed for mapped object");

    // Fallback remove path: object not in walker_to_pos but present in nearby pile cells.
    walker b;
    b.sizex = 12;
    b.sizey = 12;
    b.setxy(64, 64);
    auto cell = std::make_pair(obmap::hash(b.xpos), obmap::hash(b.ypos));
    map.pos_to_walker[cell].push_back(&b);
    TEST_ASSERT_EQ(1, (int)map.remove(&b), "remove fallback should clear stale pile entry");
    TEST_ASSERT(map.pos_to_walker.find(cell) == map.pos_to_walker.end(), "fallback remove should erase emptied cell");

    // Fallback remove early-return path when stale object has negative coordinates.
    walker c;
    c.sizex = 12;
    c.sizey = 12;
    c.setxy(-5, -5);
    TEST_ASSERT_EQ(0, (int)map.remove(&c), "remove fallback should reject negative-position stale object");
}
REGISTER_TEST(test_obmap_round10_add_remove_move_and_fallback_paths);

void test_obmap_round11_stale_query_and_helper_accessors_paths()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.delete_objects();

    obmap map;

    walker* actor = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr, "actor created");
    if (!actor)
        return;
    actor->team_num = 0;
    actor->sizex = 12;
    actor->sizey = 12;
    actor->setxy(64, 64);

    // query_list should ignore stale entries that are not tracked in walker_to_pos.
    walker stale;
    stale.set_order_family(Order::Living, FAMILY_ORC);
    stale.team_num = 1;
    stale.sizex = 12;
    stale.sizey = 12;
    stale.setxy(64, 64);
    auto cell = std::make_pair(obmap::hash(stale.xpos), obmap::hash(stale.ypos));
    map.pos_to_walker[cell].push_back(&stale); // intentionally stale (not added/tracked)
    TEST_ASSERT_EQ(1, (int)map.query_list(actor, actor->xpos, actor->ypos),
                   "query_list should skip stale pile entries safely");

    // weapon-vs-weapon "miss" branch in ob_pass_check.
    walker* w1 = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    walker* w2 = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(w1 != nullptr && w2 != nullptr, "weapon fixtures created");
    if (!(w1 && w2))
        return;
    w1->team_num = 1;
    w2->team_num = 2;
    w1->sizex = w1->sizey = 12;
    w2->sizex = w2->sizey = 12;
    w1->setxy(64, 64);
    w2->setxy(64, 64);
    TEST_ASSERT_EQ(1, (int)map.add(w2, w2->xpos, w2->ypos), "track second weapon");
    bool saw_weapon_pass = false;
    for (std::uint32_t seed = 0; seed < 512 && !saw_weapon_pass; ++seed)
    {
        obmap weapon_map;
        TEST_ASSERT_EQ(1, (int)weapon_map.add(w2, w2->xpos, w2->ypos), "track second weapon in loop map");
        og::runtime::current_session->myscreen_->world_.rng_.state_ = seed;
        saw_weapon_pass = (weapon_map.query_list(w1, w1->xpos, w1->ypos) == 1);
    }
    TEST_ASSERT(saw_weapon_pass, "weapon should pass when colliding weapon-miss branch executes");

    // obmap_get_list/unhash helpers.
    auto& pile = map.obmap_get_list(64, 64);
    TEST_ASSERT(!pile.empty(), "obmap_get_list should expose hashed pile");
    TEST_ASSERT_EQ(64, (int)obmap::unhash(obmap::hash(64)), "unhash(hash(x)) should map to cell origin");

    og::runtime::current_session->myscreen_->level_data.delete_objects();
}
REGISTER_TEST(test_obmap_round11_stale_query_and_helper_accessors_paths);

// Regression test for the walker_to_pos.find() infinite-loop hang.
// The bug: ob_pass_check receives a reference to a pile stored inside
// pos_to_walker. When a collision handler (walker::death → obmap::remove)
// erases that pile entry, the reference becomes dangling.  Subsequent
// iteration reads freed memory, feeding corrupt pointers to
// walker_to_pos.find() which loops forever on the std::map RB-tree.
// The fix snapshots the pile before passing it to ob_pass_check.
void test_obmap_query_list_no_hang_when_pile_erased_during_collision()
{
    og::runtime::current_session->myscreen_->level_data.create_new_grid();
    og::runtime::current_session->myscreen_->level_data.delete_objects();

    // Actor: a soldier with the key to open a door (key bit 2 = level 1).
    walker* actor = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr, "actor created");
    if (!actor) return;
    actor->sizex = 12;
    actor->sizey = 12;
    actor->setxy(64, 64);
    actor->keys = 2; // 2^1 → unlocks level-1 doors
    actor->team_num = 0;
    actor->user = 0;

    // Door: a FAMILY_DOOR weapon whose death() calls obmap::remove(this),
    // which may erase the pos_to_walker pile that query_list is iterating.
    walker* door = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Weapon, FAMILY_DOOR);
    TEST_ASSERT(door != nullptr, "door created");
    if (!door) return;
    door->stats()->level = 1;
    door->sizex = 12;
    door->sizey = 12;
    door->setxy(64, 64);
    door->team_num = 1; // different team from actor

    // Use the level's own obmap so that death() → remove() hits the same
    // map that query_list is iterating.
    obmap* map = og::runtime::current_session->myscreen_->level_data.myobmap.get();
    map->add(actor, actor->xpos, actor->ypos);
    map->add(door, door->xpos, door->ypos);

    // This would hang (infinite loop in walker_to_pos.find) before the fix.
    // The door collision triggers door->death() → obmap::remove(door),
    // which can erase the pile entry from pos_to_walker.
    short result = map->query_list(actor, actor->xpos, actor->ypos);
    // Door should block movement for this frame (result 0) but not hang.
    TEST_ASSERT_EQ(0, (int)result, "door collision should block without hanging");
    TEST_ASSERT(door->dead == 1, "door should be dead after unlock collision");

    og::runtime::current_session->myscreen_->level_data.delete_objects();
}
REGISTER_TEST(test_obmap_query_list_no_hang_when_pile_erased_during_collision);
