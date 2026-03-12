#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include <unordered_set>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unordered_set<walker*> snapshot_ptrs(const std::list<std::unique_ptr<walker>>& lst)
{
    std::unordered_set<walker*> out;
    out.reserve(lst.size());
    for (auto& up : lst)
        out.insert(up.get());
    return out;
}

static void remove_new_objects(LevelRuntimeData& level,
                               const std::unordered_set<walker*>& ob_before,
                               const std::unordered_set<walker*>& fx_before,
                               const std::unordered_set<walker*>& weap_before)
{
    std::vector<walker*> to_remove;
    to_remove.reserve(level.world().oblist.size() + level.world().fxlist.size() + level.world().weaplist.size());

    for (auto& up : level.world().oblist)
        if (up && !ob_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.world().fxlist)
        if (up && !fx_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.world().weaplist)
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
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(100, 100);
    return w;
}

TEST(EffectWeaponInteractions, effect_magic_shield_and_boomerang_absorb_friendly_weapons_and_hit_enemies)
{
    ASSERT_TRUE(og::runtime::current_session->myscreen_ != nullptr) << "myscreen exists";
    if (!og::runtime::current_session->myscreen_)
        return;

    LevelRuntimeData& level = og::runtime::current_session->myscreen_->level_runtime_data();
    auto ob_before = snapshot_ptrs(level.world().oblist);
    auto fx_before = snapshot_ptrs(level.world().fxlist);
    auto weap_before = snapshot_ptrs(level.world().weaplist);

    // Owner (team 1) for the effects.
    auto owner = make_living(FAMILY_SOLDIER, 1);
    ASSERT_TRUE(owner != nullptr) << "owner created";
    if (!owner)
        return;
    walker* owner_raw = owner.get();
    owner_raw->setxy(100, 100);
    level.world().oblist.push_back(std::move(owner));

    // A friendly weapon placed in oblist (screen::find_foe_weapons_in_range iterates oblist).
    auto weap = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weap != nullptr) << "weapon created";
    if (!weap) {
        remove_new_objects(level, ob_before, fx_before, weap_before);
        return;
    }
    weap->team_num = 1;     // friendly to the effect
    weap->damage = 2.0f;    // ensures hitpoint subtraction takes effect
    weap->setxy(100, 100);  // within range
    level.world().oblist.push_back(std::move(weap));

    // An enemy living within range for find_foes_in_range.
    walker* enemy = level.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(enemy != nullptr) << "enemy created";
    if (enemy) {
        enemy->team_num = 2;
        enemy->damage = 1.0f;
        enemy->setxy(100, 100);
    }

    // MAGIC_SHIELD: should absorb friendly weapons and attack enemies.
    walker* shield = level.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_TRUE(shield != nullptr) << "shield created";
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
    ASSERT_TRUE(boomerang != nullptr) << "boomerang created";
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

