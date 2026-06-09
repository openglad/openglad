#include "base.h"
#include "guy.h"
#include "pixien.h"
#include "screen.h"
#include "walker.h"

#include <cstdlib>
#include <map>
#include <string>

screen* myscreen = nullptr;
pixieN* backdrops[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

Sint32 current_difficulty = 1;
Sint32 difficulty_level[DIFFICULTY_SETTINGS] = {50, 100, 200};

bool results_screen(int, int)
{
    return false;
}

bool results_screen(int, int, std::map<int, guy*>&, std::map<int, walker*>&)
{
    return false;
}

bool yes_or_no_prompt(const char*, const char*, bool)
{
    return true;
}

bool no_or_yes_prompt(const char*, const char*, bool)
{
    return true;
}

bool prompt_for_string(const std::string&, std::string&)
{
    return false;
}

void popup_dialog(const char*, const char*) {}

Uint32 get_time_bonus(int)
{
    return 0;
}

short remaining_foes(screen* s, walker* myguy)
{
    if (s == nullptr || myguy == nullptr) return 0;
    short foes = 0;
    for (walker* w : s->level_data.oblist)
    {
        if (w && !w->dead && w->query_order() == ORDER_LIVING &&
            !myguy->is_friendly(w))
            ++foes;
    }
    return foes;
}

short remaining_team(screen* s, char team)
{
    if (s == nullptr) return 0;
    short count = 0;
    for (walker* w : s->level_data.oblist)
    {
        if (w && !w->dead && w->query_order() == ORDER_LIVING &&
            w->team_num == static_cast<unsigned char>(team))
            ++count;
    }
    return count;
}

short score_panel(screen*)
{
    return 1;
}

short score_panel(screen*, short)
{
    return 1;
}

short new_score_panel(screen*, short)
{
    return 1;
}

void quit(Sint32 code)
{
    std::exit(static_cast<int>(code));
}

int toInt(const std::string& s)
{
    return std::atoi(s.c_str());
}

int load_campaign(const std::string& campaign,
                  std::map<std::string, int>& current_levels,
                  int first_level)
{
    auto it = current_levels.find(campaign);
    return it == current_levels.end() ? first_level : it->second;
}

const char* get_family_string(short family)
{
    switch (family)
    {
        case FAMILY_ARCHER:          return "ARCHER";
        case FAMILY_CLERIC:          return "CLERIC";
        case FAMILY_DRUID:           return "DRUID";
        case FAMILY_ELF:             return "ELF";
        case FAMILY_MAGE:            return "MAGE";
        case FAMILY_SOLDIER:         return "SOLDIER";
        case FAMILY_THIEF:           return "THIEF";
        case FAMILY_ARCHMAGE:        return "ARCHMAGE";
        case FAMILY_ORC:             return "ORC";
        case FAMILY_BIG_ORC:         return "ORC CAPTAIN";
        case FAMILY_BARBARIAN:       return "BARBARIAN";
        case FAMILY_FIREELEMENTAL:   return "ELEMENTAL";
        case FAMILY_SKELETON:        return "SKELETON";
        case FAMILY_SLIME:
        case FAMILY_MEDIUM_SLIME:
        case FAMILY_SMALL_SLIME:     return "SLIME";
        case FAMILY_FAERIE:          return "FAERIE";
        case FAMILY_GHOST:           return "GHOST";
        default:                     return "BEAST";
    }
}

Sint32 beginmenu(Sint32) { return 0; }
Sint32 create_team_menu(Sint32) { return 0; }
Sint32 create_detail_menu(guy*) { return 0; }
Sint32 create_view_menu(Sint32) { return 0; }
Sint32 create_hire_menu(Sint32) { return 0; }
Sint32 create_train_menu(Sint32) { return 0; }
Sint32 create_load_menu(Sint32) { return 0; }
Sint32 create_save_menu(Sint32) { return 0; }
Sint32 create_progress_menu(Sint32) { return 0; }
Sint32 go_menu(Sint32) { return 0; }
Sint32 increase_stat(Sint32, Sint32) { return 0; }
Sint32 decrease_stat(Sint32, Sint32) { return 0; }
Sint32 cycle_guy(Sint32) { return 0; }
Sint32 cycle_team_guy(Sint32) { return 0; }
Sint32 add_guy(Sint32) { return 0; }
Sint32 edit_guy(Sint32) { return 0; }
Sint32 do_save(Sint32) { return 0; }
Sint32 do_load(Sint32) { return 0; }
Sint32 set_player_mode(Sint32) { return 0; }
Sint32 name_guy(Sint32) { return 0; }
Sint32 do_set_scen_level(Sint32) { return 0; }
Sint32 do_pick_campaign(Sint32) { return 0; }
Sint32 set_difficulty() { return 0; }
Sint32 change_teamnum(Sint32) { return 0; }
Sint32 change_hire_teamnum(Sint32) { return 0; }
Sint32 change_allied() { return 0; }
Sint32 level_editor() { return 0; }
Sint32 main_options() { return 0; }
Sint32 overscan_adjust(Sint32) { return 0; }
