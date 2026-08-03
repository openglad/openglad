// SDL-side scripted-mode presentation: the generic ModeState HUD panel
// (og.set_hud_line slots at the proven CTF panel positions), beacon edge
// arrows + in-view pulse markers, the shared respawn countdown on scripted
// worlds, the respawn camera-focus generalization, the radar landmark /
// beacon consumption, and the scripted results surfaces. Worlds are built
// in-test: TYPE_SCRIPTED + direct ModeState field writes (the og.* bindings
// are the production writers; every renderer here reads the same fields).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/order.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/view_sizes.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/game_context.h>
#include <openglad/resources/io_common.h>

#include <SDL3/SDL.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>

short new_score_panel(screen* s, short do_it);
void show_ending_popup(int ending, int nextlevel);

namespace {

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

// The HUD probes below capture a fixed 320x200 frame and assert classic
// pixel coordinates (the CtfUi canvas discipline).
class ClassicModeHudCanvasGuard
{
public:
    ClassicModeHudCanvasGuard()
        : game_(test_screen()), saved_target_(game_->active_canvas())
    {
        game_->set_world_canvas_pinned_classic(true);
        game_->relayout_views();
        game_->set_active_canvas(CanvasTarget::World);
    }

    ~ClassicModeHudCanvasGuard()
    {
        game_->set_active_canvas(CanvasTarget::UI);
        game_->set_world_canvas_pinned_classic(false);
        game_->relayout_views();
        game_->set_active_canvas(saved_target_);
    }

    ClassicModeHudCanvasGuard(const ClassicModeHudCanvasGuard&) = delete;
    ClassicModeHudCanvasGuard& operator=(const ClassicModeHudCanvasGuard&) =
        delete;

private:
    screen* game_;
    CanvasTarget saved_target_;
};

// Reload level 1 and stamp a scripted-mode world onto it: the TYPE_SCRIPTED
// bit plus a directly-activated ModeState (the render layer keys on
// (type & TYPE_SCRIPTED) && mode.active, exactly what a successful
// on_mode_init leaves behind).
struct ModeScreenWorld
{
    screen* s = test_screen();

    ModeScreenWorld()
    {
        if (get_mounted_campaign() != "org.openglad.gladiator") {
            (void)unmount_campaign_package_with_error(get_mounted_campaign());
            (void)mount_campaign_package_with_error("org.openglad.gladiator");
        }
        s->world().id = 1;
        EXPECT_TRUE(s->load_level()) << "level 1 should load for the stamp";
        GameWorld& world = s->world();
        world.ctf = og::sim::CtfState{};
        world.mode = og::sim::ModeState{};
        world.type |= GameWorld::TYPE_SCRIPTED;
        world.mode.active = true;
        world.mode.init_attempted = true;
    }

    ~ModeScreenWorld()
    {
        GameWorld& world = s->world();
        world.mode = og::sim::ModeState{};
        world.ctf = og::sim::CtfState{};
        world.type = static_cast<char>(world.type & ~GameWorld::TYPE_SCRIPTED);
        world.id = 1;
        (void)s->load_level();
    }

    void set_hud(int slot, const char* text, std::uint8_t team)
    {
        og::sim::ModeHudLine& line =
            s->world().mode.hud[static_cast<std::size_t>(slot)];
        line.team = team;
        line.text = {};
        std::strncpy(line.text.data(), text, line.text.size() - 1);
    }

    walker* spawn_living(int x, int y, unsigned char team)
    {
        walker* w = s->world().add_ob(Order::Living, FAMILY_SOLDIER);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(team);
        return w;
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

// Every lit pixel in the box lies in [base, base+span] and at least one is
// lit. Text glyphs shade within their ramp (walkputbuffertext maps font
// pixels >247 to teamcolor + (255-pixel), i.e. base..base+7); fastbox draws
// use span 0 (the exact color).
bool box_pixels_all_colored(const std::array<unsigned char, 64000>& frame,
                            int x0, int y0, int x1, int y1,
                            unsigned char base, unsigned char span = 0)
{
    bool any = false;
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            const unsigned char p =
                frame[static_cast<std::size_t>(y * 320 + x)];
            if (p == 0)
                continue;
            if (p < base || p > static_cast<unsigned char>(base + span))
                return false;
            any = true;
        }
    }
    return any;
}

void silence_hud_prefs(viewscreen* v)
{
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    v->prefs[PREF_FOES] = PREF_FOES_OFF;
}

} // namespace

TEST(ModeUi, score_panel_renders_hud_slots_at_ctf_positions)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    mode.set_hud(0, "5:3", 0);
    mode.set_hud(1, "ROUND 2", 1);
    mode.set_hud(2, "WP 12/36", 2);
    mode.set_hud(3, "BALL!", 255);

    const int tm = v->yloc;
    const int lm = v->xloc;
    const int rm = v->endx;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto frame = capture_rendered_frame(*s);

    // Slot 0: right-aligned ending at rm-60 on the tm+4 caps row, in the
    // team-0 ramp (0*16+40).
    EXPECT_TRUE(box_pixels_all_colored(frame, rm - 60 - 3 * 6, tm + 3,
                                       rm - 59, tm + 12, 40, 7))
        << "slot 0 should paint right-aligned at rm-60 in the team-0 ramp";
    EXPECT_FALSE(box_has_pixels(frame, rm - 59, tm + 3, rm - 55, tm + 12))
        << "slot 0 must end before the TEAM/FOES column at rm-55";

    // Slot 1: right-aligned ending at rm-60 on the tm+28 digits row, team 1.
    EXPECT_TRUE(box_pixels_all_colored(frame, rm - 60 - 7 * 6, tm + 27,
                                       rm - 59, tm + 36, 56, 7))
        << "slot 1 should paint right-aligned at rm-60 on the tm+28 row";

    // Slot 2: the WP-meter position (lm+2, tm+36), team 2.
    EXPECT_TRUE(box_pixels_all_colored(frame, lm + 2, tm + 35,
                                       lm + 2 + 8 * 6, tm + 44, 72, 7))
        << "slot 2 should paint at the WP-meter position";

    // Slot 3: the FLAG!-tag position (lm+2, tm+28), team 255 = HUD yellow.
    EXPECT_TRUE(box_pixels_all_colored(frame, lm + 2, tm + 27,
                                       lm + 2 + 5 * 6, tm + 36,
                                       static_cast<unsigned char>(YELLOW), 7))
        << "slot 3 should paint at the FLAG!-tag position in HUD yellow";

    EXPECT_TRUE(trace_contains("mode_hud", "slot=0 text=5:3"));
    EXPECT_TRUE(trace_contains("mode_hud", "slot=3 text=BALL!"));

    // Empty slots draw nothing: clearing slot 1 removes exactly its pixels.
    mode.set_hud(1, "", 255);
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                rm - 60 - 7 * 6, tm + 27, rm - 59, tm + 36))
        << "an empty HUD slot must not paint";

    v->control = old_control;
}

TEST(ModeUi, score_panel_mode_block_gates_on_type_and_active)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    mode.set_hud(0, "5:3", 0);
    const int tm = v->yloc;
    const int rm = v->endx;

    s->world().mode.active = false;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                rm - 90, tm + 3, rm - 59, tm + 12))
        << "no mode HUD may paint when the mode is not active";

    s->world().mode.active = true;
    s->world().type =
        static_cast<char>(s->world().type & ~GameWorld::TYPE_SCRIPTED);
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                rm - 90, tm + 3, rm - 59, tm + 12))
        << "no mode HUD may paint without the TYPE_SCRIPTED bit";
    s->world().type |= GameWorld::TYPE_SCRIPTED;

    v->control = old_control;
}

TEST(ModeUi, score_panel_suppresses_rightside_slots_in_small_panes)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);

    // Rebuild the view layout as a 3-player split; restored below.
    s->numviews = 3;
    s->viewob[0] = std::make_unique<viewscreen>(
        T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0);
    s->viewob[1] = std::make_unique<viewscreen>(
        T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1);
    s->viewob[2] = std::make_unique<viewscreen>(
        T_LEFT_THREE_FOUR, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2);
    viewscreen* v = s->viewob[0].get();
    v->control = control.get();
    s->viewob[1]->control = nullptr;
    s->viewob[2]->control = nullptr;
    for (int i = 0; i < 3; ++i)
        silence_hud_prefs(s->viewob[i].get());

    mode.set_hud(0, "5:3", 0);
    mode.set_hud(1, "ROUND 2", 1);
    mode.set_hud(2, "WP 12/36", 2);
    mode.set_hud(3, "BALL!", 3);

    const int tm = v->yloc;
    const int lm = v->xloc;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto frame = capture_rendered_frame(*s);

    EXPECT_FALSE(trace_contains("mode_hud", "slot=0"))
        << ">2-view panes suppress the right-aligned slots";
    EXPECT_FALSE(trace_contains("mode_hud", "slot=1"));
    EXPECT_TRUE(box_has_pixels(frame, lm + 2, tm + 35,
                               lm + 2 + 8 * 6, tm + 44))
        << "slot 2 still draws in small panes";
    EXPECT_TRUE(box_has_pixels(frame, lm + 2, tm + 27,
                               lm + 2 + 5 * 6, tm + 36))
        << "slot 3 still draws in small panes";

    // Restore the single-view layout for the tests that follow.
    s->viewob[2].reset();
    s->viewob[1].reset();
    s->numviews = 1;
    s->initialize_views();
}

TEST(ModeUi, beacon_pulse_marker_pulses_inside_the_view)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    walker* target = mode.spawn_living(160, 96, 2);
    ASSERT_NE(nullptr, target);
    // Camera at the world origin: the 320x200 view covers the target.
    v->topx = 0;
    v->topy = 0;
    s->world().mode.beacons[0].entity_id =
        static_cast<std::int32_t>(target->entity_id());
    s->world().mode.beacons[0].team = 2;

    // Phase 0: the narrowest bar (8px) under the sprite.
    s->world().tick_count_ = 0;
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto narrow = capture_rendered_frame(*s);
    EXPECT_TRUE(trace_contains("mode_hud", "beacon_pulse slot=0 w=8"));
    const int cx = 160 + target->sizex() / 2;
    const int bar_y = 96 + target->sizey() / 2 + target->sizey() / 2 + 1;
    EXPECT_TRUE(box_pixels_all_colored(narrow, cx - 4, bar_y - 1,
                                       cx + 4, bar_y + 3, 72))
        << "pulse bar expected under the beacon entity in the team-2 ramp";

    // Phase 5: the widest bar (18px) — the tick drives the pulse, no RNG.
    s->world().tick_count_ = 5;
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto wide = capture_rendered_frame(*s);
    EXPECT_TRUE(trace_contains("mode_hud", "beacon_pulse slot=0 w=18"));
    EXPECT_TRUE(box_has_pixels(wide, cx - 9, bar_y - 1, cx - 4, bar_y + 3))
        << "the phase-5 bar must be wider than the phase-0 bar";
    EXPECT_NE(narrow, wide);

    // An empty slot and a dead target draw nothing.
    target->set_dead(1);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(trace_contains("mode_hud", "beacon_pulse"))
        << "a dead beacon entity must not draw";

    s->world().tick_count_ = 0;
    v->control = old_control;
}

TEST(ModeUi, beacon_edge_arrows_point_at_offscreen_beacons)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    // Far right of the camera window: x = 2000 with a 320px view at topx 0.
    walker* east = mode.spawn_living(2000, 96, 1);
    ASSERT_NE(nullptr, east);
    v->topx = 0;
    v->topy = 0;
    s->world().mode.beacons[1].entity_id =
        static_cast<std::int32_t>(east->entity_id());
    s->world().mode.beacons[1].team = 1;

    const int tm = v->yloc;
    const int rm = v->endx;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto frame = capture_rendered_frame(*s);
    EXPECT_TRUE(trace_contains("mode_hud", "beacon_edge slot=1 dx=1 dy=0"));
    // The arrow tip sits at the clamped x (rm-5) on the entity's row.
    const int cy = 96 + east->sizey() / 2;
    EXPECT_TRUE(box_pixels_all_colored(frame, rm - 8, cy - 3,
                                       rm - 4, cy + 4, 56))
        << "east arrow expected at the right edge in the team-1 ramp";

    // Point it far above instead: the dominant axis flips to dy=-1.
    east->setxy(160, -2000);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto north_frame = capture_rendered_frame(*s);
    EXPECT_TRUE(trace_contains("mode_hud", "beacon_edge slot=1 dx=0 dy=-1"));
    const int ncx = 160 + east->sizex() / 2;
    EXPECT_TRUE(box_pixels_all_colored(north_frame, ncx - 3, tm + 4,
                                       ncx + 4, tm + 8, 56))
        << "north arrow expected at the top edge";

    v->control = old_control;
}

TEST(ModeUi, respawn_countdown_draws_on_scripted_worlds)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);
    control->set_dead(1);

    og::sim::CtfRespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 120; // 10 s at the 12 Hz sim rate
    entry.walker_entity_id = control->entity_id();
    s->world().respawn.respawn_queue.push_back(entry);

    const int tm = v->yloc;
    const int lm = v->xloc;

    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(box_has_pixels(capture_rendered_frame(*s),
                               lm + 4, tm + 11, lm + 4 + 13 * 6, tm + 20))
        << "RESPAWN IN countdown expected for a dead scripted-world control";

    // The scripted arm is required: without mode.active (and with classic
    // respawns off) the countdown must not draw.
    s->world().mode.active = false;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                lm + 4, tm + 11, lm + 4 + 13 * 6, tm + 20))
        << "no countdown without an active engine owning the queue";
    s->world().mode.active = true;

    s->world().respawn.respawn_queue.clear();
    v->control = old_control;
}

TEST(ModeUi, dead_pending_control_survives_cleanup_on_scripted_worlds)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    ASSERT_NE(nullptr, control->myguy);
    control->set_dead(1);
    viewscreen* v = s->viewob[0].get();
    walker* old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    og::sim::CtfRespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 60;
    entry.walker_entity_id = control->entity_id();
    s->world().respawn.respawn_queue.push_back(entry);

    // The cleanup runs at the head of every dispatched event batch; the
    // scripted arm keeps the corpse bound exactly like CTF/classic do.
    const og::sim::SimEventBatch empty_batch;
    ASSERT_TRUE(s->dispatch_sim_event_batch(empty_batch));
    ASSERT_EQ(control.get(), v->control)
        << "a pending-respawn corpse must stay bound on scripted worlds";

    // Without the pending entry the dead control is nulled as always.
    s->world().respawn.respawn_queue.clear();
    ASSERT_TRUE(s->dispatch_sim_event_batch(empty_batch));
    EXPECT_EQ(nullptr, v->control);

    // The retention is strictly gated on the ACTIVE scripted mode.
    s->world().respawn.respawn_queue.push_back(entry);
    s->world().mode.active = false;
    v->control = control.get();
    ASSERT_TRUE(s->dispatch_sim_event_batch(empty_batch));
    EXPECT_EQ(nullptr, v->control)
        << "an inactive mode must not retain dead controls";
    s->world().mode.active = true;

    s->world().respawn.respawn_queue.clear();
    v->control = old_control;
}

TEST(ModeUi, respawn_camera_focus_follows_scripted_entries)
{
    ModeScreenWorld mode;
    screen* s = mode.s;

    // A dead in-world control with a pending entry far from the corpse: the
    // camera must center the entry's recorded destination, not the corpse.
    walker* hero = mode.spawn_living(64, 64, 0);
    ASSERT_NE(nullptr, hero);
    hero->set_user(0);
    hero->set_dead(1);

    viewscreen* v = s->viewob[0].get();
    walker* old_control = v->control;
    v->control = hero;

    og::sim::CtfRespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 120;
    entry.walker_entity_id = hero->entity_id();
    entry.x = 400;
    entry.y = 300;
    s->world().respawn.respawn_queue.push_back(entry);

    s->redraw(); // settle the camera (one full refresh)
    const Sint32 focused_topx = v->topx;
    const Sint32 expected_topx =
        400 - (v->xview - hero->sizex()) / 2;
    EXPECT_EQ(expected_topx, focused_topx)
        << "scripted-world respawn entries must steer the camera (D14)";

    // An entry without a recorded destination keeps the corpse focus.
    s->world().respawn.respawn_queue[0].x = -1;
    s->world().respawn.respawn_queue[0].y = -1;
    s->redraw();
    EXPECT_NE(focused_topx, v->topx)
        << "a destination-less entry must fall back to the corpse camera";

    s->world().respawn.respawn_queue.clear();
    v->control = old_control;
}
