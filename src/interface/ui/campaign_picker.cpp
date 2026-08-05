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

#include <openglad/interface/ui/campaign_picker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/campaign_yaml.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/render/pixie.h>
#include <openglad/interface/render/text.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/button.h>
#include <openglad/interface/native_input.h>
#include <openglad/core/text_wrap.h>
#include <openglad/core/util.h>
#include <algorithm>
#include <format>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include <string>


bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);

bool prompt_for_string(const std::string& message, std::string& result);

inline constexpr int OG_OK = 4;
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);

#ifdef TESTING
bool campaign_picker_testing_take_abort();
void campaign_picker_testing_mark_entered();
void campaign_picker_testing_mark_action();
#endif

namespace
{
// The browser's control geometry lives in picker_common (pure, unit-pinned):
// see og::ui::campaign_picker_layout().
using UiRect = og::ui::PickerRect;

constexpr std::int32_t kReleaseWaitPollLimit = 5000;

// True while a genuine networked session is live at campaign-pick time:
// a hosted/joined lobby (SET CAMPAIGN while hosting) or a between-level
// team-build over the persistent connection (networked_session_ stays set
// through the return-to-menu round trip). Gates the tower off the shelf —
// the Endless Tower is local-only in v1.
bool networked_campaign_select()
{
    return picker_lobby_is_networked() ||
           (og::runtime::current_session != nullptr &&
            og::runtime::current_session->networked_session_);
}

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
            LogWarn("campaign_picker: mouse release wait timed out\n");
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
            LogWarn("campaign_picker: key release wait timed out in {}\n", context ? context : "(unknown)");
            break;
        }
    }
}
} // namespace

int toInt(const std::string& s)
{
    const auto parsed = parse_int_strict(s);
    if (!parsed)
    {
        LogWarn("Invalid integer string: '{}'\n", s);
        return 0;
    }
    return *parsed;
}

// load_campaign / load_campaign_with_error are now in
// src/io/platform_io_common.cpp (shared by SDL and headless).

class CampaignEntry
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
    
    PixieData icondata;
    std::unique_ptr<pixie> icon;
    
    // Player-specific
    int num_levels_completed;

    CampaignEntry(const std::string& campaign_id, int levels_completed);
    ~CampaignEntry();
    
    void draw(const UiRect& area, int team_power);
};

CampaignEntry::CampaignEntry(const std::string& campaign_id, int levels_completed)
    : id(campaign_id), title("Untitled"), rating(0.0f), version("1.0"), description("No description."), suggested_power(0), first_level(1), num_levels(0), num_levels_completed(levels_completed)
{
    // Load the campaign data from <user_data>/scen/<id>.glad
    if(mount_campaign_package_with_error(campaign_id) == CampaignPackageIoError::None)
    {
        og::data::CampaignYaml metadata;
        if(og::data::read_campaign_yaml("campaign.yaml", metadata) == og::data::CampaignYamlReadResult::Ok)
        {
            if(metadata.saw_title)
                title = metadata.title;
            if(metadata.saw_version)
                version = metadata.version;
            if(metadata.saw_authors)
                authors = metadata.authors;
            if(metadata.saw_contributors)
                contributors = metadata.contributors;
            if(metadata.saw_description)
                description = metadata.description;
            if(metadata.saw_suggested_power)
                suggested_power = metadata.suggested_power;
            if(metadata.saw_first_level)
                first_level = metadata.first_level;
        }

        // TODO: Get rating from website
        rating = 0.0f;
        
        std::string icon_file = "icon.png";
        icondata = read_pixie_file(icon_file.c_str());
        if(icondata.valid())
            icon = std::make_unique<pixie>(icondata);
        
	        // Count the number of levels
	        std::list<int> levels = list_levels();
	        num_levels = static_cast<int>(levels.size());

        (void)unmount_campaign_package_with_error(campaign_id);
    }

    if (version.empty())
        version = "1.0";
    if (first_level <= 0)
        first_level = 1;
}

CampaignEntry::~CampaignEntry()
{
    icondata.free();
}

void CampaignEntry::draw(const UiRect& area, int team_power)
{
    int x = area.x;
    int y = area.y;
    int w = area.w;
    int h = area.h;
    
    text& loadtext = og::runtime::current_session->myscreen_->text_normal;

    // Print title. Fitted to the width that clears the RESET/DELETE face on
    // the same pixel row (issue: long campaign names clipped into ENTER ID,
    // which now lives a row lower).
    const std::string shown_title = og::ui::fit_campaign_title(title);
    loadtext.write_xy(x + w/2 - static_cast<Sint32>(shown_title.size())*3, y - 22, shown_title.c_str(), WHITE, 1);

    // Rating stars
    std::string rating_text = "";
    for(int i = 0; i < int(rating); i++)
    {
        rating_text += '*';
    }
    loadtext.write_xy(x + w/2 - static_cast<Sint32>(rating_text.size())*3, y - 14, rating_text.c_str(), WHITE, 1);

    // Print version
    std::string buf = std::format("v{}", version);
    if(rating_text.size() > 0)
        loadtext.write_xy(x + w/2 + static_cast<Sint32>(rating_text.size())*3 + 6, y - 14, buf.c_str(), WHITE, 1);
    else
        loadtext.write_xy(x + w/2 - static_cast<Sint32>(buf.size())*3, y - 14, buf.c_str(), WHITE, 1);

    // Draw icon button
    og::runtime::current_session->myscreen_->draw_button(x - 2, y - 2, x + w + 2, y + h + 2, 1, 1);
    // Draw icon
	if (icon)
	    icon->drawMix(x, y, og::runtime::current_session->myscreen_->viewob[0].get());
	y += h + 4;

	// Print suggested power
	if(team_power >= 0)
    {
        buf = std::format("Your Power: {}", team_power);
        std::string buf2;
        if(suggested_power > 0)
            buf2 = std::format(", Suggested Power: {}", suggested_power);

        int len = static_cast<int>(buf.size());
        int len2 = static_cast<int>(buf2.size());
        loadtext.write_xy(x + w/2 - (len + len2)*3, y, buf.c_str(), LIGHT_GREEN, 1);
        loadtext.write_xy(x + w/2 - (len + len2)*3 + len*6, y, buf2.c_str(), (team_power >= suggested_power? LIGHT_GREEN : RED), 1);
    }
    else
    {
        if(suggested_power > 0)
            buf = std::format("Suggested Power: {}", suggested_power);
        else
            buf.clear();

        int len = static_cast<int>(buf.size());
        loadtext.write_xy(x + w/2 - (len)*3, y, buf.c_str(), LIGHT_GREEN, 1);
    }
    y += 8;

    // Print completion progress
    if(num_levels_completed < 0)
        buf = std::format("{} level{}", num_levels, (num_levels == 1? "" : "s"));
    else
        buf = std::format("{} out of {} completed", num_levels_completed, num_levels);
    loadtext.write_xy(x + w/2 - static_cast<int>(buf.size())*3, y, buf.c_str(), WHITE, 1);
    y += 8;

    // Print authors
    if(authors.size() > 0)
    {
        buf = std::format("By {}", authors);
        loadtext.write_xy(x + w/2 - static_cast<int>(buf.size())*3, y, buf.c_str(), WHITE, 1);
    }
    
    // Draw description box
    UiRect descbox = {160 - 225/2, area.y + area.h + 35, 225, 60};
    og::runtime::current_session->myscreen_->draw_box(descbox.x, descbox.y, descbox.x + descbox.w, descbox.y + descbox.h, GREY, 1, 1);
    
    // Print description, flowed to the box's character budget (issue #152):
    // text starts at descbox.x+5 and glyphs advance 6px. Campaign blurbs are
    // prose, so Paragraphs mode reflows the authored hand-wrap. When the
    // text is taller than the box, the last visible row becomes a
    // "(more...)" pointer at the full-text campaign intro scroller.
    const std::vector<std::string> desc_lines = og::core::wrap_text(
        description, (descbox.w - 10) / 6, og::core::WrapMode::Paragraphs);
    int j = 10;
    for(size_t li = 0; li < desc_lines.size(); li++)
    {
        if(j + 10 > descbox.h)
            break;
        const bool last_visible_row = (j + 20 > descbox.h);
        if(last_visible_row && li + 1 < desc_lines.size())
        {
            loadtext.write_xy(descbox.x + 5, descbox.y + j, "(more...)", BLACK, 1);
            break;
        }
        loadtext.write_xy(descbox.x + 5, descbox.y + j, desc_lines[li].c_str(), BLACK, 1);
        j += 10;
    }
    y = descbox.y + descbox.h + 2;
    
    // Print contributors
    if(contributors.size() > 0)
    {
        buf = std::format("Thanks to {}", contributors);
        loadtext.write_xy(x + w/2 - static_cast<int>(buf.size())*3, y, buf.c_str(), WHITE, 1);
        y += 10;
    }
}




CampaignResult pick_campaign(SaveData* save_data, bool enable_delete)
{
    // This campaign browser is fixed 320x200 UI. In particular, the editor calls
    // it while World is active; keep the campaign icon, text and controls on
    // the nearest UI present path instead of filtering the whole browser with
    // the editor map's SAI/Eagle setting.
    ScopedUiCanvas canvas_target(*og::runtime::current_session->myscreen_);
    std::string old_campaign_id = get_mounted_campaign();
    CampaignEntry* result = nullptr;
    CampaignResult ret_value;
    
    text& loadtext = og::runtime::current_session->myscreen_->text_normal;
    
    (void)unmount_campaign_package_with_error(old_campaign_id);

    // Here are the browser variables
    std::vector<std::unique_ptr<CampaignEntry>> entries;
    
    unsigned int current_campaign_index = 0;
    
    // Load campaigns (default campaign first; remainder stays alphabetical).
    std::list<std::string> campaign_ids = list_campaigns();
    og::ui::order_campaigns_for_select(campaign_ids);
    og::ui::filter_campaigns_for_networked_lobby(campaign_ids,
                                                 networked_campaign_select());
    int i = 0;
    for(auto& cid : campaign_ids)
    {
        int num_completed = -1;
        if(save_data != nullptr)
            num_completed = save_data->get_num_levels_completed(cid);
        entries.push_back(std::make_unique<CampaignEntry>(cid, num_completed));

        if(cid == old_campaign_id)
            current_campaign_index = static_cast<unsigned int>(i);

        i++;
    }

    // Figure out how good the player's army is
    int army_power = -1;
    if(save_data != nullptr)
    {
        army_power = 0;
        for(int team_idx = 0; team_idx < MAX_TEAM_SIZE; team_idx++)
        {
            if (save_data->team_list[static_cast<std::size_t>(team_idx)])
            {
                army_power += 3*save_data->team_list[static_cast<std::size_t>(team_idx)]->level;
            }
        }
    }
    
    // Campaign icon positioning + buttons (single source: picker_common).
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    const UiRect area = layout.icon;

    const UiRect prev = layout.prev;
    const UiRect next = layout.next;
    const UiRect choose = layout.choose;
    const UiRect cancel = layout.cancel;
    const UiRect delete_button = layout.delete_button;
    // ENTER ID stacks UNDER DELETE/RESET so the title row is clear.
    const UiRect id_button = layout.id_button;
    const UiRect reset_button = layout.reset_button;

    
    // Controller input
    int retvalue = 0;
	int highlighted_button = 3;
	
	const int prev_index = og::ui::kCampaignPickerPrevIndex;
	const int next_index = og::ui::kCampaignPickerNextIndex;
	const int choose_index = og::ui::kCampaignPickerChooseIndex;
	const int cancel_index = og::ui::kCampaignPickerCancelIndex;
	const int delete_index = og::ui::kCampaignPickerDeleteIndex;
	const int id_index = og::ui::kCampaignPickerIdIndex;
	const int reset_index = og::ui::kCampaignPickerResetIndex;

	// Whole-graph rewire from the current visibility, applied at setup and
	// again whenever a click changes what is showing (picker_common owns the
	// graph so the BFS pin can walk every variant headlessly).
	const auto apply_nav = [&](button* rows) {
		const og::ui::CampaignPickerVisibility visibility{
			.prev_hidden = rows[prev_index].hidden,
			.next_hidden = rows[next_index].hidden,
			.choose_hidden = rows[choose_index].hidden,
			.delete_hidden = rows[delete_index].hidden,
		};
		const auto nav = og::ui::campaign_picker_nav(visibility);
		for (int btn = 0; btn < og::ui::kCampaignPickerButtonCount; ++btn)
		{
			rows[btn].nav = MenuNav{.up = nav[static_cast<std::size_t>(btn)].up, .down = nav[static_cast<std::size_t>(btn)].down,
			                        .left = nav[static_cast<std::size_t>(btn)].left, .right = nav[static_cast<std::size_t>(btn)].right};
		}
	};
	
	button buttons[] = {
        button("prev", "PREV", KEYSTATE_UNKNOWN, prev.x, prev.y, prev.w, prev.h, 0, -1 , MenuNav{.up=id_index, .down=cancel_index, .right=next_index}),
        button("next", "NEXT", KEYSTATE_UNKNOWN, next.x, next.y, next.w, next.h, 0, -1 , MenuNav{.up=id_index, .down=choose_index, .left=prev_index}),
        button("ok", "OK", KEYSTATE_UNKNOWN, choose.x, choose.y, choose.w, choose.h, 0, -1 , MenuNav{.up=next_index, .left=cancel_index}),
        button("cancel", "CANCEL", KEYSTATE_ESCAPE, cancel.x, cancel.y, cancel.w, cancel.h, 0, -1 , MenuNav{.up=prev_index, .right=choose_index}),
        button("delete", "DELETE", KEYSTATE_UNKNOWN, delete_button.x, delete_button.y, delete_button.w, delete_button.h, 0, -1 , MenuNav{.down=id_index}),
        button("enter_id", "ENTER ID", KEYSTATE_UNKNOWN, id_button.x, id_button.y, id_button.w, id_button.h, 0, -1 , MenuNav{.up=delete_index, .down=next_index}),
        button("reset", "RESET", KEYSTATE_UNKNOWN, reset_button.x, reset_button.y, reset_button.w, reset_button.h, 0, -1 , MenuNav{.down=id_index}),
	};
	
	buttons[prev_index].hidden = (current_campaign_index == 0);
	buttons[next_index].hidden = (current_campaign_index + 1 >= entries.size());
	buttons[choose_index].hidden = !(current_campaign_index < entries.size() && entries[current_campaign_index] != nullptr);
	buttons[delete_index].hidden = !enable_delete;
	buttons[reset_index].hidden = enable_delete;
	
	apply_nav(buttons);

    bool done = false;
#ifdef TESTING
    campaign_picker_testing_mark_entered();
#endif
    while (!done)
    {
#ifdef TESTING
        if (campaign_picker_testing_take_abort())
            break;
#endif
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
		bool do_prev = !buttons[prev_index].hidden && ((do_click && prev.x <= mx && mx <= prev.x + prev.w
               && prev.y <= my && my <= prev.y + prev.h) || (retvalue == OG_OK && highlighted_button == prev_index));
        bool do_next = !buttons[next_index].hidden && ((do_click && next.x <= mx && mx <= next.x + next.w
               && next.y <= my && my <= next.y + next.h) || (retvalue == OG_OK && highlighted_button == next_index));
        bool do_choose = !buttons[choose_index].hidden && ((do_click && choose.x <= mx && mx <= choose.x + choose.w
               && choose.y <= my && my <= choose.y + choose.h) || (retvalue == OG_OK && highlighted_button == choose_index));
        bool do_cancel = (do_click && cancel.x <= mx && mx <= cancel.x + cancel.w
               && cancel.y <= my && my <= cancel.y + cancel.h) || (retvalue == OG_OK && highlighted_button == cancel_index) || og::runtime::current_session->keystates_[buttons[cancel_index].hotkey];
        bool do_delete = !buttons[delete_index].hidden && ((do_click && enable_delete && delete_button.x <= mx && mx <= delete_button.x + delete_button.w
               && delete_button.y <= my && my <= delete_button.y + delete_button.h) || (retvalue == OG_OK && highlighted_button == delete_index));
        bool do_reset = !buttons[reset_index].hidden && ((do_click && reset_button.x <= mx && mx <= reset_button.x + reset_button.w
               && reset_button.y <= my && my <= reset_button.y + reset_button.h) || (retvalue == OG_OK && highlighted_button == reset_index));
        bool do_id = (do_click && id_button.x <= mx && mx <= id_button.x + id_button.w
               && id_button.y <= my && my <= id_button.y + id_button.h) || (retvalue == OG_OK && highlighted_button == id_index);
#ifdef TESTING
        if (do_prev || do_next || do_choose || do_cancel || do_delete ||
            do_reset || do_id)
        {
            campaign_picker_testing_mark_action();
        }
#endif

			if (mymouse.left)
			{
			    wait_for_mouse_release();
			}

        // Prev
        if(do_prev)
        {
            if(current_campaign_index > 0)
            {
                current_campaign_index--;
            }
        }
        // Next
        else if(do_next)
        {
            if(current_campaign_index + 1 < entries.size())
            {
                current_campaign_index++;
            }
        }
        // Choose
        else if(do_choose)
        {
            if(current_campaign_index < entries.size() && entries[current_campaign_index] != nullptr)
            {
                result = entries[current_campaign_index].get();
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
           if(yes_or_no_prompt("Delete campaign", "Delete this campaign permanently?", false)
              && no_or_yes_prompt("Delete campaign", "Are you really sure?", false))
           {
               delete_campaign(entries[current_campaign_index]->id);
               
               restore_default_campaigns();
               (void)remount_campaign_package_with_error();  // Just in case we deleted the current campaign
               
               // Reload the picker
	               entries.clear();
               
               campaign_ids = list_campaigns();
               og::ui::order_campaigns_for_select(campaign_ids);
               og::ui::filter_campaigns_for_networked_lobby(
                   campaign_ids, networked_campaign_select());

                for(auto& cid : campaign_ids)
                {
                    int num_completed = -1;
                    if(save_data != nullptr)
                        num_completed = save_data->get_num_levels_completed(cid);
	                    entries.push_back(std::make_unique<CampaignEntry>(cid, num_completed));
	                }
                
                current_campaign_index = 0;
           }
       }
        // Enter ID
        else if(do_id)
       {
            std::string campaign;
            if(prompt_for_string("Enter Campaign ID", campaign) && campaign.size() > 0)
            {
                result = nullptr;
                ret_value.id = campaign;
                done = true;
                break;
            }
       }
       // Reset progress
       else if(do_reset)
       {
           if(yes_or_no_prompt("Reset campaign", "Reset your progress\nin this campaign?", false)
              && no_or_yes_prompt("Reset campaign", "Are you really sure?", false))
           {
               og::runtime::current_session->myscreen_->save_data.reset_campaign(entries[current_campaign_index]->id);
           }
       }
       
        retvalue = 0;

        // Update hidden buttons
        if(do_prev || do_next || do_choose || do_cancel || do_delete || do_id)
        {
            buttons[prev_index].hidden = (current_campaign_index == 0);
            buttons[next_index].hidden = (current_campaign_index + 1 >= entries.size());
            buttons[choose_index].hidden = !(current_campaign_index < entries.size() && entries[current_campaign_index] != nullptr);
            buttons[delete_index].hidden = !enable_delete;
            buttons[reset_index].hidden = enable_delete;

            apply_nav(buttons);
            
            if(buttons[highlighted_button].hidden)
            {
                if(highlighted_button == prev_index && !buttons[next_index].hidden)
                    highlighted_button = next_index;
                else if(highlighted_button == next_index && !buttons[prev_index].hidden)
                    highlighted_button = prev_index;
                else
                    highlighted_button = cancel_index;
            }
        }

        // Draw
        og::runtime::current_session->myscreen_->clearbuffer();

        if(current_campaign_index > 0)
        {
            og::runtime::current_session->myscreen_->draw_button(prev.x, prev.y, prev.x + prev.w, prev.y + prev.h, 1, 1);
            loadtext.write_xy(prev.x + 2, prev.y + 2, "Prev", DARK_BLUE, 1);
        }
        
        if(current_campaign_index + 1 < entries.size())
        {
            og::runtime::current_session->myscreen_->draw_button(next.x, next.y, next.x + next.w, next.y + next.h, 1, 1);
            loadtext.write_xy(next.x + 2, next.y + 2, "Next", DARK_BLUE, 1);
        }
        
        if(current_campaign_index < entries.size() && entries[current_campaign_index] != nullptr)
        {
            og::runtime::current_session->myscreen_->draw_button(choose.x, choose.y, choose.x + choose.w, choose.y + choose.h, 1, 1);
            loadtext.write_xy(choose.x + 9, choose.y + 2, "OK", DARK_GREEN, 1);
        }
        og::runtime::current_session->myscreen_->draw_button(cancel.x, cancel.y, cancel.x + cancel.w, cancel.y + cancel.h, 1, 1);
        loadtext.write_xy(cancel.x + 2, cancel.y + 2, "Cancel", RED, 1);
        if(enable_delete)
        {
            og::runtime::current_session->myscreen_->draw_button(delete_button.x, delete_button.y, delete_button.x + delete_button.w, delete_button.y + delete_button.h, 1, 1);
            loadtext.write_xy(delete_button.x + 2, delete_button.y + 2, "Delete", RED, 1);
        }
        else
        {
            og::runtime::current_session->myscreen_->draw_button(reset_button.x, reset_button.y, reset_button.x + reset_button.w, reset_button.y + reset_button.h, 1, 1);
            loadtext.write_xy(reset_button.x + 2, reset_button.y + 2, "Reset", RED, 1);
        }
        
        og::runtime::current_session->myscreen_->draw_button(id_button.x, id_button.y, id_button.x + id_button.w, id_button.y + id_button.h, 1, 1);
        loadtext.write_xy(id_button.x + 2, id_button.y + 2, "Enter ID", DARK_BLUE, 1);
        
        // Draw entry
        if(current_campaign_index < entries.size() && entries[current_campaign_index] != nullptr)
            entries[current_campaign_index]->draw(area, army_power);

        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        og::input_native::sleep_ms(10);
    }

    wait_for_key_release(KEYSTATE_q, "quit");
    
    // Restore old campaign
    (void)mount_campaign_package_with_error(old_campaign_id);
    
    if(result != nullptr)
    {
        ret_value.id = result->id;
        ret_value.first_level = result->first_level;

        // Campaign-select completion heal (tower-triple §5.9, risk R9):
        // mount the selection and resolve its cursor exactly as the caller's
        // load_campaign will, then let the mounted mode regenerate any
        // missing generated level BEFORE any preview reads it (a tower
        // resume can point at a floor whose user-dir files are gone; the
        // missing-file fallback would silently wrap the run to the Gate).
        // Classic campaigns: ensure_level_available is a no-op. The caller's
        // own load_campaign then finds the selection already mounted and
        // just reads the cursor, so the early mount is invisible to it.
        if(save_data != nullptr &&
           load_campaign_with_error(ret_value.id, save_data->current_levels,
                                    ret_value.first_level).error ==
               CampaignLoadError::None)
        {
            const short kept_cursor = save_data->scen_num;
            std::map<std::string, int>::const_iterator cursor =
                save_data->current_levels.find(ret_value.id);
            save_data->scen_num = static_cast<short>(
                cursor != save_data->current_levels.end() ? cursor->second
                                                          : ret_value.first_level);
            og::mode::current_progression().ensure_level_available(*save_data);
            save_data->scen_num = kept_cursor;
        }
    }

	    return ret_value;
}

#ifdef TESTING
#include "../../../tests/coverage_internal/campaign_picker_internal.inc"
#endif
