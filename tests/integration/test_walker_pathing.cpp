#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/gparser.h>
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

TEST(WalkerPathing, find_path_to_foe_routes_east_and_each_follow_walks_one_stepsize)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.delete_objects();
    ASSERT_TRUE(world.myobmap != nullptr) << "pathfinding requires an obmap";

    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(a != nullptr) << "attacker should be created";
    ASSERT_TRUE(b != nullptr) << "target should be created";

    a->setxy(32, 32);
    b->setxy(96, 32);
    a->set_foe(b);
    // Keep the corridor clear of the two bodies themselves (the sibling
    // direct-solver case does the same): what is under test is the route, not
    // how A* squeezes past the actor and its own goal.
    ASSERT_TRUE(world.myobmap->remove(a));
    ASSERT_TRUE(world.myobmap->remove(b));

    a->find_path_to_foe();
    ASSERT_FALSE(a->path_to_foe.empty())
        << "an open grass grid must yield a route to a foe four cells east";
    for (PathState node : a->path_to_foe)
        EXPECT_EQ(32, GET_STATE_Y(node))
            << "a due-east foe on open ground gets a straight-line route";
    EXPECT_EQ(96, GET_STATE_X(a->path_to_foe.back()))
        << "the route ends on the foe's cell";
    EXPECT_EQ(32, GET_STATE_X(a->path_to_foe.front()))
        << "the route opens on the actor's own cell";
    EXPECT_EQ(5u, a->path_to_foe.size())
        << "the actor's cell plus the four cells east of it";

    // follow_path_to_foe walks ONE stepsize toward the next node per call and
    // stops; the node is only erased once the walker's own cell reaches it.
    // curdir must already match the hop or walk() turns in place instead.
    a->set_curdir(FACE_RIGHT);
    a->set_stepsize(2);
    const short x_before = a->xpos();
    for (int i = 0; i < 5; i++)
        a->follow_path_to_foe();
    EXPECT_EQ(x_before + 10, static_cast<int>(a->xpos()))
        << "five follows at stepsize 2 advance exactly ten pixels east";
    EXPECT_EQ(32, static_cast<int>(a->ypos())) << "and never leave the row";
    EXPECT_EQ(4u, a->path_to_foe.size())
        << "only the already-reached opening node is erased: x=42 still aligns "
           "to cell x=32, so the 48 node is not reached yet";

    draw_walker_path(*a, v);

    delete a;
    delete b;
}


TEST(WalkerPathing, compute_outline_state_machine_and_draw_ages_damage_numbers)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    world.delete_objects();

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";

    // Damage numbers only draw for the controlling walker, and only when the
    // effect is enabled (it ships OFF).
    const bool damage_numbers_were_on = cfg.is_on("effects", "damage_numbers");
    cfg.apply_setting("effects", "damage_numbers", "on");
    v->control = w;
    w->damage_numbers.clear();
    // created_tick == the current sim tick makes the ageing loop advance by
    // exactly one step, whatever tick the binary happens to be on.
    w->damage_numbers.emplace_back(static_cast<float>(w->xpos()),
                                   static_cast<float>(w->ypos()), 12.0f, 55,
                                   world.tick_count_);
    ASSERT_EQ(1u, w->damage_numbers.size());
    const float y_before = w->damage_numbers.front().y;
    ASSERT_FLOAT_EQ(1.0f, w->damage_numbers.front().t)
        << "a fresh damage number starts with a full lifetime";

    // compute_outline from outline 0 with all three effects up: the else arm
    // tests invisibility FIRST, so the team colour wins over flight and
    // invulnerability.
    w->set_outline(0);
    w->set_invisibility_left(1);
    w->set_invulnerable_left(1);
    w->set_flight_left(1);
    w->compute_outline(v->control);
    ASSERT_EQ(40, static_cast<int>(w->outline()))
        << "outline 0 + invisibility -> the walker's team colour";

    // draw_walker runs the state machine again, and the team-colour arm tests
    // invulnerability FIRST -- so the same three flags now resolve elsewhere.
    ASSERT_TRUE(draw_walker(*w, v)) << "a live walker draws";
    EXPECT_EQ(static_cast<int>(OUTLINE_INVULNERABLE),
              static_cast<int>(w->outline()))
        << "team colour + invulnerability -> OUTLINE_INVULNERABLE";

    // ... and the same draw aged the damage number by exactly one step.
    ASSERT_EQ(1u, w->damage_numbers.size())
        << "a number with lifetime left is not erased";
    EXPECT_FLOAT_EQ(0.95f, w->damage_numbers.front().t)
        << "one draw step costs 0.05 of the number's lifetime";
    EXPECT_FLOAT_EQ(y_before - 1.5f, w->damage_numbers.front().y)
        << "and floats it 1.5px up the screen";

    // Restore view control.
    v->control = nullptr;
    cfg.apply_setting("effects", "damage_numbers",
                      damage_numbers_were_on ? "on" : "off");
    delete w;
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
    ASSERT_NE(nullptr, actor) << "pathing actor should be created";
    ASSERT_NE(nullptr, foe) << "pathing foe should be created";

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
    ASSERT_NE(nullptr, pathing) << "pathfinding state should be available";

    std::vector<PathState> path;
    float total_cost = 0.0f;
    pathing->solve_for(actor, make_state(actor->xpos(), actor->ypos()),
                       make_state(foe->xpos(), foe->ypos()), path, total_cost);

    // The solver's route OPENS on the start cell (the sibling
    // find_path_to_foe case pins the same contract), so two diagonal hops
    // from (32,32) to (64,64) are three nodes, not two.
    ASSERT_EQ(3u, path.size())
        << "the actor's own cell plus the two diagonal hops to the foe";
    EXPECT_EQ(32, GET_STATE_X(path[0])) << "the route opens on the actor";
    EXPECT_EQ(32, GET_STATE_Y(path[0])) << "the route opens on the actor";
    EXPECT_EQ(48, GET_STATE_X(path[1])) << "one diagonal step south-east";
    EXPECT_EQ(48, GET_STATE_Y(path[1])) << "one diagonal step south-east";
    EXPECT_EQ(64, GET_STATE_X(path[2])) << "the route ends on the foe's cell";
    EXPECT_EQ(64, GET_STATE_Y(path[2])) << "the route ends on the foe's cell";
    EXPECT_NEAR(2.0f * 1.41421354f, total_cost, 0.001f)
        << "two diagonal steps cost sqrt(2) each";

    world.delete_objects();
}


TEST(WalkerPathing, round10_follow_path_node_erase_and_normalize_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";

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

    // Diagonal node -> normalize dx/dy and walkstep branch. curdir must already
    // face the hop, or walk() takes its changed-direction arm and turns in
    // place (which `>= x_before` would have accepted as movement).
    w->path_to_foe.clear();
    w->path_to_foe.push_back(make_state(48, 48));
    w->set_stepsize(2);
    w->set_curdir(FACE_DOWN_RIGHT);
    const short x_before = w->xpos();
    const short y_before = w->ypos();
    w->follow_path_to_foe();
    EXPECT_EQ(x_before + 2, static_cast<int>(w->xpos()))
        << "the diagonal hop normalises to +1 and walksteps one stepsize east";
    EXPECT_EQ(y_before + 2, static_cast<int>(w->ypos()))
        << "...and one stepsize south";
    EXPECT_EQ(1u, w->path_to_foe.size())
        << "the node is two pixels closer, not reached: it stays on the path";

    delete w;
}
