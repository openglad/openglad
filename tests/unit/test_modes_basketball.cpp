// The Basketball campaign-pack Lua (lib/mode_basketball_impl.lua +
// families/fx-bball.lua, fx-bshadow.lua) behavior suite — the design doc's
// full §11.2 mechanism table (docs/basketball-design.md), one test per
// mechanism, over the shared modes-pack harness (tests/modes_pack_fixture.h)
// against the CURRENT repo pack bytes.
//
// Covered mechanisms: init/manifest banking and every refusal arm, the
// jump-ball freeze and boundary-tick toss, pickup/carry pinning, the fake
// vertical axis (head clearance, block ceiling, apex sanctuary, goaltend
// window), consumed-throw classification with the D19 provenance watermark,
// the D20 scatter curve with the pressure term, rim resolution (basket /
// clang / airball), backboard banks (z-gated reflection + crossing scores),
// dunks with the D23b DUNK_OK gate, fumbles with the D21 damage floor and
// possession grace, the D7/D25 shot-clock lifecycle (persistence across
// loose balls, late-regain turnovers, the 120-tick team grace), D24
// entity-pinned grace bars, D22 receiver leading and the contested-catch
// race, landing legality, the dead-ball and wipe watchdogs, win/timeout/
// buzzer, the AI director's role scheme, spawn caps, mirror replication,
// the determinism digest, instruction-budget headroom and the R4 slot
// budget.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>

using namespace og::modes_test;

namespace og::script {
extern std::int64_t g_test_world_instruction_budget;
}

namespace {

// The mode-var slot map of lib/mode_basketball_impl.lua (table S, design
// §2.2). A silent re-map in the Lua breaks these tests loudly.
enum BballSlot : int {
    kBbModeId = 0,
    kBbScoreLimit = 8,
    kBbRespawnTicks = 9,
    kBbTeamCount = 10,
    kBbTeamMask = 11,
    kBbTimeLimit = 12,
    kBbAnchorCursor = 13,
    kBbBallEntity = 14,
    kBbShadowEntity = 15,
    kBbBallPx = 16,
    kBbBallPy = 17,
    kBbBallPz = 18,
    kBbBallVx = 19,
    kBbBallVy = 20,
    kBbBallVz = 21,
    kBbBallState = 22,
    kBbCarrier = 23,
    kBbClockUntil = 24,
    kBbClockTeam1 = 25,
    kBbLastTouch1 = 26,
    kBbLastTouch2 = 27,
    kBbLastToucher = 28,
    kBbGraceUntil = 29,
    kBbGraceTeam1 = 30,
    kBbShotValue = 31,
    kBbShotHoop1 = 32,
    kBbShotTeam1 = 33,
    kBbShotLand = 34,
    kBbFlightTicks = 35,
    kBbPassTarget = 36,
    kBbFumbleTick = 37,
    kBbJumpUntil = 38,
    kBbJumpPos = 39,
    kBbStallSince = 40,
    kBbBallSpin = 41,
    kBbPoints = 42,     // +team
    kBbHoopPos = 46,    // +team, packed pixel center
    kBbArcRadius = 50,
    kBbSpawnCap = 51,   // +team
    kBbGraceEntity = 59,
    kBbThrowWatermark = 60,
    kBbPossessSince = 61,
    kBbDunkOk = 62,
    kBbSpare = 63,      // the LAST spare (R4) — must never be written
};

// R4 slot-budget headroom: 62 is the highest claimed slot; 63 is spare.
static_assert(kBbDunkOk == og::sim::kModeVarCount - 2,
              "slot map must top out one below the var count");
static_assert(kBbSpare == og::sim::kModeVarCount - 1);

inline constexpr int kModeIdBasketball = 6;  // mode_core.MODE.BASKETBALL

// Ball states (impl STATE_*).
inline constexpr int kStateFree = 0;
inline constexpr int kStateCarried = 1;
inline constexpr int kStateShot = 2;
inline constexpr int kStatePass = 3;
inline constexpr int kStateRebound = 4;

// Tuning mirror (impl table T, design §2.3). Re-declared so a silent
// retune breaks the suite loudly.
inline constexpr int kAiCadence = 15;
inline constexpr int kCarryZ = 12;
inline constexpr int kHeadZ = 20;
inline constexpr int kWallTop = 24;
inline constexpr int kBlockCeiling = 24;
inline constexpr int kRimZ = 32;
inline constexpr int kGoaltendCeiling = 48;
inline constexpr int kGrabZ = 20;
inline constexpr int kCatchZ = 24;
inline constexpr int kRimR = 12;
inline constexpr int kRimLip = 6;
inline constexpr int kRimPop = 512;
inline constexpr int kShotClock = 420;
inline constexpr int kClockHud = 120;
inline constexpr int kClockWarn = 36;
inline constexpr int kTurnoverGrace = 120;
inline constexpr int kSelfGrace = 12;
inline constexpr int kPossessionGrace = 12;
inline constexpr int kJumpFreeze = 36;
inline constexpr int kDeadBallTicks = 600;
inline constexpr int kDunkHalf = 24;
inline constexpr int kPointScore = 100;
inline constexpr int kScatterFree = 16;
inline constexpr int kScatterDiv = 6;
inline constexpr int kScatterCap = 16;
inline constexpr int kPressureScatter = 6;
inline constexpr int kScatterCapTotal = 24;
inline constexpr int kFumblePop = 640;
inline constexpr int kFumbleScatter = 3;
inline constexpr int kTossPop = 768;
inline constexpr int kGravity = 96;
inline constexpr int kPauseFireFrequency = 16384;
inline constexpr int kSpawnMarkBit = 32768;

int count_notifications(const og::sim::SimEventLog& log,
                        const std::string& needle)
{
    int count = 0;
    for (const auto& ev : log.events())
    {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
        {
            count++;
        }
    }
    return count;
}

bool has_notification(const og::sim::SimEventLog& log,
                      const std::string& needle)
{
    return count_notifications(log, needle) > 0;
}

// I8: every announce row fits the 25-char budget.
std::size_t longest_notification(const og::sim::SimEventLog& log)
{
    std::size_t longest = 0;
    for (const auto& ev : log.events())
    {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.size() > longest)
        {
            // The weapon act-type debug rows are engine noise, not
            // basketball announces.
            if (ev.text.find("act type") == std::string::npos &&
                ev.text.find("act random") == std::string::npos)
                longest = ev.text.size();
        }
    }
    return longest;
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

bool front_command_is(const walker* w, std::int32_t type, std::int32_t com1,
                      std::int32_t com2)
{
    const statistics* s = w->stats();
    if (s == nullptr || s->commands.empty())
        return false;
    const command& front = s->commands.front();
    return front.commandtype == type && front.com1 == com1 &&
           front.com2 == com2;
}

std::int32_t front_type(const walker* w)
{
    const statistics* s = w->stats();
    if (s == nullptr || s->commands.empty())
        return 0;
    return s->commands.front().commandtype;
}

bool player_revive_pending(GameWorld& world, std::uint32_t id)
{
    for (const auto& entry : world.respawn.respawn_queue)
    {
        if (entry.kind == 0 && entry.walker_entity_id == id)
            return true;
    }
    return false;
}

int ai_entries_for_team(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& entry : world.respawn.respawn_queue)
    {
        if (entry.kind == 1 && entry.team == team)
            count++;
    }
    return count;
}

// Advances the world to one tick short of the next director cadence
// boundary, so the NEXT fx.tick() runs the director exactly once.
void align_before_cadence(GameWorld& world)
{
    const std::uint32_t next =
        ((world.tick_count_ / kAiCadence) + 1) * kAiCadence;
    world.tick_count_ = next - 1;
}

int alive_on_team(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(team))
        {
            count++;
        }
    }
    return count;
}

// Full behavior digest: mode vars, RNG, oblist positions and command
// queues, AND the fxlist (the shadow lives there — §11.2 #23 extends the
// soccer digest to walk it).
std::string digest_world(GameWorld& world)
{
    std::string digest = std::format("rng={} tick={} act={}|",
                                     world.rng_.state_, world.tick_count_,
                                     world.mode.active ? 1 : 0);
    for (int i = 0; i < og::sim::kModeVarCount; ++i)
        digest += std::format("{},", world.mode.vars[static_cast<std::size_t>(i)]);
    digest += std::format("|q={}|", world.respawn.respawn_queue.size());
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr)
            continue;
        digest += std::format("[id={} x={} y={} dead={} team={} fr={} act={}",
                              w->entity_id(), w->xpos(), w->ypos(),
                              w->dead() ? 1 : 0,
                              static_cast<int>(w->team_num()),
                              static_cast<int>(w->frame()),
                              static_cast<int>(w->act_type()));
        if (w->stats() != nullptr)
        {
            digest += std::format(" q={}", w->stats()->commands.size());
            for (const command& c : w->stats()->commands)
            {
                digest += std::format(" ({},{},{},{})", c.commandtype,
                                      c.commandcount, c.com1, c.com2);
            }
        }
        digest += "]";
    }
    digest += "|fx|";
    for (const auto& uptr : world.fxlist)
    {
        const walker* w = uptr.get();
        if (w == nullptr)
            continue;
        digest += std::format("[id={} x={} y={} fam={} fr={} dead={}]",
                              w->entity_id(), w->xpos(), w->ypos(),
                              static_cast<int>(w->family()),
                              static_cast<int>(w->frame()),
                              w->dead() ? 1 : 0);
    }
    return digest;
}

// Replica of og::sim::SimRandom (glibc LCG constants) so the scatter tests
// can pin `world().rng_.state_` and PREDICT the draw — the recorded
// sim-random-override trap: pin the state, never a spy.
struct LcgReplica
{
    std::uint32_t s;
    int draw(int n)
    {
        s = s * 1103515245u + 12345u;
        return static_cast<int>((s >> 16) % static_cast<std::uint32_t>(n));
    }
};

// Predicted per-axis scatter offsets for error bound e: the release draws
// og.rand(2e+1)-e for x then y, and those are the first two draws after
// the pin (nothing else in the release tick touches the sim RNG).
std::pair<int, int> predict_scatter(std::uint32_t state, int e)
{
    LcgReplica lcg{state};
    const int ox = lcg.draw(2 * e + 1) - e;
    const int oy = lcg.draw(2 * e + 1) - e;
    return {ox, oy};
}

// First seed whose predicted |ox|+|oy| lands inside [min_sum, max_sum] —
// found with pure replica math, then proven against the sim.
std::uint32_t find_scatter_seed(int e, int min_sum, int max_sum)
{
    for (std::uint32_t s = 1; s < 500000; ++s)
    {
        const auto [ox, oy] = predict_scatter(s, e);
        const int sum = std::abs(ox) + std::abs(oy);
        if (sum >= min_sum && sum <= max_sum)
            return s;
    }
    ADD_FAILURE() << "no LCG seed produces a scatter sum in ["
                  << min_sum << ", " << max_sum << "] at E=" << e;
    return 1;
}

}  // namespace

using ModesBasketball = ModesPackTest;

namespace {

// The reference court on kBballLevelA: two anchor teams, one parked
// (ACT_CONTROL, undirected) living each so no bot squads spawn. Hoops
// (64,480) / (576,480), arc 128, jump (320,480) on the default 640x960 px
// test grid.
struct BballCourt : ModesCtfWorld
{
    walker* red = nullptr;
    walker* green = nullptr;

    explicit BballCourt(int level_id = kBballLevelA) : ModesCtfWorld(level_id)
    {
        spawn_anchor(0, 128, 448);
        spawn_anchor(0, 128, 512);
        spawn_anchor(1, 512, 448);
        spawn_anchor(1, 512, 512);
        red = spawn_living(FAMILY_SOLDIER, 0, 128, 96);
        green = spawn_living(FAMILY_SOLDIER, 1, 512, 96);
    }

    bool basketball_active() const
    {
        return var(kBbModeId) == kModeIdBasketball;
    }

    walker* ball()
    {
        return world().find_by_id(
            static_cast<std::uint32_t>(var(kBbBallEntity)));
    }

    walker* shadow()
    {
        return world().find_by_id(
            static_cast<std::uint32_t>(var(kBbShadowEntity)));
    }

    int ball_cx() const { return var(kBbBallPx) / 256; }
    int ball_cy() const { return var(kBbBallPy) / 256; }
    int ball_z_px() const { return var(kBbBallPz) / 256; }
    std::int32_t carrier() const { return var(kBbCarrier); }

    void thaw() { world().mode.vars[kBbJumpUntil] = 0; }

    // Parks the ball as a motionless FREE ball at a pixel center.
    void set_ball_free(int cx, int cy)
    {
        auto& vars = world().mode.vars;
        vars[kBbBallState] = kStateFree;
        vars[kBbCarrier] = 0;
        vars[kBbBallPx] = cx * 256;
        vars[kBbBallPy] = cy * 256;
        vars[kBbBallPz] = 0;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 0;
    }

    // Thaws, spots the ball under `w`'s center and runs the pickup tick.
    void give_ball(walker* w, int cx, int cy)
    {
        thaw();
        w->setxy(static_cast<short>(cx - 8), static_cast<short>(cy - 8));
        set_ball_free(cx, cy);
        tick(1);
        ASSERT_EQ(static_cast<std::int32_t>(w->entity_id()), carrier())
            << "give_ball: the walk-on pickup must take possession";
    }

    // A parked weapon (no ACT_FIRE: the engine never moves or expires it)
    // whose lastx/lasty step is the aim vector the throw scan reads.
    walker* spawn_weapon(walker* who, int x, int y, float step_x, float step_y)
    {
        walker* w = world().add_ob(Order::Weapon, FAMILY_KNIFE);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_owner(who);
        w->set_team_num(who->team_num());
        w->set_lastx(step_x);
        w->set_lasty(step_y);
        return w;
    }

    // A lethal-or-staged C++ melee hit through the real attack path (the
    // damage gate dispatches the Lua on_damage). d < 4.0 keeps
    // compute_base_damage deterministic: floor(sqrt(d)) <= 1, so the RNG
    // range is empty (next(1) still advances the LCG).
    void smash(walker* attacker, walker* target, float staged)
    {
        attacker->set_damage(staged);
        attacker->attack(target);
    }
};

// Ticks until the ball leaves the given state, bounded. Returns ticks run.
int tick_until_state_leaves(BballCourt& fx, int state, int bound)
{
    int ran = 0;
    while (fx.var(kBbBallState) == state && ran < bound)
    {
        fx.tick(1);
        ran++;
    }
    return ran;
}

}  // namespace

// ===========================================================================
// §11.2 #1-#2 — init banks the manifest; every refusal arm errors to classic
// ===========================================================================

TEST_F(ModesBasketball, init_banks_manifest_row)
{
    BballCourt fx;
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_TRUE(fx.basketball_active()) << "MODE_ID is the activation latch";
    EXPECT_EQ(3, fx.var(kBbTeamMask));
    EXPECT_EQ(2, fx.var(kBbTeamCount));
    EXPECT_EQ(6, fx.var(kBbScoreLimit));
    EXPECT_EQ(60, fx.var(kBbRespawnTicks));
    EXPECT_EQ(7200, fx.var(kBbTimeLimit));
    EXPECT_EQ(kBballArcRadius, fx.var(kBbArcRadius));
    EXPECT_EQ(pos_pack(kBballJumpX, kBballJumpY), fx.var(kBbJumpPos));
    EXPECT_EQ(pos_pack(kBballHoop0X, kBballHoop0Y),
              fx.team_var(kBbHoopPos, 0));
    EXPECT_EQ(pos_pack(kBballHoop1X, kBballHoop1Y),
              fx.team_var(kBbHoopPos, 1));
    EXPECT_EQ(0, fx.team_var(kBbPoints, 0));
    EXPECT_EQ(0, fx.team_var(kBbPoints, 1));

    // The ball is an ACTING oblist entity; the shadow lives on the fxlist
    // (never acts, renders under everything) — both must resolve.
    walker* ball = fx.ball();
    ASSERT_NE(nullptr, ball) << "init must spawn the ball";
    EXPECT_EQ(Order::FX, ball->query_order());
    walker* shadow = fx.shadow();
    ASSERT_NE(nullptr, shadow) << "init must spawn the ground shadow";
    bool shadow_in_fxlist = false;
    for (const auto& uptr : fx.world().fxlist)
    {
        if (uptr.get() == shadow)
            shadow_in_fxlist = true;
    }
    EXPECT_TRUE(shadow_in_fxlist) << "the shadow is an fx-list entity (D11)";
    bool ball_in_oblist = false;
    for (const auto& uptr : fx.world().oblist)
    {
        if (uptr.get() == ball)
            ball_in_oblist = true;
    }
    EXPECT_TRUE(ball_in_oblist) << "the ball is an oblist entity";

    // Center reset shape: neutral ball spotted on the jump tile, frozen.
    EXPECT_EQ(kStateFree, fx.var(kBbBallState));
    EXPECT_EQ(0, fx.carrier());
    EXPECT_EQ(kBballJumpX, fx.ball_cx());
    EXPECT_EQ(kBballJumpY, fx.ball_cy());
    EXPECT_EQ(kBballJumpX - 6, ball->xpos())
        << "12x12 ball is centered on the jump spot";
    EXPECT_EQ(SCORE_TEAM_COUNT, ball->team_num()) << "ball starts neutral";
    EXPECT_EQ(0, static_cast<int>(ball->frame()));
    EXPECT_EQ(1, fx.var(kBbDunkOk));
    EXPECT_GT(fx.var(kBbJumpUntil), 0) << "match opens frozen";

    // HUD + beacons (update_hud / sync_render).
    EXPECT_STREQ("RED 0/6", fx.world().mode.hud[0].text.data());
    EXPECT_STREQ("GREEN 0/6", fx.world().mode.hud[1].text.data());
    EXPECT_EQ(static_cast<std::int32_t>(shadow->entity_id()),
              fx.world().mode.beacons[0].entity_id)
        << "beacon 0 is the SHADOW — the ground truth for radar";

    EXPECT_STREQ("BASKETBALL", fx.world().mode.name.data());
    EXPECT_TRUE(has_notification(fx.events, "BASKETBALL! FIRST TO 6"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesBasketball, init_missing_hoop_errors_to_classic)
{
    BballCourt fx(kBballLevelNoHoops);
    fx.tick(1);

    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(has_script_error(fx.world(), "no hoops"));
    fx.tick(5);
    EXPECT_FALSE(fx.world().mode.active) << "the refusal is latched";
}

TEST_F(ModesBasketball, incomplete_manifest_rows_refuse_the_match)
{
    const struct
    {
        int level;
        const char* needle;
    } rows[] = {
        {kBballLevelNoArc, "no arc_radius"},
        {kBballLevelNoJump, "no jump_ball"},
        {kBballLevelHalfHoops, "no hoop for team 1"},
        {kBballLevelNoRow, "no manifest row"},
    };
    for (const auto& row : rows)
    {
        BballCourt fx(row.level);
        fx.tick(1);
        EXPECT_TRUE(fx.world().mode.init_attempted) << row.needle;
        EXPECT_FALSE(fx.world().mode.active) << row.needle;
        EXPECT_TRUE(has_script_error(fx.world(), row.needle)) << row.needle;
    }

    // Fewer than two anchor teams is its own refusal arm.
    {
        ModesCtfWorld fx(kBballLevelA);
        fx.spawn_anchor(0, 128, 448);
        fx.spawn_living(FAMILY_SOLDIER, 0, 128, 96);
        fx.tick(1);
        EXPECT_TRUE(fx.world().mode.init_attempted);
        EXPECT_FALSE(fx.world().mode.active);
        EXPECT_TRUE(has_script_error(fx.world(),
                                     "fewer than two anchor teams"));
    }
}

TEST_F(ModesBasketball, four_hoop_court_and_the_limit_rows_bank)
{
    ModesCtfWorld four(kBballLevelB);
    const int hoop_x[4] = {320, 576, 320, 64};
    const int hoop_y[4] = {64, 480, 896, 480};
    for (int team = 0; team < 4; ++team)
    {
        const short x = static_cast<short>(128 + 64 * team);
        four.spawn_anchor(team, x, 700);
        four.spawn_living(FAMILY_SOLDIER, team, x, 700);
    }
    four.tick(1);
    ASSERT_TRUE(four.world().mode.active);
    EXPECT_EQ(15, four.var(kBbTeamMask));
    EXPECT_EQ(4, four.var(kBbTeamCount));
    for (int team = 0; team < 4; ++team)
    {
        EXPECT_EQ(pos_pack(hoop_x[team], hoop_y[team]),
                  four.team_var(kBbHoopPos, team)) << "hoop " << team;
    }

    // Three anchor teams on the four-hoop court: hoops bank for ACTIVE
    // teams only — the fourth reads 0 and neither scores nor counts
    // (§6.3).
    ModesCtfWorld three(kBballLevelB);
    for (int team = 0; team < 3; ++team)
    {
        const short x = static_cast<short>(128 + 64 * team);
        three.spawn_anchor(team, x, 700);
        three.spawn_living(FAMILY_SOLDIER, team, x, 700);
    }
    three.tick(1);
    ASSERT_TRUE(three.world().mode.active);
    EXPECT_EQ(7, three.var(kBbTeamMask));
    EXPECT_EQ(3, three.var(kBbTeamCount));
    EXPECT_EQ(pos_pack(320, 896), three.team_var(kBbHoopPos, 2));
    EXPECT_EQ(0, three.team_var(kBbHoopPos, 3))
        << "an inactive team banks no hoop";

    BballCourt caps(kBballLevelCaps);
    caps.tick(1);
    ASSERT_TRUE(caps.world().mode.active);
    EXPECT_EQ(2, caps.team_var(kBbSpawnCap, 0));
    EXPECT_EQ(2, caps.team_var(kBbSpawnCap, 1));
    EXPECT_EQ(-1, caps.team_var(kBbSpawnCap, 2)) << "unlisted teams uncapped";

    BballCourt shortgame(kBballLevelShort);
    shortgame.tick(1);
    ASSERT_TRUE(shortgame.world().mode.active);
    EXPECT_EQ(120, shortgame.var(kBbTimeLimit));
    EXPECT_EQ(21, shortgame.var(kBbScoreLimit));
}

TEST_F(ModesBasketball, shipped_manifest_registers_the_basketball_levels)
{
    // scripts/mode_basketball.lua scans the committed manifest: scen824
    // binds with CENTER COURT's hoops, arc and jump spot.
    ModesCtfWorld fx(824);
    fx.spawn_anchor(0, 96, 192);
    fx.spawn_anchor(1, 528, 192);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(kModeIdBasketball, fx.var(kBbModeId));
    EXPECT_EQ(160, fx.var(kBbArcRadius));
    EXPECT_EQ(pos_pack(360, 200), fx.var(kBbJumpPos));
    EXPECT_EQ(pos_pack(56, 200), fx.team_var(kBbHoopPos, 0));
    EXPECT_EQ(pos_pack(664, 200), fx.team_var(kBbHoopPos, 1));
    EXPECT_EQ(21, fx.var(kBbScoreLimit));
}

// ===========================================================================
// §11.2 #3 — jump-ball freeze bars pickup; the toss fires on the boundary
// ===========================================================================

TEST_F(ModesBasketball, jump_ball_freeze_blocks_pickup)
{
    // A walker parked ON the spot cannot grab during the freeze and wins
    // the tip on the boundary tick exactly.
    {
        BballCourt fx;
        fx.red->setxy(312, 472);  // center (320,480) == jump spot
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        const int ju = fx.var(kBbJumpUntil);
        ASSERT_GT(ju, static_cast<int>(fx.world().tick_count_));
        while (static_cast<int>(fx.world().tick_count_) < ju - 1)
        {
            fx.tick(1);
            EXPECT_EQ(0, fx.carrier()) << "freeze bars every pickup";
        }
        fx.tick(1);  // now == JUMP_UNTIL: toss + live grab race
        EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                  fx.carrier())
            << "the tip is winnable on the boundary tick exactly";
    }
    // Nobody near: the toss itself — a one-shot vertical pop.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        const int ju = fx.var(kBbJumpUntil);
        while (static_cast<int>(fx.world().tick_count_) < ju - 1)
            fx.tick(1);
        EXPECT_EQ(kStateFree, fx.var(kBbBallState));
        fx.tick(1);
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState)) << "T19 toss";
        EXPECT_EQ(kTossPop, fx.var(kBbBallVz));
        EXPECT_EQ(0, fx.var(kBbBallVx)) << "vz only";
        EXPECT_EQ(1, count_notifications(fx.events, "JUMP BALL!"));

        // run_spin: airborne states advance a constant air_spin = 192
        // phase per tick (toss tick 192, then 384, then 576), and the
        // drawn frame is phase/256 — a frozen or unspun ball fails here.
        fx.tick(2);
        EXPECT_EQ(576, fx.var(kBbBallSpin))
            << "3 airborne ticks of air_spin backspin";
        EXPECT_EQ(2, static_cast<int>(fx.ball()->frame()))
            << "the rotation strip advances with the spin phase";
    }
}

// ===========================================================================
// §11.2 #4 — pickup and carry: the mode pins the ball to the carrier
// ===========================================================================

TEST_F(ModesBasketball, pickup_and_carry)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 350, 480);

    EXPECT_EQ(kStateCarried, fx.var(kBbBallState));
    EXPECT_EQ(350 * 256, fx.var(kBbBallPx));
    EXPECT_EQ(kCarryZ * 256, fx.var(kBbBallPz));
    EXPECT_EQ(1, fx.var(kBbLastTouch1)) << "possession stamps the toucher";
    EXPECT_EQ(0u, fx.ball()->team_num()) << "ball wears the carrier color";
    EXPECT_EQ(static_cast<std::int32_t>(fx.world().tick_count_),
              fx.var(kBbPossessSince)) << "the D21 grace stamp";

    // Ground vars track the carrier as it moves; z stays carry_z.
    fx.red->setxy(392, 472);  // center (400, 480)
    fx.tick(1);
    EXPECT_EQ(400 * 256, fx.var(kBbBallPx));
    EXPECT_EQ(480 * 256, fx.var(kBbBallPy));
    EXPECT_EQ(kCarryZ, fx.ball_z_px());
    EXPECT_EQ(0, fx.var(kBbBallVx));

    // Presentation: the ball entity draws LIFTED by z; the shadow marks
    // the true ground spot and shrinks with altitude.
    EXPECT_EQ(400 - 6, fx.ball()->xpos());
    EXPECT_EQ(480 - kCarryZ - 6, fx.ball()->ypos())
        << "drawn ball y == ground y - carry_z - half";
    EXPECT_EQ(400 - 6, fx.shadow()->xpos());
    EXPECT_EQ(480 - 6, fx.shadow()->ypos());
    EXPECT_EQ(1, static_cast<int>(fx.shadow()->frame()))
        << "carry height sits in shadow band 1";
    EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
              fx.world().mode.beacons[1].entity_id)
        << "beacon 1 tracks the carrier";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// The watermark stamps the top of the live weapon list at possession gain.
TEST_F(ModesBasketball, possession_gain_stamps_watermark_and_since)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    walker* stale = fx.spawn_weapon(fx.red, 200, 200, 2.0f, 0.0f);
    ASSERT_NE(nullptr, stale);
    fx.give_ball(fx.red, 350, 480);

    EXPECT_EQ(static_cast<std::int32_t>(stale->entity_id()),
              fx.var(kBbThrowWatermark))
        << "watermark == highest weapon entity id at the gain";
    EXPECT_EQ(static_cast<std::int32_t>(fx.world().tick_count_),
              fx.var(kBbPossessSince));
}

// ===========================================================================
// §11.2 #5 — the fake vertical axis clears heads
// ===========================================================================

TEST_F(ModesBasketball, airborne_ball_clears_heads)
{
    // A flat-throw PASS gliding over a walker at z > head_z is untouchable;
    // the identical geometry at z <= head_z is taken.
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    fx.green->setxy(392, 472);  // center (400, 480)

    auto& vars = fx.world().mode.vars;
    vars[kBbBallState] = kStatePass;
    vars[kBbPassTarget] = 0;  // flat throw: anyone may take it low
    vars[kBbBallPx] = 400 * 256;
    vars[kBbBallPy] = 480 * 256;
    vars[kBbBallPz] = 22 * 256;  // above head_z (20), inside catch_z (24)
    vars[kBbBallVx] = 0;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = kGravity;  // one tick of hang at the same height
    vars[kBbFlightTicks] = 10;
    fx.tick(1);
    EXPECT_EQ(0, fx.carrier()) << "a ball above head_z clears the walker";
    EXPECT_EQ(kStatePass, fx.var(kBbBallState));

    vars[kBbBallPz] = 12 * 256;  // at carry height: contestable
    vars[kBbBallVz] = kGravity;
    fx.tick(1);
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()), fx.carrier())
        << "the same trajectory at z <= grab_z is taken";
}

// ===========================================================================
// §11.2 #6 — throw consumption + classification (shot / chest / lob)
// ===========================================================================

TEST_F(ModesBasketball, throw_consumes_weapon_and_classifies)
{
    // ARC SHOT: aimed dead at the hoop from inside shot range.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        fx.tick(1);

        EXPECT_NE(0, static_cast<int>(shot->dead()))
            << "the consumed weapon dies at release";
        EXPECT_EQ(kStateShot, fx.var(kBbBallState));
        EXPECT_EQ(0, fx.carrier()) << "possession cleared";
        EXPECT_EQ(2, fx.var(kBbShotValue)) << "126 px < arc 128: a two";
        EXPECT_EQ(2, fx.var(kBbShotHoop1));
        EXPECT_EQ(1, fx.var(kBbShotTeam1));
        EXPECT_GT(fx.var(kBbFlightTicks), 0);
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // CHEST pass: teammate at L1 96 — fast, flat, apex under head_z.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        walker* mate = fx.spawn_living(FAMILY_SOLDIER, 0, 312, 376);
        ASSERT_NE(nullptr, mate);  // center (320, 384): 96 due north
        const float mate_hp = mate->stats()->hitpoints();
        fx.give_ball(fx.red, 320, 480);
        walker* shot = fx.spawn_weapon(fx.red, 400, 560, 0.0f, -8.0f);
        ASSERT_NE(nullptr, shot);
        fx.tick(1);

        EXPECT_NE(0, static_cast<int>(shot->dead()));
        EXPECT_EQ(kStatePass, fx.var(kBbBallState));
        EXPECT_EQ(static_cast<std::int32_t>(mate->entity_id()),
                  fx.var(kBbPassTarget));
        EXPECT_EQ(1, fx.var(kBbShotTeam1))
            << "passes stamp the thrower team too (the charm compare)";
        EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                  fx.var(kBbGraceEntity)) << "entity self grace on release";
        EXPECT_EQ(0, fx.var(kBbGraceTeam1));

        int max_z = 0;
        for (int i = 0; i < 20 && fx.var(kBbBallState) == kStatePass; ++i)
        {
            fx.tick(1);
            max_z = std::max(max_z, fx.ball_z_px());
        }
        EXPECT_LE(max_z, kHeadZ)
            << "a chest pass stays under head_z the whole way";
        EXPECT_EQ(static_cast<std::int32_t>(mate->entity_id()), fx.carrier())
            << "the receiver completes it";
        EXPECT_EQ(mate_hp, mate->stats()->hitpoints())
            << "the consumed weapon dealt no damage";
    }
    // LOB: teammate at L1 160 — slow, apex over head_z.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        walker* mate = fx.spawn_living(FAMILY_SOLDIER, 0, 312, 312);
        ASSERT_NE(nullptr, mate);  // center (320, 320): 160 due north
        fx.give_ball(fx.red, 320, 480);
        walker* shot = fx.spawn_weapon(fx.red, 400, 560, 0.0f, -8.0f);
        ASSERT_NE(nullptr, shot);
        fx.tick(1);

        EXPECT_EQ(kStatePass, fx.var(kBbBallState));
        EXPECT_EQ(static_cast<std::int32_t>(mate->entity_id()),
                  fx.var(kBbPassTarget));
        int max_z = 0;
        for (int i = 0; i < 40 && fx.var(kBbBallState) == kStatePass; ++i)
        {
            fx.tick(1);
            max_z = std::max(max_z, fx.ball_z_px());
        }
        EXPECT_GT(max_z, kHeadZ) << "a lob arcs over heads in the middle";
    }
    // Dead-center rule: a (0,0) weapon step aims along the carrier facing.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        fx.red->set_curdir(FACE_RIGHT);
        fx.red->set_enddir(FACE_RIGHT);
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 0.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        fx.tick(1);

        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "facing east from (450,480) aims at the east hoop";
        EXPECT_EQ(2, fx.var(kBbShotHoop1));
    }
}

// ===========================================================================
// §11.2 #7 — arc value at release: 2 inside, 3 beyond (pinned RNG)
// ===========================================================================

TEST_F(ModesBasketball, arc_shot_scores_2_inside_3_outside)
{
    // Inside the arc: D = 76 px on-axis (L1 == Euclid), E = 10. Seed
    // chosen by the LCG replica so the scatter stays inside the rim.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 500, 480);
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        const std::uint32_t seed = find_scatter_seed(10, 0, 8);
        const auto [ox, oy] = predict_scatter(seed, 10);
        fx.world().rng_.state_ = seed;
        const std::uint32_t score_before = fx.world().m_score[0];
        fx.tick(1);

        ASSERT_EQ(kStateShot, fx.var(kBbBallState));
        EXPECT_EQ(2, fx.var(kBbShotValue));
        EXPECT_EQ(pos_pack(kBballHoop1X + ox, kBballHoop1Y + oy),
                  fx.var(kBbShotLand))
            << "the pinned state predicts the scatter exactly";
        tick_until_state_leaves(fx, kStateShot, 40);

        EXPECT_EQ(2, fx.team_var(kBbPoints, 0)) << "basket: +2 on the metric";
        EXPECT_EQ(score_before + 2 * kPointScore, fx.world().m_score[0])
            << "m_score grows by value * point_score";
        EXPECT_TRUE(has_notification(fx.events, "BASKET! RED +2"));
        EXPECT_EQ(kStateFree, fx.var(kBbBallState)) << "center reset";
        EXPECT_EQ(kBballJumpX, fx.ball_cx());
    }
    // Beyond the arc: D = 160 > 128, value 3, E = 16 (cap).
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 416, 480);
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        const std::uint32_t seed = find_scatter_seed(16, 0, 8);
        const auto [ox, oy] = predict_scatter(seed, 16);
        fx.world().rng_.state_ = seed;
        const std::uint32_t score_before = fx.world().m_score[0];
        fx.tick(1);

        ASSERT_EQ(kStateShot, fx.var(kBbBallState));
        EXPECT_EQ(3, fx.var(kBbShotValue)) << "160 px > arc 128: a three";
        EXPECT_EQ(pos_pack(kBballHoop1X + ox, kBballHoop1Y + oy),
                  fx.var(kBbShotLand));
        EXPECT_TRUE(has_notification(fx.events, "THREE UP!"))
            << "the D26 release announce names the value";
        tick_until_state_leaves(fx, kStateShot, 40);

        EXPECT_EQ(3, fx.team_var(kBbPoints, 0));
        EXPECT_EQ(score_before + 3 * kPointScore, fx.world().m_score[0]);
        EXPECT_TRUE(has_notification(fx.events, "THREE! RED +3"));
        EXPECT_GE(25u, longest_notification(fx.events))
            << "announces stay inside the 25-char budget (I8)";
    }
}

// ===========================================================================
// §11.2 #8 — misses: rim clang scrum and the airball
// ===========================================================================

TEST_F(ModesBasketball, rim_miss_rebounds_live)
{
    // Rim clang: a pinned seed drops the landing in the lip band
    // (12 < d <= 18; the flight truncation is at most 1 px per axis, so
    // 15..16 stays inside).
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 416, 480);
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        fx.world().rng_.state_ = find_scatter_seed(16, 15, 16);
        fx.tick(1);
        ASSERT_EQ(kStateShot, fx.var(kBbBallState));
        tick_until_state_leaves(fx, kStateShot, 40);

        EXPECT_EQ(kStateRebound, fx.var(kBbBallState))
            << "the clang scrum is live";
        EXPECT_EQ(kRimPop, fx.var(kBbBallVz)) << "clang pops the ball up";
        EXPECT_EQ(0, fx.team_var(kBbPoints, 0)) << "no score";
        EXPECT_EQ(0, fx.var(kBbShotValue)) << "shot state cleared";
        EXPECT_EQ(0, fx.var(kBbShotHoop1));
        EXPECT_EQ(1, fx.var(kBbLastTouch1))
            << "LAST_TOUCH stays the shooter until someone touches";
        EXPECT_FALSE(has_notification(fx.events, "BASKET!"));
    }
    // Airball: a wide miss sails past the lip and gravity finishes it.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 416, 480);
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        fx.world().rng_.state_ = find_scatter_seed(16, 21, 32);
        fx.tick(1);
        ASSERT_EQ(kStateShot, fx.var(kBbBallState));
        tick_until_state_leaves(fx, kStateShot, 40);

        EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
        EXPECT_LT(fx.var(kBbBallVz), 0)
            << "an airball keeps falling — no clang pop";
        EXPECT_EQ(0, fx.team_var(kBbPoints, 0));
        EXPECT_FALSE(has_notification(fx.events, "BASKET!"));
    }
}

// ===========================================================================
// §11.2 #9 — bank shot: z-gated backboard reflection + crossing score (T16)
// ===========================================================================

TEST_F(ModesBasketball, bank_shot_reflects_off_backboard)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    // A backboard column behind the east hoop: tile x=37 (px 592..607).
    for (int gy = 28; gy <= 31; ++gy)
        fx.world().grid.data[static_cast<std::size_t>(37 + fx.world().grid.w * gy)] = PIX_H_WALL1;

    // A live rebound rising past the wall at LOW altitude: it banks off
    // the board (z < wall_top), keeps rising over the rim plane, and falls
    // back through it inside the rim — team 0's bank bucket.
    auto& vars = fx.world().mode.vars;
    vars[kBbBallState] = kStateRebound;
    vars[kBbLastTouch1] = 1;
    vars[kBbBallPx] = 586 * 256;
    vars[kBbBallPy] = 480 * 256;
    vars[kBbBallPz] = 23 * 256;
    vars[kBbBallVx] = 256;  // +1 px/tick into the board
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = 700;
    fx.tick(1);
    EXPECT_EQ(-256, fx.var(kBbBallVx))
        << "below wall_top the board reflects the ball (D12)";
    for (int i = 0; i < 15 && fx.var(kBbBallState) == kStateRebound; ++i)
        fx.tick(1);
    EXPECT_EQ(2, fx.team_var(kBbPoints, 0))
        << "the descending rim-plane crossing scores the bank (T16)";
    EXPECT_TRUE(has_notification(fx.events, "BASKET! RED +2"));
    EXPECT_EQ(kStateFree, fx.var(kBbBallState)) << "center reset";

    // z-gate control: the same trajectory above wall_top sails over the
    // board without reflecting.
    fx.thaw();
    vars[kBbBallState] = kStateRebound;
    vars[kBbBallPx] = 586 * 256;
    vars[kBbBallPy] = 480 * 256;
    vars[kBbBallPz] = 25 * 256;
    vars[kBbBallVx] = 256;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = 700;
    fx.tick(1);
    EXPECT_EQ(256, fx.var(kBbBallVx))
        << "above wall_top the ball clears the board";
}

// ===========================================================================
// §11.2 #10 — the presence dunk
// ===========================================================================

TEST_F(ModesBasketball, dunk_scores_2)
{
    // Carrying the ball into the ENEMY dunk box scores 2 and resets.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        const std::uint32_t score_before = fx.world().m_score[0];
        fx.red->setxy(552, 472);  // center (560, 480): Chebyshev 16 <= 24
        fx.tick(1);

        EXPECT_EQ(2, fx.team_var(kBbPoints, 0));
        EXPECT_EQ(score_before + 2 * kPointScore, fx.world().m_score[0]);
        EXPECT_TRUE(has_notification(fx.events, "DUNK! RED +2"));
        EXPECT_EQ(0, fx.carrier()) << "center reset took the ball";
        EXPECT_EQ(kBballJumpX, fx.ball_cx());
        EXPECT_EQ(kStateFree, fx.var(kBbBallState));
    }
    // One's own box never triggers (t ~= team).
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 200, 480);
        fx.red->setxy(62, 472);  // center (70, 480): inside OWN hoop box
        fx.tick(3);

        EXPECT_EQ(0, fx.team_var(kBbPoints, 0)) << "no own-hoop dunk";
        EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                  fx.carrier()) << "possession continues";
        EXPECT_FALSE(has_notification(fx.events, "DUNK!"));
    }
}

// ===========================================================================
// §11.2 #11 — fumble on qualifying carrier damage (T5)
// ===========================================================================

TEST_F(ModesBasketball, fumble_on_carrier_damage)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 350, 480);
    fx.red->stats()->set_armor(0.0f);
    fx.tick(kPossessionGrace);  // clear the D21 possession grace

    // Staged 3.0 lands as exactly 2 (3 - sqrt(3)/2 floors to 2): at the
    // fumble_min_damage threshold, the hook arms FUMBLE_TICK.
    const float hp_before = fx.red->stats()->hitpoints();
    fx.smash(fx.green, fx.red, 3.0f);
    EXPECT_EQ(hp_before - 2.0f, fx.red->stats()->hitpoints())
        << "the hit lands its damage";
    EXPECT_EQ(static_cast<std::int32_t>(fx.world().tick_count_),
              fx.var(kBbFumbleTick)) << "on_damage armed the fumble";

    const std::int32_t red_id =
        static_cast<std::int32_t>(fx.red->entity_id());
    fx.tick(1);
    EXPECT_EQ(0, fx.carrier()) << "T5 strips the ball";
    EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
    EXPECT_EQ(0, fx.var(kBbFumbleTick)) << "consumed";
    // fumble_pop 640 fp, one gravity step already integrated.
    EXPECT_EQ(kFumblePop - kGravity, fx.var(kBbBallVz));
    EXPECT_LE(std::abs(fx.var(kBbBallVx)), kFumbleScatter * 256)
        << "horizontal scatter stays inside fumble_scatter px/tick";
    EXPECT_EQ(0, fx.var(kBbGraceTeam1)) << "entity-scoped self grace";
    EXPECT_EQ(red_id, fx.var(kBbGraceEntity))
        << "the ex-carrier wears the bar — the defender may scoop";
    EXPECT_GT(fx.var(kBbGraceUntil),
              static_cast<std::int32_t>(fx.world().tick_count_));
}

// ===========================================================================
// §11.2 #12 — carrier death drops the ball in place (T6)
// ===========================================================================

TEST_F(ModesBasketball, carrier_death_drops_ball)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 350, 480);
    fx.red->set_dead(1);
    fx.tick(1);

    EXPECT_EQ(0, fx.carrier());
    EXPECT_EQ(kStateRebound, fx.var(kBbBallState)) << "T6 drop-in-place";
    EXPECT_LE(std::abs(fx.ball_cx() - 350), kFumbleScatter)
        << "the ball drops at the corpse";
    EXPECT_EQ(1, fx.var(kBbLastTouch1))
        << "LAST_TOUCH stays the dead carrier's team (§9 #1)";
    EXPECT_EQ(0, fx.var(kBbGraceEntity)) << "death arms no grace";
}

// ===========================================================================
// §11.2 #13 — the shot clock: HUD countdown, warning, turnover (T7)
// ===========================================================================

TEST_F(ModesBasketball, shot_clock_turnover)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 350, 480);
    const std::int32_t deadline = fx.var(kBbClockUntil);
    ASSERT_EQ(static_cast<std::int32_t>(fx.world().tick_count_) + kShotClock,
              deadline);
    EXPECT_EQ(1, fx.var(kBbClockTeam1));

    // Above clock_hud remaining: the plain score line.
    while (deadline - static_cast<std::int32_t>(fx.world().tick_count_) >
           kClockHud + 1)
        fx.tick(1);
    EXPECT_STREQ("RED 0/6", fx.world().mode.hud[0].text.data());
    fx.tick(1);  // remaining == clock_hud
    EXPECT_STREQ("RED 0/6 !10", fx.world().mode.hud[0].text.data())
        << "the possessing team's own line carries the countdown suffix";

    // One-shot warning at exactly clock_warn remaining.
    while (deadline - static_cast<std::int32_t>(fx.world().tick_count_) >
           kClockWarn)
        fx.tick(1);
    EXPECT_EQ(1, count_notifications(fx.events, "SHOT CLOCK!"));

    // Expiry in CARRIED: the turnover.
    while (static_cast<std::int32_t>(fx.world().tick_count_) < deadline)
        fx.tick(1);
    EXPECT_EQ(1, count_notifications(fx.events, "TURNOVER!"));
    EXPECT_EQ(0, fx.carrier());
    EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
    EXPECT_EQ(0, fx.var(kBbClockUntil)) << "T7 clears the clock (D25)";
    EXPECT_EQ(0, fx.var(kBbClockTeam1));
    EXPECT_EQ(1, fx.var(kBbGraceTeam1)) << "TEAM-scoped turnover grace";
    EXPECT_EQ(deadline + kTurnoverGrace, fx.var(kBbGraceUntil));

    // The ex-team is barred; the opponent takes it immediately and arms a
    // FRESH clock.
    fx.set_ball_free(350, 480);  // re-spot under red
    fx.tick(1);
    EXPECT_EQ(0, fx.carrier()) << "the barred team cannot re-grab";
    fx.green->setxy(342, 472);
    fx.set_ball_free(350, 480);
    fx.tick(1);
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()),
              fx.carrier()) << "the opponent may take it at once";
    EXPECT_EQ(2, fx.var(kBbClockTeam1));
    EXPECT_EQ(static_cast<std::int32_t>(fx.world().tick_count_) + kShotClock,
              fx.var(kBbClockUntil)) << "a different team arms fresh";
}

// ===========================================================================
// §11.2 #14 — block window by height; the apex sanctuary
// ===========================================================================

TEST_F(ModesBasketball, block_window_by_height)
{
    // At z <= block_ceiling a weapon swat is a legal contest.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 20 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 300;
        walker* swat = fx.spawn_weapon(fx.green, 317, 297, 3.0f, -1.0f);
        ASSERT_NE(nullptr, swat);
        swat->set_damage(5.0f);  // clamp(trunc(5)*2, 4, 12) = 10 px/tick
        fx.tick(1);

        EXPECT_EQ(kStateRebound, fx.var(kBbBallState)) << "T11 block";
        EXPECT_TRUE(has_notification(fx.events, "BLOCK!"));
        // Exact impulse pin: L1-normalized weapon step (3,-1), speed 10
        // -> vx = 2560*3/4 = 1920, vy = 2560*-1/4 = -640 (|vx|+|vy| ==
        // speed*256); the horizontal pair survives the same-tick run_air
        // untouched on open floor.
        EXPECT_EQ(1920, fx.var(kBbBallVx))
            << "the impulse follows the swatting weapon's step, "
               "L1-normalized to clamp(trunc(damage)*2, 4, 12) px/tick";
        EXPECT_EQ(-640, fx.var(kBbBallVy));
        EXPECT_EQ(300 + 512 - kGravity, fx.var(kBbBallVz))
            << "the +512 fp pop, then one tick of gravity";
        EXPECT_EQ(2, fx.var(kBbLastTouch1))
            << "restamped to the swatting owner's team";
        EXPECT_EQ(0, fx.var(kBbShotValue)) << "flight facts cleared";
        EXPECT_EQ(0, static_cast<int>(swat->dead()))
            << "the swatting weapon is NOT consumed";
    }
    // The impulse clamp bounds: damage 20 tops out at 12 px/tick; damage
    // 1.5 floors at 4 px/tick.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 20 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 300;
        walker* heavy = fx.spawn_weapon(fx.green, 317, 297, 4.0f, 0.0f);
        ASSERT_NE(nullptr, heavy);
        heavy->set_damage(20.0f);
        fx.tick(1);
        EXPECT_EQ(12 * 256, fx.var(kBbBallVx)) << "clamp top: 12 px/tick";
        EXPECT_EQ(0, fx.var(kBbBallVy));

        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbFlightTicks] = 20;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 20 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 300;
        heavy->set_dead(1);
        walker* soft = fx.spawn_weapon(fx.green, 317, 297, 0.0f, -4.0f);
        ASSERT_NE(nullptr, soft);
        soft->set_damage(1.5f);  // trunc -> 1*2 = 2, clamped up to 4
        fx.tick(1);
        EXPECT_EQ(0, fx.var(kBbBallVx));
        EXPECT_EQ(-4 * 256, fx.var(kBbBallVy)) << "clamp floor: 4 px/tick";
    }
    // §3.5's zero-step ruling: a (0,0)-step weapon cannot swat and is
    // passed over — a later in-radius weapon with a real step still
    // contests the ball the same tick.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 20 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 300;
        walker* frozen = fx.spawn_weapon(fx.green, 317, 297, 0.0f, 0.0f);
        ASSERT_NE(nullptr, frozen);  // a boomerang at turnaround
        walker* live = fx.spawn_weapon(fx.green, 315, 299, 4.0f, 0.0f);
        ASSERT_NE(nullptr, live);
        live->set_damage(5.0f);
        fx.tick(1);
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState))
            << "the zero-step weapon does not shield the ball (§3.5)";
        EXPECT_EQ(10 * 256, fx.var(kBbBallVx))
            << "the later real-step weapon lands the block";
        EXPECT_EQ(0, static_cast<int>(frozen->dead()));
    }
    // Between the windows the arc is untouchable — the apex sanctuary.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 28 * 256;  // above block_ceiling, ascending
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 300;
        walker* swat = fx.spawn_weapon(fx.green, 317, 297, 4.0f, 0.0f);
        ASSERT_NE(nullptr, swat);
        fx.tick(1);

        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "an identical weapon at the apex touches nothing";
        EXPECT_EQ(19, fx.var(kBbFlightTicks)) << "the flight continues";
        EXPECT_FALSE(has_notification(fx.events, "BLOCK!"));
    }
    // The dead band between the windows near the RIM: descending in
    // (block_ceiling, rim_z] is still untouchable, and so is a descent
    // above goaltend_ceiling.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbBallPx] = 570 * 256;  // near the target hoop
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 28 * 256;   // above block, below the rim plane
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -100;       // descending
        walker* swat = fx.spawn_weapon(fx.green, 567, 477, 4.0f, 0.0f);
        ASSERT_NE(nullptr, swat);
        fx.tick(1);
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "(block_ceiling, rim_z] descending is nobody's window";

        vars[kBbBallPz] = 60 * 256;   // above the goaltend ceiling
        vars[kBbBallVz] = -100;
        fx.tick(1);
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "above goaltend_ceiling the arc is untouchable too";
        EXPECT_EQ(0, fx.team_var(kBbPoints, 0));
    }
    // An UNOWNED weapon's swat clears LAST_TOUCH1 only — the older touch
    // history survives, and an unattributed crossing scores nothing
    // (§3.3, edge #8).
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbLastTouch1] = 1;
        vars[kBbLastTouch2] = 2;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 20 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 300;
        walker* orphan = fx.world().add_ob(Order::Weapon, FAMILY_KNIFE);
        ASSERT_NE(nullptr, orphan);  // never owned: weap::act self-owns it
        orphan->setxy(317, 297);
        orphan->set_lastx(4.0f);
        orphan->set_lasty(0.0f);
        fx.tick(1);
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
        EXPECT_EQ(0, fx.var(kBbLastTouch1))
            << "an unowned swat credits nobody";
        EXPECT_EQ(2, fx.var(kBbLastTouch2))
            << "but it does not erase who forced the play";
    }
}

// ===========================================================================
// §11.2 #15 — goaltending: defensive swat pays the basket; own-team tips
// ===========================================================================

TEST_F(ModesBasketball, goaltend_awards_basket)
{
    // Defensive swat in the window: the frozen outcome pays in full.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 3;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbBallPx] = 570 * 256;  // 6 px L1 from the target hoop
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 40 * 256;   // (rim_z, goaltend_ceiling]
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -200;       // descending
        walker* swat = fx.spawn_weapon(fx.green, 567, 477, 4.0f, 0.0f);
        ASSERT_NE(nullptr, swat);
        const std::uint32_t score_before = fx.world().m_score[0];
        fx.tick(1);

        EXPECT_EQ(3, fx.team_var(kBbPoints, 0))
            << "GOALTENDING: the shooter's team takes the full value";
        EXPECT_EQ(score_before + 3 * kPointScore, fx.world().m_score[0]);
        EXPECT_TRUE(has_notification(fx.events, "GOALTEND! RED +3"));
        EXPECT_EQ(kStateFree, fx.var(kBbBallState)) << "center reset";
        EXPECT_EQ(kBballJumpX, fx.ball_cx());
    }
    // The shooter's OWN team swatting there is an offensive tip: a live
    // rebound, no award.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 3;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbBallPx] = 570 * 256;
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 40 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -200;
        walker* swat = fx.spawn_weapon(fx.red, 567, 477, -4.0f, 0.0f);
        ASSERT_NE(nullptr, swat);
        fx.tick(1);

        EXPECT_EQ(0, fx.team_var(kBbPoints, 0)) << "a tip scores nothing yet";
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState))
            << "the offensive tip converts to a live rebound";
        EXPECT_EQ(1, fx.var(kBbLastTouch1)) << "restamped to the tipper";
        EXPECT_FALSE(has_notification(fx.events, "GOALTEND"));
        EXPECT_FALSE(has_notification(fx.events, "BLOCK!"))
            << "the tip is silent — BLOCK! belongs to the block window "
               "only (§3.5)";
    }
}

// ===========================================================================
// §11.2 #16 — dead-ball reset + the revive backstop
// ===========================================================================

TEST_F(ModesBasketball, dead_ball_resets_and_revives_wiped_team)
{
    // Plain attendance arm: both teams alive but parked far beyond
    // dead_ball_radius — the motionless unattended ball resets at 600.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        // The stall clock re-arms after the toss settles the opening
        // REBOUND back to FREE (~20 ticks past the freeze), so the reset
        // lands a little past 600 raw ticks.
        fx.tick(598);
        EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET"))
            << "not yet";
        for (int i = 0;
             i < 120 && count_notifications(fx.events, "BALL RESET") == 0;
             ++i)
            fx.tick(1);
        EXPECT_EQ(1, count_notifications(fx.events, "BALL RESET"));
        EXPECT_EQ(kBballJumpX, fx.ball_cx());
        EXPECT_GT(fx.var(kBbJumpUntil),
                  static_cast<std::int32_t>(fx.world().tick_count_))
            << "post-reset freeze armed";
    }
    // Wipe arm: an ATTENDED free ball still resets when a team is wiped
    // (D25 — the watchdog ignores attendance), and the reset's backstop
    // refields the wiped side.
    {
        BballCourt fx;
        fx.world().respawn_mode = 0;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.red->setxy(292, 452);  // center (300,460): 40 px from the ball —
                                  // attending, but NOT on it (no pickup)
        fx.green->set_dead(1);
        fx.tick(598);
        EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET"));
        ASSERT_EQ(0, alive_on_team(fx.world(), 1));
        fx.tick(5);
        EXPECT_EQ(1, count_notifications(fx.events, "BALL RESET"))
            << "attendance cannot stall the wipe watchdog";
        EXPECT_EQ(5, alive_on_team(fx.world(), 1))
            << "revive_wiped_teams refields the wiped side";
    }
}

// ===========================================================================
// §11.2 #17 — win by score; the timeout ladder; the buzzer beater
// ===========================================================================

TEST_F(ModesBasketball, win_by_score_and_timeout_ladder)
{
    // Score limit latches immediately.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.world().mode.vars[kBbPoints + 1] = 6;
        fx.tick(1);
        EXPECT_TRUE(fx.world().mode.win_latched);
        EXPECT_EQ(1, fx.world().mode.winner_team);
        EXPECT_TRUE(fx.world().game_ended);
        fx.tick(3);
        EXPECT_TRUE(fx.world().game_ended) << "the win re-asserts";
    }
    // Timeout rung 1: POINTS lead.
    {
        BballCourt fx(kBballLevelShort);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.world().mode.vars[kBbPoints + 0] = 1;
        fx.world().set_level_tick_count(120 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(0, fx.world().mode.winner_team);
    }
    // Rung 2: POINTS tied, larger m_score wins.
    {
        BballCourt fx(kBballLevelShort);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.world().m_score[1] = 700;
        fx.world().set_level_tick_count(120 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(1, fx.world().mode.winner_team);
    }
    // Rung 3: full tie — the lowest team byte.
    {
        BballCourt fx(kBballLevelShort);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.world().set_level_tick_count(120 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(0, fx.world().mode.winner_team);
    }
    // Buzzer beater: the timeout defers while a SHOT flies; the airball
    // resolution ends the deferral.
    {
        BballCourt fx(kBballLevelShort);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(200, 200);
        vars[kBbFlightTicks] = 6;
        vars[kBbBallPx] = 200 * 256;
        vars[kBbBallPy] = 200 * 256;
        vars[kBbBallPz] = 40 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 0;
        fx.world().set_level_tick_count(120);
        fx.tick(1);
        EXPECT_FALSE(fx.world().game_ended)
            << "a shot in the air defers the buzzer";
        fx.tick(6);
        EXPECT_TRUE(fx.world().game_ended)
            << "the deferral ends with the shot";
    }
}

// ===========================================================================
// §11.2 #18 — 3-4 team attribution: dunk credit, own-basket ladder, forfeit
// ===========================================================================

namespace {

// The four-hoop court with one parked living per team.
struct BballFourCourt : ModesCtfWorld
{
    walker* players[4] = {nullptr, nullptr, nullptr, nullptr};

    BballFourCourt() : ModesCtfWorld(kBballLevelB)
    {
        for (int team = 0; team < 4; ++team)
        {
            const short x = static_cast<short>(128 + 64 * team);
            spawn_anchor(team, x, 700);
            players[team] = spawn_living(FAMILY_SOLDIER, team, x, 700);
        }
    }
};

}  // namespace

TEST_F(ModesBasketball, four_team_court_attribution)
{
    // Team 2 dunks on team 0's hoop: POINTS[2] alone moves.
    {
        BballFourCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        fx.world().mode.vars[kBbJumpUntil] = 0;
        walker* c = fx.players[2];
        c->setxy(392, 472);  // center (400, 480)
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateFree;
        vars[kBbBallPx] = 400 * 256;
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 0;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 0;
        fx.tick(1);
        ASSERT_EQ(static_cast<std::int32_t>(c->entity_id()),
                  fx.var(kBbCarrier));
        c->setxy(312, 56);  // center (320, 64) == team 0's hoop
        fx.tick(1);
        EXPECT_EQ(2, fx.team_var(kBbPoints, 2));
        EXPECT_EQ(0, fx.team_var(kBbPoints, 0));
        EXPECT_EQ(0, fx.team_var(kBbPoints, 1));
        EXPECT_EQ(0, fx.team_var(kBbPoints, 3));
        EXPECT_TRUE(has_notification(fx.events, "DUNK! BLUE +2"));
    }
    // An own-basket crossing pays LAST_TOUCH2 — the side that forced it.
    {
        BballFourCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        fx.world().mode.vars[kBbJumpUntil] = 0;
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateRebound;
        vars[kBbLastTouch1] = 1;   // team 0 crosses its own hoop
        vars[kBbLastTouch2] = 3;   // team 2 forced the play
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 64 * 256;
        vars[kBbBallPz] = 33 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -300;
        const std::uint32_t score_before = fx.world().m_score[2];
        fx.tick(1);
        EXPECT_EQ(2, fx.team_var(kBbPoints, 2));
        EXPECT_EQ(0, fx.team_var(kBbPoints, 0));
        EXPECT_EQ(score_before + 2 * kPointScore, fx.world().m_score[2]);
        EXPECT_TRUE(has_notification(fx.events, "OWN BASKET! BLUE +2"));
    }
    // Nobody with a claim: the forfeit touches the POINTS metric only —
    // og.award_score is unsigned and never sees a negative (I3).
    {
        BballFourCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        fx.world().mode.vars[kBbJumpUntil] = 0;
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateRebound;
        vars[kBbLastTouch1] = 1;
        vars[kBbLastTouch2] = 0;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 64 * 256;
        vars[kBbBallPz] = 33 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -300;
        const std::uint32_t score_before = fx.world().m_score[0];
        fx.tick(1);
        EXPECT_EQ(-2, fx.team_var(kBbPoints, 0)) << "the forfeit goes negative";
        EXPECT_EQ(score_before, fx.world().m_score[0])
            << "m_score is untouched — no unsigned wrap";
        EXPECT_TRUE(has_notification(fx.events, "OWN BASKET! RED -2"));
    }
    // Two active teams: the lone rival is credited unconditionally.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateRebound;
        vars[kBbLastTouch1] = 1;  // team 0 into its own west hoop
        vars[kBbBallPx] = 64 * 256;
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 33 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -300;
        fx.tick(1);
        EXPECT_EQ(2, fx.team_var(kBbPoints, 1)) << "the opponent banks it";
        EXPECT_EQ(0, fx.team_var(kBbPoints, 0));
        EXPECT_TRUE(has_notification(fx.events, "OWN BASKET! GREEN +2"));
    }
    // Defensive arm: a two-team mask holding only the crossing side has
    // no rival to credit — the forfeit rule applies.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbTeamMask] = 1;  // team 0 alone
        vars[kBbBallState] = kStateRebound;
        vars[kBbLastTouch1] = 1;
        vars[kBbBallPx] = 64 * 256;
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 33 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -300;
        fx.tick(1);
        EXPECT_EQ(-2, fx.team_var(kBbPoints, 0));
        EXPECT_TRUE(has_notification(fx.events, "OWN BASKET! RED -2"));
    }
    // An UNTOUCHED crossing scores nothing and plays on (§3.3).
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateRebound;
        vars[kBbLastTouch1] = 0;
        vars[kBbBallPx] = 64 * 256;
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 33 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -300;
        fx.tick(1);
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState)) << "play on";
        EXPECT_EQ(0, fx.team_var(kBbPoints, 0));
        EXPECT_EQ(0, fx.team_var(kBbPoints, 1));
        EXPECT_EQ(0, count_notifications(fx.events, "BASKET!"));
    }
}

// ===========================================================================
// §11.2 #19 — respawns honor the difficulty submenu
// ===========================================================================

namespace {

struct BballRespawnCourt : BballCourt
{
    walker* hero = nullptr;
    walker* bot = nullptr;

    BballRespawnCourt()
    {
        hero = spawn_hero(FAMILY_SOLDIER, 0, 160, 448, 7);
        bot = spawn_living(FAMILY_SOLDIER, 0, 160, 480, ACT_GUARD);
    }

    void kill_both()
    {
        hero->set_dead(1);
        bot->set_dead(1);
    }
};

}  // namespace

TEST_F(ModesBasketball, respawn_honors_submenu)
{
    // Off: nobody comes back.
    {
        BballRespawnCourt fx;
        fx.world().respawn_mode = 0;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.kill_both();
        fx.tick(3);
        EXPECT_FALSE(player_revive_pending(fx.world(), fx.hero->entity_id()));
        EXPECT_EQ(0, ai_entries_for_team(fx.world(), 0));
        fx.tick(70);
        EXPECT_TRUE(fx.hero->dead());
    }
    // Heroes: the roster corpse revives at its own anchors; AI stays down.
    {
        BballRespawnCourt fx;
        fx.world().respawn_mode = 1;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.kill_both();
        fx.tick(2);
        EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
        EXPECT_EQ(0, ai_entries_for_team(fx.world(), 0));
        fx.tick(70);  // respawn_ticks 60 + slack
        EXPECT_FALSE(fx.hero->dead());
        const bool at_anchor =
            (fx.hero->xpos() == 128 &&
             (fx.hero->ypos() == 448 || fx.hero->ypos() == 512));
        EXPECT_TRUE(at_anchor)
            << "on_respawn places at a team-0 anchor, got ("
            << fx.hero->xpos() << "," << fx.hero->ypos() << ")";
        EXPECT_GT(fx.var(kBbAnchorCursor), 0) << "anchor cursor rotated";
        EXPECT_TRUE(fx.bot->dead());
    }
    // Everyone: the unowned AI corpse is scheduled too.
    {
        BballRespawnCourt fx;
        fx.world().respawn_mode = 2;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.kill_both();
        fx.tick(2);
        EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
        EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0));
    }
}

// ===========================================================================
// §11.2 #20 — director roles
// ===========================================================================

TEST_F(ModesBasketball, director_roles)
{
    // A HUMAN carrier: offense plays around it; the threatened defense
    // fields on-ball defender, rim protector and staggered HELP with the
    // single permitted peel.
    BballCourt fx;
    // Offense (team 0, ACT_SIT): two cutters + one seam holder.
    walker* r2 = fx.spawn_living(FAMILY_SOLDIER, 0, 332, 292, ACT_SIT);
    walker* r3 = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 642, ACT_SIT);
    walker* r4 = fx.spawn_living(FAMILY_SOLDIER, 0, 192, 472, ACT_SIT);
    // Defense (team 1, ACT_SIT).
    walker* g2 = fx.spawn_living(FAMILY_SOLDIER, 1, 490, 540, ACT_SIT);
    walker* g3 = fx.spawn_living(FAMILY_SOLDIER, 1, 552, 462, ACT_SIT);
    walker* g4 = fx.spawn_living(FAMILY_SOLDIER, 1, 492, 592, ACT_SIT);
    walker* g5 = fx.spawn_living(FAMILY_SOLDIER, 1, 292, 692, ACT_SIT);
    walker* g6 = fx.spawn_living(FAMILY_SOLDIER, 1, 312, 712, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 450, 480);  // human (ACT_CONTROL) carrier

    // Two of the greens are mid-brawl: exactly one may keep its fight.
    for (walker* w : {g5, g6})
    {
        w->set_foe(fx.red);
        w->stats()->force_command(COMMAND_ATTACK, 30, 1, 1);
    }
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);

    // The human carrier is untouched.
    EXPECT_EQ(0, front_type(fx.red)) << "a human handler is never commanded";

    // Offense: cutters flank the target hoop (576,480); jump (320,480) is
    // due west, so the posts sit at (536, 448) and (536, 512).
    EXPECT_TRUE(front_command_is(r3, COMMAND_GOTO, 528, 440))
        << "first cutter takes the +perp post";
    EXPECT_TRUE(front_command_is(r4, COMMAND_GOTO, 528, 504))
        << "second cutter takes the -perp post";
    // The rest hold the own-hoop/center seam: mid (192,480); the FIRST
    // seam holder takes no stagger (the {0,+16,-16} fan).
    EXPECT_TRUE(front_command_is(r2, COMMAND_GOTO, 184, 472))
        << "remaining offense holds the seam midpoint itself";
    EXPECT_EQ(fx.var(kBbBallEntity),
              static_cast<std::int32_t>(r2->leader_id()))
        << "scheme members lead on the ball (auto-foe suppression)";

    // Defense: on-ball defender is commanded ONTO the carrier center.
    EXPECT_TRUE(front_command_is(g2, COMMAND_GOTO, 442, 472))
        << "on-ball defender drives onto the carrier";
    EXPECT_EQ(fx.red, g2->foe()) << "the steal: foe set on the carrier";
    // Rim protector posts in the dunk lane: hoop (576,480), carrier due
    // west -> (544, 480).
    EXPECT_TRUE(front_command_is(g3, COMMAND_GOTO, 536, 472))
        << "rim protector holds the lane standoff";
    // HELP: midpoint (513,480) with the {0,+16,-16} fan — g4 holds the
    // midpoint itself; g5 keeps its fight (the one peel), g6 is pulled
    // back onto the +16 slot.
    EXPECT_TRUE(front_command_is(g4, COMMAND_GOTO, 505, 472));
    EXPECT_EQ(COMMAND_ATTACK, front_type(g5))
        << "exactly one defender may keep an active combat command";
    EXPECT_TRUE(front_command_is(g6, COMMAND_GOTO, 505, 488))
        << "the second brawler is pulled into the HELP fan";
}

// §4.4: a NON-threatened defending team sends one vulture and keeps the
// rest HOME — the HELP fan centered on its own hoop, not the carrier
// midpoint.
TEST_F(ModesBasketball, director_nonthreatened_defense_stays_home)
{
    BballCourt fx(kBballLevelB);  // four-hoop court; teams 0-2 anchored
    fx.spawn_anchor(2, 256, 800);
    fx.spawn_anchor(2, 256, 840);
    walker* vulture = fx.spawn_living(FAMILY_SOLDIER, 2, 442, 692, ACT_SIT);
    walker* homer = fx.spawn_living(FAMILY_SOLDIER, 2, 232, 852, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    // Human carrier at (500,480): nearest enemy hoop is team 1's E hoop
    // (576,480), so team 2 (own hoop 320,896) is NOT threatened.
    fx.give_ball(fx.red, 500, 480);
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);

    EXPECT_EQ(fx.red, vulture->foe())
        << "the one opportunist hunts the fumble";
    EXPECT_TRUE(front_command_is(vulture, COMMAND_GOTO, 492, 472))
        << "the vulture is commanded onto the carrier center";
    EXPECT_TRUE(front_command_is(homer, COMMAND_GOTO, 312, 888))
        << "the remaining member posts ON its own hoop (320,896), not at "
           "the carrier midpoint (410,688)";
}

TEST_F(ModesBasketball, director_loose_shot_flight_and_faceoff)
{
    // Loose ball: the nearest TWO race onto the ground center; the rest
    // hold the own-hoop/ball seam.
    {
        BballCourt fx;
        walker* r2 = fx.spawn_living(FAMILY_SOLDIER, 0, 212, 372, ACT_SIT);
        walker* r3 = fx.spawn_living(FAMILY_SOLDIER, 0, 412, 472, ACT_SIT);
        walker* r4 = fx.spawn_living(FAMILY_SOLDIER, 0, 112, 672, ACT_SIT);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.set_ball_free(320, 480);
        align_before_cadence(fx.world());
        fx.tick(1);
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
        EXPECT_TRUE(front_command_is(r3, COMMAND_GOTO, 312, 472))
            << "nearest chaser drives onto the ball — arriving IS pickup";
        EXPECT_TRUE(front_command_is(r2, COMMAND_GOTO, 312, 472))
            << "the second racer too";
        EXPECT_TRUE(front_command_is(r4, COMMAND_GOTO, 184, 472))
            << "the rest hold the seam (mid 192,480; first holder "
               "unstaggered)";
    }
    // Shot flight: every team's nearest member boxes out at the TARGET
    // hoop off public geometry — never SHOT_LAND (D26).
    {
        BballCourt fx;
        walker* r3 = fx.spawn_living(FAMILY_SOLDIER, 0, 412, 472, ACT_SIT);
        walker* g2 = fx.spawn_living(FAMILY_SOLDIER, 1, 600, 500, ACT_SIT);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(600, 460);  // scatter outcome: private
        vars[kBbFlightTicks] = 40;
        vars[kBbBallPx] = 400 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 40 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 100;
        align_before_cadence(fx.world());
        fx.tick(1);
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
        EXPECT_TRUE(front_command_is(r3, COMMAND_GOTO, 556, 472))
            << "team 0 boxes out own-hoop-side of the rim (564,480)";
        EXPECT_TRUE(front_command_is(g2, COMMAND_GOTO, 580, 472))
            << "the defending rebounder posts at (588,480)";
    }
    // Jump freeze: a face-off ring 24 px own-hoop-side of the tip spot.
    {
        BballCourt fx;
        walker* r2 = fx.spawn_living(FAMILY_SOLDIER, 0, 212, 372, ACT_SIT);
        walker* g2 = fx.spawn_living(FAMILY_SOLDIER, 1, 600, 500, ACT_SIT);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        ASSERT_GT(fx.var(kBbJumpUntil),
                  static_cast<std::int32_t>(fx.world().tick_count_));
        align_before_cadence(fx.world());
        fx.tick(1);
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
        EXPECT_TRUE(front_command_is(r2, COMMAND_GOTO, 288, 472))
            << "team 0 faces off west of the spot";
        EXPECT_TRUE(front_command_is(g2, COMMAND_GOTO, 336, 472))
            << "team 1 east of it";
    }
}

// ===========================================================================
// §11.2 #21 — spawn caps pause and resume generators
// ===========================================================================

TEST_F(ModesBasketball, spawn_caps_pause_generators)
{
    BballCourt fx(kBballLevelCaps);
    walker* tent = fx.spawn_generator(FAMILY_TENT, 0, 200, 200);
    ASSERT_NE(nullptr, tent);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    ASSERT_EQ(2, fx.team_var(kBbSpawnCap, 0));
    const float authored = tent->fire_frequency();

    // Two marked team-0 spawns == the cap: the tent pauses at cadence.
    walker* s1 = fx.spawn_living(FAMILY_SKELETON, 0, 250, 200, ACT_GUARD);
    walker* s2 = fx.spawn_living(FAMILY_SKELETON, 0, 250, 230, ACT_GUARD);
    s1->stats()->set_bit_flags(kSpawnMarkBit, 1);
    s2->stats()->set_bit_flags(kSpawnMarkBit, 1);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_GE(tent->fire_frequency(),
              static_cast<float>(kPauseFireFrequency))
        << "at the cap the generator pauses";

    s1->set_dead(1);
    s2->set_dead(1);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(authored, tent->fire_frequency()) << "resume restores authored";
}

// ===========================================================================
// §11.2 #22 — mirror replication: ball + shadow + frames on the wire (I4)
// ===========================================================================

TEST_F(ModesBasketball, mirror_replication_120_ticks)
{
    // Team 1 fields a bot squad (anchors only); team 0 fields one parked
    // walker that stages a real mid-arc shot before the window opens.
    ModesCtfWorld fx(kBballLevelA);
    fx.spawn_anchor(0, 128, 448);
    fx.spawn_anchor(0, 128, 512);
    fx.spawn_anchor(1, 512, 448);
    fx.spawn_anchor(1, 512, 512);
    walker* shooter = fx.spawn_living(FAMILY_SOLDIER, 0, 442, 472);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    fx.world().mode.vars[kBbJumpUntil] = 0;
    fx.world().mode.vars[kBbBallState] = kStateFree;
    fx.world().mode.vars[kBbBallPx] = 450 * 256;
    fx.world().mode.vars[kBbBallPy] = 480 * 256;
    fx.tick(1);
    ASSERT_EQ(static_cast<std::int32_t>(shooter->entity_id()),
              fx.var(kBbCarrier));
    walker* wpn = fx.world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, wpn);
    wpn->setxy(460, 560);
    wpn->set_owner(shooter);
    wpn->set_lastx(8.0f);
    wpn->set_lasty(0.0f);
    fx.tick(1);
    ASSERT_EQ(kStateShot, fx.var(kBbBallState)) << "the arc is in the air";

    ModeMirror mirror(kBballLevelA);
    const MirrorReplication replication = replicate_to_mirror(fx, mirror, 120);
    EXPECT_EQ(0, replication.strikes)
        << "the mirror first desynced at tick " << replication.first_strike_tick
        << "; 120 consecutive strikes disconnect the client";
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);

    // Family bytes above the core span travel the wire (I5/R5).
    walker* const ball = fx.world().find_by_id(
        static_cast<std::uint32_t>(fx.var(kBbBallEntity)));
    walker* const shadow = fx.world().find_by_id(
        static_cast<std::uint32_t>(fx.var(kBbShadowEntity)));
    ASSERT_NE(nullptr, ball);
    ASSERT_NE(nullptr, shadow);
    EXPECT_GE(static_cast<int>(ball->family()), NUM_FAMILIES)
        << "modes:bball must be a pack family for this test to bite";
    EXPECT_EQ(static_cast<int>(ball->family()) + 1,
              static_cast<int>(shadow->family()))
        << "fx-bshadow sorts (and numbers) directly after fx-bball";

    walker* const m_ball = mirror.world().find_by_id(ball->entity_id());
    ASSERT_NE(nullptr, m_ball) << "the mirror must materialize the ball";
    EXPECT_EQ(static_cast<int>(ball->family()),
              static_cast<int>(m_ball->family()));
    EXPECT_EQ(ball->xpos(), m_ball->xpos());
    EXPECT_EQ(ball->ypos(), m_ball->ypos());
    EXPECT_EQ(ball->team_num(), m_ball->team_num());
    EXPECT_EQ(ball->frame(), m_ball->frame())
        << "the drawn spin frame replicates — mirrors never animate()";

    walker* const m_shadow = mirror.world().find_by_id(shadow->entity_id());
    ASSERT_NE(nullptr, m_shadow) << "the fxlist shadow must replicate too";
    EXPECT_EQ(static_cast<int>(shadow->family()),
              static_cast<int>(m_shadow->family()));
    EXPECT_EQ(shadow->xpos(), m_shadow->xpos());
    EXPECT_EQ(shadow->ypos(), m_shadow->ypos());
    EXPECT_EQ(shadow->frame(), m_shadow->frame())
        << "the altitude frame replicates";

    for (int slot = 0; slot < og::sim::kModeVarCount; ++slot)
    {
        EXPECT_EQ(fx.world().mode.vars[static_cast<std::size_t>(slot)],
                  mirror.world().mode.vars[static_cast<std::size_t>(slot)])
            << "mode var slot " << slot;
    }
    EXPECT_EQ(shadow->entity_id(),
              static_cast<std::uint32_t>(
                  mirror.world().mode.beacons[0].entity_id))
        << "beacon 0 must point at the replicated shadow";
}

// ===========================================================================
// §11.2 #23 — determinism digest (4-team bot match, fxlist included)
// ===========================================================================

namespace {

std::string run_bball_bot_match_digest(int ticks)
{
    ModesCtfWorld fx(kBballLevelB);
    for (int team = 0; team < 4; ++team)
    {
        const short x = static_cast<short>(128 + 64 * team);
        fx.spawn_anchor(team, x, 680);
        fx.spawn_anchor(team, x, 720);
    }
    fx.tick(ticks);  // bot squads for all four teams play the ball
    EXPECT_TRUE(fx.world().mode.active);
    return digest_world(fx.world());
}

}  // namespace

TEST_F(ModesBasketball, determinism_digest)
{
    const std::string first = run_bball_bot_match_digest(300);
    const std::string second = run_bball_bot_match_digest(300);
    ASSERT_EQ(first, second)
        << "same seed + same court must reproduce throws, arcs and roles";
}

// R4: a full bot match never touches slot 63 — the last spare stays spare.
TEST_F(ModesBasketball, slot_budget_leaves_63_spare)
{
    ModesCtfWorld fx(kBballLevelB);
    for (int team = 0; team < 4; ++team)
    {
        const short x = static_cast<short>(128 + 64 * team);
        fx.spawn_anchor(team, x, 680);
        fx.spawn_anchor(team, x, 720);
    }
    fx.tick(150);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0, fx.var(kBbSpare))
        << "slot 63 is the LAST spare (R4) — nothing may write it";
}

// ===========================================================================
// §11.2 #24 — instruction-budget headroom (10x-reduced budget)
// ===========================================================================

namespace {

// RAII so an early ASSERT return cannot leak the 10x-reduced budget into
// every later test in the binary.
struct BudgetOverride
{
    explicit BudgetOverride(std::int64_t v)
    {
        og::script::g_test_world_instruction_budget = v;
    }
    ~BudgetOverride() { og::script::g_test_world_instruction_budget = 0; }
};

}  // namespace

TEST_F(ModesBasketball, instruction_budget_headroom)
{
    const BudgetOverride budget(500000);
    ModesCtfWorld fx(kBballLevelA);
    fx.spawn_anchor(0, 128, 448);
    fx.spawn_anchor(0, 128, 512);
    fx.spawn_anchor(1, 512, 448);
    fx.spawn_anchor(1, 512, 512);
    fx.tick(1);  // init (bot squads + ball + shadow) under the budget
    ASSERT_TRUE(fx.world().mode.active);
    fx.tick(45);  // 3 director cadences + the toss + flight + HUD
    EXPECT_FALSE(has_script_error(fx.world(), "instruction budget"))
        << "a 10x-reduced budget must never trip";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// §11.2 #25 — landing legality: the deterministic ring re-spot
// ===========================================================================

TEST_F(ModesBasketball, landing_legality_respots)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    // A 3x3 wall block at tiles (10..12, 10..12) — px 160..207 square.
    for (int gy = 10; gy <= 12; ++gy)
        for (int gx = 10; gx <= 12; ++gx)
            fx.world().grid.data[static_cast<std::size_t>(gx + fx.world().grid.w * gy)] = PIX_H_WALL1;

    // A rebound falling onto the block's center tile: the L1 ring scan
    // walks r=1 (all wall) then r=2 clockwise from due north — tile
    // (11,9) is the FIRST candidate and takes the ball.
    auto& vars = fx.world().mode.vars;
    vars[kBbBallState] = kStateRebound;
    vars[kBbLastTouch1] = 2;
    vars[kBbBallPx] = 184 * 256;
    vars[kBbBallPy] = 184 * 256;
    vars[kBbBallPz] = 256;
    vars[kBbBallVx] = 0;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = -300;
    fx.tick(1);

    EXPECT_EQ(kStateFree, fx.var(kBbBallState)) << "a FREE dead ball";
    EXPECT_EQ(184, fx.ball_cx()) << "tile (11,9) center x";
    EXPECT_EQ(152, fx.ball_cy()) << "tile (11,9) center y — due north first";
    EXPECT_EQ(0, fx.var(kBbBallVx));
    EXPECT_EQ(0, fx.var(kBbBallVz));
    EXPECT_EQ(0, fx.ball_z_px());
    EXPECT_EQ(2, fx.var(kBbLastTouch1)) << "attribution kept";
    EXPECT_EQ(0, fx.var(kBbGraceUntil)) << "no grace — a neutral scramble";

    // Pathological arm: no passable candidate within 8 rings — the scan
    // gives up into a full center reset.
    for (int gy = 5; gy <= 23; ++gy)
        for (int gx = 5; gx <= 23; ++gx)
            fx.world().grid.data[static_cast<std::size_t>(gx + fx.world().grid.w * gy)] = PIX_H_WALL1;
    vars[kBbBallState] = kStateRebound;
    vars[kBbBallPx] = 232 * 256;  // tile (14,14): rings 1..8 all walled
    vars[kBbBallPy] = 232 * 256;
    vars[kBbBallPz] = 256;
    vars[kBbBallVx] = 0;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = -300;
    fx.tick(1);
    EXPECT_EQ(kStateFree, fx.var(kBbBallState));
    EXPECT_EQ(kBballJumpX, fx.ball_cx()) << "the total fallback is center";
    EXPECT_EQ(0, fx.var(kBbLastTouch1)) << "a center reset clears history";
    EXPECT_GT(fx.var(kBbJumpUntil),
              static_cast<std::int32_t>(fx.world().tick_count_));
}

// ===========================================================================
// §11.2 #26 — the D19 provenance watermark
// ===========================================================================

TEST_F(ModesBasketball, stale_inflight_weapon_not_consumed)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    // A weapon already in flight BEFORE the pickup.
    walker* stale = fx.spawn_weapon(fx.red, 200, 200, 2.0f, 0.0f);
    ASSERT_NE(nullptr, stale);
    fx.give_ball(fx.red, 350, 480);
    ASSERT_EQ(static_cast<std::int32_t>(stale->entity_id()),
              fx.var(kBbThrowWatermark));

    fx.tick(2);
    EXPECT_EQ(kStateCarried, fx.var(kBbBallState))
        << "the first CARRIED ticks release no phantom throw";
    EXPECT_EQ(0, static_cast<int>(stale->dead()))
        << "the stale weapon flies on untouched (D19)";

    // A weapon fired AFTER the pickup IS consumed.
    walker* fresh = fx.spawn_weapon(fx.red, 400, 560, 0.0f, -8.0f);
    ASSERT_NE(nullptr, fresh);
    fx.tick(1);
    EXPECT_NE(0, static_cast<int>(fresh->dead())) << "consumed";
    EXPECT_EQ(kStatePass, fx.var(kBbBallState)) << "a flat throw north";
    EXPECT_EQ(0, static_cast<int>(stale->dead()))
        << "only the above-watermark weapon was eaten";
}

// ===========================================================================
// §11.2 #27 — clock persistence across loose balls; late regains turn over
// ===========================================================================

TEST_F(ModesBasketball, clock_persists_through_pass_and_regrab)
{
    // The anti-launder rule: an uncaught own throw rolled dead and
    // regrabbed never refreshes the deadline.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 320, 480);
        const std::int32_t deadline = fx.var(kBbClockUntil);
        ASSERT_GT(deadline, 0);

        walker* wpn = fx.spawn_weapon(fx.red, 400, 560, 0.0f, -8.0f);
        ASSERT_NE(nullptr, wpn);
        fx.tick(1);  // flat throw north (no hoop in range, no teammate)
        ASSERT_EQ(kStatePass, fx.var(kBbBallState));
        EXPECT_EQ(deadline, fx.var(kBbClockUntil))
            << "a PASS release keeps the clock running (§3.6)";
        EXPECT_EQ(1, fx.var(kBbClockTeam1));

        // Let it land, bounce and settle to FREE (T14 then T17).
        for (int i = 0; i < 60 && fx.var(kBbBallState) != kStateFree; ++i)
            fx.tick(1);
        ASSERT_EQ(kStateFree, fx.var(kBbBallState));
        EXPECT_EQ(deadline, fx.var(kBbClockUntil))
            << "no loose-ball transition clears the deadline (D25)";

        // The same team regains: the deadline PERSISTS unchanged.
        fx.set_ball_free(320, 480);  // red is still at (320,480)
        fx.tick(1);
        ASSERT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                  fx.carrier());
        EXPECT_EQ(deadline, fx.var(kBbClockUntil))
            << "a deliberate miss is never better than a catch";
        EXPECT_EQ(1, fx.var(kBbClockTeam1));
        EXPECT_EQ(0, count_notifications(fx.events, "TURNOVER!"));
    }
    // A CATCH by the clock team past the deadline turns over on the spot.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.red->setxy(392, 392);  // center (400, 400)
        auto& vars = fx.world().mode.vars;
        vars[kBbClockTeam1] = 1;
        vars[kBbClockUntil] = static_cast<std::int32_t>(fx.world().tick_count_);
        vars[kBbBallState] = kStatePass;
        vars[kBbPassTarget] =
            static_cast<std::int32_t>(fx.red->entity_id());
        vars[kBbFlightTicks] = 8;
        vars[kBbBallPx] = 400 * 256;
        vars[kBbBallPy] = 400 * 256;
        vars[kBbBallPz] = 12 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 0;
        fx.tick(1);
        EXPECT_EQ(1, count_notifications(fx.events, "TURNOVER!"))
            << "rule 1 of §3.6: the late catch is an immediate turnover";
        EXPECT_EQ(0, fx.carrier());
        EXPECT_EQ(0, fx.var(kBbClockUntil));
        EXPECT_EQ(1, fx.var(kBbGraceTeam1));
        // The mid-catch turnover is the one T7 entrance with live flight
        // facts: drop_ball must scrub them (a stale PASS_TARGET would
        // silently exclude the ex-receiver from every run_loose scheme).
        EXPECT_EQ(0, fx.var(kBbPassTarget))
            << "no pass residue on the REBOUND";
        EXPECT_EQ(0, fx.var(kBbFlightTicks));
    }
    // A late ground PICKUP by the clock team also turns over.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.red->setxy(392, 392);
        auto& vars = fx.world().mode.vars;
        vars[kBbClockTeam1] = 1;
        vars[kBbClockUntil] = static_cast<std::int32_t>(fx.world().tick_count_);
        fx.set_ball_free(400, 400);
        // set_ball_free cleared nothing of the clock: re-assert.
        vars[kBbClockTeam1] = 1;
        vars[kBbClockUntil] = static_cast<std::int32_t>(fx.world().tick_count_);
        fx.tick(1);
        EXPECT_EQ(1, count_notifications(fx.events, "TURNOVER!"));
        EXPECT_EQ(0, fx.carrier());
        EXPECT_EQ(0, fx.var(kBbClockUntil));
    }
}

// ===========================================================================
// §11.2 #28 — post-turnover semantics: disarmed clock, grace, re-arms
// ===========================================================================

TEST_F(ModesBasketball, turnover_regrab_defined)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 350, 480);
    // Shrink the deadline instead of holding 420 ticks.
    fx.world().mode.vars[kBbClockUntil] =
        static_cast<std::int32_t>(fx.world().tick_count_) + 2;
    fx.tick(2);
    ASSERT_EQ(1, count_notifications(fx.events, "TURNOVER!"));
    const std::int32_t turnover_tick =
        static_cast<std::int32_t>(fx.world().tick_count_);
    EXPECT_EQ(turnover_tick + kTurnoverGrace, fx.var(kBbGraceUntil));

    // Disarmed clock: the barred team milling next to the ball never
    // re-fires the announce (the D25 poison-loop regression).
    fx.set_ball_free(350, 480);
    fx.tick(30);
    EXPECT_EQ(1, count_notifications(fx.events, "TURNOVER!"))
        << "no repeat announce after the clock cleared";
    EXPECT_EQ(0, fx.carrier()) << "the ex-team stays barred";

    // After the 120-tick grace lifts, the ex-team's pickup arms a FRESH
    // clock. The ball is already spotted under red: the pickup fires on
    // the first tick the bar is down.
    const std::int32_t grace_until = fx.var(kBbGraceUntil);
    while (fx.carrier() == 0 &&
           static_cast<std::int32_t>(fx.world().tick_count_) <
               grace_until + 5)
        fx.tick(1);
    EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()), fx.carrier())
        << "grace expired: the ex-team may play again";
    EXPECT_GE(static_cast<std::int32_t>(fx.world().tick_count_), grace_until)
        << "and not one tick sooner";
    EXPECT_EQ(1, fx.var(kBbClockTeam1));
    EXPECT_EQ(static_cast<std::int32_t>(fx.world().tick_count_) + kShotClock,
              fx.var(kBbClockUntil)) << "a fresh 420";

    // Charm mid-possession: the clock re-arms for the carrier's live team
    // whenever it stops matching CLOCK_TEAM1.
    fx.world().mode.vars[kBbClockTeam1] = 2;  // stage the mismatch
    fx.tick(1);
    EXPECT_EQ(1, fx.var(kBbClockTeam1)) << "re-armed for the live team";
    EXPECT_EQ(static_cast<std::int32_t>(fx.world().tick_count_) + kShotClock,
              fx.var(kBbClockUntil));
}

// ===========================================================================
// §11.2 #29 — the grace bar stays pinned on GRACE_ENTITY through a block
// ===========================================================================

TEST_F(ModesBasketball, grace_stays_on_thrower_after_block)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 320, 480);
    const std::int32_t red_id =
        static_cast<std::int32_t>(fx.red->entity_id());
    walker* wpn = fx.spawn_weapon(fx.red, 400, 560, 0.0f, -8.0f);
    ASSERT_NE(nullptr, wpn);
    fx.tick(1);  // flat throw north; self grace armed on red
    ASSERT_EQ(kStatePass, fx.var(kBbBallState));
    ASSERT_EQ(red_id, fx.var(kBbGraceEntity));
    const std::int32_t grace_until = fx.var(kBbGraceUntil);
    fx.tick(1);  // ball now at (320, 464)

    // Green's weapon swats the low pass: attribution restamps, the bar
    // does not move (D24). The swat scan runs BEFORE movement, so it sees
    // the ball where the previous tick left it — (320, 464).
    walker* swat = fx.spawn_weapon(fx.green, 317, 461, 4.0f, 0.0f);
    ASSERT_NE(nullptr, swat);  // 6x6 knife center (320, 464)
    fx.tick(1);
    ASSERT_EQ(kStateRebound, fx.var(kBbBallState)) << "the block landed";
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()),
              fx.var(kBbLastToucher)) << "attribution moved to the blocker";
    EXPECT_EQ(2, fx.var(kBbLastTouch1));
    EXPECT_EQ(red_id, fx.var(kBbGraceEntity))
        << "the entity bar still names the thrower";
    EXPECT_EQ(grace_until, fx.var(kBbGraceUntil));

    // Inside the window: the thrower cannot re-grab, the blocker can —
    // immediately.
    ASSERT_LT(static_cast<std::int32_t>(fx.world().tick_count_), grace_until)
        << "still inside the self-grace window";
    fx.set_ball_free(320, 480);  // under red
    fx.tick(1);
    EXPECT_EQ(0, fx.carrier()) << "the barred thrower cannot take it";
    fx.green->setxy(392, 392);
    fx.set_ball_free(400, 400);  // under green
    fx.tick(1);
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()), fx.carrier())
        << "the blocker grabs what it earned (D24)";
}

// ===========================================================================
// §11.2 #30 — joint classification picks the best-aligned target (D23d)
// ===========================================================================

TEST_F(ModesBasketball, classification_prefers_best_aligned_target)
{
    // Aimed dead at a cutter posted hoop-side: a PASS, even though the
    // hoop is in range behind it.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        walker* cutter = fx.spawn_living(FAMILY_SOLDIER, 0, 482, 492);
        ASSERT_NE(nullptr, cutter);  // center (490, 500)
        fx.give_ball(fx.red, 420, 480);
        walker* wpn = fx.spawn_weapon(fx.red, 200, 700, 70.0f, 20.0f);
        ASSERT_NE(nullptr, wpn);  // exact cutter aim; hoop 3120 off-cross
        fx.tick(1);

        EXPECT_EQ(kStatePass, fx.var(kBbBallState))
            << "the exactly-aligned cutter beats the off-axis hoop";
        EXPECT_EQ(static_cast<std::int32_t>(cutter->entity_id()),
                  fx.var(kBbPassTarget));
        EXPECT_EQ(0, fx.var(kBbShotValue));
    }
    // Aimed dead at the hoop with the cutter off-axis: a SHOT.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        walker* cutter = fx.spawn_living(FAMILY_SOLDIER, 0, 482, 492);
        ASSERT_NE(nullptr, cutter);
        fx.give_ball(fx.red, 420, 480);
        walker* wpn = fx.spawn_weapon(fx.red, 200, 700, 8.0f, 0.0f);
        ASSERT_NE(nullptr, wpn);
        fx.tick(1);

        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "dead-on hoop aim wins over the in-cone cutter";
        EXPECT_EQ(2, fx.var(kBbShotHoop1));
        EXPECT_EQ(0, fx.var(kBbPassTarget));
    }
}

// ===========================================================================
// §11.2 #31 — the bot handler ladder (D23c order)
// ===========================================================================

TEST_F(ModesBasketball, handler_ladder_shot_over_drive_when_open)
{
    // Rung 1: open inside shot_sweet -> SHOOT.
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 492, 472, ACT_SIT);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 500, 480);  // 76 px Euclid <= sweet 88, nobody near
        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "an open bot in the sweet range releases the shot";
        EXPECT_EQ(1, fx.var(kBbShotTeam1));
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Rung 2: the same spot under pressure -> DRIVE through the hoop.
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 492, 472, ACT_SIT);
        walker* presser = fx.spawn_living(FAMILY_SOLDIER, 1, 477, 472);
        ASSERT_NE(nullptr, presser);  // center (485, 480): 15 L1 <= 24
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 500, 480);
        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(kStateCarried, fx.var(kBbBallState))
            << "pressed: no jumper";
        EXPECT_TRUE(front_command_is(bot, COMMAND_GOTO, 576, 472))
            << "the drive targets hoop + dir8 * drive_offset (584,480)";
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Rung 4: an open cutter meaningfully closer earns the pass; a covered
    // cutter is refused and the handler advances instead (rung 5).
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 342, 472, ACT_SIT);
        walker* cutter = fx.spawn_living(FAMILY_SOLDIER, 0, 472, 472, ACT_SIT);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 350, 480);  // 226 L1 to hoop: beyond sweet + drive
        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(kStatePass, fx.var(kBbBallState))
            << "the open cutter (96 + 32 <= 226) earns the lob";
        EXPECT_EQ(static_cast<std::int32_t>(cutter->entity_id()),
                  fx.var(kBbPassTarget));
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 342, 472, ACT_SIT);
        walker* cutter = fx.spawn_living(FAMILY_SOLDIER, 0, 472, 472, ACT_SIT);
        walker* cover = fx.spawn_living(FAMILY_SOLDIER, 1, 482, 482);
        ASSERT_NE(nullptr, cutter);
        ASSERT_NE(nullptr, cover);  // center (490,490): 20 L1 of the cutter
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 350, 480);
        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(kStateCarried, fx.var(kBbBallState))
            << "a covered cutter is refused by the open predicate (D23d)";
        EXPECT_EQ(0, fx.var(kBbPassTarget));
        EXPECT_TRUE(front_command_is(bot, COMMAND_GOTO, 576, 472))
            << "rung 5: advance through the drive corridor";
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Rung 4's range gate: a cutter closer to the hoop but beyond
    // pass_range_max must NOT draw the release (it would classify as a
    // 96 px flat heave — a wasted possession); the handler advances
    // (rung 5) instead.
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 342, 472, ACT_SIT);
        walker* cutter = fx.spawn_living(FAMILY_SOLDIER, 0, 552, 472, ACT_SIT);
        ASSERT_NE(nullptr, cutter);  // center (560,480): 16 L1 to the hoop,
                                     // 210 L1 from the carrier (> 192)
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 350, 480);
        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(kStateCarried, fx.var(kBbBallState))
            << "an out-of-pass-range cutter never draws the heave";
        EXPECT_EQ(0, fx.var(kBbPassTarget));
        EXPECT_TRUE(front_command_is(bot, COMMAND_GOTO, 576, 472))
            << "rung 5: advance through the drive corridor";
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Rung 4's collinear tie: carrier, cutter and an in-range hoop on one
    // exact ray — §3.1 hands the release to the hoop as a SHOT, and the
    // director must NOT command the cutter onto the returned landing
    // point (it is the private SHOT_LAND, D26).
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 392, 472, ACT_SIT);
        walker* cutter = fx.spawn_living(FAMILY_SOLDIER, 0, 492, 472, ACT_SIT);
        ASSERT_NE(nullptr, cutter);  // center (500,480): dead on the
                                     // carrier->hoop ray, 100 L1 away
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 400, 480);  // 176 px: past sweet + drive ranges
        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "the exact tie goes to the hoop (§3.1) — a SHOT releases";
        EXPECT_EQ(2, fx.var(kBbShotHoop1));
        EXPECT_EQ(0, fx.var(kBbPassTarget));
        EXPECT_EQ(0, front_type(cutter))
            << "no landing-point GOTO: SHOT_LAND stays private (D26)";
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Rung 3: clock panic forces the release even from beyond shot range
    // — the heave classifies as a flat throw (accepted, §4.2).
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 342, 472, ACT_SIT);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 350, 480);  // hoop out of range, no teammates
        align_before_cadence(fx.world());
        fx.world().mode.vars[kBbClockUntil] =
            static_cast<std::int32_t>(fx.world().tick_count_) + 1 + 20;
        fx.tick(1);
        EXPECT_EQ(kStatePass, fx.var(kBbBallState))
            << "clock panic: the bot heaves rather than eats the turnover";
        EXPECT_EQ(0, fx.var(kBbPassTarget)) << "a flat heave";
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Rung 5's staging arm: a carrier outside the drive corridor walks the
    // approach point instead of driving through.
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 342, 532, ACT_SIT);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 350, 540);  // 60 px lateral miss > drive_cross_hold
        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(kStateCarried, fx.var(kBbBallState));
        EXPECT_TRUE(front_command_is(bot, COMMAND_GOTO, 528, 472))
            << "the approach point stages hoop - dir8 * cutter_standoff";
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    }
}

// ===========================================================================
// §11.2 #32 — the D21 fumble discipline: damage floor + possession grace
// ===========================================================================

TEST_F(ModesBasketball, chip_damage_no_fumble)
{
    // Amount 1 never fumbles; amount 2 does (after the grace).
    {
        BballCourt fx;
        fx.red->stats()->set_armor(0.0f);
        fx.smash(fx.green, fx.red, 3.0f);  // pre-init: the latch gates it
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        EXPECT_EQ(0, fx.var(kBbFumbleTick))
            << "a hit before the match opens arms nothing";
        fx.smash(fx.green, fx.red, 3.0f);
        EXPECT_EQ(0, fx.var(kBbFumbleTick))
            << "a hit with nobody carrying arms nothing";
        fx.give_ball(fx.red, 350, 480);
        fx.tick(kPossessionGrace);

        // Staged 2.0 lands as exactly 1 (2 - sqrt(2)/2 floors to 1).
        const float hp_before = fx.red->stats()->hitpoints();
        fx.smash(fx.green, fx.red, 2.0f);
        EXPECT_EQ(hp_before - 1.0f, fx.red->stats()->hitpoints())
            << "the chip still hurts";
        EXPECT_EQ(0, fx.var(kBbFumbleTick))
            << "floor-value chip never strips the ball (D21)";
        fx.tick(1);
        EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                  fx.carrier());

        // Target identity: a qualifying hit on a NON-carrier while red
        // carries (past the grace) must never strip the ball.
        fx.green->stats()->set_armor(0.0f);
        fx.smash(fx.red, fx.green, 3.0f);  // lands as 2 on the bystander
        EXPECT_EQ(0, fx.var(kBbFumbleTick))
            << "damage anywhere else on the court never fumbles";
        fx.tick(1);
        EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                  fx.carrier()) << "the carrier keeps the ball";

        fx.smash(fx.green, fx.red, 3.0f);  // lands as 2
        EXPECT_NE(0, fx.var(kBbFumbleTick)) << "threshold damage fumbles";
        fx.tick(1);
        EXPECT_EQ(0, fx.carrier());
    }
    // A qualifying hit INSIDE the possession grace does not fumble.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 350, 480);
        fx.red->stats()->set_armor(0.0f);
        const float hp_before = fx.red->stats()->hitpoints();
        fx.smash(fx.green, fx.red, 3.0f);  // same tick as the gain
        EXPECT_EQ(hp_before - 2.0f, fx.red->stats()->hitpoints())
            << "the hit lands its damage";
        EXPECT_EQ(0, fx.var(kBbFumbleTick))
            << "inside possession_grace the ball stays held (D21)";
        fx.tick(1);
        EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                  fx.carrier());
    }
}

// ===========================================================================
// §11.2 #33 — contested catches (D23a) + DUNK_OK exit-re-entry (D23b)
// ===========================================================================

TEST_F(ModesBasketball, contested_catch_and_box_reentry)
{
    // Same-tick contention: oblist order among the contenders decides.
    {
        BballCourt fx;
        walker* defender = fx.spawn_living(FAMILY_SOLDIER, 1, 392, 392);
        walker* receiver = fx.spawn_living(FAMILY_SOLDIER, 0, 396, 392);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStatePass;
        vars[kBbPassTarget] =
            static_cast<std::int32_t>(receiver->entity_id());
        vars[kBbFlightTicks] = 8;
        vars[kBbBallPx] = 400 * 256;
        vars[kBbBallPy] = 400 * 256;
        vars[kBbBallPz] = 12 * 256;  // inside grab_z: both contend
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 0;
        fx.tick(1);
        EXPECT_EQ(static_cast<std::int32_t>(defender->entity_id()),
                  fx.carrier())
            << "the earlier oblist slot wins the contested catch (D23a)";
    }
    // The (grab_z, catch_z] band is the receiver's exclusive right.
    {
        BballCourt fx;
        walker* defender = fx.spawn_living(FAMILY_SOLDIER, 1, 392, 392);
        walker* receiver = fx.spawn_living(FAMILY_SOLDIER, 0, 396, 392);
        ASSERT_NE(nullptr, defender);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStatePass;
        vars[kBbPassTarget] =
            static_cast<std::int32_t>(receiver->entity_id());
        vars[kBbFlightTicks] = 8;
        vars[kBbBallPx] = 400 * 256;
        vars[kBbBallPy] = 400 * 256;
        vars[kBbBallPz] = 22 * 256;  // above grab_z, inside catch_z
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = kGravity;
        fx.tick(1);
        EXPECT_EQ(static_cast<std::int32_t>(receiver->entity_id()),
                  fx.carrier())
            << "interceptors cannot reach the high band";
    }
    // Alley-oop box camping: a catch INSIDE the enemy box does not dunk
    // until the catcher exits and re-enters.
    {
        BballCourt fx;
        walker* camper = fx.spawn_living(FAMILY_SOLDIER, 0, 562, 472);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();  // camper center (570, 480): inside the east box
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStatePass;
        vars[kBbPassTarget] =
            static_cast<std::int32_t>(camper->entity_id());
        vars[kBbFlightTicks] = 8;
        vars[kBbBallPx] = 570 * 256;
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 12 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 0;
        fx.tick(1);
        ASSERT_EQ(static_cast<std::int32_t>(camper->entity_id()),
                  fx.carrier());
        EXPECT_EQ(0, fx.var(kBbDunkOk))
            << "a catch inside the enemy box disarms the presence dunk";
        fx.tick(3);
        EXPECT_EQ(0, fx.team_var(kBbPoints, 0)) << "no auto-dunk (D23b)";
        EXPECT_FALSE(has_notification(fx.events, "DUNK!"));

        camper->setxy(532, 472);  // center (540,480): Chebyshev 36 — out
        fx.tick(1);
        EXPECT_EQ(1, fx.var(kBbDunkOk)) << "exit re-arms";
        camper->setxy(562, 472);  // back in through the defense
        fx.tick(1);
        EXPECT_EQ(2, fx.team_var(kBbPoints, 0)) << "the re-entry dunks";
        EXPECT_TRUE(has_notification(fx.events, "DUNK! RED +2"));
    }
    // A GROUND pickup inside the box still put-back dunks (T1/T2 arm).
    {
        BballCourt fx;
        walker* rebounder = fx.spawn_living(FAMILY_SOLDIER, 0, 562, 472);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(rebounder, 570, 480);  // inside the box, off the ground
        EXPECT_EQ(1, fx.var(kBbDunkOk)) << "ground gains always arm";
        fx.tick(1);
        EXPECT_EQ(2, fx.team_var(kBbPoints, 0))
            << "the rebound put-back survives (D23b)";
    }
}

// ===========================================================================
// §11.2 #34 — receiver leading (D22): radius 20 completes, steals stay 12
// ===========================================================================

TEST_F(ModesBasketball, moving_receiver_pass_completes)
{
    // Director-led: at release the receiver is GOTO'd onto the landing
    // point and the pass completes.
    {
        BballCourt fx;
        walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 342, 472, ACT_SIT);
        walker* cutter = fx.spawn_living(FAMILY_SOLDIER, 0, 472, 472, ACT_SIT);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(bot, 350, 480);
        align_before_cadence(fx.world());
        fx.tick(1);
        ASSERT_EQ(kStatePass, fx.var(kBbBallState));
        ASSERT_EQ(static_cast<std::int32_t>(cutter->entity_id()),
                  fx.var(kBbPassTarget));
        // The release-time lead: a GOTO onto the landing point (the
        // cutter's center at release), half-body corrected.
        const int lcx = cutter->xpos() + 8;
        const int lcy = cutter->ypos() + 8;
        EXPECT_TRUE(front_command_is(cutter, COMMAND_GOTO, lcx - 8, lcy - 8))
            << "the director leads the receiver onto the landing point";

        for (int i = 0; i < 45 && fx.var(kBbBallState) == kStatePass; ++i)
            fx.tick(1);
        EXPECT_EQ(static_cast<std::int32_t>(cutter->entity_id()),
                  fx.carrier()) << "the led receiver completes the lob";
    }
    // The receiver catches at 20 while a non-receiver 16 px off the lane
    // never steals — interception stays 12.
    {
        BballCourt fx;
        walker* lurker = fx.spawn_living(FAMILY_SOLDIER, 1, 328, 432);
        walker* mate = fx.spawn_living(FAMILY_SOLDIER, 0, 312, 392);
        ASSERT_NE(nullptr, lurker);  // center (336, 440): 16 off the lane
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 320, 480);
        walker* wpn = fx.spawn_weapon(fx.red, 200, 700, 0.0f, -8.0f);
        ASSERT_NE(nullptr, wpn);
        fx.tick(1);  // chest pass north to mate at (320, 400)
        ASSERT_EQ(kStatePass, fx.var(kBbBallState));
        ASSERT_EQ(static_cast<std::int32_t>(mate->entity_id()),
                  fx.var(kBbPassTarget));
        // Mid-flight, the receiver drifts 16 px east of the landing point.
        mate->setxy(328, 392);  // center (336, 400)
        for (int i = 0; i < 20 && fx.var(kBbBallState) == kStatePass; ++i)
            fx.tick(1);
        EXPECT_EQ(static_cast<std::int32_t>(mate->entity_id()), fx.carrier())
            << "receiver_catch_radius 20 completes a drifted catch";
    }
    // The same 16 px offset on a NON-receiver is out of reach; 10 px is a
    // steal.
    {
        BballCourt fx;
        walker* lurker = fx.spawn_living(FAMILY_SOLDIER, 1, 322, 432);
        walker* mate = fx.spawn_living(FAMILY_SOLDIER, 0, 312, 392);
        ASSERT_NE(nullptr, mate);  // lurker center (330, 440): 10 off-lane
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 320, 480);
        walker* wpn = fx.spawn_weapon(fx.red, 200, 700, 0.0f, -8.0f);
        ASSERT_NE(nullptr, wpn);
        fx.tick(1);
        ASSERT_EQ(kStatePass, fx.var(kBbBallState));
        for (int i = 0; i < 20 && fx.var(kBbBallState) == kStatePass; ++i)
            fx.tick(1);
        EXPECT_EQ(static_cast<std::int32_t>(lurker->entity_id()),
                  fx.carrier())
            << "10 px off the lane is inside catch_radius 12: a steal";
    }
}

// ===========================================================================
// §11.2 #35 — the wipe watchdog fires at exactly 600 in ANY state (D25)
// ===========================================================================

TEST_F(ModesBasketball, wipe_watchdog_resets_and_revives)
{
    BballCourt fx;
    fx.world().respawn_mode = 0;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 350, 480);
    // Keep the shot clock out of the way: the winner CARRIES the ball the
    // whole time — the babysit shape the watchdog exists for.
    fx.world().mode.vars[kBbClockUntil] =
        static_cast<std::int32_t>(fx.world().tick_count_) + 5000;
    fx.green->set_dead(1);
    fx.tick(1);
    const std::int32_t since = fx.var(kBbStallSince);
    ASSERT_GT(since, 0) << "the wipe watchdog armed";
    ASSERT_EQ(0, alive_on_team(fx.world(), 1));

    while (static_cast<std::int32_t>(fx.world().tick_count_) <
           since + kDeadBallTicks - 1)
        fx.tick(1);
    EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET"))
        << "one tick short: no reset yet";
    EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()), fx.carrier())
        << "the ball is still CARRIED — the countdown ran anyway";
    fx.tick(1);
    EXPECT_EQ(1, count_notifications(fx.events, "BALL RESET"))
        << "the watchdog fires at exactly dead_ball_ticks";
    EXPECT_EQ(0, fx.carrier()) << "the carried ball is taken to center";
    EXPECT_EQ(kBballJumpX, fx.ball_cx());
    EXPECT_EQ(5, alive_on_team(fx.world(), 1))
        << "revive_wiped_teams restores the wiped side (D25)";
}

// ===========================================================================
// §11.2 #36 — the pressure term of the D20 scatter (pinned RNG)
// ===========================================================================

TEST_F(ModesBasketball, pressure_scatter_applies)
{
    // Identical release, identical pinned state; only the defender count
    // near the shooter changes. E: open 10, one presser 16, three cap 24.
    const std::uint32_t seed = 777u;
    const auto open_off = predict_scatter(seed, 10);
    const auto pressed_off = predict_scatter(seed, 16);
    const auto capped_off = predict_scatter(seed, 24);

    struct Setup
    {
        int pressers;
        std::pair<int, int> expect;
    } setups[] = {
        {0, open_off},
        {1, pressed_off},
        {3, capped_off},
    };
    const short presser_spots[3][2] = {{477, 477}, {502, 484}, {480, 464}};

    for (const auto& setup : setups)
    {
        BballCourt fx;
        for (int i = 0; i < setup.pressers; ++i)
        {
            walker* p = fx.spawn_living(FAMILY_SOLDIER, 1,
                                        presser_spots[i][0],
                                        presser_spots[i][1]);
            ASSERT_NE(nullptr, p);
        }
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 500, 480);  // D = 76: base error 10
        walker* wpn = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, wpn);
        fx.world().rng_.state_ = seed;
        fx.tick(1);

        ASSERT_EQ(kStateShot, fx.var(kBbBallState))
            << setup.pressers << " pressers";
        EXPECT_EQ(pos_pack(kBballHoop1X + setup.expect.first,
                           kBballHoop1Y + setup.expect.second),
                  fx.var(kBbShotLand))
            << setup.pressers << " pressers draw E per the D20 formula "
            << "(base + 6/presser, capped at 24)";
    }

    // The radius boundary from the OUTSIDE: an enemy at exactly L1 25 of
    // the release point adds no pressure — the open E = 10 offsets stand.
    {
        BballCourt fx;
        walker* bystander = fx.spawn_living(FAMILY_SOLDIER, 1, 517, 472);
        ASSERT_NE(nullptr, bystander);  // center (525,480): 25 L1 > 24
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 500, 480);
        walker* wpn = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, wpn);
        fx.world().rng_.state_ = seed;
        fx.tick(1);
        ASSERT_EQ(kStateShot, fx.var(kBbBallState));
        EXPECT_EQ(pos_pack(kBballHoop1X + open_off.first,
                           kBballHoop1Y + open_off.second),
                  fx.var(kBbShotLand))
            << "one px outside press_radius (24) is not pressure";
    }
}

// ===========================================================================
// R1 calibration sanity — normal bot games on the five SHIPPED courts.
// Loads the real scen824-828 geometry from builtin/modes.glad (the
// ModesItemsRealCampaign shape); no team is seated, so init fields a
// 5-bot squad per active team and the director plays both ends. The
// clock/range calibration claim (D7/D20): every court produces a score
// well inside its time limit, and the reference court never needs a
// watchdog rescue.
// ===========================================================================

namespace {

// A shipped campaign level loaded with full sim context, over the shared
// modes_test loader hooks (real grid, markers, generators — not the
// synthetic 640x960 fixture court).
struct LoadedRealCourt
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedRealCourt(int id)
        : level(id, true, &modes_test_level_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedRealCourt() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

int best_points(GameWorld& world)
{
    int best = 0;
    for (int team = 0; team < 4; ++team)
    {
        const int p =
            world.mode.vars[static_cast<std::size_t>(kBbPoints + team)];
        if (p > best)
            best = p;
    }
    return best;
}

}  // namespace

TEST(ModesBasketballRealCampaign, bot_games_score_on_every_shipped_court)
{
    restore_default_campaigns();
    const std::string previous = get_mounted_campaign();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "builtin/modes.glad should restore and mount";
    const int courts[] = {824, 825, 826, 827, 828};
    for (const int id : courts)
    {
        LoadedRealCourt fx(id);
        ASSERT_TRUE(fx.loaded) << "scen" << id << " must load";
        fx.world().tick();
        ASSERT_TRUE(fx.world().mode.active) << id;
        ASSERT_EQ(kModeIdBasketball, fx.world().mode.vars[kBbModeId]) << id;
        const int time_limit = fx.world().mode.vars[kBbTimeLimit];
        ASSERT_GT(time_limit, 0) << id;

        // 824 plays a FULL game (score-limit win or buzzer) to give the
        // no-watchdog claim teeth; the other courts stop at first blood.
        const bool full_game = (id == 824);
        int ticks = 1;
        while (ticks < time_limit && !fx.world().game_ended)
        {
            fx.world().tick();
            ticks++;
            if (!full_game && best_points(fx.world()) > 0)
                break;
        }
        EXPECT_GT(best_points(fx.world()), 0)
            << "scen" << id << ": a normal bot game must produce a score "
            << "inside the time limit (R1 calibration; " << ticks
            << " ticks run)";
        std::printf("[ R1 CAL  ] scen%d: %d ticks, best %d pts%s\n", id,
                    ticks, best_points(fx.world()),
                    fx.world().game_ended ? " (game ended)" : "");
        if (id == 824)
        {
            EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET"))
                << "the reference court must never need the dead-ball/"
                << "wipe watchdog in a normal bot game (R1)";
        }
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count) << id;
        for (const auto& err : fx.world().scripts().host().errors())
            ADD_FAILURE() << "scen" << id << " script error: " << err.message;
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
