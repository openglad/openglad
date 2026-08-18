/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Kettle's Book (campaigns/longseason/packs/longseason.ledger): the
// campaign-script pack for The Long Season, whose ledger IS the Base Camp.
// Pins: registration, the camp's composition budgets in every book state
// AND the band each widget actually draws (a row past its band pages, and
// a paged decision is a decision the player never sees), the ledger_data
// graph/tile/protected-bit mirror against the SHIPPED package (the
// regeneration tripwire), var==0 byte-identity on every consequence level,
// taken-path spawn censuses, the Collector approach-lane smoke, and the
// full decision matrix — the zone compositions over CampaignZoneSession,
// Kettle's Stores over CampaignPickerSession.

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
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using og::ui::CampaignPickerSession;
using og::ui::CampaignZoneSession;
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

    // Writes the KEPT bit of every coin-bearing level in [first, last], so a
    // book with completed work can still be square (no ritual waiting).
    void bank_coins(int first, int last)
    {
        std::int32_t kept = save_.campaign_state_get("longseason",
                                                     "coin_kept");
        for (int lvl = std::max(first, 2); lvl <= std::min(last, 18); ++lvl)
            kept += 1 << lvl;
        ASSERT_TRUE(save_.campaign_state_set("longseason", "coin_kept",
                                             kept));
    }

    // The camp composition over the REAL provider glue, fetched fresh (the
    // surface refetches on entry and after every own mutation).
    CampaignZoneSession& camp()
    {
        zone_ = std::make_unique<CampaignZoneSession>(save_);
        zone_->fetch();
        return *zone_;
    }

    // The docket is always the LAST actions widget: when the warm-coin
    // ritual shows at all, it leads the composition.
    static const CampaignZoneSession::ActionsLayout& docket(
        const CampaignZoneSession& zone)
    {
        return zone.actions().back();
    }

    static int find_camp_row(const CampaignZoneSession::ActionsLayout& widget,
                             const std::string& id)
    {
        for (std::size_t i = 0; i < widget.rows.size(); ++i)
            if (widget.rows[i].id == id)
                return static_cast<int>(i);
        return -1;
    }

    static bool has_camp_row(const CampaignZoneSession::ActionsLayout& widget,
                             const std::string& id)
    {
        return find_camp_row(widget, id) >= 0;
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
    std::unique_ptr<CampaignZoneSession> zone_;
};

}  // namespace

// ===========================================================================
// Registration + composition budgets
// ===========================================================================

TEST_F(LedgerBookTest, pack_registers_the_camp_the_room_and_the_four_vars)
{
    ASSERT_TRUE(hooks::campaign_picker_registered());
    ASSERT_TRUE(hooks::campaign_zone_registered())
        << "the ledger IS the camp: the pack must compose a base_camp";
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

namespace {

// The five faces the camp morphs through, each with the book state that
// produces it. `coin_waiting` leaves the completed levels' coins
// unresolved (the ritual interrupts); every other state banks them, so the
// camp shows its ordinary composition.
struct BookState
{
    const char* name;
    int cursor;
    int completed_to;
    bool debt;
    bool settled;
    bool coin_waiting;
    // What the composition must lay out to: widget count, the roster rows
    // left over after the stanza and the action rows have taken theirs, and
    // the action widget's own band — which IS the number of rows it shows.
    int widgets;
    int roster_rows;
    int action_units;
    // The one row this state is allowed to push past its band, beyond the
    // optional contracts every state may page.
    const char* may_page;
};

const BookState kBookStates[] = {
    // One stanza line: text 1 + docket 3, so the job, the money row and the
    // shop door are all on the screen at once.
    {"ordinary week", 6, 5, false, false, false, 4, 3, 3, nullptr},
    // The ritual IS the camp while a coin waits: stanza 2, two rows, no
    // docket at all until the coin is written.
    {"coin waiting", 6, 5, false, false, true, 4, 3, 2, nullptr},
    // Owing before the Toll: the DEBT cell and the SETTLE row say it all,
    // so the stanza stays one line and the docket keeps its three.
    {"debt outstanding", 14, 13, true, false, false, 4, 3, 3, nullptr},
    // Settlement Day is the one state that spends the docket's third unit
    // on prose: the year's arithmetic runs before an irreversible one-time
    // payout, and the shop door — not a decision — pays for it.
    {"settlement day", 19, 18, true, false, false, 4, 3, 2, "stores"},
    // "New season. Same book." plus the season line: two lines, and a
    // closed book has no money row to fit under them.
    {"new season", 19, 19, false, true, false, 4, 3, 2, nullptr},
};

void apply_book_state(SaveData& book, const BookState& s)
{
    book.current_campaign = "longseason";
    book.my_team = 0;
    book.scen_num = static_cast<short>(s.cursor);
    book.m_totalcash[0] = 5000;
    for (int lvl = 1; lvl <= s.completed_to; ++lvl)
        book.add_level_completed("longseason", lvl);
    if (!s.coin_waiting)
    {
        std::int32_t kept = 0;
        for (int lvl = 2; lvl <= std::min(s.completed_to, 18); ++lvl)
            kept += 1 << lvl;
        (void)book.campaign_state_set("longseason", "coin_kept", kept);
    }
    if (s.debt)
        (void)book.campaign_state_set("longseason", "advance_debt", 900);
    if (s.settled)
        (void)book.campaign_state_set("longseason", "settled", 1);
    (void)book.campaign_state_set("longseason", "kettle_asked", 2);
    (void)book.campaign_state_set("longseason", "provisions",
                                  3 + 8 * s.cursor);
}

}  // namespace

// The composition budgets, swept over every camp face: the widget caps and
// the 16-char readout cells the panel draws, the 24/20 row budgets, the
// 38-char line budget — and the LAYOUT, which must leave the roster at
// least three rows in every state (the ritual's own adjudicated floor) AND
// keep every decision row inside its widget's band. A band shorter than its
// row list pages, and a paged decision is a decision the player never sees:
// the money row and the shop door must be inside the window everywhere, and
// the only rows allowed to fall off the end are optional contracts.
TEST_F(LedgerBookTest, every_camp_state_fits_the_zone_budgets)
{
    for (const BookState& s : kBookStates)
    {
        SaveData book;
        apply_book_state(book, s);
        hooks::clear_campaign_providers();
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(book));

        hooks::CampaignZone raw;
        ASSERT_TRUE(hooks::campaign_zone(raw)) << s.name;
        EXPECT_EQ(static_cast<std::size_t>(s.widgets), raw.widgets.size())
            << s.name << ": the camp stays inside its adjudicated ceiling "
                         "of five widgets";
        int rosters = 0;
        int readouts = 0;
        int action_widgets = 0;
        int texts = 0;
        int action_rows = 0;
        for (const hooks::CampaignZoneWidget& widget : raw.widgets)
        {
            switch (widget.kind)
            {
                case hooks::CampaignZoneWidget::Kind::Roster: rosters++; break;
                case hooks::CampaignZoneWidget::Kind::Readout:
                    readouts++;
                    break;
                case hooks::CampaignZoneWidget::Kind::Actions:
                    action_widgets++;
                    action_rows += static_cast<int>(widget.entries.size());
                    break;
                case hooks::CampaignZoneWidget::Kind::Text: texts++; break;
            }
            EXPECT_LE(widget.lines.size(), 6u) << s.name;
            for (const std::string& line : widget.lines)
                EXPECT_LE(line.size(), 38u) << s.name << ": '" << line << "'";
            for (const hooks::CampaignPageEntry& entry : widget.entries)
            {
                EXPECT_LE(entry.label.size(), 24u)
                    << s.name << ": '" << entry.label << "'";
                EXPECT_LE(entry.note.size(), 20u)
                    << s.name << ": '" << entry.note << "'";
            }
            for (const hooks::CampaignZoneWidget::ReadoutItem& item :
                 widget.items)
            {
                // The panel's cell budget: label + space + value.
                EXPECT_LE(item.label.size() + 1 + item.value.size(), 16u)
                    << s.name << ": '" << item.label << " " << item.value
                    << "'";
            }
        }
        EXPECT_EQ(1, rosters) << s.name;
        EXPECT_EQ(1, readouts) << s.name;
        EXPECT_LE(action_widgets, 2) << s.name;
        EXPECT_EQ(1, texts) << s.name << ": one stanza, never two";
        EXPECT_LE(action_rows, 16) << s.name;

        // The layout the surface actually renders.
        CampaignZoneSession zone(book);
        zone.fetch();
        ASSERT_TRUE(zone.scripted()) << s.name << ": the camp must lay out";
        EXPECT_EQ(s.roster_rows, zone.roster().rows_per_page)
            << s.name << ": the roster never drops under three rows";
        ASSERT_NE(nullptr, zone.readout()) << s.name;
        EXPECT_TRUE(zone.readout()->in_header_band)
            << s.name << ": the readout heads the panel";

        // The window the surface actually draws. A band shorter than its
        // row list pages, so every row that carries a decision has to be
        // inside it: what falls off may only ever be an optional contract
        // (extra work) or the one row this state declares it can spare.
        ASSERT_EQ(1u, zone.actions().size()) << s.name;
        const CampaignZoneSession::ActionsLayout& band = zone.actions()[0];
        EXPECT_EQ(s.action_units, band.units) << s.name;
        EXPECT_EQ(0, band.page.first_index())
            << s.name << ": the camp opens on its first page";
        for (int i = band.page.end_index();
             i < static_cast<int>(band.rows.size()); ++i)
        {
            const CampaignZoneSession::Row& off =
                band.rows[static_cast<std::size_t>(i)];
            const bool spared =
                s.may_page != nullptr && off.id == s.may_page;
            EXPECT_TRUE(spared || off.note == "optional, pays extra")
                << s.name << ": '" << off.label
                << "' paged off the camp — only contracts may";
        }

        // The one surviving room keeps the page budgets.
        hooks::CampaignPage page;
        ASSERT_TRUE(hooks::campaign_picker_page("stores", page)) << s.name;
        EXPECT_FALSE(page.title.empty()) << s.name;
        EXPECT_LE(page.lines.size(), 6u) << s.name;
        for (const std::string& line : page.lines)
            EXPECT_LE(line.size(), 38u) << s.name << ": '" << line << "'";
        EXPECT_LE(page.entries.size(), 24u) << s.name;
        for (const hooks::CampaignPageEntry& entry : page.entries)
        {
            EXPECT_LE(entry.label.size(), 24u)
                << s.name << ": '" << entry.label << "'";
            EXPECT_LE(entry.note.size(), 20u)
                << s.name << ": '" << entry.note << "'";
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
local prot = "PROTECTED"
for lvl = 1, D.LEVEL_COUNT do
  if D.PROTECTED_NOTE[lvl] ~= nil then
    prot = prot .. " " .. lvl
  end
end
og.log(prot)
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
    std::vector<int> protectees;
    bool protectees_seen = false;
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
        else if (line.rfind("PROTECTED", 0) == 0)
        {
            protectees = parse_ints(line.substr(9));
            protectees_seen = true;
        }
    }
    ASSERT_TRUE(protectees_seen);
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

        // The docket's lose-condition note is a mirror too: it exists on
        // exactly the levels whose SHIPPED package flags a protected NPC
        // (the SAVE_ALL watch). A mapgen change that moved a protectee
        // would otherwise leave the camp stating the wrong lose condition
        // — or none at all — with no test to catch it.
        const bool flagged = world.has_save_all_protected();
        const bool noted =
            std::find(protectees.begin(), protectees.end(), id) !=
            protectees.end();
        EXPECT_EQ(flagged, noted)
            << "level " << id << ": PROTECTED_NOTE drifted from the "
                                 "package's protected bit";

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

    // The count-word clamps answer their floor and ceiling words. The
    // ceiling is NINETEEN: the settlement stanza counts jobs (all nineteen
    // levels), not the seventeen coin-bearing ones.
    bool word_clamp_seen = false;
    for (const std::string& line :
         og::script::active_world_scripts().host().log())
        if (line == "WORDCLAMP none nineteen")
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
// The camp: the ledger open on the table (CampaignZoneSession over the real
// pack). Every state's readout, stanza and docket, and the acts that move
// between them.
// ===========================================================================

TEST_F(LedgerBookTest, ordinary_week_reads_like_the_open_ledger)
{
    complete_levels(1, 2);
    bank_coins(1, 2);
    save_.scen_num = 4;
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());

    // Three header cells, hoisted into the panel's heading band: what the
    // book alone counts. The purse is NOT among them — the panel's GOLD
    // cell and the terminals' COMPANY strip are engine-owned and on the
    // same screen, and two words for one purse only ask whether they are
    // two pots.
    ASSERT_NE(nullptr, zone.readout());
    EXPECT_TRUE(zone.readout()->in_header_band);
    ASSERT_EQ(3u, zone.readout()->items.size());
    EXPECT_EQ("JOBS", zone.readout()->items[0].label);
    EXPECT_EQ("2 done", zone.readout()->items[0].value);
    EXPECT_EQ("DEBT", zone.readout()->items[1].label);
    EXPECT_EQ("none", zone.readout()->items[1].value);
    EXPECT_EQ("COINS", zone.readout()->items[2].label);
    EXPECT_EQ("1 kept", zone.readout()->items[2].value);
    for (const hooks::CampaignZoneWidget::ReadoutItem& item :
         zone.readout()->items)
    {
        EXPECT_EQ(std::string::npos, item.value.find("5000"))
            << "the camp must not restate the engine's own GOLD cell";
    }

    // One stanza line: the season under the cursor.
    ASSERT_EQ(1u, zone.texts().size());
    ASSERT_EQ(1u, zone.texts()[0].lines.size());
    EXPECT_EQ("Spring. The mud pays first.", zone.texts()[0].lines[0]);
    EXPECT_EQ(1, zone.texts()[0].units);

    // The docket, in the order the band shows it: the job in front of you
    // (with its stake ON the row), the book's money row, the shop door —
    // and only then the open contract, the one row allowed to page.
    ASSERT_EQ(1u, zone.actions().size());
    const std::vector<CampaignZoneSession::Row>& rows = docket(zone).rows;
    ASSERT_EQ(4u, rows.size());
    EXPECT_EQ("4", rows[0].id);
    EXPECT_EQ("The Assessor", rows[0].label) << "the engine fills level labels";
    EXPECT_EQ("he must not fall", rows[0].note) << "the escort's lose "
                                                   "condition, before GO";
    EXPECT_TRUE(rows[0].current);
    EXPECT_TRUE(rows[0].is_level());
    EXPECT_EQ("take_advance", rows[1].id);
    EXPECT_EQ("700 now, 900 at Toll", rows[1].note)
        << "grant, debt and deadline, before signing";
    EXPECT_EQ("stores", rows[2].id);
    EXPECT_EQ("KETTLE'S STORES", rows[2].label);
    EXPECT_EQ("crates for this job", rows[2].note);
    EXPECT_EQ(CampaignPickerSession::Kind::Page, rows[2].kind);
    EXPECT_EQ("3", rows[3].id);
    EXPECT_EQ("optional, pays extra", rows[3].note);

    // The window the panel draws: the campaign's headline decision and the
    // shop door are ON the screen, and the contract is what pages.
    EXPECT_EQ(3, docket(zone).units);
    EXPECT_EQ(0, docket(zone).page.first_index());
    EXPECT_EQ(3, docket(zone).page.end_index());
    EXPECT_TRUE(docket(zone).page.multi_page()) << "and the pager says so";

    // The company keeps every capability: Long Season adds no locks, no
    // oath column, no retired affordance.
    EXPECT_TRUE(zone.roster().can_deploy);
    EXPECT_TRUE(zone.roster().can_train);
    EXPECT_TRUE(zone.roster().can_reorder);
    EXPECT_TRUE(zone.roster().can_team);
    EXPECT_TRUE(zone.roster().can_hire);
    EXPECT_TRUE(zone.roster().locks.empty());
    EXPECT_FALSE(zone.roster().assign.active);
    EXPECT_EQ(3, zone.roster().rows_per_page);
}

// The advance's upside belongs ON the advance, not in a stanza line that
// only shows up once the purse is already empty: a player deciding whether
// to sign reads the row, and the row has to state what signing pays.
TEST_F(LedgerBookTest, the_advance_row_states_both_sides_of_the_trade)
{
    save_.scen_num = 5;
    for (const std::uint32_t purse : {500u, 5000u})
    {
        save_.m_totalcash[0] = purse;
        CampaignZoneSession& zone = camp();
        ASSERT_TRUE(zone.scripted());
        const int advance = find_camp_row(docket(zone), "take_advance");
        ASSERT_GE(advance, 0) << purse;
        EXPECT_EQ("700 now, 900 at Toll",
                  docket(zone).rows[static_cast<std::size_t>(advance)].note)
            << "the trade does not change with the purse: " << purse;
        EXPECT_LT(advance, docket(zone).page.end_index())
            << "and it is on the screen, not behind the pager";

        // One stanza line, because the row carries the numbers now.
        ASSERT_EQ(1u, zone.texts().size());
        ASSERT_EQ(1u, zone.texts()[0].lines.size());
        EXPECT_EQ("Summer. The coin is common now.", zone.texts()[0].lines[0]);
        EXPECT_EQ(1, zone.texts()[0].units);
        EXPECT_EQ(3, zone.roster().rows_per_page);
    }
}

// What the job row says you are walking into, swept across the kinds of
// work the campaign carries. The type note is the docket's whole answer to
// "what is this job" before GO, and on the two escort levels the package
// flags it is replaced by the lose condition — a lose condition the docket
// does not state is one the player meets by losing.
TEST_F(LedgerBookTest, the_job_row_states_the_work_and_the_lose_condition)
{
    struct JobNote { int cursor; const char* note; };
    const JobNote notes[] = {
        {1, "kill work"},            // Mud Pay
        {2, "hold work"},            // Two Banners
        {4, "he must not fall"},     // The Assessor
        {13, "walk out"},            // the road to the Toll
        {14, "hold work"},           // The Long Toll, no debt owed
        {15, "the Reeve must live"}, // Wolf Winter
        {16, "walk out"},
    };
    for (const JobNote& n : notes)
    {
        save_.scen_num = static_cast<short>(n.cursor);
        CampaignZoneSession& zone = camp();
        ASSERT_TRUE(zone.scripted()) << n.cursor;
        ASSERT_FALSE(docket(zone).rows.empty()) << n.cursor;
        EXPECT_EQ(n.note, docket(zone).rows[0].note)
            << "level " << n.cursor << " states the wrong kind of work";
        EXPECT_TRUE(docket(zone).rows[0].current) << n.cursor;
    }
}

TEST_F(LedgerBookTest, a_cursor_past_the_season_table_falls_back)
{
    save_.scen_num = 99;
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.texts().size());
    EXPECT_EQ("Settlement Day. Square the book.", zone.texts()[0].lines[0]);
    // The road the campaign does not carry reads CLOSED rather than
    // pretending to be work.
    EXPECT_FALSE(docket(zone).rows[0].available);
}

// ---------------------------------------------------------------------------
// The warm-coin ritual: the one composition that interrupts.
// ---------------------------------------------------------------------------

TEST_F(LedgerBookTest, a_waiting_coin_takes_the_camp_until_it_is_written)
{
    complete_levels(1, 5);
    save_.scen_num = 6;
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());

    // The stanza: the coin's own line, why passing pays over the odds, and
    // WHERE keeping is redeemed. "1 ally per 4 kept" is a promise with no
    // place until the mint is named, and the mint is thirteen jobs away.
    ASSERT_EQ(1u, zone.texts().size());
    ASSERT_EQ(3u, zone.texts()[0].lines.size());
    EXPECT_EQ("One coin in the ferry pay came warm.",
              zone.texts()[0].lines[0]) << "the OLDEST unresolved coin";
    EXPECT_EQ("It spends high. Nobody asks why.", zone.texts()[0].lines[1]);
    EXPECT_EQ("Kept coins stand up at the mint.", zone.texts()[0].lines[2]);
    EXPECT_EQ(2, zone.texts()[0].units);

    // ONE actions widget: the ritual is the camp. It is the composition
    // that interrupts, and it interrupts properly — the book shows no work
    // at all until the coin is written, so both sides of the trade are on
    // the screen with the mint named over them.
    ASSERT_EQ(1u, zone.actions().size());
    const std::vector<CampaignZoneSession::Row>& ritual =
        zone.actions()[0].rows;
    ASSERT_EQ(2u, ritual.size());
    EXPECT_EQ("coin_keep", ritual[0].id);
    EXPECT_EQ("KEEP THIS COIN", ritual[0].label);
    EXPECT_EQ("1 ally per 4 kept", ritual[0].note)
        << "the mechanical payoff, disclosed on the row";
    EXPECT_EQ("coin_pass", ritual[1].id);
    EXPECT_EQ("PASS IT ON", ritual[1].label);
    EXPECT_EQ("150g now, none later", ritual[1].note);
    EXPECT_EQ(2, zone.actions()[0].units) << "both sides of the trade fit";
    EXPECT_FALSE(zone.actions()[0].page.multi_page());
    EXPECT_FALSE(has_camp_row(zone.actions()[0], "6"));
    EXPECT_FALSE(has_camp_row(zone.actions()[0], "stores"));
    EXPECT_EQ(3, zone.roster().rows_per_page)
        << "the ritual's cost is the roster's floor, never below it";
    EXPECT_EQ("none", zone.readout()->items[2].value) << "nothing kept yet";

    // And writing the backlog out hands the docket straight back (levels
    // 2..5 each paid a coin; four clicks square the book).
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted,
                  zone.act(0, 0).kind) << "coin " << i;
    }
    ASSERT_EQ(1u, zone.actions().size());
    EXPECT_TRUE(has_camp_row(docket(zone), "6"));
    EXPECT_TRUE(has_camp_row(docket(zone), "stores"));
    EXPECT_EQ("4 kept", zone.readout()->items[2].value);
}

TEST_F(LedgerBookTest, the_ritual_resolves_oldest_first_one_per_fetch)
{
    complete_levels(1, 5);
    save_.scen_num = 6;
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());

    // KEEP writes the level-2 bit and the ritual refetches to level 3's.
    const CampaignZoneSession::Outcome kept = zone.act(0, 0);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, kept.kind);
    EXPECT_EQ("Written. One coin, kept, warm.", zone.take_message());
    EXPECT_EQ(4, state("coin_kept"));
    EXPECT_EQ(0, state("coin_spent"));
    EXPECT_EQ("The bell silver was cold. One was not.",
              zone.texts()[0].lines[0]);
    EXPECT_EQ("1 kept", zone.readout()->items[2].value)
        << "the tally is a header cell now";

    // PASS writes the spent mask and pays one-fifty.
    const std::uint32_t before = save_.m_totalcash[0];
    const CampaignZoneSession::Outcome passed = zone.act(0, 1);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, passed.kind);
    EXPECT_EQ("Written. It spent high, as they do.", zone.take_message());
    EXPECT_EQ(8, state("coin_spent"));
    EXPECT_EQ(before + 150u, save_.m_totalcash[0]);
    EXPECT_EQ("He paid on the spot, in the warm coin.",
              zone.texts()[0].lines[0]);
}

TEST_F(LedgerBookTest, a_square_book_shows_no_ritual_at_all)
{
    complete_levels(1, 5);
    bank_coins(1, 5);
    save_.scen_num = 6;
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size()) << "no coin, no coin UI";
    EXPECT_TRUE(has_camp_row(docket(zone), "stores"));
    EXPECT_EQ("Summer. The coin is common now.", zone.texts()[0].lines[0]);
    EXPECT_EQ("4 kept", zone.readout()->items[2].value)
        << "the readout carries the tally when the ritual is quiet";
}

TEST_F(LedgerBookTest, the_ritual_teases_the_mint_when_the_payoff_is_close)
{
    complete_levels(1, 17);
    save_.scen_num = 18;
    // Four kept (bits 2..5); the rest unresolved, so a coin still waits.
    ASSERT_TRUE(save_.campaign_state_set("longseason", "coin_kept", 60));
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(3u, zone.texts()[0].lines.size());
    EXPECT_EQ("At the mint, carried bones stand up.",
              zone.texts()[0].lines[2]) << "the where-line says what is "
                                           "about to happen instead";
    EXPECT_EQ("4 kept", zone.readout()->items[2].value);
}

TEST_F(LedgerBookTest, the_mint_keeps_the_coins_line_until_the_payoff_banks)
{
    complete_levels(1, 17);
    save_.scen_num = 18;
    // Three kept (bits 2..4): one short of the first Carried, so the teaser
    // holds off and the fifth coin's own line stands.
    ASSERT_TRUE(save_.campaign_state_set("longseason", "coin_kept", 28));
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(3u, zone.texts()[0].lines.size());
    EXPECT_EQ("Both purses paid. Both came up warm.",
              zone.texts()[0].lines[0]);
    EXPECT_EQ("Kept coins stand up at the mint.", zone.texts()[0].lines[2]);
}

TEST_F(LedgerBookTest, the_keep_note_swaps_once_the_ally_cap_is_banked)
{
    complete_levels(1, 17);
    save_.scen_num = 18;
    // Eleven kept (bits 2..12): one short of the cap, the rate still pays.
    std::int32_t kept = 0;
    for (int lvl = 2; lvl <= 12; ++lvl)
        kept += 1 << lvl;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "coin_kept", kept));
    EXPECT_EQ("1 ally per 4 kept", camp().actions()[0].rows[0].note);

    // Twelve kept banks the third and last ally; a thirteenth coin buys
    // nothing but the door coin, and the row stops quoting a dead rate.
    ASSERT_TRUE(save_.campaign_state_set("longseason", "coin_kept",
                                         kept + (1 << 13)));
    CampaignZoneSession& zone = camp();
    EXPECT_EQ("a coin for the door", zone.actions()[0].rows[0].note);
    EXPECT_EQ("12 kept", zone.readout()->items[2].value);
}

// ---------------------------------------------------------------------------
// The advance, the collectors, and the settle window.
// ---------------------------------------------------------------------------

TEST_F(LedgerBookTest, the_advance_writes_the_debt_and_the_camp_says_so)
{
    save_.scen_num = 5;
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    const int advance = find_camp_row(docket(zone), "take_advance");
    ASSERT_GE(advance, 0);
    const CampaignZoneSession::Outcome outcome = zone.act(0, advance);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, outcome.kind);
    EXPECT_EQ("Seven hundred, counted warm.", zone.take_message());
    EXPECT_EQ(5700u, save_.m_totalcash[0]);
    EXPECT_EQ(900, state("advance_debt"));

    // The refetched camp: the debt cell, and the settle row in the money
    // row's own slot — second, directly under the job, on the screen.
    EXPECT_EQ("900g", zone.readout()->items[1].value);
    ASSERT_EQ(1u, zone.texts()[0].lines.size())
        << "the DEBT cell and the SETTLE row state the debt; the stanza "
           "keeps its one line and the docket keeps its three";
    EXPECT_FALSE(has_camp_row(docket(zone), "take_advance"))
        << "one advance outstanding at a time";
    const int settle = find_camp_row(docket(zone), "settle_book");
    ASSERT_EQ(1, settle);
    const std::size_t settle_row = static_cast<std::size_t>(settle);
    EXPECT_EQ("SETTLE THE BOOK", docket(zone).rows[settle_row].label);
    EXPECT_EQ("no Toll collectors", docket(zone).rows[settle_row].note);
    EXPECT_EQ(900, docket(zone).rows[settle_row].cost);
    EXPECT_LT(settle, docket(zone).page.end_index())
        << "the decision must be inside the band the panel draws";

    // And the row survives the panel's own 42-character face. The clip
    // cuts on a word boundary, so a note long enough to push the price
    // past the end takes the WHOLE price with it — leaving a purchase row
    // that quotes no price at all, the one thing it may never do.
    constexpr std::size_t kZoneRowChars = 42;  // (264px face - 8) / 6px
    const std::string drawn = og::ui::campaign_picker_row_text(
        docket(zone).rows[settle_row], kZoneRowChars);
    EXPECT_EQ(std::string::npos, drawn.find(".."))
        << "the settle row clipped: '" << drawn << "'";
    EXPECT_NE(std::string::npos, drawn.find("900g"))
        << "the settle row must quote its price: '" << drawn << "'";

    // SETTLE: the engine debits the cost, the action clears the book.
    const CampaignZoneSession::Outcome squared = zone.act(0, settle);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, squared.kind);
    EXPECT_EQ("The page is squared.", zone.take_message());
    EXPECT_EQ(5700u - 900u, save_.m_totalcash[0]);
    EXPECT_EQ(0, state("advance_debt"));
    EXPECT_TRUE(has_camp_row(docket(zone), "take_advance"));
}

// The money-row window, stated as a matrix: the book's money row — BOTH
// halves of it — is open exactly until the collectors ride. Before the Toll
// an advance can be taken and squared. Past it, settling 900g to dodge a
// 900g dock is a wash, AND an advance would quote a deadline that has
// already gone by to write a debt with no exit (the settlement pays out
// once and latches). So the row shuts whole, and the stanza picks up the
// only consequence left to state.
TEST_F(LedgerBookTest, the_money_row_is_open_exactly_until_the_collectors_ride)
{
    struct Window { const char* name; int cursor; bool toll_done; bool debt;
                    bool expect_settle; bool expect_advance;
                    const char* line; };
    const Window windows[] = {
        {"no debt, before the Toll", 13, false, false, false, true, nullptr},
        {"owing, before the Toll", 13, false, true, true, false, nullptr},
        {"owing, on Toll week", 14, false, true, true, false, nullptr},
        {"owing, past the Toll", 15, true, true, false, false,
         "Unpaid debt docks the settlement."},
        // The row that used to survive here wrote a 900g debt against a
        // deadline that had already passed, and nothing on the camp could
        // ever square it again.
        {"square, past the Toll", 15, true, false, false, false, nullptr},
    };
    for (const Window& w : windows)
    {
        SaveData book;
        book.current_campaign = "longseason";
        book.my_team = 0;
        book.scen_num = static_cast<short>(w.cursor);
        book.m_totalcash[0] = 5000;
        for (int lvl = 1; lvl < w.cursor; ++lvl)
        {
            if (lvl == 14 && !w.toll_done)
                continue;
            book.add_level_completed("longseason", lvl);
        }
        std::int32_t kept = 0;
        for (int lvl = 2; lvl <= 18; ++lvl)
            kept += 1 << lvl;
        ASSERT_TRUE(book.campaign_state_set("longseason", "coin_kept", kept))
            << w.name;
        if (w.debt)
        {
            ASSERT_TRUE(book.campaign_state_set("longseason", "advance_debt",
                                                900)) << w.name;
        }
        hooks::clear_campaign_providers();
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(book));

        CampaignZoneSession zone(book);
        zone.fetch();
        ASSERT_TRUE(zone.scripted()) << w.name;
        ASSERT_EQ(1u, zone.actions().size()) << w.name;
        EXPECT_EQ(w.expect_settle, has_camp_row(zone.actions()[0],
                                                "settle_book")) << w.name;
        EXPECT_EQ(w.expect_advance, has_camp_row(zone.actions()[0],
                                                 "take_advance")) << w.name;
        // Whichever half shows, it shows in the money row's slot — second,
        // under the job, inside the band the panel draws.
        if (w.expect_settle || w.expect_advance)
        {
            const int money = w.expect_settle
                ? find_camp_row(zone.actions()[0], "settle_book")
                : find_camp_row(zone.actions()[0], "take_advance");
            EXPECT_EQ(1, money) << w.name;
            EXPECT_LT(money, zone.actions()[0].page.end_index()) << w.name;
        }
        if (w.line != nullptr)
        {
            ASSERT_EQ(2u, zone.texts()[0].lines.size()) << w.name;
            EXPECT_EQ(w.line, zone.texts()[0].lines[1]) << w.name;
        }
        else
        {
            EXPECT_EQ(1u, zone.texts()[0].lines.size()) << w.name;
        }
        hooks::clear_campaign_providers();
        hooks::install_campaign_providers(
            og::data::make_campaign_providers(save_));
    }
}

TEST_F(LedgerBookTest, toll_week_warns_on_the_level_row_itself)
{
    complete_levels(1, 13);
    bank_coins(1, 13);
    save_.scen_num = 14;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    ASSERT_FALSE(docket(zone).rows.empty());
    EXPECT_EQ("14", docket(zone).rows[0].id);
    EXPECT_EQ("The Long Toll", docket(zone).rows[0].label);
    EXPECT_EQ("the collectors ride", docket(zone).rows[0].note);
    EXPECT_EQ("Winter. The toll road is quiet.", zone.texts()[0].lines[0]);
}

TEST_F(LedgerBookTest, an_unaffordable_settle_refuses_without_dispatch)
{
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    save_.m_totalcash[0] = 899;
    CampaignZoneSession& zone = camp();
    const int settle = find_camp_row(docket(zone), "settle_book");
    ASSERT_GE(settle, 0);
    EXPECT_FALSE(docket(zone).rows[static_cast<std::size_t>(settle)]
                     .affordable);
    const CampaignZoneSession::Outcome outcome = zone.act(0, settle);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Refused, outcome.kind);
    EXPECT_EQ(899u, save_.m_totalcash[0]) << "a refusal must not debit";
    EXPECT_EQ(900, state("advance_debt")) << "a refusal must not settle";
}

// ---------------------------------------------------------------------------
// Settlement Day and the year's turn.
// ---------------------------------------------------------------------------

TEST_F(LedgerBookTest, settlement_day_shows_the_arithmetic_then_pays_it)
{
    complete_levels(1, 18);
    bank_coins(1, 18);
    save_.scen_num = 19;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());

    // The day names itself, then the arithmetic, before the button that
    // does it. This is the one camp face that spends the docket's third
    // unit on prose: an irreversible one-time payout gets its numbers.
    ASSERT_EQ(3u, zone.texts()[0].lines.size());
    EXPECT_EQ("Settlement Day. Square the book.", zone.texts()[0].lines[0]);
    EXPECT_EQ("Eighteen jobs done, 100g the job.", zone.texts()[0].lines[1]);
    EXPECT_EQ("Debt comes off the draw: 900g.", zone.texts()[0].lines[2]);
    EXPECT_EQ(2, zone.texts()[0].units);
    EXPECT_EQ("18 done", zone.readout()->items[0].value);

    // The headline row carries the computed net, not a slogan — and it is
    // on the screen with the level row; the shop door is what pages here.
    const std::vector<CampaignZoneSession::Row>& rows = docket(zone).rows;
    ASSERT_EQ(3u, rows.size());
    EXPECT_EQ("draw_pay", rows[0].id);
    EXPECT_EQ("DRAW YOUR PAY", rows[0].label);
    EXPECT_EQ("pays 900g, once", rows[0].note) << "1800 earned less 900 owed";
    EXPECT_EQ("19", rows[1].id);
    EXPECT_EQ("Settlement Day", rows[1].label);
    EXPECT_EQ("stores", rows[2].id);
    EXPECT_EQ(2, docket(zone).page.end_index());
    EXPECT_FALSE(has_camp_row(docket(zone), "settle_book"))
        << "settling 900 to dodge a 900 dock is a wash: never offered";
    EXPECT_FALSE(has_camp_row(docket(zone), "take_advance"))
        << "nor an advance: past the Toll its deadline is already gone";

    // Drawing it: paid once, the debt cleared, the book latched shut.
    const CampaignZoneSession::Outcome outcome = zone.act(0, 0);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, outcome.kind);
    EXPECT_EQ("The book closes. The book keeps.", zone.take_message());
    EXPECT_EQ(5900u, save_.m_totalcash[0]);
    EXPECT_EQ(0, state("advance_debt"));
    EXPECT_EQ(1, state("settled"));

    // The 19-to-1 loop: same book, next spring.
    ASSERT_EQ(2u, zone.texts()[0].lines.size());
    EXPECT_EQ("New season. Same book.", zone.texts()[0].lines[0]);
    EXPECT_EQ("Spring. The mud pays first.", zone.texts()[0].lines[1]);
    EXPECT_FALSE(has_camp_row(docket(zone), "draw_pay")) << "the latch holds";
    EXPECT_EQ("1", docket(zone).rows[0].id);
    EXPECT_EQ("Mud Pay", docket(zone).rows[0].label);
    EXPECT_EQ("the year turns", docket(zone).rows[0].note);
    EXPECT_EQ("none", zone.readout()->items[1].value) << "the draw squared it";

    // Not farmable: a direct dispatch cannot re-open the payout.
    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("draw_pay", result));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ("The book is closed.", result.message);
    EXPECT_EQ(5900u, save_.m_totalcash[0]) << "no second payout";
}

// The closed book offers no advance, because there is nothing left that
// could clear the debt one would write: the Toll is behind us, so SETTLE
// never shows, and the settlement has already paid out and latched.
TEST_F(LedgerBookTest, a_closed_book_offers_no_advance_it_could_never_square)
{
    complete_levels(1, 19);
    bank_coins(1, 18);
    save_.scen_num = 19;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "settled", 1));
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(2u, zone.texts()[0].lines.size());
    EXPECT_EQ("New season. Same book.", zone.texts()[0].lines[0]);
    EXPECT_FALSE(has_camp_row(docket(zone), "take_advance"));
    EXPECT_FALSE(has_camp_row(docket(zone), "settle_book"));
    const std::vector<CampaignZoneSession::Row>& rows = docket(zone).rows;
    ASSERT_EQ(2u, rows.size());
    EXPECT_EQ("1", rows[0].id);
    EXPECT_EQ("stores", rows[1].id);
    EXPECT_FALSE(docket(zone).page.multi_page()) << "and no pager at all";

    // Nor by dispatch: the pack refuses to write the unclearable debt.
    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("take_advance", result));
    EXPECT_EQ("The book takes no more advances.", result.message);
    EXPECT_EQ(5000u, save_.m_totalcash[0]);
    EXPECT_EQ(0, state("advance_debt"));
}

// One advance at a time: a page that went stale must not be able to grant a
// second 700g against the same 900g debt.
TEST_F(LedgerBookTest, a_second_advance_over_an_outstanding_one_is_refused)
{
    save_.scen_num = 5;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("take_advance", result));
    EXPECT_EQ("One advance at a time.", result.message);
    EXPECT_EQ(5000u, save_.m_totalcash[0]);
    EXPECT_EQ(900, state("advance_debt"));
}

TEST_F(LedgerBookTest, the_draw_never_pays_negative_and_says_so_first)
{
    complete_levels(1, 5);
    bank_coins(1, 5);
    save_.scen_num = 19;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "advance_debt", 900));
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    EXPECT_EQ("Five jobs done, 100g the job.", zone.texts()[0].lines[1]);
    EXPECT_EQ("pays 0g, once", docket(zone).rows[0].note)
        << "500 earned against 900 owed: the note states the floor";
    const CampaignZoneSession::Outcome outcome = zone.act(0, 0);
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::Acted, outcome.kind);
    EXPECT_EQ(5000u, save_.m_totalcash[0]);
    EXPECT_EQ(0, state("advance_debt"));
    EXPECT_EQ(1, state("settled"));
}

TEST_F(LedgerBookTest, a_single_job_settles_in_the_singular)
{
    complete_levels(1, 1);
    save_.scen_num = 19;
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(2u, zone.texts()[0].lines.size())
        << "no debt, no kept coin: the day and one line of arithmetic";
    EXPECT_EQ("Settlement Day. Square the book.", zone.texts()[0].lines[0]);
    EXPECT_EQ("One job done, 100g the job.", zone.texts()[0].lines[1]);
    EXPECT_EQ("pays 100g, once", docket(zone).rows[0].note);
}

// Zero is a count too. The stanza's number is spelled, and the spelled
// zero is "none" — so the plain branch has to catch it before the book
// opens on "None jobs done, 100g the job."
TEST_F(LedgerBookTest, an_empty_year_settles_in_plain_english)
{
    save_.scen_num = 19;
    CampaignZoneSession& zone = camp();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(2u, zone.texts()[0].lines.size());
    EXPECT_EQ("Settlement Day. Square the book.", zone.texts()[0].lines[0]);
    EXPECT_EQ("No jobs done, 100g the job.", zone.texts()[0].lines[1]);
    EXPECT_EQ("0 done", zone.readout()->items[0].value);
    EXPECT_EQ("pays 0g, once", docket(zone).rows[0].note);
}

TEST_F(LedgerBookTest, draw_your_pay_refuses_off_settlement_day)
{
    // The row only exists on cursor 19; a direct dispatch elsewhere is
    // answered with the closed book.
    save_.scen_num = 7;
    CampaignZoneSession& zone = camp();
    EXPECT_FALSE(has_camp_row(docket(zone), "draw_pay"));
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

// ---------------------------------------------------------------------------
// Kettle's Stores: the one surviving room.
// ---------------------------------------------------------------------------

TEST_F(LedgerBookTest, the_camp_opens_the_stores_by_name)
{
    complete_levels(1, 13);
    bank_coins(1, 13);
    save_.scen_num = 14;
    CampaignZoneSession& zone = camp();
    const int door = find_camp_row(docket(zone), "stores");
    ASSERT_GE(door, 0);
    // A page row belongs to the surface's submenu, never to act().
    EXPECT_EQ(CampaignZoneSession::OutcomeKind::None,
              zone.act(0, door).kind);

    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("stores"));
    const CampaignPickerSession::DecoratedPage& page = session.page();
    EXPECT_EQ("KETTLE'S STORES", page.title);
    ASSERT_EQ(3u, page.lines.size()) << "no crate on the wagon yet";
    EXPECT_EQ("Crates land at the current job's camp.", page.lines[0]);
    EXPECT_EQ("This job: The Long Toll.", page.lines[1])
        << "the destination is named before the price";
    EXPECT_EQ("The kettle is not for sale.", page.lines[2]);

    ASSERT_EQ(4u, page.rows.size()) << "the fair is past: no round on offer";
    EXPECT_EQ("buy_meal", page.rows[0].id);
    EXPECT_EQ("MEAL FOR THE ROAD", page.rows[0].label);
    EXPECT_EQ("4 meals at the job", page.rows[0].note);
    EXPECT_EQ(150, page.rows[0].cost);
    EXPECT_EQ("buy_good", page.rows[1].id);
    EXPECT_EQ("8 meals at the job", page.rows[1].note);
    EXPECT_EQ("buy_strong", page.rows[2].id);
    EXPECT_EQ("8 meals + 2 silver", page.rows[2].note);
    EXPECT_EQ(600, page.rows[2].cost);
    EXPECT_EQ("ask", page.rows[3].id) << "the gag is the shop's last row";
    EXPECT_EQ(0, page.rows[3].cost) << "and it is free";
}

TEST_F(LedgerBookTest, stores_address_the_crate_and_state_the_wagon)
{
    save_.scen_num = 7;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("stores"));

    // MEAL: engine debits the 150g cost, the action addresses the crate.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_meal").kind);
    EXPECT_EQ("Four meals, crated and addressed.", session.take_message());
    EXPECT_EQ(1 + 8 * 7, state("provisions"));
    EXPECT_EQ(5000u - 150u, save_.m_totalcash[0]);
    // Six lines: the whole page budget, with the fair still ahead.
    ASSERT_EQ(6u, session.page().lines.size());
    EXPECT_EQ("On the wagon: 4 meals.", session.page().lines[2]);
    EXPECT_EQ("Buying again re-addresses the wagon.", session.page().lines[3]);

    // GOOD then STRONG overwrite the address — one crate, one wagon.
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_good").kind);
    EXPECT_EQ("Eight meals, packed and addressed.", session.take_message());
    EXPECT_EQ(2 + 8 * 7, state("provisions"));
    EXPECT_EQ("On the wagon: 8 meals.", session.page().lines[2]);
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_strong").kind);
    EXPECT_EQ("Two of everything. Weighed twice.", session.take_message());
    EXPECT_EQ(3 + 8 * 7, state("provisions"));
    EXPECT_EQ(5000u - 150u - 400u - 600u, save_.m_totalcash[0]);
    EXPECT_EQ("On the wagon: 8 meals, 2 silver.", session.page().lines[2]);
}

// A crate bought for one job and then left behind must not read as this
// job's supply: the page names where the wagon is actually going.
TEST_F(LedgerBookTest, a_crate_addressed_elsewhere_names_where_it_goes)
{
    save_.scen_num = 7;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "provisions",
                                         2 + 8 * 5));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("stores"));
    ASSERT_EQ(6u, session.page().lines.size());
    EXPECT_EQ("This job: Grey Tolls.", session.page().lines[1]);
    EXPECT_EQ("On the wagon: 8 meals.", session.page().lines[2]);
    EXPECT_EQ("Addressed to Two Banners.", session.page().lines[3]);

    // A crate addressed to a road the book does not carry still says so.
    ASSERT_TRUE(save_.campaign_state_set("longseason", "provisions",
                                         1 + 8 * 99));
    session.refresh();
    EXPECT_EQ("Addressed to another job.", session.page().lines[3]);
}

// On a settled Settlement Day the camp points at next spring, so the shop
// must too: the crate is addressed to the job the page NAMES, never to the
// closed book's own cursor.
TEST_F(LedgerBookTest, a_settled_book_addresses_the_crate_to_next_spring)
{
    complete_levels(1, 19);
    bank_coins(1, 19);
    save_.scen_num = 19;
    ASSERT_TRUE(save_.campaign_state_set("longseason", "settled", 1));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("stores"));
    EXPECT_EQ("This job: Mud Pay.", session.page().lines[1]);
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_meal").kind);
    EXPECT_EQ(1 + 8 * 1, state("provisions"));
    EXPECT_EQ("On the wagon: 4 meals.", session.page().lines[2]);
    EXPECT_EQ("Buying again re-addresses the wagon.", session.page().lines[3]);
}

TEST_F(LedgerBookTest, a_corrupt_crate_kind_claims_no_wagon)
{
    save_.scen_num = 7;
    // provisions with a kind outside 1..3 delivers nothing (level_hooks
    // refuses it), so the page must not claim a wagon either.
    ASSERT_TRUE(save_.campaign_state_set("longseason", "provisions",
                                         4 + 8 * 7));
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("stores"));
    ASSERT_EQ(4u, session.page().lines.size());
    for (const std::string& line : session.page().lines)
        EXPECT_NE(0u, line.rfind("On the wagon", 0)) << line;
    EXPECT_EQ("The kettle is not for sale.", session.page().lines[3]);
}

TEST_F(LedgerBookTest, stores_name_no_destination_off_the_road_map)
{
    save_.scen_num = 99;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("stores"));
    ASSERT_EQ(3u, session.page().lines.size());
    EXPECT_EQ("Crates land at the current job's camp.",
              session.page().lines[0]);
    for (const std::string& line : session.page().lines)
        EXPECT_NE(0u, line.rfind("This job:", 0))
            << "a road the book does not carry has no name to print";
    EXPECT_EQ("The kettle is not for sale.", session.page().lines[2]);
}

// Every job title the campaign ships must compose inside the page budget:
// the book clips a long name, never the panel edge.
TEST_F(LedgerBookTest, every_composed_destination_line_fits_the_page)
{
    for (int lvl = 1; lvl <= 19; ++lvl)
    {
        save_.scen_num = static_cast<short>(lvl);
        CampaignPickerSession session(save_);
        ASSERT_TRUE(session.open_at("stores")) << "level " << lvl;
        ASSERT_GE(session.page().lines.size(), 2u) << "level " << lvl;
        const std::string& line = session.page().lines[1];
        EXPECT_EQ(0u, line.rfind("This job: ", 0)) << "level " << lvl;
        EXPECT_LE(line.size(), 34u) << "level " << lvl << ": '" << line << "'";
    }
}

TEST_F(LedgerBookTest, the_crew_round_shows_until_the_fair_or_the_purchase)
{
    save_.scen_num = 5;
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("stores"));
    ASSERT_TRUE(has_row(session, "buy_round"));
    ASSERT_EQ(4u, session.page().lines.size());
    EXPECT_EQ("A round buys hands on the fair door.", session.page().lines[2]);
    ASSERT_EQ(CampaignPickerSession::OutcomeKind::Acted,
              choose_row(session, "buy_round").kind);
    EXPECT_EQ("The crew drinks to the fair.", session.take_message());
    EXPECT_EQ(1, state("fair_round"));
    EXPECT_EQ(5000u - 200u, save_.m_totalcash[0]);
    EXPECT_FALSE(has_row(session, "buy_round"))
        << "one round; the entry re-derives away";
    EXPECT_EQ(3u, session.page().lines.size()) << "and so does its line";

    // Completed fair hides it too (a squared book, level 9 in the ledger).
    ASSERT_TRUE(save_.campaign_state_set("longseason", "fair_round", 0));
    save_.add_level_completed("longseason", 9);
    save_.scen_num = 10;
    CampaignPickerSession after_fair(save_);
    ASSERT_TRUE(after_fair.open_at("stores"));
    EXPECT_FALSE(has_row(after_fair, "buy_round"))
        << "the fair is over; goodwill cannot be paid backward";
}

TEST_F(LedgerBookTest, kettle_gag_escalates_and_caps_at_three)
{
    CampaignPickerSession session(save_);
    ASSERT_TRUE(session.open_at("stores"));
    {
        const int ask = find_row(session, "ask");
        ASSERT_GE(ask, 0);
        EXPECT_EQ("", session.page().rows[static_cast<std::size_t>(ask)].note)
            << "unasked kettle carries no note";
    }
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

// The four v1 pages that dissolved into the camp are GONE, not hidden: the
// book answers nil for every one of them, so a stale door cannot open a
// page nobody maintains.
TEST_F(LedgerBookTest, the_retired_pages_answer_no_page)
{
    ASSERT_TRUE(hooks::campaign_picker_registered());
    for (const char* page_id : {"", "root", "work", "coin", "debts",
                                "no_such_page"})
    {
        hooks::CampaignPage page;
        EXPECT_FALSE(hooks::campaign_picker_page(page_id, page))
            << "page '" << page_id << "' must not exist";
    }
    hooks::CampaignPage stores;
    EXPECT_TRUE(hooks::campaign_picker_page("stores", stores));
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
