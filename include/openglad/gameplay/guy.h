/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

class walker;

// Holds attributes for characters.
// Used to store character data in SaveData's team_list.
// Historical note (Jonathan Dearborn, 2013): "Used as walker::myguy in-game
// for various attribute-dependent effects for walkers who are on the player
// team." Company Base Camp made the last clause historical: a guy now means
// persistence and attributes, not an alliance.
// Used as walker::myguy for company persistence and character attributes.
// Presence of a guy does not imply team membership or combat friendliness.
class guy
{
	public:
		guy ();
			guy (int whatfamily);
			guy (const guy& copy);
			~guy();
			std::int32_t query_heart_value(); // how much are we worth?
			void upgrade_to_level(short new_level, bool set_xp = true);
		
        float get_hp_bonus() const;
        float get_mp_bonus() const;
        float get_damage_bonus() const;
        float get_armor_bonus() const;
        float get_speed_bonus() const;
        float get_fire_frequency_bonus() const;
        
		void update_derived_stats(walker* w);

		std::string name;
		char family;  // our family
		short strength;
		short dexterity;
		short constitution;
		short intelligence;
		short armor;
		std::uint32_t exp;
		short kills;       // version 3+
		std::int32_t level_kills;  // version 3+
		std::int32_t total_damage; // version 4+  // This will not be exact after changing damage to floating point, but binary serialization of floats is messy.
		std::int32_t total_hits;   // version 4+
		std::int32_t total_shots;  // version 4+
		short teamnum;     // version 5+
		
		// Stats for the last battle
		float scen_damage;
		short scen_kills;
		float scen_damage_taken;
		float scen_min_hp;
		short scen_shots;
		short scen_hits;
        
        // In ID for comparing old guys with their duplicated counterparts after battle
        int id;

        short level;

        // Sentinel for "no networked owner" (single-player / AI characters).
        static constexpr std::uint8_t kNoOwner = 0xff;
        // Networked player slot that owns this character (0..MAX_PLAYERS-1), or
        // kNoOwner. Transient runtime-only state: set at lobby assembly, carried
        // through the snapshot stream, and used so each player persists only its
        // own characters. Never serialized to disk by SaveData::save/load.
        std::uint8_t owner_player_index = kNoOwner;
        // The owning player's SaveData::team_list slot this character came from
        // (its own save0 slot, NOT the combined-roster index). Lets the owner
        // write progress back to the right slot. Transient; never on disk.
        std::uint8_t owner_save_slot = kNoOwner;
        // Mission-deploy flag (GTL v14, docs/company-basecamp-design.md §3.1):
        // false = held back at base camp; held-back characters never enter a
        // level's oblist and survive SaveData::update_guys untouched (§3.3).
        // Persisted as a u8 at guy record offset +50 (former reserved bytes).
        // Base-camp/save-layer state ONLY — sim code must never read it.
        // statscopy deliberately does NOT copy it (train/rename must not
        // clobber deployment); the defaulted copy constructor DOES, so it
        // rides guy objects through the update/merge paths.
        bool deployed = true;
};
