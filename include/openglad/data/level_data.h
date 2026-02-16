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

#include "SDL.h"
#include <list>
#include <memory>
#include <string>

// Forward-declare Order enum class (defined in base.h)
enum class Order : unsigned char;

class screen;
class pixie;
class pixieN;
class loader;
class walker;
class statistics;
class obmap;

#include <openglad/data/smooth.h>
#include <openglad/data/pixie_data.h>
#include <openglad/legacy/pixdefs.h>
class CampaignData
{
public:
    enum class IoError
    {
        None = 0,
        PackageMountFailed,
        OpenReadFailed,
        OpenWriteFailed,
        PackageUnpackFailed,
        PackageRepackFailed,
        ParseFailed
    };
    
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
    std::unique_ptr<pixie> icon;
    
    CampaignData(const std::string& campaign_id);
    ~CampaignData();
    
    bool load();
    bool save();
    bool save_as(const std::string& new_id);
    [[nodiscard]] IoError load_with_error();
    [[nodiscard]] IoError save_with_error();
    [[nodiscard]] IoError save_as_with_error(const std::string& new_id);
    [[nodiscard]] IoError last_io_error() const { return last_io_error_; }
    
    std::string get_description_line(int i);
    std::string getDescriptionLine(int i);

private:
    IoError last_io_error_ = IoError::None;
};



class LevelData
{
public:
    enum class IoError
    {
        None = 0,
        OpenReadFailed,
        OpenWriteFailed,
        InvalidHeader,
        ParseFailed,
        UnsupportedVersion,
        SerializeFailed
    };

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
    short level_done = 0;  // Set by sim tick: 0=foes remain, 1=all foes dead+exit, 2=no foes
    
    smoother mysmoother;
    std::unique_ptr<loader> myloader;
    int numobs;
    std::list<std::unique_ptr<walker>> oblist;
    std::list<std::unique_ptr<walker>> fxlist;  // fx--explosions, etc.
    std::list<std::unique_ptr<walker>> weaplist;  // weapons
    // Keep a list of dead guys so weapons can still have valid owners
    std::list<std::unique_ptr<walker>> dead_list;
    
    std::unique_ptr<obmap> myobmap;
    std::list<std::string> description;
    
    // Drawing details
    PixieData pixdata[PIX_MAX];
    std::unique_ptr<pixieN> back[PIX_MAX];
    Sint32 topx, topy;
    
    LevelData(int level_id);
    ~LevelData();
    
    bool load();
    bool save();
    [[nodiscard]] IoError load_with_error();
    [[nodiscard]] IoError save_with_error();
    [[nodiscard]] IoError last_io_error() const { return last_io_error_; }
    
    walker* add_ob(Order order, Sint32 family, [[maybe_unused]] bool atstart = false);
    walker* add_fx_ob(Order order, Sint32 family);
    walker* add_weap_ob(Order order, Sint32 family);
    short remove_ob(walker  *ob);
    
    // Collision/passability queries (moved from screen)
    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);
    bool query_passable(Sint32 x, Sint32 y, walker* ob) { return query_passable(static_cast<float>(x), static_cast<float>(y), ob); }
    bool query_object_passable(Sint32 x, Sint32 y, walker* ob) { return query_object_passable(static_cast<float>(x), static_cast<float>(y), ob); }
    bool query_grid_passable(Sint32 x, Sint32 y, walker* ob) { return query_grid_passable(static_cast<float>(x), static_cast<float>(y), ob); }

    // Entity search (moved from screen)
    walker* find_near_foe(walker* ob);
    walker* find_far_foe(walker* ob);
    walker* find_nearest_blood(walker* who);
    walker* find_nearest_player(walker* ob);
    std::list<walker*> find_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
    std::list<walker*> find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
    std::list<walker*> find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
    std::list<walker*> find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);

    void create_new_grid();
    void resize_grid(int width, int height);
    void delete_grid();
    void delete_objects();
    void clear();
    
    void set_draw_pos(Sint32 new_topx, Sint32 new_topy);
    void add_draw_pos(Sint32 dx, Sint32 dy);
    void draw(screen* myscreen);
    
    std::string get_description_line(int i);

private:
    IoError last_io_error_ = IoError::None;
};

// Read a scenario title from a .fss file. Returns "none" on failure.
std::string get_scenario_title(const char* filename);

// Count living foes not friendly to the given walker.
short remaining_foes(LevelData& level, walker* myguy);
