#include <cstdint>
#include <openglad/gameplay/effect.h>
#include <openglad/resources/gloader.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)

// hits() is a free function
short hits(short x, short y, short xsize, short ysize,
           short x2, short y2, short xsize2, short ysize2);

// ---------------------------------------------------------------------------
// hits() AABB collision tests
// ---------------------------------------------------------------------------

TEST(EffectExtended, hits_overlap)
{
    short result = hits(10, 10, 20, 20, 15, 15, 20, 20);
    ASSERT_EQ(1, (int)result) << "overlapping boxes should hit";
}


TEST(EffectExtended, hits_no_overlap_right)
{
    short result = hits(10, 10, 20, 20, 50, 10, 20, 20);
    ASSERT_EQ(0, (int)result) << "boxes far apart should not hit";
}


TEST(EffectExtended, hits_no_overlap_below)
{
    short result = hits(10, 10, 20, 20, 10, 50, 20, 20);
    ASSERT_EQ(0, (int)result) << "box below should not hit";
}


TEST(EffectExtended, hits_no_overlap_left)
{
    short result = hits(50, 10, 20, 20, 10, 10, 20, 20);
    ASSERT_EQ(0, (int)result) << "box to left should not hit";
}


TEST(EffectExtended, hits_no_overlap_above)
{
    short result = hits(10, 50, 20, 20, 10, 10, 20, 20);
    ASSERT_EQ(0, (int)result) << "box above should not hit";
}


TEST(EffectExtended, hits_exact_touching)
{
    // Edge-touching: x1 right edge = x2 left edge
    short result = hits(10, 10, 20, 20, 30, 10, 20, 20);
    ASSERT_EQ(1, (int)result) << "touching edges should hit";
}


TEST(EffectExtended, hits_contained)
{
    short result = hits(10, 10, 40, 40, 15, 15, 10, 10);
    ASSERT_EQ(1, (int)result) << "contained box should hit";
}


TEST(EffectExtended, hits_same_box)
{
    short result = hits(10, 10, 20, 20, 10, 10, 20, 20);
    ASSERT_EQ(1, (int)result) << "same box should hit";
}


TEST(EffectExtended, hits_zero_size)
{
    short result = hits(10, 10, 0, 0, 10, 10, 0, 0);
    ASSERT_EQ(1, (int)result) << "zero-size at same point should hit";
}


TEST(EffectExtended, effect_ctor_defaults_and_owner_pointer_cleanup_in_act)
{
    effect headless;
    ASSERT_EQ(1, (int)headless.ignore()) << "headless effect ctor should set ignore";

    effect fx;
    fx.set_ani_type(ANI_WALK);
    fx.set_order_family(Order::FX, 120);

    walker foe;
    walker leader;
    walker owner;
    fx.set_foe(&foe);
    fx.set_leader(&leader);
    fx.set_owner(&owner);
    foe.set_dead(1);
    leader.set_dead(1);
    owner.set_dead(1);

    (void)fx.act();
    ASSERT_TRUE(fx.foe() == nullptr) << "effect act should clear dead foe pointer";
    ASSERT_TRUE(fx.leader() == nullptr) << "effect act should clear dead leader pointer";
    ASSERT_TRUE(fx.owner() == nullptr) << "effect act should clear dead owner pointer";
}
