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

#include <openglad/runtime/screen.h>
#include <openglad/input/input.h>
#include <openglad/runtime/game_context.h>
#include <cstring>
#include <list>
#include <string>
#include <vector>

extern screen* myscreen;

static screen* active_screen()
{
    if(ctx().game_screen != nullptr)
        return ctx().game_screen;
    return myscreen;
}

#ifdef TESTING
std::vector<std::string>& level_editor_testing_prompt_queue_ref();
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);
#endif

bool prompt_for_string_block(const std::string& message, std::list<std::string>& result)
{
    screen* screen_ctx = active_screen();
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
    
    SDL_Rect done_button = {320 - 52, 0, 50, 14};
    SDL_Rect cancel_button = {320 - 104, 0, 50, 14};
    
    #if defined(USE_TOUCH_INPUT) || defined(USE_CONTROLLER_INPUT)
    SDL_Rect newline_button = {320 - 75, 16, 50, 14};
    SDL_Rect up_button = {14, 0, 14, 14};
    SDL_Rect down_button = {14, 14, 14, 14};
    SDL_Rect left_button = {0, 14, 14, 14};
    SDL_Rect right_button = {28, 14, 14, 14};
    #endif
    
    std::list<std::string> original_text = result;

	clear_keyboard();
	clear_key_press_event();
	clear_text_input_event();
	MouseState& mymouse = query_mouse_no_poll();
	
    SDL_StartTextInput();
    
    if(result.size() == 0)
        result.push_back("");
    
    std::list<std::string>::iterator s = result.begin();
    size_t cursor_pos = 0;
    size_t current_line = 0;
    
    bool cancel = false;
    bool done = false;
	while (!done)
	{
        get_input_events(POLL);
        
	        if(query_key_press_event())
	        {
	            int c = query_key();
	            clear_key_press_event();
            
            if (c == SDLK_RETURN)
            {
                #if defined(USE_TOUCH_INPUT) || defined(USE_CONTROLLER_INPUT)
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
            else if (c == SDLK_BACKSPACE)
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
        }
        else if(mymouse.left)
        {
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
        }
        
        if(keystates[KEYSTATE_ESCAPE])
        {
            while(keystates[KEYSTATE_ESCAPE])
            {
                SDL_Delay(1);
                get_input_events(POLL);
            }

            done = true;
            break;
        }
        if(keystates[KEYSTATE_DELETE])
        {
            if(cursor_pos < s->size())
                s->erase(cursor_pos, 1);
            
            while(keystates[KEYSTATE_DELETE])
            {
                SDL_Delay(1);
                get_input_events(POLL);
            }
        }
        if(keystates[KEYSTATE_UP])
        {
            if(current_line > 0)
            {
                current_line--;
                s--;
                if(s->size() < cursor_pos)
                    cursor_pos = s->size();
            }
            
            while(keystates[KEYSTATE_UP])
            {
                SDL_Delay(1);
                get_input_events(POLL);
            }
        }
        if(keystates[KEYSTATE_DOWN])
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
            
            while(keystates[KEYSTATE_DOWN])
            {
                SDL_Delay(1);
                get_input_events(POLL);
            }
        }
        if(keystates[KEYSTATE_LEFT])
        {
            if(cursor_pos > 0)
                cursor_pos--;
            else if(current_line > 0)
            {
                current_line--;
                s--;
                cursor_pos = s->size();
            }
            
            while(keystates[KEYSTATE_LEFT])
            {
                SDL_Delay(1);
                get_input_events(POLL);
            }
        }
        if(keystates[KEYSTATE_RIGHT])
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
            
            while(keystates[KEYSTATE_RIGHT])
            {
                SDL_Delay(1);
                get_input_events(POLL);
            }
        }

        if(query_text_input_event())
        {
            const char* temptext = query_text_input();
            
            if(temptext != nullptr)
            {
                s->insert(cursor_pos, temptext);
                cursor_pos += static_cast<int>(strlen(temptext));
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
	        
	        SDL_Delay(10);
	}

    SDL_StopTextInput();
    
	clear_keyboard();
    
    return !cancel;
}

bool prompt_for_string(const std::string& message, std::string& result)
{
    screen* screen_ctx = active_screen();
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
	    screen_ctx->darken_screen();
	    
	    const short max_chars = 29;
	    
	    int x = 58;
	    int y = 60;
	    int w = static_cast<int>(max_chars) * 6;
	    int h = 10;
    
    screen_ctx->draw_button(x - 5, y - 20, x + w + 10, y + h + 10, 1);
    
    char* str = screen_ctx->text_normal.input_string_ex(x, y, max_chars, message.c_str(), result.c_str());
    
    if(str == nullptr)
        return false;
    
    result = str;
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


