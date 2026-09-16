#include <openglad/interface/game_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/combat_math.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <list>
#include <memory>
#include <string>
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

// Death stains are added with add_ob(Order::Weapon, FAMILY_BLOOD), and
// GameWorld::add_ob routes every Order::Weapon into weaplist -- never oblist.
// A blood oracle that scans oblist can therefore never move.
static int count_family_in_weaplist(char family)
{
    int count = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().weaplist) {
        walker* w = uptr.get();
        if (w && w->family() == family)
            count++;
    }
    return count;
}

static GameWorld& combat_world()
{
    return og::runtime::current_session->myscreen_->world();
}

// Two independent RNG streams reach combat code, and they need separate
// overrides:
//   * combat_rng() -- base damage, armor reduction, XP -- reads the gameplay
//     override that push_test_context installs from a GameContext;
//   * every AI cadence draw (walker::act, act_random, act_generate,
//     act_guard) and every Lua hook that calls og.* math go through
//     GameWorld::rng_, which consults ONLY the sim override, installed by
//     ScopedSimRandom (tests/test_sim_random_scope.h).
// A test that pins an exact damage number needs the first (ScopedCombatRandom
// below); a test that pins which AI branch ran needs the second.
class ScopedCombatRandom {
public:
    explicit ScopedCombatRandom(IRandom* rng) { ctx_.rng = rng; push_test_context(&ctx_); }
    ~ScopedCombatRandom() { pop_test_context(); }
    ScopedCombatRandom(const ScopedCombatRandom&) = delete;
    ScopedCombatRandom& operator=(const ScopedCombatRandom&) = delete;
private:
    GameContext ctx_;
};

// First Notification whose text is a "<something> DIED!" death toast.
static std::string first_death_notification()
{
    if (!(current_game && current_game->sim_events))
        return {};
    const std::string suffix = " DIED!";
    for (const auto& ev : current_game->sim_events->events())
    {
        if (ev.kind != og::sim::EventKind::Notification)
            continue;
        if (ev.text.size() > suffix.size() &&
            ev.text.compare(ev.text.size() - suffix.size(),
                            suffix.size(), suffix) == 0)
            return ev.text;
    }
    return {};
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
    level.world().grid.data[static_cast<std::size_t>(gx + level.world().grid.w * gy)] = tile;
}

// ---------------------------------------------------------------------------
// attack() - exercises the big combat function (lines 1822-2100)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_attack_basic)
{
    // Zero draws make get_base_damage()/get_damage_reduction() exact.
    SequenceRandomCombat zero({0});
    ScopedCombatRandom combat_rng(&zero);

    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    ASSERT_NE(nullptr, attacker) << "attacker created";
    ASSERT_NE(nullptr, target) << "target created";
    ASSERT_NE(nullptr, attacker->myguy) << "attacker carries a company record";

    target->setxy(101, 100);
    attacker->set_team_num(0);
    target->set_team_num(1);
    target->stats()->set_armor(0);
    target->stats()->set_max_hitpoints(200.0f);
    target->stats()->set_hitpoints(200.0f);
    attacker->set_damage(20.0f);

    const float base = compute_base_damage(20.0f, zero);
    const short expected_hit = damage_to_hit_points(base);
    ASSERT_TRUE(attacker->attack(target)) << "a hostile living is a valid attack target";
    ASSERT_FLOAT_EQ(200.0f - static_cast<float>(expected_hit), target->stats()->hitpoints())
        << "attack() must subtract exactly damage_to_hit_points(base damage - armor reduction)";

    // Force a deterministic kill path to exercise death accounting/blood.
    target->set_dead(0);
    target->stats()->set_hitpoints(1);
    target->stats()->set_max_hitpoints(1);
    attacker->set_damage(500.0f);
    const int kills_before = attacker->myguy->scen_kills;
    const int level_kills_before = attacker->myguy->level_kills;
    const int blood_before = count_family_in_weaplist(FAMILY_BLOOD);
    ASSERT_TRUE(attacker->attack(target)) << "the killing blow lands";
    ASSERT_EQ(1, (int)target->dead()) << "a target taken below 0 hp must be marked dead";
    ASSERT_EQ(kills_before + 1, (int)attacker->myguy->scen_kills)
        << "the owner-chain head banks exactly one scenario kill";
    ASSERT_EQ(level_kills_before + (int)target->stats()->level(),
              attacker->myguy->level_kills)
        << "level_kills grows by the defeated target's level";
    ASSERT_EQ(blood_before + 1, count_family_in_weaplist(FAMILY_BLOOD))
        << "a living death splats exactly one FAMILY_BLOOD stain (into weaplist)";

    // Treasure targets are never valid attack targets.
    walker* treasure = combat_world().add_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, treasure) << "treasure created";
    ASSERT_FALSE(attacker->attack(treasure)) << "attacking treasure should fail";

    delete attacker;
    delete target;
    combat_world().delete_objects();
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


TEST(WalkerCombat, same_team_scenario_npcs_can_never_damage_roster_heroes)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const short saved_allied_mode = world.allied_mode;

    for (const short allied_mode : {short{0}, short{1}})
    {
        world.allied_mode = allied_mode;
        for (unsigned char team = 0; team < 4; ++team)
        {
            walker* hero = make_guy(FAMILY_SOLDIER, team);
            walker* npc = make_guy(FAMILY_ARCHER, team);
            ASSERT_NE(nullptr, hero);
            ASSERT_NE(nullptr, npc);
            npc->clear_myguy();
            hero->stats()->set_hitpoints(1);
            npc->stats()->set_hitpoints(1);
            hero->set_damage(500.0f);
            npc->set_damage(500.0f);

            ASSERT_TRUE(hero->is_friendly(npc))
                << "allied=" << allied_mode << " team=" << static_cast<int>(team);
            ASSERT_TRUE(npc->is_friendly(hero))
                << "allied=" << allied_mode << " team=" << static_cast<int>(team);

            const float hero_hp = hero->stats()->hitpoints();
            const float npc_hp = npc->stats()->hitpoints();
            EXPECT_FALSE(npc->attack(hero));
            EXPECT_FALSE(hero->attack(npc));
            EXPECT_EQ(hero_hp, hero->stats()->hitpoints());
            EXPECT_EQ(npc_hp, npc->stats()->hitpoints());
            EXPECT_FALSE(hero->dead());
            EXPECT_FALSE(npc->dead());

            walker* projectile =
                world.add_weap_ob(Order::Weapon, FAMILY_ARROW);
            ASSERT_NE(nullptr, projectile);
            projectile->set_owner(npc);
            projectile->set_team_num(team);
            projectile->set_damage(500.0f);
            EXPECT_FALSE(projectile->attack(hero));
            EXPECT_EQ(hero_hp, hero->stats()->hitpoints());
            EXPECT_FALSE(hero->dead());

            ASSERT_TRUE(world.remove_ob(projectile));
            delete hero;
            delete npc;
        }
    }

    world.allied_mode = saved_allied_mode;
}

TEST(WalkerCombat, team_color_hostility_matrix_ignores_company_ownership_and_pvp_mode)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const short saved_allied_mode = world.allied_mode;

    // Cover every ordered color pair, both PVP seating modes, and every
    // ownership shape: scenario/scenario, company/scenario,
    // scenario/company, and company/company.
    for (const short allied_mode : {short{0}, short{1}})
    {
        world.allied_mode = allied_mode;
        for (unsigned char attacker_team = 0; attacker_team < 4;
             ++attacker_team)
        {
            for (unsigned char target_team = 0; target_team < 4;
                 ++target_team)
            {
                for (unsigned ownership = 0; ownership < 4; ++ownership)
                {
                    std::unique_ptr<walker> attacker(
                        make_guy(FAMILY_SOLDIER, attacker_team));
                    std::unique_ptr<walker> target(
                        make_guy(FAMILY_ARCHER, target_team));
                    ASSERT_NE(nullptr, attacker);
                    ASSERT_NE(nullptr, target);
                    if ((ownership & 1u) == 0)
                        attacker->clear_myguy();
                    if ((ownership & 2u) == 0)
                        target->clear_myguy();

                    const bool same_team = attacker_team == target_team;
                    SCOPED_TRACE(::testing::Message()
                        << "allied=" << allied_mode
                        << " attacker_team=" << static_cast<int>(attacker_team)
                        << " target_team=" << static_cast<int>(target_team)
                        << " ownership=" << ownership);
                    EXPECT_EQ(same_team, attacker->is_friendly(target.get()) != 0);
                    EXPECT_EQ(same_team, target->is_friendly(attacker.get()) != 0);
                    EXPECT_EQ(same_team,
                              attacker->is_friendly_to_team(target_team) != 0);

                    target->stats()->set_armor(0);
                    target->stats()->set_max_hitpoints(5000.0f);
                    target->stats()->set_hitpoints(5000.0f);
                    attacker->set_damage(12.0f);
                    const float hp_before = target->stats()->hitpoints();
                    EXPECT_EQ(!same_team, attacker->attack(target.get()));
                    if (same_team)
                        EXPECT_EQ(hp_before, target->stats()->hitpoints());
                    else
                        EXPECT_LT(target->stats()->hitpoints(), hp_before);
                }
            }
        }
    }

    world.allied_mode = saved_allied_mode;
}

TEST(WalkerCombat, owned_projectiles_use_team_color_not_company_ownership)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const short saved_allied_mode = world.allied_mode;

    for (const short allied_mode : {short{0}, short{1}})
    {
        world.allied_mode = allied_mode;
        for (unsigned char owner_team = 0; owner_team < 4; ++owner_team)
        {
            for (unsigned char target_team = 0; target_team < 4;
                 ++target_team)
            {
                std::unique_ptr<walker> owner(
                    make_guy(FAMILY_ARCHER, owner_team));
                std::unique_ptr<walker> target(
                    make_guy(FAMILY_SOLDIER, target_team));
                ASSERT_NE(nullptr, owner);
                ASSERT_NE(nullptr, target);
                walker* projectile =
                    world.add_weap_ob(Order::Weapon, FAMILY_ARROW);
                ASSERT_NE(nullptr, projectile);
                projectile->set_owner(owner.get());
                projectile->set_team_num(owner_team);
                projectile->set_damage(12.0f);
                target->stats()->set_armor(0);
                target->stats()->set_max_hitpoints(5000.0f);
                target->stats()->set_hitpoints(5000.0f);
                world.m_score[owner_team] = 0;

                const bool same_team = owner_team == target_team;
                const float hp_before = target->stats()->hitpoints();
                const Uint32 score_before = world.m_score[owner_team];
                SCOPED_TRACE(::testing::Message()
                    << "allied=" << allied_mode
                    << " owner_team=" << static_cast<int>(owner_team)
                    << " target_team=" << static_cast<int>(target_team));
                EXPECT_EQ(!same_team, projectile->attack(target.get()));
                if (same_team)
                    EXPECT_EQ(hp_before, target->stats()->hitpoints());
                else
                {
                    EXPECT_LT(target->stats()->hitpoints(), hp_before);
                    EXPECT_GT(world.m_score[owner_team], score_before)
                        << "every company color must receive projectile score";
                }

                EXPECT_TRUE(world.remove_ob(projectile));
            }
        }
    }

    world.allied_mode = saved_allied_mode;
}

TEST(WalkerCombat, company_kill_credit_is_directional_for_every_team_pair)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const short saved_allied_mode = world.allied_mode;

    for (const short allied_mode : {short{0}, short{1}})
    {
        world.allied_mode = allied_mode;
        for (unsigned char attacker_team = 0; attacker_team < 4;
             ++attacker_team)
        {
            for (unsigned char target_team = 0; target_team < 4;
                 ++target_team)
            {
                if (attacker_team == target_team)
                    continue;

                std::unique_ptr<walker> attacker(
                    make_guy(FAMILY_SOLDIER, attacker_team));
                std::unique_ptr<walker> target(
                    make_guy(FAMILY_ARCHER, target_team));
                ASSERT_NE(nullptr, attacker);
                ASSERT_NE(nullptr, target);
                ASSERT_NE(nullptr, attacker->myguy);

                attacker->set_damage(500.0f);
                target->stats()->set_armor(0);
                target->stats()->set_max_hitpoints(1.0f);
                target->stats()->set_hitpoints(1.0f);
                const short kills_before = attacker->myguy->scen_kills;
                const int level_kills_before = attacker->myguy->level_kills;
                world.m_score[attacker_team] = 0;

                SCOPED_TRACE(::testing::Message()
                    << "allied=" << allied_mode
                    << " attacker_team=" << static_cast<int>(attacker_team)
                    << " target_team=" << static_cast<int>(target_team));
                EXPECT_TRUE(attacker->attack(target.get()));
                EXPECT_TRUE(target->dead());
                EXPECT_EQ(kills_before + 1, attacker->myguy->scen_kills);
                EXPECT_EQ(level_kills_before + target->stats()->level(),
                          attacker->myguy->level_kills);
                EXPECT_GT(world.m_score[attacker_team], 0u);
            }
        }
    }

    world.allied_mode = saved_allied_mode;
}

TEST(WalkerCombat, ai_targeting_range_queries_and_victory_use_the_same_team_rule)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const short saved_allied_mode = world.allied_mode;
    world.delete_objects();

    const auto add_company_hero = [&world](unsigned char team, short x) {
        guy member(FAMILY_SOLDIER);
        member.teamnum = team;
        auto entity = guy_create_walker_owned(
            member, og::runtime::current_session->myscreen_);
        if (entity == nullptr)
            return static_cast<walker*>(nullptr);
        entity->setxy(x, static_cast<short>(100));
        walker* const raw = entity.get();
        world.oblist.push_back(std::move(entity));
        return raw;
    };

    walker* const red = add_company_hero(0, 100);
    walker* const red_friend = add_company_hero(0, 108);
    walker* const yellow = add_company_hero(1, 120);
    walker* const green = add_company_hero(2, 140);
    ASSERT_NE(nullptr, red);
    ASSERT_NE(nullptr, red_friend);
    ASSERT_NE(nullptr, yellow);
    ASSERT_NE(nullptr, green);

    for (const short allied_mode : {short{0}, short{1}})
    {
        world.allied_mode = allied_mode;
        SCOPED_TRACE(::testing::Message() << "allied=" << allied_mode);
        EXPECT_EQ(yellow, world.find_far_foe(red))
            << "AI must skip a nearer same-team hero and acquire yellow";

        std::int32_t foe_count = 0;
        const std::list<walker*> foes = world.find_foes_in_range(
            world.oblist, 1000, &foe_count, red);
        EXPECT_EQ(2, foe_count);
        EXPECT_NE(foes.end(), std::find(foes.begin(), foes.end(), yellow));
        EXPECT_NE(foes.end(), std::find(foes.begin(), foes.end(), green));

        std::int32_t friend_count = 0;
        const std::list<walker*> friends = world.find_friends_in_range(
            world.oblist, 1000, &friend_count, red);
        EXPECT_EQ(2, friend_count);
        EXPECT_NE(friends.end(),
                  std::find(friends.begin(), friends.end(), red_friend));
        EXPECT_EQ(2, world.remaining_foes(red))
            << "different-color company heroes must block extermination";
    }

    world.delete_objects();
    world.allied_mode = saved_allied_mode;
}


TEST(WalkerCombat, walker_attack_slime_magic_bonus)
{
    SequenceRandomCombat zero({0});
    ScopedCombatRandom combat_rng(&zero);

    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* slime = make_guy(FAMILY_SMALL_SLIME, 1);
    ASSERT_NE(nullptr, attacker) << "attacker created";
    ASSERT_NE(nullptr, slime) << "slime created";

    slime->setxy(101, 100);
    slime->stats()->set_armor(0);
    slime->stats()->set_max_hitpoints(500.0f);
    attacker->set_damage(20.0f);

    // packs/core/families/living-08-slime.lua: small slime carries
    // magic_damage_modifier = 2, and attack()'s Living arm multiplies
    // tempdamage by it when the attacker has BIT_MAGICAL.
    const float base = compute_base_damage(20.0f, zero);
    const short expected_plain = damage_to_hit_points(base);
    const short expected_magic = damage_to_hit_points(base * 2.0f);
    ASSERT_LT(expected_plain, expected_magic)
        << "the fixture damage must be large enough to tell the two apart";

    attacker->stats()->set_bit_flags(BIT_MAGICAL, 0);
    slime->stats()->set_hitpoints(500.0f);
    ASSERT_TRUE(attacker->attack(slime)) << "plain hit lands";
    ASSERT_FLOAT_EQ(500.0f - static_cast<float>(expected_plain), slime->stats()->hitpoints())
        << "a non-magical attacker applies unmodified damage";

    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);
    slime->set_dead(0);
    slime->stats()->set_hitpoints(500.0f);
    ASSERT_TRUE(attacker->attack(slime)) << "magical hit lands";
    ASSERT_FLOAT_EQ(500.0f - static_cast<float>(expected_magic), slime->stats()->hitpoints())
        << "BIT_MAGICAL scales damage by the slime's magic_damage_modifier (2)";

    // Weapon-owner combat path and FAMILY_SPRINKLE freeze special-case:
    // weapon_on_hit_target writes the freeze roll straight onto the target.
    walker* sprinkle = combat_world().add_weap_ob(Order::Weapon, FAMILY_SPRINKLE);
    ASSERT_NE(nullptr, sprinkle) << "sprinkle weapon created";
    sprinkle->set_owner(attacker);
    sprinkle->set_team_num(attacker->team_num());
    sprinkle->set_damage(50.0f);
    slime->set_dead(0);
    slime->stats()->set_hitpoints(200.0f);
    slime->stats()->set_max_hitpoints(200.0f);
    slime->stats()->set_frozen_delay(0);

    // og.freeze_duration() draws from GameWorld::rng_; a constant 7 is below
    // the max_time bound and below kSprinkleRollKnee, so soften() is identity.
    SequenceRandomCombat sim_seven({7});
    ScopedSimRandom sim_rng(&sim_seven);
    ASSERT_TRUE(sprinkle->attack(slime)) << "sprinkle hit lands";
    ASSERT_EQ(7, (int)slime->stats()->frozen_delay())
        << "FAMILY_SPRINKLE's on_hit_target must set the target's frozen_delay to the roll";

    delete attacker;
    delete slime;
    combat_world().delete_objects();
}


TEST(WalkerCombat, walker_attack_barbarian_magic_resistance)
{
    SequenceRandomCombat zero({0});
    ScopedCombatRandom combat_rng(&zero);

    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* barb = make_guy(FAMILY_BARBARIAN, 1);
    ASSERT_NE(nullptr, attacker) << "attacker created";
    ASSERT_NE(nullptr, barb) << "target created";

    barb->setxy(101, 100);
    barb->stats()->set_armor(0);
    barb->stats()->set_max_hitpoints(400.0f);
    attacker->set_damage(40.0f);

    // packs/core/families/living-16-barbarian.lua: magic_damage_modifier = 0.5.
    const float base = compute_base_damage(40.0f, zero);
    const short expected_plain = damage_to_hit_points(base);
    const short expected_magic = damage_to_hit_points(base * 0.5f);
    ASSERT_GT(expected_plain, expected_magic)
        << "the fixture damage must be large enough to tell the two apart";

    attacker->stats()->set_bit_flags(BIT_MAGICAL, 0);
    barb->stats()->set_hitpoints(400.0f);
    ASSERT_TRUE(attacker->attack(barb)) << "plain hit lands";
    ASSERT_FLOAT_EQ(400.0f - static_cast<float>(expected_plain), barb->stats()->hitpoints())
        << "a non-magical attacker applies unmodified damage";

    attacker->stats()->set_bit_flags(BIT_MAGICAL, 1);
    barb->set_dead(0);
    barb->stats()->set_hitpoints(400.0f);
    ASSERT_TRUE(attacker->attack(barb)) << "magical hit lands";
    ASSERT_FLOAT_EQ(400.0f - static_cast<float>(expected_magic), barb->stats()->hitpoints())
        << "a barbarian halves BIT_MAGICAL damage (magic_damage_modifier 0.5)";

    delete attacker;
    delete barb;
    combat_world().delete_objects();
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
    GameWorld& world = combat_world();
    world.create_new_grid();
    world.delete_objects();

    // --- queued command wins over the act_type switch ---------------------
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "walker created";
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    w->set_act_type(ACT_RANDOM);
    w->stats()->add_command(COMMAND_WALK, 3, 1, 0);
    ASSERT_TRUE(w->act()) << "a queued command is consumed before the act_type switch";

    // --- an act_type nobody handles is refused ----------------------------
    w->stats()->clear_command();
    w->set_ani_type(ANI_WALK);
    w->set_cycle(0);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    w->set_act_type(127);
    ASSERT_FALSE(w->act()) << "act() returns 0 for an act_type it does not know";

    // --- ACT_GUARD with nothing to guard against --------------------------
    w->set_ani_type(ANI_WALK);
    w->set_cycle(0);
    w->set_act_type(ACT_GUARD);
    w->set_foe(nullptr);
    w->stats()->clear_command();
    ASSERT_FALSE(w->act())
        << "act() reports 0 on the ACT_GUARD arm (act_guard's 1 is dropped by the switch break)";
    ASSERT_EQ(nullptr, w->foe()) << "an empty world offers no foe for a guard to acquire";
    ASSERT_FALSE(w->stats()->has_commands()) << "a guard with no foe queues nothing";

    // --- ACT_GUARD that can see a hostile ---------------------------------
    walker* guard_foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, guard_foe) << "guard foe created";
    guard_foe->set_team_num(2);
    guard_foe->setxy(static_cast<short>(w->xpos() + 8), static_cast<short>(w->ypos() + 8));
    w->set_ani_type(ANI_WALK);
    w->set_cycle(0);
    w->set_act_type(ACT_GUARD);
    w->set_foe(nullptr);
    w->stats()->clear_command();
    {
        SequenceRandomCombat guard_rng({0});
        ScopedSimRandom sim(&guard_rng);
        ASSERT_FALSE(w->act()) << "act() still reports 0 on the ACT_GUARD arm";
    }
    ASSERT_EQ(guard_foe, w->foe()) << "act_guard latches the nearest hostile";
    ASSERT_TRUE(w->stats()->has_commands())
        << "act_guard queues COMMAND_FIRE toward the foe it just acquired";

    delete w;
    world.delete_objects();

    // --- act_generate cadence ---------------------------------------------
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_NE(nullptr, l) << "loader exists";

    auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen) << "generator created";
    walker* genp = gen.get();
    genp->setxy(120, 120);
    genp->set_act_type(ACT_GENERATE);
    genp->set_ani_type(ANI_WALK);
    genp->set_cycle(0);
    genp->stats()->set_level(5);
    genp->stats()->set_max_hitpoints(20.0f);
    genp->stats()->set_hitpoints(10.0f);
    {
        // act_generate fires when level_draw * rate beats threshold_draw * 100:
        // 100 % (5*3) = 10 against a threshold draw of 0.
        SequenceRandomCombat gen_rng({100, 0, 1, 1});
        ScopedSimRandom sim(&gen_rng);
        ASSERT_FALSE(genp->act())
            << "act() reports 0 on the ACT_GENERATE arm (act_generate's 1 is dropped by the break)";
    }
    ASSERT_FLOAT_EQ(11.0f, genp->stats()->hitpoints())
        << "a generator that fires on its cadence regenerates exactly one hitpoint";

    // --- act_fire: end of range, then the collision arm -------------------
    walker* proj = world.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, proj) << "weapon created";
    proj->set_team_num(0);
    proj->setxy(120, 120);
    proj->set_act_type(ACT_FIRE);
    proj->set_ani_type(ANI_WALK);
    proj->set_cycle(0);
    proj->set_lineofsight(0);
    proj->stats()->set_bit_flags(BIT_NO_COLLIDE, 0);
    proj->stats()->set_bit_flags(BIT_IMMORTAL, 0);
    ASSERT_TRUE(proj->act()) << "weap::act reports 1 on the ACT_FIRE arm";
    ASSERT_EQ(1, (int)proj->dead()) << "a projectile that has run out of range dies";

    walker* target = make_guy(FAMILY_ORC, 2);
    ASSERT_NE(nullptr, target) << "act_fire target created";
    target->setxy(120, 120);
    target->stats()->set_armor(0);
    target->stats()->set_max_hitpoints(400.0f);
    target->stats()->set_hitpoints(400.0f);

    SequenceRandomCombat zero({0});
    ScopedCombatRandom combat_rng(&zero);
    const short expected_hit = damage_to_hit_points(compute_base_damage(10.0f, zero));
    ASSERT_GT((int)expected_hit, 0) << "the fixture must deal visible damage";

    // Mortal projectile: attacks collide_ob, then dies.
    // weap::act() clears collide_ob on entry, so the collision has to come
    // from the walk() probe: the projectile and the target overlap, and
    // BIT_NO_COLLIDE lets the probe pass through while still latching it.
    proj->set_dead(0);
    proj->setxy(120, 120);
    proj->set_curdir(FACE_RIGHT);
    proj->set_lastx(1.0f);
    proj->set_lasty(0.0f);
    proj->set_lineofsight(2);
    proj->set_damage(10.0f);
    proj->stats()->set_hitpoints(100.0f);
    proj->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    proj->stats()->set_bit_flags(BIT_IMMORTAL, 0);
    ASSERT_TRUE(proj->act()) << "the collision arm still reports 1";
    ASSERT_EQ(1, (int)proj->dead()) << "a mortal projectile dies on the hit it lands";
    ASSERT_FLOAT_EQ(400.0f - static_cast<float>(expected_hit), target->stats()->hitpoints())
        << "act_fire attacks its collide_ob";

    // Immortal projectile: hits again and survives.
    proj->set_dead(0);
    proj->setxy(120, 120);
    proj->set_curdir(FACE_RIGHT);
    proj->set_lastx(1.0f);
    proj->set_lasty(0.0f);
    proj->set_lineofsight(2);
    proj->set_damage(10.0f);
    proj->stats()->set_hitpoints(100.0f);
    proj->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    proj->stats()->set_bit_flags(BIT_IMMORTAL, 1);
    ASSERT_TRUE(proj->act()) << "the collision arm still reports 1";
    ASSERT_EQ(0, (int)proj->dead()) << "BIT_IMMORTAL survives the hit it lands";
    ASSERT_FLOAT_EQ(400.0f - 2.0f * static_cast<float>(expected_hit),
                    target->stats()->hitpoints())
        << "the immortal projectile lands a second identical hit";

    delete target;
    gen.reset();
    world.delete_objects();

    // --- base (non-living) ACT_RANDOM -------------------------------------
    walker* base_rand = world.add_ob(Order::Generator, FAMILY_TENT);
    walker* base_foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, base_rand) << "base ACT_RANDOM walker created";
    ASSERT_NE(nullptr, base_foe) << "base ACT_RANDOM foe created";
    base_rand->set_team_num(1);
    base_rand->setxy(132, 132);
    base_rand->set_lineofsight(40);
    base_rand->set_act_type(ACT_RANDOM);
    base_rand->set_ani_type(ANI_WALK);
    base_rand->set_cycle(0);
    base_rand->set_foe(nullptr);
    base_rand->stats()->clear_command();
    base_foe->set_team_num(3);
    base_foe->setxy(136, 132);

    {
        // act(): rng(4)==0 then rng(20)!=0 -> act_random().
        // act_random(): rng(70)==0 -> re-acquire the foe; a Generator's
        // fire_check always passes, so it fires and queues COMMAND_FIRE.
        SequenceRandomCombat base_rng1({0, 1, 0, 5, 0});
        ScopedSimRandom sim(&base_rng1);
        ASSERT_FALSE(base_rand->act())
            << "act() reports 0 on the act_random arm: act_random's 1 is dropped by the break";
    }
    ASSERT_EQ(base_foe, base_rand->foe()) << "act_random acquires the far foe";
    ASSERT_TRUE(base_rand->stats()->has_commands())
        << "act_random queues COMMAND_FIRE at a foe in range";

    base_rand->set_foe(nullptr);
    base_rand->stats()->clear_command();
    base_rand->set_ani_type(ANI_WALK);
    base_rand->set_cycle(0);
    {
        // act(): rng(4)!=0 -> the 3-of-4 search branch.
        SequenceRandomCombat base_rng2({1, 1, 1});
        ScopedSimRandom sim(&base_rng2);
        ASSERT_TRUE(base_rand->act()) << "the 3-of-4 search branch reports 1";
    }
    ASSERT_EQ(base_foe, base_rand->foe())
        << "the search branch acquires a foe when it has none";
    ASSERT_TRUE(base_rand->stats()->has_commands())
        << "the search branch queues COMMAND_SEARCH";

    world.delete_objects();

    // --- living ACT_RANDOM -------------------------------------------------
    walker* randomer = make_guy(FAMILY_ORC, 1);
    walker* random_foe = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, randomer) << "act_random walker created";
    ASSERT_NE(nullptr, random_foe) << "act_random foe created";
    randomer->setxy(80, 80);
    randomer->set_lineofsight(40);
    randomer->set_act_type(ACT_RANDOM);
    randomer->set_ani_type(ANI_WALK);
    randomer->set_cycle(0);
    randomer->stats()->clear_command();
    // An ODD facing on both channels: no pre-switch turn, and the search arm's
    // (enddir/2)*2 snap is observable.
    randomer->set_curdir(FACE_UP_RIGHT);
    randomer->set_enddir(FACE_UP_RIGHT);
    random_foe->set_team_num(2);
    random_foe->setxy(86, 80);
    randomer->set_foe(random_foe);

    {
        // living::act ACT_RANDOM, 4-of-5 arm: rng(5)!=0 twice.
        SequenceRandomCombat ones({1});
        ScopedSimRandom sim(&ones);
        ASSERT_TRUE(randomer->act()) << "the 4-of-5 search arm reports 1";
    }
    ASSERT_EQ(random_foe, randomer->foe()) << "the latched foe is kept";
    ASSERT_EQ(FACE_UP, (int)randomer->curdir())
        << "the search arm snaps facing down to the even (cardinal) direction";
    ASSERT_TRUE(randomer->stats()->has_commands())
        << "living::act queues COMMAND_SEARCH toward its foe";

    randomer->set_foe(nullptr);
    randomer->stats()->clear_command();
    randomer->set_ani_type(ANI_WALK);
    randomer->set_cycle(0);
    randomer->set_curdir(FACE_UP);
    randomer->set_enddir(FACE_UP);
    world.delete_objects();  // nothing hostile is left to acquire
    {
        SequenceRandomCombat ones({1});
        ScopedSimRandom sim(&ones);
        ASSERT_TRUE(randomer->act()) << "the no-foe fallback still reports 1";
    }
    ASSERT_EQ(nullptr, randomer->foe()) << "an empty world yields no foe";
    ASSERT_TRUE(randomer->stats()->has_commands())
        << "with no foe the arm falls back to COMMAND_RANDOM_WALK";

    delete randomer;
    world.delete_objects();
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
    GameWorld& world = combat_world();
    world.create_new_grid();
    world.delete_objects();

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "walker created";
    w->setxy(100, 100);

    // spaces_clear() probes the eight (+-sizex, +-sizey) offsets around us and
    // counts the passable ones; on open grass every one of them is clear.
    ASSERT_EQ(8, (int)w->spaces_clear())
        << "all eight neighbouring offsets are passable on an empty grid";

    // At the map's top-left corner every offset with i == -1 or j == -1 lands
    // on a negative coordinate, which query_grid_passable rejects: only
    // (+1,0), (0,+1) and (+1,+1) survive.
    w->setxy(0, 0);
    ASSERT_EQ(3, (int)w->spaces_clear())
        << "offsets off the top/left edge of the map are not clear";

    delete w;
    world.delete_objects();
}


// ---------------------------------------------------------------------------
// fire_check (lines 4026-4291) - complex direction logic
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_fire_check_all_dirs)
{
    static const short kDirs[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

    GameWorld& world = combat_world();
    world.create_new_grid();
    world.delete_objects();

    // Pin the SIM stream before anything is built. The probe below and the
    // fire_check under test each draw the knife's waver inside
    // walker::set_weapon_heading (src/gameplay/walker.cpp:709) from
    // GameWorld::rng_. Left unpinned those are two ADJACENT LCG outputs, the
    // asserted shot leaves the probe's line whenever
    // waver_ray - waver_probe <= -2 (3 of the 16 possible pairs), and the foe
    // parked on the probe's ray is missed: 6 of 40 repeats and 7 of 40 shuffle
    // seeds were red before this pin.
    // The knife's stepsize is 7.07, so the waver base is trunc(7.07/2) = 3 and
    // a scripted next(4) == 1 yields waver = 1 - 3/2 = 0: the shot flies dead
    // straight, and this test claims the geometry of a straight shot.
    SequenceRandomCombat straight({1});
    ScopedSimRandom sim(&straight);

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "walker created";
    w->setxy(96, 96);
    w->set_foe(nullptr);

    // With nothing to shoot at, every direction is denied at the NoFoe gate.
    for (const auto& d : kDirs)
    {
        walker::FireCheckDenial why = walker::FireCheckDenial::None;
        EXPECT_FALSE(w->fire_check(d[0], d[1], &why))
            << "no foe, direction " << d[0] << "," << d[1];
        EXPECT_EQ(walker::FireCheckDenial::NoFoe, why)
            << "the denial must be NoFoe for direction " << d[0] << "," << d[1];
    }

    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(1);
    w->set_team_num(0);
    w->set_lastx(1);
    w->set_lasty(0);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    w->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    w->stats()->set_magicpoints(9999.0f);
    w->stats()->set_weapon_cost(0.0f);
    w->set_foe(foe);

    // Park the foe on the shot ray, where an actual probe weapon would fly.
    walker* probe = w->create_weapon();
    ASSERT_NE(nullptr, probe) << "probe weapon created";
    w->set_weapon_heading(probe);
    const short start_x = probe->xpos();
    const short start_y = probe->ypos();
    const short step_x = static_cast<short>(probe->lastx());
    const short step_y = static_cast<short>(probe->lasty());
    world.remove_ob(probe);
    EXPECT_EQ(7, step_x)
        << "the knife's stepsize (base 5 scaled by 362/256 = 7.07) is the"
           " per-tick x step on a cardinal facing, truncated to short here";
    ASSERT_EQ(0, step_y)
        << "the scripted next(4) == 1 makes the knife's waver 1 - 3/2 = 0; a"
           " non-zero step_y means the waver formula or the draw order moved";
    // Full-size foe: obmap's collide() shrinks both boxes by 2, so a 1x1
    // target (the trick the intermediate-step sibling uses to make the foe
    // NOT block) can never be hit by the ray.
    foe->setxy(static_cast<short>(start_x + 3 * step_x),
               static_cast<short>(start_y + 3 * step_y));

    walker::FireCheckDenial hit_why = walker::FireCheckDenial::NoFoe;
    EXPECT_TRUE(w->fire_check(1, 0, &hit_why))
        << "a foe straight ahead, in reach, with mana and a clear path is a legal"
           " shot; denial=" << static_cast<int>(hit_why);

    // Every other vector is in reach with the same mana, so the ONLY thing
    // that can deny it is the facing gate.
    for (const auto& d : kDirs)
    {
        if (d[0] == 1 && d[1] == 0)
            continue;
        walker::FireCheckDenial why = walker::FireCheckDenial::None;
        EXPECT_FALSE(w->fire_check(d[0], d[1], &why))
            << "we face right, so direction " << d[0] << "," << d[1] << " is denied";
        EXPECT_EQ(walker::FireCheckDenial::Facing, why)
            << "the denial must be Facing for direction " << d[0] << "," << d[1];
    }

    delete w;
    world.delete_objects();
}


TEST(WalkerCombat, walker_fire_check_blocks_on_intermediate_step)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    // The same pin as walker_fire_check_all_dirs: the probe's
    // set_weapon_heading and the fire_check under test each draw the arrow's
    // waver from GameWorld::rng_, and unpinned they are adjacent LCG outputs.
    // The arrow's stepsize is 11.3125, so the waver base is
    // trunc(11.3125/2) = 5 and a scripted next(6) == 2 yields
    // waver = 2 - 5/2 = 0: a straight shot whose ray is the probe's ray.
    SequenceRandomCombat straight({2});
    ScopedSimRandom sim(&straight);

    walker* shooter = make_guy(FAMILY_ARCHER, 0);
    walker* foe = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(shooter != nullptr && foe != nullptr) << "fixtures created";

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

    walker* probe = shooter->create_weapon();
    ASSERT_TRUE(probe != nullptr) << "probe weapon created";
    shooter->set_weapon_heading(probe);

    const short start_x = probe->xpos();
    const short start_y = probe->ypos();
    const short step_x = static_cast<short>(probe->lastx());
    const short step_y = static_cast<short>(probe->lasty());
    og::runtime::current_session->myscreen_->world().remove_ob(probe);

    EXPECT_EQ(11, step_x)
        << "the arrow's stepsize (11.3125) is the per-tick x step on a cardinal"
           " facing, truncated to short here";
    ASSERT_EQ(0, step_y)
        << "the scripted next(6) == 2 makes the arrow's waver 2 - 5/2 = 0; a"
           " non-zero step_y means the waver formula or the draw order moved";

    // Wall the tile that holds the ray's third position, and park the foe
    // there. fire_check's loop moves the weapon CUMULATIVELY
    // (setxy(pos + i*step)), so the ray visits start + 0, 1, 3, 6 ... steps;
    // the 7-px-wide arrow box at start + 1*step (x 124..131 here) already
    // reaches into the 16-px tile that contains start + 2*step, the tile
    // walled below. The shot dies on terrain before it can reach the foe.
    set_world_tile(static_cast<short>(start_x + 2 * step_x),
                   static_cast<short>(start_y + 2 * step_y),
                   PIX_H_WALL1);
    foe->setxy(static_cast<short>(start_x + 3 * step_x),
               static_cast<short>(start_y + 3 * step_y));
    foe->set_sizex(1);
    foe->set_sizey(1);

    walker::FireCheckDenial why = walker::FireCheckDenial::None;
    EXPECT_FALSE(shooter->fire_check(1, 0, &why))
        << "fire_check must fail when a tile on the shot ray is blocked";
    EXPECT_EQ(walker::FireCheckDenial::WallBlocked, why)
        << "a wall on the ray's second position denies with WallBlocked;"
           " RayMiss would mean the ray walked past the wall";

    delete shooter;
    delete foe;
    og::runtime::current_session->myscreen_->world().delete_objects();
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
// set_difficulty (lines 4611-4635)
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_set_difficulty_all_families)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_NE(nullptr, l) << "loader exists";

    GameWorld& world = combat_world();
    const auto saved_difficulty = world.difficulty;
    // Family set_difficulty hooks may draw; pin the stream so the 100% and
    // 200% walkers differ ONLY by the difficulty percentage.
    SequenceRandomCombat zero({0});
    ScopedSimRandom sim(&zero);

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    // living::set_difficulty runs the family formula first and THEN applies
    // the difficulty percentage, so the only honest oracle is two identically
    // built walkers scaled at 100% and at 200%.
    const auto scaled_living = [&](short family, unsigned char team, short percent) {
        world.difficulty = percent;
        auto w = l->create_walker_owned(Order::Living, family);
        if (w != nullptr) {
            w->set_team_num(team);
            w->set_difficulty(5);
        }
        return w;
    };

    for (int i = 0; i < 14; i++) {
        SCOPED_TRACE(::testing::Message() << "family=" << (int)families[i]);

        auto easy = scaled_living(families[i], 1, 100);
        auto hard = scaled_living(families[i], 1, 200);
        ASSERT_NE(nullptr, easy) << "enemy walker created";
        ASSERT_NE(nullptr, hard) << "enemy walker created";
        ASSERT_GT(easy->stats()->max_hitpoints(), 0.0f) << "the fixture must have hp to scale";
        ASSERT_FLOAT_EQ(easy->stats()->max_hitpoints() * 2.0f, hard->stats()->max_hitpoints())
            << "difficulty 200 doubles an enemy's max hitpoints";
        ASSERT_FLOAT_EQ(easy->stats()->max_magicpoints() * 2.0f, hard->stats()->max_magicpoints())
            << "difficulty 200 doubles an enemy's max magicpoints";
        ASSERT_FLOAT_EQ(easy->damage() * 2.0f, hard->damage())
            << "difficulty 200 doubles an enemy's damage";
        ASSERT_FLOAT_EQ(hard->stats()->max_hitpoints(), hard->stats()->hitpoints())
            << "set_difficulty leaves a living at full health";
        ASSERT_FLOAT_EQ(hard->stats()->max_magicpoints(), hard->stats()->magicpoints())
            << "set_difficulty leaves a living at full magic";
    }

    // A12a: a PLACED team-0 NPC (no company record) scales exactly like a foe.
    {
        auto easy = scaled_living(FAMILY_SOLDIER, 0, 100);
        auto hard = scaled_living(FAMILY_SOLDIER, 0, 200);
        ASSERT_NE(nullptr, easy) << "team-0 NPC created";
        ASSERT_NE(nullptr, hard) << "team-0 NPC created";
        ASSERT_EQ(nullptr, hard->myguy) << "a placed NPC carries no company record";
        ASSERT_FLOAT_EQ(easy->stats()->max_hitpoints() * 2.0f, hard->stats()->max_hitpoints())
            << "an allied NPC without a company record scales with difficulty too";
        ASSERT_FLOAT_EQ(easy->damage() * 2.0f, hard->damage())
            << "an allied NPC without a company record scales its damage too";
    }

    // ...but a walker actually carrying a player guy is exempt at any setting.
    {
        world.difficulty = 100;
        walker* easy = make_guy(FAMILY_SOLDIER, 0);
        ASSERT_NE(nullptr, easy) << "player-crew walker created";
        ASSERT_NE(nullptr, easy->myguy) << "player crew carries a company record";
        easy->set_difficulty(5);
        const float easy_hp = easy->stats()->max_hitpoints();
        const float easy_dmg = easy->damage();

        world.difficulty = 200;
        walker* hard = make_guy(FAMILY_SOLDIER, 0);
        ASSERT_NE(nullptr, hard) << "player-crew walker created";
        hard->set_difficulty(5);
        ASSERT_FLOAT_EQ(easy_hp, hard->stats()->max_hitpoints())
            << "a player character's max hitpoints must never be scaled by difficulty";
        ASSERT_FLOAT_EQ(easy_dmg, hard->damage())
            << "a player character's damage must never be scaled by difficulty";
        delete easy;
        delete hard;
    }

    // Generators take walker::set_difficulty: hp == max_hp == 100*level*pct/100.
    world.difficulty = 200;
    auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen) << "generator created";
    gen->set_difficulty(7);
    ASSERT_FLOAT_EQ(1400.0f, gen->stats()->hitpoints())
        << "generator hitpoints = 100 * whatlevel * difficulty / 100";
    ASSERT_FLOAT_EQ(1400.0f, gen->stats()->max_hitpoints())
        << "a generator's fighting hp is also its denominator";

    world.difficulty = saved_difficulty;
    combat_world().delete_objects();
}


// get_current_angle's full facing->radians table (including the default arm)
// is pinned by WalkerMovement.walker_get_current_angle_all_direction_cases in
// tests/integration/test_walker_movement.cpp; the copy that used to sit here
// asserted the same rows and nothing more.

// ---------------------------------------------------------------------------
// animate smoke test
// ---------------------------------------------------------------------------

TEST(WalkerCombat, walker_animate_smoke)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_ani_type(ANI_WALK);
    w->set_cycle(0);
    ASSERT_TRUE(w->animate()) << "a walk frame advances and reports success";

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

    // Slime split branch: the ANI_SLIME_SPLIT completion hook shrinks us to a
    // small slime and adds exactly ONE more to oblist (we were released by
    // make_guy, so we are not in oblist and cannot be double-counted).
    w->transform_to(Order::Living, FAMILY_SLIME);
    w->set_ani_type(ANI_SLIME_SPLIT);
    w->set_cycle(0);
    const int small_slime_before = count_family_in_oblist(FAMILY_SMALL_SLIME);
    for (int i = 0; i < 32 && w->ani_type() != ANI_WALK; ++i) {
        (void)w->animate();
    }
    ASSERT_EQ(small_slime_before + 1, count_family_in_oblist(FAMILY_SMALL_SLIME))
        << "a completed slime split spawns exactly one new small slime";
    ASSERT_EQ((int)FAMILY_SMALL_SLIME, (int)w->family())
        << "the splitting slime itself shrinks to a small slime";
    ASSERT_EQ(ANI_WALK, (int)w->ani_type())
        << "the split hook returns the slime to its walk animation";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST(WalkerCombat, walker_act_random_generator_paths)
{
    GameWorld& world = combat_world();
    world.create_new_grid();
    world.delete_objects();

    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_NE(nullptr, l) << "loader exists";

    auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen) << "generator created";
    walker* genp = gen.get();
    // The foe has to live in oblist: find_far_foe scans that list, and the
    // whole point of both branches below is that they ACQUIRE it.
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";

    genp->set_team_num(1);
    foe->set_team_num(2);
    genp->setxy(128, 128);
    foe->setxy(132, 128);
    genp->set_lineofsight(40);
    genp->set_act_type(ACT_RANDOM);
    genp->set_ani_type(ANI_WALK);
    genp->set_cycle(0);
    genp->set_foe(nullptr);
    genp->stats()->clear_command();

    {
        // act(): rng(4)==0, rng(20)!=0 -> act_random().
        // act_random(): rng(70)==0 -> find_far_foe, in range, and a
        // Generator's fire_check always passes -> COMMAND_FIRE.
        SequenceRandomCombat rng1({0, 1, 0, 0, 0});
        ScopedSimRandom sim(&rng1);
        ASSERT_FALSE(genp->act())
            << "act() reports 0 on the act_random arm: act_random's 1 is dropped by the break";
    }
    ASSERT_EQ(foe, genp->foe()) << "act_random acquires the foe through find_far_foe";
    ASSERT_TRUE(genp->stats()->has_commands()) << "act_random queues COMMAND_FIRE";

    genp->set_foe(nullptr);
    genp->stats()->clear_command();
    genp->set_ani_type(ANI_WALK);
    genp->set_cycle(0);
    {
        // act(): rng(4)!=0 -> the 3-of-4 search branch.
        SequenceRandomCombat rng2({1, 0, 0, 0});
        ScopedSimRandom sim(&rng2);
        ASSERT_TRUE(genp->act()) << "the 3-of-4 search branch reports 1";
    }
    ASSERT_EQ(foe, genp->foe()) << "the search branch acquires a foe when it has none";
    ASSERT_TRUE(genp->stats()->has_commands()) << "the search branch queues COMMAND_SEARCH";

    gen.reset();
    world.delete_objects();
}


TEST(WalkerCombat, effect_helpers_and_recoil_branches)
{
    walker* attacker = make_guy(FAMILY_SOLDIER, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker != nullptr && target != nullptr) << "combat walkers created";

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
    SequenceRandomCombat zero({0});
    ScopedCombatRandom combat_rng(&zero);

    GameWorld& world = combat_world();
    walker* owner = make_guy(FAMILY_SOLDIER, 0);
    walker* living_target = make_guy(FAMILY_ORC, 1);
    ASSERT_NE(nullptr, owner) << "owner created";
    ASSERT_NE(nullptr, living_target) << "target created";
    ASSERT_NE(nullptr, owner->myguy) << "owner carries a company record";

    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, weapon) << "weapon created";
    owner->set_user(0);
    weapon->set_owner(owner);
    weapon->set_team_num(owner->team_num());
    weapon->set_damage(1.0f);
    weapon->stats()->set_hitpoints(50);

    // Armor reduction is clamped at zero damage, and damage_to_hit_points()
    // floors what is left: this hit lands for an exact, computable amount.
    living_target->stats()->set_armor(5000);
    living_target->stats()->set_max_hitpoints(100.0f);
    living_target->stats()->set_hitpoints(100.0f);
    const float base = compute_base_damage(1.0f, zero);
    const short applied = damage_to_hit_points(base - compute_damage_reduction(base, 5000.0f));
    ASSERT_TRUE(weapon->attack(living_target)) << "the weapon hit lands";
    ASSERT_FLOAT_EQ(100.0f - static_cast<float>(applied), living_target->stats()->hitpoints())
        << "a heavily armored target loses exactly the post-reduction damage";

    // A non-living target is not a shot: attack() gives the SHOT back to the
    // owner (attacker == owner() for a weapon), decrementing both counters.
    walker* nonliving = world.add_ob(Order::FX, FAMILY_FLASH);
    ASSERT_NE(nullptr, nonliving) << "nonliving target created";
    nonliving->set_team_num(1);
    owner->myguy->total_shots = 5;
    owner->myguy->scen_shots = 5;
    ASSERT_TRUE(weapon->attack(nonliving)) << "a hostile non-living target is still attackable";
    ASSERT_EQ(4, owner->myguy->total_shots)
        << "hitting a non-living target refunds one total_shot to the owner";
    ASSERT_EQ(4, (int)owner->myguy->scen_shots)
        << "hitting a non-living target refunds one scen_shot to the owner";

    delete owner;
    delete living_target;
    world.delete_objects();
}


TEST(WalkerCombat, batch5_heal_and_hit_effect_variants)
{
    walker* healer = make_guy(FAMILY_CLERIC, 0);
    walker* target = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(healer != nullptr && target != nullptr) << "healer and target created";

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
    ASSERT_NE(nullptr, attacker) << "attacker created";
    ASSERT_NE(nullptr, victim) << "victim created";
    ASSERT_NE(nullptr, attacker->myguy) << "attacker carries a company record";
    ASSERT_NE(nullptr, victim->myguy) << "victim carries a company record";

    victim->stats()->set_max_hitpoints(100.0f);
    victim->stats()->set_hitpoints(100.0f);
    victim->set_regen_delay(0);
    const float hp_before = victim->stats()->hitpoints();
    const float taken_before = victim->myguy->scen_damage_taken;
    const float dealt_before = attacker->myguy->scen_damage;

    attacker->do_combat_damage(attacker, victim, 7);

    ASSERT_FLOAT_EQ(hp_before, victim->last_hitpoints())
        << "last_hitpoints records the PRE-hit hitpoints";
    ASSERT_FLOAT_EQ(hp_before - 7.0f, victim->stats()->hitpoints())
        << "the full tempdamage comes off the target's hitpoints";
    ASSERT_EQ(50, (int)victim->regen_delay())
        << "positive damage restarts the 50-tick regeneration delay";
    ASSERT_FLOAT_EQ(taken_before + 7.0f, victim->myguy->scen_damage_taken)
        << "the victim banks the damage it took this scenario";
    ASSERT_FLOAT_EQ(dealt_before + 7.0f, attacker->myguy->scen_damage)
        << "the attacker banks the damage it dealt this scenario";

    delete attacker;
    delete victim;
    combat_world().delete_objects();
}


TEST(WalkerCombat, batch6_attack_branches_enemy_and_weapon_paths)
{
    const short saved_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;

    // Enemy kill path: magical modifier, kill awards, notifications, and remaining-foe branch.
    walker* attacker = make_guy(FAMILY_MAGE, 0);
    walker* enemy = make_guy(FAMILY_ORC, 1);
    ASSERT_TRUE(attacker != nullptr && enemy != nullptr) << "attacker/enemy created";
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
    SequenceRandomCombat zero({0});
    ScopedCombatRandom combat_rng(&zero);

    GameWorld& world = combat_world();
    const short saved_allied_mode = world.allied_mode;
    const short saved_my_team = world.my_team;
    world.allied_mode = 1;
    world.my_team = 0;
    ASSERT_TRUE(current_game && current_game->sim_events) << "sim event log available";

    // Team 1 stays hostile to team 0 regardless of the player seating mode.
    walker* attacker = make_guy(FAMILY_SOLDIER, 1);
    ASSERT_NE(nullptr, attacker) << "attacker created";
    attacker->clear_myguy();
    attacker->set_damage(500.0f);

    walker* t_dispelled = make_guy(FAMILY_ORC, 0);
    walker* t_named = make_guy(FAMILY_ORC, 0);
    walker* t_myguy_name = make_guy(FAMILY_ORC, 0);
    ASSERT_NE(nullptr, t_dispelled) << "targets created";
    ASSERT_NE(nullptr, t_named) << "targets created";
    ASSERT_NE(nullptr, t_myguy_name) << "targets created";

    // A walker we own is friendly THROUGH THE OWNER CHAIN (is_friendly walks
    // to the chain head), so we cannot strike it down at all -- no damage, no
    // death, no toast. The "Dispelled!" wording lives in the same-team arm
    // that a team-1 killer of a team-0 victim never reaches.
    t_dispelled->stats()->set_armor(0);
    t_dispelled->stats()->set_hitpoints(1);
    t_dispelled->stats()->name = "Summon";
    t_dispelled->set_owner(attacker);
    current_game->sim_events->clear();
    ASSERT_FALSE(attacker->attack(t_dispelled))
        << "a walker on our own owner chain is friendly and cannot be attacked";
    ASSERT_FALSE(t_dispelled->dead()) << "a refused attack must not kill";
    ASSERT_FLOAT_EQ(1.0f, t_dispelled->stats()->hitpoints()) << "a refused attack deals no damage";
    ASSERT_TRUE(first_death_notification().empty()) << "a refused attack announces nothing";

    // A named, un-summoned victim on the PLAYER's team: announced with the
    // plain wording, because the wording is chosen off world.my_team.
    t_named->stats()->set_armor(0);
    t_named->stats()->set_hitpoints(1);
    t_named->set_owner(nullptr);
    t_named->set_lifetime(0);
    t_named->stats()->name = "AllyName";
    current_game->sim_events->clear();
    ASSERT_TRUE(attacker->attack(t_named)) << "a hostile-colored victim can be killed";
    ASSERT_TRUE(t_named->dead()) << "the named victim dies";
    EXPECT_EQ(std::string("AllyName DIED!"), first_death_notification())
        << "a victim on the player's own team keeps the plain death wording";

    // Same shape with an EMPTY stats name: the first arm's toast is gated on
    // stats()->name.size(), so the myguy name is never announced here.
    t_myguy_name->stats()->set_armor(0);
    t_myguy_name->stats()->set_hitpoints(1);
    t_myguy_name->set_owner(nullptr);
    t_myguy_name->set_lifetime(0);
    t_myguy_name->stats()->name.clear();
    ASSERT_NE(nullptr, t_myguy_name->myguy) << "victim carries a company record";
    t_myguy_name->myguy->name = "GuyName";
    current_game->sim_events->clear();
    ASSERT_TRUE(attacker->attack(t_myguy_name)) << "the unnamed victim can be killed";
    ASSERT_TRUE(t_myguy_name->dead()) << "the unnamed victim dies";
    ASSERT_TRUE(first_death_notification().empty())
        << "a victim with no stats name gets no death toast on this arm";

    // High-armor path: reduction is clamped at zero damage, so what lands is
    // exactly damage_to_hit_points(base - reduction) -- not "anything <= 0".
    walker* armored = make_guy(FAMILY_ORC, 2);
    ASSERT_NE(nullptr, armored) << "armored target created";
    armored->stats()->set_armor(100000);
    armored->stats()->set_max_hitpoints(200.0f);
    armored->stats()->set_hitpoints(200.0f);
    const float armored_base = compute_base_damage(500.0f, zero);
    const short armored_applied = damage_to_hit_points(
        armored_base - compute_damage_reduction(armored_base, 100000.0f));
    ASSERT_TRUE(attacker->attack(armored)) << "the armored target is still a legal target";
    ASSERT_FLOAT_EQ(200.0f - static_cast<float>(armored_applied), armored->stats()->hitpoints())
        << "armor reduction leaves exactly the post-reduction damage";

    // do_heal_effects pushes one damage number for the healer and one for the
    // target; healing yourself lands both on you.
    const std::size_t numbers_before = attacker->damage_numbers.size();
    attacker->do_heal_effects(attacker, attacker, 5);
    ASSERT_EQ(numbers_before + 2, attacker->damage_numbers.size())
        << "do_heal_effects posts a number for the healer and one for the target";

    delete attacker;
    delete t_dispelled;
    delete t_named;
    delete t_myguy_name;
    delete armored;
    world.my_team = saved_my_team;
    world.allied_mode = saved_allied_mode;
    world.delete_objects();
}


// --- #189: a named ally's death must not be announced as an enemy death -----
//
// attack() picks the death-message arm with `playerteam != target->team_num()`,
// where `playerteam` is the KILLER's team. When an enemy kills one of the
// player's own named NPCs the teams differ, so the player used to be told
// "ENEMY DEATH: <their own ally> DIED!". Classic hardcoded `playerteam = 0`,
// i.e. it compared the victim against the PLAYER's team, and announced a
// player-team victim with the plain "<name> DIED!" wording.

TEST(WalkerCombat, named_ally_death_is_not_announced_as_enemy_death)
{
    const short saved_allied_mode = og::runtime::current_session->myscreen_->world_.allied_mode;
    const short saved_my_team = og::runtime::current_session->myscreen_->world_.my_team;
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;
    og::runtime::current_session->myscreen_->world_.my_team = 0;

    ASSERT_TRUE(current_game && current_game->sim_events) << "sim event log available";

    // An enemy (team 1) strikes down the player's own named NPC (team 0).
    walker* enemy_attacker = make_guy(FAMILY_SOLDIER, 1);
    walker* named_ally = make_guy(FAMILY_ORC, 0);
    ASSERT_TRUE(enemy_attacker && named_ally) << "attacker/ally created";

    enemy_attacker->clear_myguy();
    enemy_attacker->set_damage(500.0f);
    named_ally->set_owner(nullptr);   // not summoned: the named-NPC arm
    named_ally->set_lifetime(0);
    named_ally->stats()->set_armor(0);
    named_ally->stats()->set_hitpoints(1);
    named_ally->stats()->name = "Commander";
    named_ally->setxy(static_cast<short>(enemy_attacker->xpos() + 8),
                      enemy_attacker->ypos());

    current_game->sim_events->clear();
    ASSERT_TRUE(enemy_attacker->attack(named_ally)) << "the attack should land";
    ASSERT_TRUE(named_ally->dead()) << "the named ally must actually die";

    const std::string ally_message = first_death_notification();
    ASSERT_FALSE(ally_message.empty()) << "the named ally's death must be announced";
    EXPECT_TRUE(ally_message.find("ENEMY DEATH") == std::string::npos)
        << "the player's own ally is not an enemy, but was announced as: "
        << ally_message;
    EXPECT_EQ(std::string("Commander DIED!"), ally_message)
        << "a player-team victim uses the classic plain death wording";

    // Control: a victim on neither the player's team nor the killer's team is a
    // genuine enemy and keeps the classic wording byte-for-byte.
    walker* third_party = make_guy(FAMILY_ORC, 2);
    ASSERT_TRUE(third_party != nullptr) << "third-party victim created";
    if (third_party)
    {
        third_party->set_owner(nullptr);
        third_party->set_lifetime(0);
        third_party->stats()->set_armor(0);
        third_party->stats()->set_hitpoints(1);
        third_party->stats()->name = "DIRK";
        third_party->setxy(static_cast<short>(enemy_attacker->xpos() + 8),
                           enemy_attacker->ypos());

        current_game->sim_events->clear();
        ASSERT_TRUE(enemy_attacker->attack(third_party)) << "the attack should land";
        ASSERT_TRUE(third_party->dead()) << "the enemy must actually die";
        EXPECT_EQ(std::string("ENEMY DEATH: DIRK DIED!"), first_death_notification())
            << "a victim off the player's team keeps the classic enemy wording";
    }

    og::runtime::current_session->myscreen_->world_.my_team = saved_my_team;
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

    // Zero draws on the GAMEPLAY stream make compute_base_damage() exact.
    SequenceRandomCombat zero({0});
    ScopedCombatRandom combat_rng(&zero);

    weapon->set_owner(owner);
    weapon->set_team_num(owner->team_num());
    weapon->set_damage(16.0f);
    owner->set_team_num(0);
    target->set_team_num(1);

    target->stats()->set_armor(0);
    target->stats()->set_hitpoints(200);
    target->stats()->set_max_hitpoints(200);
    target->setxy(static_cast<short>(owner->xpos() + 8), static_cast<short>(owner->ypos()));

    const int exp_before = static_cast<int>(owner->myguy ? owner->myguy->exp : 0);
    const Uint32 score_before = og::runtime::current_session->myscreen_->world_.m_score[owner->team_num()];
    const float hp_before = target->stats()->hitpoints();
    if (current_game && current_game->sim_events)
        current_game->sim_events->clear();

    ASSERT_TRUE(weapon->attack(target)) << "weapon attack should succeed";

    const short dealt = static_cast<short>(hp_before - target->stats()->hitpoints());
    ASSERT_EQ(14, (int)dealt)
        << "compute_base_damage(16, rng -> 0) = 16 - sqrt(16)/2 + 0 = 14, armor"
           " 0 reduces nothing, and damage_to_hit_points(14.0) = 14";
    ASSERT_TRUE(target->stats()->hitpoints() > 0) << "weapon reward regression should use non-lethal hit";

    const std::int32_t level_diff = weapon->stats()->level() - target->stats()->level();
    const short expected_attack_xp = compute_xp_from_attack(level_diff, static_cast<float>(dealt));
    const int exp_after = static_cast<int>(owner->myguy ? owner->myguy->exp : 0);
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

    attacker->set_damage(16.0f);
    attacker->set_team_num(0);
    target->set_team_num(1);
    target->stats()->set_armor(0);
    target->stats()->set_hitpoints(14);
    target->stats()->set_max_hitpoints(14);
    target->setxy(attacker->xpos() + 10, attacker->ypos() + 4);
    og::runtime::current_session->myscreen_->world().rng_.state_ = 0;

    const int exp_before = static_cast<int>(attacker->myguy ? attacker->myguy->exp : 0);
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
    const int exp_after = static_cast<int>(attacker->myguy ? attacker->myguy->exp : 0);
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

    // Busy gate (merged from the former WalkerCombat.walker_init_fire_when_busy,
    // which discarded its result and never reached this branch): once curdir
    // has caught up with enddir, a busy walker is refused BEFORE the
    // fire_frequency charge, so busy() must come back out untouched.
    w->set_busy(3);
    w->set_curdir(FACE_RIGHT);
    w->set_enddir(FACE_RIGHT);
    r = w->init_fire(1, 0);
    ASSERT_TRUE(!r) << "init_fire should fail while busy";
    ASSERT_FLOAT_EQ(3.0f, w->busy())
        << "a refused init_fire must not charge the fire_frequency delay";
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
