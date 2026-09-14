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
#include <openglad/gameplay/game_world.h>
#include <openglad/core/pixdefs.h>
#include <openglad/platform/sai2x.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
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
    active->set_active_canvas(CanvasTarget::World);
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

// The palette indices inside one view's rect, in row-major order: the
// read-back oracle for "this draw landed in this view".
std::vector<int> capture_view_rect(screen& scr, const viewscreen& vs)
{
    std::vector<int> pixels;
    pixels.reserve(static_cast<std::size_t>(vs.xview) *
                   static_cast<std::size_t>(vs.yview));
    for (Sint32 y = vs.yloc; y < vs.endy; ++y)
    {
        for (Sint32 x = vs.xloc; x < vs.endx; ++x)
        {
            int color_index = 0;
            scr.get_pixel(x, y, &color_index);
            pixels.push_back(color_index);
        }
    }
    return pixels;
}

struct RectBox
{
    Sint32 min_x = 0;
    Sint32 min_y = 0;
    Sint32 max_x = -1;
    Sint32 max_y = -1;

    bool empty() const { return max_x < min_x; }
};

// Bounding box (in screen coordinates) of every pixel that differs between a
// baseline capture and a capture taken after the draw under test.
RectBox changed_box(const std::vector<int>& before,
                    const std::vector<int>& after,
                    const viewscreen& vs)
{
    RectBox box;
    box.min_x = vs.endx;
    box.min_y = vs.endy;
    box.max_x = -1;
    box.max_y = -1;
    if (before.size() != after.size())
        return box;
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        if (before[i] == after[i])
            continue;
        const Sint32 x =
            vs.xloc + static_cast<Sint32>(i % static_cast<std::size_t>(vs.xview));
        const Sint32 y =
            vs.yloc + static_cast<Sint32>(i / static_cast<std::size_t>(vs.xview));
        box.min_x = std::min(box.min_x, x);
        box.max_x = std::max(box.max_x, x);
        box.min_y = std::min(box.min_y, y);
        box.max_y = std::max(box.max_y, y);
    }
    return box;
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

// redraw()'s refusal contract (view.cpp:1025-1043): no level data, or a live
// world aimed at a fixed UI canvas, is refused outright; the World canvas with
// a level renderer behind it proceeds.
TEST(ViewRedraw, redraw_refuses_null_data_and_a_fixed_ui_canvas)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    EXPECT_FALSE(vs->redraw(nullptr, false))
        << "redraw must refuse a null LevelRuntimeData";

    ASSERT_FALSE(active->native_world_view_active())
        << "no native world view here, so the UI canvas is a fixed one";
    active->set_active_canvas(CanvasTarget::UI);
    EXPECT_FALSE(vs->redraw(&active->level_runtime_data(), false))
        << "redraw must refuse to rasterize a live world into a fixed UI canvas";

    active->set_active_canvas(CanvasTarget::World);
    EXPECT_TRUE(vs->redraw(&active->level_runtime_data(), false))
        << "World canvas plus a level renderer: redraw proceeds";
}


// The un-interpolated twin of
// redraw_uses_interpolated_control_position_for_camera_follow: a control that
// really is in the world's lists survives sanitize_control_pointer, and the
// camera centres on it (view.cpp:1050-1068).
TEST(ViewRedraw, redraw_centers_the_camera_on_a_live_control)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    walker* const w = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "the control must exist in oblist";
    w->setxy(100, 100);

    ScreenInterpolationContextGuard interpolation_guard(*active);
    interpolation_guard.set(nullptr, 1.0f); // local path, alpha 1

    vs->control = w;
    ASSERT_TRUE(vs->redraw(&active->level_runtime_data(), false))
        << "redraw with a live control should succeed";

    EXPECT_EQ(w, vs->control)
        << "a control present in oblist survives sanitize_control_pointer";
    EXPECT_EQ(static_cast<Sint32>(
                  100.0f - static_cast<float>(vs->xview - w->sizex()) / 2.0f),
              vs->topx)
        << "the camera centres horizontally on the control";
    EXPECT_EQ(static_cast<Sint32>(
                  100.0f - static_cast<float>(vs->yview - w->sizey()) / 2.0f),
              vs->topy)
        << "the camera centres vertically on the control";

    vs->control = nullptr;
    ASSERT_TRUE(active->world().remove_ob(w));
}

// Z-axis: a 3-floor world with the control on floor 1 exercises the multi-floor
// render path — floor 0 faded below, floor 1 the opaque camera floor, floor 2 a
// ghost above — i.e. per-floor alpha tiles + alpha entity sprites + air holes.
TEST(ViewRedraw, multifloor_renders_faded_below_and_ghost_above)
{
    prepare_view_world();

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.mysmoother.set_target(world.grid);

    world.set_floor_count(3);
    const int gw = world.grid.w;
    const int gh = world.grid.h;
    for (int f = 1; f < 3; ++f)
    {
        auto* buf = new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
        std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh),
                  static_cast<unsigned char>(PIX_GRASS1));
        buf[5 + 5 * gw] = static_cast<unsigned char>(PIX_AIR); // hole reveals below
        world.grid_for_floor(f) = PixieData(1, static_cast<unsigned char>(gw),
                                            static_cast<unsigned char>(gh), buf);
        world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
    }

    walker* mid = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* below = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* above = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, mid) << "the camera-floor walker must exist";
    ASSERT_NE(nullptr, below) << "the lower-floor walker must exist";
    ASSERT_NE(nullptr, above) << "the upper-floor walker must exist";
    mid->set_floor(1); mid->setxy(160, 120);
    below->set_floor(0); below->setxy(80, 80);
    above->set_floor(2); above->setxy(110, 110);

    {
        vs->control = mid;

        bool ok = vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
        EXPECT_TRUE(ok) << "multi-floor redraw should succeed";
        EXPECT_EQ(1, vs->current_floor_) << "camera follows the control's floor";

        // floor_render_alpha no longer reads the look-up hold at all: the
        // floor below ALWAYS fades (regression pin — air holes must show a
        // depth-faded lower floor in normal play), the camera floor stays
        // opaque, and floors above report the ghost alpha they'd composite
        // at (whether they draw is the redraw loop's floor_top gate on
        // ghost_hold_override_). Drive the flag both ways to prove it.
        vs->ghost_hold_override_ = true;
        EXPECT_EQ(255, vs->floor_render_alpha(1)) << "camera floor opaque";
        EXPECT_LT(vs->floor_render_alpha(0), 255) << "floor below faded";
        EXPECT_LT(vs->floor_render_alpha(2), 255) << "floor above ghosted";

        // Hold released: identical alphas — the released frame simply never
        // iterates floors above the camera (and adds the upper-floor shadow
        // pass instead). Exercise that path too.
        vs->ghost_hold_override_ = false;
        EXPECT_EQ(255 - viewscreen::kFloorBelowAlphaStep,
                  vs->floor_render_alpha(0))
            << "the below-floor fade must not need the look-up hold";
        EXPECT_EQ(viewscreen::kFloorGhostAlpha, vs->floor_render_alpha(2));
        EXPECT_TRUE(vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false));

        vs->control = nullptr;
    }

    // Restore single-floor shared state for the other tests in this binary.
    world.delete_objects();
    world.set_floor_count(1);
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

// The per-level weather kind reaches a client world through the same
// snapshot-apply path every other world field uses: capture the rolled
// server state, wipe the local kind (an un-synced mirror), and let the
// GameClient apply the received keyframe back into the render world.
TEST(ViewRedraw, weather_kind_syncs_to_client_world_through_snapshot_apply)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    GameWorld& world = active->world();
    world.tick_count_ = 1u;

    // Authoritative roll (level id 7 pins Rain under the default-0 nonce —
    // see tests/unit/test_weather.cpp).
    const int saved_id = world.id;
    world.id = 7;
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    ASSERT_EQ(WeatherKind::Rain, world.weather());

    const og::sim::WorldSnapshot keyframe =
        og::sim::capture_keyframe_snapshot(world);
    EXPECT_EQ(static_cast<std::uint8_t>(WeatherKind::Rain), keyframe.weather);

    // Play the un-synced mirror: the kind arrives only via the snapshot.
    world.set_weather(WeatherKind::None);

    MockTransport transport;
    constexpr og::sim::PeerId kPeerId = 7u;
    // Bind the client to the render world (the display-client wiring): only
    // a world-bound client applies received snapshots to it.
    og::sim::GameClient client(transport, kPeerId, &world);
    transport.queue_received(kPeerId, og::sim::serialize_snapshot(keyframe));
    client.poll_messages();

    EXPECT_EQ(WeatherKind::Rain, world.weather())
        << "the snapshot-apply path must install the server's rolled kind";

    world.set_weather(WeatherKind::None);
    world.id = saved_id;
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

TEST(ViewRedraw,
     resolve_walker_render_position_falls_back_when_client_has_no_entity_state)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    walker* const actor = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(0u, actor->entity_id());
    actor->setworldxy(37.25f, 58.75f);

    MockTransport transport;
    constexpr og::sim::PeerId kPeerId = 7u;
    og::sim::GameClient empty_client(transport, kPeerId);
    ScreenInterpolationContextGuard interpolation_guard(*active);
    interpolation_guard.set(&empty_client, 1.0f);
    GameplayActiveGuard gameplay_active;
    vs->control = actor;

    const bool previous_trace_enabled = og::runtime::runtime_trace_enabled();
    og::runtime::set_runtime_trace_enabled(true);
    og::runtime::clear_runtime_trace();

    const WalkerRenderPosition draw_pos =
        resolve_walker_render_position(*actor, 0.25f);
    const std::vector<og::runtime::RuntimeTraceRecord> records =
        og::runtime::copy_runtime_trace();
    const auto fallback_trace = std::find_if(
        records.begin(),
        records.end(),
        [](const og::runtime::RuntimeTraceRecord& record) {
            return record.category == "render" &&
                record.event == "resolve_control_render_position_fallback";
        });

    og::runtime::clear_runtime_trace();
    og::runtime::set_runtime_trace_enabled(previous_trace_enabled);
    vs->control = nullptr;

    EXPECT_FLOAT_EQ(37.25f, draw_pos.worldx);
    EXPECT_FLOAT_EQ(58.75f, draw_pos.worldy);
    EXPECT_FLOAT_EQ(37.25f, draw_pos.xpos);
    EXPECT_FLOAT_EQ(58.75f, draw_pos.ypos);
    ASSERT_NE(records.end(), fallback_trace);
    EXPECT_NE(0u, fallback_trace->trace_seq);
    EXPECT_EQ(1u,
              std::count_if(
                  records.begin(), records.end(),
                  [](const og::runtime::RuntimeTraceRecord& record) {
                      return record.category == "render" &&
                          record.event ==
                          "resolve_control_render_position_fallback";
                  }));
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

    // The elapsed-time override lands near, but not necessarily at, exactly
    // half a simulation tick.  Derive the camera sample from the alpha that
    // redraw actually resolved instead of baking in the 0.5 midpoint.
    const float expected_x = 160.0f + (224.0f - 160.0f) * vs->interpolation_alpha;
    const float expected_y = 120.0f + (168.0f - 120.0f) * vs->interpolation_alpha;
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

TEST(ViewRedraw, classic_respawn_countdown_focuses_queued_spawn)
{
    viewscreen* const vs =
        og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    GameWorld& world = active->world();
    walker* const control =
        world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);

    control->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    control->setxy(480, 400);
    control->set_floor(0);
    control->set_dead(1);

    // Give the queued destination a real second floor so the camera-floor
    // assertion covers cross-floor classic spawns as well as x/y.
    world.set_floor_count(2);
    const int gw = world.grid.w;
    const int gh = world.grid.h;
    auto* floor_one =
        new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
    std::fill(floor_one,
              floor_one + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh),
              static_cast<unsigned char>(PIX_GRASS1));
    world.grid_for_floor(1) =
        PixieData(1, static_cast<unsigned char>(gw),
                  static_cast<unsigned char>(gh), floor_one);
    world.smoother_for_floor(1).set_target(world.grid_for_floor(1));

    const short saved_respawn_mode = world.respawn_mode;
    const char saved_type = world.type;
    world.respawn_mode = og::sim::kRespawnModeHeroes;
    world.type = 0;
    world.respawn = {};
    world.respawn.respawn_ticks = 120;

    og::sim::RespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 60;
    entry.walker_entity_id = control->entity_id();
    entry.x = 160;
    entry.y = 176;
    entry.floor = 1;
    world.respawn.respawn_queue.push_back(entry);
    vs->control = control;

    ASSERT_TRUE(vs->redraw(&active->level_runtime_data(), false));
    const Sint32 spawn_topx =
        entry.x - (vs->xview - control->sizex()) / 2;
    const Sint32 spawn_topy =
        entry.y - (vs->yview - control->sizey()) / 2;
    EXPECT_EQ(spawn_topx, vs->topx);
    EXPECT_EQ(spawn_topy, vs->topy);
    EXPECT_EQ(1, vs->current_floor_);

    // (The CTF-hint exclusion sub-case retired with the CTF engine: every
    // surviving fire path writes honest entry x/y, and the scripted-entry
    // camera focus is pinned by test_mode_ui.)

    vs->control = nullptr;
    world.delete_objects();
    world.create_new_grid();
    world.mysmoother.set_target(world.grid);
    world.respawn_mode = saved_respawn_mode;
    world.type = saved_type;
    // The second floor this test added belongs to this test: the shared
    // session world has to go back to single-floor, or every later test that
    // asserts a single-floor fixture depends on running first.
    world.set_floor_count(1);
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

TEST(ViewRedrawJitter, no_control_render_sample_reports_zero_control_coordinates)
{
    prepare_view_world();
    GameplayActiveGuard gameplay_active;
    og::runtime::reset_runtime_trace_capture_state();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    vs->control = nullptr;

    ASSERT_TRUE(vs->redraw(&active->level_runtime_data(), false));
    const auto sample = og::runtime::latest_runtime_render_sample();
    ASSERT_TRUE(sample.has_value());
    EXPECT_EQ(0, sample->view_index);
    EXPECT_FLOAT_EQ(0.0f, sample->control_worldx);
    EXPECT_FLOAT_EQ(0.0f, sample->control_worldy);
    EXPECT_FLOAT_EQ(0.0f, sample->control_render_x);
    EXPECT_FLOAT_EQ(0.0f, sample->control_render_y);
}


// With no control the camera comes from the level's own stored position
// (view.cpp:1070-1075), and the shake/parallax shifts are undone before
// redraw returns (view.cpp:1279), so the level position is what is left.
TEST(ViewRedraw, no_control_takes_the_camera_from_the_level_position)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    active->level_visuals_.topx = 50;
    active->level_visuals_.topy = 50;
    vs->topx = -999;
    vs->topy = -999;

    vs->control = nullptr;
    ASSERT_TRUE(vs->redraw(&active->level_runtime_data(), false))
        << "redraw without a control should succeed";
    EXPECT_EQ(50, vs->topx)
        << "no control: topx comes from the level's stored camera position";
    EXPECT_EQ(50, vs->topy)
        << "no control: topy comes from the level's stored camera position";

    active->level_visuals_.topx = 0;
    active->level_visuals_.topy = 0;
}


// A control near the map's top-left corner drives the camera NEGATIVE — the
// case redraw() flags with xneg/yneg and fills with wall tiles. The control
// must be in the world's lists or sanitize_control_pointer drops it and the
// no-control branch runs instead (view.cpp:157-174).
TEST(ViewRedraw, control_near_the_corner_drives_the_camera_negative)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    walker* const w = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "the control must exist in oblist";
    w->setxy(5, 5); // near the corner: the centred camera goes negative

    ScreenInterpolationContextGuard interpolation_guard(*active);
    interpolation_guard.set(nullptr, 1.0f);

    vs->control = w;
    ASSERT_TRUE(vs->redraw(&active->level_runtime_data(), false));
    EXPECT_EQ(w, vs->control) << "a live control is kept";

    const Sint32 expect_topx = static_cast<Sint32>(
        5.0f - static_cast<float>(vs->xview - w->sizex()) / 2.0f);
    const Sint32 expect_topy = static_cast<Sint32>(
        5.0f - static_cast<float>(vs->yview - w->sizey()) / 2.0f);
    EXPECT_EQ(expect_topx, vs->topx) << "camera centres on the control";
    EXPECT_EQ(expect_topy, vs->topy) << "camera centres on the control";
    EXPECT_LT(vs->topx, 0) << "this is the negative-camera case";
    EXPECT_LT(vs->topy, 0) << "this is the negative-camera case";

    vs->control = nullptr;
    ASSERT_TRUE(active->world().remove_ob(w));
}


// ---------------------------------------------------------------------------
// viewscreen::draw_obs(LevelRuntimeData*)
// ---------------------------------------------------------------------------

// refresh() presents THIS view's rect (view.cpp:1356-1360): the window stops
// being black, and the present vouches for (xloc,yloc,xview,yview) only — a
// draw outside that rect stays unpresented. (The no-arg draw_obs() wrapper is
// pinned in draw_obs_draws_the_living_and_skips_the_dead below.)
TEST(ViewRedraw, refresh_presents_only_this_views_rect)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    ASSERT_TRUE(E_Screen != nullptr);

    active->set_active_canvas(CanvasTarget::World);
    // A strict sub-rect of the canvas, so "outside this view" exists at all.
    vs->resize(static_cast<short>(8), static_cast<short>(8),
               static_cast<short>(64), static_cast<short>(64));

    active->clearbuffer();
    active->testing_reset_window_state();
    std::string detail;
    ASSERT_TRUE(E_Screen->testing_render_matches_presented(detail))
        << "reset leaves the canvas counting as presented: " << detail;
    ASSERT_TRUE(active->window_is_black());

    // A mark inside the view's rect: refresh() presents it.
    active->pointb(vs->xloc + 4, vs->yloc + 4, RED);
    ASSERT_FALSE(E_Screen->testing_render_matches_presented(detail))
        << "the mark must make the canvas differ from the presented frame";
    EXPECT_TRUE(vs->refresh());
    EXPECT_FALSE(active->window_is_black())
        << "refresh() presents, so the window no longer shows black";
    EXPECT_TRUE(E_Screen->testing_render_matches_presented(detail))
        << "refresh() must present the rect holding this view's own draw: "
        << detail;

    // A mark outside it: refresh() must NOT vouch for it.
    active->pointb(vs->endx + 4, vs->endy + 4, RED);
    EXPECT_TRUE(vs->refresh());
    EXPECT_FALSE(E_Screen->testing_render_matches_presented(detail))
        << "refresh() must present only (xloc,yloc,xview,yview), not the "
           "whole canvas";

    active->clearbuffer();
    active->relayout_views();
    active->testing_reset_window_state();
}

TEST(ViewRedraw, damage_number_context_erases_one_index_without_touching_siblings)
{
    DamageNumberRenderContext context;
    const DamageNumberRenderSnapshot first{.value = 10.0f};
    const DamageNumberRenderSnapshot second{.value = 20.0f};
    context.prepare_state(42u, 0u, first);
    context.prepare_state(42u, 1u, second);
    ASSERT_EQ(2u, context.state_count());

    context.erase_index(42u, 0u);
    EXPECT_EQ(1u, context.state_count());
    EXPECT_FLOAT_EQ(20.0f, context.prepare_state(42u, 0u, second).snapshot.value);

    context.erase_index(42u, 0u);
    EXPECT_EQ(0u, context.state_count());
}


// draw_obs() draws every non-dead walker in the world's lists through
// draw_walker at worldx - topx + xloc (walker_draw.cpp:948), skips the dead
// (view.cpp:1885-1890), and the no-arg overload runs the same draw against
// active_screen()->level_runtime_data() (view.cpp:1863). Read back off the
// canvas, so a draw_obs that iterated nothing cannot pass.
TEST(ViewRedraw, draw_obs_draws_the_living_skips_the_dead_and_follows_the_camera)
{
    prepare_view_world();

    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    active->set_active_canvas(CanvasTarget::World);

    ScreenInterpolationContextGuard interpolation_guard(*active);
    interpolation_guard.set(nullptr, 1.0f);

    walker* const w = active->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "the walker to draw must exist";
    w->setxy(100, 100);

    vs->topx = 0;
    vs->topy = 0;

    // Baseline: the same call with the walker dead — everything draw_obs does
    // besides this walker (floor effects) is in both captures.
    w->set_dead(1);
    active->clearbuffer();
    ASSERT_TRUE(vs->draw_obs(&active->level_runtime_data()));
    const std::vector<int> without_walker = capture_view_rect(*active, *vs);

    w->set_dead(0);
    active->clearbuffer();
    ASSERT_TRUE(vs->draw_obs(&active->level_runtime_data()));
    const std::vector<int> with_walker = capture_view_rect(*active, *vs);

    ASSERT_NE(without_walker, with_walker)
        << "a living walker must paint pixels a dead one does not";

    const RectBox box = changed_box(without_walker, with_walker, *vs);
    ASSERT_FALSE(box.empty()) << "the sprite must land inside the view";

    // The same walker with the camera moved: the drawn pixels shift by exactly
    // the camera delta, which is the projection rule itself.
    vs->topx = 16;
    vs->topy = 8;
    w->set_dead(1);
    active->clearbuffer();
    ASSERT_TRUE(vs->draw_obs(&active->level_runtime_data()));
    const std::vector<int> shifted_base = capture_view_rect(*active, *vs);
    w->set_dead(0);
    active->clearbuffer();
    ASSERT_TRUE(vs->draw_obs(&active->level_runtime_data()));
    const std::vector<int> shifted = capture_view_rect(*active, *vs);
    const RectBox shifted_box = changed_box(shifted_base, shifted, *vs);
    ASSERT_FALSE(shifted_box.empty()) << "the sprite must still be drawn";
    EXPECT_EQ(box.min_x - 16, shifted_box.min_x)
        << "screen x is worldx - topx + xloc";
    EXPECT_EQ(box.min_y - 8, shifted_box.min_y)
        << "screen y is worldy - topy + yloc";

    // The no-arg wrapper draws the active screen's level: same pixels.
    vs->topx = 0;
    vs->topy = 0;
    active->clearbuffer();
    ASSERT_TRUE(vs->draw_obs());
    EXPECT_EQ(with_walker, capture_view_rect(*active, *vs))
        << "draw_obs() must delegate to active_screen()->level_runtime_data()";

    ASSERT_TRUE(active->world().remove_ob(w));
}


// ---------------------------------------------------------------------------
// viewscreen::clear_text
// ---------------------------------------------------------------------------

// clear_text() blanks EVERY slot on all four axes it owns — label, cycles,
// expiry tick and stamp tick (view.cpp:1632-1641). A stale stamp or expiry
// left behind is what resurrects a swept line on the next display_text().
TEST(ViewRedraw, clear_text_blanks_every_slot_label_cycles_and_ticks)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;
    active->world().tick_count_ = 900u;

    vs->clear_text();
    vs->set_display_text("Some text", 30);
    vs->set_display_text("And another", 7);
    ASSERT_EQ("Some text", vs->textlist[0]);
    ASSERT_EQ(30, static_cast<int>(vs->textcycles[0]));
    ASSERT_EQ(900u, vs->text_stamp_ticks[0]);
    ASSERT_EQ(929u, vs->text_expire_ticks[0]);
    ASSERT_EQ("And another", vs->textlist[1]);
    ASSERT_EQ(7, static_cast<int>(vs->textcycles[1]));

    vs->clear_text();

    for (int i = 0; i < MAX_MESSAGES; ++i)
    {
        EXPECT_TRUE(vs->textlist[i].empty())
            << "clear_text must blank the label of slot " << i;
        EXPECT_EQ(0, static_cast<int>(vs->textcycles[i]))
            << "clear_text must zero the cycles of slot " << i;
        EXPECT_EQ(0u, vs->text_expire_ticks[i])
            << "clear_text must zero the expiry tick of slot " << i;
        EXPECT_EQ(0u, vs->text_stamp_ticks[i])
            << "clear_text must zero the stamp tick of slot " << i;
    }

    active->world().tick_count_ = saved_tick;
}


// ---------------------------------------------------------------------------
// viewscreen::shift_text
// ---------------------------------------------------------------------------

// shift_text(row) copies slots row+1.. down one and blanks the tail slot on
// all four axes (view.cpp:1339-1354). A full feed makes both halves visible:
// the ladder must move, and the vacated tail must not keep the old line.
TEST(ViewRedraw, shift_text_moves_the_feed_up_and_blanks_the_tail_slot)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;
    active->world().tick_count_ = 200u;

    vs->clear_text();
    for (int i = 0; i < MAX_MESSAGES; ++i)
        vs->set_display_text("Line " + std::to_string(i), static_cast<short>(10 + i));
    ASSERT_EQ("Line 0", vs->textlist[0]);
    ASSERT_EQ("Line 4", vs->textlist[MAX_MESSAGES - 1]);

    vs->shift_text(0);

    for (int i = 0; i < MAX_MESSAGES - 1; ++i)
    {
        EXPECT_EQ("Line " + std::to_string(i + 1), vs->textlist[i])
            << "slot " << i << " must hold the line that was below it";
        EXPECT_EQ(11 + i, static_cast<int>(vs->textcycles[i]))
            << "the cycles travel with the line into slot " << i;
        EXPECT_EQ(200u + static_cast<std::uint32_t>(10 + i),
                  vs->text_expire_ticks[i])
            << "the expiry tick travels with the line into slot " << i;
        EXPECT_EQ(200u, vs->text_stamp_ticks[i])
            << "the stamp tick travels with the line into slot " << i;
    }

    EXPECT_TRUE(vs->textlist[MAX_MESSAGES - 1].empty())
        << "the vacated tail slot must be blank";
    EXPECT_EQ(0, static_cast<int>(vs->textcycles[MAX_MESSAGES - 1]))
        << "the vacated tail slot must have no cycles left";
    EXPECT_EQ(0u, vs->text_expire_ticks[MAX_MESSAGES - 1])
        << "the vacated tail slot must have no expiry left";
    EXPECT_EQ(0u, vs->text_stamp_ticks[MAX_MESSAGES - 1])
        << "the vacated tail slot must have no stamp left";

    vs->clear_text();
    active->world().tick_count_ = saved_tick;
}


// ---------------------------------------------------------------------------
// viewscreen::display_text
// ---------------------------------------------------------------------------

// A multi-cycle line lives for exactly n ticks: set_display_text stamps
// expiry at current_tick + n - 1 (view.cpp:1575-1579) and display_text keeps
// the slot while current_tick <= expiry, sweeping it the tick after
// (view.cpp:1306-1337).
TEST(ViewRedraw, display_text_keeps_a_five_cycle_line_until_its_expiry_tick)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;
    active->world().tick_count_ = 100u;

    vs->clear_text();
    vs->set_display_text("Display me", 5);
    EXPECT_EQ(5, static_cast<int>(vs->textcycles[0]));
    EXPECT_EQ(104u, vs->text_expire_ticks[0])
        << "a 5-cycle line stamped at tick 100 expires after tick 104";

    vs->display_text();
    EXPECT_EQ("Display me", vs->textlist[0])
        << "the line must survive its creation tick";

    active->world().tick_count_ = 104u;
    vs->display_text();
    EXPECT_EQ("Display me", vs->textlist[0])
        << "tick 104 is the line's last live tick";

    active->world().tick_count_ = 105u;
    vs->display_text();
    EXPECT_TRUE(vs->textlist[0].empty())
        << "the tick after expiry sweeps the line off the feed";

    vs->clear_text();
    active->world().tick_count_ = saved_tick;
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

TEST(ViewRedraw, zero_cycle_display_text_expires_on_its_creation_tick)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);
    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const std::uint32_t saved_tick = active->world().tick_count_;
    active->world().tick_count_ = 73u;
    vs->clear_text();
    vs->set_display_text("ONE FRAME", 0);

    EXPECT_EQ("ONE FRAME", vs->textlist[0]);
    EXPECT_EQ(0, vs->textcycles[0]);
    EXPECT_EQ(73u, vs->text_expire_ticks[0]);

    active->world().tick_count_ = 74u;
    vs->refresh_display_text("ONE FRAME", 0);
    EXPECT_EQ("ONE FRAME", vs->textlist[0]);
    EXPECT_EQ(0, vs->textcycles[0]);
    EXPECT_EQ(74u, vs->text_expire_ticks[0]);

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

// A hurt-flashing walker is blitted through walkputbuffer_flash instead of
// walkputbuffer (walker_draw.cpp:635-648) whenever cfg effects/hit_flash is
// on, the render path never consumes the flag (sim-owned: see
// scripts/check_render_no_sim_writes.sh), and with the setting off the flag
// changes nothing on the canvas.
TEST(ViewRedraw, hit_flash_brightens_the_blit_only_while_the_setting_is_on)
{
    screen* const active = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, active);

    viewscreen* const vs = active->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    active->world().create_new_grid();
    active->world().mysmoother.set_target(active->world().grid);
    active->set_active_canvas(CanvasTarget::World);

    std::unique_ptr<walker> w(make_guy(FAMILY_SOLDIER, 0));
    ASSERT_NE(nullptr, w);

    const std::string previous_hit_flash =
        cfg.get_setting("effects", "hit_flash");
    cfg.apply_setting("effects", "hit_flash", "on");

    ScreenInterpolationContextGuard interpolation_guard(*active);
    interpolation_guard.set(nullptr, 1.0f);

    const Sint32 saved_topx = vs->topx;
    const Sint32 saved_topy = vs->topy;
    vs->topx = 0;
    vs->topy = 0;
    w->setxy(100, 100);

    w->set_hurt_flash(true);
    vs->control = w.get();

    active->clearbuffer();
    ASSERT_TRUE(draw_walker(*w, vs));
    const std::vector<int> flashed = capture_view_rect(*active, *vs);
    EXPECT_TRUE(w->hurt_flash())
        << "the render path must not consume the sim-owned flash flag";

    active->clearbuffer();
    ASSERT_TRUE(draw_walker(*w, vs));
    EXPECT_EQ(flashed, capture_view_rect(*active, *vs))
        << "a second draw in the same tick flashes identically";
    EXPECT_TRUE(w->hurt_flash())
        << "the render path must not consume the sim-owned flash flag";

    w->set_hurt_flash(false);
    active->clearbuffer();
    ASSERT_TRUE(draw_walker(*w, vs));
    const std::vector<int> plain = capture_view_rect(*active, *vs);
    EXPECT_NE(flashed, plain)
        << "hurt_flash must route the sprite through the brightened blit";

    cfg.apply_setting("effects", "hit_flash", "off");
    w->set_hurt_flash(true);
    active->clearbuffer();
    ASSERT_TRUE(draw_walker(*w, vs));
    EXPECT_EQ(plain, capture_view_rect(*active, *vs))
        << "with effects/hit_flash off the flag must change nothing drawn";

    vs->control = nullptr;
    vs->topx = saved_topx;
    vs->topy = saved_topy;
    cfg.apply_setting(
        "effects",
        "hit_flash",
        previous_hit_flash.empty() ? "off" : previous_hit_flash);
}
