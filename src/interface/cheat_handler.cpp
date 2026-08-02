/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Cheat/debug key handlers extracted from render/view.cpp so that the render
// layer does not directly mutate sim/entity state.

#include <openglad/interface/cheat_handler.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/base.h>

void handle_cheat_keys(walker*& control, short mynum,
                       const void* native_event, const PlayerInput& pi,
                       screen* game_screen)
{
	if (native_event == nullptr)
		return;

	if (!pi.is_held(InputAction::Cheat) || !CHEAT_MODE)
		return;

	walker* newob;

	// Change team (Cheat+Switch)
	if (og::runtime::current_session->changedteam_[mynum] && !pi.was_pressed(InputAction::SwitchChar))
		og::runtime::current_session->changedteam_[mynum] = 0;
	if (pi.was_pressed(InputAction::SwitchChar) && !og::runtime::current_session->changedteam_[mynum])
	{
		// to debounce keys
		og::runtime::current_session->changedteam_[mynum] = 1;

		walker* result = nullptr;
		control->set_user(-1);
		// hope this works
		control->set_act_type(ACT_RANDOM);

		short oldteam = game_screen->save_data.my_team;

		do
		{
			game_screen->save_data.my_team++;
			game_screen->save_data.my_team %= MAX_TEAM;

			for (auto& uptr : game_screen->world().oblist)
			{
				walker* w = uptr.get();
				if ((w->team_num() == game_screen->save_data.my_team) &&
						(w->query_order() == Order::Living))
				{
					result = w;
					break;
				}
			}
		}
		while (result == nullptr && game_screen->save_data.my_team != oldteam);

		game_screen->world_.my_team = game_screen->save_data.my_team;

		if (result != nullptr)
			control = result;

		control->set_user(static_cast<signed char>(mynum));
		control->set_act_type(ACT_CONTROL);
	}

	// Testing effect object ..
	// kill living bad guys
	if (query_key_event(KEYCODE_F12, native_event))
	{
		for (auto& uptr : game_screen->world().oblist)
		{
			walker* w = uptr.get();
			if (w && w->query_order() == Order::Living && !control->is_friendly(w))
			{
				w->stats()->set_hitpoints(-1);
				control->attack(w);
				w->death();
			}
		}
	}

	if (query_key_event(KEYCODE_RIGHTBRACKET, native_event))
		control->stats()->set_level(control->stats()->level() + 1);

	if (query_key_event(KEYCODE_LEFTBRACKET, native_event))
	{
		if (control->stats()->level() > 1)
			control->stats()->set_level(control->stats()->level() - 1);
	}

	if (query_key_event(KEYCODE_F1, native_event))
	{
		game_screen->world().enemy_freeze += 50;
		set_palette(game_screen->bluepalette);
	}

	// generate magic shield
	if (query_key_event(KEYCODE_F2, native_event))
	{
		newob = game_screen->world().add_ob(Order::FX, FAMILY_MAGIC_SHIELD);
		if (!newob) return;
		newob->set_owner(control);
		newob->set_team_num(control->team_num());
		newob->set_ani_type(1);
		newob->set_lifetime(200);
	}

	// ability to fly
	if (query_key_event(KEYCODE_f, native_event))
	{
		if (control->stats()->query_bit_flags(BIT_FLYING))
			control->stats()->set_bit_flags(BIT_FLYING, 0);
		else
			control->stats()->set_bit_flags(BIT_FLYING, 1);
	}

	// give controller lots of hitpoints
	if (query_key_event(KEYCODE_h, native_event))
	{
		control->stats()->set_hitpoints(control->stats()->hitpoints() + 100);
		game_screen->world().control_hp += 100;
	}

	if (query_key_event(KEYCODE_i, native_event))
	{
		if (control->stats()->query_bit_flags(BIT_INVINCIBLE))
			control->stats()->set_bit_flags(BIT_INVINCIBLE, 0);
		else
			control->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
	}

	// give controller lots of magicpoints
	if (query_key_event(KEYCODE_m, native_event))
		control->stats()->set_magicpoints(control->stats()->magicpoints() + 150);

	// give us faster speed ..
	if (query_key_event(KEYCODE_s, native_event))
	{
		control->set_speed_bonus_left(control->speed_bonus_left() + 20);
		control->set_speed_bonus(control->normal_stepsize());
	}

	// transform to new shape
	if (query_key_event(KEYCODE_t, native_event))
	{
		Sint32 family = (static_cast<Sint32>(static_cast<unsigned char>(control->family())) + 1) % NUM_FAMILIES;
		control->transform_to(control->query_order(), family);
	}

	if (query_key_event(KEYCODE_v, native_event))
	{
		if (control->invisibility_left() < 3000)
			control->set_invisibility_left(control->invisibility_left() + 100);
	}
}
