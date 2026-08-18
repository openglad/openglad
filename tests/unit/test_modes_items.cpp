// The respawning-pickup suite (lib/mode_items.lua + the impl wiring), over
// the shared modes-pack fixture. Rule spec: the PR #174 playtest ruling —
// manifest item_pads drive a deterministic 30-tick-cadence respawner with
// a per-level interval (Mutant/FFA 180, soccer 180, basketball 240,
// TDM/CTF 300), a mode-var rotation cursor, and ONE spawn per firing; only
// drumstick/magic/invis/speed ever respawn (never gold), livings parked on
// a pad deny the spawn, and Onslaught stays OFF (#225 moved soccer and
// basketball off that list).
//
// Levels: 9601/9602 drive items.run directly with synthetic rows (slots
// 40/41); 840/500 bind the SHIPPED mutant/ctf registrations over the
// committed manifest rows; the real-campaign cases load the shipped
// scen300/scen820/scen824 packages end to end.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/game_context.h>
#include <openglad/resources/io_common.h>

#include "../modes_pack_fixture.h"

#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <vector>

using namespace og::modes_test;

namespace og::script {
extern std::int64_t g_test_world_instruction_budget;
}

namespace {

// The mode-var item slots of the three consuming impls (tables S).
inline constexpr int kTdmItemCursor = 17;
inline constexpr int kTdmItemLast = 18;
inline constexpr int kMutItemCursor = 49;  // band slot map, ffa-design §8
inline constexpr int kMutItemLast = 50;
[[maybe_unused]] inline constexpr int kCtfItemCursor = 62;
inline constexpr int kCtfItemLast = 63;
inline constexpr int kSoccerItemCursor = 46;
inline constexpr int kSoccerItemLast = 47;
// Basketball's pair is SPLIT: its private band was full, so the cursor
// lives in the shared header band (mode_match's MATCHED precedent) and the
// clock took the last private slot.
inline constexpr int kBballItemCursor = 7;
inline constexpr int kBballItemLast = 63;

struct ItemCensus
{
    int drum = 0;
    int magic = 0;
    int invis = 0;
    int speed = 0;
    int gold = 0;

    int respawnable() const { return drum + magic + invis + speed; }
};

ItemCensus census(GameWorld& world)
{
    ItemCensus c;
    for (const auto& uptr : world.fxlist)
    {
        const walker* fx = uptr.get();
        if (fx == nullptr || fx->dead() ||
            fx->query_order() != Order::Treasure)
        {
            continue;
        }
        switch (fx->family())
        {
            case FAMILY_DRUMSTICK: ++c.drum; break;
            case FAMILY_MAGIC_POTION: ++c.magic; break;
            case FAMILY_INVIS_POTION: ++c.invis; break;
            case FAMILY_SPEED_POTION: ++c.speed; break;
            case FAMILY_GOLD_BAR: ++c.gold; break;
            default: break;
        }
    }
    return c;
}

walker* live_item_at(GameWorld& world, int family, int x, int y)
{
    for (const auto& uptr : world.fxlist)
    {
        walker* fx = uptr.get();
        if (fx != nullptr && !fx->dead() &&
            fx->query_order() == Order::Treasure && fx->family() == family &&
            fx->xpos() == x && fx->ypos() == y)
        {
            return fx;
        }
    }
    return nullptr;
}

walker* first_live_of(GameWorld& world, int family)
{
    for (const auto& uptr : world.fxlist)
    {
        walker* fx = uptr.get();
        if (fx != nullptr && !fx->dead() &&
            fx->query_order() == Order::Treasure && fx->family() == family)
        {
            return fx;
        }
    }
    return nullptr;
}

bool vm_logged(GameWorld& world, const std::string& needle)
{
    for (const auto& line : world.scripts().host().log())
    {
        if (line.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool has_script_error(GameWorld& world, const std::string& needle)
{
    for (const auto& err : world.scripts().host().errors())
    {
        if (err.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

void expect_no_script_errors(GameWorld& world)
{
    for (const auto& err : world.scripts().host().errors())
        ADD_FAILURE() << "script error: " << err.where << ": " << err.message;
}

// Behavior digest for the determinism pin: mode vars, RNG, every ob AND
// every fx (spawned items live in the fx list — the mutant suite's
// digest walks the oblist only, so items need their own).
std::string digest_world(GameWorld& world)
{
    std::string digest = std::format("rng={} tick={}|", world.rng_.state_,
                                     world.tick_count_);
    for (int i = 0; i < og::sim::kModeVarCount; ++i)
        digest +=
            std::format("{},", world.mode.vars[static_cast<std::size_t>(i)]);
    auto walk = [&digest](const GameWorld::EntityList& list) {
        for (const auto& uptr : list)
        {
            const walker* w = uptr.get();
            if (w == nullptr)
                continue;
            digest += std::format("[{} f={} x={} y={} d={}]", w->entity_id(),
                                  w->family(), w->xpos(), w->ypos(),
                                  w->dead() ? 1 : 0);
        }
    };
    walk(world.oblist);
    walk(world.fxlist);
    return digest;
}

}  // namespace

using ModesItems = ModesPackTest;

// ===========================================================================
// items.run over synthetic rows (level 9601, slots 40/41, interval 60)
// ===========================================================================

TEST_F(ModesItems, deficit_spawns_one_item_per_firing_at_cadence_boundaries)
{
    ModesCtfWorld fx(kItemsLevelA);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active) << "the items probe init must run";
    EXPECT_EQ(1, fx.var(kItemsLastSlot)) << "ITEM_LAST seeds to the init tick";
    EXPECT_TRUE(vm_logged(fx.world(), "items_nil\t0\t0\t0"))
        << "nil row / missing pads / empty pads all answer false";

    // Full deficit from tick 1, but nothing spawns before the interval has
    // elapsed: firings at 30 (29 < 60) and 60 (59 < 60) stay quiet.
    fx.tick(88);
    EXPECT_EQ(0, census(fx.world()).respawnable())
        << "no spawn before the interval elapses";

    // Tick 90 (89 >= 60): the first firing takes pad 1, exactly one item.
    fx.tick(1);
    ItemCensus c = census(fx.world());
    EXPECT_EQ(1, c.drum);
    EXPECT_EQ(1, c.respawnable()) << "exactly ONE item per firing";
    walker* item = live_item_at(fx.world(), FAMILY_DRUMSTICK, 160, 160);
    ASSERT_NE(nullptr, item) << "the item centers on pad 1 (168, 168)";
    EXPECT_EQ(0, item->team_num()) << "authored-placement team";
    EXPECT_EQ(1, fx.var(kItemsCursorSlot)) << "cursor advances past the pad";
    EXPECT_EQ(90, fx.var(kItemsLastSlot));

    // Tick 120: 30 ticks since the spawn — the interval clock restarted.
    fx.tick(30);
    EXPECT_EQ(1, census(fx.world()).respawnable());

    // Tick 150: 150 - 90 == 60 — the boundary itself fires (>=, not >).
    fx.tick(30);
    EXPECT_EQ(2, census(fx.world()).drum);
    EXPECT_NE(nullptr, live_item_at(fx.world(), FAMILY_DRUMSTICK, 224, 160));
    EXPECT_EQ(2, fx.var(kItemsCursorSlot));
    EXPECT_EQ(150, fx.var(kItemsLastSlot));

    // Tick 210: the rotation reaches the "gold_bar" pad — inert by
    // construction (never a spawnable family) — and skips to the magic pad.
    fx.tick(60);
    c = census(fx.world());
    EXPECT_EQ(0, c.gold) << "gold NEVER respawns";
    EXPECT_EQ(1, c.magic);
    EXPECT_NE(nullptr,
              live_item_at(fx.world(), FAMILY_MAGIC_POTION, 160, 224));
    EXPECT_EQ(4, fx.var(kItemsCursorSlot))
        << "the cursor lands past the magic pad, over the skipped gold pad";

    // Ticks 270/330: invis, then speed — the cursor wraps to 0.
    fx.tick(60);
    EXPECT_EQ(1, census(fx.world()).invis);
    fx.tick(60);
    c = census(fx.world());
    EXPECT_EQ(1, c.speed);
    EXPECT_EQ(0, fx.var(kItemsCursorSlot)) << "rotation wraps";
    EXPECT_EQ(5, c.respawnable());

    // Every family at its authored target: firings scan but spawn nothing.
    fx.tick(120);
    EXPECT_EQ(5, census(fx.world()).respawnable());
    EXPECT_EQ(330, fx.var(kItemsLastSlot))
        << "a quiet scan must not restart the interval clock";

    expect_no_script_errors(fx.world());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesItems, eaten_item_respawns_on_its_own_pad_others_occupied)
{
    ModesCtfWorld fx(kItemsLevelA);
    fx.tick(330);  // all five spawnable pads filled (see the timeline above)
    ASSERT_EQ(5, census(fx.world()).respawnable());

    // Eat the magic potion: its pad is now the ONLY free pad of a family
    // in deficit, so the next firing must land exactly there, skipping the
    // occupied food pads the cursor visits first.
    walker* potion = live_item_at(fx.world(), FAMILY_MAGIC_POTION, 160, 224);
    ASSERT_NE(nullptr, potion);
    potion->set_dead(1);
    ASSERT_EQ(0, census(fx.world()).magic);

    fx.tick(60);  // tick 390: 390 - 330 == 60
    EXPECT_EQ(1, census(fx.world()).magic);
    EXPECT_NE(nullptr,
              live_item_at(fx.world(), FAMILY_MAGIC_POTION, 160, 224))
        << "the respawn centers on the eaten pad";
    EXPECT_EQ(5, census(fx.world()).respawnable());
    expect_no_script_errors(fx.world());
}

TEST_F(ModesItems, parked_living_denies_the_pad_and_row_interval_overrides)
{
    ModesCtfWorld fx(kItemsLevelB);
    // Park a living exactly on each of the three pads before init: the
    // spawn-camp deny must refuse every one of them.
    walker* b1 = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    walker* b2 = fx.spawn_living(FAMILY_SOLDIER, 0, 224, 160);
    walker* b3 = fx.spawn_living(FAMILY_SOLDIER, 0, 288, 160);
    ASSERT_NE(nullptr, b1);
    ASSERT_NE(nullptr, b2);
    ASSERT_NE(nullptr, b3);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);

    // Row item_interval = 90 overrides the call's default 60: tick 90
    // stays quiet either way (89 < 90), and the tick-120 firing walks the
    // whole pad list, finds every pad blocked, and spawns NOTHING.
    fx.tick(119);
    EXPECT_EQ(0, census(fx.world()).respawnable())
        << "all pads camped: the firing must come up empty";
    EXPECT_EQ(1, fx.var(kItemsLastSlot))
        << "an empty firing must not restart the interval clock";

    // Free pad 1: the very next firing (tick 150) takes it.
    b1->setxy(400, 400);
    fx.tick(30);
    EXPECT_EQ(1, census(fx.world()).drum);
    EXPECT_NE(nullptr, live_item_at(fx.world(), FAMILY_DRUMSTICK, 160, 160));
    EXPECT_EQ(1, fx.var(kItemsCursorSlot));
    EXPECT_EQ(150, fx.var(kItemsLastSlot));

    // Free the rest; the 90-tick row interval now paces the refill.
    b2->setxy(432, 400);
    b3->setxy(464, 400);
    fx.tick(90);  // tick 240: 240 - 150 == 90
    EXPECT_EQ(2, census(fx.world()).drum);
    fx.tick(60);  // tick 300: 60 < 90 — the call's default 60 must NOT apply
    EXPECT_EQ(2, census(fx.world()).drum)
        << "row item_interval overrides the mode default";
    fx.tick(30);  // tick 330: 90 elapsed
    EXPECT_EQ(3, census(fx.world()).drum);

    // Full: later firings stay quiet.
    fx.tick(90);
    EXPECT_EQ(3, census(fx.world()).drum);
    expect_no_script_errors(fx.world());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesItems, spawned_item_is_eaten_by_a_walking_living)
{
    ModesCtfWorld fx(kItemsLevelA);
    fx.tick(90);  // first drumstick on pad 1 at (160, 160)
    walker* item = live_item_at(fx.world(), FAMILY_DRUMSTICK, 160, 160);
    ASSERT_NE(nullptr, item);

    // A wounded living walks east across the pad: the obmap pile scan must
    // find the spawned item and dispatch the core on_eat (heal + consume).
    walker* eater = fx.spawn_living(FAMILY_SOLDIER, 0, 160 - 2 * GRID_SIZE,
                                    160);
    ASSERT_NE(nullptr, eater);
    ASSERT_NE(nullptr, eater->stats());
    eater->stats()->set_hitpoints(eater->stats()->max_hitpoints() - 10.0f);
    const float wounded = eater->stats()->hitpoints();
    eater->set_curdir(static_cast<signed char>(FACE_RIGHT));
    const float step = eater->stepsize();
    ASSERT_GT(step, 0.0f);
    int guard = 0;
    while (!item->dead() && guard++ < 300)
        eater->walk(step, 0.0f);
    ASSERT_LT(guard, 300) << "the walk must consume the item";
    EXPECT_TRUE(item->dead()) << "eaten";
    EXPECT_GT(eater->stats()->hitpoints(), wounded)
        << "the drumstick must heal the eater";
    EXPECT_EQ(0, census(fx.world()).drum);
    expect_no_script_errors(fx.world());
}

TEST_F(ModesItems, spawned_items_replicate_to_a_client_mirror)
{
    ModesCtfWorld fx(kItemsLevelA);
    ModeMirror mirror(kItemsLevelA);

    const MirrorReplication replication = replicate_to_mirror(fx, mirror, 200);
    EXPECT_EQ(0, replication.strikes)
        << "the mirror first desynced at tick "
        << replication.first_strike_tick;
    ASSERT_GE(census(fx.world()).respawnable(), 2)
        << "the run must actually spawn items (ticks 90 and 150)";
    EXPECT_EQ(census(fx.world()).respawnable(),
              census(mirror.world()).respawnable())
        << "spawned items must reach the mirror through the snapshot";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// Shipped-impl wiring (committed manifest rows over programmatic worlds)
// ===========================================================================

TEST_F(ModesItems, mutant_840_items_cycle_deterministically)
{
    // Two identical Mutant matches on the shipped 840 row (interval 180,
    // slots 49/50) with an eat mid-run: byte-identical digests, and items
    // actually cycling (spawn - eat - respawn).
    auto run_once = []() {
        ModesCtfWorld fx(840);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 544, 96);
        fx.spawn_anchor(2, 96, 800);
        fx.spawn_anchor(3, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
        fx.spawn_living(FAMILY_ORC, 1, 544, 96);
        fx.spawn_living(FAMILY_SOLDIER, 2, 96, 800);
        fx.spawn_living(FAMILY_ORC, 3, 544, 800);
        fx.tick(1);
        EXPECT_TRUE(fx.world().mode.active);
        EXPECT_EQ(1, fx.var(kMutItemLast))
            << "the mutant impl seeds ITEM_LAST at init";

        // Interval 180: firings at 30..180 stay quiet, tick 210 spawns.
        fx.tick(179);
        EXPECT_EQ(0, census(fx.world()).respawnable());
        fx.tick(30);
        EXPECT_EQ(1, census(fx.world()).respawnable());
        EXPECT_EQ(210, fx.var(kMutItemLast));
        EXPECT_EQ(1, fx.var(kMutItemCursor));

        // Eat the spawned item; the pads keep cycling on the 180 pace.
        walker* item = first_live_of(fx.world(), FAMILY_SPEED_POTION);
        EXPECT_NE(nullptr, item) << "840's pad 1 is a speed potion";
        if (item != nullptr)
            item->set_dead(1);
        fx.tick(390);  // ticks 390 and 600 both fire (180 apart)
        EXPECT_GE(census(fx.world()).respawnable(), 2);
        expect_no_script_errors(fx.world());
        return digest_world(fx.world());
    };
    const std::string first = run_once();
    const std::string second = run_once();
    EXPECT_EQ(first, second) << "item cycling must be deterministic";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesItems, ctf_500_items_pass_fits_a_tenth_of_the_budget)
{
    // The D16 budget probe, re-run WITH the items pass: a full CTF mode
    // tick on the shipped scen500 row (7 pads, interval 300) must hold a
    // 10x-reduced instruction budget through census + spawn firings.
    og::script::g_test_world_instruction_budget = 500000;
    {
        ModesCtfWorld fx(500);
        fx.spawn_flag(flag_family_, 0, 160, 128);
        fx.spawn_flag(flag_family_, 1, 160, 400);
        fx.spawn_point(point_family_, 320, 240);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 96, 416);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 300);
        fx.tick(1);
        ASSERT_TRUE(fx.ctf_active())
            << "the shipped registration must bind manifest id 500";
        EXPECT_EQ(1, fx.var(kCtfItemLast))
            << "the ctf impl seeds ITEM_LAST (slot 63) at init";

        // Interval 300: tick 330 is the first firing (programmatic world
        // authors no treasures, so the row's 7 pads are all in deficit).
        fx.tick(328);
        EXPECT_EQ(0, census(fx.world()).respawnable());
        fx.tick(1);
        EXPECT_EQ(1, census(fx.world()).respawnable());
        EXPECT_EQ(330, fx.var(kCtfItemLast));

        EXPECT_FALSE(has_script_error(fx.world(), "instruction budget"))
            << "a 10x-reduced budget must never trip";
        expect_no_script_errors(fx.world());
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    og::script::g_test_world_instruction_budget = 0;
}

// ===========================================================================
// Real campaign: TDM eat -> pad respawn on the shipped scen300
// ===========================================================================

namespace {

// A shipped campaign level loaded with full sim context (the
// ModesRealCampaign shape, over the shared modes_test loader hooks).
struct LoadedRealLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedRealLevel(int id)
        : level(id, true, &modes_test_level_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedRealLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

// Park one ACT_CONTROL soldier per team on that team's first authored
// marker, before the first tick. An active team with no livings gets a
// five-bot backfill at init, and bots wander onto pads and eat what
// respawns — a seated team keeps the world still enough to read the
// respawner. Returns the seated-team bitmask.
int seat_teams_at_markers(GameWorld& world, int teams)
{
    std::vector<std::pair<short, short>> seats(
        static_cast<std::size_t>(teams), {-1, -1});
    for (const auto& uptr : world.oblist)
    {
        const walker* ob = uptr.get();
        if (ob == nullptr || ob->query_order() != Order::Special ||
            ob->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const int team = ob->team_num();
        if (team < 0 || team >= teams)
            continue;
        if (seats[static_cast<std::size_t>(team)].first >= 0)
            continue;
        seats[static_cast<std::size_t>(team)] = {ob->xpos(), ob->ypos()};
    }
    int seated = 0;
    for (int team = 0; team < teams; ++team)
    {
        const auto& spot = seats[static_cast<std::size_t>(team)];
        if (spot.first < 0)
            continue;
        walker* seat = world.add_ob(Order::Living, FAMILY_SOLDIER);
        if (seat == nullptr)
            return -1;
        seat->setxy(spot.first, spot.second);
        seat->set_team_num(static_cast<unsigned char>(team));
        seat->set_real_team_num(255);
        seat->set_act_type(ACT_CONTROL);
        seated |= 1 << team;
    }
    return seated;
}

// The first live drumstick no living is standing on — eating a camped one
// would leave its pad denied at the next firing (pad_blocked), which is a
// different rule than the one under test.
walker* first_uncamped_drumstick(GameWorld& world)
{
    for (const auto& uptr : world.fxlist)
    {
        walker* fxob = uptr.get();
        if (fxob == nullptr || fxob->dead() ||
            fxob->query_order() != Order::Treasure ||
            fxob->family() != FAMILY_DRUMSTICK)
        {
            continue;
        }
        bool camped = false;
        for (const auto& obptr : world.oblist)
        {
            const walker* ob = obptr.get();
            if (ob != nullptr && !ob->dead() &&
                ob->query_order() == Order::Living &&
                ob->xpos() / GRID_SIZE == fxob->xpos() / GRID_SIZE &&
                ob->ypos() / GRID_SIZE == fxob->ypos() / GRID_SIZE)
            {
                camped = true;
            }
        }
        if (!camped)
            return fxob;
    }
    return nullptr;
}

}  // namespace

TEST(ModesItemsRealCampaign, tdm_scen300_eaten_drumstick_respawns_on_its_pad)
{
    restore_default_campaigns();
    const std::string previous = get_mounted_campaign();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "builtin/modes.glad should restore and mount";
    {
        LoadedRealLevel fx(300);
        ASSERT_TRUE(fx.loaded) << "scen300 must load from the package";

        // Seat all four teams before the first tick so init fields no bot
        // squads (parked ACT_CONTROL heroes at their own lead markers).
        ASSERT_EQ(15, seat_teams_at_markers(fx.world(), 4))
            << "all four TDM teams must be seatable";

        fx.world().tick();
        ASSERT_TRUE(fx.world().mode.active)
            << "the manifest registration must activate TDM on scen300";
        EXPECT_EQ(1, fx.world().mode.vars[kTdmItemLast])
            << "the tdm impl seeds ITEM_LAST (slot 18) at init";
        ASSERT_EQ(24, census(fx.world()).drum)
            << "THE CIRCLE authors 24 drumsticks";

        // Eat one drumstick that no seat is parked on; its pad is then the
        // only free pad of a family in deficit, so the respawn must land
        // exactly on its tile.
        walker* victim = first_uncamped_drumstick(fx.world());
        ASSERT_NE(nullptr, victim);
        const int pad_tx = victim->xpos() / GRID_SIZE;
        const int pad_ty = victim->ypos() / GRID_SIZE;
        victim->set_dead(1);
        ASSERT_EQ(23, census(fx.world()).drum);

        // Interval 300 (the manifest row): the tick-330 firing refills it.
        for (int i = 1; i < 330; ++i)
            fx.world().tick();
        EXPECT_EQ(24, census(fx.world()).drum)
            << "the eaten drumstick must respawn within one interval";
        walker* respawned =
            live_item_at(fx.world(), FAMILY_DRUMSTICK,
                         static_cast<short>(pad_tx * GRID_SIZE),
                         static_cast<short>(pad_ty * GRID_SIZE));
        EXPECT_NE(nullptr, respawned)
            << "the respawn centers on the eaten pad (" << pad_tx << ", "
            << pad_ty << ")";
        EXPECT_EQ(330, fx.world().mode.vars[kTdmItemLast]);
        // The cursor advanced past the spawned pad — its value is the row
        // index after the eaten pad, which may legitimately wrap to 0.
        EXPECT_LT(fx.world().mode.vars[kTdmItemCursor], 28)
            << "the rotation cursor (slot 17) stays inside the pad list";
        expect_no_script_errors(fx.world());
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    const std::string now = get_mounted_campaign();
    if (now == "modes")
        (void)unmount_campaign_package_with_error(now);
    if (previous.empty())
    {
        const std::string still = get_mounted_campaign();
        if (!still.empty())
            (void)unmount_campaign_package_with_error(still);
    }
    else if (get_mounted_campaign() != previous)
    {
        (void)mount_campaign_package_with_error(previous);
    }
}

// ===========================================================================
// Real campaign: the ball modes adopt the respawner (#225)
// ===========================================================================
//
// The playtest complaint was a pitch and a court where the authored food
// is eaten once and never comes back, so every match turns into a hunt for
// the last chicken. Both bands now carry drumstick pads on their manifest
// rows; these two cases prove the round trip on the SHIPPED geometry, and
// together they cover both halves of basketball's split slot pair (the
// cursor in the shared header band, the clock on the last private slot).

TEST(ModesItemsRealCampaign, soccer_scen820_eaten_drumstick_respawns_on_its_pad)
{
    restore_default_campaigns();
    const std::string previous = get_mounted_campaign();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "builtin/modes.glad should restore and mount";
    {
        LoadedRealLevel fx(820);
        ASSERT_TRUE(fx.loaded) << "scen820 must load from the package";
        ASSERT_EQ(3, seat_teams_at_markers(fx.world(), 2))
            << "both pitch teams must be seatable";

        fx.world().tick();
        ASSERT_TRUE(fx.world().mode.active)
            << "the manifest registration must activate soccer on scen820";
        EXPECT_EQ(1, fx.world().mode.vars[kSoccerItemLast])
            << "the soccer impl seeds ITEM_LAST (slot 47) at init";
        ASSERT_EQ(12, census(fx.world()).drum)
            << "THE PITCH authors 12 drumstick pads";

        walker* victim = first_uncamped_drumstick(fx.world());
        ASSERT_NE(nullptr, victim);
        const int pad_tx = victim->xpos() / GRID_SIZE;
        const int pad_ty = victim->ypos() / GRID_SIZE;
        victim->set_dead(1);
        ASSERT_EQ(11, census(fx.world()).drum);

        // Interval 180 (the manifest row): tick 210 is the first 30-tick
        // cadence boundary at or past the interval from the init seed.
        for (int i = 1; i < 210; ++i)
            fx.world().tick();
        EXPECT_EQ(12, census(fx.world()).drum)
            << "the eaten drumstick must respawn within one interval";
        EXPECT_NE(nullptr,
                  live_item_at(fx.world(), FAMILY_DRUMSTICK,
                               static_cast<short>(pad_tx * GRID_SIZE),
                               static_cast<short>(pad_ty * GRID_SIZE)))
            << "the respawn centers on the eaten pad (" << pad_tx << ", "
            << pad_ty << ")";
        EXPECT_EQ(210, fx.world().mode.vars[kSoccerItemLast])
            << "the firing restarts the interval clock";
        EXPECT_LT(fx.world().mode.vars[kSoccerItemCursor], 12)
            << "the rotation cursor (slot 46) stays inside the pad list";
        expect_no_script_errors(fx.world());
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    const std::string now = get_mounted_campaign();
    if (now == "modes")
        (void)unmount_campaign_package_with_error(now);
    if (previous.empty())
    {
        const std::string still = get_mounted_campaign();
        if (!still.empty())
            (void)unmount_campaign_package_with_error(still);
    }
    else if (get_mounted_campaign() != previous)
    {
        (void)mount_campaign_package_with_error(previous);
    }
}

TEST(ModesItemsRealCampaign,
     basketball_scen824_eaten_drumstick_respawns_on_its_pad)
{
    restore_default_campaigns();
    const std::string previous = get_mounted_campaign();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "builtin/modes.glad should restore and mount";
    {
        LoadedRealLevel fx(824);
        ASSERT_TRUE(fx.loaded) << "scen824 must load from the package";
        ASSERT_EQ(3, seat_teams_at_markers(fx.world(), 2))
            << "both court teams must be seatable";

        fx.world().tick();
        ASSERT_TRUE(fx.world().mode.active)
            << "the manifest registration must activate basketball on scen824";
        EXPECT_EQ(1, fx.world().mode.vars[kBballItemLast])
            << "the basketball impl seeds ITEM_LAST (slot 63) at init";
        ASSERT_EQ(10, census(fx.world()).drum)
            << "CENTER COURT authors 10 drumstick pads";

        walker* victim = first_uncamped_drumstick(fx.world());
        ASSERT_NE(nullptr, victim);
        const int pad_tx = victim->xpos() / GRID_SIZE;
        const int pad_ty = victim->ypos() / GRID_SIZE;
        victim->set_dead(1);
        ASSERT_EQ(9, census(fx.world()).drum);

        // Interval 240 (the manifest row): tick 270 is the first cadence
        // boundary at or past the interval from the init seed.
        for (int i = 1; i < 270; ++i)
            fx.world().tick();
        EXPECT_EQ(10, census(fx.world()).drum)
            << "the eaten drumstick must respawn within one interval";
        EXPECT_NE(nullptr,
                  live_item_at(fx.world(), FAMILY_DRUMSTICK,
                               static_cast<short>(pad_tx * GRID_SIZE),
                               static_cast<short>(pad_ty * GRID_SIZE)))
            << "the respawn centers on the eaten pad (" << pad_tx << ", "
            << pad_ty << ")";
        EXPECT_EQ(270, fx.world().mode.vars[kBballItemLast])
            << "the firing restarts the interval clock on the private slot";
        EXPECT_LT(fx.world().mode.vars[kBballItemCursor], 10)
            << "the header-band cursor (slot 7) stays inside the pad list";
        expect_no_script_errors(fx.world());
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    const std::string now = get_mounted_campaign();
    if (now == "modes")
        (void)unmount_campaign_package_with_error(now);
    if (previous.empty())
    {
        const std::string still = get_mounted_campaign();
        if (!still.empty())
            (void)unmount_campaign_package_with_error(still);
    }
    else if (get_mounted_campaign() != previous)
    {
        (void)mount_campaign_package_with_error(previous);
    }
}
