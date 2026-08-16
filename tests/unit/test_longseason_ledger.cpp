/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Kettle's Book (campaigns/longseason/packs/longseason.ledger): the
// scripted-picker pack for The Long Season. Pins: registration + page
// budgets, the ledger_data graph/tile mirror against the SHIPPED package
// (the regeneration tripwire), var==0 byte-identity on every consequence
// level, taken-path spawn censuses, the Collector approach-lane smoke, and
// the full menu decision matrix over CampaignPickerSession.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/resources/campaign_state_providers.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using og::ui::CampaignPickerSession;
namespace hooks = og::script::hooks;

namespace {

constexpr const char* kLedgerPackId = "longseason.ledger";

// ---------------------------------------------------------------------------
// Entity wiring (the test_longseason_levels pattern).
// ---------------------------------------------------------------------------
loader& ledger_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_ledger_world_entity_services(GameWorld* world, LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &ledger_loader();
    world->entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    world->entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family());
    };
    world->entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

const LevelDataHooks& ledger_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_ledger_world_entity_services;
        return h;
    }();
    return hooks;
}

class LedgerCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("longseason"))
            << "builtin/longseason.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error("longseason");
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

struct LoadedLedgerLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedLedgerLevel(int id, std::uint32_t seed = 0)
        : level(id, true, &ledger_levels_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.world().rng_.state_ = seed;
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &level.world().rng_, &cfg);
        gc.rng = &level.world().rng_;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedLedgerLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

// ---------------------------------------------------------------------------
// Ground predicates: the shipped-package standable set minus the Z specials
// (a crate or a posted helper belongs on plain floor, never a stair mouth).
// ---------------------------------------------------------------------------
bool ground_byte_standable(unsigned char t)
{
    switch (t)
    {
        case PIX_GRASS1: case PIX_GRASS2: case PIX_GRASS3: case PIX_GRASS4:
        case PIX_GRASS_DARK_1: case PIX_GRASS_DARK_2:
        case PIX_GRASS_DARK_3: case PIX_GRASS_DARK_4:
        case PIX_GRASS_DARK_LL: case PIX_GRASS_DARK_UR:
        case PIX_GRASS_DARK_B1: case PIX_GRASS_DARK_B2:
        case PIX_GRASS_DARK_BR: case PIX_GRASS_DARK_R1:
        case PIX_GRASS_DARK_R2: case PIX_GRASS_RUBBLE:
        case PIX_GRASS1_DAMAGED:
        case PIX_GRASS_LIGHT_1: case PIX_GRASS_LIGHT_LEFT_TOP:
        case PIX_GRASS_LIGHT_LEFT: case PIX_GRASS_LIGHT_LEFT_BOTTOM:
        case PIX_GRASS_LIGHT_TOP: case PIX_GRASS_LIGHT_BOTTOM:
        case PIX_GRASS_LIGHT_RIGHT_TOP: case PIX_GRASS_LIGHT_RIGHT:
        case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
        case PIX_GRASSWATER_LL: case PIX_GRASSWATER_LR:
        case PIX_GRASSWATER_UL: case PIX_GRASSWATER_UR:
        case PIX_PAVEMENT1: case PIX_PAVEMENT2: case PIX_PAVEMENT3:
        case PIX_COBBLE_1: case PIX_COBBLE_2: case PIX_COBBLE_3:
        case PIX_COBBLE_4:
        case PIX_FLOOR_PAVEL: case PIX_FLOOR_PAVER:
        case PIX_FLOOR_PAVEU: case PIX_FLOOR_PAVED:
        case PIX_PAVESTEPS1: case PIX_PAVESTEPS2:
        case PIX_PAVESTEPS2L: case PIX_PAVESTEPS2R:
        case PIX_FLOOR1:
        case PIX_DIRT_1: case PIX_DIRTGRASS_UL1: case PIX_DIRTGRASS_UR1:
        case PIX_DIRTGRASS_LL1: case PIX_DIRTGRASS_LR1:
        case PIX_DIRT_DARK_1: case PIX_DIRTGRASS_DARK_UL1:
        case PIX_DIRTGRASS_DARK_UR1: case PIX_DIRTGRASS_DARK_LL1:
        case PIX_DIRTGRASS_DARK_LR1:
        case PIX_PATH_1: case PIX_PATH_2: case PIX_PATH_3: case PIX_PATH_4:
        case PIX_SNOW1: case PIX_SNOW2:
        case PIX_MARSH1: case PIX_MARSH2:
        case PIX_ASH1: case PIX_ASH2:
            return true;
        default:
            return false;
    }
}

bool tile_standable(GameWorld& world, int floor, int tx, int ty)
{
    const PixieData& g = world.grid_for_floor(floor);
    if (!g.valid() || tx < 0 || ty < 0 || tx >= g.w || ty >= g.h)
        return false;
    if (!ground_byte_standable(static_cast<unsigned char>(
            g.data[static_cast<std::size_t>(tx + ty * g.w)])))
        return false;
    const PixieData& dec = world.decor_for_floor(floor);
    if (dec.valid() && dec.w == g.w && dec.h == g.h)
    {
        const unsigned char d = static_cast<unsigned char>(
            dec.data[static_cast<std::size_t>(tx + ty * dec.w)]);
        if (d < DECOR_MAX &&
            kDecorRegistry[d].pass == DecorPassability::BlocksGround)
            return false;
    }
    return true;
}

bool tile_entity_free(GameWorld& world, int floor, int tx, int ty)
{
    const auto overlaps = [&](walker* ob) {
        if (ob == nullptr)
            return false;
        if (ob->floor() != floor)
            return false;
        const int x0 = ob->xpos() / GRID_SIZE;
        const int y0 = ob->ypos() / GRID_SIZE;
        const int x1 = (ob->xpos() + ob->sizex() - 1) / GRID_SIZE;
        const int y1 = (ob->ypos() + ob->sizey() - 1) / GRID_SIZE;
        return tx >= x0 && tx <= x1 && ty >= y0 && ty <= y1;
    };
    for (const auto& uptr : world.oblist)
        if (overlaps(uptr.get()))
            return false;
    for (const auto& uptr : world.fxlist)
        if (overlaps(uptr.get()))
            return false;
    return true;
}

std::vector<walker*> find_living_by_name(GameWorld& world, const char* name)
{
    std::vector<walker*> found;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->stats() != nullptr && ob->stats()->name == name)
            found.push_back(ob);
    }
    return found;
}

int count_livings_on_team(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->team_num() == team)
            count++;
    }
    return count;
}

int count_fx_family(GameWorld& world, int family)
{
    int count = 0;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->family() == family)
            count++;
    }
    return count;
}

std::vector<int> exit_destinations(GameWorld& world)
{
    std::vector<int> destinations;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT && ob->stats() != nullptr)
            destinations.push_back(ob->stats()->level());
    }
    std::sort(destinations.begin(), destinations.end());
    return destinations;
}

std::vector<walker*> start_markers(GameWorld& world)
{
    std::vector<walker*> markers;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
            markers.push_back(ob);
    }
    return markers;
}

// A deterministic end-state fingerprint: every entity's placement and
// health folded into one value (identical sims produce identical floats,
// so the llround view is exact).
std::uint64_t world_fingerprint(GameWorld& world)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto fold = [&hash](std::uint64_t v) {
        hash ^= v;
        hash *= 1099511628211ull;
    };
    const auto fold_ob = [&](walker* ob) {
        if (ob == nullptr)
            return;
        fold(static_cast<std::uint64_t>(static_cast<std::int64_t>(ob->xpos())));
        fold(static_cast<std::uint64_t>(static_cast<std::int64_t>(ob->ypos())));
        fold(static_cast<std::uint64_t>(static_cast<std::int64_t>(ob->floor())));
        fold(static_cast<std::uint64_t>(ob->family()));
        fold(static_cast<std::uint64_t>(ob->team_num()));
        fold(static_cast<std::uint64_t>(ob->dead()));
        if (ob->stats() != nullptr)
            fold(static_cast<std::uint64_t>(
                std::llround(static_cast<double>(ob->stats()->hitpoints()) *
                             256.0)));
    };
    for (const auto& uptr : world.oblist)
        fold_ob(uptr.get());
    for (const auto& uptr : world.fxlist)
        fold_ob(uptr.get());
    for (const auto& uptr : world.weaplist)
        fold_ob(uptr.get());
    return hash;
}

// The battle-smoke crew shape: stand-in soldiers on the first markers.
void place_crew(GameWorld& world, int count, int level)
{
    const std::vector<walker*> markers = start_markers(world);
    ASSERT_GE(static_cast<int>(markers.size()), count);
    for (int i = 0; i < count; ++i)
    {
        walker* crew = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, crew);
        crew->set_team_num(0);
        crew->set_real_team_num(0);
        crew->stats()->set_level(level);
        crew->set_floor(markers[static_cast<std::size_t>(i)]->floor());
        crew->setxy(markers[static_cast<std::size_t>(i)]->xpos(),
                    markers[static_cast<std::size_t>(i)]->ypos());
    }
}

// ---------------------------------------------------------------------------
// The menu fixture: the mounted book over a scripted SaveData through the
// REAL provider glue (the same wiring every surface installs).
// ---------------------------------------------------------------------------
class LedgerBookTest : public LedgerCampaignTest
{
protected:
    void SetUp() override
    {
        LedgerCampaignTest::SetUp();
        previous_game_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        hooks::clear_campaign_providers();
        save_.current_campaign = "longseason";
        save_.my_team = 0;
        save_.scen_num = 1;
        save_.m_totalcash[0] = 5000;
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }

    void TearDown() override
    {
        hooks::clear_campaign_providers();
        current_game = previous_game_;
        LedgerCampaignTest::TearDown();
    }

    void complete_levels(int first, int last)
    {
        for (int lvl = first; lvl <= last; ++lvl)
            save_.add_level_completed("longseason", lvl);
    }

    static int find_row(const CampaignPickerSession& session,
                        const std::string& id)
    {
        const auto& rows = session.page().rows;
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == id)
                return static_cast<int>(i);
        return -1;
    }

    static bool has_row(const CampaignPickerSession& session,
                        const std::string& id)
    {
        return find_row(session, id) >= 0;
    }

    // Chooses the row with entry id `id`, asserting it exists.
    static CampaignPickerSession::Outcome choose_row(
        CampaignPickerSession& session, const std::string& id)
    {
        const int index = find_row(session, id);
        EXPECT_GE(index, 0) << "row '" << id << "' missing from '"
                            << session.page().title << "'";
        if (index < 0)
            return {};
        return session.choose(static_cast<std::size_t>(index));
    }

    std::int32_t state(const char* key) const
    {
        return save_.campaign_state_get("longseason", key);
    }

    SaveData save_;

private:
    GameplayContext* previous_game_ = nullptr;
};

}  // namespace

// ===========================================================================
// Registration + page budgets
// ===========================================================================

TEST_F(LedgerBookTest, pack_registers_the_book_with_the_four_vars)
{
    ASSERT_TRUE(hooks::campaign_picker_registered());
    const std::vector<std::string> vars = hooks::campaign_registered_vars();
    ASSERT_EQ(4u, vars.size());
    EXPECT_EQ("coin_kept", vars[0]);
    EXPECT_EQ("advance_debt", vars[1]);
    EXPECT_EQ("provisions", vars[2]);
    EXPECT_EQ("fair_round", vars[3]);
    for (const og::script::ScriptError& e :
         og::script::active_world_scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

TEST_F(LedgerBookTest, every_page_in_every_book_state_fits_the_budgets)
{
    // Four book states spanning the branchy lines: fresh spring, a
    // mid-season book with debt + backlog + crate, the settlement table,
    // and the settled new-season book.
    struct BookState { const char* name; int cursor; int completed_to;
                       bool debt; bool settled; };
    const BookState states[] = {
        {"fresh", 1, 0, false, false},
        {"mid-season", 10, 9, true, false},
        {"settlement", 19, 18, true, false},
        {"new-season", 19, 19, false, true},
    };
    const char* pages[] = {"", "work", "coin", "stores", "debts"};
    for (const BookState& s : states)
    {
        SaveData book;
        book.current_campaign = "longseason";
        book.my_team = 0;
        book.scen_num = static_cast<short>(s.cursor);
        book.m_totalcash[0] = 5000;
        for (int lvl = 1; lvl <= s.completed_to; ++lvl)
            book.add_level_completed("longseason", lvl);
        if (s.completed_to >= 3)
        {
            ASSERT_TRUE(book.campaign_state_set("longseason", "coin_kept",
                                                4));  // bit 2 kept
            ASSERT_TRUE(book.campaign_state_set("longseason", "coin_spent",
                                                8));  // bit 3 passed on
        }
        if (s.debt)
        {
            ASSERT_TRUE(book.campaign_state_set("longseason", "advance_debt",
                                                900));
        }
        if (s.settled)
        {
            ASSERT_TRUE(book.campaign_state_set("longseason", "settled", 1));
        }
        ASSERT_TRUE(book.campaign_state_set("longseason", "kettle_asked", 2));
        ASSERT_TRUE(book.campaign_state_set("longseason", "provisions",
                                            1 + 8 * s.cursor));
        hooks::clear_campaign_providers();
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(book));

        for (const char* page_id : pages)
        {
            hooks::CampaignPage page;
            ASSERT_TRUE(hooks::campaign_picker_page(page_id, page))
                << s.name << " page '" << page_id << "'";
            EXPECT_FALSE(page.title.empty()) << s.name << " " << page_id;
            EXPECT_LE(page.lines.size(), 6u) << s.name << " " << page_id;
            for (const std::string& line : page.lines)
                EXPECT_LE(line.size(), 38u)
                    << s.name << " " << page_id << ": '" << line << "'";
            EXPECT_LE(page.entries.size(), 24u) << s.name << " " << page_id;
            for (const hooks::CampaignPageEntry& entry : page.entries)
            {
                EXPECT_LE(entry.label.size(), 24u)
                    << s.name << " " << page_id << ": '" << entry.label << "'";
                EXPECT_LE(entry.note.size(), 20u)
                    << s.name << " " << page_id << ": '" << entry.note << "'";
            }
        }
        // The providers borrow the loop-local save; drop them before it
        // goes out of scope.
        hooks::clear_campaign_providers();
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }
    for (const og::script::ScriptError& e :
         og::script::active_world_scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

// ===========================================================================
// The ledger_data mirror pinned against the SHIPPED package (regeneration
// tripwire): the Lua tables are dumped through a probe chunk registered
// into the pack (so og.use resolves the real lib) and compared against the
// package the players actually get.
// ===========================================================================

namespace {

// Chunk name deliberately does NOT start with `packs/`: that prefix
// declares bytes to the pack-Lua coverage inventory, and this throwaway
// chunk exists nowhere in the repository.
constexpr const char* kGraphProbe = R"LUA(local D = og.use("ledger_data")
for lvl = 1, D.LEVEL_COUNT do
  local line = "EXITS " .. lvl
  local exits = D.EXITS[lvl]
  for i = 1, #exits do
    line = line .. " " .. exits[i]
  end
  og.log(line)
end
for lvl = 1, D.LEVEL_COUNT do
  local s = D.SUPPLY[lvl]
  local line = "SUPPLY " .. lvl .. " " .. s.floor
  for i = 1, #s.tiles do
    line = line .. " " .. s.tiles[i][1] .. " " .. s.tiles[i][2]
  end
  og.log(line)
end
local fair = "FAIR " .. D.FAIR.floor
for i = 1, #D.FAIR.helpers do
  fair = fair .. " " .. D.FAIR.helpers[i][1] .. " " .. D.FAIR.helpers[i][2]
end
for i = 1, #D.FAIR.food do
  fair = fair .. " " .. D.FAIR.food[i][1] .. " " .. D.FAIR.food[i][2]
end
og.log(fair)
local coll = "COLLECTORS " .. D.COLLECTORS.floor
for i = 1, #D.COLLECTORS.tiles do
  coll = coll .. " " .. D.COLLECTORS.tiles[i][1] .. " " .. D.COLLECTORS.tiles[i][2]
end
og.log(coll)
local carried = "CARRIED " .. D.CARRIED.floor
for i = 1, #D.CARRIED.tiles do
  carried = carried .. " " .. D.CARRIED.tiles[i][1] .. " " .. D.CARRIED.tiles[i][2]
end
og.log(carried)
og.log("DOORCOIN " .. D.DOOR_COIN.floor .. " " .. D.DOOR_COIN.tile[1] ..
       " " .. D.DOOR_COIN.tile[2])
og.log("WORDCLAMP " .. D.count_word(-1) .. " " .. D.count_word(99)))LUA";

std::vector<int> parse_ints(const std::string& text)
{
    std::vector<int> values;
    std::istringstream stream(text);
    int v = 0;
    while (stream >> v)
        values.push_back(v);
    return values;
}

}  // namespace

TEST_F(LedgerBookTest, ledger_data_mirror_matches_the_shipped_package)
{
    og::script::register_pack_script(
        {kLedgerPackId, "ledgertest/scripts/graph_probe.lua", kGraphProbe});
    ASSERT_TRUE(hooks::campaign_picker_registered());  // builds the VM

    // Collect the probe's dump.
    std::map<std::string, std::vector<int>> exits_by_level;
    std::map<std::string, std::vector<int>> supply_by_level;
    std::vector<int> fair;
    std::vector<int> collectors;
    std::vector<int> carried;
    std::vector<int> door_coin;
    for (const std::string& line :
         og::script::active_world_scripts().host().log())
    {
        if (line.rfind("EXITS ", 0) == 0)
        {
            const std::vector<int> v = parse_ints(line.substr(6));
            exits_by_level[std::to_string(v[0])] =
                std::vector<int>(v.begin() + 1, v.end());
        }
        else if (line.rfind("SUPPLY ", 0) == 0)
        {
            const std::vector<int> v = parse_ints(line.substr(7));
            supply_by_level[std::to_string(v[0])] =
                std::vector<int>(v.begin() + 1, v.end());
        }
        else if (line.rfind("FAIR ", 0) == 0)
        {
            fair = parse_ints(line.substr(5));
        }
        else if (line.rfind("COLLECTORS ", 0) == 0)
        {
            collectors = parse_ints(line.substr(11));
        }
        else if (line.rfind("CARRIED ", 0) == 0)
        {
            carried = parse_ints(line.substr(8));
        }
        else if (line.rfind("DOORCOIN ", 0) == 0)
        {
            door_coin = parse_ints(line.substr(9));
        }
    }
    ASSERT_EQ(19u, exits_by_level.size());
    ASSERT_EQ(19u, supply_by_level.size());
    ASSERT_EQ(9u, fair.size());        // floor + 2 helper + 2 food tiles
    ASSERT_EQ(5u, collectors.size());  // floor + 2 tiles
    ASSERT_EQ(7u, carried.size());     // floor + 3 tiles
    ASSERT_EQ(3u, door_coin.size());   // floor + 1 tile

    for (int id = 1; id <= 19; ++id)
    {
        LoadedLedgerLevel fx(id);
        ASSERT_TRUE(fx.loaded) << "level " << id;
        GameWorld& world = fx.world();

        // The graph mirror IS the shipped exit set.
        EXPECT_EQ(exit_destinations(world),
                  exits_by_level[std::to_string(id)])
            << "level " << id << " exit-graph mirror drifted";

        // Every supply spot: floor + 10 standable, entity-free tiles.
        const std::vector<int>& supply = supply_by_level[std::to_string(id)];
        ASSERT_EQ(21u, supply.size()) << "level " << id;
        const int floor = supply[0];
        for (std::size_t i = 1; i + 1 < supply.size(); i += 2)
        {
            const int tx = supply[i];
            const int ty = supply[i + 1];
            EXPECT_TRUE(tile_standable(world, floor, tx, ty))
                << "level " << id << " supply (" << tx << "," << ty << ")";
            EXPECT_TRUE(tile_entity_free(world, floor, tx, ty))
                << "level " << id << " supply (" << tx << "," << ty << ")";
        }

        const auto check_tiles = [&](const std::vector<int>& data,
                                     const char* what) {
            const int tile_floor = data[0];
            for (std::size_t i = 1; i + 1 < data.size(); i += 2)
            {
                EXPECT_TRUE(tile_standable(world, tile_floor, data[i],
                                           data[i + 1]))
                    << what << " (" << data[i] << "," << data[i + 1] << ")";
                EXPECT_TRUE(tile_entity_free(world, tile_floor, data[i],
                                             data[i + 1]))
                    << what << " (" << data[i] << "," << data[i + 1] << ")";
            }
        };
        if (id == 9)
            check_tiles(fair, "fair");
        if (id == 14)
            check_tiles(collectors, "collectors");
        if (id == 18)
            check_tiles(carried, "carried");
        if (id == 19)
            check_tiles(door_coin, "door coin");
    }

    // The count-word clamps answer their floor and ceiling words.
    bool word_clamp_seen = false;
    for (const std::string& line :
         og::script::active_world_scripts().host().log())
        if (line == "WORDCLAMP none seventeen")
            word_clamp_seen = true;
    EXPECT_TRUE(word_clamp_seen) << "count_word clamps drifted";

    // The Collector tiles sit ON the shipped toll road (the proven west
    // approach lane: paint_path x1..23, y29..30 — levels_winter.cpp).
    EXPECT_EQ(0, collectors[0]);
    for (std::size_t i = 1; i + 1 < collectors.size(); i += 2)
    {
        EXPECT_GE(collectors[i], 1);
        EXPECT_LE(collectors[i], 23);
        EXPECT_GE(collectors[i + 1], 29);
        EXPECT_LE(collectors[i + 1], 30);
    }
}

// ===========================================================================
// var == 0 byte-identity: with an empty book, every consequence level runs
// EXACTLY the shipped sim — same censuses, same RNG state, same entity
// fingerprint — whether or not the ledger pack is registered.
// ===========================================================================

namespace {

struct SimEndState
{
    std::uint32_t rng_state = 0;
    std::uint32_t ticks = 0;
    std::size_t oblist_size = 0;
    std::size_t fxlist_size = 0;
    int t0_livings = 0;
    int t1_livings = 0;
    int t2_livings = 0;
    std::uint64_t fingerprint = 0;

    bool operator==(const SimEndState&) const = default;
};

SimEndState run_level_300(int id, std::uint32_t seed)
{
    LoadedLedgerLevel fx(id, seed);
    EXPECT_TRUE(fx.loaded) << "level " << id;
    GameWorld& world = fx.world();
    for (int i = 0; i < 300 && !world.game_ended; ++i)
        world.tick();
    for (const og::script::ScriptError& e :
         world.scripts().host().errors())
        ADD_FAILURE() << "level " << id << " script error at " << e.where
                      << ": " << e.message;
    SimEndState state;
    state.rng_state = world.rng_.state_;
    state.ticks = world.tick_count_;
    state.oblist_size = world.oblist.size();
    state.fxlist_size = world.fxlist.size();
    state.t0_livings = count_livings_on_team(world, 0);
    state.t1_livings = count_livings_on_team(world, 1);
    state.t2_livings = count_livings_on_team(world, 2);
    state.fingerprint = world_fingerprint(world);
    return state;
}

// Re-registers the captured script set, optionally without the ledger pack.
void reload_pack_scripts(const std::vector<og::script::PackScript>& all,
                         bool with_ledger)
{
    og::script::clear_pack_scripts();
    for (const og::script::PackScript& script : all)
    {
        if (!with_ledger && script.pack_id == kLedgerPackId)
            continue;
        og::script::register_pack_script(script);
    }
}

}  // namespace

TEST_F(LedgerCampaignTest, empty_book_is_byte_identical_on_consequence_levels)
{
    const std::vector<og::script::PackScript> all_scripts =
        og::script::pack_scripts();
    bool ledger_present = false;
    for (const og::script::PackScript& script : all_scripts)
        if (script.pack_id == kLedgerPackId)
            ledger_present = true;
    ASSERT_TRUE(ledger_present) << "mount must register the ledger pack";

    for (const int id : {9, 14, 18, 19})
    {
        reload_pack_scripts(all_scripts, false);
        const SimEndState baseline = run_level_300(id, 1337u);
        reload_pack_scripts(all_scripts, true);
        const SimEndState with_pack = run_level_300(id, 1337u);
        EXPECT_EQ(baseline.rng_state, with_pack.rng_state)
            << "level " << id << ": the empty book must not touch the RNG";
        EXPECT_EQ(baseline.ticks, with_pack.ticks) << "level " << id;
        EXPECT_EQ(baseline.oblist_size, with_pack.oblist_size)
            << "level " << id;
        EXPECT_EQ(baseline.fxlist_size, with_pack.fxlist_size)
            << "level " << id;
        EXPECT_EQ(baseline.t0_livings, with_pack.t0_livings) << "level " << id;
        EXPECT_EQ(baseline.t1_livings, with_pack.t1_livings) << "level " << id;
        EXPECT_EQ(baseline.t2_livings, with_pack.t2_livings) << "level " << id;
        EXPECT_EQ(baseline.fingerprint, with_pack.fingerprint)
            << "level " << id << ": entity state drifted at var 0";
    }
}

// ===========================================================================
// Taken paths: exact spawn censuses at tick 1.
// ===========================================================================

TEST_F(LedgerCampaignTest, fair_round_posts_stallkeeper_and_potboy)
{
    LoadedLedgerLevel fx(9);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    const int t0_before = count_livings_on_team(world, 0);
    const int food_before = count_fx_family(world, FAMILY_DRUMSTICK);
    world.campaign_vars.emplace_back("fair_round", 1);
    world.tick();

    EXPECT_EQ(t0_before + 2, count_livings_on_team(world, 0));
    EXPECT_EQ(food_before + 2, count_fx_family(world, FAMILY_DRUMSTICK));
    const struct { const char* name; int tx; int ty; } expected[] = {
        {"Stallkeeper", 29, 15},
        {"Potboy", 31, 15},
    };
    for (const auto& e : expected)
    {
        const std::vector<walker*> found = find_living_by_name(world, e.name);
        ASSERT_EQ(1u, found.size()) << e.name;
        walker* helper = found[0];
        EXPECT_EQ(0, static_cast<int>(helper->team_num())) << e.name;
        EXPECT_EQ(2, helper->stats()->level()) << e.name;
        EXPECT_EQ(0, static_cast<int>(helper->floor())) << e.name;
        EXPECT_EQ(e.tx * GRID_SIZE, static_cast<int>(helper->xpos()))
            << e.name;
        EXPECT_EQ(e.ty * GRID_SIZE, static_cast<int>(helper->ypos()))
            << e.name;
        // At tick end a sighted plain guard reads ACT_RANDOM (the wake
        // fires inside its own act, AFTER the on_tick re-pin) — the
        // hold-post proof is behavioral: the repin test below and the
        // 300-tick door-hold smoke.
    }
    for (const og::script::ScriptError& e : world.scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

namespace {

// Marks every hostile living/generator within `radius_tiles` (Chebyshev) of
// the tile as dead, so nothing can sight-wake a posted guard there.
void silence_hostiles_near(GameWorld& world, int cx, int cy, int radius_tiles)
{
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob->team_num() == 0)
            continue;
        if (ob->query_order() != Order::Living &&
            ob->query_order() != Order::Generator)
            continue;
        const int tx = ob->xpos() / GRID_SIZE;
        const int ty = ob->ypos() / GRID_SIZE;
        if (std::max(std::abs(tx - cx), std::abs(ty - cy)) <= radius_tiles)
            ob->set_dead(1);
    }
}

}  // namespace

TEST_F(LedgerCampaignTest, fair_helpers_repin_to_guard_when_woken)
{
    LoadedLedgerLevel fx(9);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.campaign_vars.emplace_back("fair_round", 1);
    // Clear every hostile a door guard could see (soldier sight is 7
    // tiles; 14 leaves margin for one tick of drift), so the wake rule
    // cannot re-fire and the re-pin's write is observable at tick end.
    silence_hostiles_near(world, 30, 15, 14);
    world.tick();
    const std::vector<walker*> found =
        find_living_by_name(world, "Stallkeeper");
    ASSERT_EQ(1u, found.size());
    // A genuine sighting converts a plain guard to ACT_RANDOM
    // (walker::act_guard's wake rule); the pack's on_tick re-pin is the
    // scripted hold-post that puts it back before the next act.
    found[0]->set_act_type(ACT_RANDOM);
    world.tick();
    EXPECT_EQ(ACT_GUARD, static_cast<int>(found[0]->act_type()))
        << "the scripted hold-post must re-pin a woken helper";
    for (const og::script::ScriptError& e : world.scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

// The door-hold smoke: with the fair pressing the strongroom for 300
// ticks, the helpers must not leave their doorway tiles — a plain allied
// ACT_GUARD (no scripted hold-post) wakes on first sighting and hunts.
//
// Red-proof (2026-08-15, this worktree): with level_hooks.lua's
// `hooks.on_tick = fair_on_tick` registration commented out and the
// archive restaged, Stallkeeper wandered 113px off the door within 300
// ticks (this test failed at the position assert) and the repin test
// failed with it; restored, both hold.
TEST_F(LedgerCampaignTest, fair_helpers_hold_the_door_for_300_ticks)
{
    LoadedLedgerLevel fx(9, 77u);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.campaign_vars.emplace_back("fair_round", 1);
    world.tick();
    const std::vector<walker*> stall =
        find_living_by_name(world, "Stallkeeper");
    const std::vector<walker*> potboy = find_living_by_name(world, "Potboy");
    ASSERT_EQ(1u, stall.size());
    ASSERT_EQ(1u, potboy.size());
    for (int i = 1; i < 300 && !world.game_ended; ++i)
        world.tick();
    // A guard never walks; accumulated hit recoil from the door fight can
    // nudge it (measured 21px on Potboy over 300 ticks), but a woken
    // hunter crosses ten-plus tiles — two tiles cleanly separates them.
    EXPECT_LE(std::abs(stall[0]->xpos() - 29 * GRID_SIZE), 2 * GRID_SIZE);
    EXPECT_LE(std::abs(stall[0]->ypos() - 15 * GRID_SIZE), 2 * GRID_SIZE);
    EXPECT_LE(std::abs(potboy[0]->xpos() - 31 * GRID_SIZE), 2 * GRID_SIZE);
    EXPECT_LE(std::abs(potboy[0]->ypos() - 15 * GRID_SIZE), 2 * GRID_SIZE);
    for (const og::script::ScriptError& e : world.scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

TEST_F(LedgerCampaignTest, advance_debt_sends_two_collectors_up_the_toll_road)
{
    LoadedLedgerLevel fx(14);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    const int t2_before = count_livings_on_team(world, 2);
    world.campaign_vars.emplace_back("advance_debt", 900);
    world.tick();

    EXPECT_EQ(t2_before + 2, count_livings_on_team(world, 2));
    const std::vector<walker*> found = find_living_by_name(world, "Collector");
    ASSERT_EQ(2u, found.size());
    const int expected_tiles[2][2] = {{6, 29}, {8, 30}};
    for (std::size_t i = 0; i < found.size(); ++i)
    {
        EXPECT_EQ(2, static_cast<int>(found[i]->team_num()));
        EXPECT_EQ(4, found[i]->stats()->level());
        EXPECT_EQ(0, static_cast<int>(found[i]->floor()));
        EXPECT_EQ(expected_tiles[i][0] * GRID_SIZE,
                  static_cast<int>(found[i]->xpos()));
        EXPECT_EQ(expected_tiles[i][1] * GRID_SIZE,
                  static_cast<int>(found[i]->ypos()));
    }
    for (const og::script::ScriptError& e : world.scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

TEST_F(LedgerCampaignTest, carried_scale_one_per_four_kept_coins_capped)
{
    // popcount 3 (bits 2..4) stands nobody up; popcount 4 stands up one;
    // all seventeen coins cap at three.
    const struct { std::int32_t kept; int expected; } tiers[] = {
        {4 + 8 + 16, 0},
        {4 + 8 + 16 + 32, 1},
        {(1 << 19) - 4, 3},  // bits 2..18 all set
    };
    for (const auto& tier : tiers)
    {
        LoadedLedgerLevel fx(18);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        const int t0_before = count_livings_on_team(world, 0);
        world.campaign_vars.emplace_back("coin_kept", tier.kept);
        world.tick();
        EXPECT_EQ(t0_before + tier.expected, count_livings_on_team(world, 0))
            << "kept mask " << tier.kept;
        const std::vector<walker*> found =
            find_living_by_name(world, "The Carried");
        ASSERT_EQ(static_cast<std::size_t>(tier.expected), found.size())
            << "kept mask " << tier.kept;
        const int expected_tiles[3][2] = {{2, 19}, {2, 21}, {2, 23}};
        for (std::size_t i = 0; i < found.size(); ++i)
        {
            EXPECT_EQ(0, static_cast<int>(found[i]->team_num()));
            EXPECT_EQ(4, found[i]->stats()->level());
            EXPECT_EQ(ACT_GUARD, static_cast<int>(found[i]->act_type()));
            EXPECT_EQ(0, static_cast<int>(found[i]->floor()));
            EXPECT_EQ(expected_tiles[i][0] * GRID_SIZE,
                      static_cast<int>(found[i]->xpos()));
            EXPECT_EQ(expected_tiles[i][1] * GRID_SIZE,
                      static_cast<int>(found[i]->ypos()));
        }
        for (const og::script::ScriptError& e :
             world.scripts().host().errors())
            ADD_FAILURE() << e.where << ": " << e.message;
    }
}

TEST_F(LedgerCampaignTest, kept_coin_nails_the_door_coin_on_settlement_day)
{
    LoadedLedgerLevel fx(19);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    const int silver_before = count_fx_family(world, FAMILY_SILVER_BAR);
    world.campaign_vars.emplace_back("coin_kept", 4);  // one kept coin
    world.tick();
    EXPECT_EQ(silver_before + 1, count_fx_family(world, FAMILY_SILVER_BAR));
    bool found = false;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->family() == FAMILY_SILVER_BAR &&
            ob->xpos() == 37 * GRID_SIZE && ob->ypos() == 13 * GRID_SIZE)
            found = true;
    }
    EXPECT_TRUE(found) << "the coin must be nailed over the door tile";
    for (const og::script::ScriptError& e : world.scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

TEST_F(LedgerCampaignTest, crates_go_where_they_are_addressed)
{
    // kind 1 = 4 meals; kind 2 = 8; kind 3 = 8 meals + 2 silver. A crate
    // addressed elsewhere spawns nothing; a corrupt kind spawns nothing.
    const struct { int level; std::int32_t provisions; int meals; int silver;
                   const char* name; } cases[] = {
        {5, 1 + 8 * 5, 4, 0, "meal at its site"},
        {5, 2 + 8 * 5, 8, 0, "good crate at its site"},
        {5, 3 + 8 * 5, 8, 2, "strong crate at its site"},
        {5, 3 + 8 * 6, 0, 0, "crate addressed elsewhere"},
        {5, 0 + 8 * 5, 0, 0, "corrupt kind 0"},
        {5, 4 + 8 * 5, 0, 0, "corrupt kind 4"},
        {17, 1 + 8 * 17, 4, 0, "meal on Ashfall Gate"},
        {18, 3 + 8 * 18, 8, 2, "strong crate at the Warm Mint"},
    };
    for (const auto& c : cases)
    {
        LoadedLedgerLevel fx(c.level);
        ASSERT_TRUE(fx.loaded) << c.name;
        GameWorld& world = fx.world();
        const int food_before = count_fx_family(world, FAMILY_DRUMSTICK);
        const int silver_before = count_fx_family(world, FAMILY_SILVER_BAR);
        world.campaign_vars.emplace_back("provisions", c.provisions);
        world.tick();
        EXPECT_EQ(food_before + c.meals, count_fx_family(world, FAMILY_DRUMSTICK))
            << c.name;
        EXPECT_EQ(silver_before + c.silver,
                  count_fx_family(world, FAMILY_SILVER_BAR))
            << c.name;
        for (const og::script::ScriptError& e :
             world.scripts().host().errors())
            ADD_FAILURE() << c.name << ": " << e.where << ": " << e.message;
    }
}

// ===========================================================================
// The Collector approach-lane smoke: 600 ticks with a courtyard crew, the
// Collectors must DISPLACE from their spawn tiles and ENGAGE the defense.
//
// Red-proof (2026-08-15, this worktree): with D.COLLECTORS.tiles moved to
// the NW gully pocket (7,7)/(9,7) — standable ground north of the
// full-width cliff band, off the toll road — Collector 1 parked with 0px
// displacement over the full 600 ticks and this test failed on the
// "parked at its spawn" assert. The shipped tiles sit ON the road lane
// (paint_path x1..23 y29..30) and the same run goes green.
// ===========================================================================

TEST_F(LedgerCampaignTest, collectors_walk_the_lane_and_engage)
{
    LoadedLedgerLevel fx(14, 424242u);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    place_crew(world, 8, 7);
    world.campaign_vars.emplace_back("advance_debt", 900);
    world.tick();
    const std::vector<walker*> collectors =
        find_living_by_name(world, "Collector");
    ASSERT_EQ(2u, collectors.size());
    const int spawn_x[2] = {collectors[0]->xpos(), collectors[1]->xpos()};
    const int spawn_y[2] = {collectors[0]->ypos(), collectors[1]->ypos()};
    const float initial_hp[2] = {collectors[0]->stats()->hitpoints(),
                                 collectors[1]->stats()->hitpoints()};

    for (int i = 1; i < 600 && !world.game_ended; ++i)
        world.tick();

    // Displacement: hunt AI must carry both Collectors well off their
    // spawn tiles (a statue on an off-lane tile fails here).
    for (std::size_t i = 0; i < 2; ++i)
    {
        const int moved = std::abs(collectors[i]->xpos() - spawn_x[i]) +
                          std::abs(collectors[i]->ypos() - spawn_y[i]);
        EXPECT_GT(moved, 4 * GRID_SIZE)
            << "Collector " << i << " parked at its spawn";
    }
    // Engagement: the fight reached them — a Collector died, bled, or
    // closed to melee range of the courtyard hold.
    bool engaged = false;
    for (std::size_t i = 0; i < 2; ++i)
    {
        if (collectors[i]->dead() != 0)
            engaged = true;
        else if (collectors[i]->stats()->hitpoints() < initial_hp[i])
            engaged = true;
    }
    if (!engaged)
    {
        // Neither took damage: they must at least have crossed into the
        // courtyard (x >= 24 tiles, the west gate line).
        for (std::size_t i = 0; i < 2; ++i)
            if (collectors[i]->xpos() >= 24 * GRID_SIZE)
                engaged = true;
    }
    EXPECT_TRUE(engaged) << "the Collectors never reached the fight";
    for (const og::script::ScriptError& e : world.scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

// ===========================================================================
// The menu decision matrix (CampaignPickerSession over the real pack).
// ===========================================================================

TEST_F(LedgerBookTest, fresh_root_reads_like_the_ledger)
{
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("KETTLE'S BOOK", page.title);
    ASSERT_EQ(4u, page.lines.size());
    EXPECT_EQ("Wages banked: 5000g. Debt: none.", page.lines[0]);
    EXPECT_EQ("Coins kept: none. None waiting.", page.lines[1]);
    EXPECT_EQ("Bell open. Tolls open. Vault open.", page.lines[2]);
    EXPECT_EQ("Spring. The mud pays first.", page.lines[3]);
    // No coin waits, no settlement: WORK leads, the coin page sits after
    // the debts, no DRAW YOUR PAY.
    ASSERT_EQ(5u, page.rows.size());
    EXPECT_EQ("work", page.rows[0].id);
    EXPECT_EQ("stores", page.rows[1].id);
    EXPECT_EQ("debts", page.rows[2].id);
    EXPECT_EQ("coin", page.rows[3].id);
    EXPECT_EQ("ask", page.rows[4].id);
    EXPECT_EQ("", page.rows[4].note) << "unasked kettle carries no note";
}

TEST_F(LedgerBookTest, waiting_coin_surfaces_first_with_season_and_contracts)
{
    complete_levels(1, 9);
    save_.scen_num = 10;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    const CampaignPickerSession::DecoratedPage& page = session.page();
    // Coins 2..9 completed and unresolved: eight waiting, coin row first.
    EXPECT_EQ("Coins kept: none. Eight waiting.", page.lines[1]);
    EXPECT_EQ("Bell taken. Tolls taken. Vault open.", page.lines[2]);
    EXPECT_EQ("Autumn. The seams run warm.", page.lines[3]);
    EXPECT_EQ("Wages banked: 5000g. Debt: 900g.", page.lines[0]);
    ASSERT_GE(page.rows.size(), 1u);
    EXPECT_EQ("coin", page.rows[0].id);
    EXPECT_EQ("Eight waiting", page.rows[0].note);
    const int debts = find_row(session, "debts");
    ASSERT_GE(debts, 0);
    EXPECT_EQ("owed: nine hundred", page.rows[static_cast<std::size_t>(debts)]
                                        .note);
}

TEST_F(LedgerBookTest, coin_ritual_resolves_oldest_first_one_per_visit)
{
    complete_levels(1, 5);
    save_.scen_num = 6;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "coin").kind);
    // The oldest unresolved coin is level 2's ferry pay.
    EXPECT_EQ("One coin in the ferry pay came warm.", session.page().lines[0]);
    EXPECT_EQ("Coins waiting: four.", session.page().lines.back());

    // KEEP writes bit 2 and the page moves to level 3's coin.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "coin_keep").kind);
    EXPECT_EQ("Written. One coin, kept, warm.", session.take_message());
    EXPECT_EQ(4, state("coin_kept"));
    EXPECT_EQ(0, state("coin_spent"));
    EXPECT_EQ("The bell silver was cold. One was not.",
              session.page().lines[0]);
    EXPECT_EQ("Coins waiting: three.", session.page().lines.back());

    // PASS writes bit 3 into the spent mask and pays one-fifty.
    const std::uint32_t before = save_.m_totalcash[0];
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "coin_pass").kind);
    EXPECT_EQ("Written. It spent high, as they do.", session.take_message());
    EXPECT_EQ(4, state("coin_kept"));
    EXPECT_EQ(8, state("coin_spent"));
    EXPECT_EQ(before + 150u, save_.m_totalcash[0]);
    EXPECT_EQ("He paid on the spot, in the warm coin.",
              session.page().lines[0]);
}

TEST_F(LedgerBookTest, square_coin_book_lists_the_recap_and_no_actions)
{
    complete_levels(1, 3);
    ASSERT_TRUE(save_.campaign_state_set("longseason", "coin_kept", 4));
    ASSERT_TRUE(save_.campaign_state_set("longseason", "coin_spent", 8));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "coin").kind);
    const CampaignPickerSession::DecoratedPage& page = session.page();
    ASSERT_EQ(2u, page.lines.size());
    EXPECT_EQ("No coin waits in the book.", page.lines[0]);
    EXPECT_EQ("Kept: one. Passed on: one.", page.lines[1]);
    EXPECT_TRUE(page.rows.empty()) << "no coin, no ritual actions";
}

TEST_F(LedgerBookTest, coin_page_teases_the_carried_at_the_mint)
{
    complete_levels(1, 17);
    save_.scen_num = 18;
    // Four kept (bits 2..5), the rest unresolved so the ritual still shows.
    ASSERT_TRUE(save_.campaign_state_set("longseason", "coin_kept", 60));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "coin").kind);
    const CampaignPickerSession::DecoratedPage& page = session.page();
    ASSERT_EQ(5u, page.lines.size());
    EXPECT_EQ("You carried bones all year. At the", page.lines[2]);
    EXPECT_EQ("mint, carried bones stand up.", page.lines[3]);
}

TEST_F(LedgerBookTest, work_docket_lists_the_accessible_set_with_notes)
{
    complete_levels(1, 2);
    save_.scen_num = 4;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "work").kind);
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("THE SEASON'S WORK", page.title);
    ASSERT_EQ(1u, page.lines.size());
    EXPECT_EQ("Optional work pays. It also costs.", page.lines[0]);
    // Accessible: 1 (first), 2 (completed), 3 + 4 (exits of 2), cursor 4.
    ASSERT_EQ(4u, page.rows.size());
    EXPECT_EQ("1", page.rows[0].id);
    EXPECT_EQ("decline, go back", page.rows[0].note);
    EXPECT_TRUE(page.rows[0].cleared);
    EXPECT_EQ("2", page.rows[1].id);
    EXPECT_EQ("decline, go back", page.rows[1].note);
    EXPECT_EQ("3", page.rows[2].id);
    EXPECT_EQ("optional work", page.rows[2].note);
    EXPECT_EQ("4", page.rows[3].id);
    EXPECT_EQ("the assessor rides", page.rows[3].note);
    EXPECT_TRUE(page.rows[3].current);
    // Level rows carry the shipped titles (C++ fills missing labels).
    EXPECT_EQ("Mud Pay", page.rows[0].label);
    // Choosing a row is the SET LEVEL outcome; the session writes nothing.
    const CampaignPickerSession::Outcome outcome = session.choose(2);
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::SetLevel, outcome.kind);
    EXPECT_EQ(3, outcome.level);
    EXPECT_EQ(4, save_.scen_num);
}

TEST_F(LedgerBookTest, work_docket_type_notes_and_the_years_turn)
{
    complete_levels(1, 18);
    save_.scen_num = 19;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "work").kind);
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("The season's work, as the book has it.", page.lines[0]);
    // All nineteen rows are open (everything completed + settlement).
    ASSERT_EQ(19u, page.rows.size());
    EXPECT_EQ("the year turns", page.rows[0].note) << "19->1, in the fiction";
    EXPECT_EQ("decline, go back", page.rows[1].note);
    // The cursor row keeps its own kind of work.
    EXPECT_EQ("kill work", page.rows[18].note);
    EXPECT_TRUE(page.rows[18].current);
}

TEST_F(LedgerBookTest, work_docket_forward_notes_by_type)
{
    save_.scen_num = 1;
    complete_levels(1, 1);
    save_.scen_num = 2;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "work").kind);
    // Accessible: 1 (completed), 2 (cursor + exit of 1).
    const CampaignPickerSession::DecoratedPage& page = session.page();
    ASSERT_EQ(2u, page.rows.size());
    EXPECT_EQ("hold work", page.rows[1].note) << "the ferry is a defense";
}

TEST_F(LedgerBookTest, stores_encode_the_addressed_crate)
{
    save_.scen_num = 7;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "stores").kind);
    ASSERT_EQ(4u, session.page().lines.size())
        << "no crate on the wagon yet";
    EXPECT_EQ("All of it weighed. None of it free.", session.page().lines[0]);

    // MEAL: engine debits the 150g cost, the action addresses the crate.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_meal").kind);
    EXPECT_EQ("Four meals, crated and addressed.", session.take_message());
    EXPECT_EQ(1 + 8 * 7, state("provisions"));
    EXPECT_EQ(5000u - 150u, save_.m_totalcash[0]);
    ASSERT_EQ(5u, session.page().lines.size());
    EXPECT_EQ("One crate rides the wagon already.", session.page().lines[4]);

    // GOOD then STRONG overwrite the address — one crate, one wagon.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_good").kind);
    EXPECT_EQ("Eight meals, packed and addressed.", session.take_message());
    EXPECT_EQ(2 + 8 * 7, state("provisions"));
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_strong").kind);
    EXPECT_EQ("Two of everything. Weighed twice.", session.take_message());
    EXPECT_EQ(3 + 8 * 7, state("provisions"));
    EXPECT_EQ(5000u - 150u - 400u - 600u, save_.m_totalcash[0]);
}

TEST_F(LedgerBookTest, crew_round_shows_until_the_fair_or_the_purchase)
{
    save_.scen_num = 5;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "stores").kind);
    ASSERT_TRUE(has_row(session, "buy_round"));
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_round").kind);
    EXPECT_EQ("The crew drinks to the fair.", session.take_message());
    EXPECT_EQ(1, state("fair_round"));
    EXPECT_EQ(5000u - 200u, save_.m_totalcash[0]);
    EXPECT_FALSE(has_row(session, "buy_round"))
        << "one round; the entry re-derives away";

    // Completed fair hides it too (a squared book, level 9 in the ledger).
    ASSERT_TRUE(save_.campaign_state_set("longseason", "fair_round", 0));
    save_.add_level_completed("longseason", 9);
    save_.scen_num = 10;
    CampaignPickerSession after_fair(save_);
    ASSERT_TRUE(after_fair.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(after_fair, "stores").kind);
    EXPECT_FALSE(has_row(after_fair, "buy_round"))
        << "the fair is over; goodwill cannot be paid backward";
}

TEST_F(LedgerBookTest, advance_and_settle_are_mutually_exclusive)
{
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "debts").kind);
    ASSERT_TRUE(has_row(session, "take_advance"));
    EXPECT_FALSE(has_row(session, "settle_book"));
    ASSERT_EQ(3u, session.page().lines.size());

    // TAKE: 700 in the purse, 900 in the book.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "take_advance").kind);
    EXPECT_EQ("Seven hundred, counted warm.", session.take_message());
    EXPECT_EQ(5700u, save_.m_totalcash[0]);
    EXPECT_EQ(900, state("advance_debt"));
    EXPECT_FALSE(has_row(session, "take_advance"))
        << "one advance outstanding at a time";
    ASSERT_TRUE(has_row(session, "settle_book"));
    ASSERT_EQ(4u, session.page().lines.size());
    EXPECT_EQ("Owed: nine hundred. The season knows.", session.page().lines[3]);

    // SETTLE: the engine debits the 900g cost, the action clears the book.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "settle_book").kind);
    EXPECT_EQ("The page is squared.", session.take_message());
    EXPECT_EQ(5700u - 900u, save_.m_totalcash[0]);
    EXPECT_EQ(0, state("advance_debt"));
    ASSERT_TRUE(has_row(session, "take_advance"));
    EXPECT_FALSE(has_row(session, "settle_book"));
}

TEST_F(LedgerBookTest, winter_debt_warns_of_the_collectors)
{
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    save_.scen_num = 14;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "debts").kind);
    ASSERT_EQ(5u, session.page().lines.size());
    EXPECT_EQ("Collectors walk the toll road.", session.page().lines[4]);
}

TEST_F(LedgerBookTest, draw_your_pay_settles_once_and_docks_the_debt)
{
    complete_levels(1, 10);
    save_.scen_num = 19;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    EXPECT_EQ("Settlement Day. Square the book.",
              session.page().lines.back());
    ASSERT_TRUE(has_row(session, "draw_pay"));

    // Ten levels at 100g less the 900 owed: one hundred, once.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "draw_pay").kind);
    EXPECT_EQ("The book closes. The book keeps.", session.take_message());
    EXPECT_EQ(5100u, save_.m_totalcash[0]);
    EXPECT_EQ(0, state("advance_debt"));
    EXPECT_EQ(1, state("settled"));
    EXPECT_FALSE(has_row(session, "draw_pay")) << "the latch holds";
    EXPECT_EQ("New season. Same book.", session.page().lines[0]);

    // Not farmable: a direct dispatch cannot re-open the payout.
    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("draw_pay", result));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ("The book is closed.", result.message);
    EXPECT_EQ(5100u, save_.m_totalcash[0]) << "no second payout";
    EXPECT_EQ(1, state("settled"));
}

TEST_F(LedgerBookTest, draw_your_pay_never_pays_negative)
{
    complete_levels(1, 5);
    save_.scen_num = 19;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "draw_pay").kind);
    // 500 earned against 900 owed: the payout floors at zero, the debt
    // still clears, the latch still sets.
    EXPECT_EQ(5000u, save_.m_totalcash[0]);
    EXPECT_EQ(0, state("advance_debt"));
    EXPECT_EQ(1, state("settled"));
}

TEST_F(LedgerBookTest, draw_your_pay_refuses_off_settlement_day)
{
    // The entry only exists on cursor 19; a direct dispatch elsewhere is
    // answered with the closed book.
    save_.scen_num = 7;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    EXPECT_FALSE(has_row(session, "draw_pay"));
    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("draw_pay", result));
    EXPECT_EQ("The book is closed.", result.message);
    EXPECT_EQ(5000u, save_.m_totalcash[0]);
    EXPECT_EQ(0, state("settled"));
}

TEST_F(LedgerBookTest, coin_actions_refuse_when_no_coin_waits)
{
    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("coin_keep", result));
    EXPECT_EQ("No coin waits.", result.message);
    ASSERT_TRUE(hooks::campaign_picker_action("coin_pass", result));
    EXPECT_EQ("No coin waits.", result.message);
    EXPECT_EQ(0, state("coin_kept"));
    EXPECT_EQ(0, state("coin_spent"));
    EXPECT_EQ(5000u, save_.m_totalcash[0]) << "no coin, no pay";

    // An unknown entry id is a silent no-op (nil return, no toast).
    ASSERT_TRUE(hooks::campaign_picker_action("no_such_entry", result));
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.message.empty());
}

TEST_F(LedgerBookTest, kettle_gag_escalates_and_caps_at_three)
{
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "ask").kind);
    EXPECT_EQ("Kettle did not look up.", session.take_message());
    EXPECT_EQ(1, state("kettle_asked"));
    {
        const int ask = find_row(session, "ask");
        ASSERT_GE(ask, 0);
        EXPECT_EQ("he did not look up",
                  session.page().rows[static_cast<std::size_t>(ask)].note);
    }
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "ask").kind);
    EXPECT_EQ("Kettle looked up. Briefly.", session.take_message());
    EXPECT_EQ(2, state("kettle_asked"));
    {
        const int ask = find_row(session, "ask");
        ASSERT_GE(ask, 0);
        EXPECT_EQ("he looked up",
                  session.page().rows[static_cast<std::size_t>(ask)].note);
    }
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "ask").kind);
    EXPECT_EQ("It is not for sale. It is the company.",
              session.take_message());
    EXPECT_EQ(3, state("kettle_asked"));
    {
        const int ask = find_row(session, "ask");
        ASSERT_GE(ask, 0);
        EXPECT_EQ("not for sale",
                  session.page().rows[static_cast<std::size_t>(ask)].note);
    }
    // The counter caps; the thesis stays the answer.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "ask").kind);
    EXPECT_EQ("It is not for sale. It is the company.",
              session.take_message());
    EXPECT_EQ(3, state("kettle_asked"));
}

TEST_F(LedgerBookTest, unknown_page_id_answers_no_page)
{
    ASSERT_TRUE(hooks::campaign_picker_registered());
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("no_such_page", page))
        << "picker_menu must answer nil for a page the book never wrote";

    // A cursor past the season table falls back to the settlement line.
    save_.scen_num = 99;
    ASSERT_TRUE(hooks::campaign_picker_page("", page));
    ASSERT_FALSE(page.lines.empty());
    EXPECT_EQ("Settlement Day. Square the book.", page.lines.back());
}

// A failed spawn (no entity factory wired) is tolerated by every
// consequence: the nil-guarded helpers return quietly, no script error.
TEST_F(LedgerCampaignTest, consequences_tolerate_a_failed_spawn)
{
    LoadedLedgerLevel fx(14);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.entity_factory = {};
    const std::size_t obs_before = world.oblist.size();
    const std::size_t fx_before = world.fxlist.size();
    world.campaign_vars.emplace_back("advance_debt", 900);
    world.campaign_vars.emplace_back("provisions", 3 + 8 * 14);
    world.tick();
    EXPECT_EQ(obs_before, world.oblist.size()) << "nothing can spawn";
    EXPECT_EQ(fx_before, world.fxlist.size()) << "nothing can spawn";
    for (const og::script::ScriptError& e : world.scripts().host().errors())
        ADD_FAILURE() << e.where << ": " << e.message;
}

TEST_F(LedgerBookTest, unaffordable_settle_refuses_without_dispatch)
{
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    save_.m_totalcash[0] = 899;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open());
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::OpenedPage,
              choose_row(session, "debts").kind);
    const int settle = find_row(session, "settle_book");
    ASSERT_GE(settle, 0);
    EXPECT_FALSE(session.page().rows[static_cast<std::size_t>(settle)]
                     .affordable);
    const CampaignPickerSession::Outcome outcome =
        session.choose(static_cast<std::size_t>(settle));
    EXPECT_EQ(CampaignPickerSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ(899u, save_.m_totalcash[0]) << "a refusal must not debit";
    EXPECT_EQ(900, state("advance_debt")) << "a refusal must not settle";
}
