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
#include "../src/interface/ui/picker_sdl_defs.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

loader* sdl_entity_loader();
short new_score_panel(screen* s, short do_it);
std::string get_editor_family_label(Order order, Sint32 family, char livings[][20],
                                    const char* treasures[], const char* weapons[]);
std::string get_editor_level_label(Order order, Sint32 family, Sint32 level);

namespace {

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

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

TEST(CtfUi, team_build_ctf_buttons_follow_campaign_and_cycle)
{
    screen* s = test_screen();
    SaveData& save = s->save_data;
    const std::string old_campaign = save.current_campaign;
    const short old_teams = save.ctf_team_count;
    const short old_caps = save.ctf_capture_limit;

    // Classic campaign: the appended CTF settings buttons stay hidden.
    save.current_campaign = "org.openglad.gladiator";
    button* classic = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    ASSERT_GE(count, 13) << "team build should expose the appended CTF buttons";
    EXPECT_EQ("ctf_teams", classic[11].id);
    EXPECT_EQ("ctf_caps", classic[12].id);
    EXPECT_TRUE(classic[11].hidden) << "classic campaigns hide CTF settings";
    EXPECT_TRUE(classic[12].hidden);

    // CTF campaign: visible, with live labels from the save.
    save.current_campaign = "org.openglad.ctf";
    save.ctf_team_count = 2;
    save.ctf_capture_limit = 0;
    button* ctf_buttons = picker_createmenu_buttons();
    EXPECT_FALSE(ctf_buttons[11].hidden);
    EXPECT_FALSE(ctf_buttons[12].hidden);
    EXPECT_EQ("Teams: 2", ctf_buttons[11].label);
    EXPECT_EQ("Limit: Map", ctf_buttons[12].label);

    // The button callbacks cycle the save fields.
    (void)change_ctf_teams();
    EXPECT_EQ(3, (int)save.ctf_team_count);
    (void)change_ctf_caps();
    EXPECT_EQ(1, (int)save.ctf_capture_limit);

    save.current_campaign = old_campaign;
    save.ctf_team_count = old_teams;
    save.ctf_capture_limit = old_caps;
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

    static char livings[NUM_FAMILIES][20] = {};
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
