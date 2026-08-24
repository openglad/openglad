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
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/campaign_metadata.h>
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

// help.cpp: the intro-style scroller over an arbitrary campaign's full
// description (the browser's MORE control).
short show_campaign_description(screen* scr, const std::string& campaign_id);

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

    void draw(int team_power);
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

// Draw the highlighted entry's detail pane (right column of the browser).
void CampaignEntry::draw(int team_power)
{
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    const int cx = layout.title_center_x;

    text& loadtext = og::runtime::current_session->myscreen_->text_normal;
    const auto write_centered = [&](int y, const std::string& line,
                                    unsigned char color) {
        loadtext.write_xy(cx - static_cast<Sint32>(line.size()) * 3, y,
                          line.c_str(), color, 1);
    };

    // Title, fitted to the pane's budget so it can never reach a control.
    write_centered(layout.title_y, og::ui::fit_campaign_title(title), WHITE);

    // Rating stars
    std::string rating_text = "";
    for(int i = 0; i < int(rating); i++)
    {
        rating_text += '*';
    }
    // Print version
    std::string buf = std::format("v{}", version);
    if(rating_text.size() > 0)
    {
        loadtext.write_xy(cx - static_cast<Sint32>(rating_text.size())*3,
                          layout.title_y + 10, rating_text.c_str(), WHITE, 1);
        loadtext.write_xy(cx + static_cast<Sint32>(rating_text.size())*3 + 6,
                          layout.title_y + 10, buf.c_str(), WHITE, 1);
    }
    else
        write_centered(layout.title_y + 10, buf, WHITE);

    // Draw icon button
    og::runtime::current_session->myscreen_->draw_button(
        layout.icon.x - 2, layout.icon.y - 2, layout.icon.x + layout.icon.w + 2,
        layout.icon.y + layout.icon.h + 2, 1, 1);
    // Draw icon
    if (icon)
        icon->drawMix(layout.icon.x, layout.icon.y,
                      og::runtime::current_session->myscreen_->viewob[0].get());
    int y = layout.icon.y + layout.icon.h + 4;

    // Print suggested power
    // (the pane is 176px wide, so the power compare reads out as two rows)
    if(team_power >= 0)
    {
        write_centered(y, std::format("Your Power: {}", team_power),
                       LIGHT_GREEN);
        y += 8;
        if(suggested_power > 0)
        {
            write_centered(y, std::format("Suggested Power: {}", suggested_power),
                           team_power >= suggested_power ? LIGHT_GREEN : RED);
            y += 8;
        }
    }
    else if(suggested_power > 0)
    {
        write_centered(y, std::format("Suggested Power: {}", suggested_power),
                       LIGHT_GREEN);
        y += 8;
    }

    // Print completion progress
    if(num_levels_completed < 0)
        buf = std::format("{} level{}", num_levels, (num_levels == 1? "" : "s"));
    else
        buf = std::format("{} out of {} completed", num_levels_completed, num_levels);
    write_centered(y, buf, WHITE);
    y += 8;

    // Print authors
    if(authors.size() > 0)
    {
        write_centered(y, og::ui::fit_text_to_chars(
                              std::format("By {}", authors),
                              layout.title_max_chars),
                       WHITE);
    }

    // Draw description box
    // Text is flowed to the box's character budget (issue #152): Paragraphs
    // mode reflows the authored hand-wrap. Overflow is handled by the
    // browser's MORE control (a real button over the full-text scroller),
    // not a dead "(more...)" string.
    const og::ui::PickerRect& descbox = layout.desc_box;
    og::runtime::current_session->myscreen_->draw_box(descbox.x, descbox.y, descbox.x + descbox.w, descbox.y + descbox.h, GREY, 1, 1);
    const std::vector<std::string> desc_lines = og::core::wrap_text(
        description, layout.desc_max_chars, og::core::WrapMode::Paragraphs);
    for(int row = 0; row < layout.desc_rows &&
        row < static_cast<int>(desc_lines.size()); row++)
    {
        loadtext.write_xy(descbox.x + 5, descbox.y + 3 + 10 * row,
                          desc_lines[static_cast<std::size_t>(row)].c_str(),
                          BLACK, 1);
    }

    // Print contributors, on the MORE row left of the button.
    if(contributors.size() > 0)
    {
        const int contrib_budget =
            (layout.more_button.x - descbox.x - 6) / 6;
        buf = og::ui::fit_text_to_chars(
            std::format("Thanks to {}", contributors), contrib_budget);
        loadtext.write_xy(descbox.x, layout.more_button.y + 2, buf.c_str(),
                          WHITE, 1);
    }
}




CampaignResult pick_campaign(SaveData* save_data, bool enable_delete)
{
    // This campaign browser is fixed 320x200 UI. In particular, the editor calls
    // it while World is active; keep the campaign icon, text and controls on
    // the nearest UI present path instead of filtering the whole browser with
    // the editor map's SAI/Eagle setting.
    // #237 depth rule, applied by hand (legacy blocking browser): the
    // new-game flow's entry (depth 0) is a context switch and fades; SET
    // CAMPAIGN from an open Base Camp is nested and does not. The in-game
    // editor's browser is likewise nested inside the main menu's screen, so
    // only the standalone editor (openscen) reaches the fade here. The
    // fade-out must run BEFORE the UI-canvas switch below: it fades whatever
    // canvas is actually presented (openscen presents the World canvas), not
    // the stale UI surface.
    bool entry_fade_in_pending = og::ui::begin_legacy_menu_entry_fade();
    ScopedUiCanvas canvas_target(*og::runtime::current_session->myscreen_);
    std::string old_campaign_id = get_mounted_campaign();
    CampaignEntry* result = nullptr;
    CampaignResult ret_value;

    text& loadtext = og::runtime::current_session->myscreen_->text_normal;

    (void)unmount_campaign_package_with_error(old_campaign_id);

    // Here are the browser variables
    // The shelf (ids in select order) and its LAZY entry cache: list rows
    // need only the cached display title, so opening the browser mounts
    // nothing. The CampaignEntry (mount + yaml parse + icon decode) is built
    // on first highlight and cached.
    std::vector<std::string> ids;
    std::vector<std::unique_ptr<CampaignEntry>> entries;
    const auto rescan = [&]() {
        std::list<std::string> campaign_ids = list_campaigns();
        og::ui::order_campaigns_for_select(campaign_ids);
        og::ui::filter_campaigns_for_networked_lobby(
            campaign_ids, networked_campaign_select());
        ids.assign(campaign_ids.begin(), campaign_ids.end());
        entries.clear();
        entries.resize(ids.size());
    };
    rescan();

    const auto ensure_entry = [&](int index) -> CampaignEntry* {
        if (index < 0 || index >= static_cast<int>(ids.size()))
            return nullptr;
        auto& slot = entries[static_cast<std::size_t>(index)];
        if (!slot)
        {
            int num_completed = -1;
            if (save_data != nullptr)
                num_completed = save_data->get_num_levels_completed(
                    ids[static_cast<std::size_t>(index)]);
            slot = std::make_unique<CampaignEntry>(
                ids[static_cast<std::size_t>(index)], num_completed);
        }
        return slot.get();
    };

    // The list cursor starts on the campaign that was mounted.
    int cursor = 0;
    for (int index = 0; index < static_cast<int>(ids.size()); ++index)
    {
        if (ids[static_cast<std::size_t>(index)] == old_campaign_id)
            cursor = index;
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

    // Control geometry + nav (single source: picker_common).
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();

    const UiRect prev = layout.prev;
    const UiRect next = layout.next;
    const UiRect choose = layout.choose;
    const UiRect cancel = layout.cancel;
    const UiRect delete_button = layout.delete_button;
    // ENTER ID stacks UNDER DELETE/RESET so the title row is clear.
    const UiRect id_button = layout.id_button;
    const UiRect reset_button = layout.reset_button;
    const UiRect more_button = layout.more_button;

    int offset = og::ui::campaign_list_offset_for_cursor(
        cursor, 0, static_cast<int>(ids.size()), layout.list_rows);

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
	const int row_base_index = og::ui::kCampaignPickerRowBaseIndex;
	const int more_index = og::ui::kCampaignPickerMoreIndex;

	const UiRect row0 = og::ui::campaign_picker_row_rect(0);
	const UiRect row1 = og::ui::campaign_picker_row_rect(1);
	const UiRect row2 = og::ui::campaign_picker_row_rect(2);
	const UiRect row3 = og::ui::campaign_picker_row_rect(3);
	const UiRect row4 = og::ui::campaign_picker_row_rect(4);
	const UiRect row5 = og::ui::campaign_picker_row_rect(5);

	button buttons[og::ui::kCampaignPickerButtonCount] = {
        button("prev", "PREV", KEYSTATE_UNKNOWN, prev.x, prev.y, prev.w, prev.h, 0, -1 , MenuNav{}),
        button("next", "NEXT", KEYSTATE_UNKNOWN, next.x, next.y, next.w, next.h, 0, -1 , MenuNav{}),
        button("ok", "OK", KEYSTATE_UNKNOWN, choose.x, choose.y, choose.w, choose.h, 0, -1 , MenuNav{}),
        button("cancel", "CANCEL", KEYSTATE_ESCAPE, cancel.x, cancel.y, cancel.w, cancel.h, 0, -1 , MenuNav{}),
        button("delete", "DELETE", KEYSTATE_UNKNOWN, delete_button.x, delete_button.y, delete_button.w, delete_button.h, 0, -1 , MenuNav{}),
        button("enter_id", "ENTER ID", KEYSTATE_UNKNOWN, id_button.x, id_button.y, id_button.w, id_button.h, 0, -1 , MenuNav{}),
        button("reset", "RESET", KEYSTATE_UNKNOWN, reset_button.x, reset_button.y, reset_button.w, reset_button.h, 0, -1 , MenuNav{}),
        button("entry_1", "1", KEYSTATE_UNKNOWN, row0.x, row0.y, row0.w, row0.h, 0, -1 , MenuNav{}),
        button("entry_2", "2", KEYSTATE_UNKNOWN, row1.x, row1.y, row1.w, row1.h, 0, -1 , MenuNav{}),
        button("entry_3", "3", KEYSTATE_UNKNOWN, row2.x, row2.y, row2.w, row2.h, 0, -1 , MenuNav{}),
        button("entry_4", "4", KEYSTATE_UNKNOWN, row3.x, row3.y, row3.w, row3.h, 0, -1 , MenuNav{}),
        button("entry_5", "5", KEYSTATE_UNKNOWN, row4.x, row4.y, row4.w, row4.h, 0, -1 , MenuNav{}),
        button("entry_6", "6", KEYSTATE_UNKNOWN, row5.x, row5.y, row5.w, row5.h, 0, -1 , MenuNav{}),
        button("more", "MORE", KEYSTATE_UNKNOWN, more_button.x, more_button.y, more_button.w, more_button.h, 0, -1 , MenuNav{}),
	};

	// Rows visible on this page (recomputed by sync_visibility below).
	int visible_rows = 0;

	// Whole-graph rewire + hidden flags from the current list window
	// (picker_common owns the graph so the BFS pin can walk every variant
	// headlessly). Also loads the highlighted entry — the ONLY entry a frame
	// ever mounts.
	const auto sync_visibility = [&]() {
		const int total = static_cast<int>(ids.size());
		offset = og::ui::campaign_list_clamp_offset(offset, total,
		                                            layout.list_rows);
		cursor = og::ui::campaign_list_clamp_cursor(cursor, offset, total,
		                                            layout.list_rows);
		visible_rows = std::max(0, std::min(layout.list_rows, total - offset));

		CampaignEntry* current = ensure_entry(cursor);

		// Update hidden buttons
		buttons[prev_index].hidden = (offset == 0);
		buttons[next_index].hidden = (offset + layout.list_rows >= total);
		buttons[choose_index].hidden = (total == 0);
		buttons[delete_index].hidden = !enable_delete;
		buttons[reset_index].hidden = enable_delete;
		for (int row = 0; row < og::ui::kCampaignPickerRowCount; ++row)
			buttons[row_base_index + row].hidden = (row >= visible_rows);
		buttons[more_index].hidden =
			current == nullptr ||
			!og::ui::campaign_description_overflows(current->description);

		const og::ui::CampaignPickerVisibility visibility{
			.prev_hidden = buttons[prev_index].hidden,
			.next_hidden = buttons[next_index].hidden,
			.choose_hidden = buttons[choose_index].hidden,
			.delete_hidden = buttons[delete_index].hidden,
			.visible_rows = visible_rows,
			.more_hidden = buttons[more_index].hidden,
		};
		const auto nav = og::ui::campaign_picker_nav(visibility);
		for (int btn = 0; btn < og::ui::kCampaignPickerButtonCount; ++btn)
		{
			buttons[btn].nav = MenuNav{.up = nav[static_cast<std::size_t>(btn)].up, .down = nav[static_cast<std::size_t>(btn)].down,
			                           .left = nav[static_cast<std::size_t>(btn)].left, .right = nav[static_cast<std::size_t>(btn)].right};
		}

		if (buttons[highlighted_button].hidden)
		{
			if (highlighted_button >= row_base_index &&
			    highlighted_button < row_base_index + og::ui::kCampaignPickerRowCount &&
			    visible_rows > 0)
				highlighted_button = row_base_index + visible_rows - 1;
			else if (highlighted_button == prev_index && !buttons[next_index].hidden)
				highlighted_button = next_index;
			else if (highlighted_button == next_index && !buttons[prev_index].hidden)
				highlighted_button = prev_index;
			else
				highlighted_button = cancel_index;
		}
	};
	sync_visibility();

    bool done = false;
    int last_highlighted = highlighted_button;
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

        // Moving the keyboard highlight onto a list row moves the list
        // cursor with it, so the detail pane follows the arrow keys.
        if (highlighted_button != last_highlighted)
        {
            const int row = highlighted_button - row_base_index;
            if (row >= 0 && row < visible_rows)
                cursor = offset + row;
            last_highlighted = highlighted_button;
        }

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
        bool do_more = !buttons[more_index].hidden && ((do_click && more_button.x <= mx && mx <= more_button.x + more_button.w
               && more_button.y <= my && my <= more_button.y + more_button.h) || (retvalue == OG_OK && highlighted_button == more_index));
        // A click on a visible list row, or Enter while one is highlighted.
        int selected_row = -1;
        for (int row = 0; row < visible_rows; ++row)
        {
            const UiRect rect = og::ui::campaign_picker_row_rect(row);
            if ((do_click && rect.x <= mx && mx <= rect.x + rect.w
                 && rect.y <= my && my <= rect.y + rect.h) ||
                (retvalue == OG_OK && highlighted_button == row_base_index + row))
            {
                selected_row = row;
                break;
            }
        }
        const bool do_select = selected_row >= 0 && !do_prev && !do_next &&
            !do_choose && !do_cancel && !do_delete && !do_reset && !do_id &&
            !do_more;
#ifdef TESTING
        if (do_prev || do_next || do_choose || do_cancel || do_delete ||
            do_reset || do_id || do_more || do_select)
        {
            campaign_picker_testing_mark_action();
        }
#endif

			if (mymouse.left)
			{
			    wait_for_mouse_release();
			}

        // Prev page
        if(do_prev)
        {
            offset = og::ui::campaign_list_page_step(
                offset, static_cast<int>(ids.size()), layout.list_rows, -1);
            cursor = og::ui::campaign_list_clamp_cursor(
                cursor, offset, static_cast<int>(ids.size()), layout.list_rows);
        }
        // Next page
        else if(do_next)
        {
            offset = og::ui::campaign_list_page_step(
                offset, static_cast<int>(ids.size()), layout.list_rows, 1);
            cursor = og::ui::campaign_list_clamp_cursor(
                cursor, offset, static_cast<int>(ids.size()), layout.list_rows);
        }
        // Choose
        else if(do_choose)
        {
            result = ensure_entry(cursor);
            if(result != nullptr)
            {
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
           // Bounds check: DELETE stays clickable even when the shelf
           // scanned empty, so never index an empty list.
           if(cursor >= 0 && cursor < static_cast<int>(ids.size())
              && yes_or_no_prompt("Delete campaign", "Delete this campaign permanently?", false)
              && no_or_yes_prompt("Delete campaign", "Are you really sure?", false))
           {
               delete_campaign(ids[static_cast<std::size_t>(cursor)]);

               restore_default_campaigns();
               (void)remount_campaign_package_with_error();  // Just in case we deleted the current campaign

               // Reload the picker
               rescan();
               cursor = 0;
               offset = 0;
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
           if(cursor >= 0 && cursor < static_cast<int>(ids.size())
              && yes_or_no_prompt("Reset campaign", "Reset your progress\nin this campaign?", false)
              && no_or_yes_prompt("Reset campaign", "Are you really sure?", false))
           {
               og::runtime::current_session->myscreen_->save_data.reset_campaign(ids[static_cast<std::size_t>(cursor)]);
               // Drop the cached entry so its completion line reloads.
               entries[static_cast<std::size_t>(cursor)].reset();
           }
       }
       // Full description in the intro-style scroller
       else if(do_more)
       {
           CampaignEntry* current = ensure_entry(cursor);
           if(current != nullptr)
               (void)show_campaign_description(
                   og::runtime::current_session->myscreen_, current->id);
       }
       // Select a list row
       else if(do_select)
       {
           cursor = offset + selected_row;
           highlighted_button = row_base_index + selected_row;
           last_highlighted = highlighted_button;
       }

        retvalue = 0;

        sync_visibility();

        // Draw
        og::runtime::current_session->myscreen_->clearbuffer();

        // List pane: header, one row per campaign on this page, pagers with
        // the position readout between them.
        loadtext.write_xy(layout.list.x, layout.header_y, "CAMPAIGNS", WHITE, 1);
        for (int row = 0; row < visible_rows; ++row)
        {
            const UiRect rect = og::ui::campaign_picker_row_rect(row);
            const int index = offset + row;
            const std::string& row_id = ids[static_cast<std::size_t>(index)];
            og::runtime::current_session->myscreen_->draw_button(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, 1, 1);
            // Marker column: '*' flags the campaign that was active when the
            // browser opened.
            if (row_id == old_campaign_id)
                loadtext.write_xy(rect.x + 3, rect.y + 2, "*", RED, 1);
            const std::string label = og::ui::fit_campaign_row_label(
                og::data::campaign_display_title(row_id));
            loadtext.write_xy(rect.x + 10, rect.y + 2, label.c_str(),
                              index == cursor ? DARK_GREEN : DARK_BLUE, 1);
            if (index == cursor)
                og::runtime::current_session->myscreen_->draw_box(rect.x - 2, rect.y - 2, rect.x + rect.w + 2, rect.y + rect.h + 2, DARK_BLUE, 0, 1);
        }

        if(!buttons[prev_index].hidden)
        {
            og::runtime::current_session->myscreen_->draw_button(prev.x, prev.y, prev.x + prev.w, prev.y + prev.h, 1, 1);
            loadtext.write_xy(prev.x + 2, prev.y + 2, "Prev", DARK_BLUE, 1);
        }

        if(!buttons[next_index].hidden)
        {
            og::runtime::current_session->myscreen_->draw_button(next.x, next.y, next.x + next.w, next.y + next.h, 1, 1);
            loadtext.write_xy(next.x + 2, next.y + 2, "Next", DARK_BLUE, 1);
        }

        const std::string position_label = og::ui::format_campaign_position_label(
            cursor, static_cast<int>(ids.size()));
        loadtext.write_xy(layout.list.x + layout.list.w / 2
                              - static_cast<int>(position_label.size()) * 3,
                          prev.y + 2, position_label.c_str(), WHITE, 1);

        if(!buttons[choose_index].hidden)
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

        if(!buttons[more_index].hidden)
        {
            og::runtime::current_session->myscreen_->draw_button(more_button.x, more_button.y, more_button.x + more_button.w, more_button.y + more_button.h, 1, 1);
            loadtext.write_xy(more_button.x + 8, more_button.y + 2, "More", DARK_BLUE, 1);
        }

        // Draw entry
        // (the detail pane for the highlighted row, loaded by sync_visibility)
        if(cursor >= 0 && cursor < static_cast<int>(entries.size()) && entries[static_cast<std::size_t>(cursor)] != nullptr)
            entries[static_cast<std::size_t>(cursor)]->draw(army_power);

        draw_highlight(buttons[highlighted_button]);
        if (entry_fade_in_pending) {
            // fadeblack presents the composed buffer itself (#237).
            entry_fade_in_pending = false;
            og::runtime::current_session->myscreen_->fadeblack(1);
        } else {
            og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        }
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
            std::map<std::string, int>::const_iterator level_cursor =
                save_data->current_levels.find(ret_value.id);
            save_data->scen_num = static_cast<short>(
                level_cursor != save_data->current_levels.end()
                    ? level_cursor->second
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
