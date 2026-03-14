#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_player_walker(char family, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
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
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (w) {
        w->team_num = team;
        w->user = -1;
        w->dead = 0;
        w->setxy(60, 60);
    }
    return w;
}

TEST(ViewLifecycle, viewscreen_construct_destruct_and_clear)
{
    // Constructing a standalone viewscreen should allocate/destroy radar cleanly.
    auto vs = std::make_unique<viewscreen>(0, 0, 320, 200, 0);
    ASSERT_TRUE(vs->myradar != nullptr) << "viewscreen should create a radar";

    // Exercise viewscreen::clear() which writes into myscreen->videobuffer.
    og::runtime::current_session->myscreen_->videobuffer[0] = 123;
    og::runtime::current_session->myscreen_->videobuffer[63999] = 77;
    vs->clear();
    ASSERT_EQ(0, (int)og::runtime::current_session->myscreen_->videobuffer[0]) << "clear should zero videobuffer[0]";
    ASSERT_EQ(0, (int)og::runtime::current_session->myscreen_->videobuffer[63999]) << "clear should zero videobuffer[63999]";
}


TEST(ViewLifecycle, viewscreen_find_next_control_priorities)
{
    viewscreen v(0, 0, 320, 200, 0);
    v.my_team = 0;
    v.mynum = 0;

    // Isolate the oblist so existing tests/game state can't influence selection.
    struct ObListSwap {
        std::list<std::unique_ptr<walker>> saved;
        ObListSwap()
        {
            og::runtime::current_session->myscreen_->world().oblist.splice_into(saved);
        }
        ~ObListSwap()
        {
            og::runtime::current_session->myscreen_->world().oblist.splice(og::runtime::current_session->myscreen_->world().oblist.end(), saved);
        }
    } swap;

    auto npc_same_team = make_npc_walker(FAMILY_ORC, 0);
    auto player_same_team = make_player_walker(FAMILY_SOLDIER, 0);
    auto player_other_team = make_player_walker(FAMILY_ARCHER, 1);
    ASSERT_TRUE(npc_same_team != nullptr) << "npc walker should be created";
    ASSERT_TRUE(player_same_team != nullptr) << "player walker should be created";
    ASSERT_TRUE(player_other_team != nullptr) << "other-team player walker should be created";

    walker* npc_same_teamp = npc_same_team.get();
    walker* player_same_teamp = player_same_team.get();
    walker* player_other_teamp = player_other_team.get();

    // Insert in an order that would pick the player walker only if the priority
    // logic is working (player character loop runs first).
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(npc_same_team));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(player_same_team));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(player_other_team));

    walker* found1 = v.find_next_control();
    ASSERT_TRUE(found1 == player_same_teamp) << "should prefer un-controlled player character on team";

    // Mark player_same_team as already controlled; should fall back to any team member.
    player_same_teamp->user = 0;
    walker* found2 = v.find_next_control();
    ASSERT_TRUE(found2 == npc_same_teamp) << "should fall back to any un-controlled living team member";

    // Now eliminate team 0 options; should fall back to any remaining player character.
    npc_same_teamp->dead = 1;
    player_same_teamp->dead = 1;
    walker* found3 = v.find_next_control();
    ASSERT_TRUE(found3 == player_other_teamp) << "should fall back to any living player character";

    // Cleanup - remove just our inserted walkers.
    og::runtime::current_session->myscreen_->world().oblist.clear();
}
