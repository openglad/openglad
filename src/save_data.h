#ifndef _SAVE_DATA_H__
#define _SAVE_DATA_H__

#include "SDL.h"
#include <string>
#include <map>
#include <set>

class oblink;
class guy;

class SaveData
{
public:
    
    std::string current_campaign;
    short scen_num;
    std::map<std::string, std::set<int> > completed_levels;
    std::map<std::string, int> current_levels;
    Uint32 score;
    Uint32 m_score[4];
    Uint32 totalcash;
    Uint32 m_totalcash[4];
    Uint32 totalscore;
    Uint32 m_totalscore[4];
    short my_team;
    guy  *first_guy;  // not saved (that's what the oblist is for, but is loaded for holding character data
    unsigned char numplayers; //numviews
    short allied_mode;
    
    SaveData();
    ~SaveData();
    
    void reset();
    
    bool load(const std::string& filename);
    bool save(const std::string& filename, oblink* oblist);
    
    bool is_level_completed(int level_index) const;
    int get_num_levels_completed(const std::string& campaign) const;
    void add_level_completed(const std::string& campaign, int level_index);
};

#endif
