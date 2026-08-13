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
#include <list>
#include <vector>
#include <string>
#include <format>
#ifdef TESTING
#include <atomic>
#endif
#include <openglad/core/text_wrap.h>
#include <openglad/core/util.h>
#include <openglad/core/version.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/web_back_key.h>
#include <openglad/interface/web_control_defaults.h>
#include <openglad/interface/base.h>
#include <openglad/interface/ui/scroll_view_layout.h>
#include <openglad/resources/og_file.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include "picker_sdl_defs.h"
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

// Character budget of a help frame: text starts at HELPTEXT_LEFT+2, the
// frame's right edge sits at HELPTEXT_LEFT+box_width, glyphs advance 6px.
// box_width 200 (scenario) -> 33 chars; 240 (intro/general help) -> 39.
constexpr int help_char_budget(int box_width) { return (box_width - 2) / 6; }

namespace
{
inline short& help_end_of_file()
{
    return og::runtime::current_session->help_end_of_file_;
}

// Edge-trigger with hold-repeat over a polled key state (issue #156). The
// old `(now - start_time) % N` phase gate was open only ~13.6ms out of
// every 136ms, so a short PageDown tap did nothing about half the time.
// This fires on the press edge, then repeats while held.
struct KeyRepeat
{
	bool was_down = false;
	Sint32 next_tick = 0;
};

// query_timer() ticks every ~13.6ms.
inline constexpr Sint32 kKeyFirstDelayTicks = 20;  // ~270ms before repeat
inline constexpr Sint32 kLineRepeatTicks = 4;      // ~55ms per line step
inline constexpr Sint32 kPageRepeatTicks = 12;     // ~165ms per page step

bool key_repeat_fired(KeyRepeat& k, bool down, Sint32 now,
                      Sint32 first_delay, Sint32 repeat_every)
{
	if (!down)
	{
		k.was_down = false;
		return false;
	}
	if (!k.was_down)
	{
		k.was_down = true;
		k.next_tick = now + first_delay;
		return true;
	}
	if (now >= k.next_tick)
	{
		k.next_tick = now + repeat_every;
		return true;
	}
	return false;
}

#ifdef TESTING
bool s_help_force_scroll_text = false;
std::atomic<bool> s_help_test_page_up{false};
std::atomic<bool> s_help_test_page_down{false};
std::atomic<bool> s_help_test_line_up{false};
std::atomic<bool> s_help_test_line_down{false};
std::atomic<bool> s_help_test_home{false};
std::atomic<bool> s_help_test_end{false};
std::atomic<Sint32> s_help_last_linesdown{0};
std::atomic<bool> s_help_scroll_chrome{false};
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
// Note: this code has been redone to work in 'scanlines,'
//       so that the text scrolls by pixels rather than lines.
template <typename GetLine>
static short scroll_text_view(screen* scr, int num_lines, int box_width,
    const char* title, Sint32 buf_x, Sint32 buf_y, Sint32 buf_w, Sint32 buf_h,
    GetLine get_line)
{
	ScopedUiCanvas canvas_target(*scr);
	// Flow the source lines to the frame's character budget before display
	// (issue #152). HardBreaks keeps authored line breaks and ASCII tables
	// intact; only over-long lines re-wrap.
	std::list<std::string> source_lines;
	for (int i = 0; i < num_lines; i++)
		source_lines.push_back(get_line(i));
	const std::vector<std::string> flowed =
		og::core::wrap_lines(source_lines, help_char_budget(box_width));
	num_lines = static_cast<int>(flowed.size());
	Sint32 screenlines = num_lines * 8;
	Sint32 linesdown = 0;
	Sint32 changed = 1;
	Sint32 templines;

	text& mytext = scr->text_normal;
	Sint32 bottomrow = screenlines - ((DISPLAY_LINES-1)*8);
	if (bottomrow < 0)
		bottomrow = 0;

	// Scroll chrome geometry (issue #156): the gutter with up/down arrows
	// and a thumb exists only when the text overflows, so short briefings
	// render byte-identically to the legacy dialog. up/down/track do not
	// depend on linesdown; only the thumb does (recomputed per draw).
	const og::ui::ScrollViewLayout layout = og::ui::compute_scroll_view_layout(
		num_lines, box_width, 0, bottomrow, buf_x, buf_y, buf_w, buf_h);
#ifdef TESTING
	s_help_scroll_chrome.store(layout.scrollable, std::memory_order_release);
	s_help_last_linesdown.store(0, std::memory_order_release);
#endif

	clear_keyboard();
	// Drain a click still held from the button that opened this dialog (and
	// any queued collapsed tap) so it cannot register as a scroll press or
	// an instant dismissal.
	reset_mouse_click_tracking();

	KeyRepeat page_down_key, page_up_key, line_down_key, line_up_key;
	KeyRepeat home_key, end_key;
	// Baseline from the current state so a button still held from the
	// surface that opened this dialog is not a fresh press edge.
	bool prev_left = query_mouse_no_poll().left;

	// Loop until dismissed (Escape/keypress/click sets input_continue; a
	// click or tap on the scroll controls is swallowed instead).
	for (;;)
	{
		YIELD_SLEEP(10);
		get_input_events(POLL);
		const Sint32 now_tick = query_timer();
		const Sint32 old_linesdown = linesdown;

		short scroll_delta = get_and_reset_scroll_amount();
		if (scroll_delta < 0)
		{
			while (linesdown < bottomrow && scroll_delta != 0)
			{
				linesdown++;
				scroll_delta++;
			}
		}
		if (scroll_delta > 0)
		{
			while (linesdown && scroll_delta != 0)
			{
				linesdown--;
				scroll_delta--;
			}
		}

		bool page_down_held =
			og::runtime::current_session->keystates_[KEYSTATE_PAGEDOWN];
		bool page_up_held =
			og::runtime::current_session->keystates_[KEYSTATE_PAGEUP];
		bool line_down_held =
			og::runtime::current_session->keystates_[KEYSTATE_DOWN];
		bool line_up_held =
			og::runtime::current_session->keystates_[KEYSTATE_UP];
		bool home_held =
			og::runtime::current_session->keystates_[KEYSTATE_HOME];
		bool end_held =
			og::runtime::current_session->keystates_[KEYSTATE_END];
#ifdef TESTING
		page_down_held = page_down_held ||
			s_help_test_page_down.load(std::memory_order_acquire);
		page_up_held = page_up_held ||
			s_help_test_page_up.load(std::memory_order_acquire);
		line_down_held = line_down_held ||
			s_help_test_line_down.load(std::memory_order_acquire);
		line_up_held = line_up_held ||
			s_help_test_line_up.load(std::memory_order_acquire);
		home_held = home_held ||
			s_help_test_home.load(std::memory_order_acquire);
		end_held = end_held ||
			s_help_test_end.load(std::memory_order_acquire);
#endif

		// scrolling one page down
		if (key_repeat_fired(page_down_key, page_down_held, now_tick,
		                     kKeyFirstDelayTicks, kPageRepeatTicks))
		{
			linesdown += (DISPLAY_LINES * 7);
			if (linesdown > bottomrow)
				linesdown = bottomrow;
		}

		// scrolling one page up
		if (key_repeat_fired(page_up_key, page_up_held, now_tick,
		                     kKeyFirstDelayTicks, kPageRepeatTicks))
		{
			linesdown -= (DISPLAY_LINES * 7);
			if (linesdown < 0)
				linesdown = 0;
		}

		// one text line at a time (issue #156: arrow keys)
		if (key_repeat_fired(line_down_key, line_down_held, now_tick,
		                     kKeyFirstDelayTicks, kLineRepeatTicks))
		{
			linesdown += 8;
			if (linesdown > bottomrow)
				linesdown = bottomrow;
		}
		if (key_repeat_fired(line_up_key, line_up_held, now_tick,
		                     kKeyFirstDelayTicks, kLineRepeatTicks))
		{
			linesdown -= 8;
			if (linesdown < 0)
				linesdown = 0;
		}
		if (key_repeat_fired(home_key, home_held, now_tick,
		                     kKeyFirstDelayTicks, kPageRepeatTicks))
			linesdown = 0;
		if (key_repeat_fired(end_key, end_held, now_tick,
		                     kKeyFirstDelayTicks, kPageRepeatTicks))
			linesdown = bottomrow;

		// Clicks/taps on the scroll controls scroll; everywhere else keeps
		// tap-to-dismiss. FingerDown writes mouse_state.x/y and FingerUp
		// leaves them, so a collapsed tap still hit-tests correctly. The
		// rects are padded 3px: touch coords can land a pixel or two off
		// the UI canvas at fractional zoom. query_mouse() (not _no_poll)
		// marks standing presses as observed, so a click consumed by the
		// edge detector below is not double-counted by the collapsed-tap
		// pending queue on release.
		MouseState& mouse = query_mouse();
		const int mx = static_cast<int>(mouse.x);
		const int my = static_cast<int>(mouse.y);
		const bool click_edge =
			(mouse.left && !prev_left) || take_pending_left_click();
		prev_left = mouse.left;
		bool on_scroll_control = false;
		if (layout.scrollable)
		{
			const bool on_up = layout.up.contains_padded(mx, my, 3);
			const bool on_down =
				!on_up && layout.down.contains_padded(mx, my, 3);
			const bool on_track = !on_up && !on_down &&
				layout.track.contains_padded(mx, my, 3);
			on_scroll_control = on_up || on_down || on_track;
			if (click_edge && on_scroll_control)
			{
				if (on_up)
				{
					linesdown -= 8;
					if (linesdown < 0)
						linesdown = 0;
				}
				else if (on_down)
				{
					linesdown += 8;
					if (linesdown > bottomrow)
						linesdown = bottomrow;
				}
				else
				{
					// Track: page toward the click, relative to the thumb.
					const og::ui::ScrollViewLayout current =
						og::ui::compute_scroll_view_layout(
							num_lines, box_width, linesdown, bottomrow,
							buf_x, buf_y, buf_w, buf_h);
					if (my < current.thumb.y)
					{
						linesdown -= (DISPLAY_LINES * 7);
						if (linesdown < 0)
							linesdown = 0;
					}
					else if (my >= current.thumb.y + current.thumb.h)
					{
						linesdown += (DISPLAY_LINES * 7);
						if (linesdown > bottomrow)
							linesdown = bottomrow;
					}
				}
			}
		}

		if (linesdown != old_linesdown)
			changed = 1;
#ifdef TESTING
		s_help_last_linesdown.store(linesdown, std::memory_order_release);
#endif

		// did we scroll, etc.?
		if (changed)
		{
			// which TEXT line are we at?
			templines = linesdown/8;
			const og::ui::ScrollViewLayout frame =
				og::ui::compute_scroll_view_layout(
					num_lines, box_width, linesdown, bottomrow, buf_x,
					buf_y, buf_w, buf_h);
			scr->draw_button(frame.frame_x1, frame.frame_y1,
			                 frame.frame_x2, frame.frame_y2, 3, 1);
			for (Sint32 j = 0; j < DISPLAY_LINES; j++)
			{
				const int line_idx = j + templines;
				if (line_idx >= 0 && line_idx < num_lines &&
				    !flowed[static_cast<std::size_t>(line_idx)].empty())
					mytext.write_xy(HELPTEXT_LEFT+2, static_cast<short>(text_down(j)-linesdown%8),
					                flowed[static_cast<std::size_t>(line_idx)].c_str(),
					                static_cast<unsigned char>(DARK_BLUE), 1);
			}

			if (frame.scrollable)
			{
				// Trough, thumb, then the arrow faces with their glyphs.
				scr->draw_text_bar(frame.track.x, frame.track.y,
				                   frame.track.x + frame.track.w - 1,
				                   frame.track.y + frame.track.h - 1);
				scr->draw_button(frame.thumb.x, frame.thumb.y,
				                 frame.thumb.x + frame.thumb.w - 1,
				                 frame.thumb.y + frame.thumb.h - 1, 1, 1);
				scr->draw_button(frame.up.x, frame.up.y,
				                 frame.up.x + frame.up.w - 1,
				                 frame.up.y + frame.up.h - 1, 1, 1);
				scr->draw_button(frame.down.x, frame.down.y,
				                 frame.down.x + frame.down.w - 1,
				                 frame.down.y + frame.down.h - 1, 1, 1);
				mytext.write_xy(frame.up.x + 4, frame.up.y + 4, "^",
				                static_cast<unsigned char>(RED), 1);
				mytext.write_xy(frame.down.x + 4, frame.down.y + 4, "v",
				                static_cast<unsigned char>(RED), 1);
			}

			// Draw a bounding box (top and bottom edges). The bars span the
			// whole frame — including the scrollbar gutter when present, so
			// the header/footer read as full-width chrome and the scrollbar
			// sits between them. chrome_w == box_width whenever there is no
			// gutter, keeping the short-briefing dialog byte-identical.
			const int chrome_w = frame.frame_x2 - HELPTEXT_LEFT;
			scr->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP-8,
			                   HELPTEXT_LEFT+chrome_w-4, HELPTEXT_TOP-2);
			scr->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP+97,
			                   HELPTEXT_LEFT+chrome_w-4, HELPTEXT_TOP+103);
			int title_x = HELPTEXT_LEFT + chrome_w/2 - static_cast<int>(strlen(title))*3;
			mytext.write_xy(title_x, HELPTEXT_TOP-7, title, static_cast<unsigned char>(RED), 1);
			int cont_len = static_cast<int>(strlen(CONTINUE_ACTION_STRING " TO CONTINUE"));
			int cont_x = HELPTEXT_LEFT + chrome_w/2 - cont_len*3;
			mytext.write_xy(cont_x, HELPTEXT_TOP+98,
			                CONTINUE_ACTION_STRING " TO CONTINUE", static_cast<unsigned char>(RED), 1);
			scr->buffer_to_screen(frame.blit_x, frame.blit_y,
			                      frame.blit_w, frame.blit_h);
			changed = 0;
		}

		if (query_input_continue())
		{
			if (on_scroll_control)
			{
				// That click/tap was a scroll action: swallow the shared
				// modal latch (mouse-down and touch FingerUp both set it)
				// instead of dismissing.
				input_continue_ref() = false;
			}
			else
			{
				break;
			}
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
// This function reads one text line from file infile,
// stopping at length (length), or when encountering an
// end-of-line character ..
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

// This function fills the array with the help file text ..
// It returns the # of lines successfully filled ..
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
	og::input::kWebControlDefaults ? "Fire:    Z       .       Space   5"
	                               : "Fire:    LCtrl   .       Space   5",
	og::input::kWebControlDefaults ? "Special: X       /       ;       6"
	                               : "Special: LAlt    /       ;       6",
	"Switch:  ~/`     Enter   -       =",
	"Shifter: LShift  RShift  0       8",
	"Yell:    E       \\       U       Y",
	"",
	"SPECIAL COMBOS",
	"========================",
	"Shifter + Yell: Summon NPCs to",
	"  come fight with or protect you",
	"",
	"Shifter + Switch: Switch to next",
	"  TYPE of team member (e.g. archer)",
	"",
	"PAUSE MENU (In-Game)",
	"========================",
	"VIEW TEAM: team status overview",
	"BRIEFING: re-read the mission text",
	"Each player's row holds their controls,",
	"  view zoom and HUD display toggles.",
	// 37 chars: stays inside the 39-char flow budget so "cycling" cannot
	// orphan onto its own wrapped line.
	"Game speed, brightness, color cycling",
	"  live in the main menu settings.",
	"",
	"GENERAL CONTROLS",
	"========================",
	og::input::kWebBackKeyMode ? "BACKSPACE: Quit / Back"
	                           : "ESC: Quit / Back",
	og::input::kWebBackKeyMode ? "BACKSPACE in game: Pause menu"
	                           : "ESC in game: Pause menu",
	"",
	og::input::kWebBackKeyMode ? "Press BACKSPACE to return to menu"
	                           : "Press ESC to return to menu",
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

	std::string line_buf;
	char ch = '\0';

	while (true)
	{
		if (!og::io::og_read_exact(*infile, &ch, 1, 1))
		{
			// End of file - save any remaining content
			if (!line_buf.empty())
				lines.push_back(std::move(line_buf));
			break;
		}

		if (ch == '\n' || ch == '\r')
		{
			lines.push_back(std::move(line_buf));
			line_buf.clear();

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
		else if (line_buf.size() < static_cast<std::size_t>(HELP_WIDTH - 1))
		{
			line_buf.push_back(ch);
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
		return (*content.dynamic_lines)[static_cast<std::size_t>(index)].c_str();
	else if (content.static_lines)
		return content.static_lines[index];
	return "";
}

// ---------------------------------------------------------------------------
// Full-screen HELP (#168): engine-hosted. The spec (tab strip, BACK, and the
// PREV/NEXT pagers on the shared layout constants in picker_sdl_defs.h)
// lives in menu_screen_specs.cpp; the hooks below own the content — the
// active tab's flowed lines and its PageModel — through a file-static
// pointer (the VIEW LEVEL idiom: the rewire and state-override signatures
// carry no screen_state). Null state = no open screen (the bare engine
// sweeps) and presents the single-page shape: pagers hidden, BACK's
// right-link closed.

// Flow budget of the content frame: glyphs start at kHelpTextX and the
// frame's right edge sits at kHelpFrameRight, so the box width feeding
// help_char_budget is their distance (304px -> 50 chars; 10 + 50*6 = 310
// keeps the widest line inside the frame bevel).
inline constexpr int kHelpTextBoxWidth = kHelpFrameRight - kHelpTextX;

// Footer ink between BACK and the pagers: the key-hint row, the version
// row, and the right-aligned "p/N" page indicator.
inline constexpr int kHelpFooterBarLeft = kHelpBackX + kHelpBackWidth + 2;
inline constexpr int kHelpFooterBarRight = kHelpPrevX - 3;
inline constexpr int kHelpFooterTextX = kHelpFooterBarLeft + 2;
inline constexpr int kHelpFooterHintY = kHelpFooterY + 2;
inline constexpr int kHelpFooterVersionY = kHelpFooterY + 11;

namespace
{
struct HelpScreenState
{
	og::ui::PageModel pager;
	std::vector<std::string> lines;  // active tab, flowed to the frame budget
	HelpTab tab = HelpTab::Controls;
};

HelpScreenState* g_help_screen_state = nullptr;

#ifdef TESTING
std::atomic<int> s_help_active_tab{0};
std::atomic<int> s_help_page{0};

void help_publish_probes(const HelpScreenState& state)
{
	s_help_active_tab.store(static_cast<int>(state.tab),
	                        std::memory_order_release);
	s_help_page.store(state.pager.page, std::memory_order_release);
}
#endif

std::vector<std::string> help_flow_tab_lines(HelpTab tab)
{
	const TabContent content = get_tab_content(tab);
	std::list<std::string> source;
	for (int i = 0; i < content.num_lines; i++)
		source.push_back(get_content_line(content, i));
	// HardBreaks keeps authored line breaks and the ASCII heading rules
	// intact; only over-budget lines re-wrap (issue #152's fix, retargeted
	// from the 39-char dialog to the full-width frame).
	return og::core::wrap_lines(source, help_char_budget(kHelpTextBoxWidth));
}

// Reflow and reset the pager. Re-selecting the active tab keeps the page
// (the legacy switch-only-when-different behavior); `force` seeds entry.
void help_switch_tab(HelpScreenState& state, HelpTab tab, bool force = false)
{
	if (!force && state.tab == tab)
		return;
	state.tab = tab;
	state.lines = help_flow_tab_lines(tab);
	state.pager = og::ui::PageModel::make(static_cast<int>(state.lines.size()),
	                                      kHelpLinesPerPage);
#ifdef TESTING
	help_publish_probes(state);
#endif
}
} // namespace

// PREV/NEXT visibility: hidden while the active tab fits one page (and for
// the bare-sweep null state).
og::ui::RowState help_engine_pager_row_state(
    const og::ui::MenuLabelContext& /*context*/)
{
	const HelpScreenState* const state = g_help_screen_state;
	return (state != nullptr && state->pager.multi_page())
	    ? og::ui::RowState::Visible
	    : og::ui::RowState::Hidden;
}

// Nav closure over the hidden pagers, re-asserted per frame over the static
// base link (the VIEW LEVEL shape).
void help_engine_rewire(button* buttons, int num_buttons,
                        int& /*highlighted_button*/)
{
	if (num_buttons <= kHelpMenuBackIndex)
		return;
	const HelpScreenState* const state = g_help_screen_state;
	const bool multi_page = state != nullptr && state->pager.multi_page();
	buttons[kHelpMenuBackIndex].nav.right =
	    multi_page ? kHelpMenuPrevIndex : -1;
}

// G3 generic row dispatch: the tab rows and pagers all carry
// ButtonAction::MenuSpecRow (arg == spec ordinal), so mouse clicks, keyboard
// FIRE, and the 1/2/3 + PageUp/PageDown hotkeys land here.
Sint32 help_engine_on_spec_row(int row, void* /*screen_state*/)
{
	HelpScreenState* const state = g_help_screen_state;
	if (state == nullptr)
		return 0;
	switch (row)
	{
	case kHelpMenuControlsTabIndex:
		help_switch_tab(*state, HelpTab::Controls);
		break;
	case kHelpMenuClassesTabIndex:
		help_switch_tab(*state, HelpTab::Classes);
		break;
	case kHelpMenuEditorTabIndex:
		help_switch_tab(*state, HelpTab::Editor);
		break;
	case kHelpMenuPrevIndex:
		(void)state->pager.step(-1);
		break;
	case kHelpMenuNextIndex:
		(void)state->pager.step(1);
		break;
	default:
		break;
	}
#ifdef TESTING
	help_publish_probes(*state);
#endif
	return 0;
}

// Mouse wheel pages the active tab: the engine loop polls every ~10ms, so
// one notch's accumulated delta maps to one page step.
bool help_engine_frame_tick(void* /*screen_state*/, int /*frame*/)
{
	HelpScreenState* const state = g_help_screen_state;
	if (state == nullptr)
		return true;
	const short scroll_delta = get_and_reset_scroll_amount();
	if (scroll_delta < 0)
		(void)state->pager.step(1);
	else if (scroll_delta > 0)
		(void)state->pager.step(-1);
#ifdef TESTING
	help_publish_probes(*state);
#endif
	return true;
}

// Content pass (after draw_buttons; every rect here stays outside the
// button bands): active-tab underline, the framed page of text, and the
// footer bar with the key hint, the version string — the ONLY build-version
// surface on web — and the page indicator.
void help_engine_draw_content(void* /*screen_state*/)
{
	const HelpScreenState* const state = g_help_screen_state;
	if (state == nullptr)
		return;
	screen* const scr = og::runtime::current_session->myscreen_;
	text& mytext = scr->text_normal;

	// Active-tab underline in the 2px gap between the strip and the frame.
	const int tab_x = kHelpTabX(static_cast<int>(state->tab));
	scr->draw_text_bar(tab_x + 1, kHelpTabY + kHelpTabHeight + 1,
	                   tab_x + kHelpTabWidth - 1, kHelpFrameTop - 1);

	// Content frame and the current page of flowed text.
	scr->draw_button(kHelpFrameLeft, kHelpFrameTop, kHelpFrameRight,
	                 kHelpFrameBottom, 3, 1);
	const int first_line = state->pager.first_index();
	for (int line_index = first_line; line_index < state->pager.end_index();
	     ++line_index)
	{
		mytext.write_xy(kHelpTextX,
		                static_cast<short>(kHelpTextTop +
		                    (line_index - first_line) * kHelpLinePitch),
		                state->lines[static_cast<std::size_t>(line_index)].c_str(),
		                static_cast<unsigned char>(DARK_BLUE), 1);
	}

	// Footer bar between BACK and the pagers.
	scr->draw_text_bar(kHelpFooterBarLeft, kHelpFooterY, kHelpFooterBarRight,
	                   kHelpFooterY + kHelpFooterHeight - 1);
	mytext.write_xy(kHelpFooterTextX, kHelpFooterHintY,
	                og::input::kWebBackKeyMode ? "1/2/3:Tab  BKSP:Exit"
	                                           : "1/2/3:Tab  ESC:Exit",
	                static_cast<unsigned char>(RED), 1);
	const std::string version =
	    std::format("GLADIATOR v{}", OPENGLAD_VERSION_STRING);
	mytext.write_xy(kHelpFooterTextX, kHelpFooterVersionY, version.c_str(),
	                static_cast<unsigned char>(RED), 1);
	if (state->pager.multi_page())
	{
		const std::string indicator = state->pager.indicator();
		mytext.write_xy(
		    kHelpFooterBarRight - 1 - static_cast<int>(indicator.size()) * 6,
		    kHelpFooterVersionY, indicator.c_str(),
		    static_cast<unsigned char>(RED), 1);
	}
}

Sint32 show_general_help()
{
	// Load help files from disk (only loads once)
	load_help_files();

	HelpScreenState state;
	help_switch_tab(state, HelpTab::Controls, true);

	og::runtime::current_session->myscreen_->clearbuffer();
	// A wheel notch queued before entry must not page the first frame.
	(void)get_and_reset_scroll_amount();

	g_help_screen_state = &state;
	(void)og::ui::run_menu_screen(og::ui::help_menu_screen_spec(), &state);
	g_help_screen_state = nullptr;

	og::runtime::current_session->myscreen_->clearbuffer();

	// Drain a still-held Escape: the re-entered main menu's QUIT row carries
	// KEYSTATE_ESCAPE and would consume the same press.
	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}

	return 1;
}

#ifdef TESTING
#include "../../../tests/coverage_internal/help_internal.inc"
#endif
