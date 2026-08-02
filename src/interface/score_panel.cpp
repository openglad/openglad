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
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>

#include <openglad/interface/game_context.h>

#include <openglad/core/test_trace.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

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
        for (const og::sim::CtfRespawnEntry& entry : world.ctf.respawn_queue)
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

// Flag-state glyph for the per-team CTF readout: that team's flag is at
// (H)ome, (T)aken by an enemy carrier, or (D)ropped in the field.
static char ctf_flag_state_glyph(const og::sim::CtfFlag& flag)
{
    switch (flag.state)
    {
        case og::sim::CtfFlagState::Carried: return 'T';
        case og::sim::CtfFlagState::Dropped: return 'D';
        case og::sim::CtfFlagState::AtHome:
        default: return 'H';
    }
}

// Shared CTF/classic countdown. Classic mode deliberately calls only this
// helper: capture scores, flags and waypoint progress remain CTF-only.
static void draw_respawn_countdown(screen* s, walker* control,
                                   Sint32 lm, Sint32 tm)
{
    if (control == nullptr || !control->dead())
        return;

    const og::sim::CtfState& ctf = s->world_.ctf;
    for (const og::sim::CtfRespawnEntry& entry : ctf.respawn_queue)
    {
        if (entry.kind != 0 || entry.walker_entity_id != control->entity_id())
            continue;
        const int seconds = og::sim::ctf_respawn_seconds_left(ctf, entry);
        const std::string message = std::format("RESPAWN IN {}", seconds);
        s->text_normal.write_xy(lm + 4, tm + 12, message.c_str(),
                                static_cast<unsigned char>(YELLOW),
                                static_cast<short>(1));
        break;
    }
}

// Per-viewport CTF overlay. Reads only replicated world state (CtfState rides
// the snapshot), so it works identically on the server and network mirrors.
static void draw_ctf_panel(screen* s, walker* control, Sint32 lm, Sint32 tm,
                           Sint32 rm)
{
    const og::sim::CtfState& ctf = s->world_.ctf;
    text& mytext = s->text_normal;

    // Capture counts. Suppressed in small (>2-way) split-screen panes; the
    // two larger layouts right-align the group ending at rm-60 so it clears
    // both 12-char names at lm+3 and the TEAM/FOES column at rm-55.
    if (s->numviews == 1)
    {
        // Full-width pane: one "<caps><flag-glyph>" segment per active team
        // in its team ramp color, 6px apart.
        std::array<std::string, 4> segments;
        Sint32 total_width = 0;
        for (int team = 0; team < 4; ++team)
        {
            if (!ctf.team_active[team])
                continue;
            segments[static_cast<std::size_t>(team)] = std::format(
                "{}{}",
                ctf.captures[team],
                ctf_flag_state_glyph(ctf.flags[team]));
            if (total_width > 0)
                total_width += 6;
            total_width += static_cast<Sint32>(segments[static_cast<std::size_t>(team)].size()) * 6;
        }
        Sint32 x = rm - 60 - total_width;
        for (int team = 0; team < 4; ++team)
        {
            if (segments[static_cast<std::size_t>(team)].empty())
                continue;
            mytext.write_xy(x, tm + 4, segments[static_cast<std::size_t>(team)].c_str(),
                            static_cast<unsigned char>(team * 16 + 40),
                            static_cast<short>(1));
            x += static_cast<Sint32>(segments[static_cast<std::size_t>(team)].size()) * 6 + 6;
        }
    }
    else if (s->numviews == 2)
    {
        // Half-width panes cannot fit the glyph segments beside the name:
        // compact digits-only group ("2:1:0:3", counts in team ramp colors,
        // neutral separators) on the tm+28 row, clear of the name and the
        // HP/MP rows. The carrier's FLAG! stays left at lm+2 on that row.
        std::array<std::string, 7> pieces;
        std::array<unsigned char, 7> piece_colors;
        int piece_count = 0;
        Sint32 total_width = 0;
        for (int team = 0; team < 4; ++team)
        {
            if (!ctf.team_active[team])
                continue;
            if (piece_count > 0)
            {
                pieces[static_cast<std::size_t>(piece_count)] = ":";
                piece_colors[static_cast<std::size_t>(piece_count)] = WHITE;
                total_width += 6;
                ++piece_count;
            }
            pieces[static_cast<std::size_t>(piece_count)] = std::format("{}", ctf.captures[team]);
            piece_colors[static_cast<std::size_t>(piece_count)] =
                static_cast<unsigned char>(team * 16 + 40);
            total_width +=
                static_cast<Sint32>(pieces[static_cast<std::size_t>(piece_count)].size()) * 6;
            ++piece_count;
        }
        Sint32 x = rm - 60 - total_width;
        for (int i = 0; i < piece_count; ++i)
        {
            mytext.write_xy(x, tm + 28, pieces[static_cast<std::size_t>(i)].c_str(),
                            piece_colors[static_cast<std::size_t>(i)],
                            static_cast<short>(1));
            x += static_cast<Sint32>(pieces[static_cast<std::size_t>(i)].size()) * 6;
        }
    }

    // Waypoint capture feedback: while any control point has a contending
    // team, a compact "WP n/36" meter in that team's ramp color shows the
    // accruing (or decaying) progress — partial progress was previously
    // invisible until the flip. Drawn on the tm+36 row: below the HP/MP rows
    // (tm+10/tm+18) and the FLAG!/compact-caps row (tm+28), above the
    // score block at the pane bottom, so it collides with nothing in any
    // split layout. First contested point in index order (deterministic).
    for (int i = 0; i < ctf.cp_count; ++i)
    {
        const og::sim::CtfControlPoint& cp = ctf.cps[i];
        if (cp.progress_team < 0)
            continue;
        const std::string meter =
            std::format("WP {}/{}", cp.progress, og::sim::kCtfCpCaptureTicks);
        mytext.write_xy(lm + 2, tm + 36, meter.c_str(),
                        static_cast<unsigned char>(cp.progress_team * 16 + 40),
                        static_cast<short>(1));
        break;
    }

    if (control != nullptr && !control->dead())
    {
        // The viewport's control carries an enemy flag.
        for (int team = 0; team < 4; ++team)
        {
            const og::sim::CtfFlag& flag = ctf.flags[team];
            if (flag.state == og::sim::CtfFlagState::Carried &&
                flag.carrier_entity_id == control->entity_id())
            {
                mytext.write_xy(lm + 2, tm + 28, "FLAG!",
                                static_cast<unsigned char>(team * 16 + 40),
                                static_cast<short>(1));
                break;
            }
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
    static char namelist[NUM_FAMILIES][20] =
        { "SOLDIER", "ELF", "ARCHER", "MAGE",
          "SKELETON", "CLERIC", "ELEMENTAL",
          "FAERIE", "SLIME", "SLIME", "SLIME",
          "THIEF", "GHOST", "DRUID", "ORC",
          "ORC CAPTAIN", "BARBARIAN", "ARCHMAGE",
          "GOLEM", "GIANT SKEL", "TOWER",
        };

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
		ScopedGameplayUiViewLayout gameplay_ui_layout(
			*s->viewob[players], *s);
        control = s->viewob[players]->control;
        lm = s->viewob[players]->xloc + OVERSCAN_PADDING;
        tm = s->viewob[players]->yloc + OVERSCAN_PADDING;
        rm = s->viewob[players]->endx - OVERSCAN_PADDING;
        bm = s->viewob[players]->endy - OVERSCAN_PADDING;

        if ((s->world_.type & GameWorld::TYPE_CTF) && s->world_.ctf.active)
            draw_ctf_panel(s, control, lm, tm, rm);
        if (((s->world_.type & GameWorld::TYPE_CTF) &&
             s->world_.ctf.active) ||
            og::sim::classic_respawn_active(s->world_))
        {
            draw_respawn_countdown(s, control, lm, tm);
        }

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
                follow_name = namelist[follow_fam];
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
            // Get the button-drawing info ..
            draw_button = s->viewob[players]->prefs[PREF_OVERLAY];
            if (draw_button)
                text_color = DARK_BLUE;
            else
                text_color = YELLOW;

            // Get current number of foes
            tempfoes = remaining_foes(s, control);
            // Get current number of team-members
            tempallies = remaining_team(s, control->team_num());

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
            if (control->myguy)
                tempname = control->myguy->name;
            else if (!control->stats()->name.empty())
                tempname = control->stats()->name;
            else
                tempname = namelist[fam];

            message = tempname;

            if (draw_button)
                s->draw_button(lm+1, tm+2, lm+63, tm+9, 1, 1);

            mytext.write_xy(lm+3, tm+4, message.c_str(), text_color, 1);

            // HP/MP bars; dependent on user settings
            switch (s->viewob[players]->prefs[PREF_LIFE])
            {
                case PREF_LIFE_TEXT: // display numeric values only
                    if (draw_button)
                        s->draw_button(lm+1, tm+10, lm+63, tm+26, 1, 1);
                    message = std::format("HP: {:.0f}", ceilf(control->stats()->hitpoints()));
                    mytext.write_xy(lm+5, tm+12, message.c_str(), text_color, static_cast<short>(1));
                    message = std::format("MP: {:.0f}", ceilf(control->stats()->magicpoints()));
                    mytext.write_xy(lm+5, tm+20, message.c_str(), text_color, static_cast<short>(1));
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

            if (s->viewob[players]->prefs[PREF_SCORE] == PREF_SCORE_ON)
            {
                // Score, bottom left corner
                int special_offset = -24;
#ifdef USE_TOUCH_INPUT
                // Upper left instead
                int bm = tm + 54;
                special_offset = 0;
#endif

                // Draw box, if needed
                if (draw_button)
                    s->draw_button(lm+1, bm-26, lm+98, bm-2, 1, 1);

                const bool can_show_team_score = is_valid_score_team(control->team_num());
                if (!can_show_team_score)
                {
                    message = "SC: 0";
                    mytext.write_xy(lm+2, bm-8, message.c_str(), text_color, static_cast<short>(1));
                    continue;
                }

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
                int special_y = bm + special_offset;
                // Don't show score and XP (clutter) when in a small viewport
                if (s->numviews > 2 && !(s->numviews == 3 && players == 0))
                {
                    special_y = bm - 8;
                }
                else
                {
                    message = std::format("SC: {}", scorecountup[team_num]);
                    mytext.write_xy(lm+2, bm-8, message.c_str(), text_color, static_cast<short>(1));

                    // Level or exp, 2nd bottom left
                    if (control->myguy)
                        message = std::format("XP: {}", control->myguy->exp);
                    else
                        message = std::format("LEVEL: {}", control->stats()->level());
                    mytext.write_xy(lm+2, bm-16, message.c_str(), text_color, static_cast<short>(1));
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
                    mytext.write_xy(lm+2, special_y, message.c_str(), text_color, static_cast<short>(1));
                else
                    mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(RED), static_cast<short>(1));

#ifdef USE_TOUCH_INPUT
                // Alternate special name (if not "NONE")
                if (s->alternate_name[fam][spc] != "NONE")
                {
                    message = std::format("ALT: {}", s->alternate_name[fam][spc]);
                    if (control->specials_disabled())
                        mytext.write_xy(lm+2, bm + special_offset + 8, message.c_str(), static_cast<unsigned char>(GREY), static_cast<short>(1));
                    else if (control->stats()->magicpoints() >= control->stats()->special_cost(spc))
                        mytext.write_xy(lm+2, bm + special_offset + 8, message.c_str(), text_color, static_cast<short>(1));
                    else
                        mytext.write_xy(lm+2, bm + special_offset + 8, message.c_str(), static_cast<unsigned char>(RED), static_cast<short>(1));
                }
#endif
            }

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
                mytext.write_xy(rm - 55, tm+2, message.c_str(), text_color, static_cast<short>(1));
#else
                mytext.write_xy(rm - 55, tm+2 + 44 + 8, message.c_str(), text_color, static_cast<short>(1));
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
                mytext.write_xy(foes_x, tm+10, message.c_str(), text_color, static_cast<short>(1));
#else
                mytext.write_xy(foes_x, tm+10 + 44 + 8, message.c_str(), text_color, static_cast<short>(1));
#endif

                if (show_wave)
                {
                    const std::uint32_t wave_seconds = (next_wake_ticks + 11u) / 12u;
                    message = std::format("NEXT WAVE: {}s", wave_seconds);
                    const Sint32 wave_x = std::max<Sint32>(
                        lm, rm - 2 - 6 * static_cast<Sint32>(message.size()));
#ifndef USE_TOUCH_INPUT
                    mytext.write_xy(wave_x, tm+18, message.c_str(), text_color, static_cast<short>(1));
#else
                    mytext.write_xy(wave_x, tm+18 + 44 + 8, message.c_str(), text_color, static_cast<short>(1));
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
                    mytext.write_xy(floor_x, floor_y, floor_label.c_str(), text_color, static_cast<short>(1));
#else
                    mytext.write_xy(floor_x, floor_y + 44 + 8, floor_label.c_str(), text_color, static_cast<short>(1));
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
