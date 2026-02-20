#include <openglad/core/constants.h>
#include <openglad/data/gparser.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/irandom.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/legacy/base.h>

#include <array>
#include <cstdint>
#include <memory>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_cleric();

namespace {

struct SeqRandom final : IRandom {
    std::array<std::uint32_t, 8> vals{};
    std::size_t i = 0;

    explicit SeqRandom(std::initializer_list<std::uint32_t> init)
    {
        std::size_t idx = 0;
        for (std::uint32_t v : init)
        {
            if (idx >= vals.size())
                break;
            vals[idx++] = v;
        }
    }

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = vals[i % vals.size()] % max_exclusive;
        ++i;
        return v;
    }
};

struct R19Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    R19Fixture()
    {
        init_family_registry();
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        gc.config = &cfg;
        set_global_context(&gc);
    }

    ~R19Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(R19Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

void assign_short_ani(walker* w)
{
    static std::array<std::array<signed char, 3>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[i][0] = 0;
        seqs[i][1] = -1;
        seqs[i][2] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

} // namespace

OG_UNIT_TEST(test_coverage_r19_family_cleric_check_special_true_paths)
{
    R19Fixture fx;
    const FamilyDescriptor& desc = describe_family_cleric();

    living* self = add_living(fx, FAMILY_CLERIC, 0, 64, 64);
    living* ally = add_living(fx, FAMILY_SOLDIER, 0, 70, 64);
    OG_ASSERT(self && ally);

    self->current_special = 1;
    self->stats()->max_magicpoints = 100.0f;
    self->stats()->magicpoints = 80.0f;
    self->shifter_down = 1;
    OG_ASSERT(desc.check_special_ai(self));
    OG_ASSERT(self->shifter_down == 0);

    ally->setxy(300, 300);
    self->shifter_down = 0;
    OG_ASSERT(desc.check_special_ai(self));
    OG_ASSERT(self->shifter_down == 1);
}

OG_UNIT_TEST(test_coverage_r19_walker_init_fire_busy_and_fire_fail_paths)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 48, 48);
    OG_ASSERT(self != nullptr);

    const short fire_dir = self->facing(1, 0);
    self->curdir = static_cast<char>(fire_dir);
    self->enddir = static_cast<char>(fire_dir);
    self->busy = 1.0f;
    OG_ASSERT(!self->init_fire(1, 0));

    self->busy = 0.0f;
    self->ani_type = ANI_ATTACK;
    self->stats()->weapon_cost = 1;
    self->stats()->magicpoints = 0.0f;
    OG_ASSERT(!self->init_fire(1, 0));
}

OG_UNIT_TEST(test_coverage_r19_walker_animate_attack_completion_branch)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    OG_ASSERT(self != nullptr);

    assign_short_ani(self);
    self->ani_type = ANI_ATTACK;
    self->cycle = 0;
    self->stats()->weapon_cost = 1;
    self->stats()->magicpoints = 0.0f;

    OG_ASSERT(self->animate());
    OG_ASSERT(self->ani_type == ANI_WALK);
    OG_ASSERT(self->cycle == 0);
}

OG_UNIT_TEST(test_coverage_r19_walker_act_random_paths)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* foe = add_living(fx, FAMILY_ORC, 1, 120, 64);
    OG_ASSERT(self && foe);

    self->lineofsight = 1;
    self->foe = nullptr;
    self->set_act_type(ACT_RANDOM);
    SeqRandom rng_find_and_move{0, 1, 0};
    self->sim_rng = &rng_find_and_move;
    foe->sim_rng = &rng_find_and_move;
    (void)self->act();

    foe->dead = 1;
    self->foe = nullptr;
    SeqRandom rng_find_none{0, 1, 0};
    self->sim_rng = &rng_find_none;
    foe->sim_rng = &rng_find_none;
    (void)self->act();
}
