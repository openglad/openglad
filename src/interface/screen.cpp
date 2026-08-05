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
//screen.cpp

/* ChangeLog
	buffers: 7/31/02: *deleted some redundant headers.
			: *load_scenario now looks for all uppercase files in
			:  levels.001 if lowercase file fails
	buffers: 8/15/02: *load_scenario now checks for uppercase file names in
			   scen/ in case lowercase check fails
*/

#include <openglad/interface/game_context.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/sound.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/company.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/interface/render/effects.h>
#include <openglad/interface/render/view.h>
#include <openglad/core/util.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view_layout.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/progression.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <format>
#include <thread>

#ifdef TESTING
// The UX screenshot probes run their input driver on a second thread. These
// flags let that thread inspect one fully presented frame without racing the
// menu loop's next clear/redraw pass.
std::atomic_bool g_test_present_pause_requested{false};
std::atomic_bool g_test_present_paused{false};
#endif

// Used by statistics::do_command() COMMAND_FOLLOW to find a leader walker.
// The function is declared in stats.cpp; defined here in the SDL build.
walker* find_follow_leader()
{
    if (!og::runtime::current_session || og::runtime::current_session->myscreen_ == nullptr)
        return nullptr;
    if (og::runtime::current_session->myscreen_->numviews == 1)
        return og::runtime::current_session->myscreen_->viewob[0]->control;
    // Multi-view: pick whichever view's controller has yo_delay set
    if (og::runtime::current_session->myscreen_->viewob[0]->control && og::runtime::current_session->myscreen_->viewob[0]->control->yo_delay())
        return og::runtime::current_session->myscreen_->viewob[0]->control;
    if (og::runtime::current_session->myscreen_->viewob[1]->control && og::runtime::current_session->myscreen_->viewob[1]->control->yo_delay())
        return og::runtime::current_session->myscreen_->viewob[1]->control;
    return nullptr;
}

namespace
{
class ScopedGameplayTickActivation
{
public:
    explicit ScopedGameplayTickActivation(og::runtime::SessionState* session)
        : session_(session)
        , previous_(session_ ? session_->gameplay_active_ : false)
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = true;
    }

    ~ScopedGameplayTickActivation()
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = previous_;
    }

    ScopedGameplayTickActivation(const ScopedGameplayTickActivation&) = delete;
    ScopedGameplayTickActivation& operator=(const ScopedGameplayTickActivation&) = delete;

private:
    og::runtime::SessionState* session_ = nullptr;
    bool previous_ = false;
};

const char* scenario_title_error_string(screen::ScenarioTitleError err)
{
    switch(err)
    {
        case screen::ScenarioTitleError::None:
            return "none";
        case screen::ScenarioTitleError::OpenReadFailed:
            return "open_read_failed";
        case screen::ScenarioTitleError::InvalidHeader:
            return "invalid_header";
        case screen::ScenarioTitleError::UnsupportedVersion:
            return "unsupported_version";
        case screen::ScenarioTitleError::ReadFailed:
            return "read_failed";
    }
    return "unknown";
}

screen::ScenarioTitleError map_level_file_error(og::data::LevelFileIoError err)
{
    switch (err)
    {
        case og::data::LevelFileIoError::None:
            return screen::ScenarioTitleError::None;
        case og::data::LevelFileIoError::OpenReadFailed:
            return screen::ScenarioTitleError::OpenReadFailed;
        case og::data::LevelFileIoError::InvalidHeader:
            return screen::ScenarioTitleError::InvalidHeader;
        case og::data::LevelFileIoError::UnsupportedVersion:
            return screen::ScenarioTitleError::UnsupportedVersion;
        case og::data::LevelFileIoError::ParseFailed:
        case og::data::LevelFileIoError::OpenWriteFailed:
        case og::data::LevelFileIoError::SerializeFailed:
            return screen::ScenarioTitleError::ReadFailed;
    }
    return screen::ScenarioTitleError::ReadFailed;
}

const char* save_data_io_error_string(SaveDataIoError err)
{
    switch (err)
    {
        case SaveDataIoError::None:
            return "none";
        case SaveDataIoError::OpenReadFailed:
            return "open_read_failed";
        case SaveDataIoError::OpenWriteFailed:
            return "open_write_failed";
        case SaveDataIoError::ReadFailed:
            return "read_failed";
        case SaveDataIoError::WriteFailed:
            return "write_failed";
        case SaveDataIoError::InvalidHeader:
            return "invalid_header";
        case SaveDataIoError::UnsupportedVersion:
            return "unsupported_version";
        case SaveDataIoError::CampaignLoadFailed:
            return "campaign_load_failed";
    }
    return "unknown";
}

} // namespace


// From picker.cpp
extern Sint32 calculate_level(Uint32 temp_exp);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);

namespace
{

void cleanup_dead_view_controls(screen& self)
{
    // During an active classic respawn mode or an active scripted
    // (TYPE_SCRIPTED) mode, a dead player corpse with a pending revive
    // entry stays bound: the camera holds on the corpse until the revive.
    // Strictly gated on those modes so plain non-respawning behavior is
    // byte-identical.
    const GameWorld& world = self.world();
    const bool respawn_keepalive =
        og::sim::classic_respawn_active(world) ||
        og::sim::mode_scripted_active(world);
    for (int i = 0; i < self.numviews; i++)
    {
        walker* const control = self.viewob[i]->control;
        if (control == nullptr || !control->dead())
            continue;
        if (respawn_keepalive && control->myguy != nullptr &&
            og::sim::respawn_pending_player(world.respawn,
                                                control->entity_id()))
        {
            continue;
        }
        self.viewob[i]->control = nullptr;
    }
}

screen::TickWorldBatches split_screen_event_batches(
    const og::sim::SimEventBatch& source)
{
    og::sim::SimEventBatch cosmetic_batch;
    og::sim::GameFlowEventBatch game_flow_batch;
    og::sim::split_event_batches(source, cosmetic_batch, game_flow_batch);

    return {std::move(cosmetic_batch), std::move(game_flow_batch)};
}

void dispatch_cosmetic_screen_events(screen& self,
                                     const std::vector<og::sim::Event>& events)
{
    cleanup_dead_view_controls(self);
    for (const auto& ev : events)
    {
        switch (ev.kind)
        {
            case og::sim::EventKind::PlaySound:
                {
                    const PlatformBridge& bridge = platform_bridge();
                    if (bridge.play_sound)
                        bridge.play_sound(static_cast<int>(ev.a));
                    else if (self.soundp)
                        self.soundp->play_sound(static_cast<short>(ev.a));
                }
                break;
            case og::sim::EventKind::Notification:
                if (!ev.text.empty())
                {
                    short duration = ev.a ? static_cast<short>(ev.a)
                                          : STANDARD_TEXT_TIME;
                    for (short vi = 0; vi < self.numviews; vi++)
                        self.viewob[vi]->set_display_text(ev.text, duration);
                }
                break;
            case og::sim::EventKind::SetPalette:
                if (ev.a == 0)
                {
                    self.world().current_palette_id = 0;
                    set_palette(self.ourpalette);
                }
                else
                {
                    self.world().current_palette_id = 1;
                    set_palette(self.bluepalette);
                }
                break;
            case og::sim::EventKind::RequestRedraw:
                self.redrawme = 1;
                break;
            case og::sim::EventKind::None:
            case og::sim::EventKind::EndGame:
            case og::sim::EventKind::SetEnd:
            case og::sim::EventKind::RequestExitConfirmation:
            case og::sim::EventKind::WithdrawToLevel:
            case og::sim::EventKind::ScoreChange:
            case og::sim::EventKind::DamageTile:
            default:
                break;
        }
    }
}

bool dispatch_game_flow_screen_events(screen& self,
                                      const std::vector<og::sim::Event>& events)
{
    cleanup_dead_view_controls(self);

    const og::sim::Event* first_exit_request = nullptr;
    const og::sim::Event* first_withdraw_request = nullptr;
    for (const auto& ev : events)
    {
        switch (ev.kind)
        {
            case og::sim::EventKind::EndGame:
                self.sync_save_data_from_world();
                return self.endgame(static_cast<short>(ev.a),
                                    static_cast<short>(
                                        static_cast<std::int32_t>(ev.b)));
            case og::sim::EventKind::SetEnd:
                self.world().end = 1;
                break;
            case og::sim::EventKind::RequestExitConfirmation:
                if (first_exit_request == nullptr)
                    first_exit_request = &ev;
                break;
            case og::sim::EventKind::WithdrawToLevel:
                if (first_withdraw_request == nullptr)
                    first_withdraw_request = &ev;
                break;
            case og::sim::EventKind::ScoreChange:
                self.redrawme = 1;
                break;
            case og::sim::EventKind::DamageTile:
                self.damage_tile(static_cast<short>(ev.a),
                                 static_cast<short>(ev.b));
                break;
            case og::sim::EventKind::None:
            case og::sim::EventKind::PlaySound:
            case og::sim::EventKind::Notification:
            case og::sim::EventKind::SetPalette:
            case og::sim::EventKind::RequestRedraw:
            default:
                break;
        }
    }

    if (first_exit_request != nullptr)
    {
        const short destination_level =
            static_cast<short>(static_cast<std::int32_t>(first_exit_request->a));
        const bool is_withdraw_prompt = first_exit_request->b != 0;
        std::string prompt_text = first_exit_request->text;
        if (prompt_text.empty())
        {
            if (is_withdraw_prompt)
                prompt_text = std::format("Withdraw to Level {}?", destination_level);
            else
                prompt_text = std::format("Exit to Level {}?", destination_level);
        }

        const bool accepted = yes_or_no_prompt("Exit Field", prompt_text.c_str(), false);
        self.redrawme = 1;
        if (accepted)
        {
            if (is_withdraw_prompt)
            {
                short withdraw_level = destination_level;
                if (first_withdraw_request != nullptr)
                {
                    withdraw_level = static_cast<short>(
                        static_cast<std::int32_t>(first_withdraw_request->a));
                }

                const SaveDataIoError load_error = self.save_data.load_with_error(
                    og::data::active_company_slot());
                if (load_error != SaveDataIoError::None)
                {
                    LogError("withdraw_load_failed target_level={} error={}\n",
                             withdraw_level,
                             save_data_io_error_string(load_error));
                    self.sync_save_data_from_world();
                    self.world().withdraw_requested = false;
                    self.world().withdraw_level = -1;
                    return 1;
                }

                self.save_data.scen_num = withdraw_level;
                const SaveDataIoError save_error =
                    self.save_data.save_with_error(
                        og::data::active_company_slot());
                if (save_error != SaveDataIoError::None)
                {
                    LogError("withdraw_save_failed target_level={} error={}\n",
                             withdraw_level,
                             save_data_io_error_string(save_error));
                    self.sync_save_data_from_world();
                    self.world().withdraw_requested = false;
                    self.world().withdraw_level = -1;
                    return 1;
                }
                self.sync_world_from_save_data();
                return self.endgame(1, withdraw_level);
            }

            self.sync_save_data_from_world();
            self.world().withdraw_requested = false;
            self.world().withdraw_level = -1;
            return self.endgame(0, destination_level);
        }

        self.world().withdraw_requested = false;
        self.world().withdraw_level = -1;
    }
    else if (first_withdraw_request != nullptr)
    {
        self.world().withdraw_requested = false;
        self.world().withdraw_level = -1;
    }

    if (self.world().game_ended && !self.world().end)
    {
        self.sync_save_data_from_world();
        return self.endgame(self.world().ending, self.world().next_level);
    }

    if (self.world().end)
        return 1;

    return 1;
}

} // namespace
loader* sdl_entity_loader();

// Screen window boundaries now come from the world canvas dims via
// og::view_layout::compute_view_layout instead of the retired S_* / T_*
// absolute constants. Classic-pinned screens still resolve to 320x200.
inline constexpr int MAX_SPREAD = 10; // this controls find_near_foe

// load_version_* functions now live in level_runtime_data.cpp and take
// OgFile& + LevelRuntimeData*



Uint32 random(Uint32 x)
{
	if (x < 1)
		return 0;
	return static_cast<Uint32>( (static_cast<Uint32>(rand())) % x);
}

void screen::set_fullscreen(bool fullscreen)
{
    video_impl_->set_fullscreen(fullscreen);
}

void screen::clearbuffer()
{
    video_impl_->clearbuffer();
}

void screen::clearbuffer(int x, int y, int w, int h)
{
    video_impl_->clearbuffer(x, y, w, h);
}

void screen::clear_window()
{
    video_impl_->clear_window();
}

std::span<unsigned char> screen::getbuffer()
{
    return video_impl_->getbuffer();
}

void screen::putblack(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize)
{
    video_impl_->putblack(startx, starty, xsize, ysize);
}

void screen::fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     unsigned char color)
{
    video_impl_->fastbox(startx, starty, xsize, ysize, color);
}

void screen::fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     unsigned char color, unsigned char flag)
{
    video_impl_->fastbox(startx, starty, xsize, ysize, color, flag);
}

void screen::fastbox_outline(Sint32 startx, Sint32 starty, Sint32 xsize,
                             Sint32 ysize, unsigned char color)
{
    video_impl_->fastbox_outline(startx, starty, xsize, ysize, color);
}

void screen::point(Sint32 x, Sint32 y, unsigned char color)
{
    video_impl_->point(x, y, color);
}

void screen::pointb(Sint32 x, Sint32 y, unsigned char color)
{
    video_impl_->pointb(x, y, color);
}

void screen::pointb(Sint32 x, Sint32 y, unsigned char color, unsigned char alpha)
{
    video_impl_->pointb(x, y, color, alpha);
}

void screen::pointb(int offset, unsigned char color)
{
    video_impl_->pointb(offset, color);
}

void screen::pointb(Sint32 x, Sint32 y, int r, int g, int b)
{
    video_impl_->pointb(x, y, r, g, b);
}

void screen::hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color)
{
    video_impl_->hor_line(x, y, length, color);
}

void screen::ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color)
{
    video_impl_->ver_line(x, y, length, color);
}

void screen::hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color,
                      Sint32 tobuffer)
{
    video_impl_->hor_line(x, y, length, color, tobuffer);
}

void screen::hor_line_alpha(Sint32 x, Sint32 y, Sint32 length, unsigned char color,
                            Uint8 alpha)
{
    video_impl_->hor_line_alpha(x, y, length, color, alpha);
}

void screen::ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color,
                      Sint32 tobuffer)
{
    video_impl_->ver_line(x, y, length, color, tobuffer);
}

void screen::draw_line(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                       unsigned char color)
{
    video_impl_->draw_line(x1, y1, x2, y2, color);
}

void screen::do_cycle(Sint32 curmode, Sint32 maxmode)
{
    video_impl_->do_cycle(curmode, maxmode);
}

void screen::putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     std::span<const unsigned char> sourcedata)
{
    video_impl_->putdata(startx, starty, xsize, ysize, sourcedata);
}

void screen::putdata_alpha(Sint32 startx, Sint32 starty, Sint32 xsize,
                           Sint32 ysize,
                           std::span<const unsigned char> sourcedata,
                           unsigned char alpha)
{
    video_impl_->putdata_alpha(startx, starty, xsize, ysize, sourcedata, alpha);
}

void screen::putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                         std::span<const unsigned char> sourcedata)
{
    video_impl_->putdatatext(startx, starty, xsize, ysize, sourcedata);
}

void screen::putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     std::span<const unsigned char> sourcedata,
                     unsigned char color)
{
    video_impl_->putdata(startx, starty, xsize, ysize, sourcedata, color);
}

void screen::putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                         std::span<const unsigned char> sourcedata,
                         unsigned char color)
{
    video_impl_->putdatatext(startx, starty, xsize, ysize, sourcedata, color);
}

void screen::putbuffer(Sint32 tilestartx, Sint32 tilestarty,
                       Sint32 tilewidth, Sint32 tileheight, Sint32 portstartx,
                       Sint32 portstarty, Sint32 portendx, Sint32 portendy,
                       std::span<const unsigned char> sourceptr)
{
    video_impl_->putbuffer(tilestartx, tilestarty, tilewidth, tileheight,
                           portstartx, portstarty, portendx, portendy, sourceptr);
}

void screen::putbuffer_alpha(Sint32 tilestartx, Sint32 tilestarty,
                             Sint32 tilewidth, Sint32 tileheight,
                             Sint32 portstartx, Sint32 portstarty,
                             Sint32 portendx, Sint32 portendy,
                             std::span<const unsigned char> sourceptr,
                             unsigned char alpha)
{
    video_impl_->putbuffer_alpha(tilestartx, tilestarty, tilewidth, tileheight,
                                 portstartx, portstarty, portendx, portendy,
                                 sourceptr, alpha);
}

void screen::putbuffer_surface(Sint32 tilestartx, Sint32 tilestarty,
                               Sint32 tilewidth, Sint32 tileheight,
                               Sint32 portstartx, Sint32 portstarty,
                               Sint32 portendx, Sint32 portendy,
                               void* sourceptr)
{
    video_impl_->putbuffer_surface(tilestartx, tilestarty, tilewidth, tileheight,
                                   portstartx, portstarty, portendx, portendy,
                                   sourceptr);
}

void* screen::create_accel_surface(std::span<const unsigned char> indexed_pixels,
                                   Sint32 width, Sint32 height)
{
    return video_impl_->create_accel_surface(indexed_pixels, width, height);
}

void screen::destroy_accel_surface(void* surface)
{
    video_impl_->destroy_accel_surface(surface);
}

bool screen::floor_layer_begin(Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
    return video_impl_->floor_layer_begin(x, y, w, h);
}

void screen::floor_layer_end(Sint32 x, Sint32 y, Sint32 w, Sint32 h,
                             float scale, Sint32 cx, Sint32 cy,
                             unsigned char alpha,
                             DepthFxParams fx,
                             Sint32 pad_x, Sint32 pad_y)
{
    video_impl_->floor_layer_end(x, y, w, h, scale, cx, cy, alpha, fx,
                                 pad_x, pad_y);
}

void screen::fail_next_floor_layer_allocation_for_testing()
{
    video_impl_->fail_next_floor_layer_allocation_for_testing();
}

int screen::floor_layer_fallback_count_for_testing() const
{
    return video_impl_->floor_layer_fallback_count_for_testing();
}

std::int64_t screen::floor_layer_source_pixels_for_testing() const
{
    return video_impl_->floor_layer_source_pixels_for_testing();
}

std::int64_t screen::floor_layer_scaled_pixels_for_testing() const
{
    return video_impl_->floor_layer_scaled_pixels_for_testing();
}

bool screen::floor_layer_redirect_active_for_testing() const
{
    return video_impl_->floor_layer_redirect_active_for_testing();
}

void screen::walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                           Sint32 walkerwidth, Sint32 walkerheight,
                           Sint32 portstartx, Sint32 portstarty,
                           Sint32 portendx, Sint32 portendy,
                           std::span<const unsigned char> sourceptr,
                           unsigned char teamcolor)
{
    video_impl_->walkputbuffer(walkerstartx, walkerstarty, walkerwidth,
                               walkerheight, portstartx, portstarty, portendx,
                               portendy, sourceptr, teamcolor);
}

void screen::walkputbuffer_flash(Sint32 walkerstartx, Sint32 walkerstarty,
                                 Sint32 walkerwidth, Sint32 walkerheight,
                                 Sint32 portstartx, Sint32 portstarty,
                                 Sint32 portendx, Sint32 portendy,
                                 std::span<const unsigned char> sourceptr,
                                 unsigned char teamcolor)
{
    video_impl_->walkputbuffer_flash(walkerstartx, walkerstarty, walkerwidth,
                                     walkerheight, portstartx, portstarty,
                                     portendx, portendy, sourceptr, teamcolor);
}

void screen::walkputbuffertext(Sint32 walkerstartx, Sint32 walkerstarty,
                               Sint32 walkerwidth, Sint32 walkerheight,
                               Sint32 portstartx, Sint32 portstarty,
                               Sint32 portendx, Sint32 portendy,
                               std::span<const unsigned char> sourceptr,
                               unsigned char teamcolor)
{
    video_impl_->walkputbuffertext(walkerstartx, walkerstarty, walkerwidth,
                                   walkerheight, portstartx, portstarty,
                                   portendx, portendy, sourceptr, teamcolor);
}

void screen::walkputbuffertext_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                                     Sint32 walkerwidth, Sint32 walkerheight,
                                     Sint32 portstartx, Sint32 portstarty,
                                     Sint32 portendx, Sint32 portendy,
                                     std::span<const unsigned char> sourceptr,
                                     unsigned char teamcolor, Uint8 alpha)
{
    video_impl_->walkputbuffertext_alpha(walkerstartx, walkerstarty, walkerwidth,
                                         walkerheight, portstartx, portstarty,
                                         portendx, portendy, sourceptr,
                                         teamcolor, alpha);
}

void screen::walkputbuffer_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                                 Sint32 walkerwidth, Sint32 walkerheight,
                                 Sint32 portstartx, Sint32 portstarty,
                                 Sint32 portendx, Sint32 portendy,
                                 std::span<const unsigned char> sourceptr,
                                 unsigned char teamcolor, Uint8 alpha)
{
    video_impl_->walkputbuffer_alpha(walkerstartx, walkerstarty, walkerwidth,
                                     walkerheight, portstartx, portstarty,
                                     portendx, portendy, sourceptr,
                                     teamcolor, alpha);
}

void screen::walkputbuffer_shadow(Sint32 walkerstartx, Sint32 walkerstarty,
                                  Sint32 walkerwidth, Sint32 walkerheight,
                                  Sint32 portstartx, Sint32 portstarty,
                                  Sint32 portendx, Sint32 portendy,
                                  std::span<const unsigned char> sourceptr,
                                  Uint8 alpha, Sint32 height_divisor,
                                  Sint32 inset)
{
    video_impl_->walkputbuffer_shadow(walkerstartx, walkerstarty, walkerwidth,
                                      walkerheight, portstartx, portstarty,
                                      portendx, portendy, sourceptr, alpha,
                                      height_divisor, inset);
}

void screen::walkputbuffer_reflect(Sint32 walkerstartx, Sint32 walkerstarty,
                                   Sint32 walkerwidth, Sint32 walkerheight,
                                   Sint32 portstartx, Sint32 portstarty,
                                   Sint32 portendx, Sint32 portendy,
                                   std::span<const unsigned char> sourceptr,
                                   unsigned char teamcolor, Uint8 alpha,
                                   std::span<const unsigned char> grid,
                                   Sint32 gridw, Sint32 gridh,
                                   Sint32 world_offset_x, Sint32 world_offset_y,
                                   std::span<const bool, 256> reflect_mask)
{
    video_impl_->walkputbuffer_reflect(walkerstartx, walkerstarty, walkerwidth,
                                       walkerheight, portstartx, portstarty,
                                       portendx, portendy, sourceptr,
                                       teamcolor, alpha, grid, gridw, gridh,
                                       world_offset_x, world_offset_y,
                                       reflect_mask);
}

void screen::walkputbuffer_reflect(Sint32 walkerstartx, Sint32 walkerstarty,
                                   Sint32 walkerwidth, Sint32 walkerheight,
                                   Sint32 portstartx, Sint32 portstarty,
                                   Sint32 portendx, Sint32 portendy,
                                   std::span<const unsigned char> sourceptr,
                                   unsigned char teamcolor, Uint8 alpha,
                                   std::span<const unsigned char> grid,
                                   Sint32 gridw, Sint32 gridh,
                                   Sint32 world_offset_x, Sint32 world_offset_y)
{
    walkputbuffer_reflect(walkerstartx, walkerstarty, walkerwidth,
                          walkerheight, portstartx, portstarty,
                          portendx, portendy, sourceptr,
                          teamcolor, alpha, grid, gridw, gridh,
                          world_offset_x, world_offset_y, reflective_tiles());
}

void screen::walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                           Sint32 walkerwidth, Sint32 walkerheight,
                           Sint32 portstartx, Sint32 portstarty,
                           Sint32 portendx, Sint32 portendy,
                           std::span<const unsigned char> sourceptr,
                           unsigned char teamcolor, unsigned char mode,
                           Sint32 invisibility, unsigned char outline,
                           unsigned char shifttype)
{
    video_impl_->walkputbuffer(walkerstartx, walkerstarty, walkerwidth,
                               walkerheight, portstartx, portstarty, portendx,
                               portendy, sourceptr, teamcolor, mode,
                               invisibility, outline, shifttype);
}

void screen::buffer_to_screen(Sint32 viewstartx, Sint32 viewstarty,
                              Sint32 viewwidth, Sint32 viewheight)
{
    TRACE("present", "buffer_to_screen %d %d %d %d",
          viewstartx, viewstarty, viewwidth, viewheight);
    video_impl_->buffer_to_screen(viewstartx, viewstarty, viewwidth, viewheight);
#ifdef TESTING
    // FX-capture hook (scripts/fx_review): with OG_DUMP_DIR set, every 3rd
    // presented frame is written as a P6 PPM so blocking menu flows — which
    // never return control to a test loop — can be filmed live.
    if (const char* dump_dir = getenv("OG_DUMP_DIR"))
    {
        static int dump_counter = 0;
        if (dump_counter++ % 3 == 0)
        {
            static int dump_frame = 0;
            char path[512];
            snprintf(path, sizeof(path), "%s/%03d.ppm", dump_dir, dump_frame++);
            FILE* fp = fopen(path, "wb");
            if (fp)
            {
                fprintf(fp, "P6\n320 200\n255\n");
                for (int j = 0; j < 200; j++)
                    for (int i = 0; i < 320; i++)
                    {
                        Uint8 r, g, b;
                        get_pixel(i, j, &r, &g, &b);
                        fputc(r, fp);
                        fputc(g, fp);
                        fputc(b, fp);
                    }
                fclose(fp);
            }
        }
    }

    if (g_test_present_pause_requested.load(std::memory_order_acquire))
    {
        g_test_present_paused.store(true, std::memory_order_release);
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::seconds(30);
        while (g_test_present_pause_requested.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                fprintf(stderr,
                        "test frame capture held the presenter for more than "
                        "30000 ms\n");
                fflush(stderr);
                abort();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        g_test_present_paused.store(false, std::memory_order_release);
    }
#endif
}

void screen::draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                      unsigned char color, Sint32 filled)
{
    video_impl_->draw_box(x1, y1, x2, y2, color, filled);
}

void screen::draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                      unsigned char color, Sint32 filled, Sint32 tobuffer)
{
    video_impl_->draw_box(x1, y1, x2, y2, color, filled, tobuffer);
}

void screen::draw_rect_filled(Sint32 x, Sint32 y, Uint32 w, Uint32 h,
                              unsigned char color, Uint8 alpha)
{
    video_impl_->draw_rect_filled(x, y, w, h, color, alpha);
}

void screen::draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    video_impl_->draw_button_inverted(x, y, w, h);
}

void screen::draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border)
{
    video_impl_->draw_button(x1, y1, x2, y2, border);
}

void screen::draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                         Sint32 border, Sint32 tobuffer)
{
    video_impl_->draw_button(x1, y1, x2, y2, border, tobuffer);
}

void screen::draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                                 bool use_border, int base_color, int high_color,
                                 int shadow_color)
{
    video_impl_->draw_button_colored(x1, y1, x2, y2, use_border, base_color,
                                     high_color, shadow_color);
}

Sint32 screen::draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                           const char* header)
{
    return video_impl_->draw_dialog(x1, y1, x2, y2, header);
}

void screen::draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2)
{
    video_impl_->draw_text_bar(x1, y1, x2, y2);
}

void screen::darken_screen()
{
    video_impl_->darken_screen();
}

void screen::swap()
{
    const PlatformBridge& bridge = platform_bridge();
    if (bridge.present_frame)
    {
        bridge.present_frame();
        return;
    }

    video_impl_->swap();
}

void screen::get_pixel(int x, int y, Uint8* r, Uint8* g, Uint8* b)
{
    video_impl_->get_pixel(x, y, r, g, b);
}

int screen::get_pixel(int x, int y, int* index)
{
    return video_impl_->get_pixel(x, y, index);
}

int screen::get_pixel(int offset)
{
    return video_impl_->get_pixel(offset);
}

bool screen::save_screenshot()
{
    return video_impl_->save_screenshot();
}

void screen::fade_between24(void* surface, const Uint8* from, const Uint8* to,
                            int amount)
{
    video_impl_->fade_between24(surface, from, to, amount);
}

void screen::set_render_interpolation_client(
    const og::sim::GameClient* client) noexcept
{
    render_interpolation_client_ = client;
}

void screen::set_render_interpolation_speed_factor(float speed_factor) noexcept
{
    render_interpolation_speed_factor_ = std::max(speed_factor, 0.0f);
}

int screen::fade_between(void* old_surface, void* new_surface, void* dest_surface)
{
    return video_impl_->fade_between(old_surface, new_surface, dest_surface);
}

int screen::fadeblack(bool fade_in)
{
    return video_impl_->fadeblack(fade_in);
}

// ************************************************************
//  SCREEN -- graphics routines
//
//  This object is the video graphics object.  All display
//  must pass through this object, and all on-screen objects
//  are found in this object.
// ************************************************************


// Common initialization logic shared by both constructors.
void screen::init_common(short howmany, bool has_display)
{
    // Register this screen as the current session's myscreen_ so the myscreen
    // macro resolves during the rest of construction (text rendering, etc.).
    if (og::runtime::current_session)
        og::runtime::current_session->myscreen_ = this;
    myloader = sdl_entity_loader();
    level_runtime_data_.attach_world(&world_);

    TRACE("init", "screen constructor: numviews=%d display=%d", howmany, has_display);

	Sint32 i, j;

	grab_timer();

	timerstart = static_cast<Uint32>(query_timer_control());
	framecount = 0;
    render_interpolation_client_ = nullptr;
    render_interpolation_speed_factor_ = 1.0f;

	world_.control_hp = 0;

	// Load the palette ..
	load_and_set_palette("our.pal", newpalette);

	// Loading-screen text references (only used when has_display is true).
	text& load_text = text_normal;
	constexpr Sint32 load_left = 66;

	if (has_display) {
		draw_button(60, 50, 260, 110, 2, 1);
		draw_text_bar(64, 54, 256, 62);
		load_text.write_y(56, "Loading Gladiator..Please Wait", RED, 1);
		draw_text_bar(64, 64, 256, 106);
		load_text.write_xy(load_left, 70, "Loading Graphics...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, canvas_w(), canvas_h());
		load_text.write_xy(load_left, 70, "Loading Graphics...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 78, "Loading Gameplay Info...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, canvas_w(), canvas_h());
	}

	update_overscan_setting();

	palmode = 0;
	world_.end = 0;
	world_.timer_wait = 6;
	redrawme = 1;
	cyclemode = 1;
	world_.enemy_freeze = 0;
	world_.level_done = 0;
	world_.retry = false;

	numviews = howmany;
    for (auto& view : viewob)
        view.reset();
    initialize_views();

	if (has_display) {
		load_text.write_xy(load_left, 78, "Loading Gameplay Info...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 86, "Initializing Display...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 94, "Initializing Sound...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, canvas_w(), canvas_h());
	}

    // Init the sound data
    soundp = create_soundob(false);
    if (!cfg.is_on("sound", "sound")) {
        soundp->set_sound(1);
    }

	if (has_display) {
		load_text.write_xy(load_left, 94, "Initializing Sound...Done", DARK_BLUE, 1);
		buffer_to_screen(0, 0, canvas_w(), canvas_h());
	}

	init_all_registries();
	// Let's set the special names for all walkers ..
	for (i=0; i < NUM_FAMILIES; i++)
	{
		auto* fd = get_family_descriptor(i);
		for (j=0; j < NUM_SPECIALS; j++)
		{
			special_name[i][j] = fd ? fd->special_names[j] : "NONE";
			alternate_name[i][j] = fd ? fd->alternate_names[j] : "NONE";
		}
	}

	sync_world_from_save_data();
}

screen::screen(GameWorld& world, std::unique_ptr<video> video_impl, short howmany,
               bool has_display)
    : video()
    , video_impl_(std::move(video_impl))
    , ourpalette(video_impl_->ourpalette_ref())
    , redpalette(video_impl_->redpalette_ref())
    , bluepalette(video_impl_->bluepalette_ref())
    , dospalette(video_impl_->dospalette_ref())
    , videobuffer(video_impl_->videobuffer_ref())
    , cyclemode(video_impl_->cyclemode_ref())
    , text_normal(video_impl_->text_normal_ref())
    , text_big(video_impl_->text_big_ref())
    , world_(world)
    , myloader(nullptr)
    , level_runtime_data_(1, false, &sdl_level_data_hooks(), &level_visuals_)
    , damage_number_render_context_(
          std::make_unique<DamageNumberRenderContext>())
{
    init_common(howmany, has_display);
}

screen::~screen()
{
	release_timer();
	soundp.reset();
	cleanup(1); //make sure we've cleaned up
}

DamageNumberRenderContext& screen::damage_number_render_context() noexcept
{
    return *damage_number_render_context_;
}

const DamageNumberRenderContext& screen::damage_number_render_context() const
    noexcept
{
    return *damage_number_render_context_;
}

void screen::initialize_views()
{
    // Debounce state is shared across view instances, so fresh gameplay/replay
    // startup must clear it before creating a new view set.
    reset_viewscreen_input_debounce();

    // The constructor rects are computed from the world canvas dims (FULL
    // layout); they are then immediately overridden by the constructor's own
    // resize(prefs[PREF_VIEW]) call, which re-derives the geometry from the
    // same canvas dims plus the player's saved view mode.
    // Even though it looks okay here, these positions and sizes are overridden by viewscreen::resize() later.
	if (numviews >= 1 && numviews <= MAX_VIEWS)
	{
		for (Sint32 i = 0; i < numviews; i++)
		{
			const og::view_layout::ViewLayout r =
			    og::view_layout::compute_view_layout(
			        numviews, static_cast<int>(i), og::view_layout::kModeFull,
			        world_canvas_w(), world_canvas_h());
			viewob[i] = std::make_unique<viewscreen>(
			    static_cast<short>(r.x), static_cast<short>(r.y),
			    static_cast<short>(r.w), static_cast<short>(r.h),
			    static_cast<short>(i));
		}
	}
	else
    {
        LogError("screen_init_views_failed numviews={}\n", numviews);
        numviews = 0; // no views created for an unsupported count; keep per-frame loops in-bounds
    }
}

// Re-derives every live viewscreen's geometry from the current world canvas
// dimensions. Called after graphics/zoom changes the logical size or the
// level editor pins/restores the classic canvas:
// resize(whatmode) recomputes the pane layout via compute_view_layout and
// restarts each started radar.
void screen::relayout_views()
{
	for (Sint32 i = 0; i < numviews && i < MAX_VIEWS; i++)
	{
		if (viewob[i])
			viewob[i]->resize(viewob[i]->prefs[PREF_VIEW]);
	}
	redrawme = 1;
}

void screen::cleanup(short howmany)
{
	Sint32 i;

    numviews = howmany; // # of viewscreens
    for (i=0; i < MAX_VIEWS; i++)
    {
        viewob[i].reset();
    }
}

void screen::ready_for_battle(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);
    
	initialize_views();
	world_.reset_level_progress();

	world_.end = 0;
	
	world_.retry = false;

	redrawme = 1;

	timerstart = static_cast<Uint32>(query_timer_control());
	framecount = 0;
	world_.enemy_freeze = 0;

	world_.control_hp = 0;

	palmode = 0;

	redrawme = 1;

}

void screen::reset(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);

    // PvP reset: same canvas-derived FULL rects as initialize_views (also
    // immediately overridden by the constructor's resize(prefs[PREF_VIEW])),
    // but preserving the historical construction ORDER — viewob[1] before
    // viewob[0] — since per-player pref loading happens at construction.
	if (numviews >= 1 && numviews <= MAX_VIEWS)
	{
		static constexpr std::array<short, 4> kPvpConstructionOrder = {1, 0, 2, 3};
		for (Sint32 n = 0; n < numviews; n++)
		{
			const short i = (numviews == 1) ? static_cast<short>(0)
			                                : kPvpConstructionOrder[static_cast<size_t>(n)];
			const og::view_layout::ViewLayout r =
			    og::view_layout::compute_view_layout(
			        numviews, static_cast<int>(i), og::view_layout::kModeFull,
			        world_canvas_w(), world_canvas_h());
			viewob[i] = std::make_unique<viewscreen>(
			    static_cast<short>(r.x), static_cast<short>(r.y),
			    static_cast<short>(r.w), static_cast<short>(r.h), i);
		}
	}
	else
	{
		numviews = 0; // no views created for an unsupported count; keep per-frame loops in-bounds
	}

	world_.end = 0;

	redrawme = 1;

	save_data.reset();
	level_runtime_data_.clear();
	sync_world_from_save_data();

	timerstart = static_cast<Uint32>(query_timer_control());
	framecount = 0;
	world_.enemy_freeze = 0;

	world_.control_hp = 0;

	palmode = 0;

	world_.end = 0;

	redrawme = 1;

}

void screen::sync_world_from_save_data()
{
    world_.my_team = save_data.my_team;
    world_.allied_mode = save_data.allied_mode;
    world_.ctf_requested_team_count = save_data.ctf_team_count;
    world_.ctf_requested_capture_limit = save_data.ctf_capture_limit;
    world_.ctf_requested_respawn_ticks = save_data.ctf_respawn_ticks;
    world_.ctf_requested_strip_scenario_troops = save_data.ctf_strip_scenario_troops;
    // Modes may clamp world knobs (Classic: identity). Applied in BOTH
    // sync_world_from_save_data twins (see headless_server_runtime.cpp).
    world_.respawn_mode =
        og::mode::current_progression().clamp_respawn_mode(save_data.respawn_mode);
    world_.generator_rate = save_data.generator_rate;
    world_.keep_fallen_heroes = save_data.keep_fallen_heroes;
    world_.current_scenario = save_data.scen_num;
    for (int i = 0; i < 4; ++i)
        world_.m_score[i] = save_data.m_score[i];

    auto e = save_data.completed_levels.find(save_data.current_campaign);
    if (e != save_data.completed_levels.end())
        world_.completed_levels = e->second;
    else
        world_.completed_levels.clear();

    world_.withdraw_requested = false;
    world_.withdraw_level = -1;
}

void screen::sync_save_data_from_world()
{
    save_data.my_team = world_.my_team;
    save_data.allied_mode = world_.allied_mode;
    save_data.scen_num = world_.current_scenario;
    for (int i = 0; i < 4; ++i)
        save_data.m_score[i] = world_.m_score[i];

    save_data.completed_levels[save_data.current_campaign] = world_.completed_levels;
}

bool screen::load_level()
{
    return level_runtime_data_.load();
}

bool screen::save_level()
{
    return level_runtime_data_.save();
}

LevelRuntimeData::IoError screen::load_level_with_error()
{
    return level_runtime_data_.load_with_error();
}

LevelRuntimeData::IoError screen::save_level_with_error()
{
    return level_runtime_data_.save_with_error();
}

void screen::clear()
{
	unsigned short i;

	//buffers: PORT:  for (i=0;i<64000;i++)
	//buffers: PORT:  {
	//buffers: PORT:         videobuffer[i] = 0;
	//buffers: PORT:  }
	clearbuffer();

	for (i=0; i < numviews; i ++)
		viewob[i]->clear();
}

// REDRAW -- This function moves through the data on the grid (map)
//           finding which grid squares are on screen.  For each on
//           screen, it pashorts the appropriate graphics pixie onto
//           the screen by calling the function DRAW in PIXIE.
bool screen::redraw()
{
	short i;
	// Reserve the stable classic-density gameplay-chrome layer before any view
	// draws. At exact classic dimensions with nearest rendering, and on an
	// allocation fallback, the HUD scopes safely alias World.
	begin_gameplay_frame();
	draw_panel_chrome(numviews);
	// Advance all render-only effect state (weather drift, ripple/trail/dust
	// phases, per-entity position history): exactly once per frame.
	effects_advance_frame();
	announce_way_clear_if_needed();
	for (i=0; i < numviews; i++)
		viewob[i]->redraw();

	return 1;
}

// B4: one-shot on-screen notice when the exit conditions become satisfied.
// The sim recomputes world.level_done every tick: 0 while live hostile
// livings (including dormant delayed spawns) remain, 1 once they are gone
// AND a live exit is present (the ==1 assignment is only reachable from the
// exit scan, so "with a live exit" is implied). Pure render-side read —
// networked-safe because level_done is part of the world snapshot mirrors
// receive; nothing is written back to the sim.
void screen::announce_way_clear_if_needed()
{
	// New level (id change) or level (re)start (tick counter rewound by
	// glad_init): re-arm the latch.
	if (world_.id != way_clear_level_id_ || world_.tick_count_ == 0)
	{
		way_clear_level_id_ = world_.id;
		way_clear_last_level_done_ = -1;
		way_clear_announced_ = false;
	}

	const short done = world_.level_done;
	if (!way_clear_announced_ && way_clear_last_level_done_ == 0 && done == 1)
	{
		way_clear_announced_ = true;
		do_notify("The way is clear -- you may exit", nullptr);
		TRACE("hud", "way_clear level=%d tick=%u",
		      world_.id, static_cast<unsigned>(world_.tick_count_));
	}
	way_clear_last_level_done_ = done;
}


// REFRESH -- refreshes the viewscreens
void screen::refresh()
{
	if (numviews <= 0)
		return;

	// Every view and the shared HUD have already drawn into the world canvas.
	// Present it once as a complete frame. Besides avoiding one full texture
	// upload/present per split-screen view, this gives SAI/Eagle a complete
	// source rectangle so pixels outside the individual viewports are never
	// left stale in the doubled scratch surface.
	buffer_to_screen(0, 0, canvas_w(), canvas_h());
}


// **************************
// Useful stuff again
// **************************

short screen::input(const void* native_event)
{
	// static text mytext;
	short i;
	if (native_event == nullptr)
		return 1;

	for (i=0; i < numviews; i++)
		viewob[i]->input(native_event);

	return 1;
}

short screen::continuous_input()
{
	// static text mytext;
	short i;

	for (i=0; i < numviews; i++)
		viewob[i]->continuous_input();

	return 1;
}

void screen::process_input(const InputState& input_state)
{
    ScopedGameplayTickActivation gameplay_input_active(og::runtime::current_session);
	for (short i = 0; i < numviews; i++)
		viewob[i]->process_input(input_state);
}

bool screen::dispatch_sim_event_batch(const og::sim::SimEventBatch& batch)
{
    const auto [cosmetic_batch, game_flow_batch] =
        split_screen_event_batches(batch);
    dispatch_cosmetic_events(cosmetic_batch);
    return dispatch_game_flow_events(game_flow_batch);
}

screen::TickWorldBatches screen::tick_world()
{
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return {};

    ScopedGameplayTickActivation gameplay_tick_active(og::runtime::current_session);
    world_.tick();

    return split_screen_event_batches(
        og::sim::drain_sim_events(*current_game->sim_events));
}

void screen::dispatch_cosmetic_events(const og::sim::SimEventBatch& batch)
{
    dispatch_cosmetic_screen_events(*this, batch.events);
}

bool screen::dispatch_game_flow_events(
    const og::sim::GameFlowEventBatch& batch)
{
    return dispatch_game_flow_screen_events(*this, batch.events);
}

Uint32 get_time_bonus(int playernum);

short screen::endgame(short ending)
{
	return endgame(ending, -1);
}

short screen::endgame(short ending, short nextlevel)
{
	if (world_.end)
	{
		return 1;
	}

	sync_save_data_from_world();
	
	
	std::map<int, guy*> before;
	std::map<int, walker*> after;
	
	// Get guys from before battle
	for(int i = 0; i < save_data.team_size; i++)
    {
        if(save_data.team_list[static_cast<std::size_t>(i)] != nullptr)
            before.insert(std::make_pair(save_data.team_list[static_cast<std::size_t>(i)]->id, save_data.team_list[static_cast<std::size_t>(i)].get()));
    }
	
    // Get guys from the battle
    for(auto& uptr : world_.oblist)
	{
	    walker* ob = uptr.get();
		if (ob && ob->myguy)
			after.insert(std::make_pair(ob->myguy->id, ob));
	}
	
	const bool networked = og::runtime::current_session != nullptr &&
		og::runtime::current_session->networked_session_;
	const bool isolated_company =
		og::runtime::current_session != nullptr &&
		og::runtime::current_session->isolated_company_session_;

	// Non-win exits route the mode's run-end policy (Classic: no-op) BEFORE
	// the results screen, so mode popups/summaries read post-reset save
	// state (they derive the death floor from world.id, never the cursor).
	if (ending != 0)
	{
		og::mode::LevelOutcome run_outcome;
		run_outcome.ending = ending;
		run_outcome.next_level = nextlevel;
		run_outcome.networked = networked;
		run_outcome.withdrawn = (ending == 1 && nextlevel != -1);
		og::mode::current_progression().on_run_ended(
			save_data, world_, run_outcome);
	}

	// Let's show the results!
    world_.retry = results_screen(ending, nextlevel, before, after);
    
    if (world_.retry)
    {
        // Retry without updating the roster and saving the game
        world_.end = 1;
        sync_world_from_save_data();
        return 1;
    }
    
	if (ending == 1) // 1 = lose, for some reason
	{
		if (nextlevel == -1) // generic defeat
		{
			world_.end = 1;
		}
		else // we're withdrawing to another level
		{
			world_.end = 1;
		}
	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
		world_.end = 1;
	}
	else if (ending == 0) // we won
	{
		// The shared win fold (og::progression::apply_win_fold) tallies the
		// money, marks completion, and advances the cursor for all four
		// finalize sites. The time bonus is caller-computed from the LIVE
		// m_score BEFORE the fold zeroes it, and the CTF rematch shape is
		// evaluated before the cursor moves.
		og::progression::WinFoldContext fold_ctx;
		for (int i=0; i < 4; i++)
			fold_ctx.time_bonus[static_cast<std::size_t>(i)] = get_time_bonus(i);
		fold_ctx.rematch_shape =
			og::progression::mode_rematch_shape(world_, save_data, nextlevel);
		fold_ctx.finished_level = save_data.scen_num; // pre-advance cursor
		fold_ctx.outcome.ending = 0;
		fold_ctx.outcome.next_level = nextlevel;
		fold_ctx.outcome.networked = networked;

		// §4.6 money split: on a networked win latch the capture — the pre-fold
		// deploy roster (dead heroes still ride the oblist here) plus the fold's
		// applied per-team deltas — onto this screen so the client's
		// out-of-dispatch persist can size this machine's deploy-ratio share.
		og::progression::NetWinFoldCapture net_win_capture;
		if (networked)
			net_win_capture.deployed =
				og::progression::collect_deployed_contributors(world_);

		og::progression::apply_win_fold(save_data, world_, fold_ctx);

		if (networked)
		{
			net_win_capture.cash_delta = fold_ctx.applied_cash_delta;
			net_win_capture.score_delta = fold_ctx.applied_score_delta;
			pending_net_win_capture_ = std::move(net_win_capture);
		}

		// In a networked session this display screen holds the COMBINED roster
		// (every player's characters). Autosaving it here would clobber this
		// player's save0 with everyone's gladiators — the networked per-player
		// save path persists only this player's own characters (owner-filtered),
		// so the display must not touch save0. A local lobby's active-team
		// subset uses that same owner-aware merge; only legacy full-roster local
		// sessions autosave here (subject to the mode's persistence policy).
		// (The fold already ran update_guys; this is the save-only tail.)
		if (!networked && !isolated_company &&
		    og::mode::current_progression().persist_after_win())
		{
			// Autosave because we won: the §3.8 choke point stamps
			// last_played, atomic-writes the active company, and
			// (LevelWin) snapshots the freshly-saved file into
			// save/backups/ exactly once (§3.7 retention-pruned byte
			// copy). The netsession scratch can never reach here: the
			// networked branch is excluded above and the active slot
			// can never be "netsession", so the server economy never
			// leaves snapshots behind.
			(void)og::data::company_autosave(
			    save_data, og::data::CompanyAutosaveKind::LevelWin);
		}

		// Every win ends this display session. Single-player and local play
		// return to the team-build menu; networked play does too (the server
		// runs in return-to-lobby mode: it persists per-player progress + the
		// advanced cursor and tells every peer to end, then the menu starts the
		// next level fresh over the live connection). The networked save0 write
		// is still skipped above so the combined roster never clobbers a peer's
		// own save.
		world_.end = 1;
	}

	sync_world_from_save_data();
	return 1;
}

walker* screen::set_walker(walker *ob, Order order, Sint32 family)
{
    if (myloader == nullptr)
        return nullptr;
    return myloader->set_walker(ob, order, family);
}

screen::ScenarioTitleError screen::get_scen_title_with_error(const char *filename, std::string& out_title)
{
    out_title = "none";
    const og::data::LevelFileIoError io_err =
        og::data::load_scenario_title_with_error(filename, out_title);
    const ScenarioTitleError err = map_level_file_error(io_err);

    if(err != ScenarioTitleError::None)
    {
        LogError("scenario_title_load_failed filename={} error={}\n",
            filename ? filename : "(null)", scenario_title_error_string(err));
    }
    return err;
}

const char* screen::get_scen_title(const char *filename, screen *master)
{
    (void)master;
    static std::array<char, 31> buffer = {};
    std::string out_title;
    const ScenarioTitleError err = get_scen_title_with_error(filename, out_title);
    if(err != ScenarioTitleError::None)
        out_title = "none";
    std::snprintf(buffer.data(), buffer.size(), "%s", out_title.c_str());
    return buffer.data();

}


// Look for the first non-dead instance of a given walker ..
walker  * screen::first_of(Order whatorder, unsigned char whatfamily,
                           int team_num)
{
	for(auto& uptr : world_.oblist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead())
		{
			if (ob->query_order() == whatorder &&
			        ob->family() == whatfamily)
			{
				if (team_num == -1 || team_num == ob->team_num())
					return ob;
			}
		}
	}
	return nullptr;
}

void screen::draw_panels(short howmany)
{
	(void)howmany;
	// Force a memory clear ..
	clearbuffer();
	// This redraw covers scenery and frame-only gameplay chrome.
	redraw(); // repaint the screen area ..
}

void screen::draw_panel_chrome(short howmany)
{
	(void)howmany;
	short i;
	ScopedGameplayUiCanvas gameplay_ui(*this);
	for (i=0; i < numviews; i++)
	{
		ScopedGameplayUiViewLayout gameplay_ui_layout(*viewob[i], *this);
		if ( (viewob[i]->prefs[PREF_VIEW] == PREF_VIEW_FULL) ||
		        numviews == 4 )
			; // do nothing
		else
		{
			draw_button(viewob[i]->xloc-4, viewob[i]->yloc-3,
			            viewob[i]->endx+3, viewob[i]->endy+3, 3, 1);
			draw_box(viewob[i]->xloc-1, viewob[i]->yloc-1,
			             viewob[i]->endx, viewob[i]->endy, 0, 0,1);
		}
	}
}

// Uses pixel coordinates
char screen::damage_tile(short xloc, short yloc) // damage the specified tile
{
	return world_.damage_tile(xloc, yloc);
}

void screen::do_notify(std::string_view message, walker  *who)
{
	short i,sent=0;
	for(i=0;i<numviews;i++)
	{
		if (who && viewob[i]->control == who)
		{
			viewob[i]->set_display_text(message,STANDARD_TEXT_TIME);
			sent = 1;
		}

	}
	if (!sent)
		for (i=0; i < numviews; i++)
			viewob[i]->set_display_text(message,STANDARD_TEXT_TIME);

}

void screen::report_mem()
{
	meminfo Memory;
	Memory.FreeLinAddrSpace = 0;
	// Zardus: PORT: this is aparently an incomplete type:  union REGS regs;
	// Same here:  struct SREGS sregs;
	// Zardus: PORT: Undeclared because of problems above:  regs.x.eax = 0x00000500;
	// Same here:  memset( &sregs, 0, sizeof(sregs) );

	// See two lines up:  sregs.es = FP_SEG( &Memory );
	// See three lines up:  regs.x.edi = FP_OFF( &Memory );

	// See two lines up: (plus sounds like a dos thing):  int386x( DPMI_INT, &regs, &regs, &sregs );

	// Them:
	//sprintf(memreport, "Largest Block: %lu bytes",
	//  Memory.LargestBlockAvail);
	//viewob[0]->set_display_text(memreport, STANDARD_TEXT_TIME);
	std::string memreport = std::format("Free Linear address: {} pages",
	        Memory.FreeLinAddrSpace);
	//  Log(memreport);
	//  Log("\n");
	viewob[0]->set_display_text(memreport.c_str(), 25);
	/*
	       Log( "Largest available block (in bytes): %lu\n",
	               MemInfo.LargestBlockAvail );
	       Log( "Maximum unlocked page allocation: %lu\n",
	               MemInfo.MaxUnlockedPage );
	       Log( "Pages that can be allocated and locked: "
	               "%lu\n", MemInfo.LargestLockablePage );
	       Log( "Total linear address space including "
	               "allocated pages: %lu\n",
	               MemInfo.LinAddrSpace );
	       Log( "Number of free pages available: %lu\n",
	                MemInfo.NumFreePagesAvail );

	       Log( "Number of physical pages not in use: %lu\n",
	                MemInfo.NumPhysicalPagesFree );
	       Log( "Total physical pages managed by host: %lu\n",
	                MemInfo.TotalPhysicalPages );
	       Log( "Free linear address space (pages): %lu\n",
	                MemInfo.FreeLinAddrSpace );
	       Log( "Size of paging/file partition (pages): %lu\n",
	                MemInfo.SizeOfPageFile );
	 */
}
