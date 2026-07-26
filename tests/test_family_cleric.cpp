#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <gtest/gtest.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include <openglad/gameplay/treasure.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/game_context.h>
#include "test_gameplay_context_scope.h"
#include "test_family_hook_dispatch.h"

// --- From test_family_cleric_coverage_push.cpp ---
const FamilyDescriptor& describe_family_cleric();

namespace detail_family_cleric_coverage_push {

TEST(FamilyCleric, descriptor_difficulty_and_customize_weapon)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ASSERT_TRUE(desc.family_id == FAMILY_CLERIC);
    ASSERT_TRUE(og::test::has_do_special(desc));
    ASSERT_TRUE(og::test::has_check_special_ai(desc));
    ASSERT_TRUE(og::test::has_set_difficulty(desc));
    ASSERT_TRUE(og::test::has_customize_weapon(desc));

    living self;
    living weapon;
    self.stats()->set_level(3);
    self.stats()->set_max_hitpoints(100.0f);
    self.stats()->set_max_magicpoints(40.0f);
    self.set_damage(12.0f);
    weapon.set_lifetime(10);
    og::test::customize_weapon(desc, &self, &weapon);
    ASSERT_TRUE(weapon.ani_type() == ANI_GLOWGROW);
    ASSERT_TRUE(weapon.lifetime() == 340);

    const float old_hp = self.stats()->max_hitpoints();
    const float old_mp = self.stats()->max_magicpoints();
    const float old_damage = self.damage();
    og::test::set_difficulty(desc, &self, 2);
    ASSERT_TRUE(self.stats()->max_hitpoints() > old_hp);
    ASSERT_TRUE(self.stats()->max_magicpoints() > old_mp);
    ASSERT_TRUE(self.damage() > old_damage);
}

TEST(FamilyCleric, check_ai_default_and_do_special_busy_returns)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    living self;

    self.set_current_special(0); // default true branch in check_special_ai
    ASSERT_TRUE(og::test::check_special_ai(desc, &self));

    self.set_shifter_down(1);
    self.set_busy(1);

    self.set_current_special(1); // mystic mace branch guarded by busy
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_current_special(2); // turn undead branch guarded by busy
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_current_special(3); // turn undead high branch guarded by busy
    ASSERT_TRUE(!og::test::do_special(desc, &self));
}
} // namespace detail_family_cleric_coverage_push

// --- From test_family_cleric_r11.cpp ---
namespace detail_family_cleric_r11 {
namespace {

struct ClericFixture {
    LevelRuntimeData level{1, true};
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
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(ClericFixture& fx, unsigned char team, char family = FAMILY_CLERIC)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(80, 80);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_stain(ClericFixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->stats()->set_old_family(old_family);
    stain->set_dead(0);
    return stain;
}

} // namespace

TEST(FamilyCleric, r11_check_ai_and_heal_fail_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);

    cleric->set_current_special(1);
    cleric->stats()->set_max_magicpoints(100.0f);
    cleric->stats()->set_magicpoints(10.0f);
    ASSERT_TRUE(!og::test::check_special_ai(desc, cleric)); // line 63

    cleric->stats()->set_magicpoints(60.0f);
    ASSERT_TRUE(og::test::check_special_ai(desc, cleric));
    ASSERT_TRUE(cleric->shifter_down() == 1);

    cleric->set_shifter_down(0);
    cleric->stats()->set_level(5);
    cleric->stats()->set_magicpoints(20.0f);

    // only cleric in range => howmany <= 1 path
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // ally at full HP => didheal remains 0 path
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    ally->setxy(90, 80);
    ally->stats()->set_hitpoints(ally->stats()->max_hitpoints());
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
}

TEST(FamilyCleric, r11_heal_and_mace_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    ASSERT_TRUE(cleric != nullptr && ally != nullptr);

    cleric->setxy(80, 80);
    ally->setxy(90, 80);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->stats()->set_level(6);
    cleric->stats()->set_magicpoints(60.0f);
    cleric->stats()->set_max_magicpoints(120.0f);
    cleric->set_current_special(1);
    cleric->set_shifter_down(0);
    ally->stats()->set_max_hitpoints(100.0f);
    ally->stats()->set_hitpoints(40.0f);

    const float ally_before = ally->stats()->hitpoints();
    const float mp_before = cleric->stats()->magicpoints();
    ASSERT_TRUE(og::test::do_special(desc, cleric));
    ASSERT_TRUE(ally->stats()->hitpoints() > ally_before);
    ASSERT_TRUE(cleric->stats()->magicpoints() < mp_before);

    // mystic mace int requirement fail (lines 147-152)
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    cleric->myguy->intelligence = 30;
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // mystic mace success path (lines 158-171)
    cleric->myguy->intelligence = 70;
    cleric->stats()->set_magicpoints(80.0f);
    cleric->stats()->set_special_cost(1, 2);
    const float mp_before_mace = cleric->stats()->magicpoints();
    ASSERT_TRUE(og::test::do_special(desc, cleric));
    ASSERT_TRUE(cleric->busy() >= 5);
    ASSERT_TRUE(cleric->stats()->magicpoints() < mp_before_mace);
}

TEST(FamilyCleric, r11_turn_and_raise_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->intelligence = 70;
    cleric->stats()->set_level(8);
    cleric->stats()->set_magicpoints(200.0f);

    // case 2, turn undead with no targets => -1 branch
    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // case 2 raise skeleton, no blood => false
    cleric->set_shifter_down(0);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // add nearby blood and summon skeleton success path
    walker* stain = add_stain(fx, 88, 80, 1, FAMILY_SOLDIER);
    ASSERT_TRUE(stain != nullptr);
    (void)og::test::do_special(desc, cleric);

    // case 3 raise ghost distance fail + no blood
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    walker* far_stain = add_stain(fx, 300, 300, 1, FAMILY_SOLDIER);
    ASSERT_TRUE(far_stain != nullptr);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
    far_stain->set_dead(1);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // case 3 turn undead int fail branch
    cleric->set_shifter_down(1);
    cleric->myguy->intelligence = 10;
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
}

TEST(FamilyCleric, r11_resurrect_friendly_and_hostile_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);

    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->exp = 10000;
    cleric->stats()->set_level(9);
    cleric->stats()->set_magicpoints(300.0f);
    cleric->set_current_special(4);

    // no blood => false path
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // friendly blood resurrect path + exp penalty floor branch
    (void)add_stain(fx, 88, 80, 0, FAMILY_SOLDIER);
    cleric->myguy->exp = 0;
    (void)og::test::do_special(desc, cleric);

    // hostile blood branch summons ghost
    (void)add_stain(fx, 86, 84, 1, FAMILY_ORC);
    (void)og::test::do_special(desc, cleric);
}
} // namespace detail_family_cleric_r11

// --- From test_family_cleric_r12.cpp ---
const FamilyDescriptor& describe_family_mage();
const FamilyDescriptor& describe_family_soldier();

namespace detail_family_cleric_r12 {
namespace {

struct ClericR12Fixture {
    LevelRuntimeData level{1, true};
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
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_stain(ClericR12Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->stats()->set_old_family(old_family);
    stain->set_dead(0);
    return stain;
}

} // namespace

TEST(FamilyCleric, r12_ghost_raise_and_resurrect_penalty_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR12Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R12";
    cleric->myguy->exp = 1;
    cleric->myguy->intelligence = 80;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    // Case 3: raise ghost success path.
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    walker* near_stain = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    ASSERT_TRUE(near_stain != nullptr);
    (void)og::test::do_special(desc, cleric);

    // Case 3: shifter_down turn-undead busy fail.
    cleric->set_shifter_down(1);
    cleric->set_busy(2);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
    cleric->set_busy(0);

    // Case 4: friendly resurrect with exp floor path.
    cleric->set_current_special(4);
    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    ASSERT_TRUE(friendly_stain != nullptr);
    (void)og::test::do_special(desc, cleric);

    // Case 4: hostile resurrect ghost path.
    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    ASSERT_TRUE(hostile_stain != nullptr);
    (void)og::test::do_special(desc, cleric);

    // Case 1 heal branch with heal_numbers on and at least one ally damaged.
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    ally->setxy(90, 80);
    ally->stats()->set_max_hitpoints(100.0f);
    ally->stats()->set_hitpoints(50.0f);
    cleric->set_current_special(1);
    cleric->set_shifter_down(0);
    cleric->stats()->set_magicpoints(100.0f);
    cleric->stats()->set_max_magicpoints(100.0f);
    cfg.apply_setting("effects", "heal_numbers", "on");
    ASSERT_TRUE(og::test::do_special(desc, cleric));
}

TEST(FamilyCleric, family_mage_r12_descriptor_paths)
{
    const FamilyDescriptor& mage = describe_family_mage();
    ClericR12Fixture fx;

    living* self = add_living(fx, 0, FAMILY_MAGE);
    ASSERT_TRUE(self != nullptr);
    self->stats()->set_level(8);
    self->stats()->set_max_hitpoints(100.0f);
    self->stats()->set_hitpoints(20.0f);
    self->stats()->set_magicpoints(200.0f);
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    self->myguy->intelligence = 90;
    self->set_user(0);

    // AI branch: no foes in range.
    ASSERT_TRUE(og::test::check_special_ai(mage, self));

    // Hit response low-hp branch should attempt special.
    walker* foe = add_living(fx, 1, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr);
    og::test::hit_response(mage, self->stats(), foe);

    // Teleport handler callback branch.
    self->set_ani_type(ANI_TELE_OUT);
    self->set_cycle(5);
    ASSERT_TRUE(og::test::handle_teleport(mage, self));
    ASSERT_TRUE(self->ani_type() == ANI_TELE_IN);

    // do_special case 1 marker-placement branch.
    self->set_current_special(1);
    self->set_shifter_down(1);
    self->set_busy(0);
    self->set_ani_type(ANI_WALK);
    ASSERT_TRUE(og::test::do_special(mage, self));

    // do_special case 1 teleport-out branch.
    self->set_shifter_down(0);
    self->set_ani_type(ANI_WALK);
    ASSERT_TRUE(og::test::do_special(mage, self));
    ASSERT_TRUE(self->ani_type() == ANI_TELE_OUT);

    // do_special case 5 branch with no targets can fail.
    self->set_current_special(5);
    (void)og::test::do_special(mage, self);
}

TEST(FamilyCleric, family_soldier_and_treasure_r12_paths)
{
    const FamilyDescriptor& soldier = describe_family_soldier();
    ClericR12Fixture fx;

    auto s = std::make_unique<living>();
    s->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, s.get());
    s->setxy(60, 60);
    s->set_team_num(0);
    s->stats()->set_level(6);
    // The charge special divides lastx/lasty by stepsize; a hand-built
    // walker starts at 0, which the old C++ cast turned into UB garbage and
    // the Lua transliteration reports as an out-of-range og.trunc.
    s->set_stepsize(1.0f);
    s->set_lastx(1.0f);
    s->set_lasty(0.0f);
    living* self = s.get();
    fx.level.world().oblist.push_back(std::move(s));

    walker* enemy = add_living(fx, 1, FAMILY_ORC);
    enemy->setxy(80, 60);

    og::test::on_create(soldier, self);
    ASSERT_TRUE(self->weapons_left() >= 1);

    self->set_current_special(1);
    ASSERT_TRUE(og::test::do_special(soldier, self));

    self->set_current_special(3);
    self->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(soldier, self));
    self->set_busy(0);

    self->set_foe(enemy);
    ASSERT_EQ(20, self->distance_to_ob(enemy));
    ASSERT_FALSE(og::test::check_special_ai(soldier, self))
        << "soldier AI should reject targets at the lower charge-distance boundary";

    walker* weap = fx.level.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weap != nullptr);
    self->set_weapons_left(0);
    const float mp_before = self->stats()->magicpoints();
    ASSERT_TRUE(!og::test::on_fire_weapon(soldier, self, weap));
    ASSERT_TRUE(weap->dead() == 1);
    ASSERT_TRUE(self->stats()->magicpoints() >= mp_before);

    // treasure.cpp paths
    treasure lonely;
    lonely.stats()->set_level(2);
    ASSERT_TRUE(lonely.find_teleport_target() == nullptr);
    ASSERT_TRUE(lonely.act());
    ASSERT_TRUE(lonely.eat_me(self));

    auto t1 = std::make_unique<treasure>();
    auto t2 = std::make_unique<treasure>();
    bind_test_entity_sim_context(fx.level, t1.get());
    bind_test_entity_sim_context(fx.level, t2.get());
    t1->set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    t2->set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    t1->stats()->set_level(3);
    t2->stats()->set_level(3);
    t2->set_dead(0);
    treasure* t1_raw = t1.get();
    treasure* t2_raw = t2.get();
    fx.level.world().fxlist.push_back(std::move(t1));
    fx.level.world().fxlist.push_back(std::move(t2));
    ASSERT_TRUE(t1_raw->find_teleport_target() == t2_raw);
}

TEST(FamilyCleric, r12_shoved_ai_and_turn_undead_guard_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR12Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->set_user(0);
    cleric->stats()->set_level(6);
    cleric->stats()->set_magicpoints(120.0f);

    // on_shoved callback path.
    og::test::on_shoved(desc, cleric);

    // check_special_ai healing branch with >1 friends nearby.
    cleric->set_current_special(1);
    living* friend1 = add_living(fx, 0, FAMILY_SOLDIER);
    living* friend2 = add_living(fx, 0, FAMILY_ARCHER);
    friend1->setxy(82, 80);
    friend2->setxy(84, 80);
    ASSERT_TRUE(og::test::check_special_ai(desc, cleric));
    ASSERT_TRUE(cleric->shifter_down() == 0);

    // check_special_ai mace branch with high MP and not enough heal targets.
    friend2->setxy(300, 300);
    friend1->setxy(300, 300);
    cleric->stats()->set_max_magicpoints(100.0f);
    cleric->stats()->set_magicpoints(80.0f);
    ASSERT_TRUE(og::test::check_special_ai(desc, cleric));
    ASSERT_TRUE(cleric->shifter_down() == 1);

    // Mystic mace INT guard message path.
    cleric->set_current_special(1);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    cleric->myguy->intelligence = 40;
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // Turn-undead INT guard path for special 2.
    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    cleric->myguy->intelligence = 50;
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // Turn-undead INT guard path for special 3.
    cleric->set_current_special(3);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    cleric->myguy->intelligence = 50;
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
}
} // namespace detail_family_cleric_r12

// --- From test_family_cleric_r14.cpp ---
namespace detail_family_cleric_r14 {
namespace {

struct ClericR14Fixture {
    LevelRuntimeData level{1, true};
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

        push_test_context(&gc);
    }

    ~ClericR14Fixture()
    {
        pop_test_context();
    }
};

living* add_living(ClericR14Fixture& fx, unsigned char team, char family = FAMILY_CLERIC)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(80, 80);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_stain(ClericR14Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->stats()->set_old_family(old_family);
    stain->set_dead(0);
    return stain;
}

} // namespace

TEST(FamilyCleric, r14_lines_110_132_160_heal_plural_and_mystic_mace_branches)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    living* ally1 = add_living(fx, 0, FAMILY_SOLDIER);
    living* ally2 = add_living(fx, 0, FAMILY_SOLDIER);
    ASSERT_TRUE(cleric && ally1 && ally2);

    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R14 Cleric";
    cleric->myguy->intelligence = 90;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(200.0f);

    ally1->setxy(84, 80);
    ally2->setxy(86, 80);
    ally1->stats()->set_max_hitpoints(100.0f);
    ally2->stats()->set_max_hitpoints(100.0f);
    ally1->stats()->set_hitpoints(20.0f);
    ally2->stats()->set_hitpoints(30.0f);

    cfg.apply_setting("effects", "heal_numbers", "on");
    cleric->set_current_special(1);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(og::test::do_special(desc, cleric));

    cleric->set_current_special(1);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    const std::size_t ob_count_before_mace = fx.level.world().oblist.size();
    const int shots_before_mace = cleric->myguy->scen_shots;
    const float magic_before_mace = cleric->stats()->magicpoints();
    ASSERT_TRUE(og::test::do_special(desc, cleric));
    ASSERT_GT(fx.level.world().oblist.size(), ob_count_before_mace);
    ASSERT_EQ(shots_before_mace + 1, cleric->myguy->scen_shots);
    ASSERT_LT(cleric->stats()->magicpoints(), magic_before_mace);
    ASSERT_GT(cleric->busy(), 0.0f);
}

TEST(FamilyCleric, r14_lines_187_189_192_203_206_243_246_turn_undead_and_raise_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "UndeadTest";
    cleric->myguy->intelligence = 90;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    walker* stain = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    ASSERT_TRUE(stain != nullptr);

    cleric->set_current_special(2);
    cleric->set_shifter_down(0);
    (void)og::test::do_special(desc, cleric);

    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    (void)og::test::do_special(desc, cleric);

    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    (void)og::test::do_special(desc, cleric);
}

TEST(FamilyCleric, r14_lines_291_302_304_306_311_325_resurrect_variants)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->exp = 1;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    cleric->set_current_special(4);

    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    ASSERT_TRUE(friendly_stain != nullptr);
    (void)og::test::do_special(desc, cleric);

    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    ASSERT_TRUE(hostile_stain != nullptr);
    (void)og::test::do_special(desc, cleric);

    hostile_stain->setxy(600, 600);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
}
} // namespace detail_family_cleric_r14

// --- From test_family_cleric_r15.cpp ---
namespace detail_family_cleric_r15 {
namespace {

struct ClericR15Fixture {
    LevelRuntimeData level{1, true};
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

        push_test_context(&gc);
    }

    ~ClericR15Fixture()
    {
        pop_test_context();
    }
};

living* add_living(ClericR15Fixture& fx, unsigned char team, char family, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_stain(ClericR15Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_team_num(team);
    stain->setxy(static_cast<short>(x), static_cast<short>(y));
    stain->stats()->set_old_family(old_family);
    stain->set_dead(0);
    return stain;
}

} // namespace

TEST(FamilyCleric, r15_low_magic_heal_branch_and_mystic_mace_guard)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR15Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    living* ally = add_living(fx, 0, FAMILY_SOLDIER, 84, 80);
    ASSERT_TRUE(cleric && ally);

    cleric->stats()->set_level(8);
    cleric->stats()->set_magicpoints(1.0f); // force low-magic adjustment branch
    ally->stats()->set_max_hitpoints(100.0f);
    ally->stats()->set_hitpoints(5.0f);
    cleric->set_current_special(1);
    cleric->set_shifter_down(0);
    (void)og::test::do_special(desc, cleric);

    cleric->set_current_special(1);
    cleric->set_shifter_down(1);
    cleric->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
}

TEST(FamilyCleric, r15_turn_undead_raise_and_resurrect_branches)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR15Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R15 Cleric";
    cleric->myguy->intelligence = 90;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    // Turn undead branch with valid undead target.
    living* undead = add_living(fx, 1, FAMILY_SKELETON, 92, 80);
    ASSERT_TRUE(undead != nullptr);
    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    (void)og::test::do_special(desc, cleric);

    // Raise skeleton and ghost from nearby blood.
    walker* stain1 = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    walker* stain2 = add_stain(fx, 86, 80, 1, FAMILY_ORC);
    ASSERT_TRUE(stain1 && stain2);
    cleric->set_current_special(2);
    cleric->set_shifter_down(0);
    (void)og::test::do_special(desc, cleric);
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    (void)og::test::do_special(desc, cleric);

    // Resurrect both friendly and hostile stains.
    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    ASSERT_TRUE(friendly_stain && hostile_stain);
    cleric->set_current_special(4);
    cleric->set_shifter_down(0);
    (void)og::test::do_special(desc, cleric);
    (void)og::test::do_special(desc, cleric);
}
} // namespace detail_family_cleric_r15
