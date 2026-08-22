/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Extracted from src/glad.cpp so runtime code (game_loop) can link score_panel()
 * without depending on the app translation unit (which also defines main()).
 *
 * This file intentionally keeps the legacy global functions/signatures.
 * Follow-up work should move HUD rendering into a proper module namespace.
 */

#include <openglad/interface/base.h>
#include <openglad/interface/fps_overlay.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/session_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>

#include <cstring>

#include <openglad/interface/game_context.h>

#include <openglad/core/test_trace.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

// HUD-only randomness (the score count-up easing below), drawn once per
// rendered frame: this is the session-scope RNG — a per-session SeededRandom in
// demo sessions, the production RNG in the main game. It must never be the
// authoritative world.rng_ gameplay stream, which gameplay draws reach through
// walker_rng() (that prefers world->rng_ whenever gameplay is active). The two
// streams staying distinct is what lets the panel redraw at any frame rate
// without moving a single gameplay outcome.
static inline Uint32 rng(Uint32 max_exclusive)
{
    return ctx().rng->next(max_exclusive);
}

static bool is_valid_score_team(unsigned char team_num)
{
    return team_num < SCORE_TEAM_COUNT;
}

bool float_eq(float a, float b);

short remaining_foes(screen* s, walker* myguy);
short remaining_team(screen* s, char myteam);

// B5: dormant (delayed-spawn) hostiles pending for this viewer, plus the sim
// ticks until the earliest of them wakes (wake rule in the act phase:
// level_tick_count > spawn_delay). Snapshot mirrors keep their own level-load
// copies of dormant walkers (reconcile never erases them), so this is
// readable render-side in every mode. Non-static for the HUD tests.
//
// pending_respawn adds the classic-respawn engine's hostile queue entries
// (difficulty respawns): a foe merely awaiting its respawn timer blocks the
// extermination win exactly like a dormant delayed spawn does
// (classic_respawn_pending_hostile_foe), so without it the end of the level
// reads "FOES: 0" over an empty map that refuses to finish. Unlike dormant
// walkers these are corpses — remaining_foes does not count them — so they
// belong in the (+x) display and the countdown but NOT in the
// awake = total - pending subtraction. The queue is snapshot-synced, so
// mirrors read it the same way.
void pending_hostile_wave_counts(const GameWorld& world, walker* viewer,
                                 short& pending, short& pending_respawn,
                                 std::uint32_t& next_wake_ticks)
{
    pending = 0;
    pending_respawn = 0;
    next_wake_ticks = 0;
    if (viewer == nullptr)
        return;

    const std::uint32_t now = world.level_tick_count();
    bool have_min = false;
    std::uint32_t min_ticks = 0;
    const auto fold_min = [&](std::uint32_t remaining) {
        if (!have_min || remaining < min_ticks)
        {
            have_min = true;
            min_ticks = remaining;
        }
    };
    for (const auto& uptr : world.oblist)
    {
        walker* const w = uptr.get();
        if (w == nullptr || w->dead() || !w->dormant() ||
            w->query_order() != Order::Living || viewer->is_friendly(w))
            continue;

        pending = static_cast<short>(pending + 1);
        const std::uint32_t wake_tick =
            static_cast<std::uint32_t>(w->spawn_delay()) + 1u;
        fold_min(wake_tick > now ? wake_tick - now : 0u);
    }

    if (og::sim::classic_respawn_active(world))
    {
        const unsigned char viewer_team = viewer->team_num();
        for (const og::sim::RespawnEntry& entry : world.respawn.respawn_queue)
        {
            // Viewer-relative mirror of the engine's strict team-color rule.
            // entry.team is the corpse's true (charm-broken) team recorded
            // at schedule time; company ownership never changes hostility.
            if (entry.team == viewer_team)
                continue;
            pending_respawn = static_cast<short>(pending_respawn + 1);
            fold_min(entry.ticks_left);
        }
    }
    next_wake_ticks = min_ticks;
}

short score_panel(screen* s, short do_it);

void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor,
                         short somelength, screen* s);

// Scared/fleeing countdown source: an external fright — the ghost's SCARE
// special or a shove — force-queues a COMMAND_WALK the walker must run off
// before the player regains control (living::act consumes queued commands
// before the ACT_CONTROL branch). Player-controlled walkers never queue
// their own commands, so a front walk command IS the fleeing state, and its
// commandcount is the sim ticks left until the player is back in charge.
// Read-only render-side probe. Non-static for the HUD tests.
int hud_scared_flee_ticks(walker* viewer)
{
    if (viewer == nullptr || viewer->dead())
        return 0;
    statistics* const st = viewer->stats();
    if (st == nullptr || st->commands.empty())
        return 0;
    const command& front = st->commands.front();
    if (front.commandtype != COMMAND_WALK)
        return 0;
    return front.commandcount > 0 ? static_cast<int>(front.commandcount) : 0;
}

void new_draw_value_bar(Sint32 left, Sint32 top,
                        walker* control, short mode, screen* s)
{
    float points;
    Sint32 bar_length = 0;
    unsigned char whatcolor = 0;

    if (mode == 0) // hitpoint bar
    {
        points = control->stats()->hitpoints();

        if (float_eq(points, control->stats()->max_hitpoints()))
            whatcolor = MAX_HP_COLOR;
        else if ((points * 3) < control->stats()->max_hitpoints())
            whatcolor = LOW_HP_COLOR;
        else if ((points * 3 / 2) < control->stats()->max_hitpoints())
            whatcolor = MID_HP_COLOR;
        else if (points < control->stats()->max_hitpoints())
            whatcolor = HIGH_HP_COLOR;
        else
            whatcolor = ORANGE_START;

        if (points > control->stats()->max_hitpoints())
            bar_length = 60;
        else
            bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_hitpoints()));

        draw_percentage_bar(left, top, BAR_BACK_COLOR, 60, s);
        draw_percentage_bar(left, top, whatcolor, static_cast<short>(bar_length), s);
    }  // end of doing hp stuff..
    else if (mode == 1) // sp stuff ..
    {
        points = control->stats()->magicpoints();

        if (float_eq(points, control->stats()->max_magicpoints()))
            whatcolor = MAX_MP_COLOR;
        else if ((points * 3) < control->stats()->max_magicpoints())
            whatcolor = LOW_MP_COLOR;
        else if ((points * 3 / 2) < control->stats()->max_magicpoints())
            whatcolor = MID_MP_COLOR;
        else if (points < control->stats()->max_magicpoints())
            whatcolor = HIGH_MP_COLOR;
        else
            whatcolor = WATER_START;

        if (points > control->stats()->max_magicpoints())
            bar_length = 60;
        else
            bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_magicpoints()));

        draw_percentage_bar(left, top, BAR_BACK_COLOR, 60, s);
        draw_percentage_bar(left, top, whatcolor, static_cast<short>(bar_length), s);
    } // end of sp stuff
} // end of drawing routine ..

#ifdef REDUCE_OVERSCAN
inline constexpr int OVERSCAN_PADDING = 6;
#else
inline constexpr int OVERSCAN_PADDING = 0;
#endif

// Shared classic/scripted respawn countdown; scripted modes surface their
// scores through the generic ModeState HUD lines.
static void draw_respawn_countdown(screen* s, walker* control,
                                   Sint32 lm, Sint32 tm)
{
    if (control == nullptr || !control->dead())
        return;

    for (const og::sim::RespawnEntry& entry :
         s->world_.respawn.respawn_queue)
    {
        if (entry.kind != 0 || entry.walker_entity_id != control->entity_id())
            continue;
        const int seconds = og::sim::respawn_seconds_left(entry);
        const std::string message = std::format("RESPAWN IN {}", seconds);
        s->text_normal.write_xy(lm + 4, tm + 12, message.c_str(),
                                static_cast<unsigned char>(YELLOW),
                                static_cast<short>(1));
        break;
    }
}

// Frozen-time countdown (#232). enemy_freeze rides the world snapshot, so a
// client reads it straight off its mirror — no notification traffic. Drawn
// every frame at a fixed left-anchored cell instead of pushed into the feed,
// which the old per-tick "TIME LEFT" notification flooded out of existence.
//
// Row. The classic HUD owns the top-left column (tm+2..tm+26, name + HP/MP)
// and the bottom-left score/special box (bm-26..bm-2), which leaves bm-34 —
// but only in a pane tall enough for it. The notification banner owns
// viewport-local rows 30..59 no matter how short the pane is, and the legacy
// PREF_VIEW inset modes cut a 1p pane to 80 scanlines and a 2p/3p pane to 72,
// where bm-34 lands INSIDE the feed. So the row is clamped below the banner,
// and a pane with no clear row between banner and score box draws nothing.
//
// Column. The radar is the real constraint: a fixed 60x44 block anchored to
// the bottom-right corner (rm-64..rm across bm-48..bm-2), the same band this
// cell rides. A pane narrower than ~150px cannot hold the full string left of
// it, so the cell shortens and then gives up. The block is reserved even for
// a PREF_RADAR_OFF view, so a seat toggling its radar never moves the cell.
static void draw_freeze_countdown(screen* s, Sint32 lm, Sint32 tm, Sint32 rm,
                                  Sint32 bm)
{
    if (s->world_.enemy_freeze <= 0)
        return;

    // First 6px row clear of the notification banner.
    const Sint32 banner_bottom = tm + 30 + MAX_MESSAGES * 6;
    const Sint32 row = std::max<Sint32>(bm - 34, banner_bottom);
    if (row + 6 > bm - 26)
    {
        TRACE("hud", "freeze_countdown_suppressed row=%d",
              static_cast<int>(row));
        return;
    }

    const Sint32 x = lm + 4;
    // The radar is only in the way while the cell shares its rows.
    const Sint32 right_limit = (row + 6 > bm - 48) ? (rm - 64) : rm;
    const auto fits = [&](const std::string& text) {
        return x + 6 * static_cast<Sint32>(text.size()) <= right_limit;
    };

    std::string message = std::format("TIME LEFT: {}", s->world_.enemy_freeze);
    if (!fits(message))
        message = std::format("T: {}", s->world_.enemy_freeze);
    if (!fits(message))
    {
        TRACE("hud", "freeze_countdown_suppressed x=%d limit=%d",
              static_cast<int>(x), static_cast<int>(right_limit));
        return;
    }

    s->text_normal.write_xy(x, row, message.c_str(),
                            static_cast<unsigned char>(YELLOW),
                            static_cast<short>(1));
    TRACE("hud", "freeze_countdown left=%d row=%d text=%s",
          static_cast<int>(s->world_.enemy_freeze), static_cast<int>(row),
          message.c_str());
}

// Ramp color for a mode HUD element: team ramp when a score team is set,
// else the classic HUD text yellow (the un-overlaid draw_respawn_countdown
// default). An FFA band byte (16-31) is a scoring identity too and takes its
// fighter ramp from the shared table (docs/ffa-design.md §4).
static unsigned char mode_hud_color(std::uint8_t team)
{
    if (og::sim::is_scoring_identity(static_cast<int>(team)))
        return og::sim::team_ramp_base(static_cast<int>(team));
    return static_cast<unsigned char>(YELLOW);
}

// Fallback captions for a walker with neither a myguy nor a stats name.
// File scope so the mode row can measure the caption the classic HUD is
// about to draw on the same tm+4 row (see draw_mode_panel).
static const std::array<const char*, NUM_FAMILIES> kFamilyHudNames = {
    "SOLDIER", "ELF", "ARCHER", "MAGE",
    "SKELETON", "CLERIC", "ELEMENTAL",
    "FAERIE", "SLIME", "SLIME", "SLIME",
    "THIEF", "GHOST", "DRUID", "ORC",
    "ORC CAPTAIN", "BARBARIAN", "ARCHMAGE",
    "GOLEM", "GIANT SKEL", "TOWER",
};

// THE one copy of the upper-left HUD caption rule (a myguy's name wins even
// when blank, else a non-empty stats name, else the family word).
// new_score_panel draws it at lm+3 on the tm+4 row; draw_mode_panel measures
// it to find where its own row may start, so the two must agree exactly.
// The follow banner keeps its own blank-skipping variant below.
static std::string hud_display_name(const walker* control)
{
    if (control == nullptr)
        return {};
    if (control->myguy)
        return control->myguy->name;
    if (!control->stats()->name.empty())
        return control->stats()->name;
    int fam = static_cast<int>(control->family());
    if (fam < 0 || fam >= NUM_FAMILIES)
        fam = 0;
    return kFamilyHudNames[static_cast<std::size_t>(fam)];
}

// --- The condensed mode score row -------------------------------------------
//
// The mode row sits on ONE line, at tm+4, in every viewport.
//
// Why one line: the retired layout spread the four ModeState slots over
// tm+4 / tm+28 / tm+36, and the notification banner
// (viewscreen::display_text) writes its five messages at viewport-local
// y = 30, 36, 42, 48, 54. Two of the three score rows therefore landed
// inside the banner block and shredded every announcement. The banner's
// topmost row is viewport-local 30, i.e. tm + 30 - OVERSCAN_PADDING, so its
// worst case (REDUCE_OVERSCAN, padding 6) is tm+24. text.png is a 5x6 font,
// so a row at tm+4 ends on tm+9 — 14 clear scanlines in that worst case, 20
// normally. The two can never share a scanline.
//
// Horizontal window: the classic HUD owns the caption at lm+3 and the
// TEAM/FOES box from rm-57 on the SAME tm+4 row, so the mode row takes the
// channel between them. The left edge clears both the caption's own button
// box (lm+1..lm+63) and the caption text itself, measured through the
// shared hud_display_name; the right edge stops at rm-60 while the FOES
// column is on. With no classic HUD (dead / AI-followed / spectated view)
// the row takes the whole pane, lm+2..rm-2. Both bounds derive from this
// pane's own lm/rm, so the R1 clamp holds: the row can never paint into a
// neighboring viewport.
//
// Composition: the non-empty slots in index order, each stripped of its
// leading team color word (the segment is already drawn in that team's ramp
// — repeating "RED" costs a third of a narrow pane's budget for nothing),
// joined with " - " in the default HUD color so the per-team ramps stay
// legible as separate scores. The dash rather than a pipe because text.png
// carries a blank glyph at '|' (code 124, its last frame) — an invisible
// separator would run the scores together.
//
// Narrow panes: the old rule suppressed slots by view count. The budget
// says it better — if the joined row does not fit, first try the compact
// tier (#210): every team's bare score joined by a dash ("7-3"), so a
// split-screen pane still shows the opposing side. Only when even that
// overflows fall back to the LOCAL team's segment alone, then truncate
// end-first to the remaining characters, and draw nothing at all below
// three characters.
inline constexpr std::string_view kModeRowSeparator = " - ";
inline constexpr Sint32 kModeRowOffsetY = 4;
inline constexpr Sint32 kModeRowMinChars = 3;

// One drawn run of the row: its characters and the ramp they take.
struct ModeRowSegment
{
    std::string text;
    unsigned char color = 0;
    int team = 255;
};

// Drop a leading team color word ("RED 5" -> "5") when the slot names the
// team it is already colored by. Bails out unless a real word boundary and a
// non-empty remainder follow, so a line that IS just the color word, or one
// that merely starts with those letters ("REDOUBT 4"), survives whole.
static std::string_view strip_team_color_word(const char* text,
                                              std::uint8_t team)
{
    std::string_view line{text};
    if (team >= SCORE_TEAM_COUNT)
        return line;
    const std::string_view word{og::sim::team_color_name(static_cast<int>(team))};
    if (word.empty() || line.size() <= word.size())
        return line;
    for (std::size_t i = 0; i < word.size(); ++i)
    {
        const int a = std::toupper(static_cast<unsigned char>(line[i]));
        const int b = std::toupper(static_cast<unsigned char>(word[i]));
        if (a != b)
            return line;
    }
    std::string_view rest = line.substr(word.size());
    if (rest.front() != ' ' && rest.front() != ':')
        return line;
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == ':'))
        rest.remove_prefix(1);
    return rest.empty() ? line : rest;
}

// only_team >= 0 keeps just that team's slots (the narrow-pane fallback).
static std::vector<ModeRowSegment> compose_mode_row(
    const og::sim::ModeState& mode, int only_team)
{
    std::vector<ModeRowSegment> segments;
    for (const og::sim::ModeHudLine& line : mode.hud)
    {
        if (line.text[0] == '\0')
            continue;
        if (only_team >= 0 && static_cast<int>(line.team) != only_team)
            continue;
        const std::string_view body =
            strip_team_color_word(line.text.data(), line.team);
        if (body.empty())
            continue;
        if (!segments.empty())
            segments.push_back({std::string(kModeRowSeparator),
                                mode_hud_color(255), 255});
        segments.push_back({std::string(body), mode_hud_color(line.team),
                            static_cast<int>(line.team)});
    }
    return segments;
}

// The compact tier (#210): every team's segment cut to its bare score —
// the prefix before the first '/' or space — joined by a 1-char dash, so
// a narrow split-screen pane shows EVERY team's number ("7-3") before the
// local-only fallback ever hides a rival's score.
static std::vector<ModeRowSegment> compact_mode_row(
    const std::vector<ModeRowSegment>& segments)
{
    std::vector<ModeRowSegment> compact;
    int team_segments = 0;
    for (const ModeRowSegment& segment : segments)
    {
        if (segment.team == 255)
            continue; // separators are re-authored below
        ++team_segments;
        const std::size_t cut = segment.text.find_first_of("/ ");
        std::string score = segment.text.substr(
            0, cut == std::string::npos ? segment.text.size() : cut);
        if (score.empty())
            continue;
        if (!compact.empty())
            compact.push_back({"-", mode_hud_color(255), 255});
        compact.push_back({std::move(score), segment.color, segment.team});
    }
    // With one team on the row there is no rival to keep visible —
    // end-first truncation of the full text serves better than its stub.
    if (team_segments < 2)
        compact.clear();
    return compact;
}

static Sint32 mode_row_length(const std::vector<ModeRowSegment>& segments)
{
    Sint32 total = 0;
    for (const ModeRowSegment& segment : segments)
        total += static_cast<Sint32>(segment.text.size());
    return total;
}

static void draw_mode_panel(screen* s, viewscreen* view, Sint32 lm, Sint32 tm,
                            Sint32 rm)
{
    const og::sim::ModeState& mode = s->world_.mode;
    text& mytext = s->text_normal;
    walker* const control = view->control;

    // The exact condition new_score_panel draws the classic HUD under.
    const bool classic_hud =
        control != nullptr && !control->dead() && control->user() != -1;

    Sint32 left = lm + 2;
    Sint32 right = rm - 2;
    if (classic_hud)
    {
        const std::string caption = hud_display_name(control);
        const Sint32 caption_end =
            lm + 3 + 6 * static_cast<Sint32>(caption.size()) + 4;
        left = std::max<Sint32>(lm + 66, caption_end);
        if (view->prefs[PREF_FOES] == PREF_FOES_ON)
            right = rm - 60;
    }

    const Sint32 budget = (right - left) / 6;
    if (budget < kModeRowMinChars)
    {
        TRACE("mode_hud", "row_suppressed budget=%d", static_cast<int>(budget));
        return;
    }

    std::vector<ModeRowSegment> segments = compose_mode_row(mode, -1);
    if (mode_row_length(segments) > budget)
    {
        // #210: before hiding rivals, try every team's bare score ("7-3").
        std::vector<ModeRowSegment> compact = compact_mode_row(segments);
        if (!compact.empty() && mode_row_length(compact) <= budget)
        {
            segments = std::move(compact);
        }
        else if (classic_hud && control->team_num() < SCORE_TEAM_COUNT)
        {
            std::vector<ModeRowSegment> local =
                compose_mode_row(mode, static_cast<int>(control->team_num()));
            if (!local.empty())
                segments = std::move(local);
        }
    }
    if (segments.empty())
        return;

    const Sint32 row = tm + kModeRowOffsetY;
    Sint32 x = left;
    Sint32 drawn = 0;
    std::string shown;
    for (const ModeRowSegment& segment : segments)
    {
        if (drawn >= budget)
            break;
        const std::size_t room = static_cast<std::size_t>(budget - drawn);
        const std::string piece = segment.text.substr(0, room);
        mytext.write_xy(x, row, piece.c_str(), segment.color,
                        static_cast<short>(1));
        TRACE("mode_hud", "seg team=%d x=%d text=%s", segment.team,
              static_cast<int>(x), piece.c_str());
        x += 6 * static_cast<Sint32>(piece.size());
        drawn += static_cast<Sint32>(piece.size());
        shown += piece;
    }
    TRACE("mode_hud", "row y=%d x=%d budget=%d text=%s", static_cast<int>(row),
          static_cast<int>(left), static_cast<int>(budget), shown.c_str());
}

// A small solid triangle at the viewport edge pointing along (dx, dy) —
// exactly one of the four axis directions. (x, y) is the tip pixel.
static void draw_beacon_arrow(screen* s, Sint32 x, Sint32 y, int dx, int dy,
                              unsigned char color)
{
    for (int step = 0; step < 3; ++step)
    {
        const Sint32 span = static_cast<Sint32>(2 * step + 1);
        if (dx != 0)
        {
            // Horizontal arrow: columns shrink toward the tip.
            s->fastbox(x - dx * step, y - step, 1, span, color, 1);
        }
        else
        {
            // Vertical arrow: rows shrink toward the tip.
            s->fastbox(x - step, y - dy * step, span, 1, color, 1);
        }
    }
}

// Off-screen beacon arrows + in-view pulse markers for the ModeState beacon
// slots (og.set_beacon). Directional edge arrows point at out-of-view beacon
// entities; an in-view beacon gets a pulsing underline bar. Render-only and
// RNG-free: the pulse phase derives from the replicated tick, never from a
// rng draw (the radar jitter-0 discipline).
static void draw_mode_beacons(screen* s, viewscreen* view,
                              const GameplayUiProjector& proj, Sint32 lm,
                              Sint32 tm, Sint32 rm, Sint32 bm)
{
    const GameWorld& world = s->world_;
    const int view_floor =
        (view->control != nullptr) ? static_cast<int>(view->control->floor())
                                   : 0;
    for (int slot = 0; slot < og::sim::kModeBeacons; ++slot)
    {
        const og::sim::ModeBeacon& beacon =
            world.mode.beacons[static_cast<std::size_t>(slot)];
        if (beacon.entity_id == 0)
            continue;
        const walker* target =
            world.find_by_id(static_cast<std::uint32_t>(beacon.entity_id));
        if (target == nullptr || target->dead() || target->dormant())
            continue;
        // Multi-floor: beacons on another floor would project at a
        // meaningless spot; skip them (the radar's floor rule).
        if (world.floor_count() > 1 &&
            static_cast<int>(target->floor()) != view_floor)
            continue;

        // The radar blip's rule, verbatim: a score team or an FFA band byte
        // names its own ramp, anything else (255) borrows the target's.
        const unsigned char color =
            og::sim::is_scoring_identity(static_cast<int>(beacon.team))
                ? og::sim::team_ramp_base(static_cast<int>(beacon.team))
                : target->query_team_color();

        // Project the entity center into the stable gameplay-UI pane. The
        // caller's ScopedGameplayUiViewLayout already swapped xloc/xview to
        // UI values while topx/topy stayed WORLD (issue #220: mixing them
        // raw put the marker 1/zoom too far out and flipped an on-screen
        // ball to an edge arrow), so the projection runs through the
        // projector captured before the scope.
        const float world_cx = static_cast<float>(target->xpos()) +
            static_cast<float>(target->sizex() / 2) -
            static_cast<float>(view->topx) +
            static_cast<float>(proj.world_xloc());
        const float world_cy = static_cast<float>(target->ypos()) +
            static_cast<float>(target->sizey() / 2) -
            static_cast<float>(view->topy) +
            static_cast<float>(proj.world_yloc());
        const auto [cx, cy] = proj.project(world_cx, world_cy);

        // Clamp into the viewport interior; a binding clamp = off-view.
        const Sint32 clamped_x = std::clamp(cx, lm + 4, rm - 5);
        const Sint32 clamped_y = std::clamp(cy, tm + 4, bm - 5);
        const Sint32 over_x = cx - clamped_x;
        const Sint32 over_y = cy - clamped_y;

        if (over_x == 0 && over_y == 0)
        {
            // In view: pulsing underline bar just below the sprite. Triangle
            // wave over a 12-tick period (one second at the sim rate).
            const int phase =
                static_cast<int>(world.tick_count_ % 12u);
            const int half = (phase < 6) ? phase : (11 - phase);
            const Sint32 bar_w = proj.scale_w(
                static_cast<Sint32>(8 + 2 * half), 1);
            // The classic pulse is 2 rows tall; scale by the vertical pane
            // ratio, floored at one drawable row (fastbox takes a row
            // COUNT, unlike draw_box's inclusive corners).
            const Sint32 bar_h = proj.scale_h(2, 1);
            const Sint32 bar_x =
                std::clamp(cx - bar_w / 2, lm, rm - bar_w);
            const Sint32 bottom_cy = proj.project(
                world_cx,
                world_cy +
                    static_cast<float>(target->sizey() / 2)).second;
            const Sint32 bar_y = std::min(bottom_cy + 1, bm - bar_h);
            s->fastbox(bar_x, bar_y, bar_w, bar_h, color, 1);
            TRACE("mode_hud", "beacon_pulse slot=%d w=%d x=%d y=%d", slot,
                  static_cast<int>(bar_w), static_cast<int>(bar_x),
                  static_cast<int>(bar_y));
        }
        else
        {
            // Off view: an edge arrow pointing along the dominant overshoot
            // axis, at the clamped projection.
            int dx = 0;
            int dy = 0;
            if (std::abs(over_x) >= std::abs(over_y))
                dx = (over_x > 0) ? 1 : -1;
            else
                dy = (over_y > 0) ? 1 : -1;
            draw_beacon_arrow(s, clamped_x, clamped_y, dx, dy, color);
            TRACE("mode_hud", "beacon_edge slot=%d dx=%d dy=%d x=%d y=%d",
                  slot, dx, dy, static_cast<int>(clamped_x),
                  static_cast<int>(clamped_y));
        }
    }
}

short new_score_panel(screen* s, short /*do_it*/)
{
#define L_D(x) x*8
	ScopedGameplayUiCanvas gameplay_ui(*s);
    std::string message;
    std::string tempname;
    short tempfoes = 0;
    short players;
    short tempallies = 0;
    text& mytext = s->text_normal;

    walker* control;
    Sint32 lm, tm; // left and top margins
    Sint32 rm, bm; // right and bottom margins
    char draw_button;  // do we draw a button background?
    char text_color;

    Uint32 myscore;
    static std::array<Uint32, SCORE_TEAM_COUNT> scorecountup = {
        s->world_.m_score[0],
        s->world_.m_score[1],
        s->world_.m_score[2],
        s->world_.m_score[3],
    };

    // Bottom edge of the top viewport's TEAM/FOES counter box, for the FPS
    // overlay's dynamic placement (the box grows with the NEXT WAVE and FLR
    // rows). Default = the classic FOES-off geometry, so FOES-off frames are
    // byte-identical.
    Sint32 fps_below_y = OVERSCAN_PADDING + 16;

    for (players = 0; players < s->numviews; players++)
    {
		// Captured BEFORE the layout scope: inside it xloc/xview are already
		// UI-pane values and the world->UI projection would degenerate to
		// the identity (issue #220).
		const GameplayUiProjector beacon_proj(*s->viewob[players]);
		ScopedGameplayUiViewLayout gameplay_ui_layout(
			*s->viewob[players], *s);
        control = s->viewob[players]->control;
        lm = s->viewob[players]->xloc + OVERSCAN_PADDING;
        tm = s->viewob[players]->yloc + OVERSCAN_PADDING;
        rm = s->viewob[players]->endx - OVERSCAN_PADDING;
        bm = s->viewob[players]->endy - OVERSCAN_PADDING;

        // Scripted-mode (TYPE_SCRIPTED) worlds render the generic ModeState
        // HUD (mode Lua owns the scoreboard text).
        const bool scripted_mode_hud =
            (s->world_.type & GameWorld::TYPE_SCRIPTED) &&
            s->world_.mode.active;
        if (scripted_mode_hud)
        {
            draw_mode_panel(s, s->viewob[players].get(), lm, tm, rm);
            draw_mode_beacons(s, s->viewob[players].get(), beacon_proj,
                              lm, tm, rm, bm);
        }
        if (og::sim::classic_respawn_active(s->world_) || scripted_mode_hud)
            draw_respawn_countdown(s, control, lm, tm);

        // §2.8 follow caption: a follow-engaged view names its watched
        // target on a black strip at the viewport's bottom-center,
        // independent of the user() HUD gate below — an AI target
        // (user() == -1) renders the caption ALONE, with no HUD/radar
        // ([NET-R6] the follow camera never stamps user tags locally); a
        // followed foreign hero shows its owner's snapshot-synced HUD too.
        if (s->viewob[players]->following_ && control != nullptr)
        {
            std::string follow_name;
            if (control->myguy && !control->myguy->name.empty())
                follow_name = control->myguy->name;
            else if (!control->stats()->name.empty())
                follow_name = control->stats()->name;
            else
            {
                int follow_fam = static_cast<int>(control->family());
                if (follow_fam < 0 || follow_fam >= NUM_FAMILIES)
                    follow_fam = 0;
                follow_name = kFamilyHudNames[
                    static_cast<std::size_t>(follow_fam)];
            }
            if (follow_name.size() > 12)
                follow_name.resize(12);
            std::string caption = "FOLLOWING " + follow_name;
            const std::string& company = s->viewob[players]->follow_company_;
            if (!company.empty())
                caption += " (" + company.substr(0, 8) + ")";
            if (caption.size() > 26)
                caption.resize(26);
            const Sint32 caption_w = static_cast<Sint32>(caption.size()) * 6;
            const Sint32 cx = lm + (rm - lm - caption_w) / 2;
            const Sint32 cy = bm - 12;
            s->fastbox(cx - 2, cy - 2, caption_w + 4, 11, 0, 1);
            mytext.write_xy(cx, cy, caption.c_str(),
                            static_cast<unsigned char>(YELLOW),
                            static_cast<short>(1));
        }
        // Draw the HUD whenever this viewport's control walker is a live,
        // human-claimed walker. We must NOT compare control->user() against the
        // local viewport index: that only holds for local split-screen (where
        // game.cpp claims view->control with set_user(view_idx)). On a network
        // client the single local viewport (players == 0) shows a walker whose
        // user() is the *server-global* player slot assigned by bind_player, so
        // a == players check would hide the HUD for every non-host client.
        if (control && !control->dead() && control->user() != -1)
        {
            // The frozen-time cell is part of the classic HUD block: it rides
            // the same left column, so a view that draws no HUD (a spectator
            // camera, a corpse waiting to respawn) must not draw it either.
            draw_freeze_countdown(s, lm, tm, rm, bm);

            // Get the button-drawing info ..
            draw_button = s->viewob[players]->prefs[PREF_OVERLAY];
            if (draw_button)
                text_color = DARK_BLUE;
            else
                text_color = YELLOW;

            // Get current number of foes
            tempfoes = remaining_foes(s, control);
            // Get current number of team-members. team_num() arrives on the
            // same raw-byte snapshot channel as family()/current_special()
            // below, but it is an *unsigned* byte and remaining_team() takes a
            // signed char: a mirrored team above 127 narrows to a negative
            // value that matches no walker, so the HUD would report zero
            // allies. Clamp to the legal team range first, exactly as the
            // scenario loader does (sanitize_loaded_team_num). Every team the
            // game actually produces is 0..MAX_TEAM and passes through
            // unchanged.
            int hud_team = static_cast<int>(control->team_num());
            if (hud_team > MAX_TEAM)
                hud_team = 0;
            tempallies = remaining_team(s, static_cast<char>(hud_team));

            // family()/current_special() may be attacker-controlled on a
            // network mirror (set from raw int8 snapshot bytes), so they can be
            // any value in [-128,127]. Clamp before using them to index the
            // fixed namelist/special_name/alternate_name/special_cost arrays to
            // prevent out-of-bounds reads. In-range values pass through
            // unchanged.
            int fam = static_cast<int>(control->family());
            if (fam < 0 || fam >= NUM_FAMILIES)
                fam = 0;
            int spc = static_cast<int>(control->current_special());
            if (spc < 0 || spc >= NUM_SPECIALS)
                spc = 0;

            // Display name or type, upper left
            tempname = hud_display_name(control);

            message = tempname;

            if (draw_button)
                s->draw_button(lm+1, tm+2, lm+63, tm+9, 1, 1);

            mytext.write_xy(lm+3, tm+4, message.c_str(), static_cast<unsigned char>(text_color), 1);

            // HP/MP bars; dependent on user settings
            switch (s->viewob[players]->prefs[PREF_LIFE])
            {
                case PREF_LIFE_TEXT: // display numeric values only
                    if (draw_button)
                        s->draw_button(lm+1, tm+10, lm+63, tm+26, 1, 1);
                    message = std::format("HP: {:.0f}", ceilf(control->stats()->hitpoints()));
                    mytext.write_xy(lm+5, tm+12, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
                    message = std::format("MP: {:.0f}", ceilf(control->stats()->magicpoints()));
                    mytext.write_xy(lm+5, tm+20, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
                    break;
                case PREF_LIFE_BARS: // display graphical bars only
                    new_draw_value_bar(lm+2, tm+10, control, 0, s);
                    new_draw_value_bar(lm+2, tm+18, control, 1, s);
                    break;
                case PREF_LIFE_OFF: // do nothing
                    break;
                case PREF_LIFE_BOTH: // default case
                default:
                    new_draw_value_bar(lm+2, tm+10, control, 0, s);
                    message = std::format("HP: {:.0f}", ceilf(control->stats()->hitpoints()));
                    mytext.write_xy(lm+5, tm+11, message.c_str(), static_cast<unsigned char>(BLACK), static_cast<short>(1));

                    new_draw_value_bar(lm+2, tm+18, control, 1, s);
                    message = std::format("MP: {:.0f}", ceilf(control->stats()->magicpoints()));
                    mytext.write_xy(lm+5, tm+19, message.c_str(), static_cast<unsigned char>(BLACK), static_cast<short>(1));
                    break;
            }

            // Scared/fleeing countdown: while a fright (the ghost's scare, a
            // shove) has force-queued a walk this walker must run off, the
            // player has no control — say so instead of feeling broken, with
            // the seconds left at the 12 Hz sim rate. Drawn just below the
            // HP/MP rows (tm+10/tm+18); disappears the tick the queue drains.
            const int scared_ticks = hud_scared_flee_ticks(control);
            if (scared_ticks > 0)
            {
                const int scared_seconds = (scared_ticks + 11) / 12;
                message = std::format("SCARED: {}s", scared_seconds);
                mytext.write_xy(lm+2, tm+28, message.c_str(),
                                static_cast<unsigned char>(RED),
                                static_cast<short>(1));
                TRACE("hud", "scared ticks=%d secs=%d",
                      scared_ticks, scared_seconds);
            }

            // Score, bottom left corner
            int special_offset = -24;
            Sint32 score_bottom = bm;
#ifdef USE_TOUCH_INPUT
            // Upper left instead
            score_bottom = tm + 54;
            special_offset = 0;
#endif

            int special_y = score_bottom + special_offset;
            const bool compact_score_panel =
                s->numviews > 2 && !(s->numviews == 3 && players == 0);
            // Don't show score and XP (clutter) when in a small viewport
            if (compact_score_panel)
                special_y = score_bottom - 8;

            const bool show_score =
                s->viewob[players]->prefs[PREF_SCORE] == PREF_SCORE_ON;

            // Draw box, if needed
            if (draw_button)
            {
                if (show_score)
                {
                    s->draw_button(lm+1, score_bottom-26, lm+98,
                                   score_bottom-2, 1, 1);
                }
                else
                {
                    Sint32 special_box_bottom = special_y + 6;
#ifdef USE_TOUCH_INPUT
                    if (s->alternate_name[fam][spc] != "NONE")
                    {
                        special_box_bottom = std::max(
                            special_box_bottom,
                            score_bottom + special_offset + 14);
                    }
#endif
                    s->draw_button(lm+1, special_y-2, lm+98,
                                   special_box_bottom, 1, 1);
                }
            }

            if (show_score)
            {
                const bool can_show_team_score = is_valid_score_team(control->team_num());
                if (can_show_team_score)
                {
                    // Get our score ..
                    const unsigned char team_num = control->team_num();
                    myscore = s->world_.m_score[team_num];
                    if (scorecountup[team_num] > myscore)
                        scorecountup[team_num] = myscore;
                    if (scorecountup[team_num] < myscore)
                    {
                        scorecountup[team_num]++;
                        scorecountup[team_num] += static_cast<Uint32>(rng((myscore - scorecountup[team_num]))/12);
                    }
                    if (scorecountup[team_num] > myscore)
                        scorecountup[team_num] = myscore;

                    // above should count up the score towards the current amount
                    if (!compact_score_panel)
                    {
                        message = std::format("SC: {}", scorecountup[team_num]);
                        mytext.write_xy(lm+2, score_bottom-8, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));

                        // Level or exp, 2nd bottom left
                        if (control->myguy)
                            message = std::format("XP: {}", control->myguy->exp);
                        else
                            message = std::format("LEVEL: {}", control->stats()->level());
                        mytext.write_xy(lm+2, score_bottom-16, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
                    }
                }
                else if (!compact_score_panel)
                {
                    message = "SC: 0";
                    mytext.write_xy(lm+2, score_bottom-8, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
                }
            }

            // Currently-select special
            if (control->shifter_down() &&
                s->alternate_name[fam][spc] != "NONE")
                message = std::format("SPC: {}", s->alternate_name[fam][spc]);
            else
                message = std::format("SPC: {}", s->special_name[fam][spc]);

            // Disabled-special signifier: a walker whose specials are
            // switched off (the level's NPC flag) can NEVER fire the
            // listed special, no matter its MP — grey the line out so it
            // reads as unavailable rather than merely unaffordable (RED).
            if (control->specials_disabled())
            {
                mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(GREY), static_cast<short>(1));
                TRACE("hud", "spc_disabled fam=%d spc=%d", fam, spc);
            }
            else if (control->stats()->magicpoints() >= control->stats()->special_cost(spc))
                mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
            else
                mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(RED), static_cast<short>(1));

#ifdef USE_TOUCH_INPUT
            // Alternate special name (if not "NONE")
            if (s->alternate_name[fam][spc] != "NONE")
            {
                message = std::format("ALT: {}", s->alternate_name[fam][spc]);
                if (control->specials_disabled())
                    mytext.write_xy(lm+2, score_bottom + special_offset + 8, message.c_str(), static_cast<unsigned char>(GREY), static_cast<short>(1));
                else if (control->stats()->magicpoints() >= control->stats()->special_cost(spc))
                    mytext.write_xy(lm+2, score_bottom + special_offset + 8, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
                else
                    mytext.write_xy(lm+2, score_bottom + special_offset + 8, message.c_str(), static_cast<unsigned char>(RED), static_cast<short>(1));
            }
#endif

            // Number of allies, upper right
            if (s->viewob[players]->prefs[PREF_FOES] == PREF_FOES_ON)
            {
                // B5: split pending (dormant delayed-spawn) hostiles out of
                // the foe count and show a countdown to the next wave, so
                // "FOES: 3" over an empty map reads as "3 foes are coming",
                // not as a bug. remaining_foes counts dormant hostiles too
                // (they are alive), so awake = total - pending; foes merely
                // awaiting a classic respawn are corpses (not in the total),
                // so they only join the displayed wave count.
                short pending_foes = 0;
                short pending_respawn_foes = 0;
                std::uint32_t next_wake_ticks = 0;
                pending_hostile_wave_counts(
                    s->world_, control, pending_foes, pending_respawn_foes,
                    next_wake_ticks);
                const int awake_foes = std::max(
                    0, static_cast<int>(tempfoes) - static_cast<int>(pending_foes));
                const int wave_foes = static_cast<int>(pending_foes) +
                    static_cast<int>(pending_respawn_foes);
                const bool show_wave = wave_foes > 0;

                // Floor row (feature B): drawn only when the shared label is
                // non-empty, so single-floor frames stay byte-identical. Call
                // sites key on non-empty, never on floor_count (game modes
                // re-label inside floor_hud_label).
                const std::string floor_label = floor_hud_label(
                    s->world_, static_cast<int>(control->floor()));
                const bool show_floor = !floor_label.empty();

                Sint32 box_bottom = show_wave ? 24 : 16;
                if (show_floor)
                    box_bottom += 8;
                if (players == 0)
                    fps_below_y = tm + box_bottom;

                if (draw_button)
                    s->draw_button(rm-57, tm+1, rm-2, tm + box_bottom, 1, 1);

                message = std::format("TEAM: {}", tempallies);
#ifndef USE_TOUCH_INPUT
                mytext.write_xy(rm - 55, tm+2, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
#else
                mytext.write_xy(rm - 55, tm+2 + 44 + 8, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
#endif

                // Number of foes, 2nd upper right
                if (show_wave)
                    message = std::format("FOES: {} (+{})",
                                          awake_foes, wave_foes);
                else
                    message = std::format("FOES: {}", tempfoes);
                // Right-align when the (+m) suffix is shown so it stays
                // inside the viewport; the classic position is byte-identical
                // otherwise.
                const Sint32 foes_x = show_wave
                    ? std::max<Sint32>(lm, rm - 2 - 6 * static_cast<Sint32>(message.size()))
                    : rm - 55;
#ifndef USE_TOUCH_INPUT
                mytext.write_xy(foes_x, tm+10, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
#else
                mytext.write_xy(foes_x, tm+10 + 44 + 8, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
#endif

                if (show_wave)
                {
                    const std::uint32_t wave_seconds = (next_wake_ticks + 11u) / 12u;
                    message = std::format("NEXT WAVE: {}s", wave_seconds);
                    const Sint32 wave_x = std::max<Sint32>(
                        lm, rm - 2 - 6 * static_cast<Sint32>(message.size()));
#ifndef USE_TOUCH_INPUT
                    mytext.write_xy(wave_x, tm+18, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
#else
                    mytext.write_xy(wave_x, tm+18 + 44 + 8, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
#endif
                    TRACE("hud", "next_wave awake=%d pending=%d secs=%u",
                          awake_foes,
                          wave_foes,
                          static_cast<unsigned>(wave_seconds));
                }

                if (show_floor)
                {
                    const Sint32 floor_y = tm + (show_wave ? 26 : 18);
                    const Sint32 floor_x = std::max<Sint32>(
                        lm, rm - 2 - 6 * static_cast<Sint32>(floor_label.size()));
#ifndef USE_TOUCH_INPUT
                    mytext.write_xy(floor_x, floor_y, floor_label.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
#else
                    mytext.write_xy(floor_x, floor_y + 44 + 8, floor_label.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
#endif
                    TRACE("hud", "floor %s", floor_label.c_str());
                }
            }
        }
    }

    if (og::runtime::current_session != nullptr &&
        og::runtime::current_session->show_fps_)
    {
        draw_fps_overlay(*s, static_cast<int>(fps_below_y));
    }

    return 1;
}

short score_panel(screen* s)
{
    return score_panel(s, 0);
}

short score_panel(screen* s, short /*do_it*/)
{
    return new_score_panel(s, 1);
}

void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor,
                         short somelength, screen* s)
{
    short i, j;

    // Draw the black border ..
    s->fastbox(left+2, top, somelength-4, 1, 0, 1);
    s->fastbox(left+1, top+1, 1,  1, 0, 1);
    s->fastbox(left+58, top+1, 1, 1, 0, 1);
    s->fastbox(left,  top+2, 1, 3,   0, 1);
    s->fastbox(left+59, top+2, 1, 3, 0, 1);
    s->fastbox(left+1, top+5, 1,  1, 0, 1);
    s->fastbox(left+58, top+5, 1, 1, 0, 1);
    s->fastbox(left+2, top+6, somelength-4, 1, 0, 1);

    // Draw the box ..
    s->fastbox(left+2, top+1, somelength-4, 1, somecolor, 1);
    s->fastbox(left+1, top+2, somelength-2, 3, somecolor, 1);
    s->fastbox(left+2, top+5, somelength-4, 1, somecolor, 1);

    // rotating colors .. do special ..
    if ((somecolor == ORANGE_START) || (somecolor == WATER_START))
    {
        for (i=0; i < (somelength-4)/2; i++)
            for (j=0; j < 3; j++)
            {
                const unsigned char col = static_cast<unsigned char>(somecolor + static_cast<unsigned char>((i + j) % 16));
                s->fastbox(left+(somelength/2)-i-1, top+1+(2-j), 1, 1, col, 1);
                s->fastbox(left+(somelength/2)-i-1, top+1+(2+j), 1, 1, col, 1);
                s->fastbox(left+i+(somelength/2),   top+1+(2-j), 1, 1, col, 1);
                s->fastbox(left+i+(somelength/2),   top+1+(2+j), 1, 1, col, 1);
            }
    }
}
