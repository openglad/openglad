#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/web_back_key.h>
#include <openglad/legacy/colors.h>
#include <openglad/platform/local_transport_shadow.h>
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace {

struct ClassicViewLayoutGuard
{
    screen* game = og::runtime::current_session->myscreen_;
    viewscreen* view = game->viewob[0].get();
    short saved_numviews = game->numviews;
    short saved_mynum = view->mynum;

    ClassicViewLayoutGuard()
    {
        game->set_world_canvas_pinned_classic(true);
        game->relayout_views();
    }

    ~ClassicViewLayoutGuard()
    {
        game->numviews = saved_numviews;
        view->mynum = saved_mynum;
        game->set_world_canvas_pinned_classic(false);
        game->relayout_views();
    }
};

} // namespace

// ---------------------------------------------------------------------------
// compute_hp_color tests
// ---------------------------------------------------------------------------

TEST(ViewFuncs, compute_hp_color_full)
{
    unsigned char c = compute_hp_color(100.0f, 100.0f);
    ASSERT_EQ((int)(HIGH_HP_COLOR+2), (int)c) << "full HP should return HIGH_HP_COLOR+2";
}


TEST(ViewFuncs, compute_hp_color_slightly_damaged)
{
    unsigned char c = compute_hp_color(80.0f, 100.0f);
    ASSERT_EQ((int)(MAX_HP_COLOR+4), (int)c) << "slightly damaged HP should return MAX_HP_COLOR+4";
}


TEST(ViewFuncs, compute_hp_color_half)
{
    unsigned char c = compute_hp_color(50.0f, 100.0f);
    ASSERT_EQ((int)(MID_HP_COLOR-3), (int)c) << "half HP should return MID_HP_COLOR-3";
}


TEST(ViewFuncs, compute_hp_color_low)
{
    unsigned char c = compute_hp_color(20.0f, 100.0f);
    ASSERT_EQ((int)LOW_HP_COLOR, (int)c) << "low HP should return LOW_HP_COLOR";
}


TEST(ViewFuncs, compute_hp_color_over_max)
{
    unsigned char c = compute_hp_color(150.0f, 100.0f);
    ASSERT_EQ((int)ORANGE_START, (int)c) << "over-max HP should return ORANGE_START";
}


// ---------------------------------------------------------------------------
// compute_mp_color tests
// ---------------------------------------------------------------------------

TEST(ViewFuncs, compute_mp_color_full)
{
    unsigned char c = compute_mp_color(100.0f, 100.0f);
    ASSERT_EQ((int)(HIGH_MP_COLOR+3), (int)c) << "full MP should return HIGH_MP_COLOR+3";
}


TEST(ViewFuncs, compute_mp_color_slightly_used)
{
    unsigned char c = compute_mp_color(80.0f, 100.0f);
    ASSERT_EQ((int)MAX_MP_COLOR, (int)c) << "slightly used MP should return MAX_MP_COLOR";
}


TEST(ViewFuncs, compute_mp_color_half)
{
    unsigned char c = compute_mp_color(50.0f, 100.0f);
    ASSERT_EQ((int)MID_MP_COLOR, (int)c) << "half MP should return MID_MP_COLOR";
}


TEST(ViewFuncs, compute_mp_color_low)
{
    unsigned char c = compute_mp_color(20.0f, 100.0f);
    ASSERT_EQ((int)LOW_MP_COLOR, (int)c) << "low MP should return LOW_MP_COLOR";
}


TEST(ViewFuncs, compute_mp_color_over_max)
{
    unsigned char c = compute_mp_color(150.0f, 100.0f);
    ASSERT_EQ((int)WATER_START, (int)c) << "over-max MP should return WATER_START";
}


// ---------------------------------------------------------------------------
// viewscreen text management tests
// ---------------------------------------------------------------------------

TEST(ViewFuncs, viewscreen_set_display_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    // Clear first
    vs->clear_text();

    vs->set_display_text("Hello", 10);
    ASSERT_TRUE(!vs->textlist[0].empty()) << "textlist[0] should not be empty after set_display_text";
    ASSERT_EQ(10, (int)vs->textcycles[0]) << "textcycles[0] should be 10";
}


TEST(ViewFuncs, viewscreen_clear_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    vs->set_display_text("Test", 20);
    vs->clear_text();

    for (int i = 0; i < MAX_MESSAGES; i++) {
        ASSERT_TRUE(vs->textlist[i].empty()) << "all text should be cleared";
    }
}


// Consecutive set_display_text calls fill successive FREE slots in call order,
// each keeping its own numcycles (view.cpp set_display_text: scan to the first
// empty slot, write there). Slots and durations are pinned by value: a feed
// that reordered the lines, reused one slot, or lost the per-line duration is
// a different feed.
TEST(ViewFuncs, viewscreen_set_display_text_multiple)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 should exist";

    vs->clear_text();
    vs->set_display_text("Line 1", 10);
    vs->set_display_text("Line 2", 15);
    vs->set_display_text("Line 3", 20);

    EXPECT_EQ("Line 1", vs->textlist[0]) << "the first line takes slot 0";
    EXPECT_EQ("Line 2", vs->textlist[1]) << "the second line takes slot 1";
    EXPECT_EQ("Line 3", vs->textlist[2]) << "the third line takes slot 2";
    EXPECT_EQ(10, (int)vs->textcycles[0]) << "slot 0 keeps its own duration";
    EXPECT_EQ(15, (int)vs->textcycles[1]) << "slot 1 keeps its own duration";
    EXPECT_EQ(20, (int)vs->textcycles[2]) << "slot 2 keeps its own duration";
    for (int slot = 3; slot < MAX_MESSAGES; ++slot)
        EXPECT_TRUE(vs->textlist[slot].empty())
            << "three lines must occupy exactly three slots (slot " << slot
            << ")";

    vs->clear_text();
}


// A full feed scrolls: set_display_text shift_text(0)s the oldest line off the
// top and writes the newest into the LAST slot, duration and all. Every line
// is distinct here, so a feed that silently dropped the overflow (or scrolled
// the wrong way) cannot hide behind a non-empty slot left by an earlier fill.
TEST(ViewFuncs, viewscreen_set_display_text_overflow_scrolls_oldest_off_the_top)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 should exist";

    vs->clear_text();
    // MAX_MESSAGES + 1 distinct lines: "Msg0" .. "Msg<MAX_MESSAGES>", with a
    // distinct duration each so the scrolled slot's cycles are checkable too.
    for (int i = 0; i <= MAX_MESSAGES; i++)
        vs->set_display_text("Msg" + std::to_string(i),
                             static_cast<short>(10 + i));

    EXPECT_EQ("Msg1", vs->textlist[0])
        << "the oldest line (Msg0) must scroll off the top";
    for (int slot = 0; slot < MAX_MESSAGES; ++slot)
    {
        EXPECT_EQ("Msg" + std::to_string(slot + 1), vs->textlist[slot])
            << "slot " << slot << " after the scroll";
        EXPECT_EQ(10 + slot + 1, (int)vs->textcycles[slot])
            << "slot " << slot << " keeps the scrolled line's duration";
    }
    EXPECT_EQ("Msg" + std::to_string(MAX_MESSAGES),
              vs->textlist[MAX_MESSAGES - 1])
        << "the newest line lands in the last slot";

    vs->clear_text();
}


TEST(ViewFuncs, viewscreen_shift_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    vs->clear_text();
    vs->set_display_text("First", 10);
    vs->set_display_text("Second", 15);
    vs->set_display_text("Third", 20);

    vs->shift_text(0);
    // After shifting from 0, "Second" should now be in slot 0
    ASSERT_EQ(15, (int)vs->textcycles[0]) << "shift should move second to first";
}


// ---------------------------------------------------------------------------
// Message-feed lifecycle (#246)
// ---------------------------------------------------------------------------

// The world clock restarts at 0 on every level (re)launch. A line stamped
// before the reset kept its far-future expiry tick, so it outlived the reset
// by its whole remaining duration — the stale-PAUSED class of bug. A slot
// whose stamp is in the future is expired, full stop.
TEST(ViewFuncs, viewscreen_text_expires_when_the_tick_clock_resets)
{
    screen* game = og::runtime::current_session->myscreen_;
    viewscreen* vs = game->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    const std::uint32_t saved_tick = game->world().tick_count_;
    game->world().tick_count_ = 500;
    vs->clear_text();
    vs->set_display_text("STALE", 200);
    ASSERT_EQ("STALE", vs->textlist[0]) << "line should be on the feed";

    game->world().tick_count_ = 0; // the level relaunch
    vs->display_text();            // draw gate + garbage collection

    EXPECT_TRUE(vs->textlist[0].empty())
        << "a line stamped before a tick reset must not outlive it";

    vs->clear_text();
    game->world().tick_count_ = saved_tick;
}


TEST(ViewFuncs, viewscreen_expire_display_text_removes_only_that_line)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    vs->clear_text();
    // Never appends: an absent line stays absent (refresh_display_text's
    // fall-through would have written it).
    vs->expire_display_text("GHOST");
    EXPECT_TRUE(vs->textlist[0].empty())
        << "expiring an absent line must not put it on the feed";

    vs->set_display_text("ALPHA", 10);
    vs->set_display_text("BETA", 10);
    vs->expire_display_text("ALPHA");
    EXPECT_EQ("BETA", vs->textlist[0]) << "surviving line shifts up";
    EXPECT_TRUE(vs->textlist[1].empty()) << "only one line should remain";

    vs->clear_text();
}


// The pause overlay is re-stamped every frame while the pause holds, so
// nothing but an explicit off-switch takes it down.
TEST(ViewFuncs, pause_overlay_off_switch_retires_both_overlay_lines)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";
    const std::string hint =
        og::input::kWebBackKeyMode ? "BKSP: Menu" : "ESC: Menu";

    vs->clear_text();
    vs->set_display_text("KEEP ME", 40);
    vs->set_display_text("PAUSED by Ana", 1);
    vs->set_display_text(hint, 1);

    og::runtime::detail::clear_pause_overlay_text(*vs, "PAUSED by Ana");

    EXPECT_EQ("KEEP ME", vs->textlist[0])
        << "the off-switch must leave unrelated messages alone";
    EXPECT_TRUE(vs->textlist[1].empty()) << "banner and hint both retired";

    // The owner name can arrive after the anonymous banner was stamped, so
    // the fallback line is retired too.
    vs->clear_text();
    vs->set_display_text("PAUSED", 1);
    og::runtime::detail::clear_pause_overlay_text(*vs, "PAUSED by Ana");
    EXPECT_TRUE(vs->textlist[0].empty())
        << "anonymous PAUSED banner must be retired as well";

    // A pause that changed hands leaves a banner naming neither the current
    // owner nor the anonymous fallback. Only the write site's record knows
    // that line, so the off-switch takes it as well.
    vs->clear_text();
    vs->set_display_text("PAUSED by Bo", 1);
    og::runtime::detail::clear_pause_overlay_text(
        *vs, "PAUSED by Ana", "PAUSED by Bo");
    EXPECT_TRUE(vs->textlist[0].empty())
        << "the recorded banner must be retired too";

    vs->clear_text();
}


TEST(ViewFuncs, screen_clear_all_view_text_wipes_every_live_view)
{
    screen* game = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game != nullptr) << "screen should exist";

    const short views = std::min<short>(
        game->numviews, static_cast<short>(std::size(game->viewob)));
    ASSERT_GT(views, 0) << "at least one view expected";
    for (short i = 0; i < views; ++i)
        if (game->viewob[i] != nullptr)
            game->viewob[i]->set_display_text("LEFTOVER", 40);

    game->clear_all_view_text();

    for (short i = 0; i < views; ++i)
    {
        if (game->viewob[i] == nullptr)
            continue;
        for (int slot = 0; slot < MAX_MESSAGES; ++slot)
            EXPECT_TRUE(game->viewob[i]->textlist[slot].empty())
                << "view " << i << " slot " << slot << " should be cleared";
    }
}


// ---------------------------------------------------------------------------
// viewscreen resize tests (covers the large resize function)
// ---------------------------------------------------------------------------

// FULL for one player is the WHOLE classic canvas: kOnePlayerInsetX/Y[FULL]
// are both 0, so the pane is (0,0,320x200) — distinct from PANELS' inset
// 232x176, which a `> 200 && > 150` oracle could not tell apart.
TEST(ViewFuncs, viewscreen_resize_full)
{
    ClassicViewLayoutGuard canvas_guard;
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 should exist";
    og::runtime::current_session->myscreen_->numviews = 1;
    vs->mynum = 0;

    // Come in off the inset layout so a resize() that did nothing at all
    // cannot pass by leaving the pane where it already was.
    vs->resize(PREF_VIEW_PANELS);
    ASSERT_EQ(232, (int)vs->xview) << "the inset starting layout";

    vs->resize(PREF_VIEW_FULL);
    EXPECT_EQ(0, (int)vs->xloc) << "1p FULL starts at the canvas origin";
    EXPECT_EQ(0, (int)vs->yloc) << "1p FULL starts at the canvas origin";
    EXPECT_EQ(320, (int)vs->xview) << "1p FULL is the whole canvas width";
    EXPECT_EQ(200, (int)vs->yview) << "1p FULL is the whole canvas height";
    EXPECT_EQ(320, (int)vs->endx) << "endx = xloc + width";
    EXPECT_EQ(200, (int)vs->endy) << "endy = yloc + height";
}


TEST(ViewFuncs, viewscreen_resize_panels)
{
    ClassicViewLayoutGuard canvas_guard;
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;

    vs->resize(PREF_VIEW_PANELS);
    ASSERT_EQ(232, (int)vs->xview) << "panels mode width should be 232";
    ASSERT_EQ(176, (int)vs->yview) << "panels mode height should be 176";

    // Restore
    vs->resize(PREF_VIEW_FULL);
    og::runtime::current_session->myscreen_->numviews = old_numviews;
}


TEST(ViewFuncs, viewscreen_resize_small_modes)
{
    ClassicViewLayoutGuard canvas_guard;
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;

    vs->resize(PREF_VIEW_1);
    ASSERT_EQ(192, (int)vs->xview) << "mode 1 width should be 192";

    vs->resize(PREF_VIEW_2);
    ASSERT_EQ(148, (int)vs->xview) << "mode 2 width should be 148";

    vs->resize(PREF_VIEW_3);
    ASSERT_EQ(108, (int)vs->xview) << "mode 3 width should be 108";

    // Restore
    vs->resize(PREF_VIEW_FULL);
    og::runtime::current_session->myscreen_->numviews = old_numviews;
}
