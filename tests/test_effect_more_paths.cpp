#include <openglad/platform/game_context.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { push_test_context(ctx); }
    ~GlobalContextGuard() { pop_test_context(); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};

static std::unique_ptr<walker> make_living(char family, unsigned char team, short level = 3)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
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

TEST(EffectMorePaths, effect_magic_shield_hits_weapon_and_enemy_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Deterministic RNG for weapon targeting/miss chances, if any.
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_CLERIC, 1);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner)
        return;

    walker* shield = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_TRUE(shield != nullptr) << "shield created";
    if (!shield)
        return;

    shield->set_owner(owner.get());
    shield->set_team_num(owner->team_num());
    shield->stats()->set_hitpoints(50);
    shield->set_lifetime(5);
    shield->setxy(100, 100);

    // Inject a weapon into oblist (effect::act queries level_data.oblist for weapons).
    auto weapon = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
    if (weapon) {
        weapon->set_team_num(2); // opposing team
        weapon->set_damage(7.0f);
        weapon->setxy(102, 100);
        og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(weapon));
    }

    // Also add a nearby foe living.
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (foe) {
        foe->set_team_num(2);
        foe->set_damage(5.0f);
        foe->setxy(104, 100);
    }

    (void)shield->act();

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_boomerang_hits_weapon_and_enemy_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    SeededRandom seeded(123u);
    GameContext c;
    c.rng = &seeded;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_SOLDIER, 1);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner)
        return;

    walker* fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_TRUE(fx != nullptr) << "boomerang created";
    if (!fx)
        return;

    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());
    fx->stats()->set_hitpoints(50);
    fx->set_lifetime(5);
    fx->set_drawcycle(12);
    fx->setxy(100, 100);

    auto weapon = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
    if (weapon) {
        weapon->set_team_num(2);
        weapon->set_damage(3.0f);
        weapon->setxy(102, 100);
        og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(weapon));
    }

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    if (foe) {
        foe->set_team_num(2);
        foe->set_damage(4.0f);
        foe->setxy(104, 100);
    }

    (void)fx->act();
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_cloud_hits_collision_branch_and_walk_command_path)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    // effect.cpp cloud path has a loop that requires xd/yd != 0; use a sequence
    // that produces (-1, +1) on the first draw.
    SequenceRandom seq_rng({0, 2, 0, 2});
    GameContext c;
    c.rng = &seq_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_DRUID, 1);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner)
        return;

    walker* cloud = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CLOUD);
    ASSERT_TRUE(cloud != nullptr) << "cloud created";
    if (!cloud)
        return;

    cloud->set_owner(owner.get());
    cloud->set_team_num(1);
    cloud->set_lifetime(15);
    cloud->setxy(100, 100);

    // First act() should enqueue a walk command, second should execute it.
    (void)cloud->act();
    (void)cloud->act();
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_chain_lightning_hits_leader_and_spawns_explosion)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto caster = make_living(FAMILY_MAGE, 1, 5);
    ASSERT_TRUE(caster != nullptr) << "caster created";
    if (!caster)
        return;

    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(leader != nullptr) << "leader created";
    if (!leader)
        return;

    leader->set_team_num(2);
    leader->setxy(100, 100);

    walker* chain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_TRUE(chain != nullptr) << "chain created";
    if (!chain)
        return;

    chain->set_owner(caster.get());
    chain->set_leader(leader);
    chain->set_team_num(caster->team_num());
    chain->set_damage(50.0f);
    chain->set_lineofsight(2);
    chain->setxy(100, 100); // overlap -> hit leader immediately

    (void)chain->act();
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_death_explosion_shoves_nearby_targets)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_THIEF, 1, 10);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner)
        return;

    walker* explosion = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(explosion != nullptr) << "explosion created";
    if (!explosion)
        return;

    explosion->set_owner(owner.get());
    explosion->set_skip_exit(0);
    explosion->setxy(100, 100);
    explosion->set_damage(40.0f);

    walker* target = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(target != nullptr) << "target created";
    if (target) {
        target->set_team_num(2);
        target->setxy(110, 100);
        target->stats()->clear_command();
        // effect::death(FAMILY_EXPLOSION) shoves via force_command(), but the
        // subsequent attack triggers statistics::hit_response(), which may
        // clear commands when the attacker is a "new" foe. Pre-seed the foe
        // relationship so the shove command remains queued deterministically.
        target->set_foe(owner.get());
    }

    explosion->set_dead(1);
    (void)explosion->death();

    if (target) {
        ASSERT_TRUE(target->stats()->has_commands()) << "explosion should shove targets via COMMAND_WALK";
    }
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_batch3_explosion_owner_fallback_and_empty_target_list)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* explosion = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(explosion != nullptr) << "explosion created";
    if (!explosion)
        return;

    explosion->set_owner(nullptr); // force owner=self path in explosion_on_death()
    explosion->set_skip_exit(1);   // range gets zeroed then clamped to 16
    explosion->set_dead(1);
    (void)explosion->death();   // no targets nearby: howmany<1 branch

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_batch3_bomb_owner_dead_fallback_path)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* dead_owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_THIEF);
    ASSERT_TRUE(dead_owner != nullptr) << "owner created";
    if (!dead_owner)
        return;
    dead_owner->set_dead(1);

    walker* bomb = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_BOMB);
    ASSERT_TRUE(bomb != nullptr) << "bomb created";
    if (!bomb)
        return;

    bomb->set_owner(dead_owner); // bomb_on_death should replace this with self
    bomb->set_damage(15.0f);
    bomb->set_dead(1);
    (void)bomb->death();

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_batch3_shield_and_boomerang_collision_loops)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_CLERIC, 1, 6);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner)
        return;

    // Magic shield: hit incoming weapon and nearby foe, then die from hp/lifetime check.
    walker* shield = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_TRUE(shield != nullptr) << "shield created";
    if (!shield)
        return;
    shield->set_owner(owner.get());
    shield->set_team_num(owner->team_num());
    shield->setxy(100, 100);
    shield->stats()->set_hitpoints(1.0f);
    shield->set_lifetime(0);

    auto incoming = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(incoming != nullptr) << "incoming weapon created";
    if (incoming) {
        incoming->set_team_num(2);
        incoming->set_damage(2.0f);
        incoming->setxy(100, 100);
        og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(incoming));
    }

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (foe) {
        foe->set_team_num(2);
        foe->set_damage(2.0f);
        foe->setxy(100, 100);
    }

    (void)shield->act();

    // Boomerang: foe loop path in boomerang_on_act().
    walker* boomerang = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_TRUE(boomerang != nullptr) << "boomerang created";
    if (boomerang) {
        boomerang->set_owner(owner.get());
        boomerang->set_team_num(owner->team_num());
        boomerang->setxy(100, 100);
        boomerang->set_drawcycle(10);
        boomerang->stats()->set_hitpoints(20.0f);
        boomerang->set_lifetime(5);
        (void)boomerang->act();
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_batch3_chain_snap_to_leader_and_effect_death_guard)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    auto owner = make_living(FAMILY_MAGE, 1, 5);
    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(owner != nullptr && leader != nullptr) << "owner+leader created";
    if (!(owner && leader))
        return;
    leader->set_team_num(2);

    // Place close enough that chain takes the center_on(leader) branch.
    walker* chain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_TRUE(chain != nullptr) << "chain created";
    if (!chain)
        return;
    chain->set_owner(owner.get());
    chain->set_leader(leader);
    chain->set_lineofsight(10);
    leader->setxy(120, 120);
    chain->setxy(121, 121);
    std::int32_t before_dist = chain->distance_to_ob_center(leader);
    (void)chain->act();
    std::int32_t after_dist = chain->distance_to_ob_center(leader);
    ASSERT_TRUE(after_dist <= before_dist) << "close chain path should not move chain farther from leader";

    // effect::death() second-call guard on a fresh effect object.
    walker* death_fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(death_fx != nullptr) << "death guard fx created";
    if (!death_fx)
        return;
    death_fx->set_dead(1);
    bool first = death_fx->death();
    bool second = death_fx->death();
    ASSERT_TRUE(first) << "first death call should succeed";
    ASSERT_TRUE(!second) << "second death call should be guarded";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_batch4_chain_guard_ownerless_and_non_myguy_foe_scan)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Early guard branch: missing owner must kill chain immediately.
    walker* orphan_chain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CHAIN);
    walker* any_leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(orphan_chain != nullptr && any_leader != nullptr) << "orphan chain/leader created";
    if (orphan_chain && any_leader)
    {
        orphan_chain->set_leader(any_leader);
        orphan_chain->set_lineofsight(4);
        orphan_chain->setxy(100, 100);
        any_leader->setxy(100, 100);
        (void)orphan_chain->act();
        ASSERT_TRUE(orphan_chain->dead() == 1) << "ownerless chain should die in guard path";
    }

    og::runtime::current_session->myscreen_->world().delete_objects();

    // Hit-leader path with owner->myguy == nullptr should take non-myguy foe scan branch.
    walker* owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_CLERIC);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* chain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_TRUE(owner != nullptr && leader != nullptr && foe != nullptr && chain != nullptr) << "owner/leader/foe/chain created";
    if (!(owner && leader && foe && chain))
        return;

    owner->set_team_num(1);
    owner->stats()->set_level(8);
    leader->set_team_num(1); // not a foe, prevents leader from consuming the first chain bounce
    foe->set_team_num(2);
    leader->setxy(120, 120);
    foe->setxy(124, 120);

    chain->set_owner(owner);
    chain->set_leader(leader);
    chain->set_team_num(owner->team_num());
    chain->set_damage(70.0f); // generic=35, so branch generic>20 can run
    chain->set_lineofsight(3);
    chain->setxy(120, 120); // overlap leader -> explosion/branch fanout path

    SequenceRandom seq_rng({0});
    (void)chain->act();

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_batch4_chain_movement_negative_delta_branch)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* chain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_TRUE(owner != nullptr && leader != nullptr && chain != nullptr) << "movement chain objects created";
    if (!(owner && leader && chain))
        return;

    owner->set_team_num(1);
    leader->set_team_num(2);
    leader->setxy(60, 60);
    chain->set_owner(owner);
    chain->set_leader(leader);
    chain->set_team_num(1);
    chain->set_lineofsight(8);
    chain->set_stepsize(4.0f);
    chain->setxy(140, 140); // ensures x and y deltas are negative

    const short before_x = chain->xpos();
    const short before_y = chain->ypos();
    (void)chain->act();
    ASSERT_TRUE(chain->xpos() <= before_x && chain->ypos() <= before_y) << "chain movement should step toward upper-left leader when deltas are negative";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(EffectMorePaths, effect_batch6_chain_small_delta_else_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* chain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_TRUE(owner != nullptr && leader != nullptr && chain != nullptr) << "owner/leader/chain created";
    if (!(owner && leader && chain))
        return;

    owner->set_team_num(1);
    leader->set_team_num(2);
    chain->set_owner(owner);
    chain->set_leader(leader);
    chain->set_team_num(1);
    chain->set_lineofsight(8);
    chain->set_stepsize(10.0f);
    chain->setxy(100, 100);

    // X delta within stepsize (else sub-branch), Y delta larger than stepsize
    // (main sub-branch), while distance stays > 2*stepsize so movement branch runs.
    leader->setxy(106, 150);
    const short before_x = chain->xpos();
    const short before_y = chain->ypos();
    (void)chain->act();
    ASSERT_TRUE(chain->xpos() > before_x) << "small positive x delta should move right";
    ASSERT_TRUE(chain->ypos() > before_y) << "large positive y delta should move down toward leader";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


void orbit_offset(int drawcycle, float &xd, float &yd);
short hits(short x, short y, short xsize, short ysize,
           short x2, short y2, short xsize2, short ysize2);

TEST(EffectMorePaths, effect_round8_orbit_offset_and_default_act_death_paths)
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    orbit_offset(0, x0, y0);
    ASSERT_EQ(0, (int)x0) << "orbit offset at cycle 0 should have zero x";
    ASSERT_EQ(-24, (int)y0) << "orbit offset at cycle 0 should have negative y arc";

    float x1 = 0.0f;
    float y1 = 0.0f;
    orbit_offset(17, x1, y1); // wraps to index 1
    ASSERT_EQ(-9, (int)x1) << "orbit offset should wrap every 16 cycles";
    ASSERT_EQ(-22, (int)y1) << "orbit offset wrap y should match lookup table";

    // Default effect::act path with ANI_WALK should force dead + death.
    auto eff = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::FX, FAMILY_FLASH);
    ASSERT_TRUE(eff != nullptr) << "effect walker created";
    if (!eff)
        return;

    eff->set_ani_type(ANI_WALK);
    eff->set_dead(0);
    const bool r = eff->act();
    ASSERT_TRUE(!r) << "default effect act path should return false after killing itself";
    ASSERT_TRUE(eff->dead() == 1) << "default effect act path should mark effect dead";
}


TEST(EffectMorePaths, effect_round9_death_called_guard_returns_false_on_second_call)
{
    auto eff = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::FX, FAMILY_FLASH);
    ASSERT_TRUE(eff != nullptr) << "effect created for death guard";
    if (!eff)
        return;

    eff->set_dead(1);
    ASSERT_TRUE(eff->death()) << "first death() call should succeed";
    ASSERT_TRUE(!eff->death()) << "second death() call should be guarded and return false";
}


TEST(EffectMorePaths, effect_round10_hits_overlap_and_axis_reject_paths)
{
    ASSERT_EQ(1, (int)hits(100, 100, 10, 10, 105, 105, 8, 8)) << "hits should report overlap for intersecting boxes";
    ASSERT_EQ(0, (int)hits(100, 100, 10, 10, 200, 100, 8, 8)) << "hits should reject separated x axis";
    ASSERT_EQ(0, (int)hits(100, 100, 10, 10, 100, 200, 8, 8)) << "hits should reject separated y axis";
}


std::int32_t compute_explosion_range(std::int32_t level, short skip_exit);

TEST(EffectMorePaths, effect_round11_compute_explosion_range_clamps_and_hits_edge_touches)
{
    ASSERT_EQ(16, (int)compute_explosion_range(1, 0)) << "explosion range should clamp to minimum 16";
    ASSERT_EQ(96, (int)compute_explosion_range(40, 0)) << "explosion range should clamp to maximum 96";
    ASSERT_EQ(16, (int)compute_explosion_range(40, 1)) << "skip_exit branch should zero before min clamp, resulting in 16";

    // Boundary-touching boxes are still collisions in hits().
    ASSERT_EQ(1, (int)hits(10, 10, 10, 10, 20, 10, 5, 5)) << "touching on x edge should count as hit";
    ASSERT_EQ(1, (int)hits(10, 10, 10, 10, 10, 20, 5, 5)) << "touching on y edge should count as hit";
}
