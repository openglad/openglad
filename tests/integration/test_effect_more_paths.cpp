#include <openglad/interface/game_context.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/core/constants.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"

#include <memory>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

// effect.cpp free functions the pin-the-constants tests read directly.
void orbit_offset(int drawcycle, float &xd, float &yd);
short hits(short x, short y, short xsize, short ysize,
           short x2, short y2, short xsize2, short ysize2);
std::int32_t compute_explosion_range(std::int32_t level, short skip_exit);

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

// walker::center_on(target) puts `self`'s top-left here.
static short centered_x(const walker* self, const walker* target)
{
    return static_cast<short>(target->xpos() + target->sizex() / 2 - self->sizex() / 2);
}
static short centered_y(const walker* self, const walker* target)
{
    return static_cast<short>(target->ypos() + target->sizey() / 2 - self->sizey() / 2);
}

// The one FX walker of `family` that a script pushed into oblist (the test's
// own effects live in fxlist, so oblist only ever holds the spawned ones).
static walker* find_spawned_fx(GameWorld& world, int family, int* out_count)
{
    walker* found = nullptr;
    int count = 0;
    for (auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w && w->query_order() == Order::FX && w->family() == static_cast<char>(family))
        {
            ++count;
            if (!found)
                found = w;
        }
    }
    if (out_count)
        *out_count = count;
    return found;
}
} // namespace


// The shield orbits its owner, then sweeps: FRIENDLY weapons inside the guard
// radius are absorbed (they die and cost the shield their damage), foes inside
// it are attacked and cost theirs, and a surviving guard spends one tick of
// lifetime. find_foe_weapons_in_range keeps weapons for which the guard's
// is_friendly() holds (game_world.cpp:1370) -- the helper's "foe" name is a
// heritage misnomer -- so a hostile arrow is NOT a candidate.
TEST(EffectMorePaths, effect_magic_shield_absorbs_friendly_weapon_and_strikes_foe)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_CLERIC, 1);
    ASSERT_NE(nullptr, owner) << "owner created";

    walker* shield = world.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, shield) << "shield created";

    shield->set_owner(owner.get());
    shield->set_team_num(owner->team_num());
    shield->stats()->set_hitpoints(50.0f);
    shield->set_lifetime(5);
    // damage 1 makes the strike exact: compute_base_damage is
    // 1 - sqrt(1)/2 + rand(floor(sqrt(1))) == 0.5 for every RNG, and
    // damage_to_hit_points(0.5) == 1 against an armor-0 target.
    shield->set_damage(1.0f);
    shield->setxy(100, 100);

    // effect::act() bumps drawcycle 0 -> 1 before the hook runs, so the shield
    // lands on orbit step 1 = (-9, -22) off the owner's centre.
    const short land_x = static_cast<short>(centered_x(shield, owner.get()) - 9);
    const short land_y = static_cast<short>(centered_y(shield, owner.get()) - 22);

    auto friendly_owned =
        og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, friendly_owned) << "friendly weapon created";
    walker* friendly = friendly_owned.get();
    friendly->set_team_num(owner->team_num());
    friendly->set_damage(7.0f);
    friendly->setxy(land_x, land_y);
    world.oblist.push_back(std::move(friendly_owned));

    auto hostile_owned =
        og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, hostile_owned) << "hostile weapon created";
    walker* hostile = hostile_owned.get();
    hostile->set_team_num(2);
    hostile->set_damage(100.0f);
    hostile->setxy(land_x, land_y);
    world.oblist.push_back(std::move(hostile_owned));

    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(2);
    foe->set_damage(5.0f);
    foe->stats()->set_armor(0.0f);
    foe->setxy(land_x, land_y);
    const float foe_hp_before = foe->stats()->hitpoints();

    ASSERT_TRUE(shield->act()) << "magic_shield_on_act keeps the effect alive";

    EXPECT_EQ(land_x, shield->xpos())
        << "the shield is re-centred on its owner and offset by orbit step 1";
    EXPECT_EQ(land_y, shield->ypos())
        << "the shield is re-centred on its owner and offset by orbit step 1";
    EXPECT_EQ(1, friendly->dead())
        << "a weapon on the guard's own owner chain is absorbed and killed";
    EXPECT_EQ(0, hostile->dead())
        << "the weapon sweep keeps only is_friendly() weapons, never hostile shots";
    EXPECT_FLOAT_EQ(50.0f - 7.0f - 5.0f, shield->stats()->hitpoints())
        << "each absorbed weapon and each struck foe costs the guard its damage";
    EXPECT_FLOAT_EQ(foe_hp_before - 1.0f, foe->stats()->hitpoints())
        << "a foe inside the body radius is attacked by the guard";
    EXPECT_EQ(4, shield->lifetime())
        << "a guard that survives the sweep spends one tick of lifetime";
    EXPECT_EQ(0, shield->dead())
        << "a guard with hitpoints left is not killed by its own sweep";

    world.delete_objects();
}


// The boomerang runs the same guard tail on a drawcycle-scaled orbit: the
// weapon sweep uses sizex*2, the foe sweep sizex, and the arc radius is
// ORBIT[drawcycle] * (drawcycle + 4) / 48 from the owner's centre.
TEST(EffectMorePaths, effect_boomerang_orbit_absorbs_friendly_weapon_and_strikes_foe)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    SeededRandom seeded(123u);
    GameContext c;
    c.rng = &seeded;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_SOLDIER, 1);
    ASSERT_NE(nullptr, owner) << "owner created";

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, fx) << "boomerang created";

    fx->set_owner(owner.get());
    fx->set_team_num(owner->team_num());
    fx->stats()->set_hitpoints(50.0f);
    fx->set_lifetime(5);
    fx->set_drawcycle(12);  // effect::act() makes it 13 before the hook runs
    fx->set_damage(1.0f);   // exact 1 hit point against an armor-0 target
    fx->setxy(100, 100);

    // orbit step 13 is (22, -9); the blade scales it by (13 + 4) / 48.
    const short land_x = static_cast<short>(
        static_cast<float>(centered_x(fx, owner.get())) + 22.0f * 17.0f / 48.0f);
    const short land_y = static_cast<short>(
        static_cast<float>(centered_y(fx, owner.get())) + -9.0f * 17.0f / 48.0f);

    auto friendly_owned =
        og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, friendly_owned) << "friendly weapon created";
    walker* friendly = friendly_owned.get();
    friendly->set_team_num(owner->team_num());
    friendly->set_damage(3.0f);
    friendly->setxy(land_x, land_y);
    world.oblist.push_back(std::move(friendly_owned));

    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(2);
    foe->set_damage(4.0f);
    foe->stats()->set_armor(0.0f);
    foe->setxy(land_x, land_y);
    const float foe_hp_before = foe->stats()->hitpoints();

    ASSERT_TRUE(fx->act()) << "boomerang_on_act keeps the blade alive";

    EXPECT_EQ(13, static_cast<int>(fx->drawcycle()))
        << "effect::act advances the blade one orbit step per tick";
    EXPECT_EQ(land_x, fx->xpos())
        << "the blade re-centres on its owner and takes the scaled orbit offset";
    EXPECT_EQ(land_y, fx->ypos())
        << "the blade re-centres on its owner and takes the scaled orbit offset";
    EXPECT_EQ(1, friendly->dead())
        << "a friendly weapon inside the doubled weapon radius is absorbed";
    EXPECT_FLOAT_EQ(50.0f - 3.0f - 4.0f, fx->stats()->hitpoints())
        << "the absorbed weapon and the struck foe each cost the blade their damage";
    EXPECT_FLOAT_EQ(foe_hp_before - 1.0f, foe->stats()->hitpoints())
        << "a foe inside the body radius is attacked by the blade";
    EXPECT_EQ(4, fx->lifetime())
        << "a blade that survives the sweep spends one tick of lifetime";
    EXPECT_EQ(0, fx->dead())
        << "a blade with hitpoints left survives its own sweep";

    world.delete_objects();
}


// The cloud ages every tick; the first tick only QUEUES a random walk, the
// second executes it through statistics::do_command(). og.rand draws from
// world.rng_, so the drift is scripted through the sim-random override.
TEST(EffectMorePaths, effect_cloud_queues_walk_then_executes_the_drift_step)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();  // walkstep needs a grid (this binary loads no map)

    // Installed AFTER create_new_grid so the grid roll does not eat the script.
    // xd = rand(3) - 1 = 1, yd = rand(3) - 1 = 1, then rand(20) = 4 steps.
    SequenceRandom seq_rng({2, 2, 4});
    // og.rand / og.rand0 draw from current_game->world->rng_, NOT from
    // GameContext::rng, so the script has to go in through the sim guard.
    ScopedSimRandom sim_rng(&seq_rng);

    auto owner = make_living(FAMILY_DRUID, 1);
    ASSERT_NE(nullptr, owner) << "owner created";

    walker* cloud = world.add_fx_ob(Order::FX, FAMILY_CLOUD);
    ASSERT_NE(nullptr, cloud) << "cloud created";

    cloud->set_owner(owner.get());
    cloud->set_team_num(1);
    cloud->set_lifetime(15);
    cloud->setxy(100, 100);
    const float step = cloud->stepsize();
    ASSERT_FLOAT_EQ(4.0f, step) << "the cloud's loader stepsize";

    ASSERT_TRUE(cloud->act()) << "cloud_on_act keeps the cloud alive";

    EXPECT_EQ(14, cloud->lifetime()) << "a cloud ages one tick per act";
    ASSERT_TRUE(cloud->stats()->has_commands())
        << "a cloud with no queued command rolls one drift step";
    ASSERT_EQ(1u, cloud->stats()->commands.size())
        << "exactly one walk command is queued";
    const std::int32_t queued_type = cloud->stats()->commands.front().commandtype;
    const std::int32_t xd = cloud->stats()->commands.front().com1;
    const std::int32_t yd = cloud->stats()->commands.front().com2;
    EXPECT_EQ(COMMAND_WALK, queued_type) << "the queued command is a walk";
    EXPECT_EQ(1, xd) << "rand(3) - 1 is the x drift, scripted to +1";
    EXPECT_EQ(1, yd) << "rand(3) - 1 is the y drift, scripted to +1";
    EXPECT_EQ(4, cloud->stats()->commands.front().commandcount)
        << "rand(20) is the queued step count, scripted to 4";
    EXPECT_EQ(100, cloud->xpos()) << "the queueing tick does not move the cloud";
    EXPECT_EQ(100, cloud->ypos()) << "the queueing tick does not move the cloud";

    // walker::walk() only TURNS when the requested facing differs from curdir;
    // face the queued drift so the second tick is the moving one.
    cloud->set_curdir(static_cast<signed char>(cloud->facing(xd, yd)));

    ASSERT_TRUE(cloud->act()) << "cloud_on_act keeps the cloud alive";

    EXPECT_EQ(13, cloud->lifetime()) << "a cloud ages one tick per act";
    EXPECT_EQ(static_cast<short>(100.0f + static_cast<float>(xd) * step), cloud->xpos())
        << "the queued walk executes one stepsize on x";
    EXPECT_EQ(static_cast<short>(100.0f + static_cast<float>(yd) * step), cloud->ypos())
        << "the queued walk executes one stepsize on y";
    EXPECT_EQ(3, cloud->stats()->commands.front().commandcount)
        << "do_command() walks first and only then spends one iteration";

    world.delete_objects();
}


// A bolt that overlaps its leader detonates: it spawns an FX_EXPLOSION on the
// bolt's own damage/owner/floor, buys the leader 3 rounds of skip_exit, and
// dies. The only in-range foe here IS the leader, so no fork bolt is made.
TEST(EffectMorePaths, effect_chain_lightning_hits_leader_and_spawns_explosion)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto caster = make_living(FAMILY_MAGE, 1, 5);
    ASSERT_NE(nullptr, caster) << "caster created";

    walker* leader = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, leader) << "leader created";

    leader->set_team_num(2);
    leader->setxy(100, 100);
    const short skip_before = leader->skip_exit();

    walker* chain = world.add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_NE(nullptr, chain) << "chain created";

    chain->set_owner(caster.get());
    chain->set_leader(leader);
    chain->set_team_num(caster->team_num());
    chain->set_damage(50.0f);
    chain->set_lineofsight(2);
    chain->setxy(100, 100); // overlap -> hit leader immediately

    ASSERT_TRUE(chain->act()) << "chain_on_act consumes the tick";

    EXPECT_EQ(1, chain->dead()) << "a bolt that reaches its leader detonates and dies";
    EXPECT_EQ(static_cast<short>(skip_before + 3), leader->skip_exit())
        << "the struck leader cannot be hit again for 3 rounds";

    int blast_count = 0;
    walker* blast = find_spawned_fx(world, FAMILY_EXPLOSION, &blast_count);
    ASSERT_NE(nullptr, blast) << "the strike spawns an explosion";
    EXPECT_EQ(1, blast_count) << "exactly one explosion per strike";
    EXPECT_FLOAT_EQ(50.0f, blast->damage()) << "the blast carries the bolt's damage";
    EXPECT_EQ(caster.get(), blast->owner()) << "the blast belongs to the bolt's owner";
    EXPECT_EQ(caster->team_num(), blast->team_num()) << "the blast keeps the bolt's team";
    EXPECT_EQ(ANI_EXPLODE, static_cast<int>(blast->ani_type())) << "the blast plays ANI_EXPLODE";
    EXPECT_EQ(chain->floor(), blast->floor()) << "the blast detonates on the bolt's floor";
    EXPECT_EQ(centered_x(blast, chain), blast->xpos()) << "the blast is centred on the bolt";
    EXPECT_EQ(centered_y(blast, chain), blast->ypos()) << "the blast is centred on the bolt";

    int fork_count = 0;
    (void)find_spawned_fx(world, FAMILY_CHAIN, &fork_count);
    EXPECT_EQ(0, fork_count)
        << "the only foe in range is the leader itself, so the strike forks nowhere";

    world.delete_objects();
}


// explosion_on_death shoves every live same-floor walker in range away from
// the centre with a forced COMMAND_WALK of min(2 + owner level / 15, 8) steps
// along the sign of each axis delta, then attacks it. A hostile target that is
// neither the owner nor an ally takes the FULL blast damage.
TEST(EffectMorePaths, effect_death_explosion_shoves_nearby_targets)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_THIEF, 1, 10);
    ASSERT_NE(nullptr, owner) << "owner created";
    ASSERT_EQ(10, static_cast<int>(owner->stats()->level()))
        << "the shove distance is derived from the owner's level";

    walker* explosion = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, explosion) << "explosion created";

    explosion->set_owner(owner.get());
    explosion->set_skip_exit(0);
    explosion->setxy(100, 100);
    explosion->set_damage(40.0f);

    walker* target = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, target) << "target created";
    target->set_team_num(2);
    target->setxy(110, 100);   // 10 px due east: dx = +1, dy = 0
    target->stats()->set_armor(0.0f);
    target->stats()->set_max_hitpoints(200.0f);
    target->stats()->set_hitpoints(200.0f);
    target->stats()->clear_command();
    // effect::death(FAMILY_EXPLOSION) shoves via force_command(), but the
    // subsequent attack triggers statistics::hit_response(), which may
    // clear commands when the attacker is a "new" foe. Pre-seed the foe
    // relationship so the shove command remains queued deterministically.
    target->set_foe(owner.get());

    explosion->set_dead(1);
    ASSERT_TRUE(explosion->death()) << "the blast hook runs on the first death()";

    ASSERT_TRUE(target->stats()->has_commands())
        << "a target inside the blast range is shoved";
    EXPECT_EQ(COMMAND_WALK, target->stats()->commands.front().commandtype)
        << "the shove is a forced walk";
    EXPECT_EQ(1, target->stats()->commands.front().com1)
        << "the shove points away from the blast centre on x";
    EXPECT_EQ(0, target->stats()->commands.front().com2)
        << "a target due east gets no y component";
    EXPECT_EQ(2, target->stats()->commands.front().commandcount)
        << "shove distance is min(2 + owner level / 15, 8) = 2 for a level-10 owner";
    // Full (uncut) blast damage: compute_base_damage(40, FixedRandom(1)) =
    // 40 - sqrt(40)/2 + 1 = 37.84, armor 0 takes nothing off, and
    // damage_to_hit_points rounds that to 38.
    EXPECT_FLOAT_EQ(162.0f, target->stats()->hitpoints())
        << "a hostile, non-owner target takes the blast's full 40 damage";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


// explosion_on_death adopts itself when it has no owner, and skip_exit > 0
// zeroes the level-derived blast range, which compute_explosion_range then
// clamps back up to 16 -- so find_in_range reaches only 15 + 16 = 31 pixels.
TEST(EffectMorePaths, effect_batch3_explosion_owner_fallback_and_skip_exit_range_clamp)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* orphan = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, orphan) << "orphan explosion created";

    orphan->set_owner(nullptr); // force the owner=self path in explosion_on_death()
    orphan->set_skip_exit(1);
    orphan->setxy(100, 100);
    orphan->set_dead(1);
    ASSERT_TRUE(orphan->death()) << "the first death() runs the blast hook";

    EXPECT_EQ(orphan, orphan->owner()) << "a missing owner falls back to self";
    EXPECT_FALSE(orphan->death()) << "death() is guarded against a second call";

    world.delete_objects();

    walker* blast = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, blast) << "blast created";
    blast->set_owner(nullptr);
    blast->stats()->set_level(20);  // level*4 = 80 -> 15 + 80 = 95 without the zeroing
    blast->set_skip_exit(1);
    blast->set_damage(1.0f);        // exactly one hit point against an armor-0 target
    blast->setxy(100, 100);

    walker* near_ob = world.add_ob(Order::Living, FAMILY_ORC);
    walker* far_ob = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, near_ob) << "near target created";
    ASSERT_NE(nullptr, far_ob) << "far target created";
    for (walker* w : {near_ob, far_ob})
    {
        w->set_team_num(2);
        w->stats()->set_armor(0.0f);
        w->stats()->clear_command();
    }
    near_ob->setxy(124, 100);  // 24 Manhattan: inside 31
    far_ob->setxy(100, 164);   // 64 Manhattan: outside 31, inside an unclamped 95
    const float near_hp_before = near_ob->stats()->hitpoints();
    const float far_hp_before = far_ob->stats()->hitpoints();

    blast->set_dead(1);
    ASSERT_TRUE(blast->death()) << "the blast hook runs";

    EXPECT_EQ(blast, blast->owner()) << "a missing owner falls back to self";
    ASSERT_TRUE(near_ob->stats()->has_commands())
        << "a target inside the clamped blast range is shoved";
    EXPECT_EQ(COMMAND_WALK, near_ob->stats()->commands.front().commandtype)
        << "the shove is a forced walk";
    EXPECT_EQ(1, near_ob->stats()->commands.front().com1)
        << "the shove points away from the blast centre on x";
    EXPECT_EQ(0, near_ob->stats()->commands.front().com2)
        << "the shove has no y component for a target due east";
    EXPECT_EQ(3, near_ob->stats()->commands.front().commandcount)
        << "shove distance is min(2 + owner level / 15, 8)";
    EXPECT_FLOAT_EQ(near_hp_before - 1.0f, near_ob->stats()->hitpoints())
        << "a shoved target is also attacked at full damage";
    EXPECT_FALSE(far_ob->stats()->has_commands())
        << "skip_exit collapses the range to 16, so a target 64 px out is untouched";
    EXPECT_FLOAT_EQ(far_hp_before, far_ob->stats()->hitpoints())
        << "skip_exit collapses the range to 16, so a target 64 px out is untouched";

    world.delete_objects();
}


// bomb_on_death: a dead owner is replaced by self, and the bomb hands its own
// damage, level, floor and centre to the FX_EXPLOSION it leaves behind.
TEST(EffectMorePaths, effect_batch3_bomb_owner_dead_fallback_path)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* dead_owner = world.add_ob(Order::Living, FAMILY_THIEF);
    ASSERT_NE(nullptr, dead_owner) << "owner created";
    dead_owner->set_dead(1);

    walker* bomb = world.add_fx_ob(Order::FX, FAMILY_BOMB);
    ASSERT_NE(nullptr, bomb) << "bomb created";

    bomb->set_owner(dead_owner); // bomb_on_death should replace this with self
    bomb->set_damage(15.0f);
    bomb->stats()->set_level(4);
    bomb->setxy(100, 100);
    bomb->set_dead(1);
    ASSERT_TRUE(bomb->death()) << "the bomb's death hook runs";

    EXPECT_EQ(bomb, bomb->owner()) << "a dead owner is replaced by self";

    int blast_count = 0;
    walker* blast = find_spawned_fx(world, FAMILY_EXPLOSION, &blast_count);
    ASSERT_NE(nullptr, blast) << "a dying bomb leaves an explosion behind";
    EXPECT_EQ(1, blast_count) << "exactly one explosion per bomb";
    EXPECT_EQ(bomb, blast->owner()) << "the blast belongs to the bomb's (now self) owner";
    EXPECT_FLOAT_EQ(15.0f, blast->damage()) << "the blast carries the bomb's damage";
    EXPECT_FLOAT_EQ(0.0f, blast->stats()->hitpoints()) << "the blast is spawned at 0 hitpoints";
    EXPECT_EQ(4, blast->stats()->level()) << "the blast inherits the owner's level";
    EXPECT_EQ(ANI_EXPLODE, static_cast<int>(blast->ani_type())) << "the blast plays ANI_EXPLODE";
    EXPECT_EQ(bomb->floor(), blast->floor()) << "the blast detonates on the bomb's floor";
    EXPECT_EQ(centered_x(blast, bomb), blast->xpos()) << "the blast is centred on the bomb";
    EXPECT_EQ(centered_y(blast, bomb), blast->ypos()) << "the blast is centred on the bomb";

    world.delete_objects();
}


// guard_tail's exhaustion arm: when the absorbed weapon and the struck foe
// together take the guard past 0 hitpoints it dies instead of ageing, while a
// blade with hitpoints to spare only spends a tick of lifetime.
TEST(EffectMorePaths, effect_batch3_guard_dies_when_collisions_drain_its_hitpoints)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    auto owner = make_living(FAMILY_CLERIC, 1, 6);
    ASSERT_NE(nullptr, owner) << "owner created";

    walker* shield = world.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, shield) << "shield created";
    shield->set_owner(owner.get());
    shield->set_team_num(owner->team_num());
    shield->setxy(100, 100);
    shield->stats()->set_hitpoints(1.0f);
    shield->set_lifetime(0);

    // orbit step 1 = (-9, -22) off the owner's centre.
    const short shield_x = static_cast<short>(centered_x(shield, owner.get()) - 9);
    const short shield_y = static_cast<short>(centered_y(shield, owner.get()) - 22);

    auto incoming_owned =
        og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, incoming_owned) << "incoming weapon created";
    walker* incoming = incoming_owned.get();
    incoming->set_team_num(owner->team_num()); // friendly: absorbable by the sweep
    incoming->set_damage(2.0f);
    incoming->setxy(shield_x, shield_y);
    world.oblist.push_back(std::move(incoming_owned));

    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(2);
    foe->set_damage(2.0f);
    foe->setxy(shield_x, shield_y);

    ASSERT_TRUE(shield->act()) << "magic_shield_on_act consumes the tick";

    EXPECT_EQ(1, incoming->dead()) << "the friendly weapon is absorbed";
    EXPECT_FLOAT_EQ(1.0f - 2.0f - 2.0f, shield->stats()->hitpoints())
        << "the absorbed weapon and the struck foe each drain the guard";
    EXPECT_EQ(0, shield->lifetime())
        << "a guard already out of hitpoints never reaches the lifetime tick";
    EXPECT_EQ(1, shield->dead()) << "a guard drained past 0 hitpoints dies";

    // The same tail on a blade with hitpoints to spare: it survives and ages.
    walker* boomerang = world.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, boomerang) << "boomerang created";
    boomerang->set_owner(owner.get());
    boomerang->set_team_num(owner->team_num());
    boomerang->setxy(100, 100);
    boomerang->set_drawcycle(10);  // effect::act() makes it 11 before the hook
    boomerang->stats()->set_hitpoints(20.0f);
    boomerang->set_lifetime(5);

    // orbit step 11 is (22, 9), scaled by (11 + 4) / 48.
    const short blade_x = static_cast<short>(
        static_cast<float>(centered_x(boomerang, owner.get())) + 22.0f * 15.0f / 48.0f);
    const short blade_y = static_cast<short>(
        static_cast<float>(centered_y(boomerang, owner.get())) + 9.0f * 15.0f / 48.0f);
    foe->setxy(blade_x, blade_y);

    ASSERT_TRUE(boomerang->act()) << "boomerang_on_act consumes the tick";

    EXPECT_EQ(blade_x, boomerang->xpos()) << "the blade takes the scaled orbit offset";
    EXPECT_EQ(blade_y, boomerang->ypos()) << "the blade takes the scaled orbit offset";
    EXPECT_FLOAT_EQ(18.0f, boomerang->stats()->hitpoints())
        << "the struck foe costs the blade its damage";
    EXPECT_EQ(4, boomerang->lifetime()) << "a surviving blade spends one tick of lifetime";
    EXPECT_EQ(0, boomerang->dead()) << "a blade with hitpoints left survives the sweep";

    world.delete_objects();
}


// The "already close" arm of chain_on_act: a bolt that does NOT overlap its
// leader but sits within 2 * stepsize of its centre snaps onto that centre
// (and still spends a line of sight) instead of detonating.
TEST(EffectMorePaths, effect_batch3_chain_snap_to_leader_and_effect_death_guard)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    auto owner = make_living(FAMILY_MAGE, 1, 5);
    walker* leader = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, owner) << "owner created";
    ASSERT_NE(nullptr, leader) << "leader created";
    leader->set_team_num(2);
    leader->setxy(120, 120);

    walker* chain = world.add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_NE(nullptr, chain) << "chain created";
    chain->set_owner(owner.get());
    chain->set_leader(leader);
    chain->set_lineofsight(10);

    // Clear the leader's box so hits() is false (an overlapping bolt would
    // detonate), then widen stepsize so the close arm is the one taken.
    // distance_to_ob_center() returns a SQUARED distance, and the gate is
    // `distance > stepsize * 2`.
    const short gap = static_cast<short>(leader->sizex() + chain->sizex() + 8);
    chain->setxy(static_cast<short>(leader->xpos() + gap), leader->ypos());
    ASSERT_EQ(0, static_cast<int>(hits(chain->xpos(), chain->ypos(), chain->sizex(), chain->sizey(),
                                       leader->xpos(), leader->ypos(),
                                       leader->sizex(), leader->sizey())))
        << "the bolt must not overlap the leader or act() takes the detonate arm";
    const std::int32_t squared = chain->distance_to_ob_center(leader);
    ASSERT_GT(squared, 0) << "the bolt starts off the leader's centre";
    chain->set_stepsize(static_cast<float>(squared));  // 2 * stepsize > squared

    ASSERT_TRUE(chain->act()) << "chain_on_act consumes the tick";

    EXPECT_EQ(0, chain->dead()) << "a bolt that does not overlap its leader does not detonate";
    EXPECT_EQ(9, chain->lineofsight()) << "a homing tick spends one line of sight";
    EXPECT_EQ(centered_x(chain, leader), chain->xpos())
        << "the close arm centres the bolt on its leader";
    EXPECT_EQ(centered_y(chain, leader), chain->ypos())
        << "the close arm centres the bolt on its leader";

    // effect::death() second-call guard on a fresh effect object.
    walker* death_fx = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, death_fx) << "death guard fx created";
    death_fx->set_dead(1);
    bool first = death_fx->death();
    bool second = death_fx->death();
    ASSERT_TRUE(first) << "first death call should succeed";
    ASSERT_TRUE(!second) << "second death call should be guarded";

    world.delete_objects();
}


// A bolt whose owner has no saved guy takes the level-scaled fork range, and a
// strike whose fork damage clears fork_min_damage arms a successor bolt on the
// one nearby foe that is not the leader.
TEST(EffectMorePaths, effect_batch4_chain_guard_ownerless_and_non_myguy_foe_scan)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    // Early guard branch: missing owner must kill chain immediately.
    walker* orphan_chain = world.add_fx_ob(Order::FX, FAMILY_CHAIN);
    walker* any_leader = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, orphan_chain) << "orphan chain created";
    ASSERT_NE(nullptr, any_leader) << "orphan chain leader created";
    orphan_chain->set_leader(any_leader);
    orphan_chain->set_lineofsight(4);
    orphan_chain->setxy(100, 100);
    any_leader->setxy(100, 100);
    ASSERT_TRUE(orphan_chain->act()) << "chain_on_act consumes the tick";
    EXPECT_EQ(1, orphan_chain->dead()) << "ownerless chain should die in guard path";
    EXPECT_EQ(4, orphan_chain->lineofsight())
        << "the guard arm returns before the homing tick spends line of sight";

    world.delete_objects();

    // Hit-leader path with owner->myguy == nullptr takes the non-myguy fork scan.
    walker* owner = world.add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = world.add_ob(Order::Living, FAMILY_CLERIC);
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    walker* chain = world.add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_NE(nullptr, owner) << "owner created";
    ASSERT_NE(nullptr, leader) << "leader created";
    ASSERT_NE(nullptr, foe) << "foe created";
    ASSERT_NE(nullptr, chain) << "chain created";
    ASSERT_EQ(nullptr, owner->myguy) << "an add_ob caster carries no saved guy";

    owner->set_team_num(1);
    owner->stats()->set_level(8);
    leader->set_team_num(1); // not a foe, prevents leader from consuming the first chain bounce
    foe->set_team_num(2);
    leader->setxy(120, 120);
    foe->setxy(124, 120);

    chain->set_owner(owner);
    chain->set_leader(leader);
    chain->set_team_num(owner->team_num());
    chain->set_damage(70.0f); // fork damage trunc(70 * 0.5) = 35 > fork_min_damage 20
    chain->set_lineofsight(3);
    chain->setxy(120, 120); // overlap leader -> detonate + fork
    const short leader_skip_before = leader->skip_exit();

    ASSERT_TRUE(chain->act()) << "chain_on_act consumes the tick";

    EXPECT_EQ(1, chain->dead()) << "a bolt that reaches its leader dies";
    EXPECT_EQ(static_cast<short>(leader_skip_before + 3), leader->skip_exit())
        << "the struck leader cannot be hit again for 3 rounds";

    int blast_count = 0;
    (void)find_spawned_fx(world, FAMILY_EXPLOSION, &blast_count);
    EXPECT_EQ(1, blast_count) << "the strike leaves exactly one blast";

    int fork_count = 0;
    walker* bolt = find_spawned_fx(world, FAMILY_CHAIN, &fork_count);
    ASSERT_NE(nullptr, bolt) << "the strike forks onto the one nearby non-leader foe";
    EXPECT_EQ(1, fork_count) << "one foe in range means exactly one successor bolt";
    EXPECT_EQ(foe, bolt->leader()) << "the successor bolt seeks the foe, not the old leader";
    EXPECT_EQ(owner, bolt->owner()) << "the successor bolt keeps the caster";
    EXPECT_FLOAT_EQ(35.0f, bolt->damage()) << "fork damage is trunc(70 * 0.5)";
    EXPECT_EQ(owner->team_num(), bolt->team_num()) << "the successor bolt keeps the caster's team";
    EXPECT_EQ(BIT_MAGICAL, static_cast<int>(bolt->stats()->query_bit_flags(BIT_MAGICAL)))
        << "a forked bolt is magical";
    EXPECT_EQ(centered_x(bolt, chain), bolt->xpos()) << "the successor starts on the strike point";
    EXPECT_EQ(centered_y(bolt, chain), bolt->ypos()) << "the successor starts on the strike point";

    world.delete_objects();
}


// The homing arm clamps each axis to exactly one stepsize when the leader is
// farther than that up and to the left.
TEST(EffectMorePaths, effect_batch4_chain_movement_negative_delta_branch)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* owner = world.add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = world.add_ob(Order::Living, FAMILY_ORC);
    walker* chain = world.add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_NE(nullptr, owner) << "owner created";
    ASSERT_NE(nullptr, leader) << "leader created";
    ASSERT_NE(nullptr, chain) << "chain created";

    owner->set_team_num(1);
    leader->set_team_num(2);
    leader->setxy(60, 60);
    chain->set_owner(owner);
    chain->set_leader(leader);
    chain->set_team_num(1);
    chain->set_lineofsight(8);
    chain->set_stepsize(4.0f);
    chain->setxy(140, 140); // 80 px up-left: both deltas negative and over a step

    ASSERT_TRUE(chain->act()) << "chain_on_act consumes the tick";

    EXPECT_EQ(136, static_cast<int>(chain->xpos()))
        << "the x step is clamped to exactly one stepsize toward the leader";
    EXPECT_EQ(136, static_cast<int>(chain->ypos()))
        << "the y step is clamped to exactly one stepsize toward the leader";
    EXPECT_EQ(7, chain->lineofsight()) << "the movement arm spends one line of sight";
    EXPECT_EQ(0, chain->dead()) << "a homing bolt with line of sight left survives the tick";

    world.delete_objects();
}


// The homing arm clamps each axis independently: an axis whose delta is
// SMALLER than one stepsize closes exactly, an axis whose delta is larger
// moves exactly one stepsize. A ">" pin passed on a 1-px twitch; these are
// the two exact landing coordinates.
TEST(EffectMorePaths, effect_batch6_chain_small_delta_else_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* owner = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_MAGE);
    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* chain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_NE(nullptr, owner) << "owner created";
    ASSERT_NE(nullptr, leader) << "leader created";
    ASSERT_NE(nullptr, chain) << "chain created";

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
    ASSERT_TRUE(chain->act()) << "chain_on_act consumes the tick";

    EXPECT_EQ(106, static_cast<int>(chain->xpos()))
        << "an x delta of 6 inside the 10-px step closes exactly, it is not overshot";
    EXPECT_EQ(110, static_cast<int>(chain->ypos()))
        << "a y delta of 50 is clamped to exactly one 10-px stepsize";
    EXPECT_EQ(7, chain->lineofsight()) << "the movement arm spends one line of sight";
    EXPECT_EQ(0, chain->dead()) << "a homing bolt with line of sight left survives the tick";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


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


TEST(EffectMorePaths, effect_round11_compute_explosion_range_clamps_and_hits_edge_touches)
{
    ASSERT_EQ(16, (int)compute_explosion_range(1, 0)) << "explosion range should clamp to minimum 16";
    ASSERT_EQ(96, (int)compute_explosion_range(40, 0)) << "explosion range should clamp to maximum 96";
    ASSERT_EQ(16, (int)compute_explosion_range(40, 1)) << "skip_exit branch should zero before min clamp, resulting in 16";

    // Boundary-touching boxes are still collisions in hits().
    ASSERT_EQ(1, (int)hits(10, 10, 10, 10, 20, 10, 5, 5)) << "touching on x edge should count as hit";
    ASSERT_EQ(1, (int)hits(10, 10, 10, 10, 10, 20, 5, 5)) << "touching on y edge should count as hit";
}
