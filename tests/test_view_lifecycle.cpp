#include <openglad/entities/guy.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static std::unique_ptr<walker> make_player_walker(char family, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = g.create_walker_owned(myscreen);
    if (w) {
        w->team_num = team;
        w->user = -1;
        w->dead = 0;
        w->setxy(50, 50);
    }
    return w;
}

static std::unique_ptr<walker> make_npc_walker(char family, unsigned char team)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family, myscreen);
    if (w) {
        w->team_num = team;
        w->user = -1;
        w->dead = 0;
        w->setxy(60, 60);
    }
    return w;
}

void test_viewscreen_construct_destruct_and_clear()
{
    // Constructing a standalone viewscreen should allocate/destroy radar cleanly.
    auto vs = std::make_unique<viewscreen>(0, 0, 320, 200, 0);
    TEST_ASSERT(vs->myradar != nullptr, "viewscreen should create a radar");

    // Exercise viewscreen::clear() which writes into myscreen->videobuffer.
    myscreen->videobuffer[0] = 123;
    myscreen->videobuffer[63999] = 77;
    vs->clear();
    TEST_ASSERT_EQ(0, (int)myscreen->videobuffer[0], "clear should zero videobuffer[0]");
    TEST_ASSERT_EQ(0, (int)myscreen->videobuffer[63999], "clear should zero videobuffer[63999]");
}
REGISTER_TEST(test_viewscreen_construct_destruct_and_clear);

void test_viewscreen_find_next_control_priorities()
{
    viewscreen v(0, 0, 320, 200, 0);
    v.my_team = 0;
    v.mynum = 0;

    // Isolate the oblist so existing tests/game state can't influence selection.
    struct ObListSwap {
        std::list<std::unique_ptr<walker>> saved;
        ObListSwap()
        {
            saved.splice(saved.end(), myscreen->level_data.oblist);
        }
        ~ObListSwap()
        {
            myscreen->level_data.oblist.splice(myscreen->level_data.oblist.end(), saved);
        }
    } swap;

    auto npc_same_team = make_npc_walker(FAMILY_ORC, 0);
    auto player_same_team = make_player_walker(FAMILY_SOLDIER, 0);
    auto player_other_team = make_player_walker(FAMILY_ARCHER, 1);
    TEST_ASSERT(npc_same_team != nullptr, "npc walker should be created");
    TEST_ASSERT(player_same_team != nullptr, "player walker should be created");
    TEST_ASSERT(player_other_team != nullptr, "other-team player walker should be created");

    walker* npc_same_teamp = npc_same_team.get();
    walker* player_same_teamp = player_same_team.get();
    walker* player_other_teamp = player_other_team.get();

    // Insert in an order that would pick the player walker only if the priority
    // logic is working (player character loop runs first).
    myscreen->level_data.oblist.push_back(std::move(npc_same_team));
    myscreen->level_data.oblist.push_back(std::move(player_same_team));
    myscreen->level_data.oblist.push_back(std::move(player_other_team));

    walker* found1 = v.find_next_control();
    TEST_ASSERT(found1 == player_same_teamp, "should prefer un-controlled player character on team");

    // Mark player_same_team as already controlled; should fall back to any team member.
    player_same_teamp->user = 0;
    walker* found2 = v.find_next_control();
    TEST_ASSERT(found2 == npc_same_teamp, "should fall back to any un-controlled living team member");

    // Now eliminate team 0 options; should fall back to any remaining player character.
    npc_same_teamp->dead = 1;
    player_same_teamp->dead = 1;
    walker* found3 = v.find_next_control();
    TEST_ASSERT(found3 == player_other_teamp, "should fall back to any living player character");

    // Cleanup - remove just our inserted walkers.
    myscreen->level_data.oblist.clear();
}
REGISTER_TEST(test_viewscreen_find_next_control_priorities);
