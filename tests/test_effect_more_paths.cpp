#include <openglad/runtime/game_context.h>
#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <memory>
#include <vector>

extern screen* myscreen;

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { set_global_context(ctx); }
    ~GlobalContextGuard() { set_global_context(nullptr); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};

static std::unique_ptr<walker> make_living(char family, unsigned char team, short level = 3)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w)
        w->setxy(100, 100);
    return w;
}

class SequenceRandom : public IRandom {
public:
    explicit SequenceRandom(std::initializer_list<Uint32> vals) : vals_(vals), idx_(0) {}
    Uint32 next(Uint32 max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        Uint32 v = 0;
        if (!vals_.empty()) {
            if (idx_ < vals_.size())
                v = vals_[idx_++];
            else
                v = vals_.back();
        }
        return v % max_exclusive;
    }
private:
    std::vector<Uint32> vals_;
    size_t idx_;
};
} // namespace

void test_effect_magic_shield_hits_weapon_and_enemy_paths()
{
    myscreen->level_data.delete_objects();

    // Deterministic RNG for weapon targeting/miss chances, if any.
    FixedRandom fixed_rng(1);
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_CLERIC, 1);
    TEST_ASSERT(owner != nullptr, "owner created");
    if (!owner)
        return;

    walker* shield = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    TEST_ASSERT(shield != nullptr, "shield created");
    if (!shield)
        return;

    shield->owner = owner.get();
    shield->team_num = owner->team_num;
    shield->stats()->hitpoints = 50;
    shield->lifetime = 5;
    shield->setxy(100, 100);

    // Inject a weapon into oblist (effect::act queries level_data.oblist for weapons).
    auto weapon = myscreen->level_data.myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(weapon != nullptr, "weapon created");
    if (weapon) {
        weapon->myobmap = myscreen->level_data.myobmap.get();
        weapon->team_num = 2; // opposing team
        weapon->damage = 7.0f;
        weapon->setxy(102, 100);
        myscreen->level_data.oblist.push_back(std::move(weapon));
    }

    // Also add a nearby foe living.
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(foe != nullptr, "foe created");
    if (foe) {
        foe->team_num = 2;
        foe->damage = 5.0f;
        foe->setxy(104, 100);
    }

    (void)shield->act();

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_magic_shield_hits_weapon_and_enemy_paths);

void test_effect_boomerang_hits_weapon_and_enemy_paths()
{
    myscreen->level_data.delete_objects();

    SeededRandom seeded(123u);
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &seeded;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_SOLDIER, 1);
    TEST_ASSERT(owner != nullptr, "owner created");
    if (!owner)
        return;

    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    TEST_ASSERT(fx != nullptr, "boomerang created");
    if (!fx)
        return;

    fx->owner = owner.get();
    fx->team_num = owner->team_num;
    fx->stats()->hitpoints = 50;
    fx->lifetime = 5;
    fx->drawcycle = 12;
    fx->setxy(100, 100);

    auto weapon = myscreen->level_data.myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(weapon != nullptr, "weapon created");
    if (weapon) {
        weapon->myobmap = myscreen->level_data.myobmap.get();
        weapon->team_num = 2;
        weapon->damage = 3.0f;
        weapon->setxy(102, 100);
        myscreen->level_data.oblist.push_back(std::move(weapon));
    }

    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    if (foe) {
        foe->team_num = 2;
        foe->damage = 4.0f;
        foe->setxy(104, 100);
    }

    (void)fx->act();
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_boomerang_hits_weapon_and_enemy_paths);

void test_effect_cloud_hits_collision_branch_and_walk_command_path()
{
    myscreen->level_data.delete_objects();

    // effect.cpp cloud path has a loop that requires xd/yd != 0; use a sequence
    // that produces (-1, +1) on the first draw.
    SequenceRandom seq_rng({0, 2, 0, 2});
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &seq_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_DRUID, 1);
    TEST_ASSERT(owner != nullptr, "owner created");
    if (!owner)
        return;

    walker* cloud = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CLOUD);
    TEST_ASSERT(cloud != nullptr, "cloud created");
    if (!cloud)
        return;

    cloud->owner = owner.get();
    cloud->team_num = 1;
    cloud->lifetime = 15;
    cloud->setxy(100, 100);

    // First act() should enqueue a walk command, second should execute it.
    (void)cloud->act();
    (void)cloud->act();
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_cloud_hits_collision_branch_and_walk_command_path);

void test_effect_chain_lightning_hits_leader_and_spawns_explosion()
{
    myscreen->level_data.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto caster = make_living(FAMILY_MAGE, 1, 5);
    TEST_ASSERT(caster != nullptr, "caster created");
    if (!caster)
        return;

    walker* leader = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(leader != nullptr, "leader created");
    if (!leader)
        return;

    leader->team_num = 2;
    leader->setxy(100, 100);

    walker* chain = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(chain != nullptr, "chain created");
    if (!chain)
        return;

    chain->owner = caster.get();
    chain->leader = leader;
    chain->team_num = caster->team_num;
    chain->damage = 50.0f;
    chain->lineofsight = 2;
    chain->setxy(100, 100); // overlap -> hit leader immediately

    (void)chain->act();
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_chain_lightning_hits_leader_and_spawns_explosion);

void test_effect_death_explosion_shoves_nearby_targets()
{
    myscreen->level_data.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_THIEF, 1, 10);
    TEST_ASSERT(owner != nullptr, "owner created");
    if (!owner)
        return;

    walker* explosion = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    TEST_ASSERT(explosion != nullptr, "explosion created");
    if (!explosion)
        return;

    explosion->owner = owner.get();
    explosion->skip_exit = 0;
    explosion->setxy(100, 100);
    explosion->damage = 40.0f;

    walker* target = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(target != nullptr, "target created");
    if (target) {
        target->team_num = 2;
        target->setxy(110, 100);
        target->stats()->clear_command();
        // effect::death(FAMILY_EXPLOSION) shoves via force_command(), but the
        // subsequent attack triggers statistics::hit_response(), which may
        // clear commands when the attacker is a "new" foe. Pre-seed the foe
        // relationship so the shove command remains queued deterministically.
        target->foe = owner.get();
    }

    explosion->dead = 1;
    (void)explosion->death();

    if (target) {
        TEST_ASSERT(target->stats()->has_commands(), "explosion should shove targets via COMMAND_WALK");
    }
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_death_explosion_shoves_nearby_targets);

void test_effect_batch3_explosion_owner_fallback_and_empty_target_list()
{
    myscreen->level_data.delete_objects();

    walker* explosion = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    TEST_ASSERT(explosion != nullptr, "explosion created");
    if (!explosion)
        return;

    explosion->owner = nullptr; // force owner=self path in explosion_on_death()
    explosion->skip_exit = 1;   // range gets zeroed then clamped to 16
    explosion->dead = 1;
    (void)explosion->death();   // no targets nearby: howmany<1 branch

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_batch3_explosion_owner_fallback_and_empty_target_list);

void test_effect_batch3_bomb_owner_dead_fallback_path()
{
    myscreen->level_data.delete_objects();

    walker* dead_owner = myscreen->level_data.add_ob(Order::Living, FAMILY_THIEF);
    TEST_ASSERT(dead_owner != nullptr, "owner created");
    if (!dead_owner)
        return;
    dead_owner->dead = 1;

    walker* bomb = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_BOMB);
    TEST_ASSERT(bomb != nullptr, "bomb created");
    if (!bomb)
        return;

    bomb->owner = dead_owner; // bomb_on_death should replace this with self
    bomb->damage = 15.0f;
    bomb->dead = 1;
    (void)bomb->death();

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_batch3_bomb_owner_dead_fallback_path);

void test_effect_batch3_shield_and_boomerang_collision_loops()
{
    myscreen->level_data.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_CLERIC, 1, 6);
    TEST_ASSERT(owner != nullptr, "owner created");
    if (!owner)
        return;

    // Magic shield: hit incoming weapon and nearby foe, then die from hp/lifetime check.
    walker* shield = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    TEST_ASSERT(shield != nullptr, "shield created");
    if (!shield)
        return;
    shield->owner = owner.get();
    shield->team_num = owner->team_num;
    shield->setxy(100, 100);
    shield->stats()->hitpoints = 1.0f;
    shield->lifetime = 0;

    auto incoming = myscreen->level_data.myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(incoming != nullptr, "incoming weapon created");
    if (incoming) {
        incoming->myobmap = myscreen->level_data.myobmap.get();
        incoming->team_num = 2;
        incoming->damage = 2.0f;
        incoming->setxy(100, 100);
        myscreen->level_data.oblist.push_back(std::move(incoming));
    }

    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(foe != nullptr, "foe created");
    if (foe) {
        foe->team_num = 2;
        foe->damage = 2.0f;
        foe->setxy(100, 100);
    }

    (void)shield->act();

    // Boomerang: foe loop path in boomerang_on_act().
    walker* boomerang = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    TEST_ASSERT(boomerang != nullptr, "boomerang created");
    if (boomerang) {
        boomerang->owner = owner.get();
        boomerang->team_num = owner->team_num;
        boomerang->setxy(100, 100);
        boomerang->drawcycle = 10;
        boomerang->stats()->hitpoints = 20.0f;
        boomerang->lifetime = 5;
        (void)boomerang->act();
    }

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_batch3_shield_and_boomerang_collision_loops);

void test_effect_batch3_chain_snap_to_leader_and_effect_death_guard()
{
    myscreen->level_data.delete_objects();

    auto owner = make_living(FAMILY_MAGE, 1, 5);
    walker* leader = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(owner != nullptr && leader != nullptr, "owner+leader created");
    if (!(owner && leader))
        return;
    leader->team_num = 2;

    // Place close enough that chain takes the center_on(leader) branch.
    walker* chain = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(chain != nullptr, "chain created");
    if (!chain)
        return;
    chain->owner = owner.get();
    chain->leader = leader;
    chain->lineofsight = 10;
    leader->setxy(120, 120);
    chain->setxy(121, 121);
    std::int32_t before_dist = chain->distance_to_ob_center(leader);
    (void)chain->act();
    std::int32_t after_dist = chain->distance_to_ob_center(leader);
    TEST_ASSERT(after_dist <= before_dist, "close chain path should not move chain farther from leader");

    // effect::death() second-call guard on a fresh effect object.
    walker* death_fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    TEST_ASSERT(death_fx != nullptr, "death guard fx created");
    if (!death_fx)
        return;
    death_fx->dead = 1;
    bool first = death_fx->death();
    bool second = death_fx->death();
    TEST_ASSERT(first, "first death call should succeed");
    TEST_ASSERT(!second, "second death call should be guarded");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_batch3_chain_snap_to_leader_and_effect_death_guard);

void test_effect_batch4_chain_guard_ownerless_and_non_myguy_foe_scan()
{
    myscreen->level_data.delete_objects();

    // Early guard branch: missing owner must kill chain immediately.
    walker* orphan_chain = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CHAIN);
    walker* any_leader = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(orphan_chain != nullptr && any_leader != nullptr, "orphan chain/leader created");
    if (orphan_chain && any_leader)
    {
        orphan_chain->leader = any_leader;
        orphan_chain->lineofsight = 4;
        orphan_chain->setxy(100, 100);
        any_leader->setxy(100, 100);
        (void)orphan_chain->act();
        TEST_ASSERT(orphan_chain->dead == 1, "ownerless chain should die in guard path");
    }

    myscreen->level_data.delete_objects();

    // Hit-leader path with owner->myguy == nullptr should take non-myguy foe scan branch.
    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = myscreen->level_data.add_ob(Order::Living, FAMILY_CLERIC);
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* chain = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(owner != nullptr && leader != nullptr && foe != nullptr && chain != nullptr,
                "owner/leader/foe/chain created");
    if (!(owner && leader && foe && chain))
        return;

    owner->team_num = 1;
    owner->stats()->level = 8;
    leader->team_num = 1; // not a foe, prevents leader from consuming the first chain bounce
    foe->team_num = 2;
    leader->setxy(120, 120);
    foe->setxy(124, 120);

    chain->owner = owner;
    chain->leader = leader;
    chain->team_num = owner->team_num;
    chain->damage = 70.0f; // generic=35, so branch generic>20 can run
    chain->lineofsight = 3;
    chain->setxy(120, 120); // overlap leader -> explosion/branch fanout path

    SequenceRandom seq_rng({0});
    chain->sim_rng = &seq_rng;
    owner->sim_rng = &seq_rng;
    (void)chain->act();

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_batch4_chain_guard_ownerless_and_non_myguy_foe_scan);

void test_effect_batch4_chain_movement_negative_delta_branch()
{
    myscreen->level_data.delete_objects();

    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* chain = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(owner != nullptr && leader != nullptr && chain != nullptr, "movement chain objects created");
    if (!(owner && leader && chain))
        return;

    owner->team_num = 1;
    leader->team_num = 2;
    leader->setxy(60, 60);
    chain->owner = owner;
    chain->leader = leader;
    chain->team_num = 1;
    chain->lineofsight = 8;
    chain->stepsize = 4.0f;
    chain->setxy(140, 140); // ensures x and y deltas are negative

    const short before_x = chain->xpos;
    const short before_y = chain->ypos;
    (void)chain->act();
    TEST_ASSERT(chain->xpos <= before_x && chain->ypos <= before_y,
                "chain movement should step toward upper-left leader when deltas are negative");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_batch4_chain_movement_negative_delta_branch);

void test_effect_batch6_chain_small_delta_else_branches()
{
    myscreen->level_data.delete_objects();

    walker* owner = myscreen->level_data.add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    walker* chain = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(owner != nullptr && leader != nullptr && chain != nullptr, "owner/leader/chain created");
    if (!(owner && leader && chain))
        return;

    owner->team_num = 1;
    leader->team_num = 2;
    chain->owner = owner;
    chain->leader = leader;
    chain->team_num = 1;
    chain->lineofsight = 8;
    chain->stepsize = 10.0f;
    chain->setxy(100, 100);

    // X delta within stepsize (else sub-branch), Y delta larger than stepsize
    // (main sub-branch), while distance stays > 2*stepsize so movement branch runs.
    leader->setxy(106, 150);
    const short before_x = chain->xpos;
    const short before_y = chain->ypos;
    (void)chain->act();
    TEST_ASSERT(chain->xpos > before_x, "small positive x delta should move right");
    TEST_ASSERT(chain->ypos > before_y, "large positive y delta should move down toward leader");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_effect_batch6_chain_small_delta_else_branches);

void orbit_offset(int drawcycle, float &xd, float &yd);
short hits(short x, short y, short xsize, short ysize,
           short x2, short y2, short xsize2, short ysize2);

void test_effect_round8_orbit_offset_and_default_act_death_paths()
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    orbit_offset(0, x0, y0);
    TEST_ASSERT_EQ(0, (int)x0, "orbit offset at cycle 0 should have zero x");
    TEST_ASSERT_EQ(-24, (int)y0, "orbit offset at cycle 0 should have negative y arc");

    float x1 = 0.0f;
    float y1 = 0.0f;
    orbit_offset(17, x1, y1); // wraps to index 1
    TEST_ASSERT_EQ(-9, (int)x1, "orbit offset should wrap every 16 cycles");
    TEST_ASSERT_EQ(-22, (int)y1, "orbit offset wrap y should match lookup table");

    // Default effect::act path with ANI_WALK should force dead + death.
    auto eff = myscreen->level_data.myloader->create_walker_owned(Order::FX, FAMILY_FLASH);
    TEST_ASSERT(eff != nullptr, "effect walker created");
    if (!eff)
        return;

    eff->ani_type = ANI_WALK;
    eff->dead = 0;
    const bool r = eff->act();
    TEST_ASSERT(!r, "default effect act path should return false after killing itself");
    TEST_ASSERT(eff->dead == 1, "default effect act path should mark effect dead");
}
REGISTER_TEST(test_effect_round8_orbit_offset_and_default_act_death_paths);

void test_effect_round9_death_called_guard_returns_false_on_second_call()
{
    auto eff = myscreen->level_data.myloader->create_walker_owned(Order::FX, FAMILY_FLASH);
    TEST_ASSERT(eff != nullptr, "effect created for death guard");
    if (!eff)
        return;

    eff->dead = 1;
    TEST_ASSERT(eff->death(), "first death() call should succeed");
    TEST_ASSERT(!eff->death(), "second death() call should be guarded and return false");
}
REGISTER_TEST(test_effect_round9_death_called_guard_returns_false_on_second_call);

void test_effect_round10_hits_overlap_and_axis_reject_paths()
{
    TEST_ASSERT_EQ(1, (int)hits(100, 100, 10, 10, 105, 105, 8, 8),
                   "hits should report overlap for intersecting boxes");
    TEST_ASSERT_EQ(0, (int)hits(100, 100, 10, 10, 200, 100, 8, 8),
                   "hits should reject separated x axis");
    TEST_ASSERT_EQ(0, (int)hits(100, 100, 10, 10, 100, 200, 8, 8),
                   "hits should reject separated y axis");
}
REGISTER_TEST(test_effect_round10_hits_overlap_and_axis_reject_paths);
