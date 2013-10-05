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
 
#ifndef _LEVEL_DATA_H__
#define _LEVEL_DATA_H__

#include "SDL.h"
#include <list>
#include <string>

class screen;
class pixie;
class pixieN;
class loader;
class walker;
class statistics;
class oblink;
class obmap;

#include "smooth.h"
#include "pixie_data.h"
#include "pixdefs.h"

class CampaignData
{
public:
    
    std::string id;
    std::string title;
    float rating;
    std::string version;
    std::string authors;
    std::string contributors;
    std::list<std::string> description;
    int suggested_power;
    int first_level;
    
    int num_levels;
    
    PixieData icondata;
    pixie* icon;
    
    CampaignData(const std::string& id);
    ~CampaignData();
    
    bool load();
    bool save();
    bool save_as(const std::string& new_id);
    
    std::string getDescriptionLine(int i);
};



class LevelData
{
public:
    int id;
    std::string title;
    
    static const char TYPE_CAN_EXIT_WHENEVER = 0x1;  // Can exit without defeating all enemies
    static const char TYPE_MUST_DESTROY_GENERATORS = 0x2;  // Must destroy generators to exit
    static const char TYPE_MUST_PROTECT_NAMED_NPCS = 0x4;  // Must protect named NPCs or else you lose
    char type;
    
    std::string grid_file;
    short par_value;
    short time_bonus_limit;  // frames until you get no time bonus
    PixieData grid;
    Sint32 pixmaxx, pixmaxy;
    
    smoother mysmoother;
    loader* myloader;
    int numobs;
    std::list<walker*> oblist;
    std::list<walker*> fxlist;  // fx--explosions, etc.
    std::list<walker*> weaplist;  // weapons
    // Keep a list of dead guys so weapons can still have valid owners
    std::list<walker*> dead_list;
    
    obmap* myobmap;
    std::list<std::string> description;
    
    // Drawing details
    PixieData pixdata[PIX_MAX];
    pixieN* back[PIX_MAX];
    Sint32 topx, topy;
    
    LevelData(int id);
    ~LevelData();
    
    bool load();
    bool save();
    
    walker* add_ob(char order, char family, bool atstart = false);
    walker* add_fx_ob(char order, char family);
    walker* add_weap_ob(char order, char family);
    short remove_ob(walker  *ob);
    
    void create_new_grid();
    void resize_grid(int width, int height);
    void delete_grid();
    void delete_objects();
    void clear();
    
    void set_draw_pos(Sint32 topx, Sint32 topy);
    void add_draw_pos(Sint32 topx, Sint32 topy);
    void draw(screen* myscreen);
    
    std::string get_description_line(int i);
};



#endif
