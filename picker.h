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
// Class statistics,
// for (guess what?) controlling stats, etc ..
//

#ifndef __PICKER_H
#define __PICKER_H

class walker;

class statistics
{
	public:
		statistics();
		~statistics();
		short  try_command(short, short, short, short);
		void set_command(short, short, short, short);
		void do_command(walker  *owner);

		short hitpoints;
		short max_hitpoints;
		short magicpoints;
		short max_magicpoints;
		short commandcount;      // # times to execute command
		short command;       // command to execute
	private:
		short com1, com2;                // parameters to command


};

#endif
