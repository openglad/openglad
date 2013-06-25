#ifndef _LEVEL_DATA_H__
#define _LEVEL_DATA_H__

class LevelData;

#include <list>
#include <string>
#include "pixie.h"
#include "loader.h"
#include "walker.h"
#include "stats.h"
#include "smooth.h"


class CampaignData
{
public:
    
    std::string id;
    std::string title;
    float rating;
    std::string version;
    std::string authors;
    std::string contributors;
    std::string description;
    int suggested_power;
    int first_level;
    
    int num_levels;
    
    unsigned char* icondata;
    pixie* icon;
    
    CampaignData(const std::string& id);
    ~CampaignData();
    
    bool load();
    bool save();
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
    unsigned char* grid;
    Sint32 maxx, maxy;
    Sint32 pixmaxx, pixmaxy;
    
    smoother mysmoother;
    loader* myloader;
    int numobs;
    oblink* oblist;
    oblink* fxlist;  // fx--explosions, etc.
    oblink* weaplist;  // weapons
    obmap* myobmap;
    std::list<std::string> description;
    
    // Drawing details
    unsigned char* pixdata[PIX_MAX];
    pixieN* back[PIX_MAX];
    Sint32 topx, topy;
    
    LevelData(int id);
    ~LevelData();
    
    bool load();
    bool save();
    
    walker* add_ob(char order, char family, bool atstart = false);
    walker* add_fx_ob(char order, char family);
    walker* add_weap_ob(char order, char family);
    short remove_ob(walker  *ob, short no_delete);
    
    void create_new_grid();
    void resize_grid(int width, int height);
    void delete_grid();
    void delete_objects();
    void clear();
    
    void set_draw_pos(Sint32 topx, Sint32 topy);
    void add_draw_pos(Sint32 topx, Sint32 topy);
    void draw(screen* myscreen);
};



#endif
