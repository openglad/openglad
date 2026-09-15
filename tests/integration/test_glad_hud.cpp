#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/legacy/base.h>
#include <openglad/core/test_trace.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/video_sdl.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <memory>
#include <string>
#include <vector>

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
// From score_panel.cpp (B5). pending = dormant delayed-spawn hostiles
// (alive, so subtractable from remaining_foes); pending_respawn = hostile
// classic-respawn queue entries (corpses, display-only).
void pending_hostile_wave_counts(const GameWorld& world, walker* viewer,
                                 short& pending, short& pending_respawn,
                                 std::uint32_t& next_wake_ticks);
// From score_panel.cpp: forced-flee (scared) countdown source.
int hud_scared_flee_ticks(walker* viewer);

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
    w->set_team_num(team);
    w->set_dead(0);
    w->set_user(0);
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
    w->set_team_num(team);
    w->set_dead(0);
    w->set_user(-1);
    w->setxy(120, 100);
    return w;
}

// get_pixel's index read-back returns the FIRST palette index matching the
// pixel's RGB, so any index whose RGB duplicates an earlier entry (the grey
// ramp does) reads back as that earlier alias. Canonicalize expectations the
// same way before comparing against captured frames.
static unsigned char canonical_palette_index(unsigned char color)
{
    int r = 0, g = 0, b = 0;
    query_palette_reg(color, &r, &g, &b);
    for (int i = 0; i < 256; ++i)
    {
        int tr = 0, tg = 0, tb = 0;
        query_palette_reg(static_cast<unsigned char>(i), &tr, &tg, &tb);
        if (r == tr && g == tg && b == tb)
            return static_cast<unsigned char>(i);
    }
    return color;
}

static std::array<unsigned char, 64000> capture_rendered_frame(screen& scr)
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

// Optional visual still for review: a P6 PPM of the whole classic frame,
// written only when OG_FX_CAPTURE_DIR is set (scripts/media/capture_pr292.sh
// sets it; a normal ctest run pays nothing).  Model:
// tests/integration/test_stair_overlay.cpp dump_viewport_ppm.  Palette
// registers hold the VGA 0..63 range, so each channel takes the same *4 the
// renderer applies before handing SDL an RGB triple (video_sdl.cpp, every
// query_palette_reg -> map_surface_rgb_fast call site).
static void dump_frame_ppm(const std::array<unsigned char, 64000>& frame,
                           const char* scene)
{
    const char* base = getenv("OG_FX_CAPTURE_DIR");
    if (base == nullptr)
        return;
    const std::string dir = std::string(base) + "/" + scene;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    FILE* fp = fopen((dir + "/000.ppm").c_str(), "wb");
    if (fp == nullptr)
        return;
    fprintf(fp, "P6\n320 200\n255\n");
    for (unsigned char index : frame)
    {
        int r = 0, g = 0, b = 0;
        query_palette_reg(index, &r, &g, &b);
        fputc(std::min(255, r * 4), fp);
        fputc(std::min(255, g * 4), fp);
        fputc(std::min(255, b * 4), fp);
    }
    fclose(fp);
}

// The HUD probes below intentionally assert historical pixel positions and
// capture fixed 320x200 frames. Pin that canvas only for this suite; live
// layouts may expand one axis to match a non-16:10 display.
class GladHud : public testing::Test
{
protected:
    void SetUp() override
    {
        game_ = og::runtime::current_session->myscreen_;
        ASSERT_NE(nullptr, game_);
        saved_target_ = game_->active_canvas();
        game_->set_world_canvas_pinned_classic(true);
        game_->relayout_views();
        game_->set_active_canvas(CanvasTarget::World);
    }

    void TearDown() override
    {
        if (game_ == nullptr)
            return;
        game_->set_active_canvas(CanvasTarget::UI);
        game_->set_world_canvas_pinned_classic(false);
        game_->relayout_views();
        game_->set_active_canvas(saved_target_);
    }

private:
    screen* game_ = nullptr;
    CanvasTarget saved_target_ = CanvasTarget::UI;
};

TEST_F(GladHud, glad_remaining_counts)
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
    walker* foe2p = foe2.get();

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(foe1));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(foe2));

    ASSERT_EQ(2, (int)remaining_foes(og::runtime::current_session->myscreen_, controlp)) << "should count non-friendly living foes";
    ASSERT_EQ(2, (int)remaining_team(og::runtime::current_session->myscreen_, 0)) << "should count living on team 0 (including control)";

    foe2p->set_dead(1);
    ASSERT_EQ(1, (int)remaining_foes(og::runtime::current_session->myscreen_, controlp)) << "dead foes should not be counted";
    og::runtime::current_session->myscreen_->world().oblist.clear();
}


// The magic bar's colour is the readable signal for "how much mana is left":
// the MID band (a third to two thirds) and the HIGH band (two thirds to full)
// are separate ramps and separate lengths, and one bleeding into the other
// would misreport the pool at a glance.
TEST_F(GladHud, glad_mp_bar_paints_the_mid_and_high_bands_at_their_lengths)
{
    ASSERT_NE(canonical_palette_index(MID_MP_COLOR),
              canonical_palette_index(HIGH_MP_COLOR))
        << "the two bands must be distinguishable in the captured frame";

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    screen* const s = og::runtime::current_session->myscreen_;
    controlp->stats()->set_max_magicpoints(80);

    // The bar box is 62x7 at (left, top); count only inside it.
    constexpr int kLeft = 10;
    constexpr int kTop = 20;
    const auto band_pixels = [](const std::array<unsigned char, 64000>& frame,
                                unsigned char color) {
        const unsigned char want = canonical_palette_index(color);
        int count = 0;
        for (int y = kTop; y <= kTop + 6; ++y)
            for (int x = kLeft; x <= kLeft + 61; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] == want)
                    ++count;
        return count;
    };

    // 40 of 80: past the LOW third, short of the HIGH two thirds.
    // ceil(40 * 60 / 80) == 30 columns, 5 rows, less the two rounded corners
    // the mask paints back over at x == left + 1.
    s->clearbuffer();
    controlp->stats()->set_magicpoints(40);
    draw_value_bar(kLeft, kTop, controlp, 1, s);
    const auto mid_frame = capture_rendered_frame(*s);
    EXPECT_EQ(148, band_pixels(mid_frame, MID_MP_COLOR));
    EXPECT_EQ(0, band_pixels(mid_frame, HIGH_MP_COLOR));

    // 70 of 80: past two thirds, still short of full.
    // ceil(70 * 60 / 80) == 53 columns.
    s->clearbuffer();
    controlp->stats()->set_magicpoints(70);
    draw_value_bar(kLeft, kTop, controlp, 1, s);
    const auto high_frame = capture_rendered_frame(*s);
    EXPECT_EQ(263, band_pixels(high_frame, HIGH_MP_COLOR));
    EXPECT_EQ(0, band_pixels(high_frame, MID_MP_COLOR));
}

// The radar gems are static chrome: they change only when the watched team
// does. Redrawing them every frame would be four gem blits of pure waste, so
// the draw is memoized on the team it last painted.
TEST_F(GladHud, glad_radar_gems_skip_the_redraw_for_an_unchanged_team)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    screen* const s = og::runtime::current_session->myscreen_;

    viewscreen* const v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);
    walker* const old_control = v->control;
    v->control = controlp;

    // The gem band: the four gems at (246,140), (311,140), (246,189) and
    // (311,189), each a 5x5 sprite drawn around its anchor.
    const auto gem_band = [](const std::array<unsigned char, 64000>& frame) {
        std::vector<unsigned char> band;
        for (int y = 138; y <= 194; ++y)
            for (int x = 244; x <= 314; ++x)
                band.push_back(frame[static_cast<std::size_t>(y * 320 + x)]);
        return band;
    };

    // Settle the memo on some other team first, so the draw below is a real
    // change no matter what ran before this test.
    controlp->set_team_num(1);
    draw_radar_gems(s);

    s->clearbuffer();
    const auto cleared_band = gem_band(capture_rendered_frame(*s));
    controlp->set_team_num(2);
    draw_radar_gems(s);
    const auto drawn_band = gem_band(capture_rendered_frame(*s));
    ASSERT_NE(cleared_band, drawn_band)
        << "a team change must paint the gems";

    // Same team again: nothing is painted, so the cleared band survives.
    s->clearbuffer();
    draw_radar_gems(s);
    EXPECT_EQ(cleared_band, gem_band(capture_rendered_frame(*s)))
        << "an unchanged team must not repaint the gems";

    // Leave the memo somewhere neutral for whatever runs next.
    controlp->set_team_num(1);
    draw_radar_gems(s);
    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

// The bars are the player's only read on "how much is left", and their two
// signals are COLOUR (which band) and LENGTH (how far it fills). The old body
// made ~20 draw calls into a buffer nobody read and checked only that the two
// over-max cases painted *some* ramp pixel, so a bar that used one colour for
// every band, or drew the wrong length, stayed green.
//
// Counting arithmetic, from the two painters:
//   draw_value_bar (glad.cpp:646) fills a 5-row box `bar_length` wide, then
//   rounds the corners with masks that re-black (left+1, top+1) and
//   (left+1, top+5) -- and, once the bar reaches the full 60 columns, the
//   mirrored pair at left+60 too. So bar_length*5 - 2, or -4 at full length.
//   draw_percentage_bar (score_panel.cpp:1114), which new_draw_value_bar
//   uses, paints (L-4) + 3*(L-2) + (L-4) == 5L-14.
//   bar_length itself is ceil(points * 60 / max), capped at 60 over max.
TEST_F(GladHud, glad_hp_bars_paint_their_band_colour_at_their_exact_length)
{
    auto control = make_player(0);
    ASSERT_NE(nullptr, control) << "control should be created";
    walker* const controlp = control.get();
    screen* const s = og::runtime::current_session->myscreen_;

    // The four HP bands must survive the get_pixel index read-back as four
    // distinct values, or none of the counts below mean anything.
    const unsigned char kMax = canonical_palette_index(MAX_HP_COLOR);
    const unsigned char kLow = canonical_palette_index(LOW_HP_COLOR);
    const unsigned char kMid = canonical_palette_index(MID_HP_COLOR);
    const unsigned char kHigh = canonical_palette_index(HIGH_HP_COLOR);
    ASSERT_NE(kMax, kLow);
    ASSERT_NE(kMax, kMid);
    ASSERT_NE(kMax, kHigh);
    ASSERT_NE(kLow, kMid);
    ASSERT_NE(kLow, kHigh);
    ASSERT_NE(kMid, kHigh);

    const auto count_in = [](const std::array<unsigned char, 64000>& frame,
                             int left, int top, int width, int height,
                             unsigned char color) {
        const unsigned char want = canonical_palette_index(color);
        int n = 0;
        for (int y = top; y < top + height; ++y)
            for (int x = left; x < left + width; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] == want)
                    ++n;
        return n;
    };

    constexpr int kLeft = 10;
    constexpr int kTop = 20;
    controlp->stats()->set_max_hitpoints(100);

    struct Band { float hp; unsigned char color; int pixels; const char* why; };
    const Band bands[] = {
        // 20/100: points*3 < max -> the LOW band, ceil(12) columns.
        {20.0f, LOW_HP_COLOR, 12 * 5 - 2, "a fifth of the pool is the LOW band, 12 columns"},
        // 60/100: past a third, short of two thirds -> MID, ceil(36) columns.
        {60.0f, MID_HP_COLOR, 36 * 5 - 2, "three fifths is the MID band, 36 columns"},
        // 90/100: past two thirds, short of full -> HIGH, ceil(54) columns.
        {90.0f, HIGH_HP_COLOR, 54 * 5 - 2, "nine tenths is the HIGH band, 54 columns"},
        // exactly full -> MAX, all 60 columns (and four masked corners).
        {100.0f, MAX_HP_COLOR, 60 * 5 - 4, "a full pool is the MAX band at full length"},
    };

    for (const Band& band : bands)
    {
        SCOPED_TRACE(band.why);
        s->clearbuffer();
        controlp->stats()->set_hitpoints(band.hp);
        draw_value_bar(kLeft, kTop, controlp, 0, s);
        const auto frame = capture_rendered_frame(*s);

        EXPECT_EQ(band.pixels, count_in(frame, kLeft, kTop, 62, 7, band.color))
            << band.why;
        // ... and no other band bleeds in.
        for (unsigned char other : {MAX_HP_COLOR, LOW_HP_COLOR, MID_HP_COLOR, HIGH_HP_COLOR})
        {
            if (canonical_palette_index(other) == canonical_palette_index(band.color))
                continue;
            EXPECT_EQ(0, count_in(frame, kLeft, kTop, 62, 7, other))
                << "band " << (int)other << " must not appear at hp " << band.hp;
        }
    }

    // new_draw_value_bar goes through draw_percentage_bar, so the same bands
    // land on the 5L-14 geometry instead.
    const Band pct_bands[] = {
        {20.0f, LOW_HP_COLOR, 5 * 12 - 14, "percentage bar, LOW band, 12 columns"},
        {80.0f, HIGH_HP_COLOR, 5 * 48 - 14, "percentage bar, HIGH band, 48 columns"},
        {100.0f, MAX_HP_COLOR, 5 * 60 - 14, "percentage bar, MAX band, full length"},
    };
    for (const Band& band : pct_bands)
    {
        SCOPED_TRACE(band.why);
        s->clearbuffer();
        controlp->stats()->set_hitpoints(band.hp);
        new_draw_value_bar(kLeft, kTop, controlp, 0, s);
        const auto frame = capture_rendered_frame(*s);
        EXPECT_EQ(band.pixels, count_in(frame, kLeft, kTop, 60, 7, band.color))
            << band.why;
    }

    // draw_percentage_bar on its own: a 30-wide bar is 5*30-14 coloured pixels
    // and paints nothing past its own right edge.
    {
        s->clearbuffer();
        draw_percentage_bar(kLeft, kTop, LOW_HP_COLOR, 30, s);
        const auto frame = capture_rendered_frame(*s);
        EXPECT_EQ(5 * 30 - 14, count_in(frame, kLeft, kTop, 60, 7, LOW_HP_COLOR))
            << "a 30-long percentage bar paints 5*30-14 pixels";
        EXPECT_EQ(0, count_in(frame, kLeft + 30, kTop, 30, 7, LOW_HP_COLOR))
            << "a 30-long percentage bar paints nothing past column 30";
    }

    // Temporary buffs can put either pool above its nominal maximum. The
    // modern bar caps its LENGTH at the full 60 columns and switches to the
    // animated ramp, rather than wrapping or drawing past the frame.
    {
        constexpr int kBuffLeft = 180;
        const auto ramp_pixels = [&](const std::array<unsigned char, 64000>& frame,
                                     int left, int top, int width,
                                     unsigned char ramp_start) {
            std::array<bool, 256> ramp{};
            for (int i = 0; i < 16; ++i)
                ramp[canonical_palette_index(
                    static_cast<unsigned char>(ramp_start + i))] = true;
            int n = 0;
            for (int y = top; y < top + 7; ++y)
                for (int x = left; x < left + width; ++x)
                    if (ramp[frame[static_cast<std::size_t>(y * 320 + x)]])
                        ++n;
            return n;
        };

        s->clearbuffer();
        controlp->stats()->set_hitpoints(125);
        new_draw_value_bar(kBuffLeft, 20, controlp, 0, s);
        controlp->stats()->set_max_magicpoints(80);
        controlp->stats()->set_magicpoints(100);
        new_draw_value_bar(kBuffLeft, 32, controlp, 1, s);
        const auto buffed = capture_rendered_frame(*s);

        EXPECT_EQ(5 * 60 - 14, ramp_pixels(buffed, kBuffLeft, 20, 60, ORANGE_START))
            << "over-max HP fills the capped 60 columns with the orange ramp";
        EXPECT_EQ(0, ramp_pixels(buffed, kBuffLeft + 60, 20, 40, ORANGE_START))
            << "the over-max bar stops at column 60 instead of running on";
        EXPECT_EQ(5 * 60 - 14, ramp_pixels(buffed, kBuffLeft, 32, 60, WATER_START))
            << "over-max MP fills the capped 60 columns with the water ramp";
        EXPECT_EQ(0, ramp_pixels(buffed, kBuffLeft + 60, 32, 40, WATER_START))
            << "the over-max bar stops at column 60 instead of running on";
    }
}


// draw_gem paints a fixed 12-pixel diamond out of four shades derived from the
// team colour: base, +2, +4, +6 (glad.cpp:624-643). Reading the pixels back is
// the only way to notice the shading or the diamond's shape changing.
TEST_F(GladHud, glad_draw_gem_paints_its_twelve_shaded_points)
{
    screen* const s = og::runtime::current_session->myscreen_;
    constexpr short kX = 10;
    constexpr short kY = 10;
    constexpr unsigned char kLight = 32;
    constexpr unsigned char kMed = kLight + 2;
    constexpr unsigned char kDarker = kMed + 2;
    constexpr unsigned char kDarkest = kDarker + 2;

    s->clearbuffer();
    draw_gem(kX, kY, kLight, s);
    const auto frame = capture_rendered_frame(*s);

    struct Point { int dx; int dy; unsigned char color; };
    const Point gem[] = {
        { 0, 0, kLight},
        {-1, 1, kLight}, { 0, 1, kMed},   { 1, 1, kDarker},
        {-2, 2, kLight}, {-1, 2, kMed},   { 0, 2, kMed},    { 1, 2, kMed}, { 2, 2, kDarkest},
        {-1, 3, kDarker},{ 0, 3, kMed},   { 1, 3, kDarkest},
        { 0, 4, kDarkest},
    };
    for (const Point& p : gem)
    {
        SCOPED_TRACE(testing::Message() << "gem point (" << p.dx << "," << p.dy << ")");
        EXPECT_EQ((int)canonical_palette_index(p.color),
                  (int)frame[static_cast<std::size_t>((kY + p.dy) * 320 + (kX + p.dx))])
            << "draw_gem must shade this point with " << (int)p.color;
    }
}


// new_score_panel unconditionally `return 1` (score_panel.cpp:1101), so
// ASSERT_EQ(1, new_score_panel(...)) was a literal against a literal; and the
// old life-mode probe flipped PREF_LIFE and the score in the SAME step, so a
// HUD that ignored the score entirely still showed a changed row.
//
// What the panel actually promises: the two wrappers are the same drawing as
// new_score_panel; the three PREF_LIFE readouts are three DIFFERENT readouts;
// and the team score reaches the SC row (via an animated count-up) without
// disturbing the life rows.
TEST_F(GladHud, score_panel_wrappers_life_modes_and_the_score_row_are_distinct)
{
    auto control = make_player(0);
    ASSERT_NE(nullptr, control) << "control should be created";
    walker* const controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_level(7);
    controlp->stats()->set_hitpoints(55);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(33);
    controlp->stats()->set_max_magicpoints(80);
    controlp->stats()->set_special_cost(static_cast<unsigned char>(controlp->current_special()), 10);

    screen* const s = og::runtime::current_session->myscreen_;
    viewscreen* const v = s->viewob[0].get();
    ASSERT_NE(nullptr, v) << "view should exist";
    walker* const old_control = v->control;
    v->control = controlp;

    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    Uint32& score = og::runtime::current_session->myscreen_->world_.m_score[0];
    struct ScoreRestore
    {
        Uint32& destination;
        Uint32 value;
        ~ScoreRestore() { destination = value; }
    } restore_score{score, score};

    // A settled score (the count-up clamps down to 0 and consumes no rng) makes
    // the panel a pure function of state, which every comparison below needs.
    score = 0;
    v->prefs[PREF_LIFE] = PREF_LIFE_BARS;
    s->clearbuffer();
    new_score_panel(s, 1);
    const auto settled = capture_rendered_frame(*s);
    s->clearbuffer();
    new_score_panel(s, 1);
    ASSERT_EQ(settled, capture_rendered_frame(*s))
        << "with the score settled the panel must redraw identically";

    // The wrappers ARE new_score_panel: same state in, same pixels out.
    s->clearbuffer();
    score_panel(s, 1);
    EXPECT_EQ(settled, capture_rendered_frame(*s))
        << "score_panel(s, do_it) must draw exactly what new_score_panel does";
    s->clearbuffer();
    score_panel(s);
    EXPECT_EQ(settled, capture_rendered_frame(*s))
        << "score_panel(s) must draw exactly what new_score_panel does";

    // The HP/MP readout block: bars at (lm+2, tm+10) and (lm+2, tm+18), text at
    // (lm+5, tm+12) / (lm+5, tm+20), inside the button box lm+1..lm+63.
    const auto life_rows = [&](const std::array<unsigned char, 64000>& frame) {
        std::vector<unsigned char> rows;
        for (int y = v->yloc + 10; y < v->yloc + 27; ++y)
            for (int x = v->xloc + 1; x < v->xloc + 64; ++x)
                rows.push_back(frame[static_cast<std::size_t>(y * 320 + x)]);
        return rows;
    };
    const auto capture_life = [&](char life_pref) {
        v->prefs[PREF_LIFE] = life_pref;
        s->clearbuffer();
        new_score_panel(s, 1);
        return life_rows(capture_rendered_frame(*s));
    };

    // Three modes, three different readouts -- with the score held fixed, so
    // the difference can only come from PREF_LIFE.
    const auto text_rows = capture_life(PREF_LIFE_TEXT);
    const auto bars_rows = capture_life(PREF_LIFE_BARS);
    const auto both_rows = capture_life(PREF_LIFE_BOTH);
    const auto off_rows = capture_life(PREF_LIFE_OFF);
    EXPECT_NE(text_rows, bars_rows) << "TEXT and BARS are different readouts";
    EXPECT_NE(text_rows, both_rows) << "TEXT and BOTH are different readouts";
    EXPECT_NE(bars_rows, both_rows) << "BARS and BOTH are different readouts";
    EXPECT_NE(off_rows, text_rows) << "OFF draws no readout at all";
    EXPECT_NE(off_rows, bars_rows) << "OFF draws no readout at all";

    // The shifter toggle swaps the special's name for its alternate, and a
    // drained pool recolours the line: both must reach the panel.
    v->prefs[PREF_LIFE] = PREF_LIFE_BARS;
    s->clearbuffer();
    new_score_panel(s, 1);
    const auto plain_special = capture_rendered_frame(*s);
    controlp->set_shifter_down(1);
    controlp->stats()->set_magicpoints(0);
    s->clearbuffer();
    new_score_panel(s, 1);
    EXPECT_NE(plain_special, capture_rendered_frame(*s))
        << "holding shifter with an empty pool must change what the panel shows";
    controlp->set_shifter_down(0);
    controlp->stats()->set_magicpoints(33);

    // Finally the score itself. Hold PREF_LIFE fixed and change ONLY the score:
    // the SC row must move and the life rows must NOT.
    v->prefs[PREF_LIFE] = PREF_LIFE_BARS;
    s->clearbuffer();
    new_score_panel(s, 1);
    const auto zero_score_frame = capture_rendered_frame(*s);

    score = 1'000'000u;
    s->clearbuffer();
    new_score_panel(s, 1);
    const auto counting_score_frame = capture_rendered_frame(*s);

    bool score_row_changed = false;
    for (int y = v->endy - 8; y < v->endy; ++y)
        for (int x = v->xloc; x < v->xloc + 70; ++x)
            score_row_changed = score_row_changed ||
                zero_score_frame[static_cast<std::size_t>(y * 320 + x)] !=
                    counting_score_frame[static_cast<std::size_t>(y * 320 + x)];
    EXPECT_TRUE(score_row_changed)
        << "raising the team score must advance the visible SC count";
    EXPECT_EQ(life_rows(zero_score_frame), life_rows(counting_score_frame))
        << "the score count-up must not disturb the HP/MP readout";

    // Settle the count-up back down for whatever runs next.
    score = 0;
    s->clearbuffer();
    new_score_panel(s, 1);

    v->control = control_pointer_is_live(og::runtime::current_session->myscreen_->level_runtime_data(), old_control) ? old_control : nullptr;
}

TEST_F(GladHud, quarter_screen_special_is_always_visible)
{
    screen* const s = og::runtime::current_session->myscreen_;

    struct ViewSetRestore
    {
        screen* scr;
        short numviews;
        std::array<std::unique_ptr<viewscreen>, MAX_VIEWS> views;

        explicit ViewSetRestore(screen* screen_ptr)
            : scr(screen_ptr), numviews(screen_ptr->numviews)
        {
            for (int i = 0; i < MAX_VIEWS; ++i)
                views[static_cast<std::size_t>(i)] = std::move(scr->viewob[i]);
        }

        ~ViewSetRestore()
        {
            for (int i = 0; i < MAX_VIEWS; ++i)
                scr->viewob[i].reset();
            for (int i = 0; i < MAX_VIEWS; ++i)
                scr->viewob[i] = std::move(views[static_cast<std::size_t>(i)]);
            scr->numviews = numviews;
            scr->relayout_views();
        }
    } restore_views{s};

    s->numviews = 4;
    s->initialize_views();

    std::array<std::unique_ptr<walker>, 4> controls;
    constexpr std::array<unsigned char, 4> kTeams = {0, 16, 17, 1};
    constexpr std::array<char, 4> kScorePrefs = {
        PREF_SCORE_OFF, PREF_SCORE_ON, PREF_SCORE_OFF, PREF_SCORE_ON};
    constexpr std::array<char, 4> kOverlayPrefs = {
        PREF_OVERLAY_ON, PREF_OVERLAY_ON, PREF_OVERLAY_OFF, PREF_OVERLAY_OFF};
    for (int i = 0; i < 4; ++i)
    {
        const std::size_t seat = static_cast<std::size_t>(i);
        controls[seat] = make_player(kTeams[seat]);
        ASSERT_NE(nullptr, controls[seat]);
        walker* const control = controls[seat].get();
        control->set_user(static_cast<signed char>(i));
        control->set_current_special(1); // Soldier CHARGE
        control->stats()->set_magicpoints(1000.0f);

        viewscreen* const view = s->viewob[i].get();
        ASSERT_NE(nullptr, view);
        view->control = control;
        view->prefs[PREF_OVERLAY] = kOverlayPrefs[seat];
        view->prefs[PREF_LIFE] = PREF_LIFE_OFF;
        view->prefs[PREF_SCORE] = kScorePrefs[seat];
        view->prefs[PREF_FOES] = PREF_FOES_OFF;
    }

    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto actual = capture_rendered_frame(*s);

    s->clearbuffer();
    for (int i = 0; i < 4; ++i)
    {
        const viewscreen* const view = s->viewob[i].get();
        if (kOverlayPrefs[static_cast<std::size_t>(i)] == PREF_OVERLAY_ON)
        {
            const int box_top =
                view->endy - (kScorePrefs[static_cast<std::size_t>(i)] == PREF_SCORE_ON
                                  ? 26
                                  : 10);
            s->draw_button(view->xloc + 1, box_top,
                           view->xloc + 98, view->endy - 2, 1, 1);
        }
        const unsigned char text_color =
            kOverlayPrefs[static_cast<std::size_t>(i)] == PREF_OVERLAY_ON
                ? static_cast<unsigned char>(DARK_BLUE)
                : static_cast<unsigned char>(YELLOW);
        s->text_normal.write_xy(view->xloc + 2, view->endy - 8,
                                "SPC: CHARGE",
                                text_color,
                                static_cast<short>(1));
    }
    const auto expected = capture_rendered_frame(*s);

    for (int i = 0; i < 4; ++i)
    {
        const viewscreen* const view = s->viewob[i].get();
        std::vector<unsigned char> actual_row;
        std::vector<unsigned char> expected_row;
        for (int y = view->endy - 28; y < view->endy; ++y)
        {
            for (int x = view->xloc; x < view->xloc + 100; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y * 320 + x);
                actual_row.push_back(actual[offset]);
                expected_row.push_back(expected[offset]);
            }
        }
        EXPECT_EQ(expected_row, actual_row)
            << "seat " << i << " must show its exact current-special row"
            << " (team=" << static_cast<int>(kTeams[static_cast<std::size_t>(i)])
            << ", score_pref="
            << static_cast<int>(kScorePrefs[static_cast<std::size_t>(i)])
            << ", overlay_pref="
            << static_cast<int>(kOverlayPrefs[static_cast<std::size_t>(i)])
            << ")";
    }
}

// Regression: a network client renders a single local viewport (players == 0),
// but its controlled walker carries the server-global player slot in user()
// (set by GameServer::bind_player and shipped to the client in the snapshot).
// new_score_panel used to gate the HUD on `control->user() == players`, which
// only holds for local split-screen (where game.cpp claims view->control with
// set_user(view_idx)). For any non-host client (slot 1/2/3) that check was
// false, so the HUD silently vanished. The HUD must draw whenever the view's
// control is a live, human-claimed walker, regardless of its global slot.
TEST_F(GladHud, score_panel_draws_for_network_client_with_nonlocal_user_slot)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    // Simulate this client being server-assigned player slot 2 while it renders
    // its own walker in local viewport index 0 (numviews == 1 on a client).
    controlp->set_user(2);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);

    walker* old_control = v->control;
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;  // draws the TEAM/FOES button box
    v->prefs[PREF_LIFE]    = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE]   = PREF_SCORE_OFF;   // score count-up uses rng(); keep off
    v->prefs[PREF_FOES]    = PREF_FOES_ON;

    // The TEAM/FOES counter box lives in the top-right corner: new_score_panel
    // draws it at x in [endx-57, endx-2], y in [tm+1, tm+16].
    const int rm = v->endx;
    const int kX0 = rm - 57;
    const int kX1 = rm - 2;
    constexpr int kY0 = 1;
    constexpr int kY1 = 16;
    auto box_has_pixels = [&](const std::array<unsigned char, 64000>& frame) {
        for (int y = kY0; y < kY1; ++y)
            for (int x = kX0; x < kX1; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };

    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(box_has_pixels(capture_rendered_frame(*s)))
        << "HUD must render for a client whose control->user() (server slot 2) "
           "differs from the local viewport index 0";

    // Negative control: a genuinely uncontrolled walker (user() == -1, e.g. a
    // spectator camera target) must still leave the HUD off.
    controlp->set_user(-1);
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s)))
        << "an uncontrolled (user() == -1) view must not paint the HUD";

    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

// §2.8 follow caption + §4.5 [NET-R6]: a follow-engaged view draws the
// bottom-center "FOLLOWING <name>" strip. Watching an AI target (user() ==
// -1) renders the caption ALONE — the HUD box stays dark, because the follow
// camera never stamps user tags and the HUD gates on them; a followed
// foreign hero keeps its owner's snapshot-synced HUD alongside the caption.
// With following_ off nothing new renders (both polarities pinned).
TEST_F(GladHud, follow_caption_draws_and_ai_target_keeps_hud_dark)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    controlp->set_user(-1); // AI-shaped follow target
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);

    walker* old_control = v->control;
    const bool old_following = v->following_;
    const std::string old_company = v->follow_company_;
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    // The TEAM/FOES counter box (the HUD's most robust probe region).
    const int rm = v->endx;
    const int kBoxX0 = rm - 57;
    const int kBoxX1 = rm - 2;
    constexpr int kBoxY0 = 1;
    constexpr int kBoxY1 = 16;
    auto box_has_pixels = [&](const std::array<unsigned char, 64000>& frame) {
        for (int y = kBoxY0; y < kBoxY1; ++y)
            for (int x = kBoxX0; x < kBoxX1; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };
    // The caption strip band: score_panel draws the yellow FOLLOWING text at
    // cy = bm - 12 (strip cy-2..cy+8), bottom-center of the viewport.
    // bm == endy in this build (score_panel's OVERSCAN_PADDING is 0 without
    // REDUCE_OVERSCAN).
    const int band_y0 = v->endy - 14;
    const int band_y1 = v->endy - 2;
    auto caption_has_pixels = [&](const std::array<unsigned char, 64000>& frame) {
        for (int y = band_y0; y < band_y1; ++y)
            for (int x = v->xloc; x < v->endx; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };

    // Not following: no caption band, no HUD (user() == -1).
    v->following_ = false;
    v->follow_company_.clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    {
        const auto frame = capture_rendered_frame(*s);
        EXPECT_FALSE(caption_has_pixels(frame))
            << "a non-following view must not paint the caption strip";
        EXPECT_FALSE(box_has_pixels(frame));
    }

    // Following an AI target: the caption alone; the HUD stays dark.
    v->following_ = true;
    v->follow_company_ = "Wolfpack";
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    {
        const auto frame = capture_rendered_frame(*s);
        EXPECT_TRUE(caption_has_pixels(frame))
            << "the FOLLOWING strip must render for an engaged view";
        EXPECT_FALSE(box_has_pixels(frame))
            << "[NET-R6] an AI follow target (user() == -1) must not paint "
               "the HUD";
    }

    // Following a foreign hero (its owner's snapshot-synced tag): the
    // caption AND the owner's HUD.
    controlp->set_user(2);
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    {
        const auto frame = capture_rendered_frame(*s);
        EXPECT_TRUE(caption_has_pixels(frame));
        EXPECT_TRUE(box_has_pixels(frame))
            << "a followed foreign hero shows its owner's HUD";
    }

    v->following_ = old_following;
    v->follow_company_ = old_company;
    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

TEST_F(GladHud, score_panel_sanitizes_mirrored_identity_and_special_metadata)
{
    screen* const s = og::runtime::current_session->myscreen_;
    viewscreen* const v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    auto control = make_player(0);
    ASSERT_NE(nullptr, control);
    walker* const controlp = control.get();
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(80);
    controlp->stats()->set_max_magicpoints(80);

    walker* const old_control = v->control;
    const bool old_following = v->following_;
    const std::string old_company = v->follow_company_;
    const char old_overlay = v->prefs[PREF_OVERLAY];
    const char old_life = v->prefs[PREF_LIFE];
    const char old_score = v->prefs[PREF_SCORE];
    const char old_foes = v->prefs[PREF_FOES];
    struct ViewRestore
    {
        screen* scr;
        viewscreen* view;
        walker* control;
        bool following;
        std::string company;
        char overlay;
        char life;
        char score;
        char foes;
        ~ViewRestore()
        {
            view->following_ = following;
            view->follow_company_ = std::move(company);
            view->prefs[PREF_OVERLAY] = overlay;
            view->prefs[PREF_LIFE] = life;
            view->prefs[PREF_SCORE] = score;
            view->prefs[PREF_FOES] = foes;
            view->control = control_pointer_is_live(
                scr->level_runtime_data(), control) ? control : nullptr;
        }
    } restore{s, v, old_control, old_following, old_company,
              old_overlay, old_life, old_score, old_foes};

    v->control = controlp;
    v->following_ = true;
    v->follow_company_.clear();
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    v->prefs[PREF_FOES] = PREF_FOES_OFF;

    // Network mirrors can have no guy object.  Prefer their replicated stats
    // name and cap it to the caption's documented 12-character field.
    ASSERT_NE(nullptr, controlp->myguy);
    controlp->myguy->name.clear();
    controlp->stats()->name = "TwelveChars-then-more";
    controlp->set_user(-1);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto named_frame = capture_rendered_frame(*s);
    int caption_pixels = 0;
    int caption_min_x = 320;
    int caption_max_x = -1;
    const unsigned char yellow = canonical_palette_index(YELLOW);
    for (int y = v->endy - 14; y < v->endy - 2; ++y)
    {
        for (int x = v->xloc; x < v->endx; ++x)
        {
            if (named_frame[static_cast<std::size_t>(y * 320 + x)] != yellow)
                continue;
            ++caption_pixels;
            caption_min_x = std::min(caption_min_x, x);
            caption_max_x = std::max(caption_max_x, x);
        }
    }
    ASSERT_GT(caption_pixels, 0);
    EXPECT_LE(caption_max_x - caption_min_x + 1, 22 * 6)
        << "FOLLOWING plus a capped 12-character name must fit its field";

    // With neither guy nor stats name, fall back to the family label.  A raw
    // signed-byte family from a hostile snapshot is clamped before indexing.
    controlp->clear_myguy();
    controlp->stats()->name.clear();
    controlp->set_family(127);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    EXPECT_NE(named_frame, capture_rendered_frame(*s));

    // The live HUD applies the same bounds policy to family and special, and
    // uses replicated stats for both name and level when no guy exists.
    controlp->stats()->name = "Mirror Hero";
    controlp->set_current_special(127);
    controlp->set_user(0);
    controlp->set_team_num(0);
    v->following_ = false;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto mirrored_frame = capture_rendered_frame(*s);
    bool mirrored_name_visible = false;
    for (int y = v->yloc + 3; y < v->yloc + 12; ++y)
        for (int x = v->xloc + 2; x < v->xloc + 75; ++x)
            mirrored_name_visible = mirrored_name_visible ||
                mirrored_frame[static_cast<std::size_t>(y * 320 + x)] != 0;
    EXPECT_TRUE(mirrored_name_visible);

    // Invalid team bytes never index the fixed four-team score array; the HUD
    // renders the neutral SC: 0 fallback instead.
    controlp->set_team_num(255);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto invalid_team_frame = capture_rendered_frame(*s);
    bool invalid_score_visible = false;
    for (int y = v->endy - 8; y < v->endy; ++y)
        for (int x = v->xloc; x < v->xloc + 45; ++x)
            invalid_score_visible = invalid_score_visible ||
                invalid_team_frame[static_cast<std::size_t>(y * 320 + x)] != 0;
    EXPECT_TRUE(invalid_score_visible);

    // Find a real alternate-special label and verify holding Shifter changes
    // the rendered special row through the normal HUD path.
    int alternate_family = -1;
    int alternate_special = -1;
    for (int family = 0; family < NUM_FAMILIES && alternate_family < 0; ++family)
    {
        for (int special = 0; special < NUM_SPECIALS; ++special)
        {
            if (s->alternate_name[family][special] != "NONE" &&
                s->alternate_name[family][special] !=
                    s->special_name[family][special])
            {
                alternate_family = family;
                alternate_special = special;
                break;
            }
        }
    }
    ASSERT_GE(alternate_family, 0);
    controlp->set_family(static_cast<char>(alternate_family));
    controlp->set_current_special(static_cast<char>(alternate_special));
    controlp->set_team_num(0);
    controlp->set_shifter_down(0);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto normal_special = capture_rendered_frame(*s);
    controlp->set_shifter_down(1);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto alternate_special_frame = capture_rendered_frame(*s);
    bool special_row_changed = false;
    for (int y = v->endy - 24; y < v->endy - 16; ++y)
        for (int x = v->xloc; x < v->xloc + 100; ++x)
            special_row_changed = special_row_changed ||
                normal_special[static_cast<std::size_t>(y * 320 + x)] !=
                    alternate_special_frame[static_cast<std::size_t>(y * 320 + x)];
    EXPECT_TRUE(special_row_changed);
}

TEST_F(GladHud, fps_overlay_draws_when_enabled)
{
    struct ShowFpsGuard {
        ~ShowFpsGuard() { og::runtime::current_session->show_fps_ = false; }
    } guard;

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr) << "control should be created";
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
    // Keep the base HUD off in the top-right strip the overlay scans.
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE]    = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE]   = PREF_SCORE_OFF;
    v->prefs[PREF_FOES]    = PREF_FOES_OFF;

    // The overlay now renders one row below the TEAM/FOES counter box
    // (which spans y in [1,16]), so scan the band just beneath it.
    constexpr int kY0 = 16;
    constexpr int kY1 = 30;
    constexpr int kX0 = 280;
    constexpr int kX1 = 320;

    auto scan_rect_nonzero = [](const std::array<unsigned char, 64000>& frame) {
        for (int y = kY0; y < kY1; ++y)
            for (int x = kX0; x < kX1; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };

    og::runtime::current_session->show_fps_ = true;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    auto frame_on = capture_rendered_frame(*s);
    ASSERT_TRUE(scan_rect_nonzero(frame_on))
        << "expected FPS overlay pixels in y[" << kY0 << "," << kY1
        << ") x[" << kX0 << "," << kX1 << ")";

    og::runtime::current_session->show_fps_ = false;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    auto frame_off = capture_rendered_frame(*s);
    ASSERT_FALSE(scan_rect_nonzero(frame_off))
        << "rectangle must be clean when show_fps_ is false";

    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

TEST_F(GladHud, fps_overlay_clears_foes_counter)
{
    // Regression: the FPS overlay used to render at y=2, flush right, directly
    // on top of the TEAM/FOES counter that new_score_panel draws in the
    // top-right corner (box spanning y in [1,16]). It must now render strictly
    // below that box. We diff a FOES-on frame with vs. without the overlay;
    // every pixel the overlay touches must lie below the counter box.
    struct ShowFpsGuard {
        ~ShowFpsGuard() { og::runtime::current_session->show_fps_ = false; }
    } guard;

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr) << "control should be created";
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
    // Draw the TEAM/FOES counter (and its button background) in the top-right.
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_LIFE]    = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE]   = PREF_SCORE_OFF;  // score countup uses rng(); keep off for determinism
    v->prefs[PREF_FOES]    = PREF_FOES_ON;

    // The counter box bottom edge: new_score_panel draws it at tm+16 with
    // tm == yloc (0 for the top viewport) plus zero overscan in the default build.
    constexpr int kFoesBoxBottom = 16;

    og::runtime::current_session->show_fps_ = false;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    auto frame_without = capture_rendered_frame(*s);

    og::runtime::current_session->show_fps_ = true;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    auto frame_with = capture_rendered_frame(*s);

    bool overlay_drew = false;
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * 320 + x);
            if (frame_with[i] == frame_without[i])
                continue;
            overlay_drew = true;
            ASSERT_GT(y, kFoesBoxBottom)
                << "FPS overlay pixel at (" << x << "," << y
                << ") overlaps the TEAM/FOES counter box (y in [1,"
                << kFoesBoxBottom << "])";
        }
    }
    ASSERT_TRUE(overlay_drew) << "FPS overlay should have rendered some pixels";

    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

TEST_F(GladHud, RedrawmeFlickerNoUnpaintedPresent)
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

TEST_F(GladHud, render_pending_redraw_presents_hud_overlay_in_single_frame)
{
    class SpyScreen final : public screen
    {
    public:
        SpyScreen(GameWorld& world, std::unique_ptr<video> video_impl)
            : screen(world, std::move(video_impl), 1, false)
        {
        }

        void buffer_to_screen(Sint32 viewstartx,
                              Sint32 viewstarty,
                              Sint32 viewwidth,
                              Sint32 viewheight) override
        {
            ++buffer_to_screen_calls;
            last_viewstartx = viewstartx;
            last_viewstarty = viewstarty;
            last_viewwidth = viewwidth;
            last_viewheight = viewheight;
            presented_frame = capture_rendered_frame(*this);
            presented_frame_captured = true;
        }

        int buffer_to_screen_calls = 0;
        Sint32 last_viewstartx = -1;
        Sint32 last_viewstarty = -1;
        Sint32 last_viewwidth = -1;
        Sint32 last_viewheight = -1;
        bool presented_frame_captured = false;
        std::array<unsigned char, 64000> presented_frame{};
    };

    struct ScreenRestoreGuard
    {
        screen*& current_screen;
        screen* saved_screen;

        ~ScreenRestoreGuard()
        {
            current_screen = saved_screen;
        }
    };

    screen*& session_screen = og::runtime::current_session->myscreen_;
    ScreenRestoreGuard restore_guard{session_screen, session_screen};
    GameWorld world;
    {
        SpyScreen spy_screen(world, std::make_unique<sdl_video>(false));
        spy_screen.set_world_canvas_pinned_classic(true);
        spy_screen.relayout_views();
        spy_screen.set_active_canvas(CanvasTarget::World);
        viewscreen* const view = spy_screen.viewob[0].get();
        ASSERT_TRUE(view != nullptr);

        view->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
        view->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
        view->prefs[PREF_SCORE] = PREF_SCORE_OFF;
        view->prefs[PREF_FOES] = PREF_FOES_OFF;

        auto control = make_player(0);
        ASSERT_TRUE(control != nullptr);
        walker* const controlp = control.get();
        controlp->set_user(0);
        controlp->set_team_num(0);
        controlp->set_dead(0);
        controlp->stats()->set_level(7);
        controlp->stats()->set_hitpoints(55);
        controlp->stats()->set_max_hitpoints(100);
        controlp->stats()->set_magicpoints(33);
        controlp->stats()->set_max_magicpoints(80);
        view->control = controlp;
        spy_screen.world().oblist.push_back(std::move(control));

        ASSERT_EQ(0, spy_screen.buffer_to_screen_calls);

        spy_screen.draw_panels(1);
        const auto overlayless_frame = capture_rendered_frame(spy_screen);

        spy_screen.redrawme = 1;
        og::runtime::detail::render_pending_redraw(spy_screen, true);

        ASSERT_EQ(1, spy_screen.buffer_to_screen_calls);
        EXPECT_TRUE(spy_screen.presented_frame_captured);
        EXPECT_EQ(0, spy_screen.last_viewstartx);
        EXPECT_EQ(0, spy_screen.last_viewstarty);
        EXPECT_EQ(320, spy_screen.last_viewwidth);
        EXPECT_EQ(200, spy_screen.last_viewheight);
        EXPECT_EQ(0, spy_screen.redrawme);
        EXPECT_NE(overlayless_frame, spy_screen.presented_frame);

        spy_screen.draw_panels(1);
        score_panel(&spy_screen, 1);
        EXPECT_EQ(capture_rendered_frame(spy_screen), spy_screen.presented_frame);

        // A frame with nothing to repaint costs nothing: no HUD repaint and,
        // above all, no second present (the flicker this path exists to avoid
        // comes from presenting more than once around the overlay).
        const auto settled_frame = capture_rendered_frame(spy_screen);
        ASSERT_EQ(0, spy_screen.redrawme);
        og::runtime::detail::render_pending_redraw(spy_screen, true);
        EXPECT_EQ(1, spy_screen.buffer_to_screen_calls)
            << "a clean frame must not present";
        EXPECT_EQ(settled_frame, capture_rendered_frame(spy_screen))
            << "a clean frame must not repaint the HUD";
    }
}


// ---------------------------------------------------------------------------
// B5: pending-wave HUD (dormant delayed-spawn hostiles)
// ---------------------------------------------------------------------------

namespace {
// Isolate the oblist so prior game state doesn't affect counts.
struct HudObListSwap {
    std::list<std::unique_ptr<walker>> saved;
    HudObListSwap()
    {
        og::runtime::current_session->myscreen_->world().oblist.splice_into(saved);
    }
    ~HudObListSwap()
    {
        og::runtime::current_session->myscreen_->world().oblist.clear();
        og::runtime::current_session->myscreen_->world().oblist.splice(
            og::runtime::current_session->myscreen_->world().oblist.end(), saved);
    }
};

// The frozen-time probes below drive shared display state (the world's freeze
// counter, one view's HUD prefs, the split-screen view count). Restore it from
// destructors so a failing ASSERT cannot leak any of it into the next test.
struct FreezeGuard
{
    GameWorld& world;
    std::int32_t saved;
    explicit FreezeGuard(GameWorld& w) : world(w), saved(w.enemy_freeze) {}
    ~FreezeGuard() { world.enemy_freeze = saved; }
};

struct ViewHudStateGuard
{
    viewscreen& view;
    walker* control;
    signed char score_pref;
    signed char view_pref;
    explicit ViewHudStateGuard(viewscreen& v)
        : view(v)
        , control(v.control)
        , score_pref(v.prefs[PREF_SCORE])
        , view_pref(v.prefs[PREF_VIEW])
    {
    }
    ~ViewHudStateGuard()
    {
        view.control = control;
        view.prefs[PREF_SCORE] = score_pref;
        view.prefs[PREF_VIEW] = view_pref;
        view.resize(static_cast<char>(view_pref));
    }
};

// ready_for_battle rebuilds the view objects, so this guard must outlive
// every reference to them.
struct ViewCountGuard
{
    screen& game;
    short saved;
    explicit ViewCountGuard(screen& s) : game(s), saved(s.numviews) {}
    ~ViewCountGuard()
    {
        if (game.numviews == saved)
            return;
        game.ready_for_battle(saved);
        game.relayout_views();
    }
};
} // namespace

TEST_F(GladHud, pending_hostile_wave_counts_splits_dormant_and_counts_down)
{
    HudObListSwap swap;
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const std::uint32_t saved_ltc = world.level_tick_count();

    auto control = make_player(0);
    auto ally = make_living(FAMILY_ELF, 0);
    auto awake_foe = make_living(FAMILY_ORC, 1);
    auto wave1 = make_living(FAMILY_ORC, 1);
    auto wave2 = make_living(FAMILY_SKELETON, 1);
    ASSERT_TRUE(control && ally && awake_foe && wave1 && wave2);

    // A dormant ALLY must not count as a pending hostile.
    ally->set_spawn_delay(200);
    ally->set_dormant(true);
    wave1->set_spawn_delay(100);
    wave1->set_dormant(true);
    wave2->set_spawn_delay(60);
    wave2->set_dormant(true);

    walker* const controlp = control.get();
    walker* const wave2p = wave2.get();
    world.oblist.push_back(std::move(control));
    world.oblist.push_back(std::move(ally));
    world.oblist.push_back(std::move(awake_foe));
    world.oblist.push_back(std::move(wave1));
    world.oblist.push_back(std::move(wave2));

    // Wake rule is level_tick_count > spawn_delay, i.e. wake tick is
    // spawn_delay + 1. At tick 12 the nearest wave (delay 60) is 49 away.
    world.set_level_tick_count(12);
    short pending = 0;
    short respawn = 0;
    std::uint32_t ticks = 0;
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(2, static_cast<int>(pending));
    EXPECT_EQ(0, static_cast<int>(respawn));
    EXPECT_EQ(49u, ticks);

    // Dead dormant hostiles are not pending; the min moves to delay 100.
    wave2p->set_dead(1);
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(1, static_cast<int>(pending));
    EXPECT_EQ(89u, ticks);

    // Past the wake tick already: the countdown clamps to zero.
    world.set_level_tick_count(500);
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(1, static_cast<int>(pending));
    EXPECT_EQ(0u, ticks);

    // Null viewer guard.
    pending_hostile_wave_counts(world, nullptr, pending, respawn, ticks);
    EXPECT_EQ(0, static_cast<int>(pending));
    EXPECT_EQ(0, static_cast<int>(respawn));
    EXPECT_EQ(0u, ticks);

    world.set_level_tick_count(saved_ltc);
}

// End-of-scenario honesty for the difficulty respawn modes: a hostile foe
// merely awaiting its respawn timer blocks the extermination win
// (classic_respawn_pending_hostile_foe), so it must show up in the (+x) and
// the countdown instead of leaving "FOES: 0" over a map that refuses to end.
TEST_F(GladHud, pending_hostile_wave_counts_includes_classic_respawn_queue)
{
    HudObListSwap swap;
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const std::uint32_t saved_ltc = world.level_tick_count();
    const short saved_mode = world.respawn_mode;
    const short saved_allied = world.allied_mode;
    const auto saved_queue = world.respawn.respawn_queue;

    auto control = make_player(0);
    auto dormant_foe = make_living(FAMILY_ORC, 1);
    ASSERT_TRUE(control && dormant_foe);
    dormant_foe->set_spawn_delay(100);
    dormant_foe->set_dormant(true);
    walker* const controlp = control.get();
    world.oblist.push_back(std::move(control));
    world.oblist.push_back(std::move(dormant_foe));

    world.set_level_tick_count(12);
    world.respawn_mode = 2;
    world.allied_mode = 1;
    world.respawn.respawn_queue.clear();

    og::sim::RespawnEntry ai_foe; // mode-2 AI replacement: hostile
    ai_foe.kind = 1;
    ai_foe.team = 1;
    ai_foe.ticks_left = 30;
    og::sim::RespawnEntry same_team; // the viewer's own team: never hostile
    same_team.kind = 1;
    same_team.team = 0;
    same_team.ticks_left = 5;
    og::sim::RespawnEntry hero; // different-color company hero: hostile
    hero.kind = 0;
    hero.team = 2;
    hero.ticks_left = 3;
    world.respawn.respawn_queue.push_back(ai_foe);
    world.respawn.respawn_queue.push_back(same_team);
    world.respawn.respawn_queue.push_back(hero);

    short pending = 0;
    short respawn = 0;
    std::uint32_t ticks = 0;
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(1, static_cast<int>(pending)) << "the dormant foe";
    EXPECT_EQ(2, static_cast<int>(respawn))
        << "both different-color entries are hostile";
    EXPECT_EQ(3u, ticks) << "the hostile hero has the shortest timer";

    // PVP seating mode cannot change the hostility result.
    world.allied_mode = 0;
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(2, static_cast<int>(respawn));
    EXPECT_EQ(3u, ticks);

    // Engine off: leftover queue entries are ignored entirely.
    world.respawn_mode = 0;
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(1, static_cast<int>(pending));
    EXPECT_EQ(0, static_cast<int>(respawn));
    EXPECT_EQ(89u, ticks);

    world.respawn.respawn_queue = saved_queue;
    world.respawn_mode = saved_mode;
    world.allied_mode = saved_allied;
    world.set_level_tick_count(saved_ltc);
}

TEST_F(GladHud, score_panel_shows_next_wave_countdown_for_dormant_hostiles)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();
    const std::uint32_t saved_ltc = world.level_tick_count();

    auto control = make_player(0);
    auto awake_foe = make_living(FAMILY_ORC, 1);
    auto wave = make_living(FAMILY_ORC, 1);
    ASSERT_TRUE(control && awake_foe && wave);
    wave->set_spawn_delay(60);
    wave->set_dormant(true);

    walker* const controlp = control.get();
    walker* const wavep = wave.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));
    world.oblist.push_back(std::move(awake_foe));
    world.oblist.push_back(std::move(wave));

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    world.set_level_tick_count(12);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "next_wave awake=1 pending=1 secs=5"))
        << "one awake foe, one pending wave, 49 ticks -> 5 s at 12 Hz";

    // Once the wave wakes there is nothing pending: classic FOES readout,
    // no NEXT WAVE line.
    wavep->set_dormant(false);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "next_wave"));

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    world.set_level_tick_count(saved_ltc);
}

// The endgame moment in respawn mode 2: the last live foe is dead, an AI
// replacement sits in the queue. The panel must show it as a pending wave
// (with the countdown) rather than a bare "FOES: 0" that never ends.
TEST_F(GladHud, score_panel_counts_respawn_pending_foe_in_wave)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();
    const short saved_mode = world.respawn_mode;
    const auto saved_queue = world.respawn.respawn_queue;

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));

    world.respawn_mode = 2;
    world.respawn.respawn_queue.clear();
    og::sim::RespawnEntry entry;
    entry.kind = 1;
    entry.team = 1;
    entry.ticks_left = 55; // (55 + 11) / 12 -> 5 s at 12 Hz
    world.respawn.respawn_queue.push_back(entry);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "next_wave awake=0 pending=1 secs=5"))
        << "a foe awaiting its respawn timer is a pending wave, not FOES: 0";

    // The end-of-level flush clears the queue (every end shape does): the
    // classic FOES readout returns with no NEXT WAVE line.
    world.respawn.respawn_queue.clear();
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "next_wave"));

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    world.respawn.respawn_queue = saved_queue;
    world.respawn_mode = saved_mode;
}

// ---------------------------------------------------------------------------
// Disabled-special signifier: specials_disabled greys the SPC line
// ---------------------------------------------------------------------------

TEST_F(GladHud, score_panel_greys_special_line_when_specials_disabled)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    // MP well above the special cost: without the disabled flag this line
    // would draw in the normal text color, never RED, never GREY.
    controlp->stats()->set_magicpoints(80);
    controlp->stats()->set_max_magicpoints(80);
    controlp->stats()->set_special_cost(
        static_cast<int>(controlp->current_special()), 10);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF; // no button box pixels
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;      // keep the legacy SC/XP rows on
    v->prefs[PREF_FOES] = PREF_FOES_OFF;

    // The SPC line draws at (lm+2, bm-24) = (2, 176) for the full pane.
    const int spc_y = v->endy - 24;
    auto count_color_in_spc_row = [&](const std::array<unsigned char, 64000>& frame,
                                      unsigned char color) {
        int n = 0;
        for (int y = spc_y; y < spc_y + 8; ++y)
            for (int x = 2; x < 100; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] == color)
                    ++n;
        return n;
    };

    const unsigned char grey = canonical_palette_index(GREY);
    const unsigned char yellow = canonical_palette_index(YELLOW);

    // Baseline: specials enabled, plenty of MP -> normal color, no grey.
    controlp->set_specials_disabled(false);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "spc_disabled"));
    const auto enabled_frame = capture_rendered_frame(*s);
    EXPECT_EQ(0, count_color_in_spc_row(enabled_frame, grey))
        << "an enabled special must not draw grey";
    EXPECT_GT(count_color_in_spc_row(enabled_frame, yellow), 0)
        << "an enabled, affordable special draws in the normal text color";

    // Disabled: the same line renders GREY regardless of the full MP pool.
    controlp->set_specials_disabled(true);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "spc_disabled"))
        << "the grey path must be taken for a specials-disabled walker";
    const auto disabled_frame = capture_rendered_frame(*s);
    EXPECT_GT(count_color_in_spc_row(disabled_frame, grey), 0)
        << "the SPC line must render in GREY";
    EXPECT_EQ(0, count_color_in_spc_row(disabled_frame, yellow))
        << "no normal-color SPC pixels may remain when disabled";

    controlp->set_specials_disabled(false);
    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
}

// ---------------------------------------------------------------------------
// SCARED countdown: a forced flee (front COMMAND_WALK) shows its seconds left
// ---------------------------------------------------------------------------

TEST_F(GladHud, hud_scared_flee_ticks_reads_front_walk_command_only)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();

    // Calm walker: no commands, no countdown.
    controlp->stats()->clear_command();
    EXPECT_EQ(0, hud_scared_flee_ticks(controlp));

    // A forced walk (the ghost-scare / yell-for-help shape) is the state.
    controlp->stats()->force_command(COMMAND_WALK, 25, 1, 0);
    EXPECT_EQ(25, hud_scared_flee_ticks(controlp));
    controlp->stats()->clear_command();

    // A non-walk front command is not fear.
    controlp->stats()->force_command(COMMAND_FIRE, 10, 0, 0);
    EXPECT_EQ(0, hud_scared_flee_ticks(controlp));
    controlp->stats()->clear_command();

    // Dead and null viewers are calm.
    controlp->stats()->force_command(COMMAND_WALK, 25, 1, 0);
    controlp->set_dead(1);
    EXPECT_EQ(0, hud_scared_flee_ticks(controlp));
    controlp->set_dead(0);
    controlp->stats()->clear_command();
    EXPECT_EQ(0, hud_scared_flee_ticks(nullptr));
}

TEST_F(GladHud, score_panel_shows_scared_countdown_while_fleeing)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

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

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_OFF;

    // The countdown draws at (lm+2, tm+28), just below the HP/MP rows.
    const unsigned char red = canonical_palette_index(RED);
    auto count_red_in_row = [&](const std::array<unsigned char, 64000>& frame) {
        int n = 0;
        for (int y = 28; y < 36; ++y)
            for (int x = 2; x < 90; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] == red)
                    ++n;
        return n;
    };

    // Scared: a ghost-scare-shaped forced walk with 25 sim ticks left reads
    // as ceil(25 / 12) = 3 seconds at the 12 Hz sim rate.
    controlp->stats()->force_command(COMMAND_WALK, 25, 1, 0);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "scared ticks=25 secs=3"))
        << "25 forced-walk ticks -> 3 s at 12 Hz";
    EXPECT_GT(count_red_in_row(capture_rendered_frame(*s)), 0)
        << "the SCARED line must paint red pixels below the status rows";

    // Calm again: the line disappears with the drained command queue.
    controlp->stats()->clear_command();
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "scared"))
        << "no countdown once the flee command is gone";
    EXPECT_EQ(0, count_red_in_row(capture_rendered_frame(*s)))
        << "the SCARED row must be clean when calm";

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
}

// ---------------------------------------------------------------------------
// B4: one-shot "the way is clear" notice on level_done 0 -> 1
// ---------------------------------------------------------------------------

TEST_F(GladHud, way_clear_notice_fires_once_per_level_and_rearms_on_new_level)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    GameWorld& world = s->world();
    const int saved_id = world.id;
    const std::uint32_t saved_tick = world.tick_count_;
    const short saved_done = world.level_done;

    for (short i = 0; i < s->numviews; i++)
        s->viewob[i]->clear_text();
    trace_clear();

    static const char* const kWayClearText = "The way is clear -- you may exit";
    const auto view_has_way_clear_text = [&]() {
        for (int slot = 0; slot < MAX_MESSAGES; ++slot)
            if (v->textlist[slot] == kWayClearText)
                return true;
        return false;
    };

    // Mid-level: hostiles alive (level_done == 0). No announcement.
    world.id = 4242;
    world.tick_count_ = 10;
    world.level_done = 0;
    ASSERT_TRUE(s->redraw());
    EXPECT_FALSE(trace_contains("hud", "way_clear"));
    EXPECT_FALSE(view_has_way_clear_text());

    // Foes cleared with a live exit: the sim flips level_done to 1.
    world.level_done = 1;
    world.tick_count_ = 11;
    ASSERT_TRUE(s->redraw());
    EXPECT_TRUE(trace_contains("hud", "way_clear"));
    EXPECT_TRUE(view_has_way_clear_text())
        << "the one-shot notice must be queued on the view";

    // One-shot: another 0 -> 1 swing on the SAME level must not re-fire.
    trace_clear();
    world.level_done = 0;
    world.tick_count_ = 12;
    ASSERT_TRUE(s->redraw());
    world.level_done = 1;
    world.tick_count_ = 13;
    ASSERT_TRUE(s->redraw());
    EXPECT_FALSE(trace_contains("hud", "way_clear"));

    // A new level id re-arms the latch.
    trace_clear();
    world.id = 4243;
    world.tick_count_ = 5;
    world.level_done = 0;
    ASSERT_TRUE(s->redraw());
    world.level_done = 1;
    world.tick_count_ = 6;
    ASSERT_TRUE(s->redraw());
    EXPECT_TRUE(trace_contains("hud", "way_clear"));

    for (short i = 0; i < s->numviews; i++)
        s->viewob[i]->clear_text();
    world.id = saved_id;
    world.tick_count_ = saved_tick;
    world.level_done = saved_done;
}

// ---------------------------------------------------------------------------
// floor_hud_label (feature B): the pure shared formatter (D5). Every HUD
// surface (SDL FOES-box row, curses status line) draws exactly this string,
// so the format pins live here once.

TEST(FloorHudLabel, empty_on_single_floor)
{
    GameWorld w(0);
    ASSERT_EQ(1, w.floor_count());
    EXPECT_EQ("", floor_hud_label(w, 0));
}

TEST(FloorHudLabel, multifloor_is_one_indexed)
{
    GameWorld w(0);
    w.set_floor_count(3);
    EXPECT_EQ("FLR: 1/3", floor_hud_label(w, 0))
        << "floor 0 reads as 1/3 (players count from one)";
    EXPECT_EQ("FLR: 2/3", floor_hud_label(w, 1));
    EXPECT_EQ("FLR: 3/3", floor_hud_label(w, 2));
}

TEST(FloorHudLabel, clamps_out_of_range_walker_floor)
{
    // Mirror int8 safety: walker_floor is attacker-controllable on a network
    // mirror, so the label clamps instead of printing garbage.
    GameWorld w(0);
    w.set_floor_count(3);
    EXPECT_EQ("FLR: 1/3", floor_hud_label(w, -5));
    EXPECT_EQ("FLR: 3/3", floor_hud_label(w, 99));
}

TEST(FloorHudLabel, tower_single_story_shows_absolute_floor)
{
    GameWorld w(0);
    w.type = SCEN_TYPE_TOWER;
    w.id = 723; // Tower floor 23 (id = kTowerGateLevel + N)
    ASSERT_EQ(1, w.floor_count());
    EXPECT_EQ("FLR: 23", floor_hud_label(w, 0));
}

TEST(FloorHudLabel, tower_multi_story_combines_floor_and_story)
{
    GameWorld w(0);
    w.type = SCEN_TYPE_TOWER;
    w.id = 723;
    w.set_floor_count(3);
    EXPECT_EQ("F23: 2/3", floor_hud_label(w, 1))
        << "8 chars for 2-digit floors -- fits the classic 8-glyph box";
}

TEST(FloorHudLabel, tower_gate_is_unlabeled)
{
    // The Gate (id == kTowerGateLevel) is floor 0 of the climb, not a
    // numbered floor: it falls through to the non-tower branch.
    GameWorld w(0);
    w.type = SCEN_TYPE_TOWER;
    w.id = og::kTowerGateLevel;
    ASSERT_EQ(1, w.floor_count());
    EXPECT_EQ("", floor_hud_label(w, 0));
}

// ---------------------------------------------------------------------------
// The FLR row in the PREF_FOES box (feature B, SDL surface).

TEST_F(GladHud, score_panel_shows_floor_indicator_on_multifloor)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));

    world.set_floor_count(3);
    controlp->set_floor(1);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "FLR: 2/3"))
        << "the shared label for floor 1 of 3 reaches the panel";

    // The box grows one row (bottom edge 16 -> 24) and the FLR text renders
    // in the new last row: pixels must appear in the extension band.
    const auto frame = capture_rendered_frame(*s);
    // Zero overscan in the default build: tm == yloc == 0, rm == 320.
    const int tm = 0;
    const int rm = 320;
    bool extension_has_pixels = false;
    for (int y = tm + 17; y <= tm + 24 && !extension_has_pixels; ++y)
        for (int x = rm - 57; x <= rm - 2; ++x)
            if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
            {
                extension_has_pixels = true;
                break;
            }
    EXPECT_TRUE(extension_has_pixels)
        << "FLR row (box extension y[" << tm + 17 << "," << tm + 24
        << "]) should have drawn";

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
    controlp->set_floor(0);
    world.set_floor_count(1);
}

TEST_F(GladHud, score_panel_floor_row_absent_single_floor)
{
    // Byte-identity witness: on a single-floor level the label is empty, the
    // box keeps its classic 16px height, and the extension band stays clean.
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();
    ASSERT_EQ(1, world.floor_count()) << "fixture must be single-floor";

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "floor "))
        << "no floor row trace on single-floor levels";

    const auto frame = capture_rendered_frame(*s);
    // Zero overscan in the default build: tm == yloc == 0, rm == 320.
    const int tm = 0;
    const int rm = 320;
    for (int y = tm + 17; y <= tm + 24; ++y)
        for (int x = rm - 57; x <= rm - 2; ++x)
            ASSERT_EQ(0, static_cast<int>(
                          frame[static_cast<std::size_t>(y * 320 + x)]))
                << "pixel at (" << x << "," << y
                << ") must stay clean: single-floor frames are byte-identical";

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
}

// The TEAM/FOES counter box is anchored to the TOP of its pane: TEAM at
// (rm-55, tm+2), FOES at (rm-55, tm+10), inside the box tm+1..tm+16. The
// 2013 touch build drew both rows 52 px lower (`tm+2 + 44 + 8`) behind a
// retired touch-build `#ifndef` fork that no configured build ever selected;
// the fork is gone (PR #292), so the surviving arm is the only geometry and
// the band 52 px below it must stay empty.
TEST_F(GladHud, team_row_sits_at_the_pane_top)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    ASSERT_EQ(1, static_cast<int>(s->numviews))
        << "the pane geometry below assumes the single-view layout";

    HudObListSwap swap;
    GameWorld& world = s->world();
    ASSERT_EQ(1, world.floor_count()) << "no FLR row: the box keeps its 16px height";

    auto control = make_player(0);
    ASSERT_NE(nullptr, control);
    walker* const controlp = control.get();
    world.oblist.push_back(std::move(control));
    auto foe = make_living(FAMILY_ORC, 1);
    ASSERT_NE(nullptr, foe);
    world.oblist.push_back(std::move(foe));

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    struct PrefRestore {
        viewscreen* view; walker* control;
        char life, score, foes, overlay;
        ~PrefRestore()
        {
            view->control = control;
            view->prefs[PREF_LIFE] = life;
            view->prefs[PREF_SCORE] = score;
            view->prefs[PREF_FOES] = foes;
            view->prefs[PREF_OVERLAY] = overlay;
        }
    } restore{v, old_control, old_pref_life, old_pref_score, old_pref_foes,
              old_pref_overlay};

    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // the count-up consumes rng()
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    // Zero overscan in the default build, so these are the viewport's own
    // edges -- the same lm/tm/rm/bm score_panel computes.
    const int tm = v->yloc;
    const int rm = v->endx;
    // PREF_OVERLAY_ON draws the box, and the box colour is what picks the
    // text colour (score_panel.cpp: draw_button ? DARK_BLUE : YELLOW).
    const unsigned char text_color = static_cast<unsigned char>(DARK_BLUE);

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    ASSERT_FALSE(trace_contains("hud", "next_wave"))
        << "no pending wave: the box must keep its two-row height";
    const auto actual = capture_rendered_frame(*s);

    // Redraw the counter box by hand at the surviving arm's coordinates and
    // demand the pixels match exactly: position, colour and text all pinned.
    const std::string team_text =
        "TEAM: " + std::to_string(
            static_cast<int>(remaining_team(s, static_cast<char>(0))));
    const std::string foes_text =
        "FOES: " + std::to_string(
            static_cast<int>(remaining_foes(s, controlp)));
    ASSERT_EQ("TEAM: 1", team_text) << "one living team-0 walker: the control";
    ASSERT_EQ("FOES: 1", foes_text) << "one living hostile walker";

    s->clearbuffer();
    s->draw_button(rm - 57, tm + 1, rm - 2, tm + 16, 1, 1);
    s->text_normal.write_xy(rm - 55, tm + 2, team_text.c_str(), text_color,
                            static_cast<short>(1));
    s->text_normal.write_xy(rm - 55, tm + 10, foes_text.c_str(), text_color,
                            static_cast<short>(1));
    const auto expected = capture_rendered_frame(*s);

    std::vector<unsigned char> actual_box;
    std::vector<unsigned char> expected_box;
    for (int y = tm + 1; y <= tm + 16; ++y)
        for (int x = rm - 57; x <= rm - 2; ++x)
        {
            const std::size_t offset = static_cast<std::size_t>(y * 320 + x);
            actual_box.push_back(actual[offset]);
            expected_box.push_back(expected[offset]);
        }
    EXPECT_EQ(expected_box, actual_box)
        << "the counter box must render TEAM at (" << rm - 55 << ","
        << tm + 2 << ") and FOES at (" << rm - 55 << "," << tm + 10 << ")";

    // The retired touch arm's rows: TEAM at tm+2+44+8 spans y in [tm+54,
    // tm+60). Nothing draws there now, so any ink is the dead geometry
    // coming back.
    for (int y = tm + 54; y < tm + 60; ++y)
        for (int x = rm - 55; x < rm - 2; ++x)
            ASSERT_EQ(0, static_cast<int>(
                          actual[static_cast<std::size_t>(y * 320 + x)]))
                << "pixel at (" << x << "," << y << ") must stay clean: the"
                << " counter rows belong at the pane top, not 52px down";
}


TEST_F(GladHud, fps_overlay_clears_extended_foes_counter)
{
    // Clone of fps_overlay_clears_foes_counter on a 2-floor fixture: the FLR
    // row grows the counter box to tm+24, and the dynamically-placed FPS
    // overlay must move strictly below the taller box.
    struct ShowFpsGuard {
        ~ShowFpsGuard() { og::runtime::current_session->show_fps_ = false; }
    } guard;

    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));

    world.set_floor_count(2);
    controlp->set_floor(0);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    // Extended box bottom: 16 (classic) + 8 (FLR row) with no wave pending.
    const int kExtendedBoxBottom = 24; // zero overscan in the default build

    og::runtime::current_session->show_fps_ = false;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    const auto frame_without = capture_rendered_frame(*s);

    og::runtime::current_session->show_fps_ = true;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    const auto frame_with = capture_rendered_frame(*s);

    bool overlay_drew = false;
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * 320 + x);
            if (frame_with[i] == frame_without[i])
                continue;
            overlay_drew = true;
            ASSERT_GT(y, kExtendedBoxBottom)
                << "FPS overlay pixel at (" << x << "," << y
                << ") overlaps the extended counter box (bottom "
                << kExtendedBoxBottom << ")";
        }
    }
    ASSERT_TRUE(overlay_drew) << "FPS overlay should have rendered some pixels";

    og::runtime::current_session->show_fps_ = false;
    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
    world.set_floor_count(1);
}


// #232: the frozen-time countdown is HUD state read off world.enemy_freeze
// every frame, not a notification pushed into the message feed (where it
// used to evict every other line for the whole freeze). The cell is
// left-anchored at lm+4 on the bm-34 row — the band the classic HUD's
// top-left column and its bottom-left score box both leave free.
TEST_F(GladHud, freeze_countdown_cell_draws_from_enemy_freeze)
{
    HudObListSwap swap;
    screen* const s = og::runtime::current_session->myscreen_;
    viewscreen* const v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    GameWorld& world = s->world();
    FreezeGuard freeze_guard(world);
    ViewHudStateGuard view_guard(*v);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    world.oblist.push_back(std::move(control));
    v->control = controlp;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // the score count-up uses rng()

    // "TIME LEFT: 30" at 6px per glyph, on the 6px-tall text row at bm-34.
    const int cell_x0 = v->xloc + 2; // the classic HUD block's left column
    const int cell_x1 = cell_x0 + 13 * 6;
    const int cell_y0 = v->endy - 34;
    const int cell_y1 = cell_y0 + 6;
    const auto cell_has_pixels = [&](const std::array<unsigned char, 64000>& frame) {
        for (int y = cell_y0; y < cell_y1; ++y)
            for (int x = cell_x0; x < cell_x1; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };

    world.enemy_freeze = 0;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(cell_has_pixels(capture_rendered_frame(*s)))
        << "no countdown cell without an active freeze";

    world.enemy_freeze = 30;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(cell_has_pixels(capture_rendered_frame(*s)))
        << "an active freeze must draw the countdown cell";

    // A view with no HUD of its own (a spectator camera watching an AI body)
    // draws no cell either: it belongs to the classic HUD block.
    controlp->set_user(-1);
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(cell_has_pixels(capture_rendered_frame(*s)))
        << "the cell rides the classic HUD gate, not the bare viewport";
    controlp->set_user(0);
}

// The cell may never land on the notification banner, which owns
// viewport-local rows 30..59 in EVERY pane however short the pane is. The
// legacy PREF_VIEW inset modes are the short ones (1p mode 4 leaves 80
// scanlines, 2p/3p mode 4 leaves 72), and a quadrant pane is narrow enough
// for the string to reach the radar block in the bottom-right corner.
TEST_F(GladHud, freeze_countdown_cell_never_lands_on_the_notification_feed)
{
    HudObListSwap swap;
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s->viewob[0] != nullptr);

    GameWorld& world = s->world();
    FreezeGuard freeze_guard(world);
    ViewCountGuard view_count_guard(*s);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    world.oblist.push_back(std::move(control));

    // Counts every pixel the freeze adds to the frame and pins each of them
    // outside the feed band and the radar block of every live pane.
    int freeze_pixels = 0;
    const auto probe_freeze_pixels = [&](const char* what,
                                        std::int32_t freeze_left = 30) {
        freeze_pixels = 0;
        for (int i = 0; i < s->numviews; ++i)
        {
            ASSERT_TRUE(s->viewob[i] != nullptr) << what;
            s->viewob[i]->control = controlp;
            s->viewob[i]->prefs[PREF_SCORE] = PREF_SCORE_OFF;
        }

        world.enemy_freeze = 0;
        s->clearbuffer();
        ASSERT_EQ(1, (int)new_score_panel(s, 1)) << what;
        const auto without = capture_rendered_frame(*s);

        world.enemy_freeze = freeze_left;
        s->clearbuffer();
        ASSERT_EQ(1, (int)new_score_panel(s, 1)) << what;
        const auto with = capture_rendered_frame(*s);

        for (int y = 0; y < 200; ++y)
        {
            for (int x = 0; x < 320; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y * 320 + x);
                if (with[i] == without[i])
                    continue;
                ++freeze_pixels;
                for (int view = 0; view < s->numviews; ++view)
                {
                    const viewscreen& pane = *s->viewob[view];
                    const bool in_pane = x >= pane.xloc && x <= pane.endx &&
                                         y >= pane.yloc && y <= pane.endy;
                    if (!in_pane)
                        continue;
                    ASSERT_FALSE(y >= pane.yloc + 30 &&
                                 y < pane.yloc + 30 + MAX_MESSAGES * 6)
                        << what << ": freeze pixel at (" << x << "," << y
                        << ") is inside view " << view
                        << "'s notification feed band";
                    ASSERT_FALSE(x > pane.endx - 64 && y > pane.endy - 48)
                        << what << ": freeze pixel at (" << x << "," << y
                        << ") is inside view " << view << "'s radar block";
                }
            }
        }
    };

    // The HUD lays itself out from prefs[PREF_VIEW] (ScopedGameplayUiViewLayout),
    // so the pref and the view geometry have to be moved together.
    const auto set_view_mode = [](viewscreen& view, signed char mode) {
        view.prefs[PREF_VIEW] = mode;
        view.resize(static_cast<char>(mode));
    };

    {
        ViewHudStateGuard view_guard(*s->viewob[0]);

        // Full screen: the cell draws, clear of both.
        set_view_mode(*s->viewob[0], PREF_VIEW_FULL);
        probe_freeze_pixels("1p full");
        EXPECT_GT(freeze_pixels, 0)
            << "a full-screen pane has room for the cell";

        // 1p PREF_VIEW mode 4: an 80-scanline pane, where the old fixed bm-34
        // row (y=46) sat squarely on the feed. Nothing may be drawn at all.
        set_view_mode(*s->viewob[0], PREF_VIEW_3);
        probe_freeze_pixels("1p view3");
        EXPECT_EQ(0, freeze_pixels)
            << "a pane with no clear row between feed and score box draws "
               "nothing";
    }

    // Three-way split in PREF_VIEW mode 1: 100px-wide columns, where the full
    // string would run straight into the radar block. The cell shortens
    // instead of overlapping it. ready_for_battle rebuilds the views, so
    // nothing may hold a reference to the old ones from here on.
    s->ready_for_battle(3);
    ASSERT_EQ(3, static_cast<int>(s->numviews));
    for (int i = 0; i < s->numviews; ++i)
    {
        ASSERT_TRUE(s->viewob[i] != nullptr);
        set_view_mode(*s->viewob[i], PREF_VIEW_PANELS);
    }
    probe_freeze_pixels("3p panels");
    EXPECT_GT(freeze_pixels, 0)
        << "a narrow column still gets the short-form cell";

    // The same column cannot hold a three-digit countdown even shortened
    // ("T: 300" is 36px against 32px of room), and overlapping the radar is
    // not an option: it draws nothing until the counter comes back down.
    probe_freeze_pixels("3p panels, three digits", 300);
    EXPECT_EQ(0, freeze_pixels)
        << "a column with no room for the short form draws nothing";

    // Four-way split: 159x99 quadrants, each with its own feed band and its
    // own radar block in the bottom-right corner.
    s->ready_for_battle(4);
    ASSERT_EQ(4, static_cast<int>(s->numviews));
    for (int i = 0; i < s->numviews; ++i)
    {
        ASSERT_TRUE(s->viewob[i] != nullptr);
        set_view_mode(*s->viewob[i], PREF_VIEW_FULL);
    }
    probe_freeze_pixels("4p quadrants");
    EXPECT_GT(freeze_pixels, 0)
        << "a quadrant pane still has room for the cell";
}

// ---------------------------------------------------------------------------
// Q2: the counter box fits the rows it holds.
//
// The 2026 wave rows print unbounded numbers, and the base tree right-aligns
// them to rm-2 with max(lm, rm - 2 - 6*len): "FOES: 12 (+34)" and
// "NEXT WAVE: 120s" start 29 and 35 px LEFT of the 55-px box and hang over
// the world in DARK_BLUE.  The rule these three cases pin is the one the fix
// implements: the box's WIDTH is derived from its widest row exactly as its
// HEIGHT is already derived from its row count.  Every row is left-aligned at
// one shared text column left+2 (the classic rm-55 column), with
//
//     left  = min(rm - 57, rm - 4 - 6 * widest_glyphs)
//     floor = min(lm + 66, rm - 57)      (never left of the caption column)
//
// and a row that does not fit the capacity (rm - 4 - floor) / 6 drops its
// label ("FOES: 1+2" -> "1+2") rather than hanging off the box.  Rows of <= 8
// glyphs reproduce the classic box byte for byte, which is why
// team_row_sits_at_the_pane_top and the two FLR band scans above stay green
// unchanged.
//
// All three are RED BY DESIGN on the phase-3 base tree (ae396461 + the commit
// that adds them): they assert the geometry the defect withholds.  That
// refusal cut is also the "before" half of the PR's proof media --
// scripts/media/capture_pr292.sh runs the first one as the `q2` scene with
// SCENE_ALLOW_FAIL=1 and keeps the PPM it dumps.

namespace {

// Restores everything the Q2 cases touch on the shared viewscreen, from a
// destructor, so a failing ASSERT cannot leak a pref into the next test
// (the PrefRestore idiom of team_row_sits_at_the_pane_top).
struct HudPrefRestore
{
    viewscreen* view;
    walker* control;
    char life, score, foes, overlay;
    explicit HudPrefRestore(viewscreen* v)
        : view(v)
        , control(v->control)
        , life(v->prefs[PREF_LIFE])
        , score(v->prefs[PREF_SCORE])
        , foes(v->prefs[PREF_FOES])
        , overlay(v->prefs[PREF_OVERLAY])
    {
    }
    ~HudPrefRestore()
    {
        view->control = control;
        view->prefs[PREF_LIFE] = life;
        view->prefs[PREF_SCORE] = score;
        view->prefs[PREF_FOES] = foes;
        view->prefs[PREF_OVERLAY] = overlay;
    }
};

struct LevelTickGuard
{
    GameWorld& world;
    std::uint32_t saved;
    explicit LevelTickGuard(GameWorld& w)
        : world(w), saved(w.level_tick_count())
    {
    }
    ~LevelTickGuard() { world.set_level_tick_count(saved); }
};

struct FloorCountGuard
{
    GameWorld& world;
    int saved;
    explicit FloorCountGuard(GameWorld& w) : world(w), saved(w.floor_count()) {}
    ~FloorCountGuard() { world.set_floor_count(saved); }
};

// One viewport mode, undone from a destructor (the ViewResize idiom of
// tests/integration/test_view_resize.cpp).  The pref and the pane geometry
// move TOGETHER: score_panel draws inside a ScopedGameplayUiViewLayout, which
// recomputes the pane from prefs[PREF_VIEW], so a resize() the pref does not
// agree with is thrown away before a single lm/rm is read.  The GladHud
// fixture's TearDown relayout_views() runs after this.
struct ViewModeGuard
{
    viewscreen* view;
    char saved;
    ViewModeGuard(viewscreen* v, char mode)
        : view(v), saved(v->prefs[PREF_VIEW])
    {
        view->prefs[PREF_VIEW] = mode;
        view->resize(mode);
    }
    ~ViewModeGuard()
    {
        view->prefs[PREF_VIEW] = saved;
        view->resize(saved);
    }
};

struct HudRefRow
{
    int x;
    int y;
    std::string text;
};

// Redraw the counter box by hand at the coordinates the fit rule prescribes
// and demand the captured frame match it pixel for pixel over the whole box
// rectangle: position, size, colour and text are all pinned at once.  This
// clobbers the buffer, so `actual` must already be captured.
static void expect_counter_box_pixels(
    screen* s, const std::array<unsigned char, 64000>& actual,
    int left, int top, int right, int bottom,
    const std::vector<HudRefRow>& rows, const char* what)
{
    s->clearbuffer();
    s->draw_button(left, top, right, bottom, 1, 1);
    for (const HudRefRow& row : rows)
        s->text_normal.write_xy(row.x, row.y, row.text.c_str(),
                                static_cast<unsigned char>(DARK_BLUE),
                                static_cast<short>(1));
    const auto expected = capture_rendered_frame(*s);

    std::vector<unsigned char> actual_box;
    std::vector<unsigned char> expected_box;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
        {
            const std::size_t offset = static_cast<std::size_t>(y * 320 + x);
            actual_box.push_back(actual[offset]);
            expected_box.push_back(expected[offset]);
        }
    EXPECT_EQ(expected_box, actual_box)
        << what << ": the box must span x[" << left << "," << right << "] y["
        << top << "," << bottom << "] with its rows at the shared text column "
        << left + 2;
}

// Nothing may be drawn in the band the rows used to hang into.
static void expect_band_clean(const std::array<unsigned char, 64000>& frame,
                              int x0, int x1, int y0, int y1, const char* what)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            ASSERT_EQ(0, static_cast<int>(
                          frame[static_cast<std::size_t>(y * 320 + x)]))
                << what << ": pixel at (" << x << "," << y << ") must stay "
                << "clean -- no counter row may hang off the box";
}

} // namespace

TEST_F(GladHud, zz_capture_pending_wave_counter_box)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    ASSERT_EQ(1, static_cast<int>(s->numviews))
        << "the pane geometry below assumes the single-view layout";

    ASSERT_EQ(0, static_cast<int>(v->mynum)) << "pane 0 of the layout";

    HudObListSwap swap;
    GameWorld& world = s->world();
    ASSERT_EQ(1, world.floor_count()) << "no FLR row in this scene";
    LevelTickGuard tick_guard(world);
    HudPrefRestore restore(v);
    ViewModeGuard view_mode(v, static_cast<char>(PREF_VIEW_FULL));

    auto control = make_player(0);
    ASSERT_NE(nullptr, control);
    walker* const controlp = control.get();
    ASSERT_NE(nullptr, controlp->myguy);
    ASSERT_EQ("SOLDIER", controlp->myguy->name)
        << "the caption is 7 glyphs wide (lm+3 .. lm+44), clear of the band "
           "scanned below";
    world.oblist.push_back(std::move(control));

    // 12 awake hostiles and a 34-strong wave that wakes at tick 1440
    // (the wake rule is level_tick_count > spawn_delay).
    for (int i = 0; i < 12; ++i)
    {
        auto foe = make_living(FAMILY_ORC, 1);
        ASSERT_NE(nullptr, foe);
        world.oblist.push_back(std::move(foe));
    }
    for (int i = 0; i < 34; ++i)
    {
        auto foe = make_living(FAMILY_ORC, 1);
        ASSERT_NE(nullptr, foe);
        foe->set_spawn_delay(1439);
        foe->set_dormant(true);
        world.oblist.push_back(std::move(foe));
    }
    world.set_level_tick_count(0);

    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;    // no HP box in the scanned band
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;  // the count-up consumes rng()
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    // Zero overscan in the default build, so these are the same lm/tm/rm
    // score_panel computes from this pane.
    const int tm = v->yloc;
    const int lm = v->xloc;
    const int rm = v->endx;
    ASSERT_EQ(0, tm);
    ASSERT_EQ(0, lm);
    ASSERT_EQ(320, rm) << "the full classic pane";

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto actual = capture_rendered_frame(*s);
    dump_frame_ppm(actual, "pending_wave_counter_box");

    EXPECT_TRUE(trace_contains("hud", "next_wave awake=12 pending=34 secs=120"))
        << "46 living hostiles, 34 of them dormant -> 12 awake; the wave wakes "
           "at tick 1440, i.e. (1440 + 11) / 12 = 120 s at the 12 Hz sim rate";

    // Rows: "TEAM: 1" (7), "FOES: 12+34" (11), "WAVE: 120s" (10).  The floor
    // is min(lm + 66, rm - 57) = 66, capacity (rm - 4 - 66) / 6 = 41, so every
    // row is drawn whole.  Widest 11 glyphs -> left = rm - 4 - 6*11 = rm - 70,
    // text column left + 2 = rm - 68, rows at tm+2 / tm+10 / tm+18 inside a
    // box tm+1 .. tm+24 (the wave row's height, unchanged by this rule).
    const int box_left = rm - 70;
    const int text_x = rm - 68;
    expect_counter_box_pixels(
        s, actual, box_left, tm + 1, rm - 2, tm + 24,
        {{text_x, tm + 2, "TEAM: 1"},
         {text_x, tm + 10, "FOES: 12+34"},
         {text_x, tm + 18, "WAVE: 120s"}},
        "pending-wave counter box");

    // Left of the box, right of the caption column: the band the base tree's
    // right-aligned rows hang into (FOES at rm-86, NEXT WAVE at rm-92).
    expect_band_clean(actual, lm + 64, box_left - 1, tm + 1, tm + 24,
                      "pending-wave counter box");
}

TEST_F(GladHud, counter_box_drops_labels_in_the_108px_pane)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    ASSERT_EQ(1, static_cast<int>(s->numviews))
        << "PREF_VIEW_3's 1p geometry assumes the single-view layout";

    ASSERT_EQ(0, static_cast<int>(v->mynum)) << "pane 0 of the layout";

    HudObListSwap swap;
    GameWorld& world = s->world();
    ASSERT_EQ(1, world.floor_count()) << "no FLR row in this scene";
    LevelTickGuard tick_guard(world);
    HudPrefRestore restore(v);
    ViewModeGuard view_mode(v, static_cast<char>(PREF_VIEW_3));

    // The 1p VIEW_3 inset pane (test_view_resize.cpp 1p_view3).
    ASSERT_EQ(106, static_cast<int>(v->xloc));
    ASSERT_EQ(214, static_cast<int>(v->endx));
    ASSERT_EQ(60, static_cast<int>(v->yloc));
    const int tm = v->yloc;
    const int lm = v->xloc;
    const int rm = v->endx;

    auto control = make_player(0);
    ASSERT_NE(nullptr, control);
    walker* const controlp = control.get();
    world.oblist.push_back(std::move(control));
    auto awake = make_living(FAMILY_ORC, 1);
    ASSERT_NE(nullptr, awake);
    world.oblist.push_back(std::move(awake));
    for (int i = 0; i < 2; ++i)
    {
        auto foe = make_living(FAMILY_ORC, 1);
        ASSERT_NE(nullptr, foe);
        foe->set_spawn_delay(143); // wakes at 144 -> (144 + 11) / 12 = 12 s
        foe->set_dormant(true);
        world.oblist.push_back(std::move(foe));
    }
    world.set_level_tick_count(0);

    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto actual = capture_rendered_frame(*s);

    EXPECT_TRUE(trace_contains("hud", "next_wave awake=1 pending=2 secs=12"))
        << "one awake hostile, a two-strong wave 144 ticks out";

    // The floor is min(lm + 66, rm - 57) = rm - 57 = 157: classic already
    // crosses the caption column in this 108-px pane, so the box may not grow
    // past it.  Capacity (rm - 4 - 157) / 6 = 8, so "FOES: 1+2" (9) and
    // "WAVE: 12s" (9) drop their labels to "1+2" / "12s" and "TEAM: 1" (7)
    // stays whole -- widest 7 -> left = min(rm - 57, rm - 4 - 42) = rm - 57,
    // the classic box, with its classic rm-55 text column.
    expect_counter_box_pixels(
        s, actual, rm - 57, tm + 1, rm - 2, tm + 24,
        {{rm - 55, tm + 2, "TEAM: 1"},
         {rm - 55, tm + 10, "1+2"},
         {rm - 55, tm + 18, "12s"}},
        "108-px pane counter box");

    // PREF_LIFE_OFF draws no HP box, the name box ends at tm+9 and the SPC
    // row sits at bm-24, so everything left of the box on the FOES/WAVE rows
    // is world.  The base tree paints "FOES: 1 (+2)" from lm+34 here.
    expect_band_clean(actual, lm, rm - 58, tm + 10, tm + 24,
                      "108-px pane counter box");
}

TEST_F(GladHud, counter_box_widens_for_a_two_digit_floor_label)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    ASSERT_EQ(1, static_cast<int>(s->numviews))
        << "the pane geometry below assumes the single-view layout";

    ASSERT_EQ(0, static_cast<int>(v->mynum)) << "pane 0 of the layout";

    HudObListSwap swap;
    GameWorld& world = s->world();
    FloorCountGuard floor_guard(world);
    HudPrefRestore restore(v);
    ViewModeGuard view_mode(v, static_cast<char>(PREF_VIEW_FULL));

    auto control = make_player(0);
    ASSERT_NE(nullptr, control);
    walker* const controlp = control.get();
    world.oblist.push_back(std::move(control));
    auto foe = make_living(FAMILY_ORC, 1);
    ASSERT_NE(nullptr, foe);
    world.oblist.push_back(std::move(foe));

    world.set_floor_count(12);
    controlp->set_floor(9); // reads as floor 10 of 12 (players count from one)

    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    const int tm = v->yloc;
    const int lm = v->xloc;
    const int rm = v->endx;
    ASSERT_EQ(0, tm);
    ASSERT_EQ(0, lm);
    ASSERT_EQ(320, rm) << "the full classic pane";

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto actual = capture_rendered_frame(*s);

    EXPECT_TRUE(trace_contains("hud", "floor FLR: 10/12"))
        << "the shared floor_hud_label for floor 9 of 12";
    EXPECT_FALSE(trace_contains("hud", "next_wave"))
        << "no pending wave: the FLR row is the third row, at tm+18";

    // Rows: "TEAM: 1" (7), "FOES: 1" (7), "FLR: 10/12" (10).  The FLR row
    // goes through the identical fit path, so it -- not the classic pair --
    // sets the width: left = rm - 4 - 6*10 = rm - 64, text column rm - 62,
    // box tm+1 .. tm+24 (16 classic + 8 for the floor row).
    const int box_left = rm - 64;
    const int text_x = rm - 62;
    expect_counter_box_pixels(
        s, actual, box_left, tm + 1, rm - 2, tm + 24,
        {{text_x, tm + 2, "TEAM: 1"},
         {text_x, tm + 10, "FOES: 1"},
         {text_x, tm + 18, "FLR: 10/12"}},
        "two-digit floor counter box");

    // The base tree right-aligns FLR from rm-62 while leaving the box at
    // rm-57; nothing may sit left of the widened box on the floor row.
    expect_band_clean(actual, lm + 64, box_left - 1, tm + 17, tm + 24,
                      "two-digit floor counter box");
}
