#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <cstdint>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
class ScopedGameplayWorld
{
public:
    explicit ScopedGameplayWorld(GameWorld& world)
        : previous_(current_game)
    {
        context_.world = &world;
        current_game = &context_;
    }

    ~ScopedGameplayWorld()
    {
        current_game = previous_;
    }

    ScopedGameplayWorld(const ScopedGameplayWorld&) = delete;
    ScopedGameplayWorld& operator=(const ScopedGameplayWorld&) = delete;

private:
    GameplayContext context_{};
    GameplayContext* previous_ = nullptr;
};

PathState make_state(int x, int y)
{
    return reinterpret_cast<PathState>(
        static_cast<intptr_t>(((y / GRID_SIZE) * MAP_WIDTH) + (x / GRID_SIZE)));
}
} // namespace

static walker* make_guy(char family, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(32, 32);
    return w.release();
}

TEST(WalkerPathing, walker_pathfinding_follow_and_draw_path_smoke)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(a != nullptr) << "attacker should be created";
    ASSERT_TRUE(b != nullptr) << "target should be created";

    a->set_foe(b);
    b->setxy(96, 32);

    a->find_path_to_foe();
    // Solve can fail depending on map/obstacles; main goal is to execute logic.
    if (!a->path_to_foe.empty())
    {
        // Follow a few nodes.
        for (int i = 0; i < 5; i++)
            a->follow_path_to_foe();

        draw_walker_path(*a, v);
    }

    delete a;
    delete b;
}


TEST(WalkerPathing, walker_damage_numbers_and_compute_outline_smoke)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";

    // Damage numbers only draw for the controlling walker.
    v->control = w;
    w->damage_numbers.emplace_back(w->xpos(), w->ypos(), 12.0f, 55);

    // Outline mode changes based on these fields.
    w->set_invisibility_left(1);
    w->set_invulnerable_left(1);
    w->set_flight_left(1);
    w->compute_outline(v->control);

    // Draw should update and consume damage numbers over time.
    (void)draw_walker(*w, v);

    // Restore view control.
    v->control = nullptr;
}

TEST(WalkerPathing, direct_solver_returns_expected_route_and_cost)
{
    GameWorld world(0u);
    sdl_level_data_hooks().wire_world_entity_services(&world, nullptr);
    world.clear();
    world.create_new_grid();
    ASSERT_TRUE(world.myobmap != nullptr) << "pathfinding requires an obmap";

    ScopedGameplayWorld gameplay_world(world);

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(actor != nullptr && foe != nullptr) << "pathing actors should be created";
    if (!(actor && foe))
        return;

    actor->set_team_num(0);
    foe->set_team_num(1);
    actor->set_sizey(GRID_SIZE - 1);
    actor->set_sizex(GRID_SIZE - 1);
    foe->set_sizey(GRID_SIZE - 1);
    foe->set_sizex(GRID_SIZE - 1);
    ASSERT_TRUE(actor->setxy(32, 32));
    ASSERT_TRUE(foe->setxy(64, 64));
    actor->set_foe(foe);
    ASSERT_TRUE(world.myobmap->remove(actor));
    ASSERT_TRUE(world.myobmap->remove(foe));
    EXPECT_TRUE(world.myobmap->obmap_get_list(48, 48).empty());
    EXPECT_TRUE(world.myobmap->obmap_get_list(64, 64).empty());

    GameplayPathfindingState* pathing = ensure_pathfinding_state(*current_game);
    ASSERT_TRUE(pathing != nullptr) << "pathfinding state should be available";
    if (!pathing)
        return;

    std::vector<PathState> path;
    float total_cost = 0.0f;
    pathing->solve_for(actor, make_state(actor->xpos(), actor->ypos()),
                       make_state(foe->xpos(), foe->ypos()), path, total_cost);

    ASSERT_GE(path.size(), 2u) << "solver should produce a route on an open grid";
    const std::size_t route_offset =
        (GET_STATE_X(path.front()) == actor->xpos() && GET_STATE_Y(path.front()) == actor->ypos())
            ? 1u
            : 0u;
    ASSERT_EQ(path.size(), 2u + route_offset);
    EXPECT_EQ(GET_STATE_X(path[route_offset]), 48);
    EXPECT_EQ(GET_STATE_Y(path[route_offset]), 48);
    EXPECT_EQ(GET_STATE_X(path.back()), 64);
    EXPECT_EQ(GET_STATE_Y(path.back()), 64);
    EXPECT_NEAR(2.0f * 1.41421354f, total_cost, 0.001f);

    world.delete_objects();
}


TEST(WalkerPathing, round10_follow_path_node_erase_and_normalize_paths)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    if (!w)
        return;

    using State = typename decltype(w->path_to_foe)::value_type;
    auto make_state = [](int x, int y) -> State {
        constexpr int grid = 16;
        constexpr int width = 400;
        const std::intptr_t idx = static_cast<std::intptr_t>((y / grid) * width + (x / grid));
        return reinterpret_cast<State>(idx);
    };

    w->setxy(32, 32);

    // First node equals current position -> erase-node path.
    w->path_to_foe.clear();
    w->path_to_foe.push_back(make_state(32, 32));
    w->follow_path_to_foe();
    ASSERT_TRUE(w->path_to_foe.empty()) << "follow_path_to_foe should erase already-reached node";

    // Diagonal node -> normalize dx/dy and walkstep branch.
    w->path_to_foe.clear();
    w->path_to_foe.push_back(make_state(48, 48));
    const short x_before = w->xpos();
    const short y_before = w->ypos();
    w->follow_path_to_foe();
    ASSERT_TRUE(w->xpos() >= x_before && w->ypos() >= y_before) << "follow_path_to_foe should move toward diagonal next node";

    delete w;
}
