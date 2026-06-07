/* Tests for CursesRenderer.
 *
 * Two flavors of world:
 *   - A hand-built GameWorld (custom entity_factory + a small painted grid) for
 *     exact-cell assertions on tiles, entity placement, camera centering, the
 *     HUD, and the message log. No level files, no SDL.
 *   - A real headless level 1 loaded via LevelRuntimeData for an integration
 *     smoke test that the renderer survives a realistic, populated world.
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/curses_renderer.h>
#include <openglad/platform/curses/headless_terminal.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include <memory>

using namespace og::curses;

namespace {

// A GameWorld we fully control: a custom entity_factory so add_ob() works
// without a level loader, plus a painted grid wired to the smoother. The
// helper owns the world; callers paint tiles and add walkers.
class HandWorld
{
public:
    HandWorld(int tiles_w, int tiles_h)
        : world_(std::make_unique<GameWorld>(0)), w_(tiles_w), h_(tiles_h)
    {
        // Headless walkers carry their own stats; that is all the renderer needs.
        world_->entity_factory = [](Order, std::int32_t) {
            return std::make_unique<walker>();
        };

        // Paint an all-grass grid and point the smoother at it.
        auto* cells = new unsigned char[static_cast<std::size_t>(w_ * h_)];
        for (int i = 0; i < w_ * h_; ++i)
            cells[i] = PIX_GRASS1;
        world_->grid = PixieData(1, static_cast<unsigned char>(w_),
                                 static_cast<unsigned char>(h_), cells);
        world_->mysmoother.set_target(world_->grid);

        world_->pixmaxx = w_ * GRID_SIZE;
        world_->pixmaxy = h_ * GRID_SIZE;
    }

    GameWorld& world() { return *world_; }

    // Set the terrain PIX value at a tile (smoother reflects it immediately).
    void paint(int tx, int ty, unsigned char pix)
    {
        if (tx >= 0 && tx < w_ && ty >= 0 && ty < h_)
            world_->grid.data[static_cast<std::size_t>(tx + ty * w_)] = pix;
    }

    // Add a living creature at a tile center, returning the walker.
    walker* add_creature(int tx, int ty, int family, unsigned char team)
    {
        return add_entity(Order::Living, family, tx, ty, team);
    }

    // Add an entity of any order (Living / Treasure / Generator) at a tile
    // center, stamping the order/family/position/team the production entity
    // factory would normally apply (the hand-built factory makes bare walkers).
    walker* add_entity(Order order, int family, int tx, int ty, unsigned char team)
    {
        walker* w = world_->add_ob(order, family);
        if (w) {
            w->set_order(order);
            w->set_family(static_cast<char>(family));
            w->setxy(static_cast<short>(tx * GRID_SIZE),
                     static_cast<short>(ty * GRID_SIZE));
            w->set_team_num(team);
        }
        return w;
    }

    // Add an effect (Order::FX) to the world's fxlist at a tile center — how
    // boomerangs / magic shields / explosions actually live in the world.
    walker* add_effect(int family, int tx, int ty)
    {
        walker* w = world_->add_fx_ob(Order::FX, family);
        if (w) {
            w->set_order(Order::FX);
            w->set_family(static_cast<char>(family));
            w->setxy(static_cast<short>(tx * GRID_SIZE),
                     static_cast<short>(ty * GRID_SIZE));
        }
        return w;
    }

private:
    std::unique_ptr<GameWorld> world_;
    int w_;
    int h_;
};

// Count occurrences of `ch` only inside the viewport region (excluding the HUD
// top rows and the log bottom rows), so HUD/log text (e.g. the 'o' in "Score")
// does not pollute entity-presence assertions. Matches the renderer's default
// layout: 2 HUD rows on top, max_log_lines (6) on the bottom.
int count_in_viewport(const HeadlessTerminal& term, char32_t ch,
                      int hud_rows = 2, int log_rows = 6)
{
    int n = 0;
    for (int r = hud_rows; r < term.rows() - log_rows; ++r)
        for (int c = 0; c < term.cols(); ++c)
            if (term.char_at(r, c) == ch)
                ++n;
    return n;
}

} // namespace

// --- Coordinate helper (kept from the original placeholder) --------------

TEST(CursesRenderer, world_to_tile_divides_by_grid_size)
{
    int tx = 0;
    int ty = 0;
    CursesRenderer::world_to_tile(0, 0, tx, ty);
    EXPECT_EQ(tx, 0);
    EXPECT_EQ(ty, 0);
    CursesRenderer::world_to_tile(GRID_SIZE * 3 + 7, GRID_SIZE * 5 + 2, tx, ty);
    EXPECT_EQ(tx, 3) << "nearest tile snaps via integer division by GRID_SIZE";
    EXPECT_EQ(ty, 5);
}

TEST(CursesRenderer, message_log_trims_to_capacity)
{
    CursesRenderer renderer(CursesRenderer::Options{true, true, /*max_log_lines=*/3});
    renderer.log_message("a");
    renderer.log_message("b");
    renderer.log_message("c");
    renderer.log_message("d");
    ASSERT_EQ(renderer.log().size(), 3u);
    EXPECT_EQ(renderer.log().front(), "b");
    EXPECT_EQ(renderer.log().back(), "d");
}

// --- Frame plumbing ------------------------------------------------------

TEST(CursesRenderer, draw_clears_and_presents)
{
    HandWorld hw(8, 8);
    HeadlessTerminal term(24, 40);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), /*followed_id=*/0);
    EXPECT_EQ(term.present_count(), 1) << "draw() must flush exactly once";
}

// --- Arena boundary / HUD / exit visibility (reported gameplay issues) ----

// Tiles beyond the level grid render as a distinct border, not grass, so the
// player can see where the arena ends (the engine reports out-of-range tiles as
// grass, which previously made the whole screen look like an open field).
TEST(CursesRenderer, draws_a_border_outside_the_grid)
{
    HandWorld hw(4, 4); // a tiny 4x4 arena inside a much larger viewport
    HeadlessTerminal term(12, 24);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), /*followed_id=*/0);

    EXPECT_GT(term.count_char(U'▓'), 0)
        << "out-of-bounds tiles draw the border glyph\n" << term.dump();
    EXPECT_GT(count_in_viewport(term, U'.'), 0)
        << "in-bounds grass still renders";
}

// The HUD names the followed character and shows its current special, so the
// player knows who they are and what casting will do.
TEST(CursesRenderer, hud_shows_character_name_and_current_special)
{
    HandWorld hw(20, 20);
    walker* hero = hw.add_creature(10, 10, FAMILY_SOLDIER, 0);
    hero->set_user(0);
    hero->set_current_special(1); // soldier special index 1 == "CHARGE"
    auto g = std::make_unique<guy>(FAMILY_SOLDIER);
    g->name = "Maximus";
    hero->set_owned_myguy(std::move(g));

    HeadlessTerminal term(24, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), hero->entity_id());

    EXPECT_NE(term.text_row(0).find("Maximus"), std::string::npos)
        << "row 0 names the followed character; got: " << term.text_row(0);
    EXPECT_NE(term.text_row(1).find("CHARGE"), std::string::npos)
        << "row 1 shows the current special; got: " << term.text_row(1);
}

// A level exit (Order::Treasure / FAMILY_EXIT) renders as '>' so the player can
// see where to leave once one exists.
TEST(CursesRenderer, exit_object_renders_as_marker)
{
    HandWorld hw(20, 20);
    walker* hero = hw.add_creature(10, 10, FAMILY_SOLDIER, 0);
    hero->set_user(0);
    hw.add_entity(Order::Treasure, FAMILY_EXIT, 12, 10, 0); // exit two tiles east

    HeadlessTerminal term(24, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), hero->entity_id());

    EXPECT_GT(count_in_viewport(term, U'>'), 0)
        << "the level exit renders as '>'\n" << term.dump();
}

// Effects (Order::FX in fxlist) are drawn — the boomerang the user fired must be
// visible. The renderer previously skipped fxlist entirely.
TEST(CursesRenderer, effect_in_fxlist_renders)
{
    HandWorld hw(20, 20);
    walker* hero = hw.add_creature(10, 10, FAMILY_SOLDIER, 0);
    hero->set_user(0);
    hw.add_effect(FAMILY_BOOMERANG, 12, 10); // a boomerang two tiles east

    HeadlessTerminal term(24, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), hero->entity_id());

    EXPECT_GT(count_in_viewport(term, U'%'), 0)
        << "the boomerang ('%') in fxlist must render\n" << term.dump();
}

// --- Tiles ---------------------------------------------------------------

TEST(CursesRenderer, viewport_fills_with_tile_glyphs)
{
    HandWorld hw(8, 8);
    HeadlessTerminal term(20, 30);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), /*followed_id=*/0);

    // An all-grass 8x8 map centered in a wider viewport: grass '.' in the middle,
    // a border '▓' in the off-grid margins (the renderer no longer paints
    // out-of-range tiles as grass — that hid the arena edge).
    EXPECT_GT(count_in_viewport(term, U'.'), 0) << "grass tiles should render";
    EXPECT_GT(term.count_char(U'▓'), 0) << "off-grid margin draws the border";
    // The viewport center is the map middle (an in-grid grass tile); the far-left
    // column is off-grid and draws the border.
    EXPECT_EQ(term.char_at(8, 15), U'.') << "viewport center is in-grid grass";
    EXPECT_EQ(term.char_at(8, 0), U'▓') << "left margin is the off-grid border";
}

TEST(CursesRenderer, painted_water_and_wall_tiles_show_their_glyphs)
{
    // Center the followed avatar so we know where the camera sits, then paint
    // distinctive tiles next to it and read them back at the expected cells.
    HandWorld hw(20, 20);
    hw.add_creature(10, 10, FAMILY_SOLDIER, /*team*/ 0);
    walker* hero = hw.world().oblist.front().get();
    const std::uint32_t id = hero->entity_id();

    hw.paint(11, 10, PIX_WATER1); // one tile right of the hero
    hw.paint(10, 9, PIX_WALL2);   // one tile above the hero

    HeadlessTerminal term(21, 41);
    term.set_unicode(false); // assert the ASCII glyphs
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    // The hero's tile maps to the viewport center. Viewport spans rows 2..14
    // (HUD=2, log=6, height=21-8=13), cols 0..40.
    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 41 / 2;

    EXPECT_EQ(term.char_at(center_row, center_col + 1), U'~') << "water to the right";
    EXPECT_EQ(term.char_at(center_row - 1, center_col), U'#') << "wall above";
}

TEST(CursesRenderer, ascii_fallback_downgrades_unicode_tiles)
{
    HandWorld hw(12, 12);
    hw.add_creature(6, 6, FAMILY_SOLDIER, 0);
    walker* hero = hw.world().oblist.front().get();
    const std::uint32_t id = hero->entity_id();
    hw.paint(7, 6, PIX_WATER1);

    HeadlessTerminal term(20, 30);
    term.set_unicode(false); // terminal cannot render wide glyphs

    CursesRenderer renderer; // options default allow_unicode=true
    renderer.draw(term, hw.world(), id);

    // With unicode off, water must be the ASCII '~', never the Unicode '≈'.
    EXPECT_GT(term.count_char(U'~'), 0) << "ASCII water present";
    EXPECT_EQ(term.count_char(U'≈'), 0) << "no Unicode water when unsupported";
}

TEST(CursesRenderer, option_can_force_ascii_even_on_unicode_terminal)
{
    HandWorld hw(12, 12);
    hw.add_creature(6, 6, FAMILY_SOLDIER, 0);
    walker* hero = hw.world().oblist.front().get();
    const std::uint32_t id = hero->entity_id();
    hw.paint(7, 6, PIX_WATER1);

    HeadlessTerminal term(20, 30); // unicode-capable by default
    CursesRenderer renderer(CursesRenderer::Options{/*unicode=*/false, true, 6});
    renderer.draw(term, hw.world(), id);

    EXPECT_GT(term.count_char(U'~'), 0);
    EXPECT_EQ(term.count_char(U'≈'), 0);
}

// --- Entities ------------------------------------------------------------

TEST(CursesRenderer, creature_renders_family_glyph_at_relative_cell)
{
    // Hero at tile (10,10) is the camera center. A thief two tiles right and one
    // tile down should land at center+(2 cols, 1 row) with the 't' glyph.
    HandWorld hw(24, 24);
    walker* hero = hw.add_creature(10, 10, FAMILY_SOLDIER, /*team*/ 0);
    const std::uint32_t id = hero->entity_id();
    hw.add_creature(12, 11, FAMILY_THIEF, /*team*/ 1);

    HeadlessTerminal term(21, 41);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 41 / 2;

    EXPECT_EQ(term.char_at(center_row + 1, center_col + 2), U't')
        << "thief glyph at the expected offset from the hero";
    // Enemy team 1 -> green; not bold (only my_team==0 creatures are bold).
    const Cell& c = term.cell_at(center_row + 1, center_col + 2);
    EXPECT_EQ(c.fg, Color::Green);
    EXPECT_FALSE(c.bold);
}

TEST(CursesRenderer, followed_avatar_renders_as_at_sign_bold)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_MAGE, /*team*/ 0);
    const std::uint32_t id = hero->entity_id();

    HeadlessTerminal term(21, 41);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 41 / 2;

    const Cell& c = term.cell_at(center_row, center_col);
    EXPECT_EQ(c.ch, U'@') << "followed avatar is '@' regardless of family";
    EXPECT_TRUE(c.bold);
    EXPECT_EQ(c.fg, Color::Red) << "team 0 is red";
    EXPECT_EQ(term.count_char(U'@'), 1) << "exactly one avatar";
    EXPECT_EQ(term.count_char(U'm'), 0) << "the mage glyph is replaced by '@'";
}

TEST(CursesRenderer, entities_overwrite_tiles)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();
    // Paint water under a second creature; the creature glyph must win.
    hw.paint(9, 8, PIX_WATER1);
    hw.add_creature(9, 8, FAMILY_ORC, /*team*/ 1);

    HeadlessTerminal term(21, 41);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 41 / 2;

    EXPECT_EQ(term.char_at(center_row, center_col + 1), U'o')
        << "entity overlay beats the terrain tile beneath it";
}

TEST(CursesRenderer, dead_entities_are_skipped)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();
    walker* corpse = hw.add_creature(10, 8, FAMILY_ORC, 1);
    corpse->set_dead(1);

    HeadlessTerminal term(21, 41);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    EXPECT_EQ(count_in_viewport(term, U'o'), 0) << "dead orc must not be drawn";
}

TEST(CursesRenderer, invisible_entities_are_skipped_but_avatar_is_not)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();
    hero->set_invisibility_left(50); // invisible, but it's us

    walker* ghost = hw.add_creature(10, 8, FAMILY_GHOST, 1);
    ghost->set_invisibility_left(50);

    HeadlessTerminal term(21, 41);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    EXPECT_EQ(term.count_char(U'@'), 1)
        << "the followed avatar is always visible, even when invisible";
    EXPECT_EQ(term.count_char(U'g'), 0)
        << "an invisible enemy is hidden";
}

TEST(CursesRenderer, entities_outside_the_viewport_are_clipped)
{
    HandWorld hw(60, 60);
    walker* hero = hw.add_creature(30, 30, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();
    // Far away, well outside any reasonable viewport around the hero.
    hw.add_creature(58, 58, FAMILY_ORC, 1);

    HeadlessTerminal term(21, 41);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    EXPECT_EQ(count_in_viewport(term, U'o'), 0) << "off-screen creature is not drawn";
    EXPECT_EQ(term.count_char(U'@'), 1) << "but the centered hero is";
}

TEST(CursesRenderer, weapons_are_drawn_over_oblist)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();

    // A flying rock one tile to the right. The hand-built factory makes bare
    // walkers, so stamp the order/family the production factory would apply.
    walker* rock = hw.world().add_weap_ob(Order::Weapon, FAMILY_ROCK);
    ASSERT_NE(rock, nullptr);
    rock->set_order(Order::Weapon);
    rock->set_family(static_cast<char>(FAMILY_ROCK));
    rock->setxy(static_cast<short>(9 * GRID_SIZE), static_cast<short>(8 * GRID_SIZE));

    HeadlessTerminal term(21, 41);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 41 / 2;

    EXPECT_EQ(term.char_at(center_row, center_col + 1), U'*') << "rock weapon glyph";
}

// Non-living entities (treasure, generators) render with their own glyphs at the
// correct camera-relative cells: an exit '>', a gold bar '$', and a tower
// generator 'T', each placed at a known offset from the followed hero.
TEST(CursesRenderer, treasure_and_generator_glyphs_render_in_viewport)
{
    HandWorld hw(24, 24);
    walker* hero = hw.add_creature(10, 10, FAMILY_SOLDIER, /*team*/ 0);
    const std::uint32_t id = hero->entity_id();

    // Exit one tile right, gold bar two tiles right, tower one tile down.
    hw.add_entity(Order::Treasure, FAMILY_EXIT, 11, 10, /*team*/ 0);
    hw.add_entity(Order::Treasure, FAMILY_GOLD_BAR, 12, 10, /*team*/ 0);
    hw.add_entity(Order::Generator, FAMILY_TOWER, 10, 11, /*team*/ 1);

    HeadlessTerminal term(21, 41);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    // Same camera math the creature test uses: HUD=2 top rows, log=6 bottom rows,
    // viewport centered on the hero's tile.
    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 41 / 2;

    EXPECT_EQ(term.char_at(center_row, center_col + 1), U'>')
        << "exit portal renders as '>'";
    EXPECT_EQ(term.char_at(center_row, center_col + 2), U'$')
        << "gold bar renders as '$'";
    EXPECT_EQ(term.char_at(center_row + 1, center_col), U'T')
        << "tower generator renders as 'T'";
}

// With color disabled at the terminal, every glyph is drawn with Color::Default
// (the terminal cannot color) while the characters themselves are unchanged.
TEST(CursesRenderer, no_color_terminal_renders_default_fg_but_keeps_glyphs)
{
    HandWorld hw(24, 24);
    walker* hero = hw.add_creature(10, 10, FAMILY_SOLDIER, /*team*/ 0);
    const std::uint32_t id = hero->entity_id();
    hw.add_creature(12, 10, FAMILY_ORC, /*team*/ 1); // an enemy two tiles right

    HeadlessTerminal term(21, 41);
    term.set_color(false); // terminal has no color support
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 41 / 2;

    // The avatar cell: '@' glyph kept (bold preserved), but fg falls back to Default.
    const Cell& avatar = term.cell_at(center_row, center_col);
    EXPECT_EQ(avatar.ch, U'@') << "avatar glyph unchanged with color off";
    EXPECT_EQ(avatar.fg, Color::Default) << "no color -> Default fg on the avatar";

    // The enemy cell: 'o' glyph kept, fg Default (not the team-1 green).
    const Cell& enemy = term.cell_at(center_row, center_col + 2);
    EXPECT_EQ(enemy.ch, U'o') << "enemy glyph unchanged with color off";
    EXPECT_EQ(enemy.fg, Color::Default) << "no color -> Default fg on the enemy";
}

// Same expectation, but driven by the renderer option allow_color=false on an
// otherwise color-capable terminal (the other side of the color toggle).
TEST(CursesRenderer, option_can_force_no_color_even_on_color_terminal)
{
    HandWorld hw(24, 24);
    walker* hero = hw.add_creature(10, 10, FAMILY_SOLDIER, /*team*/ 0);
    const std::uint32_t id = hero->entity_id();
    hw.add_creature(12, 10, FAMILY_ORC, /*team*/ 1);

    HeadlessTerminal term(21, 41); // color-capable by default
    CursesRenderer renderer(CursesRenderer::Options{/*unicode=*/true,
                                                    /*color=*/false, 6});
    renderer.draw(term, hw.world(), id);

    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 41 / 2;

    const Cell& avatar = term.cell_at(center_row, center_col);
    EXPECT_EQ(avatar.ch, U'@');
    EXPECT_EQ(avatar.fg, Color::Default) << "allow_color=false -> Default fg";

    const Cell& enemy = term.cell_at(center_row, center_col + 2);
    EXPECT_EQ(enemy.ch, U'o');
    EXPECT_EQ(enemy.fg, Color::Default) << "allow_color=false -> Default fg";
}

// --- HUD -----------------------------------------------------------------

TEST(CursesRenderer, hud_shows_followed_name_hp_and_title)
{
    HandWorld hw(16, 16);
    hw.world().title = "Test Arena";
    walker* hero = hw.add_creature(8, 8, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();

    auto g = std::make_unique<guy>();
    g->name = "Conan";
    hero->set_owned_myguy(std::move(g));
    hero->stats()->set_hitpoints(42.0f);
    hero->stats()->set_max_hitpoints(80.0f);
    hero->stats()->set_level(3);

    HeadlessTerminal term(21, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    const std::string row0 = term.text_row(0);
    EXPECT_NE(row0.find("Conan"), std::string::npos) << "name on HUD line 0: " << row0;
    EXPECT_NE(row0.find("Test Arena"), std::string::npos)
        << "level title on HUD line 0: " << row0;

    const std::string row1 = term.text_row(1);
    EXPECT_NE(row1.find("HP 42/80"), std::string::npos) << "HP on HUD line 1: " << row1;
    EXPECT_NE(row1.find("Lv 3"), std::string::npos) << "level on HUD line 1: " << row1;
}

TEST(CursesRenderer, hud_uses_family_label_when_no_guy_name)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_ARCHER, 0);
    const std::uint32_t id = hero->entity_id();
    // No myguy: HUD should fall back to a family label.

    HeadlessTerminal term(21, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    const std::string row0 = term.text_row(0);
    EXPECT_NE(row0.find("ARCHER"), std::string::npos)
        << "family label when no guy name: " << row0;
}

TEST(CursesRenderer, hud_shows_team_score)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();
    hw.world().my_team = 0;
    hw.world().m_score[0] = 1234;

    HeadlessTerminal term(21, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    const std::string row1 = term.text_row(1);
    EXPECT_NE(row1.find("1234"), std::string::npos)
        << "team score on HUD line 1: " << row1;
}

TEST(CursesRenderer, hud_shows_mp_only_when_present)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_MAGE, 0);
    const std::uint32_t id = hero->entity_id();
    hero->stats()->set_hitpoints(30.0f);
    hero->stats()->set_max_hitpoints(30.0f);
    hero->stats()->set_magicpoints(15.0f);
    hero->stats()->set_max_magicpoints(40.0f);

    HeadlessTerminal term(21, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);

    EXPECT_NE(term.text_row(1).find("MP 15/40"), std::string::npos)
        << "mage shows MP: " << term.text_row(1);
}

TEST(CursesRenderer, hud_with_no_followed_entity_says_spectating)
{
    HandWorld hw(16, 16);
    HeadlessTerminal term(21, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), /*followed_id=*/0);

    EXPECT_NE(term.text_row(0).find("spectating"), std::string::npos)
        << "no avatar -> spectating banner: " << term.text_row(0);
}

// A NON-ZERO followed id whose entity has been removed (the player's avatar died
// and was reaped) must fall back to spectator mode: the HUD shows "(spectating)"
// and the camera recenters on the map middle. This is distinct from the
// followed_id==0 case above, which takes a special-cased early branch — here a
// real, plausible id simply no longer resolves in the oblist.
TEST(CursesRenderer, dead_or_removed_avatar_falls_back_to_spectator)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(2, 2, FAMILY_SOLDIER, /*team*/ 0);
    const std::uint32_t id = hero->entity_id();
    ASSERT_NE(id, 0u);
    // A bystander elsewhere so the world is not empty after the avatar is reaped.
    hw.add_creature(13, 13, FAMILY_ORC, /*team*/ 1);

    // Reap the avatar: remove_ob erases it from the oblist and the id index, so
    // the renderer's followed-id lookup now misses for a still-non-zero id.
    ASSERT_EQ(hw.world().remove_ob(hero), 1);
    ASSERT_EQ(hw.world().find_by_id(id), nullptr) << "the avatar id no longer resolves";

    HeadlessTerminal term(21, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id); // non-zero, but absent

    EXPECT_NE(term.text_row(0).find("spectating"), std::string::npos)
        << "an unresolved avatar id -> spectating banner: " << term.text_row(0);
    EXPECT_EQ(term.count_char(U'@'), 0)
        << "no avatar glyph when the followed id does not resolve";

    // Camera recentered on the map middle (tile (8,8) of a 16x16 all-grass map):
    // the viewport-center cell is grass, not a clipped/blank edge.
    const int vp_top = 2;
    const int vp_h = 21 - 2 - 6;
    const int center_row = vp_top + vp_h / 2;
    const int center_col = 60 / 2;
    EXPECT_EQ(term.char_at(center_row, center_col), U'.')
        << "spectator camera centers on the (grass) map middle";
}

// --- Message log ---------------------------------------------------------

TEST(CursesRenderer, message_log_renders_recent_lines)
{
    HandWorld hw(16, 16);
    HeadlessTerminal term(21, 40);
    CursesRenderer renderer(CursesRenderer::Options{true, true, /*max_log_lines=*/4});
    renderer.log_message("first event");
    renderer.log_message("second event");
    renderer.draw(term, hw.world(), 0);

    // Log occupies the last 4 rows (17..20). The two messages appear there.
    bool found_first = false;
    bool found_second = false;
    for (int r = 0; r < term.rows(); ++r) {
        const std::string row = term.text_row(r);
        if (row.find("first event") != std::string::npos) found_first = true;
        if (row.find("second event") != std::string::npos) found_second = true;
    }
    EXPECT_TRUE(found_first) << "log line 'first event' should render";
    EXPECT_TRUE(found_second) << "log line 'second event' should render";
}

TEST(CursesRenderer, message_log_shows_only_the_newest_lines_that_fit)
{
    HandWorld hw(16, 16);
    // Log region capped to 2 rows even though more lines are buffered.
    HeadlessTerminal term(21, 40);
    CursesRenderer renderer(CursesRenderer::Options{true, true, /*max_log_lines=*/2});
    renderer.log_message("oldest");
    renderer.log_message("middle");
    renderer.log_message("newest");
    renderer.draw(term, hw.world(), 0);

    bool found_oldest = false;
    bool found_newest = false;
    for (int r = 0; r < term.rows(); ++r) {
        const std::string row = term.text_row(r);
        if (row.find("oldest") != std::string::npos) found_oldest = true;
        if (row.find("newest") != std::string::npos) found_newest = true;
    }
    EXPECT_FALSE(found_oldest) << "the oldest line is trimmed past capacity";
    EXPECT_TRUE(found_newest) << "the newest line is always shown";
}

// --- Robustness ----------------------------------------------------------

TEST(CursesRenderer, tiny_terminal_does_not_crash_or_overflow)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();

    // A handful of degenerate sizes. The renderer must not divide by zero or
    // produce negative loop bounds; HeadlessTerminal::put() drops any stray
    // out-of-bounds write, so a clean present() confirms it stayed alive.
    for (auto dims : {std::pair<int, int>{1, 1}, {2, 3}, {3, 2}, {1, 40}, {40, 1}}) {
        HeadlessTerminal term(dims.first, dims.second);
        CursesRenderer renderer;
        renderer.draw(term, hw.world(), id); // must not crash
        EXPECT_EQ(term.present_count(), 1);
    }
}

TEST(CursesRenderer, zero_log_lines_gives_all_space_to_viewport)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_creature(8, 8, FAMILY_SOLDIER, 0);
    const std::uint32_t id = hero->entity_id();

    HeadlessTerminal term(10, 20);
    CursesRenderer renderer(CursesRenderer::Options{true, true, /*max_log_lines=*/0});
    renderer.draw(term, hw.world(), id);

    // The avatar still renders; the last row is viewport, not log.
    EXPECT_EQ(term.count_char(U'@'), 1);
}

// --- Integration smoke test: real level 1 -------------------------------

TEST(CursesRenderer, renders_real_level_one_without_crashing)
{
    og::sim::SimEventLog events;
    LevelRuntimeData level(1, /*headless=*/true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    SaveData save;
    save.scen_num = 1;
    save.numplayers = 1;
    level.set_sim_context(&save, &world.enemy_freeze, &events, &world.rng_, &cfg);
    ASSERT_TRUE(level.load()) << "level 1 should load headlessly";

    // Spawn one player-controlled hero and follow it.
    walker* hero = level.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(hero, nullptr);
    hero->setxy(static_cast<short>(5 * GRID_SIZE), static_cast<short>(5 * GRID_SIZE));
    hero->set_team_num(0);
    hero->set_user(0);
    auto g = std::make_unique<guy>();
    g->name = "Hero";
    hero->set_owned_myguy(std::move(g));
    world.my_team = 0;

    HeadlessTerminal term(24, 80);
    CursesRenderer renderer;
    renderer.draw(term, world, hero->entity_id());

    EXPECT_EQ(term.present_count(), 1);
    EXPECT_EQ(term.count_char(U'@'), 1) << "the followed hero is on screen";
    EXPECT_NE(term.text_row(0).find("Hero"), std::string::npos)
        << "HUD names the hero: " << term.text_row(0);

    level.delete_objects();
}
