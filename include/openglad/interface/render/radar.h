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

// Definition of RADAR class

#include <openglad/interface/level_runtime_data.h>
#include <utility>
#include <vector>

class screen;
class viewscreen;

inline constexpr int RADAR_X = 60;  // These are the dimensions of the radar
inline constexpr int RADAR_Y = 44;  // viewport

// The radar block — where the minimap sits inside a view pane — as ONE rule,
// so the things that mirror it never drift from it. radar::sync_to_grid and
// radar::sync_position_to_view place the live radar with it;
// screen::relayout_camera_view mirrors it to stack the one-seat camera pane
// directly above the block (docs/camera-views-design.md §6, the second-minimap
// ruling). Coordinates are the pane's own — the GameplayUI canvas coordinates
// the radar draws in, inside a ScopedGameplayUiViewLayout scope.
struct RadarBlock
{
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int margin = 0;  // the gap the block keeps from its pane edge
};

// The block's viewport extents on a grid_w x grid_h tile grid: RADAR_X x
// RADAR_Y, clamped to a grid smaller than itself. A degenerate grid (no level
// loaded) clamps to nothing, which is what keeps the pre-level radar's plot
// loops empty; a caller that needs a drawable block regardless substitutes
// the unclamped RADAR_X/RADAR_Y.
[[nodiscard]] std::pair<int, int> radar_block_extents(int grid_w, int grid_h);

// The w x h block anchored inside a view pane whose top edge is pane_yloc and
// whose right/bottom edges are pane_endx/pane_endy. force_lower is the level
// editor's minimap placement (touch builds only).
[[nodiscard]] RadarBlock radar_block_for_pane(int pane_yloc, int pane_endx,
                                              int pane_endy, int w, int h,
                                              bool force_lower);

class radar
{
	public:
		radar(viewscreen * myview, screen * screen_ctx, short whatnum);
		~radar();
		short draw();
		short draw(LevelRuntimeData* data);
		// Extents recorded from the grid at the last sync; zero-initialized
		// so update()/draw() before any start() can detect the mismatch and
		// resync instead of looping over garbage bounds.
		short sizex = 0, sizey = 0;
		short xpos = 0, ypos = 0;
		short xloc = 0, yloc = 0;        // where on the screen to display
		// Zardus: radarx and radary are now class members (instead of temp vars) so that scen can use them for scroll
		short radarx = 0, radary = 0;	  // what actual portion of the map is on the radar (top-left coord)
		short on_screen();
		short on_screen(short whatx, short whaty, short hor, short ver);
		short refresh();
		void update(); // slow function to update radar/map info
		void update(LevelRuntimeData* data);
		void start();
		void start(LevelRuntimeData* data);

		screen * screenp;
		viewscreen * viewscreenp;
		std::vector<unsigned char> bmp;
		// Which floor's terrain is baked into bmp. draw() re-bakes whenever
		// the wanted floor (control walker's floor, or the editor's floor
		// override) changes; single-floor levels never leave floor 0, so
		// their radar stays pixel-identical to the pre-Z renderer.
		short bmp_floor_ = 0;
		bool force_lower_position;
		short xview = 0;
		short yview = 0;
	protected:
		// Re-derive sizex/sizey/size, the view clamps, the on-screen
		// placement, and the bmp allocation from the live grid.
		void sync_to_grid(LevelRuntimeData* data);
		void sync_position_to_view();
		short mynum; // what is my viewscreen-related number?
		//         char  *buffer;
		unsigned short size = 0;
};
