#include <openglad/runtime/game_context.h>
#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/guy.h>
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
    auto w = g.create_walker_owned(myscreen);
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
    auto weapon = myscreen->level_data.myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW, myscreen);
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

    auto weapon = myscreen->level_data.myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW, myscreen);
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
