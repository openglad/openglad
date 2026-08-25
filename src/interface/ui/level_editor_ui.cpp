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

#include <openglad/interface/screen.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <list>
#include <string>
#include <utility>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

static screen* active_screen()
{
    return og::runtime::current_session->myscreen_;
}

struct UiRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

#ifdef TESTING
std::vector<std::string>& level_editor_testing_prompt_queue_ref();
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);

namespace {

std::atomic<std::uint64_t> s_prompt_block_entered_count{0};
std::atomic<std::uint64_t> s_prompt_block_input_observed_count{0};
std::atomic<std::uint64_t> s_prompt_block_input_completed_count{0};
std::atomic<int> s_prompt_block_held_key{-1};
std::atomic<int> s_prompt_block_click_x{0};
std::atomic<int> s_prompt_block_click_y{0};
std::atomic<bool> s_prompt_block_click_pending{false};

void prompt_block_testing_input_observed()
{
    s_prompt_block_input_observed_count.fetch_add(1, std::memory_order_release);
}

void prompt_block_testing_input_completed()
{
    s_prompt_block_input_completed_count.fetch_add(1, std::memory_order_release);
}

} // namespace

void level_editor_testing_prompt_block_input_reset()
{
    s_prompt_block_entered_count.store(0, std::memory_order_release);
    s_prompt_block_input_observed_count.store(0, std::memory_order_release);
    s_prompt_block_input_completed_count.store(0, std::memory_order_release);
    s_prompt_block_held_key.store(-1, std::memory_order_release);
    s_prompt_block_click_pending.store(false, std::memory_order_release);
}

void level_editor_testing_prompt_block_set_held_key(int key_state)
{
    s_prompt_block_held_key.store(key_state, std::memory_order_release);
}

void level_editor_testing_prompt_block_click(int x, int y)
{
    s_prompt_block_click_x.store(x, std::memory_order_relaxed);
    s_prompt_block_click_y.store(y, std::memory_order_relaxed);
    s_prompt_block_click_pending.store(true, std::memory_order_release);
}

std::uint64_t level_editor_testing_prompt_block_entered_count()
{
    return s_prompt_block_entered_count.load(std::memory_order_acquire);
}

std::uint64_t level_editor_testing_prompt_block_input_observed_count()
{
    return s_prompt_block_input_observed_count.load(std::memory_order_acquire);
}

std::uint64_t level_editor_testing_prompt_block_input_completed_count()
{
    return s_prompt_block_input_completed_count.load(std::memory_order_acquire);
}
#endif

bool prompt_for_string_block(const std::string& message, std::list<std::string>& result)
{
#ifdef TESTING
    auto& q = level_editor_testing_prompt_queue_ref();
    if (!q.empty()) {
        (void)message;
        result.clear();
        result.push_back(q.front());
        q.erase(q.begin());
        return true;
    }
#endif
    screen* screen_ctx = active_screen();
    ScopedUiCanvas ui_canvas(*screen_ctx);
    screen_ctx->darken_screen();
    
    int max_chars = 40;
    int max_lines = 8;
    
    int w = max_chars*6;
    int h = max_lines*10;
    int x = 160 - w/2;
    int y = 100 - h/2;
    
    text& mytext = screen_ctx->text_normal;
    
    // Background
    screen_ctx->draw_button(x - 5, y - 20, x + w + 10, y + h + 10, 1);
    
    unsigned char forecolor = DARK_BLUE;
    
    UiRect done_button = {320 - 52, 0, 50, 14};
    UiRect cancel_button = {320 - 104, 0, 50, 14};
    
    #if defined(USE_TOUCH_INPUT) || defined(USE_CONTROLLER_INPUT)
    UiRect newline_button = {320 - 75, 16, 50, 14};
    UiRect up_button = {14, 0, 14, 14};
    UiRect down_button = {14, 14, 14, 14};
    UiRect left_button = {0, 14, 14, 14};
    UiRect right_button = {28, 14, 14, 14};
    #endif
    
    std::list<std::string> original_text = result;

	clear_keyboard();
	clear_key_press_event();
	clear_text_input_event();
	MouseState& mymouse = query_mouse_no_poll();
	
    const std::string initial_text = result.empty() ? std::string() : result.front();
    og::input_native::start_text_input(
        initial_text.c_str(), max_chars * max_lines, message.c_str(), true);
    
    if(result.size() == 0)
        result.push_back("");
    
    std::list<std::string>::iterator s = result.begin();
    size_t cursor_pos = 0;
    size_t current_line = 0;
    
    bool cancel = false;
    bool done = false;
    const auto prompt_key_down = [](int key_state) {
        if (og::runtime::current_session->keystates_[key_state])
            return true;
#ifdef TESTING
        return s_prompt_block_held_key.load(std::memory_order_acquire) ==
            key_state;
#else
        return false;
#endif
    };
#ifdef TESTING
    s_prompt_block_entered_count.fetch_add(1, std::memory_order_release);
#endif
	while (!done)
	{
        get_input_events(POLL);
        // Pointer handoff: this prompt pumps its own events and reads the
        // pointer level directly — acknowledge its presses so its clicks
        // cannot mint collapsed-tap pending clicks for a later surface.
        acknowledge_mouse_presses();
#ifdef TESTING
        if (s_prompt_block_click_pending.exchange(
                false, std::memory_order_acquire))
        {
            mymouse.x = static_cast<float>(
                s_prompt_block_click_x.load(std::memory_order_relaxed));
            mymouse.y = static_cast<float>(
                s_prompt_block_click_y.load(std::memory_order_relaxed));
            mymouse.left = true;
        }
#endif
        
	        if(query_key_press_event())
	        {
#ifdef TESTING
                prompt_block_testing_input_observed();
#endif
	            int c = query_key();
	            clear_key_press_event();
            
            if (c == KEYCODE_RETURN)
            {
                #if defined(USE_TOUCH_INPUT) || defined(USE_CONTROLLER_INPUT)
                // FIXME: SDL does not have keyboard customization, so we can't make newlines with RETURN.
                // I need to either modify SDL or add click/touch text navigation.
                done = true;  // Some soft keyboards might disappear anyhow if you press return...
                break;
                #else
                std::string rest_of_line = s->substr(cursor_pos);
                s->erase(cursor_pos);
                s++;
                s = result.insert(s, rest_of_line);
                current_line++;
                cursor_pos = 0;
                #endif
            }
            else if (c == KEYCODE_BACKSPACE)
            {
                // At the beginning of the line?
                if(cursor_pos == 0)
                {
                    // Deleting a line break
                    // Not at the first line?
                    if(result.size() > 1 && current_line > 0)
                    {
                        // Then move up into the previous line, copying the old line
                        current_line--;
                        std::string old_line = *s;
                        s = result.erase(s);
                        s--;
                        cursor_pos = s->size();
                        // Append the old line
                        *s += old_line;
                    }
                }
                else
                {
                    // Delete previous character
                    cursor_pos--;
                    s->erase(cursor_pos, 1);
                }
            }
#ifdef TESTING
            prompt_block_testing_input_completed();
#endif
        }
        else if(mymouse.left)
        {
#ifdef TESTING
            prompt_block_testing_input_observed();
#endif
            mymouse.left = false;
            
            if(mymouse.in(done_button))
            {
                done = true;
            }
            else if(mymouse.in(cancel_button))
            {
                result = original_text;
                done = true;
            }
            #if defined(USE_TOUCH_INPUT) || defined(USE_CONTROLLER_INPUT)
            else if(mymouse.in(newline_button))
            {
                std::string rest_of_line = s->substr(cursor_pos);
                s->erase(cursor_pos);
                s++;
                s = result.insert(s, rest_of_line);
                current_line++;
                cursor_pos = 0;
            }
            else if(mymouse.in(up_button))
            {
                if(current_line > 0)
                {
                    current_line--;
                    s--;
                    if(s->size() < cursor_pos)
                        cursor_pos = s->size();
                }
            }
            else if(mymouse.in(down_button))
            {
                if(current_line+1 < result.size())
                {
                    current_line++;
                    s++;
                }
                else  // At the bottom already
                    cursor_pos = s->size();
                
                if(s->size() < cursor_pos)
                    cursor_pos = s->size();
            }
            else if(mymouse.in(left_button))
            {
                if(cursor_pos > 0)
                    cursor_pos--;
                else if(current_line > 0)
                {
                    current_line--;
                    s--;
                    cursor_pos = s->size();
                }
            }
            else if(mymouse.in(right_button))
            {
                cursor_pos++;
                if(cursor_pos > s->size())
                {
                    if(current_line+1 < result.size())
                    {
                        // Go to next line
                        current_line++;
                        s++;
                        cursor_pos = 0;
                    }
                    else  // No next line
                        cursor_pos = s->size();
                }
            }
            #endif
#ifdef TESTING
            prompt_block_testing_input_completed();
#endif
        }
        
        if(prompt_key_down(KEYSTATE_ESCAPE))
        {
#ifdef TESTING
            prompt_block_testing_input_observed();
#endif
            while(prompt_key_down(KEYSTATE_ESCAPE))
            {
                og::input_native::sleep_ms(1);
                get_input_events(POLL);
            }
#ifdef TESTING
            prompt_block_testing_input_completed();
#endif

            done = true;
            break;
        }
        if(prompt_key_down(KEYSTATE_DELETE))
        {
#ifdef TESTING
            prompt_block_testing_input_observed();
#endif
            if(cursor_pos < s->size())
                s->erase(cursor_pos, 1);
            
            while(prompt_key_down(KEYSTATE_DELETE))
            {
                og::input_native::sleep_ms(1);
                get_input_events(POLL);
            }
#ifdef TESTING
            prompt_block_testing_input_completed();
#endif
        }
        if(prompt_key_down(KEYSTATE_UP))
        {
#ifdef TESTING
            prompt_block_testing_input_observed();
#endif
            if(current_line > 0)
            {
                current_line--;
                s--;
                if(s->size() < cursor_pos)
                    cursor_pos = s->size();
            }
            
            while(prompt_key_down(KEYSTATE_UP))
            {
                og::input_native::sleep_ms(1);
                get_input_events(POLL);
            }
#ifdef TESTING
            prompt_block_testing_input_completed();
#endif
        }
        if(prompt_key_down(KEYSTATE_DOWN))
        {
#ifdef TESTING
            prompt_block_testing_input_observed();
#endif
            if(current_line+1 < result.size())
            {
                current_line++;
                s++;
            }
            else  // At the bottom already
                cursor_pos = s->size();
            
            if(s->size() < cursor_pos)
                cursor_pos = s->size();
            
            while(prompt_key_down(KEYSTATE_DOWN))
            {
                og::input_native::sleep_ms(1);
                get_input_events(POLL);
            }
#ifdef TESTING
            prompt_block_testing_input_completed();
#endif
        }
        if(prompt_key_down(KEYSTATE_LEFT))
        {
#ifdef TESTING
            prompt_block_testing_input_observed();
#endif
            if(cursor_pos > 0)
                cursor_pos--;
            else if(current_line > 0)
            {
                current_line--;
                s--;
                cursor_pos = s->size();
            }
            
            while(prompt_key_down(KEYSTATE_LEFT))
            {
                og::input_native::sleep_ms(1);
                get_input_events(POLL);
            }
#ifdef TESTING
            prompt_block_testing_input_completed();
#endif
        }
        if(prompt_key_down(KEYSTATE_RIGHT))
        {
#ifdef TESTING
            prompt_block_testing_input_observed();
#endif
            cursor_pos++;
            if(cursor_pos > s->size())
            {
                if(current_line+1 < result.size())
                {
                    // Go to next line
                    current_line++;
                    s++;
                    cursor_pos = 0;
                }
                else  // No next line
                    cursor_pos = s->size();
            }
            
            while(prompt_key_down(KEYSTATE_RIGHT))
            {
                og::input_native::sleep_ms(1);
                get_input_events(POLL);
            }
#ifdef TESTING
            prompt_block_testing_input_completed();
#endif
        }

        if(query_text_input_event())
        {
            const char* temptext = query_text_input();
            
            if(temptext != nullptr)
            {
                s->insert(cursor_pos, temptext);
                cursor_pos += strlen(temptext);
#ifdef TESTING
                prompt_block_testing_input_observed();
                prompt_block_testing_input_completed();
#endif
            }
        }
		
        clear_text_input_event();
        screen_ctx->draw_button(x - 5, y - 20, x + w + 10, y + h + 10, 1);
        mytext.write_xy(x, y - 13, message.c_str(), BLACK, 1);
        screen_ctx->hor_line(x, y - 5, w, BLACK);
        
        screen_ctx->draw_button(done_button.x, done_button.y, done_button.x + done_button.w, done_button.y + done_button.h, 1);
        mytext.write_xy(done_button.x + done_button.w/2 - 12, done_button.y + done_button.h/2 - 3, "DONE", DARK_BLUE, 1);
        screen_ctx->draw_button(cancel_button.x, cancel_button.y, cancel_button.x + cancel_button.w, cancel_button.y + cancel_button.h, 1);
        mytext.write_xy(cancel_button.x + cancel_button.w/2 - 18, cancel_button.y + cancel_button.h/2 - 3, "CANCEL", DARK_BLUE, 1);
        
        #if defined(USE_TOUCH_INPUT) || defined(USE_CONTROLLER_INPUT)
        screen_ctx->draw_button(newline_button.x, newline_button.y, newline_button.x + newline_button.w, newline_button.y + newline_button.h, 1);
        mytext.write_xy(newline_button.x + newline_button.w/2 - 18, newline_button.y + newline_button.h/2 - 3, "NEWLINE", DARK_BLUE, 1);
        screen_ctx->draw_button(up_button.x, up_button.y, up_button.x + up_button.w, up_button.y + up_button.h, 1);
        mytext.write_xy(up_button.x + up_button.w/2 - 6, up_button.y + up_button.h/2 - 3, "UP", DARK_BLUE, 1);
        screen_ctx->draw_button(left_button.x, left_button.y, left_button.x + left_button.w, left_button.y + left_button.h, 1);
        mytext.write_xy(left_button.x + left_button.w/2 - 6, left_button.y + left_button.h/2 - 3, "LT", DARK_BLUE, 1);
        screen_ctx->draw_button(down_button.x, down_button.y, down_button.x + down_button.w, down_button.y + down_button.h, 1);
        mytext.write_xy(down_button.x + down_button.w/2 - 6, down_button.y + down_button.h/2 - 3, "DN", DARK_BLUE, 1);
        screen_ctx->draw_button(right_button.x, right_button.y, right_button.x + right_button.w, right_button.y + right_button.h, 1);
        mytext.write_xy(right_button.x + right_button.w/2 - 6, right_button.y + right_button.h/2 - 3, "RT", DARK_BLUE, 1);
        #endif
        
	        int offset = 0;
	        if(current_line > 3)
	            offset = static_cast<int>((current_line - 3) * 10);
	        int j = 0;
	        for(auto& line : result)
	        {
	            int ypos = y + j*10 - offset;
            if(y <= ypos && ypos <= y + h)
                mytext.write_xy(x, ypos, line.c_str(), forecolor, 1);
            j++;
	        }
	        const Sint32 cursor_x = static_cast<Sint32>(x) + static_cast<Sint32>(cursor_pos * 6);
	        const Sint32 cursor_y = static_cast<Sint32>(y) + static_cast<Sint32>(current_line * 10) - 2 - static_cast<Sint32>(offset);
	        screen_ctx->ver_line(cursor_x, cursor_y, 10, RED);
			screen_ctx->buffer_to_screen(0, 0, 320, 200);
	        
	        og::input_native::sleep_ms(10);
	}

    og::input_native::stop_text_input();
    
	clear_keyboard();
    
    return !cancel;
}

bool prompt_for_string(const std::string& message, std::string& result)
{
#ifdef TESTING
    // Tests can optionally queue deterministic inputs for prompt_for_string().
    // If the queue is empty, accept the existing value without blocking.
    (void)message;
    auto& q = level_editor_testing_prompt_queue_ref();
    if (!q.empty()) {
        result = q.front();
        q.erase(q.begin());
    }
    return true;
#endif
	    screen* screen_ctx = active_screen();
	    ScopedUiCanvas ui_canvas(*screen_ctx);
	    screen_ctx->darken_screen();
	    
	    const short max_chars = 29;
	    
	    int x = 58;
	    int y = 60;
	    int w = static_cast<int>(max_chars) * 6;
	    int h = 10;
    
    screen_ctx->draw_button(x - 5, y - 20, x + w + 10, y + h + 10, 1);
    
    auto str_opt = screen_ctx->text_normal.input_string_ex_value(x, y, max_chars, message.c_str(), result.c_str());

    if(!str_opt)
        return false;

    result = std::move(*str_opt);
    return true;
}

#ifdef TESTING
// Test helper API (used by deterministic coverage-driving tests).
void level_editor_testing_prompt_queue_clear()
{
    level_editor_testing_prompt_queue_ref().clear();
}

void level_editor_testing_prompt_queue_push(const char* s)
{
    level_editor_testing_prompt_queue_ref().push_back(s ? std::string(s) : std::string());
}

// Provide access to the prompt queue without exposing it globally in non-test builds.
std::vector<std::string>& level_editor_testing_prompt_queue_ref()
{
    static std::vector<std::string> s_prompt_queue;
    return s_prompt_queue;
}
#endif
