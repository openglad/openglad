#ifndef _TEST_LIVE_GAME_H__
#define _TEST_LIVE_GAME_H__

// Helpers for tests that play a level in the REAL game: glad_init, then
// game_frame_with_result with the transport shadow syncing the display
// mirror from the authoritative server world every tick (AI acts, walk
// animations advance, palette cycling runs).
//
// One implementation, shared by tests/integration/test_game_loop.cpp (its
// gameplay_rec and new_specials_mirror namespaces re-export these names so
// their callers read as before) and tests/integration/test_special_scenes.cpp
// (the New Specials and Gameplay Fixes scenes). Every body here was moved
// verbatim from test_game_loop.cpp; only the effects helper was renamed (to
// all_effects_on).

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <openglad/core/constants.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/resources/save_data.h>

#include "test_sim_random_scope.h"

// glad_gameplay.cpp: loads the level in save_data and starts the shadow.
void glad_init(bool preserve_frame_timing = false);

// Swap the session's SDL keystate pointer for a writable fake so the game
// loop and redraw see chosen keys "physically held" (the seat's input is
// read through isPlayerHoldingKey).
struct SessionKeyStateGuard
{
    const bool* old_keystates = nullptr;
    std::array<bool, SDL_SCANCODE_COUNT> fake_keystates{};

    SessionKeyStateGuard()
        : old_keystates(og::runtime::current_session->keystates_)
    {
        fake_keystates.fill(false);
        og::runtime::current_session->keystates_ = fake_keystates.data();
    }

    ~SessionKeyStateGuard()
    {
        og::runtime::current_session->keystates_ = old_keystates;
    }

    void set(SDL_Keycode key, bool pressed)
    {
        const SDL_Scancode scancode = SDL_GetScancodeFromKey(key, nullptr);
        if (scancode >= 0 && scancode < SDL_SCANCODE_COUNT)
            fake_keystates[static_cast<std::size_t>(scancode)] = pressed;
    }
};

// Install a gameplay context bound to the authoritative server world so
// direct sim calls (attack, setxy) run against the right world/obmap and
// have an event log to write to (the shadow's own server context is
// installed only inside finish_tick).
struct ScopedServerWorldContext
{
    GameplayContext context;
    GameplayContext* previous;
    og::sim::SimEventLog events;

    explicit ScopedServerWorldContext(screen& server_screen)
        : previous(current_game)
    {
        context.world = &server_screen.world();
        context.save = &server_screen.save_data;
        context.sim_events = &events;
        current_game = &context;
    }
    ~ScopedServerWorldContext() { current_game = previous; }

    ScopedServerWorldContext(const ScopedServerWorldContext&) = delete;
    ScopedServerWorldContext& operator=(const ScopedServerWorldContext&) = delete;
};

namespace live_game {

// Where a test writes frames when asked to: $OG_FX_CAPTURE_DIR/<scene>/.
// The tools under scripts/media/ and scripts/fx_review/ set that variable;
// nothing in a normal test run does.
inline void dump_dir_for(const char* scene, char* dir, size_t n)
{
    const char* base = getenv("OG_FX_CAPTURE_DIR");
    snprintf(dir, n, "%s/%s", base, scene);
    std::filesystem::create_directories(dir);
}

// Writes the primary viewport (single-player gameplay scenes) as frame
// `frame` of `scene`: $OG_FX_CAPTURE_DIR/<scene>/NNN.ppm, a P6 PPM.
inline void dump_viewport(screen* s, const char* scene, int frame)
{
    viewscreen* vs = s->viewob[0].get();
    const int x0 = vs->xloc, y0 = vs->yloc, w = vs->xview, h = vs->yview;
    char dir[512];
    dump_dir_for(scene, dir, sizeof(dir));
    char path[544];
    snprintf(path, sizeof(path), "%s/%03d.ppm", dir, frame);
    FILE* fp = fopen(path, "wb");
    ASSERT_NE(nullptr, fp);
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
        {
            Uint8 r, g, b;
            s->get_pixel(x0 + i, y0 + j, &r, &g, &b);
            fputc(r, fp);
            fputc(g, fp);
            fputc(b, fp);
        }
    fclose(fp);
}

inline void build_save(screen* s, const char* campaign, int scen, int numplayers,
                       const std::vector<int>& roster, int level)
{
    s->save_data.reset();
    s->save_data.current_campaign = campaign;
    s->save_data.current_levels[s->save_data.current_campaign] =
        static_cast<short>(scen);
    s->save_data.scen_num = static_cast<short>(scen);
    s->save_data.numplayers = static_cast<unsigned char>(numplayers);
    for (size_t i = 0; i < roster.size(); i++)
    {
        auto g = std::make_unique<guy>(roster[i]);
        g->upgrade_to_level(static_cast<short>(level));
        g->teamnum = 0;
        s->save_data.team_list[i] = std::move(g);
    }
    s->save_data.team_size = static_cast<unsigned char>(roster.size());
    ASSERT_TRUE(s->save_data.save("save0"));
}

// Every draw effect on, and depth_fx at the selector's default, so a scene
// shows the game as a player with everything switched on sees it. The
// DEFAULT floor presentation (overhang/blob shadows) is what shows: the
// ghost view exists only while a player holds the look-up key.
inline void all_effects_on()
{
    for (const char* key : {"shadows", "reflections", "weather", "ripples",
                            "trails", "dust", "fire_glow", "screen_shake",
                            "floor_glide"})
        cfg.apply_setting("effects", key, "on");
    cfg.apply_setting("effects", "depth_fx", "fog"); // the selector's default
}

// The per-level weather roll is world state; force the requested kind on
// BOTH worlds (authoritative server + display mirror) so the scene shows it.
inline void force_weather(WeatherKind kind)
{
    screen* const s = og::runtime::current_session->myscreen_;
    screen* const server =
        og::runtime::local_transport_shadow_testing_server_screen(
            *og::runtime::current_game_session);
    ASSERT_NE(nullptr, server);
    server->world().set_weather(kind);
    s->world().set_weather(kind);
}

// The seat's own walker — the one the camera follows.
inline walker* find_seat_control(GameWorld& w)
{
    for (auto& up : w.oblist)
        if (walker* a = up.get(); a != nullptr && !a->dead() &&
            a->query_order() == Order::Living && a->user() != -1)
            return a;
    return nullptr;
}

// Freeze a walker's HP where it stands: compute_regen_tick adds per_round
// every tick AND a flat +1 whenever current_heal_delay reaches max_heal_delay,
// so the ceiling has to move out of reach too, or the bar creeps up on its own.
inline void stop_hp_regen(walker* w)
{
    w->stats()->set_heal_per_round(0.0f);
    w->stats()->set_current_heal_delay(0);
    w->stats()->set_max_heal_delay(1000000000);
}

// A frame that composed at all has more than one colour in the viewport.
inline bool viewport_is_blank(screen* s)
{
    viewscreen* vs = s->viewob[0].get();
    Uint8 r0, g0, b0;
    s->get_pixel(vs->xloc, vs->yloc, &r0, &g0, &b0);
    for (int j = 0; j < vs->yview; j += 8)
        for (int i = 0; i < vs->xview; i += 8)
        {
            Uint8 r, g, b;
            s->get_pixel(vs->xloc + i, vs->yloc + j, &r, &g, &b);
            if (r != r0 || g != g0 || b != b0)
                return false;
        }
    return true;
}

inline screen* server_screen()
{
    return og::runtime::local_transport_shadow_testing_server_screen(
        *og::runtime::current_game_session);
}

// Strips a loaded level to the walkers in `keep` plus ONE hostile living
// kept dormant far away (a world with no live hostile wins the level on its
// first tick; a dormant one never acts and never draws but holds the level
// open). Generators go too. Run on the server first, then on the mirror with
// the same ids, so the mirror never paints a frame the server disagrees
// with. Returns the sentinel's id (0 when the level has no hostile).
inline std::uint32_t strip_level(GameWorld& w, const std::vector<std::uint32_t>& keep,
                                 unsigned char own_team, std::uint32_t sentinel_id)
{
    std::vector<walker*> doomed;
    for (auto& up : w.oblist)
    {
        walker* const a = up.get();
        if (a == nullptr ||
            std::find(keep.begin(), keep.end(), a->entity_id()) != keep.end())
            continue;
        if (a->query_order() != Order::Living &&
            a->query_order() != Order::Generator)
            continue;
        if (a->query_order() == Order::Living && !a->dead() &&
            a->team_num() != own_team &&
            (sentinel_id == 0 || sentinel_id == a->entity_id()))
        {
            sentinel_id = a->entity_id();
            continue;
        }
        doomed.push_back(a);
    }
    for (walker* a : doomed)
        w.remove_ob(a);
    if (walker* sentinel = w.find_by_id(sentinel_id))
    {
        sentinel->set_spawn_delay(65535);
        sentinel->set_dormant(true);
    }
    w.type = static_cast<char>(w.type & ~SCEN_TYPE_SAVE_ALL);
    return sentinel_id;
}

inline void finish_scene()
{
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    screen* const s = og::runtime::current_session->myscreen_;
    s->world().end = 0;
    s->world().delete_objects();
}

// The sim-RNG state search every test shares (test_sim_random_scope.h).
using ::state_where_draws_are;

} // namespace live_game

#endif
