#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include "test_framework.h"
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_guy(char family, unsigned char team = 0, short level = 3)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// walker::death - various family-specific death behaviors
// ---------------------------------------------------------------------------

TEST(WalkerDeath, soldier)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, mage)
{
    auto w = make_guy(FAMILY_MAGE, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, skeleton)
{
    auto w = make_guy(FAMILY_SKELETON, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, fire_elemental2)
{
    auto w = make_guy(FAMILY_FIREELEMENTAL, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
    // Fire elemental death should create an explosion
}


TEST(WalkerDeath, small_slime)
{
    auto w = make_guy(FAMILY_SMALL_SLIME, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, medium_slime)
{
    auto w = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Living, FAMILY_MEDIUM_SLIME);
    if (!w) return;
    w->setxy(100, 100);
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, large_slime)
{
    auto w = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Living, FAMILY_SLIME);
    if (!w) return;
    w->setxy(100, 100);
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, ghost)
{
    auto w = make_guy(FAMILY_GHOST, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, faerie)
{
    auto w = make_guy(FAMILY_FAERIE, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, myguy_present)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    ASSERT_TRUE(w->myguy != nullptr) << "should have myguy from guy::create_walker_owned";
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, orc)
{
    auto w = make_guy(FAMILY_ORC, 1);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, barbarian)
{
    auto w = make_guy(FAMILY_BARBARIAN, 1);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, archer)
{
    auto w = make_guy(FAMILY_ARCHER, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, cleric)
{
    auto w = make_guy(FAMILY_CLERIC, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, druid)
{
    auto w = make_guy(FAMILY_DRUID, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, thief)
{
    auto w = make_guy(FAMILY_THIEF, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


TEST(WalkerDeath, elf)
{
    auto w = make_guy(FAMILY_ELF, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
}


// ---------------------------------------------------------------------------
// walker::death double-call protection
// ---------------------------------------------------------------------------

TEST(WalkerDeath, double_call)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->dead = 1;
    w->death();
    bool result = w->death();
    ASSERT_TRUE(!result) << "second death call returns false";
}


// ---------------------------------------------------------------------------
// walker::compute_outline
// ---------------------------------------------------------------------------

TEST(WalkerDeath, walker_compute_outline_invulnerable)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->invulnerable_left = 10;
    w->invisibility_left = 0;
    w->flight_left = 0;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    w->compute_outline(vs ? vs->control : nullptr);

}


TEST(WalkerDeath, walker_compute_outline_flying)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->flight_left = 10;
    w->invisibility_left = 0;
    w->invulnerable_left = 0;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    w->compute_outline(vs ? vs->control : nullptr);

}


TEST(WalkerDeath, walker_compute_outline_invisible)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->invisibility_left = 10;
    w->flight_left = 0;
    w->invulnerable_left = 0;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    w->compute_outline(vs ? vs->control : nullptr);

}


TEST(WalkerDeath, walker_compute_outline_no_status)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->invisibility_left = 0;
    w->flight_left = 0;
    w->invulnerable_left = 0;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    w->compute_outline(vs ? vs->control : nullptr);

}

