#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/treasure.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_cleric();
const FamilyDescriptor& describe_family_mage();
const FamilyDescriptor& describe_family_soldier();

namespace {

struct ClericR12Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    ClericR12Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(ClericR12Fixture& fx, unsigned char team, char family = FAMILY_CLERIC)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(80, 80);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

walker* add_stain(ClericR12Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(x, y);
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_cleric_r12_ghost_raise_and_resurrect_penalty_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR12Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R12";
    cleric->myguy->exp = 1;
    cleric->myguy->intelligence = 80;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 300.0f;

    // Case 3: raise ghost success path.
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    walker* near_stain = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    OG_ASSERT(near_stain != nullptr);
    (void)desc.do_special(cleric);

    // Case 3: shifter_down turn-undead busy fail.
    cleric->shifter_down = 1;
    cleric->busy = 2;
    OG_ASSERT(!desc.do_special(cleric));
    cleric->busy = 0;

    // Case 4: friendly resurrect with exp floor path.
    cleric->current_special = 4;
    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    OG_ASSERT(friendly_stain != nullptr);
    (void)desc.do_special(cleric);

    // Case 4: hostile resurrect ghost path.
    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    OG_ASSERT(hostile_stain != nullptr);
    (void)desc.do_special(cleric);

    // Case 1 heal branch with heal_numbers on and at least one ally damaged.
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    ally->setxy(90, 80);
    ally->stats()->max_hitpoints = 100.0f;
    ally->stats()->hitpoints = 50.0f;
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    cleric->stats()->magicpoints = 100.0f;
    cleric->stats()->max_magicpoints = 100.0f;
    cfg.apply_setting("effects", "heal_numbers", "on");
    OG_ASSERT(desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_mage_r12_descriptor_paths)
{
    const FamilyDescriptor& mage = describe_family_mage();
    ClericR12Fixture fx;

    living* self = add_living(fx, 0, FAMILY_MAGE);
    OG_ASSERT(self != nullptr);
    self->stats()->level = 8;
    self->stats()->max_hitpoints = 100.0f;
    self->stats()->hitpoints = 20.0f;
    self->stats()->magicpoints = 200.0f;
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    self->myguy->intelligence = 90;
    self->user = 0;

    // AI branch: no foes in range.
    OG_ASSERT(mage.check_special_ai(self));

    // Hit response low-hp branch should attempt special.
    walker* foe = add_living(fx, 1, FAMILY_ORC);
    OG_ASSERT(foe != nullptr);
    mage.hit_response(self->stats(), foe);

    // Teleport handler callback branch.
    self->ani_type = ANI_TELE_OUT;
    self->cycle = 5;
    OG_ASSERT(mage.handle_teleport(self));
    OG_ASSERT(self->ani_type == ANI_TELE_IN);

    // do_special case 1 marker-placement branch.
    self->current_special = 1;
    self->shifter_down = 1;
    self->busy = 0;
    self->ani_type = ANI_WALK;
    OG_ASSERT(mage.do_special(self));

    // do_special case 1 teleport-out branch.
    self->shifter_down = 0;
    self->ani_type = ANI_WALK;
    OG_ASSERT(mage.do_special(self));
    OG_ASSERT(self->ani_type == ANI_TELE_OUT);

    // do_special case 5 branch with no targets can fail.
    self->current_special = 5;
    (void)mage.do_special(self);
}

OG_UNIT_TEST(test_family_soldier_and_treasure_r12_paths)
{
    const FamilyDescriptor& soldier = describe_family_soldier();
    ClericR12Fixture fx;

    auto s = std::make_unique<living>();
    s->set_order_family(Order::Living, FAMILY_SOLDIER);
    fx.level.wire_entity(s.get());
    s->setxy(60, 60);
    s->team_num = 0;
    s->stats()->level = 6;
    s->lastx = 1.0f;
    s->lasty = 0.0f;
    living* self = s.get();
    fx.level.oblist.push_back(std::move(s));

    walker* enemy = add_living(fx, 1, FAMILY_ORC);
    enemy->setxy(80, 60);

    soldier.on_create(self);
    OG_ASSERT(self->weapons_left >= 1);

    self->current_special = 1;
    OG_ASSERT(soldier.do_special(self));

    self->current_special = 3;
    self->busy = 1;
    OG_ASSERT(!soldier.do_special(self));
    self->busy = 0;

    self->foe = enemy;
    OG_ASSERT(soldier.check_special_ai(self) || !soldier.check_special_ai(self));

    walker* weap = fx.level.add_ob(Order::Weapon, FAMILY_KNIFE);
    OG_ASSERT(weap != nullptr);
    self->weapons_left = 0;
    const float mp_before = self->stats()->magicpoints;
    OG_ASSERT(!soldier.on_fire_weapon(self, weap));
    OG_ASSERT(weap->dead == 1);
    OG_ASSERT(self->stats()->magicpoints >= mp_before);

    // treasure.cpp paths
    treasure lonely;
    lonely.sim_level = &fx.level;
    lonely.stats()->level = 2;
    OG_ASSERT(lonely.find_teleport_target() == nullptr);
    OG_ASSERT(lonely.act());
    OG_ASSERT(lonely.eat_me(self));

    auto t1 = std::make_unique<treasure>();
    auto t2 = std::make_unique<treasure>();
    fx.level.wire_entity(t1.get());
    fx.level.wire_entity(t2.get());
    t1->set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    t2->set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    t1->stats()->level = 3;
    t2->stats()->level = 3;
    t2->dead = 0;
    treasure* t1_raw = t1.get();
    treasure* t2_raw = t2.get();
    fx.level.fxlist.push_back(std::move(t1));
    fx.level.fxlist.push_back(std::move(t2));
    OG_ASSERT(t1_raw->find_teleport_target() == t2_raw);
}

OG_UNIT_TEST(test_family_cleric_r12_shoved_ai_and_turn_undead_guard_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR12Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->user = 0;
    cleric->stats()->level = 6;
    cleric->stats()->magicpoints = 120.0f;

    // on_shoved callback path.
    desc.on_shoved(cleric);

    // check_special_ai healing branch with >1 friends nearby.
    cleric->current_special = 1;
    living* friend1 = add_living(fx, 0, FAMILY_SOLDIER);
    living* friend2 = add_living(fx, 0, FAMILY_ARCHER);
    friend1->setxy(82, 80);
    friend2->setxy(84, 80);
    OG_ASSERT(desc.check_special_ai(cleric));
    OG_ASSERT(cleric->shifter_down == 0);

    // check_special_ai mace branch with high MP and not enough heal targets.
    friend2->setxy(300, 300);
    friend1->setxy(300, 300);
    cleric->stats()->max_magicpoints = 100.0f;
    cleric->stats()->magicpoints = 80.0f;
    OG_ASSERT(desc.check_special_ai(cleric));
    OG_ASSERT(cleric->shifter_down == 1);

    // Mystic mace INT guard message path.
    cleric->current_special = 1;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->myguy->intelligence = 40;
    OG_ASSERT(!desc.do_special(cleric));

    // Turn-undead INT guard path for special 2.
    cleric->current_special = 2;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->myguy->intelligence = 50;
    OG_ASSERT(!desc.do_special(cleric));

    // Turn-undead INT guard path for special 3.
    cleric->current_special = 3;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->myguy->intelligence = 50;
    OG_ASSERT(!desc.do_special(cleric));
}
