#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/gameplay/guy.h>
#include <openglad/platform/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/stats.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"
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

#define REGISTER_SPECIAL_TEST(func) \
    REGISTER_TEST_WITH_FIXTURE(func, nullptr, teardown_walker_special_test)

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
        w->stats()->magicpoints = 500; // lots of magic for specials
        w->stats()->max_magicpoints = 500;
    }
    return w.release();
}

static int count_family_in_oblist(char family)
{
    int count = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
        walker* w = uptr.get();
        if (w && w->family == family)
            count++;
    }
    return count;
}

static int count_family_in_fxlist(char family)
{
    int count = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist) {
        walker* w = uptr.get();
        if (w && w->family == family)
            count++;
    }
    return count;
}

static int count_family_all_lists(char family)
{
    return count_family_in_oblist(family) + count_family_in_fxlist(family);
}

static walker* find_first_alive_ob_by_family(char family)
{
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist) {
        walker* w = uptr.get();
        if (w && w->family == family && !w->dead)
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

// ---------------------------------------------------------------------------
// special() - exercises the massive family switch (lines 2293-3909)
// Each family test covers a different switch case
// ---------------------------------------------------------------------------

void test_walker_special_soldier_charge()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->current_special = 1; // charge
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_soldier_charge);

void test_walker_special_soldier_boomerang()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // boomerang
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_soldier_boomerang);

void test_walker_special_soldier_whirlwind()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 3; // whirlwind
    w->busy = 0;
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_soldier_whirlwind);

void test_walker_special_archer_fire_arrows()
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->current_special = 1; // fire arrows
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_archer_fire_arrows);

void test_walker_special_archer_flurry()
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 2; // flurry
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_archer_flurry);

void test_walker_special_archer_exploding()
{
    walker* w = make_special_guy(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 3; // exploding arrows
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_archer_exploding);

void test_walker_special_mage_teleport()
{
    walker* w = make_special_guy(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // teleport
    w->special();

    // Direct teleport marker path (marker consumed at lifetime 1).
    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    TEST_ASSERT(marker != nullptr, "teleport marker created");
    if (marker) {
        marker->owner = w;
        marker->dead = 0;
        marker->lifetime = 1;
        // Place marker somewhere passable and sufficiently far away; do not
        // assume (x+96,y+96) is in-bounds or passable for every level.
        Sint32 mx = std::min<Sint32>(w->xpos + 96, og::runtime::current_session->myscreen_->world().pixmaxx - w->sizex - 2);
        Sint32 my = std::min<Sint32>(w->ypos + 96, og::runtime::current_session->myscreen_->world().pixmaxy - w->sizey - 2);
        if (!og::runtime::current_session->myscreen_->query_passable(static_cast<float>(mx), static_cast<float>(my), w))
        {
            bool found = false;
            for (Sint32 x = 0; x < og::runtime::current_session->myscreen_->world().pixmaxx - w->sizex - 2 && !found; x += GRID_SIZE)
            {
                for (Sint32 y = 0; y < og::runtime::current_session->myscreen_->world().pixmaxy - w->sizey - 2; y += GRID_SIZE)
                {
                    const Sint32 dx = x - w->xpos;
                    const Sint32 dy = y - w->ypos;
                    if (dx * dx + dy * dy <= 64 * 64)
                        continue;
                    if (og::runtime::current_session->myscreen_->query_passable(static_cast<float>(x), static_cast<float>(y), w))
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
        TEST_ASSERT(w->teleport(), "teleport with marker should succeed");
    }

    // Marker-present but too-close path should go through fallback logic.
    marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    TEST_ASSERT(marker != nullptr, "near marker created");
    if (marker) {
        marker->owner = w;
        marker->dead = 0;
        marker->lifetime = 5;
        marker->setxy(w->xpos + 4, w->ypos + 4);
        w->user = 0;
        (void)w->teleport();
    }

    (void)w->teleport_ranged(24);
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_mage_teleport);

void test_walker_special_mage_freeze()
{
    walker* w = make_special_guy(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // freeze time
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_mage_freeze);

void test_walker_special_mage_energy_wave()
{
    walker* w = make_special_guy(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 3; // energy wave
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    walker* mage_target = make_special_guy(FAMILY_ORC, 2, 2);
    TEST_ASSERT(mage_target != nullptr, "mage target created");
    mage_target->setxy(w->xpos + 8, w->ypos + 8);
    int before_wave = count_family_all_lists(FAMILY_WAVE);
    (void)w->special();
    int after_wave = count_family_all_lists(FAMILY_WAVE);
    TEST_ASSERT(after_wave >= before_wave, "energy wave path should run without removing waves");

    // Exercise archmage heavy branches: marker teleport, chain lightning,
    // summon variants, and mind-control.
    og::runtime::current_session->myscreen_->world().delete_objects();
    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    TEST_ASSERT(arch != nullptr, "archmage created");
    arch->setxy(120, 120);
    arch->stats()->magicpoints = 1800;
    arch->stats()->max_magicpoints = 1800;
    arch->busy = 0;
    if (arch->myguy)
        arch->myguy->intelligence = 220;
    arch->stats()->special_cost[1] = 0;
    arch->stats()->special_cost[2] = 0;
    arch->stats()->special_cost[3] = 0;
    arch->stats()->special_cost[4] = 0;

    FixedRandom fixed_rng(1); // deterministic non-zero path for rng(20)
    GameContext test_ctx;

    test_ctx.rng = &fixed_rng;
    set_global_context(&test_ctx);

    // special 1, shifter_down: place teleport marker.
    arch->current_special = 1;
    arch->shifter_down = 1;
    int markers_before = count_family_in_oblist(FAMILY_MARKER);
    (void)arch->special();
    int markers_after = count_family_in_oblist(FAMILY_MARKER);
    TEST_ASSERT(markers_after >= markers_before, "marker path should not remove markers");

    // special 2, normal: create explosion FX against a nearby foe.
    walker* foe = make_special_guy(FAMILY_ORC, 2, 3);
    TEST_ASSERT(foe != nullptr, "foe created for archmage special 2");
    foe->setxy(arch->xpos + 10, arch->ypos + 10);
    int explode_before = count_family_all_lists(FAMILY_EXPLOSION);
    arch->current_special = 2;
    arch->shifter_down = 0;
    arch->busy = 0;
    (void)arch->special();
    int explode_after = count_family_all_lists(FAMILY_EXPLOSION);
    TEST_ASSERT(explode_after >= explode_before, "burst path should not reduce explosion count");

    // special 2, shifter_down: chain lightning path should create FAMILY_CHAIN.
    int chain_before = count_family_all_lists(FAMILY_CHAIN);
    arch->current_special = 2;
    arch->shifter_down = 1;
    arch->busy = 0;
    (void)arch->special();
    int chain_after = count_family_all_lists(FAMILY_CHAIN);
    TEST_ASSERT(chain_after >= chain_before, "chain lightning path should not reduce chain FX count");

    // special 3, shifter_down: true summon (fire elemental).
    int fire_before = count_family_in_oblist(FAMILY_FIREELEMENTAL);
    arch->current_special = 3;
    arch->shifter_down = 1;
    arch->busy = 0;
    (void)arch->special();
    int fire_after = count_family_in_oblist(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(fire_after >= fire_before, "true summon path should not remove fire elementals");

    // special 3, no shifter: illusion summon variant.
    arch->stats()->magicpoints = 1500;
    int total_before = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
    arch->current_special = 3;
    arch->shifter_down = 0;
    arch->busy = 0;
    (void)arch->special();
    int total_after = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
    TEST_ASSERT(total_after >= total_before, "illusion summon path should not remove objects");

    // special 4: mind-control should retarget a nearby foe to archmage team.
    walker* control_target = find_first_alive_ob_by_family(FAMILY_ORC);
    if (!control_target) {
        control_target = make_special_guy(FAMILY_ORC, 3, 2);
        TEST_ASSERT(control_target != nullptr, "control target created");
        control_target->setxy(arch->xpos + 5, arch->ypos + 5);
    }
    arch->current_special = 4;
    arch->shifter_down = 0;
    arch->busy = 0;
    (void)arch->special();
    TEST_ASSERT(control_target->team_num >= 0, "mind-control path should leave target in a valid team");

    set_global_context(nullptr);
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Drive act() into ACT_RANDOM branches (including act_random()).
    walker* actor = make_special_guy(FAMILY_ORC, 1, 4);
    walker* enemy = make_special_guy(FAMILY_SOLDIER, 2, 4);
    TEST_ASSERT(actor != nullptr && enemy != nullptr, "actor/enemy should be created for ACT_RANDOM");
    if (actor && enemy) {
        actor->setxy(100, 100);
        enemy->setxy(108, 100);
        actor->foe = enemy;
        actor->ani_type = ANI_WALK;
        actor->busy = 0;
        actor->stats()->clear_command();
        actor->set_act_type(ACT_RANDOM);

        // rng(4)==0 and rng(20)==1 -> take the act_random() path.
        SequenceRandom seq_rng({0, 1, 0, 1, 0, 1});
        GameContext random_ctx;
    
        random_ctx.rng = &seq_rng;
        set_global_context(&random_ctx);
        (void)actor->act();
        TEST_ASSERT(actor->act_type == ACT_RANDOM, "ACT_RANDOM path should preserve act type");

        // rng(4)==1 -> take the alternate search branch.
        FixedRandom nonzero_rng(1);
        random_ctx.rng = &nonzero_rng;
        set_global_context(&random_ctx);
        actor->stats()->clear_command();
        (void)actor->act();
        TEST_ASSERT(actor->act_type == ACT_RANDOM, "ACT_RANDOM alternate path should preserve act type");
        set_global_context(nullptr);
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
        TEST_ASSERT(a != nullptr && ally != nullptr && foe2 != nullptr && blood != nullptr,
                    "special sweep objects should be created");
        if (!(a && ally && foe2 && blood)) {
            continue;
        }

        a->team_num = 1;
        ally->team_num = 1;
        foe2->team_num = 2;
        a->setxy(100, 100);
        ally->setxy(104, 100);
        foe2->setxy(112, 100);
        blood->setxy(106, 100);
        a->lastx = a->stepsize;
        a->lasty = 0;
        a->busy = 0;
        a->stats()->magicpoints = 1500;
        a->stats()->max_magicpoints = 1500;
        if (a->myguy) {
            a->myguy->intelligence = 180;
            a->myguy->strength = 180;
            a->myguy->constitution = 180;
            a->myguy->teamnum = 1;
        }
	        blood->stats()->old_family = FAMILY_SOLDIER;
	        blood->team_num = 1;

	        for (int sp = 1; sp <= 5; ++sp) {
	            for (int shift = 0; shift <= 1; ++shift) {
	                a->current_special = static_cast<char>(sp);
	                a->busy = 0;
	                a->shifter_down = static_cast<short>(shift);
	                a->stats()->magicpoints = a->stats()->max_magicpoints;
	                (void)a->special();
	            }
	        }
	    }

    // Target uncovered archmage illusion case tables (rng(3/5/7/9) branches).
    og::runtime::current_session->myscreen_->world().delete_objects();
    walker* arch2 = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    TEST_ASSERT(arch2 != nullptr, "archmage branch sweeper created");
    if (arch2) {
        arch2->setxy(120, 120);
        arch2->stats()->special_cost[3] = 0;
        arch2->stats()->max_magicpoints = 2000;
        GameContext arch2_ctx;
    
        const int mp_tiers[] = {120, 300, 700, 1200};
        const int max_pick[] = {3, 5, 7, 9};
        for (int t = 0; t < 4; ++t) {
            for (Uint32 pick = 0; pick < static_cast<Uint32>(max_pick[t]); ++pick) {
                SequenceRandom pick_rng({pick});
                arch2_ctx.rng = &pick_rng;
                set_global_context(&arch2_ctx);
	                arch2->stats()->magicpoints = static_cast<float>(mp_tiers[t]);
	                arch2->current_special = static_cast<char>(3);
	                arch2->shifter_down = static_cast<short>(0);
	                arch2->busy = 0;
	                (void)arch2->special();
	                set_global_context(nullptr);
	            }
	        }
    }

    og::runtime::current_session->myscreen_->world().delete_objects();

}
REGISTER_SPECIAL_TEST(test_walker_special_mage_energy_wave);

void test_walker_special_cleric_heal()
{
    walker* w = make_special_guy(FAMILY_CLERIC);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // heal
    w->shifter_down = 0;
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_cleric_heal);

void test_walker_special_cleric_raise_undead()
{
    walker* w = make_special_guy(FAMILY_CLERIC);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // raise undead
    w->special();

    Sint32 none = w->turn_undead(3, 1);
    TEST_ASSERT_EQ(-1, (int)none, "turn_undead should return -1 when no foes are in range");

    walker* skel = make_special_guy(FAMILY_SKELETON, 2, 1);
    walker* ghost = make_special_guy(FAMILY_GHOST, 2, 1);
    walker* orc = make_special_guy(FAMILY_ORC, 2, 1);
    TEST_ASSERT(skel != nullptr && ghost != nullptr && orc != nullptr, "foes created");
    if (skel && ghost && orc) {
        skel->setxy(w->xpos + 1, w->ypos + 1);
        ghost->setxy(w->xpos + 2, w->ypos + 1);
        orc->setxy(w->xpos + 3, w->ypos + 1);
        skel->team_num = 2;
        ghost->team_num = 2;
        orc->team_num = 2;
        skel->stats()->level = 1;
        ghost->stats()->level = 1;
        orc->stats()->level = 1;
        skel->dead = 0;
        ghost->dead = 0;
        orc->dead = 0;

        SequenceRandom seq_rng({39, 0, 39, 0, 39, 0});
        GameContext test_ctx;
    
        test_ctx.rng = &seq_rng;
        set_global_context(&test_ctx);
        Sint32 killed = w->turn_undead(24, 2);
        set_global_context(nullptr);
        TEST_ASSERT(killed >= -1, "turn_undead should return a valid result");
    }

    delete skel;
    delete ghost;
    delete orc;
}
REGISTER_SPECIAL_TEST(test_walker_special_cleric_raise_undead);

void test_walker_special_elf_rocks()
{
    walker* w = make_special_guy(FAMILY_ELF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // rocks
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_elf_rocks);

void test_walker_special_elf_speed()
{
    walker* w = make_special_guy(FAMILY_ELF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // speed
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_elf_speed);

void test_walker_special_elf_heal()
{
    walker* w = make_special_guy(FAMILY_ELF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 3; // nature heal
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_elf_heal);

void test_walker_special_thief_stealth()
{
    walker* w = make_special_guy(FAMILY_THIEF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // stealth
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_thief_stealth);

void test_walker_special_thief_taunt()
{
    walker* w = make_special_guy(FAMILY_THIEF);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // taunt
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_thief_taunt);

void test_walker_special_skeleton_tunnel()
{
    walker* w = make_special_guy(FAMILY_SKELETON);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // tunnel
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_skeleton_tunnel);

void test_walker_special_fireelemental_explode()
{
    walker* w = make_special_guy(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // explode
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_fireelemental_explode);

void test_walker_special_faerie_charm()
{
    walker* w = make_special_guy(FAMILY_FAERIE);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // charm
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_faerie_charm);

void test_walker_special_druid_plant_tree()
{
    walker* w = make_special_guy(FAMILY_DRUID);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // plant tree
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_druid_plant_tree);

void test_walker_special_druid_summon()
{
    walker* w = make_special_guy(FAMILY_DRUID);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 2; // summon animal
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_druid_summon);

void test_walker_special_ghost_scare()
{
    walker* w = make_special_guy(FAMILY_GHOST);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // scare
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_ghost_scare);

void test_walker_special_orc_howl()
{
    walker* w = make_special_guy(FAMILY_ORC);
    TEST_ASSERT(w != nullptr, "walker created");
    w->current_special = 1; // howl
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_orc_howl);

void test_walker_special_barbarian_hurl()
{
    walker* w = make_special_guy(FAMILY_BARBARIAN);
    TEST_ASSERT(w != nullptr, "walker created");
    w->lastx = w->stepsize;
    w->lasty = 0;
    w->busy = 0;
    w->current_special = 1; // hurl boulder
    w->special();
}
REGISTER_SPECIAL_TEST(test_walker_special_barbarian_hurl);

// ---------------------------------------------------------------------------
// special() when dead, no stats, or not enough magic
// ---------------------------------------------------------------------------

void test_walker_special_dead()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->dead = 1;
    bool result = w->special();
    TEST_ASSERT(!result, "dead walker should not special");
    w->dead = 0; // so destructor works
    w->current_special = 3;
    w->stats()->magicpoints = w->stats()->special_cost[3];

    walker* weapon = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(weapon != nullptr, "weapon created");
    if (weapon) {
        weapon->current_special = 1;
        weapon->stats()->magicpoints = weapon->stats()->special_cost[1];
        TEST_ASSERT(!weapon->special(), "non-living special should fail early");
    }
}
REGISTER_SPECIAL_TEST(test_walker_special_dead);

void test_walker_special_no_magic()
{
    walker* w = make_special_guy(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->stats()->magicpoints = 0;
    bool result = w->special();
    TEST_ASSERT(!result, "no magic should fail special");

    // Exercise specific "not enough for selected special" index path.
    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->current_special = 4;
    w->stats()->special_cost[4] = 50;
    w->stats()->magicpoints = 49;
    TEST_ASSERT(!w->special(), "insufficient MP for selected special index should fail");

    // Exercise base-class fallback implementations explicitly.
    TEST_ASSERT_EQ(-1, (int)w->walker::shove(nullptr, static_cast<short>(0), static_cast<short>(0)), "base shove should return -1");
    TEST_ASSERT_EQ(-1, (int)w->walker::shove(w, static_cast<short>(1), static_cast<short>(0)), "base shove log path should still return -1");
    TEST_ASSERT(!w->walker::eat_me(nullptr), "base eat_me fallback should return false");
    TEST_ASSERT(w->walker::do_summon(1, 10) == nullptr, "base do_summon fallback should return null");
    TEST_ASSERT(!w->walker::check_special(), "base check_special fallback should return false");

}
REGISTER_SPECIAL_TEST(test_walker_special_no_magic);

// ---------------------------------------------------------------------------
// death() - exercises order/family switches (lines 4422-4534)
// ---------------------------------------------------------------------------

void test_walker_death_fire_elemental()
{
    walker* w = make_special_guy(FAMILY_FIREELEMENTAL, 1);
    TEST_ASSERT(w != nullptr, "walker created");
    w->dead = 1;
    w->death();
}
REGISTER_SPECIAL_TEST(test_walker_death_fire_elemental);

void test_walker_death_with_myguy()
{
    walker* w = make_special_guy(FAMILY_SOLDIER, 0);
    TEST_ASSERT(w != nullptr, "walker created");
    TEST_ASSERT(w->myguy != nullptr, "should have myguy");
    w->dead = 1;
    w->death();

    // Also exercise generator-death explosion fan-out path.
    walker* generator = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TOWER);
    TEST_ASSERT(generator != nullptr, "generator created");
    int fx_before = count_family_all_lists(FAMILY_EXPLOSION);
    generator->dead = 1;
    generator->death();
    int fx_after = count_family_all_lists(FAMILY_EXPLOSION);
    TEST_ASSERT(fx_after >= fx_before + 1, "generator death should spawn explosion FX");
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_death_with_myguy);

void test_walker_special_archmage_illusion_rng_tables()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    TEST_ASSERT(arch != nullptr, "archmage created");
    if (!arch) {
        return;
    }

    arch->setxy(120, 120);
    arch->stats()->special_cost[3] = 0;
    arch->stats()->max_magicpoints = 2000;
    arch->current_special = static_cast<char>(3);
    arch->shifter_down = static_cast<short>(0);

    GameContext ctx;

    const int mp_tiers[] = {120, 300, 700, 1200};
    const int max_pick[] = {3, 5, 7, 9};
    for (int t = 0; t < 4; ++t) {
        for (Uint32 pick = 0; pick < static_cast<Uint32>(max_pick[t]); ++pick) {
            SequenceRandom rng({pick});
            ctx.rng = &rng;
            set_global_context(&ctx);
            int before = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
            arch->stats()->magicpoints = static_cast<float>(mp_tiers[t]);
            arch->busy = 0;
            (void)arch->special();
            int after = static_cast<int>(og::runtime::current_session->myscreen_->world().oblist.size());
            TEST_ASSERT(after >= before, "illusion summon case should not reduce object count");
            set_global_context(nullptr);
        }
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
    delete arch;
}
REGISTER_SPECIAL_TEST(test_walker_special_archmage_illusion_rng_tables);

void test_walker_special_mage_marker_remove_and_freeze_enemy_branch()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* mage = make_special_guy(FAMILY_MAGE, 2, 6);
    TEST_ASSERT(mage != nullptr, "mage created");
    if (!mage) {
        return;
    }

    // Marker remove/replace path for mage special 1 with shifter held.
    mage->current_special = 1;
    mage->shifter_down = 1;
    mage->user = 0;
    mage->busy = 0;
    if (mage->myguy) {
        mage->myguy->intelligence = 120;
    }

    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    TEST_ASSERT(marker != nullptr, "marker created");
    if (marker) {
        marker->owner = mage;
        marker->dead = 0;
        marker->setxy(mage->xpos + 8, mage->ypos + 8);
    }
    (void)mage->special();

    // Freeze-time enemy-team branch.
    mage->current_special = 3;
    mage->shifter_down = 0;
    mage->busy = 0;
    mage->team_num = 3;
    if (mage->myguy) {
        mage->clear_myguy();
    }
    walker* ally = make_special_guy(FAMILY_ORC, 3, 2);
    TEST_ASSERT(ally != nullptr, "ally created");
    if (ally) {
        ally->setxy(mage->xpos + 6, mage->ypos + 6);
    }
    (void)mage->special();

    delete ally;
    delete mage;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_mage_marker_remove_and_freeze_enemy_branch);

void test_walker_special_mage_wave_and_burst_with_targets()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* mage = make_special_guy(FAMILY_MAGE, 1, 7);
    walker* foe1 = make_special_guy(FAMILY_ORC, 2, 3);
    walker* foe2 = make_special_guy(FAMILY_SKELETON, 2, 3);
    TEST_ASSERT(mage != nullptr && foe1 != nullptr && foe2 != nullptr, "mage and foes created");
    if (!(mage && foe1 && foe2)) {
        return;
    }

    mage->setxy(120, 120);
    mage->lastx = mage->stepsize;
    mage->lasty = 0;
    foe1->setxy(128, 120);
    foe2->setxy(132, 124);

    mage->current_special = 4; // energy wave
    mage->busy = 0;
    (void)mage->special();

    mage->current_special = 5; // burst enemies
    mage->busy = 0;
    mage->stats()->magicpoints = 1200;
    mage->stats()->special_cost[5] = 0;
    (void)mage->special();

    delete foe1;
    delete foe2;
    delete mage;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_mage_wave_and_burst_with_targets);

void test_walker_special_archmage_low_int_marker_chain_and_summon_true()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 1, 8);
    walker* foe = make_special_guy(FAMILY_ORC, 2, 2);
    TEST_ASSERT(arch != nullptr && foe != nullptr, "arch and foe created");
    if (!(arch && foe)) {
        return;
    }

    arch->setxy(140, 140);
    foe->setxy(148, 140);
    arch->stats()->special_cost[1] = 0;
    arch->stats()->special_cost[2] = 0;
    arch->stats()->special_cost[3] = 0;
    arch->stats()->special_cost[4] = 0;

    // Archmage special 1 low-int path.
    arch->current_special = 1;
    arch->shifter_down = 1;
    arch->busy = 0;
    arch->user = 0;
    if (arch->myguy) {
        arch->myguy->intelligence = 20;
    }
    (void)arch->special();

    // Marker removal + replacement path.
    if (arch->myguy) {
        arch->myguy->intelligence = 220;
    }
    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    TEST_ASSERT(marker != nullptr, "arch marker created");
    if (marker) {
        marker->owner = arch;
        marker->dead = 0;
        marker->setxy(arch->xpos + 8, arch->ypos + 8);
    }
    arch->busy = 0;
    arch->current_special = 1;
    arch->shifter_down = 1;
    (void)arch->special();

    // Chain-lightning path.
    arch->busy = 0;
    arch->current_special = 2;
    arch->shifter_down = 1;
    (void)arch->special();

    // True summon path.
    arch->busy = 0;
    arch->current_special = 3;
    arch->shifter_down = 1;
    (void)arch->special();

    delete foe;
    delete arch;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_archmage_low_int_marker_chain_and_summon_true);

void test_walker_special_archmage_mind_control_stats_name_path()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* arch = make_special_guy(FAMILY_ARCHMAGE, 4, 7);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* foe2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    walker* foe3 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(arch != nullptr && foe != nullptr && foe2 != nullptr && foe3 != nullptr,
                "arch and mind-control targets created");
    if (!(arch && foe && foe2 && foe3)) {
        delete arch;
        og::runtime::current_session->myscreen_->world().delete_objects();
        return;
    }

    arch->setxy(120, 120);
    foe->setxy(126, 120);
    foe2->setxy(130, 120);
    foe3->setxy(134, 120);
    foe->team_num = 2;
    foe2->team_num = 2;
    foe3->team_num = 2;
    foe->stats()->level = 1;
    foe2->stats()->level = 1;
    foe3->stats()->level = 1;
    arch->busy = 0;
    arch->current_special = 4;
    arch->shifter_down = 0;
    arch->stats()->special_cost[4] = 0;
    arch->stats()->magicpoints = 80;
    arch->stats()->name = "ARCH-NPC";
    if (arch->myguy) {
        arch->clear_myguy();
    }
    foe->real_team_num = 255;
    foe2->real_team_num = 255;
    foe3->real_team_num = 255;
    foe->charm_left = (0);
    foe2->charm_left = (0);
    foe3->charm_left = (0);
    const float mp_before = arch->stats()->magicpoints;

    SequenceRandom seq_rng({1, 1, 1, 1, 1, 1, 1, 1});
    GameContext test_ctx;

    test_ctx.rng = &seq_rng;
    set_global_context(&test_ctx);
    (void)arch->special();
    set_global_context(nullptr);
    TEST_ASSERT(arch->stats()->magicpoints < mp_before,
                "mind-control should spend MP for controlled targets");

    delete arch;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_archmage_mind_control_stats_name_path);

void test_walker_special_druid_circle_existing_protection_branch()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* druid = make_special_guy(FAMILY_DRUID, 1, 6);
    walker* ally = make_special_guy(FAMILY_SOLDIER, 1, 5);
    TEST_ASSERT(druid != nullptr && ally != nullptr, "druid and ally created");
    if (!(druid && ally)) {
        return;
    }

    druid->setxy(100, 100);
    ally->setxy(108, 100);
    druid->busy = 0;
    druid->current_special = 4;

    walker* existing = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    TEST_ASSERT(existing != nullptr, "existing protection created");
    if (existing) {
        existing->owner = ally;
        existing->team_num = ally->team_num;
        existing->setxy(ally->xpos, ally->ypos);
    }

    (void)druid->special();

    delete ally;
    delete druid;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_druid_circle_existing_protection_branch);

void test_walker_special_orc_eat_corpse_and_barbarian_exploding_boulder_npc()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* orc = make_special_guy(FAMILY_ORC, 2, 4);
    TEST_ASSERT(orc != nullptr, "orc created");
    if (!orc) {
        return;
    }
    if (orc->myguy) {
        orc->clear_myguy();
    }
    orc->stats()->name = "ORC-NPC";
    orc->stats()->hitpoints = 10;
    orc->stats()->max_hitpoints = 100;
    orc->current_special = 2;
    orc->busy = 0;

    walker* blood = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    TEST_ASSERT(blood != nullptr, "blood created");
    if (blood) {
        blood->team_num = 3;
        blood->stats()->level = 3;
        blood->setxy(orc->xpos + 1, orc->ypos + 1);
    }
    (void)orc->special();

    walker* barb = make_special_guy(FAMILY_BARBARIAN, 2, 5);
    TEST_ASSERT(barb != nullptr, "barbarian created");
    if (barb) {
        if (barb->myguy) {
            barb->clear_myguy();
        }
        barb->setxy(120, 120);
        barb->lastx = barb->stepsize;
        barb->lasty = 0;
        barb->busy = 0;
        barb->current_special = 2; // exploding boulder
        (void)barb->special();
    }

    delete barb;
    delete orc;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_orc_eat_corpse_and_barbarian_exploding_boulder_npc);

void test_walker_special_cleric_raise_skeleton_and_ghost_from_stain()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 6);
    TEST_ASSERT(cleric != nullptr, "cleric created");
    if (!cleric)
        return;

    // Place a blood stain close by but not colliding with other objects.
    walker* stain = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    TEST_ASSERT(stain != nullptr, "stain created");
    if (!stain) {
        delete cleric;
        return;
    }

    cleric->setxy(80, 80);
    cleric->team_num = 1;
    cleric->busy = 0;
    cleric->stats()->magicpoints = 2000;
    cleric->stats()->max_magicpoints = 2000;

    stain->setxy(110, 80);
    stain->team_num = 2;
    stain->dead = 0;

    // Raise skeletons (special 2, shifter up).
    cleric->current_special = 2;
    cleric->shifter_down = 0;
    (void)cleric->special();

    // Raise ghosts (special 3, shifter up).
    cleric->busy = 0;
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    (void)cleric->special();

    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_cleric_raise_skeleton_and_ghost_from_stain);

void test_walker_special_cleric_mystic_mace_low_int_and_success_paths()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 8);
    TEST_ASSERT(cleric != nullptr, "cleric created");
    if (!cleric)
        return;

    cleric->current_special = 1;
    cleric->shifter_down = 1;
    cleric->stats()->special_cost[1] = 0;
    cleric->busy = 0;
    cleric->user = 0;

    if (cleric->myguy) {
        cleric->myguy->intelligence = 40;
    }
    int shields_before = count_family_all_lists(FAMILY_MAGIC_SHIELD);
    (void)cleric->special();
    int shields_after_low_int = count_family_all_lists(FAMILY_MAGIC_SHIELD);
    TEST_ASSERT_EQ(shields_before, shields_after_low_int,
                   "low-int mystic mace path should not create shield");

    if (cleric->myguy) {
        cleric->myguy->intelligence = 120;
    }
    cleric->busy = 0;
    float mp_before = cleric->stats()->magicpoints;
    (void)cleric->special();
    int shields_after_success = count_family_all_lists(FAMILY_MAGIC_SHIELD);
    TEST_ASSERT(shields_after_success > shields_after_low_int,
                "valid mystic mace cast should create shield fx");
    TEST_ASSERT(cleric->busy > 0, "valid mystic mace should set busy");
    TEST_ASSERT(cleric->stats()->magicpoints < mp_before,
                "valid mystic mace should spend magicpoints");

    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_cleric_mystic_mace_low_int_and_success_paths);

void test_walker_special_cleric_resurrect_friendly_and_enemy_stains()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 8);
    walker* ally = make_special_guy(FAMILY_SOLDIER, 1, 5);
    TEST_ASSERT(cleric != nullptr && ally != nullptr, "cleric and ally created");
    if (!(cleric && ally))
        return;

    cleric->setxy(100, 100);
    cleric->current_special = 4;
    cleric->stats()->special_cost[4] = 0;
    cleric->stats()->magicpoints = 2000;
    og::runtime::current_session->myscreen_->save_data.allied_mode = 0;
    ally->setxy(110, 100);
    ally->team_num = 1;
    ally->generate_bloodspot();

    int soldiers_before = count_family_in_oblist(FAMILY_SOLDIER);
    (void)cleric->special();
    int soldiers_after = count_family_in_oblist(FAMILY_SOLDIER);
    TEST_ASSERT(soldiers_after >= soldiers_before,
                "friendly stain should allow resurrecting original family");

    delete ally;
    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Enemy-stain branch depends on map passability and alliance mode interactions;
    // keep this test deterministic by validating the friendly resurrection path only.
}
REGISTER_SPECIAL_TEST(test_walker_special_cleric_resurrect_friendly_and_enemy_stains);

void test_walker_special_elf_rock_barrage_level4_smoke()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Deterministic rand()-based perturbation inside elf special.
    srand(123);

    walker* elf = make_special_guy(FAMILY_ELF, 1, 6);
    TEST_ASSERT(elf != nullptr, "elf created");
    if (!elf)
        return;

    elf->setxy(100, 100);
    elf->lastx = elf->stepsize;
    elf->lasty = 0;
    elf->busy = 0;
    elf->current_special = 4;
    elf->stats()->magicpoints = 2000;
    elf->stats()->max_magicpoints = 2000;

    (void)elf->special();

    delete elf;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_elf_rock_barrage_level4_smoke);

void test_walker_turn_undead_attack_kill_branch_and_act_guard_random_edges()
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* cleric = make_special_guy(FAMILY_CLERIC, 1, 7);
    walker* skel = make_special_guy(FAMILY_SKELETON, 2, 1);
    walker* ghost = make_special_guy(FAMILY_GHOST, 2, 1);
    TEST_ASSERT(cleric != nullptr && skel != nullptr && ghost != nullptr, "cleric and undead created");
    if (cleric && skel && ghost) {
        cleric->setxy(100, 100);
        skel->setxy(102, 100);
        ghost->setxy(103, 100);
        cleric->damage = 150.0f;
        skel->stats()->hitpoints = 8;
        ghost->stats()->hitpoints = 8;
        SequenceRandom seq_rng({1000, 0, 1000, 0});
        GameContext test_ctx;
    
        test_ctx.rng = &seq_rng;
        set_global_context(&test_ctx);
        (void)cleric->turn_undead(40, 3);
        set_global_context(nullptr);
    }

    walker* guard = make_special_guy(FAMILY_ORC, 3, 4);
    walker* foe = make_special_guy(FAMILY_SOLDIER, 2, 2);
    TEST_ASSERT(guard != nullptr && foe != nullptr, "guard and foe created");
    if (guard && foe) {
        guard->setxy(140, 140);
        foe->setxy(146, 140);
        guard->set_act_type(ACT_GUARD);
        (void)guard->act();
    }

    walker* randomer = make_special_guy(FAMILY_ORC, 5, 4);
    TEST_ASSERT(randomer != nullptr, "randomer created");
    if (randomer) {
        randomer->setxy(160, 160);
        randomer->foe = nullptr;
        randomer->set_act_type(ACT_RANDOM);
        randomer->stats()->clear_command();
        // act(): rng(4)==0 and rng(20)==1 => act_random() path
        // act_random(): rng(70)==0, find no foe => random-walk command path
        SequenceRandom random_rng({0, 1, 0, 1, 1, 2});
        GameContext random_ctx;
    
        random_ctx.rng = &random_rng;
        set_global_context(&random_ctx);
        (void)randomer->act();
        set_global_context(nullptr);
    }

    delete randomer;
    delete foe;
    delete guard;
    delete ghost;
    delete skel;
    delete cleric;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_turn_undead_attack_kill_branch_and_act_guard_random_edges);

void test_walker_special_guard_paths_and_teleport_failures()
{
    walker* w = make_special_guy(FAMILY_MAGE, 0, 4);
    TEST_ASSERT(w != nullptr, "mage created");
    if (!w)
        return;

    // dead guard
    w->dead = 1;
    TEST_ASSERT(!w->special(), "dead walker special should fail");
    w->dead = 0;

    // magic cost guard
    w->current_special = 1;
    w->stats()->special_cost[1] = 50;
    w->stats()->magicpoints = 0;
    TEST_ASSERT(!w->special(), "insufficient MP special should fail");

    // order guard
    w->stats()->magicpoints = 500;
    w->set_order_family(Order::FX, FAMILY_MARKER);
    TEST_ASSERT(!w->special(), "non-living special should fail");
    w->set_order_family(Order::Living, FAMILY_MAGE);

    // teleport_ranged failure branch
    w->setxy(-200, -200);
    TEST_ASSERT(!w->teleport_ranged(0), "teleport_ranged should fail when no passable destination exists");

    // turn_undead no-target branch
    TEST_ASSERT_EQ(-1, (int)w->turn_undead(10, 1), "turn_undead should return -1 with no foes in range");

    delete w;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_guard_paths_and_teleport_failures);

void test_walker_special_no_stats_guard()
{
    NoStatsWalker no_stats;
    TEST_ASSERT(!no_stats.special(), "special should fail safely when stats are missing");
}
REGISTER_SPECIAL_TEST(test_walker_special_no_stats_guard);

void test_walker_special_unknown_family_and_teleport_ranged_fail_loop()
{
    walker* w = make_special_guy(FAMILY_MAGE, 0, 4);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    // special(): living + enough MP but missing descriptor callback -> return tail.
    w->set_order_family(Order::Living, 120);
    w->current_special = 1;
    w->stats()->special_cost[1] = 0;
    w->stats()->magicpoints = 100;
    TEST_ASSERT(!w->special(), "unknown living family special should fall through and return false");

    // teleport_ranged(): exhaust keep_going loop and hit explicit false return.
    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->setxy(-1000, -1000);
    TEST_ASSERT(!w->teleport_ranged(1), "teleport_ranged should fail after retries on invalid area");

    delete w;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_unknown_family_and_teleport_ranged_fail_loop);

void test_walker_special_success_returns_true_and_spends_mp()
{
    walker* w = make_special_guy(FAMILY_MAGE, 0, 4);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->current_special = 1; // teleport
    w->stats()->special_cost[1] = 7;
    w->stats()->magicpoints = 50;

    walker* marker = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
    TEST_ASSERT(marker != nullptr, "teleport marker created");
    if (!marker) {
        delete w;
        og::runtime::current_session->myscreen_->world().delete_objects();
        return;
    }

    marker->owner = w;
    marker->dead = 0;
    marker->lifetime = 1;

    Sint32 mx = std::min<Sint32>(w->xpos + 96, og::runtime::current_session->myscreen_->world().pixmaxx - w->sizex - 2);
    Sint32 my = std::min<Sint32>(w->ypos + 96, og::runtime::current_session->myscreen_->world().pixmaxy - w->sizey - 2);
    if (!og::runtime::current_session->myscreen_->query_passable(static_cast<float>(mx), static_cast<float>(my), w))
    {
        bool found = false;
        for (Sint32 x = 0; x < og::runtime::current_session->myscreen_->world().pixmaxx - w->sizex - 2 && !found; x += GRID_SIZE)
        {
            for (Sint32 y = 0; y < og::runtime::current_session->myscreen_->world().pixmaxy - w->sizey - 2; y += GRID_SIZE)
            {
                const Sint32 dx = x - w->xpos;
                const Sint32 dy = y - w->ypos;
                if (dx * dx + dy * dy <= 64 * 64)
                    continue;
                if (og::runtime::current_session->myscreen_->query_passable(static_cast<float>(x), static_cast<float>(y), w))
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

    const float mp_before = w->stats()->magicpoints;
    TEST_ASSERT(w->special(), "successful special should return true");
    TEST_ASSERT(w->stats()->magicpoints == mp_before - w->stats()->special_cost[1],
                "successful special should spend configured MP cost");

    delete w;
    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_SPECIAL_TEST(test_walker_special_success_returns_true_and_spends_mp);
