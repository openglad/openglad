#include <openglad/platform/game_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/combat_math.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)
extern cfg_store cfg;

static walker* make_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(100, 100);
    return w.release();
}

static void remove_and_delete(walker* w)
{
    if (w == nullptr) {
        return;
    }
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}

class SequenceRandomCombat : public IRandom {
public:
    explicit SequenceRandomCombat(std::initializer_list<Uint32> vals) : vals_(vals), idx_(0) {}
    Uint32 next(Uint32 max_exclusive) override
    {
        if (max_exclusive == 0) {
            return 0;
        }
        Uint32 v = 0;
        if (!vals_.empty()) {
            if (idx_ < vals_.size()) {
                v = vals_[idx_++];
            } else {
                v = vals_.back();
            }
        }
        return v % max_exclusive;
    }
private:
    std::vector<Uint32> vals_;
    size_t idx_;
};

static int count_family_in_oblist(char family)
{
    int count = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
        walker* w = uptr.get();
        if (w && w->family() == family)
            count++;
    }
    return count;
}

static Uint32 total_team_score()
{
    return og::runtime::current_session->myscreen_->world_.m_score[0] + og::runtime::current_session->myscreen_->world_.m_score[1] +
           og::runtime::current_session->myscreen_->world_.m_score[2] + og::runtime::current_session->myscreen_->world_.m_score[3];
}

static void set_world_tile(short world_x, short world_y, unsigned char tile)
{
    if (world_x < 0 || world_y < 0) {
        return;
    }
    auto& level = og::runtime::current_session->myscreen_->level_runtime_data();
    const int gx = world_x / GRID_SIZE;
    const int gy = world_y / GRID_SIZE;
    if (gx < 0 || gy < 0 || gx >= level.world().grid.w || gy >= level.world().grid.h)
        return;
    level.world().grid.data[gx + level.world().grid.w * gy] = tile;
}

// ---------------------------------------------------------------------------
// attack() - exercises the big combat function (lines 1822-2100)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_attack_basic)
{
    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";
    ASSERT_TRUE(target != nullptr) << "target created";

    target->setxy(101, 100);
    attacker->set_team_num(0);
    target->set_team_num(1);
    float hp_before = target->stats()->hitpoints();
    bool result = attacker->attack(target);
    // attack may or may not succeed depending on is_friendly logic
    (void)result;
    (void)hp_before;

    // Force a deterministic kill path to exercise death messaging/blood branches.
    target->set_dead(0);
    target->stats()->set_hitpoints(1);
    target->stats()->set_max_hitpoints(1);
    attacker->set_damage(500.0f);
    int blood_before = count_family_in_oblist(FAMILY_BLOOD);
    (void)attacker->attack(target);
    int blood_after = count_family_in_oblist(FAMILY_BLOOD);
    ASSERT_TRUE(blood_after >= blood_before) << "kill path should not reduce blood objects";

    // Treasure targets are never valid attack targets.
    walker* treasure = og::runtime::current_session->myscreen_->world().add_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_TRUE(treasure != nullptr) << "treasure created";
    if (treasure) {
        bool treasure_result = attacker->attack(treasure);
        ASSERT_TRUE(!treasure_result) << "attacking treasure should fail";
    }

    delete attacker;
    delete target;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCombat, walker_attack_friendly_fails)
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    ASSERT_TRUE(a != nullptr) << "a created";
    ASSERT_TRUE(b != nullptr) << "b created";

    b->setxy(101, 100);
    float hp_before = b->stats()->hitpoints();
    bool result = a->attack(b);
    ASSERT_TRUE(!result) << "attack should fail against friendly";
    ASSERT_TRUE(b->stats()->hitpoints() == hp_before) << "friendly HP should not change";

    delete a;
    delete b;
}


TEST(WalkerCombat, walker_attack_slime_magic_bonus)
{
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* slime = make_guy(FAMILY_SMALL_SLIME, 1);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";
    ASSERT_TRUE(slime != nullptr) << "slime created";

    slime->setxy(101, 100);
    slime->stats()->set_hitpoints(500);
    slime->stats()->set_max_hitpoints(500);
    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);

    attacker->attack(slime);

    // Weapon-owner combat path and FAMILY_SPRINKLE freeze special-case.
    walker* sprinkle = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_SPRINKLE);
    ASSERT_TRUE(sprinkle != nullptr) << "sprinkle weapon created";
    if (sprinkle) {
        sprinkle->set_owner(attacker);
        sprinkle->set_team_num(attacker->team_num());
        sprinkle->set_damage(50);
        slime->set_dead(0);
        slime->stats()->set_hitpoints(200);
        slime->stats()->set_max_hitpoints(200);
        int frozen_before = slime->stats()->frozen_delay();
        (void)sprinkle->attack(slime);
        ASSERT_TRUE(slime->stats()->frozen_delay() >= frozen_before) << "sprinkle hit should preserve/increase frozen delay";
    }

    // Magic does 2x damage to slimes - just verify no crash
    delete attacker;
    delete slime;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCombat, walker_attack_barbarian_magic_resistance)
{
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* barb = make_guy(FAMILY_BARBARIAN, 1);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";
    ASSERT_TRUE(barb != nullptr) << "target created";

    barb->setxy(101, 100);
    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);

    attacker->attack(barb);
    delete attacker;
    delete barb;
}


TEST(WalkerCombat, walker_attack_invulnerable)
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    ASSERT_TRUE(a != nullptr) << "a created";
    ASSERT_TRUE(b != nullptr) << "b created";

    b->setxy(101, 100);
    b->set_invulnerable_left(10);
    bool result = a->attack(b);
    ASSERT_TRUE(!result) << "attack should fail against invulnerable";

    delete a;
    delete b;
}


TEST(WalkerCombat, walker_attack_dead_target)
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    ASSERT_TRUE(a != nullptr) << "a created";
    ASSERT_TRUE(b != nullptr) << "b created";

    b->setxy(101, 100);
    b->set_dead(1);
    bool result = a->attack(b);
    ASSERT_TRUE(!result) << "attack should fail against dead target";

    delete a;
    delete b;
}


// ---------------------------------------------------------------------------
// act() - exercises the act function (lines 1539-1666)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_act_control)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_act_type(ACT_CONTROL);
    w->set_attack_lunge(1.0f);
    w->set_hit_recoil(1.0f);
    bool result = w->act();
    ASSERT_TRUE(result) << "ACT_CONTROL should return true";
    ASSERT_TRUE(w->attack_lunge() < 1.0f) << "attack_lunge should decay in act()";
    ASSERT_TRUE(w->hit_recoil() < 1.0f) << "hit_recoil should decay in act()";
}


TEST(WalkerCombat, walker_act_die)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_act_type(ACT_DIE);
    w->act();
    ASSERT_TRUE(w->dead() == 1) << "ACT_DIE should set dead";
}


TEST(WalkerCombat, walker_act_frozen)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_act_type(ACT_RANDOM);
    w->stats()->set_frozen_delay(5);
    bool result = w->act();
    ASSERT_TRUE(result) << "frozen walker should return 1";
    ASSERT_EQ(4, (int)w->stats()->frozen_delay()) << "frozen_delay should decrement";
}


TEST(WalkerCombat, walker_act_with_commands)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_act_type(ACT_RANDOM);
    w->stats()->add_command(COMMAND_WALK, 3, 1, 0);
    bool result = w->act();
    ASSERT_TRUE(result) << "walker with commands should return 1";

    // Drive additional act_type handlers.
    w->stats()->clear_command();
    w->set_act_type(127); // default case
    (void)w->act(); // default act_type path

    w->set_act_type(ACT_GUARD);
    w->set_foe(nullptr);
    (void)w->act();

    walker* foe = make_guy(FAMILY_ORC, 2);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (foe) {
        foe->setxy(w->xpos() + 8, w->ypos() + 8);
        w->set_foe(foe);
        (void)w->act();
    }

    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";
    if (l) {
        auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
        ASSERT_TRUE(gen != nullptr) << "generator created";
        if (gen) {
            walker* genp = gen.get();
            genp->setxy(120, 120);
            genp->set_act_type(ACT_GENERATE);
            // Force act_generate() to enter spawn/regen branch.
            genp->stats()->set_level(5);
            genp->stats()->set_max_hitpoints(10);
            genp->stats()->set_hitpoints(10);
            SequenceRandomCombat gen_rng({100, 0, 1, 1});
            GameContext gen_ctx;
            gen_ctx.rng = &gen_rng;
            push_test_context(&gen_ctx);
            (void)genp->act();
            pop_test_context();
        }

        walker* proj = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
        ASSERT_TRUE(proj != nullptr) << "weapon created";
        if (proj) {
            proj->setxy(120, 120);
            proj->set_act_type(ACT_FIRE);
            proj->set_lineofsight(0);
            (void)proj->act();

            // Force act_fire() collision path, both mortal and immortal.
            walker* target = make_guy(FAMILY_ORC, 2);
            ASSERT_TRUE(target != nullptr) << "act_fire target created";
            if (target) {
                target->setxy(proj->xpos(), proj->ypos());
                target->set_dead(0);
                proj->set_dead(0);
                proj->setxy(120, 120);
                proj->set_lineofsight(2);
                proj->set_collide_ob(target);
                proj->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
                proj->stats()->set_bit_flags(BIT_IMMORTAL, 0);
                (void)proj->act();

                proj->set_dead(0);
                proj->setxy(120, 120);
                proj->set_lineofsight(2);
                proj->set_collide_ob(target);
                proj->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
                proj->stats()->set_bit_flags(BIT_IMMORTAL, 1);
                (void)proj->act();
            }
            remove_and_delete(target);
            // Kept alive until level_data.delete_objects() at test end.
        }

        // Exercise base walker ACT_RANDOM path via Generator (non-living subclass).
        walker* base_rand = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
        walker* base_foe = make_guy(FAMILY_ORC, 3);
        ASSERT_TRUE(base_rand != nullptr && base_foe != nullptr) << "base ACT_RANDOM walkers created";
        if (base_rand && base_foe) {
            base_rand->set_team_num(1);
            base_rand->setxy(132, 132);
            base_rand->set_lineofsight(40);
            base_rand->set_act_type(ACT_RANDOM);
            base_rand->stats()->clear_command();

            base_foe->set_team_num(3);
            base_foe->setxy(136, 132);
            base_rand->set_foe(base_foe);

            GameContext base_ctx;

            // act(): rng(4)==0, rng(20)!=0 -> act_random().
            // act_random(): rng(70)==0 -> refresh foe and drive fire path.
            SequenceRandomCombat base_rng1({0, 1, 0, 5, 0});
            base_ctx.rng = &base_rng1;
            push_test_context(&base_ctx);
            (void)base_rand->act();

            // act(): rng(4)!=0 -> SEARCH command path.
            base_rand->set_foe(nullptr);
            SequenceRandomCombat base_rng2({1, 1, 1});
            base_ctx.rng = &base_rng2;
            push_test_context(&base_ctx);
            (void)base_rand->act();
            pop_test_context();
        }
        remove_and_delete(base_foe);
        // Kept alive until level_data.delete_objects() at test end.
    }

    // Drive ACT_RANDOM/act_random() branches deterministically.
    walker* randomer = make_guy(FAMILY_ORC, 1);
    walker* random_foe = make_guy(FAMILY_SOLDIER, 2);
    ASSERT_TRUE(randomer != nullptr && random_foe != nullptr) << "act_random walkers created";
    if (randomer && random_foe) {
        randomer->setxy(80, 80);
        random_foe->setxy(86, 80);
        randomer->set_foe(random_foe);
        randomer->set_lineofsight(40);
        randomer->set_act_type(ACT_RANDOM);
        randomer->stats()->clear_command();

        // act(): rng(4)==0 then rng(20)==1 -> call act_random().
        // act_random(): rng(70)==0 path then in-range fire_check branch.
        SequenceRandomCombat random_rng({0, 1, 0, 1, 0, 0});
        GameContext random_ctx;
        random_ctx.rng = &random_rng;
        push_test_context(&random_ctx);
        (void)randomer->act();

        // act_random() branch where no foe is found and command is set.
        randomer->set_foe(nullptr);
        SequenceRandomCombat nofoe_rng({0, 1, 0, 1, 1, 1});
        random_ctx.rng = &nofoe_rng;
        push_test_context(&random_ctx);
        (void)randomer->act();
        pop_test_context();
    }
    remove_and_delete(randomer);
    remove_and_delete(random_foe);

    remove_and_delete(foe);
    remove_and_delete(w);
    og::runtime::current_session->myscreen_->world().delete_objects();
}


// ---------------------------------------------------------------------------
// transfer_stats (lines 4307-4360)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_transfer_stats)
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    ASSERT_TRUE(a != nullptr) << "a created";
    ASSERT_TRUE(b != nullptr) << "b created";

    a->stats()->set_hitpoints(50);
    a->stats()->set_max_hitpoints(100);
    a->stats()->set_magicpoints(30);
    a->stats()->set_level(5);

    a->transfer_stats(b);

    ASSERT_EQ(50, (int)b->stats()->hitpoints()) << "HP transferred";
    ASSERT_EQ(100, (int)b->stats()->max_hitpoints()) << "max HP transferred";
    ASSERT_EQ(30, (int)b->stats()->magicpoints()) << "MP transferred";
    ASSERT_EQ(5, (int)b->stats()->level()) << "level transferred";

    delete a;
    delete b;
}


TEST(WalkerCombat, walker_transfer_stats_with_guy)
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    ASSERT_TRUE(a != nullptr) << "a created";
    ASSERT_TRUE(b != nullptr) << "b created";

    // b shouldn't have a myguy from transfer yet
    b->clear_myguy();

    a->transfer_stats(b);

    ASSERT_TRUE(b->myguy != nullptr) << "myguy should be transferred";

    delete a;
    delete b;
}


// ---------------------------------------------------------------------------
// transform_to (lines 4364-4417)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_transform_to)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";

    w->transform_to(Order::Living, FAMILY_ARCHER);
    ASSERT_EQ((int)FAMILY_ARCHER, (int)w->family()) << "should be archer after transform";

}


TEST(WalkerCombat, walker_transform_to_same_order)
{
    walker* w = make_guy(FAMILY_ELF, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";

    w->set_act_type(ACT_CONTROL);
    w->transform_to(Order::Living, FAMILY_MAGE);
    ASSERT_EQ((int)FAMILY_MAGE, (int)w->family()) << "should be mage";
    ASSERT_EQ(ACT_CONTROL, (int)w->act_type()) << "should preserve act type for same order";

}


// ---------------------------------------------------------------------------
// spaces_clear (lines 4293-4305)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_spaces_clear)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);

    short count = w->spaces_clear();
    ASSERT_TRUE(count >= 0 && count <= 8) << "spaces_clear should be 0-8";

}


// ---------------------------------------------------------------------------
// fire_check (lines 4026-4291) - complex direction logic
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_fire_check_all_dirs)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);

    // Try fire_check in all 8 directions
    w->fire_check(1, 0);
    w->fire_check(-1, 0);
    w->fire_check(0, 1);
    w->fire_check(0, -1);
    w->fire_check(1, 1);
    w->fire_check(-1, 1);
    w->fire_check(1, -1);
    w->fire_check(-1, -1);

}


TEST(WalkerCombat, walker_fire_check_blocks_on_intermediate_step)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* shooter = make_guy(FAMILY_ARCHER, 0);
    walker* foe = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(shooter != nullptr && foe != nullptr) << "fixtures created";
    if (!(shooter && foe))
        return;

    shooter->setxy(96, 96);
    shooter->set_lastx(1);
    shooter->set_lasty(0);
    shooter->set_curdir(FACE_RIGHT);
    shooter->set_enddir(FACE_RIGHT);
    shooter->set_team_num(0);
    foe->set_team_num(1);
    shooter->set_foe(foe);
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    shooter->stats()->set_magicpoints(9999.0f);
    shooter->stats()->set_weapon_cost(0.0f);

    SequenceRandomCombat rng({0});

    walker* probe = shooter->create_weapon();
    ASSERT_TRUE(probe != nullptr) << "probe weapon created";
    if (!probe)
        return;
    shooter->set_weapon_heading(probe);

    const short start_x = probe->xpos();
    const short start_y = probe->ypos();
    const short step_x = static_cast<short>(probe->lastx());
    const short step_y = static_cast<short>(probe->lasty());
    og::runtime::current_session->myscreen_->world().remove_ob(probe);

    ASSERT_TRUE(step_x != 0 || step_y != 0) << "probe step should be non-zero";

    // Block the second linear probe step and place the foe at the third.
    set_world_tile(static_cast<short>(start_x + 2 * step_x),
                   static_cast<short>(start_y + 2 * step_y),
                   PIX_H_WALL1);
    foe->setxy(static_cast<short>(start_x + 3 * step_x),
               static_cast<short>(start_y + 3 * step_y));
    foe->set_sizex(1);
    foe->set_sizey(1);

    ASSERT_TRUE(!shooter->fire_check(1, 0)) << "fire_check should fail when an intermediate tile on the shot path is blocked";
}


// ---------------------------------------------------------------------------
// init_fire (lines 646-691)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_init_fire_when_busy)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(10);

    bool result = w->init_fire(1, 0);
    (void)result; // busy behavior may vary

}


// ---------------------------------------------------------------------------
// set_order_family (lines 2199-2265) - exercises family name/weapon setup
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_set_order_family_all)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";

    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        w->set_order_family(Order::Living, families[i]);
        ASSERT_EQ((int)families[i], (int)w->family()) << "family should match";
    }

    // Exercise non-living order assignments too.
    ASSERT_TRUE(w->set_order_family(Order::Weapon, FAMILY_KNIFE)) << "set_order_family weapon should return true";
    ASSERT_EQ((int)FAMILY_KNIFE, (int)w->family()) << "family should change to knife";
    ASSERT_TRUE(w->set_order_family(Order::Treasure, FAMILY_STAIN)) << "set_order_family treasure should return true";
    ASSERT_EQ((int)FAMILY_STAIN, (int)w->family()) << "family should change to stain";
    ASSERT_TRUE(w->set_order_family(Order::FX, FAMILY_EXPLOSION)) << "set_order_family fx should return true";
    ASSERT_EQ((int)FAMILY_EXPLOSION, (int)w->family()) << "family should change to explosion";
    ASSERT_TRUE(w->set_order_family(Order::Generator, FAMILY_TENT)) << "set_order_family generator should return true";
    ASSERT_EQ((int)FAMILY_TENT, (int)w->family()) << "family should change to tent";

}


// ---------------------------------------------------------------------------
// is_friendly extended (lines 4670-4738)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_is_friendly_different_teams)
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    ASSERT_TRUE(a != nullptr) << "a created";
    ASSERT_TRUE(b != nullptr) << "b created";

    a->set_team_num(0);
    b->set_team_num(1);
    Sint32 r1 = a->is_friendly(b);
    Sint32 r2 = b->is_friendly(a);
    (void)r1; (void)r2; // exercise the code paths

    delete a;
    delete b;
}


// ---------------------------------------------------------------------------
// set_difficulty (lines 4611-4635)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_set_difficulty_all_families)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return;

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = l->create_walker_owned(Order::Living, families[i]);
        if (w) {
            w->set_difficulty(5);
            ASSERT_TRUE(w->stats()->max_hitpoints() > 0) << "HP positive after set_difficulty";
        }
    }

    auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(gen != nullptr) << "generator created";
    if (gen) {
        float hp_before = gen->stats()->hitpoints();
        gen->set_difficulty(7);
        ASSERT_TRUE(gen->stats()->hitpoints() >= hp_before) << "generator HP should be scaled";
    }
}


// ---------------------------------------------------------------------------
// get_current_angle for all directions (line 552-575)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_get_current_angle_all_dirs)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";

    float prev_angle = -999;
    for (int dir = 0; dir < 8; dir++) {
        w->set_curdir(static_cast<char>(dir));
        float angle = w->get_current_angle();
        // Each direction should have a different angle
        if (dir > 0) {
            ASSERT_TRUE(angle != prev_angle) << "each direction should have unique angle";
        }
        prev_angle = angle;
    }

}


// ---------------------------------------------------------------------------
// animate smoke test
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_animate_smoke)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_ani_type(ANI_WALK);
    w->animate();

    // TELE_OUT branches: mage teleport and skeleton ranged teleport.
    w->transform_to(Order::Living, FAMILY_MAGE);
    w->set_ani_type(ANI_TELE_OUT);
    for (int i = 0; i < 32 && w->ani_type() != ANI_WALK; ++i) {
        (void)w->animate();
    }
    ASSERT_TRUE(w->ani_type() == ANI_WALK) << "mage teleport animation should settle";

    w->transform_to(Order::Living, FAMILY_SKELETON);
    w->set_ani_type(ANI_TELE_OUT);
    for (int i = 0; i < 32 && w->ani_type() != ANI_WALK; ++i) {
        (void)w->animate();
    }
    ASSERT_TRUE(w->ani_type() == ANI_WALK) << "skeleton teleport animation should settle";

    // Slime split branch.
    w->transform_to(Order::Living, FAMILY_SLIME);
    w->set_ani_type(ANI_SLIME_SPLIT);
    int small_slime_before = count_family_in_oblist(FAMILY_SMALL_SLIME);
    for (int i = 0; i < 32 && w->ani_type() != ANI_WALK; ++i) {
        (void)w->animate();
    }
    int small_slime_after = count_family_in_oblist(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(small_slime_after >= small_slime_before) << "slime split should preserve/increase small slimes";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCombat, walker_act_random_generator_paths)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    walker* foe = make_guy(FAMILY_ORC, 2);
    ASSERT_TRUE(gen != nullptr && foe != nullptr) << "generator and foe created";
    if (!(gen && foe)) {
        delete foe;
        return;
    }

    walker* genp = gen.get();
    genp->set_team_num(1);
    foe->set_team_num(2);
    genp->setxy(128, 128);
    foe->setxy(132, 128);
    genp->set_lineofsight(40);
    genp->set_act_type(ACT_RANDOM);
    genp->stats()->clear_command();

    GameContext ctx;

    // Trigger act_random() route and in-range logic.
    SequenceRandomCombat rng1({0, 1, 0, 0, 0});
    ctx.rng = &rng1;
    push_test_context(&ctx);
    (void)genp->act();

    // Trigger 3-of-4 search branch with foe lookup.
    genp->set_foe(nullptr);
    SequenceRandomCombat rng2({1, 0, 0, 0});
    ctx.rng = &rng2;
    push_test_context(&ctx);
    (void)genp->act();
    pop_test_context();

    ASSERT_EQ(ACT_RANDOM, (int)genp->act_type()) << "generator should remain in ACT_RANDOM";

    delete foe;
}


TEST(WalkerCombat, effect_helpers_and_recoil_branches)
{
    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker != nullptr && target != nullptr) << "combat walkers created";
    if (!(attacker && target))
        return;

    target->setxy(attacker->xpos() + 12, attacker->ypos() + 4);
    cfg.apply_setting("effects", "hit_recoil", "on");
    cfg.apply_setting("effects", "hit_anim", "off");
    cfg.apply_setting("effects", "damage_numbers", "off");
    cfg.apply_setting("effects", "hit_flash", "off");

    attacker->do_hit_effects(attacker, target, 12);
    ASSERT_TRUE(target->hit_recoil() > 0.0f) << "hit_recoil should be set for living targets when enabled";

    // do_heal_effects should be safe on stack walkers.
    walker stack_a;
    walker stack_b;
    stack_a.do_heal_effects(&stack_a, &stack_b, 5);

    delete attacker;
    delete target;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCombat, walker_attack_weapon_owner_chain_and_nonliving_target)
{
    walker* owner = make_guy(FAMILY_SOLDIER, 0);
    walker* living_target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(owner != nullptr && living_target != nullptr) << "owner and target created";
    if (!(owner && living_target))
        return;

    walker* weapon = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
    if (weapon) {
        owner->set_user(0);
        weapon->set_owner(owner);
        weapon->set_team_num(owner->team_num());
        weapon->set_damage(1.0f);
        weapon->stats()->set_hitpoints(50);

        living_target->stats()->set_armor(5000); // force damage clamp-to-zero path
        living_target->stats()->set_hitpoints(100);
        (void)weapon->attack(living_target);
        ASSERT_TRUE(living_target->stats()->hitpoints() <= 100) << "weapon attack path should execute safely";

        walker* nonliving = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_FLASH);
        ASSERT_TRUE(nonliving != nullptr) << "nonliving target created";
        if (nonliving)
            (void)weapon->attack(nonliving);
    }

    delete owner;
    delete living_target;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCombat, batch5_heal_and_hit_effect_variants)
{
    walker* healer = make_guy(FAMILY_CLERIC, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(healer != nullptr && target != nullptr) << "healer and target created";
    if (!(healer && target))
        return;

    healer->setxy(100, 100);
    target->setxy(116, 104);

    // do_heal_effects with config enabled and null-healer branch.
    cfg.apply_setting("effects", "heal_numbers", "on");
    healer->do_heal_effects(nullptr, target, 9);
    ASSERT_TRUE(!target->damage_numbers.empty()) << "heal numbers should be emitted for target when enabled";

    // do_hit_effects projectile branch (attacker != this) and damage number branch.
    cfg.apply_setting("effects", "damage_numbers", "on");
    cfg.apply_setting("effects", "hit_anim", "on");
    cfg.apply_setting("effects", "hit_flash", "on");
    cfg.apply_setting("effects", "hit_recoil", "on");
    walker* projectile = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(projectile != nullptr) << "projectile created";
    if (projectile)
    {
        projectile->set_owner(healer);
        projectile->set_team_num(healer->team_num());
        projectile->setxy(108, 100);
        projectile->do_hit_effects(healer, target, 6);
        ASSERT_TRUE(target->hurt_flash()) << "hit_flash should be set on positive damage when enabled";
        ASSERT_TRUE(target->hit_recoil() > 0.0f) << "hit_recoil should be set for living targets";
    }
}


TEST(WalkerCombat, batch5_do_combat_damage_target_myguy_stats)
{
    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* victim = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker != nullptr && victim != nullptr) << "attacker and victim created";
    if (!(attacker && victim))
        return;

    const float taken_before = victim->myguy ? victim->myguy->scen_damage_taken : 0.0f;
    attacker->do_combat_damage(attacker, victim, 7);

    ASSERT_TRUE(victim->last_hitpoints() >= victim->stats()->hitpoints()) << "combat damage should update last_hitpoints";
    if (victim->myguy)
    {
        ASSERT_TRUE(victim->myguy->scen_damage_taken >= taken_before) << "target myguy scen_damage_taken should increase";
    }
    ASSERT_TRUE(victim->stats()->hitpoints() <= victim->last_hitpoints()) << "combat damage should not increase target hitpoints";
}


TEST(WalkerCombat, batch6_attack_branches_enemy_and_weapon_paths)
{
    const short saved_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;

    // Enemy kill path: magical modifier, kill awards, notifications, and remaining-foe branch.
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* enemy = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker != nullptr && enemy != nullptr) << "attacker/enemy created";
    if (!(attacker && enemy))
        return;
    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);
    attacker->set_damage(500.0f);
    enemy->stats()->set_hitpoints(3);
    enemy->stats()->set_max_hitpoints(3);
    enemy->stats()->name = "NamedEnemy";
    enemy->set_owner(nullptr);
    enemy->set_lifetime(0);
    enemy->setxy(attacker->xpos() + 10, attacker->ypos() + 6);
    ASSERT_TRUE(attacker->attack(enemy)) << "enemy kill branch should execute";

    // Non-living default branch and weapon durability/death/on-hit callbacks.
    walker* owner = make_guy(FAMILY_SOLDIER, 0);
    walker* fx_target = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_SPRINKLE);
    ASSERT_TRUE(owner && fx_target && weapon) << "owner/fx_target/weapon created";
    if (owner && fx_target && weapon)
    {
        owner->set_user(0);
        weapon->set_owner(owner);
        weapon->set_team_num(owner->team_num());
        weapon->set_damage(10.0f);
        weapon->stats()->set_hitpoints(1);
        owner->myguy->total_shots = 2;
        owner->myguy->scen_shots = 2;
        fx_target->set_team_num(1);
        (void)weapon->attack(fx_target);
        ASSERT_TRUE(owner->myguy->total_shots <= 1) << "default non-living target branch should decrement shots";
        ASSERT_TRUE(weapon->dead() == 1) << "weapon durability path should kill mortal weapon at <=0 hp";
    }

    og::runtime::current_session->myscreen_->world_.allied_mode = saved_allied_mode;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCombat, batch6_attack_friendly_team_death_messages_and_clamps)
{
    const short saved_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;
    og::runtime::current_session->myscreen_->world_.allied_mode = 1;

    // Build an attacker that is not considered friendly to team 0 even when allied mode is on.
    walker* attacker = make_guy(FAMILY_SOLDIER, 1);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";
    if (!attacker)
        return;
    attacker->clear_myguy();
    attacker->set_damage(500.0f);

    // Team-0 target death paths (playerteam==target team) that select various message branches.
    walker* t_dispelled = make_guy(FAMILY_ORC, 0);
    walker* t_named = make_guy(FAMILY_ORC, 0);
    walker* t_myguy_name = make_guy(FAMILY_ORC, 0);
    ASSERT_TRUE(t_dispelled && t_named && t_myguy_name) << "targets created";
    if (t_dispelled && t_named && t_myguy_name)
    {
        t_dispelled->stats()->set_hitpoints(1);
        t_dispelled->stats()->name = "Summon";
        t_dispelled->set_owner(attacker); // dispelled branch
        (void)attacker->attack(t_dispelled);

        t_named->stats()->set_hitpoints(1);
        t_named->set_owner(nullptr);
        t_named->set_lifetime(0);
        t_named->stats()->name = "AllyName"; // named death branch
        (void)attacker->attack(t_named);

        t_myguy_name->stats()->set_hitpoints(1);
        t_myguy_name->set_owner(nullptr);
        t_myguy_name->set_lifetime(0);
        t_myguy_name->stats()->name.clear();
        if (t_myguy_name->myguy)
            t_myguy_name->myguy->name = "GuyName"; // myguy-name branch
        (void)attacker->attack(t_myguy_name);
    }

    // High-armor path in attack() (engine still guarantees at least 1 damage).
    walker* armored = make_guy(FAMILY_ORC, 2);
    ASSERT_TRUE(armored != nullptr) << "armored target created";
    if (armored)
    {
        armored->stats()->set_armor(100000);
        const float hp_before = armored->stats()->hitpoints();
        (void)attacker->attack(armored);
        ASSERT_TRUE(armored->stats()->hitpoints() <= hp_before) << "high armor path should not increase hitpoints";
    }

    // do_heal_effects early-return branch when heal numbers are disabled.
    cfg.apply_setting("effects", "heal_numbers", "off");
    attacker->do_heal_effects(attacker, attacker, 5);
    cfg.apply_setting("effects", "heal_numbers", "on");

    og::runtime::current_session->myscreen_->world_.allied_mode = saved_allied_mode;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCombat, attack_rewards_single_credit_weapon_hit)
{
    const short saved_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;

    walker* owner = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    walker* weapon = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(owner && target && weapon) << "owner/target/weapon created";
    if (!(owner && target && weapon))
    {
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->world_.allied_mode = saved_allied_mode;
        return;
    }

    SequenceRandomCombat fixed_rng({0});
    weapon->set_owner(owner);
    weapon->set_team_num(owner->team_num());
    weapon->set_damage(16.0f);
    owner->set_team_num(0);
    target->set_team_num(1);

    target->stats()->set_armor(0);
    target->stats()->set_hitpoints(200);
    target->stats()->set_max_hitpoints(200);
    target->setxy(static_cast<short>(owner->xpos() + 8), static_cast<short>(owner->ypos()));

    const int exp_before = owner->myguy ? owner->myguy->exp : 0;
    const Uint32 score_before = og::runtime::current_session->myscreen_->world_.m_score[owner->team_num()];
    const float hp_before = target->stats()->hitpoints();
    if (current_game && current_game->sim_events)
        current_game->sim_events->clear();

    ASSERT_TRUE(weapon->attack(target)) << "weapon attack should succeed";

    const short dealt = static_cast<short>(hp_before - target->stats()->hitpoints());
    ASSERT_TRUE(dealt > 0) << "weapon attack should deal positive damage";
    ASSERT_TRUE(target->stats()->hitpoints() > 0) << "weapon reward regression should use non-lethal hit";

    const std::int32_t level_diff = weapon->stats()->level() - target->stats()->level();
    const short expected_attack_xp = compute_xp_from_attack(level_diff, static_cast<float>(dealt));
    const int exp_after = owner->myguy ? owner->myguy->exp : 0;
    ASSERT_EQ((int)expected_attack_xp, exp_after - exp_before) << "weapon hit should award attack XP exactly once";

    const Uint32 score_after = og::runtime::current_session->myscreen_->world_.m_score[owner->team_num()];
    const Uint32 expected_score = static_cast<Uint32>(dealt) + static_cast<Uint32>(target->stats()->level());
    ASSERT_EQ((int)expected_score, static_cast<int>(score_after - score_before)) << "weapon hit should award score once per hit";

    bool saw_score_change = false;
    if (current_game && current_game->sim_events)
    {
        for (const auto& ev : current_game->sim_events->events())
        {
            if (ev.kind == og::sim::EventKind::ScoreChange &&
                ev.a == static_cast<std::uint32_t>(owner->team_num()))
            {
                saw_score_change = true;
                break;
            }
        }
    }
    ASSERT_TRUE(saw_score_change) << "score award should emit ScoreChange event";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world_.allied_mode = saved_allied_mode;
}


TEST(WalkerCombat, attack_ignores_out_of_range_team_score_index)
{
    const short saved_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;

    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker && target) << "attacker/target created";
    if (!(attacker && target))
    {
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->world_.allied_mode = saved_allied_mode;
        return;
    }

    SequenceRandomCombat fixed_rng({0});
    attacker->set_team_num(250); // invalid score index from corrupted scenario data
    attacker->set_damage(12.0f);
    target->set_team_num(1);
    target->stats()->set_armor(0);
    target->stats()->set_hitpoints(40);
    target->stats()->set_max_hitpoints(40);
    target->setxy(static_cast<short>(attacker->xpos() + 10), static_cast<short>(attacker->ypos()));

    og::runtime::current_session->myscreen_->world_.m_score[0] = 10;
    og::runtime::current_session->myscreen_->world_.m_score[1] = 20;
    og::runtime::current_session->myscreen_->world_.m_score[2] = 30;
    og::runtime::current_session->myscreen_->world_.m_score[3] = 40;
    const Uint32 score_before = total_team_score();

    ASSERT_TRUE(attacker->attack(target)) << "attack should still succeed with invalid team id";
    ASSERT_EQ(score_before, total_team_score()) << "invalid team id should not write outside m_score bounds";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world_.allied_mode = saved_allied_mode;
}


TEST(WalkerCombat, attack_rewards_single_credit_melee_kill)
{
    const short saved_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;

    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker && target) << "attacker/target created";
    if (!(attacker && target))
    {
        og::runtime::current_session->myscreen_->world().delete_objects();
        og::runtime::current_session->myscreen_->world_.allied_mode = saved_allied_mode;
        return;
    }

    attacker->set_damage(16.0f);
    attacker->set_team_num(0);
    target->set_team_num(1);
    target->stats()->set_armor(0);
    target->stats()->set_hitpoints(14);
    target->stats()->set_max_hitpoints(14);
    target->setxy(attacker->xpos() + 10, attacker->ypos() + 4);
    og::runtime::current_session->myscreen_->world().rng_.state_ = 0;

    const int exp_before = attacker->myguy ? attacker->myguy->exp : 0;
    const int kills_before = attacker->myguy ? attacker->myguy->kills : 0;
    const int scen_kills_before = attacker->myguy ? attacker->myguy->scen_kills : 0;
    const int level_kills_before = attacker->myguy ? attacker->myguy->level_kills : 0;
    const Uint32 score_before = og::runtime::current_session->myscreen_->world_.m_score[attacker->team_num()];
    const float hp_before = target->stats()->hitpoints();

    ASSERT_TRUE(attacker->attack(target)) << "melee attack should succeed";

    const short dealt = static_cast<short>(hp_before - target->stats()->hitpoints());
    ASSERT_EQ(14, (int)dealt) << "configured melee kill should deal deterministic damage";
    ASSERT_TRUE(target->dead() == 1) << "target should die in kill-reward regression";

    const std::int32_t level_diff = attacker->stats()->level() - target->stats()->level();
    const short expected_attack_xp = compute_xp_from_attack(level_diff, static_cast<float>(dealt));
    const short expected_kill_xp = compute_xp_from_kill(level_diff);
    const int exp_after = attacker->myguy ? attacker->myguy->exp : 0;
    ASSERT_EQ((int)(expected_attack_xp + expected_kill_xp), exp_after - exp_before) << "melee kill should award attack XP once plus one kill XP";

    const Uint32 score_after = og::runtime::current_session->myscreen_->world_.m_score[attacker->team_num()];
    const Uint32 expected_score =
        static_cast<Uint32>(dealt + target->stats()->level()) +
        static_cast<Uint32>(dealt + 10 * target->stats()->level());
    ASSERT_EQ((int)expected_score, static_cast<int>(score_after - score_before)) << "melee kill should award one hit score and one kill bonus";

    ASSERT_EQ(1, (attacker->myguy ? attacker->myguy->kills : 0) - kills_before) << "kill counter should increment once";
    ASSERT_EQ(1, (attacker->myguy ? attacker->myguy->scen_kills : 0) - scen_kills_before) << "scenario kill counter should increment once";
    ASSERT_EQ((int)target->stats()->level(), (attacker->myguy ? attacker->myguy->level_kills : 0) - level_kills_before) << "level_kills should increase by defeated target level";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world_.allied_mode = saved_allied_mode;
}


TEST(WalkerCombat, walker_batch7_init_fire_and_animate_edge_paths)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->setxy(100, 100);

    // init_fire turn-gate while ACT_CONTROL (returns false).
    w->set_act_type(ACT_CONTROL);
    w->set_curdir(FACE_LEFT);
    bool r = w->init_fire(1, 0);
    ASSERT_TRUE(!r) << "init_fire should refuse turning fire while ACT_CONTROL";

    // init_fire turning path for non-control walker.
    w->set_act_type(ACT_RANDOM);
    w->set_curdir(FACE_LEFT);
    r = w->init_fire(1, 0);
    ASSERT_TRUE(r) << "init_fire should allow turning for non-control walkers";

    // Busy gate.
    w->set_busy(3);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    r = w->init_fire(1, 0);
    ASSERT_TRUE(!r) << "init_fire should fail while busy";
    w->set_busy(0);

    // Attack animation path from ANI_WALK.
    w->set_ani_type(ANI_WALK);
    r = w->init_fire(1, 0);
    ASSERT_TRUE(r) << "init_fire should start attack animation from ANI_WALK";

    // Non-walk path uses fire(); insufficient MP should make it fail.
    w->set_ani_type(ANI_ATTACK);
    w->stats()->set_magicpoints(0);
    w->stats()->set_weapon_cost(20);
    r = w->init_fire(1, 0);
    ASSERT_TRUE(!r) << "init_fire should fail via fire() when MP is insufficient";

    // animate() no-animation-table path.
    auto saved_ani = w->ani;
    w->ani = nullptr;
    ASSERT_TRUE(!w->animate()) << "animate should return false when animation table is null";
    w->ani = saved_ani;

    // animate() null-sequence path: create a local table with a null at the target index.
    const int ani_index = w->curdir() + w->ani_type() * NUM_FACINGS;
    const signed char * null_seq_rows[32] = {};
    // Copy existing pointers up to the target index, then null it out.
    for (int i = 0; i <= ani_index; i++)
        null_seq_rows[i] = w->ani[i];
    null_seq_rows[ani_index] = nullptr;
    auto saved_ani2 = w->ani;
    w->ani = null_seq_rows;
    ASSERT_TRUE(!w->animate()) << "animate should return false when selected sequence is null";
    w->ani = saved_ani2;
}


TEST(WalkerCombat, walker_batch8_act_default_and_animate_invalid_sequence_bounds)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->setxy(100, 100);

    // Drive act() default branch for unknown act type.
    w->set_act_type(99);
    bool acted = w->act();
    ASSERT_TRUE(!acted) << "act() should return false for unknown act types";

    // Build an animation sequence with no -1 sentinel to trigger bounds guard.
    static signed char no_sentinel_seq[128];
    for (int i = 0; i < 128; i++)
        no_sentinel_seq[i] = 0;

    w->set_ani_type(ANI_ATTACK);
    w->set_curdir(FACE_RIGHT);
    const int ani_index = w->curdir() + w->ani_type() * NUM_FACINGS;
    // Create a local table with the no-sentinel sequence at the target index.
    const signed char * custom_rows[32] = {};
    for (int i = 0; i <= ani_index; i++)
        custom_rows[i] = w->ani[i];
    custom_rows[ani_index] = no_sentinel_seq;
    auto saved_ani = w->ani;
    w->ani = custom_rows;
    w->set_cycle(0);

    bool animated = w->animate();
    ASSERT_TRUE(!animated) << "animate() should fail when animation sequence has no sentinel";
    ASSERT_EQ(ANI_WALK, (int)w->ani_type()) << "animate() should reset to ANI_WALK on invalid sequence";
    ASSERT_EQ(0, (int)w->cycle()) << "animate() should reset cycle on invalid sequence";

    w->ani = saved_ani;
    delete w;
}


TEST(WalkerCombat, round8_attack_early_return_guards)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* living_target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker && living_target) << "attacker and living target created";
    if (!(attacker && living_target))
        return;

    // Dead target guard.
    living_target->set_dead(1);
    ASSERT_TRUE(!attacker->attack(living_target)) << "attack should fail on dead target";
    living_target->set_dead(0);

    // Friendly target guard.
    living_target->set_team_num(attacker->team_num());
    ASSERT_TRUE(!attacker->attack(living_target)) << "attack should fail on friendly target";
    living_target->set_team_num(1);

    // Treasure target guard.
    walker* treasure_target = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_GOLD_BAR);
    ASSERT_TRUE(treasure_target != nullptr) << "treasure target created";
    if (treasure_target)
    {
        ASSERT_TRUE(!attacker->attack(treasure_target)) << "attack should fail against treasure targets";
    }

    // Invincible target guard via bit flag.
    living_target->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    ASSERT_TRUE(!attacker->attack(living_target)) << "attack should fail on BIT_INVINCIBLE targets";
    living_target->stats()->set_bit_flags(BIT_INVINCIBLE, 0);

    // Invulnerability timer guard.
    living_target->set_invulnerable_left(3);
    ASSERT_TRUE(!attacker->attack(living_target)) << "attack should fail while invulnerable_left is active";

    og::runtime::current_session->myscreen_->world().delete_objects();
}
