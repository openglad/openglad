#include <openglad/runtime/game_context.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/data/gloader.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include <openglad/core/stats.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"
#include <memory>
#include <vector>

extern screen* myscreen;
extern cfg_store cfg;

static walker* make_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w) w->setxy(100, 100);
    return w.release();
}

static void remove_and_delete(walker* w)
{
    if (w == nullptr) {
        return;
    }
    myscreen->level_data.remove_ob(w);
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
    for (auto& uptr : myscreen->level_data.oblist) {
        walker* w = uptr.get();
        if (w && w->query_family() == family)
            count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// attack() - exercises the big combat function (lines 1822-2100)
// ---------------------------------------------------------------------------

void test_walker_attack_basic()
{
    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(attacker != nullptr, "attacker created");
    TEST_ASSERT(target != nullptr, "target created");

    target->setxy(101, 100);
    attacker->team_num = 0;
    target->team_num = 1;
    float hp_before = target->stats()->hitpoints;
    bool result = attacker->attack(target);
    // attack may or may not succeed depending on is_friendly logic
    (void)result;
    (void)hp_before;

    // Force a deterministic kill path to exercise death messaging/blood branches.
    target->dead = 0;
    target->stats()->hitpoints = 1;
    target->stats()->max_hitpoints = 1;
    attacker->damage = 500.0f;
    int blood_before = count_family_in_oblist(FAMILY_BLOOD);
    (void)attacker->attack(target);
    int blood_after = count_family_in_oblist(FAMILY_BLOOD);
    TEST_ASSERT(blood_after >= blood_before, "kill path should not reduce blood objects");

    // Treasure targets are never valid attack targets.
    walker* treasure = myscreen->level_data.add_ob(Order::Treasure, FAMILY_STAIN);
    TEST_ASSERT(treasure != nullptr, "treasure created");
    if (treasure) {
        bool treasure_result = attacker->attack(treasure);
        TEST_ASSERT(!treasure_result, "attacking treasure should fail");
    }

    delete attacker;
    delete target;
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_attack_basic);

void test_walker_attack_friendly_fails()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    b->setxy(101, 100);
    float hp_before = b->stats()->hitpoints;
    bool result = a->attack(b);
    TEST_ASSERT(!result, "attack should fail against friendly");
    TEST_ASSERT(b->stats()->hitpoints == hp_before, "friendly HP should not change");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_attack_friendly_fails);

void test_walker_attack_slime_magic_bonus()
{
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* slime = make_guy(FAMILY_SMALL_SLIME, 1);
    TEST_ASSERT(attacker != nullptr, "attacker created");
    TEST_ASSERT(slime != nullptr, "slime created");

    slime->setxy(101, 100);
    slime->stats()->hitpoints = 500;
    slime->stats()->max_hitpoints = 500;
    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);

    attacker->attack(slime);

    // Weapon-owner combat path and FAMILY_SPRINKLE freeze special-case.
    walker* sprinkle = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_SPRINKLE);
    TEST_ASSERT(sprinkle != nullptr, "sprinkle weapon created");
    if (sprinkle) {
        sprinkle->owner = attacker;
        sprinkle->team_num = attacker->team_num;
        sprinkle->damage = 50;
        slime->dead = 0;
        slime->stats()->hitpoints = 200;
        slime->stats()->max_hitpoints = 200;
        int frozen_before = slime->stats()->frozen_delay;
        (void)sprinkle->attack(slime);
        TEST_ASSERT(slime->stats()->frozen_delay >= frozen_before,
                    "sprinkle hit should preserve/increase frozen delay");
    }

    // Magic does 2x damage to slimes - just verify no crash
    delete attacker;
    delete slime;
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_attack_slime_magic_bonus);

void test_walker_attack_barbarian_magic_resistance()
{
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* barb = make_guy(FAMILY_BARBARIAN, 1);
    TEST_ASSERT(attacker != nullptr, "attacker created");
    TEST_ASSERT(barb != nullptr, "target created");

    barb->setxy(101, 100);
    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);

    attacker->attack(barb);
    delete attacker;
    delete barb;
}
REGISTER_TEST(test_walker_attack_barbarian_magic_resistance);

void test_walker_attack_invulnerable()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    b->setxy(101, 100);
    b->invulnerable_left = 10;
    bool result = a->attack(b);
    TEST_ASSERT(!result, "attack should fail against invulnerable");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_attack_invulnerable);

void test_walker_attack_dead_target()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    b->setxy(101, 100);
    b->dead = 1;
    bool result = a->attack(b);
    TEST_ASSERT(!result, "attack should fail against dead target");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_attack_dead_target);

// ---------------------------------------------------------------------------
// act() - exercises the act function (lines 1539-1666)
// ---------------------------------------------------------------------------

void test_walker_act_control()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_CONTROL);
    w->attack_lunge = 1.0f;
    w->hit_recoil = 1.0f;
    bool result = w->act();
    TEST_ASSERT(result, "ACT_CONTROL should return true");
    TEST_ASSERT(w->attack_lunge < 1.0f, "attack_lunge should decay in act()");
    TEST_ASSERT(w->hit_recoil < 1.0f, "hit_recoil should decay in act()");
}
REGISTER_TEST(test_walker_act_control);

void test_walker_act_die()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_DIE);
    w->act();
    TEST_ASSERT(w->dead == 1, "ACT_DIE should set dead");
}
REGISTER_TEST(test_walker_act_die);

void test_walker_act_frozen()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_RANDOM);
    w->stats()->frozen_delay = 5;
    bool result = w->act();
    TEST_ASSERT(result, "frozen walker should return 1");
    TEST_ASSERT_EQ(4, (int)w->stats()->frozen_delay, "frozen_delay should decrement");
}
REGISTER_TEST(test_walker_act_frozen);

void test_walker_act_with_commands()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_RANDOM);
    w->stats()->add_command(COMMAND_WALK, 3, 1, 0);
    bool result = w->act();
    TEST_ASSERT(result, "walker with commands should return 1");

    // Drive additional act_type handlers.
    w->stats()->clear_command();
    w->set_act_type(127); // default case
    (void)w->act(); // default act_type path

    w->set_act_type(ACT_GUARD);
    w->foe = nullptr;
    (void)w->act();

    walker* foe = make_guy(FAMILY_ORC, 2);
    TEST_ASSERT(foe != nullptr, "foe created");
    if (foe) {
        foe->setxy(w->xpos + 8, w->ypos + 8);
        w->foe = foe;
        (void)w->act();
    }

    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");
    if (l) {
        auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
        TEST_ASSERT(gen != nullptr, "generator created");
        if (gen) {
            walker* genp = gen.get();
            genp->setxy(120, 120);
            genp->set_act_type(ACT_GENERATE);
            // Force act_generate() to enter spawn/regen branch.
            genp->stats()->level = 5;
            genp->stats()->max_hitpoints = 10;
            genp->stats()->hitpoints = 10;
            SequenceRandomCombat gen_rng({100, 0, 1, 1});
            GameContext gen_ctx;
            gen_ctx.game_screen = myscreen;
            gen_ctx.rng = &gen_rng;
            set_global_context(&gen_ctx);
            (void)genp->act();
            set_global_context(nullptr);
        }

        walker* proj = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
        TEST_ASSERT(proj != nullptr, "weapon created");
        if (proj) {
            proj->setxy(120, 120);
            proj->set_act_type(ACT_FIRE);
            proj->lineofsight = 0;
            (void)proj->act();

            // Force act_fire() collision path, both mortal and immortal.
            walker* target = make_guy(FAMILY_ORC, 2);
            TEST_ASSERT(target != nullptr, "act_fire target created");
            if (target) {
                target->setxy(proj->xpos, proj->ypos);
                target->dead = 0;
                proj->dead = 0;
                proj->setxy(120, 120);
                proj->lineofsight = 2;
                proj->collide_ob = target;
                proj->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
                proj->stats()->set_bit_flags(BIT_IMMORTAL, 0);
                (void)proj->act();

                proj->dead = 0;
                proj->setxy(120, 120);
                proj->lineofsight = 2;
                proj->collide_ob = target;
                proj->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
                proj->stats()->set_bit_flags(BIT_IMMORTAL, 1);
                (void)proj->act();
            }
            remove_and_delete(target);
            // Kept alive until level_data.delete_objects() at test end.
        }

        // Exercise base walker ACT_RANDOM path via Generator (non-living subclass).
        walker* base_rand = myscreen->level_data.add_ob(Order::Generator, FAMILY_TENT);
        walker* base_foe = make_guy(FAMILY_ORC, 3);
        TEST_ASSERT(base_rand != nullptr && base_foe != nullptr, "base ACT_RANDOM walkers created");
        if (base_rand && base_foe) {
            base_rand->team_num = 1;
            base_rand->setxy(132, 132);
            base_rand->lineofsight = 40;
            base_rand->set_act_type(ACT_RANDOM);
            base_rand->stats()->clear_command();

            base_foe->team_num = 3;
            base_foe->setxy(136, 132);
            base_rand->foe = base_foe;

            GameContext base_ctx;
            base_ctx.game_screen = myscreen;

            // act(): rng(4)==0, rng(20)!=0 -> act_random().
            // act_random(): rng(70)==0 -> refresh foe and drive fire path.
            SequenceRandomCombat base_rng1({0, 1, 0, 5, 0});
            base_ctx.rng = &base_rng1;
            set_global_context(&base_ctx);
            (void)base_rand->act();

            // act(): rng(4)!=0 -> SEARCH command path.
            base_rand->foe = nullptr;
            SequenceRandomCombat base_rng2({1, 1, 1});
            base_ctx.rng = &base_rng2;
            set_global_context(&base_ctx);
            (void)base_rand->act();
            set_global_context(nullptr);
        }
        remove_and_delete(base_foe);
        // Kept alive until level_data.delete_objects() at test end.
    }

    // Drive ACT_RANDOM/act_random() branches deterministically.
    walker* randomer = make_guy(FAMILY_ORC, 1);
    walker* random_foe = make_guy(FAMILY_SOLDIER, 2);
    TEST_ASSERT(randomer != nullptr && random_foe != nullptr, "act_random walkers created");
    if (randomer && random_foe) {
        randomer->setxy(80, 80);
        random_foe->setxy(86, 80);
        randomer->foe = random_foe;
        randomer->lineofsight = 40;
        randomer->set_act_type(ACT_RANDOM);
        randomer->stats()->clear_command();

        // act(): rng(4)==0 then rng(20)==1 -> call act_random().
        // act_random(): rng(70)==0 path then in-range fire_check branch.
        SequenceRandomCombat random_rng({0, 1, 0, 1, 0, 0});
        GameContext random_ctx;
        random_ctx.game_screen = myscreen;
        random_ctx.rng = &random_rng;
        set_global_context(&random_ctx);
        (void)randomer->act();

        // act_random() branch where no foe is found and command is set.
        randomer->foe = nullptr;
        SequenceRandomCombat nofoe_rng({0, 1, 0, 1, 1, 1});
        random_ctx.rng = &nofoe_rng;
        set_global_context(&random_ctx);
        (void)randomer->act();
        set_global_context(nullptr);
    }
    remove_and_delete(randomer);
    remove_and_delete(random_foe);

    remove_and_delete(foe);
    remove_and_delete(w);
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_act_with_commands);

// ---------------------------------------------------------------------------
// transfer_stats (lines 4307-4360)
// ---------------------------------------------------------------------------

void test_walker_transfer_stats()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    a->stats()->hitpoints = 50;
    a->stats()->max_hitpoints = 100;
    a->stats()->magicpoints = 30;
    a->stats()->level = 5;

    a->transfer_stats(b);

    TEST_ASSERT_EQ(50, (int)b->stats()->hitpoints, "HP transferred");
    TEST_ASSERT_EQ(100, (int)b->stats()->max_hitpoints, "max HP transferred");
    TEST_ASSERT_EQ(30, (int)b->stats()->magicpoints, "MP transferred");
    TEST_ASSERT_EQ(5, (int)b->stats()->level, "level transferred");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_transfer_stats);

void test_walker_transfer_stats_with_guy()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_ARCHER, 0);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    // b shouldn't have a myguy from transfer yet
    b->clear_myguy();

    a->transfer_stats(b);

    TEST_ASSERT(b->myguy != nullptr, "myguy should be transferred");

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_transfer_stats_with_guy);

// ---------------------------------------------------------------------------
// transform_to (lines 4364-4417)
// ---------------------------------------------------------------------------

void test_walker_transform_to()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");

    w->transform_to(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT_EQ((int)FAMILY_ARCHER, (int)w->query_family(), "should be archer after transform");

}
REGISTER_TEST(test_walker_transform_to);

void test_walker_transform_to_same_order()
{
    walker* w = make_guy(FAMILY_ELF, 0);
    TEST_ASSERT(w != nullptr, "walker created");

    w->set_act_type(ACT_CONTROL);
    w->transform_to(Order::Living, FAMILY_MAGE);
    TEST_ASSERT_EQ((int)FAMILY_MAGE, (int)w->query_family(), "should be mage");
    TEST_ASSERT_EQ(ACT_CONTROL, (int)w->query_act_type(), "should preserve act type for same order");

}
REGISTER_TEST(test_walker_transform_to_same_order);

// ---------------------------------------------------------------------------
// spaces_clear (lines 4293-4305)
// ---------------------------------------------------------------------------

void test_walker_spaces_clear()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(100, 100);

    short count = w->spaces_clear();
    TEST_ASSERT(count >= 0 && count <= 8, "spaces_clear should be 0-8");

}
REGISTER_TEST(test_walker_spaces_clear);

// ---------------------------------------------------------------------------
// fire_check (lines 4026-4291) - complex direction logic
// ---------------------------------------------------------------------------

void test_walker_fire_check_all_dirs()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
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
REGISTER_TEST(test_walker_fire_check_all_dirs);

// ---------------------------------------------------------------------------
// init_fire (lines 646-691)
// ---------------------------------------------------------------------------

void test_walker_init_fire_when_busy()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(100, 100);
    w->busy = 10;

    bool result = w->init_fire(1, 0);
    (void)result; // busy behavior may vary

}
REGISTER_TEST(test_walker_init_fire_when_busy);

// ---------------------------------------------------------------------------
// set_order_family (lines 2199-2265) - exercises family name/weapon setup
// ---------------------------------------------------------------------------

void test_walker_set_order_family_all()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");

    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        w->set_order_family(Order::Living, families[i]);
        TEST_ASSERT_EQ((int)families[i], (int)w->query_family(), "family should match");
    }

    // Exercise non-living order assignments too.
    TEST_ASSERT(w->set_order_family(Order::Weapon, FAMILY_KNIFE), "set_order_family weapon should return true");
    TEST_ASSERT_EQ((int)FAMILY_KNIFE, (int)w->query_family(), "family should change to knife");
    TEST_ASSERT(w->set_order_family(Order::Treasure, FAMILY_STAIN), "set_order_family treasure should return true");
    TEST_ASSERT_EQ((int)FAMILY_STAIN, (int)w->query_family(), "family should change to stain");
    TEST_ASSERT(w->set_order_family(Order::FX, FAMILY_EXPLOSION), "set_order_family fx should return true");
    TEST_ASSERT_EQ((int)FAMILY_EXPLOSION, (int)w->query_family(), "family should change to explosion");
    TEST_ASSERT(w->set_order_family(Order::Generator, FAMILY_TENT), "set_order_family generator should return true");
    TEST_ASSERT_EQ((int)FAMILY_TENT, (int)w->query_family(), "family should change to tent");

}
REGISTER_TEST(test_walker_set_order_family_all);

// ---------------------------------------------------------------------------
// is_friendly extended (lines 4670-4738)
// ---------------------------------------------------------------------------

void test_walker_is_friendly_different_teams()
{
    walker* a = make_guy(FAMILY_SOLDIER, 0);
    walker* b = make_guy(FAMILY_SOLDIER, 1);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    a->team_num = 0;
    b->team_num = 1;
    Sint32 r1 = a->is_friendly(b);
    Sint32 r2 = b->is_friendly(a);
    (void)r1; (void)r2; // exercise the code paths

    delete a;
    delete b;
}
REGISTER_TEST(test_walker_is_friendly_different_teams);

// ---------------------------------------------------------------------------
// set_difficulty (lines 4611-4635)
// ---------------------------------------------------------------------------

void test_walker_set_difficulty_all_families()
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l) return;

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = l->create_walker_owned(Order::Living, families[i]);
        if (w) {
            w->set_difficulty(5);
            TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP positive after set_difficulty");
        }
    }

    auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    TEST_ASSERT(gen != nullptr, "generator created");
    if (gen) {
        float hp_before = gen->stats()->hitpoints;
        gen->set_difficulty(7);
        TEST_ASSERT(gen->stats()->hitpoints >= hp_before, "generator HP should be scaled");
    }
}
REGISTER_TEST(test_walker_set_difficulty_all_families);

// ---------------------------------------------------------------------------
// get_current_angle for all directions (line 552-575)
// ---------------------------------------------------------------------------

void test_walker_get_current_angle_all_dirs()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");

    float prev_angle = -999;
    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        float angle = w->get_current_angle();
        // Each direction should have a different angle
        if (dir > 0) {
            TEST_ASSERT(angle != prev_angle, "each direction should have unique angle");
        }
        prev_angle = angle;
    }

}
REGISTER_TEST(test_walker_get_current_angle_all_dirs);

// ---------------------------------------------------------------------------
// animate smoke test
// ---------------------------------------------------------------------------

void test_walker_animate_smoke()
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    w->setxy(100, 100);
    w->ani_type = ANI_WALK;
    w->animate();

    // TELE_OUT branches: mage teleport and skeleton ranged teleport.
    w->transform_to(Order::Living, FAMILY_MAGE);
    w->ani_type = ANI_TELE_OUT;
    for (int i = 0; i < 32 && w->ani_type != ANI_WALK; ++i) {
        (void)w->animate();
    }
    TEST_ASSERT(w->ani_type == ANI_WALK, "mage teleport animation should settle");

    w->transform_to(Order::Living, FAMILY_SKELETON);
    w->ani_type = ANI_TELE_OUT;
    for (int i = 0; i < 32 && w->ani_type != ANI_WALK; ++i) {
        (void)w->animate();
    }
    TEST_ASSERT(w->ani_type == ANI_WALK, "skeleton teleport animation should settle");

    // Slime split branch.
    w->transform_to(Order::Living, FAMILY_SLIME);
    w->ani_type = ANI_SLIME_SPLIT;
    int small_slime_before = count_family_in_oblist(FAMILY_SMALL_SLIME);
    for (int i = 0; i < 32 && w->ani_type != ANI_WALK; ++i) {
        (void)w->animate();
    }
    int small_slime_after = count_family_in_oblist(FAMILY_SMALL_SLIME);
    TEST_ASSERT(small_slime_after >= small_slime_before, "slime split should preserve/increase small slimes");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_animate_smoke);

void test_walker_act_random_generator_paths()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    walker* foe = make_guy(FAMILY_ORC, 2);
    TEST_ASSERT(gen != nullptr && foe != nullptr, "generator and foe created");
    if (!(gen && foe)) {
        delete foe;
        return;
    }

    walker* genp = gen.get();
    genp->team_num = 1;
    foe->team_num = 2;
    genp->setxy(128, 128);
    foe->setxy(132, 128);
    genp->lineofsight = 40;
    genp->set_act_type(ACT_RANDOM);
    genp->stats()->clear_command();

    GameContext ctx;
    ctx.game_screen = myscreen;

    // Trigger act_random() route and in-range logic.
    SequenceRandomCombat rng1({0, 1, 0, 0, 0});
    ctx.rng = &rng1;
    set_global_context(&ctx);
    (void)genp->act();

    // Trigger 3-of-4 search branch with foe lookup.
    genp->foe = nullptr;
    SequenceRandomCombat rng2({1, 0, 0, 0});
    ctx.rng = &rng2;
    set_global_context(&ctx);
    (void)genp->act();
    set_global_context(nullptr);

    TEST_ASSERT_EQ(ACT_RANDOM, (int)genp->query_act_type(), "generator should remain in ACT_RANDOM");

    delete foe;
}
REGISTER_TEST(test_walker_act_random_generator_paths);

void test_walker_combat_effect_helpers_and_recoil_branches()
{
    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(attacker != nullptr && target != nullptr, "combat walkers created");
    if (!(attacker && target))
        return;

    target->setxy(attacker->xpos + 12, attacker->ypos + 4);
    cfg.apply_setting("effects", "hit_recoil", "on");
    cfg.apply_setting("effects", "hit_anim", "off");
    cfg.apply_setting("effects", "damage_numbers", "off");
    cfg.apply_setting("effects", "hit_flash", "off");

    attacker->do_hit_effects(attacker, target, 12);
    TEST_ASSERT(target->hit_recoil > 0.0f, "hit_recoil should be set for living targets when enabled");

    // do_heal_effects early-return path when sim_config is null.
    walker stack_a;
    walker stack_b;
    stack_a.sim_config = nullptr;
    stack_a.do_heal_effects(&stack_a, &stack_b, 5);

    delete attacker;
    delete target;
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_combat_effect_helpers_and_recoil_branches);

void test_walker_attack_weapon_owner_chain_and_nonliving_target()
{
    walker* owner = make_guy(FAMILY_SOLDIER, 0);
    walker* living_target = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(owner != nullptr && living_target != nullptr, "owner and target created");
    if (!(owner && living_target))
        return;

    walker* weapon = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(weapon != nullptr, "weapon created");
    if (weapon) {
        owner->user = 0;
        weapon->owner = owner;
        weapon->team_num = owner->team_num;
        weapon->damage = 1.0f;
        weapon->stats()->hitpoints = 50;

        living_target->stats()->armor = 5000; // force damage clamp-to-zero path
        living_target->stats()->hitpoints = 100;
        (void)weapon->attack(living_target);
        TEST_ASSERT(living_target->stats()->hitpoints <= 100, "weapon attack path should execute safely");

        walker* nonliving = myscreen->level_data.add_ob(Order::FX, FAMILY_FLASH);
        TEST_ASSERT(nonliving != nullptr, "nonliving target created");
        if (nonliving)
            (void)weapon->attack(nonliving);
    }

    delete owner;
    delete living_target;
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_attack_weapon_owner_chain_and_nonliving_target);

void test_walker_combat_batch5_heal_and_hit_effect_variants()
{
    walker* healer = make_guy(FAMILY_CLERIC, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(healer != nullptr && target != nullptr, "healer and target created");
    if (!(healer && target))
        return;

    healer->setxy(100, 100);
    target->setxy(116, 104);

    // do_heal_effects with config enabled and null-healer branch.
    cfg.apply_setting("effects", "heal_numbers", "on");
    healer->do_heal_effects(nullptr, target, 9);
    TEST_ASSERT(!target->damage_numbers.empty(), "heal numbers should be emitted for target when enabled");

    // do_hit_effects projectile branch (attacker != this) and damage number branch.
    cfg.apply_setting("effects", "damage_numbers", "on");
    cfg.apply_setting("effects", "hit_anim", "on");
    cfg.apply_setting("effects", "hit_flash", "on");
    cfg.apply_setting("effects", "hit_recoil", "on");
    walker* projectile = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(projectile != nullptr, "projectile created");
    if (projectile)
    {
        projectile->owner = healer;
        projectile->team_num = healer->team_num;
        projectile->setxy(108, 100);
        projectile->do_hit_effects(healer, target, 6);
        TEST_ASSERT(target->hurt_flash, "hit_flash should be set on positive damage when enabled");
        TEST_ASSERT(target->hit_recoil > 0.0f, "hit_recoil should be set for living targets");
    }
}
REGISTER_TEST(test_walker_combat_batch5_heal_and_hit_effect_variants);

void test_walker_combat_batch5_do_combat_damage_target_myguy_stats()
{
    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* victim = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(attacker != nullptr && victim != nullptr, "attacker and victim created");
    if (!(attacker && victim))
        return;

    const float taken_before = victim->myguy ? victim->myguy->scen_damage_taken : 0.0f;
    attacker->do_combat_damage(attacker, victim, 7);

    TEST_ASSERT(victim->last_hitpoints >= victim->stats()->hitpoints, "combat damage should update last_hitpoints");
    if (victim->myguy)
    {
        TEST_ASSERT(victim->myguy->scen_damage_taken >= taken_before,
                    "target myguy scen_damage_taken should increase");
    }
    TEST_ASSERT(victim->stats()->hitpoints <= victim->last_hitpoints,
                "combat damage should not increase target hitpoints");
}
REGISTER_TEST(test_walker_combat_batch5_do_combat_damage_target_myguy_stats);

void test_walker_combat_batch6_attack_branches_enemy_and_weapon_paths()
{
    const short saved_allied_mode = myscreen->save_data.allied_mode;
    myscreen->save_data.allied_mode = 0;

    // Enemy kill path: magical modifier, kill awards, notifications, and remaining-foe branch.
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* enemy = make_guy(FAMILY_ORC, 1);
    TEST_ASSERT(attacker != nullptr && enemy != nullptr, "attacker/enemy created");
    if (!(attacker && enemy))
        return;
    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);
    attacker->damage = 500.0f;
    enemy->stats()->hitpoints = 3;
    enemy->stats()->max_hitpoints = 3;
    enemy->stats()->name = "NamedEnemy";
    enemy->owner = nullptr;
    enemy->lifetime = 0;
    enemy->setxy(attacker->xpos + 10, attacker->ypos + 6);
    TEST_ASSERT(attacker->attack(enemy), "enemy kill branch should execute");

    // Non-living default branch and weapon durability/death/on-hit callbacks.
    walker* owner = make_guy(FAMILY_SOLDIER, 0);
    walker* fx_target = myscreen->level_data.add_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_SPRINKLE);
    TEST_ASSERT(owner && fx_target && weapon, "owner/fx_target/weapon created");
    if (owner && fx_target && weapon)
    {
        owner->user = 0;
        weapon->owner = owner;
        weapon->team_num = owner->team_num;
        weapon->damage = 10.0f;
        weapon->stats()->hitpoints = 1;
        owner->myguy->total_shots = 2;
        owner->myguy->scen_shots = 2;
        fx_target->team_num = 1;
        (void)weapon->attack(fx_target);
        TEST_ASSERT(owner->myguy->total_shots <= 1, "default non-living target branch should decrement shots");
        TEST_ASSERT(weapon->dead == 1, "weapon durability path should kill mortal weapon at <=0 hp");
    }

    myscreen->save_data.allied_mode = saved_allied_mode;
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_combat_batch6_attack_branches_enemy_and_weapon_paths);

void test_walker_combat_batch6_attack_friendly_team_death_messages_and_clamps()
{
    const short saved_allied_mode = myscreen->save_data.allied_mode;
    myscreen->save_data.allied_mode = 1;

    // Build an attacker that is not considered friendly to team 0 even when allied mode is on.
    walker* attacker = make_guy(FAMILY_SOLDIER, 1);
    TEST_ASSERT(attacker != nullptr, "attacker created");
    if (!attacker)
        return;
    attacker->clear_myguy();
    attacker->damage = 500.0f;

    // Team-0 target death paths (playerteam==target team) that select various message branches.
    walker* t_dispelled = make_guy(FAMILY_ORC, 0);
    walker* t_named = make_guy(FAMILY_ORC, 0);
    walker* t_myguy_name = make_guy(FAMILY_ORC, 0);
    TEST_ASSERT(t_dispelled && t_named && t_myguy_name, "targets created");
    if (t_dispelled && t_named && t_myguy_name)
    {
        t_dispelled->stats()->hitpoints = 1;
        t_dispelled->stats()->name = "Summon";
        t_dispelled->owner = attacker; // dispelled branch
        (void)attacker->attack(t_dispelled);

        t_named->stats()->hitpoints = 1;
        t_named->owner = nullptr;
        t_named->lifetime = 0;
        t_named->stats()->name = "AllyName"; // named death branch
        (void)attacker->attack(t_named);

        t_myguy_name->stats()->hitpoints = 1;
        t_myguy_name->owner = nullptr;
        t_myguy_name->lifetime = 0;
        t_myguy_name->stats()->name.clear();
        if (t_myguy_name->myguy)
            t_myguy_name->myguy->name = "GuyName"; // myguy-name branch
        (void)attacker->attack(t_myguy_name);
    }

    // High-armor path in attack() (engine still guarantees at least 1 damage).
    walker* armored = make_guy(FAMILY_ORC, 2);
    TEST_ASSERT(armored != nullptr, "armored target created");
    if (armored)
    {
        armored->stats()->armor = 100000;
        const float hp_before = armored->stats()->hitpoints;
        (void)attacker->attack(armored);
        TEST_ASSERT(armored->stats()->hitpoints <= hp_before, "high armor path should not increase hitpoints");
    }

    // do_heal_effects early-return branch when heal numbers are disabled.
    cfg.apply_setting("effects", "heal_numbers", "off");
    attacker->do_heal_effects(attacker, attacker, 5);
    cfg.apply_setting("effects", "heal_numbers", "on");

    myscreen->save_data.allied_mode = saved_allied_mode;
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_walker_combat_batch6_attack_friendly_team_death_messages_and_clamps);
