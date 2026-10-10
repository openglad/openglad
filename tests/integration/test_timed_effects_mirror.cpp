/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// The seat's countdown seen from the DISPLAY world, the snapshot-fed copy
// the player looks at, through the real local transport shadow: the server
// possesses an orc with a ghost hero and later phases the ghost, and the
// display, which runs no sim of its own, answers the same countdown and
// draws it in the HUD.
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/timed_effects.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

void glad_init(bool preserve_frame_timing = false);
short new_score_panel(screen* scr, short do_it); // score_panel.cpp, the HUD

namespace {

// The authoritative world behind the shadow.
screen* server_screen()
{
    return og::runtime::local_transport_shadow_testing_server_screen(
        *og::runtime::current_game_session);
}

// Direct sim calls on the server world run against its world and obmap
// with an event log to write to.
struct ServerContext
{
    GameplayContext context;
    GameplayContext* previous;
    og::sim::SimEventLog events;

    explicit ServerContext(screen& server)
        : previous(current_game)
    {
        context.world = &server.world();
        context.save = &server.save_data;
        context.sim_events = &events;
        current_game = &context;
    }
    ~ServerContext() { current_game = previous; }
    ServerContext(const ServerContext&) = delete;
    ServerContext& operator=(const ServerContext&) = delete;
};

// One exact tick through the shadow: the seat's input reaches the server,
// the server steps once, the display applies the result.
void drive_tick(const InputState& input = InputState{})
{
    og::runtime::GameSession& session = *og::runtime::current_game_session;
    screen* const s = og::runtime::current_session->myscreen_;
    og::runtime::local_transport_shadow_send_input(
        session, input, s->world().tick_count_ + 1u);
    og::runtime::local_transport_shadow_finish_tick(session);
}

InputState switch_char_input()
{
    InputState input{};
    input.players[0].held[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    return input;
}

// Strips a loaded level to `keep` plus one hostile kept dormant far away
// (a level with no live hostile is won on its first tick). Run on the
// server, then on the display with the same ids.
std::uint32_t strip_level(GameWorld& w, std::uint32_t keep,
                          unsigned char own_team, std::uint32_t sentinel_id)
{
    std::vector<walker*> doomed;
    for (auto& up : w.oblist)
    {
        walker* const a = up.get();
        if (a == nullptr || a->entity_id() == keep)
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

// Gladiator level 1 with one level-10 ghost hero and the New Specials
// setting on, stripped to the hero. Returns its id, 0 on failure.
std::uint32_t load_ghost_hero()
{
    screen* const s = og::runtime::current_session->myscreen_;
    s->save_data.reset();
    s->save_data.current_campaign = "gladiator";
    s->save_data.current_levels[s->save_data.current_campaign] = 1;
    s->save_data.scen_num = 1;
    s->save_data.numplayers = 1;
    auto g = std::make_unique<guy>(FAMILY_GHOST);
    g->upgrade_to_level(10);
    g->teamnum = 0;
    s->save_data.team_list[0] = std::move(g);
    s->save_data.team_size = 1;
    if (!s->save_data.save("save0"))
        return 0;
    s->save_data.new_specials = 1;
    s->world().rng_.state_ = 0x71e3d00du;
    std::srand(0x71e3d00du);
    glad_init();
    screen* const server = server_screen();
    if (server == nullptr || server->world().new_specials != 1)
        return 0;
    server->world().rng_.state_ = 0x71e3d00eu;
    walker* hero = nullptr;
    for (auto& up : server->world().oblist)
        if (walker* a = up.get(); a != nullptr && !a->dead() &&
            a->query_order() == Order::Living && a->user() != -1)
            hero = a;
    if (hero == nullptr || hero->family() != FAMILY_GHOST)
        return 0;
    const std::uint32_t id = hero->entity_id();
    const std::uint32_t sentinel =
        strip_level(server->world(), id, hero->team_num(), 0);
    strip_level(s->world(), id, hero->team_num(), sentinel);
    return id;
}

void finish_scene()
{
    og::runtime::clear_local_transport_shadow(*og::runtime::current_game_session);
    screen* const s = og::runtime::current_session->myscreen_;
    s->world().end = 0;
    s->world().delete_objects();
}

std::string label_of(const og::sim::SeatTimer& t)
{
    return t.label != nullptr ? std::string(t.label) : std::string("(none)");
}

}  // namespace

// RED (run by hand): seat_timer reading `control.possess_link() == 0` for
// the POSSESS candidate -> the display (and the server) answer nothing for
// the possessed orc, and the HUD trace has no POSSESS cell.
TEST(TimedEffectsMirror, the_display_shows_the_servers_countdown)
{
    const std::uint32_t ghost_id = load_ghost_hero();
    ASSERT_NE(0u, ghost_id) << "a level-10 ghost seated on gladiator 1";
    screen* const s = og::runtime::current_session->myscreen_;
    screen* const server = server_screen();
    ASSERT_NE(nullptr, server);
    GameWorld& display = s->world();
    ASSERT_NE(&display, &server->world())
        << "the display is a copy fed by snapshots, not the server's world";

    std::uint32_t orc_id = 0;
    {
        ServerContext ctx(*server);
        walker* const ghost = server->world().find_by_id(ghost_id);
        ASSERT_NE(nullptr, ghost);
        walker* const orc = server->world().add_ob(Order::Living, FAMILY_ORC);
        ASSERT_NE(nullptr, orc);
        orc->set_team_num(1);
        orc->setxy(static_cast<short>(ghost->xpos() + 24), ghost->ypos());
        orc->set_act_type(ACT_GUARD);
        orc_id = orc->entity_id();
    }
    drive_tick(); // the display learns of the orc
    {
        ServerContext ctx(*server);
        walker* const ghost = server->world().find_by_id(ghost_id);
        walker* const orc = server->world().find_by_id(orc_id);
        ASSERT_TRUE(ghost != nullptr && orc != nullptr);
        ASSERT_TRUE(og::sim::possess(server->world(), *ghost, *orc, 580).ok);
    }
    for (int t = 0; t < 4 && s->viewob[0]->control != nullptr &&
                    s->viewob[0]->control->entity_id() != orc_id;
         ++t)
        drive_tick();
    walker* const control = s->viewob[0]->control;
    ASSERT_NE(nullptr, control);
    ASSERT_EQ(orc_id, control->entity_id()) << "the seat drives the orc";

    const walker* const s_orc = server->world().find_by_id(orc_id);
    ASSERT_NE(nullptr, s_orc);
    const og::sim::SeatTimer on_server = og::sim::seat_timer(server->world(), *s_orc);
    const og::sim::SeatTimer on_display = og::sim::seat_timer(display, *control);
    EXPECT_EQ("POSSESS", label_of(on_server));
    EXPECT_EQ("POSSESS", label_of(on_display));
    EXPECT_GT(on_display.ticks, 560) << "the clock runs from 580";
    EXPECT_LE(std::abs(on_display.ticks - on_server.ticks), 1)
        << "the display's clock is the server's";

    trace_clear();
    new_score_panel(s, 1);
    EXPECT_TRUE(trace_contains("hud", "seat_timer label=POSSESS"))
        << "the display's HUD draws the cell";

    // Back in the ghost, which PHASES on the server: the veil's clock
    // reaches the display the same way.
    drive_tick(switch_char_input());
    for (int t = 0; t < 3 && s->viewob[0]->control != nullptr &&
                    s->viewob[0]->control->entity_id() != ghost_id;
         ++t)
        drive_tick();
    ASSERT_NE(nullptr, s->viewob[0]->control);
    ASSERT_EQ(ghost_id, s->viewob[0]->control->entity_id());
    {
        ServerContext ctx(*server);
        walker* const ghost = server->world().find_by_id(ghost_id);
        ASSERT_NE(nullptr, ghost);
        ghost->stats()->set_max_magicpoints(500.0f);
        ghost->stats()->set_magicpoints(500.0f);
        ghost->set_current_special(4);
        ghost->set_shifter_down(0);
        std::string reason;
        ASSERT_TRUE(ghost->special(nullptr, &reason)) << "PHASE: " << reason;
    }
    drive_tick();
    drive_tick();
    const walker* const s_ghost = server->world().find_by_id(ghost_id);
    ASSERT_NE(nullptr, s_ghost);
    const og::sim::SeatTimer phase_server =
        og::sim::seat_timer(server->world(), *s_ghost);
    const og::sim::SeatTimer phase_display =
        og::sim::seat_timer(display, *s->viewob[0]->control);
    EXPECT_EQ("PHASE", label_of(phase_server));
    EXPECT_EQ("PHASE", label_of(phase_display));
    EXPECT_GT(phase_display.ticks, 0);
    EXPECT_LE(std::abs(phase_display.ticks - phase_server.ticks), 1);

    trace_clear();
    new_score_panel(s, 1);
    EXPECT_TRUE(trace_contains("hud", "seat_timer label=PHASE"));

    finish_scene();
}
