#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "test_game_world_fixture.h"

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    // Place at a valid position so obmap operations are safe.
    w->setxy(50, 50);
    return w;
}

TEST(LoaderAndWalker, loader_sets_soldier_defaults)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker(soldier) should succeed";

    ASSERT_EQ((int)FAMILY_KNIFE, (int)w->default_weapon()) << "soldier default weapon should be knife";
    ASSERT_EQ(2, (int)w->stats()->weapon_cost()) << "soldier weapon_cost should be set";
    ASSERT_EQ(25, (int)w->stats()->special_cost(1)) << "soldier charge cost";
    ASSERT_EQ(100, (int)w->stats()->special_cost(2)) << "soldier boomerang cost";
    ASSERT_EQ(120, (int)w->stats()->special_cost(3)) << "soldier whirlwind cost";
    ASSERT_EQ(150, (int)w->stats()->special_cost(4)) << "soldier disarm cost";

}


TEST(LoaderAndWalker, loader_sets_faerie_flags)
{
    auto w = create_living(FAMILY_FAERIE);
    ASSERT_TRUE(w != nullptr) << "create_walker(faerie) should succeed";

    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_ANIMATE)) << "faerie should have BIT_ANIMATE";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_FLYING)) << "faerie should have BIT_FLYING";
    ASSERT_EQ((int)FAMILY_SPRINKLE, (int)w->default_weapon()) << "faerie default weapon should be sprinkle";
    ASSERT_EQ(2, (int)w->stats()->weapon_cost()) << "faerie weapon_cost should be set";

}


TEST(LoaderAndWalker, loader_sets_ghost_flags)
{
    auto w = create_living(FAMILY_GHOST);
    ASSERT_TRUE(w != nullptr) << "create_walker(ghost) should succeed";

    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_ANIMATE)) << "ghost should have BIT_ANIMATE";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_FLYING)) << "ghost should have BIT_FLYING";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_ETHEREAL)) << "ghost should have BIT_ETHEREAL";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_NO_RANGED)) << "ghost should have BIT_NO_RANGED";
    ASSERT_EQ(0, (int)w->stats()->weapon_cost()) << "ghost melee should be free";

}


TEST(LoaderAndWalker, walker_attack_deals_damage_and_awards_score)
{
    auto attacker = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(attacker != nullptr) << "create_walker(attacker) should succeed";
    ASSERT_TRUE(target != nullptr) << "create_walker(target) should succeed";

    attacker->set_team_num(0);
    target->set_team_num(1);

    attacker->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    attacker->myguy->teamnum = 0;
    attacker->myguy->exp = 0;
    attacker->myguy->total_hits = 0;
    attacker->myguy->total_shots = 0;

    target->stats()->set_armor(0);
    target->stats()->set_hitpoints(100);
    target->stats()->set_max_hitpoints(100);

    og::runtime::current_session->myscreen_->world_.m_score[0] = 0;

    bool ok = attacker->attack(target.get());
    ASSERT_TRUE(ok) << "attack should succeed against enemy living target";
    ASSERT_TRUE(target->stats()->hitpoints() < 100) << "attack should reduce target HP";
    ASSERT_TRUE(attacker->myguy->total_hits >= 1) << "attack should increment attacker hits";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world_.m_score[0] > 0) << "attack should award score for team 0";

}


// Issue #150: with real loader art, a mirror entity that changes family across
// a snapshot must end up blitting the NEW family's bitmap. Slimes grow by
// walker::transform_to, which keeps the entity_id, so the mirror only ever sees
// a family change on an entity it already owns. When apply_snapshot dropped the
// PixieData* the configurator handed back, the mirror kept the 12x12 s_slime
// buffer while sizex/sizey became m_slime's 20x20 — striped noise on screen and
// a read past the end of the small-slime buffer.
TEST(LoaderAndWalker, snapshot_family_change_repoints_the_render_bitmap)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";
    const PixieData* small_art = l->graphics_for(Order::Living, FAMILY_SMALL_SLIME);
    const PixieData* medium_art = l->graphics_for(Order::Living, FAMILY_MEDIUM_SLIME);
    ASSERT_TRUE(small_art != nullptr && small_art->valid());
    ASSERT_TRUE(medium_art != nullptr && medium_art->valid());
    ASSERT_TRUE(small_art->w != medium_art->w)
        << "s_slime and m_slime must differ in size for this pin to mean anything";

    TestGameWorld source_fx(2202);
    TestGameWorld mirror_fx(2203);
    GameWorld& source = source_fx.world();
    GameWorld& mirror = mirror_fx.world();

    // Leave the source slime at its default spot: walker::setworldxy indexes
    // into current_game->world's obmap, which is the mirror here (last fixture
    // constructed wins the ambient context), and that cross-world registration
    // would outlive the source walker.
    walker* slime = source.add_ob(Order::Living, FAMILY_SMALL_SLIME);
    ASSERT_TRUE(slime != nullptr);
    const std::uint32_t slime_id = slime->entity_id();

    ASSERT_TRUE(og::sim::apply_snapshot(mirror, og::sim::capture_snapshot(source)));
    walker* mirror_slime = mirror.find_by_id(slime_id);
    ASSERT_TRUE(mirror_slime != nullptr);
    ASSERT_EQ((int)small_art->w, (int)mirror_slime->sizex())
        << "fresh mirror entity should draw the small-slime sheet";

    // Grow, the way slime_grow_into does on the authoritative side.
    const PixieData* grown =
        source.configure_existing_entity(*slime, Order::Living, FAMILY_MEDIUM_SLIME);
    ASSERT_TRUE(grown != nullptr);
    slime->set_data(*grown);
    source.set_entity_derived_stats(slime, Order::Living, FAMILY_MEDIUM_SLIME);

    ASSERT_TRUE(og::sim::apply_snapshot(mirror, og::sim::capture_snapshot(source)));
    mirror_slime = mirror.find_by_id(slime_id);
    ASSERT_TRUE(mirror_slime != nullptr);
    ASSERT_EQ(FAMILY_MEDIUM_SLIME, (int)mirror_slime->family());
    ASSERT_EQ((int)medium_art->w, (int)mirror_slime->sizex());
    ASSERT_EQ((int)medium_art->h, (int)mirror_slime->sizey());

    // Pointer identity against the same world's loader: each LevelRuntimeData
    // owns its own loader instance, so the reference sheets have to come from
    // this mirror, not from the session's loader.
    walker* reference_small = mirror.add_ob(Order::Living, FAMILY_SMALL_SLIME);
    walker* reference_medium = mirror.add_ob(Order::Living, FAMILY_MEDIUM_SLIME);
    ASSERT_TRUE(reference_small != nullptr && reference_medium != nullptr);
    reference_small->set_frame(0);
    reference_medium->set_frame(0);
    mirror_slime->set_frame(0);
    ASSERT_TRUE(reference_small->bmp_data() != reference_medium->bmp_data());
    ASSERT_EQ(reference_medium->bmp_data(), mirror_slime->bmp_data())
        << "grown mirror slime must blit the medium-slime sheet, not the stale "
           "small-slime buffer at the new family's 20x20 stride";
}

