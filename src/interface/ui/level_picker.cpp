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

#include <openglad/interface/ui/level_picker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/interface/render/radar.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/render/text.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/native_input.h>
#include <openglad/resources/io_common.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <list>
#include <memory>
#include <string>

#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>



// MAX_TEAM_SIZE is defined in <openglad/resources/save_data.h>
#include <openglad/resources/save_data.h>


bool yes_or_no_prompt(const char* title, const char* message, bool default_value);

bool prompt_for_string(const std::string& message, std::string& result);

void popup_dialog(const char* title, const char* message);


inline constexpr int OK = 4;
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);

namespace
{
struct UiRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

constexpr std::int32_t kReleaseWaitPollLimit = 5000;

void wait_for_mouse_release()
{
    MouseState& mymouse = query_mouse();
    std::int32_t poll_count = 0;
    while (mymouse.left)
    {
        get_input_events(POLL);
        og::input_native::sleep_ms(1);
        poll_count++;
        if (poll_count >= kReleaseWaitPollLimit)
        {
            LogWarn("level_picker: mouse release wait timed out\n");
            break;
        }
    }
}

void wait_for_key_release(int key, const char* context)
{
    std::int32_t poll_count = 0;
    while (og::runtime::current_session->keystates_[key])
    {
        og::input_native::sleep_ms(1);
        get_input_events(POLL);
        poll_count++;
        if (poll_count >= kReleaseWaitPollLimit)
        {
            LogWarn("level_picker: key release wait timed out in {}\n", context ? context : "(unknown)");
            break;
        }
    }
}
} // namespace


void getLevelStats(LevelRuntimeData& level_data, int* max_enemy_level, float* average_enemy_level, int* num_enemies, float* difficulty, std::list<int>& exits)
{
    int num = 0;
    int level_sum = 0;
    int difficulty_sum = 0;
    int difficulty_sum_friends = 0;
    int diff_per_level = 3;
    
    int max_level = 0;
    exits.clear();
    
    // Go through objects
		for (auto& uptr : level_data.world().oblist)
		{
		    walker* ob = uptr.get();
		    if (!ob) continue;
	        switch(ob->query_order())
	        {
	            case Order::Living:
                if(ob->team_num() != 0)
                {
                    num++;
                    level_sum += ob->stats()->level();
                    difficulty_sum += diff_per_level*ob->stats()->level();
                    if(ob->stats()->level() > max_level)
                        max_level = ob->stats()->level();
                }
                else
                {
                    difficulty_sum_friends += diff_per_level*ob->stats()->level();
                }
	                break;
	            default:
	                break;
	        }
		}
	
	// Go through effects
		for (auto& uptr : level_data.world().fxlist)
		{
		    walker* ob = uptr.get();
		    if (!ob) continue;
	        switch(ob->query_order())
	        {
	            case Order::Treasure:
	                if(ob->family() == FAMILY_EXIT)
	                {
	                    exits.push_back(ob->stats()->level());
	                }
	                break;
	            default:
	                break;
	        }
		}
	
	if(num_enemies)
	    *num_enemies = num;
	if(max_enemy_level)
	    *max_enemy_level = max_level;
	if(average_enemy_level)
	{
		if(num == 0)
			*average_enemy_level = 0;
		else
			*average_enemy_level = static_cast<float>(level_sum) / static_cast<float>(num);
	}
	if(difficulty)
		*difficulty = static_cast<float>(difficulty_sum - difficulty_sum_friends);
    
    exits.sort();
    exits.unique();
}


bool isDir(const std::string& filename)
{
    struct stat status;
    if (stat(filename.c_str(), &status) != 0)
        return false;

    return (status.st_mode & S_IFDIR);
}


bool sort_scen(const std::string& first, const std::string& second)
{
    std::string s1;
    std::string s1num;
    std::string s2;
    std::string s2num;
    
    bool gotNum = false;
    for(std::string::const_iterator e = first.begin(); e != first.end(); e++)
    {
        if(!gotNum && std::isalpha(static_cast<unsigned char>(*e)))
            s1 += *e;
        else
            s1num += *e;
    }
    
    gotNum = false;
    for(std::string::const_iterator e = second.begin(); e != second.end(); e++)
    {
        if(!gotNum && std::isalpha(static_cast<unsigned char>(*e)))
            s2 += *e;
        else
            s2num += *e;
    }
    
    if(s1 == s2)
    {
        const auto n1 = parse_int_strict(s1num);
        const auto n2 = parse_int_strict(s2num);
        if (n1 && n2)
            return *n1 < *n2;
        return first < second;
    }
    return (first < second);
}


class BrowserEntry
{
    public:
    
    LevelRuntimeData level_data;
    UiRect mapAreas;
    radar myradar;
    std::string level_name;
    int max_enemy_level;
    float average_enemy_level;
    int num_enemies;
    float difficulty;
    std::list<int> exits;
    std::array<std::string, 80> scentext;           // Array to hold scenario information
    int scentextlines = 0;                 // How many lines of text in scenario info
    
    BrowserEntry(screen* screenp, int index, int scen_num);
    ~BrowserEntry();
    
    void updateIndex(int index);
    void draw(screen* screenp);
};

BrowserEntry::BrowserEntry(screen* screenp, int index, int scen_num)
	    : level_data(scen_num, false, &sdl_level_data_hooks()), myradar(nullptr, og::runtime::current_session->myscreen_, 0)
{
	(void)screenp;
	    level_data.load();
    
    myradar.start(&level_data);
    

	    const int w = myradar.xview;
	    const int h = myradar.yview;
    
    mapAreas.w = w;
    mapAreas.h = h;
    mapAreas.x = 10;
    mapAreas.y = 5 + (53 + 12)*index;
    
	    myradar.xloc = static_cast<short>(mapAreas.x + mapAreas.w/2 - w/2);
	    myradar.yloc = static_cast<short>(mapAreas.y + 10);
    
    
    getLevelStats(level_data, &max_enemy_level, &average_enemy_level, &num_enemies, &difficulty, exits);
    
    // Store this level's info
    level_name = level_data.world().title;
    if(level_name.size() > 20)
    {
        level_name = level_name.substr(0, 20) + "...";
    }
    
	    scentextlines = static_cast<int>(std::min<size_t>(level_data.description.size(), 80u));
    int i = 0;
    for(auto& line : level_data.description)
    {
        scentext[static_cast<std::size_t>(i)] = line;
        i++;
        if(i >= 80)
            break;
    }
}


BrowserEntry::~BrowserEntry() = default;

void BrowserEntry::updateIndex(int index)
{
	    const int w = myradar.xview;
	    mapAreas.y = 5 + (53 + 12)*index;
	    
	    myradar.xloc = static_cast<short>(mapAreas.x + mapAreas.w/2 - w/2);
	    myradar.yloc = static_cast<short>(mapAreas.y + 10);
}

void BrowserEntry::draw(screen* screenp)
{
	(void)screenp;
	    int x = myradar.xloc;
	    int y = myradar.yloc;
    int w = myradar.xview;
    int h = myradar.yview;
    og::runtime::current_session->myscreen_->draw_button(x - 2, y - 2, x + w + 2, y + h + 2, 1, 1);
    // Draw radar
    myradar.draw(&level_data);
    
    text& loadtext = og::runtime::current_session->myscreen_->text_normal;
    loadtext.write_xy(mapAreas.x, mapAreas.y, level_name.c_str(), DARK_BLUE, 1);
    
    std::string buf = std::format("ID: {}", level_data.world().id);
    loadtext.write_xy(x + w + 5, y, buf.c_str(), WHITE, 1);
    buf = std::format("Enemies: {}", num_enemies);
    loadtext.write_xy(x + w + 5, y + 8, buf.c_str(), WHITE, 1);
    buf = std::format("Max level: {}", max_enemy_level);
    loadtext.write_xy(x + w + 5, y + 16, buf.c_str(), WHITE, 1);
    buf = std::format("Avg level: {:.1f}", average_enemy_level);
    loadtext.write_xy(x + w + 5, y + 24, buf.c_str(), WHITE, 1);
    buf = std::format("Difficulty: {:.0f}", difficulty);
    loadtext.write_xy(x + w + 5, y + 32, buf.c_str(), RED, 1);

    if(exits.size() > 0)
    {
        std::string exits_str = "Exits: ";
        bool first = true;
        for(auto exit_level : exits)
        {
            if(!first) exits_str += ", ";
            exits_str += std::to_string(exit_level);
            first = false;
        }
        if(exits_str.size() > 20)
            exits_str = exits_str.substr(0, 17) + "...";
        loadtext.write_xy(x + w + 5, y + 40, exits_str.c_str(), WHITE, 1);
    }
}


inline constexpr int NUM_BROWSE_RADARS = 3;

// Load a scenario...
int pick_level(screen *screenp, int default_level, bool enable_delete)
{
    // The radar previews and browser controls are fixed-coordinate UI. The
    // editor enters this picker from its World target, so explicitly use the
    // nearest UI canvas and restore World on return.
    ScopedUiCanvas canvas_target(*screenp);
    int result = default_level;
    
	text& loadtext = og::runtime::current_session->myscreen_->text_normal;
    
    // Here are the browser variables
    std::array<std::unique_ptr<BrowserEntry>, NUM_BROWSE_RADARS> entries;
    
	    std::vector<int> level_list = list_levels_v();
	    int level_list_length = static_cast<int>(level_list.size());
    
    // This indexes into the level_list.
    int current_level_index = 0;
    
    // Figure out the list index for the current scen_level, so we can start there.
    for(int i = 0; i < level_list_length; i++)
    {
        if(level_list[i] == default_level)
            current_level_index = i;
    }

    // Clamp so a full NUM_BROWSE_RADARS window fits (mirrors the delete-reload
    // clamp below); otherwise current_level_index + i can index past the end of
    // level_list in the initial radar-load loop (out-of-bounds read).
    if(current_level_index > level_list_length - NUM_BROWSE_RADARS)
        current_level_index = std::max(0, level_list_length - NUM_BROWSE_RADARS);

    // Load the radars (minimaps)
    for(int i = 0; i < NUM_BROWSE_RADARS; i++)
    {
        if(current_level_index + i < level_list_length)
            entries[i] = std::make_unique<BrowserEntry>(og::runtime::current_session->myscreen_, i, level_list[current_level_index + i]);
        else
            entries[i].reset();
    }
    
    int selected_entry = -1;
    
    // Figure out how good the player's army is
    int army_power = 0;
	for(int i=0; i<MAX_TEAM_SIZE; i++)
	{
		if (og::runtime::current_session->myscreen_->save_data.team_list[i])
		{
		    army_power += 3*og::runtime::current_session->myscreen_->save_data.team_list[i]->level;
		}
	}
    
    // Buttons
    int screenW = 320;
    int screenH = 200;
    UiRect prev = {screenW - 150, 20, 30, 10};
    UiRect next = {screenW - 150, screenH - 50, 30, 10};
    UiRect descbox = {prev.x - 40, prev.y + 15, 185, next.y - 10 - (prev.y + prev.h)};
    
    UiRect choose = {screenW - 50, screenH - 30, 30, 10};
    UiRect cancel = {screenW - 100, screenH - 30, 38, 10};
    UiRect delete_button = {screenW - 50, 10, 38, 10};
    UiRect id_button = {delete_button.x - 52 - 10, 10, 52, 10};
    
    // Controller input
    int retvalue = 0;
	int highlighted_button = 3;
	
	int prev_index = 0;
	int next_index = 1;
	int choose_index = 2;
	int cancel_index = 3;
	int delete_index = 4;
	int id_index = 5;
	int entry1_index = 6;
	int entry2_index = 7;
	int entry3_index = 8;
	
	button buttons[] = {
        button("prev", "PREV", KEYSTATE_UNKNOWN, prev.x, prev.y, prev.w, prev.h, 0, -1 , MenuNav{.down=next_index, .left=entry1_index, .right=id_index}),
        button("next", "NEXT", KEYSTATE_UNKNOWN, next.x, next.y, next.w, next.h, 0, -1 , MenuNav{.up=prev_index, .left=entry3_index, .right=cancel_index}),
        button("ok", "OK", KEYSTATE_UNKNOWN, choose.x, choose.y, choose.w, choose.h, 0, -1 , MenuNav{.up=id_index, .left=cancel_index}, true),
        button("cancel", "CANCEL", KEYSTATE_ESCAPE, cancel.x, cancel.y, cancel.w, cancel.h, 0, -1 , MenuNav{.up=id_index, .left=next_index, .right=choose_index}),
        button("delete", "DELETE", KEYSTATE_UNKNOWN, delete_button.x, delete_button.y, delete_button.w, delete_button.h, 0, -1 , MenuNav{.down=choose_index, .left=id_index}, true),
        button("enter_id", "ENTER ID", KEYSTATE_UNKNOWN, id_button.x, id_button.y, id_button.w, id_button.h, 0, -1 , MenuNav{.down=cancel_index, .left=prev_index, .right=delete_index}),
        button("entry_1", "1", KEYSTATE_UNKNOWN, 10, 15, 40, (53 - 12), 0, -1 , MenuNav{.down=entry2_index, .right=prev_index}),
        button("entry_2", "2", KEYSTATE_UNKNOWN, 10, 15 + (53 + 12), 40, (53 - 12), 0, -1 , MenuNav{.up=entry1_index, .down=entry3_index, .right=next_index}),
        button("entry_3", "3", KEYSTATE_UNKNOWN, 10, 15 + (53 + 12)*2, 40, (53 - 12), 0, -1 , MenuNav{.up=entry2_index, .right=next_index}),

	};
    
    bool done = false;
	while (!done)
	{
		// Reset the timer count to zero ...
		reset_timer();

		if (og::runtime::current_session->myscreen_->world().end)
			break;

		// Get keys and stuff
		get_input_events(POLL);
		
        handle_menu_nav(buttons, highlighted_button, retvalue, false);
		
		// Quit if 'q' is pressed
		if(og::runtime::current_session->keystates_[KEYSTATE_q])
            done = true;
		
		// Mouse stuff ..
	        MouseState& mymouse = query_mouse();
	        int mx = static_cast<int>(mymouse.x);
	        int my = static_cast<int>(mymouse.y);
        
        bool do_click = mymouse.left;
		bool do_prev = (do_click && prev.x <= mx && mx <= prev.x + prev.w
               && prev.y <= my && my <= prev.y + prev.h) || (retvalue == OK && highlighted_button == prev_index);
        bool do_next = (do_click && next.x <= mx && mx <= next.x + next.w
               && next.y <= my && my <= next.y + next.h) || (retvalue == OK && highlighted_button == next_index);
        bool do_choose = selected_entry >= 0 && ((do_click && choose.x <= mx && mx <= choose.x + choose.w
               && choose.y <= my && my <= choose.y + choose.h) || (retvalue == OK && highlighted_button == choose_index));
        bool do_cancel = (do_click && cancel.x <= mx && mx <= cancel.x + cancel.w
               && cancel.y <= my && my <= cancel.y + cancel.h) || (retvalue == OK && highlighted_button == cancel_index) || og::runtime::current_session->keystates_[buttons[cancel_index].hotkey];
        bool do_delete = selected_entry >= 0 && ((do_click && enable_delete && delete_button.x <= mx && mx <= delete_button.x + delete_button.w
               && delete_button.y <= my && my <= delete_button.y + delete_button.h) || (retvalue == OK && highlighted_button == delete_index));
        bool do_id = (do_click && id_button.x <= mx && mx <= id_button.x + id_button.w
               && id_button.y <= my && my <= id_button.y + id_button.h) || (retvalue == OK && highlighted_button == id_index);
        bool do_select = do_click || (retvalue == OK && (highlighted_button == entry1_index || highlighted_button == entry2_index || highlighted_button == entry3_index));
        
        
			if (mymouse.left)
			{
			    wait_for_mouse_release();
			}
        
        // Prev
        if(do_prev)
	            {
	                if(current_level_index > 0)
	                {
	                    selected_entry = -1;
	                    if(highlighted_button == delete_index || highlighted_button == choose_index)
	                        highlighted_button = prev_index;
	                    current_level_index--;
	                    
	                    // Delete the bottom one and shift the rest down
	                    entries[NUM_BROWSE_RADARS-1].reset();
	                    for(int i = NUM_BROWSE_RADARS-1; i > 0; i--)
	                    {
	                        entries[i] = std::move(entries[i-1]);
	                        if(entries[i] != nullptr)
	                            entries[i]->updateIndex(i);
	                    }
	                    // Load the new top one
	                    if(current_level_index < level_list_length)
	                        entries[0] = std::make_unique<BrowserEntry>(og::runtime::current_session->myscreen_, 0, level_list[current_level_index]);
	                }
	           }
        // Next
        else if(do_next)
           {
                if(current_level_index < level_list_length - NUM_BROWSE_RADARS)
	                {
	                    selected_entry = -1;
	                    if(highlighted_button == delete_index || highlighted_button == choose_index)
	                        highlighted_button = prev_index;
	                    current_level_index++;
	                    
	                    // Delete the top one and shift the rest up
	                    entries[0].reset();
	                    for(int i = 0; i < NUM_BROWSE_RADARS-1; i++)
	                    {
	                        entries[i] = std::move(entries[i+1]);
	                        if(entries[i] != nullptr)
	                            entries[i]->updateIndex(i);
	                    }
	                    // Load the new bottom one
	                    if(current_level_index + NUM_BROWSE_RADARS-1 < level_list_length)
	                        entries[NUM_BROWSE_RADARS-1] = std::make_unique<BrowserEntry>(og::runtime::current_session->myscreen_, NUM_BROWSE_RADARS-1, level_list[current_level_index + NUM_BROWSE_RADARS-1]);
	                }
	           }
        // Choose
        else if(do_choose)
           {
               if(selected_entry != -1)
               {
                   result = level_list[current_level_index + selected_entry];
                   done = true;
                   break;
               }
           }
        // Cancel
        else if(do_cancel)
           {
                wait_for_key_release(buttons[cancel_index].hotkey, "cancel");
               done = true;
               break;
           }
        // Delete
        else if(do_delete)
           {
               if(yes_or_no_prompt("Delete level", "Delete this level permanently?", false))
               {
                   delete_level(level_list[current_level_index + selected_entry]);
                   
	                   // Reload the picker
	                   level_list = list_levels_v();
	                    level_list_length = static_cast<int>(level_list.size());
                    
                    // Make sure our currently showing radars are not blank
                    if(current_level_index + NUM_BROWSE_RADARS >= level_list_length)
                    {
                        if(level_list_length > NUM_BROWSE_RADARS)
                            current_level_index = level_list_length-NUM_BROWSE_RADARS;
                        else
                            current_level_index = 0;
                    }
                    
	                    // Load the radars (minimaps)
	                    for(int i = 0; i < NUM_BROWSE_RADARS; i++)
	                    {
	                        if(i < level_list_length)
	                            entries[i] = std::make_unique<BrowserEntry>(og::runtime::current_session->myscreen_, i, level_list[current_level_index + i]);
	                        else
	                            entries[i].reset();
	                    }
                    
                    selected_entry = -1;
                    if(highlighted_button == delete_index || highlighted_button == choose_index)
                        highlighted_button = prev_index;
               }
           }
        // Enter ID
        else if(do_id)
           {
                std::string level;
                if(prompt_for_string("Enter Level ID (num)", level) && level.size() > 0)
                {
                    const auto parsed = parse_int_strict(level);
                    if (parsed && *parsed > 0)
                    {
                        result = *parsed;
                        done = true;
                        break;
                    }
                    popup_dialog("Invalid input", "Please enter a positive integer Level ID.");
                }
           }
        else if(do_select)
        {
            selected_entry = -1;
            if(highlighted_button == delete_index || highlighted_button == choose_index)
                highlighted_button = prev_index;
            // Select
            for(int i = 0; i < NUM_BROWSE_RADARS; i++)
            {
                if(i < level_list_length && entries[i] != nullptr)
                {
                    int x = entries[i]->myradar.xloc;
                    int y = entries[i]->myradar.yloc;
                    int w = entries[i]->myradar.xview;
                    int h = entries[i]->myradar.yview;
                    UiRect b = {x - 2, y - 2, w + 2, h + 2};
                    if((do_click && b.x <= mx && mx <= b.x+b.w
                       && b.y <= my && my <= b.y+b.h) || (retvalue == OK && highlighted_button - entry1_index == i))
                       {
                           selected_entry = i;
                           break;
                       }
                }
            }
        }
		
        retvalue = 0;
		
		// Update hidden buttons
		if(selected_entry >= 0 && enable_delete)
            buttons[delete_index].hidden = false;
        else
            buttons[delete_index].hidden = true;
        
		if(selected_entry >= 0)
            buttons[choose_index].hidden = false;
        else
            buttons[choose_index].hidden = true;
        
        // Draw
        og::runtime::current_session->myscreen_->clearbuffer();
        
        std::string buf = std::format("Army power: {}", army_power);
        loadtext.write_xy(prev.x + 50, prev.y + 2, buf.c_str(), RED, 1);
        
        og::runtime::current_session->myscreen_->draw_button(prev.x, prev.y, prev.x + prev.w, prev.y + prev.h, 1, 1);
        loadtext.write_xy(prev.x + 2, prev.y + 2, "Prev", DARK_BLUE, 1);
        og::runtime::current_session->myscreen_->draw_button(next.x, next.y, next.x + next.w, next.y + next.h, 1, 1);
        loadtext.write_xy(next.x + 2, next.y + 2, "Next", DARK_BLUE, 1);
        if(selected_entry != -1 && selected_entry < level_list_length && entries[selected_entry] != nullptr)
        {
            og::runtime::current_session->myscreen_->draw_button(choose.x, choose.y, choose.x + choose.w, choose.y + choose.h, 1, 1);
            loadtext.write_xy(choose.x + 9, choose.y + 2, "OK", DARK_GREEN, 1);
            loadtext.write_xy(next.x, choose.y + 20, entries[selected_entry]->level_name.c_str(), DARK_GREEN, 1);
        }
        og::runtime::current_session->myscreen_->draw_button(cancel.x, cancel.y, cancel.x + cancel.w, cancel.y + cancel.h, 1, 1);
        loadtext.write_xy(cancel.x + 2, cancel.y + 2, "Cancel", RED, 1);
        if(selected_entry >= 0 && enable_delete)
        {
            og::runtime::current_session->myscreen_->draw_button(delete_button.x, delete_button.y, delete_button.x + delete_button.w, delete_button.y + delete_button.h, 1, 1);
            loadtext.write_xy(delete_button.x + 2, delete_button.y + 2, "Delete", RED, 1);
        }
        
        og::runtime::current_session->myscreen_->draw_button(id_button.x, id_button.y, id_button.x + id_button.w, id_button.y + id_button.h, 1, 1);
        loadtext.write_xy(id_button.x + 2, id_button.y + 2, "Enter ID", DARK_BLUE, 1);
        
        if(selected_entry != -1)
        {
            int i = selected_entry;
            if(i < level_list_length && entries[i] != nullptr)
            {
                int x = entries[i]->myradar.xloc - 4;
                int y = entries[i]->myradar.yloc - 4;
                int w = entries[i]->myradar.xview + 8;
                int h = entries[i]->myradar.yview + 8;
                og::runtime::current_session->myscreen_->draw_box(x, y, x + w, y + h, DARK_BLUE, 1, 1);
            }
        }
        for(int i = 0; i < NUM_BROWSE_RADARS; i++)
        {
            if(i < level_list_length && entries[i] != nullptr)
                entries[i]->draw(og::runtime::current_session->myscreen_);
        }
        
        // Description
        if(selected_entry != -1 && selected_entry < level_list_length && entries[selected_entry] != nullptr)
        {
            og::runtime::current_session->myscreen_->draw_box(descbox.x, descbox.y, descbox.x + descbox.w, descbox.y + descbox.h, GREY, 1, 1);
            for(int i = 0; i < entries[selected_entry]->scentextlines; i++)
            {
                if(prev.y + 20 + 10*i+1 > descbox.y + descbox.h)
                    break;
                loadtext.write_xy(descbox.x, descbox.y + 10*i+1, entries[selected_entry]->scentext[static_cast<std::size_t>(i)].c_str(), BLACK, 1);
            }
        }
        
        
        draw_highlight(buttons[highlighted_button]);
		og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
		og::input_native::sleep_ms(10);
	}
	
    wait_for_key_release(KEYSTATE_q, "quit");
	
		return result;
	}
