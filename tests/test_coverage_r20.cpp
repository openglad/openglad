#include <openglad/core/constants.h>
#include <openglad/core/stats.h>
#include <openglad/data/gparser.h>
#include <openglad/data/level_data.h>
#include <openglad/data/pixie_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/smooth.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/guy.h>
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
const FamilyDescriptor& describe_family_mage();
const FamilyDescriptor& describe_family_druid();

namespace {

struct ConstantRandom final : IRandom {
    std::uint32_t value = 0;

    explicit ConstantRandom(std::uint32_t v)
        : value(v)
    {
    }

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        return value % max_exclusive;
    }
};

struct SequenceRandom final : IRandom {
    std::array<std::uint32_t, 8> values{};
    std::size_t idx = 0;

    explicit SequenceRandom(std::initializer_list<std::uint32_t> init)
    {
        std::size_t i = 0;
        for (std::uint32_t v : init)
        {
            if (i >= values.size())
                break;
            values[i++] = v;
        }
    }

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = values[idx % values.size()] % max_exclusive;
        ++idx;
        return v;
    }
};

struct R20Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    ConstantRandom rng{1};
    GameContext gc;

    R20Fixture()
    {
        init_family_registry();
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);

        gc.rng = &rng;
        gc.config = &cfg;
        set_global_context(&gc);
    }

    ~R20Fixture()
    {
        set_global_context(nullptr);
    }
};

walker* add_walker(R20Fixture& fx, Order order, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(order, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->lineofsight = 4;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    if (order == Order::Weapon)
        fx.level.weaplist.push_back(std::move(w));
    else
        fx.level.oblist.push_back(std::move(w));
    return out;
}

living* add_living(R20Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->lineofsight = 4;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[x + y * pd.w] = v;
}

PixieData make_grid(unsigned char fill, int w = 7, int h = 7)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[i] = fill;
    return pd;
}

void set_neighbors_mask(PixieData& pd, int cx, int cy, unsigned char center,
                        unsigned char same_genre, unsigned char other, int mask)
{
    set_at(pd, cx, cy, center);
    set_at(pd, cx, cy - 1, (mask & TO_UP) ? same_genre : other);
    set_at(pd, cx + 1, cy, (mask & TO_RIGHT) ? same_genre : other);
    set_at(pd, cx, cy + 1, (mask & TO_DOWN) ? same_genre : other);
    set_at(pd, cx - 1, cy, (mask & TO_LEFT) ? same_genre : other);

    set_at(pd, cx - 1, cy - 1, other);
    set_at(pd, cx + 1, cy - 1, other);
    set_at(pd, cx - 1, cy + 1, other);
    set_at(pd, cx + 1, cy + 1, other);
}

} // namespace

OG_UNIT_TEST(test_coverage_r20_walker_act_random_no_foe_and_chase_paths)
{
    R20Fixture fx;

    walker* self = add_walker(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    OG_ASSERT(self != nullptr);

    SequenceRandom rng_no_foe{0, 1, 1};
    self->sim_rng = &rng_no_foe;
    self->foe = nullptr;
    self->lineofsight = 1;
    self->set_act_type(ACT_RANDOM);
    (void)self->act();

    walker* foe = add_walker(fx, Order::Living, FAMILY_ORC, 1, 220, 64);
    OG_ASSERT(foe != nullptr);
    self->stats()->clear_command();
    SequenceRandom rng_chase{0, 1, 1};
    self->sim_rng = &rng_chase;
    self->foe = foe;
    self->lineofsight = 1;
    self->collide_ob = reinterpret_cast<walker*>(0x1);
    (void)self->act();
    OG_ASSERT(self->collide_ob == nullptr);
}

OG_UNIT_TEST(test_coverage_r20_walker_movement_stationary_walkstep_walk_turn)
{
    R20Fixture fx;

    walker* tower = add_walker(fx, Order::Living, FAMILY_TOWER1, 0, 80, 80);
    OG_ASSERT(tower != nullptr);

    tower->stepsize = 2.0f;
    const float old_lastx = tower->lastx;
    const float old_lasty = tower->lasty;

    OG_ASSERT(tower->walkstep(1.0f, 0.0f));
    OG_ASSERT(tower->lastx == 1.0f);
    OG_ASSERT(tower->lasty == 0.0f);

    OG_ASSERT(tower->walk(0.0f, -1.0f));

    tower->curdir = FACE_UP;
    tower->turn(FACE_LEFT);
    OG_ASSERT(tower->lastx == 1.0f);
    OG_ASSERT(tower->lasty == 0.0f);
    OG_ASSERT(tower->curdir != FACE_UP);
    OG_ASSERT(old_lastx != tower->lastx || old_lasty != tower->lasty);
}

OG_UNIT_TEST(test_coverage_r20_level_data_add_paths_and_clear_reset)
{
    R20Fixture fx;

    walker* as_weapon = fx.level.add_ob(Order::Weapon, FAMILY_ARROW);
    OG_ASSERT(as_weapon != nullptr);

    walker* fx_ob = fx.level.add_fx_ob(Order::FX, FAMILY_HIT);
    OG_ASSERT(fx_ob != nullptr);

    walker* weap = fx.level.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    OG_ASSERT(weap != nullptr);

    fx.level.title = "changed";
    fx.level.type = 7;
    fx.level.par_value = 9;
    fx.level.time_bonus_limit = 10;
    fx.level.topx = 5;
    fx.level.topy = 6;
    fx.level.clear();

    OG_ASSERT(fx.level.title == "New Level");
    OG_ASSERT(fx.level.type == 0);
    OG_ASSERT(fx.level.par_value == 1);
    OG_ASSERT(fx.level.time_bonus_limit == 4000);
    OG_ASSERT(fx.level.topx == 0);
    OG_ASSERT(fx.level.topy == 0);

    walker dummy;
    OG_ASSERT(fx.level.remove_ob(&dummy) == 0);
}

OG_UNIT_TEST(test_coverage_r20_smooth_dark_grass_specific_branches)
{
    ConstantRandom rng1{1};
    GameContext gc;
    gc.rng = &rng1;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 3;
    const int y = 3;

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_UP | TO_DOWN | TO_LEFT);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_DOWN);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_LEFT | TO_DOWN);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_WATER1,
                       TO_LEFT | TO_DOWN);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_WATER1,
                       TO_DOWN);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_RIGHT | TO_UP);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_WATER1,
                       TO_RIGHT | TO_UP);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_RIGHT);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_WATER1,
                       TO_RIGHT);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_UP);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_UP | TO_DOWN);
    s.smooth(x, y);

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_coverage_r20_family_cleric_do_special_guard_conditions)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    living self;

    self.set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    self.user = 0;

    self.current_special = 1;
    self.shifter_down = 1;
    self.busy = 1.0f;
    OG_ASSERT(!desc.do_special(&self));

    self.busy = 0.0f;
    self.myguy->intelligence = 40;
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 2;
    self.shifter_down = 1;
    self.busy = 1.0f;
    OG_ASSERT(!desc.do_special(&self));

    self.busy = 0.0f;
    self.myguy->intelligence = 30;
    const float old_busy2 = self.busy;
    OG_ASSERT(!desc.do_special(&self));
    OG_ASSERT(self.busy > old_busy2);

    self.current_special = 3;
    self.shifter_down = 1;
    self.busy = 0.0f;
    self.myguy->intelligence = 30;
    OG_ASSERT(!desc.do_special(&self));
}

OG_UNIT_TEST(test_coverage_r20_family_mage_do_special_guard_conditions)
{
    const FamilyDescriptor& desc = describe_family_mage();
    living self;

    self.set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    self.user = 0;

    self.current_special = 1;
    self.ani_type = ANI_TELE_OUT;
    OG_ASSERT(!desc.do_special(&self));

    self.ani_type = ANI_WALK;
    self.shifter_down = 1;
    self.busy = 1.0f;
    OG_ASSERT(!desc.do_special(&self));

    self.busy = 0.0f;
    self.myguy->intelligence = 40;
    OG_ASSERT(!desc.do_special(&self));

    self.shifter_down = 0;
    self.ani_type = ANI_WALK;
    OG_ASSERT(desc.do_special(&self));
    OG_ASSERT(self.ani_type == ANI_TELE_OUT);
}

OG_UNIT_TEST(test_coverage_r20_family_druid_do_special_default_and_busy_guards)
{
    const FamilyDescriptor& desc = describe_family_druid();
    R20Fixture fx;

    living* self = add_living(fx, FAMILY_DRUID, 0, 64, 64);
    OG_ASSERT(self != nullptr);

    self->current_special = 4;
    self->busy = 0.0f;
    OG_ASSERT(!desc.do_special(self));

    self->current_special = 1;
    self->busy = 1.0f;
    OG_ASSERT(!desc.do_special(self));

    self->current_special = 2;
    self->busy = 1.0f;
    OG_ASSERT(!desc.do_special(self));
}
