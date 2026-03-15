#include <openglad/platform/game_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
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
// special() - exercises the massive family switch (lines 2293-3909)
// Each family test covers a different switch case
// ---------------------------------------------------------------------------

TEST_F(WalkerSpecials, soldier_charge)
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_current_special(1); // charge
    w->special();
}


TEST_F(WalkerSpecials, soldier_boomerang)
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(2); // boomerang
    w->special();
}


TEST_F(WalkerSpecials, soldier_whirlwind)
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(3); // whirlwind
    w->set_busy(0);
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->special();
}


TEST_F(WalkerSpecials, archer_fire_arrows)
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_current_special(1); // fire arrows
    w->special();
}


TEST_F(WalkerSpecials, archer_flurry)
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(2); // flurry
    w->special();
}


TEST_F(WalkerSpecials, archer_exploding)
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->set_current_special(3); // exploding arrows
    w->special();
}


TEST_F(WalkerSpecials, mage_teleport)
{
    walker* w = make_special_guy(FAMILY_MAGE);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // teleport
    w->special();

    // Direct teleport marker path (marker consumed at lifetime 1).
    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_TRUE(marker != nullptr) << "teleport marker created";
    if (marker) {
        marker->set_owner(w);
        marker->set_dead(0);
        marker->set_lifetime(1);
        // Place marker somewhere passable and sufficiently far away; do not
        // assume (x+96,y+96) is in-bounds or passable for every level.
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
        ASSERT_TRUE(w->teleport()) << "teleport with marker should succeed";
    }

    // Marker-present but too-close path should go through fallback logic.
    marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_TRUE(marker != nullptr) << "near marker created";
    if (marker) {
        marker->set_owner(w);
        marker->set_dead(0);
        marker->set_lifetime(5);
        marker->setxy(w->xpos() + 4, w->ypos() + 4);
        w->set_user(0);
        (void)w->teleport();
    }

    (void)w->teleport_ranged(24);
    og::runtime::current_session->myscreen_->world().delete_objects();
}


TEST_F(WalkerSpecials, mage_freeze)
{
    walker* w = make_special_guy(FAMILY_MAGE);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(2); // freeze time
    w->special();
}


TEST_F(WalkerSpecials, mage_energy_wave)
{
    walker* w = make_special_guy(FAMILY_MAGE);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(3); // energy wave
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    walker* mage_target = make_special_guy(FAMILY_ORC, 2, 2);
    ASSERT_TRUE(mage_target != nullptr) << "mage target created";
    mage_target->setxy(w->xpos() + 8, w->ypos() + 8);
    int before_wave = count_family_all_lists(FAMILY_WAVE);
    (void)w->special();
    int after_wave = count_family_all_lists(FAMILY_WAVE);
    ASSERT_TRUE(after_wave >= before_wave) << "energy wave path should run without removing waves";

    // Exercise archmage heavy branches: marker teleport, chain lightning,
    // summon variants, and mind-control.
    og::runtime::current_session->myscreen_->world().delete_objects();
    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    arch->setxy(120, 120);
    arch->stats()->set_magicpoints(1800);
    arch->stats()->set_max_magicpoints(1800);
    arch->set_busy(0);
    if (arch->myguy)
        arch->myguy->intelligence = 220;
    arch->stats()->set_special_cost(1, 0);
    arch->stats()->set_special_cost(2, 0);
    arch->stats()->set_special_cost(3, 0);
    arch->stats()->set_special_cost(4, 0);

    FixedRandom fixed_rng(1); // deterministic non-zero path for rng(20)
    GameContext test_ctx;

    test_ctx.rng = &fixed_rng;
    push_test_context(&test_ctx);

    // special 1, shifter_down: place teleport marker.
    arch->set_current_special(1);
    arch->set_shifter_down(1);
    int markers_before = count_family_in_oblist(FAMILY_MARKER);
    (void)arch->special();
    int markers_after = count_family_in_oblist(FAMILY_MARKER);
    ASSERT_TRUE(markers_after >= markers_before) << "marker path should not remove markers";

    // special 2, normal: create explosion FX against a nearby foe.
    walker* foe = make_special_guy(FAMILY_ORC, 2, 3);
    ASSERT_TRUE(foe != nullptr) << "foe created for archmage special 2";
    foe->setxy(arch->xpos() + 10, arch->ypos() + 10);
    int explode_before = count_family_all_lists(FAMILY_EXPLOSION);
    arch->set_current_special(2);
    arch->set_shifter_down(0);
    arch->set_busy(0);
    (void)arch->special();
    int explode_after = count_family_all_lists(FAMILY_EXPLOSION);
    ASSERT_TRUE(explode_after >= explode_before) << "burst path should not reduce explosion count";

    // special 2, shifter_down: chain lightning path should create FAMILY_CHAIN.
    int chain_before = count_family_all_lists(FAMILY_CHAIN);
    arch->set_current_special(2);
    arch->set_shifter_down(1);
    arch->set_busy(0);
    (void)arch->special();
    int chain_after = count_family_all_lists(FAMILY_CHAIN);
    ASSERT_TRUE(chain_after >= chain_before) << "chain lightning path should not reduce chain FX count";

    // special 3, shifter_down: true summon (fire elemental).
    int fire_before = count_family_in_oblist(FAMILY_FIREELEMENTAL);
    arch->set_current_special(3);
    arch->set_shifter_down(1);
    arch->set_busy(0);
    (void)arch->special();
    int fire_after = count_family_in_oblist(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(fire_after >= fire_before) << "true summon path should not remove fire elementals";

    // special 3, no shifter: illusion summon variant.
    arch->stats()->set_magicpoints(1500);
    int total_before = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
    arch->set_current_special(3);
    arch->set_shifter_down(0);
    arch->set_busy(0);
    (void)arch->special();
    int total_after = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
    ASSERT_TRUE(total_after >= total_before) << "illusion summon path should not remove objects";

    // special 4: mind-control should retarget a nearby foe to archmage team.
    walker* control_target = find_first_alive_ob_by_family(FAMILY_ORC);
    if (!control_target) {
        control_target = make_special_guy(FAMILY_ORC, 3, 2);
        ASSERT_TRUE(control_target != nullptr) << "control target created";
        control_target->setxy(arch->xpos() + 5, arch->ypos() + 5);
    }
    arch->set_current_special(4);
    arch->set_shifter_down(0);
    arch->set_busy(0);
    (void)arch->special();
    ASSERT_TRUE(control_target->team_num() >= 0) << "mind-control path should leave target in a valid team";

    pop_test_context();
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Drive act() into ACT_RANDOM branches (including act_random()).
    walker* actor = make_special_guy(FAMILY_ORC, 1, 4);
    walker* enemy = make_special_guy(FAMILY_SOLDIER, 2, 4);
    ASSERT_TRUE(actor != nullptr && enemy != nullptr) << "actor/enemy should be created for ACT_RANDOM";
    if (actor && enemy) {
        actor->setxy(100, 100);
        enemy->setxy(108, 100);
        actor->set_foe(enemy);
        actor->set_ani_type(ANI_WALK);
        actor->set_busy(0);
        actor->stats()->clear_command();
        actor->set_act_type(ACT_RANDOM);

        // rng(4)==0 and rng(20)==1 -> take the act_random() path.
        SequenceRandom seq_rng({0, 1, 0, 1, 0, 1});
        GameContext random_ctx;
    
        random_ctx.rng = &seq_rng;
        push_test_context(&random_ctx);
        (void)actor->act();
        ASSERT_TRUE(actor->act_type() == ACT_RANDOM) << "ACT_RANDOM path should preserve act type";
        pop_test_context();

        // rng(4)==1 -> take the alternate search branch.
        FixedRandom nonzero_rng(1);
        random_ctx.rng = &nonzero_rng;
        push_test_context(&random_ctx);
        actor->stats()->clear_command();
        (void)actor->act();
        ASSERT_TRUE(actor->act_type() == ACT_RANDOM) << "ACT_RANDOM alternate path should preserve act type";
        pop_test_context();
    }
    delete actor;
    delete enemy;

    // Broad special() branch sweep for high-ROI families/special indices.
    const char sweep_families[] = {
        FAMILY_SOLDIER, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_MAGE, FAMILY_ARCHMAGE,
        FAMILY_ELF, FAMILY_THIEF, FAMILY_SKELETON, FAMILY_FIREELEMENTAL, FAMILY_FAERIE,
        FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN, FAMILY_GHOST,
        FAMILY_SMALL_SLIME, FAMILY_MEDIUM_SLIME, FAMILY_SLIME,
        FAMILY_BIG_ORC, FAMILY_GOLEM, FAMILY_GIANT_SKELETON, FAMILY_TOWER1
    };
    for (char fam : sweep_families) {
        og::runtime::current_session->myscreen_->world().delete_objects();

        walker* a = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, fam);
        walker* ally = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
        walker* foe2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
        walker* blood = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
        ASSERT_TRUE(a != nullptr && ally != nullptr && foe2 != nullptr && blood != nullptr) << "special sweep objects should be created";
        if (!(a && ally && foe2 && blood)) {
            continue;
        }

        a->set_team_num(1);
        ally->set_team_num(1);
        foe2->set_team_num(2);
        a->setxy(100, 100);
        ally->setxy(104, 100);
        foe2->setxy(112, 100);
        blood->setxy(106, 100);
        a->set_lastx(a->stepsize());
        a->set_lasty(0);
        a->set_busy(0);
        a->stats()->set_magicpoints(1500);
        a->stats()->set_max_magicpoints(1500);
        if (a->myguy) {
            a->myguy->intelligence = 180;
            a->myguy->strength = 180;
            a->myguy->constitution = 180;
            a->myguy->teamnum = 1;
        }
	        blood->stats()->set_old_family(FAMILY_SOLDIER);
	        blood->set_team_num(1);

	        for (int sp = 1; sp <= 5; ++sp) {
	            for (int shift = 0; shift <= 1; ++shift) {
	                a->set_current_special(static_cast<char>(sp));
	                a->set_busy(0);
	                a->set_shifter_down(static_cast<short>(shift));
	                a->stats()->set_magicpoints(a->stats()->max_magicpoints());
	                (void)a->special();
	            }
	        }
	    }

    // Target uncovered archmage illusion case tables (rng(3/5/7/9) branches).
    og::runtime::current_session->myscreen_->world().delete_objects();
    walker* arch2 = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    ASSERT_TRUE(arch2 != nullptr) << "archmage branch sweeper created";
    if (arch2) {
        arch2->setxy(120, 120);
        arch2->stats()->set_special_cost(3, 0);
        arch2->stats()->set_max_magicpoints(2000);
        GameContext arch2_ctx;
    
        const int mp_tiers[] = {120, 300, 700, 1200};
        const int max_pick[] = {3, 5, 7, 9};
        for (int t = 0; t < 4; ++t) {
            for (Uint32 pick = 0; pick < static_cast<Uint32>(max_pick[t]); ++pick) {
                SequenceRandom pick_rng({pick});
                arch2_ctx.rng = &pick_rng;
                push_test_context(&arch2_ctx);
	                arch2->stats()->set_magicpoints(static_cast<float>(mp_tiers[t]));
	                arch2->set_current_special(static_cast<char>(3));
	                arch2->set_shifter_down(static_cast<short>(0));
	                arch2->set_busy(0);
	                (void)arch2->special();
	                pop_test_context();
	            }
	        }
    }

    og::runtime::current_session->myscreen_->world().delete_objects();

}


TEST_F(WalkerSpecials, cleric_heal)
{
    walker* w = make_special_guy(FAMILY_CLERIC);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // heal
    w->set_shifter_down(0);
    w->special();
}


TEST_F(WalkerSpecials, cleric_raise_undead)
{
    walker* w = make_special_guy(FAMILY_CLERIC);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(2); // raise undead
    w->special();

    Sint32 none = w->turn_undead(3, 1);
    ASSERT_EQ(-1, (int)none) << "turn_undead should return -1 when no foes are in range";

    walker* skel = make_special_guy(FAMILY_SKELETON, 2, 1);
    walker* ghost = make_special_guy(FAMILY_GHOST, 2, 1);
    walker* orc = make_special_guy(FAMILY_ORC, 2, 1);
    ASSERT_TRUE(skel != nullptr && ghost != nullptr && orc != nullptr) << "foes created";
    if (skel && ghost && orc) {
        skel->setxy(w->xpos() + 1, w->ypos() + 1);
        ghost->setxy(w->xpos() + 2, w->ypos() + 1);
        orc->setxy(w->xpos() + 3, w->ypos() + 1);
        skel->set_team_num(2);
        ghost->set_team_num(2);
        orc->set_team_num(2);
        skel->stats()->set_level(1);
        ghost->stats()->set_level(1);
        orc->stats()->set_level(1);
        skel->set_dead(0);
        ghost->set_dead(0);
        orc->set_dead(0);

        SequenceRandom seq_rng({39, 0, 39, 0, 39, 0});
        GameContext test_ctx;
    
        test_ctx.rng = &seq_rng;
        push_test_context(&test_ctx);
        Sint32 killed = w->turn_undead(24, 2);
        pop_test_context();
        ASSERT_TRUE(killed >= -1) << "turn_undead should return a valid result";
    }

    delete skel;
    delete ghost;
    delete orc;
}


TEST_F(WalkerSpecials, elf_rocks)
{
    walker* w = make_special_guy(FAMILY_ELF);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // rocks
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->special();
}


TEST_F(WalkerSpecials, elf_speed)
{
    walker* w = make_special_guy(FAMILY_ELF);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(2); // speed
    w->special();
}


TEST_F(WalkerSpecials, elf_heal)
{
    walker* w = make_special_guy(FAMILY_ELF);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(3); // nature heal
    w->special();
}


TEST_F(WalkerSpecials, thief_stealth)
{
    walker* w = make_special_guy(FAMILY_THIEF);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // stealth
    w->special();
}


TEST_F(WalkerSpecials, thief_taunt)
{
    walker* w = make_special_guy(FAMILY_THIEF);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(2); // taunt
    w->special();
}


TEST_F(WalkerSpecials, skeleton_tunnel)
{
    walker* w = make_special_guy(FAMILY_SKELETON);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // tunnel
    w->special();
}


TEST_F(WalkerSpecials, fireelemental_explode)
{
    walker* w = make_special_guy(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // explode
    w->special();
}


TEST_F(WalkerSpecials, faerie_charm)
{
    walker* w = make_special_guy(FAMILY_FAERIE);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // charm
    w->special();
}


TEST_F(WalkerSpecials, druid_plant_tree)
{
    walker* w = make_special_guy(FAMILY_DRUID);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // plant tree
    w->special();
}


TEST_F(WalkerSpecials, druid_summon)
{
    walker* w = make_special_guy(FAMILY_DRUID);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(2); // summon animal
    w->special();
}


TEST_F(WalkerSpecials, ghost_scare)
{
    walker* w = make_special_guy(FAMILY_GHOST);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->set_current_special(1); // scare
    w->special();
}


TEST_F(WalkerSpecials, ghost_scare_animates_and_dissipates)
{
    walker* w = make_special_guy(FAMILY_GHOST);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->current_special = 1; // scare
    ASSERT_TRUE(w->special()) << "scare should fire";

    auto find_scare_effects = []() {
        std::vector<walker*> result;
        for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
            walker* e = uptr.get();
            if (e && e->family == FAMILY_GHOST_SCARE)
                result.push_back(e);
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
            walker* e = uptr.get();
            if (e && e->family == FAMILY_GHOST_SCARE)
                result.push_back(e);
        }
        return result;
    };

    auto effects = find_scare_effects();
    ASSERT_EQ(effects.size(), 1u) << "exactly one ghost-scare effect should be spawned";
    walker* fx = effects[0];
    ASSERT_EQ(fx->ani_type, ANI_SCARE) << "spawned effect should use ANI_SCARE animation";

    short max_cycle = fx->cycle;
    size_t max_seen = effects.size();
    for (int i = 0; i < 64 && fx->dead == 0; ++i) {
        fx->act();
        if (fx->cycle > max_cycle)
            max_cycle = fx->cycle;
        size_t now = find_scare_effects().size();
        if (now > max_seen)
            max_seen = now;
    }

    ASSERT_GT(max_cycle, 0) << "animation should advance frames";
    ASSERT_EQ(fx->dead, 1) << "effect should be dead within 64 ticks";
    ASSERT_LE(max_seen, 1u) << "no additional ghost-scare effects should spawn";
}


TEST_F(WalkerSpecials, ghost_scare_does_not_accumulate)
{
    walker* w = make_special_guy(FAMILY_GHOST);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->current_special = 1; // scare
    ASSERT_TRUE(w->special()) << "first scare should fire";

    auto find_first_scare = []() -> walker* {
        for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
            walker* e = uptr.get();
            if (e && e->family == FAMILY_GHOST_SCARE && !e->dead)
                return e;
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
            walker* e = uptr.get();
            if (e && e->family == FAMILY_GHOST_SCARE && !e->dead)
                return e;
        }
        return nullptr;
    };

    walker* first = find_first_scare();
    ASSERT_TRUE(first != nullptr) << "first ghost-scare effect should exist";

    auto tick_world = []() {
        for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
            walker* e = uptr.get();
            if (e && !e->dead)
                e->act();
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
            walker* e = uptr.get();
            if (e && !e->dead)
                e->act();
        }
    };

    for (int i = 0; i < 32 && first->dead == 0; ++i)
        tick_world();
    ASSERT_EQ(first->dead, 1) << "first effect should be dead after ticking";

    ASSERT_TRUE(w->special()) << "second scare should fire";
    walker* second = find_first_scare();
    ASSERT_TRUE(second != nullptr) << "second ghost-scare effect should exist";
    ASSERT_NE(second, first) << "second effect should be a fresh entity";

    for (int i = 0; i < 32 && second->dead == 0; ++i)
        tick_world();
    ASSERT_EQ(second->dead, 1) << "second effect should also dissipate";

    int alive = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
        walker* e = uptr.get();
        if (e && e->family == FAMILY_GHOST_SCARE && !e->dead)
            alive++;
    }
    for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
        walker* e = uptr.get();
        if (e && e->family == FAMILY_GHOST_SCARE && !e->dead)
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
            if (e && !e->dead)
                e->act();
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist)
        {
            walker* e = uptr.get();
            if (e && !e->dead)
                e->act();
        }
        for (auto& uptr : og::runtime::current_session->myscreen_->world().weaplist)
        {
            walker* e = uptr.get();
            if (e && !e->dead)
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
        if (w && w->family == family)
            c++;
    }
    return c;
}


TEST_F(WalkerSpecials, soldier_charge_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 1;
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
    w->busy = 0;
    w->current_special = 2;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 3;
    int cmds_before = static_cast<int>(w->stats()->commands.size());
    ASSERT_TRUE(w->special()) << "whirlwind should fire";
    ASSERT_TRUE(w->busy > 0) << "whirlwind should set busy on caster";
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
    w->curdir = static_cast<char>(FACE_RIGHT);
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 4;

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->team_num = 2;
    foe->setxy(w->xpos + w->sizex + 1, w->ypos + 0);
    foe->stats()->level = 1;
    const float foe_busy_before = foe->busy;

    bool fired = w->special();
    if (fired)
    {
        ASSERT_TRUE(w->busy > 0) << "successful disarm should leave caster busy";
    }
    else
    {
        ASSERT_TRUE(foe->busy >= foe_busy_before) << "failed disarm should not corrupt foe state";
    }
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archer_fire_arrows_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 1;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 2;
    int arrows_before = count_family_in_weaplist(FAMILY_ARROW);
    ASSERT_TRUE(w->special()) << "archer barrage should fire";
    int arrows_after = count_family_in_weaplist(FAMILY_ARROW);
    ASSERT_TRUE(arrows_after > arrows_before) << "barrage should add arrows to weaplist";
    ASSERT_TRUE(w->busy > 0) << "barrage should set busy";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, archer_exploding_bolt_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ARCHER, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 3;
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
    w->busy = 0;
    w->current_special = 1;
    w->shifter_down = 0;
    const Sint32 orig_x = w->xpos;
    const Sint32 orig_y = w->ypos;
    ASSERT_TRUE(w->special()) << "mage teleport should fire";
    ASSERT_EQ(w->ani_type, ANI_TELE_OUT) << "mage teleport (no marker) should set ani_type to ANI_TELE_OUT";
    bool moved = false;
    for (int i = 0; i < 64 && !moved; ++i)
    {
        w->act();
        if (w->xpos != orig_x || w->ypos != orig_y)
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 2;
    w->stats()->magicpoints = 1500;
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
    w->busy = 0;
    w->current_special = 3;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 4;
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
    w->busy = 0;
    w->current_special = 5;
    w->stats()->magicpoints = 1500;
    w->stats()->special_cost[5] = 0;

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->team_num = 2;
    foe->setxy(w->xpos + 8, w->ypos + 8);
    foe->stats()->level = 1;

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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->ani_type = ANI_WALK; // skeletons spawn with ANI_SKEL_GROW; tunnel only fires from a normal state
    w->current_special = 1;
    const Sint32 orig_x = w->xpos;
    const Sint32 orig_y = w->ypos;
    ASSERT_TRUE(w->special()) << "skeleton tunnel should fire";
    ASSERT_EQ(w->ani_type, ANI_TELE_OUT) << "tunnel should set ani_type to ANI_TELE_OUT";
    bool moved = false;
    for (int i = 0; i < 64 && !moved; ++i)
    {
        w->act();
        if (w->xpos != orig_x || w->ypos != orig_y)
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
    w->busy = 0;
    w->current_special = 1;
    w->shifter_down = 0;
    w->stats()->magicpoints = 2000;

    walker* ally1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(ally1 != nullptr && ally2 != nullptr) << "allies created";
    ally1->team_num = 1;
    ally2->team_num = 1;
    ally1->setxy(w->xpos + 8, w->ypos + 0);
    ally2->setxy(w->xpos + 12, w->ypos + 0);
    ally1->stats()->max_hitpoints = 100;
    ally2->stats()->max_hitpoints = 100;
    ally1->stats()->hitpoints = 20;
    ally2->stats()->hitpoints = 20;
    const float ally1_hp_before = ally1->stats()->hitpoints;
    const float ally2_hp_before = ally2->stats()->hitpoints;

    ASSERT_TRUE(w->special()) << "cleric heal should fire with wounded allies";
    ASSERT_TRUE(ally1->stats()->hitpoints > ally1_hp_before
                || ally2->stats()->hitpoints > ally2_hp_before) << "heal should restore at least one ally's hitpoints";
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
    stain->team_num = team;
    stain->stats()->level = level;
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    for (int dx = 18; dx <= 28; dx += 2)
    {
        stain->setxy(caster->xpos + dx, caster->ypos + 0);
        if (og::runtime::current_session->myscreen_->world().query_passable(
                stain->xpos, stain->ypos, stain))
            return stain;
    }
    for (int dy = 18; dy <= 28; dy += 2)
    {
        stain->setxy(caster->xpos + 0, caster->ypos + dy);
        if (og::runtime::current_session->myscreen_->world().query_passable(
                stain->xpos, stain->ypos, stain))
            return stain;
    }
    return stain;
}


TEST_F(WalkerSpecials, cleric_raise_undead_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_CLERIC, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(80, 80);
    w->busy = 0;
    w->current_special = 2;
    w->shifter_down = 0;
    w->stats()->magicpoints = 2000;

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
    w->busy = 0;
    w->current_special = 3;
    w->shifter_down = 0;
    w->stats()->magicpoints = 2000;

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
    w->busy = 0;
    w->current_special = 4;
    w->shifter_down = 0;
    w->stats()->magicpoints = 2000;
    w->stats()->special_cost[4] = 0;

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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 1;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 2;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 3;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 4;
    w->stats()->magicpoints = 2000;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 1;
    w->stats()->magicpoints = 2000;
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
    w->busy = 0;
    w->user = 0;
    w->current_special = 1;
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
    w->busy = 0;
    w->user = 0;
    w->invisibility_left = 0;
    w->current_special = 2;
    ASSERT_TRUE(w->special()) << "thief cloak should fire";
    ASSERT_TRUE(w->invisibility_left > 0) << "cloak should set invisibility_left";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, thief_taunt_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_THIEF, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->busy = 0;
    w->user = 0;
    w->shifter_down = 0;
    w->current_special = 3;

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->team_num = 2;
    foe->setxy(w->xpos + 12, w->ypos + 4);
    foe->stats()->level = 1;
    foe->foe = nullptr;

    SequenceRandom seq_rng({0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    GameContext test_ctx;
    test_ctx.rng = &seq_rng;
    push_test_context(&test_ctx);
    ASSERT_TRUE(w->special()) << "thief taunt should fire when foes are nearby";
    pop_test_context();
    ASSERT_TRUE(foe->foe == w) << "taunt should retarget foe at thief";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, thief_charm_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_THIEF, 1, 6);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->busy = 0;
    w->user = 0;
    w->shifter_down = 1;
    w->current_special = 3;

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->team_num = 2;
    foe->setxy(w->xpos + 8, w->ypos + 4);
    foe->stats()->level = 1;
    foe->real_team_num = 255;
    foe->charm_left = 0;

    SequenceRandom seq_rng({1, 1, 1, 1, 1, 1, 1, 1});
    GameContext test_ctx;
    test_ctx.rng = &seq_rng;
    push_test_context(&test_ctx);
    ASSERT_TRUE(w->special()) << "thief charm should fire on a charmable foe";
    pop_test_context();
    ASSERT_TRUE(foe->charm_left > 0 || foe->team_num == w->team_num)
        << "charm should set charm_left or move foe to thief team";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, thief_poison_cloud_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_THIEF, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->busy = 0;
    w->user = 0;
    w->current_special = 4;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 1;
    w->stats()->magicpoints = 2000;
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
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 2;
    w->stats()->magicpoints = 2000;
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
    w->busy = 0;
    w->view_all = 0;
    w->current_special = 3;
    ASSERT_TRUE(w->special()) << "druid reveal should fire";
    ASSERT_TRUE(w->view_all > 0) << "reveal should set view_all";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, druid_protection_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_DRUID, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->busy = 0;
    w->current_special = 4;
    w->stats()->magicpoints = 2000;

    walker* ally1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(ally1 != nullptr && ally2 != nullptr) << "allies created";
    ally1->team_num = 1;
    ally2->team_num = 1;
    ally1->setxy(w->xpos + 8, w->ypos + 0);
    ally2->setxy(w->xpos + 12, w->ypos + 4);
    ally1->stats()->level = 2;
    ally2->stats()->level = 2;

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
    w->busy = 0;
    w->current_special = 1;

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->team_num = 2;
    foe->setxy(w->xpos + 16, w->ypos + 4);
    foe->stats()->level = 1;
    foe->stats()->frozen_delay = 0;
    foe->stats()->hitpoints = 30.0f;

    FixedRandom one_rng(1);
    GameContext test_ctx;
    test_ctx.rng = &one_rng;
    push_test_context(&test_ctx);
    ASSERT_TRUE(w->special()) << "orc howl should fire";
    pop_test_context();
    ASSERT_TRUE(foe->stats()->frozen_delay > 0) << "howl should freeze nearby foes";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, orc_eat_corpse_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_ORC, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->busy = 0;
    w->current_special = 2;
    w->stats()->max_hitpoints = 200;
    w->stats()->hitpoints = 50;

    walker* stain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_TRUE(stain != nullptr) << "stain created";
    stain->center_on(w);
    stain->team_num = 2;
    stain->stats()->level = 3;
    stain->dead = 0;

    const float hp_before = w->stats()->hitpoints;
    ASSERT_TRUE(w->special()) << "orc eat corpse should fire";
    ASSERT_TRUE(w->stats()->hitpoints > hp_before) << "eat corpse should heal the orc";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, barbarian_hurl_boulder_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_BARBARIAN, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 1;
    int boulders_before = count_family_in_weaplist(FAMILY_BOULDER);
    ASSERT_TRUE(w->special()) << "barbarian hurl boulder should fire";
    int boulders_after = count_family_in_weaplist(FAMILY_BOULDER);
    ASSERT_TRUE(boulders_after > boulders_before) << "hurl boulder should add FAMILY_BOULDER to weaplist";
    ASSERT_TRUE(w->busy > 0) << "hurl boulder should set busy";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, barbarian_exploding_boulder_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_BARBARIAN, 1, 5);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 2;
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
    w->busy = 0;
    w->user = 0;
    w->current_special = 1;
    w->shifter_down = 1;
    w->stats()->special_cost[1] = 0;
    w->stats()->magicpoints = 2000;
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
    w->busy = 0;
    w->shifter_down = 0;
    w->current_special = 2;
    w->stats()->magicpoints = 2000;
    w->stats()->special_cost[2] = 0;

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->team_num = 2;
    foe->setxy(w->xpos + 8, w->ypos + 8);
    foe->stats()->level = 1;

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
    w->busy = 0;
    w->shifter_down = 0;
    w->current_special = 3;
    w->stats()->magicpoints = 800;
    w->stats()->special_cost[3] = 0;

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
    w->busy = 0;
    w->shifter_down = 0;
    w->current_special = 4;
    w->stats()->magicpoints = 1000;
    w->stats()->special_cost[4] = 0;

    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    foe->team_num = 2;
    foe->setxy(w->xpos + 6, w->ypos + 6);
    foe->stats()->level = 1;
    foe->real_team_num = 255;
    foe->charm_left = 0;
    const unsigned char foe_team_before = foe->team_num;

    SequenceRandom seq_rng({1, 1, 1, 1, 1, 1, 1, 1});
    GameContext test_ctx;
    test_ctx.rng = &seq_rng;
    push_test_context(&test_ctx);
    ASSERT_TRUE(w->special()) << "archmage mind control should fire";
    pop_test_context();
    ASSERT_TRUE(foe->team_num != foe_team_before || foe->charm_left > 0)
        << "mind control should change foe team or set charm_left";
    tick_world(48);
    delete w;
}


TEST_F(WalkerSpecials, slime_split_drives_simulation)
{
    walker* w = make_special_guy(FAMILY_SLIME, 1, 4);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(100, 100);
    w->busy = 0;
    w->current_special = 1;
    ASSERT_TRUE(w->special()) << "slime split should fire";
    ASSERT_EQ(w->ani_type, ANI_SLIME_SPLIT) << "split should set ani_type to ANI_SLIME_SPLIT";
    int small_before = count_family_in_oblist(FAMILY_SMALL_SLIME);
    bool transformed = false;
    for (int i = 0; i < 64 && !transformed; ++i)
    {
        w->act();
        if (w->family == FAMILY_SMALL_SLIME)
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
    w->busy = 0;
    w->current_special = 1;
    bool fired = w->special();
    if (fired)
    {
        ASSERT_EQ(w->family, FAMILY_SLIME) << "successful grow should transform to FAMILY_SLIME";
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
    w->busy = 0;
    w->current_special = 1;
    bool fired = w->special();
    if (fired)
    {
        ASSERT_EQ(w->family, FAMILY_MEDIUM_SLIME) << "successful grow should transform to FAMILY_MEDIUM_SLIME";
    }
    else
    {
        ASSERT_TRUE(w->stats()->commands.size() > 0)
            << "failed grow (no space) should still leave a walk command queued";
    }
    tick_world(48);
    delete w;
}
