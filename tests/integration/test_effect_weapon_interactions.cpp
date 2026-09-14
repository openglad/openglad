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

// An arrow parked at (x, y) in the OBLIST — the list
// GameWorld::find_foe_weapons_in_range scans.
static walker* add_parked_arrow(LevelRuntimeData& level, unsigned char team,
                                float damage, short x, short y)
{
    auto weap = og::runtime::current_session->myscreen_->myloader
                    ->create_walker_owned(Order::Weapon, FAMILY_ARROW);
    if (!weap)
        return nullptr;
    walker* raw = weap.get();
    raw->set_team_num(team);
    raw->set_damage(damage);
    raw->setxy(x, y);
    level.world().oblist.push_back(std::move(weap));
    return raw;
}

// An enemy living parked at (x, y) with enough hitpoints to survive the
// guard's attack(), so the guard's own drain is the only thing under test.
static walker* add_parked_orc(LevelRuntimeData& level, short x, short y)
{
    walker* orc = level.add_ob(Order::Living, FAMILY_ORC);
    if (!orc)
        return nullptr;
    orc->set_team_num(2);
    orc->set_damage(1.0f);
    orc->stats()->set_hitpoints(10000.0f);
    orc->setxy(x, y);
    return orc;
}

// guard_tail (packs/core/lib/effect_shield.lua), shared by the magic shield and
// the boomerang: weapons FRIENDLY to the guard inside its weapon radius are
// destroyed and each costs the guard its damage in hitpoints; enemy livings
// inside the body radius are attacked and cost the guard theirs; a guard
// drained to 0 hp (or past its lifetime) dies, and a surviving guard burns one
// lifetime tick.
//
// Both guards scan from their POST-ORBIT position (center_on(owner) plus the
// drawcycle's orbit offset). drawcycle only advances in the renderer, so one
// dry act — nothing in range — parks the guard where every later act will also
// land it, and the targets go there.
TEST(EffectWeaponInteractions, effect_magic_shield_and_boomerang_absorb_friendly_weapons_and_hit_enemies)
{
    ASSERT_NE(nullptr, og::runtime::current_session->myscreen_) << "myscreen exists";

    LevelRuntimeData& level = og::runtime::current_session->myscreen_->level_runtime_data();
    auto ob_before = snapshot_ptrs(level.world().oblist);
    auto fx_before = snapshot_ptrs(level.world().fxlist);
    auto weap_before = snapshot_ptrs(level.world().weaplist);

    // Owner (team 1) for both effects.
    auto owner = make_living(FAMILY_SOLDIER, 1);
    ASSERT_NE(nullptr, owner.get()) << "owner created";
    walker* owner_raw = owner.get();
    owner_raw->setxy(100, 100);
    level.world().oblist.push_back(std::move(owner));

    // --- MAGIC_SHIELD --------------------------------------------------
    walker* shield = level.add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, shield) << "shield created";
    shield->set_owner(owner_raw);
    shield->set_team_num(1);
    shield->stats()->set_hitpoints(100.0f);
    shield->set_lifetime(5);
    shield->setxy(100, 100);
    // effect::act advances drawcycle before dispatching on_act, so the orbit
    // slot is re-pinned before EVERY act to keep the post fixed.
    shield->set_drawcycle(0);
    (void)shield->act(); // dry act: park the shield on its orbit post
    const short shield_x = shield->xpos();
    const short shield_y = shield->ypos();
    ASSERT_EQ(0, shield->dead())
        << "a dry act with nothing in range neither drains nor expires the guard";

    walker* friendly_arrow =
        add_parked_arrow(level, /*team=*/1, /*damage=*/2.0f, shield_x, shield_y);
    ASSERT_NE(nullptr, friendly_arrow) << "friendly arrow created";
    walker* enemy_arrow =
        add_parked_arrow(level, /*team=*/2, /*damage=*/40.0f, shield_x, shield_y);
    ASSERT_NE(nullptr, enemy_arrow) << "enemy arrow created";
    walker* orc = add_parked_orc(level, shield_x, shield_y);
    ASSERT_NE(nullptr, orc) << "enemy orc created";

    shield->stats()->set_hitpoints(1.0f); // low enough that the drain kills it
    shield->set_lifetime(1);
    shield->set_drawcycle(0);
    (void)shield->act();

    EXPECT_EQ(shield_x, shield->xpos())
        << "the orbit post does not move while drawcycle is frozen";
    EXPECT_EQ(shield_y, shield->ypos());
    EXPECT_EQ(1, friendly_arrow->dead())
        << "a weapon friendly to the guard inside the guard radius is absorbed";
    EXPECT_EQ(0, enemy_arrow->dead())
        << "an ENEMY weapon is not the guard's to absorb";
    EXPECT_FLOAT_EQ(1.0f - 2.0f - 1.0f, shield->stats()->hitpoints())
        << "the guard pays the absorbed arrow's damage and the orc's damage";
    EXPECT_EQ(1, shield->dead())
        << "a guard drained to 0 hp or below dies";

    // --- BOOMERANG -----------------------------------------------------
    // Park the shield's orc out of every radius so only the boomerang's own
    // foe pays into the boomerang's drain.
    orc->setxy(600, 600);

    walker* boomerang = level.add_fx_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, boomerang) << "boomerang created";
    boomerang->set_owner(owner_raw);
    boomerang->set_team_num(1);
    boomerang->stats()->set_hitpoints(100.0f);
    boomerang->set_lifetime(5);
    boomerang->set_drawcycle(1); // below the >253 early-kill branch
    boomerang->setxy(100, 100);
    (void)boomerang->act(); // dry act: park the blade on its arc (drawcycle 2)
    const short blade_x = boomerang->xpos();
    const short blade_y = boomerang->ypos();
    ASSERT_EQ(0, boomerang->dead()) << "the dry act leaves the blade flying";

    // A FRESH friendly arrow: the shield's is dead and filtered out.
    walker* arrow2 =
        add_parked_arrow(level, /*team=*/1, /*damage=*/2.0f, blade_x, blade_y);
    ASSERT_NE(nullptr, arrow2) << "second friendly arrow created";
    walker* orc2 = add_parked_orc(level, blade_x, blade_y);
    ASSERT_NE(nullptr, orc2) << "second enemy orc created";

    boomerang->stats()->set_hitpoints(5.0f); // survives this round's drain
    boomerang->set_lifetime(2);
    boomerang->set_drawcycle(1);
    (void)boomerang->act();

    EXPECT_EQ(blade_x, boomerang->xpos())
        << "the arc post does not move while drawcycle is re-pinned";
    EXPECT_EQ(blade_y, boomerang->ypos());
    EXPECT_EQ(1, arrow2->dead())
        << "the boomerang absorbs friendly weapons on the same rule";
    EXPECT_FLOAT_EQ(5.0f - 2.0f - 1.0f, boomerang->stats()->hitpoints())
        << "the blade pays the arrow's and the orc's damage";
    EXPECT_EQ(1, boomerang->lifetime())
        << "a surviving guard burns exactly one lifetime tick";
    EXPECT_EQ(0, boomerang->dead())
        << "hp still above 0 and lifetime not expired: the blade flies on";

    remove_new_objects(level, ob_before, fx_before, weap_before);
}
