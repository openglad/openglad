#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/legacy/base.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/core/constants.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include "unit/unit.h"

namespace {

class MaxRandom final : public IRandom {
public:
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        return (max_exclusive == 0) ? 0u : (max_exclusive - 1u);
    }
};

struct WalkerR15Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    MaxRandom rng;

    WalkerR15Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

} // namespace

OG_UNIT_TEST(test_walker_r15_generator_fire_and_heading_branches)
{
    WalkerR15Fixture fx;

    walker* gen_tower = fx.level.add_ob(Order::Generator, FAMILY_TOWER);
    OG_ASSERT(gen_tower != nullptr);
    gen_tower->setxy(64, 64);
    gen_tower->sizex = 16;
    gen_tower->sizey = 16;
    gen_tower->stepsize = 2.0f;
    gen_tower->stats()->level = 6;
    gen_tower->stats()->magicpoints = 9999.0f;
    gen_tower->lastx = 1.0f;
    gen_tower->lasty = 0.0f;

    walker* fired = gen_tower->fire();
    OG_ASSERT(fired != nullptr);
    OG_ASSERT(fired->ani_type == ANI_TELE_IN);
    OG_ASSERT(fired->owner == nullptr);

    walker* weapon = fx.level.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    OG_ASSERT(weapon != nullptr);
    gen_tower->lastx = -1.0f;
    gen_tower->lasty = 0.0f;
    gen_tower->set_weapon_heading(weapon);
    OG_ASSERT(weapon->lastx <= 0.0f);

    gen_tower->lastx = 0.0f;
    gen_tower->lasty = 1.0f;
    gen_tower->set_weapon_heading(weapon);
    OG_ASSERT(weapon->lasty >= 0.0f);
}

OG_UNIT_TEST(test_walker_r15_compute_outline_and_next_frame_and_generate_paths)
{
    WalkerR15Fixture fx;

    walker* a = fx.level.add_ob(Order::Living, FAMILY_CLERIC);
    walker* viewer = fx.level.add_ob(Order::Living, FAMILY_SOLDIER);
    OG_ASSERT(a && viewer);
    a->team_num = 1;
    viewer->team_num = 0;
    a->stats()->set_bit_flags(BIT_NAMED, 1);

    a->outline = OUTLINE_INVULNERABLE;
    a->invulnerable_left = 1;
    a->flight_left = 0;
    a->invisibility_left = 0;
    a->compute_outline(viewer);
    OG_ASSERT(a->outline == OUTLINE_NAMED || a->outline == OUTLINE_INVULNERABLE);

    a->outline = OUTLINE_FLYING;
    a->flight_left = 1;
    a->compute_outline(viewer);
    OG_ASSERT(a->outline == OUTLINE_FLYING || a->outline == OUTLINE_NAMED);

    a->outline = static_cast<short>(a->query_team_color());
    a->invulnerable_left = 1;
    a->flight_left = 0;
    a->compute_outline(viewer);
    OG_ASSERT(a->outline == OUTLINE_INVULNERABLE || a->outline == OUTLINE_NAMED);

    walker* gen_tent = fx.level.add_ob(Order::Generator, FAMILY_TENT);
    OG_ASSERT(gen_tent != nullptr);
    gen_tent->stats()->level = 200;
    gen_tent->stats()->hitpoints = 10.0f;
    gen_tent->stats()->max_hitpoints = 10.0f;
    gen_tent->lineofsight = 3;
    gen_tent->set_act_type(ACT_GENERATE);
    (void)gen_tent->act();
    OG_ASSERT(gen_tent->stats()->hitpoints <= gen_tent->stats()->max_hitpoints);

    // next_frame path using real animation data loaded by loader.
    walker* living = fx.level.add_ob(Order::Living, FAMILY_SOLDIER);
    OG_ASSERT(living != nullptr);
    (void)living->next_frame();
}
