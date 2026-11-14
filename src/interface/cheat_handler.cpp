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
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/base.h>

walker* cheat_cycle_next_team(GameWorld& world, short& team_in_out)
{
	// Normalize into 0..MAX_TEAM-1 before the lap. A signed remainder keeps
	// the sign of its left operand, so a negative my_team (a corrupted save,
	// a hand-edited slot) would otherwise start the walk on a negative
	// residue and spend its bounded lap scanning teams no walker can hold.
	short team = static_cast<short>(
	    ((team_in_out % MAX_TEAM) + MAX_TEAM) % MAX_TEAM);

	for (short step = 0; step < MAX_TEAM; step++)
	{
		team = static_cast<short>((team + 1) % MAX_TEAM);

		for (auto& uptr : world.oblist)
		{
			walker* w = uptr.get();
			// Corpses and dormant (delayed-spawn) bodies are not takeover
			// candidates: the same rule the normal switch-character scan
			// applies (sim_input_handler.cpp).
			if (w && !w->dead() && !w->dormant() && !w->hidden() && w->possess_link() == 0 &&
					(w->team_num() == team) &&
					(w->query_order() == Order::Living))
			{
				team_in_out = team;
				return w;
			}
		}
	}

	return nullptr;
}

namespace {
// The one rule every cheat key follows (#413, #432). In local play the screen
// the keys arrive on holds a copy of the game that the next snapshot
// overwrites, so a cheat written there was gone a tick later; the cheat runs
// on the server's own world instead, with the server's game context active,
// and the snapshot shows it on every view. With no shadow installed (a direct
// test) the display world is the game. In a networked session, a replay
// playback, a spectator view or a finished level nothing runs: a host's cheat
// would change every player's game, and a joiner has no server world.
// Returns whether the cheat ran. The server's walkers never reach `control`.
bool run_cheat(walker*& control, short mynum, screen* game_screen,
               const og::runtime::CheatBody& cheat)
{
	og::runtime::SessionState& session = *og::runtime::current_session;
	if (og::runtime::local_transport_active(session))
		return og::runtime::run_cheat_on_game(session, mynum, cheat);
	control = cheat(*game_screen, control);
	return true;
}

// A cheat on the seat's hero alone: a seat that plays none changes nothing.
template <typename Fn>
void run_hero_cheat(walker*& control, short mynum, screen* game_screen, Fn&& fn)
{
	run_cheat(control, mynum, game_screen,
		[&fn](screen& game, walker* hero) -> walker* {
			if (hero != nullptr)
				fn(game, *hero);
			return hero;
		});
}
} // namespace

void handle_cheat_keys(walker*& control, short mynum,
                       const void* native_event, const PlayerInput& pi,
                       screen* game_screen)
{
	if (!pi.is_held(InputAction::Cheat) || !CHEAT_MODE)
		return;

	// Change team (Cheat+Switch)
	if (og::runtime::current_session->changedteam_[static_cast<std::size_t>(mynum)] && !pi.was_pressed(InputAction::SwitchChar))
		og::runtime::current_session->changedteam_[static_cast<std::size_t>(mynum)] = 0;
	if (pi.was_pressed(InputAction::SwitchChar) && !og::runtime::current_session->changedteam_[static_cast<std::size_t>(mynum)])
	{
		// to debounce keys
		og::runtime::current_session->changedteam_[static_cast<std::size_t>(mynum)] = 1;

		bool no_other_team = false;
		const bool ran = run_cheat(control, mynum, game_screen,
			[mynum, &no_other_team](screen& game, walker* hero) -> walker* {
				if (hero == nullptr)
					return hero;
				walker* const borrowed = hero;
				hero->set_user(-1);
				// hope this works
				hero->set_act_type(ACT_RANDOM);

				short newteam = game.save_data.my_team;
				walker* result = cheat_cycle_next_team(game.world(), newteam);

				if (result != nullptr)
				{
					// Only a hop that landed commits the team: a failed lap used to
					// leave save_data (and the world) parked on whatever team the
					// walk stopped at, silently changing whose side the seat is on.
					game.save_data.my_team = newteam;
					game.world().my_team = newteam;
					hero = result;
				}
				else
				{
					no_other_team = true;
				}

				hero->set_user(static_cast<signed char>(mynum));
				hero->set_act_type(ACT_CONTROL);
				// A seat that takes a body over drops the orders its AI queued,
				// as the old game does after a hop (and a switch does).
				if (hero != borrowed)
					hero->stats()->clear_command_for_control_switch();
				return hero;
			});
		if (ran && no_other_team)
			game_screen->do_notify("NO OTHER TEAM", control);
	}

	// Testing effect object ..
	// kill living bad guys
	if (query_key_event(KEYCODE_F12, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen& game, walker& hero) {
			for (auto& uptr : game.world().oblist)
			{
				walker* w = uptr.get();
				if (w && w->query_order() == Order::Living && !hero.is_friendly(w))
				{
					w->stats()->set_hitpoints(-1);
					hero.attack(w);
					w->death();
				}
			}
		});
	}

	if (query_key_event(KEYCODE_RIGHTBRACKET, native_event))
		run_hero_cheat(control, mynum, game_screen, [](screen&, walker& hero) {
			hero.stats()->set_level(hero.stats()->level() + 1);
		});

	if (query_key_event(KEYCODE_LEFTBRACKET, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen&, walker& hero) {
			if (hero.stats()->level() > 1)
				hero.stats()->set_level(hero.stats()->level() - 1);
		});
	}

	if (query_key_event(KEYCODE_F1, native_event)) // freeze time
	{
		if (run_cheat(control, mynum, game_screen, [](screen& game, walker* hero) -> walker* {
				game.world().enemy_freeze += 50;
				game.world().current_palette_id = 1;
				return hero;
			}))
			game_screen->show_palette_id(1); // blue now; the next snapshot says the same
		//clear_key_code(SDLK_F1);
	}//end freeze time

	// generate magic shield
	if (query_key_event(KEYCODE_F2, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen& game, walker& hero) {
			walker* const newob = game.world().add_ob(Order::FX, FAMILY_MAGIC_SHIELD);
			if (!newob) return;
			newob->set_owner(&hero);
			newob->set_team_num(hero.team_num());
			newob->set_ani_type(1);
			newob->set_lifetime(200);
		});
	}

	// ability to fly
	if (query_key_event(KEYCODE_f, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen&, walker& hero) {
			if (hero.stats()->query_bit_flags(BIT_FLYING))
				hero.stats()->set_bit_flags(BIT_FLYING, 0);
			else
				hero.stats()->set_bit_flags(BIT_FLYING, 1);
		});
	}

	// give controller lots of hitpoints
	if (query_key_event(KEYCODE_h, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen& game, walker& hero) {
			hero.stats()->set_hitpoints(hero.stats()->hitpoints() + 100);
			game.world().control_hp += 100;
		});
	}

	if (query_key_event(KEYCODE_i, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen&, walker& hero) {
			if (hero.stats()->query_bit_flags(BIT_INVINCIBLE))
				hero.stats()->set_bit_flags(BIT_INVINCIBLE, 0);
			else
				hero.stats()->set_bit_flags(BIT_INVINCIBLE, 1);
		});
	}

	// give controller lots of magicpoints
	if (query_key_event(KEYCODE_m, native_event))
		run_hero_cheat(control, mynum, game_screen, [](screen&, walker& hero) {
			hero.stats()->set_magicpoints(hero.stats()->magicpoints() + 150);
		});

	// give us faster speed ..
	if (query_key_event(KEYCODE_s, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen&, walker& hero) {
			hero.set_speed_bonus_left(hero.speed_bonus_left() + 20);
			hero.set_speed_bonus(hero.normal_stepsize());
		});
	}

	// transform to new shape
	if (query_key_event(KEYCODE_t, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen&, walker& hero) {
			Sint32 family = (static_cast<Sint32>(static_cast<unsigned char>(hero.family())) + 1) % NUM_FAMILIES;
			hero.transform_to(hero.query_order(), family);
		});
	}

	if (query_key_event(KEYCODE_v, native_event))
	{
		run_hero_cheat(control, mynum, game_screen, [](screen&, walker& hero) {
			if (hero.invisibility_left() < 3000)
				hero.set_invisibility_left(hero.invisibility_left() + 100);
		});
	}
}
