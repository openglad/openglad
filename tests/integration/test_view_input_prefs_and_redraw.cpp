#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <openglad/core/pixdefs.h>
#include <openglad/interface/render/video.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct KeyStateGuard
{
    const bool* saved = nullptr;
    std::array<bool, SDL_SCANCODE_COUNT> fake{};
    KeyStateGuard()
    {
        saved = og::runtime::current_session->keystates_;
        fake.fill(false);
        og::runtime::current_session->keystates_ = fake.data();
    }
    ~KeyStateGuard()
    {
        og::runtime::current_session->keystates_ = saved;
    }
};

struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old_key;
    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_),
          old_key(og::runtime::current_session->player_keys_[player_][key_enum_])
    {
        og::runtime::current_session->player_keys_[player][key_enum] = new_key;
    }
    ~KeyBindingGuard()
    {
        og::runtime::current_session->player_keys_[player][key_enum] = old_key;
    }
};

static SDL_Event keydown(SDL_Keycode key)
{
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    e.key.repeat = false;
    return e;
}
} // namespace

// Design §7.4: InputAction::OpenPrefs (slot 14) is a RESERVED wire slot with
// nothing behind it — the per-player options menu it used to open is retired
// and its rows live on the pause player screen / GAME SETTINGS now. The old
// menu set screen::redrawme = 1 and persisted prefs on exit, so both are the
// observables: a pressed OpenPrefs must leave the world and the view's prefs
// exactly as they were. The slot is deliberately BOUND here (its shipped
// default is KEYCODE_UNKNOWN) so this pins the missing dispatch, not just the
// missing binding.
TEST(ViewInputPrefsAndRedraw, viewscreen_input_open_prefs_dispatches_nothing)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    // Ensure a control exists so input() doesn't early-exit before the slot
    // would have been sampled.
    if (!vs->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(w != nullptr) << "control walker created";
        if (!w)
            return;
        w->set_team_num(0);
        w->set_user(0);
        w->set_act_type(ACT_CONTROL);
        vs->control = w;
    }

    vs->mynum = 0;
    vs->my_team = 0;

    KeyStateGuard ks;
    KeyBindingGuard bind_prefs(0, KEY_PREFS, SDLK_1);

    std::array<signed char, 10> saved_prefs{};
    std::copy(std::begin(vs->prefs), std::end(vs->prefs), saved_prefs.begin());
    const short saved_redrawme = og::runtime::current_session->myscreen_->redrawme;
    og::runtime::current_session->myscreen_->redrawme = 0;

    // No injector thread: nothing blocks any more. If a modal came back this
    // call would hang the suite instead of returning.
    (void)vs->input(keydown(SDLK_1));

    EXPECT_EQ(0, og::runtime::current_session->myscreen_->redrawme)
        << "a pressed OpenPrefs must not open anything that requests a redraw";
    EXPECT_TRUE(std::equal(saved_prefs.begin(), saved_prefs.end(), std::begin(vs->prefs)))
        << "a pressed OpenPrefs must not touch the view's preferences";

    og::runtime::current_session->myscreen_->redrawme = saved_redrawme;
}


// Moved here when test_view_get_keypress_and_edge_cases.cpp was retired with
// viewscreen::get_keypress and the options menu.
//
// input() runs sanitize_control_pointer FIRST (view.cpp): a control pointer
// that is not live in one of the level's owning lists is a dangling seat, so
// it is cleared to nullptr and input() returns 1 without dispatching
// anything. Nothing is logged — the old name's "logs" was wrong.
TEST(ViewInputPrefsAndRedraw, viewscreen_input_drops_a_control_not_in_the_level)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, v) << "view should exist";

    KeyStateGuard ks;

    KeyBindingGuard bind_switch(0, KEY_SWITCH, SDLK_TAB);
    KeyBindingGuard bind_shifter(0, KEY_SHIFTER, SDLK_LSHIFT);
    KeyBindingGuard bind_cheat(0, KEY_CHEAT, SDLK_C);

    // Create a control walker not present in level_data.oblist.
    PixieData px(1, 1, 1, new unsigned char[1]{0});
    walker orphan(px);
    orphan.set_team_num(0);
    orphan.set_real_team_num(255);
    orphan.set_dead(0);
    orphan.set_user(0);
    orphan.set_act_type(ACT_CONTROL);

    walker* saved_control = v->control;
    v->mynum = 0;
    v->my_team = 0;
    v->control = &orphan;

    // Forward switch: the orphan seat is dropped, not driven.
    EXPECT_EQ(1, (int)v->input(keydown(SDLK_TAB)))
        << "input() still reports the event consumed";
    EXPECT_EQ(nullptr, v->control)
        << "a control that is not live in the level must be cleared";

    // The sanitize step runs before any key is looked at, so an unrelated key
    // drops the orphan just the same.
    v->control = &orphan;
    EXPECT_EQ(1, (int)v->input(keydown(SDLK_F1)))
        << "input() still reports the event consumed";
    EXPECT_EQ(nullptr, v->control)
        << "any event drops an orphan seat, not just the switch key";

    // And again with the shifter held (the reverse-switch branch).
    v->control = &orphan;
    ks.fake[SDL_SCANCODE_LSHIFT] = true;
    EXPECT_EQ(1, (int)v->input(keydown(SDLK_TAB)))
        << "input() still reports the event consumed";
    EXPECT_EQ(nullptr, v->control)
        << "shift+switch on an orphan seat must drop it too";
    ks.fake[SDL_SCANCODE_LSHIFT] = false;

    v->control = saved_control;
}


// Design §7.2: the Shift+/ briefing chord is DELETED (raw '/' was player
// 2's SPECIAL key; the briefing lives on the PAUSED menu now). The old
// handler called read_scenario and set redrawme=1 — under TESTING
// read_scenario returns immediately, so redrawme is the observable: it must
// stay untouched when the chord arrives.
TEST(ViewInputPrefsAndRedraw, viewscreen_input_shift_slash_chord_is_gone)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    if (!vs->control)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(w != nullptr) << "control walker created";
        if (!w)
            return;
        w->set_team_num(0);
        w->set_user(0);
        w->set_act_type(ACT_CONTROL);
        vs->control = w;
    }

    vs->mynum = 0;
    vs->my_team = 0;

    const short saved_redrawme = og::runtime::current_session->myscreen_->redrawme;
    KeyStateGuard ks;
    // Hold the shifter key (LSHIFT by default for player 0).
    ks.fake[SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SHIFTER]), nullptr)] = true;
    og::runtime::current_session->myscreen_->redrawme = 0;
    (void)vs->input(keydown(SDLK_SLASH));
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->redrawme)
        << "Shift+/ must no longer dispatch the scenario briefing";
    ks.fake[SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SHIFTER]), nullptr)] = false;
    og::runtime::current_session->myscreen_->redrawme = saved_redrawme;
}


namespace
{
// redraw() refuses to rasterize the world into the fixed UI canvas, so a
// pixel-level view test has to be on the World canvas — and must put the
// binary's canvas back where it found it.
struct WorldCanvasGuard
{
    screen& game;
    CanvasTarget saved;
    short saved_numviews;
    short saved_mynum;
    explicit WorldCanvasGuard(screen& g)
        : game(g), saved(g.active_canvas()), saved_numviews(g.numviews),
          saved_mynum(g.viewob[0]->mynum)
    {
        game.set_active_canvas(CanvasTarget::World);
        game.set_world_canvas_pinned_classic(true);
        game.relayout_views();
    }
    ~WorldCanvasGuard()
    {
        game.numviews = saved_numviews;
        game.viewob[0]->mynum = saved_mynum;
        game.set_world_canvas_pinned_classic(false);
        game.relayout_views();
        game.set_active_canvas(saved);
    }
};

std::vector<int> capture_tile(screen& game, int x, int y)
{
    std::vector<int> block;
    block.reserve(static_cast<std::size_t>(GRID_SIZE * GRID_SIZE));
    for (int dy = 0; dy < GRID_SIZE; ++dy)
        for (int dx = 0; dx < GRID_SIZE; ++dx)
        {
            int index = -1;
            game.get_pixel(x + dx, y + dy, &index);
            block.push_back(index);
        }
    return block;
}
} // namespace

// The off-map margin is not black: for the base floor redraw() fills the
// out-of-bounds cells with wall tiles — j == -1 gets PIX_WALLSIDE1, j == -2
// gets PIX_H_WALL1, and everything else (the corner columns) gets
// PIX_WALLTOP_H (view.cpp, the `i<0 || j<0 || i>=maxx || j>=maxy` arm).
//
// The oracle is the tile itself: each margin cell is compared, pixel for
// pixel, against the SAME tile drawn through the ordinary on-map path at a
// known screen position. A margin that stopped drawing (or drew the wrong
// tile) cannot match.
TEST(ViewInputPrefsAndRedraw, viewscreen_redraw_negative_scroll_fills_the_margin_with_wall_tiles)
{
    screen* const game = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game);
    viewscreen* vs = game->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen exists";

    WorldCanvasGuard canvas_guard(*game);
    game->numviews = 1;
    vs->mynum = 0;
    vs->resize(PREF_VIEW_FULL);
    ASSERT_EQ(0, (int)vs->xloc) << "the classic 1p pane starts at the origin";
    ASSERT_EQ(0, (int)vs->yloc) << "the classic 1p pane starts at the origin";
    ASSERT_GE((int)vs->yview, 3 * GRID_SIZE) << "the pane must show 3 tile rows";

    GameWorld& world = game->world();
    world.create_new_grid();
    world.delete_objects();
    ASSERT_GE((int)world.grid.w, 3) << "grid wide enough for the reference row";

    // The three border tiles, laid out on the grid's first row, so the
    // reference blocks come off the ordinary in-bounds draw path.
    world.grid.data[0] = PIX_H_WALL1;
    world.grid.data[1] = PIX_WALLSIDE1;
    world.grid.data[2] = PIX_WALLTOP_H;

    const Sint32 saved_topx = game->level_visuals_.topx;
    const Sint32 saved_topy = game->level_visuals_.topy;
    walker* saved_control = vs->control;
    vs->control = nullptr; // no control => the camera reads level_visuals_
    vs->clear_text();      // the message feed inks over the sampled rows

    // Reference pass: camera at the origin, so grid cell (n,0) lands at
    // screen (n*GRID_SIZE, 0).
    game->level_visuals_.topx = 0;
    game->level_visuals_.topy = 0;
    game->clearbuffer();
    ASSERT_TRUE(vs->redraw()) << "redraw must run on the World canvas";
    const std::vector<int> ref_h_wall = capture_tile(*game, 0, 0);
    const std::vector<int> ref_wallside = capture_tile(*game, GRID_SIZE, 0);
    const std::vector<int> ref_walltop = capture_tile(*game, 2 * GRID_SIZE, 0);
    ASSERT_NE(ref_h_wall, ref_wallside)
        << "the three border tiles must be visually distinct for this oracle";
    ASSERT_NE(ref_h_wall, ref_walltop)
        << "the three border tiles must be visually distinct for this oracle";
    ASSERT_NE(ref_wallside, ref_walltop)
        << "the three border tiles must be visually distinct for this oracle";

    // Margin pass: scroll two whole tiles plus one pixel off the top-left, so
    // the j == -2, j == -1 and corner cells all land fully inside the pane.
    // Cell (i,j) lands at screen (i*16 + 33, j*16 + 33).
    game->level_visuals_.topx = -2 * GRID_SIZE - 1;
    game->level_visuals_.topy = -2 * GRID_SIZE - 1;
    game->clearbuffer();
    ASSERT_TRUE(vs->redraw()) << "redraw must run on the World canvas";

    EXPECT_EQ(ref_h_wall,
              capture_tile(*game, 2 * GRID_SIZE + 1, 1))
        << "the j == -2 margin row must be PIX_H_WALL1";
    EXPECT_EQ(ref_wallside,
              capture_tile(*game, 2 * GRID_SIZE + 1, GRID_SIZE + 1))
        << "the j == -1 margin row must be PIX_WALLSIDE1";
    EXPECT_EQ(ref_walltop, capture_tile(*game, GRID_SIZE + 1, 1))
        << "the off-map corner (i < 0) must be PIX_WALLTOP_H";

    game->level_visuals_.topx = saved_topx;
    game->level_visuals_.topy = saved_topy;
    vs->control = saved_control;
}

