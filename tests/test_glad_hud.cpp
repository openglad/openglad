#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/legacy/base.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

// From glad.cpp
short remaining_foes(screen* scr, walker* myguy);
short remaining_team(screen* scr, char myteam);
void draw_radar_gems(screen* scr);
void draw_gem(short x, short y, short color, screen* scr);
void draw_value_bar(short left, short top, walker* control, short mode, screen* scr);
void new_draw_value_bar(Sint32 left, Sint32 top, walker* control, short mode, screen* scr);
void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor, short somelength, screen* scr);
short score_panel(screen* scr);
short score_panel(screen* scr, short do_it);
short new_score_panel(screen* scr, short do_it);

static bool control_pointer_is_live(LevelRuntimeData& level_data, const walker* candidate)
{
    if (candidate == nullptr)
        return false;

    const auto in_list = [candidate](const std::list<std::unique_ptr<walker>>& list) {
        for (const auto& entry : list)
        {
            if (entry.get() == candidate)
                return true;
        }
        return false;
    };

    return in_list(level_data.world().oblist)
        || in_list(level_data.world().fxlist)
        || in_list(level_data.world().weaplist);
}

static std::unique_ptr<walker> make_player(unsigned char team)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
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
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    w->team_num = team;
    w->dead = 0;
    w->user = -1;
    w->setxy(120, 100);
    return w;
}

TEST(GladHud, glad_remaining_counts)
{
    // Isolate the oblist so prior game state doesn't affect counts.
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

    auto control = make_player(0);
    auto ally = make_living(FAMILY_ELF, 0);
    auto foe1 = make_living(FAMILY_ORC, 1);
    auto foe2 = make_living(FAMILY_ORC, 2);
    ASSERT_TRUE(control && ally && foe1 && foe2) << "walkers should be created";

    walker* controlp = control.get();
    walker* allyp = ally.get();
    walker* foe1p = foe1.get();
    walker* foe2p = foe2.get();

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(foe1));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(foe2));

    ASSERT_EQ(2, (int)remaining_foes(og::runtime::current_session->myscreen_, controlp)) << "should count non-friendly living foes";
    ASSERT_EQ(2, (int)remaining_team(og::runtime::current_session->myscreen_, 0)) << "should count living on team 0 (including control)";

    foe2p->dead = 1;
    ASSERT_EQ(1, (int)remaining_foes(og::runtime::current_session->myscreen_, controlp)) << "dead foes should not be counted";
    og::runtime::current_session->myscreen_->world().oblist.clear();
}


TEST(GladHud, glad_draw_gems_and_value_bars_smoke)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr) << "control should be created";
    walker* controlp = control.get();

    // Attach control to view so draw_radar_gems can find it.
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    walker* old_control = v->control;
    v->control = controlp;

    // draw_radar_gems caches old team; change team to force multiple draws.
    controlp->team_num = 0;
    draw_radar_gems(og::runtime::current_session->myscreen_);
    controlp->team_num = 1;
    draw_radar_gems(og::runtime::current_session->myscreen_);

    // Direct gem draw.
    draw_gem(10, 10, 32, og::runtime::current_session->myscreen_);

    // Exercise value bar thresholds for HP and MP.
    controlp->stats()->max_hitpoints = 100;
    controlp->stats()->hitpoints = 100;
    draw_value_bar(10, 20, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->hitpoints = 20;
    draw_value_bar(10, 28, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->hitpoints = 60;
    draw_value_bar(10, 36, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->hitpoints = 90;
    draw_value_bar(10, 44, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->hitpoints = 120;
    draw_value_bar(10, 52, controlp, 0, og::runtime::current_session->myscreen_);

    controlp->stats()->max_magicpoints = 80;
    controlp->stats()->magicpoints = 80;
    draw_value_bar(10, 60, controlp, 1, og::runtime::current_session->myscreen_);
    controlp->stats()->magicpoints = 10;
    draw_value_bar(10, 68, controlp, 1, og::runtime::current_session->myscreen_);
    controlp->stats()->magicpoints = 100;
    draw_value_bar(10, 76, controlp, 1, og::runtime::current_session->myscreen_);

    // New percentage-bar-based drawing.
    new_draw_value_bar(80, 20, controlp, 0, og::runtime::current_session->myscreen_);
    new_draw_value_bar(80, 28, controlp, 1, og::runtime::current_session->myscreen_);
    draw_percentage_bar(80, 36, 12, 30, og::runtime::current_session->myscreen_);

    v->control = control_pointer_is_live(og::runtime::current_session->myscreen_->level_runtime_data(), old_control) ? old_control : nullptr;
}


TEST(GladHud, glad_score_panel_and_new_score_panel_modes)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr) << "control should be created";
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

    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "view should exist";
    walker* old_control = v->control;
    v->control = controlp;

    // Make sure these overlays execute.
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    // Exercise all life display variants in new_score_panel.
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    ASSERT_EQ(1, (int)new_score_panel(og::runtime::current_session->myscreen_, 1)) << "new_score_panel text mode";
    v->prefs[PREF_LIFE] = PREF_LIFE_BARS;
    ASSERT_EQ(1, (int)new_score_panel(og::runtime::current_session->myscreen_, 1)) << "new_score_panel bars mode";
    v->prefs[PREF_LIFE] = PREF_LIFE_BOTH;
    ASSERT_EQ(1, (int)new_score_panel(og::runtime::current_session->myscreen_, 1)) << "new_score_panel both mode";

    // Toggle shifter special-name branch and low-mp branch.
    controlp->shifter_down = 1;
    controlp->stats()->magicpoints = 0;
    (void)new_score_panel(og::runtime::current_session->myscreen_, 1);
    controlp->shifter_down = 0;

    // Wrapper functions.
    ASSERT_EQ(1, (int)score_panel(og::runtime::current_session->myscreen_)) << "score_panel wrapper";
    ASSERT_EQ(1, (int)score_panel(og::runtime::current_session->myscreen_, 1)) << "score_panel overload";

    v->control = control_pointer_is_live(og::runtime::current_session->myscreen_->level_runtime_data(), old_control) ? old_control : nullptr;
}

TEST(GladHud, RedrawmeFlickerNoUnpaintedPresent)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);

    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);
    walker* old_control = v->control;
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_LIFE]    = PREF_LIFE_BARS;
    v->prefs[PREF_SCORE]   = PREF_SCORE_ON;
    v->prefs[PREF_FOES]    = PREF_FOES_ON;

    s->redrawme = 1;
    trace_clear();

    s->draw_panels(s->numviews);
    ASSERT_EQ(0, trace_count("present"))
        << "regression: draw_panels must not present; presenting before "
           "score_panel paints the HUD causes the overlay flicker";

    score_panel(s, 1);
    ASSERT_EQ(0, trace_count("present"))
        << "score_panel paints into the back buffer only; it must not present";

    s->buffer_to_screen(0, 0, 320, 200);
    ASSERT_EQ(1, trace_count("present"))
        << "exactly one full-screen present must follow score_panel in the "
           "redrawme path";

    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
    s->redrawme = 0;
}
