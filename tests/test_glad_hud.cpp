#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

// From glad.cpp
short remaining_foes(screen* myscreen, walker* myguy);
short remaining_team(screen* myscreen, char myteam);
void draw_radar_gems(screen* myscreen);
void draw_gem(short x, short y, short color, screen* myscreen);
void draw_value_bar(short left, short top, walker* control, short mode, screen* myscreen);
void new_draw_value_bar(Sint32 left, Sint32 top, walker* control, short mode, screen* myscreen);
void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor, short somelength, screen* myscreen);
short score_panel(screen* myscreen);
short score_panel(screen* myscreen, short do_it);
short new_score_panel(screen* myscreen, short do_it);

static std::unique_ptr<walker> make_player(unsigned char team)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (!w)
        return nullptr;
    w->team_num = team;
    w->dead = 0;
    w->user = 0;
    w->setxy(100, 100);
    return w;
}

static std::unique_ptr<walker> make_living(unsigned char family, unsigned char team)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family, myscreen);
    if (!w)
        return nullptr;
    w->team_num = team;
    w->dead = 0;
    w->user = -1;
    w->setxy(120, 100);
    return w;
}

void test_glad_remaining_counts()
{
    // Isolate the oblist so prior game state doesn't affect counts.
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

    auto control = make_player(0);
    auto ally = make_living(FAMILY_ELF, 0);
    auto foe1 = make_living(FAMILY_ORC, 1);
    auto foe2 = make_living(FAMILY_ORC, 2);
    TEST_ASSERT(control && ally && foe1 && foe2, "walkers should be created");

    walker* controlp = control.get();
    walker* allyp = ally.get();
    walker* foe1p = foe1.get();
    walker* foe2p = foe2.get();

    myscreen->level_data.oblist.push_back(std::move(control));
    myscreen->level_data.oblist.push_back(std::move(ally));
    myscreen->level_data.oblist.push_back(std::move(foe1));
    myscreen->level_data.oblist.push_back(std::move(foe2));

    TEST_ASSERT_EQ(2, (int)remaining_foes(myscreen, controlp), "should count non-friendly living foes");
    TEST_ASSERT_EQ(2, (int)remaining_team(myscreen, 0), "should count living on team 0 (including control)");

    foe2p->dead = 1;
    TEST_ASSERT_EQ(1, (int)remaining_foes(myscreen, controlp), "dead foes should not be counted");
    myscreen->level_data.oblist.clear();
}
REGISTER_TEST(test_glad_remaining_counts);

void test_glad_draw_gems_and_value_bars_smoke()
{
    auto control = make_player(0);
    TEST_ASSERT(control != nullptr, "control should be created");
    walker* controlp = control.get();

    // Attach control to view so draw_radar_gems can find it.
    viewscreen* v = myscreen->viewob[0].get();
    walker* old_control = v->control;
    v->control = controlp;

    // draw_radar_gems caches old team; change team to force multiple draws.
    controlp->team_num = 0;
    draw_radar_gems(myscreen);
    controlp->team_num = 1;
    draw_radar_gems(myscreen);

    // Direct gem draw.
    draw_gem(10, 10, 32, myscreen);

    // Exercise value bar thresholds for HP and MP.
    controlp->stats()->max_hitpoints = 100;
    controlp->stats()->hitpoints = 100;
    draw_value_bar(10, 20, controlp, 0, myscreen);
    controlp->stats()->hitpoints = 20;
    draw_value_bar(10, 28, controlp, 0, myscreen);
    controlp->stats()->hitpoints = 60;
    draw_value_bar(10, 36, controlp, 0, myscreen);
    controlp->stats()->hitpoints = 90;
    draw_value_bar(10, 44, controlp, 0, myscreen);
    controlp->stats()->hitpoints = 120;
    draw_value_bar(10, 52, controlp, 0, myscreen);

    controlp->stats()->max_magicpoints = 80;
    controlp->stats()->magicpoints = 80;
    draw_value_bar(10, 60, controlp, 1, myscreen);
    controlp->stats()->magicpoints = 10;
    draw_value_bar(10, 68, controlp, 1, myscreen);
    controlp->stats()->magicpoints = 100;
    draw_value_bar(10, 76, controlp, 1, myscreen);

    // New percentage-bar-based drawing.
    new_draw_value_bar(80, 20, controlp, 0, myscreen);
    new_draw_value_bar(80, 28, controlp, 1, myscreen);
    draw_percentage_bar(80, 36, 12, 30, myscreen);

    v->control = old_control;
}
REGISTER_TEST(test_glad_draw_gems_and_value_bars_smoke);

void test_glad_score_panel_and_new_score_panel_modes()
{
    auto control = make_player(0);
    TEST_ASSERT(control != nullptr, "control should be created");
    walker* controlp = control.get();
    controlp->user = 0;
    controlp->team_num = 0;
    controlp->dead = 0;
    controlp->stats()->level = 7;
    controlp->stats()->hitpoints = 55;
    controlp->stats()->max_hitpoints = 100;
    controlp->stats()->magicpoints = 33;
    controlp->stats()->max_magicpoints = 80;
    controlp->stats()->special_cost[static_cast<unsigned char>(controlp->current_special)] = 10;

    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    walker* old_control = v->control;
    v->control = controlp;

    // Make sure these overlays execute.
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    // Exercise all life display variants in new_score_panel.
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    TEST_ASSERT_EQ(1, (int)new_score_panel(myscreen, 1), "new_score_panel text mode");
    v->prefs[PREF_LIFE] = PREF_LIFE_BARS;
    TEST_ASSERT_EQ(1, (int)new_score_panel(myscreen, 1), "new_score_panel bars mode");
    v->prefs[PREF_LIFE] = PREF_LIFE_BOTH;
    TEST_ASSERT_EQ(1, (int)new_score_panel(myscreen, 1), "new_score_panel both mode");

    // Toggle shifter special-name branch and low-mp branch.
    controlp->shifter_down = 1;
    controlp->stats()->magicpoints = 0;
    (void)new_score_panel(myscreen, 1);
    controlp->shifter_down = 0;

    // Wrapper functions.
    TEST_ASSERT_EQ(1, (int)score_panel(myscreen), "score_panel wrapper");
    TEST_ASSERT_EQ(1, (int)score_panel(myscreen, 1), "score_panel overload");

    v->control = old_control;
}
REGISTER_TEST(test_glad_score_panel_and_new_score_panel_modes);
