/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch libs-3: the rules the review found in kit_captain.lua, kit_elemental.lua, kit_faerie.lua, kit_ghost.lua, kit_marker.lua, kit_skeleton.lua, kit_thief.lua.
// Each test is named after the rule it asserts.
//
// Every kit here is a New Specials kit (absent from the old game), so every
// test runs with world.new_specials = 1; the setting-off twins of each cast
// are pinned by the kit's own test file (tests/unit/test_kit_*.cpp), which
// this file does not repeat. Every cast goes through the real
// walker::special() and every bot gate through the family's
// check_special_ai dispatch. Each test fails if a Lua hook errored
// (ScopedHookFailureGuard).

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_gameplay_context_scope.h"
#include "../test_sim_random_scope.h"
#include "bot_gate_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/kit_marker_role.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using Failure = walker::SpecialFailure;

// ---------------------------------------------------------------- shared

struct Cast {
    bool ok = false;
    Failure why = Failure::None;
    std::string reason;
    float mp_spent = 0.0f;
};

Cast cast(walker* w, int slot, bool shifted)
{
    w->set_current_special(static_cast<char>(slot));
    w->set_shifter_down(shifted ? 1 : 0);
    Cast out;
    const float before = w->stats()->magicpoints();
    out.ok = w->special(&out.why, &out.reason);
    out.mp_spent = before - w->stats()->magicpoints();
    w->set_shifter_down(0);
    return out;
}

int count_sound(const og::sim::SimEventLog& log, std::uint32_t sound)
{
    int n = 0;
    for (const auto& e : log.events())
        if (e.kind == og::sim::EventKind::PlaySound && e.a == sound)
            ++n;
    return n;
}

int notices_ending(const og::sim::SimEventLog& log, std::size_t from,
                   const std::string& tail)
{
    int n = 0;
    const auto& events = log.events();
    for (std::size_t i = from; i < events.size(); ++i)
        if (events[i].kind == og::sim::EventKind::Notification &&
            events[i].text.ends_with(tail))
            ++n;
    return n;
}

// Live entities of (order, family) on every list.
std::vector<walker*> alive(GameWorld& world, Order order, int family)
{
    std::vector<walker*> out;
    for (const auto* list : {&world.oblist, &world.fxlist, &world.weaplist})
        for (const auto& uptr : *list)
            if (uptr && !uptr->dead() && uptr->query_order() == order &&
                uptr->family() == family)
                out.push_back(uptr.get());
    return out;
}

// Every entity of (order, family) ever made and still listed, dead or not.
int listed(GameWorld& world, Order order, int family)
{
    int n = 0;
    for (const auto* list : {&world.oblist, &world.fxlist, &world.weaplist})
        for (const auto& uptr : *list)
            if (uptr && uptr->query_order() == order && uptr->family() == family)
                ++n;
    return n;
}

int fx_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::FX, id);
}

int weapon_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::Weapon, id);
}

bool in_obmap(GameWorld& world, walker* w)
{
    return world.myobmap->walker_to_pos.count(w) != 0;
}

const command* front_command(const walker* w)
{
    if (w->stats()->commands.empty())
        return nullptr;
    return &w->stats()->commands.front();
}

void grass_field(GameWorld& world, int tiles)
{
    world.resize_grid(tiles, tiles);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.mysmoother.set_target(world.grid);
    world.my_team = 0;
}

// ---------------------------------------------------------- the captain

namespace cap {

constexpr unsigned char kUs = 0;
constexpr unsigned char kThem = 1;

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y, short level = 1)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    w->stats()->set_level(level);
    w->stats()->set_max_hitpoints(200.0f);
    w->stats()->set_hitpoints(200.0f);
    w->set_busy(0);
    return w;
}

walker* add_captain(TestGameWorld& tw, short x, short y, short level = 10)
{
    walker* c = add_living(tw, FAMILY_BIG_ORC, kUs, x, y, level);
    if (c == nullptr)
        return nullptr;
    c->stats()->set_max_magicpoints(500.0f);
    c->stats()->set_magicpoints(500.0f);
    return c;
}

walker* add_stain(TestGameWorld& tw, walker* under)
{
    walker* stain = tw.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (stain == nullptr)
        return nullptr;
    stain->set_team_num(kThem);
    stain->set_dead(0);
    stain->set_floor(under->floor());
    stain->center_on(under);
    stain->stats()->set_old_family(FAMILY_SOLDIER);
    stain->stats()->set_level(3);
    return stain;
}

std::vector<walker*> banners(TestGameWorld& tw)
{
    return alive(tw.world(), Order::Weapon, weapon_family("core:war_banner"));
}

std::vector<walker*> grunts_of(TestGameWorld& tw, walker* captain)
{
    std::vector<walker*> out;
    for (walker* w : alive(tw.world(), Order::Living, FAMILY_ORC))
        if (w->owner() == captain)
            out.push_back(w);
    return out;
}

std::vector<short> xs_of(const std::vector<walker*>& ws)
{
    std::vector<short> out;
    for (const walker* w : ws)
        out.push_back(w->xpos());
    return out;
}

std::vector<short> ys_of(const std::vector<walker*>& ws)
{
    std::vector<short> out;
    for (const walker* w : ws)
        out.push_back(w->ypos());
    return out;
}

// A 512 x 512 px open field with New Specials on.
void field(TestGameWorld& tw)
{
    grass_field(tw.world(), 32);
    tw.world().new_specials = 1;
}

struct PointBlank {
    int live_knives = 0;
    int knives_made = 0;
    int clangs = 0;
    float body_hp = 0.0f;
    float mp_spent = 0.0f;
    long long shots = 0;
    long long scen_shots = 0;
    long long hits = 0;
    bool ok = false;
};

// A level-10 hero captain at (200,200) facing right with damage 1 (a blow
// of 1 is 1 - 0.5 + rng(1) = 0.5 before armour, 1 hit point after
// rounding, whatever the stream: the jitter draw of a 1 is always 0), a
// level-1 body of `team` flush against his right side (216,200), armour 0.
// The fan's middle knife has its pad (217,205) inside that body; the two
// diagonal pads (217,193) and (217,217) are clear of it.
PointBlank knife_fan_point_blank(unsigned char team, bool invincible)
{
    PointBlank out;
    TestGameWorld tw;
    field(tw);
    walker* captain = add_captain(tw, 200, 200);
    walker* body = add_living(tw, FAMILY_SOLDIER, team, 216, 200);
    EXPECT_TRUE(captain && body);
    if (captain == nullptr || body == nullptr)
        return out;
    EXPECT_EQ(16, captain->sizex());
    EXPECT_EQ(16, captain->sizey());
    captain->set_owned_myguy(std::make_unique<guy>(FAMILY_BIG_ORC));
    captain->myguy->total_shots = 0;
    captain->myguy->scen_shots = 0;
    captain->myguy->total_hits = 0;
    captain->set_damage(1.0f);
    captain->set_curdir(FACE_RIGHT);
    captain->set_lastx(1.0f);
    captain->set_lasty(0.0f);
    body->stats()->set_armor(0.0f);
    body->set_act_type(ACT_SIT);
    if (invincible)
        body->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    tw.world().rng_.state_ = 777u;
    const Cast c = cast(captain, 3, true);
    out.ok = c.ok;
    out.mp_spent = c.mp_spent;
    out.live_knives = static_cast<int>(
        alive(tw.world(), Order::Weapon, FAMILY_KNIFE).size());
    out.knives_made = listed(tw.world(), Order::Weapon, FAMILY_KNIFE);
    out.clangs = count_sound(tw.events, SOUND_CLANG);
    out.body_hp = body->stats()->hitpoints();
    out.shots = captain->myguy->total_shots;
    out.scen_shots = captain->myguy->scen_shots;
    out.hits = captain->myguy->total_hits;
    return out;
}

}  // namespace cap

// ---------------------------------------------------------- the elemental

namespace elem {

struct ElementalWorld {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    explicit ElementalWorld(short new_specials)
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = 0;
        level.set_sim_context(&save, &events, &cfg);
        level.world().new_specials = new_specials;
        level.world().tick_count_ = 1000;
    }

    GameWorld& world() { return level.world(); }

    ElementalWorld(const ElementalWorld&) = delete;
    ElementalWorld& operator=(const ElementalWorld&) = delete;
};

living* add_living(ElementalWorld& w, int family, unsigned char team, int x,
                   int y, short level = 4)
{
    walker* ob = w.world().add_ob(Order::Living, family);
    if (ob == nullptr)
        return nullptr;
    ob->set_team_num(team);
    ob->set_real_team_num(255);
    ob->setxy(static_cast<short>(x), static_cast<short>(y));
    ob->stats()->set_level(level);
    ob->stats()->set_armor(0.0f);
    ob->stats()->set_max_hitpoints(100.0f);
    ob->stats()->set_hitpoints(100.0f);
    ob->stats()->set_magicpoints(0.0f);
    return dynamic_cast<living*>(ob);
}

living* add_elemental(ElementalWorld& w, short level, float mp, int slot)
{
    living* e = add_living(w, FAMILY_FIREELEMENTAL, 0, 100, 100, level);
    if (e == nullptr)
        return nullptr;
    e->stats()->set_max_magicpoints(mp);
    e->stats()->set_magicpoints(mp);
    e->set_current_special(static_cast<char>(slot));
    e->set_shifter_down(0);
    return e;
}

std::vector<walker*> markers(ElementalWorld& w)
{
    return alive(w.world(), Order::FX, fx_family("core:kit_marker"));
}

std::vector<walker*> embers(ElementalWorld& w)
{
    return alive(w.world(), Order::FX, fx_family("core:ember"));
}

bool gate(living* bot)
{
    return og::test::check_special_ai(
        *get_family_descriptor(FAMILY_FIREELEMENTAL), bot);
}

}  // namespace elem

// ---------------------------------------------------------- the faerie

namespace faerie {

living* add_living(TestGameWorld& tw, int family, unsigned char team, short x,
                   short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    return dynamic_cast<living*>(w);
}

// A team-1 faerie of `level` with `mp` magic, standing at (x, y).
living* add_faerie(TestGameWorld& tw, short level, float mp, short x = 160,
                   short y = 160)
{
    living* f = add_living(tw, FAMILY_FAERIE, 1, x, y);
    if (f == nullptr)
        return nullptr;
    f->stats()->set_level(level);
    f->stats()->set_max_magicpoints(mp);
    f->stats()->set_magicpoints(mp);
    return f;
}

void set_hp(walker* w, float hp, float max_hp)
{
    w->stats()->set_max_hitpoints(max_hp);
    w->stats()->set_hitpoints(hp);
}

walker* add_stain(TestGameWorld& tw, unsigned char team, short x, short y)
{
    walker* stain = tw.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (stain == nullptr)
        return nullptr;
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->set_dead(0);
    stain->stats()->set_old_family(FAMILY_SOLDIER);
    stain->stats()->set_level(3);
    stain->stats()->set_max_hitpoints(120.0f);
    stain->stats()->set_hitpoints(-5.0f);
    return stain;
}

// The living standing at (x, y), or nullptr.
walker* living_at(TestGameWorld& tw, short x, short y)
{
    for (const auto& uptr : tw.world().oblist)
        if (uptr && !uptr->dead() && uptr->query_order() == Order::Living &&
            uptr->xpos() == x && uptr->ypos() == y)
            return uptr.get();
    return nullptr;
}

struct GlimmerPointBlank {
    bool ok = false;
    int live_sprinkles = 0;
    int sprinkles_made = 0;
    int marked = 0;
    int clangs = 0;
    float body_hp = 0.0f;
    int body_frozen = 0;
    float mp = 0.0f;
    long long shots = 0;
    long long hits = 0;
};

// A level-4 hero faerie (8 x 8) at (160,160) with 100 MP and damage 1 (a
// blow of exactly 1 hit point, whatever the stream), and a foe faerie
// (team 0, 8 x 8, armour 0) flush against her right side at (168,160):
// the right-hand sprinkle's pad (169, 160 + (8 - h)/2) lies inside that
// body, the up-right and down-right pads (above y 159, below y 168) do not.
GlimmerPointBlank glimmer_point_blank(bool invincible)
{
    GlimmerPointBlank out;
    TestGameWorld tw;
    grass_field(tw.world(), 32);
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 4, 100.0f);
    living* body = add_living(tw, FAMILY_FAERIE, 0, 168, 160);
    EXPECT_TRUE(f && body);
    if (f == nullptr || body == nullptr)
        return out;
    EXPECT_EQ(8, f->sizex());
    EXPECT_EQ(8, f->sizey());
    EXPECT_EQ(8, body->sizex());
    EXPECT_EQ(8, body->sizey());
    f->set_owned_myguy(std::make_unique<guy>(FAMILY_FAERIE));
    f->myguy->total_shots = 0;
    f->myguy->scen_shots = 0;
    f->myguy->total_hits = 0;
    f->set_damage(1.0f);
    f->set_lastx(1);
    f->set_lasty(0);
    f->set_busy(0.0f);
    body->stats()->set_level(1);
    body->stats()->set_armor(0.0f);
    set_hp(body, 50.0f, 50.0f);
    body->set_act_type(ACT_SIT);
    if (invincible)
        body->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    tw.world().rng_.state_ = 777u;
    const Cast c = cast(f, 2, false);
    out.ok = c.ok;
    out.live_sprinkles = static_cast<int>(
        alive(tw.world(), Order::Weapon, FAMILY_SPRINKLE).size());
    out.sprinkles_made = listed(tw.world(), Order::Weapon, FAMILY_SPRINKLE);
    for (walker* s : alive(tw.world(), Order::Weapon, FAMILY_SPRINKLE))
        if ((s->kit_state() & KIT_QUARTER_FREEZE) != 0)
            ++out.marked;
    out.clangs = count_sound(tw.events, SOUND_CLANG);
    out.body_hp = body->stats()->hitpoints();
    out.body_frozen = static_cast<int>(body->stats()->frozen_delay());
    out.mp = f->stats()->magicpoints();
    out.shots = f->myguy->total_shots;
    out.hits = f->myguy->total_hits;
    return out;
}

}  // namespace faerie

// ---------------------------------------------------------- the ghost

namespace ghost {

constexpr unsigned char kGhostTeam = 0;
constexpr unsigned char kFoeTeam = 1;

walker* add_living(TestGameWorld& tw, int family, unsigned char team, short x,
                   short y, short level, float hp)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    w->stats()->set_level(level);
    w->stats()->set_max_hitpoints(hp);
    w->stats()->set_hitpoints(hp);
    w->set_act_type(ACT_GUARD);
    return w;
}

walker* add_ghost(TestGameWorld& tw, short level, float mp, short x = 64,
                  short y = 64)
{
    walker* g = add_living(tw, FAMILY_GHOST, kGhostTeam, x, y, level, 100.0f);
    if (g == nullptr)
        return nullptr;
    g->stats()->set_max_magicpoints(mp);
    g->stats()->set_magicpoints(mp);
    return g;
}

// The first world-RNG state whose level roll (next(level_bound)) and then
// constitution roll (next(con_bound)) satisfy `resist`.
std::uint32_t possess_state(std::uint32_t level_bound, std::uint32_t con_bound,
                            bool resist)
{
    for (std::uint32_t seed = 1; seed < 2000000u; ++seed) {
        og::sim::SimRandom probe(seed);
        const std::uint32_t level_roll = probe.next(level_bound);
        const std::uint32_t con_roll = probe.next(con_bound);
        if ((con_roll > level_roll) == resist)
            return seed;
    }
    return 0;
}

// The live kit marker of `owner` playing `role`, or nullptr.
walker* marker_of(TestGameWorld& tw, const walker* owner, int role)
{
    for (walker* m : alive(tw.world(), Order::FX, fx_family("core:kit_marker")))
        if (m->owner() == owner && m->ani_type() == role)
            return m;
    return nullptr;
}

}  // namespace ghost

// ---------------------------------------------------------- the skeleton

constexpr int kSlotDigIn = 2;
constexpr int kSlotWall = 3;
constexpr int kSlotReassemble = 4;

class SemLibsSkeleton : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    std::unique_ptr<TestGameWorld> tw_;
    og::test::ScopedHookFailureGuard guard_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        if (weapon_family("core:bone_wall") != 20 ||
            fx_family("core:kit_marker") != 17)
            (void)og::resources::refresh_pack_scripts();
        ASSERT_EQ(20, weapon_family("core:bone_wall"));
        ASSERT_EQ(17, fx_family("core:kit_marker"));
        tw_ = std::make_unique<TestGameWorld>();
        grass_field(world(), 32);
        world().new_specials = 1;
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    GameWorld& world() { return tw_->world(); }

    void tick(int n = 1)
    {
        for (int i = 0; i < n; ++i)
            world().tick();
    }

    // A skeleton that takes no decisions (ACT_CONTROL, no seat), with no
    // regeneration of its own, so every MP and HP change is the kit's.
    walker* add_skeleton(unsigned char team, short x, short y, float mp,
                         int level = 4)
    {
        walker* w = world().add_ob(Order::Living, FAMILY_SKELETON);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->setxy(x, y);
        w->set_act_type(ACT_CONTROL);
        w->set_ani_type(ANI_WALK);
        w->set_cycle(0);
        w->stats()->set_level(level);
        w->stats()->set_max_hitpoints(60.0f);
        w->stats()->set_hitpoints(60.0f);
        w->stats()->set_max_magicpoints(mp);
        w->stats()->set_magicpoints(mp);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_max_magic_delay(1000000);
        w->stats()->set_heal_per_round(0.0f);
        w->set_curdir(FACE_RIGHT);
        w->set_enddir(FACE_RIGHT);
        return w;
    }

    walker* add_orc(unsigned char team, short x, short y, int level = 4)
    {
        walker* w = world().add_ob(Order::Living, FAMILY_ORC);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->setxy(x, y);
        w->set_act_type(ACT_CONTROL);
        w->stats()->set_level(level);
        w->stats()->set_max_hitpoints(80.0f);
        w->stats()->set_hitpoints(80.0f);
        w->stats()->set_heal_per_round(0.0f);
        return w;
    }

    std::vector<walker*> markers(const walker* owner, int role)
    {
        std::vector<walker*> out;
        for (walker* m : alive(world(), Order::FX, fx_family("core:kit_marker")))
            if (m->owner() == owner && m->ani_type() == role)
                out.push_back(m);
        return out;
    }

    int skeletons_on_team(unsigned char team)
    {
        int n = 0;
        for (walker* w : alive(world(), Order::Living, FAMILY_SKELETON))
            if (w->team_num() == team)
                ++n;
        return n;
    }

    bool ask_gate(walker* w, int slot)
    {
        w->set_current_special(static_cast<char>(slot));
        w->set_shifter_down(0);
        return og::test::check_special_ai(
            *get_family_descriptor(FAMILY_SKELETON), dynamic_cast<living*>(w));
    }
};

// ---------------------------------------------------------- the thief

namespace thief {

constexpr unsigned char kThiefTeam = 0;
constexpr unsigned char kFoeTeam = 1;

int mine_family() { return fx_family("core:mine"); }

walker* add_living(TestGameWorld& tw, int family, unsigned char team, short x,
                   short y)
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

}  // namespace thief

// The bot-gate fixture of test_bot_gates_kits.cpp under this file's suite.
class SemLibsCaptainGates : public og::test::bot_gates::GameplayFixesBots {};

}  // namespace

// ===========================================================================
// kit_captain.lua
// ===========================================================================

// kit_captain.lua:155 knife_fan, the point-blank arm. A knife whose pad a
// live foe body blocks is walker::fire()'s melee arm: the captain strikes
// that body (one CLANG, the blow's exact hit point gone, one hit on his
// record), the shot is spent (made and dead, one more shot on his record),
// the two other knives fly, and KNIFE FAN's 30 is all he pays. When the
// blocking body cannot be hurt (invincible) attack() answers false: no
// CLANG and no damage, but the shot is spent and counted all the same.
// An ally in the same spot does not block a weapon pad at all (obmap
// query_list: a weapon passes a friendly body), so all three knives fly.
TEST(SemLibsCaptain, a_knife_fan_point_blank_strikes_the_body_on_its_pad_and_throws_the_other_two)
{
    og::test::ScopedHookFailureGuard guard;
    const cap::PointBlank foe = cap::knife_fan_point_blank(cap::kThem, false);
    EXPECT_TRUE(foe.ok);
    EXPECT_EQ(3, foe.knives_made) << "foe: three knives made";
    EXPECT_EQ(2, foe.live_knives) << "foe: the blocked knife is spent";
    EXPECT_EQ(1, foe.clangs) << "foe: the melee blow clangs";
    EXPECT_FLOAT_EQ(199.0f, foe.body_hp) << "foe: the blow's one hit point";
    EXPECT_EQ(3, foe.shots) << "foe: two flights and the blow, each a shot";
    EXPECT_EQ(3, foe.scen_shots);
    EXPECT_EQ(1, foe.hits) << "foe: the blow is a hit";
    EXPECT_FLOAT_EQ(30.0f, foe.mp_spent) << "foe: KNIFE FAN's price, no refund";

    const cap::PointBlank wall = cap::knife_fan_point_blank(cap::kThem, true);
    EXPECT_TRUE(wall.ok);
    EXPECT_EQ(3, wall.knives_made) << "invincible: three knives made";
    EXPECT_EQ(2, wall.live_knives) << "invincible: the blocked knife is spent";
    EXPECT_EQ(0, wall.clangs) << "invincible: attack() refused, no clang";
    EXPECT_FLOAT_EQ(200.0f, wall.body_hp) << "invincible: no damage";
    EXPECT_EQ(3, wall.shots) << "invincible: the spent shot still counts";
    EXPECT_EQ(0, wall.hits);
    EXPECT_FLOAT_EQ(30.0f, wall.mp_spent);

    const cap::PointBlank ally = cap::knife_fan_point_blank(cap::kUs, false);
    EXPECT_TRUE(ally.ok);
    EXPECT_EQ(3, ally.live_knives) << "ally: a weapon passes a friendly body";
    EXPECT_EQ(0, ally.clangs);
    EXPECT_FLOAT_EQ(200.0f, ally.body_hp);
    EXPECT_EQ(3, ally.shots);
    EXPECT_FLOAT_EQ(30.0f, ally.mp_spent);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_captain.lua:212 war_banner, the first plant's sound and spot. The
// three refusals (no corpse, a corpse on another floor, a corpse out of
// reach) roar nothing; the plant roars once and stands the banner on the
// corpse's centre.
TEST(SemLibsCaptain, planting_a_war_banner_roars_once_and_stands_it_on_the_corpse)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    cap::field(tw);
    tw.world().set_floor_count(2);
    walker* captain = cap::add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);

    Cast c = cast(captain, 4, false);
    EXPECT_EQ("NO CORPSE TO PLANT ON", c.reason);
    EXPECT_EQ(0, count_sound(tw.events, SOUND_ROAR)) << "no corpse: silent";
    walker* stain = cap::add_stain(tw, captain);
    ASSERT_NE(nullptr, stain);
    stain->set_floor(1);
    c = cast(captain, 4, false);
    EXPECT_EQ("NO CORPSE TO PLANT ON", c.reason);
    EXPECT_EQ(0, count_sound(tw.events, SOUND_ROAR)) << "other floor: silent";
    stain->set_floor(0);
    stain->setxy(stain->xpos() + 20, stain->ypos() + 20);
    c = cast(captain, 4, false);
    EXPECT_EQ("NO CORPSE TO PLANT ON", c.reason);
    EXPECT_EQ(0, count_sound(tw.events, SOUND_ROAR)) << "out of reach: silent";

    stain->center_on(captain);
    const int sx = stain->xpos() + stain->sizex() / 2;
    const int sy = stain->ypos() + stain->sizey() / 2;
    c = cast(captain, 4, false);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_EQ(1, count_sound(tw.events, SOUND_ROAR)) << "the plant roars once";
    const auto planted = cap::banners(tw);
    ASSERT_EQ(1u, planted.size());
    EXPECT_EQ(sx, planted[0]->xpos() + planted[0]->sizex() / 2)
        << "on the corpse's centre (x)";
    EXPECT_EQ(sy, planted[0]->ypos() + planted[0]->sizey() / 2)
        << "on the corpse's centre (y)";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_captain.lua:266 on_the_captain: a tile that only touches the
// captain's box is clear. A level-13 captain (3 grunts) at (32,200) on the
// west scan: tile 16 touches his left edge (16 + 16 == 32), tile 32
// overlaps, tile 48 touches his right edge (48 == 32 + 16). The same on
// the north scan from (200,32): tiles 0, 16 and 48 on his column.
TEST(SemLibsCaptain, a_warband_tile_touching_the_captains_edge_is_clear_ground)
{
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        cap::field(tw);
        walker* captain = cap::add_captain(tw, 32, 200, 13);
        ASSERT_NE(nullptr, captain);
        ASSERT_EQ(16, captain->sizex());
        const Cast c = cast(captain, 4, true);
        ASSERT_TRUE(c.ok) << c.reason;
        const auto grunts = cap::grunts_of(tw, captain);
        EXPECT_EQ((std::vector<short>{0, 16, 48}), cap::xs_of(grunts))
            << "west scan: the touching tiles 16 and 48 are used";
        for (const walker* g : grunts)
            EXPECT_EQ(200, g->ypos());
    }
    {
        TestGameWorld tw;
        cap::field(tw);
        walker* captain = cap::add_captain(tw, 200, 32, 13);
        ASSERT_NE(nullptr, captain);
        ASSERT_EQ(16, captain->sizey());
        const Cast c = cast(captain, 4, true);
        ASSERT_TRUE(c.ok) << c.reason;
        const auto grunts = cap::grunts_of(tw, captain);
        EXPECT_EQ((std::vector<short>{0, 16, 48}), cap::ys_of(grunts))
            << "north scan: the touching tiles 16 and 48 are used";
        for (const walker* g : grunts)
            EXPECT_EQ(200, g->xpos());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_captain.lua:288 edge_spots, equal gaps: the first minimum in west,
// east, north, south order is kept. A captain at (256,256) on the 512
// field has four gaps of 256: the grunts come from the west, (0,256). A
// captain at (400,112) has east and north gaps of 112: east comes first;
// its scan's first tile (496) is refused by the grid (its far edge is the
// map's last pixel), so the first grunt is at (480,112).
TEST(SemLibsCaptain, a_warband_with_equal_edge_gaps_comes_in_west_east_north_south_order)
{
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        cap::field(tw);
        walker* captain = cap::add_captain(tw, 256, 256);
        ASSERT_NE(nullptr, captain);
        ASSERT_TRUE(cast(captain, 4, true).ok);
        const auto grunts = cap::grunts_of(tw, captain);
        ASSERT_EQ(2u, grunts.size());
        EXPECT_EQ(0, grunts[0]->xpos()) << "four equal gaps: the west";
        EXPECT_EQ(256, grunts[0]->ypos());
        EXPECT_EQ(16, grunts[1]->xpos());
        EXPECT_EQ(256, grunts[1]->ypos());
    }
    {
        TestGameWorld tw;
        cap::field(tw);
        walker* captain = cap::add_captain(tw, 400, 112);
        ASSERT_NE(nullptr, captain);
        ASSERT_TRUE(cast(captain, 4, true).ok);
        const auto grunts = cap::grunts_of(tw, captain);
        ASSERT_EQ(2u, grunts.size());
        EXPECT_EQ(480, grunts[0]->xpos()) << "east ties north: the east";
        EXPECT_EQ(112, grunts[0]->ypos());
        EXPECT_EQ(464, grunts[1]->xpos());
        EXPECT_EQ(112, grunts[1]->ypos());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_captain.lua:320 warband, the no-banner arm: with no banner of his
// standing every grunt's first command is COMMAND_GOTO to the captain's
// own spot (com1 = his x, com2 = his y), on all four edges.
TEST(SemLibsCaptain, a_warband_with_no_banner_runs_to_the_captains_spot)
{
    og::test::ScopedHookFailureGuard guard;
    struct Case {
        short x, y;
    };
    const Case cases[] = {{40, 200}, {470, 200}, {200, 30}, {200, 470}};
    for (const Case& e : cases) {
        TestGameWorld tw;
        cap::field(tw);
        walker* captain = cap::add_captain(tw, e.x, e.y);
        ASSERT_NE(nullptr, captain);
        const Cast c = cast(captain, 4, true);
        ASSERT_TRUE(c.ok) << c.reason;
        const auto grunts = cap::grunts_of(tw, captain);
        ASSERT_EQ(2u, grunts.size()) << e.x << "," << e.y;
        for (const walker* g : grunts) {
            const command* go = front_command(g);
            ASSERT_NE(nullptr, go) << e.x << "," << e.y;
            EXPECT_EQ(COMMAND_GOTO, go->commandtype) << e.x << "," << e.y;
            EXPECT_EQ(e.x, go->com1) << "to the captain's x";
            EXPECT_EQ(e.y, go->com2) << "to the captain's y";
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_captain.lua:420 foes_in_fan, the past-flight arm. A level-7 captain
// (no hero) facing up throws a knife 63 px on a straight line: los 7 + 7/3
// = 9 steps, 9 x 309/256 = 10 on a straight heading, the first walk only
// turns, so 9 steps of 5 x 362/256 px. A tower straight ahead at a 40 px
// gap is in flight; a second tower on the same line at a 64 px gap is past
// it (inside the gate's 200 px scan, past the hook's 60): one foe on the
// lines, no fan. Moved to a 63 px gap it is in flight: two, the fan.
TEST_F(SemLibsCaptainGates, a_bot_captain_counts_no_foe_past_the_knives_flight)
{
    living* captain = body(add(FAMILY_BIG_ORC, 1, 200, 200, 7, 100.0f), 16, 16);
    living* ahead = body(add(FAMILY_TOWER1, 2, 196, 131, 1), 25, 29);
    living* far = body(add(FAMILY_TOWER1, 2, 196, 107, 1), 25, 29);  // gap 64
    ASSERT_TRUE(captain && ahead && far);
    captain->set_lastx(0.0f);
    captain->set_lasty(-1.0f);
    captain->set_curdir(FACE_UP);
    captain->set_shifter_down(9);
    EXPECT_FALSE(kit_gate(captain, 3)) << "the second foe is past the flight";
    EXPECT_EQ(9, captain->shifter_down()) << "no shift written";

    far->setxy(196, 108);  // gap 63: the knife's last pixel
    captain->set_shifter_down(9);
    EXPECT_TRUE(kit_gate(captain, 3)) << "both inside the flight";
    EXPECT_EQ(1, captain->shifter_down()) << "the fan";
}

// kit_captain.lua:459 M.ai_banner, the tents arm (the row is deferred,
// #475: the friend count has no floor filter; that arm is not pinned
// here). og.find_friends_in_range counts living friends only, so two
// friendly tents within 96 are no friends around: with a corpse
// underfoot, a foe at 100 and no banner the plant is not chosen, and with
// one foe nothing presses for the warband either: false, no shift. Two
// living allies on the same spots plant (shift 0).
TEST_F(SemLibsCaptainGates, a_bot_captain_counts_no_tent_among_the_friends_round_a_banner)
{
    living* captain = body(add(FAMILY_BIG_ORC, 1, 200, 200, 10, 200.0f), 16, 16);
    ASSERT_NE(nullptr, captain);
    walker* s = world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, s);
    s->set_team_num(2);
    s->set_dead(0);
    s->center_on(captain);
    s->stats()->set_old_family(FAMILY_SOLDIER);
    s->stats()->set_level(3);
    walker* t1 = world().add_ob(Order::Generator, FAMILY_TENT);
    walker* t2 = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(t1 && t2);
    for (walker* t : {t1, t2}) {
        t->set_team_num(1);
        t->set_sizex(16);
        t->set_sizey(16);
    }
    t1->setxy(240, 200);
    t2->setxy(200, 240);
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 300, 200, 1), 16, 16));  // 100
    captain->set_shifter_down(9);
    EXPECT_FALSE(kit_gate(captain, 4)) << "two tents are not two friends";
    EXPECT_EQ(9, captain->shifter_down()) << "no shift written";

    t1->set_dead(1);
    t2->set_dead(1);
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 1, 240, 200, 1), 16, 16));
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 1, 200, 240, 1), 16, 16));
    captain->set_shifter_down(9);
    EXPECT_TRUE(kit_gate(captain, 4)) << "two living allies: the plant";
    EXPECT_EQ(0, captain->shifter_down());
}

// ===========================================================================
// kit_elemental.lua
// ===========================================================================

// kit_elemental.lua:36 M.end_channel, the not-burning arm: an elemental
// that dies without IMMOLATE lit keeps every kit bit as it was (on_death
// calls end_channel); one that dies burning loses the channel bit only.
TEST(SemLibsElemental, ending_immolate_on_an_elemental_that_is_not_burning_changes_no_kit_bit)
{
    og::test::ScopedHookFailureGuard guard;
    {
        elem::ElementalWorld w(1);
        living* e = elem::add_elemental(w, 5, 100.0f, 4);
        ASSERT_NE(nullptr, e);
        e->set_kit_state(KIT_FEARLESS);
        e->stats()->set_hitpoints(-3.0f);
        e->set_dead(1);
        e->death();
        EXPECT_EQ(static_cast<std::uint32_t>(KIT_FEARLESS),
                  static_cast<std::uint32_t>(e->kit_state()))
            << "not burning: nothing cleared, nothing set";
    }
    {
        elem::ElementalWorld w(1);
        living* e = elem::add_elemental(w, 4, 1000.0f, 2);
        ASSERT_NE(nullptr, e);
        ASSERT_TRUE(cast(e, 2, false).ok);
        ASSERT_NE(0, e->kit_state() & KIT_CHANNEL);
        e->set_kit_state(e->kit_state() | KIT_FEARLESS);
        e->stats()->set_hitpoints(-3.0f);
        e->set_dead(1);
        e->death();
        EXPECT_EQ(static_cast<std::uint32_t>(KIT_FEARLESS),
                  static_cast<std::uint32_t>(e->kit_state()))
            << "burning: only the channel bit goes";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_elemental.lua:81 ember_underfoot, the other-floor arm: an ember of
// the elemental's own on the spot under its feet but on another floor is
// no ember underfoot, so the next drop lays a new one, on the elemental's
// floor, at the bottom centre of its box.
TEST(SemLibsElemental, a_burning_elementals_own_ember_on_another_floor_does_not_count_as_underfoot)
{
    og::test::ScopedHookFailureGuard guard;
    elem::ElementalWorld w(1);
    w.world().set_floor_count(2);
    living* e = elem::add_elemental(w, 4, 10000.0f, 2);
    ASSERT_NE(nullptr, e);
    ASSERT_TRUE(cast(e, 2, false).ok);
    walker* marker = elem::markers(w).at(0);
    for (int t = 1; t <= 4; ++t)
        ASSERT_TRUE(marker->act());
    auto lit = elem::embers(w);
    ASSERT_EQ(1u, lit.size()) << "the first drop on the fourth tick";
    walker* first = lit[0];
    first->set_floor(1);
    for (int t = 5; t <= 8; ++t)
        ASSERT_TRUE(marker->act());
    lit = elem::embers(w);
    ASSERT_EQ(2u, lit.size()) << "the ember upstairs is not under its feet";
    walker* second = lit[0] == first ? lit[1] : lit[0];
    EXPECT_EQ(0, second->floor()) << "the new one on the elemental's floor";
    EXPECT_EQ(e->xpos() + (e->sizex() - second->sizex()) / 2, second->xpos());
    EXPECT_EQ(e->ypos() + e->sizey(), second->ypos() + second->sizey());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_elemental.lua:177 meteor_rain_act, the strike's sound: each strike
// (every sixth of the rain's sixty ticks) plays one SOUND_EXPLODE; the
// ticks between are silent.
TEST(SemLibsElemental, every_meteor_rain_strike_plays_one_explosion_sound)
{
    og::test::ScopedHookFailureGuard guard;
    elem::ElementalWorld w(1);
    living* e = elem::add_elemental(w, 7, 1000.0f, 3);
    ASSERT_NE(nullptr, e);
    living* foe = elem::add_living(w, FAMILY_SOLDIER, 1, 200, 100);
    ASSERT_NE(nullptr, foe);
    e->set_foe(foe);
    ASSERT_TRUE(cast(e, 3, false).ok);
    walker* rain = elem::markers(w).at(0);
    const int before = count_sound(w.events, SOUND_EXPLODE);
    std::vector<int> after;
    for (int t = 1; t <= 60; ++t) {
        ASSERT_TRUE(rain->act());
        after.push_back(count_sound(w.events, SOUND_EXPLODE) - before);
    }
    EXPECT_EQ(0, after[4]) << "silent before the sixth tick";
    EXPECT_EQ(1, after[5]) << "the first strike";
    EXPECT_EQ(1, after[10]);
    EXPECT_EQ(2, after[11]);
    EXPECT_EQ(10, after[59]) << "ten strikes, ten sounds";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_elemental.lua:266 rekindle, the hurt arm's sound: each paid heal
// plays one SOUND_HEAL; the refusal at full health plays none.
TEST(SemLibsElemental, a_rekindle_heal_plays_one_heal_sound_and_the_refusal_none)
{
    og::test::ScopedHookFailureGuard guard;
    elem::ElementalWorld w(1);
    living* e = elem::add_elemental(w, 10, 200.0f, 4);
    ASSERT_NE(nullptr, e);
    e->stats()->set_hitpoints(10.0f);
    ASSERT_TRUE(cast(e, 4, false).ok);
    EXPECT_EQ(1, count_sound(w.events, SOUND_HEAL)) << "the first heal";
    ASSERT_TRUE(cast(e, 4, false).ok);
    EXPECT_EQ(2, count_sound(w.events, SOUND_HEAL)) << "the second heal";
    const Cast full = cast(e, 4, false);
    EXPECT_EQ("ALREADY AT FULL HEALTH", full.reason);
    EXPECT_EQ(2, count_sound(w.events, SOUND_HEAL)) << "the refusal is silent";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_elemental.lua:362 M.ai_meteor, the friendly-tent arm (the row is
// deferred, #458; the enemy-tent cluster and the other-floor ally arms are
// the suspect's and are not pinned here). og.find_friends_in_range counts
// living friends only, so a friendly tent inside the strike box does not
// hold the rain: two clustered foes, a team-0 tent 30 px below the target ->
// true. A living ally on the same spot holds it.
TEST(SemLibsElemental, a_friendly_tent_in_the_strike_box_does_not_hold_a_bot_meteor_rain)
{
    og::test::ScopedHookFailureGuard guard;
    elem::ElementalWorld w(1);
    living* bot = elem::add_elemental(w, 7, 200.0f, 3);
    ASSERT_NE(nullptr, bot);
    living* foe = elem::add_living(w, FAMILY_SOLDIER, 1, 220, 100);
    living* second = elem::add_living(w, FAMILY_SOLDIER, 1, 250, 100);
    ASSERT_TRUE(foe && second);
    walker* tent = w.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(0);
    tent->set_sizex(16);
    tent->set_sizey(16);
    tent->setxy(220, 130);
    bot->set_foe(foe);
    EXPECT_TRUE(elem::gate(bot)) << "a friendly tent is no ally under the rain";
    living* ally = elem::add_living(w, FAMILY_SOLDIER, 0, 220, 130);
    ASSERT_NE(nullptr, ally);
    bot->set_foe(foe);
    EXPECT_FALSE(elem::gate(bot)) << "a living ally on the spot holds it";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// kit_faerie.lua
// ===========================================================================

// kit_faerie.lua:81 hasten_partner, the other-floor arm: the nearer ally
// (24 px) on another floor is skipped and the farther one (36 px) on the
// faerie's floor is hastened.
TEST(SemLibsFaerie, hasten_skips_an_ally_on_another_floor)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().set_floor_count(2);
    tw.world().new_specials = 1;
    living* f = faerie::add_faerie(tw, 7, 200.0f, 160, 160);
    living* near = faerie::add_living(tw, FAMILY_SOLDIER, 1, 184, 160);   // 24
    living* other = faerie::add_living(tw, FAMILY_SOLDIER, 1, 160, 196);  // 36
    ASSERT_TRUE(f && near && other);
    near->set_floor(1);
    const Cast c = cast(f, 3, false);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_EQ(0, near->speed_bonus_left()) << "upstairs: skipped";
    EXPECT_EQ(150, other->speed_bonus_left()) << "the ally on her floor";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_faerie.lua:101 wish_stain, the other-floor arm: the nearest friendly
// stain (40 px) on another floor is never raised; the next one on her
// floor (100 px) is. With both upstairs: NO FALLEN ALLY NEARBY, free.
TEST(SemLibsFaerie, wish_never_raises_a_fallen_ally_on_another_floor)
{
    og::test::ScopedHookFailureGuard guard;
    for (const bool both_up : {false, true}) {
        SCOPED_TRACE(both_up ? "both stains upstairs" : "the near stain upstairs");
        TestGameWorld tw;
        tw.world().set_floor_count(2);
        tw.world().new_specials = 1;
        living* f = faerie::add_faerie(tw, 10, 300.0f, 160, 160);
        walker* near = faerie::add_stain(tw, 1, 200, 160);  // 40
        walker* far = faerie::add_stain(tw, 1, 260, 160);   // 100
        ASSERT_TRUE(f && near && far);
        near->set_floor(1);
        if (both_up)
            far->set_floor(1);
        const Cast c = cast(f, 4, false);
        if (!both_up) {
            ASSERT_TRUE(c.ok) << c.reason;
            EXPECT_FALSE(near->dead()) << "the stain upstairs stays";
            EXPECT_TRUE(far->dead()) << "the one on her floor is used";
            EXPECT_NE(nullptr, faerie::living_at(tw, 260, 160));
            EXPECT_EQ(nullptr, faerie::living_at(tw, 200, 160));
        } else {
            EXPECT_FALSE(c.ok);
            EXPECT_EQ("NO FALLEN ALLY NEARBY", c.reason);
            EXPECT_FLOAT_EQ(300.0f, f->stats()->magicpoints());
            EXPECT_FALSE(near->dead());
            EXPECT_FALSE(far->dead());
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_faerie.lua:133 heal_party, the whole-ally arm: an ally at full health
// in the radius gets no heal and no number; the hurt ones each get one
// number of exactly what they were missing, and the healer sees one
// number for each heal (the risen ally's included).
TEST(SemLibsFaerie, wish_shows_no_heal_number_over_an_ally_already_whole)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = faerie::add_faerie(tw, 10, 300.0f, 160, 160);
    walker* stain = faerie::add_stain(tw, 1, 200, 160);
    living* hurt = faerie::add_living(tw, FAMILY_SOLDIER, 1, 160, 200);   // 40
    living* whole = faerie::add_living(tw, FAMILY_SOLDIER, 1, 180, 160);  // 20
    ASSERT_TRUE(f && stain && hurt && whole);
    faerie::set_hp(hurt, 20.0f, 100.0f);
    faerie::set_hp(whole, 100.0f, 100.0f);
    const Cast c = cast(f, 4, false);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(100.0f, whole->stats()->hitpoints());
    EXPECT_TRUE(whole->damage_numbers.empty()) << "no number over a whole ally";
    ASSERT_EQ(1u, hurt->damage_numbers.size());
    EXPECT_FLOAT_EQ(80.0f, hurt->damage_numbers.front().value);
    EXPECT_FLOAT_EQ(100.0f, hurt->stats()->hitpoints());
    ASSERT_EQ(2u, f->damage_numbers.size()) << "the risen one and the hurt one";
    EXPECT_FLOAT_EQ(120.0f, f->damage_numbers.front().value);
    EXPECT_FLOAT_EQ(80.0f, f->damage_numbers.back().value);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_faerie.lua:257 glimmer, the point-blank arm (the row is deferred:
// the truncated-aim arm is triaged unreachable). A direction whose pad a
// live foe body blocks is fire()'s melee arm: one CLANG, the blow's exact
// hit point, one hit; the shot is spent (made and dead) and counted, the
// other seven fly with the quarter-freeze mark, GLIMMER's 40 is all she
// pays, and the struck body is not frozen (fire() answered nil, so no mark
// went on a sprinkle that never flew). An invincible body: no CLANG, no
// damage, the shot spent and counted all the same.
TEST(SemLibsFaerie, a_glimmer_point_blank_strikes_the_body_on_its_pad_and_fires_the_other_seven)
{
    og::test::ScopedHookFailureGuard guard;
    const faerie::GlimmerPointBlank foe = faerie::glimmer_point_blank(false);
    EXPECT_TRUE(foe.ok);
    EXPECT_EQ(8, foe.sprinkles_made) << "foe: eight made";
    EXPECT_EQ(7, foe.live_sprinkles) << "foe: the blocked one is spent";
    EXPECT_EQ(7, foe.marked) << "foe: every flying sprinkle carries the mark";
    EXPECT_EQ(1, foe.clangs) << "foe: the blow clangs";
    EXPECT_FLOAT_EQ(49.0f, foe.body_hp) << "foe: the blow's one hit point";
    EXPECT_EQ(0, foe.body_frozen) << "foe: a blow, not a freeze";
    EXPECT_EQ(8, foe.shots) << "foe: seven flights and the blow";
    EXPECT_EQ(1, foe.hits);
    EXPECT_FLOAT_EQ(60.0f, foe.mp) << "foe: GLIMMER's 40, no refund";

    const faerie::GlimmerPointBlank wall = faerie::glimmer_point_blank(true);
    EXPECT_TRUE(wall.ok);
    EXPECT_EQ(8, wall.sprinkles_made);
    EXPECT_EQ(7, wall.live_sprinkles) << "invincible: the blocked one is spent";
    EXPECT_EQ(0, wall.clangs) << "invincible: attack() refused";
    EXPECT_FLOAT_EQ(50.0f, wall.body_hp);
    EXPECT_EQ(8, wall.shots) << "invincible: the spent shot still counts";
    EXPECT_EQ(0, wall.hits);
    EXPECT_FLOAT_EQ(60.0f, wall.mp);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_faerie.lua:354 M.ai_blink, the other-floor arm (triaged a test gap:
// the cornered check asks foes_within with floor_only false, like the
// other flee gates). A hurt bot faerie whose only foe within 30 px stands
// on another floor blinks (true, shift 0).
TEST(SemLibsFaerie, a_hurt_bot_faerie_blinks_from_a_foe_within_30_on_another_floor)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().set_floor_count(2);
    tw.world().new_specials = 1;
    living* f = faerie::add_faerie(tw, 1, 52.0f, 160, 160);
    living* foe = faerie::add_living(tw, FAMILY_SOLDIER, 0, 136, 160);  // 24
    ASSERT_TRUE(f && foe);
    foe->set_floor(1);
    faerie::set_hp(f, 30.0f, 75.0f);
    f->set_current_special(1);
    f->set_shifter_down(7);
    EXPECT_TRUE(og::test::check_special_ai(*get_family_descriptor(FAMILY_FAERIE), f))
        << "a foe within 30 on any floor corners her";
    EXPECT_EQ(0, f->shifter_down()) << "BLINK";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// kit_ghost.lua
// ===========================================================================

// kit_ghost.lua:30 wail, the launch's sound: a WAIL at a foe in the scare
// radius plays one SOUND_FWIP; NO FOE TO WAIL AT and COULD NOT CREATE
// WAIL play none.
TEST(SemLibsGhost, a_wail_launch_plays_one_fwip_and_its_refusals_none)
{
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* g = ghost::add_ghost(tw, 10, 100.0f, 100, 100);
        ASSERT_NE(nullptr, g);
        ASSERT_NE(nullptr, ghost::add_living(tw, FAMILY_ORC, ghost::kFoeTeam,
                                             140, 100, 1, 140.0f));
        const Cast c = cast(g, 1, true);
        ASSERT_TRUE(c.ok) << c.reason;
        EXPECT_EQ(1, count_sound(tw.events, SOUND_FWIP)) << "the launch";
    }
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* g = ghost::add_ghost(tw, 10, 100.0f, 100, 100);
        ASSERT_NE(nullptr, g);
        const Cast c = cast(g, 1, true);
        EXPECT_EQ("NO FOE TO WAIL AT", c.reason);
        EXPECT_EQ(0, count_sound(tw.events, SOUND_FWIP)) << "no foe: silent";
    }
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* g = ghost::add_ghost(tw, 10, 300.0f);
        ASSERT_NE(nullptr, g);
        ASSERT_NE(nullptr, ghost::add_living(tw, FAMILY_ORC, ghost::kFoeTeam,
                                             80, 64, 1, 140.0f));
        const auto factory = tw.world().entity_factory;
        tw.world().entity_factory = [factory](Order order, std::int32_t family) {
            if (order == Order::FX)
                return std::unique_ptr<walker>();
            return factory(order, family);
        };
        const Cast c = cast(g, 1, true);
        EXPECT_EQ("COULD NOT CREATE WAIL", c.reason);
        EXPECT_EQ(0, count_sound(tw.events, SOUND_FWIP)) << "world full: silent";
        tw.world().entity_factory = factory;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_ghost.lua:214 M.possess, the taken arm's sound: a ride that lands
// plays one SOUND_FWIP; a resisted touch and every free refusal play none.
TEST(SemLibsGhost, a_possession_that_lands_plays_one_fwip_and_a_resisted_one_none)
{
    og::test::ScopedHookFailureGuard guard;
    // Orc: 140 hp, no guy -> con 4 -> the roll is over 40; ghost level 7 ->
    // over 70.
    const std::uint32_t lands = ghost::possess_state(70, 40, false);
    const std::uint32_t resists = ghost::possess_state(70, 40, true);
    ASSERT_NE(0u, lands);
    ASSERT_NE(0u, resists);
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* g = ghost::add_ghost(tw, 7, 100.0f);
        walker* orc = ghost::add_living(tw, FAMILY_ORC, ghost::kFoeTeam, 80, 64,
                                        1, 140.0f);
        ASSERT_TRUE(g && orc);
        tw.world().rng_.state_ = lands;
        const Cast c = cast(g, 3, false);
        ASSERT_TRUE(c.ok) << c.reason;
        ASSERT_EQ(orc->entity_id(), g->possess_link()) << "the ride landed";
        EXPECT_EQ(1, count_sound(tw.events, SOUND_FWIP)) << "landed: one fwip";
    }
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* g = ghost::add_ghost(tw, 7, 100.0f);
        walker* orc = ghost::add_living(tw, FAMILY_ORC, ghost::kFoeTeam, 80, 64,
                                        1, 140.0f);
        ASSERT_TRUE(g && orc);
        tw.world().rng_.state_ = resists;
        const Cast c = cast(g, 3, false);
        ASSERT_TRUE(c.ok) << c.reason;
        ASSERT_EQ(0u, g->possess_link()) << "the host resisted";
        EXPECT_EQ(0, count_sound(tw.events, SOUND_FWIP)) << "resisted: silent";
    }
    {
        // A refusal: no host in reach.
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* g = ghost::add_ghost(tw, 7, 100.0f);
        ASSERT_NE(nullptr, ghost::add_living(tw, FAMILY_ORC, ghost::kFoeTeam,
                                             300, 300, 1, 140.0f));
        ASSERT_NE(nullptr, g);
        const Cast c = cast(g, 3, false);
        EXPECT_EQ("NO HOST IN REACH", c.reason);
        EXPECT_EQ(0, count_sound(tw.events, SOUND_FWIP)) << "refused: silent";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// kit_marker.lua
// ===========================================================================

// kit_marker.lua:70 M.find, the other-owner and dead arms. Ghost A's live
// PHASE veil never answers for ghost B (B phases), and A's own veil once
// dead never answers for A (A phases again, not ALREADY PHASED). A's own
// live veil answers (ALREADY PHASED) as the control.
TEST(SemLibsMarker, a_kit_marker_answers_only_for_its_live_owner)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* a = ghost::add_ghost(tw, 10, 300.0f, 64, 64);
    walker* b = ghost::add_ghost(tw, 10, 300.0f, 200, 200);
    ASSERT_TRUE(a && b);
    a->set_act_type(ACT_CONTROL);
    b->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(cast(a, 4, false).ok);
    a->set_busy(0.0f);
    EXPECT_EQ("ALREADY PHASED", cast(a, 4, false).reason) << "A's own live veil";

    const Cast for_b = cast(b, 4, false);
    EXPECT_TRUE(for_b.ok) << "A's veil is not B's: " << for_b.reason;
    EXPECT_TRUE(b->stats()->query_bit_flags(BIT_PHANTOM));
    EXPECT_NE(nullptr, ghost::marker_of(tw, b, MARKER_PHASE_VEIL));

    walker* veil = ghost::marker_of(tw, a, MARKER_PHASE_VEIL);
    ASSERT_NE(nullptr, veil);
    veil->set_dead(1);
    a->set_busy(0.0f);
    const Cast again = cast(a, 4, false);
    EXPECT_TRUE(again.ok) << "a dead veil never answers: " << again.reason;
    EXPECT_FLOAT_EQ(180.0f, a->stats()->magicpoints()) << "two PHASEs paid";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// kit_skeleton.lua
// ===========================================================================

// kit_skeleton.lua:155 sink, the taken-over arm: when anything else takes
// over the sinking skeleton's animation it goes under on that very tick
// (sink age 2, not 4): hidden, frame 27, parked on walk, the channel
// cleared, out of the obmap, where it stood.
TEST_F(SemLibsSkeleton, a_sinking_skeleton_whose_animation_is_taken_over_goes_under_at_once)
{
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotDigIn, false).ok);
    tick();
    ASSERT_FALSE(skel->hidden());
    ASSERT_EQ(25, skel->frame());
    skel->set_ani_type(ANI_ATTACK);
    tick();
    EXPECT_TRUE(skel->hidden()) << "under on the second tick";
    EXPECT_EQ(27, skel->frame());
    EXPECT_EQ(ANI_WALK, skel->ani_type());
    EXPECT_EQ(0, skel->kit_state() & KIT_CHANNEL);
    EXPECT_FALSE(in_obmap(world(), skel));
    EXPECT_EQ(96, skel->xpos());
    EXPECT_EQ(96, skel->ypos());
}

// kit_skeleton.lua:193 buried, the regen pulse's clamp: a buried skeleton
// at 59.5 of 60 regenerates to exactly 60 on its pulse, never 60.5, and
// stays at 60 on the next.
TEST_F(SemLibsSkeleton, a_buried_skeletons_regen_pulse_never_heals_past_its_maximum)
{
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotDigIn, false).ok);
    tick(4);
    ASSERT_TRUE(skel->hidden());
    skel->stats()->set_hitpoints(59.5f);
    tick(4);
    EXPECT_FLOAT_EQ(60.0f, skel->stats()->hitpoints()) << "clamped on the pulse";
    tick(4);
    EXPECT_FLOAT_EQ(60.0f, skel->stats()->hitpoints()) << "and on the next";
}

// kit_skeleton.lua:354 bone_wall, the cast arm's sound: a wall with a
// segment placed clangs once; the refusal (NO ROOM FOR A WALL) is silent.
TEST_F(SemLibsSkeleton, a_bone_wall_clangs_once_when_it_stands_and_never_when_refused)
{
    walker* skel = add_skeleton(0, 96, 96, 60.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotWall, false).ok);
    EXPECT_EQ(1, count_sound(tw_->events, SOUND_CLANG)) << "the wall stands";
    const Cast again = cast(skel, kSlotWall, false);
    EXPECT_EQ("NO ROOM FOR A WALL", again.reason);
    EXPECT_EQ(1, count_sound(tw_->events, SOUND_CLANG)) << "the refusal is silent";
}

// kit_skeleton.lua:506 ward_window, the unpaid pulse: an armed ward whose
// pulse finds less than ward_drain (1) in the pool fades on that pulse
// (clock 354, not the timeout), with its notice, and takes nothing.
TEST_F(SemLibsSkeleton, an_armed_ward_fades_on_a_pulse_it_cannot_pay)
{
    walker* skel = add_skeleton(0, 96, 96, 160.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotReassemble, false).ok);
    ASSERT_FLOAT_EQ(100.0f, skel->stats()->magicpoints());
    const auto wards = markers(skel, MARKER_WARD);
    ASSERT_EQ(1u, wards.size());
    walker* ward = wards[0];
    const std::size_t mark = tw_->events.events().size();
    tick(5);
    ASSERT_EQ(355, ward->lifetime());
    ASSERT_FLOAT_EQ(100.0f, skel->stats()->magicpoints()) << "no pulse yet";
    skel->stats()->set_magicpoints(0.5f);
    ward->act();  // the sixth tick, by hand: the pulse at 354
    EXPECT_TRUE(ward->dead());
    EXPECT_EQ(354, ward->lifetime()) << "the dry pulse, not the timeout";
    EXPECT_EQ(0, skel->kit_state() & KIT_WARD) << "the ward faded";
    EXPECT_EQ(1, notices_ending(tw_->events, mark, "'s ward fades"));
    EXPECT_FLOAT_EQ(0.5f, skel->stats()->magicpoints()) << "nothing taken";
}

// kit_skeleton.lua:557 M.on_kill, the heads arm's level floor: a level-1
// foe killed in the LEGION window on heads rises at level 1 (max(1, 1/2)),
// on the killer's team, where it fell. An undead foe killed on heads stays
// down.
TEST_F(SemLibsSkeleton, a_legion_kill_of_a_level_1_foe_rises_at_level_1)
{
    const std::uint32_t heads = state_where_draws_are({1, 1, 1, 2}, {0, 0, 0, 1});
    walker* skel = add_skeleton(0, 96, 96, 120.0f, 10);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotReassemble, true).ok);
    skel->set_damage(1000.0f);
    walker* orc = add_orc(1, 112, 96, 1);
    ASSERT_NE(nullptr, orc);
    world().rng_.state_ = heads;
    ASSERT_TRUE(skel->attack(orc));
    ASSERT_TRUE(orc->dead());
    walker* risen = nullptr;
    for (walker* w : alive(world(), Order::Living, FAMILY_SKELETON))
        if (w != skel)
            risen = w;
    ASSERT_NE(nullptr, risen) << "heads: it rises";
    EXPECT_EQ(1, risen->stats()->level()) << "never below level 1";
    EXPECT_EQ(0, risen->team_num());
    EXPECT_EQ(112, risen->xpos());
    EXPECT_EQ(96, risen->ypos());

    walker* undead = add_skeleton(1, 300, 300, 0.0f);
    ASSERT_NE(nullptr, undead);
    world().rng_.state_ = heads;
    ASSERT_TRUE(skel->attack(undead));
    ASSERT_TRUE(undead->dead());
    EXPECT_EQ(2, skeletons_on_team(0)) << "an undead kill on heads stays down";
}

// kit_skeleton.lua:657 M.ai_dig_in, the mid-teleport arm: a bot skeleton
// whose gate would dig in (hurt, two foes close) never does while it is in
// either teleport row.
TEST_F(SemLibsSkeleton, a_bot_skeleton_mid_teleport_never_picks_dig_in)
{
    walker* skel = add_skeleton(1, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, add_orc(0, 150, 96));
    ASSERT_NE(nullptr, add_orc(0, 96, 150));
    skel->stats()->set_hitpoints(30.0f);
    ASSERT_TRUE(ask_gate(skel, kSlotDigIn)) << "walking: it digs";
    skel->set_ani_type(ANI_TELE_OUT);
    EXPECT_FALSE(ask_gate(skel, kSlotDigIn)) << "tele-out";
    skel->set_ani_type(ANI_TELE_IN);
    EXPECT_FALSE(ask_gate(skel, kSlotDigIn)) << "tele-in";
    skel->set_ani_type(ANI_WALK);
    EXPECT_TRUE(ask_gate(skel, kSlotDigIn)) << "walking again";
}

// ===========================================================================
// kit_thief.lua
// ===========================================================================

// kit_thief.lua:31 step_off, the rolled-step arm: a flee roll that moves
// is walked as rolled, never replaced by the still roll's (+1, 0): rolls
// (-1, +1) and (0, +1).
TEST(SemLibsThief, a_bot_thief_walks_off_its_mine_on_the_step_it_rolled)
{
    og::test::ScopedHookFailureGuard guard;
    struct Roll {
        std::uint32_t dx_draw, dy_draw;
        int dx, dy;
    };
    const Roll rolls[] = {{0, 2, -1, 1}, {1, 2, 0, 1}};
    for (const Roll& r : rolls) {
        const std::uint32_t state =
            state_where_draws_are({2, 3, 3}, {1, r.dx_draw, r.dy_draw});
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* t = thief::add_living(tw, FAMILY_THIEF, thief::kThiefTeam, 160, 160);
        walker* foe = thief::add_living(tw, FAMILY_SOLDIER, thief::kFoeTeam, 193, 160);
        ASSERT_TRUE(t && foe);
        t->set_act_type(ACT_RANDOM);
        foe->set_act_type(ACT_SIT);
        t->stats()->set_max_magicpoints(100.0f);
        t->stats()->set_magicpoints(100.0f);
        t->set_foe(foe);
        foe->set_foe(t);
        t->set_current_special(1);
        tw.world().rng_.state_ = state;
        living* bot = dynamic_cast<living*>(t);
        ASSERT_NE(nullptr, bot);
        ASSERT_TRUE(bot->check_special());
        ASSERT_TRUE(bot->special());
        ASSERT_EQ(1, static_cast<int>(
                         alive(tw.world(), Order::FX, thief::mine_family()).size()));
        const command* walk = front_command(t);
        ASSERT_NE(nullptr, walk);
        EXPECT_EQ(COMMAND_WALK, walk->commandtype);
        EXPECT_EQ(20, walk->commandcount);
        EXPECT_EQ(r.dx, walk->com1) << "the rolled x step";
        EXPECT_EQ(r.dy, walk->com2) << "the rolled y step";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// kit_thief.lua:62 mine, the seated arm: a thief driven by a seat lays its
// mine and is left where it is: no command queued, no flee roll drawn.
TEST(SemLibsThief, a_seated_thief_lays_a_mine_without_stepping_off_it)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* t = thief::add_living(tw, FAMILY_THIEF, thief::kThiefTeam, 160, 160);
    ASSERT_NE(nullptr, t);
    t->set_user(0);
    t->set_act_type(ACT_CONTROL);
    t->stats()->set_max_magicpoints(500.0f);
    t->stats()->set_magicpoints(500.0f);
    ASSERT_TRUE(t->stats()->commands.empty());
    tw.world().rng_.state_ = 4242u;
    const Cast c = cast(t, 1, true);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_EQ(1, static_cast<int>(
                     alive(tw.world(), Order::FX, thief::mine_family()).size()));
    EXPECT_TRUE(t->stats()->commands.empty()) << "no walk queued for a seat";
    EXPECT_EQ(4242u, tw.world().rng_.state_) << "no flee roll drawn";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
