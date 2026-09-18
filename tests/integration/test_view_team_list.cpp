#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/render/video.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

// TESTING seam (view.cpp): one-shot pass budget for the compiled-out wait
// loop, driving the optional poll callback like real wait passes.
void view_team_testing_set_poll_passes(int passes);

namespace
{
// get_pixel resolves a pixel back to a palette INDEX by reverse RGB lookup and
// returns the LOWEST index with that colour, so an expected index has to be
// put through the same rule before it can be compared.
int canonical_index(unsigned char color)
{
    int wr = 0, wg = 0, wb = 0;
    query_palette_reg(color, &wr, &wg, &wb);
    for (int i = 0; i < 256; ++i)
    {
        int r = 0, g = 0, b = 0;
        query_palette_reg(static_cast<unsigned char>(i), &r, &g, &b);
        if (r == wr && g == wg && b == wb)
            return i;
    }
    return static_cast<int>(color);
}
} // namespace

// viewscreen::view_team draws one 6px row per LIVING walker on my_team that
// has a name: name at left+5 (RED for the seat's own control, BLACK for
// everyone else), "hp/max" at left+70 in compute_hp_color, "mp/max" at
// left+130 in compute_mp_color, level at left+195, starting at top+9.
// Walkers on other teams are filtered out entirely.
//
// The oracle is the rendered glyph: every opaque pixel of the expected
// character at the expected cell must carry the expected colour. The
// other-team walker is deliberately FIRST in oblist, so a broken team filter
// does not merely add a row — it puts the wrong character in row 0.
TEST(ViewTeamList, viewscreen_view_team_renders_entries_for_my_team)
{
    screen* const game = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game);
    viewscreen* vs = game->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen exists";

    // view_team draws on the UI canvas (ScopedUiCanvas) and restores whatever
    // canvas was active, so the read-back has to happen with UI active.
    const CanvasTarget saved_canvas = game->active_canvas();
    game->set_active_canvas(CanvasTarget::UI);

    game->world().create_new_grid();
    game->world().delete_objects();

    // Deterministic oblist order: the filtered-out foreigner leads.
    walker* w_other = game->world().add_ob(Order::Living, FAMILY_ORC);
    walker* w0 = game->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* w1 = game->world().add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, w_other);
    ASSERT_NE(nullptr, w0);
    ASSERT_NE(nullptr, w1);

    const short saved_my_team = vs->my_team;
    walker* const saved_control = vs->control;
    vs->my_team = 1;
    w0->set_team_num(1);
    w1->set_team_num(1);
    w_other->set_team_num(2);

    w0->stats()->name = "A";
    w1->stats()->name = "B";
    w_other->stats()->name = "X";

    vs->control = w1; // the seat's own fighter: RED name

    w0->stats()->set_hitpoints(1);        // 1/10 -> LOW_HP_COLOR
    w0->stats()->set_max_hitpoints(10);
    w0->stats()->set_magicpoints(10);
    w0->stats()->set_max_magicpoints(10); // mp == max -> HIGH_MP_COLOR+3
    w0->stats()->set_level(7);

    w1->stats()->set_hitpoints(20);       // hp > max -> ORANGE_START
    w1->stats()->set_max_hitpoints(10);
    w1->stats()->set_magicpoints(0);      // 0/10 -> LOW_MP_COLOR
    w1->stats()->set_max_magicpoints(10);

    // The classic rect, passed explicitly so the row geometry is pinned here
    // rather than read out of view.cpp's file-local constants.
    constexpr int kLeft = 20;
    constexpr int kTop = 2;
    vs->view_team(kLeft, kTop, 280, 198);

    text& font = game->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());
    ASSERT_EQ(5, (int)font.sizex) << "the 5x5 text font sets the 6px advance";
    const int advance = font.sizex + 1;
    const std::size_t stride = static_cast<std::size_t>(font.sizex) *
                               static_cast<std::size_t>(font.sizey);

    const auto expect_glyph = [&](char letter, int x, int y,
                                  unsigned char color, const char* what) {
        const int want = canonical_index(color);
        const unsigned char* const glyph =
            font.letters->data.get() +
            static_cast<std::size_t>(static_cast<unsigned char>(letter)) *
                stride;
        int opaque = 0;
        for (int row = 0; row < font.sizey; ++row)
            for (int col = 0; col < font.sizex; ++col)
            {
                if (glyph[static_cast<std::size_t>(row * font.sizex + col)] == 0)
                    continue;
                ++opaque;
                int actual = -1;
                game->get_pixel(x + col, y + row, &actual);
                EXPECT_EQ(want, actual)
                    << what << ": '" << letter << "' pixel " << col << ","
                    << row;
            }
        EXPECT_GT(opaque, 0) << what << ": glyph has ink to check";
    };

    const int name_x = kLeft + 5;
    const int hp_x = kLeft + 70;
    const int mp_x = kLeft + 130;
    const int level_x = kLeft + 195;
    const int row0_y = kTop + 9;
    const int row1_y = row0_y + 6;
    const int row2_y = row1_y + 6;

    // Row 0 is the teammate: BLACK name, "   1/10", "  10/10", " 7".
    expect_glyph('A', name_x, row0_y, BLACK, "row 0 name");
    expect_glyph('1', hp_x + 3 * advance, row0_y, LOW_HP_COLOR, "row 0 hp");
    expect_glyph('1', mp_x + 2 * advance, row0_y,
                 static_cast<unsigned char>(HIGH_MP_COLOR + 3), "row 0 mp");
    expect_glyph('7', level_x + advance, row0_y, BLACK, "row 0 level");

    // Row 1 is the seat's own control: RED name, over-max hp, empty mp.
    expect_glyph('B', name_x, row1_y, RED, "row 1 name");
    expect_glyph('2', hp_x + 2 * advance, row1_y, ORANGE_START, "row 1 hp");
    expect_glyph('0', mp_x + 3 * advance, row1_y, LOW_MP_COLOR, "row 1 mp");

    // Row 2 does not exist: the team-2 walker is filtered out, so the third
    // row's name cell is still bare panel face.
    int face = -1;
    game->get_pixel(260, 150, &face);
    for (int row = 0; row < font.sizey; ++row)
        for (int col = 0; col < font.sizex; ++col)
        {
            int actual = -1;
            game->get_pixel(name_x + col, row2_y + row, &actual);
            EXPECT_EQ(face, actual)
                << "a walker on another team must not get a roster row ("
                << col << "," << row << ")";
        }

    vs->control = saved_control;
    vs->my_team = saved_my_team;
    game->world().delete_objects();
    game->set_active_canvas(saved_canvas);
}


namespace
{
int s_poll_count = 0;
bool counting_poll()
{
    // True for the first two passes, false on the third: the wait must end
    // on the first false.
    return ++s_poll_count < 3;
}
} // namespace

// The optional poll callback (design §7.2 VIEW TEAM hosting): invoked once
// per wait pass, false ends the wait. Under TESTING the blocking loop is
// compiled out; the one-shot pass seam drives the same contract.
TEST(ViewTeamList, viewscreen_view_team_poll_runs_per_pass_and_false_ends_wait)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;
    og::runtime::current_session->myscreen_->world().create_new_grid();

    // A 10-pass budget, but the callback denies on pass 3: exactly 3 polls.
    view_team_testing_set_poll_passes(10);
    s_poll_count = 0;
    vs->view_team(&counting_poll);
    EXPECT_EQ(3, s_poll_count)
        << "the wait must end on the first false return from the poll";

    // The pass budget is one-shot: a plain call afterwards never polls.
    s_poll_count = 0;
    vs->view_team(&counting_poll);
    EXPECT_EQ(0, s_poll_count) << "the TESTING pass budget must not persist";
}

