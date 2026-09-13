#include <openglad/interface/game_context.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/guy_create.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
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

// Every walker of `order`/`family` that appeared in `lst` since `before`.
static std::vector<walker*> new_of_family(const std::list<std::unique_ptr<walker>>& lst,
                                          const std::unordered_set<walker*>& before,
                                          Order order, int family)
{
    std::vector<walker*> out;
    for (auto& up : lst) {
        walker* w = up.get();
        if (w && !before.contains(w) && w->query_order() == order &&
            w->family() == static_cast<char>(family))
            out.push_back(w);
    }
    return out;
}

static void remove_new_leveldata_objects(LevelRuntimeData& level,
                                        const std::unordered_set<walker*>& ob_before,
                                        const std::unordered_set<walker*>& fx_before,
                                        const std::unordered_set<walker*>& weap_before)
{
    std::vector<walker*> to_remove;
    to_remove.reserve((level.world().oblist.size() + level.world().fxlist.size() + level.world().weaplist.size()));

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
    return guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
}

// effect_chain.lua on_act, contact branch: a bolt that overlaps its leader
// detonates — it spawns an EXPLOSION carrying the bolt's own damage and owner,
// grants the struck leader 3 rounds of strike immunity (skip_exit += 3), then
// forks up to rand0(owner level)+1 successor bolts at the OTHER foes in range,
// each armed with trunc(damage * 0.5); finally the bolt dies.
//
// The fork budget is made deterministic without touching the RNG: the owner is
// level 1, so rand0(1) is 0 by construction (IRandom::next(1) can only answer
// 0) and the budget is exactly one slot. foes come back in oblist order, so
// putting foe2 AHEAD of the leader spends that one slot on foe2 — the leader
// itself can never take a bolt (its skip_exit is already 3 by then) — and foe3,
// past the budget, must get nothing.
TEST(EffectChainAndDoor, effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx)
{
    ASSERT_NE(nullptr, og::runtime::current_session->myscreen_) << "myscreen exists";

    LevelRuntimeData& level = og::runtime::current_session->myscreen_->level_runtime_data();

    const auto ob_before = snapshot_ptrs(level.world().oblist);
    const auto fx_before = snapshot_ptrs(level.world().fxlist);
    const auto weap_before = snapshot_ptrs(level.world().weaplist);

    // -----------------------------------------------------------------------
    // FAMILY_CHAIN: an overlapping leader hit detonates, forks and dies.
    // -----------------------------------------------------------------------
    auto owner_up = make_living(FAMILY_SOLDIER, /*team*/ 0);
    auto leader_up = make_living(FAMILY_ORC, /*team*/ 1);
    auto foe2_up = make_living(FAMILY_ORC, /*team*/ 1);
    auto foe3_up = make_living(FAMILY_ORC, /*team*/ 1);
    ASSERT_TRUE(owner_up && leader_up && foe2_up && foe3_up) << "livings created";

    owner_up->setxy(100, 100);
    leader_up->setxy(120, 120);
    foe2_up->setxy(140, 120);
    foe3_up->setxy(160, 120);

    walker* owner = owner_up.get();
    walker* leader = leader_up.get();
    walker* foe2 = foe2_up.get();
    walker* foe3 = foe3_up.get();

    // One fork slot: rand0(1) cannot answer anything but 0.
    owner->stats()->set_level(1);

    // find_foes_in_range answers in oblist order, so foe2 precedes the leader.
    level.world().oblist.push_back(std::move(owner_up));
    level.world().oblist.push_back(std::move(foe2_up));
    level.world().oblist.push_back(std::move(leader_up));
    level.world().oblist.push_back(std::move(foe3_up));

    walker* chain = level.add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_NE(nullptr, chain) << "chain created";
    chain->set_team_num(0);
    chain->set_owner(owner);
    chain->set_leader(leader);
    chain->set_lineofsight(5);
    chain->set_damage(100.0f); // fork damage = trunc(100 * 0.5) = 50 (> 20)
    chain->setxy(leader->xpos(), leader->ypos()); // guarantee hits() with leader

    const short leader_skip_before = leader->skip_exit();
    ASSERT_EQ(0, foe2->skip_exit()) << "foe2 is fork-eligible";
    ASSERT_EQ(0, foe3->skip_exit()) << "foe3 is fork-eligible";
    const auto ob_before_strike = snapshot_ptrs(level.world().oblist);

    ASSERT_TRUE(chain->act()) << "the chain handled its own act";

    EXPECT_EQ(1, chain->dead()) << "a bolt that struck its leader is spent";
    EXPECT_EQ(leader_skip_before + 3, leader->skip_exit())
        << "the struck leader gets 3 rounds of strike immunity";

    const std::vector<walker*> blasts = new_of_family(
        level.world().oblist, ob_before_strike, Order::FX, FAMILY_EXPLOSION);
    ASSERT_EQ(1u, blasts.size()) << "the strike spawns exactly one explosion";
    EXPECT_EQ(owner, blasts[0]->owner())
        << "the blast answers to the bolt's owner";
    EXPECT_FLOAT_EQ(100.0f, blasts[0]->damage())
        << "the blast carries the bolt's FULL damage";
    EXPECT_EQ(ANI_EXPLODE, static_cast<int>(blasts[0]->ani_type()));
    EXPECT_EQ(0, blasts[0]->distance_to_ob_center(chain))
        << "the blast is centred on the bolt";

    const std::vector<walker*> bolts = new_of_family(
        level.world().oblist, ob_before_strike, Order::FX, FAMILY_CHAIN);
    ASSERT_EQ(1u, bolts.size())
        << "one fork slot spends on one successor bolt, and never on the leader";
    EXPECT_EQ(foe2, bolts[0]->leader())
        << "the fork seeks the first eligible foe in list order";
    EXPECT_FLOAT_EQ(50.0f, bolts[0]->damage())
        << "a successor inherits trunc(damage * 0.5)";
    EXPECT_EQ(owner, bolts[0]->owner())
        << "a successor keeps the original caster as owner";

    // -----------------------------------------------------------------------
    // FAMILY_DOOR_OPEN: hands the opened-door sprite to a NON-ACTING fxlist
    // copy at the same spot (so it cannot respawn itself) and retires.
    // -----------------------------------------------------------------------
    walker* door_open = level.add_fx_ob(Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_NE(nullptr, door_open) << "door_open created";
    // Snapshot AFTER the door itself is on the list: it is a DOOR_OPEN too.
    const auto fx_before_door = snapshot_ptrs(level.world().fxlist);
    door_open->set_ani_type(ANI_WALK);
    door_open->set_curdir(FACE_DOWN);
    door_open->setworldxy(200.0f, 200.0f);

    ASSERT_TRUE(door_open->act()) << "the door handled its own act";
    EXPECT_EQ(1, door_open->dead()) << "the animating door retires itself";

    const std::vector<walker*> opened = new_of_family(
        level.world().fxlist, fx_before_door, Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_EQ(1u, opened.size())
        << "exactly one opened-door effect lands on the fxlist";
    EXPECT_FLOAT_EQ(200.0f, opened[0]->worldx()) << "the copy stays put";
    EXPECT_FLOAT_EQ(200.0f, opened[0]->worldy());
    EXPECT_EQ(1, opened[0]->ignore())
        << "the copy is ignored by collision (it is scenery)";
    EXPECT_EQ(FACE_DOWN, opened[0]->curdir()) << "the copy keeps the facing";
    EXPECT_EQ(ANI_WALK, static_cast<int>(opened[0]->ani_type()));

    remove_new_leveldata_objects(level, ob_before, fx_before, weap_before);
}

TEST(EffectChainAndDoor, effect_chain_early_exit_and_movement_branches)
{
    ASSERT_TRUE(og::runtime::current_session->myscreen_ != nullptr) << "myscreen exists";
    if (!og::runtime::current_session->myscreen_)
        return;

    LevelRuntimeData& level = og::runtime::current_session->myscreen_->level_runtime_data();

    // Missing leader should kill the chain immediately.
    walker* chain = level.add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_TRUE(chain != nullptr) << "chain created";
    if (chain) {
        chain->set_owner(nullptr);
        chain->set_leader(nullptr);
        chain->set_lineofsight(0);
        chain->set_dead(0);
        (void)chain->act();
        ASSERT_TRUE(chain->dead() == 1) << "chain without leader/owner should die";
    }

    // Non-hit movement path: chain should move toward leader and consume LOS.
    walker* owner = level.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* leader = level.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(owner != nullptr && leader != nullptr) << "owner and leader created";
    if (!(owner && leader))
        return;

    owner->setxy(20, 20);
    leader->setxy(260, 180);

    walker* moving_chain = level.add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_TRUE(moving_chain != nullptr) << "moving chain created";
    if (!moving_chain)
        return;

    moving_chain->set_owner(owner);
    moving_chain->set_leader(leader);
    moving_chain->set_lineofsight(5);
    moving_chain->setxy(40, 40);
    const short x_before = moving_chain->xpos();
    const short y_before = moving_chain->ypos();
    (void)moving_chain->act();
    ASSERT_TRUE(moving_chain->lineofsight() == 4) << "movement path should decrement lineofsight";
    ASSERT_TRUE(moving_chain->xpos() != x_before || moving_chain->ypos() != y_before) << "movement path should move toward leader";

    level.delete_objects();
}


// effect_chain.lua on_act, movement branch: each act that does NOT reach the
// leader burns exactly one lineofsight and steps toward the leader with the
// sign of the offset on each axis independently (an axis with no offset does
// not move at all). An act that DOES overlap the leader is a contact, not a
// move: it detonates and burns no lineofsight.
TEST(EffectChainAndDoor, effect_chain_movement_axis_delta_branches)
{
    ASSERT_NE(nullptr, og::runtime::current_session->myscreen_) << "myscreen exists";

    LevelRuntimeData& level = og::runtime::current_session->myscreen_->level_runtime_data();
    level.delete_objects();

    walker* owner = level.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* leader = level.add_ob(Order::Living, FAMILY_ORC);
    walker* chain = level.add_fx_ob(Order::FX, FAMILY_CHAIN);
    ASSERT_NE(nullptr, owner) << "owner created";
    ASSERT_NE(nullptr, leader) << "leader created";
    ASSERT_NE(nullptr, chain) << "chain created";

    owner->set_team_num(0);
    leader->set_team_num(1);
    chain->set_team_num(0);
    chain->set_owner(owner);
    chain->set_leader(leader);
    chain->set_lineofsight(20);
    chain->setxy(200, 200);

    // Each leg parks the leader 100 px off the bolt's CURRENT position on the
    // named axes — far enough that the boxes never overlap, so the move branch
    // is what runs.
    auto park_leader = [&](int dx, int dy) {
        leader->setxy(static_cast<short>(chain->xpos() + dx),
                      static_cast<short>(chain->ypos() + dy));
    };

    // Leg 1: leader east and north -> x up, y down.
    park_leader(+100, -100);
    short px = chain->xpos();
    short py = chain->ypos();
    ASSERT_TRUE(chain->act()) << "the chain handled its own act";
    EXPECT_GT(chain->xpos(), px) << "leader to the east: the bolt steps east";
    EXPECT_LT(chain->ypos(), py) << "leader to the north: the bolt steps north";
    EXPECT_EQ(19, chain->lineofsight()) << "one move burns one lineofsight";
    EXPECT_EQ(0, chain->dead()) << "a non-contact act leaves the bolt flying";

    // Leg 2: leader west and south -> both signs flip.
    park_leader(-100, +100);
    px = chain->xpos();
    py = chain->ypos();
    ASSERT_TRUE(chain->act());
    EXPECT_LT(chain->xpos(), px) << "leader to the west: the bolt steps west";
    EXPECT_GT(chain->ypos(), py) << "leader to the south: the bolt steps south";
    EXPECT_EQ(18, chain->lineofsight());

    // Leg 3: same column -> the x branch is skipped entirely, y still moves.
    park_leader(0, -100);
    px = chain->xpos();
    py = chain->ypos();
    ASSERT_TRUE(chain->act());
    EXPECT_EQ(px, chain->xpos()) << "equal x: neither x branch fires";
    EXPECT_LT(chain->ypos(), py) << "the y axis still closes";
    EXPECT_EQ(17, chain->lineofsight());

    // Leg 4: the leader is ON the bolt, so this act is a CONTACT — it
    // detonates (immunising the leader) and burns NO lineofsight.
    const short skip_before = leader->skip_exit();
    park_leader(0, 0);
    ASSERT_TRUE(chain->act());
    EXPECT_EQ(1, chain->dead()) << "overlapping the leader detonates the bolt";
    EXPECT_EQ(skip_before + 3, leader->skip_exit())
        << "the struck leader gets 3 rounds of strike immunity";
    EXPECT_EQ(17, chain->lineofsight())
        << "a contact act consumes no lineofsight";

    level.delete_objects();
}
