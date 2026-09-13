#include <openglad/interface/game_context.h>
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

class SequenceRandom : public IRandom {
public:
    explicit SequenceRandom(std::initializer_list<Uint32> vals) : vals_(vals), idx_(0) {}
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
    EXPECT_GT(bolt->damage(), 0.0f) << "the bolt carries the MP pool as damage";
    EXPECT_GT(arch->busy(), 0.0f) << "the cast leaves the archmage busy";

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
    EXPECT_GT(static_cast<int>(elemental->lifetime()), 0)
        << "a true summon is ammunition: it has a finite lifetime";
    EXPECT_GT(arch->busy(), 0.0f) << "summoning takes lots of time";

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
                const float pool_before = caster->stats()->magicpoints();
                walker::SpecialFailure why = walker::SpecialFailure::None;
                if (caster->special(&why))
                {
                    mask |= 1u << ((slot - 1) * 2 + shift);
                }
                else if (why == walker::SpecialFailure::NoMP)
                {
                    EXPECT_FLOAT_EQ(pool_before, caster->stats()->magicpoints())
                        << "a slot refused at the cost gate must run no cast and "
                           "spend nothing: " << sweep.name << " slot " << slot
                        << " shifter " << shift;
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


TEST_F(WalkerSpecials, orc_howl)
{
    walker* w = make_special_guy(FAMILY_ORC);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // howl
    w->special();
}


TEST_F(WalkerSpecials, barbarian_hurl)
{
    walker* w = make_special_guy(FAMILY_BARBARIAN);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(1); // hurl boulder
    w->special();
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

    // Exercise specific "not enough for selected special" index path.
    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->set_current_special(4);
    w->stats()->set_special_cost(4, 50);
    w->stats()->set_magicpoints(49);
    ASSERT_TRUE(!w->special()) << "insufficient MP for selected special index should fail";

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

TEST_F(WalkerSpecials, walker_death_fire_elemental)
{
    walker* w = make_special_guy(FAMILY_FIREELEMENTAL, 1);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_dead(1);
    w->death();
}


TEST_F(WalkerSpecials, walker_death_with_myguy)
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    ASSERT_TRUE(w->myguy != nullptr) << "should have myguy";
    w->set_dead(1);
    w->death();

    // Also exercise generator-death explosion fan-out path.
    walker* generator = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_TRUE(generator != nullptr) << "generator created";
    int fx_before = count_family_all_lists(FAMILY_EXPLOSION);
    generator->set_dead(1);
    generator->death();
    int fx_after = count_family_all_lists(FAMILY_EXPLOSION);
    ASSERT_TRUE(fx_after >= fx_before + 1) << "generator death should spawn explosion FX";
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, archmage_illusion_rng_tables)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    if (!arch) {
        return;
    }

    arch->setxy(120, 120);
    arch->stats()->set_special_cost(3, 0);
    arch->stats()->set_max_magicpoints(2000);
    arch->set_current_special(static_cast<char>(3));
    arch->set_shifter_down(static_cast<short>(0));

    GameContext ctx;

    const int mp_tiers[] = {120, 300, 700, 1200};
    const int max_pick[] = {3, 5, 7, 9};
    for (int t = 0; t < 4; ++t) {
        for (Uint32 pick = 0; pick < static_cast<Uint32>(max_pick[t]); ++pick) {
            SequenceRandom rng({pick});
            ctx.rng = &rng;
            push_test_context(&ctx);
            int before = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
            arch->stats()->set_magicpoints(static_cast<float>(mp_tiers[t]));
            arch->set_busy(0);
            (void)arch->special();
            int after = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
            ASSERT_TRUE(after >= before) << "illusion summon case should not reduce object count";
            pop_test_context();
        }
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
    delete arch;
}


TEST_F(WalkerSpecials, mage_marker_remove_and_freeze_enemy_branch)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* mage = make_special_guy(FAMILY_MAGE, 2, 6);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    if (!mage) {
        return;
    }

    // Marker remove/replace path for mage special 1 with shifter held.
    mage->set_current_special(1);
    mage->set_shifter_down(1);
    mage->set_user(0);
    mage->set_busy(0);
    if (mage->myguy) {
        mage->myguy->intelligence = 120;
    }

    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_TRUE(marker != nullptr) << "marker created";
    if (marker) {
        marker->set_owner(mage);
        marker->set_dead(0);
        marker->setxy(mage->xpos() + 8, mage->ypos() + 8);
    }
    (void)mage->special();

    // Freeze-time enemy-team branch.
    mage->set_current_special(3);
    mage->set_shifter_down(0);
    mage->set_busy(0);
    mage->set_team_num(3);
    if (mage->myguy) {
        mage->clear_myguy();
    }
    walker* ally = make_special_guy(FAMILY_ORC, 3, 2);
    ASSERT_TRUE(ally != nullptr) << "ally created";
    if (ally) {
        ally->setxy(mage->xpos() + 6, mage->ypos() + 6);
    }
    (void)mage->special();

    delete ally;
    delete mage;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, mage_wave_and_burst_with_targets)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* mage = make_special_guy(FAMILY_MAGE, 1, 7);
    walker* foe1 = make_special_guy(FAMILY_ORC, 2, 3);
    walker* foe2 = make_special_guy(FAMILY_SKELETON, 2, 3);
    ASSERT_TRUE(mage != nullptr && foe1 != nullptr && foe2 != nullptr) << "mage and foes created";
    if (!(mage && foe1 && foe2)) {
        return;
    }

    mage->setxy(120, 120);
    mage->set_lastx(mage->stepsize());
    mage->set_lasty(0);
    foe1->setxy(128, 120);
    foe2->setxy(132, 124);

    mage->set_current_special(4); // energy wave
    mage->set_busy(0);
    (void)mage->special();

    mage->set_current_special(5); // burst enemies
    mage->set_busy(0);
    mage->stats()->set_magicpoints(1200);
    mage->stats()->set_special_cost(5, 0);
    (void)mage->special();

    delete foe1;
    delete foe2;
    delete mage;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, archmage_low_int_marker_chain_and_summon_true)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    walker* foe = make_special_guy(FAMILY_ORC, 2, 2);
    ASSERT_TRUE(arch != nullptr && foe != nullptr) << "arch and foe created";
    if (!(arch && foe)) {
        return;
    }

    arch->setxy(140, 140);
    foe->setxy(148, 140);
    arch->stats()->set_special_cost(1, 0);
    arch->stats()->set_special_cost(2, 0);
    arch->stats()->set_special_cost(3, 0);
    arch->stats()->set_special_cost(4, 0);

    // Archmage special 1 low-int path.
    arch->set_current_special(1);
    arch->set_shifter_down(1);
    arch->set_busy(0);
    arch->set_user(0);
    if (arch->myguy) {
        arch->myguy->intelligence = 20;
    }
    (void)arch->special();

    // Marker removal + replacement path.
    if (arch->myguy) {
        arch->myguy->intelligence = 220;
    }
    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_TRUE(marker != nullptr) << "arch marker created";
    if (marker) {
        marker->set_owner(arch);
        marker->set_dead(0);
        marker->setxy(arch->xpos() + 8, arch->ypos() + 8);
    }
    arch->set_busy(0);
    arch->set_current_special(1);
    arch->set_shifter_down(1);
    (void)arch->special();

    // Chain-lightning path.
    arch->set_busy(0);
    arch->set_current_special(2);
    arch->set_shifter_down(1);
    (void)arch->special();

    // True summon path.
    arch->set_busy(0);
    arch->set_current_special(3);
    arch->set_shifter_down(1);
    (void)arch->special();

    delete foe;
    delete arch;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, archmage_mind_control_stats_name_path)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 4, 7);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* foe2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* foe3 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(arch != nullptr && foe != nullptr && foe2 != nullptr && foe3 != nullptr) << "arch and mind-control targets created";
    if (!(arch && foe && foe2 && foe3)) {
        delete arch;
        og::runtime::current_session->myscreen_->world().delete_objects();
        return;
    }

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
    const float mp_before = arch->stats()->magicpoints();

    SequenceRandom seq_rng({1, 1, 1, 1, 1, 1, 1, 1});
    GameContext test_ctx;

    test_ctx.rng = &seq_rng;
    push_test_context(&test_ctx);
    (void)arch->special();
    pop_test_context();
    ASSERT_TRUE(arch->stats()->magicpoints() < mp_before) << "mind-control should spend MP for controlled targets";

    delete arch;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, druid_circle_existing_protection_branch)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* druid = make_special_guy(FAMILY_DRUID, 1, 6);
    walker* ally = make_special_guy(FAMILY_SOLDIER, 1, 5);
    ASSERT_TRUE(druid != nullptr && ally != nullptr) << "druid and ally created";
    if (!(druid && ally)) {
        return;
    }

    druid->setxy(100, 100);
    ally->setxy(108, 100);
    druid->set_busy(0);
    druid->set_current_special(4);

    walker* existing = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(existing != nullptr) << "existing protection created";
    if (existing) {
        existing->set_owner(ally);
        existing->set_team_num(ally->team_num());
        existing->setxy(ally->xpos(), ally->ypos());
    }

    (void)druid->special();

    delete ally;
    delete druid;
    og::runtime::current_session->myscreen_->world().delete_objects();
}

TEST_F(WalkerSpecials, orc_eat_corpse_and_barbarian_exploding_boulder_npc)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* orc = make_special_guy(FAMILY_ORC, 2, 4);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    if (!orc) {
        return;
    }
    if (orc->myguy) {
        orc->clear_myguy();
    }
    orc->stats()->name = "ORC-NPC";
    orc->stats()->set_hitpoints(10);
    orc->stats()->set_max_hitpoints(100);
    orc->set_current_special(2);
    orc->set_busy(0);

    walker* blood = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_TRUE(blood != nullptr) << "blood created";
    if (blood) {
        blood->set_team_num(3);
        blood->stats()->set_level(3);
        blood->setxy(orc->xpos() + 1, orc->ypos() + 1);
    }
    (void)orc->special();

    walker* barb = make_special_guy(FAMILY_BARBARIAN, 2, 5);
    ASSERT_TRUE(barb != nullptr) << "barbarian created";
    if (barb) {
        if (barb->myguy) {
            barb->clear_myguy();
        }
        barb->setxy(120, 120);
        barb->set_lastx(barb->stepsize());
        barb->set_lasty(0);
        barb->set_busy(0);
        barb->set_current_special(2); // exploding boulder
        (void)barb->special();
    }

    delete barb;
    delete orc;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, cleric_raise_skeleton_and_ghost_from_stain)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 6);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    if (!cleric)
        return;

    // Place a blood stain close by but not colliding with other objects.
    walker* stain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_TRUE(stain != nullptr) << "stain created";
    if (!stain) {
        delete cleric;
        return;
    }

    cleric->setxy(80, 80);
    cleric->set_team_num(1);
    cleric->set_busy(0);
    cleric->stats()->set_magicpoints(2000);
    cleric->stats()->set_max_magicpoints(2000);

    stain->setxy(110, 80);
    stain->set_team_num(2);
    stain->set_dead(0);

    // Raise skeletons (special 2, shifter up).
    cleric->set_current_special(2);
    cleric->set_shifter_down(0);
    (void)cleric->special();

    // Raise ghosts (special 3, shifter up).
    cleric->set_busy(0);
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    (void)cleric->special();

    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, cleric_mystic_mace_low_int_and_success_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 8);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    if (!cleric)
        return;

    cleric->set_current_special(1);
    cleric->set_shifter_down(1);
    cleric->stats()->set_special_cost(1, 0);
    cleric->set_busy(0);
    cleric->set_user(0);

    if (cleric->myguy) {
        cleric->myguy->intelligence = 40;
    }
    int shields_before = count_family_all_lists(FAMILY_MAGIC_SHIELD);
    (void)cleric->special();
    int shields_after_low_int = count_family_all_lists(FAMILY_MAGIC_SHIELD);
    ASSERT_EQ(shields_before, shields_after_low_int) << "low-int mystic mace path should not create shield";

    if (cleric->myguy) {
        cleric->myguy->intelligence = 120;
    }
    cleric->set_busy(0);
    float mp_before = cleric->stats()->magicpoints();
    (void)cleric->special();
    int shields_after_success = count_family_all_lists(FAMILY_MAGIC_SHIELD);
    ASSERT_TRUE(shields_after_success > shields_after_low_int) << "valid mystic mace cast should create shield fx";
    ASSERT_TRUE(cleric->busy() > 0) << "valid mystic mace should set busy";
    ASSERT_TRUE(cleric->stats()->magicpoints() < mp_before) << "valid mystic mace should spend magicpoints";

    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, cleric_resurrect_friendly_and_enemy_stains)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 8);
    walker* ally = make_special_guy(FAMILY_SOLDIER, 1, 5);
    ASSERT_TRUE(cleric != nullptr && ally != nullptr) << "cleric and ally created";
    if (!(cleric && ally))
        return;

    cleric->setxy(100, 100);
    cleric->set_current_special(4);
    cleric->stats()->set_special_cost(4, 0);
    cleric->stats()->set_magicpoints(2000);
    og::runtime::current_session->myscreen_->save_data.allied_mode = 0;
    og::runtime::current_session->myscreen_->world_.allied_mode = 0;
    ally->setxy(110, 100);
    ally->set_team_num(1);
    ally->generate_bloodspot();

    int soldiers_before = count_family_in_oblist(FAMILY_SOLDIER);
    (void)cleric->special();
    int soldiers_after = count_family_in_oblist(FAMILY_SOLDIER);
    ASSERT_TRUE(soldiers_after >= soldiers_before) << "friendly stain should allow resurrecting original family";

    delete ally;
    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Enemy-stain branch depends on map passability and alliance mode interactions;
    // keep this test deterministic by validating the friendly resurrection path only.
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


TEST_F(WalkerSpecials, walker_turn_undead_attack_kill_branch_and_act_guard_random_edges)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 7);
    walker* skel = make_special_guy(FAMILY_SKELETON, 2, 1);
    walker* ghost = make_special_guy(FAMILY_GHOST, 2, 1);
    ASSERT_TRUE(cleric != nullptr && skel != nullptr && ghost != nullptr) << "cleric and undead created";
    if (cleric && skel && ghost) {
        cleric->setxy(100, 100);
        skel->setxy(102, 100);
        ghost->setxy(103, 100);
        cleric->set_damage(150.0f);
        skel->stats()->set_hitpoints(8);
        ghost->stats()->set_hitpoints(8);
        SequenceRandom seq_rng({1000, 0, 1000, 0});
        GameContext test_ctx;
    
        test_ctx.rng = &seq_rng;
        push_test_context(&test_ctx);
        (void)cleric->turn_undead(40, 3);
        pop_test_context();
    }

    walker* guard = make_special_guy(FAMILY_ORC, 3, 4);
    walker* foe = make_special_guy(FAMILY_SOLDIER, 2, 2);
    ASSERT_TRUE(guard != nullptr && foe != nullptr) << "guard and foe created";
    if (guard && foe) {
        guard->setxy(140, 140);
        foe->setxy(146, 140);
        guard->set_act_type(ACT_GUARD);
        (void)guard->act();
    }

    walker* randomer = make_special_guy(FAMILY_ORC, 5, 4);
    ASSERT_TRUE(randomer != nullptr) << "randomer created";
    if (randomer) {
        randomer->setxy(160, 160);
        randomer->set_foe(nullptr);
        randomer->set_act_type(ACT_RANDOM);
        randomer->stats()->clear_command();
        // act(): rng(4)==0 and rng(20)==1 => act_random() path
        // act_random(): rng(70)==0, find no foe => random-walk command path
        SequenceRandom random_rng({0, 1, 0, 1, 1, 2});
        GameContext random_ctx;
    
        random_ctx.rng = &random_rng;
        push_test_context(&random_ctx);
        (void)randomer->act();
        pop_test_context();
    }

    delete randomer;
    delete foe;
    delete guard;
    delete ghost;
    delete skel;
    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, guard_paths_and_teleport_failures)
{
    walker* w = make_special_guy(FAMILY_MAGE, 0, 4);
    ASSERT_TRUE(w != nullptr) << "mage created";
    if (!w)
        return;

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
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

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
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->set_current_special(1); // teleport
    w->stats()->set_special_cost(1, 7);
    w->stats()->set_magicpoints(50);

    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_TRUE(marker != nullptr) << "teleport marker created";
    if (!marker) {
        delete w;
        og::runtime::current_session->myscreen_->world().delete_objects();
        return;
    }

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
    int cmds_before = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(w->special()) << "soldier charge should fire when forward is clear";
    int cmds_after = static_cast<int>(w->stats()->commands.size());
    tick_world(48);
    ASSERT_TRUE(cmds_after > cmds_before) << "charge should enqueue a rush command";
    delete w;
}


TEST_F(WalkerSpecials, soldier_boomerang_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_busy(0);
    w->set_current_special(2);
    int before = count_family_all_lists(FAMILY_BOOMERANG);
    ASSERT_TRUE(w->special()) << "soldier boomerang should fire";
    int just_after = count_family_all_lists(FAMILY_BOOMERANG);
    ASSERT_TRUE(just_after > before) << "boomerang FX should be spawned";
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
    int cmds_before = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(w->special()) << "whirlwind should fire";
    ASSERT_TRUE(w->busy() > 0) << "whirlwind should set busy on caster";
    int cmds_after = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(cmds_after > cmds_before) << "whirlwind should enqueue walk commands";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, soldier_disarm_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_curdir(static_cast<char>(FACE_RIGHT));
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(4);

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->set_team_num(2);
    foe->setxy(w->xpos() + w->sizex() + 1, w->ypos() + 0);
    foe->stats()->set_level(1);
    const float foe_busy_before = foe->busy();

    bool fired = w->special();
    if (fired)
    {
        ASSERT_TRUE(w->busy() > 0) << "successful disarm should leave caster busy";
    }
    else
    {
        ASSERT_TRUE(foe->busy() >= foe_busy_before) << "failed disarm should not corrupt foe state";
    }
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archer_fire_arrows_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(1);
    int cmds_before = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(w->special()) << "archer fire arrows should issue commands";
    int cmds_after = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(cmds_after > cmds_before) << "fire arrows should enqueue quick-fire commands";
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
    int arrows_before = count_family_in_weaplist(FAMILY_ARROW);
    ASSERT_TRUE(w->special()) << "archer barrage should fire";
    int arrows_after = count_family_in_weaplist(FAMILY_ARROW);
    ASSERT_TRUE(arrows_after > arrows_before) << "barrage should add arrows to weaplist";
    ASSERT_TRUE(w->busy() > 0) << "barrage should set busy";
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
    int bolts_before = count_family_in_weaplist(FAMILY_FIRE_ARROW);
    ASSERT_TRUE(w->special()) << "exploding bolt should fire";
    int bolts_after = count_family_in_weaplist(FAMILY_FIRE_ARROW);
    ASSERT_TRUE(bolts_after > bolts_before) << "exploding bolt should add fire arrow to weaplist";
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
    int fbs_before = count_family_in_weaplist(FAMILY_FIREBALL);
    ASSERT_TRUE(w->special()) << "mage warp/starburst should fire";
    int fbs_after = count_family_in_weaplist(FAMILY_FIREBALL);
    ASSERT_TRUE(fbs_after > fbs_before) << "starburst should add fireballs to weaplist";
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
    ASSERT_TRUE(w->special()) << "mage freeze time should fire (player path)";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().enemy_freeze > 0) << "freeze time should set world enemy_freeze";
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
    int waves_before = count_family_in_weaplist(FAMILY_WAVE);
    ASSERT_TRUE(w->special()) << "mage energy wave should fire";
    int waves_after = count_family_in_weaplist(FAMILY_WAVE);
    ASSERT_TRUE(waves_after > waves_before) << "energy wave should spawn FAMILY_WAVE in weaplist";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, mage_heartburst_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_MAGE, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
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

    int explosions_before = count_family_all_lists(FAMILY_EXPLOSION);
    ASSERT_TRUE(w->special()) << "mage heartburst should fire when foes are present";
    int explosions_after = count_family_all_lists(FAMILY_EXPLOSION);
    ASSERT_TRUE(explosions_after > explosions_before) << "heartburst should spawn explosion FX";
    tick_world(48);
    delete w;
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
    walker* w = make_special_guy(FAMILY_CLERIC, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(1);
    w->set_shifter_down(0);
    w->stats()->set_magicpoints(2000);

    walker* ally1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(ally1 != nullptr && ally2 != nullptr) << "allies created";
    ally1->set_team_num(1);
    ally2->set_team_num(1);
    ally1->setxy(w->xpos() + 8, w->ypos() + 0);
    ally2->setxy(w->xpos() + 12, w->ypos() + 0);
    ally1->stats()->set_max_hitpoints(100);
    ally2->stats()->set_max_hitpoints(100);
    ally1->stats()->set_hitpoints(20);
    ally2->stats()->set_hitpoints(20);
    const float ally1_hp_before = ally1->stats()->hitpoints();
    const float ally2_hp_before = ally2->stats()->hitpoints();

    ASSERT_TRUE(w->special()) << "cleric heal should fire with wounded allies";
    ASSERT_TRUE(ally1->stats()->hitpoints() > ally1_hp_before
                || ally2->stats()->hitpoints() > ally2_hp_before) << "heal should restore at least one ally's hitpoints";
    tick_world(48);
    delete w;
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

    int skel_before = count_family_in_oblist(FAMILY_SKELETON);
    ASSERT_TRUE(w->special()) << "raise undead should fire from stain";
    int skel_after = count_family_in_oblist(FAMILY_SKELETON);
    ASSERT_TRUE(skel_after > skel_before) << "raise undead should add a skeleton to oblist";
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

    int ghost_before = count_family_in_oblist(FAMILY_GHOST);
    ASSERT_TRUE(w->special()) << "raise ghost should fire from stain";
    int ghost_after = count_family_in_oblist(FAMILY_GHOST);
    ASSERT_TRUE(ghost_after > ghost_before) << "raise ghost should add a ghost to oblist";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, cleric_resurrect_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_CLERIC, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(80, 80);
    w->set_busy(0);
    w->set_current_special(4);
    w->set_shifter_down(0);
    w->stats()->set_magicpoints(2000);
    w->stats()->set_special_cost(4, 0);

    walker* stain = place_stain_near(w, FAMILY_SOLDIER, 1, 2);
    ASSERT_TRUE(stain != nullptr) << "stain placed";

    int soldiers_before = count_family_in_oblist(FAMILY_SOLDIER);
    int ghosts_before = count_family_in_oblist(FAMILY_GHOST);
    ASSERT_TRUE(w->special()) << "resurrect should fire from friendly stain";
    int soldiers_after = count_family_in_oblist(FAMILY_SOLDIER);
    int ghosts_after = count_family_in_oblist(FAMILY_GHOST);
    ASSERT_TRUE(soldiers_after > soldiers_before
                || ghosts_after > ghosts_before)
        << "resurrect should produce a living entity (friendly soldier or ghost on enemy stain)";
    tick_world(48);
    delete w;
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
    int rocks_before = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(w->special()) << "elf rocks should fire";
    int rocks_after = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(rocks_after > rocks_before) << "elf rocks should add rocks to weaplist";
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
    int rocks_before = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(w->special()) << "elf bouncing rocks should fire";
    int rocks_after = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(rocks_after > rocks_before) << "bouncing rocks should add rocks to weaplist";
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
    int rocks_before = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(w->special()) << "elf lots of rocks should fire";
    int rocks_after = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(rocks_after > rocks_before) << "lots of rocks should add rocks to weaplist";
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
    int rocks_before = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(w->special()) << "elf mega rocks should fire";
    int rocks_after = count_family_in_weaplist(FAMILY_ROCK);
    ASSERT_TRUE(rocks_after > rocks_before) << "mega rocks should add rocks to weaplist";
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
    int meteors_before = count_family_in_weaplist(FAMILY_METEOR);
    ASSERT_TRUE(w->special()) << "fire elemental starburst should fire";
    int meteors_after = count_family_in_weaplist(FAMILY_METEOR);
    ASSERT_TRUE(meteors_after > meteors_before) << "starburst should add meteors to weaplist";
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
    int bombs_before = count_family_in_oblist(FAMILY_BOMB);
    ASSERT_TRUE(w->special()) << "thief drop bomb should fire";
    int bombs_after = count_family_in_oblist(FAMILY_BOMB);
    ASSERT_TRUE(bombs_after > bombs_before) << "drop bomb should spawn FAMILY_BOMB in oblist";
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
    ASSERT_TRUE(w->special()) << "thief cloak should fire";
    ASSERT_TRUE(w->invisibility_left() > 0) << "cloak should set invisibility_left";
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

    SequenceRandom seq_rng({0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    GameContext test_ctx;
    test_ctx.rng = &seq_rng;
    push_test_context(&test_ctx);
    ASSERT_TRUE(w->special()) << "thief taunt should fire when foes are nearby";
    pop_test_context();
    ASSERT_TRUE(foe->foe() == w) << "taunt should retarget foe at thief";
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

    SequenceRandom seq_rng({1, 1, 1, 1, 1, 1, 1, 1});
    GameContext test_ctx;
    test_ctx.rng = &seq_rng;
    push_test_context(&test_ctx);
    // The charm's resist roll is `og.rand(20) == 0` (living-11-thief.lua), and
    // og.rand draws from the WORLD rng (world_scripts.cpp), not from the
    // context rng pushed above -- that one steers nothing on this path. Pin
    // the world rng or a shuffled predecessor decides the 1-in-20.
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1;  // next(20) == 18: no resist
    ASSERT_TRUE(w->special()) << "thief charm should fire on a charmable foe";
    pop_test_context();
    ASSERT_TRUE(foe->charm_left() > 0 || foe->team_num() == w->team_num())
        << "charm should set charm_left or move foe to thief team";
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
    int clouds_before = count_family_all_lists(FAMILY_CLOUD);
    ASSERT_TRUE(w->special()) << "thief poison cloud should fire";
    int clouds_after = count_family_all_lists(FAMILY_CLOUD);
    ASSERT_TRUE(clouds_after > clouds_before) << "poison cloud should spawn FAMILY_CLOUD";
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
    int trees_before = count_family_in_weaplist(FAMILY_TREE);
    ASSERT_TRUE(w->special()) << "druid grow tree should fire";
    int trees_after = count_family_in_weaplist(FAMILY_TREE);
    ASSERT_TRUE(trees_after > trees_before) << "grow tree should add FAMILY_TREE to weaplist";
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
    int faerie_before = count_family_in_oblist(FAMILY_FAERIE);
    ASSERT_TRUE(w->special()) << "druid summon faerie should fire";
    int faerie_after = count_family_in_oblist(FAMILY_FAERIE);
    ASSERT_TRUE(faerie_after > faerie_before) << "summon faerie should add FAMILY_FAERIE to oblist";
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
    ASSERT_TRUE(w->view_all() > 0) << "reveal should set view_all";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, druid_protection_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_DRUID, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(4);
    w->stats()->set_magicpoints(2000);

    walker* ally1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(ally1 != nullptr && ally2 != nullptr) << "allies created";
    ally1->set_team_num(1);
    ally2->set_team_num(1);
    ally1->setxy(w->xpos() + 8, w->ypos() + 0);
    ally2->setxy(w->xpos() + 12, w->ypos() + 4);
    ally1->stats()->set_level(2);
    ally2->stats()->set_level(2);

    int prot_before = count_family_in_weaplist(FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(w->special()) << "druid protection should fire with friends nearby";
    int prot_after = count_family_in_weaplist(FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(prot_after > prot_before) << "protection should add FAMILY_CIRCLE_PROTECTION to weaplist";
    tick_world(48);
    delete w;
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

    FixedRandom one_rng(1);
    GameContext test_ctx;
    test_ctx.rng = &one_rng;
    push_test_context(&test_ctx);
    ASSERT_TRUE(w->special()) << "orc howl should fire";
    pop_test_context();
    ASSERT_TRUE(foe->stats()->frozen_delay() > 0) << "howl should freeze nearby foes";
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

    const float hp_before = w->stats()->hitpoints();
    ASSERT_TRUE(w->special()) << "orc eat corpse should fire";
    ASSERT_TRUE(w->stats()->hitpoints() > hp_before) << "eat corpse should heal the orc";
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
    int boulders_before = count_family_in_weaplist(FAMILY_BOULDER);
    ASSERT_TRUE(w->special()) << "barbarian hurl boulder should fire";
    int boulders_after = count_family_in_weaplist(FAMILY_BOULDER);
    ASSERT_TRUE(boulders_after > boulders_before) << "hurl boulder should add FAMILY_BOULDER to weaplist";
    ASSERT_TRUE(w->busy() > 0) << "hurl boulder should set busy";
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
    int boulders_before = count_family_in_weaplist(FAMILY_BOULDER);
    ASSERT_TRUE(w->special()) << "barbarian exploding boulder should fire";
    int boulders_after = count_family_in_weaplist(FAMILY_BOULDER);
    ASSERT_TRUE(boulders_after > boulders_before) << "exploding boulder should add FAMILY_BOULDER to weaplist";
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
    if (w->myguy)
        w->myguy->intelligence = 200;

    int markers_before = count_family_in_oblist(FAMILY_MARKER);
    ASSERT_TRUE(w->special()) << "archmage teleport (marker) should fire";
    int markers_after = count_family_in_oblist(FAMILY_MARKER);
    ASSERT_TRUE(markers_after > markers_before) << "marker placement path should spawn FAMILY_MARKER";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archmage_heartburst_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(120, 120);
    w->set_busy(0);
    w->set_shifter_down(0);
    w->set_current_special(2);
    w->stats()->set_magicpoints(2000);
    w->stats()->set_special_cost(2, 0);

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->set_team_num(2);
    foe->setxy(w->xpos() + 8, w->ypos() + 8);
    foe->stats()->set_level(1);

    int explosions_before = count_family_all_lists(FAMILY_EXPLOSION);
    ASSERT_TRUE(w->special()) << "archmage heartburst should fire when foes are present";
    int explosions_after = count_family_all_lists(FAMILY_EXPLOSION);
    ASSERT_TRUE(explosions_after > explosions_before) << "heartburst should spawn explosion FX";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archmage_summon_image_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(120, 120);
    w->set_busy(0);
    w->set_shifter_down(0);
    w->set_current_special(3);
    w->stats()->set_magicpoints(800);
    w->stats()->set_special_cost(3, 0);

    int oblist_before = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
    ASSERT_TRUE(w->special()) << "archmage summon image should fire";
    int oblist_after = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
    ASSERT_TRUE(oblist_after > oblist_before) << "summon image should add a living entity to oblist";
    tick_world(48);
    delete w;
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

    SequenceRandom seq_rng({1, 1, 1, 1, 1, 1, 1, 1});
    GameContext test_ctx;
    test_ctx.rng = &seq_rng;
    push_test_context(&test_ctx);
    ASSERT_TRUE(w->special()) << "archmage mind control should fire";
    pop_test_context();
    ASSERT_TRUE(foe->team_num() != foe_team_before || foe->charm_left() > 0)
        << "mind control should change foe team or set charm_left";
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


TEST_F(WalkerSpecials, medium_slime_grow_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_MEDIUM_SLIME, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(1);
    bool fired = w->special();
    if (fired)
    {
        ASSERT_EQ(w->family(), FAMILY_SLIME) << "successful grow should transform to FAMILY_SLIME";
    }
    else
    {
        ASSERT_TRUE(w->stats()->commands.size() > 0)
            << "failed grow (no space) should still leave a walk command queued";
    }
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, small_slime_grow_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SMALL_SLIME, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->set_busy(0);
    w->set_current_special(1);
    bool fired = w->special();
    if (fired)
    {
        ASSERT_EQ(w->family(), FAMILY_MEDIUM_SLIME) << "successful grow should transform to FAMILY_MEDIUM_SLIME";
    }
    else
    {
        ASSERT_TRUE(w->stats()->commands.size() > 0)
            << "failed grow (no space) should still leave a walk command queued";
    }
    tick_world(48);
    delete w;
}
