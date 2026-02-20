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
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "unit/unit.h"

namespace {

struct SeqRandom final : IRandom {
    std::vector<std::uint32_t> vals;
    std::size_t i = 0;

    explicit SeqRandom(std::initializer_list<std::uint32_t> init)
        : vals(init)
    {}

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        if (vals.empty())
            return 0;
        const std::uint32_t v = vals[i % vals.size()] % max_exclusive;
        ++i;
        return v;
    }
};

struct R17Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    R17Fixture()
    {
        init_family_registry();
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        gc.config = &cfg;
        set_global_context(&gc);
    }

    ~R17Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(R17Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->lineofsight = 8;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

walker* add_fx(R17Fixture& fx, char family, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::FX, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->team_num = 0;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

void assign_basic_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[i][0] = 0;
        seqs[i][1] = 1;
        seqs[i][2] = -1;
        seqs[i][3] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

PixieData make_grid(unsigned char fill, int w = 9, int h = 9)
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

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[x + y * pd.w] = v;
}

} // namespace

OG_UNIT_TEST(test_coverage_r17_family_mage_specials_and_reactions)
{
    R17Fixture fx;
    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    OG_ASSERT(mage != nullptr);

    living* self = add_living(fx, FAMILY_MAGE, 0, 64, 64);
    OG_ASSERT(self != nullptr);
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    self->myguy->name = "R17 Mage";
    self->myguy->intelligence = 70;
    self->user = 0;
    self->stats()->level = 8;
    self->stats()->magicpoints = 300.0f;

    self->current_special = 1;
    self->shifter_down = 1;
    OG_ASSERT(!mage->do_special(self));

    self->myguy->intelligence = 90;
    walker* marker = add_fx(fx, FAMILY_MARKER, 66, 64);
    OG_ASSERT(marker != nullptr);
    marker->owner = self;
    marker->dead = 0;
    self->busy = 0;
    self->current_special = 1;
    self->shifter_down = 1;
    OG_ASSERT(mage->do_special(self));

    self->busy = 0;
    self->current_special = 2;
    self->shifter_down = 0;
    self->lastx = 1.0f;
    self->lasty = 0.0f;
    OG_ASSERT(mage->do_special(self));

    living* foe = add_living(fx, FAMILY_ORC, 1, 96, 64);
    OG_ASSERT(foe != nullptr);
    self->stats()->hitpoints = self->stats()->max_hitpoints;
    self->stats()->level = 5;
    self->foe = nullptr;
    foe->foe = nullptr;
    mage->hit_response(self->stats(), foe);
    OG_ASSERT(self->foe == foe);
    OG_ASSERT(foe->foe == self);

    add_living(fx, FAMILY_ORC, 1, 100, 64);
    add_living(fx, FAMILY_ORC, 1, 104, 64);
    add_living(fx, FAMILY_ORC, 1, 108, 64);
    self->current_special = 1;
    OG_ASSERT(mage->check_special_ai(self));
}

OG_UNIT_TEST(test_coverage_r17_family_druid_protection_tree_and_faerie)
{
    R17Fixture fx;
    const FamilyDescriptor* druid = get_family_descriptor(FAMILY_DRUID);
    OG_ASSERT(druid != nullptr);

    living* self = add_living(fx, FAMILY_DRUID, 0, 80, 80);
    living* ally1 = add_living(fx, FAMILY_SOLDIER, 0, 84, 80);
    living* ally2 = add_living(fx, FAMILY_SOLDIER, 0, 88, 80);
    OG_ASSERT(self && ally1 && ally2);

    self->set_owned_myguy(std::make_unique<guy>(FAMILY_DRUID));
    self->stats()->level = 7;
    self->stats()->magicpoints = 300.0f;
    self->busy = 0;

    self->current_special = 1;
    self->lastx = 1.0f;
    self->lasty = 0.0f;
    OG_ASSERT(druid->do_special(self));

    walker* existing = fx.level.add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    OG_ASSERT(existing != nullptr);
    existing->owner = ally2;
    existing->team_num = ally2->team_num;
    existing->setxy(ally2->xpos, ally2->ypos);

    self->busy = 0;
    self->current_special = 4;
    OG_ASSERT(druid->do_special(self));

    self->busy = 0;
    self->current_special = 2;
    self->setxy(0, 0);
    self->lastx = -1.0f;
    self->lasty = 0.0f;
    (void)druid->do_special(self);
}

OG_UNIT_TEST(test_coverage_r17_walker_movement_and_act_cleanup)
{
    R17Fixture fx;

    walker* actor = fx.level.add_ob(Order::FX, FAMILY_EXPLOSION);
    OG_ASSERT(actor != nullptr);
    actor->setxy(static_cast<short>(64), static_cast<short>(64));
    actor->sizex = 16;
    actor->sizey = 16;
    actor->stepsize = 1.0f;
    actor->team_num = 0;

    living* dead_foe = add_living(fx, FAMILY_ORC, 1, 80, 64);
    living* dead_leader = add_living(fx, FAMILY_ORC, 1, 84, 64);
    living* dead_owner = add_living(fx, FAMILY_ORC, 1, 88, 64);
    OG_ASSERT(actor && dead_foe && dead_leader && dead_owner);
    dead_foe->dead = 1;
    dead_leader->dead = 1;
    dead_owner->dead = 1;

    actor->foe = dead_foe;
    actor->leader = dead_leader;
    actor->owner = dead_owner;
    actor->ani_type = ANI_WALK;
    actor->attack_lunge = 0.2f;
    actor->hit_recoil = 0.3f;
    actor->stats()->clear_command();
    actor->stats()->frozen_delay = 0;
    actor->set_act_type(ACT_CONTROL);
    (void)actor->act();

    assign_basic_ani(actor);
    actor->user = -1;
    actor->setxy(static_cast<short>(0), static_cast<short>(fx.level.pixmaxy - 1));
    actor->curdir = FACE_DOWN_LEFT;
    (void)actor->walkstep(-1.0f, 1.0f);

    actor->user = 0;
    actor->setxy(static_cast<short>(0), static_cast<short>(fx.level.pixmaxy - 1));
    actor->curdir = FACE_DOWN_LEFT;
    (void)actor->walkstep(-1.0f, 1.0f);

    actor->curdir = 127;
    actor->stepsize = 2.0f;
    (void)actor->turn(FACE_RIGHT);
}

OG_UNIT_TEST(test_coverage_r17_smooth_grass_water_and_dark_variants)
{
    SeqRandom rng{1, 2, 0, 0, 1, 0};
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 4;
    const int y = 4;

    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x - 1, y - 1, PIX_WATER1);
    set_at(pd, x + 1, y - 1, PIX_WATER1);
    set_at(pd, x - 1, y + 1, PIX_WATER1);
    set_at(pd, x, y - 1, PIX_WATER1);
    set_at(pd, x - 1, y, PIX_WATER1);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x + 1, y - 1, PIX_WATER1);
    set_at(pd, x + 1, y + 1, PIX_WATER1);
    set_at(pd, x - 1, y + 1, PIX_WATER1);
    set_at(pd, x + 1, y, PIX_WATER1);
    set_at(pd, x, y + 1, PIX_WATER1);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x - 1, y - 1, PIX_GRASS1);
    set_at(pd, x + 1, y - 1, PIX_GRASS1);
    set_at(pd, x - 1, y + 1, PIX_GRASS1);
    set_at(pd, x + 1, y + 1, PIX_GRASS1);
    set_at(pd, x, y - 1, PIX_GRASS1);
    set_at(pd, x, y + 1, PIX_GRASS1);
    set_at(pd, x - 1, y, PIX_GRASS1);
    set_at(pd, x + 1, y, PIX_GRASS1);
    s.smooth(x, y);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_GRASS_DARK_1);
    set_at(pd, x - 1, y - 1, PIX_TREE_B1);
    set_at(pd, x, y - 1, PIX_TREE_B1);
    set_at(pd, x, y + 1, PIX_FLOOR1);
    s.smooth(x, y);

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_coverage_r17_save_data_reset_defaults)
{
    SaveData save;
    save.current_campaign = "custom.campaign";
    save.completed_levels["custom.campaign"].insert(4);
    save.current_levels["custom.campaign"] = 9;
    save.team_size = 2;
    save.score = 123;
    save.totalcash = 456;
    save.totalscore = 789;
    save.scen_num = 9;
    save.my_team = 3;

    save.reset();

    OG_ASSERT(save.current_campaign == "org.openglad.gladiator");
    OG_ASSERT(save.team_size == 0);
    OG_ASSERT(save.scen_num == 1);
    OG_ASSERT(save.my_team == 0);
    OG_ASSERT(save.current_levels["org.openglad.gladiator"] == 1);
}
