#include "graph.h"
#include "guy.h"
#include "gloader.h"
#include "test_framework.h"

extern screen* myscreen;

// From glad.cpp
short remaining_foes(screen* myscreen, walker* myguy);
short remaining_team(screen* myscreen, char myteam);
void draw_radar_gems(screen* myscreen);
void draw_gem(short x, short y, short color, screen* myscreen);
void draw_value_bar(short left, short top, walker* control, short mode, screen* myscreen);
void new_draw_value_bar(short left, short top, walker* control, short mode, screen* myscreen);
void draw_percentage_bar(short left, short top, unsigned char somecolor, short somelength, screen* myscreen);
short score_panel(screen* myscreen);
short score_panel(screen* myscreen, short do_it);
short new_score_panel(screen* myscreen, short do_it);

static walker* make_player(unsigned char team)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    walker* w = g.create_walker(myscreen);
    if (w) {
        w->team_num = team;
        w->dead = 0;
        w->user = 0;
        w->setxy(100, 100);
    }
    return w;
}

static walker* make_living(unsigned char family, unsigned char team)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    walker* w = l->create_walker(Order::Living, family, myscreen);
    if (w) {
        w->team_num = team;
        w->dead = 0;
        w->user = -1;
        w->setxy(120, 100);
    }
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

    walker* control = make_player(0);
    walker* ally = make_living(FAMILY_ELF, 0);
    walker* foe1 = make_living(FAMILY_ORC, 1);
    walker* foe2 = make_living(FAMILY_ORC, 2);
    TEST_ASSERT(control && ally && foe1 && foe2, "walkers should be created");

    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(control));
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(ally));
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(foe1));
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(foe2));

    TEST_ASSERT_EQ(2, (int)remaining_foes(myscreen, control), "should count non-friendly living foes");
    TEST_ASSERT_EQ(2, (int)remaining_team(myscreen, 0), "should count living on team 0 (including control)");

    foe2->dead = 1;
    TEST_ASSERT_EQ(1, (int)remaining_foes(myscreen, control), "dead foes should not be counted");
    myscreen->level_data.oblist.clear();
}
REGISTER_TEST(test_glad_remaining_counts);

void test_glad_draw_gems_and_value_bars_smoke()
{
    walker* control = make_player(0);
    TEST_ASSERT(control != nullptr, "control should be created");

    // Attach control to view so draw_radar_gems can find it.
    viewscreen* v = myscreen->viewob[0].get();
    walker* old_control = v->control;
    v->control = control;

    // draw_radar_gems caches old team; change team to force multiple draws.
    control->team_num = 0;
    draw_radar_gems(myscreen);
    control->team_num = 1;
    draw_radar_gems(myscreen);

    // Direct gem draw.
    draw_gem(10, 10, 32, myscreen);

    // Exercise value bar thresholds for HP and MP.
    control->stats()->max_hitpoints = 100;
    control->stats()->hitpoints = 100;
    draw_value_bar(10, 20, control, 0, myscreen);
    control->stats()->hitpoints = 20;
    draw_value_bar(10, 28, control, 0, myscreen);
    control->stats()->hitpoints = 60;
    draw_value_bar(10, 36, control, 0, myscreen);
    control->stats()->hitpoints = 90;
    draw_value_bar(10, 44, control, 0, myscreen);
    control->stats()->hitpoints = 120;
    draw_value_bar(10, 52, control, 0, myscreen);

    control->stats()->max_magicpoints = 80;
    control->stats()->magicpoints = 80;
    draw_value_bar(10, 60, control, 1, myscreen);
    control->stats()->magicpoints = 10;
    draw_value_bar(10, 68, control, 1, myscreen);
    control->stats()->magicpoints = 100;
    draw_value_bar(10, 76, control, 1, myscreen);

    // New percentage-bar-based drawing.
    new_draw_value_bar(80, 20, control, 0, myscreen);
    new_draw_value_bar(80, 28, control, 1, myscreen);
    draw_percentage_bar(80, 36, 12, 30, myscreen);

    v->control = old_control;
    delete control;
}
REGISTER_TEST(test_glad_draw_gems_and_value_bars_smoke);

void test_glad_score_panel_and_new_score_panel_modes()
{
    walker* control = make_player(0);
    TEST_ASSERT(control != nullptr, "control should be created");
    control->user = 0;
    control->team_num = 0;
    control->dead = 0;
    control->stats()->level = 7;
    control->stats()->hitpoints = 55;
    control->stats()->max_hitpoints = 100;
    control->stats()->magicpoints = 33;
    control->stats()->max_magicpoints = 80;
    control->stats()->special_cost[control->current_special] = 10;

    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    walker* old_control = v->control;
    v->control = control;

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
    control->shifter_down = 1;
    control->stats()->magicpoints = 0;
    (void)new_score_panel(myscreen, 1);
    control->shifter_down = 0;

    // Wrapper functions.
    TEST_ASSERT_EQ(1, (int)score_panel(myscreen), "score_panel wrapper");
    TEST_ASSERT_EQ(1, (int)score_panel(myscreen, 1), "score_panel overload");

    v->control = old_control;
    delete control;
}
REGISTER_TEST(test_glad_score_panel_and_new_score_panel_modes);
