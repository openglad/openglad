/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Cheat/debug key handlers extracted from render/view.cpp so that the render
// layer does not directly mutate sim/entity state.

#include <openglad/runtime/cheat_handler.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_session.h>
#include <openglad/input/input.h>
#include <openglad/input/input_state.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/data/level_data.h>
#include <openglad/render/pal32.h>
#include <openglad/legacy/base.h>

void handle_cheat_keys(walker*& control, short mynum,
                       const SDL_Event& event, const PlayerInput& pi,
                       screen* game_screen)
{
	if (!pi.is_held(InputAction::Cheat) || !CHEAT_MODE)
		return;

	walker* newob;

	// Change team (Cheat+Switch)
	if (og::runtime::current_session->changedteam_[mynum] && !pi.was_pressed(InputAction::SwitchChar))
		og::runtime::current_session->changedteam_[mynum] = 0;
	if (pi.was_pressed(InputAction::SwitchChar) && !og::runtime::current_session->changedteam_[mynum])
	{
		og::runtime::current_session->changedteam_[mynum] = 1;

		walker* result = nullptr;
		control->user = -1;
		control->set_act_type(ACT_RANDOM);

		short oldteam = game_screen->save_data.my_team;

		do
		{
			game_screen->save_data.my_team++;
			game_screen->save_data.my_team %= MAX_TEAM;

			for (auto& uptr : game_screen->world().oblist)
			{
				walker* w = uptr.get();
				if ((w->team_num == game_screen->save_data.my_team) &&
						(w->query_order() == Order::Living))
				{
					result = w;
					break;
				}
			}
		}
		while (result == nullptr && game_screen->save_data.my_team != oldteam);

		if (result != nullptr)
			control = result;

		control->user = static_cast<signed char>(mynum);
		control->set_act_type(ACT_CONTROL);
	}

	if (query_key_event(SDLK_F12, event))
	{
		for (auto& uptr : game_screen->world().oblist)
		{
			walker* w = uptr.get();
			if (w && w->query_order() == Order::Living && !control->is_friendly(w))
			{
				w->stats()->hitpoints = -1;
				control->attack(w);
				w->death();
			}
		}
	}

	if (query_key_event(SDLK_RIGHTBRACKET, event))
		control->stats()->level++;

	if (query_key_event(SDLK_LEFTBRACKET, event))
	{
		if (control->stats()->level > 1)
			control->stats()->level--;
	}

	if (query_key_event(SDLK_F1, event))
	{
		game_screen->world_.enemy_freeze += 50;
		set_palette(game_screen->bluepalette);
	}

	if (query_key_event(SDLK_F2, event))
	{
		newob = game_screen->world().add_ob(Order::FX, FAMILY_MAGIC_SHIELD);
		newob->owner = control;
		newob->team_num = control->team_num;
		newob->ani_type = 1;
		newob->lifetime = 200;
	}

	if (query_key_event(SDLK_f, event))
	{
		if (control->stats()->query_bit_flags(BIT_FLYING))
			control->stats()->set_bit_flags(BIT_FLYING, 0);
		else
			control->stats()->set_bit_flags(BIT_FLYING, 1);
	}

	if (query_key_event(SDLK_h, event))
	{
		control->stats()->hitpoints += 100;
		game_screen->world_.control_hp += 100;
	}

	if (query_key_event(SDLK_i, event))
	{
		if (control->stats()->query_bit_flags(BIT_INVINCIBLE))
			control->stats()->set_bit_flags(BIT_INVINCIBLE, 0);
		else
			control->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
	}

	if (query_key_event(SDLK_m, event))
		control->stats()->magicpoints += 150;

	if (query_key_event(SDLK_s, event))
	{
		control->speed_bonus_left = control->speed_bonus_left + 20;
		control->speed_bonus = control->normal_stepsize;
	}

	if (query_key_event(SDLK_t, event))
	{
		Sint32 family = (static_cast<Sint32>(static_cast<unsigned char>(control->family)) + 1) % NUM_FAMILIES;
		control->transform_to(control->query_order(), family);
	}

	if (query_key_event(SDLK_v, event))
	{
		if (control->invisibility_left < 3000)
			control->invisibility_left = control->invisibility_left + 100;
	}
}
