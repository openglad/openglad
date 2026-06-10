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
#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>

#include <openglad/interface/game_context.h>

#include <cmath>
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

short score_panel(screen* s, short do_it);

void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor,
                         short somelength, screen* s);

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
#define OVERSCAN_PADDING 6
#else
#define OVERSCAN_PADDING 0
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
        std::string segments[4];
        Sint32 total_width = 0;
        for (int team = 0; team < 4; ++team)
        {
            if (!ctf.team_active[team])
                continue;
            segments[team] = std::format(
                "{}{}",
                ctf.captures[team],
                ctf_flag_state_glyph(ctf.flags[team]));
            if (total_width > 0)
                total_width += 6;
            total_width += static_cast<Sint32>(segments[team].size()) * 6;
        }
        Sint32 x = rm - 60 - total_width;
        for (int team = 0; team < 4; ++team)
        {
            if (segments[team].empty())
                continue;
            mytext.write_xy(x, tm + 4, segments[team].c_str(),
                            static_cast<unsigned char>(team * 16 + 40),
                            static_cast<short>(1));
            x += static_cast<Sint32>(segments[team].size()) * 6 + 6;
        }
    }
    else if (s->numviews == 2)
    {
        // Half-width panes cannot fit the glyph segments beside the name:
        // compact digits-only group ("2:1:0:3", counts in team ramp colors,
        // neutral separators) on the tm+28 row, clear of the name and the
        // HP/MP rows. The carrier's FLAG! stays left at lm+2 on that row.
        std::string pieces[7];
        unsigned char piece_colors[7];
        int piece_count = 0;
        Sint32 total_width = 0;
        for (int team = 0; team < 4; ++team)
        {
            if (!ctf.team_active[team])
                continue;
            if (piece_count > 0)
            {
                pieces[piece_count] = ":";
                piece_colors[piece_count] = WHITE;
                total_width += 6;
                ++piece_count;
            }
            pieces[piece_count] = std::format("{}", ctf.captures[team]);
            piece_colors[piece_count] =
                static_cast<unsigned char>(team * 16 + 40);
            total_width +=
                static_cast<Sint32>(pieces[piece_count].size()) * 6;
            ++piece_count;
        }
        Sint32 x = rm - 60 - total_width;
        for (int i = 0; i < piece_count; ++i)
        {
            mytext.write_xy(x, tm + 28, pieces[i].c_str(), piece_colors[i],
                            static_cast<short>(1));
            x += static_cast<Sint32>(pieces[i].size()) * 6;
        }
    }

    if (control == nullptr)
        return;

    if (!control->dead())
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
        return;
    }

    // Dead control with a pending revive entry: countdown in whole seconds.
    for (const og::sim::CtfRespawnEntry& entry : ctf.respawn_queue)
    {
        if (entry.kind != 0 || entry.walker_entity_id != control->entity_id())
            continue;
        const int seconds = og::sim::ctf_respawn_seconds_left(ctf, entry);
        const std::string message = std::format("RESPAWN IN {}", seconds);
        mytext.write_xy(lm + 4, tm + 12, message.c_str(),
                        static_cast<unsigned char>(YELLOW),
                        static_cast<short>(1));
        break;
    }
}

short new_score_panel(screen* s, short /*do_it*/)
{
#define L_D(x) x*8
    std::string message;
    std::string tempname;
    short tempfoes = 0;
    short players;
    short tempallies = 0;
    text& mytext = s->text_normal;

    walker* control;
    Sint32 lm, tm; // left and top margins
    Sint32 rm, bm; // right and bottom margins
    (void)bm;
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
    static Uint32 scorecountup[SCORE_TEAM_COUNT] = {
        s->world_.m_score[0],
        s->world_.m_score[1],
        s->world_.m_score[2],
        s->world_.m_score[3],
    };

    for (players = 0; players < s->numviews; players++)
    {
        control = s->viewob[players]->control;
        lm = s->viewob[players]->xloc + OVERSCAN_PADDING;
        tm = s->viewob[players]->yloc + OVERSCAN_PADDING;
        rm = s->viewob[players]->endx - OVERSCAN_PADDING;
        bm = s->viewob[players]->endy - OVERSCAN_PADDING;

        if ((s->world_.type & GameWorld::TYPE_CTF) && s->world_.ctf.active)
            draw_ctf_panel(s, control, lm, tm, rm);
        // Draw the HUD whenever this viewport's control walker is a live,
        // human-claimed walker. We must NOT compare control->user() against the
        // local viewport index: that only holds for local split-screen (where
        // game.cpp claims view->control with set_user(view_idx)). On a network
        // client the single local viewport (players == 0) shows a walker whose
        // user() is the *server-global* player slot assigned by bind_player, so
        // a == players check would hide the HUD for every non-host client.
        if (control && !control->dead() && control->user() != -1)
        {
            draw_button = s->viewob[players]->prefs[PREF_OVERLAY];
            if (draw_button)
                text_color = DARK_BLUE;
            else
                text_color = YELLOW;

            tempfoes = remaining_foes(s, control);
            tempallies = remaining_team(s, control->team_num());

            if (control->myguy)
                tempname = control->myguy->name;
            else if (!control->stats()->name.empty())
                tempname = control->stats()->name;
            else
                tempname = namelist[static_cast<int>(control->family())];

            message = tempname;

            if (draw_button)
                s->draw_button(lm+1, tm+2, lm+63, tm+9, 1, 1);

            mytext.write_xy(lm+3, tm+4, message.c_str(), text_color, 1);

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

            if (s->viewob[players]->prefs[PREF_SCORE] == PREF_SCORE_ON)
            {
                int special_offset = -24;
#ifdef USE_TOUCH_INPUT
                int bm = tm + 54;
                special_offset = 0;
#endif

                if (draw_button)
                    s->draw_button(lm+1, bm-26, lm+98, bm-2, 1, 1);

                const bool can_show_team_score = is_valid_score_team(control->team_num());
                if (!can_show_team_score)
                {
                    message = "SC: 0";
                    mytext.write_xy(lm+2, bm-8, message.c_str(), text_color, static_cast<short>(1));
                    continue;
                }

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

                int special_y = bm + special_offset;
                if (s->numviews > 2 && !(s->numviews == 3 && players == 0))
                {
                    special_y = bm - 8;
                }
                else
                {
                    message = std::format("SC: {}", scorecountup[team_num]);
                    mytext.write_xy(lm+2, bm-8, message.c_str(), text_color, static_cast<short>(1));

                    if (control->myguy)
                        message = std::format("XP: {}", control->myguy->exp);
                    else
                        message = std::format("LEVEL: {}", control->stats()->level());
                    mytext.write_xy(lm+2, bm-16, message.c_str(), text_color, static_cast<short>(1));
                }

                if (control->shifter_down() &&
                    s->alternate_name[static_cast<int>(control->family())][static_cast<int>(control->current_special())] != "NONE")
                    message = std::format("SPC: {}", s->alternate_name[static_cast<int>(control->family())][static_cast<int>(control->current_special())]);
                else
                    message = std::format("SPC: {}", s->special_name[static_cast<int>(control->family())][static_cast<int>(control->current_special())]);

                if (control->stats()->magicpoints() >= control->stats()->special_cost(static_cast<int>(control->current_special())))
                    mytext.write_xy(lm+2, special_y, message.c_str(), text_color, static_cast<short>(1));
                else
                    mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(RED), static_cast<short>(1));

#ifdef USE_TOUCH_INPUT
                if (s->alternate_name[static_cast<int>(control->family())][static_cast<int>(control->current_special())] != "NONE")
                {
                    message = std::format("ALT: {}", s->alternate_name[static_cast<int>(control->family())][static_cast<int>(control->current_special())]);
                    if (control->stats()->magicpoints() >= control->stats()->special_cost(static_cast<int>(control->current_special())))
                        mytext.write_xy(lm+2, bm + special_offset + 8, message.c_str(), text_color, static_cast<short>(1));
                    else
                        mytext.write_xy(lm+2, bm + special_offset + 8, message.c_str(), static_cast<unsigned char>(RED), static_cast<short>(1));
                }
#endif
            }

            if (s->viewob[players]->prefs[PREF_FOES] == PREF_FOES_ON)
            {
                if (draw_button)
                    s->draw_button(rm-57, tm+1, rm-2, tm+16, 1, 1);

                message = std::format("TEAM: {}", tempallies);
#ifndef USE_TOUCH_INPUT
                mytext.write_xy(rm - 55, tm+2, message.c_str(), text_color, static_cast<short>(1));
#else
                mytext.write_xy(rm - 55, tm+2 + 44 + 8, message.c_str(), text_color, static_cast<short>(1));
#endif

                message = std::format("FOES: {}", tempfoes);
#ifndef USE_TOUCH_INPUT
                mytext.write_xy(rm-55, tm+10, message.c_str(), text_color, static_cast<short>(1));
#else
                mytext.write_xy(rm-55, tm+10 + 44 + 8, message.c_str(), text_color, static_cast<short>(1));
#endif
            }
        }
    }

    if (og::runtime::current_session != nullptr &&
        og::runtime::current_session->show_fps_)
    {
        draw_fps_overlay(*s);
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

    s->fastbox(left+2, top, somelength-4, 1, 0, 1);
    s->fastbox(left+1, top+1, 1,  1, 0, 1);
    s->fastbox(left+58, top+1, 1, 1, 0, 1);
    s->fastbox(left,  top+2, 1, 3,   0, 1);
    s->fastbox(left+59, top+2, 1, 3, 0, 1);
    s->fastbox(left+1, top+5, 1,  1, 0, 1);
    s->fastbox(left+58, top+5, 1, 1, 0, 1);
    s->fastbox(left+2, top+6, somelength-4, 1, 0, 1);

    s->fastbox(left+2, top+1, somelength-4, 1, somecolor, 1);
    s->fastbox(left+1, top+2, somelength-2, 3, somecolor, 1);
    s->fastbox(left+2, top+5, somelength-4, 1, somecolor, 1);

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
