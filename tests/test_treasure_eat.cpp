#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/treasure.h>
#include <openglad/data/gloader.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static walker* make_eater(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w) w->setxy(100, 100);
    return w.release();
}

static walker* make_treasure(char family, short level = 1)
{
    walker* t = myscreen->level_data.add_fx_ob(Order::Treasure, family);
    if (t) {
        t->setxy(100, 100);
        t->stats()->level = level;
    }
    return t;
}


// ---------------------------------------------------------------------------
// treasure::eat_me - various treasure types
// ---------------------------------------------------------------------------

void test_treasure_eat_drumstick()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->stats()->hitpoints = 50;
    eater->stats()->max_hitpoints = 100;

    walker* drum = make_treasure(FAMILY_DRUMSTICK, 1);
    if (!drum) { delete eater; return; }

    drum->eat_me(eater);
    TEST_ASSERT(eater->stats()->hitpoints > 50, "drumstick should heal");
    TEST_ASSERT(eater->stats()->hitpoints <= 100, "should not exceed max HP");

    myscreen->level_data.remove_ob(drum);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_drumstick);

void test_treasure_eat_drumstick_full_hp()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->stats()->hitpoints = eater->stats()->max_hitpoints;

    walker* drum = make_treasure(FAMILY_DRUMSTICK, 1);
    if (!drum) { delete eater; return; }

    float hp_before = eater->stats()->hitpoints;
    drum->eat_me(eater);
    TEST_ASSERT(eater->stats()->hitpoints == hp_before, "full HP should not eat drumstick");
    TEST_ASSERT(drum->dead != 1, "drumstick should not die when full HP");

    myscreen->level_data.remove_ob(drum);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_drumstick_full_hp);

void test_treasure_eat_gold_bar()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->team_num = 0;

    walker* gold = make_treasure(FAMILY_GOLD_BAR, 2);
    if (!gold) { delete eater; return; }

    Uint32 score_before = myscreen->save_data.m_score[0];
    gold->eat_me(eater);
    TEST_ASSERT(myscreen->save_data.m_score[0] > score_before, "gold bar adds score");
    TEST_ASSERT(gold->dead == 1, "gold bar consumed");

    myscreen->save_data.m_score[0] = score_before;
    myscreen->level_data.remove_ob(gold);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_gold_bar);

void test_treasure_eat_silver_bar()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->team_num = 0;

    walker* silver = make_treasure(FAMILY_SILVER_BAR, 3);
    if (!silver) { delete eater; return; }

    Uint32 score_before = myscreen->save_data.m_score[0];
    silver->eat_me(eater);
    TEST_ASSERT(myscreen->save_data.m_score[0] > score_before, "silver bar adds score");
    TEST_ASSERT(silver->dead == 1, "silver bar consumed");

    myscreen->save_data.m_score[0] = score_before;
    myscreen->level_data.remove_ob(silver);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_silver_bar);

void test_treasure_eat_flight_potion()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->flight_left = 0;
    eater->user = 0;

    walker* potion = make_treasure(FAMILY_FLIGHT_POTION, 2);
    if (!potion) { delete eater; return; }

    potion->eat_me(eater);
    TEST_ASSERT(eater->flight_left > 0, "flight potion grants flight");
    TEST_ASSERT(potion->dead == 1, "potion consumed");

    myscreen->level_data.remove_ob(potion);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_flight_potion);

void test_treasure_eat_magic_potion()
{
    walker* eater = make_eater(FAMILY_MAGE, 0);
    if (!eater) return;
    eater->stats()->magicpoints = 10;
    eater->stats()->max_magicpoints = 100;
    eater->user = 0;

    walker* potion = make_treasure(FAMILY_MAGIC_POTION, 2);
    if (!potion) { delete eater; return; }

    potion->eat_me(eater);
    TEST_ASSERT(eater->stats()->magicpoints >= 100, "magic potion restores MP");
    TEST_ASSERT(potion->dead == 1, "potion consumed");

    myscreen->level_data.remove_ob(potion);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_magic_potion);

void test_treasure_eat_invulnerable_potion()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->invulnerable_left = 0;
    eater->user = 0;

    walker* potion = make_treasure(FAMILY_INVULNERABLE_POTION, 1);
    if (!potion) { delete eater; return; }

    potion->eat_me(eater);
    TEST_ASSERT(eater->invulnerable_left > 0, "invuln potion grants invulnerability");
    TEST_ASSERT(potion->dead == 1, "potion consumed");

    myscreen->level_data.remove_ob(potion);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_invulnerable_potion);

void test_treasure_eat_invis_potion()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->invisibility_left = 0;
    eater->user = 0;

    walker* potion = make_treasure(FAMILY_INVIS_POTION, 2);
    if (!potion) { delete eater; return; }

    potion->eat_me(eater);
    TEST_ASSERT(eater->invisibility_left > 0, "invis potion grants invisibility");
    TEST_ASSERT(potion->dead == 1, "potion consumed");

    myscreen->level_data.remove_ob(potion);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_invis_potion);

void test_treasure_eat_speed_potion()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->speed_bonus_left = 0;
    eater->user = 0;

    walker* potion = make_treasure(FAMILY_SPEED_POTION, 3);
    if (!potion) { delete eater; return; }

    potion->eat_me(eater);
    TEST_ASSERT(eater->speed_bonus_left > 0, "speed potion grants speed");
    TEST_ASSERT(potion->dead == 1, "potion consumed");

    myscreen->level_data.remove_ob(potion);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_speed_potion);

void test_treasure_eat_key()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->keys = 0;
    eater->team_num = 0;

    walker* key = make_treasure(FAMILY_KEY, 1);
    if (!key) { delete eater; return; }

    key->eat_me(eater);
    TEST_ASSERT(eater->keys != 0, "key pickup sets key flags");

    myscreen->level_data.remove_ob(key);
    delete eater;
}
REGISTER_TEST(test_treasure_eat_key);

void test_treasure_eat_life_gem()
{
    walker* eater = make_eater(FAMILY_SOLDIER, 0);
    if (!eater) return;
    eater->team_num = 0;

    walker* gem = make_treasure(FAMILY_LIFE_GEM, 1);
    if (!gem) { delete eater; return; }
    gem->team_num = 0;
    gem->stats()->hitpoints = 500;

    Uint32 score_before = myscreen->save_data.m_score[0];
    gem->eat_me(eater);
    TEST_ASSERT(myscreen->save_data.m_score[0] > score_before, "life gem adds score");
    TEST_ASSERT(gem->dead == 1, "gem consumed");

    myscreen->save_data.m_score[0] = score_before;
    delete eater;
}
REGISTER_TEST(test_treasure_eat_life_gem);

void test_treasure_find_teleport_target_loop_and_missing_self()
{
    myscreen->level_data.delete_objects();

    walker* tele_a = make_treasure(FAMILY_TELEPORTER, 5);
    walker* tele_b = make_treasure(FAMILY_TELEPORTER, 5);
    walker* tele_c = make_treasure(FAMILY_TELEPORTER, 6); // mismatch level
    TEST_ASSERT(tele_a && tele_b && tele_c, "teleporters created");
    if (!(tele_a && tele_b && tele_c))
        return;

    tele_a->setxy(80, 100);
    tele_b->setxy(100, 100);
    tele_c->setxy(120, 100);

    walker* target = static_cast<treasure*>(tele_a)->find_teleport_target();
    TEST_ASSERT(target == tele_b, "find_teleport_target should pick next same-level teleporter");

    tele_b->dead = 1;
    target = static_cast<treasure*>(tele_a)->find_teleport_target();
    TEST_ASSERT(target == nullptr, "dead/mismatched teleporters should yield null target");

    treasure detached;
    detached.stats()->level = 5;
    detached.sim_level = &myscreen->level_data;
    TEST_ASSERT(detached.find_teleport_target() == nullptr,
                "teleporter lookup should return null when self is not in fx list");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_treasure_find_teleport_target_loop_and_missing_self);

void test_treasure_eat_default_fallback_and_teleporter_wraparound()
{
    treasure t;
    t.set_order_family(Order::Treasure, 127);
    TEST_ASSERT(t.eat_me(nullptr), "unknown treasure family should take default eat_me path");

    myscreen->level_data.delete_objects();
    walker* tele_a = make_treasure(FAMILY_TELEPORTER, 9);
    walker* tele_b = make_treasure(FAMILY_TELEPORTER, 9);
    walker* tele_c = make_treasure(FAMILY_TELEPORTER, 9);
    TEST_ASSERT(tele_a && tele_b && tele_c, "teleporters created");
    if (!(tele_a && tele_b && tele_c))
        return;

    tele_a->setxy(60, 100);
    tele_b->setxy(80, 100);
    tele_c->setxy(100, 100);
    tele_a->dead = 1; // force wraparound search to skip dead teleporter

    walker* target = static_cast<treasure*>(tele_c)->find_teleport_target();
    TEST_ASSERT(target == tele_b, "teleporter at list tail should wrap and find earlier same-level target");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_treasure_eat_default_fallback_and_teleporter_wraparound);

void test_treasure_batch7_explicit_fxlist_teleporter_branches()
{
    myscreen->level_data.delete_objects();
    auto& fx = myscreen->level_data.fxlist;
    fx.clear();

    auto before = std::make_unique<treasure>();
    before->sim_level = &myscreen->level_data;
    before->set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    before->stats()->level = 7;
    before->setxy(60, 60);
    walker* before_ptr = before.get();
    fx.push_back(std::move(before));

    auto self = std::make_unique<treasure>();
    self->sim_level = &myscreen->level_data;
    self->set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    self->stats()->level = 7;
    self->setxy(80, 60);
    walker* self_ptr = self.get();
    fx.push_back(std::move(self));

    auto mismatch = std::make_unique<treasure>();
    mismatch->sim_level = &myscreen->level_data;
    mismatch->set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    mismatch->stats()->level = 8; // level mismatch should be skipped
    mismatch->setxy(100, 60);
    fx.push_back(std::move(mismatch));

    walker* found = static_cast<treasure*>(self_ptr)->find_teleport_target();
    TEST_ASSERT(found == before_ptr, "tail search should wrap around and return earlier matching teleporter");

    static_cast<treasure*>(before_ptr)->dead = 1;
    found = static_cast<treasure*>(self_ptr)->find_teleport_target();
    TEST_ASSERT(found == nullptr, "dead and mismatched teleporters should be rejected");

    treasure detached;
    detached.sim_level = &myscreen->level_data;
    detached.stats()->level = 7;
    TEST_ASSERT(detached.find_teleport_target() == nullptr,
                "find_teleport_target should return null when self is not in fx list");

    treasure unknown_family;
    unknown_family.set_order_family(Order::Treasure, static_cast<char>(-1));
    TEST_ASSERT(unknown_family.eat_me(nullptr), "unknown treasure family should use default eat_me return path");

    fx.clear();
}
REGISTER_TEST(test_treasure_batch7_explicit_fxlist_teleporter_branches);
