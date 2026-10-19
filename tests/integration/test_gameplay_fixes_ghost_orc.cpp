// The ghost's SCARE (packs/core/families/living-12-ghost.lua) and the orc's
// HOWL (packs/core/lib/orc_specials.lua) with the Gameplay Fixes setting.
// The classic casts succeed with nobody to frighten or stun: they spend
// their mana and start their rest for nothing. With the setting on they are
// refused before they spend anything. SCARE asks for a LIVING foe inside
// the scare radius, because the fright only ever moves living foes; HOWL
// asks for any foe in its radius (it stuns a generator too).
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"
#include <initializer_list>
#include <string>

namespace {

GameWorld& world()
{
    return og::runtime::current_session->myscreen_->world();
}

void ensure_level_loaded()
{
    if (world().grid.valid() && world().pixmaxx > 0 && world().pixmaxy > 0)
        return;
    world().id = 1;
    (void)og::runtime::current_session->myscreen_->load_level();
}

walker* make_caster(char family, short level)
{
    ensure_level_loaded();
    guy g(family);
    g.teamnum = 1;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
    {
        w->setxy(100, 100);
        w->set_busy(0);
        w->stats()->set_magicpoints(500);
        w->stats()->set_max_magicpoints(500);
        w->set_current_special(1);
    }
    return w.release();
}

int count_scare_carriers()
{
    int n = 0;
    for (auto* list : {&world().oblist, &world().fxlist})
        for (auto& uptr : *list)
            if (uptr && !uptr->dead() && uptr->query_order() == Order::FX &&
                uptr->family() == FAMILY_GHOST_SCARE)
                ++n;
    return n;
}

int count_sounds()
{
    int n = 0;
    for (const og::sim::Event& e : current_game->sim_events->events())
        if (e.kind == og::sim::EventKind::PlaySound)
            ++n;
    return n;
}

// What the neighbourhood around the caster holds.
enum class Neighbour
{
    Nobody,
    Generator,       // an enemy tent 30 px east
    FarLiving,       // an enemy soldier 300 px east
    NearLiving,      // an enemy soldier 30 px east
    NearAlly,        // an allied soldier 30 px east
};

void add_neighbour(walker* caster, Neighbour n)
{
    if (n == Neighbour::Nobody)
        return;
    walker* ob = nullptr;
    if (n == Neighbour::Generator)
        ob = world().add_ob(Order::Generator, FAMILY_TENT);
    else
        ob = world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, ob);
    ob->set_team_num(n == Neighbour::NearAlly ? caster->team_num() : 2);
    const Sint32 dx = (n == Neighbour::FarLiving) ? 300 : 30;
    ob->setxy(static_cast<Sint32>(caster->xpos() + dx), static_cast<Sint32>(caster->ypos()));
}

struct CastOutcome
{
    bool cast = false;
    std::string reason;
    float mp_spent = 0.0f;
    int rest_left = 0;
    int sounds = 0;
    float busy = 0.0f;
    int carriers = 0;
};

CastOutcome cast_slot1(char family, short level, short fixes, Neighbour n)
{
    world().delete_objects();
    CastOutcome out;
    walker* w = make_caster(family, level);
    EXPECT_NE(nullptr, w);
    if (w == nullptr)
        return out;
    add_neighbour(w, n);
    world().gameplay_fixes = fixes;
    current_game->sim_events->clear();
    const float mp_before = w->stats()->magicpoints();
    walker::SpecialFailure why = walker::SpecialFailure::None;
    {
        // HOWL's stun rolls draw from the sim stream; pin them.
        FixedRandom rng(0u);
        ScopedSimRandom scoped(&rng);
        out.cast = w->special(&why, &out.reason);
    }
    out.mp_spent = mp_before - w->stats()->magicpoints();
    out.rest_left = static_cast<int>(w->special_rest_left());
    out.sounds = count_sounds();
    out.busy = w->busy();
    out.carriers = count_scare_carriers();
    delete w;
    return out;
}

class GameplayFixesGhostOrc : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensure_level_loaded();
        fixes_before_ = world().gameplay_fixes;
        specials_before_ = world().new_specials;
        // SCARE, not WAIL, and the classic slot table.
        world().new_specials = 0;
        world().delete_objects();
    }
    void TearDown() override
    {
        world().gameplay_fixes = fixes_before_;
        world().new_specials = specials_before_;
        world().delete_objects();
    }

private:
    short fixes_before_ = 0;
    short specials_before_ = 0;
};

void expect_refused(const CastOutcome& o, const char* what)
{
    EXPECT_FALSE(o.cast) << what;
    EXPECT_EQ(std::string("NO FOE IN RANGE"), o.reason) << what;
    EXPECT_FLOAT_EQ(0.0f, o.mp_spent) << "no MP, " << what;
    EXPECT_EQ(0, o.rest_left) << "no rest, " << what;
    EXPECT_EQ(0, o.sounds) << "no sound, " << what;
    EXPECT_FLOAT_EQ(0.0f, o.busy) << "no busy, " << what;
    EXPECT_EQ(0, o.carriers) << "no scare carrier, " << what;
}

} // namespace

// SCARE at a level-3 ghost (scare radius 50 + 10 * 3 = 80 px).
TEST_F(GameplayFixesGhostOrc, scare_refuses_with_no_living_foe_when_on)
{
    // The classic cast with nobody about: one carrier, 30 MP, the rest.
    const CastOutcome off = cast_slot1(static_cast<char>(FAMILY_GHOST), 3, 0, Neighbour::Nobody);
    EXPECT_TRUE(off.cast);
    EXPECT_EQ(1, off.carriers);
    EXPECT_FLOAT_EQ(30.0f, off.mp_spent);
    EXPECT_GT(off.rest_left, 0);

    expect_refused(cast_slot1(static_cast<char>(FAMILY_GHOST), 3, 1, Neighbour::Nobody), "nobody");
    expect_refused(cast_slot1(static_cast<char>(FAMILY_GHOST), 3, 1, Neighbour::Generator),
                   "only a generator in reach (the fright never moves one)");
    expect_refused(cast_slot1(static_cast<char>(FAMILY_GHOST), 3, 1, Neighbour::FarLiving),
                   "a living foe outside the scare radius");
    expect_refused(cast_slot1(static_cast<char>(FAMILY_GHOST), 3, 1, Neighbour::NearAlly),
                   "only an ally in reach");

    // A living foe inside the radius: the setting lets the scare happen.
    const CastOutcome on = cast_slot1(static_cast<char>(FAMILY_GHOST), 3, 1, Neighbour::NearLiving);
    EXPECT_TRUE(on.cast);
    EXPECT_EQ(1, on.carriers);
    EXPECT_FLOAT_EQ(30.0f, on.mp_spent);
    EXPECT_GT(on.rest_left, 0);
}

// The setting-on twin of test_walker_specials.cpp's
// ghost_scare_animates_and_dissipates: with a living foe in reach the
// carrier animates and dies as it always did.
TEST_F(GameplayFixesGhostOrc, scare_with_a_living_foe_animates_and_dissipates_when_on)
{
    walker* w = make_caster(static_cast<char>(FAMILY_GHOST), 3);
    ASSERT_NE(nullptr, w);
    add_neighbour(w, Neighbour::NearLiving);
    world().gameplay_fixes = 1;
    ASSERT_TRUE(w->special());
    walker* fx = nullptr;
    for (auto* list : {&world().oblist, &world().fxlist})
        for (auto& uptr : *list)
            if (uptr && uptr->query_order() == Order::FX && uptr->family() == FAMILY_GHOST_SCARE)
                fx = uptr.get();
    ASSERT_NE(nullptr, fx);
    EXPECT_EQ(ANI_SCARE, static_cast<int>(fx->ani_type()));
    for (int i = 0; i < 64 && fx->dead() == 0; ++i)
        fx->act();
    EXPECT_EQ(1, static_cast<int>(fx->dead())) << "the carrier dies within 64 ticks";
    delete w;
}

// HOWL at a level-4 orc.
TEST_F(GameplayFixesGhostOrc, howl_refuses_with_no_foe_when_on)
{
    // The classic howl at nobody: 2 busy, the roar, the slot's MP, the rest.
    const CastOutcome off = cast_slot1(static_cast<char>(FAMILY_ORC), 4, 0, Neighbour::Nobody);
    EXPECT_TRUE(off.cast);
    EXPECT_FLOAT_EQ(2.0f, off.busy);
    EXPECT_EQ(1, off.sounds);
    EXPECT_GT(off.mp_spent, 0.0f);
    EXPECT_GT(off.rest_left, 0);

    expect_refused(cast_slot1(static_cast<char>(FAMILY_ORC), 4, 1, Neighbour::Nobody), "nobody");
    expect_refused(cast_slot1(static_cast<char>(FAMILY_ORC), 4, 1, Neighbour::NearAlly),
                   "only an ally in reach");

    // HOWL stuns a generator, so a generator in reach is enough; and a
    // living foe of course.
    for (const Neighbour n : {Neighbour::Generator, Neighbour::NearLiving})
    {
        const CastOutcome on = cast_slot1(static_cast<char>(FAMILY_ORC), 4, 1, n);
        EXPECT_TRUE(on.cast) << static_cast<int>(n);
        EXPECT_FLOAT_EQ(2.0f, on.busy) << static_cast<int>(n);
        EXPECT_EQ(1, on.sounds) << static_cast<int>(n);
        EXPECT_GT(on.rest_left, 0) << static_cast<int>(n);
    }
}
