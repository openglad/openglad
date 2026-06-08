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
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include <format>
#include <openglad/core/util.h>
#include <openglad/core/version.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/base.h>
#include <openglad/resources/og_file.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/game_context.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define YIELD_SLEEP(ms) emscripten_sleep(ms)
#else
#define YIELD_SLEEP(ms) og::input_native::sleep_ms(ms)
#endif

inline constexpr Sint32 HELPTEXT_LEFT = 40;
inline constexpr Sint32 HELPTEXT_TOP = 40;
inline constexpr Sint32 DISPLAY_LINES = 15;
constexpr Sint32 text_down(Sint32 x) { return (x * 7) + HELPTEXT_TOP; }

namespace
{
inline short& help_end_of_file()
{
    return og::runtime::current_session->help_end_of_file_;
}

#ifdef TESTING
bool s_help_force_scroll_text = false;
#endif
} // namespace

#ifdef TESTING
void help_testing_set_force_scroll_text(bool enabled)
{
	s_help_force_scroll_text = enabled;
}

bool help_testing_force_scroll_text()
{
	return s_help_force_scroll_text;
}
#endif


// Shared scrolling text viewer used by read_scenario and read_campaign_intro.
// GetLine(int index) should return a std::string for the given line index.
template <typename GetLine>
static short scroll_text_view(screen* scr, int num_lines, int box_width,
    const char* title, Sint32 buf_x, Sint32 buf_y, Sint32 buf_w, Sint32 buf_h,
    GetLine get_line)
{
	Sint32 screenlines = num_lines * 8;
	Sint32 linesdown = 0;
	Sint32 changed = 1;
	Sint32 templines;
	Sint32 text_delay = 1;
	Sint32 key_presses = 0;

	text& mytext = scr->text_normal;
	Sint32 start_time = query_timer();
	Sint32 now_time;
	Sint32 bottomrow = screenlines - ((DISPLAY_LINES-1)*8);

	clear_keyboard();

	while (!query_input_continue())
	{
		YIELD_SLEEP(10);
		get_input_events(POLL);

		short scroll_delta = get_and_reset_scroll_amount();
		if (scroll_delta < 0)
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % text_delay;
			if (!key_presses && (linesdown < bottomrow))
			{
				while (linesdown < bottomrow && scroll_delta != 0)
				{
					linesdown++;
					scroll_delta++;
				}
				changed = 1;
			}
		}

		if (og::runtime::current_session->keystates_[KEYSTATE_PAGEDOWN])
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % (10*text_delay);
			if (!key_presses && (linesdown < bottomrow))
			{
				templines = linesdown + (DISPLAY_LINES * 7);
				if (templines > bottomrow)
					templines = bottomrow;
				if (linesdown != templines)
				{
					linesdown = templines;
					changed = 1;
				}
			}
		}

		if (scroll_delta > 0)
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % text_delay;
			if (!key_presses && linesdown)
			{
				while (linesdown && scroll_delta != 0)
				{
					linesdown--;
					scroll_delta--;
				}
				changed = 1;
			}
		}

		if (og::runtime::current_session->keystates_[KEYSTATE_PAGEUP])
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % (10*text_delay);
			if (!key_presses && linesdown)
			{
				linesdown -= (DISPLAY_LINES * 7);
				if (linesdown < 0)
					linesdown = 0;
				changed = 1;
			}
		}

		if (changed)
		{
			templines = linesdown/8;
			scr->draw_button(HELPTEXT_LEFT-4, HELPTEXT_TOP-4-8,
			                 HELPTEXT_LEFT+box_width, HELPTEXT_TOP+107, 3, 1);
			for (Sint32 j = 0; j < DISPLAY_LINES; j++)
			{
				std::string line = get_line(j + templines);
				if (line.size() > 0)
					mytext.write_xy(HELPTEXT_LEFT+2, static_cast<short>(text_down(j)-linesdown%8),
					                line.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
			}

			scr->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP-8,
			                   HELPTEXT_LEFT+box_width-4, HELPTEXT_TOP-2);
			scr->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP+97,
			                   HELPTEXT_LEFT+box_width-4, HELPTEXT_TOP+103);
			int title_x = HELPTEXT_LEFT + box_width/2 - static_cast<int>(strlen(title))*3;
			mytext.write_xy(title_x, HELPTEXT_TOP-7, title, static_cast<unsigned char>(RED), 1);
			int cont_len = static_cast<int>(strlen(CONTINUE_ACTION_STRING " TO CONTINUE"));
			int cont_x = HELPTEXT_LEFT + box_width/2 - cont_len*3;
			mytext.write_xy(cont_x, HELPTEXT_TOP+98,
			                CONTINUE_ACTION_STRING " TO CONTINUE", static_cast<unsigned char>(RED), 1);
			scr->buffer_to_screen(buf_x, buf_y, buf_w, buf_h);
			changed = 0;
		}
	}

	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}

	return static_cast<short>(screenlines);
}

short read_scenario(screen *s)
{
#ifdef TESTING
	if (!help_testing_force_scroll_text())
		return 1;
#endif
	return scroll_text_view(s,
		static_cast<int>(s->level_description().size()), 200,
		"SCENARIO INFORMATION", 0, 0, 320, 200,
		[&](int idx) { return s->get_level_description_line(idx); });
}

short read_campaign_intro(screen *s)
{
	CampaignData data(s->save_data.current_campaign);
	if (!data.load())
		return 1;

	help_end_of_file() = 0;
	return scroll_text_view(s,
		static_cast<int>(data.description.size()), 240,
		data.title.c_str(), HELPTEXT_LEFT-4, HELPTEXT_TOP-4-8, 244, 119,
		[&](int idx) { return data.getDescriptionLine(idx); });
}


// OgFile-based overloads (used by tests and headless builds)
std::string read_one_line(og::io::OgFile& infile, short length)
{
    char temp;
    std::string newline;
    newline.reserve(static_cast<size_t>(length));
    for (short i = 0; i < length; i++) {
        size_t n = infile.read(&temp, 1, 1);
        if (n != 1) { help_end_of_file() = 1; return newline; }
        if (temp == '\n' || temp == '\r') return newline;
        newline.push_back(temp);
    }
    return newline;
}

short fill_help_array(char somearray[HELP_WIDTH][MAX_LINES], og::io::OgFile& infile)
{
    short i;
    for (i = 0; i < MAX_LINES; i++) {
        std::string someline = read_one_line(infile, HELP_WIDTH);
        snprintf(somearray[i], HELP_WIDTH, "%s", someline.c_str());
        if (help_end_of_file()) return i;
    }
    return MAX_LINES;
}

// General help text lines (Controls tab)
static const char* controls_help_lines[] = {
	"*** GLADIATOR HELP ***",
	"",
	"MOVEMENT KEYS (Default)",
	"========================",
	"Player 1:  W A S D |  Player 2: Arrows",
	"4-direction is default for all players.",
	"Switch to 8-direction in Player Controls.",
	"",
	"Player 3: I J K L |  Player 4: F T G H",
	"Yell defaults: P1=E, P2=\\, P3=U, P4=Y",
	"Diagonal keys are configurable per player.",
	"",
	"ACTION KEYS",
	"========================",
	"         P1      P2      P3      P4",
	"Fire:    LCtrl   .       Space   5",
	"Special: LAlt    /       ;       6",
	"Switch:  ~/`     Enter   -       =",
	"Shifter: LShift  RShift  0       8",
	"Yell:    E       \\       U       Y",
	"Options: 1       2       3       4",
	"",
	"SPECIAL COMBOS",
	"========================",
	"Shifter + Yell: Summon NPCs to",
	"  come fight with or protect you",
	"",
	"Shifter + Switch: Switch to next",
	"  TYPE of team member (e.g. archer)",
	"",
	"OPTIONS MENU (In-Game)",
	"========================",
	"+/-: Game speed (slower/faster)",
	"[/]: Viewscreen size (bigger/smaller)",
	"</> : Screen brightness",
	"C: Toggle color cycling",
	"F: Toggle foes/allies display",
	"H: Cycle HP/SP display modes",
	"R: Toggle radar display",
	"S: Toggle score/exp display",
	"T: Show team status overview",
	"Key remapping moved to main menu settings.",
	"",
	"GENERAL CONTROLS",
	"========================",
	"ESC: Quit / Back",
	"Shift+/: Show scenario help",
	"",
	"Press ESC to return to menu",
};

static const int NUM_CONTROLS_LINES = sizeof(controls_help_lines) / sizeof(controls_help_lines[0]);

// Dynamic help content loaded from files
static std::vector<std::string> classes_help_lines;
static std::vector<std::string> editor_help_lines;
static bool help_files_loaded = false;

// Load a help file into a vector of strings
static bool load_help_file(const char* filename, std::vector<std::string>& lines)
{
	lines.clear();
	auto infile = og::io::og_open_read(filename);
	if (!infile)
	{
		Log("Could not open help file: {}", filename);
		lines.push_back("Error: Could not load help file.");
		return false;
	}

	char line_buf[HELP_WIDTH];
	char ch = '\0';
	int pos = 0;

	while (true)
	{
		if (!og::io::og_read_exact(*infile, &ch, 1, 1))
		{
			// End of file - save any remaining content
			if (pos > 0)
			{
				line_buf[pos] = '\0';
				lines.push_back(std::string(line_buf));
			}
			break;
		}

		if (ch == '\n' || ch == '\r')
		{
			line_buf[pos] = '\0';
			lines.push_back(std::string(line_buf));
			pos = 0;

			// Handle \r\n line endings
			if (ch == '\r')
			{
				char next = '\0';
				if (og::io::og_read_exact(*infile, &next, 1, 1) && next != '\n')
				{
					(void)infile->seek(-1, 1);
				}
			}
		}
		else if (pos < HELP_WIDTH - 1)
		{
			line_buf[pos++] = ch;
		}
	}
	return true;
}

// Load all help files (called once)
static void load_help_files()
{
	if (help_files_loaded)
		return;

	load_help_file("cfg/help_classes.txt", classes_help_lines);
	load_help_file("cfg/help_editor.txt", editor_help_lines);

	help_files_loaded = true;
}

// Tab definitions
enum class HelpTab {
	Controls = 0,
	Classes = 1,
	Editor = 2,
	NumTabs = 3
};

static const char* tab_names[] = { "Controls", "Classes", "Editor" };

// Helper struct to hold tab content reference
struct TabContent {
	const char** static_lines;           // For controls (static array)
	const std::vector<std::string>* dynamic_lines;  // For classes/editor (loaded from files)
	int num_lines;
	bool is_dynamic;
};

// Helper function to get content for current tab
static TabContent get_tab_content(HelpTab tab)
{
	TabContent content;
	content.static_lines = nullptr;
	content.dynamic_lines = nullptr;
	content.is_dynamic = false;

	switch (tab)
	{
	case HelpTab::Controls:
		content.static_lines = controls_help_lines;
		content.num_lines = NUM_CONTROLS_LINES;
		content.is_dynamic = false;
		break;
	case HelpTab::Classes:
		content.dynamic_lines = &classes_help_lines;
		content.num_lines = static_cast<int>(classes_help_lines.size());
		content.is_dynamic = true;
		break;
	case HelpTab::Editor:
		content.dynamic_lines = &editor_help_lines;
		content.num_lines = static_cast<int>(editor_help_lines.size());
		content.is_dynamic = true;
		break;
	default:
		content.static_lines = controls_help_lines;
		content.num_lines = NUM_CONTROLS_LINES;
		content.is_dynamic = false;
		break;
	}
	return content;
}

// Helper to get a line from TabContent
static const char* get_content_line(const TabContent& content, int index)
{
	if (index < 0 || index >= content.num_lines)
		return "";
	if (content.is_dynamic && content.dynamic_lines)
		return (*content.dynamic_lines)[index].c_str();
	else if (content.static_lines)
		return content.static_lines[index];
	return "";
}

// Tab button dimensions
#define TAB_Y 20
#define TAB_HEIGHT 12
#define TAB_WIDTH 58
#define TAB_SPACING 2
inline constexpr Sint32 TAB_START_X = HELPTEXT_LEFT + 10;
inline constexpr Sint32 CONTENT_TOP = TAB_Y + TAB_HEIGHT + 4;  // Content starts below tabs

Sint32 show_general_help()
{
	// Load help files from disk (only loads once)
	load_help_files();

	HelpTab current_tab = HelpTab::Controls;
	TabContent current_content = get_tab_content(current_tab);

	Sint32 screenlines = current_content.num_lines * 8;
	Sint32 j;
	Sint32 linesdown;
	Sint32 changed;
	Sint32 templines;
	Sint32 text_delay = 1;
	Sint32 key_presses = 0;

	text& mytext = og::runtime::current_session->myscreen_->text_normal;
	Sint32 start_time, now_time;
	Sint32 bottomrow = (screenlines - ((DISPLAY_LINES-1)*8));
	if (bottomrow < 0) bottomrow = 0;

	// Lambda to update content when tab changes
	auto switch_tab = [&](HelpTab new_tab) {
		current_tab = new_tab;
		current_content = get_tab_content(current_tab);
		screenlines = current_content.num_lines * 8;
		bottomrow = (screenlines - ((DISPLAY_LINES-1)*8));
		if (bottomrow < 0) bottomrow = 0;
		linesdown = 0;
		changed = 1;
	};

	clear_keyboard();
	linesdown = 0;
	changed = 1;
	start_time = query_timer();

	// Track previous mouse state for click detection
	bool prev_mouse_left = false;

	while (!og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(10);
		get_input_events(POLL);

		// Handle mouse clicks on tabs
		MouseState& mymouse = query_mouse_no_poll();
		bool mouse_clicked = mymouse.left && !prev_mouse_left;
		prev_mouse_left = mymouse.left;

			if (mouse_clicked)
			{
				const int mx = static_cast<int>(mymouse.x);
				const int my = static_cast<int>(mymouse.y);

			// Check if click is in tab area
			if (my >= TAB_Y && my <= TAB_Y + TAB_HEIGHT)
			{
				for (int t = 0; t < static_cast<int>(HelpTab::NumTabs); t++)
				{
					int tab_x = TAB_START_X + t * (TAB_WIDTH + TAB_SPACING);
					if (mx >= tab_x && mx <= tab_x + TAB_WIDTH)
					{
						if (static_cast<HelpTab>(t) != current_tab)
						{
							switch_tab(static_cast<HelpTab>(t));
						}
						break;
					}
				}
			}
		}

		// Handle keyboard tab switching (1, 2, 3 keys)
		static bool key1_was_pressed = false;
		static bool key2_was_pressed = false;
		static bool key3_was_pressed = false;

		if (og::runtime::current_session->keystates_[KEYSTATE_1] && !key1_was_pressed && current_tab != HelpTab::Controls)
		{
			switch_tab(HelpTab::Controls);
		}
		key1_was_pressed = og::runtime::current_session->keystates_[KEYSTATE_1];

		if (og::runtime::current_session->keystates_[KEYSTATE_2] && !key2_was_pressed && current_tab != HelpTab::Classes)
		{
			switch_tab(HelpTab::Classes);
		}
		key2_was_pressed = og::runtime::current_session->keystates_[KEYSTATE_2];

		if (og::runtime::current_session->keystates_[KEYSTATE_3] && !key3_was_pressed && current_tab != HelpTab::Editor)
		{
			switch_tab(HelpTab::Editor);
		}
		key3_was_pressed = og::runtime::current_session->keystates_[KEYSTATE_3];

		short scroll_delta = get_and_reset_scroll_amount();
		if (scroll_delta < 0)
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % text_delay;
			if (!key_presses && (linesdown < bottomrow))
			{
				while(linesdown < bottomrow && scroll_delta != 0)
				{
					linesdown++;
					scroll_delta++;
				}
				changed = 1;
			}
		}

		if (og::runtime::current_session->keystates_[KEYSTATE_PAGEDOWN])
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % (10*text_delay);
			if (!key_presses && (linesdown < bottomrow))
			{
				templines = linesdown + (DISPLAY_LINES * 7);
				if (templines > bottomrow)
					templines = bottomrow;
				if (linesdown != templines)
				{
					linesdown = templines;
					changed = 1;
				}
			}
		}

		if (scroll_delta > 0)
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % text_delay;
			if (!key_presses && linesdown)
			{
				while(linesdown && scroll_delta != 0)
				{
					linesdown--;
					scroll_delta--;
				}
				changed = 1;
			}
		}

		if (og::runtime::current_session->keystates_[KEYSTATE_PAGEUP])
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % (10*text_delay);
			if (!key_presses && linesdown)
			{
				linesdown -= (DISPLAY_LINES * 7);
				if (linesdown < 0)
					linesdown = 0;
				changed = 1;
			}
		}

		if (changed)
		{
			templines = linesdown/8;

			// Calculate content area dimensions
			int content_text_top = CONTENT_TOP + 8;  // Where text starts (after title bar)
			int content_bottom = content_text_top + (DISPLAY_LINES * 7) + 10;

			// Draw the main dialog background (covers everything)
			og::runtime::current_session->myscreen_->draw_button(HELPTEXT_LEFT-4, TAB_Y-4,
			                      HELPTEXT_LEFT+240, content_bottom + 8, 3, 1);

			// Draw tabs
			for (int t = 0; t < static_cast<int>(HelpTab::NumTabs); t++)
			{
				int tab_x = TAB_START_X + t * (TAB_WIDTH + TAB_SPACING);

				// Draw tab button
				og::runtime::current_session->myscreen_->draw_button(tab_x, TAB_Y, tab_x + TAB_WIDTH, TAB_Y + TAB_HEIGHT, 1, 1);

				// Highlight selected tab by drawing over bottom edge
				if (static_cast<HelpTab>(t) == current_tab)
				{
					og::runtime::current_session->myscreen_->draw_text_bar(tab_x + 1, TAB_Y + TAB_HEIGHT - 1,
					                        tab_x + TAB_WIDTH - 1, TAB_Y + TAB_HEIGHT);
				}

				// Draw tab label
				int label_len = static_cast<int>(strlen(tab_names[t]));
				int label_x = tab_x + (TAB_WIDTH - label_len * 6) / 2;
				mytext.write_xy(label_x, TAB_Y + 3, tab_names[t],
				                (static_cast<HelpTab>(t) == current_tab) ? static_cast<unsigned char>(DARK_BLUE) : static_cast<unsigned char>(BLACK), 1);
			}

			// Draw content area (below tabs)
			og::runtime::current_session->myscreen_->draw_button(HELPTEXT_LEFT-4, CONTENT_TOP,
			                      HELPTEXT_LEFT+240, content_bottom + 8, 3, 1);

			// Draw content text
			for (j=0; j < DISPLAY_LINES; j++)
			{
				int line_idx = j + templines;
				if (line_idx >= 0 && line_idx < current_content.num_lines)
				{
					int text_y = content_text_top + (j * 7) - (linesdown % 8);
					mytext.write_xy(HELPTEXT_LEFT+2, static_cast<short>(text_y),
					                get_content_line(current_content, line_idx), static_cast<unsigned char>(DARK_BLUE), 1);
				}
			}

			// Draw title bar (top of content area)
			og::runtime::current_session->myscreen_->draw_text_bar(HELPTEXT_LEFT, CONTENT_TOP,
			                        HELPTEXT_LEFT+240-4, CONTENT_TOP + 6);
			std::string title = std::format("GLADIATOR v{}", OPENGLAD_VERSION_STRING);
			mytext.write_xy(HELPTEXT_LEFT+60, CONTENT_TOP + 1, title.c_str(), static_cast<unsigned char>(RED), 1);

			// Draw footer bar (bottom of content area)
			og::runtime::current_session->myscreen_->draw_text_bar(HELPTEXT_LEFT, content_bottom,
			                        HELPTEXT_LEFT+240-4, content_bottom + 6);
			mytext.write_xy(HELPTEXT_LEFT+30, content_bottom + 1,
			                "1/2/3:Tab  ESC:Exit", static_cast<unsigned char>(RED), 1);

			og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
			changed = 0;
		}
	}

	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}

	return 1;
}

#ifdef TESTING
// GCOVR_EXCL_START -- test-only coverage harness in src/, not shipped code.
Sint32 help_testing_exercise_internal_paths()
{
	int checks = 0;
	bool failed = false;
	const auto check = [&checks, &failed](bool condition) {
		if (condition)
			++checks;
		else
			failed = true;
	};

	std::vector<std::string> lines;
	check(!load_help_file("__missing_help_file__.txt", lines) &&
	      !lines.empty());

	help_files_loaded = false;
	load_help_files();
	check(help_files_loaded);
	load_help_files();

	classes_help_lines = {"Class line 1", "Class line 2"};
	editor_help_lines = {"Editor line 1"};
	const TabContent controls = get_tab_content(HelpTab::Controls);
	const TabContent classes = get_tab_content(HelpTab::Classes);
	const TabContent editor = get_tab_content(HelpTab::Editor);
	const TabContent fallback =
		get_tab_content(static_cast<HelpTab>(99));
	check(!controls.is_dynamic && controls.num_lines == NUM_CONTROLS_LINES);
	check(classes.is_dynamic && classes.num_lines == 2);
	check(editor.is_dynamic && editor.num_lines == 1);
	check(!fallback.is_dynamic && fallback.num_lines == NUM_CONTROLS_LINES);
	check(std::strcmp(get_content_line(controls, 0),
	                  controls_help_lines[0]) == 0);
	check(std::strcmp(get_content_line(classes, 1), "Class line 2") == 0);
	check(std::strcmp(get_content_line(editor, 0), "Editor line 1") == 0);
	check(std::strcmp(get_content_line(editor, 3), "") == 0);
	check(std::strcmp(get_content_line(editor, -1), "") == 0);

	screen* const scr = og::runtime::current_session->myscreen_;
	const std::string saved_campaign = scr->save_data.current_campaign;
	scr->save_data.current_campaign = "__missing_campaign__";
	check(read_campaign_intro(scr) == 1);
	scr->save_data.current_campaign = saved_campaign;
	const auto saved_description = scr->level_description();
	scr->level_description().clear();
	check(read_scenario(scr) == 1);
	scr->level_description() = saved_description;

	help_files_loaded = false;
	classes_help_lines.clear();
	editor_help_lines.clear();
	return failed ? 0 : checks;
}
// GCOVR_EXCL_STOP
#endif
