#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/gloader.h>
#include <gtest/gtest.h>

#include <utility>
#include <vector>

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
    // textcycles should decrement
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
