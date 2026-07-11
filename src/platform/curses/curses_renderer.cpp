/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/curses/curses_renderer.h>

#include <openglad/platform/curses/glyph_map.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/ui/picker_common.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace og::curses {

namespace {

// Number of HUD rows we draw at the top of the screen. The first line names the
// followed character; the second shows numeric stats. Both are dropped on very
// short terminals (see compute_layout).
constexpr int kHudRows = 2;

// Resolve the abstract glyph color through the renderer's color toggle and the
// terminal's own capability. When color is off everything is Color::Default.
Color resolve_color(Color fg, bool allow_color)
{
    return allow_color ? fg : Color::Default;
}

// Best display name for the followed walker: the character's name if it has one
// (myguy, then stats), else a family label (e.g. "SOLDIER"). Never returns empty.
std::string followed_name(const walker* w)
{
    const std::string_view name = entity_display_name(w);
    if (!name.empty())
        return std::string(name);
    return og::ui::family_display_name(w->family());
}

// Name of the walker's currently selected special ability, or "" if it has none
// (family with no specials, or the special slot is empty/"NONE").
std::string current_special_name(const walker* w)
{
    const int sp = static_cast<int>(w->current_special());
    if (sp < 0 || sp >= FD_NUM_SPECIALS)
        return {};
    const FamilyDescriptor* fd = get_family_descriptor(w->family());
    if (!fd)
        return {};
    const char* name = fd->special_names[sp];
    if (!name || name[0] == '\0' || std::strcmp(name, "NONE") == 0)
        return {};
    return name;
}

} // namespace

CursesRenderer::CursesRenderer() = default;
CursesRenderer::CursesRenderer(Options opt) : opt_(opt) {}

void CursesRenderer::log_message(std::string line)
{
    log_.push_back(std::move(line));
    while (static_cast<int>(log_.size()) > opt_.max_log_lines)
        log_.pop_front();
}

void CursesRenderer::clear_log()
{
    log_.clear();
}

void CursesRenderer::world_to_tile(int xpos, int ypos, int& tile_x, int& tile_y)
{
    tile_x = xpos / GRID_SIZE;
    tile_y = ypos / GRID_SIZE;
}

void CursesRenderer::draw(ITerminal& term, const GameWorld& world,
                          std::uint32_t followed_id)
{
    term.clear();

    const int rows = term.rows();
    const int cols = term.cols();

    // --- Layout ---------------------------------------------------------
    // Top:    HUD (up to kHudRows lines, dropped on tiny terminals).
    // Bottom: message log (up to max_log_lines, clamped to leave the viewport).
    // Middle: the viewport, which takes whatever rows remain.
    int hud_rows = (rows >= kHudRows + 1) ? kHudRows : ((rows >= 2) ? 1 : 0);

    int log_rows = std::max(0, opt_.max_log_lines);
    // Always keep at least one viewport row between HUD and log.
    const int max_log = std::max(0, rows - hud_rows - 1);
    log_rows = std::min(log_rows, max_log);

    const int viewport_top = hud_rows;
    const int viewport_height = std::max(0, rows - hud_rows - log_rows);
    const int log_top = viewport_top + viewport_height;

    if (viewport_height > 0 && cols > 0)
        draw_viewport(term, world, followed_id, viewport_top, 0, viewport_height, cols);

    if (hud_rows > 0 && cols > 0)
        draw_hud(term, world, followed_id, 0, 0, cols);

    if (log_rows > 0 && cols > 0)
        draw_log(term, log_top, 0, log_rows, cols);

    term.present();
}

void CursesRenderer::draw_viewport(ITerminal& term, const GameWorld& world,
                                   std::uint32_t followed_id,
                                   int top, int left, int height, int width)
{
    const bool allow_unicode = opt_.allow_unicode && term.supports_unicode();
    const bool allow_color = opt_.allow_color && term.supports_color();

    // --- Camera: center the viewport on the followed walker's tile ------
    int center_tx = 0;
    int center_ty = 0;
    const walker* followed = nullptr;
    if (followed_id != 0) {
        for (const auto& up : world.oblist) {
            const walker* w = up.get();
            if (w && w->entity_id() == followed_id) {
                followed = w;
                break;
            }
        }
    }
    if (followed) {
        world_to_tile(followed->xpos(), followed->ypos(), center_tx, center_ty);
    } else {
        // No followed entity: center on the map middle (or origin if unknown).
        world_to_tile(world.pixmaxx / 2, world.pixmaxy / 2, center_tx, center_ty);
    }

    // Multi-floor: render the followed walker's floor (terrain + entities).
    const int cam_floor = followed ? static_cast<int>(followed->floor()) : 0;

    // Top-left tile of the viewport, so the camera tile lands in the middle.
    const int cam_x = center_tx - width / 2;
    const int cam_y = center_ty - height / 2;

    // --- Tiles ----------------------------------------------------------
    // query_genre_x_y() is total: out-of-range tiles report grass, which would
    // hide the arena edge. So when the grid size is known, tiles outside
    // [0,grid.w) x [0,grid.h) are drawn as a distinct border instead.
    smoother& sm = const_cast<GameWorld&>(world).smoother_for_floor(cam_floor);
    const PixieData& cam_grid = world.grid_for_floor(cam_floor);
    const int grid_w = cam_grid.w;
    const int grid_h = cam_grid.h;
    const bool have_bounds = grid_w > 0 && grid_h > 0;
    for (int row = 0; row < height; ++row) {
        const int ty = cam_y + row;
        for (int col = 0; col < width; ++col) {
            const int tx = cam_x + col;
            const bool outside =
                have_bounds && (tx < 0 || ty < 0 || tx >= grid_w || ty >= grid_h);
            Glyph g;
            if (outside) {
                g = border_glyph();
            } else {
                const int genre = sm.query_genre_x_y(tx, ty);
                // Z-stairs: the genre is direction-less, but the raw tile id
                // knows which way they go — show '<' (up) vs '>' (down).
                if (genre == TYPE_ZSTAIRS && cam_grid.valid() &&
                    tx >= 0 && ty >= 0 && tx < grid_w && ty < grid_h) {
                    const unsigned char pix =
                        cam_grid.data[tx + grid_w * ty];
                    g = zstair_glyph(pix == PIX_ZSTAIR_UP);
                } else {
                    g = tile_glyph(genre);
                }
            }
            if (g.skip)
                continue;
            term.put(top + row, left + col, g.pick(allow_unicode),
                     resolve_color(g.fg, allow_color), Color::Default, g.bold);
        }
    }

    // --- Entities -------------------------------------------------------
    // Draw order (bottom to top): fxlist (effects: boomerang, magic shield,
    // explosions — the visible part of specials), then oblist (creatures), then
    // weaplist. Effects sit under creatures so the followed '@' is never hidden by
    // one; a projectile in weaplist sits on top so a shot in flight is always
    // visible.
    const auto draw_entities = [&](const GameWorld::EntityList& list) {
        for (const auto& up : list) {
            const walker* w = up.get();
            // dormant(): a delayed spawn not in the world yet (mirror worlds
            // never contain one — snapshots exclude them — but a locally
            // rendered authoritative world must hide them too).
            if (!w || w->dead() || w->dormant())
                continue;

            const bool is_followed = (followed_id != 0 && w->entity_id() == followed_id);

            // Invisible dudes vanish from the map, except the player's own
            // followed avatar (you always see yourself).
            if (w->invisibility_left() > 0 && !is_followed)
                continue;

            // Multi-floor: only entities on the followed walker's floor show.
            if (world.floor_count() > 1 && !is_followed &&
                static_cast<int>(w->floor()) != cam_floor)
                continue;

            int tx = 0;
            int ty = 0;
            world_to_tile(w->xpos(), w->ypos(), tx, ty);
            const int col = tx - cam_x;
            const int row = ty - cam_y;
            if (row < 0 || row >= height || col < 0 || col >= width)
                continue;

            const Glyph g = entity_glyph(w->query_order(), w->family(), w->team_num(),
                                         is_followed,
                                         static_cast<unsigned char>(world.my_team));
            if (g.skip)
                continue;
            term.put(top + row, left + col, g.pick(allow_unicode),
                     resolve_color(g.fg, allow_color), Color::Default, g.bold);
        }
    };
    draw_entities(world.fxlist);
    draw_entities(world.oblist);
    draw_entities(world.weaplist);
}

void CursesRenderer::draw_hud(ITerminal& term, const GameWorld& world,
                              std::uint32_t followed_id, int top, int left, int width)
{
    const bool allow_color = opt_.allow_color && term.supports_color();

    const walker* followed = nullptr;
    if (followed_id != 0) {
        for (const auto& up : world.oblist) {
            const walker* w = up.get();
            if (w && w->entity_id() == followed_id) {
                followed = w;
                break;
            }
        }
    }

    // Truncate a string to the HUD width so put_str never spills past the edge.
    const auto clip = [&](std::string s) {
        if (static_cast<int>(s.size()) > width)
            s.resize(static_cast<std::size_t>(std::max(0, width)));
        return s;
    };

    // --- Line 0: character name + team color, then the level title. -----
    std::string line0;
    Color name_color = Color::Default;
    if (followed) {
        line0 = followed_name(followed);
        name_color = resolve_color(team_color(followed->team_num()), allow_color);
    } else {
        line0 = "(spectating)";
    }
    if (!world.title.empty()) {
        line0 += "  -  ";
        line0 += world.title;
    }
    term.put_str(top, left, clip(line0), name_color, Color::Default, true);

    // --- Line 1: HP / MP / level / CTF status / special / score. Only drawn
    // when the layout actually reserved a second HUD row (see compute_layout
    // in draw()); otherwise it would clobber the viewport on a 1-row-HUD
    // terminal. Fields are appended in priority order so an 80-column clip()
    // eats Score and Sp before the CTF caps/FLAG/RESPAWN status.
    if (term.rows() >= kHudRows + 1) {
        std::string line1;
        if (followed && followed->stats()) {
            const statistics* st = followed->stats();
            line1 += "HP " + std::to_string(static_cast<int>(st->hitpoints())) + "/" +
                     std::to_string(static_cast<int>(st->max_hitpoints()));
            if (st->max_magicpoints() > 0.0f) {
                line1 += "  MP " + std::to_string(static_cast<int>(st->magicpoints())) + "/" +
                         std::to_string(static_cast<int>(st->max_magicpoints()));
            }
            line1 += "  Lv " + std::to_string(static_cast<int>(st->level()));
        }
        if (world.ctf.active) {
            // Per-team capture counts (replicated CtfState; the mirror world
            // carries it in every snapshot). Active teams only, matching the
            // SDL panel's filter.
            line1 += "  Caps ";
            bool first = true;
            for (int t = 0; t < 4; ++t) {
                if (!world.ctf.team_active[t])
                    continue;
                if (!first)
                    line1 += ":";
                first = false;
                line1 += std::to_string(world.ctf.captures[t]);
            }
            if (followed_id != 0) {
                for (int t = 0; t < 4; ++t) {
                    const auto& flag = world.ctf.flags[t];
                    if (flag.state == og::sim::CtfFlagState::Carried &&
                        flag.carrier_entity_id == followed_id) {
                        line1 += "  FLAG";
                        break;
                    }
                }
                for (const auto& entry : world.ctf.respawn_queue) {
                    if (entry.kind == 0 &&
                        entry.walker_entity_id == followed_id) {
                        line1 += "  RESPAWN " + std::to_string(
                            og::sim::ctf_respawn_seconds_left(world.ctf,
                                                              entry));
                        break;
                    }
                }
            }
            // Waypoint capture meter: first contested control point in index
            // order, mirroring the SDL panel's "WP n/36" readout. Rides the
            // CTF group so narrow terminals clip Sp:/Score first.
            for (int i = 0; i < world.ctf.cp_count; ++i) {
                const auto& cp = world.ctf.cps[i];
                if (cp.progress_team < 0)
                    continue;
                line1 += "  WP " + std::to_string(cp.progress) + "/" +
                         std::to_string(og::sim::kCtfCpCaptureTicks);
                break;
            }
        }
        // Currently selected special (Tab/SwitchSpecial cycles it). Shown so the
        // player can see what casting fire/special will do.
        if (followed) {
            const std::string special = current_special_name(followed);
            if (!special.empty())
                line1 += "  Sp:" + special;
        }
        const int team = std::clamp(static_cast<int>(world.my_team), 0, 3);
        line1 += "  Score " + std::to_string(world.m_score[team]);
        term.put_str(top + 1, left, clip(line1), Color::Default, Color::Default, false);
    }
}

void CursesRenderer::draw_log(ITerminal& term, int top, int left, int height, int width)
{
    // Show the most recent `height` lines, oldest at the top of the region.
    const int count = std::min(height, static_cast<int>(log_.size()));
    const int start = static_cast<int>(log_.size()) - count;
    for (int i = 0; i < count; ++i) {
        std::string line = log_[static_cast<std::size_t>(start + i)];
        if (static_cast<int>(line.size()) > width)
            line.resize(static_cast<std::size_t>(std::max(0, width)));
        term.put_str(top + i, left, line, Color::Default, Color::Default, false);
    }
}

} // namespace og::curses
