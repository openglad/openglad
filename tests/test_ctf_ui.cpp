// SDL-side CTF presentation: the score panel's CTF block (captures, carrier
// indicator, respawn countdown), radar blips for flags/control points, the
// results-screen formatting helpers, and the level-editor labels. The CTF
// world is built in-test (flags + anchors + TYPE_CTF, then one tick for the
// lazy init) — no CTF campaign is loaded.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/view_sizes.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/game_context.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>
#include <openglad/interface/button.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/io_common.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Picker entry points for the injector-driven flows.
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
// TESTING hook (picker_team_build.cpp): render one TEAMS-subscreen frame in
// the real draw order so pixel probes can run without the blocking loop.
void picker_test_render_teams_menu_frame();

loader* sdl_entity_loader();
short new_score_panel(screen* s, short do_it);
std::string get_editor_family_label(Order order, Sint32 family, std::span<const std::string> livings,
                                    const char* treasures[], const char* weapons[]);
std::string get_editor_level_label(Order order, Sint32 family, Sint32 level);

namespace {

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

// The CTF HUD probes below capture a fixed 320x200 frame and assert classic
// pixel coordinates.  Keep that historical geometry scoped to those tests;
// production zoom 1.0 and unrelated CTF tests continue using the live canvas.
class ClassicCtfHudCanvasGuard
{
public:
    ClassicCtfHudCanvasGuard()
        : game_(test_screen()), saved_target_(game_->active_canvas())
    {
        game_->set_world_canvas_pinned_classic(true);
        game_->relayout_views();
        game_->set_active_canvas(CanvasTarget::World);
    }

    ~ClassicCtfHudCanvasGuard()
    {
        game_->set_active_canvas(CanvasTarget::UI);
        game_->set_world_canvas_pinned_classic(false);
        game_->relayout_views();
        game_->set_active_canvas(saved_target_);
    }

    ClassicCtfHudCanvasGuard(const ClassicCtfHudCanvasGuard&) = delete;
    ClassicCtfHudCanvasGuard& operator=(const ClassicCtfHudCanvasGuard&) = delete;

private:
    screen* game_;
    CanvasTarget saved_target_;
};

// Reload level 1 and stamp a minimal CTF map onto it: one flag per team
// (0/1), two respawn anchors per team, and the TYPE_CTF bit. One world tick
// then runs the gated lazy init and activates the match.
struct CtfScreenWorld
{
    screen* s = test_screen();
    walker* flag0 = nullptr;
    walker* flag1 = nullptr;

    CtfScreenWorld()
    {
        // Mount-in-test: under --gtest_shuffle a predecessor can leave a
        // non-gladiator campaign mounted (e.g. the CTF package, which ships
        // no scen1) — pre-existing order dependence surfaced by seed 23.
        // Level 1 lives in the gladiator package; remount it if needed.
        if (get_mounted_campaign() != "org.openglad.gladiator") {
            (void)unmount_campaign_package_with_error(get_mounted_campaign());
            (void)mount_campaign_package_with_error("org.openglad.gladiator");
        }
        s->world().id = 1;
        EXPECT_TRUE(s->load_level()) << "level 1 should load for the CTF stamp";
        GameWorld& world = s->world();
        world.ctf = og::sim::CtfState{};
        world.type |= GameWorld::TYPE_CTF;

        flag0 = spawn_flag(0, 80, 80);
        flag1 = spawn_flag(1, 240, 160);
        spawn_anchor(0, 112, 80);
        spawn_anchor(0, 80, 112);
        spawn_anchor(1, 208, 160);
        spawn_anchor(1, 240, 128);

        world.tick();
    }

    ~CtfScreenWorld()
    {
        // Reset the CTF stamp and reload the authored level so later tests
        // (and shuffled orders) see a pristine classic world.
        s->world().ctf = og::sim::CtfState{};
        s->world().id = 1;
        (void)s->load_level();
    }

    walker* spawn_flag(int team, int x, int y)
    {
        walker* flag = s->world().add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
        if (flag == nullptr)
            return nullptr;
        flag->setxy(static_cast<short>(x), static_cast<short>(y));
        flag->set_team_num(static_cast<unsigned char>(team));
        return flag;
    }

    walker* spawn_anchor(int team, int x, int y)
    {
        walker* marker = s->world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (marker == nullptr)
            return nullptr;
        marker->setxy(static_cast<short>(x), static_cast<short>(y));
        marker->set_team_num(static_cast<unsigned char>(team));
        return marker;
    }
};

std::unique_ptr<walker> make_control(unsigned char team)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, test_screen());
    if (!w)
        return nullptr;
    w->set_team_num(team);
    w->set_dead(0);
    w->set_user(0);
    w->setxy(100, 100);
    return w;
}

std::array<unsigned char, 64000> capture_rendered_frame(screen& scr)
{
    std::array<unsigned char, 64000> frame{};
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            int color_index = 0;
            scr.get_pixel(x, y, &color_index);
            frame[static_cast<std::size_t>(y * 320 + x)] =
                static_cast<unsigned char>(color_index);
        }
    }
    return frame;
}

bool box_has_pixels(const std::array<unsigned char, 64000>& frame,
                    int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                return true;
    return false;
}

// Quiet HUD baseline: only the CTF block should paint into the probed boxes.
void silence_hud_prefs(viewscreen* v)
{
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    v->prefs[PREF_FOES] = PREF_FOES_OFF;
}

// Returns how many pixels the CTF block painted on virgin ground, and fails
// the test if it overwrote any pixel the classic HUD had already lit (i.e.
// any glyph collision with the name, life rows, or the TEAM/FOES column).
int ctf_added_pixels_expect_no_overwrite(
    const std::array<unsigned char, 64000>& classic,
    const std::array<unsigned char, 64000>& with_ctf)
{
    int added = 0;
    int overwritten = 0;
    std::size_t first = 0;
    for (std::size_t i = 0; i < classic.size(); ++i)
    {
        if (classic[i] != 0 && with_ctf[i] != classic[i])
        {
            if (overwritten == 0)
                first = i;
            ++overwritten;
        }
        else if (classic[i] == 0 && with_ctf[i] != 0)
        {
            ++added;
        }
    }
    EXPECT_EQ(0, overwritten)
        << "CTF HUD overwrote classic HUD pixels, first at ("
        << (first % 320) << "," << (first / 320) << ")";
    return added;
}

} // namespace

TEST(CtfUi, sdl_loader_has_ctf_entries)
{
    loader* game_loader = sdl_entity_loader();
    ASSERT_NE(nullptr, game_loader);
    ASSERT_NE(nullptr,
              game_loader->graphics_for(Order::Treasure, og::FAMILY_FLAG));
    ASSERT_NE(nullptr,
              game_loader->graphics_for(Order::Treasure, og::FAMILY_CTF_POINT));
}

TEST(CtfUi, in_test_ctf_world_activates_via_lazy_init)
{
    CtfScreenWorld ctf;
    ASSERT_NE(nullptr, ctf.flag0);
    ASSERT_NE(nullptr, ctf.flag1);
    ASSERT_TRUE(ctf.s->world().ctf.active)
        << "one tick of a TYPE_CTF world must activate the match";
    ASSERT_TRUE(ctf.s->world().ctf.team_active[0]);
    ASSERT_TRUE(ctf.s->world().ctf.team_active[1]);
}

TEST(CtfUi, score_panel_renders_ctf_captures_block)
{
    ClassicCtfHudCanvasGuard classic_canvas;
    CtfScreenWorld ctf;
    screen* s = ctf.s;
    ASSERT_TRUE(s->world().ctf.active);

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    // Distinct counts + a Taken glyph so both segment branches execute. The
    // single-view group ("2H 0T", 30px) is right-aligned ending at rm-60.
    s->world().ctf.captures[0] = 2;
    s->world().ctf.flags[1].state = og::sim::CtfFlagState::Carried;

    const int tm = v->yloc;
    const int rm = v->endx;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(box_has_pixels(capture_rendered_frame(*s),
                               rm - 92, tm + 3, rm - 59, tm + 12))
        << "the per-team captures block should paint right-aligned at rm-60";
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                rm - 59, tm + 3, rm - 55, tm + 12))
        << "the group must end before the TEAM/FOES column at rm-55";

    // The block is gated on ctf.active: deactivated worlds draw nothing here.
    s->world().ctf.active = false;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                rm - 92, tm + 3, rm - 59, tm + 12))
        << "no CTF block may paint when the match is not active";

    v->control = old_control;
}

TEST(CtfUi, score_panel_shows_flag_carrier_indicator)
{
    ClassicCtfHudCanvasGuard classic_canvas;
    CtfScreenWorld ctf;
    screen* s = ctf.s;
    ASSERT_TRUE(s->world().ctf.active);

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    const int lm = v->xloc;
    const int tm = v->yloc;

    // Our control carries team 1's flag -> FLAG! indicator.
    s->world().ctf.flags[1].state = og::sim::CtfFlagState::Carried;
    s->world().ctf.flags[1].carrier_entity_id = control->entity_id();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(box_has_pixels(capture_rendered_frame(*s),
                               lm + 2, tm + 27, lm + 34, tm + 36))
        << "carrying an enemy flag should paint the FLAG! indicator";

    // Someone else carries it -> no indicator.
    s->world().ctf.flags[1].carrier_entity_id = control->entity_id() + 1;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                lm + 2, tm + 27, lm + 34, tm + 36))
        << "the indicator belongs to the carrier only";

    v->control = old_control;
}

TEST(CtfUi, score_panel_shows_respawn_countdown_for_dead_control)
{
    ClassicCtfHudCanvasGuard classic_canvas;
    CtfScreenWorld ctf;
    screen* s = ctf.s;
    ASSERT_TRUE(s->world().ctf.active);

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    control->set_dead(1);
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    og::sim::CtfRespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 60; // 5 s at 12 ticks/s
    entry.walker_entity_id = control->entity_id();
    s->world().ctf.respawn_queue.push_back(entry);

    const int lm = v->xloc;
    const int tm = v->yloc;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(box_has_pixels(capture_rendered_frame(*s),
                               lm + 4, tm + 11, lm + 90, tm + 20))
        << "a dead control with a queued revive should see RESPAWN IN <s>";

    // Without a queue entry the dead viewport stays blank.
    s->world().ctf.respawn_queue.clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                lm + 4, tm + 11, lm + 90, tm + 20))
        << "no countdown may paint without a pending respawn entry";

    v->control = old_control;
}

// Playtest bug A (display side): a dead myguy control with a pending revive
// entry must SURVIVE the per-batch dead-control cleanup — the binding real
// play loses — so the score panel renders the RESPAWN IN countdown without
// any manual re-stamping. Once the entry is gone (or CTF is inactive) the
// cleanup nulls dead controls exactly as before.
TEST(CtfUi, dead_pending_respawn_control_survives_cleanup_and_shows_countdown)
{
    ClassicCtfHudCanvasGuard classic_canvas;
    CtfScreenWorld ctf;
    screen* s = ctf.s;
    ASSERT_TRUE(s->world().ctf.active);

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    ASSERT_NE(nullptr, control->myguy);
    control->set_dead(1);
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    og::sim::CtfRespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 60;
    entry.walker_entity_id = control->entity_id();
    s->world().ctf.respawn_queue.push_back(entry);

    // The cleanup runs at the head of every dispatched event batch.
    const og::sim::SimEventBatch empty_batch;
    ASSERT_TRUE(s->dispatch_sim_event_batch(empty_batch));
    ASSERT_EQ(control.get(), v->control)
        << "a pending-respawn corpse must stay bound through the cleanup";

    // ... and the still-bound dead control renders the countdown.
    const int lm = v->xloc;
    const int tm = v->yloc;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(box_has_pixels(capture_rendered_frame(*s),
                               lm + 4, tm + 11, lm + 90, tm + 20))
        << "the retained dead control should render RESPAWN IN <s>";

    // Entry gone (live-duplicate cancel): the cleanup nulls it again.
    s->world().ctf.respawn_queue.clear();
    ASSERT_TRUE(s->dispatch_sim_event_batch(empty_batch));
    EXPECT_EQ(nullptr, v->control)
        << "without a pending entry a dead control must still be nulled";

    // Inactive CTF: classic behavior, dead controls always nulled.
    s->world().ctf.respawn_queue.push_back(entry);
    s->world().ctf.active = false;
    v->control = control.get();
    ASSERT_TRUE(s->dispatch_sim_event_batch(empty_batch));
    EXPECT_EQ(nullptr, v->control)
        << "the retention is strictly gated on an active CTF match";
    s->world().ctf.active = true;

    v->control = old_control;
}

// Playtest bug B (perception): while a control point has a contending team
// the panel paints a compact "WP n/36" meter on the tm+36 row; with no
// contender the row stays blank.
TEST(CtfUi, score_panel_shows_waypoint_capture_meter)
{
    ClassicCtfHudCanvasGuard classic_canvas;
    CtfScreenWorld ctf;
    screen* s = ctf.s;
    ASSERT_TRUE(s->world().ctf.active);

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    og::sim::CtfState& state = s->world().ctf;
    state.cp_count = 1;
    state.cps[0].owner = 1;
    state.cps[0].progress = 12;
    state.cps[0].progress_team = 0;

    const int lm = v->xloc;
    const int tm = v->yloc;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(box_has_pixels(capture_rendered_frame(*s),
                               lm + 2, tm + 35, lm + 52, tm + 44))
        << "a contested waypoint should paint the WP n/36 meter";

    // No contender: the meter row stays blank.
    state.cps[0].progress_team = -1;
    state.cps[0].progress = 0;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                lm + 2, tm + 35, lm + 52, tm + 44))
        << "no meter may paint without a contending team";

    v->control = old_control;
}

// Audit findings 3+4: a 12-char name, four active teams, and double-digit
// captures must coexist with zero glyph collisions against the name and the
// TEAM/FOES column. Proven per text-bearing PREF_LIFE mode by rendering the
// classic HUD alone, then with CTF active, and requiring every classic pixel
// to survive untouched.
TEST(CtfUi, hud_caps_clear_name_and_foes_column_in_single_view)
{
    ClassicCtfHudCanvasGuard classic_canvas;
    CtfScreenWorld ctf;
    screen* s = ctf.s;
    ASSERT_TRUE(s->world().ctf.active);

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    ASSERT_NE(nullptr, control->myguy);
    control->myguy->name = "Mordenkainen"; // 12 chars: the audited worst case

    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    walker* old_control = v->control;
    v->control = control.get();
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // its rng count-up varies per frame
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    og::sim::CtfState& state = s->world().ctf;
    for (int team = 0; team < 4; ++team)
        state.team_active[team] = true;
    state.captures[0] = 10;
    state.captures[1] = 9;
    state.captures[3] = 3;
    // Worst-case contested waypoint meter ("WP 35/36" on the tm+36 row) must
    // also stay collision-free against every classic HUD pixel.
    state.cp_count = 1;
    state.cps[0].owner = 1;
    state.cps[0].progress = 35;
    state.cps[0].progress_team = 0;

    const int tm = v->yloc;
    const int rm = v->endx;
    for (char life_pref : {PREF_LIFE_TEXT, PREF_LIFE_BOTH})
    {
        v->prefs[PREF_LIFE] = life_pref;

        state.active = false;
        s->clearbuffer();
        ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
        const auto classic = capture_rendered_frame(*s);

        state.active = true;
        s->clearbuffer();
        ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
        const auto with_ctf = capture_rendered_frame(*s);

        ASSERT_GT(ctf_added_pixels_expect_no_overwrite(classic, with_ctf), 0)
            << "the CTF caps group must actually paint";
        EXPECT_TRUE(box_has_pixels(with_ctf, rm - 150, tm + 3, rm - 59, tm + 12))
            << "caps group expected right-aligned ending at rm-60";
        EXPECT_FALSE(box_has_pixels(with_ctf, rm - 59, tm + 3, rm - 55, tm + 12))
            << "caps group must stop short of the TEAM/FOES column";
    }

    v->control = old_control;
}

// The 2-player split uses the compact digits-only group on the FLAG! row
// (tm+28), right-aligned ending at rm-60: it may not collide with the name,
// the TEAM/FOES column, or the carrier's FLAG! indicator at lm+2.
TEST(CtfUi, hud_caps_compact_group_clears_name_and_flag_in_two_view)
{
    ClassicCtfHudCanvasGuard classic_canvas;
    CtfScreenWorld ctf;
    screen* s = ctf.s;
    ASSERT_TRUE(s->world().ctf.active);

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    ASSERT_NE(nullptr, control->myguy);
    control->myguy->name = "Mordenkainen"; // 12 chars: the audited worst case

    // Rebuild the view layout as a 2-player split; restored below.
    s->numviews = 2;
    s->viewob[0] = std::make_unique<viewscreen>(
        T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT, 0);
    s->viewob[1] = std::make_unique<viewscreen>(
        T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HEIGHT, 1);
    viewscreen* v = s->viewob[0].get();
    v->control = control.get();
    s->viewob[1]->control = nullptr;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    og::sim::CtfState& state = s->world().ctf;
    for (int team = 0; team < 4; ++team)
        state.team_active[team] = true;
    state.captures[0] = 10;
    state.captures[1] = 10;
    state.captures[3] = 7;
    // Worst-case contested waypoint meter on the tm+36 row, below the shared
    // FLAG!/compact-caps row: must add pixels without any collision.
    state.cp_count = 1;
    state.cps[0].owner = 1;
    state.cps[0].progress = 35;
    state.cps[0].progress_team = 0;
    // The control carries team 1's flag, so FLAG! shares the tm+28 row with
    // the right-aligned compact caps group.
    state.flags[1].state = og::sim::CtfFlagState::Carried;
    state.flags[1].carrier_entity_id = control->entity_id();

    const int lm = v->xloc;
    const int tm = v->yloc;
    const int rm = v->endx;

    for (char life_pref : {PREF_LIFE_TEXT, PREF_LIFE_BOTH})
    {
        v->prefs[PREF_LIFE] = life_pref;

        state.active = false;
        s->clearbuffer();
        ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
        const auto classic = capture_rendered_frame(*s);

        state.active = true;
        s->clearbuffer();
        ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
        const auto with_ctf = capture_rendered_frame(*s);

        ASSERT_GT(ctf_added_pixels_expect_no_overwrite(classic, with_ctf), 0)
            << "the compact caps group must actually paint";

        // FLAG! draws after the caps group, so any collision would repaint
        // group pixels: render the group alone (carrier is someone else) and
        // require the full frame to preserve each of its pixels.
        state.flags[1].carrier_entity_id = control->entity_id() + 1;
        s->clearbuffer();
        ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
        const auto caps_only = capture_rendered_frame(*s);
        state.flags[1].carrier_entity_id = control->entity_id();
        int flag_caps_collisions = 0;
        for (std::size_t i = 0; i < caps_only.size(); ++i)
        {
            if (caps_only[i] != 0 && classic[i] == 0 &&
                with_ctf[i] != caps_only[i])
                ++flag_caps_collisions;
        }
        EXPECT_EQ(0, flag_caps_collisions)
            << "FLAG! indicator overlapped the compact caps group";

        EXPECT_TRUE(box_has_pixels(with_ctf, lm + 2, tm + 27, lm + 32, tm + 36))
            << "carrier indicator expected at lm+2 on the tm+28 row";
        EXPECT_TRUE(box_has_pixels(with_ctf, lm + 34, tm + 27, rm - 59, tm + 36))
            << "compact caps group expected right-aligned ending at rm-60";
        EXPECT_FALSE(box_has_pixels(with_ctf, rm - 59, tm + 27, rm - 55, tm + 36))
            << "compact caps group must stop short of the TEAM/FOES column";
    }

    // Restore the single-view layout for the tests that follow.
    s->viewob[1].reset();
    s->numviews = 1;
    s->initialize_views();
}

TEST(CtfUi, radar_draws_flag_and_control_point_blips)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    push_test_context(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();

    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);

    // The standalone default loader has no CTF sprite entries, and the radar's
    // fxlist pass reads only order/family/team/position — hand-built walkers
    // are exactly what it needs.
    const auto push_ctf_fx = [&d](int family, int x, int y,
                                  unsigned char team) -> walker* {
        auto fx = std::make_unique<walker>();
        fx->set_order_family(Order::Treasure, static_cast<char>(family));
        fx->set_team_num(team);
        fx->set_dead(0);
        fx->setxy(static_cast<short>(x), static_cast<short>(y));
        walker* raw = fx.get();
        d.world().fxlist.push_back(std::move(fx));
        return raw;
    };
    walker* flag = push_ctf_fx(og::FAMILY_FLAG, GRID_SIZE * 3, GRID_SIZE * 2, 1);
    walker* point = push_ctf_fx(og::FAMILY_CTF_POINT, GRID_SIZE * 4, GRID_SIZE * 2, 2);
    ASSERT_NE(nullptr, flag);
    ASSERT_NE(nullptr, point);

    control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
    control->set_team_num(0);

    viewscreen* vs = test_screen()->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = control;
    vs->radarstart = 0;

    radar r(vs, test_screen(), 0);
    r.force_lower_position = true;
    r.start(&d);
    // Two draws exercise the flicker (each consumes cosmetic rng only).
    ASSERT_EQ(1, static_cast<int>(r.draw(&d)));
    ASSERT_EQ(1, static_cast<int>(r.draw(&d)));

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    pop_test_context();
}

// The team-build screen keeps a single always-visible SCENARIO entry (the
// scenario commands and the TEAMS subscreen live behind it now), and the
// TEAMS subscreen's settings buttons cycle the save fields through the same
// handlers.
TEST(CtfUi, team_build_row_and_teams_subscreen_settings_cycle)
{
    screen* s = test_screen();
    SaveData& save = s->save_data;
    const std::string old_campaign = save.current_campaign;
    const short old_teams = save.ctf_team_count;
    const short old_caps = save.ctf_capture_limit;
    const short old_troops = save.ctf_strip_scenario_troops;

    // The SCENARIO entry never depends on the campaign.
    for (const char* campaign :
         {"org.openglad.gladiator", "org.openglad.ctf"})
    {
        save.current_campaign = campaign;
        button* row = picker_createmenu_buttons();
        ASSERT_EQ(kCreateMenuButtonCount, picker_createmenu_button_count());
        EXPECT_EQ("scenario", row[kCreateMenuScenarioIndex].id) << campaign;
        EXPECT_FALSE(row[kCreateMenuScenarioIndex].hidden) << campaign;
        // TEAMS and VIEW LEVEL moved into the SCENARIO subscreen.
        button* scenario = picker_scenariomenu_buttons();
        ASSERT_EQ(kScenarioMenuButtonCount,
                  picker_scenariomenu_button_count());
        EXPECT_EQ("teams", scenario[kScenarioMenuTeamsIndex].id) << campaign;
        EXPECT_EQ("view_scenario",
                  scenario[kScenarioMenuViewScenarioIndex].id) << campaign;
        EXPECT_FALSE(scenario[kScenarioMenuTeamsIndex].hidden) << campaign;
        EXPECT_FALSE(scenario[kScenarioMenuViewScenarioIndex].hidden)
            << campaign;
    }

    // The CTF match settings live in the TEAMS subscreen now; their handlers
    // cycle the save fields and refresh the subscreen's descriptor labels.
    save.current_campaign = "org.openglad.ctf";
    save.ctf_team_count = 2;
    save.ctf_capture_limit = 0;
    save.ctf_strip_scenario_troops = 0;
    button* teams_menu = picker_teamsmenu_buttons();
    ASSERT_EQ(kTeamsMenuButtonCount, picker_teamsmenu_button_count());
    EXPECT_EQ("ctf_teams", teams_menu[kTeamsMenuCtfTeamsIndex].id);
    EXPECT_EQ("ctf_caps", teams_menu[kTeamsMenuCtfCapsIndex].id);
    EXPECT_EQ("ctf_troops", teams_menu[kTeamsMenuCtfTroopsIndex].id);

    (void)change_ctf_teams();
    EXPECT_EQ(3, (int)save.ctf_team_count);
    (void)change_ctf_caps();
    EXPECT_EQ(1, (int)save.ctf_capture_limit);
    (void)change_ctf_troops();
    EXPECT_EQ(1, (int)save.ctf_strip_scenario_troops);

    const auto& live_rows = og::runtime::current_session->picker_->teamsmenu_buttons;
    ASSERT_EQ(static_cast<std::size_t>(kTeamsMenuButtonCount), live_rows.size());
    EXPECT_EQ("Teams: 3", live_rows[kTeamsMenuCtfTeamsIndex].label);
    EXPECT_EQ("Limit: 1", live_rows[kTeamsMenuCtfCapsIndex].label);
    EXPECT_EQ("Troops: Own", live_rows[kTeamsMenuCtfTroopsIndex].label);

    (void)change_ctf_troops();
    EXPECT_EQ(0, (int)save.ctf_strip_scenario_troops);
    EXPECT_EQ("Troops: Scen", live_rows[kTeamsMenuCtfTroopsIndex].label);

    save.current_campaign = old_campaign;
    save.ctf_team_count = old_teams;
    save.ctf_capture_limit = old_caps;
    save.ctf_strip_scenario_troops = old_troops;
}

TEST(CtfUi, results_helpers_format_winner_and_captures)
{
    // Color names, matching the sim's match-end notification wording.
    ASSERT_EQ("BLUE TEAM WINS!", format_ctf_winner_banner(2));
    ASSERT_EQ("RED TEAM WINS!", format_ctf_winner_banner(0));
    ASSERT_EQ("YELLOW TEAM WINS!", format_ctf_winner_banner(3));

    // The capture summary is one compact line: a neutral "CAPS " prefix,
    // active-team counts in team order, neutral ':' separators between them.
    og::sim::CtfState ctf;
    ctf.team_active[0] = true;
    ctf.team_active[1] = true;
    ctf.team_active[3] = true;
    ctf.captures[0] = 3;
    ctf.captures[1] = 5;
    ctf.captures[3] = 10;
    const std::vector<CtfCapsSegment> segments = format_ctf_caps_segments(ctf);
    ASSERT_EQ(6u, segments.size());
    EXPECT_EQ("CAPS ", segments[0].text);
    EXPECT_EQ(-1, segments[0].team);
    EXPECT_EQ("3", segments[1].text);
    EXPECT_EQ(0, segments[1].team);
    EXPECT_EQ(":", segments[2].text);
    EXPECT_EQ(-1, segments[2].team);
    EXPECT_EQ("5", segments[3].text);
    EXPECT_EQ(1, segments[3].team);
    EXPECT_EQ(":", segments[4].text);
    EXPECT_EQ(-1, segments[4].team);
    EXPECT_EQ("10", segments[5].text);
    EXPECT_EQ(3, segments[5].team);
}

TEST(CtfUi, editor_labels_resolve_for_ctf_objects)
{
    ASSERT_EQ("CAPS TO WIN: 5",
              get_editor_level_label(Order::Treasure, og::FAMILY_FLAG, 5));
    ASSERT_EQ("", get_editor_level_label(Order::Treasure, og::FAMILY_CTF_POINT, 3));

    static std::array<std::string, NUM_FAMILIES> livings;
    const char* treasures[NUM_FAMILIES] =
        { "BLOOD", "DRUMSTICK", "GOLD", "SILVER",
          "MAGIC", "INVIS", "INVULN", "FLIGHT",
          "EXIT", "TELEPORTER", "LIFE GEM", "KEY", "SPEED",
          "FLAG", "CTF POINT",
        };
    const char* weapons[NUM_FAMILIES] = { "KNIFE" };
    ASSERT_EQ("FLAG", get_editor_family_label(Order::Treasure, og::FAMILY_FLAG,
                                              livings, treasures, weapons));
    ASSERT_EQ("CTF POINT",
              get_editor_family_label(Order::Treasure, og::FAMILY_CTF_POINT,
                                      livings, treasures, weapons));
}

// ---------------------------------------------------------------------------
// Injector-driven picker flows for the TEAMS subscreen and the VIEW LEVEL
// viewer (test_back_to_mainmenu's continue_game pattern: save0 is written
// first so CONTINUE lands straight in team build without prompts).
// ---------------------------------------------------------------------------

namespace {

PickerState& picker_state()
{
    return *og::runtime::current_session->picker_;
}

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        picker_state().backdrops[i].reset();
        picker_state().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    picker_state().main_columns_pix.reset();
    picker_state().main_columns_data.free();
    picker_state().main_title_logo_pix.reset();
    picker_state().main_title_logo_data.free();
}

// Wait until the (visible) interactable `id` shows label `want`.
bool wait_for_interactable_label(const std::string& id, const std::string& want,
                                 int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden && item.label == want)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' label '%s'\n",
            id.c_str(), want.c_str());
    return false;
}

// Wait until a (visible) interactable `id` exists at game coords (x, y) —
// disambiguates the per-screen "back" buttons by their geometry.
bool wait_for_interactable_at(const std::string& id, int x, int y,
                              int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden && item.x == x && item.y == y)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' at (%d,%d)\n",
            id.c_str(), x, y);
    return false;
}

// Stash/restore the picker save across an injector flow.
struct SavedPickerSave
{
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team_list;
    SaveData snapshot_fields;

    SavedPickerSave()
    {
        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            team_list[i] = std::move(save.team_list[i]);
        snapshot_fields.team_size = save.team_size;
        snapshot_fields.my_team = save.my_team;
        snapshot_fields.numplayers = save.numplayers;
        snapshot_fields.allied_mode = save.allied_mode;
        snapshot_fields.scen_num = save.scen_num;
        snapshot_fields.current_campaign = save.current_campaign;
        snapshot_fields.ctf_team_count = save.ctf_team_count;
        snapshot_fields.ctf_capture_limit = save.ctf_capture_limit;
        snapshot_fields.ctf_respawn_ticks = save.ctf_respawn_ticks;
        snapshot_fields.ctf_strip_scenario_troops =
            save.ctf_strip_scenario_troops;
    }

    ~SavedPickerSave()
    {
        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[i] = std::move(team_list[i]);
        save.team_size = snapshot_fields.team_size;
        save.my_team = snapshot_fields.my_team;
        save.numplayers = snapshot_fields.numplayers;
        save.allied_mode = snapshot_fields.allied_mode;
        save.scen_num = snapshot_fields.scen_num;
        save.current_campaign = snapshot_fields.current_campaign;
        save.ctf_team_count = snapshot_fields.ctf_team_count;
        save.ctf_capture_limit = snapshot_fields.ctf_capture_limit;
        save.ctf_respawn_ticks = snapshot_fields.ctf_respawn_ticks;
        save.ctf_strip_scenario_troops =
            snapshot_fields.ctf_strip_scenario_troops;
    }
};

void write_save0_with_soldiers(const std::string& campaign, short scen_num,
                               const std::vector<std::string>& names)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[i]->name = names[i];
        save.team_list[i]->teamnum = 0;
    }
    save.team_size = static_cast<unsigned char>(names.size());
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;
    save.scen_num = scen_num;
    save.current_campaign = campaign;
    save.current_levels.clear();
    save.current_levels[campaign] = scen_num;
    save.ctf_team_count = 0;
    save.ctf_capture_limit = 0;
    save.ctf_strip_scenario_troops = 0;
    ASSERT_TRUE(save.save("save0"));
}

void write_save0_with_two_soldiers(const std::string& campaign, short scen_num)
{
    write_save0_with_soldiers(campaign, scen_num, {"Alpha", "Beta"});
}

struct TeamsFlowState
{
    bool started = false;
    bool finished = false;
    bool subscreen_opened = false;
    bool ctf_buttons_hidden = false;
    bool you_on_team_0 = false;
    bool you_on_team_1 = false;
    bool viewer_opened = false;
    bool viewer_back_seen = false;
};

int teams_local_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TeamsFlowState* state = static_cast<TeamsFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Team build -> SCENARIO submenu -> TEAMS subscreen.
    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");
    wait_for_interactable("teams", 10000);
    SDL_Delay(300);
    interact("teams");

    // TEAMS subscreen: classic local — joins + guy row, no CTF settings.
    state->subscreen_opened = wait_for_interactable("join_team_0", 10000);
    SDL_Delay(300);
    state->ctf_buttons_hidden = !has_interactable("ctf_teams") &&
        !has_interactable("ctf_caps") && !has_interactable("ctf_troops");
    state->you_on_team_0 =
        wait_for_interactable_label("join_team_0", "YOU", 3000);

    // Move the selected hero to GREEN, then take its seat.
    interact("guy_team");
    SDL_Delay(300);
    interact("join_team_1");
    state->you_on_team_1 =
        wait_for_interactable_label("join_team_1", "YOU", 5000);
    // The label can flip while the previous click's press is still held;
    // settle before the next click so its down-transition isn't swallowed.
    SDL_Delay(300);

    // Empty team: the JOIN bounces with a popup (TESTING: trace only).
    interact("join_team_2");
    SDL_Delay(300);

    // TEAMS back returns to the SCENARIO submenu.
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(300);

    // VIEW LEVEL: framed report over a scratch load; BACK returns.
    interact("view_scenario");
    state->viewer_opened = wait_for_interactable_at("back", 10, 170, 10000);
    SDL_Delay(300);
    interact("back");

    // Viewer back -> SCENARIO submenu; its back (30,170) -> team build.
    SDL_Delay(300);
    wait_for_interactable("teams", 10000);
    SDL_Delay(300);
    interact("back");

    SDL_Delay(300);
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

int teams_ctf_settings_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TeamsFlowState* state = static_cast<TeamsFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Team build -> SCENARIO submenu -> TEAMS subscreen.
    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");
    wait_for_interactable("teams", 10000);
    SDL_Delay(300);
    interact("teams");

    // CTF campaign + local host: the match-settings trio lives here.
    state->subscreen_opened = wait_for_interactable("ctf_teams", 10000);
    SDL_Delay(300);

    // Each label can flip while the previous click's press is still held;
    // settle after every wait so the next down-transition isn't swallowed.
    interact("ctf_teams");
    state->you_on_team_0 =
        wait_for_interactable_label("ctf_teams", "Teams: 2", 5000);
    SDL_Delay(300);
    interact("ctf_caps");
    state->you_on_team_1 =
        wait_for_interactable_label("ctf_caps", "Limit: 1", 5000);
    SDL_Delay(300);
    interact("ctf_troops");
    state->viewer_opened =
        wait_for_interactable_label("ctf_troops", "Troops: Own", 5000);
    SDL_Delay(300);

    // TEAMS back returns to the SCENARIO submenu.
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(300);

    // VIEW LEVEL on the loaded CTF map (the save0 load mounted the CTF
    // campaign): the framed CTF report renders, BACK returns.
    interact("view_scenario");
    state->viewer_back_seen = wait_for_interactable_at("back", 10, 170, 10000);
    SDL_Delay(300);
    interact("back");

    // Viewer back -> SCENARIO submenu; its back (30,170) -> team build.
    SDL_Delay(300);
    wait_for_interactable("teams", 10000);
    SDL_Delay(300);
    interact("back");

    SDL_Delay(300);
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

// Count picker traces whose message carries the given substring (trace_count
// is per-category only; the page-flip assertions need the page number).
int count_picker_trace_containing(const char* substring)
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int count = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == "picker" &&
            entry.message.find(substring) != std::string::npos)
        {
            ++count;
        }
    }
    return count;
}

struct PagerFlowState
{
    bool started = false;
    bool finished = false;
    bool viewer_opened = false;
    bool pager_visible = false;
};

// handle_menu_nav reads SDL_GetKeyboardState (not the event queue), so the
// injector pokes the state array directly — the same approach as
// KeyStateGuard in test_picker_menu_nav.cpp.
void hold_nav_key(SDL_Keycode key, bool down)
{
    int numkeys = 0;
    const bool* ro = SDL_GetKeyboardState(&numkeys);
    bool* keys = const_cast<bool*>(ro);
    const SDL_Scancode sc = SDL_GetScancodeFromKey(key, nullptr);
    if (keys != nullptr && sc >= 0 && sc < numkeys)
        keys[sc] = down;
}

void press_nav_key(SDL_Keycode key, int hold_ms)
{
    hold_nav_key(key, true);
    SDL_Delay(hold_ms);
    hold_nav_key(key, false);
}

int view_scenario_pager_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PagerFlowState* state = static_cast<PagerFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Team build -> SCENARIO submenu -> VIEW LEVEL.
    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(300);
    interact("view_scenario");

    state->viewer_opened = wait_for_interactable_at("back", 10, 170, 10000);
    SDL_Delay(300);
    state->pager_visible =
        has_interactable("page_prev") && has_interactable("page_next");

    // Mouse path: click NEXT (page 0 -> 1).
    interact("page_next");
    SDL_Delay(500);

    // Keyboard path: RIGHT moves the highlight BACK -> PREV (and enables
    // menu nav); FIRE activates PREV through its ButtonAction (page 1 -> 0).
    press_nav_key(SDLK_RIGHT, 120);
    SDL_Delay(300);
    press_nav_key(SDLK_SPACE, 120);
    SDL_Delay(500);

    // Viewer back -> SCENARIO submenu; its back (30,170) -> team build.
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("teams", 10000);
    SDL_Delay(300);
    interact("back");

    SDL_Delay(300);
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

// Wait until at least `count` picker traces carry the given substring.
bool wait_for_picker_trace(const char* substring, int count, int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (count_picker_trace_containing(substring) >= count)
            return true;
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for %d picker trace(s) '%s'\n",
            count, substring);
    return false;
}

struct TeamsPagerFlowState
{
    bool started = false;
    bool finished = false;
    bool subscreen_opened = false;
    bool pager_team0_visible = false;
    bool pager_team1_hidden = false;
    bool page2_seen = false;
    bool page3_seen = false;
};

// TEAMS per-team pager: an 8-hero RED team overflows the detail line, so
// team_page_0 shows and cycles the slice; empty GREEN has no pager.
int teams_pager_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TeamsPagerFlowState* state = static_cast<TeamsPagerFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");
    wait_for_interactable("teams", 10000);
    SDL_Delay(300);
    interact("teams");

    state->subscreen_opened = wait_for_interactable("join_team_0", 10000);
    SDL_Delay(300);
    state->pager_team0_visible = wait_for_interactable("team_page_0", 5000);
    state->pager_team1_hidden = !has_interactable("team_page_1");
    SDL_Delay(300);

    // Flip twice: 1/3 -> 2/3 -> 3/3. Each flip is confirmed through the
    // page trace before the next click (the label itself never changes).
    interact("team_page_0");
    state->page2_seen =
        wait_for_picker_trace("teams_detail t=0 page=2/", 1, 5000);
    SDL_Delay(300);
    interact("team_page_0");
    state->page3_seen =
        wait_for_picker_trace("teams_detail t=0 page=3/", 1, 5000);
    SDL_Delay(300);

    // Unwind: TEAMS -> SCENARIO -> team build -> main menu.
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(300);
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

} // namespace

TEST(CtfUi, teams_subscreen_local_join_and_viewer_flow)
{
    trace_clear();
    SavedPickerSave save_guard;
    write_save0_with_two_soldiers("org.openglad.gladiator", 1);

    TeamsFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        teams_local_flow_injector, "teams_local_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.subscreen_opened) << "TEAMS subscreen should open";
    EXPECT_TRUE(state.ctf_buttons_hidden)
        << "classic campaigns hide the CTF settings trio";
    EXPECT_TRUE(state.you_on_team_0) << "P1 starts as YOU on RED";
    EXPECT_TRUE(state.you_on_team_1) << "JOIN should relabel YOU on GREEN";
    EXPECT_TRUE(state.viewer_opened) << "VIEW LEVEL should open its frame";

    EXPECT_EQ(1, save.my_team) << "JOIN GREEN must set my_team";
    const guy* alpha = nullptr;
    for (const auto& member : save.team_list)
    {
        if (member && member->name == "Alpha")
            alpha = member.get();
    }
    ASSERT_NE(nullptr, alpha);
    EXPECT_EQ(1, alpha->teamnum) << "TEAM > must cycle the selected hero";

    EXPECT_TRUE(trace_contains("popup", "NO HEROES"))
        << "JOIN on an empty team must explain itself";
    EXPECT_TRUE(trace_contains("picker", "view_scenario lines="))
        << "the viewer should trace its report";
}

TEST(CtfUi, teams_subscreen_ctf_settings_flow)
{
    trace_clear();
    SavedPickerSave save_guard;
    // The save0 load mounts the CTF campaign; its levels start at scen 500.
    write_save0_with_two_soldiers("org.openglad.ctf", 500);

    TeamsFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        teams_ctf_settings_flow_injector, "teams_ctf_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.subscreen_opened)
        << "CTF campaign + host shows the settings in the subscreen";
    EXPECT_TRUE(state.you_on_team_0) << "Teams cycle should relabel";
    EXPECT_TRUE(state.you_on_team_1) << "Limit cycle should relabel";
    EXPECT_TRUE(state.viewer_opened) << "Troops toggle should relabel";

    EXPECT_EQ(2, (int)save.ctf_team_count);
    EXPECT_EQ(1, (int)save.ctf_capture_limit);
    EXPECT_EQ(1, (int)save.ctf_strip_scenario_troops);

    EXPECT_TRUE(state.viewer_back_seen)
        << "VIEW LEVEL should open on the mounted CTF map";
    EXPECT_TRUE(trace_contains("picker", "view_scenario lines="))
        << "the viewer should trace its CTF report";

    // The save0 load remounted the CTF campaign; restore the default mount
    // so later (or shuffled) tests load classic levels again.
    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    (void)mount_campaign_package_with_error("org.openglad.gladiator");
}

TEST(CtfUi, view_scenario_pager_flips_by_mouse_and_keyboard)
{
    trace_clear();
    SavedPickerSave save_guard;
    // Gladiator scen 15 (BATTLE OF THE SLIME) formats to well over one page
    // at 23 rows/page: a stable shipped multi-page fixture.
    write_save0_with_two_soldiers("org.openglad.gladiator", 15);

    // Deterministic keyboard nav: bind player-0 menu keys explicitly and
    // keep any configured joystick from shadowing them.
    const int old_up = og::runtime::current_session->player_keys_[0][KEY_UP];
    const int old_down = og::runtime::current_session->player_keys_[0][KEY_DOWN];
    const int old_left = og::runtime::current_session->player_keys_[0][KEY_LEFT];
    const int old_right = og::runtime::current_session->player_keys_[0][KEY_RIGHT];
    const int old_fire = og::runtime::current_session->player_keys_[0][KEY_FIRE];
    const int old_joy_index = player_joy[0].index;
    set_player_key_binding(0, KEY_UP, SDLK_UP);
    set_player_key_binding(0, KEY_DOWN, SDLK_DOWN);
    set_player_key_binding(0, KEY_LEFT, SDLK_LEFT);
    set_player_key_binding(0, KEY_RIGHT, SDLK_RIGHT);
    set_player_key_binding(0, KEY_FIRE, SDLK_SPACE);
    disablePlayerJoystick(0);

    PagerFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        view_scenario_pager_injector, "view_pager_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    set_player_key_binding(0, KEY_UP, old_up);
    set_player_key_binding(0, KEY_DOWN, old_down);
    set_player_key_binding(0, KEY_LEFT, old_left);
    set_player_key_binding(0, KEY_RIGHT, old_right);
    set_player_key_binding(0, KEY_FIRE, old_fire);
    player_joy[0].index = old_joy_index;

    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.viewer_opened) << "VIEW LEVEL should open its frame";
    EXPECT_TRUE(state.pager_visible)
        << "scen 15's report must span multiple pages (PREV/NEXT visible)";
    EXPECT_GE(count_picker_trace_containing("page=1"), 1)
        << "mouse click on NEXT must flip to page 1";
    EXPECT_GE(count_picker_trace_containing("page=0"), 2)
        << "keyboard FIRE on PREV must flip back (entry trace + flip trace)";
}

// TEAMS per-team member paging: an 8-hero team overflows the 34-char detail
// line, so the row gets a '>' pager + a p/N indicator and the slices rotate
// through every member; an empty team never shows a pager.
TEST(CtfUi, teams_subscreen_member_pager_cycles_slices)
{
    trace_clear();
    SavedPickerSave save_guard;
    // 8 names whose ", "-joined detail is 56 chars: three 26-char slices
    // ("Alpha, Bravo, Charlie" | "Delta, Echo, Foxtrot, Golf" | "Hotel").
    write_save0_with_soldiers(
        "org.openglad.gladiator", 1,
        {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf",
         "Hotel"});

    TeamsPagerFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        teams_pager_flow_injector, "teams_pager_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.subscreen_opened) << "TEAMS subscreen should open";
    EXPECT_TRUE(state.pager_team0_visible)
        << "the overflowing RED team must show its '>' pager";
    EXPECT_TRUE(state.pager_team1_hidden)
        << "the empty GREEN team must not show a pager";
    EXPECT_TRUE(state.page2_seen) << "first flip should reach page 2/3";
    EXPECT_TRUE(state.page3_seen) << "second flip should reach page 3/3";

    // The traces carry the rendered indicator (p/N) and the slice text:
    // each page shows a different run of member names.
    EXPECT_GE(count_picker_trace_containing(
                  "teams_detail t=0 page=1/3 Alpha, Bravo, Charlie"), 1)
        << "entry slice should list the first members";
    EXPECT_GE(count_picker_trace_containing(
                  "teams_detail t=0 page=2/3 Delta, Echo, Foxtrot, Golf"), 1)
        << "page 2 should rotate to the middle members";
    EXPECT_GE(count_picker_trace_containing(
                  "teams_detail t=0 page=3/3 Hotel"), 1)
        << "page 3 should show the overflow tail";
}

// Draw-order regression: the per-team '>' pager is the only TEAMS button
// whose face sits inside a row readability bar (8..234 x row band). The bars
// must render BENEATH the buttons — when they were painted after
// draw_buttons, every visible pager was dimmed to ~41% brightness (PURE_BLACK
// alpha 150) unlike every other button. Renders one real frame via the
// TESTING hook and compares the pager's face pixels against the same
// BUTTON_FACING face style on buttons outside the bars.
TEST(CtfUi, teams_subscreen_pager_face_not_dimmed_by_row_bar)
{
    SavedPickerSave save_guard;
    // Same 8-name roster as the pager flow: team 0's detail line overflows
    // the 34-char unpaged budget, so team_page_0 is visible.
    write_save0_with_soldiers(
        "org.openglad.gladiator", 1,
        {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf",
         "Hotel"});

    picker_test_render_teams_menu_frame();

    screen* s = test_screen();
    // Face-interior probes: inside each button, clear of the 1px borders and
    // of the centered 1-char '>' label (glyph starts at x=(x+xend)/2).
    // team_page_0: (219,39,14x12) -> face (220..231, 40..49), glyph from 226.
    int pager_face = -1;
    s->get_pixel(221, 44, &pager_face);
    // guy_next '>': (120,146,16x12) -> face (121..134, 147..156), glyph from
    // 128 — same face style, same 1-char label, outside every row bar.
    int reference_face = -1;
    s->get_pixel(122, 151, &reference_face);
    // join_team_0: (240,32,50x12) -> face interior at (243,35) — a second
    // reference outside the bars (they end at x=234), inside the row band.
    int join_face = -1;
    s->get_pixel(243, 35, &join_face);

    EXPECT_EQ(reference_face, pager_face)
        << "pager face inside the row bar must match the undimmed button "
           "face style (bar painted over the button?)";
    EXPECT_EQ(join_face, pager_face)
        << "pager face must match the JOIN button face beside it";

    // And the bar itself still dims the backdrop around the button: a bar
    // pixel just below the pager (row band is y 30..51; the pager ends at
    // y=50) must not be the button-face color.
    int bar_pixel = -1;
    s->get_pixel(221, 51, &bar_pixel);
    EXPECT_NE(bar_pixel, pager_face)
        << "row readability bar should still darken non-button pixels";

    cleanup_picker_state();
}
