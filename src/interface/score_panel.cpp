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
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
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
        points = control->stats()->hitpoints;

        if (float_eq(points, control->stats()->max_hitpoints))
            whatcolor = MAX_HP_COLOR;
        else if ((points * 3) < control->stats()->max_hitpoints)
            whatcolor = LOW_HP_COLOR;
        else if ((points * 3 / 2) < control->stats()->max_hitpoints)
            whatcolor = MID_HP_COLOR;
        else if (points < control->stats()->max_hitpoints)
            whatcolor = HIGH_HP_COLOR;
        else
            whatcolor = ORANGE_START;

        if (points > control->stats()->max_hitpoints)
            bar_length = 60;
        else
            bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_hitpoints));

        draw_percentage_bar(left, top, BAR_BACK_COLOR, 60, s);
        draw_percentage_bar(left, top, whatcolor, static_cast<short>(bar_length), s);
    }  // end of doing hp stuff..
    else if (mode == 1) // sp stuff ..
    {
        points = control->stats()->magicpoints;

        if (float_eq(points, control->stats()->max_magicpoints))
            whatcolor = MAX_MP_COLOR;
        else if ((points * 3) < control->stats()->max_magicpoints)
            whatcolor = LOW_MP_COLOR;
        else if ((points * 3 / 2) < control->stats()->max_magicpoints)
            whatcolor = MID_MP_COLOR;
        else if (points < control->stats()->max_magicpoints)
            whatcolor = HIGH_MP_COLOR;
        else
            whatcolor = WATER_START;

        if (points > control->stats()->max_magicpoints)
            bar_length = 60;
        else
            bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_magicpoints));

        draw_percentage_bar(left, top, BAR_BACK_COLOR, 60, s);
        draw_percentage_bar(left, top, whatcolor, static_cast<short>(bar_length), s);
    } // end of sp stuff
} // end of drawing routine ..

#ifdef REDUCE_OVERSCAN
#define OVERSCAN_PADDING 6
#else
#define OVERSCAN_PADDING 0
#endif

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
        if (control && !control->dead && control->user == players)
        {
            draw_button = s->viewob[players]->prefs[PREF_OVERLAY];
            if (draw_button)
                text_color = DARK_BLUE;
            else
                text_color = YELLOW;

            tempfoes = remaining_foes(s, control);
            tempallies = remaining_team(s, control->team_num);

            if (control->myguy)
                tempname = control->myguy->name;
            else if (!control->stats()->name.empty())
                tempname = control->stats()->name;
            else
                tempname = namelist[static_cast<int>(control->family)];

            message = tempname;

            if (draw_button)
                s->draw_button(lm+1, tm+2, lm+63, tm+9, 1, 1);

            mytext.write_xy(lm+3, tm+4, message.c_str(), text_color, 1);

            switch (s->viewob[players]->prefs[PREF_LIFE])
            {
                case PREF_LIFE_TEXT: // display numeric values only
                    if (draw_button)
                        s->draw_button(lm+1, tm+10, lm+63, tm+26, 1, 1);
                    message = std::format("HP: {:.0f}", ceilf(control->stats()->hitpoints));
                    mytext.write_xy(lm+5, tm+12, message.c_str(), text_color, static_cast<short>(1));
                    message = std::format("MP: {:.0f}", ceilf(control->stats()->magicpoints));
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
                    message = std::format("HP: {:.0f}", ceilf(control->stats()->hitpoints));
                    mytext.write_xy(lm+5, tm+11, message.c_str(), static_cast<unsigned char>(BLACK), static_cast<short>(1));

                    new_draw_value_bar(lm+2, tm+18, control, 1, s);
                    message = std::format("MP: {:.0f}", ceilf(control->stats()->magicpoints));
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

                const bool can_show_team_score = is_valid_score_team(control->team_num);
                if (!can_show_team_score)
                {
                    message = "SC: 0";
                    mytext.write_xy(lm+2, bm-8, message.c_str(), text_color, static_cast<short>(1));
                    continue;
                }

                const unsigned char team_num = control->team_num;
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
                        message = std::format("LEVEL: {}", control->stats()->level);
                    mytext.write_xy(lm+2, bm-16, message.c_str(), text_color, static_cast<short>(1));
                }

                if (control->shifter_down &&
                    s->alternate_name[static_cast<int>(control->family)][static_cast<int>(control->current_special)] != "NONE")
                    message = std::format("SPC: {}", s->alternate_name[static_cast<int>(control->family)][static_cast<int>(control->current_special)]);
                else
                    message = std::format("SPC: {}", s->special_name[static_cast<int>(control->family)][static_cast<int>(control->current_special)]);

                if (control->stats()->magicpoints >= control->stats()->special_cost[static_cast<int>(control->current_special)])
                    mytext.write_xy(lm+2, special_y, message.c_str(), text_color, static_cast<short>(1));
                else
                    mytext.write_xy(lm+2, special_y, message.c_str(), static_cast<unsigned char>(RED), static_cast<short>(1));

#ifdef USE_TOUCH_INPUT
                if (s->alternate_name[static_cast<int>(control->family)][static_cast<int>(control->current_special)] != "NONE")
                {
                    message = std::format("ALT: {}", s->alternate_name[static_cast<int>(control->family)][static_cast<int>(control->current_special)]);
                    if (control->stats()->magicpoints >= control->stats()->special_cost[static_cast<int>(control->current_special)])
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
