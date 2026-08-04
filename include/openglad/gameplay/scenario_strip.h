/* Scenario-troops strip for CLASSIC (non-scripted) maps.
 *
 * The scripted modes run their own strip from Lua at on_mode_init
 * (lib/mode_strip.lua). Classic maps have no mode script, so the engine
 * performs the "TROOPS: NONE" sweep itself.
 */
#pragma once

class GameWorld;

namespace og::sim {

// Remove every authored Order::Living / Order::Generator that has no roster
// guy attached, on ANY team (wildlife included).
//
// Removal, not set_dead: a removed walker leaves no corpse, so the classic
// respawn engine has nothing to resurrect and nothing to scrub, and the
// snapshot the mirrors receive is simply smaller.
//
// The ONE exception is save_all_protected() — the level's "protected" npc
// flag marks characters a scenario needs alive (the two blessed campaign
// levels rely on it). Stripping those would break the campaign, so they
// stay regardless of the setting.
//
// Deterministic: list order, no RNG. Returns the number removed.
int classic_strip_authored_troops(GameWorld& world);

} // namespace og::sim
