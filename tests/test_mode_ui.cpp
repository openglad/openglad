// SDL-side scripted-mode presentation: the generic ModeState HUD panel
// (og.set_hud_line slots at the proven CTF panel positions), beacon edge
// arrows + in-view pulse markers, the shared respawn countdown on scripted
// worlds, the respawn camera-focus generalization, the radar landmark /
// beacon consumption, and the scripted results surfaces. Worlds are built
// in-test: TYPE_SCRIPTED + direct ModeState field writes (the og.* bindings
// are the production writers; every renderer here reads the same fields).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/resources/gparser.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/view_sizes.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/game_context.h>
#include <openglad/resources/io_common.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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
        world.mode = og::sim::ModeState{};
        world.type |= GameWorld::TYPE_SCRIPTED;
        world.mode.active = true;
        world.mode.init_attempted = true;
    }

    ~ModeScreenWorld()
    {
        GameWorld& world = s->world();
        world.mode = og::sim::ModeState{};
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
        const std::size_t copied =
            std::min(std::strlen(text), line.text.size() - 1);
        std::memcpy(line.text.data(), text, copied);
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

// The mode row's own horizontal window, recomputed from the same inputs
// draw_mode_panel uses: it starts past the classic caption's button box
// (lm+1..lm+63) and past the caption text itself, and stops short of the
// TEAM/FOES column while PREF_FOES is on.
struct ModeRowWindow
{
    int left = 0;
    int right = 0;
    int budget = 0;
};

ModeRowWindow mode_row_window(viewscreen* v)
{
    walker* const control = v->control;
    const bool classic_hud =
        control != nullptr && !control->dead() && control->user() != -1;
    ModeRowWindow w;
    w.left = v->xloc + 2;
    w.right = v->endx - 2;
    if (classic_hud)
    {
        std::string caption;
        if (control->myguy)
            caption = control->myguy->name;
        else if (!control->stats()->name.empty())
            caption = control->stats()->name;
        const int caption_end =
            v->xloc + 3 + 6 * static_cast<int>(caption.size()) + 4;
        w.left = std::max(v->xloc + 66, caption_end);
        if (v->prefs[PREF_FOES] == PREF_FOES_ON)
            w.right = v->endx - 60;
    }
    w.budget = (w.right - w.left) / 6;
    return w;
}

// Rows in [x0, x1) that carry any lit pixel, as a first/last pair.
// first > last means the band is empty.
std::pair<int, int> lit_row_span(const std::array<unsigned char, 64000>& frame,
                                 int x0, int x1, int y0, int y1)
{
    int first = y1;
    int last = y0 - 1;
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            if (frame[static_cast<std::size_t>(y * 320 + x)] == 0)
                continue;
            first = std::min(first, y);
            last = std::max(last, y);
            break;
        }
    }
    return {first, last};
}

int lit_pixel_count(const std::array<unsigned char, 64000>& frame)
{
    int n = 0;
    for (unsigned char p : frame)
        if (p != 0)
            ++n;
    return n;
}

// THE R1 clamp proof. Render the same frame twice — once with the ModeState
// HUD slots blanked, once as authored — and diff. Every pixel that differs
// IS the mode row, whatever else the classic HUD painted around it. Each of
// those pixels must fall inside SOME pane's own row window and on that
// pane's own tm+4 row band, so no row can reach a neighboring viewport.
void expect_mode_row_confined_to_its_pane(screen* s)
{
    const og::sim::ModeState authored = s->world().mode;
    og::sim::ModeState blanked = authored;
    for (og::sim::ModeHudLine& line : blanked.hud)
        line.text = {};

    s->world().mode = blanked;
    s->clearbuffer();
    EXPECT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto without = capture_rendered_frame(*s);

    s->world().mode = authored;
    s->clearbuffer();
    EXPECT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto with = capture_rendered_frame(*s);

    struct Band { int x0, x1, y0, y1; };
    std::vector<Band> bands;
    for (int i = 0; i < s->numviews; ++i)
    {
        viewscreen* v = s->viewob[i].get();
        if (v == nullptr)
            continue;
        const ModeRowWindow w = mode_row_window(v);
        // The 5x6 font puts the tm+4 row on scanlines tm+4 .. tm+9.
        bands.push_back({w.left, w.right, v->yloc + 4, v->yloc + 10});
    }

    int changed = 0;
    int outside = 0;
    std::string first_stray;
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * 320 + x);
            if (with[i] == without[i])
                continue;
            ++changed;
            bool inside = false;
            for (const Band& b : bands)
            {
                if (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1)
                {
                    inside = true;
                    break;
                }
            }
            if (!inside)
            {
                ++outside;
                if (first_stray.empty())
                    first_stray = std::to_string(x) + "," + std::to_string(y);
            }
        }
    }
    EXPECT_GT(changed, 0) << "the mode row must actually paint something";
    EXPECT_EQ(0, outside)
        << outside << " mode-row pixels landed outside every pane's row "
        << "window (first at " << first_stray << ")";
}

} // namespace

// The four ModeState slots collapse onto ONE row at tm+4: each slot loses
// the team color word it duplicates from its own ramp, the remainders join
// with " | ", and every segment keeps its team's ramp.
TEST(ModeUi, score_panel_composes_one_row_from_the_hud_slots)
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

    mode.set_hud(0, "RED 5", 0);
    mode.set_hud(1, "BLUE 3", 2);
    mode.set_hud(2, "", 255);
    mode.set_hud(3, "", 255);

    const int tm = v->yloc;
    const int lm = v->xloc;
    const ModeRowWindow win = mode_row_window(v);
    ASSERT_GT(win.budget, 10) << "a single 320-wide view has room for the row";

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto frame = capture_rendered_frame(*s);

    // The composed row: "5 | 3" at tm+4, starting at the window's left edge.
    const std::string row_trace = std::format(
        "row y={} x={} budget={} text=5 - 3", tm + 4, win.left, win.budget);
    EXPECT_TRUE(trace_contains("mode_hud", row_trace.c_str()))
        << "expected the joined one-line row; trace: " << row_trace;

    // Word-stripping: the team words never reach the draw.
    EXPECT_FALSE(trace_contains("mode_hud", "RED"))
        << "slot 0's team word is redundant beside its team ramp";
    EXPECT_FALSE(trace_contains("mode_hud", "BLUE"));

    // Per-segment colors: "5" in the team-0 ramp, "3" in the team-2 ramp,
    // and the " | " between them in the default HUD yellow.
    EXPECT_TRUE(box_pixels_all_colored(frame, win.left, tm + 3,
                                       win.left + 6, tm + 12, 40, 7))
        << "the first segment takes the team-0 ramp";
    EXPECT_TRUE(box_pixels_all_colored(frame, win.left + 6, tm + 3,
                                       win.left + 24, tm + 12,
                                       static_cast<unsigned char>(YELLOW), 7))
        << "the separator takes the default HUD color";
    EXPECT_TRUE(box_pixels_all_colored(frame, win.left + 24, tm + 3,
                                       win.left + 30, tm + 12, 72, 7))
        << "the second segment takes the team-2 ramp";

    // The retired multi-row layout is gone: nothing paints on the old
    // tm+28 / tm+36 slot rows, which is where the banner block begins.
    EXPECT_FALSE(box_has_pixels(frame, lm + 2, tm + 26, win.right, tm + 46))
        << "the old slot rows must be empty — that band belongs to the banner";

    // A slot whose team byte is unset keeps its whole text (nothing is
    // colored to make the leading word redundant).
    mode.set_hud(0, "", 255);
    mode.set_hud(1, "", 255);
    mode.set_hud(2, "OVERTIME", 255);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(trace_contains("mode_hud", "seg team=255"));
    EXPECT_TRUE(trace_contains("mode_hud", "text=OVERTIME"))
        << "an unteamed slot keeps its text verbatim";

    // Stripping needs a real word boundary: a line that merely starts with
    // the color's letters survives whole.
    mode.set_hud(2, "", 255);
    mode.set_hud(0, "REDOUBT 4", 0);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(trace_contains("mode_hud", "text=REDOUBT 4"))
        << "only a whole leading color word is stripped";

    // A line that is nothing BUT the color word keeps it, so the slot still
    // shows something.
    mode.set_hud(0, "RED", 0);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(trace_contains("mode_hud", "text=RED"))
        << "stripping must never empty a slot";

    // All slots empty: the row draws nothing at all.
    mode.set_hud(0, "", 255);
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s), win.left, tm + 3,
                                win.right, tm + 12))
        << "an all-empty ModeState paints no row";

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
    const ModeRowWindow win = mode_row_window(v);

    // The row does paint while both gates are set — otherwise the negative
    // probes below would pass vacuously.
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    ASSERT_TRUE(box_has_pixels(capture_rendered_frame(*s),
                               win.left, tm + 3, win.right, tm + 12))
        << "the mode row must paint when the mode is active and scripted";

    s->world().mode.active = false;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                win.left, tm + 3, win.right, tm + 12))
        << "no mode HUD may paint when the mode is not active";

    s->world().mode.active = true;
    s->world().type =
        static_cast<char>(s->world().type & ~GameWorld::TYPE_SCRIPTED);
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s),
                                win.left, tm + 3, win.right, tm + 12))
        << "no mode HUD may paint without the TYPE_SCRIPTED bit";
    s->world().type |= GameWorld::TYPE_SCRIPTED;

    v->control = old_control;
}

// The retired layout suppressed slots by view count. The budget decides it
// now: a half-width pane that cannot hold the joined row shows only its own
// team's segment, and never crosses into its neighbor.
TEST(ModeUi, score_panel_small_panes_keep_only_the_local_team_segment)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control0 = make_control(0);
    auto control1 = make_control(1);
    auto control2 = make_control(2);
    ASSERT_NE(nullptr, control0);
    ASSERT_NE(nullptr, control1);
    ASSERT_NE(nullptr, control2);

    // Rebuild the view layout as a 3-player split; restored below.
    s->numviews = 3;
    s->viewob[0] = std::make_unique<viewscreen>(
        T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0);
    s->viewob[1] = std::make_unique<viewscreen>(
        T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1);
    s->viewob[2] = std::make_unique<viewscreen>(
        T_LEFT_THREE_FOUR, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2);
    viewscreen* v = s->viewob[0].get();
    v->control = control0.get();
    s->viewob[1]->control = control1.get();
    s->viewob[2]->control = control2.get();
    for (int i = 0; i < 3; ++i)
        silence_hud_prefs(s->viewob[i].get());

    // Four teams' worth of score: far more than a half-width pane can hold.
    mode.set_hud(0, "RED 1234", 0);
    mode.set_hud(1, "GREEN 5678", 1);
    mode.set_hud(2, "BLUE 9012", 2);
    mode.set_hud(3, "YELLOW 3456", 3);

    const int tm = v->yloc;
    const ModeRowWindow win = mode_row_window(v);
    const ModeRowWindow win1 = mode_row_window(s->viewob[1].get());
    ASSERT_GE(win.budget, 4) << "the pane must still be wide enough to draw";
    ASSERT_LT(win.budget, 25) << "the pane must be too narrow for the join";

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto frame = capture_rendered_frame(*s);

    // The documented narrow-pane rule: when the joined row does not fit,
    // fall back to the LOCAL team's segment alone. Each pane keeps its own
    // control's team — no separator, no rivals.
    EXPECT_TRUE(trace_contains(
        "mode_hud",
        std::format("row y={} x={} budget={} text=1234", tm + 4, win.left,
                    win.budget).c_str()))
        << "the team-0 pane keeps only team 0's score";
    EXPECT_TRUE(trace_contains(
        "mode_hud",
        std::format("row y={} x={} budget={} text=5678", tm + 4, win1.left,
                    win1.budget).c_str()))
        << "the team-1 pane keeps only team 1's score";
    EXPECT_FALSE(trace_contains("mode_hud", " - "))
        << "a single surviving segment needs no separator";
    EXPECT_TRUE(box_has_pixels(frame, win.left, tm + 3, win.right, tm + 12))
        << "the local team's score still draws in a small pane";

    // R1: diff the frame against a blank-slot render — every mode-row pixel
    // in every pane lands inside that pane's own window.
    expect_mode_row_confined_to_its_pane(s);

    // Restore the single-view layout for the tests that follow.
    s->viewob[2].reset();
    s->viewob[1].reset();
    s->numviews = 1;
    s->initialize_views();
}

// og.set_hud_line accepts 25 characters; a 2-view right pane has room for
// fewer. Nothing downstream clips to the pane (write_xy passes the
// whole-canvas port), so the row has to clamp itself — it starts at a bound
// inside its own pane and truncates end-first to the remaining characters.
TEST(ModeUi, score_panel_truncates_the_row_to_the_pane)
{
    ClassicModeHudCanvasGuard classic_canvas;
    ModeScreenWorld mode;
    screen* s = mode.s;

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);

    s->numviews = 2;
    s->initialize_views();
    viewscreen* left = s->viewob[0].get();
    viewscreen* right = s->viewob[1].get();
    ASSERT_NE(nullptr, right);
    left->control = nullptr;
    right->control = control.get();
    silence_hud_prefs(left);
    silence_hud_prefs(right);

    // 25 characters — the binding's maximum.
    const char* const long_line = "YELLOW 999/999 OVERTIME!!";
    ASSERT_EQ(25u, std::strlen(long_line));
    mode.set_hud(0, long_line, 0);
    mode.set_hud(1, "", 255);
    mode.set_hud(2, "", 255);
    mode.set_hud(3, "", 255);

    const int tm = right->yloc;
    const ModeRowWindow win = mode_row_window(right);
    ASSERT_GT(win.budget, 0);
    ASSERT_LT(win.budget, 25) << "the right pane must be too narrow for 25 chars";

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto frame = capture_rendered_frame(*s);

    EXPECT_TRUE(box_has_pixels(frame, win.left, tm + 3, win.right, tm + 12))
        << "the truncated line must still paint inside the right pane";
    // R1: the row owns only [win.left, win.right) of its own pane. Diffing
    // against a blank-slot render isolates the row from the classic HUD the
    // neighboring pane also draws on this scanline.
    expect_mode_row_confined_to_its_pane(s);

    // The trace carries the truncated HEAD, not the authored line: end-first
    // truncation drops the tail.
    const std::string expected_head = std::format(
        "row y={} x={} budget={} text={}", tm + 4, win.left, win.budget,
        std::string(long_line).substr(0, static_cast<std::size_t>(win.budget)));
    EXPECT_TRUE(trace_contains("mode_hud", expected_head.c_str()))
        << "truncation keeps the head and drops the tail; want: "
        << expected_head;

    // A line that fits is untouched (past its team word — team 0 is RED, and
    // this line names it, so the word goes).
    mode.set_hud(0, "RED 5/20", 0);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    EXPECT_TRUE(trace_contains("mode_hud", "text=5/20"))
        << "a line inside the budget draws whole, minus its team word";

    s->viewob[1].reset();
    s->numviews = 1;
    s->initialize_views();
}

// THE reason the row was condensed. viewscreen::display_text writes the
// announcement banner at viewport-local y = 30, 36, 42, 48, 54; the retired
// slot rows sat at tm+28 and tm+36 and shredded it. Render each channel
// alone over the SAME columns and prove their lit scanlines cannot meet.
TEST(ModeUi, mode_row_and_announcement_banner_never_share_a_scanline)
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

    mode.set_hud(0, "RED 12", 0);
    mode.set_hud(1, "BLUE 9", 2);
    mode.set_hud(2, "", 255);
    mode.set_hud(3, "", 255);

    const int tm = v->yloc;
    const ModeRowWindow win = mode_row_window(v);

    // Pass 1: the mode row alone.
    v->clear_text();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto row_only = capture_rendered_frame(*s);
    const auto row_span =
        lit_row_span(row_only, win.left, win.right, v->yloc, v->endy);
    ASSERT_LE(row_span.first, row_span.second) << "the mode row must paint";

    // Pass 2: the banner alone, over the same columns. A long announcement
    // is centered across the pane, so it covers the mode row's columns.
    s->do_notify("ANNOUNCEMENT LINE ONE", nullptr);
    s->do_notify("ANNOUNCEMENT LINE TWO", nullptr);
    s->clearbuffer();
    v->display_text();
    const auto banner_only = capture_rendered_frame(*s);
    const auto banner_span =
        lit_row_span(banner_only, win.left, win.right, v->yloc, v->endy);
    ASSERT_LE(banner_span.first, banner_span.second)
        << "the banner must paint over the mode row's columns";

    // The proof: every lit banner scanline is strictly below every lit mode
    // row scanline.
    EXPECT_LT(row_span.second, banner_span.first)
        << "mode row rows [" << row_span.first << "," << row_span.second
        << "] must end above banner rows [" << banner_span.first << ","
        << banner_span.second << "]";
    EXPECT_GE(banner_span.first, v->yloc + 30)
        << "the banner block still starts at viewport-local y=30";
    EXPECT_EQ(tm + 4, row_span.first)
        << "the mode row still starts on the tm+4 scanline";

    // And drawn together, neither channel loses a pixel: the union is the
    // exact sum, so nothing overdraws anything.
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    v->display_text();
    const auto both = capture_rendered_frame(*s);
    for (int y = row_span.first; y <= row_span.second; ++y)
    {
        for (int x = win.left; x < win.right; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * 320 + x);
            EXPECT_EQ(row_only[i], both[i])
                << "the banner must not touch the mode row at " << x << ","
                << y;
        }
    }

    v->clear_text();
    v->control = old_control;
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

    og::sim::RespawnEntry entry;
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

    og::sim::RespawnEntry entry;
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

    og::sim::RespawnEntry entry;
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

TEST(ModeUi, radar_landmark_families_blip_without_treasure_sight)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    push_test_context(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();

    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
    control->set_team_num(0);
    ASSERT_LE(control->view_all(), 0)
        << "the probe control must lack treasure sight";

    // A landmark-flagged treasure family: copy a live descriptor, flag it,
    // restore the original afterwards (registry hygiene under shuffle).
    const TreasureFamilyDescriptor* live =
        get_treasure_family_descriptor(FAMILY_LIFE_GEM);
    ASSERT_NE(nullptr, live);
    const TreasureFamilyDescriptor original = *live;
    TreasureFamilyDescriptor flagged = original;
    flagged.radar.color = 100;
    flagged.radar.jitter = 0; // landmarks may not roll the game rng
    flagged.radar.landmark = true;
    ASSERT_TRUE(set_treasure_family_descriptor(FAMILY_LIFE_GEM, flagged));

    {
        auto fx = std::make_unique<walker>();
        fx->set_order_family(Order::Treasure, FAMILY_LIFE_GEM);
        fx->set_dead(0);
        fx->setxy(GRID_SIZE * 3, GRID_SIZE * 2);
        d.world().fxlist.push_back(std::move(fx));
    }

    viewscreen* vs = test_screen()->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = control;
    vs->radarstart = 0;

    radar r(vs, test_screen(), 0);
    r.force_lower_position = true;
    r.start(&d);

    trace_clear();
    ASSERT_EQ(1, static_cast<int>(r.draw(&d)));
    const std::string expected_trace =
        "landmark_blip fam=" + std::to_string(static_cast<int>(FAMILY_LIFE_GEM));
    EXPECT_TRUE(trace_contains("radar", expected_trace.c_str()))
        << "a landmark-flagged family must blip without treasure sight";

    // Un-flagged again: the loot rule — no blip without treasure sight.
    ASSERT_TRUE(set_treasure_family_descriptor(FAMILY_LIFE_GEM, original));
    trace_clear();
    ASSERT_EQ(1, static_cast<int>(r.draw(&d)));
    EXPECT_FALSE(trace_contains("radar", "landmark_blip"))
        << "without the flag the family follows the treasure-sight rule";

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    pop_test_context();
}

TEST(ModeUi, radar_draws_beacon_blips_in_beacon_team_color)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    push_test_context(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();

    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
    control->set_team_num(0);

    walker* target = d.add_ob(Order::Living, FAMILY_THIEF);
    ASSERT_NE(nullptr, target);
    target->setxy(GRID_SIZE * 4, GRID_SIZE * 2);
    target->set_team_num(3);

    GameWorld& world = d.world();
    world.type |= GameWorld::TYPE_SCRIPTED;
    world.mode.active = true;
    world.mode.beacons[0].entity_id =
        static_cast<std::int32_t>(target->entity_id());
    world.mode.beacons[0].team = 3;

    viewscreen* vs = test_screen()->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = control;
    vs->radarstart = 0;

    radar r(vs, test_screen(), 0);
    r.force_lower_position = true;
    r.start(&d);

    trace_clear();
    ASSERT_EQ(1, static_cast<int>(r.draw(&d)));
    const std::string expected_trace =
        "beacon_blip id=" + std::to_string(target->entity_id());
    EXPECT_TRUE(trace_contains("radar", expected_trace.c_str()))
        << "an occupied beacon slot must blip on the radar";
    EXPECT_TRUE(trace_contains("radar", "color=88"))
        << "the blip carries the beacon team ramp (3*16+40)";

    // A dead target draws nothing; neither does an inactive mode.
    target->set_dead(1);
    trace_clear();
    ASSERT_EQ(1, static_cast<int>(r.draw(&d)));
    EXPECT_FALSE(trace_contains("radar", "beacon_blip"));

    target->set_dead(0);
    world.mode.active = false;
    trace_clear();
    ASSERT_EQ(1, static_cast<int>(r.draw(&d)));
    EXPECT_FALSE(trace_contains("radar", "beacon_blip"));

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    pop_test_context();
}

TEST(ModeUi, format_mode_scoreboard_segments_reads_hud_slot_zero)
{
    og::sim::ModeState mode;
    EXPECT_TRUE(format_mode_scoreboard_segments(mode).empty())
        << "an empty slot 0 draws nothing";

    mode.hud[0].team = 2;
    std::strncpy(mode.hud[0].text.data(), "GOALS 1:3",
                 mode.hud[0].text.size() - 1);
    std::vector<ScoreboardSegment> segments =
        format_mode_scoreboard_segments(mode);
    ASSERT_EQ(1u, segments.size());
    EXPECT_EQ("GOALS 1:3", segments[0].text);
    EXPECT_EQ(2, segments[0].team);

    mode.hud[0].team = 255;
    segments = format_mode_scoreboard_segments(mode);
    ASSERT_EQ(1u, segments.size());
    EXPECT_EQ(-1, segments[0].team) << "team 255 reads as neutral";
}

TEST(ModeUi, scripted_ending_popup_reports_outcome_from_local_controls)
{
    ModeScreenWorld mode;
    screen* s = mode.s;
    GameWorld& world = s->world();
    world.mode.winner_team = 1;
    std::strncpy(world.mode.name.data(), "SOCCER",
                 world.mode.name.size() - 1);
    s->save_data.scen_num = 1;

    auto control = make_control(1); // local hero on the winning team
    ASSERT_NE(nullptr, control);
    viewscreen* v = s->viewob[0].get();
    walker* old_control = v->control;
    v->control = control.get();

    // Rematch shape (nextlevel == scen_num): VICTORY + rematch line, and the
    // body names the mode.
    trace_clear();
    (void)results_screen(0, 1);
    EXPECT_TRUE(trace_contains("popup", "VICTORY!"));
    EXPECT_TRUE(trace_contains("popup", "SOCCER: GREEN TEAM WINS!"));
    EXPECT_TRUE(trace_contains("popup", "Get ready for a rematch!"));

    // Moving-on shape names the next scenario.
    trace_clear();
    (void)results_screen(0, 2);
    EXPECT_TRUE(trace_contains("popup", "Moving on to"));

    // A local control on the losing side reads DEFEAT!.
    control->set_team_num(0);
    trace_clear();
    (void)results_screen(0, 1);
    EXPECT_TRUE(trace_contains("popup", "DEFEAT!"));

    // No local controls at all: the neutral MATCH OVER.
    v->control = nullptr;
    trace_clear();
    (void)results_screen(0, 1);
    EXPECT_TRUE(trace_contains("popup", "MATCH OVER"));

    // Undecided match: the scripted popup declines and the generic victory
    // popup shows instead.
    world.mode.winner_team = -1;
    v->control = control.get();
    trace_clear();
    (void)results_screen(0, 2);
    EXPECT_FALSE(trace_contains("popup", "TEAM WINS"));
    EXPECT_TRUE(trace_contains("popup", "Victory!"))
        << "without a winner the classic popup chain runs";

    v->control = old_control;
}

// --- Generator HP bars ---------------------------------------------------

// Generators take damage like livings and get the same mini HP bar. What
// they do NOT reliably get is a denominator: walker::set_difficulty's
// Order::Generator branch writes hitpoints (100 * level, difficulty-scaled)
// and never max_hitpoints, so the three stock families whose gloader row
// carries 0 base HP (tower, bones, treehouse) reach the renderer with
// hp > 0 and max_hp == 0. Both arms are pinned here.
TEST(ModeUi, generator_mini_hp_bar_follows_the_living_rules)
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
    v->topx = 0;
    v->topy = 0;

    const std::string saved_cfg = cfg.get_setting("effects", "mini_hp_bar");
    cfg.apply_setting("effects", "mini_hp_bar", "on");

    walker* gen = s->world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_NE(nullptr, gen);
    gen->setxy(160, 96);
    gen->set_team_num(1);

    // Clear, draw the bar alone, and count what landed. Coordinate-free: any
    // lit pixel at all means a bar was drawn.
    const auto render = [&]() {
        trace_clear();
        ScopedGameplayUiCanvas gameplay_ui(*s);
        s->clearbuffer();
        draw_small_health_bar(gen, v);
        return lit_pixel_count(capture_rendered_frame(*s));
    };

    // Arm 1 — the stock quirk. FAMILY_TOWER's gloader row carries 0 base HP,
    // so max_hitpoints is 0 while the entity is alive and damaged.
    ASSERT_FLOAT_EQ(0.0f, gen->stats()->max_hitpoints())
        << "FAMILY_TOWER is one of the max_hp == 0 generator families";
    gen->stats()->set_hitpoints(150.0f);
    gen->set_last_hitpoints(300.0f);
    EXPECT_EQ(0, render())
        << "no denominator means no bar — a full bar would lie about damage";
    EXPECT_TRUE(trace_contains("hp_bar", "skip_no_max"))
        << "the skip is deliberate, not a division that fell through";

    // Arm 2 — normalized. A real max_hitpoints turns the ordinary living
    // rules on and a damaged generator gets its bar.
    gen->stats()->set_max_hitpoints(300.0f);
    gen->stats()->set_hitpoints(150.0f);
    EXPECT_GT(render(), 0) << "a damaged generator shows a bar";
    EXPECT_TRUE(trace_contains(
        "hp_bar",
        std::format("draw order={}",
                    static_cast<int>(Order::Generator)).c_str()))
        << "and it is drawn through the generator arm";

    // Arm 3 — undamaged. The living "hides at full" rule applies unchanged.
    gen->stats()->set_hitpoints(300.0f);
    gen->set_last_hitpoints(300.0f);
    EXPECT_EQ(0, render()) << "a full-HP generator shows no bar";

    // Arm 4 — hp above the recorded max (a tent's difficulty-scaled HP over
    // its 100-HP family row) reads as full instead of overflowing the bar.
    gen->stats()->set_max_hitpoints(100.0f);
    gen->stats()->set_hitpoints(300.0f);
    EXPECT_EQ(0, render()) << "hp > max reads as full, not as an overflow";

    // Livings are untouched by any of it: same world, same call, a bar.
    walker* foe = mode.spawn_living(200, 96, 1);
    ASSERT_NE(nullptr, foe);
    foe->stats()->set_max_hitpoints(100.0f);
    foe->stats()->set_hitpoints(40.0f);
    foe->set_last_hitpoints(60.0f);
    {
        ScopedGameplayUiCanvas gameplay_ui(*s);
        s->clearbuffer();
        draw_small_health_bar(foe, v);
        EXPECT_GT(lit_pixel_count(capture_rendered_frame(*s)), 0)
            << "a damaged living still shows its bar";
    }

    cfg.apply_setting("effects", "mini_hp_bar", saved_cfg);
    gen->set_dead(1);
    foe->set_dead(1);
    v->control = old_control;
}

