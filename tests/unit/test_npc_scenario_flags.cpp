/* Per-placed-NPC scenario extras: specials-disabled + delayed spawn +
 * SAVE_ALL "protected".
 *
 * These are the direct sim/serialization tests for the v10 reserved-block
 * fields (npc_flags at reserved[3], spawn_delay at reserved[4..5]):
 *   - loader/saver round-trip through the real .fss writer/reader,
 *   - guard hold-post (npc_flags bit 1) round-trip: waking guard, hold-post
 *     guard, and the inert hold-post non-guard,
 *   - the all-default writer still emitting v9 (the parity byte-identity pin),
 *   - walker::special()/living::check_special() gating (no fire, no charge),
 *   - a deterministic twin-mage run: the enabled twin initiates its teleport
 *     special, the disabled twin never fires any special,
 *   - dormancy: invisible to obmap/targeting/snapshots before the spawn tick,
 *     present after, with the teleport-in flash flourish,
 *   - victory logic: a dormant-only enemy team keeps the level alive,
 *   - SCEN_TYPE_SAVE_ALL scoping (Wave F2): summoned/charmed walkers never
 *     fail the mission; with any npc_flags bit-2 "protected" walker placed,
 *     ONLY flagged walkers are watched; with none, the legacy any-named-
 *     team-0 rule still applies (full back-compat).
 * og_test_parity stays blind to all of this by construction: every branch is
 * gated on a non-default field value that no golden scenario sets. (The
 * "delayed spawn" TRACE only exists in TESTING-compiled gameplay builds; these
 * unit binaries link the production components, so the tests assert the
 * structural effects instead of trace text.)
 */
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/og_file.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace {

// True when the snapshot's oblist carries the entity id.
bool snapshot_has_entity(const og::sim::WorldSnapshot& snap, std::uint32_t id)
{
    return std::any_of(snap.oblist.begin(), snap.oblist.end(),
                       [id](const og::sim::EntitySnapshot& e) {
                           return e.entity_id == id;
                       });
}

// Count alive FX walkers of `family` in the world's oblist.
int count_fx(const GameWorld& w, int family)
{
    int n = 0;
    for (const auto& uptr : w.oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && !ob->dead() && ob->query_order() == Order::FX &&
            ob->family() == family)
            ++n;
    }
    return n;
}

// Save the world to a PhysFS-write-dir file and return the version byte the
// writer chose (asserting the FSS header). Returns -1 on failure.
int save_and_read_version(GameWorld& world, const std::string& filename)
{
    og::data::LevelFileMetadata metadata;
    metadata.grid_file = "npcflags";
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(world, filename, metadata, &err))
        return -1;

    auto infile = og::io::og_open_read(filename.c_str());
    if (!infile)
        return -1;
    char header[3] = {};
    char version = 0;
    if (infile->read(header, 1, 3) != 3 || infile->read(&version, 1, 1) != 1)
        return -1;
    if (header[0] != 'F' || header[1] != 'S' || header[2] != 'S')
        return -1;
    return static_cast<int>(version);
}

} // namespace

TEST(NpcScenarioFlags, defaults_are_inert_and_level_saves_as_v9)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* ob = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(ob, nullptr);
    ob->setxy(64, 64);

    EXPECT_FALSE(ob->specials_disabled());
    EXPECT_EQ(0u, ob->spawn_delay());
    EXPECT_FALSE(ob->dormant());

    // The parity pin: a single-floor level with all-default NPC extras must
    // keep saving as v9, byte-identical to the pre-feature writer.
    EXPECT_EQ(9, save_and_read_version(w, "npcflags_default.fss"));
}

TEST(NpcScenarioFlags, round_trip_v10_specials_disabled_and_spawn_delay)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* no_special = w.add_ob(Order::Living, FAMILY_MAGE);
    walker* delayed = w.add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(no_special, nullptr);
    ASSERT_NE(delayed, nullptr);
    no_special->setxy(64, 64);
    no_special->set_team_num(1);
    no_special->set_specials_disabled(true);
    delayed->setxy(128, 128);
    delayed->set_team_num(2);
    delayed->set_spawn_delay(1234);

    // Non-default extras force the v10 reserved-block layout even though the
    // level is single-floor.
    const std::string file = "npcflags_roundtrip.fss";
    ASSERT_EQ(10, save_and_read_version(w, file));

    // Reload through the production version-bridge reader.
    TestGameWorld tw2(2);
    GameWorld& w2 = tw2.world();
    auto infile = og::io::og_open_read(file.c_str());
    ASSERT_TRUE(infile);
    char skip[4] = {};
    ASSERT_EQ(4u, infile->read(skip, 1, 4)); // FSS header + version byte
    og::data::LevelFileMetadata metadata;
    ASSERT_EQ(1, og::data::load_scenario_version(*infile, &w2, &metadata, 10));

    const walker* loaded_no_special = nullptr;
    const walker* loaded_delayed = nullptr;
    for (const auto& uptr : w2.oblist)
    {
        const walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->family() == FAMILY_MAGE)
            loaded_no_special = ob;
        else if (ob->family() == FAMILY_SKELETON)
            loaded_delayed = ob;
    }
    ASSERT_NE(loaded_no_special, nullptr) << "mage should survive the round trip";
    ASSERT_NE(loaded_delayed, nullptr) << "skeleton should survive the round trip";

    EXPECT_TRUE(loaded_no_special->specials_disabled());
    EXPECT_EQ(0u, loaded_no_special->spawn_delay());
    EXPECT_FALSE(loaded_no_special->dormant());

    EXPECT_FALSE(loaded_delayed->specials_disabled());
    EXPECT_EQ(1234u, loaded_delayed->spawn_delay());
    EXPECT_TRUE(loaded_delayed->dormant())
        << "a loaded walker with spawn_delay > 0 must start the level dormant";
}

TEST(NpcScenarioFlags, round_trip_v10_guard_hold_post)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    // (a) a waking guard: GUARD command byte, hold-post clear (the default) —
    //     the ambush-post policy both regenerated campaigns ship for enemy
    //     guards (they wake into ACT_RANDOM on a genuine sighting).
    walker* waking = w.add_ob(Order::Living, FAMILY_ORC);
    // (b) a hold-post guard: GUARD byte + npc_flags bit 1 — the classic
    //     stationary sentry (the allied-post policy).
    walker* posted = w.add_ob(Order::Living, FAMILY_SKELETON);
    // (c) a hold-post NON-guard: bit 1 set on an ACT_RANDOM living — the bit
    //     must round-trip but is inert (walker::act_guard is its only reader).
    walker* roamer = w.add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(waking, nullptr);
    ASSERT_NE(posted, nullptr);
    ASSERT_NE(roamer, nullptr);

    waking->setxy(64, 64);
    waking->set_team_num(2);
    waking->set_act_type(ACT_GUARD);

    posted->setxy(128, 128);
    posted->set_team_num(2);
    posted->set_act_type(ACT_GUARD);
    posted->set_guard_hold_post(true);

    roamer->setxy(192, 192);
    roamer->set_team_num(1);
    roamer->set_act_type(ACT_RANDOM);
    roamer->set_guard_hold_post(true);

    // The hold-post bits are non-default extras: the file must go v10.
    const std::string file = "npcflags_holdpost.fss";
    ASSERT_EQ(10, save_and_read_version(w, file));

    // Reload through the production version-bridge reader.
    TestGameWorld tw2(2);
    GameWorld& w2 = tw2.world();
    auto infile = og::io::og_open_read(file.c_str());
    ASSERT_TRUE(infile);
    char skip[4] = {};
    ASSERT_EQ(4u, infile->read(skip, 1, 4)); // FSS header + version byte
    og::data::LevelFileMetadata metadata;
    ASSERT_EQ(1, og::data::load_scenario_version(*infile, &w2, &metadata, 10));

    const walker* loaded_waking = nullptr;
    const walker* loaded_posted = nullptr;
    const walker* loaded_roamer = nullptr;
    for (const auto& uptr : w2.oblist)
    {
        const walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->family() == FAMILY_ORC)
            loaded_waking = ob;
        else if (ob->family() == FAMILY_SKELETON)
            loaded_posted = ob;
        else if (ob->family() == FAMILY_ELF)
            loaded_roamer = ob;
    }
    ASSERT_NE(loaded_waking, nullptr) << "orc should survive the round trip";
    ASSERT_NE(loaded_posted, nullptr)
        << "skeleton should survive the round trip";
    ASSERT_NE(loaded_roamer, nullptr) << "elf should survive the round trip";

    EXPECT_EQ(ACT_GUARD, loaded_waking->act_type());
    EXPECT_FALSE(loaded_waking->guard_hold_post())
        << "a waking guard must reload with the hold-post bit clear";

    EXPECT_EQ(ACT_GUARD, loaded_posted->act_type());
    EXPECT_TRUE(loaded_posted->guard_hold_post())
        << "npc_flags bit 1 must survive the round trip";
    EXPECT_FALSE(loaded_posted->specials_disabled())
        << "bit 1 must not bleed into bit 0";
    EXPECT_FALSE(loaded_posted->save_all_protected())
        << "bit 1 must not bleed into bit 2";

    EXPECT_NE(ACT_GUARD, loaded_roamer->act_type())
        << "the hold-post bit must not smuggle in a GUARD act type";
    EXPECT_TRUE(loaded_roamer->guard_hold_post())
        << "bit 1 round-trips on a non-guard too (inert: act_guard is its "
           "only reader)";
}

TEST(NpcScenarioFlags, specials_disabled_gates_direct_special_and_ai_check)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.rng_.state_ = 0x12345u;

    walker* enabled = w.add_ob(Order::Living, FAMILY_MAGE);
    walker* disabled = w.add_ob(Order::Living, FAMILY_MAGE);
    ASSERT_NE(enabled, nullptr);
    ASSERT_NE(disabled, nullptr);
    enabled->setxy(5 * GRID_SIZE, 5 * GRID_SIZE);
    disabled->setxy(12 * GRID_SIZE, 12 * GRID_SIZE);
    for (walker* mage : {enabled, disabled})
    {
        mage->set_team_num(1);
        mage->set_real_team_num(1);
        mage->stats()->set_level(1);
        mage->stats()->set_magicpoints(1000.0f);
        mage->set_current_special(1);
    }
    disabled->set_specials_disabled(true);

    // Direct execution path: the enabled twin fires (and is charged), the
    // disabled twin does not fire and is not charged.
    const float enabled_mp = enabled->stats()->magicpoints();
    EXPECT_TRUE(enabled->special());
    EXPECT_LT(enabled->stats()->magicpoints(), enabled_mp)
        << "an executed special must consume its magic cost";

    const float disabled_mp = disabled->stats()->magicpoints();
    EXPECT_FALSE(disabled->special());
    EXPECT_EQ(disabled_mp, disabled->stats()->magicpoints())
        << "a disabled special must not consume any charge";

    // AI decision path: check_special() must refuse before consulting family
    // AI (the enabled twin, with no foes in range, says yes).
    EXPECT_TRUE(enabled->check_special());
    EXPECT_FALSE(disabled->check_special());
}

TEST(NpcScenarioFlags, enabled_twin_mage_teleports_disabled_twin_never)
{
    // Two identical worlds, identical seeds, one flag difference: over the
    // same deterministic act() run the enabled mage initiates its teleport
    // special (ANI_TELE_OUT) while the disabled mage never fires ANY special —
    // no teleport wind-up and no teleport marker (special 1's other branch).
    struct MageRun
    {
        bool teleported = false;
        bool placed_marker = false;
    };
    auto run_mage = [](bool specials_disabled, int ticks) -> MageRun {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.rng_.state_ = 0x9E3779B9u;

        walker* mage = w.add_ob(Order::Living, FAMILY_MAGE);
        MageRun result;
        if (mage == nullptr)
            return result;
        mage->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
        mage->set_team_num(1);
        mage->set_real_team_num(1);
        mage->set_act_type(ACT_RANDOM);
        mage->stats()->set_level(1); // (level+2)/3 == 1 -> always special 1 (teleport)
        mage->set_specials_disabled(specials_disabled);

        for (int t = 0; t < ticks && !result.teleported; ++t)
        {
            mage->stats()->set_magicpoints(1000.0f); // never starved of charge
            mage->set_in_act(true);
            mage->act();
            mage->set_in_act(false);
            if (mage->ani_type() == ANI_TELE_OUT)
                result.teleported = true;
            if (count_fx(w, FAMILY_MARKER) > 0)
                result.placed_marker = true;
        }
        return result;
    };

    const MageRun enabled = run_mage(false, 4000);
    EXPECT_TRUE(enabled.teleported)
        << "the enabled twin should initiate its teleport special";

    const MageRun disabled = run_mage(true, 4000);
    EXPECT_FALSE(disabled.teleported)
        << "the specials-disabled twin must never initiate a teleport";
    EXPECT_FALSE(disabled.placed_marker)
        << "the specials-disabled twin must never fire the marker branch either";
}

TEST(NpcScenarioFlags, dormant_walker_hidden_until_spawn_tick_then_enters_world)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.rng_.state_ = 1u;
    w.my_team = 0;

    walker* hero = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* invader = w.add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(hero, nullptr);
    ASSERT_NE(invader, nullptr);

    hero->setxy(2 * GRID_SIZE, 2 * GRID_SIZE);
    hero->set_team_num(0);
    hero->set_real_team_num(0);
    hero->set_act_type(ACT_GUARD);

    invader->setxy(14 * GRID_SIZE, 12 * GRID_SIZE);
    invader->set_team_num(1);
    invader->set_real_team_num(1);
    invader->set_act_type(ACT_RANDOM);
    invader->set_spawn_delay(3);
    invader->set_dormant(true);

    const std::uint32_t invader_id = invader->entity_id();
    const float spot_x = static_cast<float>(invader->xpos());
    const float spot_y = static_cast<float>(invader->ypos());

    // --- Before the spawn tick -------------------------------------------
    // Uncollidable: the dormant walker's spot reads as passable to others.
    EXPECT_TRUE(w.query_object_passable(spot_x, spot_y, hero))
        << "a dormant walker must be absent from the obmap";
    // Untargetable: foe scans skip it.
    EXPECT_EQ(nullptr, w.find_far_foe(hero))
        << "a dormant walker must not be targetable";
    // Unsnapshotted: capture excludes it (mirrors/replays never see it).
    EXPECT_FALSE(snapshot_has_entity(og::sim::capture_keyframe_snapshot(w),
                                     invader_id));
    // No flourish yet.
    EXPECT_EQ(0, count_fx(w, FAMILY_FLASH));

    // Victory logic: the dormant enemy team still counts as alive.
    for (int t = 1; t <= 3; ++t)
    {
        w.tick(); // level_tick_count 1..3: within the delay, still dormant
        EXPECT_FALSE(w.game_ended)
            << "a dormant-only enemy team must keep the level alive (tick "
            << t << ")";
        EXPECT_EQ(0, w.level_done);
        EXPECT_TRUE(invader->dormant());
    }

    // --- The spawn tick ----------------------------------------------------
    w.tick(); // level_tick_count 4 > spawn_delay 3: activation
    EXPECT_FALSE(invader->dormant());

    // Present: collidable again at its (possibly moved) position...
    EXPECT_FALSE(w.query_object_passable(static_cast<float>(invader->xpos()),
                                         static_cast<float>(invader->ypos()),
                                         hero))
        << "an activated walker must be back in the obmap";
    // ...targetable...
    EXPECT_EQ(invader, w.find_far_foe(hero));
    // ...and captured by snapshots.
    EXPECT_TRUE(snapshot_has_entity(og::sim::capture_keyframe_snapshot(w),
                                    invader_id));
    // Teleport-in flourish: the activation spawned a FLASH fx.
    EXPECT_GE(count_fx(w, FAMILY_FLASH), 1)
        << "activation should emit the teleport-in flash flourish";

    // Once the (only) enemy actually dies, the level completes: dormancy was
    // the thing keeping it alive earlier.
    invader->set_dead(1);
    invader->myguy = nullptr;
    for (const auto& uptr : w.oblist)
    {
        // Let the flourish finish so no live FX lingers; only the hero's team
        // remains alive after the invader dies.
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::FX)
            ob->set_dead(1);
    }
    w.tick();
    EXPECT_TRUE(w.game_ended)
        << "with the activated invader dead, no live or dormant foes remain";
    EXPECT_EQ(2, w.level_done);
}

// Regression (PR playtest): dormant walkers were interactable before their
// spawn tick — walking into a dormant foe's cell collided with an invisible
// body and triggered melee. Every interaction (collision blocking, melee
// collide_ob, weapon hits, treasure eat dispatch) routes through the obmap
// piles in ob_pass_check, so the contract is a single invariant: a dormant
// walker is NEVER obmap-registered. This test drives the real movement code:
// a soldier physically walks straight through the dormant foe's cell with no
// collision, a projectile probe passes over it, and after the wake tick the
// same walk is blocked with collide_ob set (the melee trigger) again.
TEST(NpcScenarioFlags, dormant_foe_cell_is_walk_through_until_wake)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.rng_.state_ = 1u;
    w.my_team = 0;

    walker* hero = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* lurker = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, hero);
    ASSERT_NE(nullptr, lurker);

    hero->set_team_num(0);
    hero->set_real_team_num(0);
    hero->set_act_type(ACT_GUARD);

    lurker->setxy(10 * GRID_SIZE, 8 * GRID_SIZE);
    lurker->set_team_num(1);
    lurker->set_real_team_num(1);
    lurker->set_act_type(ACT_GUARD);
    lurker->set_spawn_delay(2);
    lurker->set_dormant(true);

    // The terrain is all-grass (create_new_grid), so any blocked step below
    // can only come from an obmap occupant.
    ASSERT_NE(nullptr, w.myobmap.get());
    EXPECT_EQ(w.myobmap->walker_to_pos.end(),
              w.myobmap->walker_to_pos.find(lurker))
        << "a dormant walker must not be obmap-registered";

    const float step = hero->stepsize();
    ASSERT_GT(step, 0.0f);

    // Walk the hero eastwards straight through the dormant foe's cell: every
    // step must succeed and none may set collide_ob (no melee trigger, and no
    // eat/door dispatch either — those fire from the same obmap pile scan).
    hero->setxy(static_cast<short>(lurker->xpos() - 2 * GRID_SIZE),
                lurker->ypos());
    hero->set_curdir(static_cast<signed char>(FACE_RIGHT));
    const short beyond =
        static_cast<short>(lurker->xpos() + lurker->sizex() + GRID_SIZE);
    int guard = 0;
    while (hero->xpos() < beyond && guard++ < 300)
    {
        EXPECT_TRUE(hero->walk(step, 0.0f))
            << "walking through a dormant foe's cell must be free (x="
            << hero->xpos() << ")";
        EXPECT_EQ(nullptr, hero->collide_ob())
            << "no collision may be recorded against a dormant walker (x="
            << hero->xpos() << ")";
    }
    ASSERT_LT(guard, 300) << "the hero must actually cross the cell";

    // A projectile probe over the cell: weapons hit via the same obmap query,
    // so a hostile arrow's path across the dormant foe must read as clear.
    walker* arrow = w.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, arrow);
    arrow->set_team_num(0);
    arrow->set_owner(hero);
    EXPECT_TRUE(w.query_object_passable(
        static_cast<float>(lurker->xpos()),
        static_cast<float>(lurker->ypos()), arrow))
        << "attacks must not be able to hit a dormant walker";
    arrow->set_dead(1); // probe done: keep it out of the wake ticks below

    // The walk-through changed nothing the NEXT WAVE HUD reads: still
    // oblist-resident, dormant, authored delay intact.
    EXPECT_TRUE(lurker->dormant());
    EXPECT_EQ(2u, static_cast<unsigned>(lurker->spawn_delay()));

    // Park the hero away from the spawn cell and let the delay elapse.
    hero->setxy(3 * GRID_SIZE, 14 * GRID_SIZE);
    w.tick(); // level tick 1
    w.tick(); // level tick 2
    w.tick(); // level tick 3 > delay 2: wake
    ASSERT_FALSE(lurker->dormant());
    EXPECT_NE(w.myobmap->walker_to_pos.end(),
              w.myobmap->walker_to_pos.find(lurker))
        << "waking must re-register the walker in the obmap";

    // The same eastward walk is now blocked, and the block records the melee
    // collision target — solid exactly like a normally spawned foe.
    hero->setxy(static_cast<short>(lurker->xpos() - hero->sizex()),
                lurker->ypos());
    hero->set_curdir(static_cast<signed char>(FACE_RIGHT));
    bool blocked = false;
    for (int i = 0; i < 8 && !blocked; ++i)
        blocked = !hero->walk(step, 0.0f);
    EXPECT_TRUE(blocked) << "an awakened walker must block movement";
    EXPECT_EQ(lurker, hero->collide_ob())
        << "the blocked step must record the melee collision target";
}

// The intangibility invariant must survive repositioning: setxy/setworldxy/
// change_floor route obmap bookkeeping, and none of them may re-register a
// dormant walker (pre-fix, obmap::move re-added it). Waking afterwards
// registers it at wherever it ended up.
TEST(NpcScenarioFlags, repositioning_a_dormant_walker_keeps_it_unregistered)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* lurker = w.add_ob(Order::Living, FAMILY_SKELETON);
    ASSERT_NE(nullptr, lurker);
    lurker->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    lurker->set_team_num(1);
    lurker->set_spawn_delay(100);
    lurker->set_dormant(true);

    ASSERT_NE(nullptr, w.myobmap.get());
    auto registered = [&]() {
        return w.myobmap->walker_to_pos.find(lurker) !=
               w.myobmap->walker_to_pos.end();
    };
    ASSERT_FALSE(registered());

    lurker->setxy(11 * GRID_SIZE, 9 * GRID_SIZE);
    EXPECT_FALSE(registered()) << "setxy must not re-register a dormant walker";

    lurker->setworldxy(12.0f * GRID_SIZE, 10.0f * GRID_SIZE);
    EXPECT_FALSE(registered())
        << "setworldxy must not re-register a dormant walker";

    lurker->change_floor(1);
    EXPECT_FALSE(registered())
        << "change_floor must not re-register a dormant walker";

    // Waking is the single re-entry point — at the final spot and floor.
    lurker->set_dormant(false);
    EXPECT_TRUE(registered());
    EXPECT_EQ(1, static_cast<int>(lurker->floor()));
}

// ---------------------------------------------------------------------------
// SCEN_TYPE_SAVE_ALL scoping (Wave F2).
// ---------------------------------------------------------------------------

namespace {

// Count SAVE_ALL mission-loss events accumulated so far.
int save_all_losses(const TestGameWorld& tw)
{
    int n = 0;
    for (const auto& ev : tw.events.events())
    {
        if (ev.kind == og::sim::EventKind::EndGame &&
            ev.a == static_cast<std::uint32_t>(SCEN_TYPE_SAVE_ALL))
            ++n;
    }
    return n;
}

// Kill `ob` the way the sim does: mark dead, then run the death hook.
void kill(walker* ob)
{
    ob->set_dead(1);
    ob->death();
}

} // namespace

TEST(SaveAllScoping, round_trip_v10_protected_flag)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* bearer = w.add_ob(Order::Living, FAMILY_THIEF);
    walker* ally = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(bearer, nullptr);
    ASSERT_NE(ally, nullptr);
    bearer->setxy(64, 64);
    bearer->stats()->name = "Bearer";
    bearer->set_save_all_protected(true);
    ally->setxy(128, 128);
    ally->stats()->name = "Ranger-King";

    EXPECT_FALSE(ally->save_all_protected()) << "default must stay clear";

    // The protected bit alone forces the v10 reserved-block layout.
    const std::string file = "npcflags_protected.fss";
    ASSERT_EQ(10, save_and_read_version(w, file));

    TestGameWorld tw2(2);
    GameWorld& w2 = tw2.world();
    auto infile = og::io::og_open_read(file.c_str());
    ASSERT_TRUE(infile);
    char skip[4] = {};
    ASSERT_EQ(4u, infile->read(skip, 1, 4)); // FSS header + version byte
    og::data::LevelFileMetadata metadata;
    ASSERT_EQ(1, og::data::load_scenario_version(*infile, &w2, &metadata, 10));

    const walker* loaded_bearer = nullptr;
    const walker* loaded_ally = nullptr;
    for (const auto& uptr : w2.oblist)
    {
        const walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->family() == FAMILY_THIEF)
            loaded_bearer = ob;
        else if (ob->family() == FAMILY_SOLDIER)
            loaded_ally = ob;
    }
    ASSERT_NE(loaded_bearer, nullptr);
    ASSERT_NE(loaded_ally, nullptr);
    EXPECT_TRUE(loaded_bearer->save_all_protected())
        << "npc_flags bit 2 must survive the round trip";
    EXPECT_FALSE(loaded_ally->save_all_protected());
    EXPECT_FALSE(loaded_bearer->specials_disabled())
        << "bit 2 must not bleed into bit 0";
    EXPECT_TRUE(w2.has_save_all_protected());
}

TEST(SaveAllScoping, legacy_named_team0_death_still_fails_mission)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.type = SCEN_TYPE_SAVE_ALL;

    walker* hero = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(hero, nullptr);
    hero->setxy(64, 64);
    hero->set_team_num(0);
    hero->set_real_team_num(255);
    hero->stats()->name = "Ranger-King";

    ASSERT_FALSE(w.has_save_all_protected())
        << "no flagged NPC: the legacy rule must apply";
    kill(hero);
    EXPECT_EQ(1, save_all_losses(tw))
        << "legacy SAVE_ALL: a named team-0 death fails the mission";
}

TEST(SaveAllScoping, summoned_walker_death_never_fails_mission)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.type = SCEN_TYPE_SAVE_ALL;

    // The archmage names its illusions "Phantom"; expiring on team 0 they
    // used to read as a mission-critical named-ally death.
    walker* phantom = w.add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(phantom, nullptr);
    phantom->setxy(64, 64);
    phantom->set_team_num(0);
    phantom->set_real_team_num(255);
    phantom->stats()->name = "Phantom";
    phantom->set_summoned(true);

    kill(phantom);
    EXPECT_EQ(0, save_all_losses(tw))
        << "summoned walkers are ammunition, never a SAVE_ALL loss";
}

TEST(SaveAllScoping, do_summon_marks_the_conjured_walker)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.type = SCEN_TYPE_SAVE_ALL;

    living* cleric =
        static_cast<living*>(w.add_ob(Order::Living, FAMILY_CLERIC));
    ASSERT_NE(cleric, nullptr);
    cleric->setxy(64, 64);
    cleric->set_team_num(0);

    walker* raised = cleric->do_summon(FAMILY_SKELETON, 100);
    ASSERT_NE(raised, nullptr);
    EXPECT_TRUE(raised->summoned())
        << "do_summon must mark its output as conjured ammunition";
    EXPECT_EQ(cleric, raised->owner());

    // Even named and on team 0, its death is not a mission loss.
    raised->setxy(128, 128);
    raised->set_team_num(0);
    raised->stats()->name = "Bonewalker";
    kill(raised);
    EXPECT_EQ(0, save_all_losses(tw));
}

TEST(SaveAllScoping, charmed_foe_death_never_fails_mission)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.type = SCEN_TYPE_SAVE_ALL;

    // A charmed foe wears team 0 while real_team_num holds its true colors.
    walker* charmed = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(charmed, nullptr);
    charmed->setxy(64, 64);
    charmed->stats()->name = "Grushnak";
    charmed->set_real_team_num(1);
    charmed->set_team_num(0);

    kill(charmed);
    EXPECT_EQ(0, save_all_losses(tw))
        << "a charmed foe temporarily wearing our colors is not a character";
}

TEST(SaveAllScoping, protected_flag_scopes_the_watch_to_flagged_walkers)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.type = SCEN_TYPE_SAVE_ALL;

    walker* bearer = w.add_ob(Order::Living, FAMILY_THIEF);
    walker* ally = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(bearer, nullptr);
    ASSERT_NE(ally, nullptr);

    bearer->setxy(64, 64);
    bearer->set_team_num(0);
    bearer->set_real_team_num(255);
    bearer->stats()->name = "Bearer";
    bearer->set_save_all_protected(true);

    ally->setxy(128, 128);
    ally->set_team_num(0);
    ally->set_real_team_num(255);
    ally->stats()->name = "Ranger-King";

    ASSERT_TRUE(w.has_save_all_protected());

    // The named-but-unflagged ally dies: the mission survives.
    kill(ally);
    EXPECT_EQ(0, save_all_losses(tw))
        << "with a protected cast present, unflagged named allies are "
           "non-critical";

    // The flagged Bearer dies: the mission fails — and the corpse in
    // dead_list keeps scoped mode stable across the fatal tick.
    w.tick(); // sweeps the dead ally into dead_list
    ASSERT_TRUE(w.has_save_all_protected());
    kill(bearer);
    EXPECT_EQ(1, save_all_losses(tw))
        << "the flagged walker is exactly the mission-critical one";
}
