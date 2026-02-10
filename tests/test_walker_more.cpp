#include "graph.h"
#include "guy.h"
#include "test_framework.h"

extern screen* myscreen;

static walker* create_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    walker* w = l->create_walker(Order::Living, family, myscreen);
    if (!w)
        return nullptr;
    w->setxy(50, 50);
    return w;
}

void test_walker_misc_methods_smoke()
{
    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker(soldier) should succeed");

    // Basic movement helpers (should not crash).
    w->move(1, 0);
    w->worldmove(1.0f, 0.0f);
    w->setworldxy(60.0f, 60.0f);
    w->setxy(60, 60);
    w->facing(61, 60);
    w->turn(1);

    // Path helpers and distance checks.
    TEST_ASSERT(w->distance_to_ob(w) == 0.0f, "distance to self should be 0");
    (void)w->distance_to_ob_center(w);

    // Order/family reassignment and simple state transitions.
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_act_type(0);
    (void)w->query_act_type();

    // Reset is a large code path; smoke it to improve coverage.
    w->reset();

    delete w;
}
REGISTER_TEST(test_walker_misc_methods_smoke);

void test_walker_friendliness_and_attack_paths()
{
    walker* a = create_living(FAMILY_SOLDIER);
    walker* b = create_living(FAMILY_SMALL_SLIME);
    TEST_ASSERT(a != nullptr, "create_walker(attacker) should succeed");
    TEST_ASSERT(b != nullptr, "create_walker(target) should succeed");

    a->team_num = 0;
    b->team_num = 1;

    TEST_ASSERT(!a->is_friendly(b), "enemy should not be friendly");
    TEST_ASSERT(a->is_friendly_to_team(0), "same team should be friendly");
    TEST_ASSERT(!a->is_friendly_to_team(1), "other team should not be friendly");

    // Give attacker a guy to record tallies.
    a->myguy = new guy(FAMILY_SOLDIER);
    a->myguy->teamnum = 0;
    a->myguy->exp = 0;

    b->stats()->armor = 0;
    b->stats()->hitpoints = 50;
    b->stats()->max_hitpoints = 50;

    (void)a->attack(b);

    delete a; // deletes a->myguy
    delete b;
}
REGISTER_TEST(test_walker_friendliness_and_attack_paths);

void test_walker_specials_and_render_paths_smoke()
{
    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker(soldier) should succeed");
    w->team_num = 0;

    // Give the walker a guy so specials/XP paths have something to update.
    w->myguy = new guy(FAMILY_SOLDIER);
    w->myguy->teamnum = 0;
    w->stats()->magicpoints = 999;
    w->stats()->max_magicpoints = 999;

    // Cover various large methods. Some may legitimately return false in a
    // headless/unit-test context; we're targeting execution, not gameplay.
    (void)w->init_fire();
    (void)w->init_fire(1, 0);
    (void)w->fire_check(1, 0);
    (void)w->query_next_to();
    (void)w->special();
    (void)w->teleport();
    (void)w->teleport_ranged(3);
    (void)w->turn_undead(3, 1);

    // Drawing paths (should be safe with an initialized viewscreen).
    (void)w->draw(v);
    (void)w->draw_tile(v);

    delete w; // deletes myguy
}
REGISTER_TEST(test_walker_specials_and_render_paths_smoke);
