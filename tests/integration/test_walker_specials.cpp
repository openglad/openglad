#include <openglad/interface/game_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

static void ensure_level_loaded()
{
    if (og::runtime::current_session->myscreen_ == nullptr)
        return;
    if (og::runtime::current_session->myscreen_->world().grid.valid() && og::runtime::current_session->myscreen_->world().pixmaxx > 0 && og::runtime::current_session->myscreen_->world().pixmaxy > 0)
        return;

    og::runtime::current_session->myscreen_->world().id = 1;
    (void)og::runtime::current_session->myscreen_->load_level();
}

static void teardown_walker_special_test()
{
    if (og::runtime::current_session->myscreen_ != nullptr) {
        og::runtime::current_session->myscreen_->world().delete_objects();
    }
}


static walker* make_special_guy(char family, unsigned char team = 0, short level = 3)
{
    // Many specials/teleports rely on grid passability; make tests self-contained
    // by ensuring a level is loaded when running filtered subsets.
    ensure_level_loaded();

    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) {
        w->setxy(100, 100);
        w->stats()->set_magicpoints(500); // lots of magic for specials
        w->stats()->set_max_magicpoints(500);
    }
    return w.release();
}

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

static int count_family_in_fxlist(char family)
{
    int count = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
        walker* w = uptr.get();
        if (w && w->family() == family)
            count++;
    }
    return count;
}

static int count_family_all_lists(char family)
{
    return count_family_in_oblist(family) + count_family_in_fxlist(family);
}

// Family ids are per-ORDER -- living 10 is a medium slime, treasure 10 a
// life gem and fx 10 a chain bolt, all at once -- so anything that counts
// one specific spawn has to match the order as well as the family.
static int count_order_family(Order order, char family)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    int count = 0;
    for (auto& uptr : world.oblist)
        if (uptr && uptr->order() == order && uptr->family() == family)
            count++;
    for (auto& uptr : world.fxlist)
        if (uptr && uptr->order() == order && uptr->family() == family)
            count++;
    for (auto& uptr : world.weaplist)
        if (uptr && uptr->order() == order && uptr->family() == family)
            count++;
    return count;
}

// The LAST entity of (order, family) across the three lists, live or dead --
// i.e. the one a cast just appended.
static walker* find_order_family(Order order, char family)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    walker* found = nullptr;
    for (auto& uptr : world.oblist)
        if (uptr && uptr->order() == order && uptr->family() == family)
            found = uptr.get();
    for (auto& uptr : world.fxlist)
        if (uptr && uptr->order() == order && uptr->family() == family)
            found = uptr.get();
    for (auto& uptr : world.weaplist)
        if (uptr && uptr->order() == order && uptr->family() == family)
            found = uptr.get();
    return found;
}

struct VelocitySample
{
    float x = 0.0f;
    float y = 0.0f;
};

class ScopedGameplayActiveOverride
{
public:
    explicit ScopedGameplayActiveOverride(bool active)
        : session_(og::runtime::current_session)
        , previous_(session_ ? session_->gameplay_active_ : false)
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = active;
    }

    ~ScopedGameplayActiveOverride()
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = previous_;
    }

    ScopedGameplayActiveOverride(const ScopedGameplayActiveOverride&) = delete;
    ScopedGameplayActiveOverride& operator=(const ScopedGameplayActiveOverride&) = delete;

private:
    og::runtime::SessionState* session_ = nullptr;
    bool previous_ = false;
};

static std::vector<VelocitySample> run_elf_barrage(std::uint32_t seed, char special)
{
    // Level loading creates walkers and consumes simulation RNG, so make sure
    // the map exists before seeding the barrage under test.
    ensure_level_loaded();
    ScopedGameplayActiveOverride gameplay_active(true);

    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.rng_.state_ = seed;

    walker* elf = make_special_guy(FAMILY_ELF, 1, 6);
    if (elf == nullptr)
        return {};

    elf->setxy(100, 100);
    elf->set_lastx(elf->stepsize());
    elf->set_lasty(0.0f);
    elf->set_busy(0.0f);
    elf->set_current_special(special);
    elf->stats()->set_magicpoints(2000.0f);
    elf->stats()->set_max_magicpoints(2000.0f);

    const std::size_t weapon_count_before = world.weaplist.size();
    (void)elf->special();

    std::vector<VelocitySample> result;
    std::size_t index = 0;
    for (auto& uptr : world.weaplist)
    {
        if (index++ < weapon_count_before || uptr == nullptr)
            continue;
        result.push_back({uptr->lastx(), uptr->lasty()});
    }

    delete elf;
    world.delete_objects();
    return result;
}

static walker* find_first_alive_ob_by_family(char family)
{
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
        walker* w = uptr.get();
        if (w && w->family() == family && !w->dead())
            return w;
    }
    return nullptr;
}

class NoStatsWalker : public walker {
public:
    NoStatsWalker() : walker()
    {
        stats_.reset();
    }
};

class WalkerSpecials : public ::testing::Test {
public:
    void SetUp() override
    {}

    void TearDown() override
    {
        teardown_walker_special_test();
    }
};

// ---------------------------------------------------------------------------
// special() and the two self-teleport helpers. The per-(family, special)
// "does the cast actually DO its thing" cases live in the
// *_drives_simulation block further down; what is left here is the shared
// walker-level contract: where the teleport lands, what turn_undead returns,
// what a family with no specials does, and the whole-registry sweep.
// ---------------------------------------------------------------------------

namespace {

// Every draw is the top of its range (max_exclusive - 1). turn_undead's
// resistance test is `next(range*40) > next(level*10)`, so a ceiling stream
// makes the left side 959 and the right side 9 no matter how many draws
// walker::attack consumes between the two undead -- the kill arm without a
// seed hunt.
class CeilingRandom : public IRandom
{
public:
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        return (max_exclusive == 0) ? 0 : (max_exclusive - 1);
    }
};

// Installs a stream over the world's SimRandom -- the one living::act,
// act_random, turn_undead, walker::attack and statistics::try_command
// actually draw from. A GameContext rng reaches only walker construction and
// combat math (walker_rng/combat_rng), so pushing a context RNG leaves these
// branch picks to whatever a shuffled predecessor left in the LCG.
class ScopedSimStream
{
public:
    explicit ScopedSimStream(IRandom* rng) : rng_ptr_(rng)
    {
        og::sim::set_sim_random_override(&rng_ptr_);
    }
    ~ScopedSimStream() { og::sim::set_sim_random_override(nullptr); }
    ScopedSimStream(const ScopedSimStream&) = delete;
    ScopedSimStream& operator=(const ScopedSimStream&) = delete;

private:
    IRandom* rng_ptr_;
};

// The first live entity of a family, in oblist or fxlist (add_ob routes FX to
// one, add_fx_ob to the other, and which one is not this test's business).
walker* find_any_by_family(char family)
{
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
        if (uptr && uptr->family() == family && !uptr->dead())
            return uptr.get();
    for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist)
        if (uptr && uptr->family() == family && !uptr->dead())
            return uptr.get();
    return nullptr;
}

// A passable landing for the teleport-marker arm: farther than the 64 px the
// marker path requires, and in-bounds for every level layout.
bool find_far_passable_spot(walker* w, Sint32& out_x, Sint32& out_y)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    for (Sint32 x = 0; x < world.pixmaxx - w->sizex() - 2; x += GRID_SIZE)
    {
        for (Sint32 y = 0; y < world.pixmaxy - w->sizey() - 2; y += GRID_SIZE)
        {
            const Sint32 dx = x - w->xpos();
            const Sint32 dy = y - w->ypos();
            if (dx * dx + dy * dy <= 80 * 80)
                continue;
            if (world.query_passable(static_cast<float>(x), static_cast<float>(y), w))
            {
                out_x = x;
                out_y = y;
                return true;
            }
        }
    }
    return false;
}

} // namespace


// walker::teleport, marker arm: a live owned marker more than 64 px away is
// the destination. The caster lands centered on it (center_on math) and the
// marker spends one use, dying at zero.
TEST_F(WalkerSpecials, teleport_marker_lands_centered_and_spends_a_use)
{
    walker* w = make_special_guy(FAMILY_MAGE);
    ASSERT_NE(nullptr, w) << "mage created";
    auto& world = og::runtime::current_session->myscreen_->world();

    walker* marker = world.add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker) << "teleport marker created";
    marker->set_owner(w);
    marker->set_dead(0);
    marker->set_lifetime(1);

    Sint32 mx = 0;
    Sint32 my = 0;
    ASSERT_TRUE(find_far_passable_spot(w, mx, my))
        << "the loaded level must offer a passable cell more than 64 px away";
    marker->setxy(mx, my);
    const Sint32 expect_x = marker->xpos() + marker->sizex() / 2 - w->sizex() / 2;
    const Sint32 expect_y = marker->ypos() + marker->sizey() / 2 - w->sizey() / 2;

    ASSERT_TRUE(w->teleport()) << "a far, live, owned marker is a valid destination";
    EXPECT_EQ(expect_x, static_cast<Sint32>(w->xpos()))
        << "the caster lands centered on the marker in x";
    EXPECT_EQ(expect_y, static_cast<Sint32>(w->ypos()))
        << "the caster lands centered on the marker in y";
    EXPECT_EQ(0, static_cast<int>(marker->lifetime()))
        << "the trip spends one marker use";
    EXPECT_EQ(1, static_cast<int>(marker->dead()))
        << "a marker with no uses left dies";

    delete w;
    world.delete_objects();
}


// walker::teleport, near-marker arm: a marker within 64 px is not a
// destination at all -- it keeps every use and the caster takes the random
// blink instead. teleport_ranged then keeps its hop inside the range it was
// given.
TEST_F(WalkerSpecials, teleport_ignores_a_marker_within_64_px)
{
    walker* w = make_special_guy(FAMILY_MAGE);
    ASSERT_NE(nullptr, w) << "mage created";
    auto& world = og::runtime::current_session->myscreen_->world();
    w->set_user(0);

    walker* marker = world.add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker) << "near marker created";
    marker->set_owner(w);
    marker->set_dead(0);
    marker->set_lifetime(5);
    marker->setxy(static_cast<Sint32>(w->xpos() + 4), static_cast<Sint32>(w->ypos() + 4));
    const Sint32 near_x = marker->xpos() + marker->sizex() / 2 - w->sizex() / 2;
    const Sint32 near_y = marker->ypos() + marker->sizey() / 2 - w->sizey() / 2;

    world.rng_.state_ = 7; // fixed blink destination
    ASSERT_TRUE(w->teleport()) << "with no usable marker the random blink runs";
    EXPECT_EQ(5, static_cast<int>(marker->lifetime()))
        << "a marker within 64 px is skipped, not consumed";
    EXPECT_EQ(0, static_cast<int>(marker->dead()))
        << "a skipped marker stays alive";
    EXPECT_FALSE(static_cast<Sint32>(w->xpos()) == near_x
                 && static_cast<Sint32>(w->ypos()) == near_y)
        << "the blink must not land the caster on the too-close marker";

    const Sint32 from_x = w->xpos();
    const Sint32 from_y = w->ypos();
    world.rng_.state_ = 11; // fixed ranged-hop destination
    ASSERT_TRUE(w->teleport_ranged(24)) << "a 24 px hop should find a clear cell";
    EXPECT_GE(static_cast<Sint32>(w->xpos()), from_x - 24) << "the hop stays within range in x";
    EXPECT_LE(static_cast<Sint32>(w->xpos()), from_x + 24) << "the hop stays within range in x";
    EXPECT_GE(static_cast<Sint32>(w->ypos()), from_y - 24) << "the hop stays within range in y";
    EXPECT_LE(static_cast<Sint32>(w->ypos()), from_y + 24) << "the hop stays within range in y";

    delete w;
    world.delete_objects();
}


// Archmage special 2 with the shifter held is chain lightning: ONE bolt,
// charged from the MP pool and leashed to the nearest same-floor foe.
TEST_F(WalkerSpecials, archmage_chain_lightning_spawns_one_leashed_bolt)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_NE(nullptr, arch) << "archmage created";
    arch->setxy(120, 120);
    arch->set_busy(0);
    arch->set_shifter_down(1);
    arch->set_current_special(2);
    arch->stats()->set_max_magicpoints(1800);
    arch->stats()->set_magicpoints(1800);
    arch->stats()->set_special_cost(2, 0);
    if (arch->myguy)
        arch->myguy->intelligence = 220;

    walker* near_foe = world.add_ob(Order::Living, FAMILY_ORC);
    walker* far_foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, near_foe) << "near foe created";
    ASSERT_NE(nullptr, far_foe) << "far foe created";
    near_foe->set_team_num(2);
    far_foe->set_team_num(2);
    near_foe->stats()->set_level(1);
    far_foe->stats()->set_level(1);
    near_foe->setxy(static_cast<Sint32>(arch->xpos() + 10), static_cast<Sint32>(arch->ypos()));
    far_foe->setxy(static_cast<Sint32>(arch->xpos() + 40), static_cast<Sint32>(arch->ypos()));

    const int chains_before = count_family_all_lists(FAMILY_CHAIN);
    ASSERT_TRUE(arch->special()) << "chain lightning fires with foes in range";
    EXPECT_EQ(chains_before + 1, count_family_all_lists(FAMILY_CHAIN))
        << "one cast summons exactly one chain bolt";
    walker* bolt = find_any_by_family(FAMILY_CHAIN);
    ASSERT_NE(nullptr, bolt) << "the chain bolt is in the world";
    EXPECT_EQ(near_foe, bolt->leader())
        << "the bolt is leashed to the NEAREST same-floor foe";
    // mp_pool_damage(slot 2) = min((1800 MP - 0 cost) / 2, kMpPoolDamageCap 600)
    // -- the cap binds here -- and the same number is both the bolt's damage
    // and the pool the cast spends.
    EXPECT_FLOAT_EQ(600.0f, bolt->damage())
        << "the bolt carries the capped MP pool as its damage";
    EXPECT_FLOAT_EQ(1200.0f, arch->stats()->magicpoints())
        << "and the archmage paid exactly that pool out of its 1800";
    EXPECT_FLOAT_EQ(5.0f, arch->busy()) << "chain lightning costs 5 busy ticks";

    delete arch;
    world.delete_objects();
}


// Archmage special 3 with the shifter held is the TRUE summon: one real fire
// elemental, owned by the caster, on the caster's team, with a finite
// lifetime (it is ammunition, not a recruit).
TEST_F(WalkerSpecials, archmage_true_summon_places_one_owned_fire_elemental)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_NE(nullptr, arch) << "archmage created";
    arch->setxy(120, 120);
    arch->set_busy(0);
    arch->set_shifter_down(1);
    arch->set_current_special(3);
    arch->stats()->set_max_magicpoints(1800);
    arch->stats()->set_magicpoints(1800);
    arch->stats()->set_special_cost(3, 0);
    if (arch->myguy)
        arch->myguy->intelligence = 220;

    const int elementals_before = count_family_in_oblist(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(arch->special()) << "the true summon fires with Int 220 and MP to spare";
    EXPECT_EQ(elementals_before + 1, count_family_in_oblist(FAMILY_FIREELEMENTAL))
        << "one cast summons exactly one fire elemental";
    walker* elemental = find_first_alive_ob_by_family(FAMILY_FIREELEMENTAL);
    ASSERT_NE(nullptr, elemental) << "the summoned elemental is in the world";
    EXPECT_EQ(arch, elemental->owner()) << "the summon belongs to its summoner";
    EXPECT_EQ(static_cast<int>(arch->team_num()), static_cast<int>(elemental->team_num()))
        << "the summon joins the summoner's team";
    ASSERT_EQ(8, static_cast<int>(arch->stats()->level()))
        << "fixture precondition: the lifetime below is this level's value";
    // elemental_lifetime(8) = 200 + 60*8 = 680, below the 980 soft knee.
    EXPECT_EQ(680, static_cast<int>(elemental->lifetime()))
        << "a true summon is ammunition: it carries the level's exact lifetime";
    EXPECT_TRUE(elemental->summoned())
        << "and is flagged summoned, so it is never a SAVE_ALL casualty";
    // halve_mp_surcharge(slot 3): half of the 1800 post-cost pool.
    EXPECT_FLOAT_EQ(900.0f, arch->stats()->magicpoints())
        << "the summon surcharge is half the post-cost pool";
    EXPECT_FLOAT_EQ(15.0f, arch->busy()) << "summoning costs 15 busy ticks";

    delete arch;
    world.delete_objects();
}


// living::act's ACT_RANDOM "4 of 5 times" arm: both 1-in-5 rolls miss, the
// walker already has a foe, so it snaps its facing down to an even step and
// queues a 300-tick search. (The act_random() arm and its own commands are
// pinned in test_walker_core_more.cpp.)
TEST_F(WalkerSpecials, act_random_search_arm_snaps_facing_and_queues_300_tick_search)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* actor = make_special_guy(FAMILY_ORC, 1, 4);
    walker* enemy = make_special_guy(FAMILY_SOLDIER, 2, 4);
    ASSERT_NE(nullptr, actor) << "actor created";
    ASSERT_NE(nullptr, enemy) << "enemy created";
    actor->setxy(100, 100);
    enemy->setxy(108, 100);
    actor->set_foe(enemy);
    actor->set_act_type(ACT_RANDOM);
    // The exact pre-switch state act() needs: nothing animating, nothing
    // queued, no pending turn, not busy, not frozen -- otherwise act()
    // returns early from animate()/turn()/do_command() and never reaches the
    // ACT_RANDOM arm. curdir == enddir == FACE_UP_RIGHT so the arm's own
    // (enddir/2)*2 snap is observable.
    actor->stats()->clear_command();
    actor->stats()->set_frozen_delay(0);
    actor->set_ani_type(ANI_WALK);
    actor->set_busy(0);
    actor->set_curdir(static_cast<signed char>(FACE_UP_RIGHT));
    actor->set_enddir(static_cast<char>(FACE_UP_RIGHT));

    {
        // next(5) == 1 twice: the special roll misses and the act_random()
        // roll misses, so the search arm runs.
        FixedRandom miss_both_rolls(1u);
        ScopedSimStream scoped(&miss_both_rolls);
        ASSERT_TRUE(actor->act()) << "living::act's search arm returns 1";
    }

    ASSERT_EQ(1u, actor->stats()->commands.size())
        << "the search arm queues exactly one command";
    EXPECT_EQ(COMMAND_SEARCH, static_cast<int>(actor->stats()->commands.back().commandtype))
        << "and that command is COMMAND_SEARCH";
    EXPECT_EQ(300, static_cast<int>(actor->stats()->commands.back().commandcount))
        << "with living::act's own 300-tick budget, not act_random's 200";
    EXPECT_EQ(FACE_UP, static_cast<int>(actor->curdir()))
        << "the arm snaps facing to an even step: (enddir/2)*2";
    EXPECT_EQ(FACE_UP, static_cast<int>(actor->enddir()))
        << "and parks enddir there too, so no turn is pending";

    delete actor;
    delete enemy;
    world.delete_objects();
}


// walker::turn_undead: -1 when there is nothing in range, and otherwise the
// number of UNDEAD foes whose resistance roll lost. The roll is
// next(range*40) > next(level*10) off the world stream, so a floor stream
// spares everyone and a ceiling stream destroys every undead -- and the
// living orc standing between them is never a candidate either way.
TEST_F(WalkerSpecials, turn_undead_destroys_undead_foes_and_spares_the_living)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 3);
    ASSERT_NE(nullptr, cleric) << "cleric created";
    cleric->setxy(100, 100);

    ASSERT_EQ(-1, static_cast<int>(cleric->turn_undead(3, 1)))
        << "turn_undead returns -1 when no foe is in range at all";

    walker* skel = world.add_ob(Order::Living, FAMILY_SKELETON);
    walker* ghost = world.add_ob(Order::Living, FAMILY_GHOST);
    walker* orc = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, skel) << "skeleton created";
    ASSERT_NE(nullptr, ghost) << "ghost created";
    ASSERT_NE(nullptr, orc) << "orc created";
    for (walker* foe : {skel, ghost, orc})
    {
        foe->set_team_num(2);
        foe->stats()->set_level(1);
        foe->set_dead(0);
    }
    skel->setxy(static_cast<Sint32>(cleric->xpos() + 1), static_cast<Sint32>(cleric->ypos() + 1));
    ghost->setxy(static_cast<Sint32>(cleric->xpos() + 2), static_cast<Sint32>(cleric->ypos() + 1));
    orc->setxy(static_cast<Sint32>(cleric->xpos() + 3), static_cast<Sint32>(cleric->ypos() + 1));

    {
        // Every draw 0: `0 > 0` is false, so every undead resists.
        FixedRandom floor_rng(0u);
        ScopedSimStream scoped(&floor_rng);
        ASSERT_EQ(0, static_cast<int>(cleric->turn_undead(24, 2)))
            << "targets in range but every resistance roll held: 0 destroyed";
    }
    EXPECT_EQ(0, static_cast<int>(skel->dead())) << "a resisting skeleton survives";
    EXPECT_EQ(0, static_cast<int>(ghost->dead())) << "a resisting ghost survives";

    {
        // Every draw max-1: 959 > 9, so every undead is destroyed.
        CeilingRandom ceiling_rng;
        ScopedSimStream scoped(&ceiling_rng);
        ASSERT_EQ(2, static_cast<int>(cleric->turn_undead(24, 2)))
            << "both undead lose the roll; the orc is not undead";
    }
    EXPECT_EQ(1, static_cast<int>(skel->dead())) << "the skeleton is destroyed";
    EXPECT_FLOAT_EQ(0.0f, skel->stats()->hitpoints()) << "and zeroed out";
    EXPECT_EQ(1, static_cast<int>(ghost->dead())) << "the ghost is destroyed";
    EXPECT_FLOAT_EQ(0.0f, ghost->stats()->hitpoints()) << "and zeroed out";
    EXPECT_EQ(0, static_cast<int>(orc->dead()))
        << "turning is for the undead: the orc is untouched";

    delete cleric;
    world.delete_objects();
}


// core:faerie declares `specials = {}` (living-07-faerie.lua). Every
// undeclared slot is loaded at kSpecialCostDisabled (5000 MP), so a faerie
// with an ordinary pool is refused at the cost gate (NoMP) before any
// dispatch; hand it a pool it cannot possibly have in play and the dispatch
// itself runs, finds no hook and declines. Either way nothing is spent: the
// faerie's freezing sprinkle is a weapon, not a special.
TEST_F(WalkerSpecials, faerie_has_no_special_and_spends_no_magic)
{
    walker* w = make_special_guy(FAMILY_FAERIE);
    ASSERT_NE(nullptr, w) << "faerie created";
    w->set_current_special(1);
    EXPECT_EQ(static_cast<int>(kSpecialCostDisabled),
              static_cast<int>(w->stats()->special_cost(1)))
        << "an undeclared special slot keeps the disabled sentinel cost";

    const float mp_before = w->stats()->magicpoints();
    walker::SpecialFailure why = walker::SpecialFailure::None;
    ASSERT_FALSE(w->special(&why)) << "a family with no specials cannot cast one";
    EXPECT_EQ(walker::SpecialFailure::NoMP, why)
        << "the disabled sentinel cost puts the slot out of reach of any real pool";
    EXPECT_FLOAT_EQ(mp_before, w->stats()->magicpoints())
        << "a refused special spends no magic";

    w->stats()->set_max_magicpoints(kSpecialCostDisabled + 1000);
    w->stats()->set_magicpoints(kSpecialCostDisabled + 1000);
    const float rich_mp_before = w->stats()->magicpoints();
    why = walker::SpecialFailure::None;
    ASSERT_FALSE(w->special(&why)) << "past the cost gate there is still no cast to run";
    EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, why)
        << "the dispatch ran and declined; not Dead/NoStats/NotLiving";
    EXPECT_FLOAT_EQ(rich_mp_before, w->stats()->magicpoints())
        << "a declined special spends no magic either";

    delete w;
}


// Whole-registry sweep: every living family in the table below, every special
// slot 1..5, with and without the shifter, cast from one fixed world state
// with the world RNG re-seeded before each cast. `special()` returning true
// is the one contract every slot shares, so all ten outcomes are pinned per
// family as a bitmask (bit (slot-1)*2 + shifter). A slot that stops firing,
// or one that starts firing where the family declares no such special, flips
// a bit and names its family.
//
// This is a characterization pin over the whole registry, not a substitute
// for the per-special tests: what each cast DOES is asserted by the
// *_drives_simulation block further down, and those tests build their casters
// the way the game does. A zero row here means no slot fires from this bare
// add_ob caster -- because the family declares no specials at all (faerie,
// big orc, golem, giant skeleton, tower), because it spawns mid-animation
// (skeleton), or because its cast declines against this fixed neighbourhood
// (elf, barbarian, the small slimes). Those rows still carry the cost-gate
// assertion below, which is family-independent.
TEST_F(WalkerSpecials, family_special_sweep_outcomes_are_pinned)
{
    struct SweepCase
    {
        char family;
        const char* name;
        unsigned expected_mask;
    };

    static const SweepCase kSweep[] = {
        {FAMILY_SOLDIER, "soldier", 255u},        // slots 1-4, both shifters
        {FAMILY_ARCHER, "archer", 63u},           // slots 1-3, both shifters
        {FAMILY_CLERIC, "cleric", 2u},            // slot 1 shifted: mystic mace
        {FAMILY_MAGE, "mage", 829u},              // 1 unshifted, 2/3/5 both
        {FAMILY_ARCHMAGE, "archmage", 125u},      // 1 unshifted, 2/3 both, 4 unshifted
        {FAMILY_ELF, "elf", 0u},                  // rock volleys decline here (see below)
        {FAMILY_THIEF, "thief", 255u},            // slots 1-4, both shifters
        {FAMILY_SKELETON, "skeleton", 0u},        // spawns in ANI_SKEL_GROW: mid-teleport
        {FAMILY_FIREELEMENTAL, "fire elemental", 3u}, // slot 1, both shifters
        {FAMILY_FAERIE, "faerie", 0u},            // declares no specials at all
        {FAMILY_DRUID, "druid", 240u},            // slots 3-4, both shifters
        {FAMILY_ORC, "orc", 3u},                  // slot 1, both shifters
        {FAMILY_BARBARIAN, "barbarian", 0u},      // boulder declines here (see below)
        {FAMILY_GHOST, "ghost", 3u},              // slot 1, both shifters
        {FAMILY_SMALL_SLIME, "small slime", 0u},  // no room to grow beside the ally
        {FAMILY_MEDIUM_SLIME, "medium slime", 0u},// no room to grow beside the ally
        {FAMILY_SLIME, "slime", 3u},              // slot 1, both shifters
        {FAMILY_BIG_ORC, "big orc", 0u},          // declares no specials at all
        {FAMILY_GOLEM, "golem", 0u},              // declares no specials at all
        {FAMILY_GIANT_SKELETON, "giant skeleton", 0u}, // declares no specials at all
        {FAMILY_TOWER1, "tower", 0u},             // declares no specials at all
    };

    // The sweep builds its casters with add_ob, which does not load a level
    // on its own: without this the grid is invalid when the test runs first
    // in a filtered subset, and every passability probe answers differently.
    ensure_level_loaded();
    auto& world = og::runtime::current_session->myscreen_->world();

    for (const SweepCase& sweep : kSweep)
    {
        world.delete_objects();

        walker* caster = world.add_ob(Order::Living, sweep.family);
        walker* ally = world.add_ob(Order::Living, FAMILY_SOLDIER);
        walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
        walker* blood = world.add_fx_ob(Order::Treasure, FAMILY_STAIN);
        ASSERT_NE(nullptr, caster) << "sweep caster created for " << sweep.name;
        ASSERT_NE(nullptr, ally) << "sweep ally created for " << sweep.name;
        ASSERT_NE(nullptr, foe) << "sweep foe created for " << sweep.name;
        ASSERT_NE(nullptr, blood) << "sweep corpse created for " << sweep.name;

        caster->set_team_num(1);
        ally->set_team_num(1);
        foe->set_team_num(2);
        caster->setxy(100, 100);
        ally->setxy(104, 100);
        foe->setxy(112, 100);
        blood->setxy(106, 100);
        caster->set_lastx(caster->stepsize());
        caster->set_lasty(0);
        caster->stats()->set_max_magicpoints(1500);
        if (caster->myguy)
        {
            caster->myguy->intelligence = 180;
            caster->myguy->strength = 180;
            caster->myguy->constitution = 180;
            caster->myguy->teamnum = 1;
        }
        blood->stats()->set_old_family(FAMILY_SOLDIER);
        blood->set_team_num(1);

        unsigned mask = 0;
        for (int slot = 1; slot <= 5; ++slot)
        {
            for (int shift = 0; shift <= 1; ++shift)
            {
                world.rng_.state_ = 2468013579u; // one fixed stream per cast
                caster->set_current_special(static_cast<char>(slot));
                caster->set_busy(0);
                caster->set_shifter_down(static_cast<short>(shift));
                caster->stats()->set_magicpoints(caster->stats()->max_magicpoints());
                // (The "a cost-gate refusal spends nothing" rule used to be
                // asserted here under `why == NoMP`. It could not fail: every
                // one-line break of the gate ALSO stops `why` from being NoMP,
                // so the assertion simply stopped running. It is pinned
                // falsifiably in WalkerSpecials.no_magic instead.)
                if (caster->special())
                {
                    mask |= 1u << ((slot - 1) * 2 + shift);
                }
            }
        }
        EXPECT_EQ(sweep.expected_mask, mask)
            << "special() outcomes changed for family " << sweep.name
            << " (bit (slot-1)*2+shifter)";
    }

    world.delete_objects();
}


TEST_F(WalkerSpecials, ghost_scare_animates_and_dissipates)
{
    walker* w = make_special_guy(FAMILY_GHOST);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // scare
    ASSERT_TRUE(w->special()) << "scare should fire";

    auto find_scare_effects = []() {
        std::vector<walker*> result;
        for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
            walker* e = uptr.get();
            if (e && e->family() == FAMILY_GHOST_SCARE)
                result.push_back(e);
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
            walker* e = uptr.get();
            if (e && e->family() == FAMILY_GHOST_SCARE)
                result.push_back(e);
        }
        return result;
    };

    auto effects = find_scare_effects();
    ASSERT_EQ(effects.size(), 1u) << "exactly one ghost-scare effect should be spawned";
    walker* fx = effects[0];
    ASSERT_EQ(fx->ani_type(), ANI_SCARE) << "spawned effect should use ANI_SCARE animation";

    short max_cycle = fx->cycle();
    size_t max_seen = effects.size();
    for (int i = 0; i < 64 && fx->dead() == 0; ++i) {
        fx->act();
        if (fx->cycle() > max_cycle)
            max_cycle = fx->cycle();
        size_t now = find_scare_effects().size();
        if (now > max_seen)
            max_seen = now;
    }

    ASSERT_GT(max_cycle, 0) << "animation should advance frames";
    ASSERT_EQ(fx->dead(), 1) << "effect should be dead within 64 ticks";
    ASSERT_LE(max_seen, 1u) << "no additional ghost-scare effects should spawn";
}


TEST_F(WalkerSpecials, ghost_scare_does_not_accumulate)
{
    walker* w = make_special_guy(FAMILY_GHOST);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // scare
    ASSERT_TRUE(w->special()) << "first scare should fire";

    auto find_first_scare = []() -> walker* {
        for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
            walker* e = uptr.get();
            if (e && e->family() == FAMILY_GHOST_SCARE && !e->dead())
                return e;
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
            walker* e = uptr.get();
            if (e && e->family() == FAMILY_GHOST_SCARE && !e->dead())
                return e;
        }
        return nullptr;
    };

    walker* first = find_first_scare();
    ASSERT_TRUE(first != nullptr) << "first ghost-scare effect should exist";

    auto tick_world = []() {
        for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
            walker* e = uptr.get();
            if (e && !e->dead())
                e->act();
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
            walker* e = uptr.get();
            if (e && !e->dead())
                e->act();
        }
    };

    for (int i = 0; i < 32 && first->dead() == 0; ++i)
        tick_world();
    ASSERT_EQ(first->dead(), 1) << "first effect should be dead after ticking";

    ASSERT_TRUE(w->special()) << "second scare should fire";
    walker* second = find_first_scare();
    ASSERT_TRUE(second != nullptr) << "second ghost-scare effect should exist";
    ASSERT_NE(second, first) << "second effect should be a fresh entity";

    for (int i = 0; i < 32 && second->dead() == 0; ++i)
        tick_world();
    ASSERT_EQ(second->dead(), 1) << "second effect should also dissipate";

    int alive = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
        walker* e = uptr.get();
        if (e && e->family() == FAMILY_GHOST_SCARE && !e->dead())
            alive++;
    }
    for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
        walker* e = uptr.get();
        if (e && e->family() == FAMILY_GHOST_SCARE && !e->dead())
            alive++;
    }
    ASSERT_EQ(alive, 0) << "no live ghost-scare effects should remain";
}


// ---------------------------------------------------------------------------
// special() when dead, no stats, or not enough magic
// ---------------------------------------------------------------------------

TEST_F(WalkerSpecials, dead)
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_dead(1);
    bool result = w->special();
    ASSERT_TRUE(!result) << "dead walker should not special";
    w->set_dead(0); // so destructor works
    w->set_current_special(3);
    w->stats()->set_magicpoints(w->stats()->special_cost(3));

    walker* weapon = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
    if (weapon) {
        weapon->set_current_special(1);
        weapon->stats()->set_magicpoints(weapon->stats()->special_cost(1));
        ASSERT_TRUE(!weapon->special()) << "non-living special should fail early";
    }
}


TEST_F(WalkerSpecials, no_magic)
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->stats()->set_magicpoints(0);
    bool result = w->special();
    ASSERT_TRUE(!result) << "no magic should fail special";

    // The cost gate, pinned so a one-line break of it is visible: a caster one
    // point short of the SELECTED slot's cost is refused with NoMP -- before
    // any family dispatch -- and pays nothing. Deleting the gate's `return`
    // changes `why` (the cast runs on and declines, or succeeds), which is the
    // observable; the pool then also moves.
    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->set_current_special(4);
    w->stats()->set_special_cost(4, 50);
    w->stats()->set_magicpoints(49);
    walker::SpecialFailure why = walker::SpecialFailure::None;
    ASSERT_TRUE(!w->special(&why)) << "insufficient MP for the selected slot must fail";
    ASSERT_EQ(walker::SpecialFailure::NoMP, why)
        << "and must fail AT THE COST GATE, not somewhere downstream";
    ASSERT_FLOAT_EQ(49.0f, w->stats()->magicpoints())
        << "a cost-gate refusal runs no cast and spends nothing";

    // Exercise base-class fallback implementations explicitly.
    ASSERT_EQ(-1, (int)w->walker::shove(nullptr, static_cast<short>(0), static_cast<short>(0))) << "base shove should return -1";
    ASSERT_EQ(-1, (int)w->walker::shove(w, static_cast<short>(1), static_cast<short>(0))) << "base shove log path should still return -1";
    ASSERT_TRUE(!w->walker::eat_me(nullptr)) << "base eat_me fallback should return false";
    ASSERT_TRUE(w->walker::do_summon(1, 10) == nullptr) << "base do_summon fallback should return null";
    ASSERT_TRUE(!w->walker::check_special()) << "base check_special fallback should return false";

}


// ---------------------------------------------------------------------------
// death() - exercises order/family switches (lines 4422-4534)
// ---------------------------------------------------------------------------

// core:elemental's on_death is a FREE parting starburst: the hook un-kills
// the corpse, refunds the special's own cost, casts it (eight fireballs, one
// per compass direction, pre-paid the same way) and kills the corpse again.
// So a dying elemental ends dead with its magic pool exactly where it was
// (packs/core/families/living-06-elemental.lua on_death / do_special).
TEST_F(WalkerSpecials, elemental_death_fires_a_free_parting_starburst)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* w = make_special_guy(FAMILY_FIREELEMENTAL, 1);
    ASSERT_NE(nullptr, w) << "fire elemental created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(1);
    w->stats()->set_magicpoints(500.0f);
    const float mp_before = w->stats()->magicpoints();
    const int meteors_before = count_order_family(Order::Weapon, FAMILY_METEOR);

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs the family's on_death hook";

    EXPECT_EQ(meteors_before + 8, count_order_family(Order::Weapon, FAMILY_METEOR))
        << "the parting starburst releases one meteor per compass direction";
    EXPECT_EQ(1, static_cast<int>(w->dead()))
        << "the hook re-kills the corpse it un-killed to cast";
    EXPECT_FLOAT_EQ(mp_before, w->stats()->magicpoints())
        << "the parting shot is free: refunded before the cast, spent by it";

    delete w;
    world.delete_objects();
}


// A player character's death leaves the two things the score screen and a
// cleric each read: one life gem worth 37.5 % of the hero's heart value on
// the hero's team, and one bloodstain at the corpse remembering the family
// that fell. A generator's death is the other arm of the same switch:
// exactly four explosions, never one (src/gameplay/walker.cpp death()).
TEST_F(WalkerSpecials, character_death_drops_one_heart_and_one_stain)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    // Under SAVE_ALL a named team-my_team death ends the level and returns
    // before the bloodspot; this test is about the ordinary corpse. Permadeath
    // on (the default) keeps the gem at full legacy value.
    // The level is loaded once per BINARY, so both knobs are restored below:
    // leaving them clobbered would silently re-rule every later test.
    const char saved_world_type = world.type;
    const auto saved_keep_fallen_heroes = world.keep_fallen_heroes;
    world.type = static_cast<char>(world.type & ~SCEN_TYPE_SAVE_ALL);
    world.keep_fallen_heroes = 0;

    walker* w = make_special_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier created";
    ASSERT_NE(nullptr, w->myguy) << "should have myguy";
    w->setxy(100, 100);

    const int gems_before = count_order_family(Order::Treasure, FAMILY_LIFE_GEM);
    const int stains_before = count_order_family(Order::Treasure, FAMILY_STAIN);
    const float heart = static_cast<float>(w->myguy->query_heart_value());
    ASSERT_GT(heart, 0.0f) << "a levelled hero is worth something";
    const Sint32 corpse_x = static_cast<Sint32>(w->xpos());
    const Sint32 corpse_y = static_cast<Sint32>(w->ypos());

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs to completion";

    ASSERT_EQ(gems_before + 1, count_order_family(Order::Treasure, FAMILY_LIFE_GEM))
        << "a character death drops exactly one life gem";
    walker* gem = find_order_family(Order::Treasure, FAMILY_LIFE_GEM);
    ASSERT_NE(nullptr, gem) << "the life gem is in the world";
    EXPECT_FLOAT_EQ(heart * 0.375f, gem->stats()->hitpoints())
        << "the gem is 75 % of the heart value, halved because score doubles";
    EXPECT_EQ(static_cast<int>(w->team_num()), static_cast<int>(gem->team_num()))
        << "and it belongs to the fallen hero's team";

    ASSERT_EQ(stains_before + 1, count_order_family(Order::Treasure, FAMILY_STAIN))
        << "a soldier leaves exactly one bloodstain";
    walker* stain = find_order_family(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain) << "the bloodstain is in the world";
    EXPECT_EQ(FAMILY_SOLDIER, static_cast<int>(stain->stats()->old_family()))
        << "the stain remembers the family that fell, so a cleric can raise it";
    EXPECT_EQ(corpse_x, static_cast<Sint32>(stain->xpos())) << "it marks the corpse's x";
    EXPECT_EQ(corpse_y, static_cast<Sint32>(stain->ypos())) << "it marks the corpse's y";

    // Generators go up in flames: four explosions per death.
    walker* generator = world.add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_NE(nullptr, generator) << "generator created";
    const int fx_before = count_order_family(Order::FX, FAMILY_EXPLOSION);
    generator->set_dead(1);
    ASSERT_TRUE(generator->death()) << "generator death() runs";
    EXPECT_EQ(fx_before + 4, count_order_family(Order::FX, FAMILY_EXPLOSION))
        << "a dying generator spawns exactly four explosions";

    delete w;
    world.delete_objects();
    world.type = saved_world_type;
    world.keep_fallen_heroes = saved_keep_fallen_heroes;
}


// Archmage special 3 WITHOUT the shifter is the illusion summon, and which
// phantom appears is a pure function of the post-cost magic pool: under 100
// MP it is always an elf and no die is rolled at all, and each higher tier
// rolls one og.rand over a wider table (living-17-archmage.lua summon_image).
// og.rand draws from the WORLD stream -- a GameContext rng steers nothing
// here -- so every row below seeds world.rng_ with a state whose FIRST draw
// is the wanted table index.
TEST_F(WalkerSpecials, archmage_illusion_family_follows_the_magic_pool_tier)
{
    auto& world = og::runtime::current_session->myscreen_->world();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_NE(nullptr, arch) << "archmage created";
    world.delete_objects();

    arch->setxy(120, 120);
    arch->stats()->set_special_cost(3, 0);
    arch->stats()->set_max_magicpoints(2000);
    arch->set_current_special(static_cast<char>(3));
    arch->set_shifter_down(static_cast<short>(0));

    // The archmage itself is not in oblist (make_special_guy owns it), so
    // oblist holds exactly the phantom of the cast under test.
    auto only_ob = [&world]() -> walker* {
        for (auto& uptr : world.oblist)
            if (uptr)
                return uptr.get();
        return nullptr;
    };

    // Tier 0: below 100 MP the ladder short-circuits to the elf with no draw.
    arch->set_busy(0);
    arch->stats()->set_magicpoints(50.0f);
    world.rng_.state_ = 4242u;
    ASSERT_TRUE(arch->special()) << "the illusion summon fires";
    ASSERT_EQ(1u, world.oblist.size()) << "exactly one phantom per cast";
    ASSERT_NE(nullptr, only_ob()) << "the phantom is in oblist";
    EXPECT_EQ(FAMILY_ELF, static_cast<int>(only_ob()->family()))
        << "the sub-100 MP tier is always an elf";
    EXPECT_EQ(4242u, world.rng_.state_)
        << "and it costs no die roll: the world stream is untouched";
    world.remove_ob(only_ob());

    struct TierRow
    {
        float mp;
        std::uint32_t seed;
        int expected_family;
    };
    // (s * 1103515245 + 12345) >> 16, modulo the tier's table size, is the
    // index the cast reads; these seeds are that equation solved per index.
    const TierRow rows[] = {
        {120.0f, 7u, FAMILY_ELF},   {120.0f, 2u, FAMILY_SOLDIER},
        {120.0f, 1u, FAMILY_ARCHER},
        {300.0f, 3u, FAMILY_ELF},   {300.0f, 2u, FAMILY_SOLDIER},
        {300.0f, 4u, FAMILY_ARCHER}, {300.0f, 1u, FAMILY_ORC},
        {300.0f, 6u, FAMILY_SKELETON},
        {700.0f, 5u, FAMILY_ELF},   {700.0f, 8u, FAMILY_SOLDIER},
        {700.0f, 13u, FAMILY_ARCHER}, {700.0f, 1u, FAMILY_ORC},
        {700.0f, 4u, FAMILY_SKELETON}, {700.0f, 12u, FAMILY_DRUID},
        {700.0f, 2u, FAMILY_CLERIC},
        {1200.0f, 35u, FAMILY_ELF}, {1200.0f, 31u, FAMILY_SOLDIER},
        {1200.0f, 27u, FAMILY_ARCHER}, {1200.0f, 22u, FAMILY_ORC},
        {1200.0f, 15u, FAMILY_SKELETON}, {1200.0f, 11u, FAMILY_DRUID},
        {1200.0f, 7u, FAMILY_CLERIC}, {1200.0f, 2u, FAMILY_FIREELEMENTAL},
        {1200.0f, 1u, FAMILY_BIG_ORC},
    };

    for (const TierRow& row : rows)
    {
        SCOPED_TRACE(::testing::Message() << "mp=" << row.mp << " seed=" << row.seed);
        ASSERT_EQ(0u, world.oblist.size()) << "the previous phantom was removed";
        arch->set_busy(0);
        arch->stats()->set_magicpoints(row.mp);
        world.rng_.state_ = row.seed;
        ASSERT_TRUE(arch->special()) << "the illusion summon fires";
        ASSERT_EQ(1u, world.oblist.size()) << "exactly one phantom per cast";
        walker* phantom = only_ob();
        ASSERT_NE(nullptr, phantom) << "the phantom is in oblist";
        EXPECT_EQ(row.expected_family, static_cast<int>(phantom->family()))
            << "this tier's table maps this roll to this family";
        EXPECT_TRUE(phantom->summoned())
            << "a phantom is conjured ammunition, never a SAVE_ALL casualty";
        EXPECT_EQ(arch, phantom->owner()) << "and it belongs to its summoner";
        world.remove_ob(phantom);
    }

    delete arch;
    world.delete_objects();
}


// Mage special 1 with the shifter held is TELEPORT MARKER, and recasting it
// can never bank two markers: the marker the caster already owns is killed
// and a fresh one is centered on the caster, the removal and the placement
// costing 8 busy ticks each, with the new marker's uses read off the guy's
// Intelligence / 33 (packs/core/families/living-03-mage.lua teleport).
TEST_F(WalkerSpecials, mage_marker_recast_replaces_the_owned_marker)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* mage = make_special_guy(FAMILY_MAGE, 2, 6);
    ASSERT_NE(nullptr, mage) << "mage created";
    mage->setxy(100, 100);
    mage->set_current_special(1);
    mage->set_shifter_down(1);
    mage->set_user(0);
    mage->set_busy(0);
    ASSERT_NE(nullptr, mage->myguy) << "the marker path reads the guy's Intelligence";
    mage->myguy->intelligence = 120;

    walker* old_marker = world.add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, old_marker) << "marker created";
    old_marker->set_owner(mage);
    old_marker->set_dead(0);
    old_marker->setxy(static_cast<Sint32>(mage->xpos() + 8), static_cast<Sint32>(mage->ypos() + 8));

    ASSERT_TRUE(mage->special()) << "the marker recast fires";
    EXPECT_EQ(1, static_cast<int>(old_marker->dead()))
        << "the marker the caster already owned is removed";

    int live_markers = 0;
    walker* survivor = nullptr;
    for (auto& uptr : world.oblist)
    {
        if (uptr && uptr->order() == Order::FX && uptr->family() == FAMILY_MARKER
            && !uptr->dead() && uptr->owner() == mage)
        {
            live_markers++;
            survivor = uptr.get();
        }
    }
    ASSERT_EQ(1, live_markers) << "a mage never holds two live markers at once";
    ASSERT_NE(nullptr, survivor) << "the replacement marker exists";
    EXPECT_EQ(120 / 33, static_cast<int>(survivor->lifetime()))
        << "the new marker carries Intelligence / 33 uses";
    EXPECT_FLOAT_EQ(16.0f, mage->busy())
        << "8 busy ticks for the removal plus 8 for the placement";

    delete mage;
    world.delete_objects();
}


// Mage special 3 is FREEZE TIME, and enemy_freeze is relative to
// world.my_team: a mage that is NOT on the player's team banks NO global time
// stop at all. It hands min(base + per_level*level, cap) bonus rounds to its
// own side instead (living-03-mage.lua freeze_time).
TEST_F(WalkerSpecials, mage_freeze_time_off_team_grants_bonus_rounds_not_a_global_stop)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* mage = make_special_guy(FAMILY_MAGE, 3, 6);
    ASSERT_NE(nullptr, mage) << "mage created";
    mage->setxy(100, 100);
    mage->set_current_special(3);
    mage->set_shifter_down(0);
    mage->set_busy(0);
    mage->set_team_num(3);
    mage->clear_myguy();
    ASSERT_NE(3, static_cast<int>(world.my_team))
        << "the arm under test is the one where the caster is NOT the player's side";

    // find_friends_in_range scans oblist, so the ally has to BE in it.
    walker* ally = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, ally) << "ally created";
    ally->set_team_num(3);
    ally->stats()->set_level(2);
    ally->setxy(static_cast<Sint32>(mage->xpos() + 6), static_cast<Sint32>(mage->ypos() + 6));
    ally->set_bonus_rounds(0);

    world.enemy_freeze = 0;
    ASSERT_TRUE(mage->special()) << "freeze time fires";
    EXPECT_EQ(0, world.enemy_freeze)
        << "an off-team mage banks no global time stop";
    EXPECT_EQ(5 + 2 * 6, static_cast<int>(ally->bonus_rounds()))
        << "it grants bonus_rounds_base + per_level * level rounds to its own side";

    delete mage;
    world.delete_objects();
}


// Mage special 4 (ENERGY WAVE) converts one fired bolt into exactly one
// rolling wave; special 5 (HEARTBURST) detonates one explosion ON EACH foe in
// range and pays for every one of them out of the caster's pool, so the count
// scales with the targets and not with the cast
// (packs/core/families/living-03-mage.lua energy_wave / heartburst).
TEST_F(WalkerSpecials, mage_wave_is_one_and_heartburst_is_one_per_foe)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* mage = make_special_guy(FAMILY_MAGE, 1, 7);
    ASSERT_NE(nullptr, mage) << "mage created";
    mage->setxy(120, 120);
    mage->set_lastx(mage->stepsize());
    mage->set_lasty(0);
    mage->set_busy(0);

    // find_foes_in_range scans oblist, so the foes have to BE in it.
    walker* foe1 = world.add_ob(Order::Living, FAMILY_ORC);
    walker* foe2 = world.add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, foe1) << "first foe created";
    ASSERT_NE(nullptr, foe2) << "second foe created";
    for (walker* foe : {foe1, foe2})
    {
        foe->set_team_num(2);
        foe->stats()->set_level(3);
    }
    // Well inside heartburst's 80 + 2*level reach, but OFF the muzzle: a foe
    // standing where the bolt spawns turns walker::fire() into a melee swing
    // that returns no bolt at all, and energy_wave needs the bolt.
    foe1->setxy(110, 170);
    foe2->setxy(150, 175);

    mage->set_current_special(4);
    const int waves_before = count_order_family(Order::Weapon, FAMILY_WAVE);
    ASSERT_TRUE(mage->special()) << "energy wave fires";
    EXPECT_EQ(waves_before + 1, count_order_family(Order::Weapon, FAMILY_WAVE))
        << "one cast leaves exactly one wave behind";

    mage->set_current_special(5);
    mage->set_busy(0);
    mage->stats()->set_magicpoints(1200);
    mage->stats()->set_special_cost(5, 0);
    const int bursts_before = count_order_family(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(mage->special()) << "heartburst fires with two foes in range";
    EXPECT_EQ(bursts_before + 2, count_order_family(Order::FX, FAMILY_EXPLOSION))
        << "heartburst is one explosion per acquired foe, not one per cast";
    // mp_pool_damage(slot 5) = min((1200 - 0)/2, cap 600) = 600, split over the
    // two foes as 300 each and debited once per burst: 1200 - 2*300 = 600.
    EXPECT_FLOAT_EQ(600.0f, mage->stats()->magicpoints())
        << "each burst is paid for out of the caster's pool, at pool/foes each";
    EXPECT_FLOAT_EQ(5.0f, mage->busy()) << "heartburst costs the caster 5 busy ticks";

    delete mage;
    world.delete_objects();
}


// The archmage's four shifter-held arms, each pinned by what it leaves
// behind: special 1 is refused outright under 75 Int and spends nothing;
// with the Int to pay for it, it removes the marker the caster already owns
// and plants one fresh; special 2 is exactly one chain bolt; special 3 is
// exactly one fire elemental (packs/core/families/living-17-archmage.lua).
TEST_F(WalkerSpecials, archmage_marker_needs_75_int_then_chain_and_summon_are_one_each)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_NE(nullptr, arch) << "archmage created";
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(2);
    foe->stats()->set_level(2);

    arch->setxy(140, 140);
    foe->setxy(148, 140);
    for (int slot = 1; slot <= 4; ++slot)
        arch->stats()->set_special_cost(slot, 0);
    arch->stats()->set_max_magicpoints(1800);
    arch->stats()->set_magicpoints(1800);
    ASSERT_NE(nullptr, arch->myguy) << "the Int gate reads the guy";

    // 1. Marker with too little Intelligence: refused, nothing placed or spent.
    arch->set_current_special(1);
    arch->set_shifter_down(1);
    arch->set_busy(0);
    arch->set_user(0);
    arch->myguy->intelligence = 20;
    const float mp_before_refusal = arch->stats()->magicpoints();
    ASSERT_FALSE(arch->special()) << "under 75 Int the marker is refused";
    EXPECT_EQ(0, count_order_family(Order::FX, FAMILY_MARKER))
        << "a refused marker places nothing";
    EXPECT_FLOAT_EQ(mp_before_refusal, arch->stats()->magicpoints())
        << "a refused cast spends no magic";
    EXPECT_FLOAT_EQ(0.0f, arch->busy()) << "and costs no time";

    // 2. Same cast with the Int for it, over a marker the caster already owns.
    arch->myguy->intelligence = 220;
    walker* old_marker = world.add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, old_marker) << "arch marker created";
    old_marker->set_owner(arch);
    old_marker->set_dead(0);
    old_marker->setxy(static_cast<Sint32>(arch->xpos() + 8), static_cast<Sint32>(arch->ypos() + 8));
    arch->set_busy(0);
    ASSERT_TRUE(arch->special()) << "with 220 Int the marker is placed";
    EXPECT_EQ(1, static_cast<int>(old_marker->dead())) << "the old marker is removed";
    int live_markers = 0;
    walker* survivor = nullptr;
    for (auto& uptr : world.oblist)
    {
        if (uptr && uptr->order() == Order::FX && uptr->family() == FAMILY_MARKER
            && !uptr->dead() && uptr->owner() == arch)
        {
            live_markers++;
            survivor = uptr.get();
        }
    }
    ASSERT_EQ(1, live_markers) << "an archmage never holds two live markers";
    ASSERT_NE(nullptr, survivor) << "the replacement marker exists";
    EXPECT_EQ(220 / 33, static_cast<int>(survivor->lifetime()))
        << "the new marker carries Intelligence / 33 uses";
    EXPECT_FLOAT_EQ(16.0f, arch->busy())
        << "8 busy ticks for the removal plus 8 for the placement";

    // 3. Chain lightning: one bolt per cast.
    arch->set_busy(0);
    arch->set_current_special(2);
    arch->set_shifter_down(1);
    const int chains_before = count_order_family(Order::FX, FAMILY_CHAIN);
    ASSERT_TRUE(arch->special()) << "chain lightning fires with a foe in range";
    EXPECT_EQ(chains_before + 1, count_order_family(Order::FX, FAMILY_CHAIN))
        << "one cast is exactly one chain bolt";

    // 4. True summon: one elemental per cast.
    arch->set_busy(0);
    arch->set_current_special(3);
    arch->set_shifter_down(1);
    const int elementals_before = count_order_family(Order::Living, FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(arch->special()) << "the true summon fires";
    EXPECT_EQ(elementals_before + 1, count_order_family(Order::Living, FAMILY_FIREELEMENTAL))
        << "one cast summons exactly one fire elemental";

    delete arch;
    world.delete_objects();
}


TEST_F(WalkerSpecials, archmage_mind_control_stats_name_path)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 4, 7);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* foe2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* foe3 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, arch) << "archmage created";
    ASSERT_NE(nullptr, foe) << "first mind-control target created";
    ASSERT_NE(nullptr, foe2) << "second mind-control target created";
    ASSERT_NE(nullptr, foe3) << "third mind-control target created";

    arch->setxy(120, 120);
    foe->setxy(126, 120);
    foe2->setxy(130, 120);
    foe3->setxy(134, 120);
    foe->set_team_num(2);
    foe2->set_team_num(2);
    foe3->set_team_num(2);
    foe->stats()->set_level(1);
    foe2->stats()->set_level(1);
    foe3->stats()->set_level(1);
    arch->set_busy(0);
    arch->set_current_special(4);
    arch->set_shifter_down(0);
    arch->stats()->set_special_cost(4, 0);
    arch->stats()->set_magicpoints(80);
    arch->stats()->name = "ARCH-NPC";
    if (arch->myguy) {
        arch->clear_myguy();
    }
    foe->set_real_team_num(255);
    foe2->set_real_team_num(255);
    foe3->set_real_team_num(255);
    foe->set_charm_left((0));
    foe2->set_charm_left((0));
    foe3->set_charm_left((0));
    ASSERT_NE(nullptr, current_game->sim_events) << "the notification channel exists";
    current_game->sim_events->clear();

    {
        // Every draw mind_control makes is on the WORLD stream: og.rand(20)
        // for the resist (non-zero = proper control, not the berserk arm) and
        // compute_charm_duration's 25 + next(20 * level edge). A GameContext
        // rng reaches neither, so the seeded stream goes here.
        FixedRandom one_rng(1u);
        ScopedSimStream scoped(&one_rng);
        ASSERT_TRUE(arch->special()) << "mind control fires on three charmable foes";
    }

    // Budget = post-cost pool (80) + 10, and each target costs 10: all three
    // are controlled. The first is covered by the special's own cost, so the
    // spend is (3 - 1) * 10 = 20 out of 80.
    EXPECT_FLOAT_EQ(60.0f, arch->stats()->magicpoints())
        << "extra targets cost 10 MP each; the first is free";
    EXPECT_FLOAT_EQ(10.0f, arch->busy()) << "mind control costs 10 busy ticks";
    for (walker* target : {foe, foe2, foe3})
    {
        EXPECT_EQ(static_cast<int>(arch->team_num()), static_cast<int>(target->team_num()))
            << "a properly controlled foe joins the archmage's team";
        EXPECT_EQ(2, static_cast<int>(target->real_team_num()))
            << "and its old team is banked in real_team_num";
        // charm_duration(edge 7 - 1 = 6) = 25 + next(120) = 26, under the knee.
        EXPECT_EQ(26, static_cast<int>(target->charm_left()))
            << "with the level edge's exact charm duration";
        EXPECT_EQ(nullptr, target->foe())
            << "control clears the foe so the convert picks a new one";
    }

    // The path this test is named for: an archmage with NO guy is announced by
    // its statistics name, not by the generic family label.
    bool announced = false;
    for (const og::sim::Event& e : current_game->sim_events->events())
    {
        if (e.kind == og::sim::EventKind::Notification)
        {
            EXPECT_EQ(std::string("ARCH-NPC has controlled 3 men"), e.text)
                << "a guy-less caster is announced by its statistics name";
            announced = true;
        }
    }
    EXPECT_TRUE(announced) << "mind control announces its haul";

    delete arch;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


// Druid special 4 (PROTECTION) never mints a second ring for an ally that
// already owns one: a fresh circle is created only to read a full charge off
// it, that charge tops up the existing ring, and the fresh one is discarded
// dead (packs/core/families/living-13-druid.lua protection_circle).
TEST_F(WalkerSpecials, druid_protection_tops_up_an_existing_circle)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* druid = make_special_guy(FAMILY_DRUID, 1, 6);
    ASSERT_NE(nullptr, druid) << "druid created";
    druid->setxy(100, 100);
    druid->set_busy(0);
    druid->set_current_special(4);
    druid->stats()->set_magicpoints(2000);

    // find_friends_in_range scans oblist, and the cast is refused unless it
    // counts more than one friend there -- and the caster itself is NOT in
    // oblist, so two allies are the minimum. One of them already owns a ring
    // (the branch under test), the other does not (the control).
    walker* protected_ally = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* bare_ally = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, protected_ally) << "already-protected ally created";
    ASSERT_NE(nullptr, bare_ally) << "unprotected ally created";
    for (walker* ally : {protected_ally, bare_ally})
    {
        ally->set_team_num(1);
        ally->stats()->set_level(5);
    }
    protected_ally->setxy(108, 100);
    bare_ally->setxy(130, 100);

    walker* existing = world.add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_NE(nullptr, existing) << "existing protection created";
    existing->set_owner(protected_ally);
    existing->set_team_num(protected_ally->team_num());
    existing->set_dead(0);
    existing->setxy(static_cast<Sint32>(protected_ally->xpos()),
                    static_cast<Sint32>(protected_ally->ypos()));
    existing->stats()->set_hitpoints(5.0f);

    ASSERT_TRUE(druid->special()) << "protection fires with friends in range";

    int rings_on_protected = 0;
    int rings_on_bare = 0;
    int spent_circles = 0;
    walker* fresh = nullptr;
    for (auto& uptr : world.weaplist)
    {
        if (uptr == nullptr || uptr->order() != Order::Weapon
            || uptr->family() != FAMILY_CIRCLE_PROTECTION)
            continue;
        if (uptr->dead())
        {
            spent_circles++;
            fresh = uptr.get();
        }
        else if (uptr->owner() == protected_ally)
        {
            rings_on_protected++;
        }
        else if (uptr->owner() == bare_ally)
        {
            rings_on_bare++;
        }
    }
    ASSERT_EQ(1, rings_on_protected)
        << "the already-protected ally keeps exactly one live ring";
    EXPECT_EQ(0, static_cast<int>(existing->dead()))
        << "and the survivor is the ring it already had";
    EXPECT_EQ(1, rings_on_bare)
        << "the unprotected ally gets one ring of its own (the other branch)";
    ASSERT_EQ(1, spent_circles)
        << "the fresh circle is minted only to read a charge off it, then discarded";
    ASSERT_NE(nullptr, fresh) << "the discarded circle is still in weaplist";
    ASSERT_GT(fresh->stats()->hitpoints(), 0.0f) << "a fresh ring carries a charge";
    EXPECT_FLOAT_EQ(5.0f + fresh->stats()->hitpoints(), existing->stats()->hitpoints())
        << "the whole fresh charge is poured into the existing ring";

    delete druid;
    world.delete_objects();
}

// Two NPC (no-guy) casts, each pinned by the exact number it writes. Orc
// special 2 (EAT CORPSE) heals corpse.level * corpse_heal_per_level and eats
// the stain; barbarian special 2 hurls one boulder carrying the legacy 5000
// "explode on impact" sentinel, at 1 + slot*5 busy
// (living-14-orc.lua eat_corpse; living-16-barbarian.lua do_special).
TEST_F(WalkerSpecials, npc_orc_eats_a_corpse_and_npc_barbarian_arms_its_boulder)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* orc = make_special_guy(FAMILY_ORC, 2, 4);
    ASSERT_NE(nullptr, orc) << "orc created";
    orc->clear_myguy();
    orc->stats()->name = "ORC-NPC";
    orc->stats()->set_hitpoints(10);
    orc->stats()->set_max_hitpoints(100);
    orc->set_current_special(2);
    orc->set_busy(0);

    walker* blood = world.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, blood) << "blood created";
    blood->set_team_num(3);
    blood->stats()->set_level(3);
    blood->set_dead(0);
    blood->setxy(static_cast<Sint32>(orc->xpos() + 1), static_cast<Sint32>(orc->ypos() + 1));

    ASSERT_TRUE(orc->special()) << "a hungry orc beside a corpse eats it";
    // corpse_heal_per_level is 5 (living-14-orc.lua tuning), level 3 corpse.
    EXPECT_FLOAT_EQ(10.0f + 3.0f * 5.0f, orc->stats()->hitpoints())
        << "the meal is worth corpse level * corpse_heal_per_level, under the cap";
    EXPECT_EQ(1, static_cast<int>(blood->dead())) << "and the corpse is consumed";

    walker* barb = make_special_guy(FAMILY_BARBARIAN, 2, 5);
    ASSERT_NE(nullptr, barb) << "barbarian created";
    barb->clear_myguy();
    barb->setxy(120, 120);
    barb->set_lastx(barb->stepsize());
    barb->set_lasty(0);
    barb->set_busy(0);
    barb->set_current_special(2);
    const int boulders_before = count_order_family(Order::Weapon, FAMILY_BOULDER);
    ASSERT_TRUE(barb->special()) << "the exploding boulder is hurled";
    EXPECT_EQ(boulders_before + 1, count_order_family(Order::Weapon, FAMILY_BOULDER))
        << "one cast adds exactly one boulder to weaplist";
    walker* boulder = find_order_family(Order::Weapon, FAMILY_BOULDER);
    ASSERT_NE(nullptr, boulder) << "the boulder is in the world";
    EXPECT_EQ(barb, boulder->owner()) << "and it belongs to the thrower";
    EXPECT_EQ(5000, static_cast<int>(boulder->skip_exit()))
        << "slot 2 arms the legacy explode-on-impact sentinel";
    EXPECT_FLOAT_EQ(11.0f, barb->busy()) << "busy += 1 + slot * 5 == 11 for slot 2";

    delete barb;
    delete orc;
    world.delete_objects();
}


TEST_F(WalkerSpecials, cleric_mystic_mace_low_int_and_success_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 8);
    ASSERT_NE(nullptr, cleric) << "cleric created";
    ASSERT_NE(nullptr, cleric->myguy) << "the Int gate reads the guy";

    cleric->set_current_special(1);
    cleric->set_shifter_down(1);
    cleric->stats()->set_special_cost(1, 0);
    cleric->set_busy(0);
    cleric->set_user(0);

    cleric->myguy->intelligence = 40;
    const int shields_before = count_order_family(Order::FX, FAMILY_MAGIC_SHIELD);
    const float mp_before = cleric->stats()->magicpoints();
    ASSERT_FLOAT_EQ(500.0f, mp_before) << "fixture precondition: the pool the maths below use";
    ASSERT_FALSE(cleric->special()) << "under the Int requirement the mace is refused";
    ASSERT_EQ(shields_before, count_order_family(Order::FX, FAMILY_MAGIC_SHIELD))
        << "a refused mace summons nothing";
    ASSERT_FLOAT_EQ(mp_before, cleric->stats()->magicpoints())
        << "and spends nothing";

    cleric->myguy->intelligence = 120;
    cleric->set_busy(0);
    ASSERT_TRUE(cleric->special()) << "with the Int to pay for it the mace is summoned";
    ASSERT_EQ(shields_before + 1, count_order_family(Order::FX, FAMILY_MAGIC_SHIELD))
        << "exactly one mace per cast";
    walker* mace = find_order_family(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, mace) << "the mace is in the world";
    EXPECT_EQ(cleric, mace->owner()) << "and belongs to its caster";
    // spare = (500 MP - 0 cost) / 2 = 250: the mace's surcharge, its extra
    // lifetime (100 + 250 = 350, under the 468 cap) and its extra charge.
    EXPECT_EQ(350, static_cast<int>(mace->lifetime()))
        << "mace_life_base + half the spare pool";
    EXPECT_FLOAT_EQ(250.0f, cleric->stats()->magicpoints())
        << "the mace surcharge is half the post-cost pool";
    EXPECT_FLOAT_EQ(5.0f, cleric->busy()) << "and it costs 5 busy ticks";

    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, elf_rock_barrage_level4_deterministic_spread)
{
    const auto first = run_elf_barrage(123u, 4);
    const auto second = run_elf_barrage(123u, 4);
    const auto third = run_elf_barrage(124u, 4);

    ASSERT_EQ(first.size(), 4u);
    ASSERT_EQ(first.size(), second.size());
    ASSERT_EQ(first.size(), third.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_FLOAT_EQ(first[i].x, second[i].x);
        EXPECT_FLOAT_EQ(first[i].y, second[i].y);
    }

    EXPECT_NEAR(7.2965625f, first[0].x, 0.0001f);
    EXPECT_NEAR(0.0f, first[0].y, 0.0001f);
    EXPECT_NEAR(7.89046875f, first[1].x, 0.0001f);
    EXPECT_NEAR(-0.98f, first[1].y, 0.0001f);
    EXPECT_NEAR(5.68453125f, first[2].x, 0.0001f);
    EXPECT_NEAR(-0.868f, first[2].y, 0.0001f);
    EXPECT_NEAR(6.8440625f, first[3].x, 0.0001f);
    EXPECT_NEAR(-0.816f, first[3].y, 0.0001f);

    EXPECT_NEAR(5.68453125f, third[0].x, 0.0001f);
    EXPECT_NEAR(1.696f, third[0].y, 0.0001f);
    EXPECT_NEAR(7.8621875f, third[1].x, 0.0001f);
    EXPECT_NEAR(0.0f, third[1].y, 0.0001f);
    EXPECT_NEAR(7.32484375f, third[2].x, 0.0001f);
    EXPECT_NEAR(-1.06f, third[2].y, 0.0001f);
    EXPECT_NEAR(6.70265625f, third[3].x, 0.0001f);
    EXPECT_NEAR(0.964f, third[3].y, 0.0001f);
}


// living::act's two remaining AI arms, each pinned by what it queues.
// ACT_GUARD hands off to act_guard(), which acquires the nearest foe, faces
// it, wakes a non-hold-post guard to ACT_RANDOM and queues one directed
// COMMAND_FIRE -- then falls out of the switch, so act() itself reports 0.
// ACT_RANDOM's 1-in-5 special roll with a pool too small for special 1 does
// NOT cast: it drops into act_random(), which with no foe anywhere queues a
// 40-tick random walk and returns 1 (src/gameplay/living.cpp act/act_random,
// src/gameplay/walker.cpp act_guard). turn_undead's own counts are pinned in
// WalkerSpecials.turn_undead_destroys_undead_foes_and_spares_the_living.
TEST_F(WalkerSpecials, act_guard_and_low_magic_act_random_pin_what_they_queue)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* guard = make_special_guy(FAMILY_ORC, 3, 4);
    ASSERT_NE(nullptr, guard) << "guard created";
    // find_near_foe scans oblist, so the foe has to BE in it.
    walker* foe = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(2);
    foe->stats()->set_level(2);
    guard->setxy(140, 140);
    foe->setxy(160, 140);
    guard->set_foe(nullptr);
    guard->set_act_type(ACT_GUARD);
    guard->set_guard_hold_post(false);
    // The pre-switch state living::act needs: nothing animating, nothing
    // queued, no pending turn, not busy, not frozen.
    guard->stats()->clear_command();
    guard->stats()->set_frozen_delay(0);
    guard->set_ani_type(ANI_WALK);
    guard->set_busy(0);
    guard->set_curdir(static_cast<signed char>(FACE_UP));
    guard->set_enddir(static_cast<char>(FACE_UP));

    {
        // The only draw on this path is act_guard's COMMAND_FIRE count.
        FixedRandom one_rng(1u);
        ScopedSimStream scoped(&one_rng);
        ASSERT_FALSE(guard->act())
            << "the ACT_GUARD arm breaks out of the switch, so act() returns 0";
    }
    ASSERT_EQ(foe, guard->foe()) << "act_guard acquires the nearest foe";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(guard->curdir()))
        << "and turns to face it (face_delta, not a bare curdir write)";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(guard->enddir()))
        << "parking enddir there too, so the facing survives the next tick";
    ASSERT_EQ(1u, guard->stats()->commands.size())
        << "a sighted guard queues exactly one command";
    EXPECT_EQ(COMMAND_FIRE, static_cast<int>(guard->stats()->commands.back().commandtype))
        << "and that command is the directed parting shot";
    EXPECT_EQ(ACT_RANDOM, static_cast<int>(guard->act_type()))
        << "a genuine sighting wakes a non-hold-post guard";

    delete guard;
    world.delete_objects();

    walker* randomer = make_special_guy(FAMILY_ORC, 5, 4);
    ASSERT_NE(nullptr, randomer) << "randomer created";
    randomer->setxy(160, 160);
    randomer->set_foe(nullptr);
    randomer->set_act_type(ACT_RANDOM);
    randomer->stats()->clear_command();
    randomer->stats()->set_frozen_delay(0);
    randomer->set_ani_type(ANI_WALK);
    randomer->set_busy(0);
    // Too poor to cast: the 1-in-5 special roll wins but the magic gate sends
    // the tick to act_random() instead. Nothing regenerates it mid-tick.
    randomer->stats()->set_magicpoints(0.0f);
    randomer->stats()->set_magic_per_round(0.0f);
    ASSERT_GT(static_cast<int>(randomer->stats()->special_cost(1)), 0)
        << "special 1 costs something, so an empty pool cannot pay for it";

    {
        // Every draw 0: next(5) == 0 picks the special arm, and inside
        // act_random next(80) == 0 re-acquires (finding nothing).
        FixedRandom floor_rng(0u);
        ScopedSimStream scoped(&floor_rng);
        ASSERT_TRUE(randomer->act()) << "the low-magic ACT_RANDOM arm returns 1";
    }
    ASSERT_EQ(1u, randomer->stats()->commands.size())
        << "act_random with no foe queues exactly one command";
    EXPECT_EQ(COMMAND_WALK, static_cast<int>(randomer->stats()->commands.back().commandtype))
        << "try_command(COMMAND_RANDOM_WALK) lands as a directed COMMAND_WALK";
    EXPECT_EQ(40, static_cast<int>(randomer->stats()->commands.back().commandcount))
        << "with act_random's own 40-tick budget, not living::act's 300";
    EXPECT_EQ(nullptr, randomer->foe()) << "and it found no foe to chase";

    delete randomer;
    world.delete_objects();
}


TEST_F(WalkerSpecials, guard_paths_and_teleport_failures)
{
    walker* w = make_special_guy(FAMILY_MAGE, 0, 4);
    ASSERT_NE(nullptr, w) << "mage created";

    // dead guard
    w->set_dead(1);
    ASSERT_TRUE(!w->special()) << "dead walker special should fail";
    w->set_dead(0);

    // magic cost guard
    w->set_current_special(1);
    w->stats()->set_special_cost(1, 50);
    w->stats()->set_magicpoints(0);
    ASSERT_TRUE(!w->special()) << "insufficient MP special should fail";

    // order guard: exercise the base walker path directly. `living::query_order()`
    // always reports Order::Living, so mutating a living's stored order does not
    // cover the non-living early return deterministically.
    walker non_living_special;
    non_living_special.set_current_special(1);
    non_living_special.stats()->set_special_cost(1, 0);
    non_living_special.stats()->set_magicpoints(500);
    non_living_special.set_order_family(Order::FX, FAMILY_MARKER);
    ASSERT_TRUE(!non_living_special.special()) << "non-living special should fail";

    // teleport_ranged failure branch
    w->setxy(-200, -200);
    ASSERT_TRUE(!w->teleport_ranged(0)) << "teleport_ranged should fail when no passable destination exists";

    // turn_undead no-target branch
    ASSERT_EQ(-1, (int)w->turn_undead(10, 1)) << "turn_undead should return -1 with no foes in range";

    delete w;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, no_stats_guard)
{
    NoStatsWalker no_stats;
    ASSERT_TRUE(!no_stats.special()) << "special should fail safely when stats are missing";
}


TEST_F(WalkerSpecials, unknown_family_and_teleport_ranged_fail_loop)
{
    walker* w = make_special_guy(FAMILY_MAGE, 0, 4);
    ASSERT_NE(nullptr, w) << "walker created";

    // special(): living + enough MP but missing descriptor callback -> return tail.
    w->set_order_family(Order::Living, 120);
    w->set_current_special(1);
    w->stats()->set_special_cost(1, 0);
    w->stats()->set_magicpoints(100);
    ASSERT_TRUE(!w->special()) << "unknown living family special should fall through and return false";

    // teleport_ranged(): exhaust keep_going loop and hit explicit false return.
    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->setxy(-1000, -1000);
    ASSERT_TRUE(!w->teleport_ranged(1)) << "teleport_ranged should fail after retries on invalid area";

    delete w;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, success_returns_true_and_spends_mp)
{
    walker* w = make_special_guy(FAMILY_MAGE, 0, 4);
    ASSERT_NE(nullptr, w) << "walker created";

    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->set_current_special(1); // teleport
    w->stats()->set_special_cost(1, 7);
    w->stats()->set_magicpoints(50);

    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker) << "teleport marker created";

    marker->set_owner(w);
    marker->set_dead(0);
    marker->set_lifetime(1);

    Sint32 mx = std::min<Sint32>(w->xpos() + 96, og::runtime::current_session->myscreen_->world().pixmaxx - w->sizex() - 2);
    Sint32 my = std::min<Sint32>(w->ypos() + 96, og::runtime::current_session->myscreen_->world().pixmaxy - w->sizey() - 2);
    if (!og::runtime::current_session->myscreen_->world().query_passable(static_cast<float>(mx), static_cast<float>(my), w))
    {
        bool found = false;
        for (Sint32 x = 0; x < og::runtime::current_session->myscreen_->world().pixmaxx - w->sizex() - 2 && !found; x += GRID_SIZE)
        {
            for (Sint32 y = 0; y < og::runtime::current_session->myscreen_->world().pixmaxy - w->sizey() - 2; y += GRID_SIZE)
            {
                const Sint32 dx = x - w->xpos();
                const Sint32 dy = y - w->ypos();
                if (dx * dx + dy * dy <= 64 * 64)
                    continue;
                if (og::runtime::current_session->myscreen_->world().query_passable(static_cast<float>(x), static_cast<float>(y), w))
                {
                    mx = x;
                    my = y;
                    found = true;
                    break;
                }
            }
        }
    }
    marker->setxy(mx, my);

    const float mp_before = w->stats()->magicpoints();
    ASSERT_TRUE(w->special()) << "successful special should return true";
    ASSERT_TRUE(w->stats()->magicpoints() == mp_before - w->stats()->special_cost(1)) << "successful special should spend configured MP cost";

    delete w;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


// ---------------------------------------------------------------------------
// Phase 2: simulation-driving regression tests for every special.
// One TEST_F per (family, current_special) pair from the audit table, each
// driving the world simulation forward and asserting at least one observable
// post-condition beyond construction.
// ---------------------------------------------------------------------------

static void tick_world(int n)
{
    for (int i = 0; i < n; ++i)
    {
        for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
        {
            walker* e = uptr.get();
            if (e && !e->dead())
                e->act();
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist)
        {
            walker* e = uptr.get();
            if (e && !e->dead())
                e->act();
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().weaplist)
        {
            walker* e = uptr.get();
            if (e && !e->dead())
                e->act();
        }
    }
}

// grow_into needs all EIGHT neighbouring cells clear, and the stock level's
// (100,100) has only six -- the scenery around the spawn eats two. Walk the
// map for a cell with real room and leave the walker there.
static bool place_with_room_to_grow(walker* w)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    for (Sint32 x = 2 * GRID_SIZE; x + 2 * w->sizex() < world.pixmaxx; x += GRID_SIZE)
    {
        for (Sint32 y = 2 * GRID_SIZE; y + 2 * w->sizey() < world.pixmaxy; y += GRID_SIZE)
        {
            w->setxy(x, y);
            if (w->spaces_clear() > 7)
                return true;
        }
    }
    return false;
}

static int count_family_in_weaplist(char family)
{
    int c = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().weaplist)
    {
        walker* w = uptr.get();
        if (w && w->family() == family)
            c++;
    }
    return c;
}


TEST_F(WalkerSpecials, soldier_charge_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(1);
    const int cmds_before = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(w->special()) << "soldier charge should fire when forward is clear";
    ASSERT_EQ(cmds_before + 1, static_cast<int>(w->stats()->commands.size()))
        << "charge enqueues exactly one order";
    EXPECT_EQ(COMMAND_RUSH, static_cast<int>(w->stats()->commands.back().commandtype))
        << "and that order is the rush";
    EXPECT_EQ(3, static_cast<int>(w->stats()->commands.back().commandcount))
        << "for the pack's three rush ticks";
    EXPECT_EQ(1, static_cast<int>(w->stats()->commands.back().com1))
        << "carrying lastx / stepsize as the x step";
    EXPECT_EQ(0, static_cast<int>(w->stats()->commands.back().com2))
        << "and lasty / stepsize as the y step";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, soldier_boomerang_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_busy(0);
    w->set_current_special(2);
    const int before = count_order_family(Order::FX, FAMILY_BOOMERANG);
    ASSERT_TRUE(w->special()) << "soldier boomerang should fire";
    ASSERT_EQ(before + 1, count_order_family(Order::FX, FAMILY_BOOMERANG))
        << "one cast throws exactly one boomerang";
    walker* blade = find_order_family(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, blade) << "the boomerang is in the world";
    EXPECT_EQ(w, blade->owner()) << "and belongs to the thrower";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, soldier_whirlwind_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(3);
    const int cmds_before = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(w->special()) << "whirlwind should fire";
    EXPECT_FLOAT_EQ(8.0f, w->busy()) << "whirlwind costs the caster 8 busy ticks";
    ASSERT_EQ(cmds_before + 8, static_cast<int>(w->stats()->commands.size()))
        << "whirlwind queues one walk per compass direction -- eight, not some";
    int walk_orders = 0;
    for (const auto& c : w->stats()->commands)
    {
        if (static_cast<int>(c.commandtype) == COMMAND_WALK)
            walk_orders++;
    }
    EXPECT_EQ(8, walk_orders) << "and every one of them is a walk";
    tick_world(48);
    delete w;
}


// Soldier special 4 (DISARM) needs busy == 0 AND something blocking the way
// forward. With caster level 4 against a level-1 foe, og.rand(foe.level) is
// always 0, so the roll always wins and the foe eats
// 6 * (self.level - foe.level + 1) == 24 busy while the caster eats 5.
// With nothing in front, the cast is refused before any roll
// (packs/core/families/living-00-soldier.lua disarm).
TEST_F(WalkerSpecials, soldier_disarm_busies_the_foe_it_disarms)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* w = make_special_guy(FAMILY_SOLDIER, 1, 4);
    ASSERT_NE(nullptr, w) << "walker created";
    w->setxy(100, 100);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(4);

    // forward_blocked() probes our own bounding box one pixel ahead, so the
    // foe has to overlap THAT box, not merely stand a pixel clear of it.
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(2);
    foe->stats()->set_level(1);
    foe->setxy(static_cast<Sint32>(w->xpos() + w->sizex() - 4), static_cast<Sint32>(w->ypos()));
    foe->set_busy(0);
    ASSERT_TRUE(w->stats()->forward_blocked())
        << "the arm under test is the one with an enemy blocking the way forward";

    ASSERT_TRUE(w->special()) << "disarm fires with a frontal enemy";
    EXPECT_FLOAT_EQ(24.0f, foe->busy())
        << "the winning roll adds 6 * (level diff + 1) busy to the foe";
    EXPECT_FLOAT_EQ(5.0f, w->busy()) << "and 5 busy to the caster";

    delete w;
    world.delete_objects();

    // Decline arm: nothing in front, so nothing is rolled and nothing spent.
    walker* lone = make_special_guy(FAMILY_SOLDIER, 1, 4);
    ASSERT_NE(nullptr, lone) << "lone fighter created";
    lone->setxy(100, 100);
    lone->set_curdir(static_cast<signed char>(FACE_RIGHT));
    lone->set_lastx(lone->stepsize());
    lone->set_lasty(0);
    lone->set_busy(0);
    lone->set_current_special(4);
    ASSERT_FALSE(lone->stats()->forward_blocked()) << "the way forward is clear";
    const float mp_before = lone->stats()->magicpoints();
    ASSERT_FALSE(lone->special()) << "disarm needs something blocking the way forward";
    EXPECT_FLOAT_EQ(mp_before, lone->stats()->magicpoints())
        << "a refused cast spends no magic";
    EXPECT_FLOAT_EQ(0.0f, lone->busy()) << "and no time";

    delete lone;
    world.delete_objects();
}


TEST_F(WalkerSpecials, archer_fire_arrows_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(1);
    const int cmds_before = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(w->special()) << "archer fire arrows should issue commands";
    // SET_WEAPON + one QUICK_FIRE per compass direction + RESET_WEAPON.
    ASSERT_EQ(cmds_before + 10, static_cast<int>(w->stats()->commands.size()))
        << "fire arrows queues exactly ten orders";
    ASSERT_EQ(0, cmds_before) << "fixture precondition: the queue started empty";
    std::vector<int> order_types;
    for (const auto& c : w->stats()->commands)
        order_types.push_back(static_cast<int>(c.commandtype));
    const std::vector<int> expected_order_types = {
        COMMAND_SET_WEAPON,  // swap to the fire arrow
        COMMAND_QUICK_FIRE, COMMAND_QUICK_FIRE, COMMAND_QUICK_FIRE,
        COMMAND_QUICK_FIRE, COMMAND_QUICK_FIRE, COMMAND_QUICK_FIRE,
        COMMAND_QUICK_FIRE, COMMAND_QUICK_FIRE,  // one per compass direction
        COMMAND_RESET_WEAPON  // and put the old weapon back
    };
    EXPECT_EQ(expected_order_types, order_types)
        << "the volley is swap, eight quick-fires, swap back -- in that order";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archer_barrage_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(2);
    const int arrows_before = count_family_in_weaplist(FAMILY_ARROW);
    ASSERT_TRUE(w->special()) << "archer barrage should fire";
    ASSERT_EQ(arrows_before + 3, count_family_in_weaplist(FAMILY_ARROW))
        << "a barrage is exactly three arrows, never one";
    EXPECT_FLOAT_EQ(w->fire_frequency() * 2.0f, w->busy())
        << "and it costs two fire cycles of busy";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archer_exploding_bolt_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(3);
    const int bolts_before = count_family_in_weaplist(FAMILY_FIRE_ARROW);
    ASSERT_TRUE(w->special()) << "exploding bolt should fire";
    ASSERT_EQ(bolts_before + 1, count_family_in_weaplist(FAMILY_FIRE_ARROW))
        << "an exploding bolt is exactly one fire arrow";
    walker* bolt = find_order_family(Order::Weapon, FAMILY_FIRE_ARROW);
    ASSERT_NE(nullptr, bolt) << "the bolt is in the world";
    EXPECT_EQ(5000, static_cast<int>(bolt->skip_exit()))
        << "carrying the explode-on-impact sentinel that makes it 'exploding'";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, mage_teleport_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_MAGE, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(1);
    w->set_shifter_down(0);
    const Sint32 orig_x = w->xpos();
    const Sint32 orig_y = w->ypos();
    ASSERT_TRUE(w->special()) << "mage teleport should fire";
    ASSERT_EQ(w->ani_type(), ANI_TELE_OUT) << "mage teleport (no marker) should set ani_type to ANI_TELE_OUT";
    bool moved = false;
    for (int i = 0; i < 64 && !moved; ++i)
    {
        w->act();
        if (w->xpos() != orig_x || w->ypos() != orig_y)
            moved = true;
    }
    tick_world(8);
    ASSERT_TRUE(moved) << "mage teleport should change position after animation completes";
    delete w;
}


TEST_F(WalkerSpecials, mage_warp_starburst_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_MAGE, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(2);
    w->stats()->set_magicpoints(1500);
    const int fbs_before = count_family_in_weaplist(FAMILY_FIREBALL);
    ASSERT_TRUE(w->special()) << "mage warp/starburst should fire";
    ASSERT_EQ(fbs_before + 8, count_family_in_weaplist(FAMILY_FIREBALL))
        << "a starburst is one fireball per compass direction -- eight, not some";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, mage_freeze_time_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_MAGE, 0, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(3);
    og::runtime::current_session->myscreen_->world().enemy_freeze = 0;
    ASSERT_EQ(4, static_cast<int>(w->stats()->level()))
        << "fixture precondition: the bank below is this level's value";
    ASSERT_TRUE(w->special()) << "mage freeze time should fire (player path)";
    // freeze_base 20 + freeze_per_level 11 * level 4 == 64, under the 300 bank cap.
    ASSERT_EQ(64, static_cast<int>(og::runtime::current_session->myscreen_->world().enemy_freeze))
        << "an on-team mage banks exactly 20 + 11 * level of global time-stop";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, mage_energy_wave_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_MAGE, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(4);
    const int waves_before = count_family_in_weaplist(FAMILY_WAVE);
    ASSERT_TRUE(w->special()) << "mage energy wave should fire";
    ASSERT_EQ(waves_before + 1, count_family_in_weaplist(FAMILY_WAVE))
        << "one cast leaves exactly one wave behind";
    walker* wave = find_order_family(Order::Weapon, FAMILY_WAVE);
    ASSERT_NE(nullptr, wave) << "the wave is in the world";
    EXPECT_EQ(w, wave->owner()) << "and belongs to the caster";
    EXPECT_EQ(static_cast<int>(w->stats()->level()), static_cast<int>(wave->stats()->level()))
        << "carrying the caster's level";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, mage_heartburst_drives_simulation)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    walker* w = make_special_guy(FAMILY_MAGE, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    // One burst per acquired foe, so the neighbourhood has to hold exactly the
    // foe this test puts in it.
    world.delete_objects();
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(5);
    w->stats()->set_magicpoints(1500);
    w->stats()->set_special_cost(5, 0);

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->set_team_num(2);
    foe->setxy(w->xpos() + 8, w->ypos() + 8);
    foe->stats()->set_level(1);

    const int explosions_before = count_order_family(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(w->special()) << "mage heartburst should fire when foes are present";
    ASSERT_EQ(explosions_before + 1, count_order_family(Order::FX, FAMILY_EXPLOSION))
        << "heartburst is one explosion per acquired foe: one foe, one burst";
    // mp_pool_damage(slot 5) = min((1500 - 0)/2, cap 600) = 600, split over the
    // single foe and debited once: 1500 - 600 = 900.
    EXPECT_FLOAT_EQ(900.0f, w->stats()->magicpoints())
        << "and the whole pool is spent on that one burst";
    EXPECT_FLOAT_EQ(5.0f, w->busy()) << "heartburst costs the caster 5 busy ticks";
    tick_world(48);
    delete w;
    world.delete_objects();
}


TEST_F(WalkerSpecials, skeleton_tunnel_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SKELETON, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_ani_type(ANI_WALK); // skeletons spawn with ANI_SKEL_GROW; tunnel only fires from a normal state
    w->set_current_special(1);
    const Sint32 orig_x = w->xpos();
    const Sint32 orig_y = w->ypos();
    ASSERT_TRUE(w->special()) << "skeleton tunnel should fire";
    ASSERT_EQ(w->ani_type(), ANI_TELE_OUT) << "tunnel should set ani_type to ANI_TELE_OUT";
    bool moved = false;
    for (int i = 0; i < 64 && !moved; ++i)
    {
        w->act();
        if (w->xpos() != orig_x || w->ypos() != orig_y)
            moved = true;
    }
    ASSERT_TRUE(moved) << "skeleton tunnel should change position";
    tick_world(8);
    delete w;
}


TEST_F(WalkerSpecials, cleric_heal_drives_simulation)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    walker* w = make_special_guy(FAMILY_CLERIC, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    // The heal walks every wounded friend in range, so the neighbourhood has
    // to hold exactly the two this test puts in it.
    world.delete_objects();
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(1);
    w->set_shifter_down(0);
    w->stats()->set_magicpoints(2000);

    walker* ally1 = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally2 = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, ally1) << "first ally created";
    ASSERT_NE(nullptr, ally2) << "second ally created";
    ally1->set_team_num(1);
    ally2->set_team_num(1);
    ally1->setxy(w->xpos() + 8, w->ypos() + 0);
    ally2->setxy(w->xpos() + 12, w->ypos() + 0);
    ally1->stats()->set_max_hitpoints(100);
    ally2->stats()->set_max_hitpoints(100);
    ally1->stats()->set_hitpoints(20);
    ally2->stats()->set_hitpoints(20);

    {
        // compute_heal_amount draws ONE number off the world stream per
        // patient (base = mp/4 + next(mp/4)); a floor stream makes both draws
        // 0, so every value below is arithmetic:
        //   ally1: base 2000/4 = 500, cost 250, amount 500 + 5*5 = 525
        //   ally2: base 1750/4 = 437, cost 218, amount 437 + 25 = 462
        FixedRandom floor_rng(0u);
        ScopedSimStream scoped(&floor_rng);
        ASSERT_TRUE(w->special()) << "cleric heal should fire with wounded allies";
    }
    // EVERY wounded ally in range is healed, not merely one of them, and the
    // second is healed out of the pool the first one left.
    EXPECT_FLOAT_EQ(545.0f, ally1->stats()->hitpoints())
        << "the first patient gets mp/4 + 5 * level";
    EXPECT_FLOAT_EQ(482.0f, ally2->stats()->hitpoints())
        << "the second is healed off the REDUCED pool, so it gets less";
    // 2000 - 250 - 218 = 1532, then the HEAL slot's own 2 MP cost.
    EXPECT_FLOAT_EQ(1530.0f, w->stats()->magicpoints())
        << "the cleric paid base/2 per patient, plus the slot's own cost";
    tick_world(48);
    delete w;
    world.delete_objects();
}


// Dynamically place a stain at a passable spot near the caster so the cleric
// raise-* / resurrect specials can succeed regardless of level layout.
static walker* place_stain_near(walker* caster, char old_family, unsigned char team, int level)
{
    walker* stain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (!stain)
        return nullptr;
    stain->set_team_num(team);
    stain->stats()->set_level(level);
    stain->stats()->set_old_family(old_family);
    stain->set_dead(0);
    for (int dx = 18; dx <= 28; dx += 2)
    {
        stain->setxy(caster->xpos() + dx, caster->ypos() + 0);
        if (og::runtime::current_session->myscreen_->world().query_passable(
                stain->xpos(), stain->ypos(), stain))
            return stain;
    }
    for (int dy = 18; dy <= 28; dy += 2)
    {
        stain->setxy(caster->xpos() + 0, caster->ypos() + dy);
        if (og::runtime::current_session->myscreen_->world().query_passable(
                stain->xpos(), stain->ypos(), stain))
            return stain;
    }
    return stain;
}


TEST_F(WalkerSpecials, cleric_raise_undead_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_CLERIC, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(80, 80);
    w->set_busy(0);
    w->set_current_special(2);
    w->set_shifter_down(0);
    w->stats()->set_magicpoints(2000);

    walker* stain = place_stain_near(w, FAMILY_SOLDIER, 2, 2);
    ASSERT_TRUE(stain != nullptr) << "stain placed";

    const int skel_before = count_order_family(Order::Living, FAMILY_SKELETON);
    ASSERT_TRUE(w->special()) << "raise undead should fire from stain";
    ASSERT_EQ(skel_before + 1, count_order_family(Order::Living, FAMILY_SKELETON))
        << "one cast raises exactly one skeleton";
    EXPECT_EQ(1, static_cast<int>(stain->dead()))
        << "and the corpse it rose from is consumed";
    walker* raised = find_order_family(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, raised) << "the skeleton is in oblist";
    EXPECT_EQ(static_cast<int>(w->team_num()), static_cast<int>(raised->team_num()))
        << "the raised undead serves the cleric";
    EXPECT_EQ(w, raised->owner()) << "and is owned by it";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, cleric_raise_ghost_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_CLERIC, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(80, 80);
    w->set_busy(0);
    w->set_current_special(3);
    w->set_shifter_down(0);
    w->stats()->set_magicpoints(2000);

    walker* stain = place_stain_near(w, FAMILY_SOLDIER, 2, 2);
    ASSERT_TRUE(stain != nullptr) << "stain placed";

    const int ghost_before = count_order_family(Order::Living, FAMILY_GHOST);
    ASSERT_TRUE(w->special()) << "raise ghost should fire from stain";
    ASSERT_EQ(ghost_before + 1, count_order_family(Order::Living, FAMILY_GHOST))
        << "one cast raises exactly one ghost";
    EXPECT_EQ(1, static_cast<int>(stain->dead()))
        << "and the corpse it rose from is consumed";
    walker* raised = find_order_family(Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, raised) << "the ghost is in oblist";
    EXPECT_EQ(static_cast<int>(w->team_num()), static_cast<int>(raised->team_num()))
        << "the raised undead serves the cleric";
    EXPECT_EQ(w, raised->owner()) << "and is owned by it";
    tick_world(48);
    delete w;
}


// Cleric special 4 (RESURRECT) on a FRIENDLY corpse re-creates a living of
// the stain's remembered family, on the stain's team, at exactly half of the
// max health the corpse carried, and eats the corpse. It never raises a
// friend as undead (packs/core/families/living-05-cleric.lua resurrect).
TEST_F(WalkerSpecials, cleric_resurrect_drives_simulation)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    og::runtime::current_session->myscreen_->save_data.allied_mode = 0;
    world.allied_mode = 0;

    walker* w = make_special_guy(FAMILY_CLERIC, 1, 5);
    ASSERT_NE(nullptr, w) << "walker created";
    w->setxy(80, 80);
    w->set_busy(0);
    w->set_current_special(4);
    w->set_shifter_down(0);
    w->stats()->set_magicpoints(2000);
    w->stats()->set_special_cost(4, 0);

    walker* stain = place_stain_near(w, FAMILY_SOLDIER, 1, 2);
    ASSERT_NE(nullptr, stain) << "stain placed";
    // transfer_stats copies the corpse's pool onto the revived body, so pin
    // the half-health rule against a value this test chose.
    stain->stats()->set_max_hitpoints(120.0f);

    const int soldiers_before = count_order_family(Order::Living, FAMILY_SOLDIER);
    const int ghosts_before = count_order_family(Order::Living, FAMILY_GHOST);
    ASSERT_TRUE(w->special()) << "resurrect should fire from friendly stain";
    ASSERT_EQ(soldiers_before + 1, count_order_family(Order::Living, FAMILY_SOLDIER))
        << "a friendly corpse comes back as exactly one of its own family";
    ASSERT_EQ(ghosts_before, count_order_family(Order::Living, FAMILY_GHOST))
        << "a friendly corpse is never raised as undead";
    EXPECT_EQ(1, static_cast<int>(stain->dead())) << "and the corpse is consumed";
    walker* raised = find_order_family(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, raised) << "the resurrected soldier is in oblist";
    EXPECT_FLOAT_EQ(60.0f, raised->stats()->hitpoints())
        << "resurrection returns the fallen at half their max health";
    EXPECT_FLOAT_EQ(120.0f, raised->stats()->max_hitpoints())
        << "carrying the corpse's own pool";
    EXPECT_EQ(1, static_cast<int>(raised->team_num())) << "on the corpse's team";
    tick_world(48);
    delete w;
    world.delete_objects();
}


// The other arm of the same cast: an ENEMY corpse is not resurrected, it is
// raised as one ghost on the CLERIC's team, owned by the cleric, and the
// fallen family never comes back (living-05-cleric.lua resurrect, else arm).
TEST_F(WalkerSpecials, cleric_resurrect_raises_an_enemy_corpse_as_a_ghost)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    og::runtime::current_session->myscreen_->save_data.allied_mode = 0;
    world.allied_mode = 0;

    walker* w = make_special_guy(FAMILY_CLERIC, 1, 5);
    ASSERT_NE(nullptr, w) << "walker created";
    w->setxy(80, 80);
    w->set_busy(0);
    w->set_current_special(4);
    w->set_shifter_down(0);
    w->stats()->set_magicpoints(2000);
    w->stats()->set_special_cost(4, 0);

    walker* stain = place_stain_near(w, FAMILY_SOLDIER, 2, 2);
    ASSERT_NE(nullptr, stain) << "enemy stain placed";
    ASSERT_FALSE(w->is_friendly(stain)) << "the arm under test is the enemy corpse";

    const int soldiers_before = count_order_family(Order::Living, FAMILY_SOLDIER);
    const int ghosts_before = count_order_family(Order::Living, FAMILY_GHOST);
    ASSERT_TRUE(w->special()) << "resurrect fires on an enemy corpse too";
    ASSERT_EQ(ghosts_before + 1, count_order_family(Order::Living, FAMILY_GHOST))
        << "an enemy corpse rises as exactly one ghost";
    ASSERT_EQ(soldiers_before, count_order_family(Order::Living, FAMILY_SOLDIER))
        << "the enemy's own family is never restored";
    EXPECT_EQ(1, static_cast<int>(stain->dead())) << "and the corpse is consumed";
    walker* ghost = find_order_family(Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, ghost) << "the ghost is in oblist";
    EXPECT_EQ(static_cast<int>(w->team_num()), static_cast<int>(ghost->team_num()))
        << "a raised enemy serves the cleric, not its old side";
    EXPECT_EQ(w, ghost->owner()) << "and is owned by it";
    tick_world(48);
    delete w;
    world.delete_objects();
}


TEST_F(WalkerSpecials, elf_rocks_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ELF, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(1);
    const int rocks_before = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(w->special()) << "elf rocks should fire";
    ASSERT_EQ(rocks_before + 2, count_family_in_weaplist(FAMILY_ROCK))
        << "ROCKS throws exactly two rocks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, elf_bouncing_rocks_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ELF, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(2);
    const int rocks_before = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(w->special()) << "elf bouncing rocks should fire";
    ASSERT_EQ(rocks_before + 2, count_family_in_weaplist(FAMILY_ROCK))
        << "BOUNCING ROCKS throws exactly two rocks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, elf_lots_of_rocks_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ELF, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(3);
    const int rocks_before = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(w->special()) << "elf lots of rocks should fire";
    ASSERT_EQ(rocks_before + 3, count_family_in_weaplist(FAMILY_ROCK))
        << "LOTS OF ROCKS throws exactly three rocks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, elf_mega_rocks_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ELF, 1, 6);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(4);
    w->stats()->set_magicpoints(2000);
    const int rocks_before = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(w->special()) << "elf mega rocks should fire";
    ASSERT_EQ(rocks_before + 4, count_family_in_weaplist(FAMILY_ROCK))
        << "MEGA ROCKS throws exactly four rocks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, fire_elemental_starburst_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_FIREELEMENTAL, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(1);
    w->stats()->set_magicpoints(2000);
    const int meteors_before = count_family_in_weaplist(FAMILY_METEOR);
    ASSERT_TRUE(w->special()) << "fire elemental starburst should fire";
    ASSERT_EQ(meteors_before + 8, count_family_in_weaplist(FAMILY_METEOR))
        << "a starburst is one meteor per compass direction -- eight, not some";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, thief_drop_bomb_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_THIEF, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_user(0);
    w->set_current_special(1);
    const int bombs_before = count_order_family(Order::FX, FAMILY_BOMB);
    ASSERT_TRUE(w->special()) << "thief drop bomb should fire";
    ASSERT_EQ(bombs_before + 1, count_order_family(Order::FX, FAMILY_BOMB))
        << "one cast drops exactly one bomb";
    walker* bomb = find_order_family(Order::FX, FAMILY_BOMB);
    ASSERT_NE(nullptr, bomb) << "the bomb is in the world";
    EXPECT_EQ(w, bomb->owner()) << "and belongs to the thief who dropped it";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, thief_cloak_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_THIEF, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_user(0);
    w->set_invisibility_left(0);
    w->set_current_special(2);
    {
        // cloak gain = cloak_base 20 + og.rand(20) * level, drawn off the
        // WORLD stream; a floor stream zeroes the roll and leaves the base.
        FixedRandom floor_rng(0u);
        ScopedSimStream scoped(&floor_rng);
        ASSERT_TRUE(w->special()) << "thief cloak should fire";
    }
    ASSERT_EQ(20, static_cast<int>(w->invisibility_left()))
        << "cloak banks exactly cloak_base on top of the 0 it started from";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, thief_taunt_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_THIEF, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_user(0);
    w->set_shifter_down(0);
    w->set_current_special(3);

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->set_team_num(2);
    foe->setxy(w->xpos() + 12, w->ypos() + 4);
    foe->stats()->set_level(1);
    foe->set_foe(nullptr);

    ASSERT_EQ(0u, foe->stats()->commands.size()) << "fixture precondition: no orders yet";
    {
        // taunt's contest is og.rand(self.level) >= og.rand(foe.level) on the
        // WORLD stream (a GameContext rng steers nothing), and the follow
        // order's length is 10 + og.rand(self.level) on the same stream.
        FixedRandom floor_rng(0u);
        ScopedSimStream scoped(&floor_rng);
        ASSERT_TRUE(w->special()) << "thief taunt should fire when foes are nearby";
    }
    EXPECT_EQ(w, foe->foe()) << "taunt retargets the foe at the thief";
    EXPECT_EQ(w, foe->leader()) << "and makes the thief its leader";
    ASSERT_EQ(1u, foe->stats()->commands.size())
        << "a taunted foe is given exactly one order";
    EXPECT_EQ(COMMAND_FOLLOW, static_cast<int>(foe->stats()->commands.front().commandtype))
        << "and that order is to follow the thief";
    EXPECT_EQ(10, static_cast<int>(foe->stats()->commands.front().commandcount))
        << "for 10 + rand(level) ticks -- 10 on a floor stream";
    EXPECT_FLOAT_EQ(2.0f, w->busy()) << "taunting costs the thief 2 busy ticks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, thief_charm_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_THIEF, 1, 6);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_user(0);
    w->set_shifter_down(1);
    w->set_current_special(3);

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->set_team_num(2);
    foe->setxy(w->xpos() + 8, w->ypos() + 4);
    foe->stats()->set_level(1);
    foe->set_real_team_num(255);
    foe->set_charm_left(0);

    const unsigned char foe_team_before = foe->team_num();
    {
        // The resist roll is `og.rand(20) == 0` (living-11-thief.lua), drawn
        // off the WORLD stream -- a GameContext rng steers nothing here. Any
        // non-zero draw is a successful charm.
        FixedRandom one_rng(1u);
        ScopedSimStream scoped(&one_rng);
        ASSERT_TRUE(w->special()) << "thief charm should fire on a charmable foe";
    }
    // A successful charm does ALL of this, not any one of it.
    EXPECT_EQ(static_cast<int>(w->team_num()), static_cast<int>(foe->team_num()))
        << "the charmed foe joins the thief's team";
    EXPECT_EQ(static_cast<int>(foe_team_before), static_cast<int>(foe->real_team_num()))
        << "and its old team is remembered in real_team_num";
    // charm_duration_base 75 + level diff 5 * 25 == 200, under the soft knee.
    EXPECT_EQ(200, static_cast<int>(foe->charm_left()))
        << "with the pack's own charm duration for this level difference";
    EXPECT_EQ(nullptr, foe->foe())
        << "and it inherits the thief's (absent) foe, so it picks a new one";
    EXPECT_FLOAT_EQ(10.0f, w->busy()) << "the charm costs the thief 10 busy ticks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, thief_poison_cloud_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_THIEF, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_user(0);
    w->set_current_special(4);
    const int clouds_before = count_order_family(Order::FX, FAMILY_CLOUD);
    ASSERT_TRUE(w->special()) << "thief poison cloud should fire";
    const int clouds_after = count_order_family(Order::FX, FAMILY_CLOUD);
    ASSERT_EQ(clouds_before + 1, clouds_after) << "one cast lays exactly one cloud";
    walker* cloud = find_order_family(Order::FX, FAMILY_CLOUD);
    ASSERT_NE(nullptr, cloud) << "the cloud is in the world";
    EXPECT_EQ(w, cloud->owner()) << "and belongs to the thief";
    // cloud_lifetime_base 40 + cloud_lifetime_per_level 3 * level 4 == 52.
    EXPECT_EQ(52, static_cast<int>(cloud->lifetime()))
        << "carrying the level's exact cloud lifetime";
    EXPECT_EQ(4, static_cast<int>(cloud->damage()))
        << "and the caster's level as its damage";
    EXPECT_FLOAT_EQ(5.0f, w->busy()) << "laying the cloud costs 5 busy ticks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, druid_grow_tree_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_DRUID, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(1);
    w->stats()->set_magicpoints(2000);
    const int trees_before = count_order_family(Order::Weapon, FAMILY_TREE);
    ASSERT_TRUE(w->special()) << "druid grow tree should fire";
    ASSERT_EQ(trees_before + 1, count_order_family(Order::Weapon, FAMILY_TREE))
        << "one cast plants exactly one tree";
    walker* tree = find_order_family(Order::Weapon, FAMILY_TREE);
    ASSERT_NE(nullptr, tree) << "the tree is in the world";
    EXPECT_EQ(w, tree->owner()) << "and belongs to the druid that planted it";
    EXPECT_EQ(ANI_GROW, static_cast<int>(tree->ani_type()))
        << "and it comes up growing";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, druid_summon_faerie_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_DRUID, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(2);
    w->stats()->set_magicpoints(2000);
    const int faerie_before = count_order_family(Order::Living, FAMILY_FAERIE);
    ASSERT_TRUE(w->special()) << "druid summon faerie should fire";
    ASSERT_EQ(faerie_before + 1, count_order_family(Order::Living, FAMILY_FAERIE))
        << "one cast summons exactly one faerie";
    walker* faerie = find_order_family(Order::Living, FAMILY_FAERIE);
    ASSERT_NE(nullptr, faerie) << "the faerie is in the world";
    EXPECT_EQ(w, faerie->owner()) << "and belongs to its summoner";
    EXPECT_EQ(static_cast<int>(w->team_num()), static_cast<int>(faerie->team_num()))
        << "on the summoner's team";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, druid_reveal_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_DRUID, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_view_all(0);
    w->set_current_special(3);
    ASSERT_TRUE(w->special()) << "druid reveal should fire";
    // view_all += level * 10 == 40 for this level-4 druid.
    ASSERT_EQ(40, static_cast<int>(w->view_all()))
        << "reveal banks exactly 10 ticks of sight per caster level";
    EXPECT_FLOAT_EQ(w->fire_frequency() * 4.0f, w->busy())
        << "and costs four fire cycles of busy";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, druid_protection_drives_simulation)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    walker* w = make_special_guy(FAMILY_DRUID, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    // One ring per unprotected friend in range, so the neighbourhood has to
    // hold exactly the two this test puts in it.
    world.delete_objects();
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(4);
    w->stats()->set_magicpoints(2000);

    walker* ally1 = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally2 = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, ally1) << "first ally created";
    ASSERT_NE(nullptr, ally2) << "second ally created";
    ally1->set_team_num(1);
    ally2->set_team_num(1);
    ally1->setxy(w->xpos() + 8, w->ypos() + 0);
    ally2->setxy(w->xpos() + 12, w->ypos() + 4);
    ally1->stats()->set_level(2);
    ally2->stats()->set_level(2);

    const int prot_before = count_order_family(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(w->special()) << "druid protection should fire with friends nearby";
    // One ring per unprotected FRIEND -- never one per cast, and never one for
    // the druid itself.
    ASSERT_EQ(prot_before + 2, count_order_family(Order::Weapon, FAMILY_CIRCLE_PROTECTION))
        << "two unprotected allies get two rings";
    int owned_by_ally1 = 0;
    int owned_by_ally2 = 0;
    int owned_by_druid = 0;
    for (auto& uptr : world.weaplist)
    {
        if (!uptr || uptr->family() != FAMILY_CIRCLE_PROTECTION)
            continue;
        if (uptr->owner() == ally1) owned_by_ally1++;
        if (uptr->owner() == ally2) owned_by_ally2++;
        if (uptr->owner() == w) owned_by_druid++;
    }
    EXPECT_EQ(1, owned_by_ally1) << "the first ally owns exactly one ring";
    EXPECT_EQ(1, owned_by_ally2) << "the second ally owns exactly one ring";
    EXPECT_EQ(0, owned_by_druid) << "the caster protects its friends, not itself";
    tick_world(48);
    delete w;
    world.delete_objects();
}


TEST_F(WalkerSpecials, orc_howl_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ORC, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(1);

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->set_team_num(2);
    foe->setxy(w->xpos() + 16, w->ypos() + 4);
    foe->stats()->set_level(1);
    foe->stats()->set_frozen_delay(0);
    foe->stats()->set_hitpoints(30.0f);

    {
        // The stun is yell_stun_base + rand0(level*10) - rand0(con*10) off the
        // WORLD stream (og.rand0 never sees a GameContext rng), so a floor
        // stream zeroes both rolls and the stun is exactly the base 10.
        FixedRandom floor_rng(0u);
        ScopedSimStream scoped(&floor_rng);
        ASSERT_TRUE(w->special()) << "orc howl should fire";
    }
    EXPECT_EQ(10, static_cast<int>(foe->stats()->frozen_delay()))
        << "howl banks exactly yell_stun_base on the foe it reaches";
    EXPECT_FLOAT_EQ(2.0f, w->busy()) << "and costs the caster 2 busy ticks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, orc_eat_corpse_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ORC, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(2);
    w->stats()->set_max_hitpoints(200);
    w->stats()->set_hitpoints(50);

    walker* stain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_TRUE(stain != nullptr) << "stain created";
    stain->center_on(w);
    stain->set_team_num(2);
    stain->stats()->set_level(3);
    stain->set_dead(0);

    ASSERT_FLOAT_EQ(50.0f, w->stats()->hitpoints()) << "fixture precondition";
    ASSERT_TRUE(w->special()) << "orc eat corpse should fire";
    EXPECT_EQ(1, static_cast<int>(stain->dead()))
        << "the corpse is consumed by the meal";
    // corpse_heal_per_level 5 * the corpse's level 3 == 15, on top of 50 and
    // well under the 200 cap.
    EXPECT_FLOAT_EQ(65.0f, w->stats()->hitpoints())
        << "a corpse is worth 5 HP per level of the body that fell";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, barbarian_hurl_boulder_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_BARBARIAN, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(1);
    const int boulders_before = count_order_family(Order::Weapon, FAMILY_BOULDER);
    ASSERT_TRUE(w->special()) << "barbarian hurl boulder should fire";
    EXPECT_EQ(boulders_before + 1, count_order_family(Order::Weapon, FAMILY_BOULDER))
        << "one hurl adds exactly one boulder to weaplist";
    walker* boulder = find_order_family(Order::Weapon, FAMILY_BOULDER);
    ASSERT_NE(nullptr, boulder) << "the boulder is in the world";
    EXPECT_EQ(w, boulder->owner()) << "and belongs to the thrower";
    EXPECT_EQ(0, static_cast<int>(boulder->skip_exit()))
        << "slot 1 is the plain boulder: no explode-on-impact sentinel";
    EXPECT_FLOAT_EQ(6.0f, w->busy()) << "busy += 1 + slot * 5 == 6 for slot 1";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, barbarian_exploding_boulder_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_BARBARIAN, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(2);
    const int boulders_before = count_order_family(Order::Weapon, FAMILY_BOULDER);
    ASSERT_TRUE(w->special()) << "barbarian exploding boulder should fire";
    ASSERT_EQ(boulders_before + 1, count_order_family(Order::Weapon, FAMILY_BOULDER))
        << "one hurl adds exactly one boulder";
    walker* boulder = find_order_family(Order::Weapon, FAMILY_BOULDER);
    ASSERT_NE(nullptr, boulder) << "the boulder is in the world";
    EXPECT_EQ(w, boulder->owner()) << "and belongs to the thrower";
    EXPECT_EQ(5000, static_cast<int>(boulder->skip_exit()))
        << "slot 2 is the EXPLODING boulder: it carries the 5000 sentinel";
    EXPECT_FLOAT_EQ(11.0f, w->busy()) << "busy += 1 + slot * 5 == 11 for slot 2";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archmage_teleport_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(120, 120);
    w->set_busy(0);
    w->set_user(0);
    w->set_current_special(1);
    w->set_shifter_down(1);
    w->stats()->set_special_cost(1, 0);
    w->stats()->set_magicpoints(2000);
    ASSERT_NE(nullptr, w->myguy) << "the marker's uses are read off the guy";
    w->myguy->intelligence = 200;

    const int markers_before = count_order_family(Order::FX, FAMILY_MARKER);
    ASSERT_TRUE(w->special()) << "archmage teleport (marker) should fire";
    ASSERT_EQ(markers_before + 1, count_order_family(Order::FX, FAMILY_MARKER))
        << "one cast plants exactly one marker";
    walker* marker = find_order_family(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker) << "the marker is in the world";
    EXPECT_EQ(w, marker->owner()) << "and belongs to the caster";
    // A guy-backed caster reads its uses off Intelligence / 33 == 200 / 33 == 6.
    EXPECT_EQ(6, static_cast<int>(marker->lifetime()))
        << "the marker's uses come from the caster's Intelligence / 33";
    EXPECT_FLOAT_EQ(8.0f, w->busy()) << "planting a marker costs 8 busy ticks";
    // Marker surcharge: half the post-cost pool, out of 2000.
    EXPECT_FLOAT_EQ(1000.0f, w->stats()->magicpoints())
        << "and half the remaining pool";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archmage_heartburst_drives_simulation)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    walker* w = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(w != nullptr) << "walker created";
    // One burst per acquired foe, so the neighbourhood has to hold exactly the
    // foe this test puts in it.
    world.delete_objects();
    w->setxy(120, 120);
    w->set_busy(0);
    w->set_shifter_down(0);
    w->set_current_special(2);
    w->stats()->set_magicpoints(2000);
    w->stats()->set_special_cost(2, 0);

    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(2);
    foe->setxy(w->xpos() + 8, w->ypos() + 8);
    foe->stats()->set_level(1);

    const int explosions_before = count_order_family(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(w->special()) << "archmage heartburst should fire when foes are present";
    ASSERT_EQ(explosions_before + 1, count_order_family(Order::FX, FAMILY_EXPLOSION))
        << "heartburst is one explosion per acquired foe: one foe, one burst";
    // mp_pool_damage(slot 2) = min((2000 - 0)/2, cap 600) = 600, all of it
    // spent on the single burst.
    EXPECT_FLOAT_EQ(1400.0f, w->stats()->magicpoints())
        << "and the whole pool is spent on that one burst";
    EXPECT_FLOAT_EQ(5.0f, w->busy()) << "heartburst costs the caster 5 busy ticks";
    tick_world(48);
    delete w;
    world.delete_objects();
}


TEST_F(WalkerSpecials, archmage_summon_image_drives_simulation)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    walker* w = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(w != nullptr) << "walker created";
    // The caster is loader-owned, so after this oblist holds exactly the
    // phantom the cast conjures.
    world.delete_objects();
    w->setxy(120, 120);
    w->set_busy(0);
    w->set_shifter_down(0);
    w->set_current_special(3);
    w->stats()->set_magicpoints(800);
    w->stats()->set_special_cost(3, 0);

    ASSERT_EQ(0u, world.oblist.size()) << "fixture precondition: an empty oblist";
    // 800 post-cost MP is the 500..999 tier, whose table is seven families
    // deep; seed 5 is the state whose first draw indexes the elf.
    world.rng_.state_ = 5u;
    ASSERT_TRUE(w->special()) << "archmage summon image should fire";
    ASSERT_EQ(1u, world.oblist.size()) << "one cast conjures exactly one phantom";
    walker* phantom = world.oblist.front().get();
    ASSERT_NE(nullptr, phantom) << "the phantom is in oblist";
    EXPECT_EQ(FAMILY_ELF, static_cast<int>(phantom->family()))
        << "this tier's table maps this roll to the elf";
    EXPECT_EQ(Order::Living, phantom->order()) << "a phantom is a living, not an FX";
    EXPECT_EQ(w, phantom->owner()) << "and belongs to its summoner";
    EXPECT_EQ(static_cast<int>(w->team_num()), static_cast<int>(phantom->team_num()))
        << "on the summoner's team";
    EXPECT_TRUE(phantom->summoned())
        << "a phantom is conjured ammunition, never a SAVE_ALL casualty";
    tick_world(48);
    delete w;
    world.delete_objects();
}


TEST_F(WalkerSpecials, archmage_mind_control_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(120, 120);
    w->set_busy(0);
    w->set_shifter_down(0);
    w->set_current_special(4);
    w->stats()->set_magicpoints(1000);
    w->stats()->set_special_cost(4, 0);

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->set_team_num(2);
    foe->setxy(w->xpos() + 6, w->ypos() + 6);
    foe->stats()->set_level(1);
    foe->set_real_team_num(255);
    foe->set_charm_left(0);
    const unsigned char foe_team_before = foe->team_num();

    {
        // Two world-stream draws: og.rand(20) for the resist (non-zero means
        // proper control, not the berserk arm) and compute_charm_duration's
        // 25 + next(20*edge). A context rng reaches neither.
        FixedRandom one_rng(1u);
        ScopedSimStream scoped(&one_rng);
        ASSERT_TRUE(w->special()) << "archmage mind control should fire";
    }
    // Proper control, not berserk: the foe joins the CASTER's team.
    EXPECT_EQ(static_cast<int>(w->team_num()), static_cast<int>(foe->team_num()))
        << "the controlled foe joins the archmage's team";
    EXPECT_EQ(static_cast<int>(foe_team_before), static_cast<int>(foe->real_team_num()))
        << "and its old team is remembered in real_team_num";
    EXPECT_EQ(26, static_cast<int>(foe->charm_left()))
        << "charm_duration is 25 + one draw over 20 * level edge";
    EXPECT_EQ(nullptr, foe->foe()) << "control clears the foe so it may choose a new one";
    EXPECT_FLOAT_EQ(10.0f, w->busy()) << "mind control costs the caster 10 busy ticks";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, slime_split_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SLIME, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(1);
    ASSERT_TRUE(w->special()) << "slime split should fire";
    ASSERT_EQ(w->ani_type(), ANI_SLIME_SPLIT) << "split should set ani_type to ANI_SLIME_SPLIT";
    int small_before = count_family_in_oblist(FAMILY_SMALL_SLIME);
    bool transformed = false;
    for (int i = 0; i < 64 && !transformed; ++i)
    {
        w->act();
        if (w->family() == FAMILY_SMALL_SLIME)
            transformed = true;
        if (count_family_in_oblist(FAMILY_SMALL_SLIME) > small_before)
            transformed = true;
    }
    tick_world(8);
    ASSERT_TRUE(transformed) << "split should transform slime into a small slime and spawn another";
    delete w;
}


// grow_into (packs/core/families/living-08-slime.lua) has exactly two
// outcomes and both are pinned here: with all eight neighbouring cells clear
// the slime transforms into the next size up and the cast returns true; with
// fewer than eight clear it declines and queues one 10-tick walk instead.
TEST_F(WalkerSpecials, medium_slime_grow_drives_simulation)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* w = make_special_guy(FAMILY_MEDIUM_SLIME, 1, 4);
    ASSERT_NE(nullptr, w) << "walker created";
    w->set_busy(0);
    w->set_current_special(1);
    ASSERT_TRUE(place_with_room_to_grow(w)) << "found an open cell on this level";
    ASSERT_GT(static_cast<int>(w->spaces_clear()), 7)
        << "the arm under test is the one with room to grow";
    ASSERT_TRUE(w->special()) << "a medium slime with room grows";
    EXPECT_EQ(FAMILY_SLIME, static_cast<int>(w->family()))
        << "and it becomes a full slime";
    tick_world(48);
    delete w;
    world.delete_objects();

    // No-room arm: at the map origin five of the eight probes are off-map.
    walker* boxed = make_special_guy(FAMILY_MEDIUM_SLIME, 1, 4);
    ASSERT_NE(nullptr, boxed) << "boxed slime created";
    boxed->setxy(0, 0);
    boxed->set_busy(0);
    boxed->set_current_special(1);
    boxed->stats()->clear_command();
    ASSERT_LE(static_cast<int>(boxed->spaces_clear()), 7)
        << "the arm under test is the one with no room to grow";
    ASSERT_FALSE(boxed->special()) << "a boxed-in slime declines to grow";
    EXPECT_EQ(FAMILY_MEDIUM_SLIME, static_cast<int>(boxed->family()))
        << "and stays the size it was";
    ASSERT_EQ(1u, boxed->stats()->commands.size())
        << "the declined grow queues exactly one command";
    EXPECT_EQ(COMMAND_WALK, static_cast<int>(boxed->stats()->commands.back().commandtype))
        << "and that command is a shuffle toward room";
    EXPECT_EQ(10, static_cast<int>(boxed->stats()->commands.back().commandcount))
        << "with the pack's own 10-tick budget";
    tick_world(48);
    delete boxed;
    world.delete_objects();
}


// The small slime shares grow_into's body, one size lower: room means
// FAMILY_MEDIUM_SLIME, no room means the same declined 10-tick shuffle.
TEST_F(WalkerSpecials, small_slime_grow_drives_simulation)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();

    walker* w = make_special_guy(FAMILY_SMALL_SLIME, 1, 4);
    ASSERT_NE(nullptr, w) << "walker created";
    w->set_busy(0);
    w->set_current_special(1);
    ASSERT_TRUE(place_with_room_to_grow(w)) << "found an open cell on this level";
    ASSERT_GT(static_cast<int>(w->spaces_clear()), 7)
        << "the arm under test is the one with room to grow";
    ASSERT_TRUE(w->special()) << "a small slime with room grows";
    EXPECT_EQ(FAMILY_MEDIUM_SLIME, static_cast<int>(w->family()))
        << "and it becomes a medium slime";
    tick_world(48);
    delete w;
    world.delete_objects();

    walker* boxed = make_special_guy(FAMILY_SMALL_SLIME, 1, 4);
    ASSERT_NE(nullptr, boxed) << "boxed slime created";
    boxed->setxy(0, 0);
    boxed->set_busy(0);
    boxed->set_current_special(1);
    boxed->stats()->clear_command();
    ASSERT_LE(static_cast<int>(boxed->spaces_clear()), 7)
        << "the arm under test is the one with no room to grow";
    ASSERT_FALSE(boxed->special()) << "a boxed-in slime declines to grow";
    EXPECT_EQ(FAMILY_SMALL_SLIME, static_cast<int>(boxed->family()))
        << "and stays the size it was";
    ASSERT_EQ(1u, boxed->stats()->commands.size())
        << "the declined grow queues exactly one command";
    EXPECT_EQ(COMMAND_WALK, static_cast<int>(boxed->stats()->commands.back().commandtype))
        << "and that command is a shuffle toward room";
    EXPECT_EQ(10, static_cast<int>(boxed->stats()->commands.back().commandcount))
        << "with the pack's own 10-tick budget";
    tick_world(48);
    delete boxed;
    world.delete_objects();
}
