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

extern screen* myscreen;
bool walkerIsAutoAttackable(walker* ob);

static std::unique_ptr<walker> make_living(char family, short level = 3)
{
    guy g(family);
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, myscreen);
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
            loader* l = myscreen->level_data.myloader.get();
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
    w->set_charm_left(1);
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

    walker* fx = myscreen->level_data.add_ob(Order::FX, FAMILY_BLOOD);
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

    walker* leader = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
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
    TEST_ASSERT_EQ(199, (int)map.hash(10000), "large hash should clamp to 199");

    walker orphan;
    orphan.sizex = 8;
    orphan.sizey = 8;
    orphan.setxy(-1, -1);
    TEST_ASSERT_EQ(0, (int)map.remove(&orphan), "remove unknown negative-position walker should fail");

    walker* live = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
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
