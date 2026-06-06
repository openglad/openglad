#include <openglad/gameplay/game_client.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/gloader.h>
#include <gtest/gtest.h>

#include <utility>
#include <vector>

extern cfg_store cfg;

namespace {

class MockTransport final : public og::sim::ITransport
{
public:
    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        sent_messages_.push_back(
            {peer_id, std::vector<std::uint8_t>(data, data + len)});
    }

    std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> drained =
            std::move(received_messages_);
        received_messages_.clear();
        return drained;
    }

    void accept_connections() override {}

    void disconnect(og::sim::PeerId /*peer_id*/) override {}

    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return {};
    }

    void queue_received(og::sim::PeerId peer_id, std::vector<std::uint8_t> data)
    {
        received_messages_.push_back({peer_id, std::move(data)});
    }

private:
    std::vector<og::sim::ReceivedMessage> received_messages_;
    std::vector<og::sim::ReceivedMessage> sent_messages_;
};

class ScreenInterpolationContextGuard
{
public:
    explicit ScreenInterpolationContextGuard(screen& game_screen)
        : screen_(game_screen)
        , previous_client_(game_screen.render_interpolation_client())
        , previous_speed_factor_(
              game_screen.render_interpolation_speed_factor())
    {
    }

    ~ScreenInterpolationContextGuard()
    {
        screen_.set_render_interpolation_client(previous_client_);
        screen_.set_render_interpolation_speed_factor(previous_speed_factor_);
    }

    void set(const og::sim::GameClient* client, float speed_factor) noexcept
    {
        screen_.set_render_interpolation_client(client);
        screen_.set_render_interpolation_speed_factor(speed_factor);
    }

    ScreenInterpolationContextGuard(const ScreenInterpolationContextGuard&) =
        delete;
    ScreenInterpolationContextGuard& operator=(
        const ScreenInterpolationContextGuard&) = delete;

private:
    screen& screen_;
    const og::sim::GameClient* previous_client_ = nullptr;
    float previous_speed_factor_ = 1.0f;
};

class GameplayActiveGuard
{
public:
    GameplayActiveGuard()
        : previous_(og::runtime::current_session->gameplay_active_)
    {
        og::runtime::current_session->gameplay_active_ = true;
    }

    ~GameplayActiveGuard()
    {
        og::runtime::current_session->gameplay_active_ = previous_;
    }

private:
    bool previous_ = false;
};

void prepare_view_world()
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    active->world().create_new_grid();
    active->world().delete_objects();
    active->world().clear_removed_entity_ids();
    active->world().tick_count_ = 0u;
    active->world().mysmoother.set_target(active->world().grid);
}

void sync_client_to_world(og::sim::GameClient& client,
                          MockTransport& transport,
                          og::sim::PeerId peer_id,
                          GameWorld& world,
                          bool delta)
{
    const og::sim::WorldSnapshot snapshot =
        delta ? og::sim::capture_snapshot(world)
              : og::sim::capture_keyframe_snapshot(world);
    transport.queue_received(peer_id,
                             delta ? og::sim::serialize_delta(snapshot)
                                   : og::sim::serialize_snapshot(snapshot));
    client.poll_messages();
}

} // namespace

static walker* make_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(100, 100);
    return w.release();
}

// ---------------------------------------------------------------------------
// viewscreen::redraw(LevelRuntimeData*, bool) - the big grid rendering function
// ---------------------------------------------------------------------------

TEST(ViewRedraw, with_level_data)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);

    bool result = vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
    ASSERT_TRUE(result) << "redraw with level data should succeed";
}


TEST(ViewRedraw, with_control)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    vs->control = w;
    bool result = vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
    ASSERT_TRUE(result) << "redraw with control should succeed";
    vs->control = nullptr;

}

TEST(ViewRedraw, resolve_walker_render_position_uses_interpolated_snapshot_state)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    walker* const actor = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setxy(32, 48);
    active->world().tick_count_ = 1u;

    MockTransport transport;
    constexpr og::sim::PeerId kPeerId = 7u;
    og::sim::GameClient client(transport, kPeerId);
    sync_client_to_world(client, transport, kPeerId, active->world(), false);

    actor->setxy(80, 96);
    active->world().tick_count_ = 2u;
    sync_client_to_world(client, transport, kPeerId, active->world(), true);

    ScreenInterpolationContextGuard interpolation_guard(*active);
    interpolation_guard.set(&client, 1.0f);
    const WalkerRenderPosition draw_pos =
        resolve_walker_render_position(*actor, 0.5f);

    EXPECT_FLOAT_EQ(56.0f, draw_pos.worldx);
    EXPECT_FLOAT_EQ(72.0f, draw_pos.worldy);
    EXPECT_FLOAT_EQ(56.0f, draw_pos.xpos);
    EXPECT_FLOAT_EQ(72.0f, draw_pos.ypos);
}

TEST(ViewRedraw,
     resolve_walker_render_position_keeps_local_render_anchors_on_world_position)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    walker* const actor = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setworldxy(32.25f, 48.75f);

    active->set_render_interpolation_client(nullptr);
    const WalkerRenderPosition draw_pos =
        resolve_walker_render_position(*actor, 1.0f);

    EXPECT_FLOAT_EQ(32.25f, draw_pos.worldx);
    EXPECT_FLOAT_EQ(48.75f, draw_pos.worldy);
    EXPECT_FLOAT_EQ(32.25f, draw_pos.xpos);
    EXPECT_FLOAT_EQ(48.75f, draw_pos.ypos);
}

TEST(ViewRedraw, redraw_uses_interpolated_control_position_for_camera_follow)
{
    viewscreen* const vs =
        og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    walker* const control =
        active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(160, 120);
    active->world().tick_count_ = 1u;

    MockTransport transport;
    constexpr og::sim::PeerId kPeerId = 7u;
    og::sim::GameClient client(transport, kPeerId);
    sync_client_to_world(client, transport, kPeerId, active->world(), false);

    control->setxy(224, 168);
    active->world().tick_count_ = 2u;
    sync_client_to_world(client, transport, kPeerId, active->world(), true);

    ScreenInterpolationContextGuard interpolation_guard(*active);
    interpolation_guard.set(&client, 1.0f);
    client.testing_set_render_interpolation_elapsed_ms(41.0f);

    vs->control = control;
    const bool result = vs->redraw(&active->level_runtime_data(), false);
    ASSERT_TRUE(result);

    const float expected_x = 192.0f;
    const float expected_y = 144.0f;
    const Sint32 expected_topx = static_cast<Sint32>(
        expected_x - static_cast<float>(vs->xview - control->sizex()) / 2.0f);
    const Sint32 expected_topy = static_cast<Sint32>(
        expected_y - static_cast<float>(vs->yview - control->sizey()) / 2.0f);
    const Sint32 snapped_topx = static_cast<Sint32>(
        static_cast<float>(control->xpos()) -
        static_cast<float>(vs->xview - control->sizex()) / 2.0f);
    const Sint32 snapped_topy = static_cast<Sint32>(
        static_cast<float>(control->ypos()) -
        static_cast<float>(vs->yview - control->sizey()) / 2.0f);

    EXPECT_NEAR(0.5f, vs->interpolation_alpha, 0.02f);
    EXPECT_EQ(expected_topx, vs->topx);
    EXPECT_EQ(expected_topy, vs->topy);
    EXPECT_NE(snapped_topx, vs->topx);
    EXPECT_NE(snapped_topy, vs->topy);

    vs->control = nullptr;
}

TEST(ViewRedrawJitter, render_sample_preserves_float_camera_while_camera_snaps)
{
    viewscreen* const vs =
        og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    prepare_view_world();
    GameplayActiveGuard gameplay_active;
    og::runtime::reset_runtime_trace_capture_state();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    walker* const control =
        active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setworldxy(160.25f, 120.25f);
    active->world().tick_count_ = 1u;

    MockTransport transport;
    constexpr og::sim::PeerId kPeerId = 7u;
    og::sim::GameClient client(transport, kPeerId);
    sync_client_to_world(client, transport, kPeerId, active->world(), false);

    control->setworldxy(161.25f, 121.25f);
    active->world().tick_count_ = 2u;
    sync_client_to_world(client, transport, kPeerId, active->world(), true);

    ScreenInterpolationContextGuard interpolation_guard(*active);
    interpolation_guard.set(&client, 1.0f);
    client.testing_set_render_interpolation_elapsed_ms(41.0f);

    vs->control = control;
    ASSERT_TRUE(vs->redraw(&active->level_runtime_data(), false));

    const auto sample = og::runtime::latest_runtime_render_sample();
    ASSERT_TRUE(sample.has_value());
    EXPECT_EQ(0, sample->view_index);
    EXPECT_EQ(sample->camera_topx, vs->topx);
    EXPECT_EQ(sample->camera_topy, vs->topy);
    EXPECT_NEAR(vs->interpolation_alpha, sample->interpolation_alpha, 0.02f);
    EXPECT_NE(sample->camera_topx_float, static_cast<float>(sample->camera_topx));
    EXPECT_NE(sample->camera_topy_float, static_cast<float>(sample->camera_topy));
    EXPECT_NEAR(160.75f, sample->control_render_x, 0.02f);
    EXPECT_NEAR(120.75f, sample->control_render_y, 0.02f);

    vs->control = nullptr;
}

TEST(ViewRedrawJitter, render_sample_seq_increments_once_per_primary_redraw)
{
    viewscreen* const vs =
        og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    prepare_view_world();
    GameplayActiveGuard gameplay_active;
    og::runtime::reset_runtime_trace_capture_state();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    walker* const control =
        active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(100, 100);
    vs->control = control;

    ASSERT_TRUE(vs->redraw(&active->level_runtime_data(), false));
    const auto first = og::runtime::latest_runtime_render_sample();
    ASSERT_TRUE(first.has_value());

    const auto stable = og::runtime::latest_runtime_render_sample();
    ASSERT_TRUE(stable.has_value());
    EXPECT_EQ(first->render_sample_seq, stable->render_sample_seq);

    ASSERT_TRUE(vs->redraw(&active->level_runtime_data(), false));
    const auto second = og::runtime::latest_runtime_render_sample();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->render_sample_seq + 1u, second->render_sample_seq);

    vs->control = nullptr;
}


TEST(ViewRedraw, no_control)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);
    og::runtime::current_session->myscreen_->level_visuals_.topx = 50;
    og::runtime::current_session->myscreen_->level_visuals_.topy = 50;

    vs->control = nullptr;
    bool result = vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
    ASSERT_TRUE(result) << "redraw without control uses level data pos";

    og::runtime::current_session->myscreen_->level_visuals_.topx = 0;
    og::runtime::current_session->myscreen_->level_visuals_.topy = 0;
}


TEST(ViewRedraw, negative_pos)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);

    // Force negative topx/topy by positioning control near edge
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(5, 5); // near edge, topx/topy may go negative

    vs->control = w;
    vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
    vs->control = nullptr;

}


// ---------------------------------------------------------------------------
// viewscreen::draw_obs(LevelRuntimeData*)
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_draw_obs_with_level_data)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    vs->draw_obs(&og::runtime::current_session->myscreen_->level_runtime_data());
}


TEST(ViewRedraw, view_draw_obs_with_entities)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();

    // Add a living entity
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    if (!w) return;
    w->setxy(100, 100);

    vs->draw_obs(&og::runtime::current_session->myscreen_->level_runtime_data());

    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


// ---------------------------------------------------------------------------
// viewscreen::clear_text
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_clear_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("Some text", 30);
    vs->clear_text();
    // Exercise the clear_text code path
}


// ---------------------------------------------------------------------------
// viewscreen::shift_text
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_shift_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("First message", 30);
    vs->shift_text(0);
}


// ---------------------------------------------------------------------------
// viewscreen::display_text
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_display_text_with_cycles)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("Display me", 5);
    vs->display_text();
    // Smoke-test the text draw path with a finite-duration message.
}

TEST(ViewRedraw, view_refresh_display_text_refreshes_matching_overlay_within_same_tick)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;

    active->world().tick_count_ = 41u;
    vs->clear_text();
    vs->refresh_display_text("PAUSED", 1);

    vs->display_text();
    ASSERT_EQ(std::string("PAUSED"), vs->textlist[0]);
    ASSERT_TRUE(vs->textlist[1].empty());

    // Re-issuing the same overlay during a frozen tick should refresh the
    // existing slot instead of queueing a duplicate line.
    vs->refresh_display_text("PAUSED", 1);
    EXPECT_EQ(std::string("PAUSED"), vs->textlist[0]);
    EXPECT_TRUE(vs->textlist[1].empty());

    // Multiple redraws during the same sim tick should still keep it visible.
    vs->display_text();
    EXPECT_EQ(std::string("PAUSED"), vs->textlist[0]);
    EXPECT_TRUE(vs->textlist[1].empty());

    active->world().tick_count_ = 42u;
    vs->display_text();
    EXPECT_TRUE(vs->textlist[0].empty());

    active->world().tick_count_ = saved_tick;
    vs->clear_text();
}

TEST(ViewRedraw, damage_numbers_advance_once_per_sim_tick)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;

    prepare_view_world();
    walker* const w = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);

    const std::string previous_damage_numbers =
        cfg.get_setting("effects", "damage_numbers");
    cfg.apply_setting("effects", "damage_numbers", "on");

    vs->control = w;
    active->world().tick_count_ = 50u;
    w->damage_numbers.emplace_back(
        static_cast<float>(w->xpos()),
        static_cast<float>(w->ypos()),
        12.0f,
        RED,
        active->world().tick_count_);

    const float initial_t = w->damage_numbers.front().t;
    const float initial_y = w->damage_numbers.front().y;

    (void)draw_walker(*w, vs);
    ASSERT_FALSE(w->damage_numbers.empty());

    const float after_first_draw_t = w->damage_numbers.front().t;
    const float after_first_draw_y = w->damage_numbers.front().y;
    EXPECT_LT(after_first_draw_t, initial_t);
    EXPECT_LT(after_first_draw_y, initial_y);

    (void)draw_walker(*w, vs);
    ASSERT_FALSE(w->damage_numbers.empty());
    EXPECT_FLOAT_EQ(after_first_draw_t, w->damage_numbers.front().t);
    EXPECT_FLOAT_EQ(after_first_draw_y, w->damage_numbers.front().y);

    active->world().tick_count_ = 51u;
    (void)draw_walker(*w, vs);
    ASSERT_FALSE(w->damage_numbers.empty());
    EXPECT_LT(w->damage_numbers.front().t, after_first_draw_t);
    EXPECT_LT(w->damage_numbers.front().y, after_first_draw_y);

    vs->control = nullptr;
    active->world().tick_count_ = saved_tick;
    cfg.apply_setting(
        "effects",
        "damage_numbers",
        previous_damage_numbers.empty() ? "off" : previous_damage_numbers);
}

TEST(ViewRedraw, damage_numbers_catch_up_across_batched_sim_ticks)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;

    prepare_view_world();
    walker* const w = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);

    const std::string previous_damage_numbers =
        cfg.get_setting("effects", "damage_numbers");
    cfg.apply_setting("effects", "damage_numbers", "on");

    vs->control = w;
    active->world().tick_count_ = 50u;
    w->damage_numbers.emplace_back(
        static_cast<float>(w->xpos()),
        static_cast<float>(w->ypos()),
        12.0f,
        RED,
        active->world().tick_count_);

    (void)draw_walker(*w, vs);
    ASSERT_FALSE(w->damage_numbers.empty());

    const float after_first_draw_t = w->damage_numbers.front().t;
    const float after_first_draw_y = w->damage_numbers.front().y;

    active->world().tick_count_ = 53u;
    (void)draw_walker(*w, vs);
    ASSERT_FALSE(w->damage_numbers.empty());

    EXPECT_FLOAT_EQ(after_first_draw_t - 0.15f, w->damage_numbers.front().t);
    EXPECT_FLOAT_EQ(after_first_draw_y - 4.5f, w->damage_numbers.front().y);

    vs->control = nullptr;
    active->world().tick_count_ = saved_tick;
    cfg.apply_setting(
        "effects",
        "damage_numbers",
        previous_damage_numbers.empty() ? "off" : previous_damage_numbers);
}

TEST(ViewRedraw, damage_numbers_catch_up_on_first_draw_after_batched_sim_ticks)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;

    prepare_view_world();
    walker* const w = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);

    const std::string previous_damage_numbers =
        cfg.get_setting("effects", "damage_numbers");
    cfg.apply_setting("effects", "damage_numbers", "on");

    vs->control = w;
    active->world().tick_count_ = 50u;
    w->damage_numbers.emplace_back(
        static_cast<float>(w->xpos()),
        static_cast<float>(w->ypos()),
        12.0f,
        RED,
        active->world().tick_count_);

    const float initial_t = w->damage_numbers.front().t;
    const float initial_y = w->damage_numbers.front().y;

    active->world().tick_count_ = 53u;
    (void)draw_walker(*w, vs);
    ASSERT_FALSE(w->damage_numbers.empty());

    EXPECT_FLOAT_EQ(initial_t - 0.20f, w->damage_numbers.front().t);
    EXPECT_FLOAT_EQ(initial_y - 6.0f, w->damage_numbers.front().y);

    vs->control = nullptr;
    active->world().tick_count_ = saved_tick;
    cfg.apply_setting(
        "effects",
        "damage_numbers",
        previous_damage_numbers.empty() ? "off" : previous_damage_numbers);
}

TEST(ViewRedraw, damage_number_cache_prunes_removed_walkers)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;

    prepare_view_world();

    const std::string previous_damage_numbers =
        cfg.get_setting("effects", "damage_numbers");
    cfg.apply_setting("effects", "damage_numbers", "on");

    walker* const first = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, first);
    vs->control = first;
    active->world().tick_count_ = 60u;
    first->damage_numbers.emplace_back(
        static_cast<float>(first->xpos()),
        static_cast<float>(first->ypos()),
        12.0f,
        RED,
        active->world().tick_count_);

    (void)draw_walker(*first, vs);
    EXPECT_EQ(1u, damage_number_render_state_count(active));

    const std::uint32_t first_entity_id = first->entity_id();
    ASSERT_NE(0u, first_entity_id);
    ASSERT_TRUE(active->world().remove_ob(first));
    EXPECT_EQ(nullptr, active->world().find_by_id(first_entity_id));

    walker* const second = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, second);
    vs->control = second;
    active->world().tick_count_ = 61u;
    second->damage_numbers.emplace_back(
        static_cast<float>(second->xpos()),
        static_cast<float>(second->ypos()),
        21.0f,
        RED,
        active->world().tick_count_);

    const float initial_t = second->damage_numbers.front().t;
    (void)draw_walker(*second, vs);

    ASSERT_FALSE(second->damage_numbers.empty());
    EXPECT_LT(second->damage_numbers.front().t, initial_t);
    EXPECT_EQ(1u, damage_number_render_state_count(active));

    vs->control = nullptr;
    active->world().tick_count_ = saved_tick;
    cfg.apply_setting(
        "effects",
        "damage_numbers",
        previous_damage_numbers.empty() ? "off" : previous_damage_numbers);
}

TEST(ViewRedraw, hit_flash_persists_across_repeated_redraws_in_same_tick)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    active->world().create_new_grid();
    active->world().mysmoother.set_target(active->world().grid);

    std::unique_ptr<walker> w(make_guy(FAMILY_SOLDIER, 0));
    ASSERT_NE(nullptr, w);

    const std::string previous_hit_flash =
        cfg.get_setting("effects", "hit_flash");
    cfg.apply_setting("effects", "hit_flash", "on");

    w->set_hurt_flash(true);
    vs->control = w.get();

    (void)draw_walker(*w, vs);
    EXPECT_TRUE(w->hurt_flash());

    (void)draw_walker(*w, vs);
    EXPECT_TRUE(w->hurt_flash());

    vs->control = nullptr;
    cfg.apply_setting(
        "effects",
        "hit_flash",
        previous_hit_flash.empty() ? "off" : previous_hit_flash);
}


// ---------------------------------------------------------------------------
// viewscreen::change_gamma
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_change_gamma)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    Sint32 g0 = vs->change_gamma(0);
    Sint32 g1 = vs->change_gamma(1);
    Sint32 g2 = vs->change_gamma(-1);
    (void)g0; (void)g1; (void)g2;
}
