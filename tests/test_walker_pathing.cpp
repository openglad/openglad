#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/render/walker_draw.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <cstdint>

// myscreen is now a macro defined in base.h (via game_session.h)

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

void test_walker_pathfinding_follow_and_draw_path_smoke()
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(a != nullptr, "attacker should be created");
    TEST_ASSERT(b != nullptr, "target should be created");

    a->foe = b;
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
REGISTER_TEST(test_walker_pathfinding_follow_and_draw_path_smoke);

void test_walker_damage_numbers_and_compute_outline_smoke()
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker should be created");

    // Damage numbers only draw for the controlling walker.
    v->control = w;
    w->damage_numbers.emplace_back(w->xpos, w->ypos, 12.0f, 55);

    // Outline mode changes based on these fields.
    w->invisibility_left = 1;
    w->invulnerable_left = 1;
    w->flight_left = 1;
    w->compute_outline(v->control);

    // Draw should update and consume damage numbers over time.
    (void)draw_walker(*w, v);

    // Restore view control.
    v->control = nullptr;
}
REGISTER_TEST(test_walker_damage_numbers_and_compute_outline_smoke);

void test_walker_pathing_round10_follow_path_node_erase_and_normalize_paths()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker should be created");
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
    TEST_ASSERT(w->path_to_foe.empty(), "follow_path_to_foe should erase already-reached node");

    // Diagonal node -> normalize dx/dy and walkstep branch.
    w->path_to_foe.clear();
    w->path_to_foe.push_back(make_state(48, 48));
    const short x_before = w->xpos;
    const short y_before = w->ypos;
    w->follow_path_to_foe();
    TEST_ASSERT(w->xpos >= x_before && w->ypos >= y_before,
                "follow_path_to_foe should move toward diagonal next node");

    delete w;
}
REGISTER_TEST(test_walker_pathing_round10_follow_path_node_erase_and_normalize_paths);
