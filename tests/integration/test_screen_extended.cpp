#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/render/view.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

#include "test_network_fixture.h"

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_walker_at(char family, short x, short y, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(2, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(x, y);
    return w;
}

namespace {

screen& test_screen()
{
    return *og::runtime::current_session->myscreen_;
}

GameWorld& test_world()
{
    return og::runtime::current_session->myscreen_->world();
}

// Paint one base-grid cell, addressed in PIXEL coords like the sim's own
// passability/damage entry points.
void set_grid_tile(short px, short py, unsigned char tile)
{
    GameWorld& w = test_world();
    ASSERT_TRUE(w.grid.valid()) << "grid must be allocated before painting a tile";
    const int gx = px / GRID_SIZE;
    const int gy = py / GRID_SIZE;
    ASSERT_TRUE(gx >= 0 && gy >= 0 && gx < w.grid.w && gy < w.grid.h)
        << "tile coordinate must be inside the grid";
    w.grid.data[static_cast<std::size_t>(gx + w.grid.w * gy)] = tile;
}

unsigned char grid_tile(short px, short py)
{
    GameWorld& w = test_world();
    const int gx = px / GRID_SIZE;
    const int gy = py / GRID_SIZE;
    return w.grid.data[static_cast<std::size_t>(gx + w.grid.w * gy)];
}

// A decor plane matching the grid's dims (the only shape the sim consults).
void allocate_decor_plane()
{
    GameWorld& w = test_world();
    ASSERT_TRUE(w.grid.valid()) << "grid must exist before a decor plane";
    w.decor.free();
    w.decor.frames = 1;
    w.decor.w = w.grid.w;
    w.decor.h = w.grid.h;
    const std::size_t size =
        static_cast<std::size_t>(w.decor.w) * static_cast<std::size_t>(w.decor.h);
    w.decor.data = std::make_unique<unsigned char[]>(size);
    for (std::size_t i = 0; i < size; ++i)
        w.decor.data[i] = DECOR_NONE;
}

void set_decor_tile(short px, short py, unsigned char decor_id)
{
    GameWorld& w = test_world();
    ASSERT_TRUE(w.decor.valid()) << "decor plane must be allocated first";
    const int gx = px / GRID_SIZE;
    const int gy = py / GRID_SIZE;
    w.decor.data[static_cast<std::size_t>(gx + w.decor.w * gy)] = decor_id;
}

} // namespace

TEST(ScreenExtended, network_fixture_advances_empty_tick)
{
    og::sim::test::NetworkTestConfig config;
    config.level_id = 1;
    config.tick_count = 1;
    og::sim::test::NetworkTestFixture fixture(config);
    fixture.run();

    EXPECT_EQ(1u, fixture.server_world().tick_count_);
    EXPECT_EQ(1u, fixture.client_world(0).tick_count_);
    fixture.expect_clients_match_server();
}


// ---------------------------------------------------------------------------
// add_ob / remove_ob
// ---------------------------------------------------------------------------

// add_ob(order, family, atstart) must build a walker of EXACTLY that
// order/family and file it into the list the order selects
// (GameWorld::add_ob / add_to_list).
TEST(ScreenExtended, screen_add_ob_living)
{
    GameWorld& world = test_world();
    const std::size_t ob_before = world.oblist.size();
    const std::size_t weap_before = world.weaplist.size();
    const int living_before = world.living_count;

    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "add_ob(Living, SOLDIER) must construct a walker";
    EXPECT_EQ(static_cast<int>(Order::Living), static_cast<int>(w->query_order()))
        << "add_ob must build the order it was asked for";
    EXPECT_EQ(FAMILY_SOLDIER, static_cast<int>(w->family()))
        << "add_ob must build the family it was asked for";
    EXPECT_EQ(ob_before + 1u, world.oblist.size())
        << "a non-Weapon order is filed into oblist";
    EXPECT_EQ(w, world.oblist.back().get())
        << "atstart=false appends at the BACK of oblist";
    EXPECT_EQ(weap_before, world.weaplist.size())
        << "a Living must never land in weaplist";
    EXPECT_EQ(living_before + 1, world.living_count)
        << "add_ob(Living, ...) increments living_count";

    ASSERT_EQ(1, world.remove_ob(w)) << "cleanup: the walker was in oblist";
    EXPECT_EQ(living_before, world.living_count)
        << "remove_ob(Living) hands living_count back";
}


TEST(ScreenExtended, screen_add_ob_weapon)
{
    GameWorld& world = test_world();
    const std::size_t ob_before = world.oblist.size();
    const std::size_t weap_before = world.weaplist.size();

    walker* w = world.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, w) << "add_ob(Weapon, KNIFE) must construct a walker";
    EXPECT_EQ(static_cast<int>(Order::Weapon), static_cast<int>(w->query_order()))
        << "add_ob must build the order it was asked for";
    EXPECT_EQ(FAMILY_KNIFE, static_cast<int>(w->family()))
        << "add_ob must build the family it was asked for";
    EXPECT_EQ(weap_before + 1u, world.weaplist.size())
        << "Order::Weapon is the one order add_ob routes to weaplist";
    EXPECT_EQ(w, world.weaplist.back().get())
        << "the weapon is appended to weaplist";
    EXPECT_EQ(ob_before, world.oblist.size())
        << "a weapon must never be filed into oblist";

    ASSERT_EQ(1, world.remove_ob(w)) << "cleanup: the weapon was in weaplist";
}


// The third argument is `bool atstart`, and the front-insertion it asks for
// has to be observable (add_to_list's push_front arm).
TEST(ScreenExtended, screen_add_ob_treasure)
{
    GameWorld& world = test_world();
    world.delete_objects();

    walker* sibling = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, sibling)
        << "a sibling walker makes front vs back distinguishable";

    walker* stain = world.add_ob(Order::Treasure, FAMILY_STAIN, /*atstart=*/true);
    ASSERT_NE(nullptr, stain) << "add_ob(Treasure, STAIN) must construct a walker";
    EXPECT_EQ(static_cast<int>(Order::Treasure), static_cast<int>(stain->query_order()))
        << "add_ob must build the order it was asked for";
    EXPECT_EQ(FAMILY_STAIN, static_cast<int>(stain->family()))
        << "add_ob must build the family it was asked for";
    ASSERT_EQ(std::size_t{2}, world.oblist.size()) << "a Treasure goes into oblist";
    EXPECT_EQ(stain, world.oblist.front().get())
        << "atstart=true pushes the new walker to the FRONT of oblist";
    EXPECT_EQ(sibling, world.oblist.back().get())
        << "the incumbent keeps its place at the back";

    walker* tail = world.add_ob(Order::Treasure, FAMILY_STAIN, /*atstart=*/false);
    ASSERT_NE(nullptr, tail);
    ASSERT_EQ(std::size_t{3}, world.oblist.size());
    EXPECT_EQ(tail, world.oblist.back().get())
        << "atstart=false appends, so the flag is really read";
    EXPECT_EQ(stain, world.oblist.front().get())
        << "the front-inserted treasure is still at the front";

    world.delete_objects();
}


// add_ob routes EVERY non-Weapon order (FX included) to oblist; fxlist has
// exactly one door, add_fx_ob.
TEST(ScreenExtended, screen_add_ob_effect)
{
    GameWorld& world = test_world();
    world.delete_objects();

    walker* w = world.add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, w) << "add_ob(FX, EXPLOSION) must construct a walker";
    EXPECT_EQ(static_cast<int>(Order::FX), static_cast<int>(w->query_order()))
        << "add_ob must build the order it was asked for";
    EXPECT_EQ(FAMILY_EXPLOSION, static_cast<int>(w->family()))
        << "add_ob must build the family it was asked for";
    EXPECT_EQ(std::size_t{1}, world.oblist.size())
        << "add_ob files an FX into oblist, not fxlist";
    EXPECT_EQ(w, world.oblist.back().get());
    EXPECT_EQ(std::size_t{0}, world.fxlist.size())
        << "add_ob must never feed fxlist";

    walker* fx = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, fx) << "add_fx_ob must construct a walker";
    EXPECT_EQ(std::size_t{1}, world.fxlist.size())
        << "add_fx_ob is the fxlist door";
    EXPECT_EQ(fx, world.fxlist.back().get());
    EXPECT_EQ(std::size_t{1}, world.oblist.size())
        << "add_fx_ob must not also append to oblist";

    world.delete_objects();
}


// ---------------------------------------------------------------------------
// query_grid_passable - extended tests for all terrain types
// ---------------------------------------------------------------------------

// query_grid_passable scans the footprint's cells: walkable ground is true,
// a wall byte under the footprint is false, and out-of-range / null is false.
TEST(ScreenExtended, screen_query_grid_passable_walking)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_NE(nullptr, l) << "the session must own a loader";
    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "the loader must create the probe soldier";
    w->setxy(100, 100);

    GameWorld& world = test_world();
    world.create_new_grid(); // 40x60, all grass

    ASSERT_TRUE(world.query_grid_passable(100, 100, w.get()))
        << "grass under the footprint is walkable";
    ASSERT_TRUE(world.query_grid_passable(50, 50, w.get()))
        << "grass under the footprint is walkable";
    ASSERT_TRUE(world.query_grid_passable(200, 150, w.get()))
        << "grass under the footprint is walkable";

    set_grid_tile(100, 100, PIX_H_WALL1);
    ASSERT_FALSE(world.query_grid_passable(100, 100, w.get()))
        << "a wall byte under the footprint makes the spot impassable";
    ASSERT_TRUE(world.query_grid_passable(200, 150, w.get()))
        << "the wall must only block the cells it covers";

    ASSERT_FALSE(world.query_grid_passable(-1, 50, w.get()))
        << "a negative coordinate is out of range";
    ASSERT_FALSE(world.query_grid_passable(100, 100, nullptr))
        << "a null walker is never passable";
}


// The weapon-specific arm: PIX_TREE_B1 lets Order::Weapon through while it
// blocks a ground living, and walls stop both.
TEST(ScreenExtended, screen_query_grid_passable_weapon)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_NE(nullptr, l) << "the session must own a loader";
    auto knife = l->create_walker_owned(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife) << "the loader must create the probe knife";
    auto soldier = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier) << "the loader must create the contrast soldier";
    knife->setxy(100, 100);
    soldier->setxy(100, 100);

    GameWorld& world = test_world();
    world.create_new_grid();

    ASSERT_TRUE(world.query_grid_passable(100, 100, knife.get()))
        << "grass passes a weapon";

    set_grid_tile(100, 100, PIX_TREE_B1);
    ASSERT_TRUE(world.query_grid_passable(100, 100, knife.get()))
        << "PIX_TREE_B1 lets Order::Weapon through";
    ASSERT_FALSE(world.query_grid_passable(100, 100, soldier.get()))
        << "the same tree base blocks a ground living -- the rule is order-specific";

    set_grid_tile(100, 100, PIX_H_WALL1);
    ASSERT_FALSE(world.query_grid_passable(100, 100, knife.get()))
        << "a wall stops a weapon too";
}


// ---------------------------------------------------------------------------
// query_passable
// ---------------------------------------------------------------------------

// query_passable = query_grid_passable AND query_object_passable: a body
// standing on the target spot makes it impassable even over clear ground.
TEST(ScreenExtended, screen_query_passable_living)
{
    GameWorld& world = test_world();
    world.delete_objects();
    world.create_new_grid();

    auto seeker = make_walker_at(FAMILY_SOLDIER, 50, 50, 0);
    ASSERT_NE(nullptr, seeker) << "the probe walker must be created";

    ASSERT_TRUE(world.query_passable(100, 100, seeker.get()))
        << "clear grass with nobody on it is passable";

    walker* blocker = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, blocker) << "the blocking body must be created";
    blocker->set_team_num(0); // same team: collide() must not start a fight
    blocker->setxy(100, 100); // registers it in the obmap

    ASSERT_FALSE(world.query_passable(100, 100, seeker.get()))
        << "a living body on the spot makes query_passable refuse";
    ASSERT_TRUE(world.query_grid_passable(100, 100, seeker.get()))
        << "the grid half is still clear -- only the object half refused";
    ASSERT_TRUE(world.query_passable(50, 50, seeker.get()))
        << "a spot the blocker does not occupy stays passable";

    world.delete_objects();
}


// ---------------------------------------------------------------------------
// first_of extended (various order types)
// ---------------------------------------------------------------------------

// screen::first_of scans world_.oblist ONLY, for the first non-dead walker
// matching order+family. add_ob(Weapon, ...) files into weaplist, which
// first_of never sees -- so the knife has to be placed in oblist by hand.
TEST(ScreenExtended, screen_first_of_weapon)
{
    GameWorld& world = test_world();
    world.delete_objects();
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Weapon, FAMILY_KNIFE))
        << "an empty oblist yields no first_of";

    loader* l = test_screen().myloader;
    ASSERT_NE(nullptr, l) << "the session must own a loader";
    auto knife = l->create_walker_owned(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife) << "the loader must create the knife";
    walker* knife_ptr = knife.get();
    world.oblist.push_back(std::move(knife));

    ASSERT_EQ(knife_ptr, test_screen().first_of(Order::Weapon, FAMILY_KNIFE))
        << "first_of returns the oblist walker matching order+family";
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Weapon, FAMILY_ARROW))
        << "a different weapon family must not match";
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Treasure, FAMILY_KNIFE))
        << "a different order must not match";

    walker* in_weaplist = world.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, in_weaplist) << "the weaplist arrow must be created";
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Weapon, FAMILY_ARROW))
        << "first_of never sees weaplist, only oblist";

    knife_ptr->set_dead(1);
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Weapon, FAMILY_KNIFE))
        << "first_of skips dead walkers";

    world.delete_objects();
}


TEST(ScreenExtended, screen_first_of_treasure)
{
    GameWorld& world = test_world();
    world.delete_objects();
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Treasure, FAMILY_STAIN))
        << "an empty oblist yields no first_of";

    walker* stain = world.add_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain) << "the stain must be created";
    ASSERT_EQ(stain, test_screen().first_of(Order::Treasure, FAMILY_STAIN))
        << "add_ob files a Treasure into oblist, where first_of looks";
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Treasure, FAMILY_GOLD_BAR))
        << "a different treasure family must not match";
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Living, FAMILY_STAIN))
        << "a different order must not match";

    stain->set_dead(1);
    ASSERT_EQ(nullptr, test_screen().first_of(Order::Treasure, FAMILY_STAIN))
        << "first_of skips dead walkers";

    world.delete_objects();
}


// ---------------------------------------------------------------------------
// save_data access
// ---------------------------------------------------------------------------

// (ScreenExtended.screen_save_data_score was deleted: it added 100 to the
// POD SaveData::m_score[] array and asserted the array grew, running no
// product code at all. The real rule -- the mission score folds into
// m_totalscore exactly once per win, then resets -- is pinned below.)

TEST(ScreenExtended, screen_endgame_clears_mission_score_after_payout)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    og::runtime::current_session->myscreen_->save_data.m_totalscore[0] = 1000;
    og::runtime::current_session->myscreen_->save_data.m_score[0] = 250;
    og::runtime::current_session->myscreen_->sync_world_from_save_data();
    og::runtime::current_session->myscreen_->world().end = 0;
    (void)og::runtime::current_session->myscreen_->endgame(0, -1);

    ASSERT_EQ(1250, static_cast<int>(og::runtime::current_session->myscreen_->save_data.m_totalscore[0])) << "win should add current mission score exactly once";
    ASSERT_EQ(0, static_cast<int>(og::runtime::current_session->myscreen_->save_data.m_score[0])) << "mission score should reset after payout";

    og::runtime::current_session->myscreen_->world().end = 0;
    (void)og::runtime::current_session->myscreen_->endgame(0, -1);

    ASSERT_EQ(1250, static_cast<int>(og::runtime::current_session->myscreen_->save_data.m_totalscore[0])) << "subsequent wins must not re-credit previous mission score";

    og::runtime::current_session->myscreen_->world().end = saved_end;
}


// ---------------------------------------------------------------------------
// Campaign scripting (issue #206) authoritative sync, screen twin
// ---------------------------------------------------------------------------

namespace {

// Registers a throwaway scripted campaign picker for one test and restores
// the pack-script registry (and the gameplay context campaign dispatch
// resolves) afterwards — the test_campaign_picker_session fixture approach.
// The chunk name deliberately does NOT start with `packs/`: that prefix
// declares the bytes to the pack-Lua coverage inventory, and this throwaway
// chunk exists nowhere in the repository.
class ScopedSyntheticCampaignPicker
{
public:
    explicit ScopedSyntheticCampaignPicker(const std::string& source)
        : previous_game_(current_game)
        , scripts_(og::script::pack_scripts())
    {
        current_game = nullptr;  // dispatch resolves the shared UI VM
        og::script::register_pack_script(
            {"test.screensync", "screensync/scripts/c.lua", source});
    }

    ~ScopedSyntheticCampaignPicker()
    {
        og::script::clear_pack_scripts();
        for (const og::script::PackScript& script : scripts_)
            og::script::register_pack_script(script);
        current_game = previous_game_;
    }

private:
    GameplayContext* previous_game_;
    std::vector<og::script::PackScript> scripts_;
};

} // namespace

// screen::sync_world_from_save_data must REPLACE world.campaign_vars with
// the current campaign's decision book filtered to
// og.register_campaign_hooks' vars list — never merge, never carry an
// unregistered or stale name (docs/campaign-scripting-design.md,
// "campaign_vars replace-not-merge sync"). The server twin is pinned by
// HeadlessServerRuntimeTest.
// authoritative_sync_replaces_campaign_vars_with_registered_names_only.
TEST(ScreenExtended, sync_world_from_save_data_replaces_campaign_vars)
{
    auto* scr = og::runtime::current_session->myscreen_;
    ScopedSyntheticCampaignPicker picker(R"LUA(og.register_campaign_hooks({
  vars = { "watch_paid", "delve_counted" },
  picker_menu = function(page_id)
    return { title = "BOOK" }
  end,
}))LUA");

    scr->save_data.reset();
    scr->save_data.current_campaign = "gladiator";
    scr->save_data.scen_num = 1;
    // The decision book: two registered names, one unregistered name, and
    // one entry filed under a DIFFERENT campaign.
    ASSERT_TRUE(scr->save_data.campaign_state_set("gladiator", "watch_paid", 3));
    ASSERT_TRUE(
        scr->save_data.campaign_state_set("gladiator", "delve_counted", -2));
    ASSERT_TRUE(
        scr->save_data.campaign_state_set("gladiator", "not_registered", 9));
    ASSERT_TRUE(scr->save_data.campaign_state_set("othercamp", "watch_paid", 7));

    // Pre-pollute the world: a stale foreign var AND a stale value for a
    // registered name — both must be wiped by the clear-then-copy.
    scr->world().campaign_vars.clear();
    scr->world().campaign_vars.emplace_back("stale_var", 5);
    scr->world().campaign_vars.emplace_back("watch_paid", 99);

    scr->sync_world_from_save_data();

    // Exactly the registered current-campaign entries, in the book's sorted
    // order: the stale entries are gone (clear), the unregistered and
    // foreign-campaign names never crossed (filter), and the registered
    // value is the save's, not the polluted one (replace, not merge).
    const auto& vars = scr->world().campaign_vars;
    ASSERT_EQ(2u, vars.size());
    EXPECT_EQ("delve_counted", vars[0].first);
    EXPECT_EQ(-2, vars[0].second);
    EXPECT_EQ("watch_paid", vars[1].first);
    EXPECT_EQ(3, vars[1].second);

    // Leave no campaign state behind for shuffled siblings.
    scr->world().campaign_vars.clear();
    scr->save_data.reset();
    scr->save_data.current_campaign = "gladiator";
}


// Match clock clamp at world entry (#241), screen twin: a SaveData whose
// time_limit was never sanitized — a hand-edited company file is the one
// route that skips both sanitize_settings and clamp_match_setting — must
// still reach the sim inside [720, 21600], because the mirror that
// snapshot-applies this world clamps the same field (world_snapshot.cpp
// apply_mode_state). The server twin is pinned by
// HeadlessServerRuntimeTest.
// authoritative_sync_clamps_an_out_of_range_save_time_limit.
TEST(ScreenExtended, sync_world_from_save_data_clamps_time_limit)
{
    auto* scr = og::runtime::current_session->myscreen_;
    const short saved_limit = scr->save_data.time_limit;

    scr->save_data.time_limit = 100; // under the floor
    scr->sync_world_from_save_data();
    EXPECT_EQ(720, scr->world().ctf_requested_time_limit);

    scr->save_data.time_limit = 30000; // over the ceiling
    scr->sync_world_from_save_data();
    EXPECT_EQ(21600, scr->world().ctf_requested_time_limit);

    scr->save_data.time_limit = 0; // the map's own value
    scr->sync_world_from_save_data();
    EXPECT_EQ(0, scr->world().ctf_requested_time_limit)
        << "0 is the sentinel, never clamped to the floor";

    scr->save_data.time_limit = 7200; // in range, untouched
    scr->sync_world_from_save_data();
    EXPECT_EQ(7200, scr->world().ctf_requested_time_limit);

    // Leave no clock behind for shuffled siblings.
    scr->save_data.time_limit = saved_limit;
    scr->sync_world_from_save_data();
}

// The retired TEAMS knob (amendment A3): a .gtl written before the
// amendment still carries 2/3/4 in its ctf_team_count field — the save
// layout did not move — but the world-entry twin refuses to field a
// different team set for it. Dropping a team is the LINEUP band's BOTS:
// OFF now, and there is exactly one rule for which teams play.
TEST(ScreenExtended, sync_world_from_save_data_retires_the_team_count)
{
    auto* scr = og::runtime::current_session->myscreen_;
    const short saved_count = scr->save_data.ctf_team_count;

    scr->save_data.ctf_team_count = 3;  // a legacy v18 value
    scr->sync_world_from_save_data();
    EXPECT_EQ(0, scr->world().ctf_requested_team_count)
        << "inert: every team the map authors, whatever the save says";

    scr->save_data.ctf_team_count = saved_count;
    scr->sync_world_from_save_data();
}


// ---------------------------------------------------------------------------
// §3.7 level-win backup producer (screen::endgame local-win autosave tail)
// ---------------------------------------------------------------------------

static std::string read_user_file_bytes(const std::string& relative)
{
    std::ifstream in(std::filesystem::path(get_user_path()) / relative,
                     std::ios::binary);
    std::stringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

TEST(ScreenExtended, level_win_snapshots_one_company_backup)
{
    auto* scr = og::runtime::current_session->myscreen_;
    const char saved_end = scr->world().end;

    scr->save_data.reset();
    scr->save_data.current_campaign = "gladiator";
    scr->save_data.scen_num = 1;
    scr->sync_world_from_save_data();
    scr->world().end = 0;

    const std::string slot = og::data::active_company_slot();
    ASSERT_EQ("save0", slot)
        << "WP2 invisibility: the producer targets the default slot";
    // Relative accounting keeps this deterministic under --gtest_shuffle and
    // repeated runs against the binary's persistent config dir (other tests
    // in this suite also win levels and snapshot backups).
    const std::vector<og::data::CompanyBackupInfo> before =
        og::data::list_company_backups(slot);
    const int max_before = before.empty() ? 0 : before.front().seq;

    (void)scr->endgame(0, -1); // local, non-networked win

    const std::vector<og::data::CompanyBackupInfo> after =
        og::data::list_company_backups(slot);
    ASSERT_FALSE(after.empty())
        << "a local level win must snapshot a company backup (§3.7)";
    EXPECT_EQ(max_before + 1, after.front().seq)
        << "exactly one new snapshot per win, seq = max(existing) + 1";
    EXPECT_LE(after.size(),
              static_cast<std::size_t>(og::data::kCompanyBackupRetention))
        << "retention must keep pruning the win-producer's snapshots";

    // The snapshot is a byte copy of the freshly autosaved company file
    // (backup runs AFTER the win autosave, so both must match).
    EXPECT_EQ(read_user_file_bytes("save/" + slot + ".gtl"),
              read_user_file_bytes("save/backups/" + after.front().filename))
        << "the level-win snapshot must byte-match the autosaved company";

    scr->world().end = saved_end;
}


// ---------------------------------------------------------------------------
// do_notify
// ---------------------------------------------------------------------------

// screen::do_notify routes the line to the view whose control == who, and
// broadcasts to EVERY view only when no view owns `who`.
TEST(ScreenExtended, screen_do_notify_with_walker)
{
    screen& s = test_screen();
    const short saved_views = s.numviews;
    s.reset(2); // two views, so routing and broadcasting differ
    ASSERT_EQ(2, static_cast<int>(s.numviews)) << "reset(2) must field two views";
    ASSERT_NE(nullptr, s.viewob[0]);
    ASSERT_NE(nullptr, s.viewob[1]);

    auto w = make_walker_at(FAMILY_SOLDIER, 100, 100, 0);
    ASSERT_NE(nullptr, w) << "the notifier walker must be created";

    s.viewob[0]->control = w.get();
    s.viewob[1]->control = nullptr;
    s.viewob[0]->clear_text();
    s.viewob[1]->clear_text();

    s.do_notify("Owned line", w.get());
    EXPECT_EQ("Owned line", s.viewob[0]->textlist[0])
        << "do_notify writes the line to the view whose control is `who`";
    EXPECT_EQ("", s.viewob[1]->textlist[0])
        << "a view that does not control `who` must not receive the line";

    s.viewob[0]->clear_text();
    s.viewob[1]->clear_text();
    s.do_notify("Broadcast line", nullptr);
    EXPECT_EQ("Broadcast line", s.viewob[0]->textlist[0])
        << "with no owning view the line broadcasts to every view";
    EXPECT_EQ("Broadcast line", s.viewob[1]->textlist[0])
        << "with no owning view the line broadcasts to every view";

    s.viewob[0]->control = nullptr;
    s.viewob[0]->clear_text();
    s.viewob[1]->clear_text();
    s.reset(saved_views);
}


// ---------------------------------------------------------------------------
// find functions with populated lists
// ---------------------------------------------------------------------------

// find_near_foe walks the obmap spiral around the seeker and returns the
// first hostile Living it meets; a friendly body at the same spot is not a
// foe (and the find_nearest_foe fallback finds none either).
TEST(ScreenExtended, screen_find_near_foe_with_enemies)
{
    GameWorld& world = test_world();
    world.delete_objects();
    // pixmaxx/pixmaxy bound the spiral: with a 0-sized level the very first
    // probe is out of range and find_near_foe degrades to find_nearest_foe.
    world.create_new_grid();

    auto seeker = make_walker_at(FAMILY_SOLDIER, 50, 50, 0);
    ASSERT_NE(nullptr, seeker) << "the seeker must be created";

    // add_ob so the orc is a tracked oblist entity, and setxy so it is
    // registered in the obmap pile the spiral actually walks.
    walker* enemy = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, enemy) << "the enemy must be created";
    enemy->set_team_num(1);
    enemy->setxy(70, 50);

    ASSERT_EQ(enemy, world.find_near_foe(seeker.get()))
        << "the team-1 orc 20px away is the seeker's near foe";

    enemy->set_team_num(0); // same body, same spot, friendly now
    ASSERT_EQ(nullptr, world.find_near_foe(seeker.get()))
        << "team equality is what makes a body a foe";

    world.delete_objects();
}


// find_nearest_foe scans oblist and returns the CLOSEST non-dead hostile Living
// (distance starts at 10000 and takes the minimum).
TEST(ScreenExtended, screen_find_nearest_foe_with_enemies)
{
    GameWorld& world = test_world();
    world.delete_objects();

    auto seeker = make_walker_at(FAMILY_SOLDIER, 50, 50, 0);
    ASSERT_NE(nullptr, seeker) << "the seeker must be created";
    ASSERT_EQ(nullptr, world.find_nearest_foe(seeker.get()))
        << "an empty oblist holds no foe";

    auto far_enemy = make_walker_at(FAMILY_ORC, 200, 150, 1);
    ASSERT_NE(nullptr, far_enemy) << "the far enemy must be created";
    walker* far_ptr = far_enemy.get();
    world.oblist.push_back(std::move(far_enemy));
    ASSERT_EQ(far_ptr, world.find_nearest_foe(seeker.get()))
        << "the only hostile living in oblist is the answer";

    // Pushed AFTER the far one, so list order cannot explain the result.
    auto near_enemy = make_walker_at(FAMILY_ORC, 70, 50, 1);
    ASSERT_NE(nullptr, near_enemy) << "the near enemy must be created";
    walker* near_ptr = near_enemy.get();
    world.oblist.push_back(std::move(near_enemy));
    ASSERT_EQ(near_ptr, world.find_nearest_foe(seeker.get()))
        << "find_nearest_foe keeps the MINIMUM distance_to_ob";

    near_ptr->set_dead(1);
    ASSERT_EQ(far_ptr, world.find_nearest_foe(seeker.get()))
        << "a dead hostile is skipped";

    world.delete_objects();
}


// ---------------------------------------------------------------------------
// damage_tile extended
// ---------------------------------------------------------------------------

// damage_tile chars PIX_GRASS1..4 to PIX_GRASS1_DAMAGED and returns the
// resulting byte; a non-grass tile comes back unchanged; a decorated cell or
// an out-of-range coordinate returns 0 and writes nothing.
TEST(ScreenExtended, screen_damage_tile_various)
{
    GameWorld& world = test_world();
    world.create_new_grid();

    static constexpr short kSpots[3][2] = {{50, 50}, {100, 100}, {200, 150}};
    for (const auto& spot : kSpots)
    {
        set_grid_tile(spot[0], spot[1], PIX_GRASS4);
        ASSERT_EQ(static_cast<int>(PIX_GRASS1_DAMAGED),
                  static_cast<int>(static_cast<unsigned char>(
                      test_screen().damage_tile(spot[0], spot[1]))))
            << "grass must char to PIX_GRASS1_DAMAGED and be returned";
        ASSERT_EQ(static_cast<int>(PIX_GRASS1_DAMAGED),
                  static_cast<int>(grid_tile(spot[0], spot[1])))
            << "the grid byte itself must be rewritten";
    }

    set_grid_tile(100, 100, PIX_WALL2);
    ASSERT_EQ(static_cast<int>(PIX_WALL2),
              static_cast<int>(static_cast<unsigned char>(
                  test_screen().damage_tile(100, 100))))
        << "a non-grass tile is returned unchanged";
    ASSERT_EQ(static_cast<int>(PIX_WALL2), static_cast<int>(grid_tile(100, 100)))
        << "a non-grass tile is not rewritten";

    // The range guard is in CELL units (xloc / GRID_SIZE), so -1 still maps
    // to cell 0: a genuinely out-of-range probe needs a whole cell of slack.
    ASSERT_EQ(0, static_cast<int>(test_screen().damage_tile(-GRID_SIZE, 50)))
        << "a negative cell coordinate returns 0";
    ASSERT_EQ(0, static_cast<int>(test_screen().damage_tile(
                     static_cast<short>(world.grid.w * GRID_SIZE), 50)))
        << "a cell coordinate past the grid width returns 0";

    // Decor shields the ground (legacy combined tiles never charred).
    set_grid_tile(200, 150, PIX_GRASS4);
    allocate_decor_plane();
    set_decor_tile(200, 150, DECOR_PEBBLES);
    ASSERT_EQ(0, static_cast<int>(test_screen().damage_tile(200, 150)))
        << "a decorated cell is exempt from the transform and reports 0";
    ASSERT_EQ(static_cast<int>(PIX_GRASS4), static_cast<int>(grid_tile(200, 150)))
        << "the shielded grass byte must be intact";
    set_grid_tile(50, 50, PIX_GRASS4);
    ASSERT_EQ(static_cast<int>(PIX_GRASS1_DAMAGED),
              static_cast<int>(static_cast<unsigned char>(
                  test_screen().damage_tile(50, 50))))
        << "an undecorated cell on the same decor plane still chars";

    world.decor.free(); // leave the level as the siblings expect it
}


namespace
{
struct PaneRect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const PaneRect&) const = default;
};

std::ostream& operator<<(std::ostream& os, const PaneRect& r)
{
    return os << '(' << r.x << ',' << r.y << ' ' << r.w << 'x' << r.h << ')';
}

PaneRect pane_rect(const viewscreen& v)
{
    return {static_cast<int>(v.xloc), static_cast<int>(v.yloc),
            static_cast<int>(v.xview), static_cast<int>(v.yview)};
}
} // namespace

// ready_for_battle(n) / reset(n) publish n viewscreens: numviews == n, slots
// 0..n-1 hold a view whose mynum is its own index, every higher slot is empty
// (screen::cleanup resets all MAX_VIEWS before the rebuild), and each pane's
// FULL-mode rect is the og::view_layout partition of the 320x200 world canvas
// -- two side-by-side halves at 2, a full-height left half plus a stacked
// right half at 3, quadrants at 4. A rebuild that leaks the previous view
// set, numbers the panes wrong, or hands every seat the whole canvas fails
// here instead of shipping overlapping split screens.
TEST(ScreenExtended, screen_multiview_lifecycle_pins_numviews_and_every_pane_rect)
{
    screen& s = test_screen();
    ASSERT_EQ(320, s.world_canvas_w()) << "the pinned rects below are the 320-wide canvas";
    ASSERT_EQ(200, s.world_canvas_h()) << "the pinned rects below are the 200-tall canvas";

    struct Case
    {
        const char* label;
        short count;
        bool via_reset;
        PaneRect rects[4];
    };
    // half_w = 320/2-1 = 159, right_x = 320/2+1 = 161,
    // half_h = 200/2-1 =  99, bottom_y = 200/2+1 = 101.
    static const Case kCases[] = {
        {"ready_for_battle(2)", 2, false,
         {{0, 0, 159, 200}, {161, 0, 159, 200}, {}, {}}},
        {"ready_for_battle(3)", 3, false,
         {{0, 0, 159, 200}, {161, 0, 159, 99}, {161, 101, 159, 99}, {}}},
        {"reset(4)", 4, true,
         {{0, 0, 159, 99}, {161, 0, 159, 99}, {0, 101, 159, 99}, {161, 101, 159, 99}}},
    };

    for (const Case& c : kCases)
    {
        if (c.via_reset)
            s.reset(c.count);
        else
            s.ready_for_battle(c.count);

        ASSERT_EQ(static_cast<int>(c.count), static_cast<int>(s.numviews))
            << c.label << ": numviews is the requested seat count";
        for (int i = 0; i < MAX_VIEWS; i++)
        {
            if (i < c.count)
            {
                ASSERT_NE(nullptr, s.viewob[i])
                    << c.label << ": seat " << i << " has a view";
                EXPECT_EQ(i, static_cast<int>(s.viewob[i]->mynum))
                    << c.label << ": view " << i << " carries its own seat number";
            }
            else
            {
                EXPECT_EQ(nullptr, s.viewob[i])
                    << c.label << ": slot " << i
                    << " must be cleared, not left over from the previous set";
            }
        }
        for (int i = 0; i < c.count; i++)
        {
            // The constructor rect is immediately re-derived from each
            // player's saved PREF_VIEW; ask for FULL explicitly so the pinned
            // numbers are the layout partition and not a saved HUD inset.
            s.viewob[i]->resize(PREF_VIEW_FULL);
            EXPECT_EQ(c.rects[i], pane_rect(*s.viewob[i]))
                << c.label << ": seat " << i << "'s FULL pane rect";
            EXPECT_EQ(c.rects[i].x + c.rects[i].w, static_cast<int>(s.viewob[i]->endx))
                << c.label << ": seat " << i << "'s endx is xloc + xview";
            EXPECT_EQ(c.rects[i].y + c.rects[i].h, static_cast<int>(s.viewob[i]->endy))
                << c.label << ": seat " << i << "'s endy is yloc + yview";
        }
    }

    s.reset(1);
    EXPECT_EQ(1, static_cast<int>(s.numviews)) << "and back to a single seat";
    EXPECT_EQ(nullptr, s.viewob[1]) << "the second pane is gone again";
}


// find_nearest_player returns the CLOSEST user-controlled walker, and
// draw_panels(n) repaints the frame: it clears the buffer and runs the full
// redraw(), whose draw_panel_chrome leg draws each non-FULL view's border.
// That border is the observable -- a draw_panels that stops redrawing (or a
// chrome leg that ignores the view pref) leaves the trace empty.
TEST(ScreenExtended, screen_find_nearest_player_and_draw_panels_repaint_the_view_chrome)
{
    auto seeker = make_walker_at(FAMILY_SOLDIER, 20, 20, 1);
    auto p1 = make_walker_at(FAMILY_ARCHER, 24, 20, 0);
    auto p2 = make_walker_at(FAMILY_MAGE, 200, 160, 0);
    ASSERT_NE(nullptr, seeker) << "the seeker walker must be created";
    ASSERT_NE(nullptr, p1) << "the near player walker must be created";
    ASSERT_NE(nullptr, p2) << "the far player walker must be created";

    walker* p1p = p1.get();
    walker* p2p = p2.get();

    p1p->set_user(0);
    p2->set_user(1);
    screen& s = test_screen();
    s.world().oblist.push_back(std::move(p1));
    s.world().oblist.push_back(std::move(p2));

    EXPECT_EQ(p1p, s.world().find_nearest_player(seeker.get()))
        << "nearest player should be the closest user-controlled walker";
    // Positive control for that oracle: with the near player retired, the
    // same call must fall through to the far one rather than to nullptr.
    p1p->set_user(-1);
    EXPECT_EQ(p2p, s.world().find_nearest_player(seeker.get()))
        << "with no near user left, the far user-controlled walker wins";
    p1p->set_user(0);

    s.ready_for_battle(1);
    ASSERT_EQ(1, static_cast<int>(s.numviews)) << "one seat for the chrome pins below";
    ASSERT_NE(nullptr, s.viewob[0]) << "and it has a view";
    const signed char old_view_pref = s.viewob[0]->prefs[PREF_VIEW];

    s.viewob[0]->prefs[PREF_VIEW] = PREF_VIEW_PANELS;
    trace_clear();
    s.draw_panels(1);
    EXPECT_TRUE(trace_contains("hud", "panel_border view=0"))
        << "draw_panels must run the redraw that frames a panelled view";

    // Negative control: a FULL-screen view has no border to draw, so the
    // trace above is the pref-driven branch and not an unconditional emit.
    s.viewob[0]->prefs[PREF_VIEW] = PREF_VIEW_FULL;
    trace_clear();
    s.draw_panels(1);
    EXPECT_FALSE(trace_contains("hud", "panel_border"))
        << "a FULL view must not be framed";

    s.viewob[0]->prefs[PREF_VIEW] = old_view_pref;
    s.world().oblist.pop_back();
    s.world().oblist.pop_back();
}


namespace
{
struct ScenTitleFixture
{
    std::filesystem::path scen_dir;
    std::filesystem::path valid_file;
    std::filesystem::path invalid_header_file;
    std::filesystem::path truncated_file;
} g_scen_title_fixture;

void write_bytes(const std::filesystem::path& file, const void* data, size_t size)
{
    SDL_IOStream* out = SDL_IOFromFile(file.string().c_str(), "wb");
    if(out == nullptr)
        return;
    SDL_WriteIO(out, data, size);
    SDL_CloseIO(out);
}

void setup_scen_title_fixture()
{
    g_scen_title_fixture.scen_dir = std::filesystem::path("scen");
    std::filesystem::create_directories(g_scen_title_fixture.scen_dir);
    g_scen_title_fixture.valid_file = g_scen_title_fixture.scen_dir / "typed_title_valid.fss";
    g_scen_title_fixture.invalid_header_file = g_scen_title_fixture.scen_dir / "typed_title_bad_header.fss";
    g_scen_title_fixture.truncated_file = g_scen_title_fixture.scen_dir / "typed_title_truncated.fss";

    // Valid minimal v6+ scenario title payload.
    char valid_payload[42] = {};
    std::memcpy(valid_payload, "FSS", 3);
    valid_payload[3] = 6;
    std::memcpy(valid_payload + 4, "gridname", 8);
    std::memcpy(valid_payload + 12, "Typed Test Title", 16);
    write_bytes(g_scen_title_fixture.valid_file, valid_payload, sizeof(valid_payload));

    char bad_header[42] = {};
    std::memcpy(bad_header, "BAD", 3);
    bad_header[3] = 6;
    write_bytes(g_scen_title_fixture.invalid_header_file, bad_header, sizeof(bad_header));

    char truncated[4] = {'F', 'S', 'S', 6};
    write_bytes(g_scen_title_fixture.truncated_file, truncated, sizeof(truncated));
}

void teardown_scen_title_fixture()
{
    std::error_code ec;
    std::filesystem::remove(g_scen_title_fixture.valid_file, ec);
    std::filesystem::remove(g_scen_title_fixture.invalid_header_file, ec);
    std::filesystem::remove(g_scen_title_fixture.truncated_file, ec);
}
} // namespace

class ScreenExtendedFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        setup_scen_title_fixture();
    }

    void TearDown() override
    {
        teardown_scen_title_fixture();
    }
};

TEST_F(ScreenExtendedFixture, screen_get_scen_title_with_error_typed_paths)
{
    std::string title;
    screen::ScenarioTitleError err = og::runtime::current_session->myscreen_->get_scen_title_with_error("typed_title_valid", title);
    ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::None), static_cast<int>(err)) << "valid scenario file should return typed None";
    ASSERT_TRUE(title == "Typed Test Title") << "valid scenario file should return title string";

    title.clear();
    err = og::runtime::current_session->myscreen_->get_scen_title_with_error("typed_title_bad_header", title);
    ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::InvalidHeader), static_cast<int>(err)) << "invalid header should return InvalidHeader";

    title.clear();
    err = og::runtime::current_session->myscreen_->get_scen_title_with_error("typed_title_truncated", title);
    ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::ReadFailed), static_cast<int>(err)) << "truncated file should return ReadFailed";

    title.clear();
    err = og::runtime::current_session->myscreen_->get_scen_title_with_error("typed_title_missing_file", title);
    ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::OpenReadFailed), static_cast<int>(err)) << "missing file should return OpenReadFailed";
}


// screen::get_scen_title maps EVERY ScenarioTitleError to the literal
// "none" and otherwise hands back the stored title. (This replaces the old
// ScreenExtended.screen_get_scen_title_paths_and_null_foe_guards, whose
// `== "ERROR" || == "none"` accepted a fallback string the product never
// produces and left the success path unasserted; it needs the fixture's
// typed_title_valid file, hence the move into ScreenExtendedFixture.)
TEST_F(ScreenExtendedFixture, screen_get_scen_title_paths_and_null_foe_guards)
{
    screen* s = og::runtime::current_session->myscreen_;

    ASSERT_STREQ("none", s->get_scen_title("definitely_missing_scen_file", s))
        << "a missing scenario file falls back to exactly \"none\"";
    ASSERT_STREQ("none", s->get_scen_title("typed_title_bad_header", s))
        << "an InvalidHeader error falls back to exactly \"none\"";
    ASSERT_STREQ("none", s->get_scen_title("typed_title_truncated", s))
        << "a ReadFailed error falls back to exactly \"none\"";
    ASSERT_STREQ("Typed Test Title", s->get_scen_title("typed_title_valid", s))
        << "a readable scenario file yields its stored title";

    ASSERT_EQ(nullptr, s->world().find_near_foe(nullptr))
        << "find_near_foe must guard nullptr";
    ASSERT_EQ(nullptr, s->world().find_nearest_foe(nullptr))
        << "find_nearest_foe must guard nullptr";
}
