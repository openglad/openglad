/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials, thief: Shift + DROP BOMB = MINE (packs/core/lib/kit_thief.lua,
// packs/core/lib/effect_mine.lua, packs/core/families/effect-13-mine.lua).
//
// Every case runs the real thief through the real walker::special() (the
// cost gate included) in a world with the New Specials setting ON, and has
// an OFF twin on the same staging: with the setting off the shifted cast is
// the classic bomb at the bomb's price and no mine ever exists.
//
// Perturbation proofs (each edit made once in the staged copy under
// build/ci-test/packs/core/, the test binary re-run, the copy restored with
// `cmake --build --preset ci-test --target stage_runtime_assets`):
//   mine_cap = 3 -> 1 (effect-13-mine.lua):
//     KitThief.fourth_mine_is_refused fails ("the second mine is laid":
//     MINE LIMIT REACHED on the second cast).
//   mine_chain_px = 24 -> 0 (effect-13-mine.lua):
//     KitThief.mines_chain_on_their_own_next_tick fails ("the neighbour is
//     lit": its ani_type stays ANI_WALK).
//   `if self:alternate_down() then` -> `if false then` (kit_thief.lua):
//     every ON row that expects a mine fails, the OFF twins stay green.
// The exact output lines are in the change's report.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr unsigned char kThiefTeam = 0;
constexpr unsigned char kFoeTeam = 1;

int mine_family()
{
    return og::families::resolve_family_string_id(Order::FX, "core:mine");
}

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    w->stats()->set_max_hitpoints(200.0f);
    w->stats()->set_hitpoints(200.0f);
    return w;
}

std::vector<walker*> live_of(const TestGameWorld& tw, Order order,
                             int family)
{
    std::vector<walker*> found;
    for (const auto& uptr : tw.world().oblist)
        if (uptr && !uptr->dead() && uptr->query_order() == order &&
            uptr->family() == family)
            found.push_back(uptr.get());
    return found;
}

int count_of(const TestGameWorld& tw, Order order, int family)
{
    return static_cast<int>(live_of(tw, order, family).size());
}

// A thief on team 0, driven by a seat (so the cast queues no bot flee walk
// and draws nothing), slot 1, with `mp` magic and the setting `ns`.
struct ThiefScene {
    TestGameWorld tw;
    walker* thief = nullptr;

    explicit ThiefScene(short ns, short level = 1, float mp = 500.0f)
    {
        tw.world().new_specials = ns;
        thief = add_living(tw, FAMILY_THIEF, kThiefTeam, 160, 160);
        if (thief == nullptr)
            return;
        thief->set_user(0);
        thief->set_act_type(ACT_CONTROL);
        thief->stats()->set_level(level);
        thief->stats()->set_max_magicpoints(mp);
        thief->stats()->set_magicpoints(mp);
        thief->set_current_special(1);
    }

    // One DROP BOMB slot cast, shifted or not, through walker::special().
    bool cast(bool shifted, walker::SpecialFailure* why = nullptr,
              std::string* reason = nullptr)
    {
        thief->set_current_special(1);
        thief->set_shifter_down(shifted ? 1 : 0);
        const bool ok = thief->special(why, reason);
        thief->set_shifter_down(0);
        return ok;
    }

    walker* lay_mine()
    {
        const int before = count_of(tw, Order::FX, mine_family());
        if (!cast(true))
            return nullptr;
        const auto mines = live_of(tw, Order::FX, mine_family());
        if (static_cast<int>(mines.size()) != before + 1)
            return nullptr;
        return mines.back();
    }

    int mines() const { return count_of(tw, Order::FX, mine_family()); }
    int bombs() const { return count_of(tw, Order::FX, FAMILY_BOMB); }
    int explosions() const
    {
        return count_of(tw, Order::FX, FAMILY_EXPLOSION);
    }
    int clouds() const { return count_of(tw, Order::FX, FAMILY_CLOUD); }
};

// Act an effect until it dies (an explosion animates, then strikes).
void act_until_dead(walker* fx)
{
    for (int i = 0; i < 64 && !fx->dead(); ++i)
        fx->act();
}

// The OFF twin every row shares: on the same staging with the setting off,
// a shifted cast is the classic bomb at the bomb's 35 MP, and no mine exists.
void expect_off_twin_lays_a_bomb(short level, float mp)
{
    ThiefScene off(0, level, mp);
    ASSERT_NE(nullptr, off.thief);
    EXPECT_TRUE(off.cast(true)) << "the shifted cast is DROP BOMB";
    EXPECT_EQ(0, off.mines()) << "no mine with the setting off";
    EXPECT_EQ(1, off.bombs()) << "the classic bomb instead";
    EXPECT_FLOAT_EQ(mp - 35.0f, off.thief->stats()->magicpoints())
        << "at the bomb's price";
}

}  // namespace

// The mine's family is effect wire id 13, drawn from mine.png (two 9x9
// frames), with a visible red curses glyph and no radar blip.
TEST(KitThief, new_kit_entities_load_art_and_glyphs)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    ASSERT_EQ(13, mine_family()) << "core:mine is effect wire id 13";
    const auto* efd = get_effect_family_descriptor(13);
    ASSERT_NE(nullptr, efd);
    EXPECT_STREQ("core:mine", efd->declared_id);
    ASSERT_NE(nullptr, efd->pix_filename);
    EXPECT_STREQ("mine.png", efd->pix_filename);
    EXPECT_EQ(U'⊗', efd->glyph.codepoint);
    EXPECT_EQ('x', efd->glyph.ascii);
    EXPECT_EQ(og::GlyphColor::Red, efd->glyph.color);
    EXPECT_TRUE(efd->glyph.bold);
    EXPECT_FALSE(efd->glyph.transparent) << "the own team sees a glyph";
    EXPECT_EQ(og::kRadarColorNone, efd->radar.color);

    ThiefScene s(1);
    ASSERT_NE(nullptr, s.thief);
    walker* mine = s.lay_mine();
    ASSERT_NE(nullptr, mine);
    EXPECT_EQ(9, mine->sizex()) << "the sprite's frame size";
    EXPECT_EQ(9, mine->sizey());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The HUD names the alternate MINE only with the setting on, and the shifted
// price is double the bomb's.
TEST(KitThief, mine_is_named_and_priced_only_with_the_setting_on)
{
    og::test::mount_core_pack();
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_NE(nullptr, fd);
    EXPECT_STREQ("MINE", og::sim::alternate_name(fd, 1, 1));
    EXPECT_STREQ("NONE", og::sim::alternate_name(fd, 1, 0));
    EXPECT_EQ(70, og::sim::alternate_cost_in_play(fd, 1, 1));
    EXPECT_EQ(0, og::sim::alternate_cost_in_play(fd, 1, 0));
    EXPECT_STREQ("DROP BOMB", og::sim::special_name(fd, 1, 0))
        << "the slot itself is classic";
}

// ON: the shifted cast lays one mine under the thief for 70 MP: owned, on the
// thief's team and floor, carrying the bomb's damage and the thief's level,
// shimmering (invisibility 20, which an effect never spends), outside the
// collision table's reach, with a lifetime of 600 + 30 per level. It then
// waits: with no foe near it neither blows nor fades, only ages, and its
// light blinks every 8 ticks.
TEST(KitThief, mine_is_laid_invisible_and_waits)
{
    og::test::ScopedHookFailureGuard guard;
    ThiefScene s(1, 4, 500.0f);
    ASSERT_NE(nullptr, s.thief);
    s.thief->set_floor(1);
    walker* mine = s.lay_mine();
    ASSERT_NE(nullptr, mine);
    EXPECT_FLOAT_EQ(430.0f, s.thief->stats()->magicpoints())
        << "MINE costs 70, double the bomb";
    EXPECT_EQ(0, s.bombs()) << "a mine, not a bomb";
    EXPECT_EQ(s.thief, mine->owner());
    EXPECT_EQ(kThiefTeam, mine->team_num());
    EXPECT_EQ(1, mine->floor());
    EXPECT_EQ(4, mine->stats()->level());
    EXPECT_FLOAT_EQ(static_cast<float>(og::combat::bomb_damage(4)),
                    mine->damage());
    EXPECT_EQ(20, mine->invisibility_left());
    EXPECT_TRUE(mine->ignore());
    EXPECT_EQ(ANI_WALK, mine->ani_type());
    EXPECT_EQ(600 + 30 * 4, mine->lifetime());
    EXPECT_EQ(s.thief->xpos() + s.thief->sizex() / 2,
              mine->xpos() + mine->sizex() / 2)
        << "centred under the thief";
    EXPECT_EQ(s.thief->ypos() + s.thief->sizey() / 2,
              mine->ypos() + mine->sizey() / 2);

    s.tw.world().tick_count_ = 8;
    EXPECT_TRUE(mine->act());
    EXPECT_EQ(1, mine->frame()) << "ticks 8..15 show the dim frame";
    s.tw.world().tick_count_ = 16;
    for (int i = 0; i < 49; ++i)
        EXPECT_TRUE(mine->act());
    EXPECT_FALSE(mine->dead()) << "no fuse: it waits";
    EXPECT_EQ(0, mine->frame()) << "ticks 16..23 show the bright frame";
    EXPECT_EQ(600 + 30 * 4 - 50, mine->lifetime());
    EXPECT_EQ(20, mine->invisibility_left()) << "the shimmer never fades";
    EXPECT_EQ(0, s.explosions());
    EXPECT_EQ(0u, guard.count()) << guard.message();

    expect_off_twin_lays_a_bomb(4, 500.0f);
}

// A foe stepping on the mine sets it off: the bomb's own blast, owned by the
// thief, on the mine's floor and centre, at the mine's damage. An ally
// standing on it sets nothing off, and a foe beside it (not overlapping)
// neither.
TEST(KitThief, mine_detonates_under_a_foe_not_an_ally)
{
    og::test::ScopedHookFailureGuard guard;
    ThiefScene s(1, 3, 500.0f);
    ASSERT_NE(nullptr, s.thief);
    walker* mine = s.lay_mine();
    ASSERT_NE(nullptr, mine);
    s.thief->setxy(40, 40);

    walker* ally = add_living(s.tw, FAMILY_SOLDIER, kThiefTeam,
                              mine->xpos(), mine->ypos());
    ASSERT_NE(nullptr, ally);
    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kFoeTeam,
                             static_cast<short>(mine->xpos() + 30),
                             mine->ypos());
    ASSERT_NE(nullptr, foe);
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(mine->act());
    EXPECT_FALSE(mine->dead()) << "an ally on it and a foe beside it";

    foe->setxy(static_cast<short>(mine->xpos() - 4),
               static_cast<short>(mine->ypos() - 4));
    EXPECT_TRUE(mine->act());
    EXPECT_TRUE(mine->dead()) << "the foe stepped on it";
    const auto blasts = live_of(s.tw, Order::FX, FAMILY_EXPLOSION);
    ASSERT_EQ(1u, blasts.size());
    walker* blast = blasts.front();
    EXPECT_EQ(s.thief, blast->owner());
    EXPECT_FLOAT_EQ(mine->damage(), blast->damage());
    EXPECT_EQ(mine->floor(), blast->floor());
    EXPECT_EQ(mine->xpos() + mine->sizex() / 2,
              blast->xpos() + blast->sizex() / 2);

    const float foe_hp = foe->stats()->hitpoints();
    act_until_dead(blast);
    EXPECT_LT(foe->stats()->hitpoints(), foe_hp) << "full bomb damage lands";
    EXPECT_EQ(0u, guard.count()) << guard.message();

    expect_off_twin_lays_a_bomb(3, 500.0f);
}

// Faeries and ghosts float over mines: a foe with the FLYING bit, or with
// flight left from a potion, crosses unharmed; the same foe on its feet
// sets the mine off.
TEST(KitThief, flying_foe_does_not_trip_a_mine)
{
    og::test::ScopedHookFailureGuard guard;
    ThiefScene s(1);
    ASSERT_NE(nullptr, s.thief);
    walker* mine = s.lay_mine();
    ASSERT_NE(nullptr, mine);
    s.thief->setxy(40, 40);
    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kFoeTeam, mine->xpos(),
                             mine->ypos());
    ASSERT_NE(nullptr, foe);

    foe->stats()->set_bit_flags(BIT_FLYING, 1);
    EXPECT_TRUE(mine->act());
    EXPECT_FALSE(mine->dead()) << "a flier floats over";
    foe->stats()->set_bit_flags(BIT_FLYING, 0);
    foe->set_flight_left(30);
    EXPECT_TRUE(mine->act());
    EXPECT_FALSE(mine->dead()) << "flight from a potion floats over too";
    foe->set_flight_left(0);
    foe->set_floor(1);
    EXPECT_TRUE(mine->act());
    EXPECT_FALSE(mine->dead()) << "a foe on another floor is not on it";
    foe->set_floor(0);
    EXPECT_TRUE(mine->act());
    EXPECT_TRUE(mine->dead()) << "on its feet, it sets the mine off";
    EXPECT_EQ(0u, guard.count()) << guard.message();

    expect_off_twin_lays_a_bomb(1, 500.0f);
}

// A blast lights every other live mine of its team within 24 px; each lit
// mine blows on its OWN next act (no recursion inside the first death). A
// mine further away, and an enemy mine right beside it, are not lit.
TEST(KitThief, mines_chain_on_their_own_next_tick)
{
    og::test::ScopedHookFailureGuard guard;
    ThiefScene s(1, 2, 500.0f);
    ASSERT_NE(nullptr, s.thief);
    walker* first = s.lay_mine();
    ASSERT_NE(nullptr, first);
    s.thief->setxy(176, 160);
    walker* near = s.lay_mine();
    ASSERT_NE(nullptr, near);
    ASSERT_LE(near->distance_to_ob(first), 24);
    s.thief->setxy(240, 160);
    walker* far = s.lay_mine();
    ASSERT_NE(nullptr, far);
    ASSERT_GT(far->distance_to_ob(first), 24);
    s.thief->setxy(40, 40);

    walker* enemy_thief = add_living(s.tw, FAMILY_THIEF, kFoeTeam, 150, 160);
    ASSERT_NE(nullptr, enemy_thief);
    enemy_thief->set_user(1);
    enemy_thief->set_act_type(ACT_CONTROL);
    enemy_thief->stats()->set_magicpoints(500.0f);
    enemy_thief->set_current_special(1);
    enemy_thief->set_shifter_down(1);
    ASSERT_TRUE(enemy_thief->special());
    enemy_thief->set_shifter_down(0);
    walker* enemy_mine = nullptr;
    for (walker* m : live_of(s.tw, Order::FX, mine_family()))
        if (m->owner() == enemy_thief)
            enemy_mine = m;
    ASSERT_NE(nullptr, enemy_mine);
    ASSERT_LE(enemy_mine->distance_to_ob(first), 24);
    enemy_thief->setxy(400, 400);

    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kFoeTeam, first->xpos(),
                             first->ypos());
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(first->act());
    ASSERT_TRUE(first->dead());
    EXPECT_FALSE(near->dead()) << "lit, not blown inside the first death";
    EXPECT_NE(ANI_WALK, near->ani_type()) << "the neighbour is lit";
    EXPECT_EQ(ANI_WALK, far->ani_type()) << "out of reach";
    EXPECT_EQ(ANI_WALK, enemy_mine->ani_type()) << "another team's mine";

    foe->setxy(400, 10);
    EXPECT_TRUE(near->act());
    EXPECT_TRUE(near->dead()) << "a lit mine blows on its own next act";
    EXPECT_EQ(2, s.explosions());
    EXPECT_TRUE(far->act());
    EXPECT_FALSE(far->dead());
    EXPECT_EQ(0u, guard.count()) << guard.message();

    expect_off_twin_lays_a_bomb(2, 500.0f);
}

// Three live mines per thief. The fourth is refused (MINE LIMIT REACHED) and
// costs nothing; another thief's mines do not count; once one of the three
// is gone the thief may lay again.
TEST(KitThief, fourth_mine_is_refused)
{
    og::test::ScopedHookFailureGuard guard;
    ThiefScene s(1, 1, 500.0f);
    ASSERT_NE(nullptr, s.thief);
    walker* other = add_living(s.tw, FAMILY_THIEF, kThiefTeam, 300, 300);
    ASSERT_NE(nullptr, other);
    other->set_user(1);
    other->set_act_type(ACT_CONTROL);
    other->stats()->set_magicpoints(500.0f);
    other->set_current_special(1);
    other->set_shifter_down(1);
    ASSERT_TRUE(other->special()) << "another thief's mine";
    other->set_shifter_down(0);

    walker* first = s.lay_mine();
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, s.lay_mine()) << "the second mine is laid";
    ASSERT_NE(nullptr, s.lay_mine()) << "the third mine is laid";
    EXPECT_EQ(4, s.mines());
    const float mp = s.thief->stats()->magicpoints();
    EXPECT_FLOAT_EQ(500.0f - 3 * 70.0f, mp);

    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    EXPECT_FALSE(s.cast(true, &why, &reason));
    EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, why);
    EXPECT_EQ("MINE LIMIT REACHED", reason);
    EXPECT_FLOAT_EQ(mp, s.thief->stats()->magicpoints()) << "refused free";
    EXPECT_EQ(4, s.mines());
    EXPECT_TRUE(s.cast(false)) << "the bomb is still there unshifted";
    EXPECT_EQ(1, s.bombs());

    first->set_lifetime(1);
    EXPECT_TRUE(first->act());
    ASSERT_TRUE(first->dead());
    EXPECT_NE(nullptr, s.lay_mine()) << "a spent mine frees its place";
    EXPECT_EQ(0u, guard.count()) << guard.message();

    expect_off_twin_lays_a_bomb(1, 500.0f);
}

// From thief level 7 a blowing mine also leaves a poison puff: a cloud of
// 20 ticks at the mine, on its team and floor, doing half the level per
// touch, starting faint. Below level 7 there is no puff.
TEST(KitThief, high_level_mine_leaves_a_puff)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short level : {short{6}, short{7}}) {
        ThiefScene s(1, level, 500.0f);
        ASSERT_NE(nullptr, s.thief);
        s.thief->set_floor(1);
        walker* mine = s.lay_mine();
        ASSERT_NE(nullptr, mine);
        s.thief->setxy(40, 40);
        walker* foe = add_living(s.tw, FAMILY_SOLDIER, kFoeTeam,
                                 mine->xpos(), mine->ypos());
        ASSERT_NE(nullptr, foe);
        foe->set_floor(1);
        EXPECT_TRUE(mine->act());
        ASSERT_TRUE(mine->dead()) << "level " << level;
        const auto clouds = live_of(s.tw, Order::FX, FAMILY_CLOUD);
        if (level < 7) {
            EXPECT_TRUE(clouds.empty()) << "no puff below level 7";
            continue;
        }
        ASSERT_EQ(1u, clouds.size()) << "a puff at level 7";
        walker* puff = clouds.front();
        EXPECT_EQ(20, puff->lifetime());
        EXPECT_FLOAT_EQ(3.0f, puff->damage()) << "7 / 2";
        EXPECT_EQ(kThiefTeam, puff->team_num());
        EXPECT_EQ(1, puff->floor());
        EXPECT_EQ(s.thief, puff->owner());
        EXPECT_EQ(10, puff->invisibility_left());
        EXPECT_EQ(ANI_SPIN, puff->ani_type());
        EXPECT_TRUE(puff->ignore());
        EXPECT_EQ(mine->xpos() + mine->sizex() / 2,
                  puff->xpos() + puff->sizex() / 2);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();

    expect_off_twin_lays_a_bomb(7, 500.0f);
}

// A mine whose lifetime runs out goes quietly: no blast, no puff (even at a
// high level), nobody hurt. A thief's death does not disarm its mines: they
// keep their team and still blow under a foe.
TEST(KitThief, mine_expires_quietly)
{
    og::test::ScopedHookFailureGuard guard;
    ThiefScene s(1, 9, 500.0f);
    ASSERT_NE(nullptr, s.thief);
    walker* mine = s.lay_mine();
    ASSERT_NE(nullptr, mine);
    s.thief->setxy(220, 160);
    walker* orphan = s.lay_mine();
    ASSERT_NE(nullptr, orphan);
    s.thief->setxy(40, 40);
    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kFoeTeam, 100, 100);
    ASSERT_NE(nullptr, foe);
    const float foe_hp = foe->stats()->hitpoints();

    mine->set_lifetime(2);
    EXPECT_TRUE(mine->act());
    EXPECT_FALSE(mine->dead());
    EXPECT_TRUE(mine->act());
    EXPECT_TRUE(mine->dead()) << "out of time";
    EXPECT_EQ(0, s.explosions()) << "a quiet expiry";
    EXPECT_EQ(0, s.clouds()) << "and no puff";
    EXPECT_FLOAT_EQ(foe_hp, foe->stats()->hitpoints());

    s.thief->set_dead(1);
    s.thief->death();
    foe->setxy(orphan->xpos(), orphan->ypos());
    EXPECT_TRUE(orphan->act());
    EXPECT_NE(s.thief, orphan->owner())
        << "the effect let its dead owner go (the blast adopts the mine)";
    EXPECT_TRUE(orphan->dead()) << "still armed after the thief fell";
    EXPECT_EQ(1, s.explosions());
    EXPECT_EQ(0u, guard.count()) << guard.message();

    expect_off_twin_lays_a_bomb(9, 500.0f);
}

// The price: ON, shifted costs 70 and lays a mine, unshifted costs 35 and
// drops the bomb; a seated thief with 50 MP cannot afford the mine (no
// fall-back for a player). OFF, the shifted cast is the bomb at 35 even with
// 50 MP, and no mine is ever laid.
TEST(KitThief, mine_costs_double_and_is_a_bomb_when_off)
{
    og::test::ScopedHookFailureGuard guard;
    {
        ThiefScene s(1, 1, 200.0f);
        ASSERT_NE(nullptr, s.thief);
        EXPECT_TRUE(s.cast(true));
        EXPECT_FLOAT_EQ(130.0f, s.thief->stats()->magicpoints());
        EXPECT_EQ(1, s.mines());
        EXPECT_EQ(0, s.bombs());
        EXPECT_TRUE(s.cast(false));
        EXPECT_FLOAT_EQ(95.0f, s.thief->stats()->magicpoints());
        EXPECT_EQ(1, s.mines());
        EXPECT_EQ(1, s.bombs());
    }
    {
        ThiefScene poor(1, 1, 50.0f);
        ASSERT_NE(nullptr, poor.thief);
        walker::SpecialFailure why = walker::SpecialFailure::None;
        EXPECT_FALSE(poor.cast(true, &why));
        EXPECT_EQ(walker::SpecialFailure::NoMP, why);
        EXPECT_EQ(0, poor.mines());
        EXPECT_EQ(0, poor.bombs());
        EXPECT_FLOAT_EQ(50.0f, poor.thief->stats()->magicpoints());
    }
    {
        ThiefScene off(0, 1, 50.0f);
        ASSERT_NE(nullptr, off.thief);
        EXPECT_TRUE(off.cast(true));
        EXPECT_FLOAT_EQ(15.0f, off.thief->stats()->magicpoints());
        EXPECT_EQ(0, off.mines());
        EXPECT_EQ(1, off.bombs());
    }
    {
        // A hero's mine counts as a shot thrown, as the bomb does.
        ThiefScene hero(1, 1, 200.0f);
        ASSERT_NE(nullptr, hero.thief);
        hero.thief->set_owned_myguy(std::make_unique<guy>(FAMILY_THIEF));
        hero.thief->myguy->total_shots = 4;
        hero.thief->myguy->scen_shots = 2;
        EXPECT_TRUE(hero.cast(true));
        EXPECT_EQ(1, hero.mines());
        EXPECT_EQ(5, hero.thief->myguy->total_shots);
        EXPECT_EQ(3, hero.thief->myguy->scen_shots);
    }
    expect_off_twin_lays_a_bomb(1, 200.0f);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

// A bot thief beside a foe (inside the bomb gate's 35 px), in ACT_RANDOM.
struct BotScene {
    TestGameWorld tw;
    walker* thief = nullptr;
    walker* foe = nullptr;

    BotScene(short ns, float mp)
    {
        tw.world().new_specials = ns;
        thief = add_living(tw, FAMILY_THIEF, kThiefTeam, 160, 160);
        foe = add_living(tw, FAMILY_SOLDIER, kFoeTeam, 180, 160);
        if (thief == nullptr || foe == nullptr)
            return;
        thief->set_act_type(ACT_RANDOM);
        foe->set_act_type(ACT_SIT);
        thief->stats()->set_max_magicpoints(mp);
        thief->stats()->set_magicpoints(mp);
        thief->set_foe(foe);
    }

    int mines() const { return count_of(tw, Order::FX, mine_family()); }
    int bombs() const { return count_of(tw, Order::FX, FAMILY_BOMB); }
};

std::uint32_t state_where_next_is(std::uint32_t bound, std::uint32_t want)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        if (probe.next(bound) == want)
            return seed;
    }
}

}  // namespace

// The bot gate is the thief's classic slot-1 gate (unchanged) plus the shift
// coin living::check_special draws: heads with the mine affordable and the
// setting on -> a MINE; the same coin with the setting off -> the bomb; heads
// with the mine unaffordable (50 MP) -> the engine drops the shift -> the
// bomb. The bot steps off its mine as it runs from its bomb.
TEST(KitThief, ai_mine_fires_when_the_coin_lands_shifted_and_mana_allows)
{
    og::test::ScopedHookFailureGuard guard;
    const std::uint32_t heads = state_where_next_is(2, 1);
    struct Outcome {
        int mines;
        int bombs;
        float mp;
        bool walking_off;
    };
    const auto run = [&](short ns, float mp) {
        BotScene b(ns, mp);
        EXPECT_NE(nullptr, b.thief);
        b.thief->set_current_special(1);
        b.tw.world().rng_.state_ = heads;
        living* bot = dynamic_cast<living*>(b.thief);
        EXPECT_NE(nullptr, bot);
        EXPECT_TRUE(bot->check_special()) << "the foe is inside 35 px";
        EXPECT_TRUE(bot->special());
        return Outcome{b.mines(), b.bombs(),
                       b.thief->stats()->magicpoints(),
                       !b.thief->stats()->commands.empty() &&
                           b.thief->stats()->commands.front().commandtype ==
                               COMMAND_WALK};
    };
    const Outcome on = run(1, 100.0f);
    EXPECT_EQ(1, on.mines) << "heads, affordable, setting on: a mine";
    EXPECT_EQ(0, on.bombs);
    EXPECT_FLOAT_EQ(30.0f, on.mp);
    EXPECT_TRUE(on.walking_off) << "the bot steps off its mine";
    const Outcome off = run(0, 100.0f);
    EXPECT_EQ(0, off.mines) << "the same coin with the setting off";
    EXPECT_EQ(1, off.bombs);
    EXPECT_FLOAT_EQ(65.0f, off.mp);
    const Outcome poor = run(1, 50.0f);
    EXPECT_EQ(0, poor.mines) << "an unaffordable mine drops the shift";
    EXPECT_EQ(1, poor.bombs);
    EXPECT_FLOAT_EQ(15.0f, poor.mp);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The whole bot path, through living::act: from the first world-RNG state
// whose act() makes a bot thief beside a foe lay a mine with the setting on,
// the same state with the setting off drops the classic bomb instead, and
// draws exactly as many numbers (the mine's flee step is the bomb's).
TEST(KitThief, bot_thief_lays_a_mine_through_act)
{
    og::test::ScopedHookFailureGuard guard;
    std::uint32_t seed = 0;
    std::uint32_t on_state = 0;
    for (std::uint32_t candidate = 1; candidate < 400 && seed == 0;
         ++candidate) {
        BotScene b(1, 100.0f);
        ASSERT_NE(nullptr, b.thief);
        b.tw.world().rng_.state_ = candidate;
        b.thief->act();
        if (b.mines() == 1) {
            seed = candidate;
            on_state = b.tw.world().rng_.state_;
        }
    }
    ASSERT_NE(0u, seed) << "some state makes the bot lay a mine";

    BotScene off(0, 100.0f);
    ASSERT_NE(nullptr, off.thief);
    off.tw.world().rng_.state_ = seed;
    off.thief->act();
    EXPECT_EQ(0, off.mines()) << "the setting off never lays a mine";
    EXPECT_EQ(1, off.bombs()) << "the same roll drops the bomb";
    EXPECT_EQ(on_state, off.tw.world().rng_.state_)
        << "mine and bomb draw the same numbers";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The thief's own slot-1 gate is untouched by the kit: it answers the same
// with the setting off and on (yes beside a foe, no at 60 px, yes with no
// foe and three foes in 110 px, no with one) and draws nothing.
TEST(KitThief, ai_off_answers_the_classic_gate)
{
    og::test::ScopedHookFailureGuard guard;
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_NE(nullptr, fd);
    for (const short ns : {short{0}, short{1}}) {
        BotScene b(ns, 100.0f);
        ASSERT_NE(nullptr, b.thief);
        living* bot = dynamic_cast<living*>(b.thief);
        ASSERT_NE(nullptr, bot);
        b.thief->set_current_special(1);
        b.tw.world().rng_.state_ = 12345u;
        EXPECT_TRUE(og::test::check_special_ai(*fd, bot)) << "flag " << ns;
        b.foe->setxy(220, 160);
        EXPECT_FALSE(og::test::check_special_ai(*fd, bot)) << "flag " << ns;
        b.thief->set_foe(nullptr);
        EXPECT_FALSE(og::test::check_special_ai(*fd, bot))
            << "one foe in range, flag " << ns;
        ASSERT_NE(nullptr,
                  add_living(b.tw, FAMILY_SOLDIER, kFoeTeam, 170, 180));
        ASSERT_NE(nullptr,
                  add_living(b.tw, FAMILY_SOLDIER, kFoeTeam, 150, 140));
        EXPECT_TRUE(og::test::check_special_ai(*fd, bot))
            << "three foes in range, flag " << ns;
        EXPECT_EQ(12345u, b.tw.world().rng_.state_)
            << "the gate draws nothing, flag " << ns;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A bot steps off its mine on the bomb's own flee roll: one step direction
// drawn per axis, and when both come up 0 it steps right. From a state whose
// coin lands shifted and whose two flee draws are both 0 (next(3) == 1 each,
// as the roll is rand(3) - 1), the bot is told to walk right for 20 steps.
TEST(KitThief, bot_steps_right_off_a_mine_when_the_flee_roll_is_still)
{
    og::test::ScopedHookFailureGuard guard;
    std::uint32_t state = 0;
    for (std::uint32_t seed = 1; state == 0; ++seed) {
        og::sim::SimRandom probe(seed);
        if (probe.next(2) == 1 && probe.next(3) == 1 && probe.next(3) == 1)
            state = seed;
    }
    BotScene b(1, 100.0f);
    ASSERT_NE(nullptr, b.thief);
    living* bot = dynamic_cast<living*>(b.thief);
    ASSERT_NE(nullptr, bot);
    b.thief->set_current_special(1);
    b.tw.world().rng_.state_ = state;
    ASSERT_TRUE(bot->check_special());
    ASSERT_TRUE(bot->special());
    EXPECT_EQ(1, b.mines());
    const auto& commands = b.thief->stats()->commands;
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(COMMAND_WALK, commands.front().commandtype);
    EXPECT_EQ(20, commands.front().commandcount);
    EXPECT_EQ(1, commands.front().com1) << "a still roll steps right";
    EXPECT_EQ(0, commands.front().com2);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
