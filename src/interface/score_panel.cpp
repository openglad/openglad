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

#include <openglad/interface/ability_icons.h>
#include <openglad/interface/base.h>
#include <openglad/interface/fps_overlay.h>
#include <openglad/interface/hud_counter_box.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/timed_effects.h>
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
#include <optional>
#include <span>
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

inline constexpr int OVERSCAN_PADDING = 0;

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
        const int seconds = og::sim::timer_seconds(entry.ticks_left);
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
// Shown in seconds through og::sim::timer_seconds, like the seat's own
// countdown below ("TIME LEFT: 12s"); the counter itself stays in ticks.
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
//
// New Ability Select's hotbar grows the bottom-left box (`box_top` names the
// box's top edge and the cell rides 8px above it): the full panel's strip
// puts the top at bm-kBeltHudFullBoxTop (bm-46, cell at bm-54), the compact
// panel's at bm-kBeltHudCompactBoxTop (bm-28, cell at bm-36); split screen's
// half-size strip at bm-kBeltHudHalfFullBoxTop (bm-34, cell at bm-42) or
// bm-kBeltHudHalfCompactBoxTop (bm-18, cell at bm-26); where the strip
// does not fit, the text rows' top (bm-42, cell at bm-50) or the classic
// bm-kHudBoxTop (bm-26, cell at bm-34) stands, as it does with the setting
// off.
//
// Answers whether it drew, so the seat's own countdown below can step clear.
static bool draw_freeze_countdown(screen* s, Sint32 lm, Sint32 tm, Sint32 rm,
                                  Sint32 bm, Sint32 box_top)
{
    if (s->world_.enemy_freeze <= 0)
        return false;

    // First 6px row clear of the notification banner.
    const Sint32 banner_bottom = tm + 30 + MAX_MESSAGES * 6;
    const Sint32 row = std::max<Sint32>(box_top - 8, banner_bottom);
    if (row + 6 > box_top)
    {
        TRACE("hud", "freeze_countdown_suppressed row=%d",
              static_cast<int>(row));
        return false;
    }

    // Same left column as the SPC/XP/SC rows below the cell.
    const Sint32 x = lm + 2;
    // The radar is only in the way while the cell shares its rows.
    const Sint32 right_limit = (row + 6 > bm - 48) ? (rm - 64) : rm;
    const auto fits = [&](const std::string& text) {
        return x + 6 * static_cast<Sint32>(text.size()) <= right_limit;
    };

    const int secs = og::sim::timer_seconds(s->world_.enemy_freeze);
    std::string message = std::format("TIME LEFT: {}s", secs);
    if (!fits(message))
        message = std::format("T: {}s", secs);
    if (!fits(message))
    {
        TRACE("hud", "freeze_countdown_suppressed x=%d limit=%d",
              static_cast<int>(x), static_cast<int>(right_limit));
        return false;
    }

    s->text_normal.write_xy(x, row, message.c_str(),
                            static_cast<unsigned char>(YELLOW),
                            static_cast<short>(1));
    TRACE("hud", "freeze_countdown left=%d secs=%d row=%d text=%s",
          static_cast<int>(s->world_.enemy_freeze), secs,
          static_cast<int>(row), message.c_str());
    return true;
}

// The seat's own timed effect, a kit's or a classic one's (a possession,
// a potion, a stun...), drawn in the frozen-time cell's manner: the same
// left column and row rules, YELLOW, every frame off the mirror
// (og::sim::seat_timer reads snapshot fields only), in whole seconds.
// When the frozen-time cell drew this frame it keeps its row (it is the
// world's clock and the classic one) and this cell takes the row above it,
// if that row is still clear of the notification banner; otherwise it
// yields. Too narrow a pane drops the label, then the cell. `box_top` is
// the frozen-time cell's: the bottom-left box's top edge.
static void draw_seat_timer(screen* s, const walker& control, Sint32 lm,
                            Sint32 tm, Sint32 rm, Sint32 bm, Sint32 box_top,
                            bool freeze_drew)
{
    const og::sim::SeatTimer timer = og::sim::seat_timer(s->world_, control);
    if (timer.label == nullptr)
        return;

    const Sint32 banner_bottom = tm + 30 + MAX_MESSAGES * 6;
    Sint32 row = std::max<Sint32>(box_top - 8, banner_bottom);
    if (freeze_drew)
        row -= 6;
    if (row < banner_bottom || row + 6 > box_top)
    {
        TRACE("hud", "seat_timer_suppressed label=%s row=%d", timer.label,
              static_cast<int>(row));
        return;
    }

    const Sint32 x = lm + 2;
    const Sint32 right_limit = (row + 6 > bm - 48) ? (rm - 64) : rm;
    const auto fits = [&](const std::string& text) {
        return x + 6 * static_cast<Sint32>(text.size()) <= right_limit;
    };

    const int secs = og::sim::timer_seconds(timer.ticks);
    std::string message = std::format("{}: {}s", timer.label, secs);
    if (!fits(message))
        message = std::format("{}s", secs);
    if (!fits(message))
    {
        TRACE("hud", "seat_timer_suppressed label=%s x=%d limit=%d",
              timer.label, static_cast<int>(x),
              static_cast<int>(right_limit));
        return;
    }

    s->text_normal.write_xy(x, row, message.c_str(),
                            static_cast<unsigned char>(YELLOW),
                            static_cast<short>(1));
    TRACE("hud", "seat_timer label=%s ticks=%d secs=%d row=%d text=%s",
          timer.label, timer.ticks, secs, static_cast<int>(row),
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

// THE one copy of the upper-left HUD caption rule (a myguy's name wins even
// when blank, else a non-empty stats name, else the family word: the family
// registry's display name for ANY registered family, class-pack families
// past the core span included, through og::ui::family_display_name).
// new_score_panel draws it at lm+3 on the tm+4 row; draw_mode_panel measures
// it to find where its own row may start, so the two must agree exactly.
// The follow banner keeps its own blank-skipping variant below.
static std::string hud_display_name(const walker* control)
{
    if (control->myguy)
        return control->myguy->name;
    if (!control->stats()->name.empty())
        return control->stats()->name;
    return og::ui::family_display_name(static_cast<int>(control->family()));
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
// topmost row is viewport-local 30, i.e. tm + 30 - OVERSCAN_PADDING, which
// is tm+30 with the padding at 0. text.png is a 5x6 font, so a row at tm+4
// ends on tm+9 — 20 clear scanlines. The two can never share a scanline.
//
// Horizontal window: the classic HUD owns the caption at lm+3 and the
// TEAM/FOES counter box on the SAME tm+4 row, so the mode row takes the
// channel between them. The left edge clears both the caption's own button
// box (lm+1..lm+63) and the caption text itself, measured through the
// shared hud_display_name; while the FOES column is on, the right edge stops
// 3 px short of the counter box's LEFT edge (rm-60 for the classic 55-px
// box; the box grows leftward to fit its widest row, so the box's edge is
// this row's input, not a second constant — openglad/interface/
// hud_counter_box.h, and the plan new_score_panel hands in below). With no
// classic HUD (dead / AI-followed / spectated view) the row takes the whole
// pane, lm+2..rm-2. Both bounds derive from this pane's own lm/rm, so the R1
// clamp holds: the row can never paint into a neighboring viewport.
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
                            Sint32 rm, Sint32 counter_box_left)
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
            right = counter_box_left - 3;
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
        // A hidden beacon target gives nothing away to other teams.
        if (target->hidden() && (view->control == nullptr ||
                                 target->team_num() != view->control->team_num()))
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

// New Ability Select's HUD (docs/ability-select-design.md) shows the three
// SPEC keys of the layer the seat's PHYSICAL SHIFTER picks (held: SHIFTER +
// 1/2/3; up: 1/2/3). Not the mirrored shifter_down: under the belt that is
// the alternate bit of the last cast, not the key. Two pictures of it:
//
// The hotbar (plan_belt_strip, below): three 18px slots side by side, each
// the ability's 16x16 icon (og::ui::AbilityIcons) on a dark fill in a 1px
// frame, the key lettered under it. No ability name is drawn.
//
// The text rows (plan_belt_rows), the fallback for a pane too short or too
// narrow for the strip (the 100px inset columns, the 80-scanline VIEW_3):
// one row per key, "<key> <NAME>" cut by og::ui::belt_hud_line. The compact
// panel (4 seats; P2/P3 of 3) puts them at bm-24/-16/-8 inside the box the
// score mode already draws (SC/XP are hidden there); the full panel stacks
// them at bm-40/-32/-24 above SC (bm-8) and XP (bm-16), growing the box's
// top to bm-kBeltHudRowsBoxTop (bm-42, which moves the frozen-time and
// seat-timer cells up to bm-50). Budget: 15 glyphs ("S1 " + 12), 24 in a
// pane 300px or wider.
//
// Fit or yield, the frozen-time cell's rule: a row draws only below the
// notification banner; while it shares the radar's band (rm-64..rm across
// bm-48..bm-2) its text stops left of rm-64; belt_hud_line drops to the key
// label alone ("1", "S1") with fewer than 4 name glyphs left; with no room
// for even that the row is not drawn (trace belt_row_suppressed). The box
// shrinks to the rows that drew.
struct BeltHudRow
{
    int key = 0;          // 1..3
    bool shift = false;   // the SHIFTER layer
    Sint32 y = 0;
    std::string text;     // "" when the row yields
    unsigned char ink = 0;
    bool shadow = false;  // a 1-px black shadow under the glyphs (dim, no box)
};

// A key that cannot cast (locked, empty, specials switched off) reads dim,
// apart from a ready key, yet stays legible on whatever is behind it. The
// legacy GREY ramp (palette 23..30, 132..216) is the box's own grey (164)
// and the stone floors' greys, so it vanished on both. In the box the dim
// ink is a dark grey ramp (palette 17: 60/84/108 at the glyph's shades),
// grey against the ready rows' blue; with no box (the shipped OVERLAY-off
// look) it is a light grey ramp (palette 27: 180/204/228) over a 1-px
// black silhouette down-left (write_xy_flat: one ink, no ramp), the
// classic write_xy_shadow offset, so the row reads on stone, grass and
// dark ground alike.
inline constexpr unsigned char kBeltHudDimInkBox = 17;
inline constexpr unsigned char kBeltHudDimInkPlain = 27;
inline constexpr unsigned char kBeltHudShadowInk = PURE_BLACK;

inline constexpr int kBeltHudChars = 15;
inline constexpr int kBeltHudWideChars = 24;
inline constexpr Sint32 kBeltHudWidePane = 300;
inline constexpr Sint32 kBeltHudBoxRight = 98;
inline constexpr Sint32 kBeltHudWideBoxRight = 150;
// The box's right edge is at most rm-66: the radar block starts at rm-64,
// and rm-65 stays a blank column between them.
inline constexpr Sint32 kBeltHudRadarGap = 66;
// The bottom-left box's top edge, as bm minus: classic (and the compact
// panel's text rows) / the full panel's text rows / the full panel's strip /
// the compact panel's strip. kHudBoxTop is the setting-off HUD's too, so it
// never moves.
inline constexpr Sint32 kHudBoxTop = 26;
inline constexpr Sint32 kBeltHudRowsBoxTop = 42;
inline constexpr Sint32 kBeltHudFullBoxTop = 46;
inline constexpr Sint32 kBeltHudCompactBoxTop = 28;

// The hotbar. A slot (cell) is a kBeltIconSize icon inside a 1px frame,
// cells kBeltCellPitch apart from lm+2: x = lm+2, lm+21, lm+40, the strip's
// last column lm+57. The key label's top row sits kBeltLabelGap rows under
// the cell's last row (one blank row; the 5x6 font inks rows 0-4), so the
// strip is kBeltStripH tall from the cells' top to the label's last row.
//   full panel     cells bm-44..bm-27, labels bm-25..bm-20 (XP at bm-16)
//   compact panel  cells bm-26..bm-9,  labels bm-7..bm-2
inline constexpr Sint32 kBeltIconSize = og::ui::kAbilityIconSize;  // 16
inline constexpr Sint32 kBeltCellSize = kBeltIconSize + 2;          // 18
inline constexpr Sint32 kBeltCellPitch = kBeltCellSize + 1;         // 19
inline constexpr Sint32 kBeltStripLeft = 2;
inline constexpr Sint32 kBeltStripW = 2 * kBeltCellPitch + kBeltCellSize;  // 56
inline constexpr Sint32 kBeltLabelGap = 2;
inline constexpr Sint32 kBeltLabelH = 6;
inline constexpr Sint32 kBeltStripH =
    kBeltCellSize - 1 + kBeltLabelGap + kBeltLabelH;  // 25
inline constexpr Sint32 kBeltFullCellTop = 44;
inline constexpr Sint32 kBeltCompactCellTop = 26;
// The compact box's right edge: the strip (lm+57) plus its bevel.
inline constexpr Sint32 kBeltHudCompactBoxRight = 59;
// One ground for every icon (the locked LUT, the icon gates and the
// ABILITIES face are all designed against it): dark grey 18 in a black 16
// frame. The frame says the state: red while the MP cannot pay, white while
// the seat holds the slot's SPEC key.
inline constexpr unsigned char kBeltCellFill = 18;
inline constexpr unsigned char kBeltFrameInk = 16;
inline constexpr unsigned char kBeltFrameShortInk = 40;
inline constexpr unsigned char kBeltFramePressedInk = 31;
static_assert(kBeltStripW == 56 && kBeltStripH == 25,
              "the strip the design's fit table was measured for");

// Split screen (two or more local seats, every pane) draws the strip at half
// its size with every pixel kept: the cells, frames and key labels go into a
// HUD sprite plane of kBeltHalfPlaneW x kBeltHalfPlaneH, the strip plus a
// one-pixel margin left (the first label's shadow lands at x-1) and below
// (the shadow's row), composited onto a canvas rectangle half that size at
// (lm+1, bm-kBeltHalf*CellTop). Its pixels are the display's own: one screen
// pixel each at a 640x400 window, two at 1280x800. A window under 2 screen
// pixels per canvas pixel would have fewer screen pixels than the icon has,
// so it keeps the full-size strip. Inside the plane the cells sit at x 1,
// 20, 39, rows 0-17, the labels' top at row 19.
//   full panel     plane bm-32..bm-20, box top bm-34 (XP at bm-16)
//   compact panel  plane bm-16..bm-4,  box bm-18..bm-2, right lm+31
inline constexpr Sint32 kBeltHalfPlaneW = kBeltStripW + 2;  // 58
inline constexpr Sint32 kBeltHalfPlaneH = kBeltStripH + 1;  // 26
inline constexpr Sint32 kBeltHalfFullCellTop = 32;
inline constexpr Sint32 kBeltHalfCompactCellTop = 16;
inline constexpr Sint32 kBeltHudHalfFullBoxTop = 34;
inline constexpr Sint32 kBeltHudHalfCompactBoxTop = 18;
inline constexpr Sint32 kBeltHudHalfCompactBoxRight = 31;
inline constexpr float kBeltHalfMinScale = 2.0f;
static_assert(kBeltHalfPlaneW % 2 == 0 && kBeltHalfPlaneH % 2 == 0,
              "the plane lands on whole canvas pixels at half size");

// See-through slots, each cell pixel written once (no fill under the
// picture): the dark fill where the picture is transparent at 44 %, the
// picture and the black frame at 75 % (at 50 % the grey pictures turn green
// on grass), the red and white state frames and the key labels solid (at
// 50 % a red frame over grass is brown and the half-size labels vanish).
// Every seat count, one player included.
struct BeltAlpha
{
    unsigned char fill = 255;
    unsigned char icon = 255;
    unsigned char frame = 255;  // the red and white state frames
};
inline constexpr unsigned char kBeltFillAlpha = 112;
inline constexpr unsigned char kBeltIconAlpha = 192;
inline constexpr BeltAlpha kBeltAlpha{kBeltFillAlpha, kBeltIconAlpha, 255};

static std::array<BeltHudRow, 3> plan_belt_rows(screen* s, walker& control,
                                                int players, bool compact,
                                                Sint32 lm, Sint32 tm,
                                                Sint32 rm, Sint32 bm,
                                                unsigned char text_color,
                                                bool boxed)
{
    namespace belt = og::sim::belt;
    const FamilyDescriptor* const fd = get_family_descriptor(
        static_cast<int>(control.family()));
    const belt::Layout layout =
        belt::resolve(fd, control.stats()->level(), s->world_.new_specials,
                      belt::bytes_for(control));
    const bool shift = isPlayerHoldingKey(players, KEY_SHIFTER);

    const Sint32 banner_bottom = tm + 30 + MAX_MESSAGES * 6;
    const Sint32 first_row = compact ? bm - 24 : bm - 40;
    const int budget = (!compact && rm - lm >= kBeltHudWidePane)
        ? kBeltHudWideChars
        : kBeltHudChars;
    const Sint32 x = lm + 2;

    std::array<BeltHudRow, 3> rows;
    for (int i = 0; i < 3; ++i)
    {
        BeltHudRow& row = rows[static_cast<std::size_t>(i)];
        row.key = i + 1;
        row.shift = shift;
        row.y = first_row + 8 * i;
        if (row.y < banner_bottom)
        {
            TRACE("hud", "belt_row_suppressed key=%d", row.key);
            continue;
        }
        const Sint32 right_limit = (row.y + 6 > bm - 48) ? (rm - 64) : rm;
        const int width = std::min<int>(
            budget, std::max<int>(0, static_cast<int>((right_limit - x) / 6)));
        const belt::Resolved& slot =
            layout[static_cast<std::size_t>(belt::slot_index(row.key, shift))];
        row.text = og::ui::belt_hud_line(slot, row.key, shift, width);
        if (row.text.empty())
        {
            TRACE("hud", "belt_row_suppressed key=%d", row.key);
            continue;
        }
        if (control.specials_disabled() ||
            slot.state != belt::SlotState::Castable || !slot.entry)
        {
            row.ink = boxed ? kBeltHudDimInkBox : kBeltHudDimInkPlain;
            row.shadow = !boxed;
        }
        else if (control.stats()->magicpoints() >=
                 og::sim::cast_cost(control, slot.entry->slot,
                                    slot.entry->alt))
            row.ink = text_color;
        else
            row.ink = static_cast<unsigned char>(RED);
    }
    return rows;
}

// One hotbar slot as it is drawn.
struct BeltHudCell
{
    int key = 0;                        // 1..3
    bool shift = false;                 // the SHIFTER layer
    Sint32 x = 0;                       // the cell's top-left, frame included
    Sint32 y = 0;
    og::sim::belt::Entry entry{};       // slot 0: an empty cell
    const char* declared = "none";      // the icon path the ability names
    const char* drawn = "none";         // the path whose pixels the cell shows
    const PixieData* picture = nullptr; // the ready or the locked copy
    const char* state = "empty";        // castable, short, locked, disabled
    bool pressed = false;               // the seat holds this SPEC key
    unsigned char frame = kBeltFrameInk;
    std::string label;                  // "1", "S1", or "L7" when locked
    Sint32 label_x = 0;
    Sint32 label_y = 0;
    unsigned char ink = 0;
    bool shadow = false;                // a 1-px black shadow (no box)
};

struct BeltHudStrip
{
    bool fits = false;
    bool half = false;   // drawn into a HUD sprite plane at half size
    Sint32 top = 0;      // the cells' top row (the plane's, when half)
    Sint32 dst_y = 0;    // the strip's top on the canvas
    Sint32 box_top = 0;  // the bottom-left box's top edge
    std::array<BeltHudCell, 3> cells{};
};

// Where the strip goes and whether it fits, in this order (the text rows'
// fit-or-yield rule): its box's top bevel (two rows above the cells, so
// the box look never lands on the feed) at or below the feed floor
// (tm+60), and its last column left of rm-kBeltHudRadarGap, the radar
// reserve's edge, which stays reserved with the radar off so the radar
// pref never moves the strip. A pane narrower than 124px or shorter than
// the feed plus the strip and its bevel (two seats at PREF_VIEW_2, 104
// scanlines, the box at bm-46 = tm+58) takes the text rows instead.
//
// At half size (split screen) the same rule with the half constants: the
// box's top at bm-34 (full) or bm-18 (compact), the strip's last column
// lm+29, so the 100px columns and the 104-scanline two-seat pane take it.
static BeltHudStrip place_belt_strip(bool compact, bool half, Sint32 lm,
                                     Sint32 tm, Sint32 rm, Sint32 bm)
{
    BeltHudStrip strip;
    strip.half = half;
    if (half)
        strip.top = bm - (compact ? kBeltHalfCompactCellTop
                                  : kBeltHalfFullCellTop);
    else
        strip.top = bm - (compact ? kBeltCompactCellTop : kBeltFullCellTop);
    strip.dst_y = strip.top;
    Sint32 box_top =
        bm - (compact ? kBeltHudCompactBoxTop : kBeltHudFullBoxTop);
    if (half)
        box_top = bm - (compact ? kBeltHudHalfCompactBoxTop
                                : kBeltHudHalfFullBoxTop);
    strip.box_top = box_top;
    const Sint32 banner_bottom = tm + 30 + MAX_MESSAGES * 6;
    const Sint32 right = half ? lm + kBeltHalfPlaneW / 2
                              : lm + kBeltStripLeft + kBeltStripW - 1;
    strip.fits = box_top >= banner_bottom && right < rm - kBeltHudRadarGap;
    if (!strip.fits)
        TRACE("hud", "belt_strip_yields top=%d feed=%d right=%d limit=%d",
              static_cast<int>(box_top), static_cast<int>(banner_bottom),
              static_cast<int>(right),
              static_cast<int>(rm - kBeltHudRadarGap));
    return strip;
}

// The three slots of the layer the seat's SHIFTER picks. The frame says the
// state, the picture the ability, the label the key:
//   castable  the icon, black frame, the key in the panel's ink
//   short     the icon, red frame, the key in RED (the MP cannot pay)
//   pressed   the frame white while the seat holds the slot's SPEC key; it
//             wins over short (the label keeps its red) and lights only a
//             castable or short slot. It follows the physical key, so a
//             latched SHIFTER cast released mid-hold lights the plain slot.
//   locked    the grey copy, black frame, "L<level>" in the dim ink
//   disabled  the grey copy, black frame, the key in the dim ink (a walker
//             whose specials the level switched off)
//   empty     the fill alone, black frame, the key in the dim ink
// An ability with no icon, or one whose file does not load, shows the
// generic tile; with not even that, the cell is its frame and its label.
static void plan_belt_strip(BeltHudStrip& strip, screen* s, walker& control,
                            int players, Sint32 lm,
                            unsigned char text_color, bool boxed)
{
    namespace belt = og::sim::belt;
    const FamilyDescriptor* const fd = get_family_descriptor(
        static_cast<int>(control.family()));
    const belt::Layout layout =
        belt::resolve(fd, control.stats()->level(), s->world_.new_specials,
                      belt::bytes_for(control));
    const bool shift = isPlayerHoldingKey(players, KEY_SHIFTER);
    static constexpr std::array<int, 3> kSpecKeys = {KEY_SPECIAL, KEY_SPEC2,
                                                     KEY_SPEC3};
    const unsigned char dim = boxed ? kBeltHudDimInkBox : kBeltHudDimInkPlain;
    // The session's icon set (screen::ability_icons), the one the ABILITIES
    // screen draws from too. Its pointers are used within this frame, never
    // kept.
    og::ui::AbilityIcons& icons = s->ability_icons();

    for (int i = 0; i < 3; ++i)
    {
        BeltHudCell& cell = strip.cells[static_cast<std::size_t>(i)];
        cell.key = i + 1;
        cell.shift = shift;
        cell.x = lm + kBeltStripLeft + kBeltCellPitch * i;
        cell.y = strip.top;
        cell.shadow = !boxed;
        const int index = belt::slot_index(cell.key, shift);
        const belt::Resolved& slot = layout[static_cast<std::size_t>(index)];
        cell.label = og::ui::ability_key_label(index);
        cell.ink = dim;

        if (fd != nullptr && slot.entry &&
            slot.state != belt::SlotState::Empty)
        {
            cell.entry = *slot.entry;
            if (const char* path = og::ui::declared_icon_path(*fd, cell.entry))
                cell.declared = path;
            const og::ui::AbilityIcon* const icon =
                icons.icon(*fd, cell.entry);
            if (icon != nullptr)
                cell.drawn = icon == icons.icon_at(nullptr)
                    ? og::ui::kMissingIconPath
                    : cell.declared;

            if (slot.state == belt::SlotState::Locked)
            {
                cell.state = "locked";
                cell.label = std::format("L{}", slot.opens_at);
                cell.picture = icon ? &icon->locked : nullptr;
            }
            else if (control.specials_disabled())
            {
                cell.state = "disabled";
                cell.picture = icon ? &icon->locked : nullptr;
            }
            else
            {
                const bool paid = control.stats()->magicpoints() >=
                    og::sim::cast_cost(control, cell.entry.slot,
                                       cell.entry.alt);
                cell.state = paid ? "castable" : "short";
                cell.ink = paid ? text_color
                                : static_cast<unsigned char>(RED);
                cell.frame = paid ? kBeltFrameInk : kBeltFrameShortInk;
                cell.picture = icon ? &icon->ready : nullptr;
                cell.pressed = isPlayerHoldingKey(
                    players, kSpecKeys[static_cast<std::size_t>(i)]);
                if (cell.pressed)
                    cell.frame = kBeltFramePressedInk;
            }
        }

        // Centred under the cell: a glyph is 6px with a blank last column.
        const Sint32 label_w = 6 * static_cast<Sint32>(cell.label.size()) - 1;
        cell.label_x = cell.x + (kBeltCellSize - label_w) / 2;
        cell.label_y = cell.y + kBeltCellSize - 1 + kBeltLabelGap;
    }
}

// Each cell pixel is written once, at its element's alpha (BeltAlpha): the
// 18x18 ring (the black frame at the picture's alpha, a state frame at
// `frame`), then the 16x16 inside in one pass, the fill only where the
// picture is transparent. The labels are solid.
static void draw_belt_strip(screen* s, text& mytext,
                            [[maybe_unused]] int view,
                            const BeltHudStrip& strip, BeltAlpha alpha)
{
    for (const BeltHudCell& cell : strip.cells)
    {
        const unsigned char ring_alpha =
            cell.frame == kBeltFrameInk ? alpha.icon : alpha.frame;
        for (Sint32 y = 0; y < kBeltCellSize; ++y)
            for (Sint32 x = 0; x < kBeltCellSize; ++x)
                if (y == 0 || y == kBeltCellSize - 1 || x == 0 ||
                    x == kBeltCellSize - 1)
                    s->pointb(cell.x + x, cell.y + y, cell.frame, ring_alpha);
        for (Sint32 y = 0; y < kBeltIconSize; ++y)
            for (Sint32 x = 0; x < kBeltIconSize; ++x)
            {
                const unsigned char p = cell.picture != nullptr
                    ? cell.picture->data[static_cast<std::size_t>(
                          y * kBeltIconSize + x)]
                    : 0;
                if (p == 0)
                    s->pointb(cell.x + 1 + x, cell.y + 1 + y, kBeltCellFill,
                              alpha.fill);
                else
                    s->pointb(cell.x + 1 + x, cell.y + 1 + y, p, alpha.icon);
            }
        TRACE("hud",
              "belt_cell key=%d shift=%d slot=%d alt=%d icon=%s state=%s "
              "x=%d y=%d view=%d pressed=%d frame=%d drawn=%s",
              cell.key, cell.shift ? 1 : 0, static_cast<int>(cell.entry.slot),
              cell.entry.alt ? 1 : 0, cell.declared, cell.state,
              static_cast<int>(cell.x), static_cast<int>(cell.y), view,
              cell.pressed ? 1 : 0, static_cast<int>(cell.frame), cell.drawn);
    }
    for (const BeltHudCell& cell : strip.cells)
    {
        if (cell.shadow)
            mytext.write_xy_flat(cell.label_x - 1, cell.label_y + 1,
                                 cell.label, kBeltHudShadowInk);
        mytext.write_xy(cell.label_x, cell.label_y, cell.label.c_str(),
                        cell.ink, static_cast<short>(1));
        TRACE("hud", "belt_label key=%d text=%s ink=%d shadow=%d", cell.key,
              cell.label.c_str(), static_cast<int>(cell.ink),
              cell.shadow ? 1 : 0);
    }
}

// The hotbar of one pane: on the canvas at full size, or (split screen)
// into its HUD sprite plane at (lm+1, dst_y), 29x13 canvas pixels holding
// the 58x26 strip. A plane that does not open (a bug, not a mode: the
// decision already asked hud_sprite_scale) draws the strip full size at
// today's place instead, never a blank HUD.
static void draw_belt_hotbar(screen* s, text& mytext, int view,
                             BeltHudStrip& strip, walker& control,
                             int players, bool compact, Sint32 lm, Sint32 bm,
                             unsigned char text_color, bool boxed,
                             [[maybe_unused]] float scale)
{
    TRACE("hud", "belt_strip half=%d dst_x=%d dst_y=%d scale=%.2f view=%d",
          strip.half ? 1 : 0, static_cast<int>(lm + 1),
          static_cast<int>(strip.dst_y), static_cast<double>(scale), view);
    if (!strip.half)
    {
        draw_belt_strip(s, mytext, view, strip, kBeltAlpha);
        return;
    }
    const NativeWorldViewDestination destination{
        CanvasTarget::GameplayUI, static_cast<int>(lm + 1),
        static_cast<int>(strip.dst_y), kBeltHalfPlaneW / 2,
        kBeltHalfPlaneH / 2};
    if (s->begin_native_hud_sprite(destination, kBeltHalfPlaneW,
                                   kBeltHalfPlaneH))
    {
        draw_belt_strip(s, mytext, view, strip, kBeltAlpha);
        s->end_native_hud_sprite();
        return;
    }
    TRACE("hud", "belt_half_fallback reason=no_plane view=%d", view);
    strip.half = false;
    strip.top = bm - (compact ? kBeltCompactCellTop : kBeltFullCellTop);
    plan_belt_strip(strip, s, control, players, lm, text_color, boxed);
    draw_belt_strip(s, mytext, view, strip, kBeltAlpha);
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
    // overlay's dynamic placement (the box grows with the WAVE and FLR
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

        // The counter box's horizontal plan, computed BEFORE anything on the
        // tm+4 row is drawn: the mode row's right edge is the box's LEFT edge
        // (draw_mode_panel just below), and the box itself is drawn last. One
        // fit rule sizes it to its widest row and fits every row to the
        // result — openglad/interface/hud_counter_box.h. Absent (and the
        // classic rm-57 edge stands in for the mode row) whenever this
        // viewport draws no counter box at all.
        std::optional<og::hud_counter_box::Layout> counter_box;
        bool counter_show_wave = false;
        bool counter_show_floor = false;
        int counter_awake_foes = 0;
        int counter_wave_foes = 0;
        std::uint32_t counter_wave_seconds = 0;
        std::string counter_floor_label;
        if (control && !control->dead() && control->user() != -1 &&
            s->viewob[players]->prefs[PREF_FOES] == PREF_FOES_ON)
        {
            // Get current number of foes
            tempfoes = remaining_foes(s, control);
            // Get current number of team-members. team_num() arrives on the
            // same raw-byte snapshot channel as family()/current_special()
            // below, but it is an *unsigned* byte and remaining_team() takes a
            // signed char: a mirrored team above 127 narrows to a negative
            // value that matches no walker, so the HUD would report zero
            // allies. Junk bytes above MAX_TEAM clamp to team 0, as the
            // scenario loader does (sanitize_loaded_team_num). The FFA band
            // (kFfaTeamBase + c, 16..31) is a real scoring identity above
            // MAX_TEAM and passes through, so a fighter counts its own band.
            int hud_team = static_cast<int>(control->team_num());
            if (hud_team > MAX_TEAM && !og::sim::is_scoring_identity(hud_team))
                hud_team = 0;
            tempallies = remaining_team(s, static_cast<char>(hud_team));

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
            counter_awake_foes = std::max(
                0, static_cast<int>(tempfoes) - static_cast<int>(pending_foes));
            counter_wave_foes = static_cast<int>(pending_foes) +
                static_cast<int>(pending_respawn_foes);
            counter_show_wave = counter_wave_foes > 0;
            counter_wave_seconds = static_cast<std::uint32_t>(
                og::sim::timer_seconds(static_cast<int>(next_wake_ticks)));

            // Floor row (feature B): drawn only when the shared label is
            // non-empty, so single-floor frames stay byte-identical. Call
            // sites key on non-empty, never on floor_count (game modes
            // re-label inside floor_hud_label).
            counter_floor_label = floor_hud_label(
                s->world_, static_cast<int>(control->floor()));
            counter_show_floor = !counter_floor_label.empty();

            std::array<std::optional<og::hud_counter_box::Row>,
                       og::hud_counter_box::kMaxRows> rows;
            rows[0] = og::hud_counter_box::split_row(
                std::format("TEAM: {}", tempallies));
            rows[1] = og::hud_counter_box::split_row(
                counter_show_wave
                    ? std::format("FOES: {}+{}", counter_awake_foes,
                                  counter_wave_foes)
                    : std::format("FOES: {}", tempfoes));
            if (counter_show_wave)
                rows[2] = og::hud_counter_box::split_row(
                    std::format("WAVE: {}s", counter_wave_seconds));
            if (counter_show_floor)
                rows[3] = og::hud_counter_box::split_row(counter_floor_label);
            counter_box = og::hud_counter_box::fit(rows, lm, rm);
        }

        // Scripted-mode (TYPE_SCRIPTED) worlds render the generic ModeState
        // HUD (mode Lua owns the scoreboard text).
        const bool scripted_mode_hud =
            (s->world_.type & GameWorld::TYPE_SCRIPTED) &&
            s->world_.mode.active;
        if (scripted_mode_hud)
        {
            draw_mode_panel(
                s, s->viewob[players].get(), lm, tm, rm,
                counter_box ? static_cast<Sint32>(counter_box->left)
                            : rm - og::hud_counter_box::kClassicLeftInset);
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
                follow_name = og::ui::family_display_name(
                    static_cast<int>(control->family()));
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
            // Deliberately keyed on numviews (humans), never on the camera-
            // inclusive layout_pane_count() — docs/camera-views-design.md
            // constraint 7 (a docked camera must not densify seat HUDs).
            const bool compact_score_panel =
                s->numviews > 2 && !(s->numviews == 3 && players == 0);
            // New Ability Select draws the hotbar where it fits
            // (place_belt_strip) and the belt rows where it does not; both
            // grow the bottom-left box (kBeltHud*BoxTop above).
            const bool belt_hud = og::input::ability_select_on();
            // Split screen (two or more local seats; a network client has
            // one) draws the hotbar at half size when the display has at
            // least two pixels per canvas pixel for it.
            const float belt_scale = belt_hud ? s->hud_sprite_scale() : 0.0f;
            const bool belt_half =
                s->numviews >= 2 && belt_scale >= kBeltHalfMinScale;
            BeltHudStrip belt_strip;
            if (belt_hud)
                belt_strip = place_belt_strip(compact_score_panel, belt_half,
                                              lm, tm, rm, bm);
            Sint32 box_top = bm - kHudBoxTop;
            if (belt_strip.fits)
                box_top = belt_strip.box_top;
            else if (belt_hud && !compact_score_panel)
                box_top = bm - kBeltHudRowsBoxTop;
            const bool freeze_drew =
                draw_freeze_countdown(s, lm, tm, rm, bm, box_top);
            draw_seat_timer(s, *control, lm, tm, rm, bm, box_top, freeze_drew);

            // Get the button-drawing info ..
            draw_button = s->viewob[players]->prefs[PREF_OVERLAY];
            if (draw_button)
                text_color = DARK_BLUE;
            else
                text_color = YELLOW;

            // family()/current_special() may be attacker-controlled on a
            // network mirror (set from raw int8 snapshot bytes), so they can be
            // any value in [-128,127]. The family is never used as an index:
            // its words come from its registry descriptor, and a family with
            // no descriptor (unregistered or out of range) names no special.
            // The special IS an index (the descriptor's name arrays and
            // special_cost), so it is clamped to prevent out-of-bounds reads.
            // In-range values pass through unchanged.
            const int fam = static_cast<int>(control->family());
            static_assert(FD_NUM_SPECIALS == NUM_SPECIALS,
                          "spc indexes the descriptor's special name arrays");
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
            // the seconds left (og::sim::timer_seconds). Drawn just below the
            // HP/MP rows (tm+10/tm+18); disappears the tick the queue drains.
            const int scared_ticks = hud_scared_flee_ticks(control);
            if (scared_ticks > 0)
            {
                const int scared_seconds =
                    og::sim::timer_seconds(scared_ticks);
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

            int special_y = score_bottom + special_offset;
            TRACE("hud", "panel_density view=%d compact=%d",
                  players, compact_score_panel ? 1 : 0);
            // Don't show score and XP (clutter) when in a small viewport
            if (compact_score_panel)
                special_y = score_bottom - 8;

            const bool show_score =
                s->viewob[players]->prefs[PREF_SCORE] == PREF_SCORE_ON;

            std::array<BeltHudRow, 3> belt_rows{};
            if (belt_strip.fits)
            {
                // At half size the strip is planned in its plane: cell 0's
                // frame at x 1, the cells' top at row 0.
                if (belt_strip.half)
                    belt_strip.top = 0;
                plan_belt_strip(belt_strip, s, *control, players,
                                belt_strip.half ? -1 : lm,
                                static_cast<unsigned char>(text_color),
                                draw_button != 0);
            }
            else if (belt_hud)
                belt_rows = plan_belt_rows(s, *control, players,
                                           compact_score_panel, lm, tm, rm,
                                           bm, static_cast<unsigned char>(text_color),
                                           draw_button != 0);
            // The rows that drew, top and bottom (no rows: 0/0).
            Sint32 belt_top = 0;
            Sint32 belt_bottom = 0;
            for (const BeltHudRow& row : belt_rows)
            {
                if (row.text.empty())
                    continue;
                if (belt_top == 0)
                    belt_top = row.y;
                belt_bottom = row.y + 6;
            }

            // Draw box, if needed
            if (draw_button && belt_strip.fits)
            {
                // The hotbar's box: from box_top down to the score box's
                // bottom when SC/XP (or the compact panel) sit there, else
                // to the bevel under the key labels. Its right edge is the
                // text rows' in a full panel (they shared it), the strip's
                // own plus its bevel in a compact one; never past rm-66.
                const Sint32 strip_right = compact_score_panel
                    ? lm + (belt_strip.half ? kBeltHudHalfCompactBoxRight
                                            : kBeltHudCompactBoxRight)
                    : lm + (rm - lm >= kBeltHudWidePane
                                ? kBeltHudWideBoxRight
                                : kBeltHudBoxRight);
                const Sint32 box_right =
                    std::min<Sint32>(strip_right, rm - kBeltHudRadarGap);
                const Sint32 bottom = (show_score || compact_score_panel)
                    ? score_bottom - 2
                    : belt_strip.dst_y + (belt_strip.half
                                              ? kBeltHalfPlaneH / 2
                                              : kBeltStripH);
                s->draw_button(lm+1, box_top, box_right, bottom, 1, 1);
                TRACE("hud", "belt_box view=%d x1=%d y1=%d x2=%d y2=%d",
                      players, static_cast<int>(lm + 1),
                      static_cast<int>(box_top), static_cast<int>(box_right),
                      static_cast<int>(bottom));
            }
            else if (draw_button && belt_hud)
            {
                // The belt box: the rows that drew, down to the score box's
                // bottom when SC/XP (or the compact panel's rows) sit there.
                // Its right bevel stops one blank column short of the
                // radar's reserved block (rm-64, the frozen-time cell's
                // rule; reserved with the radar off too): every
                // box row lies in the radar's band (bm-48..bm-2), and in a
                // pane narrower than 164px the classic lm+98 edge would run
                // into it (it starts at lm+95 in a two-seat pane).
                const Sint32 box_right = std::min<Sint32>(
                    lm + ((!compact_score_panel && rm - lm >= kBeltHudWidePane)
                              ? kBeltHudWideBoxRight
                              : kBeltHudBoxRight),
                    rm - kBeltHudRadarGap);
                Sint32 top = belt_top - 2;
                Sint32 bottom = belt_bottom;
                if (show_score || compact_score_panel)
                {
                    bottom = score_bottom - 2;
                    if (belt_top == 0)
                        top = score_bottom - 26;
                }
                if (belt_top != 0 || show_score)
                {
                    s->draw_button(lm+1, top, box_right, bottom, 1, 1);
                    TRACE("hud", "belt_box view=%d x1=%d y1=%d x2=%d y2=%d",
                          players, static_cast<int>(lm + 1),
                          static_cast<int>(top), static_cast<int>(box_right),
                          static_cast<int>(bottom));
                }
            }
            else if (draw_button)
            {
                if (show_score)
                {
                    s->draw_button(lm+1, score_bottom-26, lm+98,
                                   score_bottom-2, 1, 1);
                }
                else
                {
                    Sint32 special_box_bottom = special_y + 6;
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

            if (belt_strip.fits)
                draw_belt_hotbar(s, mytext, players, belt_strip, *control,
                                 players, compact_score_panel, lm, bm,
                                 static_cast<unsigned char>(text_color),
                                 draw_button != 0, belt_scale);
            else if (belt_hud)
            {
                for (const BeltHudRow& row : belt_rows)
                {
                    if (row.text.empty())
                        continue;
                    if (row.shadow)
                        mytext.write_xy_flat(lm+1, row.y+1, row.text,
                                             kBeltHudShadowInk);
                    mytext.write_xy(lm+2, row.y, row.text.c_str(), row.ink,
                                    static_cast<short>(1));
                    TRACE("hud", "belt_row key=%d shift=%d text=%s ink=%d shadow=%d",
                          row.key, row.shift ? 1 : 0, row.text.c_str(), static_cast<int>(row.ink),
                          row.shadow ? 1 : 0);
                }
            }
            else
            {
                // Currently-select special
                // Alternate special name (if not "NONE")
                // (New Specials: the names as this session's setting shows them.)
                const FamilyDescriptor* spc_fd = get_family_descriptor(fam);
                const char* spc_name = og::sim::special_name(spc_fd, spc, s->world_.new_specials);
                const char* spc_alternate = og::sim::alternate_name(spc_fd, spc, s->world_.new_specials);
                if (control->shifter_down() &&
                    std::strcmp(spc_alternate, kSpecialNameNone) != 0)
                    message = std::format("SPC: {}", spc_alternate);
                else
                    message = std::format("SPC: {}", spc_name);
                TRACE("hud", "spc_row fam=%d text=%s", fam, message.c_str());

                // Disabled-special signifier: a walker whose specials are
                // switched off (the level's NPC flag) can NEVER fire the
                // listed special, no matter its MP — grey the line out so it
                // reads as unavailable rather than merely unaffordable (RED).
                if (control->specials_disabled())
                {
                    mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(GREY), static_cast<short>(1));
                    TRACE("hud", "spc_disabled fam=%d spc=%d", fam, spc);
                }
                else if (control->stats()->magicpoints() >= og::sim::cast_cost(*control, spc, control->shifter_down() != 0))
                    mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(text_color), static_cast<short>(1));
                else
                    mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(RED), static_cast<short>(1));
            }

            // Number of allies, upper right
            if (counter_box.has_value())
            {
                // Every row is drawn from the plan computed at the top of this
                // viewport's pass: one fit rule sized the box to its widest
                // row and fitted each row to the result, so all four sit at
                // the one shared text column instead of being right-aligned
                // to rm-2 with a per-row expression (hud_counter_box.h).
                Sint32 box_bottom = counter_show_wave ? 24 : 16;
                if (counter_show_floor)
                    box_bottom += 8;
                if (players == 0)
                    fps_below_y = tm + box_bottom;

                if (draw_button)
                    s->draw_button(counter_box->left, tm+1, rm-2,
                                   tm + box_bottom, 1, 1);

                mytext.write_xy(counter_box->text_x, tm+2,
                                counter_box->text[0].c_str(),
                                static_cast<unsigned char>(text_color),
                                static_cast<short>(1));

                // Number of foes, 2nd upper right
                mytext.write_xy(counter_box->text_x, tm+10,
                                counter_box->text[1].c_str(),
                                static_cast<unsigned char>(text_color),
                                static_cast<short>(1));

                if (counter_show_wave)
                {
                    mytext.write_xy(counter_box->text_x, tm+18,
                                    counter_box->text[2].c_str(),
                                    static_cast<unsigned char>(text_color),
                                    static_cast<short>(1));
                    TRACE("hud", "next_wave awake=%d pending=%d secs=%u",
                          counter_awake_foes,
                          counter_wave_foes,
                          static_cast<unsigned>(counter_wave_seconds));
                }

                if (counter_show_floor)
                {
                    const Sint32 floor_y = tm + (counter_show_wave ? 26 : 18);
                    mytext.write_xy(counter_box->text_x, floor_y,
                                    counter_box->text[3].c_str(),
                                    static_cast<unsigned char>(text_color),
                                    static_cast<short>(1));
                    // The SHARED label, never the fitted text: the trace is
                    // the readout's identity, not its pixels.
                    TRACE("hud", "floor %s", counter_floor_label.c_str());
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
