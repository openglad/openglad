#include <openglad/gameplay/families/family_descriptor.h>

#include "test_family_lookup.h"
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/game_world.h>
#include <cstddef>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <gtest/gtest.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include <openglad/gameplay/treasure.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/game_context.h>
#include "test_gameplay_context_scope.h"
#include "test_family_hook_dispatch.h"

// ---------------------------------------------------------------------------
// Shared oracles for the cleric's summon specials.
//
// Every raise/resurrect arm ends in a body standing where the corpse was, so
// the observable is "which family appeared, on whose team, owned by whom" —
// found by scanning the world's three entity lists (the Order -> list
// mapping is not one-to-one, so scan all three and select on the entity's
// own order).
// ---------------------------------------------------------------------------
namespace {

walker* find_live_family(GameWorld& w, Order order, int family)
{
    for (const GameWorld::EntityList* list : {&w.oblist, &w.fxlist,
                                              &w.weaplist}) {
        for (const auto& ob : *list) {
            if (ob != nullptr && ob->dead() == 0 &&
                ob->query_order() == order &&
                ob->family() == static_cast<char>(family))
                return ob.get();
        }
    }
    return nullptr;
}

std::size_t count_live_family(GameWorld& w, Order order, int family)
{
    std::size_t n = 0;
    for (const GameWorld::EntityList* list : {&w.oblist, &w.fxlist,
                                              &w.weaplist}) {
        for (const auto& ob : *list) {
            if (ob != nullptr && ob->dead() == 0 &&
                ob->query_order() == order &&
                ob->family() == static_cast<char>(family))
                n++;
        }
    }
    return n;
}

// nearby_corpse() (living-05-cleric.lua:52-62) accepts a bloodstain only
// when its tile is PASSABLE and it sits within `max_distance` (Manhattan).
// The caster's own body blocks the tile it stands on, so hard-coding an
// offset would depend on sprite sizes; walk outward for the first spot that
// satisfies both conditions instead.
bool place_corpse_in_reach(GameWorld& w, walker* caster, walker* stain,
                           std::int32_t max_distance)
{
    for (std::int32_t d = 1; d < max_distance; d++) {
        const std::int32_t ring[4][2] = {{d, 0}, {0, d}, {-d, 0}, {0, -d}};
        for (const auto& offset : ring) {
            const float x = static_cast<float>(caster->xpos() + offset[0]);
            const float y = static_cast<float>(caster->ypos() + offset[1]);
            if (x < 0.0f || y < 0.0f)
                continue;
            stain->setxy(x, y);
            if (caster->distance_to_ob(stain) < max_distance &&
                w.query_passable(x, y, stain))
                return true;
        }
    }
    return false;
}


// The world RNG is the LCG the sim draws from. Find a seed whose FIRST
// next(n) draw is `want`, so a test can steer one roll inside the sim
// without duplicating the generator's formula.
std::uint32_t seed_whose_first_draw_is(std::uint32_t n, std::uint32_t want)
{
    for (std::uint32_t seed = 0; seed < 4096u; seed++) {
        og::sim::SimRandom probe(seed);
        if (probe.next(n) == want)
            return seed;
    }
    ADD_FAILURE() << "no seed produced the wanted first draw";
    return 0;
}

} // namespace

// --- From test_family_cleric_coverage_push.cpp ---

namespace detail_family_cleric_coverage_push {

TEST(FamilyCleric, descriptor_difficulty_and_customize_weapon)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
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
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
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
    // fxlist, not oblist: GameWorld::find_nearest_blood (game_world.cpp:1259)
    // only ever scans fxlist, which is where walker::death files a real
    // bloodstain. A stain parked in oblist is a corpse no cleric can see.
    walker* stain = fx.level.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->stats()->set_old_family(old_family);
    stain->set_dead(0);
    return stain;
}

} // namespace

TEST(FamilyCleric, r11_check_ai_and_heal_fail_paths)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
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
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
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
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    ClericFixture fx;
    GameWorld& world = fx.level.world();
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

    // RAISE UNDEAD success: a bloodstain on a passable tile inside
    // raise_skeleton_range (60). The old body parked the stain at a
    // hard-coded (88, 80) and `(void)`-cast the cast, so a refusal on
    // passability looked identical to a summon -- and the raise-ghost
    // negatives below only held BECAUSE of that hidden refusal.
    walker* stain = add_stain(fx, 88, 80, 1, FAMILY_SOLDIER);
    ASSERT_TRUE(stain != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, stain, 60))
        << "no passable bloodstain spot inside raise_skeleton_range";
    const short blood_x = stain->xpos();
    const short blood_y = stain->ypos();
    cleric->myguy->exp = 0;

    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "blood on a passable tile inside range must raise a skeleton";
    EXPECT_EQ(1, stain->dead()) << "the blood is spent by the raise";
    walker* risen = find_live_family(world, Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, risen) << "a skeleton must be standing where the blood was";
    EXPECT_EQ(cleric, risen->owner()) << "the skeleton serves its raiser";
    EXPECT_EQ(0, static_cast<int>(risen->team_num()))
        << "the skeleton fights on the cleric's team, not the corpse's";
    EXPECT_EQ(blood_x, risen->xpos()) << "the skeleton rises at the bloodstain";
    EXPECT_EQ(blood_y, risen->ypos()) << "the skeleton rises at the bloodstain";
    EXPECT_EQ(45u, cleric->myguy->exp)
        << "a raised skeleton pays its cleric a flat 45 experience";

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
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    ClericFixture fx;
    GameWorld& world = fx.level.world();
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);

    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->exp = 10000;
    cleric->stats()->set_level(9);
    cleric->stats()->set_magicpoints(300.0f);
    cleric->set_current_special(4);

    // no blood => false path
    ASSERT_TRUE(!og::test::do_special(desc, cleric));

    // Friendly blood: the corpse's OLD family comes back, on the corpse's
    // team, at half its transferred max HP, and the stain is consumed.
    // The exp penalty is target_level^2 * 100 = 100 here, more than the
    // cleric owns, so it floors at 0 before the flat +90 the resurrect pays.
    walker* friendly_stain = add_stain(fx, 88, 80, 0, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, friendly_stain);
    friendly_stain->stats()->set_level(1);
    friendly_stain->stats()->set_max_hitpoints(100.0f);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, friendly_stain, 30))
        << "no passable bloodstain spot inside resurrect_range";
    cleric->myguy->exp = 0;

    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "friendly blood inside resurrect_range must revive";
    EXPECT_EQ(1, friendly_stain->dead()) << "the blood is spent";
    walker* revived = find_live_family(world, Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, revived) << "the corpse's old family walks again";
    EXPECT_EQ(0, static_cast<int>(revived->team_num()))
        << "the revived body joins the blood's team";
    EXPECT_FLOAT_EQ(50.0f, revived->stats()->hitpoints())
        << "a resurrection returns at half of the transferred max HP";
    EXPECT_EQ(90u, cleric->myguy->exp)
        << "penalty 100 floors an exp-0 cleric at 0, then resurrect pays 90";

    // Hostile blood: no revival, a ghost enslaved to the cleric instead.
    walker* hostile_stain = add_stain(fx, 86, 84, 1, FAMILY_ORC);
    ASSERT_NE(nullptr, hostile_stain);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, hostile_stain, 30));
    ASSERT_EQ(0u, count_live_family(world, Order::Living, FAMILY_GHOST))
        << "no ghost exists before the hostile cast";

    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "hostile blood inside resurrect_range must raise a ghost";
    EXPECT_EQ(1, hostile_stain->dead()) << "the blood is spent";
    walker* ghost = find_live_family(world, Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, ghost) << "hostile blood becomes a ghost, not an orc";
    EXPECT_EQ(cleric, ghost->owner()) << "the ghost serves its raiser";
    EXPECT_EQ(0, static_cast<int>(ghost->team_num()))
        << "and fights on the cleric's team";
    EXPECT_EQ(0u, count_live_family(world, Order::Living, FAMILY_ORC))
        << "hostile blood never revives the enemy it came from";
    EXPECT_EQ(180u, cleric->myguy->exp)
        << "the second resurrect pays another flat 90";
}
} // namespace detail_family_cleric_r11

// --- From test_family_cleric_r12.cpp ---

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
    // fxlist, not oblist: GameWorld::find_nearest_blood (game_world.cpp:1259)
    // only ever scans fxlist, which is where walker::death files a real
    // bloodstain. A stain parked in oblist is a corpse no cleric can see.
    walker* stain = fx.level.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->stats()->set_old_family(old_family);
    stain->set_dead(0);
    return stain;
}

} // namespace

TEST(FamilyCleric, r12_ghost_raise_and_resurrect_penalty_paths)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    ClericR12Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R12";
    cleric->myguy->exp = 1;
    cleric->myguy->intelligence = 80;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    GameWorld& world = fx.level.world();

    // Case 3: raise ghost success path. RAISE GHOST pays a flat 60 exp, the
    // stain is consumed, and the ghost answers to the cleric.
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    walker* near_stain = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    ASSERT_TRUE(near_stain != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, near_stain, 30))
        << "no passable bloodstain spot inside raise_ghost_range";
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "blood in reach must raise a ghost";
    EXPECT_EQ(1, near_stain->dead()) << "the blood is spent";
    walker* raised = find_live_family(world, Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, raised) << "a ghost rises from the blood";
    EXPECT_EQ(cleric, raised->owner()) << "the ghost serves its raiser";
    EXPECT_EQ(0, static_cast<int>(raised->team_num()))
        << "and fights on the cleric's team";
    EXPECT_EQ(61u, cleric->myguy->exp)
        << "starting exp 1 plus the flat 60 a raise_ghost pays";

    // Case 3: shifter_down turn-undead busy fail.
    cleric->set_shifter_down(1);
    cleric->set_busy(2);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
    EXPECT_EQ(61u, cleric->myguy->exp) << "a refused cast pays nothing";
    cleric->set_busy(0);

    // Case 4: friendly resurrect with exp floor path. The penalty is
    // target_level^2 * 100 = 100, more than the 61 the cleric holds, so the
    // exp floors at 0 before the flat +90 the resurrect itself pays.
    cleric->set_current_special(4);
    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    ASSERT_TRUE(friendly_stain != nullptr);
    friendly_stain->stats()->set_level(1);
    friendly_stain->stats()->set_max_hitpoints(100.0f);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, friendly_stain, 30));
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "friendly blood in reach must revive";
    EXPECT_EQ(1, friendly_stain->dead()) << "the blood is spent";
    walker* revived = find_live_family(world, Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, revived) << "the corpse's old family walks again";
    EXPECT_FLOAT_EQ(50.0f, revived->stats()->hitpoints())
        << "a resurrection returns at half of the transferred max HP";
    EXPECT_EQ(90u, cleric->myguy->exp)
        << "61 is under the 100 penalty, so it floors at 0 and gains 90";

    // Case 4: hostile resurrect ghost path.
    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    ASSERT_TRUE(hostile_stain != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, hostile_stain, 30));
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "hostile blood in reach must raise a ghost";
    EXPECT_EQ(1, hostile_stain->dead()) << "the blood is spent";
    EXPECT_EQ(2u, count_live_family(world, Order::Living, FAMILY_GHOST))
        << "the hostile corpse adds a SECOND ghost, it does not revive an orc";
    EXPECT_EQ(0u, count_live_family(world, Order::Living, FAMILY_ORC))
        << "hostile blood never revives the enemy it came from";
    EXPECT_EQ(180u, cleric->myguy->exp)
        << "the hostile resurrect pays the same flat 90 (no penalty arm)";

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
    const FamilyDescriptor& mage = describe_family(FAMILY_MAGE);
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

    // do_special case 5 is heartburst. It used to be `(void)`-cast under a
    // comment claiming "no targets", but the teleport-out branch above had
    // already carried the caster 576 px away from the orc -- so the call was
    // refusing for a reason the test never stated. Put the foe back inside the
    // 80 + 2 * level = 96 pixel reach and pin the burst the cast plants.
    self->set_current_special(5);
    foe->setxy(static_cast<short>(self->xpos() + 8), self->ypos());
    ASSERT_GE(96, self->distance_to_ob(foe))
        << "the foe must sit inside heartburst's reach for this arm";
    self->stats()->set_magicpoints(500.0f);
    self->stats()->set_special_cost(5, 100);
    const float busy_before_burst = self->busy();
    ASSERT_EQ(0u, count_live_family(fx.level.world(), Order::FX, FAMILY_EXPLOSION))
        << "no burst exists before the cast";
    ASSERT_TRUE(og::test::do_special(mage, self))
        << "heartburst must fire when a foe sits inside its range";
    ASSERT_EQ(1u, count_live_family(fx.level.world(), Order::FX, FAMILY_EXPLOSION))
        << "one foe in range means exactly one burst";
    walker* burst = find_live_family(fx.level.world(), Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, burst) << "heartburst plants an explosion on its target";
    EXPECT_EQ(self, burst->owner()) << "the burst belongs to the caster";
    EXPECT_EQ(ANI_EXPLODE, static_cast<int>(burst->ani_type()))
        << "the burst plays ANI_EXPLODE";
    EXPECT_EQ(100, static_cast<int>(burst->skip_exit()))
        << "skip_exit 100 is the legacy 'do not hurt the caster' marker";
    // The burst pool is (500 MP - 100 slot cost) / 2 = 200, split across the
    // one foe, and the caster pays that pool out of its own magic.
    EXPECT_FLOAT_EQ(200.0f, burst->damage())
        << "each burst carries mp_pool_damage / foe_count";
    EXPECT_FLOAT_EQ(300.0f, self->stats()->magicpoints())
        << "the caster pays exactly the damage each burst carries";
    EXPECT_FLOAT_EQ(busy_before_burst + 5.0f, self->busy())
        << "heartburst costs the caster 5 ticks of busy";
}

TEST(FamilyCleric, family_soldier_and_treasure_r12_paths)
{
    const FamilyDescriptor& soldier = describe_family(FAMILY_SOLDIER);
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
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
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
    // fxlist, not oblist: GameWorld::find_nearest_blood (game_world.cpp:1259)
    // only ever scans fxlist, which is where walker::death files a real
    // bloodstain. A stain parked in oblist is a corpse no cleric can see.
    walker* stain = fx.level.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->stats()->set_old_family(old_family);
    stain->set_dead(0);
    return stain;
}

} // namespace

TEST(FamilyCleric, r14_lines_110_132_160_heal_plural_and_mystic_mace_branches)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
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

// RAISE UNDEAD and RAISE GHOST each spend the nearest bloodstain on a body
// that serves the raiser, and each pays its own flat experience; TURN UNDEAD
// with nothing hostile to turn refuses.
TEST(FamilyCleric, r14_raise_skeleton_then_ghost_then_turn_undead_with_no_foes)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    ClericR14Fixture fx;
    GameWorld& world = fx.level.world();

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "UndeadTest";
    cleric->myguy->intelligence = 90;
    cleric->myguy->exp = 0;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    walker* stain = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    ASSERT_TRUE(stain != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, stain, 30))
        << "no passable bloodstain spot inside raise_skeleton_range";

    cleric->set_current_special(2);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "blood in reach must raise a skeleton";
    EXPECT_EQ(1, stain->dead()) << "the blood is spent";
    walker* skeleton = find_live_family(world, Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, skeleton) << "a skeleton rises from the blood";
    EXPECT_EQ(cleric, skeleton->owner()) << "the skeleton serves its raiser";
    EXPECT_EQ(0, static_cast<int>(skeleton->team_num()))
        << "and fights on the cleric's team";
    EXPECT_EQ(45u, cleric->myguy->exp) << "raise_skeleton pays a flat 45";

    // The first stain was consumed, so the ghost needs a second corpse.
    walker* stain2 = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    ASSERT_TRUE(stain2 != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, stain2, 30))
        << "no passable bloodstain spot inside raise_ghost_range";
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "a second corpse in reach must raise a ghost";
    EXPECT_EQ(1, stain2->dead()) << "the second blood is spent";
    walker* ghost = find_live_family(world, Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, ghost) << "a ghost rises from the second blood";
    EXPECT_EQ(cleric, ghost->owner()) << "the ghost serves its raiser";
    EXPECT_EQ(105u, cleric->myguy->exp)
        << "45 for the skeleton plus the flat 60 a raise_ghost pays";

    // shifter_down turns the same slot into TURN UNDEAD. The only undead in
    // the world are the cleric's OWN summons, and turn_undead counts foes
    // only, so find_foes_in_range reports no targets: walker::turn_undead
    // answers -1 and the special refuses.
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    EXPECT_FALSE(og::test::do_special(desc, cleric))
        << "turn undead with no hostile undead in range must refuse";
    EXPECT_EQ(105u, cleric->myguy->exp) << "a refused turn pays nothing";
    EXPECT_EQ(0, skeleton->dead())
        << "and a cleric never turns its own risen servants";
}

// RESURRECT in all three shapes: friendly blood revives its old family,
// hostile blood becomes an enslaved ghost, and blood out of reach refuses.
TEST(FamilyCleric, r14_resurrect_revives_friendly_blood_and_enslaves_hostile_blood)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->exp = 1;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    cleric->set_current_special(4);
    GameWorld& world = fx.level.world();

    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    ASSERT_TRUE(friendly_stain != nullptr);
    friendly_stain->stats()->set_level(1);
    friendly_stain->stats()->set_max_hitpoints(100.0f);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, friendly_stain, 30));
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "friendly blood in reach must revive";
    EXPECT_EQ(1, friendly_stain->dead()) << "the blood is spent";
    walker* revived = find_live_family(world, Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, revived) << "the corpse's old family walks again";
    EXPECT_EQ(0, static_cast<int>(revived->team_num()))
        << "the revived body joins the blood's team";
    EXPECT_FLOAT_EQ(50.0f, revived->stats()->hitpoints())
        << "a resurrection returns at half of the transferred max HP";
    EXPECT_EQ(90u, cleric->myguy->exp)
        << "the 100 penalty floors an exp-1 cleric at 0, then +90";

    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    ASSERT_TRUE(hostile_stain != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, hostile_stain, 30));
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "hostile blood in reach must raise a ghost";
    EXPECT_EQ(1, hostile_stain->dead()) << "the blood is spent";
    walker* ghost = find_live_family(world, Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, ghost) << "hostile blood becomes a ghost, not an orc";
    EXPECT_EQ(cleric, ghost->owner()) << "the ghost serves its raiser";
    EXPECT_EQ(0u, count_live_family(world, Order::Living, FAMILY_ORC))
        << "hostile blood never revives the enemy it came from";
    EXPECT_EQ(180u, cleric->myguy->exp)
        << "the hostile resurrect pays the same flat 90";

    // Out of reach: the third corpse is beyond resurrect_range, so nothing
    // is raised and nothing is spent. (The first two stains are already
    // dead, so this one is the nearest blood.)
    walker* far_stain = add_stain(fx, 600, 600, 0, FAMILY_SOLDIER);
    ASSERT_TRUE(far_stain != nullptr);
    const std::size_t ghosts_before = count_live_family(world, Order::Living,
                                                        FAMILY_GHOST);
    ASSERT_TRUE(!og::test::do_special(desc, cleric))
        << "blood outside resurrect_range must refuse";
    EXPECT_EQ(0, far_stain->dead()) << "an out-of-reach corpse is untouched";
    EXPECT_EQ(ghosts_before,
              count_live_family(world, Order::Living, FAMILY_GHOST))
        << "a refused resurrect summons nothing";
    EXPECT_EQ(180u, cleric->myguy->exp) << "and pays nothing";
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

// The slot price is the FLOOR of a heal (P5).
//
// compute_heal_amount (src/core/combat_math.cpp:97-105) builds
// base = trunc(mp)/4 + rand(trunc(mp)/4) and prices the cast at cost =
// base/2, so for any pool below 8 MP the surcharge is 0. That surcharge is
// NOT the whole price: the HEAL slot's declared mp_cost (2) is, and the
// engine charges it in walker::special on a true return
// (src/gameplay/walker_specials.cpp:180-181). So the rule this test pins,
// through the real engine path rather than the bare hook, is:
//
//   * below the slot price the cast is refused at the gate, with
//     SpecialFailure::NoMP, nobody healed and nothing charged;
//   * AT the slot price the heal lands for base + level*5 and the pool is
//     emptied by exactly the slot price (Lua charges 0, the engine 2).
//
// Before the P5 fix the second arm was a silent fizzle: the hook broke on
// `cost <= 0`, returned false, and a cleric the gate had just approved
// healed nobody and paid nothing.
TEST(FamilyCleric, r15_the_slot_price_is_the_floor_of_a_heal)
{
    og::test::mount_core_pack();
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    ClericR15Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    living* ally = add_living(fx, 0, FAMILY_SOLDIER, 84, 80);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, ally);

    cleric->stats()->set_level(8);
    ally->stats()->set_max_hitpoints(100.0f);
    ally->stats()->set_hitpoints(5.0f);
    cleric->set_current_special(1);
    cleric->set_shifter_down(0);
    // add_living() builds a bare walker; the loader is what copies the pack's
    // declared slot prices onto a real cleric (src/resources/gloader.cpp:841),
    // so do the same here and pin the declared value while we are at it.
    ASSERT_EQ(2u, desc.special_cost[1])
        << "the HEAL slot's declared price (living-05-cleric.lua mp_cost = 2) "
           "is the floor this test is about";
    cleric->stats()->set_special_cost(1, desc.special_cost[1]);

    // Below the floor: the engine refuses before the hook ever runs.
    cleric->stats()->set_magicpoints(1.0f);
    walker::SpecialFailure why = walker::SpecialFailure::None;
    ASSERT_FALSE(cleric->special(&why))
        << "a pool under the slot price cannot cast at all";
    EXPECT_EQ(walker::SpecialFailure::NoMP, why)
        << "and it is the mp_cost gate that refuses, not the script";
    EXPECT_FLOAT_EQ(5.0f, ally->stats()->hitpoints())
        << "a refused cast heals nobody";
    EXPECT_FLOAT_EQ(1.0f, cleric->stats()->magicpoints())
        << "a refused cast charges nothing";

    // At the floor: trunc(2)/4 == 0 prices the surcharge at 0, and the heal
    // still lands for base(0) + level*5(40).
    cleric->stats()->set_magicpoints(2.0f);
    cleric->set_current_special(1);
    why = walker::SpecialFailure::None;
    ASSERT_TRUE(cleric->special(&why))
        << "a heal the mp_cost gate approved must land";
    EXPECT_EQ(walker::SpecialFailure::None, why);
    EXPECT_FLOAT_EQ(45.0f, ally->stats()->hitpoints())
        << "amount = base(0) + level*5(40) reaches the wounded ally";
    EXPECT_FLOAT_EQ(0.0f, cleric->stats()->magicpoints())
        << "the pool pays the slot price (Lua 0 + engine 2), no more";

    // shifter_down turns the same slot into MYSTIC MACE, which is busy-gated.
    cleric->stats()->set_magicpoints(1.0f);
    cleric->set_current_special(1);
    cleric->set_shifter_down(1);
    cleric->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(desc, cleric));
    EXPECT_FLOAT_EQ(1.0f, cleric->stats()->magicpoints())
        << "a busy-refused mace charges nothing either";
}

// TURN UNDEAD destroys a hostile undead inside 4 px per caster level and
// leaves the living alone. walker::turn_undead's roll is
// next(range*40) > next(target_level*10); the target's level is 0, so the
// right-hand draw returns 0 without advancing the LCG and a seeded
// left-hand draw of 1 makes the turn land.
TEST(FamilyCleric, r15_turn_undead_destroys_a_hostile_undead_in_range)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    ClericR15Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R15 Cleric";
    cleric->myguy->intelligence = 90;
    cleric->myguy->exp = 0;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    living* undead = add_living(fx, 1, FAMILY_SKELETON, 92, 80);
    living* breather = add_living(fx, 1, FAMILY_SOLDIER, 94, 80);
    ASSERT_TRUE(undead && breather);

    // The roll is next(range*40) > next(target_level*10), with
    // range = turn_undead_range_per_level(4) * caster level(10) = 40. A
    // level-0 target makes the right-hand draw next(0), which answers 0
    // without advancing the LCG, so the one seeded draw decides the turn.
    undead->stats()->set_level(0);
    fx.level.world().rng_ =
        og::sim::SimRandom(seed_whose_first_draw_is(40u * 40u, 1u));

    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "turn undead with a hostile undead in range must resolve";

    EXPECT_EQ(1, undead->dead()) << "the hostile skeleton is turned";
    EXPECT_EQ(0, breather->dead())
        << "turning is only ever fatal to the undead";
    EXPECT_EQ(3u, cleric->myguy->exp)
        << "turning pays 3 experience per undead destroyed";
}

// The three corpse specials, each with its own summon, its own consumed
// bloodstain and its own flat experience. The resurrect here takes the
// SUBTRACT arm of the exp penalty (the cleric can afford it), the mirror of
// the floor arm pinned in r12/r14.
TEST(FamilyCleric, r15_raise_and_resurrect_consume_blood_and_pay_their_own_exp)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    ClericR15Fixture fx;
    GameWorld& world = fx.level.world();

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    ASSERT_TRUE(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R15 Cleric";
    cleric->myguy->intelligence = 90;
    cleric->myguy->exp = 0;
    cleric->stats()->set_level(10);
    cleric->stats()->set_magicpoints(300.0f);

    // Raise skeleton from the nearest blood.
    walker* stain1 = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    ASSERT_TRUE(stain1 != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, stain1, 30));
    cleric->set_current_special(2);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "blood in reach must raise a skeleton";
    EXPECT_EQ(1, stain1->dead()) << "the blood is spent";
    walker* skeleton = find_live_family(world, Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, skeleton) << "a skeleton rises from the blood";
    EXPECT_EQ(cleric, skeleton->owner()) << "the skeleton serves its raiser";
    EXPECT_EQ(0, static_cast<int>(skeleton->team_num()))
        << "and fights on the cleric's team";
    EXPECT_EQ(45u, cleric->myguy->exp) << "raise_skeleton pays a flat 45";

    // Raise ghost from a second corpse.
    walker* stain2 = add_stain(fx, 86, 80, 1, FAMILY_ORC);
    ASSERT_TRUE(stain2 != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, stain2, 30));
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "a second corpse in reach must raise a ghost";
    EXPECT_EQ(1, stain2->dead()) << "the second blood is spent";
    walker* ghost = find_live_family(world, Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, ghost) << "a ghost rises from the second blood";
    EXPECT_EQ(cleric, ghost->owner()) << "the ghost serves its raiser";
    EXPECT_EQ(105u, cleric->myguy->exp)
        << "45 for the skeleton plus the flat 60 a raise_ghost pays";

    // Friendly resurrect: the penalty (level^2 * 100 = 100) is affordable
    // now, so it is SUBTRACTED rather than floored, then +90 is paid.
    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    ASSERT_TRUE(friendly_stain != nullptr);
    friendly_stain->stats()->set_level(1);
    friendly_stain->stats()->set_max_hitpoints(100.0f);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, friendly_stain, 30));
    cleric->set_current_special(4);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "friendly blood in reach must revive";
    EXPECT_EQ(1, friendly_stain->dead()) << "the blood is spent";
    walker* revived = find_live_family(world, Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, revived) << "the corpse's old family walks again";
    EXPECT_FLOAT_EQ(50.0f, revived->stats()->hitpoints())
        << "a resurrection returns at half of the transferred max HP";
    EXPECT_EQ(95u, cleric->myguy->exp)
        << "105 - 100 penalty + 90 resurrect: the affordable-penalty arm";

    // Hostile resurrect: a second ghost, no orc, another flat 90.
    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    ASSERT_TRUE(hostile_stain != nullptr);
    ASSERT_TRUE(place_corpse_in_reach(world, cleric, hostile_stain, 30));
    ASSERT_TRUE(og::test::do_special(desc, cleric))
        << "hostile blood in reach must raise a ghost";
    EXPECT_EQ(1, hostile_stain->dead()) << "the blood is spent";
    EXPECT_EQ(2u, count_live_family(world, Order::Living, FAMILY_GHOST))
        << "the hostile corpse adds a SECOND ghost";
    EXPECT_EQ(0u, count_live_family(world, Order::Living, FAMILY_ORC))
        << "hostile blood never revives the enemy it came from";
    EXPECT_EQ(185u, cleric->myguy->exp)
        << "the hostile resurrect pays the same flat 90 (no penalty arm)";
}
} // namespace detail_family_cleric_r15
