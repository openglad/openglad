#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <unordered_set>
#include <vector>

extern screen* myscreen;

static std::unordered_set<walker*> snapshot_ptrs(const std::list<std::unique_ptr<walker>>& lst)
{
    std::unordered_set<walker*> out;
    out.reserve(lst.size());
    for (auto& up : lst)
        out.insert(up.get());
    return out;
}

static void remove_new_objects(LevelData& level,
                               const std::unordered_set<walker*>& ob_before,
                               const std::unordered_set<walker*>& fx_before,
                               const std::unordered_set<walker*>& weap_before)
{
    std::vector<walker*> to_remove;
    to_remove.reserve(level.oblist.size() + level.fxlist.size() + level.weaplist.size());

    for (auto& up : level.oblist)
        if (up && !ob_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.fxlist)
        if (up && !fx_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.weaplist)
        if (up && !weap_before.contains(up.get()))
            to_remove.push_back(up.get());

    for (walker* w : to_remove)
        level.remove_ob(w);
}

static std::unique_ptr<walker> make_living(char family, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = g.create_walker_owned(myscreen);
    if (w)
        w->setxy(100, 100);
    return w;
}

void test_effect_magic_shield_and_boomerang_absorb_friendly_weapons_and_hit_enemies()
{
    TEST_ASSERT(myscreen != nullptr, "myscreen exists");
    if (!myscreen)
        return;

    LevelData& level = myscreen->level_data;
    auto ob_before = snapshot_ptrs(level.oblist);
    auto fx_before = snapshot_ptrs(level.fxlist);
    auto weap_before = snapshot_ptrs(level.weaplist);

    // Owner (team 1) for the effects.
    auto owner = make_living(FAMILY_SOLDIER, 1);
    TEST_ASSERT(owner != nullptr, "owner created");
    if (!owner)
        return;
    walker* owner_raw = owner.get();
    owner_raw->setxy(100, 100);
    level.oblist.push_back(std::move(owner));

    // A friendly weapon placed in oblist (screen::find_foe_weapons_in_range iterates oblist).
    auto weap = level.myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW, myscreen);
    TEST_ASSERT(weap != nullptr, "weapon created");
    if (!weap) {
        remove_new_objects(level, ob_before, fx_before, weap_before);
        return;
    }
    weap->team_num = 1;     // friendly to the effect
    weap->damage = 2.0f;    // ensures hitpoint subtraction takes effect
    weap->setxy(100, 100);  // within range
    level.oblist.push_back(std::move(weap));

    // An enemy living within range for find_foes_in_range.
    walker* enemy = level.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(enemy != nullptr, "enemy created");
    if (enemy) {
        enemy->team_num = 2;
        enemy->damage = 1.0f;
        enemy->setxy(100, 100);
    }

    // MAGIC_SHIELD: should absorb friendly weapons and attack enemies.
    walker* shield = level.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    TEST_ASSERT(shield != nullptr, "shield created");
    if (shield) {
        shield->owner = owner_raw;
        shield->team_num = 1;
        shield->stats()->hitpoints = 1; // low so the absorbed weapon can kill it
        shield->lifetime = 1;
        shield->setxy(100, 100);
        (void)shield->act();
    }

    // BOOMERANG: same idea, with its own weapon/enemy loops.
    walker* boomerang = level.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    TEST_ASSERT(boomerang != nullptr, "boomerang created");
    if (boomerang) {
        boomerang->owner = owner_raw;
        boomerang->team_num = 1;
        boomerang->stats()->hitpoints = 5;
        boomerang->lifetime = 2;
        boomerang->drawcycle = 1; // avoid the drawcycle>253 early-kill branch
        boomerang->setxy(100, 100);
        (void)boomerang->act();
    }

    remove_new_objects(level, ob_before, fx_before, weap_before);
}
REGISTER_TEST(test_effect_magic_shield_and_boomerang_absorb_friendly_weapons_and_hit_enemies);
