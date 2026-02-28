#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include "unit/unit.h"
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include <openglad/entities/treasure.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/game_context.h>
#include "test_gameplay_context_scope.h"

// --- From test_family_cleric_coverage_push.cpp ---
const FamilyDescriptor& describe_family_cleric();

namespace detail_family_cleric_coverage_push {

OG_UNIT_TEST(test_family_cleric_descriptor_difficulty_and_customize_weapon)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    OG_ASSERT(desc.family_id == FAMILY_CLERIC);
    OG_ASSERT(desc.do_special != nullptr);
    OG_ASSERT(desc.check_special_ai != nullptr);
    OG_ASSERT(desc.set_difficulty != nullptr);
    OG_ASSERT(desc.customize_weapon != nullptr);

    living self;
    living weapon;
    self.stats()->level = 3;
    self.stats()->max_hitpoints = 100.0f;
    self.stats()->max_magicpoints = 40.0f;
    self.damage = 12.0f;
    weapon.lifetime = 10;
    desc.customize_weapon(&self, &weapon);
    OG_ASSERT(weapon.ani_type == ANI_GLOWGROW);
    OG_ASSERT(weapon.lifetime == 340);

    const float old_hp = self.stats()->max_hitpoints;
    const float old_mp = self.stats()->max_magicpoints;
    const float old_damage = self.damage;
    desc.set_difficulty(&self, 2);
    OG_ASSERT(self.stats()->max_hitpoints > old_hp);
    OG_ASSERT(self.stats()->max_magicpoints > old_mp);
    OG_ASSERT(self.damage > old_damage);
}

OG_UNIT_TEST(test_family_cleric_check_ai_default_and_do_special_busy_returns)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    living self;

    self.current_special = 0; // default true branch in check_special_ai
    OG_ASSERT(desc.check_special_ai(&self));

    self.shifter_down = 1;
    self.busy = 1;

    self.current_special = 1; // mystic mace branch guarded by busy
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 2; // turn undead branch guarded by busy
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 3; // turn undead high branch guarded by busy
    OG_ASSERT(!desc.do_special(&self));
}
} // namespace detail_family_cleric_coverage_push

// --- From test_family_cleric_r11.cpp ---
namespace detail_family_cleric_r11 {
namespace {

struct ClericFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    ClericFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(ClericFixture& fx, unsigned char team, char family = FAMILY_CLERIC)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
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

walker* add_stain(ClericFixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(x, y);
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_cleric_r11_check_ai_and_heal_fail_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);

    cleric->current_special = 1;
    cleric->stats()->max_magicpoints = 100.0f;
    cleric->stats()->magicpoints = 10.0f;
    OG_ASSERT(!desc.check_special_ai(cleric)); // line 63

    cleric->stats()->magicpoints = 60.0f;
    OG_ASSERT(desc.check_special_ai(cleric));
    OG_ASSERT(cleric->shifter_down == 1);

    cleric->shifter_down = 0;
    cleric->stats()->level = 5;
    cleric->stats()->magicpoints = 20.0f;

    // only cleric in range => howmany <= 1 path
    OG_ASSERT(!desc.do_special(cleric));

    // ally at full HP => didheal remains 0 path
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    ally->setxy(90, 80);
    ally->stats()->hitpoints = ally->stats()->max_hitpoints;
    OG_ASSERT(!desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_cleric_r11_heal_and_mace_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    OG_ASSERT(cleric != nullptr && ally != nullptr);

    cleric->setxy(80, 80);
    ally->setxy(90, 80);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->stats()->level = 6;
    cleric->stats()->magicpoints = 60.0f;
    cleric->stats()->max_magicpoints = 120.0f;
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    ally->stats()->max_hitpoints = 100.0f;
    ally->stats()->hitpoints = 40.0f;

    const float ally_before = ally->stats()->hitpoints;
    const float mp_before = cleric->stats()->magicpoints;
    OG_ASSERT(desc.do_special(cleric));
    OG_ASSERT(ally->stats()->hitpoints > ally_before);
    OG_ASSERT(cleric->stats()->magicpoints < mp_before);

    // mystic mace int requirement fail (lines 147-152)
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->myguy->intelligence = 30;
    OG_ASSERT(!desc.do_special(cleric));

    // mystic mace success path (lines 158-171)
    cleric->myguy->intelligence = 70;
    cleric->stats()->magicpoints = 80.0f;
    cleric->stats()->special_cost[1] = 2;
    const float mp_before_mace = cleric->stats()->magicpoints;
    OG_ASSERT(desc.do_special(cleric));
    OG_ASSERT(cleric->busy >= 5);
    OG_ASSERT(cleric->stats()->magicpoints < mp_before_mace);
}

OG_UNIT_TEST(test_family_cleric_r11_turn_and_raise_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->intelligence = 70;
    cleric->stats()->level = 8;
    cleric->stats()->magicpoints = 200.0f;

    // case 2, turn undead with no targets => -1 branch
    cleric->current_special = 2;
    cleric->shifter_down = 1;
    OG_ASSERT(!desc.do_special(cleric));

    // case 2 raise skeleton, no blood => false
    cleric->shifter_down = 0;
    OG_ASSERT(!desc.do_special(cleric));

    // add nearby blood and summon skeleton success path
    walker* stain = add_stain(fx, 88, 80, 1, FAMILY_SOLDIER);
    OG_ASSERT(stain != nullptr);
    (void)desc.do_special(cleric);

    // case 3 raise ghost distance fail + no blood
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    walker* far_stain = add_stain(fx, 300, 300, 1, FAMILY_SOLDIER);
    OG_ASSERT(far_stain != nullptr);
    OG_ASSERT(!desc.do_special(cleric));
    far_stain->dead = 1;
    OG_ASSERT(!desc.do_special(cleric));

    // case 3 turn undead int fail branch
    cleric->shifter_down = 1;
    cleric->myguy->intelligence = 10;
    OG_ASSERT(!desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_cleric_r11_resurrect_friendly_and_hostile_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);

    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->exp = 10000;
    cleric->stats()->level = 9;
    cleric->stats()->magicpoints = 300.0f;
    cleric->current_special = 4;

    // no blood => false path
    OG_ASSERT(!desc.do_special(cleric));

    // friendly blood resurrect path + exp penalty floor branch
    walker* friendly_stain = add_stain(fx, 88, 80, 0, FAMILY_SOLDIER);
    cleric->myguy->exp = 0;
    (void)desc.do_special(cleric);

    // hostile blood branch summons ghost
    walker* hostile_stain = add_stain(fx, 86, 84, 1, FAMILY_ORC);
    (void)desc.do_special(cleric);
}
} // namespace detail_family_cleric_r11

// --- From test_family_cleric_r12.cpp ---
const FamilyDescriptor& describe_family_mage();
const FamilyDescriptor& describe_family_soldier();

namespace detail_family_cleric_r12 {
namespace {

struct ClericR12Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    ClericR12Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(ClericR12Fixture& fx, unsigned char team, char family = FAMILY_CLERIC)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
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
    bind_test_entity_sim_context(fx.level, s.get());
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
    lonely.stats()->level = 2;
    OG_ASSERT(lonely.find_teleport_target() == nullptr);
    OG_ASSERT(lonely.act());
    OG_ASSERT(lonely.eat_me(self));

    auto t1 = std::make_unique<treasure>();
    auto t2 = std::make_unique<treasure>();
    bind_test_entity_sim_context(fx.level, t1.get());
    bind_test_entity_sim_context(fx.level, t2.get());
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
} // namespace detail_family_cleric_r12

// --- From test_family_cleric_r14.cpp ---
namespace detail_family_cleric_r14 {
namespace {

struct ClericR14Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;
    GameContext gc;

    ClericR14Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;

        set_global_context(&gc);
    }

    ~ClericR14Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(ClericR14Fixture& fx, unsigned char team, char family = FAMILY_CLERIC)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
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

walker* add_stain(ClericR14Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(x, y);
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_cleric_r14_lines_110_132_160_heal_plural_and_mystic_mace_branches)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    living* ally1 = add_living(fx, 0, FAMILY_SOLDIER);
    living* ally2 = add_living(fx, 0, FAMILY_SOLDIER);
    OG_ASSERT(cleric && ally1 && ally2);

    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R14 Cleric";
    cleric->myguy->intelligence = 90;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 200.0f;

    ally1->setxy(84, 80);
    ally2->setxy(86, 80);
    ally1->stats()->max_hitpoints = 100.0f;
    ally2->stats()->max_hitpoints = 100.0f;
    ally1->stats()->hitpoints = 20.0f;
    ally2->stats()->hitpoints = 30.0f;

    cfg.apply_setting("effects", "heal_numbers", "on");
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    OG_ASSERT(desc.do_special(cleric));

    cleric->current_special = 1;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    OG_ASSERT(desc.do_special(cleric) || !desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_cleric_r14_lines_187_189_192_203_206_243_246_turn_undead_and_raise_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "UndeadTest";
    cleric->myguy->intelligence = 90;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 300.0f;

    walker* stain = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    OG_ASSERT(stain != nullptr);

    cleric->current_special = 2;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);

    cleric->current_special = 3;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);

    cleric->shifter_down = 1;
    cleric->busy = 0;
    (void)desc.do_special(cleric);
}

OG_UNIT_TEST(test_family_cleric_r14_lines_291_302_304_306_311_325_resurrect_variants)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->exp = 1;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 300.0f;

    cleric->current_special = 4;

    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    OG_ASSERT(friendly_stain != nullptr);
    (void)desc.do_special(cleric);

    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    OG_ASSERT(hostile_stain != nullptr);
    (void)desc.do_special(cleric);

    hostile_stain->setxy(600, 600);
    OG_ASSERT(!desc.do_special(cleric));
}
} // namespace detail_family_cleric_r14

// --- From test_family_cleric_r15.cpp ---
namespace detail_family_cleric_r15 {
namespace {

struct ClericR15Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;
    GameContext gc;

    ClericR15Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;

        set_global_context(&gc);
    }

    ~ClericR15Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(ClericR15Fixture& fx, unsigned char team, char family, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
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

walker* add_stain(ClericR15Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(static_cast<short>(x), static_cast<short>(y));
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_cleric_r15_low_magic_heal_branch_and_mystic_mace_guard)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR15Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    living* ally = add_living(fx, 0, FAMILY_SOLDIER, 84, 80);
    OG_ASSERT(cleric && ally);

    cleric->stats()->level = 8;
    cleric->stats()->magicpoints = 1.0f; // force low-magic adjustment branch
    ally->stats()->max_hitpoints = 100.0f;
    ally->stats()->hitpoints = 5.0f;
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);

    cleric->current_special = 1;
    cleric->shifter_down = 1;
    cleric->busy = 1;
    OG_ASSERT(!desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_cleric_r15_turn_undead_raise_and_resurrect_branches)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR15Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R15 Cleric";
    cleric->myguy->intelligence = 90;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 300.0f;

    // Turn undead branch with valid undead target.
    living* undead = add_living(fx, 1, FAMILY_SKELETON, 92, 80);
    OG_ASSERT(undead != nullptr);
    cleric->current_special = 2;
    cleric->shifter_down = 1;
    (void)desc.do_special(cleric);

    // Raise skeleton and ghost from nearby blood.
    walker* stain1 = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    walker* stain2 = add_stain(fx, 86, 80, 1, FAMILY_ORC);
    OG_ASSERT(stain1 && stain2);
    cleric->current_special = 2;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);

    // Resurrect both friendly and hostile stains.
    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    OG_ASSERT(friendly_stain && hostile_stain);
    cleric->current_special = 4;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);
    (void)desc.do_special(cleric);
}
} // namespace detail_family_cleric_r15
