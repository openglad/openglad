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
// possession grace, the D27 dead-weapon release + D28 launch-resource refund
// (and its engine-gated drained-carrier residual, edge #28),
// the D7/D25 shot-clock lifecycle (persistence across
// loose balls, late-regain turnovers, the 120-tick team grace), D24
// entity-pinned grace bars, D22 receiver leading and the contested-catch
// race, landing legality, the dead-ball and wipe watchdogs, win/timeout/
// buzzer, the AI director's role scheme, spawn caps, mirror replication,
// the determinism digest, instruction-budget headroom, the R4 slot
// budget, the D29-D31 hoop furniture (spawn counts and tints, the
// tick-exact swish/clang frame programs, wire replication, the win-latch
// freeze), and the D33/D34 body denial (the low window and its z boundary,
// the shooter/teammate exclusion prongs, body-before-weapon precedence,
// clock/RNG parity with the weapon block, the open-release regression).

#include <gtest/gtest.h>

#include <openglad/core/colors.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"
#include "../test_save_state_guard.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

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
    kBbItemLast = 63,   // #225 claimed the former R4 spare
    kBbItemCursor = 7,  // shared header band (mode_match's precedent)
};

// The private slot map is FULL: 62 was the last private claim before #225
// spent 63 on the item clock (the R4 slot review), which is why the pad
// cursor had to go into the shared header band alongside MATCHED (2-5) and
// mode_anchors' squad seed (6).
static_assert(kBbDunkOk == og::sim::kModeVarCount - 2,
              "slot map must top out one below the var count");
static_assert(kBbItemLast == og::sim::kModeVarCount - 1);

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

void paint_water(GameWorld& world, int left, int top, int right, int bottom)
{
    for (int y = top; y <= bottom; ++y)
    {
        for (int x = left; x <= right; ++x)
        {
            world.grid.data[static_cast<std::size_t>(x + world.grid.w * y)] =
                PIX_WATER1;
        }
    }
}

int live_weapons_owned_by(GameWorld& world, const walker* owner)
{
    int count = 0;
    for (const auto& uptr : world.weaplist)
    {
        const walker* shot = uptr.get();
        if (shot != nullptr && !shot->dead() && shot->owner() == owner)
            count++;
    }
    return count;
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

// Retires a team's dead guy-less bodies, simulating the engine sweep for
// corpses whose entries were genuinely lost (a full all-player queue). With
// corpse persistence (#221) a body whose entry is merely hand-cleared is
// re-adopted by the always-on scan next tick, so the zero-live-zero-pending
// watchdog edge needs the drained entries' bodies gone too.
void retire_dead_guyless(GameWorld& world, int team)
{
    std::vector<walker*> corpses;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && w->dead() && w->myguy == nullptr &&
            w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(team))
        {
            corpses.push_back(w);
        }
    }
    for (walker* w : corpses)
        world.retire_corpse(w);
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

    // A weapon the throw scan consumes is erased by the SAME tick's dead
    // sweep (GameWorld::tick), so its pointer dangles after fx.tick() —
    // judge weapon fate by id lookup, never by dereferencing the spawn
    // pointer across a tick.
    bool weapon_present(std::uint32_t id)
    {
        return world().find_by_id(id) != nullptr;
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

// The installed modes:hoop family byte (D30) — resolvable only after the
// fixture mounts the pack, so tests call this, never a static.
int hoop_family_byte()
{
    return og::families::resolve_family_string_id(Order::FX, "modes:hoop");
}

// The C++ twin of the impl's find_hoop (D31): one fxlist scan for the hoop
// family member wearing this team's spawn stamp. The pointer is valid while
// the hoop lives (hoops never die); cross-tick fate is still judged by
// entity id where the world keeps ticking past a latch.
walker* find_hoop_fx(GameWorld& world, int hoop_family, int team)
{
    for (const auto& uptr : world.fxlist)
    {
        walker* w = uptr.get();
        if (w != nullptr && static_cast<int>(w->family()) == hoop_family &&
            w->team_num() == static_cast<unsigned char>(team))
        {
            return w;
        }
    }
    return nullptr;
}

int count_hoop_fx(GameWorld& world, int hoop_family)
{
    int count = 0;
    for (const auto& uptr : world.fxlist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && static_cast<int>(w->family()) == hoop_family)
            count++;
    }
    return count;
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
    ASSERT_NE(nullptr, ball->stats());
    ASSERT_NE(nullptr, shadow->stats());
    EXPECT_TRUE(ball->stats()->query_bit_flags(BIT_SWIMMING))
        << "the real basketball carries its descriptor's SWIMMING flag";
    EXPECT_FALSE(shadow->stats()->query_bit_flags(BIT_SWIMMING))
        << "the visual-only shadow never gains terrain capabilities";
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

// The camera declaration (issue #224, design §8 F): camera slot 0 follows
// the SHADOW, not the ball. The ball draws lifted by up to ~40px of fake Z,
// so a ball camera would bob through every shot arc; the shadow is the
// ground truth on the floor — the same reason beacon 0 is the shadow.
TEST_F(ModesBasketball, init_points_the_camera_view_at_the_shadow)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());

    const walker* const ball = fx.ball();
    const walker* const shadow = fx.shadow();
    ASSERT_NE(nullptr, ball);
    ASSERT_NE(nullptr, shadow);
    ASSERT_NE(ball->entity_id(), shadow->entity_id());

    EXPECT_EQ(static_cast<std::int32_t>(shadow->entity_id()),
              fx.world().mode.cameras[0].entity_id)
        << "camera slot 0 follows the SHADOW (the fake-Z decision)";
    EXPECT_NE(static_cast<std::int32_t>(ball->entity_id()),
              fx.world().mode.cameras[0].entity_id)
        << "following the ball would bob the pane on every arc";
    EXPECT_EQ(fx.var(kBbShadowEntity), fx.world().mode.cameras[0].entity_id);
    EXPECT_EQ(og::sim::kCameraStyleAuto, fx.world().mode.cameras[0].style)
        << "auto: each machine resolves docked-vs-inset locally";

    // Set ONCE, in on_mode_init — no per-tick re-assertion (the
    // instruction-budget rule), so a hand-cleared slot stays cleared.
    fx.world().mode.cameras[0] = og::sim::ModeCameraView{};
    fx.tick(30);
    EXPECT_EQ(0, fx.world().mode.cameras[0].entity_id)
        << "on_mode_tick must not re-declare the camera every tick";
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

TEST_F(ModesBasketball, all_bot_anchor_only_court_takes_the_manifest_default)
{
    // Soccer's twin (see test_modes_soccer.cpp): anchors-only teams carry
    // no fielded units, so on a FOUR-anchor court whose manifest row
    // declares teams = 2 with two hoops (kBballLevelA) exactly the first
    // two sides play — the retired OWN arm's whole-domain refusal shape
    // is gone with TROOPS (B5).
    ModesCtfWorld fx(kBballLevelA);
    for (int team = 0; team < 4; ++team)
    {
        fx.spawn_anchor(team, static_cast<short>(128 + 64 * team), 700);
        fx.world().ctf_requested_fill[static_cast<std::size_t>(team)] =
            og::sim::kFillFair;  // E5: every wheel turned
    }
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active)
        << "the manifest default is a clean two-side court";
    EXPECT_EQ(0b0011, fx.var(kBbTeamMask));
    EXPECT_EQ(0, alive_on_team(fx.world(), 2))
        << "an anchors-only team past the default fields nothing";
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

// #241: the lobby TIME LIMIT knob beats the manifest row — up, here, from
// the 120-tick short court. The buzzer rules are untouched: this only
// changes the number the buzzer waits for.
TEST_F(ModesBasketball, time_limit_knob_overrides_the_manifest_row)
{
    BballCourt fx(kBballLevelShort);
    fx.world().ctf_requested_time_limit = 3600;
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(3600, fx.var(kBbTimeLimit)) << "the request beats the row";
    EXPECT_EQ(21, fx.var(kBbScoreLimit)) << "and moves no other knob";
    fx.world().set_level_tick_count(120 + 8);
    fx.tick(2);
    EXPECT_FALSE(fx.world().game_ended) << "the row's 120 no longer decides";
}

TEST_F(ModesBasketball, fair_teams_four_with_a_solo_roster_fields_four_hoops)
{
    // Issue #218 on the four-hoop court: TROOPS: FAIR + TEAMS: 4 with a
    // solo roster fields all four sides — the explicit lobby count wins
    // the COUNT, FAIR only the composition. All four hoops bank and all
    // four rims hang (the basketball half of issue #219's audit, positive
    // direction: a live goal always has its rim).
    ModesCtfWorld fx(kBballLevelB);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(128 + 64 * team), 700);
    arm_matched(fx.world());
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: backfills
    fx.world().ctf_requested_fill[2] = og::sim::kFillFair;
    fx.world().ctf_requested_fill[3] = og::sim::kFillFair;
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 128, 640, 1);
    ASSERT_NE(nullptr, hero);
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(15, fx.var(kBbTeamMask))
        << "AUTO is every authored side: all four field (the TEAMS count "
           "is retired, lineup A1)";
    EXPECT_EQ(4, fx.var(kBbTeamCount));
    const int hoop_x[4] = {320, 576, 320, 64};
    const int hoop_y[4] = {64, 480, 896, 480};
    for (int team = 0; team < 4; ++team)
    {
        EXPECT_EQ(pos_pack(hoop_x[team], hoop_y[team]),
                  fx.team_var(kBbHoopPos, team)) << "hoop " << team;
    }
    const int hoop_family = hoop_family_byte();
    ASSERT_GE(hoop_family, 0) << "modes:hoop must install on mount";
    EXPECT_EQ(4, count_hoop_fx(fx.world(), hoop_family))
        << "every activated goal hangs its rim";
    EXPECT_EQ(1, alive_on_team(fx.world(), 0)) << "the roster is untouched";
    for (int team = 1; team < 4; ++team)
    {
        EXPECT_EQ(1, alive_on_team(fx.world(), team))
            << "FAIR fields a matched-headcount squad on team " << team;
    }
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
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
        const std::uint32_t shot_id = shot->entity_id();
        fx.tick(1);

        EXPECT_FALSE(fx.weapon_present(shot_id))
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
        const std::uint32_t shot_id = shot->entity_id();
        fx.tick(1);

        EXPECT_FALSE(fx.weapon_present(shot_id));
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
// #209 — objective radar beacons: soccer's drawn ball pings directly;
// basketball's ground-truth shadow owns its single yellow landmark/pulse so
// the fake-z ball stays radar-quiet. The hoop remains furniture.
// ===========================================================================

TEST_F(ModesBasketball, ball_families_install_the_radar_ping)
{
    BballCourt fx;  // mounts + installs the CURRENT repo pack bytes

    const int bball =
        og::families::resolve_family_string_id(Order::FX, "modes:bball");
    const int ball =
        og::families::resolve_family_string_id(Order::FX, "modes:ball");
    const int bshadow =
        og::families::resolve_family_string_id(Order::FX, "modes:bshadow");
    ASSERT_GE(bball, 0);
    ASSERT_GE(ball, 0);
    ASSERT_GE(bshadow, 0);

    const EffectFamilyDescriptor* bball_d =
        get_effect_family_descriptor(bball);
    const EffectFamilyDescriptor* ball_d = get_effect_family_descriptor(ball);
    const EffectFamilyDescriptor* shadow_d =
        get_effect_family_descriptor(bshadow);
    const EffectFamilyDescriptor* hoop_d =
        get_effect_family_descriptor(hoop_family_byte());
    ASSERT_NE(nullptr, bball_d);
    ASSERT_NE(nullptr, ball_d);
    ASSERT_NE(nullptr, shadow_d);
    ASSERT_NE(nullptr, hoop_d);

    EXPECT_FALSE(bball_d->radar.ping)
        << "the fake-z basketball never draws a lifted duplicate pulse";
    EXPECT_FALSE(bball_d->radar.landmark);
    EXPECT_EQ(og::kRadarColorNone, bball_d->radar.color);
    EXPECT_TRUE(ball_d->radar.ping)
        << "modes:ball declares radar_ping (#209)";
    EXPECT_TRUE(shadow_d->radar.ping)
        << "the ground shadow is basketball's objective proxy";
    EXPECT_TRUE(shadow_d->radar.landmark);
    EXPECT_EQ(COLOR_YELLOW, shadow_d->radar.color);
    EXPECT_FALSE(hoop_d->radar.ping)
        << "the hoop is furniture, not the objective";
    EXPECT_NE(0, bball_d->init_bit_flags & BIT_SWIMMING)
        << "modes:bball declares SWIMMING";
    EXPECT_NE(0, ball_d->init_bit_flags & BIT_SWIMMING)
        << "modes:ball declares SWIMMING";
    EXPECT_EQ(0, shadow_d->init_bit_flags & BIT_SWIMMING)
        << "the shadow is not a physical ball";
}

// ===========================================================================
// Surface water: footprint/sweep drag, airborne clearance, wet landings and
// projectile recovery. All values are x256 fixed point and exact.
// ===========================================================================

TEST_F(ModesBasketball, free_roll_water_drag_has_dry_and_swept_controls)
{
    // Dry keep=256 and loss=64 preserves the existing roll exactly.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.set_ball_free(200, 200);
        fx.world().mode.vars[kBbBallVx] = 8 * 256;
        fx.tick(1);
        EXPECT_EQ(208, fx.ball_cx());
        EXPECT_EQ(1984, fx.var(kBbBallVx));
        EXPECT_EQ(0, fx.var(kBbBallVy));
    }

    // A final footprint one pixel into water uses keep=64 and loss=64:
    // div(2048*64,256)-64 = 448. A center-only probe would call this dry.
    {
        BballCourt fx;
        paint_water(fx.world(), 13, 12, 13, 12);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.set_ball_free(196, 200);
        fx.world().mode.vars[kBbBallVx] = 8 * 256;
        fx.tick(1);
        EXPECT_EQ(204, fx.ball_cx());
        EXPECT_EQ(448, fx.var(kBbBallVx));
        EXPECT_EQ(0, fx.var(kBbBallVy));
    }

    // Starts and ends dry; only an accepted substep crosses tile (12,12).
    // Accumulated contact yields div(10752*64,256)-64 = 2624.
    {
        BballCourt fx;
        paint_water(fx.world(), 12, 12, 12, 12);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.set_ball_free(173, 200);
        fx.world().mode.vars[kBbBallVx] = 42 * 256;
        fx.tick(1);
        EXPECT_EQ(215, fx.ball_cx());
        EXPECT_EQ(2624, fx.var(kBbBallVx));
        EXPECT_EQ(0, fx.var(kBbBallVy));
    }
}

TEST_F(ModesBasketball, low_airborne_ball_clears_water_without_drag)
{
    BballCourt fx;
    paint_water(fx.world(), 10, 10, 16, 16);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    auto& vars = fx.world().mode.vars;
    vars[kBbBallState] = kStateRebound;
    vars[kBbBallPx] = 200 * 256;
    vars[kBbBallPy] = 200 * 256;
    vars[kBbBallPz] = 1 * 256;
    vars[kBbBallVx] = 8 * 256;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = 512;
    fx.tick(1);

    EXPECT_EQ(208, fx.ball_cx()) << "even a low arc crosses water";
    EXPECT_EQ(3, fx.ball_z_px());
    EXPECT_EQ(8 * 256, fx.var(kBbBallVx))
        << "airborne horizontal speed is untouched by surface drag";
    EXPECT_EQ(512 - kGravity, fx.var(kBbBallVz));
    EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
}

TEST_F(ModesBasketball, wet_landing_slows_and_settles)
{
    // Wet contact: horizontal keep=64/loss=64 and vertical restitution
    // 32/256. Incoming vz -512 integrates to -608; 608*32/256 = 76 is
    // below settle_vz, so the ball becomes FREE immediately.
    {
        BballCourt fx;
        paint_water(fx.world(), 10, 10, 16, 16);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateRebound;
        vars[kBbBallPx] = 200 * 256;
        vars[kBbBallPy] = 200 * 256;
        vars[kBbBallPz] = 1 * 256;
        vars[kBbBallVx] = 8 * 256;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -512;
        fx.tick(1);
        EXPECT_EQ(208, fx.ball_cx());
        EXPECT_EQ(0, fx.var(kBbBallPz));
        EXPECT_EQ(448, fx.var(kBbBallVx));
        EXPECT_EQ(0, fx.var(kBbBallVz));
        EXPECT_EQ(kStateFree, fx.var(kBbBallState));
    }

    // Dry landing control retains the old 3/4 horizontal and 1/2 vertical
    // bounce: vx=1536, vz=304, still a live REBOUND.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateRebound;
        vars[kBbBallPx] = 200 * 256;
        vars[kBbBallPy] = 200 * 256;
        vars[kBbBallPz] = 1 * 256;
        vars[kBbBallVx] = 8 * 256;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -512;
        fx.tick(1);
        EXPECT_EQ(1536, fx.var(kBbBallVx));
        EXPECT_EQ(304, fx.var(kBbBallVz));
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
    }
}

TEST_F(ModesBasketball, projectile_pops_waterlogged_free_ball_but_not_dry)
{
    // Waterlogged FREE objective: the ordinary swat impulse/pop path is
    // admitted. Same-tick launch begins at the surface, so 4 px/tick
    // damps to 192 while vz 512 integrates to 416.
    {
        BballCourt fx;
        paint_water(fx.world(), 18, 17, 22, 21);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.set_ball_free(320, 300);
        walker* swat = fx.spawn_weapon(fx.green, 317, 297, 4.0f, 0.0f);
        ASSERT_NE(nullptr, swat);
        swat->set_damage(1.0f);
        const std::uint32_t swat_id = swat->entity_id();
        fx.tick(1);

        EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
        EXPECT_EQ(324, fx.ball_cx());
        EXPECT_EQ(2, fx.ball_z_px());
        EXPECT_EQ(192, fx.var(kBbBallVx));
        EXPECT_EQ(512 - kGravity, fx.var(kBbBallVz));
        EXPECT_EQ(2, fx.var(kBbLastTouch1));
        EXPECT_TRUE(fx.weapon_present(swat_id));
        EXPECT_FALSE(has_notification(fx.events, "BLOCK!"))
            << "a recovery pop is not a blocked shot";
    }

    // Identical dry FREE control: swats remain irrelevant to a loose ball.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.set_ball_free(320, 300);
        walker* swat = fx.spawn_weapon(fx.green, 317, 297, 4.0f, 0.0f);
        ASSERT_NE(nullptr, swat);
        swat->set_damage(1.0f);
        const std::uint32_t swat_id = swat->entity_id();
        fx.tick(1);
        EXPECT_EQ(kStateFree, fx.var(kBbBallState));
        EXPECT_EQ(320, fx.ball_cx());
        EXPECT_EQ(0, fx.var(kBbBallVx));
        EXPECT_EQ(0, fx.var(kBbBallVz));
        EXPECT_TRUE(fx.weapon_present(swat_id));
    }
}

TEST_F(ModesBasketball, director_fires_to_recover_a_waterlogged_free_ball)
{
    {
        BballCourt fx;
        paint_water(fx.world(), 19, 17, 22, 21);
        walker* shooter =
            fx.spawn_living(FAMILY_ARCHER, 0, 96, 292, ACT_SIT);
        ASSERT_NE(nullptr, shooter);
        fx.red->set_act_type(ACT_SIT);
        fx.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        shooter->stats()->set_weapon_cost(0);
        shooter->set_current_weapon(FAMILY_ARROW);
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.thaw();
        fx.set_ball_free(320, 300);

        // Immediate range is not the gate: the dry shoreline lies within this
        // arrow's eventual reach, so COMMAND_ATTACK walks there normally.
        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(0, live_weapons_owned_by(fx.world(), shooter));
        EXPECT_EQ(SCORE_TEAM_COUNT, fx.ball()->team_num());
        EXPECT_EQ(COMMAND_ATTACK, front_type(shooter));
        EXPECT_EQ(fx.ball(), shooter->foe());
        EXPECT_EQ(nullptr, shooter->leader());
        const int far_x = shooter->xpos();
        fx.tick(3);
        EXPECT_GT(shooter->xpos(), far_x)
            << "the out-of-range weapon approaches instead of wasting shots";
        EXPECT_EQ(0, live_weapons_owned_by(fx.world(), shooter));

        // The same command must complete the whole dry approach itself, then
        // handle facing, animation, mana, cooldown and the family projectile.
        bool fired = false;
        bool cooldown_seen = false;
        bool dislodged = false;
        for (int tick = 0; tick < 180 && !dislodged; ++tick)
        {
            fx.tick(1);
            fired = fired || live_weapons_owned_by(fx.world(), shooter) > 0;
            cooldown_seen = cooldown_seen || shooter->busy() > 0.0f;
            dislodged =
                fx.var(kBbLastToucher) ==
                    static_cast<std::int32_t>(shooter->entity_id()) &&
                (fx.var(kBbBallState) != kStateFree ||
                 std::abs(fx.var(kBbBallVx)) + std::abs(fx.var(kBbBallVy)) > 0);
        }
        EXPECT_TRUE(fired)
            << "normal COMMAND_ATTACK must launch a live family projectile";
        EXPECT_TRUE(cooldown_seen)
            << "the normal fire-frequency cooldown must remain active";
        EXPECT_TRUE(dislodged)
            << "the director-fired projectile must pop the wet FREE ball";
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
        EXPECT_EQ(416, fx.var(kBbBallVz));
    }

    // Dry control: the same capable racer keeps the normal loose-ball GOTO
    // and does not spend a weapon.
    {
        BballCourt dry;
        walker* dry_shooter =
            dry.spawn_living(FAMILY_ARCHER, 0, 240, 292, ACT_SIT);
        ASSERT_NE(nullptr, dry_shooter);
        dry.red->set_act_type(ACT_SIT);
        dry.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        dry_shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        dry_shooter->stats()->set_weapon_cost(0);
        dry_shooter->set_current_weapon(FAMILY_ARROW);
        dry.tick(1);
        ASSERT_TRUE(dry.basketball_active());
        dry.thaw();
        dry.set_ball_free(320, 300);
        align_before_cadence(dry.world());
        dry.tick(1);
        EXPECT_EQ(0, live_weapons_owned_by(dry.world(), dry_shooter));
        EXPECT_TRUE(front_command_is(dry_shooter, COMMAND_GOTO, 312, 292));
    }
}

TEST_F(ModesBasketball, director_never_shoots_airborne_water_overlap)
{
    BballCourt fx;
    paint_water(fx.world(), 19, 17, 22, 21);
    walker* shooter =
        fx.spawn_living(FAMILY_ARCHER, 0, 240, 292, ACT_SIT);
    ASSERT_NE(nullptr, shooter);
    fx.red->set_act_type(ACT_SIT);
    fx.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    shooter->stats()->set_weapon_cost(0);
    shooter->set_current_weapon(FAMILY_ARROW);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    fx.set_ball_free(320, 300);
    auto& vars = fx.world().mode.vars;
    vars[kBbBallState] = kStateRebound;
    vars[kBbBallPz] = 1 * 256;
    vars[kBbBallVz] = 512;

    align_before_cadence(fx.world());
    fx.tick(1);

    EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
    EXPECT_GT(fx.var(kBbBallPz), 0)
        << "the low ball remains genuinely airborne over water";
    EXPECT_EQ(0, live_weapons_owned_by(fx.world(), shooter));
    EXPECT_NE(COMMAND_ATTACK, front_type(shooter));
    EXPECT_TRUE(front_command_is(shooter, COMMAND_GOTO, 312, 292))
        << "REBOUND keeps the ordinary loose-flight racer role";
    EXPECT_EQ(nullptr, shooter->foe());
    EXPECT_EQ(fx.ball(), shooter->leader());
}

TEST_F(ModesBasketball, attended_waterlogged_ball_resets_at_600_ticks)
{
    BballCourt fx;
    paint_water(fx.world(), 18, 28, 22, 32);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    fx.set_ball_free(320, 480);
    fx.red->setxy(292, 452);  // center (300,460): attended, no pickup
    fx.tick(1);
    const std::int32_t since = fx.var(kBbStallSince);
    ASSERT_GT(since, 0) << "waterlogging starts the recovery clock";

    const std::int32_t one_before_reset = since + kDeadBallTicks - 1;
    for (int step = 0;
         step < kDeadBallTicks &&
         static_cast<std::int32_t>(fx.world().tick_count_) < one_before_reset;
         ++step)
    {
        fx.tick(1);
    }
    ASSERT_EQ(one_before_reset,
              static_cast<std::int32_t>(fx.world().tick_count_))
        << "the bounded watchdog probe must reach the exact boundary";
    EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET"))
        << "one tick short: projectile recovery still has time";
    fx.tick(1);
    EXPECT_EQ(1, count_notifications(fx.events, "BALL RESET"));
    EXPECT_EQ(kBballJumpX, fx.ball_cx());
    EXPECT_EQ(kBballJumpY, fx.ball_cy());
}

// ===========================================================================
// #210 — the scoreboard names both sides: one HUD row per ACTIVE team,
// re-derived every tick, so a rescore updates the scorer's row while the
// OPPOSING team's row stays posted (the TDM per-team hud_score_line shape).
// ===========================================================================

TEST_F(ModesBasketball, hud_rescore_keeps_the_opposing_team_row)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());

    // Before any score: one row per active team, slots 2/3 empty on a
    // two-team court.
    EXPECT_STREQ("RED 0/6", fx.world().mode.hud[0].text.data());
    EXPECT_STREQ("GREEN 0/6", fx.world().mode.hud[1].text.data());
    EXPECT_EQ(0, fx.world().mode.hud[0].team);
    EXPECT_EQ(1, fx.world().mode.hud[1].team);
    EXPECT_EQ('\0', fx.world().mode.hud[2].text[0]);
    EXPECT_EQ('\0', fx.world().mode.hud[3].text[0]);

    // RED dunks (the dunk_scores_2 recipe). The rescore lands on RED's row
    // the same tick — and GREEN's row must still be posted beside it.
    fx.give_ball(fx.red, 450, 480);
    fx.red->setxy(552, 472);  // center (560, 480): Chebyshev 16 <= 24
    fx.tick(1);
    ASSERT_EQ(2, fx.team_var(kBbPoints, 0)) << "the dunk scored";

    EXPECT_STREQ("RED 2/6", fx.world().mode.hud[0].text.data());
    EXPECT_STREQ("GREEN 0/6", fx.world().mode.hud[1].text.data())
        << "the opposing team's score stays visible after a rescore (#210)";
    EXPECT_EQ(0, fx.world().mode.hud[0].team);
    EXPECT_EQ(1, fx.world().mode.hud[1].team);
    EXPECT_EQ('\0', fx.world().mode.hud[2].text[0]);
    EXPECT_EQ('\0', fx.world().mode.hud[3].text[0]);
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
        const std::uint32_t swat_id = swat->entity_id();
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
        EXPECT_TRUE(fx.weapon_present(swat_id))
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
        const std::uint32_t frozen_id = frozen->entity_id();
        ASSERT_NE(nullptr, frozen);  // a boomerang at turnaround
        walker* live = fx.spawn_weapon(fx.green, 315, 299, 4.0f, 0.0f);
        ASSERT_NE(nullptr, live);
        live->set_damage(5.0f);
        fx.tick(1);
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState))
            << "the zero-step weapon does not shield the ball (§3.5)";
        EXPECT_EQ(10 * 256, fx.var(kBbBallVx))
            << "the later real-step weapon lands the block";
        EXPECT_TRUE(fx.weapon_present(frozen_id));
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
    // (D25 — the watchdog ignores attendance). Under always-on respawns
    // the revive normally owns the comeback, so the watchdog arms only
    // for a side with zero live AND zero pending — drain the entry to
    // exercise exactly that backstop.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        // E5: turned AFTER init — at init an explicit FAIR would put a
        // squad beside the authored green troop (D3); the revive backstop
        // reads the knob at spawn time.
        fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
        fx.thaw();
        fx.red->setxy(292, 452);  // center (300,460): 40 px from the ball —
                                  // attending, but NOT on it (no pickup)
        fx.green->set_dead(1);
        fx.tick(1);
        ASSERT_EQ(0, alive_on_team(fx.world(), 1));
        EXPECT_EQ(1, ai_entries_for_team(fx.world(), 1))
            << "the corpse scheduled the tick it fell — always-on";
        fx.world().respawn.respawn_queue.clear();
        retire_dead_guyless(fx.world(), 1);
        fx.tick(597);
        EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET"));
        fx.tick(6);
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

TEST_F(ModesBasketball, respawn_ignores_submenu_everyone_returns)
{
    // Off: everyone still comes back — the submenu no longer gates a
    // ball game.
    {
        BballRespawnCourt fx;
        fx.world().respawn_mode = 0;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.kill_both();
        fx.tick(3);
        EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
        EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0));
        fx.tick(70);
        EXPECT_FALSE(fx.hero->dead()) << "Off no longer keeps anyone down";
    }
    // Heroes: scheduling identical to Off; the roster corpse still lands
    // on its own anchors.
    {
        BballRespawnCourt fx;
        fx.world().respawn_mode = 1;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.kill_both();
        fx.tick(2);
        EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
        EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0))
            << "the AI corpse is scheduled regardless of the choice";
        fx.tick(70);  // respawn_ticks 60 + slack
        EXPECT_FALSE(fx.hero->dead());
        const bool at_anchor =
            (fx.hero->xpos() == 128 &&
             (fx.hero->ypos() == 448 || fx.hero->ypos() == 512));
        EXPECT_TRUE(at_anchor)
            << "on_respawn places at a team-0 anchor, got ("
            << fx.hero->xpos() << "," << fx.hero->ypos() << ")";
        EXPECT_GT(fx.var(kBbAnchorCursor), 0) << "anchor cursor rotated";
    }
    // Everyone: the same always-on semantics, spelled by the submenu.
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

// The matched-teams D38 coverage gap, defense half: at n = 1 every
// scheme's FIRST greedy pick is the ball-relevant role, so a threatened
// team's sole member is the ON-BALL defender — commanded onto the carrier
// with the steal foe set, never parked at the rim or the HELP fan. No
// impl change: this pins that basketball already degrades correctly.
TEST_F(ModesBasketball, sole_defender_takes_the_ball_not_the_rim)
{
    BballCourt fx;
    walker* lone = fx.spawn_living(FAMILY_SOLDIER, 1, 490, 540, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    // Human carrier at (450,480): hoop 1 (576,480) is the nearest enemy
    // hoop, so team 1 is THREATENED — the on-ball pick must still come
    // first (the D38 walk: run_defense assigns it before the threatened
    // branch).
    fx.give_ball(fx.red, 450, 480);
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    EXPECT_EQ(fx.red, lone->foe()) << "the steal: foe set on the carrier";
    EXPECT_TRUE(front_command_is(lone, COMMAND_GOTO, 442, 472))
        << "the sole defender drives onto the carrier center (450,480), "
           "not the rim lane or its own hoop";
}

// The matched-teams D38 coverage gap, loose-ball half: a sole member
// races onto the ground center — arriving IS pickup — instead of holding
// the own-hoop seam. No impl change: a gap pin only.
TEST_F(ModesBasketball, sole_member_races_the_loose_ball)
{
    BballCourt fx;
    walker* lone = fx.spawn_living(FAMILY_SOLDIER, 0, 212, 372, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    fx.set_ball_free(320, 480);
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    EXPECT_TRUE(front_command_is(lone, COMMAND_GOTO, 312, 472))
        << "the singleton is commanded onto the ball's ground center "
           "(320,480), not the seam midpoint";
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
// §11.2 #22 — mirror replication: ball + shadow + frames on the wire (I4);
// extended per #46 with the hoops (family byte, tint, mid-swish frames)
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
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: the squad
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

    // Test 46 (D31): arm a swish BEFORE the window so a mid-ripple frame
    // program plays inside it — the per-tick set_frame results must ride
    // the wire tick-for-tick (mirrors never animate; the hash check below
    // strikes on any tick where they diverge).
    const int hoop_family = hoop_family_byte();
    ASSERT_GE(hoop_family, 0) << "modes:hoop must install on mount";
    walker* const armed = find_hoop_fx(fx.world(), hoop_family, 1);
    ASSERT_NE(nullptr, armed) << "the east hoop must have spawned";
    const std::uint32_t armed_id = armed->entity_id();
    armed->set_cycle(12);  // == arm_hoop_anim(1, hoop_swish_ticks)

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
    ASSERT_NE(nullptr, ball->stats());
    ASSERT_NE(nullptr, m_ball->stats());
    EXPECT_TRUE(ball->stats()->query_bit_flags(BIT_SWIMMING));
    EXPECT_TRUE(m_ball->stats()->query_bit_flags(BIT_SWIMMING))
        << "the objective's terrain capability crosses the snapshot";

    walker* const m_shadow = mirror.world().find_by_id(shadow->entity_id());
    ASSERT_NE(nullptr, m_shadow) << "the fxlist shadow must replicate too";
    EXPECT_EQ(static_cast<int>(shadow->family()),
              static_cast<int>(m_shadow->family()));
    EXPECT_EQ(shadow->xpos(), m_shadow->xpos());
    EXPECT_EQ(shadow->ypos(), m_shadow->ypos());
    EXPECT_EQ(shadow->frame(), m_shadow->frame())
        << "the altitude frame replicates";
    ASSERT_NE(nullptr, shadow->stats());
    ASSERT_NE(nullptr, m_shadow->stats());
    EXPECT_FALSE(shadow->stats()->query_bit_flags(BIT_SWIMMING));
    EXPECT_FALSE(m_shadow->stats()->query_bit_flags(BIT_SWIMMING))
        << "the visual shadow stays non-physical on both peers";

    // Test 46 — the hoops on the wire (D30/D31): family byte directly
    // after the shadow's, position/team/frame/cycle replicated. Judged by
    // id — 120 ticks of bot play ran since the pointers above were taken.
    walker* const hoop = fx.world().find_by_id(armed_id);
    ASSERT_NE(nullptr, hoop) << "the armed hoop must survive the window";
    EXPECT_EQ(static_cast<int>(shadow->family()) + 1,
              static_cast<int>(hoop->family()))
        << "fx-hoop sorts (and numbers) directly after fx-bshadow (D30)";
    EXPECT_EQ(2, count_hoop_fx(fx.world(), hoop_family))
        << "one hoop per active team on the authority";
    EXPECT_EQ(2, count_hoop_fx(mirror.world(), hoop_family))
        << "both hoops crossed the wire";
    walker* const m_hoop = mirror.world().find_by_id(armed_id);
    ASSERT_NE(nullptr, m_hoop) << "the mirror must materialize the hoop";
    EXPECT_EQ(static_cast<int>(hoop->family()),
              static_cast<int>(m_hoop->family()));
    EXPECT_EQ(hoop->xpos(), m_hoop->xpos());
    EXPECT_EQ(hoop->ypos(), m_hoop->ypos());
    EXPECT_EQ(hoop->team_num(), m_hoop->team_num())
        << "the tint stamp replicates";
    EXPECT_EQ(hoop->frame(), m_hoop->frame())
        << "the driven net frame replicates — mirrors never animate";
    EXPECT_EQ(hoop->cycle(), m_hoop->cycle())
        << "the anim countdown replicates coherently (BIT_CYCLE)";

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
    EXPECT_EQ(shadow->entity_id(),
              static_cast<std::uint32_t>(
                  mirror.world().mode.cameras[0].entity_id))
        << "the camera declaration must point at the replicated shadow";
}

// The camera channel is replicated state, not a local render decision: a
// mirror never runs mode Lua, so it learns the declaration ONLY through a
// snapshot apply — and learning it must not cost a hash strike.
TEST_F(ModesBasketball, the_camera_declaration_replicates_to_a_client_mirror)
{
    ModesCtfWorld fx(kBballLevelA);
    fx.spawn_anchor(0, 128, 448);
    fx.spawn_anchor(1, 512, 448);
    fx.spawn_living(FAMILY_SOLDIER, 0, 128, 128);
    fx.spawn_living(FAMILY_SOLDIER, 1, 512, 128);
    ModeMirror mirror(kBballLevelA);

    // Ordering: empty BEFORE the first apply — mirrors run no mode Lua.
    EXPECT_EQ(0, mirror.world().mode.cameras[0].entity_id)
        << "a mirror has no camera before its first snapshot apply";
    EXPECT_FALSE(mirror.world().mode.active);

    // One tick: the authority inits and the mirror applies that keyframe.
    const MirrorReplication first = replicate_to_mirror(fx, mirror, 1);
    EXPECT_EQ(0, first.strikes) << "the camera slot must hash identically";
    ASSERT_TRUE(fx.world().mode.active);

    const std::int32_t declared = fx.world().mode.cameras[0].entity_id;
    ASSERT_NE(0, declared) << "the authority declared a camera on init";
    EXPECT_EQ(fx.var(kBbShadowEntity), declared) << "the shadow, not the ball";
    EXPECT_EQ(declared, mirror.world().mode.cameras[0].entity_id)
        << "the mirror follows the same replicated entity id";
    EXPECT_EQ(fx.world().mode.cameras[0].style,
              mirror.world().mode.cameras[0].style);
    EXPECT_NE(nullptr, mirror.world().find_by_id(
                           static_cast<std::uint32_t>(declared)))
        << "the followed shadow resolves in the mirror world too";

    // ... and stays matched across a live window of play.
    const MirrorReplication rest = replicate_to_mirror(fx, mirror, 60);
    EXPECT_EQ(0, rest.strikes)
        << "the mirror first desynced at tick " << rest.first_strike_tick;
    EXPECT_EQ(fx.world().mode.cameras[0].entity_id,
              mirror.world().mode.cameras[0].entity_id);
    EXPECT_EQ(fx.world().mode.cameras[0].style,
              mirror.world().mode.cameras[0].style);
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
        fx.world().ctf_requested_fill[static_cast<std::size_t>(team)] =
            og::sim::kFillFair;  // E5: bot sides
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

// The slot map is exactly full: #225 did the slot review R4 asked for and
// spent 63 on the item clock, so what used to be "nothing may write 63" is
// now "the item clock owns 63, seeded once at init". 9702 authors no pads,
// so the clock is written by init and never again — which also pins that a
// pad-free row costs the header cursor nothing.
TEST_F(ModesBasketball, slot_budget_is_exactly_full_item_clock_owns_63)
{
    ModesCtfWorld fx(kBballLevelB);
    for (int team = 0; team < 4; ++team)
    {
        const short x = static_cast<short>(128 + 64 * team);
        fx.spawn_anchor(team, x, 680);
        fx.spawn_anchor(team, x, 720);
        fx.world().ctf_requested_fill[static_cast<std::size_t>(team)] =
            og::sim::kFillFair;  // E5: bot sides
    }
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(1, fx.var(kBbItemLast))
        << "init seeds the item clock with the world tick it ran on";
    fx.tick(150);
    EXPECT_EQ(1, fx.var(kBbItemLast))
        << "9702 authors no pads, so no firing ever restarts the clock";
    EXPECT_EQ(0, fx.var(kBbItemCursor))
        << "and the header-band pad cursor stays untouched";
}

// ===========================================================================
// §11.2 #24 — instruction-budget headroom (10x-reduced budget)
// ===========================================================================

// BudgetOverride (RAII budget restore) now lives in modes_pack_fixture.h,
// shared with the matched-teams budget probe in test_modes_tdm.cpp.

TEST_F(ModesBasketball, instruction_budget_headroom)
{
    const BudgetOverride budget(500000);
    ModesCtfWorld fx(kBballLevelA);
    fx.spawn_anchor(0, 128, 448);
    fx.spawn_anchor(0, 128, 512);
    fx.spawn_anchor(1, 512, 448);
    fx.spawn_anchor(1, 512, 512);
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
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
    const std::uint32_t fresh_id = fresh->entity_id();
    fx.tick(1);
    EXPECT_FALSE(fx.weapon_present(fresh_id)) << "consumed";
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
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    // E5: turned AFTER init (a FAIR at init would field a D3 squad beside
    // the authored green troop); the watchdog refield reads it at spawn.
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
    fx.give_ball(fx.red, 350, 480);
    // Keep the shot clock out of the way: the winner CARRIES the ball the
    // whole time — the babysit shape the watchdog exists for.
    fx.world().mode.vars[kBbClockUntil] =
        static_cast<std::int32_t>(fx.world().tick_count_) + 5000;
    fx.green->set_dead(1);
    fx.tick(1);
    ASSERT_EQ(0, alive_on_team(fx.world(), 1));
    // The always-on revive would own this comeback; drain it (entry AND
    // body, the full-queue swept state) — the watchdog arms only for a
    // side with zero live AND zero pending.
    fx.world().respawn.respawn_queue.clear();
    retire_dead_guyless(fx.world(), 1);
    fx.tick(1);
    const std::int32_t since = fx.var(kBbStallSince);
    ASSERT_GT(since, 0) << "the wipe watchdog armed";

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
// §11.2 #38 — D27: a point-blank weapon dies on its own act tick and is
// consumed anyway; one weapon per possession
// ===========================================================================

TEST_F(ModesBasketball, point_blank_release_still_throws)
{
    // A defender parked over the weapon's first step: the walk is blocked,
    // the weapon melees the defender and dies (walker.cpp act_fire) BEFORE
    // the mode tick — the old live-only scan skipped it and the shoot key
    // released nothing. D27: the dead weapon is still on the weaplist
    // (the sweep runs after the mode tick) and the throw releases.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        walker* defender = fx.spawn_living(FAMILY_SOLDIER, 1, 468, 556);
        ASSERT_NE(nullptr, defender);
        const float defender_hp = defender->stats()->hitpoints();
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        // Aligned curdir: the first act WALKS (no turn tick) straight into
        // the defender and dies this tick. Staged d < 4.0 keeps the chip
        // deterministic (see smash()).
        shot->set_curdir(FACE_RIGHT);
        shot->set_damage(3.0f);
        const std::uint32_t shot_id = shot->entity_id();
        fx.tick(1);

        EXPECT_LT(defender->stats()->hitpoints(), defender_hp)
            << "the point-blank melee landed — the weapon really died on "
            << "its own act, not in the consumption";
        EXPECT_FALSE(fx.weapon_present(shot_id))
            << "the dead weapon is consumed as the throw (D27)";
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "aim (8,0) from (450,480): the release still happens";
        EXPECT_EQ(2, fx.var(kBbShotHoop1));
        EXPECT_EQ(0, fx.carrier()) << "possession cleared by the release";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Two carrier weapons the same tick: the FIRST in weaplist order is
    // the throw; the second flies on and never triggers a second release
    // (one weapon per POSSESSION — the release clears CARRIER).
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        walker* first = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, first);
        walker* second = fx.spawn_weapon(fx.red, 400, 560, 0.0f, -8.0f);
        ASSERT_NE(nullptr, second);
        second->set_lineofsight(50);  // outlive the assertion window
        const std::uint32_t first_id = first->entity_id();
        const std::uint32_t second_id = second->entity_id();
        fx.tick(1);

        EXPECT_FALSE(fx.weapon_present(first_id))
            << "the first in weaplist order becomes the throw";
        EXPECT_TRUE(fx.weapon_present(second_id))
            << "the second same-tick weapon is NOT consumed";
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "the first weapon's (8,0) aim classified: the east hoop";
        for (int i = 0; i < 3; ++i)
        {
            fx.tick(1);
            EXPECT_TRUE(fx.weapon_present(second_id))
                << "tick +" << (i + 1) << ": it flies on as a plain weapon";
            EXPECT_EQ(kStateShot, fx.var(kBbBallState))
                << "tick +" << (i + 1) << ": no second release mid-flight";
        }
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Third arm: the OTHER point-blank shape — the defender already parked
    // ON the weapon spawn pad, so walker::fire() itself melees and kills
    // its own spawn (walker.cpp:594-628: set_dead(1), return nullptr) with
    // death_called() still 0, AFTER deducting weapon_cost (walker.cpp:514).
    // Consumed via D27's ACT_CONTROL arm; the refund makes the fire()-time
    // death net zero too. A future gate keyed on death_called() == 1 alone
    // would regress exactly this shape — this arm is its tripwire.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        statistics* st = fx.red->stats();
        st->set_max_magicpoints(30.0f);
        st->set_magicpoints(20.0f);  // below max: the clamp never engages
        st->set_magic_per_round(0.0f);
        st->set_current_magic_delay(0);
        st->set_max_magic_delay(1000);
        const float pre = st->magicpoints();
        const float cost = static_cast<float>(st->weapon_cost());
        ASSERT_GT(cost, 0.0f);
        // Carrier top-left (442,472): the FACE_RIGHT pad starts at x = 459.
        // (460,468) overlaps every knife-sized pad rect while staying clear
        // of the carrier's own 16x16 box (ends at x = 458).
        walker* defender = fx.spawn_living(FAMILY_SOLDIER, 1, 460, 468);
        ASSERT_NE(nullptr, defender);
        const float defender_hp = defender->stats()->hitpoints();
        fx.red->set_damage(3.0f);  // deterministic chip (see smash())
        fx.red->set_lastx(8.0f);
        fx.red->set_lasty(0.0f);
        ASSERT_EQ(nullptr, fx.red->fire())
            << "pad blocked: fire() melees and returns no weapon";
        ASSERT_EQ(pre - cost, st->magicpoints())
            << "the engine deducted before the pad probe (walker.cpp:514)";
        ASSERT_FALSE(fx.world().weaplist.empty());
        walker* dead_shot = fx.world().weaplist.back().get();
        ASSERT_NE(nullptr, dead_shot);
        ASSERT_NE(0, static_cast<int>(dead_shot->dead()));
        ASSERT_EQ(0, static_cast<int>(dead_shot->death_called()))
            << "the fire()-time pad death never calls death()";
        ASSERT_EQ(fx.red, dead_shot->owner());
        const std::uint32_t shot_id = dead_shot->entity_id();
        fx.tick(1);

        EXPECT_LT(defender->stats()->hitpoints(), defender_hp)
            << "the pad melee chip landed before the consumption";
        EXPECT_FALSE(fx.weapon_present(shot_id))
            << "the fire()-time dead weapon is consumed (D27 ACT_CONTROL arm)";
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "aim (8,0): the throw still releases";
        EXPECT_EQ(0, fx.carrier()) << "possession cleared by the release";
        EXPECT_EQ(pre, st->magicpoints())
            << "float-exact net zero across fire()-time death + consume (D28)";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
}

// ===========================================================================
// §11.2 #39 — D28: a consumed throw refunds the carrier's weapon_cost
// ===========================================================================

TEST_F(ModesBasketball, throw_refund_is_mana_neutral)
{
    // Net zero across fire + consume: drive walker::fire() for real so the
    // engine deducts weapon_cost (walker.cpp:514), then the consumption
    // refunds it. Regen is staged inert (per_round 0, delay far from its
    // pulse) so the equality below isolates the refund.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        statistics* st = fx.red->stats();
        st->set_max_magicpoints(30.0f);
        st->set_magicpoints(20.0f);  // below max: the clamp never engages
        st->set_magic_per_round(0.0f);
        st->set_current_magic_delay(0);
        st->set_max_magic_delay(1000);
        const float pre = st->magicpoints();
        const float cost = static_cast<float>(st->weapon_cost());
        ASSERT_GT(cost, 0.0f) << "soldier fire_mp_cost must be > 0";
        fx.red->set_lastx(8.0f);
        fx.red->set_lasty(0.0f);
        walker* shot = fx.red->fire();
        ASSERT_NE(nullptr, shot) << "mp >= cost: the engine gate passes";
        const std::uint32_t shot_id = shot->entity_id();
        ASSERT_EQ(pre - cost, st->magicpoints())
            << "the engine really deducted (walker.cpp:514)";
        fx.tick(1);

        EXPECT_FALSE(fx.weapon_present(shot_id)) << "consumed as the throw";
        EXPECT_NE(kStateCarried, fx.var(kBbBallState)) << "released";
        EXPECT_EQ(pre, st->magicpoints())
            << "float-exact net zero across fire + consume (D28)";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Clamp arm: a full-mp carrier consuming an UNPAID fixture weapon must
    // stay exactly at max — the C++ setter does not clamp, the Lua og.min
    // must.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        statistics* st = fx.red->stats();
        st->set_max_magicpoints(30.0f);
        st->set_magicpoints(30.0f);
        ASSERT_GT(st->weapon_cost(), 0);
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        const std::uint32_t shot_id = shot->entity_id();
        fx.tick(1);

        EXPECT_FALSE(fx.weapon_present(shot_id));
        EXPECT_EQ(30.0f, st->magicpoints())
            << "refund clamped at max_magicpoints (D28)";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Control arm: a NON-carrier's fired weapon is never consumed, so its
    // cost stays spent — no refund without consumption.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        statistics* gst = fx.green->stats();
        gst->set_max_magicpoints(30.0f);
        gst->set_magicpoints(20.0f);
        gst->set_magic_per_round(0.0f);
        gst->set_current_magic_delay(0);
        gst->set_max_magic_delay(1000);
        const float gpre = gst->magicpoints();
        const float gcost = static_cast<float>(gst->weapon_cost());
        ASSERT_GT(gcost, 0.0f);
        fx.green->set_lastx(8.0f);
        fx.green->set_lasty(0.0f);
        walker* shot = fx.green->fire();
        ASSERT_NE(nullptr, shot);
        const std::uint32_t shot_id = shot->entity_id();
        fx.tick(1);

        EXPECT_TRUE(fx.weapon_present(shot_id))
            << "not the carrier's weapon: never consumed";
        EXPECT_EQ(kStateCarried, fx.var(kBbBallState)) << "red still carries";
        EXPECT_EQ(gpre - gcost, gst->magicpoints())
            << "non-carrying combat still costs (D28 control)";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
}

// A ball release borrows the normal-fire input, but the consumed knife never
// reaches core:knife's death/return path. A fresh level-1 soldier has exactly
// one returning blade, so failing to restore that launch credit disables the
// soldier's ranged attack for the rest of the match.
TEST_F(ModesBasketball, consumed_throw_restores_soldiers_returning_knife)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 450, 480);

    fx.red->set_weapons_left(1);
    fx.red->stats()->set_magicpoints(20.0f);
    fx.red->set_lastx(8.0f);
    fx.red->set_lasty(0.0f);
    walker* throw_token = fx.red->fire();
    ASSERT_NE(nullptr, throw_token);
    EXPECT_EQ(0, fx.red->weapons_left())
        << "the real soldier fire hook must spend its only knife first";

    fx.tick(1);

    EXPECT_NE(kStateCarried, fx.var(kBbBallState))
        << "the fired knife must release the basketball";
    EXPECT_EQ(1, fx.red->weapons_left())
        << "consuming the throw token must restore the returning knife";
    walker* combat_knife = fx.red->fire();
    EXPECT_NE(nullptr, combat_knife)
        << "the fresh soldier must still be able to throw a combat knife";
}

TEST_F(ModesBasketball, consumed_dead_knife_keeps_its_single_return_credit)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 450, 480);

    fx.red->set_weapons_left(1);
    fx.red->stats()->set_magicpoints(20.0f);
    fx.red->set_lastx(8.0f);
    fx.red->set_lasty(0.0f);
    walker* throw_token = fx.red->fire();
    ASSERT_NE(nullptr, throw_token);
    EXPECT_EQ(0, fx.red->weapons_left());

    // Model the D27 contact-death arm before the mode tick. death() has
    // already spawned the normal knife_back, so consume_throw must not mint
    // a second immediate credit. Keep the returner far enough away that it
    // cannot naturally reach its owner during this assertion tick.
    throw_token->setxy(static_cast<std::int32_t>(fx.red->xpos()) + 100,
                       static_cast<std::int32_t>(fx.red->ypos()));
    throw_token->set_dead(1);
    ASSERT_TRUE(throw_token->death());
    int returners = 0;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* candidate = uptr.get();
        if (candidate != nullptr && !candidate->dead() &&
            candidate->query_order() == Order::FX &&
            candidate->family() == FAMILY_KNIFE_BACK &&
            candidate->owner() == fx.red)
        {
            ++returners;
        }
    }
    ASSERT_EQ(1, returners) << "knife death must own the eventual refund";

    fx.tick(1);

    EXPECT_NE(kStateCarried, fx.var(kBbBallState));
    EXPECT_EQ(0, fx.red->weapons_left())
        << "the existing knife_back must remain the only return credit";
}

// ===========================================================================
// §11.2 #40 — edge #28 pin: a carrier below weapon_cost cannot release
// (engine-gated pre-spawn; the accepted residual of D28)
// ===========================================================================

TEST_F(ModesBasketball, drained_carrier_release_residual)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(fx.red, 450, 480);
    statistics* st = fx.red->stats();
    st->set_max_magicpoints(30.0f);
    st->set_magic_per_round(0.0f);
    st->set_current_magic_delay(0);
    st->set_max_magic_delay(1000);
    const float cost = static_cast<float>(st->weapon_cost());
    ASSERT_GT(cost, 0.0f);
    st->set_magicpoints(cost - 1.0f);  // below the engine's pre-spawn gate
    const float pre = st->magicpoints();
    fx.red->set_lastx(8.0f);
    fx.red->set_lasty(0.0f);

    EXPECT_EQ(nullptr, fx.red->fire())
        << "walker.cpp:506-507: no weapon spawns below weapon_cost";
    fx.tick(1);

    EXPECT_EQ(kStateCarried, fx.var(kBbBallState))
        << "nothing to consume: no release (edge #28, out of Lua reach)";
    EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()), fx.carrier());
    EXPECT_EQ(pre, st->magicpoints()) << "nothing spent, nothing refunded";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// §11.2 #41 — D27 probe immunity: fire_check scratch weapons are never
// consumed as throws and never mint mana (edge #29)
// ===========================================================================

TEST_F(ModesBasketball, fire_check_probes_never_release_or_mint)
{
    // walker::fire_check (walker.cpp:1245-1346) creates a REAL weaplist
    // weapon as its ray probe — owner set, heading set — and set_dead(1)s
    // it on every denial/miss/success path WITHOUT paying weapon_cost. An
    // AI carrier engaging an in-range foe produces one per engagement tick
    // (act_random, and every COMMAND_FIRE/ATTACK dispatch). An unqualified
    // dead-weapon scan consumed them — a phantom release the carrier never
    // triggered — and the D28 refund credited a cost that was never paid.
    // Both arms drive the COMMAND_FIRE dispatch (stats.cpp:407), which
    // runs fire_check on every dispatch tick.
    //
    // Arm 1 — the self-refuel exploit: a DRAINED bot carrier (mp < cost)
    // produces NoMagic-denial probes. Nothing may release, and mp must not
    // move: edge #28's engine gate must not be bypassable through probes.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        walker* foe = fx.spawn_living(FAMILY_SOLDIER, 1, 476, 472);
        ASSERT_NE(nullptr, foe);
        // Morph the carrier into an engine-AI bot the mode director skips
        // (user != -1 fails is_directable, so the script-invoked release
        // ladder stays out of the picture), with every non-probe mana
        // writer staged inert: regen off, specials off, and init_fire
        // busy-blocked so no REAL fire() can occur during the window.
        fx.red->set_act_type(ACT_RANDOM);
        fx.red->set_user(0);
        fx.red->set_specials_disabled(true);
        fx.red->set_busy(10000.0f);
        statistics* st = fx.red->stats();
        st->set_max_magicpoints(30.0f);
        st->set_magic_per_round(0.0f);
        st->set_current_magic_delay(0);
        st->set_max_magic_delay(1000);
        const float cost = static_cast<float>(st->weapon_cost());
        ASSERT_GT(cost, 0.0f);
        st->set_magicpoints(cost - 1.0f);  // below the engine's fire gate
        const float pre = st->magicpoints();
        for (int i = 0; i < 8; ++i)
        {
            fx.red->set_foe(foe);  // fire_check's no-foe path must not run
            st->force_command(COMMAND_FIRE, 1, 8, 0);
            fx.tick(1);
            ASSERT_EQ(kStateCarried, fx.var(kBbBallState))
                << "tick " << i << ": a dead probe was consumed as a throw";
            ASSERT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                      fx.carrier());
            ASSERT_EQ(pre, st->magicpoints())
                << "tick " << i << ": the refund minted unpaid mana";
        }
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Arm 2 — mana available, facing aligned: fire_check passes every gate
    // and dies as a RAY probe (collide_ob set when the ray hits the foe),
    // the hardest probe to tell from a real fire()-time death. busy keeps
    // init_fire refusing, so no real fire() can follow the probe. Still no
    // release, still no mana movement.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        fx.give_ball(fx.red, 450, 480);
        walker* foe = fx.spawn_living(FAMILY_SOLDIER, 1, 476, 472);
        ASSERT_NE(nullptr, foe);
        fx.red->set_act_type(ACT_RANDOM);
        fx.red->set_user(0);
        fx.red->set_specials_disabled(true);
        fx.red->set_busy(10000.0f);
        fx.red->set_curdir(FACE_RIGHT);  // aligned: the Facing gate passes
        statistics* st = fx.red->stats();
        st->set_max_magicpoints(30.0f);
        st->set_magicpoints(20.0f);
        st->set_magic_per_round(0.0f);
        st->set_current_magic_delay(0);
        st->set_max_magic_delay(1000);
        const float pre = st->magicpoints();
        for (int i = 0; i < 8; ++i)
        {
            fx.red->set_foe(foe);
            st->force_command(COMMAND_FIRE, 1, 8, 0);
            fx.tick(1);
            ASSERT_EQ(kStateCarried, fx.var(kBbBallState))
                << "tick " << i << ": a dead probe was consumed as a throw";
            ASSERT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
                      fx.carrier());
            ASSERT_EQ(pre, st->magicpoints())
                << "tick " << i << ": mana moved without any real fire()";
        }
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
}

// ===========================================================================
// §11.2 #42 — D29/D30: hoop spawn counts, tint stamps, placement, lifecycle
// ===========================================================================

TEST_F(ModesBasketball, hoop_spawn_count_and_tint)
{
    // 9701: exactly one team-tinted rim per active team, spawned AFTER the
    // shadow so the ball/shadow entity ids every older test observed stay
    // put (the D30 spawn-order pin).
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        const int hoop_family = hoop_family_byte();
        ASSERT_GE(hoop_family, 0) << "modes:hoop must install on mount";
        walker* ball = fx.ball();
        walker* shadow = fx.shadow();
        ASSERT_NE(nullptr, ball);
        ASSERT_NE(nullptr, shadow);
        EXPECT_EQ(static_cast<int>(shadow->family()) + 1, hoop_family)
            << "fx-hoop sorts (and numbers) directly after fx-bshadow (D30)";
        EXPECT_EQ(2, count_hoop_fx(fx.world(), hoop_family))
            << "a two-team court hangs exactly two rims";

        walker* west = find_hoop_fx(fx.world(), hoop_family, 0);
        walker* east = find_hoop_fx(fx.world(), hoop_family, 1);
        ASSERT_NE(nullptr, west) << "team 0's rim carries its tint stamp";
        ASSERT_NE(nullptr, east) << "team 1's rim carries its tint stamp";
        EXPECT_EQ(kBballHoop0X - 12, west->xpos())
            << "the 24x20 rim is centered on the manifest hoop pixel";
        EXPECT_EQ(kBballHoop0Y - 10, west->ypos());
        EXPECT_EQ(kBballHoop1X - 12, east->xpos());
        EXPECT_EQ(kBballHoop1Y - 10, east->ypos());
        EXPECT_EQ(0, static_cast<int>(west->frame())) << "spawned idle";
        EXPECT_EQ(0, static_cast<int>(east->frame()));
        EXPECT_EQ(0, static_cast<int>(west->cycle())) << "no program armed";
        EXPECT_EQ(0, static_cast<int>(east->cycle()));
        EXPECT_EQ(ball->entity_id() + 1, shadow->entity_id())
            << "ball then shadow — byte-identical to the pre-hoop order";
        EXPECT_EQ(shadow->entity_id() + 1, west->entity_id())
            << "hoops take the ids AFTER the shadow (D30)";
        EXPECT_EQ(shadow->entity_id() + 2, east->entity_id());

        // Lifecycle: a scoring center reset moves the BALL, never the
        // hoops — position, team and the unscored rim's idle all hold.
        const std::uint32_t west_id = west->entity_id();
        const std::uint32_t east_id = east->entity_id();
        fx.give_ball(fx.red, 450, 480);
        fx.red->setxy(552, 472);  // the east dunk box forces a reset
        fx.tick(1);
        ASSERT_TRUE(has_notification(fx.events, "DUNK! RED +2"));
        ASSERT_EQ(kBballJumpX, fx.ball_cx()) << "the reset re-spotted play";
        walker* west_after = fx.world().find_by_id(west_id);
        walker* east_after = fx.world().find_by_id(east_id);
        ASSERT_NE(nullptr, west_after) << "the reset must not despawn rims";
        ASSERT_NE(nullptr, east_after);
        EXPECT_EQ(kBballHoop0X - 12, west_after->xpos());
        EXPECT_EQ(kBballHoop0Y - 10, west_after->ypos());
        EXPECT_EQ(kBballHoop1X - 12, east_after->xpos());
        EXPECT_EQ(kBballHoop1Y - 10, east_after->ypos());
        EXPECT_EQ(0, west_after->team_num()) << "never restamped";
        EXPECT_EQ(1, east_after->team_num());
        EXPECT_EQ(0, static_cast<int>(west_after->frame()))
            << "the unscored rim stays idle through the reset";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // 9702: all four teams active — four rims, four tints, each centered
    // on its own wall's manifest hoop.
    {
        BballFourCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        const int hoop_family = hoop_family_byte();
        ASSERT_GE(hoop_family, 0);
        EXPECT_EQ(4, count_hoop_fx(fx.world(), hoop_family));
        const int hoop_x[4] = {320, 576, 320, 64};
        const int hoop_y[4] = {64, 480, 896, 480};
        for (int team = 0; team < 4; ++team)
        {
            walker* hoop = find_hoop_fx(fx.world(), hoop_family, team);
            ASSERT_NE(nullptr, hoop) << "team " << team;
            EXPECT_EQ(hoop_x[team] - 12, hoop->xpos()) << "team " << team;
            EXPECT_EQ(hoop_y[team] - 10, hoop->ypos()) << "team " << team;
            EXPECT_EQ(0, static_cast<int>(hoop->frame())) << "team " << team;
        }
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
}

// ===========================================================================
// §11.2 #43 — edge #30: inactive-team goals on the four-hoop court get NO
// sprite — the bare carpet is the dead-goal tell
// ===========================================================================

TEST_F(ModesBasketball, hoop_partial_spawn_two_of_four)
{
    // Teams 0 and 2 field anchors on the four-hoop court: a two-team
    // activation. Only the goals that can score get rims.
    ModesCtfWorld fx(kBballLevelB);
    for (int team = 0; team < 4; team += 2)
    {
        const short x = static_cast<short>(128 + 64 * team);
        fx.spawn_anchor(team, x, 700);
        fx.spawn_living(FAMILY_SOLDIER, team, x, 700);
    }
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_EQ(5, fx.var(kBbTeamMask));
    ASSERT_EQ(0, fx.team_var(kBbHoopPos, 1)) << "inactive teams bank no hoop";
    ASSERT_EQ(0, fx.team_var(kBbHoopPos, 3));

    const int hoop_family = hoop_family_byte();
    ASSERT_GE(hoop_family, 0);
    EXPECT_EQ(2, count_hoop_fx(fx.world(), hoop_family))
        << "exactly the ACTIVE teams' rims spawn (edge #30)";
    walker* north = find_hoop_fx(fx.world(), hoop_family, 0);
    walker* south = find_hoop_fx(fx.world(), hoop_family, 2);
    ASSERT_NE(nullptr, north);
    ASSERT_NE(nullptr, south);
    EXPECT_EQ(320 - 12, north->xpos());
    EXPECT_EQ(64 - 10, north->ypos());
    EXPECT_EQ(320 - 12, south->xpos());
    EXPECT_EQ(896 - 10, south->ypos());
    EXPECT_EQ(nullptr, find_hoop_fx(fx.world(), hoop_family, 1))
        << "a dead goal gets no rim sprite";
    EXPECT_EQ(nullptr, find_hoop_fx(fx.world(), hoop_family, 3));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// §11.2 #44 — D31: the swish program, tick-exact, from every scoring arm
// ===========================================================================

TEST_F(ModesBasketball, hoop_swish_sequence_tick_exact)
{
    // Dunk arm — drives the FULL program: frame 1 on the score tick, 2 at
    // +4, 3 at +8, idle at +12, playing THROUGH the same-tick center
    // reset's jump freeze (armed before the reset, D31). The other rim
    // never moves.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        const int hoop_family = hoop_family_byte();
        ASSERT_GE(hoop_family, 0);
        fx.give_ball(fx.red, 450, 480);
        fx.red->setxy(552, 472);
        fx.tick(1);  // the dunk scores, arms the swish and resets
        ASSERT_TRUE(has_notification(fx.events, "DUNK! RED +2"));
        ASSERT_GT(fx.var(kBbJumpUntil),
                  static_cast<std::int32_t>(fx.world().tick_count_))
            << "the reset froze play — the ripple must outlive it";
        walker* scored = find_hoop_fx(fx.world(), hoop_family, 1);
        walker* other = find_hoop_fx(fx.world(), hoop_family, 0);
        ASSERT_NE(nullptr, scored);
        ASSERT_NE(nullptr, other);
        EXPECT_EQ(1, static_cast<int>(scored->frame()))
            << "score tick: the ripple starts the SAME tick";
        EXPECT_EQ(11, static_cast<int>(scored->cycle()));
        EXPECT_EQ(0, static_cast<int>(other->frame()));
        fx.tick(3);
        EXPECT_EQ(1, static_cast<int>(scored->frame())) << "+3: frame 1";
        fx.tick(1);
        EXPECT_EQ(2, static_cast<int>(scored->frame())) << "+4: frame 2";
        fx.tick(4);
        EXPECT_EQ(3, static_cast<int>(scored->frame())) << "+8: frame 3";
        fx.tick(3);
        EXPECT_EQ(3, static_cast<int>(scored->frame()))
            << "+11: the last swish tick";
        fx.tick(1);
        EXPECT_EQ(0, static_cast<int>(scored->frame())) << "+12: idle again";
        EXPECT_EQ(0, static_cast<int>(scored->cycle()));
        fx.tick(2);
        EXPECT_EQ(0, static_cast<int>(scored->frame()))
            << "idle is idempotent (the c == 0 self-heal)";
        EXPECT_EQ(0, static_cast<int>(other->frame()))
            << "the unscored rim held frame 0 throughout";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Arc-shot arm (T8): idle through the flight, ripple on resolution.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        const int hoop_family = hoop_family_byte();
        fx.give_ball(fx.red, 500, 480);
        walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
        ASSERT_NE(nullptr, shot);
        fx.world().rng_.state_ = find_scatter_seed(10, 0, 8);
        fx.tick(1);
        ASSERT_EQ(kStateShot, fx.var(kBbBallState));
        walker* target = find_hoop_fx(fx.world(), hoop_family, 1);
        ASSERT_NE(nullptr, target);
        EXPECT_EQ(0, static_cast<int>(target->frame()))
            << "no ripple while the arc is still in the air";
        tick_until_state_leaves(fx, kStateShot, 40);
        ASSERT_TRUE(has_notification(fx.events, "BASKET! RED +2"));
        EXPECT_EQ(1, static_cast<int>(target->frame()))
            << "T8 resolution swishes the target rim the same tick";
        EXPECT_EQ(11, static_cast<int>(target->cycle()));
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Goaltend arm (T12): the frozen award swishes — never the clang.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        const int hoop_family = hoop_family_byte();
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
        walker* swat = fx.spawn_weapon(fx.green, 567, 477, 4.0f, 0.0f);
        ASSERT_NE(nullptr, swat);
        fx.tick(1);
        ASSERT_TRUE(has_notification(fx.events, "GOALTEND! RED +3"));
        walker* target = find_hoop_fx(fx.world(), hoop_family, 1);
        walker* other = find_hoop_fx(fx.world(), hoop_family, 0);
        ASSERT_NE(nullptr, target);
        ASSERT_NE(nullptr, other);
        EXPECT_EQ(1, static_cast<int>(target->frame()))
            << "goaltend pays the basket, so it SWISHES (concept ruling)";
        EXPECT_EQ(11, static_cast<int>(target->cycle()))
            << "a positive program — the clang would read -7 here";
        EXPECT_EQ(0, static_cast<int>(other->frame()));
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Crossing arm (T16): a descending rim-plane crossing swishes the
    // DEFENDING rim it crossed.
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        const int hoop_family = hoop_family_byte();
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateRebound;
        vars[kBbLastTouch1] = 1;  // team 0 touched: the tip-in credits it
        vars[kBbBallPx] = 576 * 256;
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 33 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -300;
        fx.tick(1);
        ASSERT_TRUE(has_notification(fx.events, "BASKET! RED +2"));
        walker* crossed = find_hoop_fx(fx.world(), hoop_family, 1);
        walker* other = find_hoop_fx(fx.world(), hoop_family, 0);
        ASSERT_NE(nullptr, crossed);
        ASSERT_NE(nullptr, other);
        EXPECT_EQ(1, static_cast<int>(crossed->frame()));
        EXPECT_EQ(11, static_cast<int>(crossed->cycle()));
        EXPECT_EQ(0, static_cast<int>(other->frame()));
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    // Own-basket arm: the ball went in regardless of credit — the CROSSED
    // rim ripples even though the points go to the rival (D31).
    {
        BballCourt fx;
        fx.tick(1);
        ASSERT_TRUE(fx.basketball_active());
        const int hoop_family = hoop_family_byte();
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateRebound;
        vars[kBbLastTouch1] = 1;  // team 0 into its OWN west hoop
        vars[kBbBallPx] = 64 * 256;
        vars[kBbBallPy] = 480 * 256;
        vars[kBbBallPz] = 33 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = -300;
        fx.tick(1);
        ASSERT_TRUE(has_notification(fx.events, "OWN BASKET! GREEN +2"));
        walker* crossed = find_hoop_fx(fx.world(), hoop_family, 0);
        walker* other = find_hoop_fx(fx.world(), hoop_family, 1);
        ASSERT_NE(nullptr, crossed);
        ASSERT_NE(nullptr, other);
        EXPECT_EQ(1, static_cast<int>(crossed->frame()))
            << "the crossed rim swishes, not the beneficiary's";
        EXPECT_EQ(11, static_cast<int>(crossed->cycle()));
        EXPECT_EQ(0, static_cast<int>(other->frame()));
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
}

// ===========================================================================
// §11.2 #45 — D31: the clang flash, tick-exact, and the newest-event
// overwrite
// ===========================================================================

TEST_F(ModesBasketball, hoop_clang_flash_sequence)
{
    // The pinned rim-lip miss from #8: frame 4 on the clang tick, 5 at +4,
    // idle at +8.
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    const int hoop_family = hoop_family_byte();
    ASSERT_GE(hoop_family, 0);
    fx.give_ball(fx.red, 416, 480);
    walker* shot = fx.spawn_weapon(fx.red, 460, 560, 8.0f, 0.0f);
    ASSERT_NE(nullptr, shot);
    fx.world().rng_.state_ = find_scatter_seed(16, 15, 16);
    fx.tick(1);
    ASSERT_EQ(kStateShot, fx.var(kBbBallState));
    tick_until_state_leaves(fx, kStateShot, 40);
    ASSERT_EQ(kStateRebound, fx.var(kBbBallState)) << "the lip clang scrum";
    walker* rim = find_hoop_fx(fx.world(), hoop_family, 1);
    walker* other = find_hoop_fx(fx.world(), hoop_family, 0);
    ASSERT_NE(nullptr, rim);
    ASSERT_NE(nullptr, other);
    EXPECT_EQ(4, static_cast<int>(rim->frame()))
        << "clang tick: the bright flash, the same tick as SOUND_CLANG";
    EXPECT_EQ(-7, static_cast<int>(rim->cycle()))
        << "a negative program (the swish would read +11 here)";
    fx.tick(3);
    EXPECT_EQ(4, static_cast<int>(rim->frame())) << "+3: still bright";
    fx.tick(1);
    EXPECT_EQ(5, static_cast<int>(rim->frame())) << "+4: the dim flash";
    EXPECT_EQ(0, static_cast<int>(other->frame()));

    // A put-back crossing DURING the flash: the newest event overwrites
    // the countdown — swish from the top, mid-program (D31).
    auto& vars = fx.world().mode.vars;
    vars[kBbBallState] = kStateRebound;
    vars[kBbLastTouch1] = 1;
    vars[kBbBallPx] = 576 * 256;
    vars[kBbBallPy] = 480 * 256;
    vars[kBbBallPz] = 33 * 256;
    vars[kBbBallVx] = 0;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = -300;
    fx.tick(1);
    ASSERT_TRUE(has_notification(fx.events, "BASKET! RED +2"));
    EXPECT_EQ(1, static_cast<int>(rim->frame()))
        << "the put-back swish overwrites the flash";
    EXPECT_EQ(11, static_cast<int>(rim->cycle()));
    fx.tick(12);
    EXPECT_EQ(0, static_cast<int>(rim->frame()))
        << "the overwritten program completes normally";
    EXPECT_EQ(0, static_cast<int>(rim->cycle()));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// §11.2 #47 — edge #32: a winning basket freezes the net mid-ripple
// ===========================================================================

TEST_F(ModesBasketball, hoop_freezes_on_win_latch)
{
    // One dunk from the score limit: the winning basket's swish is armed
    // and drawn on the win tick, then the engine runs no more Lua — the
    // caught frame holds, the §9 #6 frozen-ball parity.
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    const int hoop_family = hoop_family_byte();
    ASSERT_GE(hoop_family, 0);
    fx.world().mode.vars[kBbPoints + 0] = 5;  // limit is 6
    fx.give_ball(fx.red, 450, 480);
    fx.red->setxy(552, 472);
    fx.tick(1);  // the dunk: 5 + 2 >= 6 latches the win THIS tick

    ASSERT_TRUE(has_notification(fx.events, "DUNK! RED +2"));
    ASSERT_TRUE(fx.world().mode.win_latched);
    EXPECT_EQ(0, fx.world().mode.winner_team);
    ASSERT_TRUE(fx.world().game_ended);
    walker* rim = find_hoop_fx(fx.world(), hoop_family, 1);
    ASSERT_NE(nullptr, rim);
    const std::uint32_t rim_id = rim->entity_id();
    EXPECT_EQ(1, static_cast<int>(rim->frame()))
        << "the win tick still ran its own driver pass";
    EXPECT_EQ(11, static_cast<int>(rim->cycle()));

    fx.tick(5);  // decided: no further Lua runs (mode_tick step 1)
    walker* held = fx.world().find_by_id(rim_id);
    ASSERT_NE(nullptr, held) << "the rim survives the latched ticks";
    EXPECT_EQ(1, static_cast<int>(held->frame()))
        << "the ripple freezes mid-frame after the latch (edge #32)";
    EXPECT_EQ(11, static_cast<int>(held->cycle()))
        << "the countdown froze with it";
    EXPECT_TRUE(fx.world().game_ended) << "the win re-asserts";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// R1 calibration sanity — normal bot games on the six SHIPPED courts.
// Loads the real scen824-829 geometry from builtin/modes.glad (the
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

struct RealCourtBotGamePin
{
    int id;
    int time_limit;
    int first_score_tick;
    int first_points[4];
    bool play_to_regulation;
    int final_tick;
    int final_points[4];
    int winner;
    bool game_ended;
    int ball_resets;
};

inline constexpr std::uint32_t kRealCourtBotGameSeed = 0x9E3779B9u;
inline constexpr RealCourtBotGamePin kRealCourtBotGamePins[] = {
    {824, 7200, 1174, {2, 0, 0, 0}, true, 7200, {7, 4, 0, 0}, 0, true, 0},
    {825, 5400, 142, {0, 2, 0, 0}, false, 142, {0, 2, 0, 0}, -1, false, 0},
    {826, 7200, 886, {0, 2, 0, 0}, false, 886, {0, 2, 0, 0}, -1, false, 0},
    {827, 7200, 363, {0, 2, 0, 0}, false, 363, {0, 2, 0, 0}, -1, false, 0},
    {828, 7200, 3255, {0, 2, 0, 0}, false, 3255, {0, 2, 0, 0}, -1, false, 0},
    {829, 7200, 460, {2, 0, 0, 0}, false, 460, {2, 0, 0, 0}, -1, false, 0},
};

}  // namespace

TEST(ModesBasketballRealCampaign, bot_games_score_on_every_shipped_court)
{
    og::test::ScopedCampaignMountState mount_restore;
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "builtin/modes.glad should restore and mount";
    // One exact simulation per court. This seed scores quickly even on 828,
    // whose default seed can reach the buzzer scoreless; retries would turn a
    // deterministic regression into a probabilistic pass. Court 824 continues
    // through regulation to pin the full-game and no-watchdog calibration.
    for (const RealCourtBotGamePin& expected : kRealCourtBotGamePins)
    {
        LoadedRealCourt fx(expected.id);
        ASSERT_TRUE(fx.loaded) << "scen" << expected.id << " must load";
        // E5: the shipped courts' empty sides field bots only under a turned
        // wheel — all-NONE would refuse the match.
        for (auto& knob : fx.world().ctf_requested_fill)
            knob = og::sim::kFillFair;
        fx.world().rng_.state_ = kRealCourtBotGameSeed;
        fx.world().tick();
        ASSERT_TRUE(fx.world().mode.active) << expected.id;
        ASSERT_EQ(kModeIdBasketball, fx.world().mode.vars[kBbModeId])
            << expected.id;
        const int time_limit = fx.world().mode.vars[kBbTimeLimit];
        ASSERT_EQ(expected.time_limit, time_limit) << expected.id;

        int ticks = 1;
        int first_score_tick = 0;
        int first_points[4] = {0, 0, 0, 0};
        while (ticks < time_limit && !fx.world().game_ended)
        {
            fx.world().tick();
            ticks++;
            if (first_score_tick == 0 && best_points(fx.world()) > 0)
            {
                first_score_tick = ticks;
                for (int team = 0; team < 4; ++team)
                {
                    first_points[team] = fx.world().mode.vars[
                        static_cast<std::size_t>(kBbPoints + team)];
                }
                if (!expected.play_to_regulation)
                    break;
            }
        }

        EXPECT_EQ(expected.first_score_tick, first_score_tick)
            << expected.id << " first-score tick";
        EXPECT_EQ(expected.final_tick, ticks) << expected.id << " stop tick";
        EXPECT_EQ(expected.final_tick,
                  static_cast<int>(fx.world().tick_count_))
            << expected.id << " world tick";
        for (int team = 0; team < 4; ++team)
        {
            EXPECT_EQ(expected.first_points[team], first_points[team])
                << expected.id << " first-score team " << team;
            EXPECT_EQ(expected.final_points[team],
                      fx.world().mode.vars[
                          static_cast<std::size_t>(kBbPoints + team)])
                << expected.id << " final team " << team;
        }
        EXPECT_EQ(expected.winner, fx.world().mode.winner_team)
            << expected.id;
        EXPECT_EQ(expected.game_ended, fx.world().game_ended)
            << expected.id;
        EXPECT_EQ(expected.game_ended, fx.world().mode.win_latched)
            << expected.id << " win latch";
        EXPECT_EQ(expected.ball_resets,
                  count_notifications(fx.events, "BALL RESET"))
            << expected.id << " dead-ball/wipe watchdog";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count)
            << expected.id;
        for (const auto& err : fx.world().scripts().host().errors())
            ADD_FAILURE() << "scen" << expected.id
                          << " script error: " << err.message;
    }
}

// ===========================================================================
// TROOPS: FAIR system tests (matched-teams WP-F, spec §8)
// ===========================================================================

namespace {

// Sorted walker-stats levels of a team's live Livings.
std::vector<int> team_levels_sorted(GameWorld& world, int team)
{
    std::vector<int> levels;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team))
            continue;
        if (w->stats() == nullptr)
            continue;
        levels.push_back(w->stats()->level());
    }
    std::sort(levels.begin(), levels.end());
    return levels;
}

// The reference court with a solo LEVELED roster hero on team 0 and an
// empty team 1 — the shape where OWN and FAIR differ only inside the
// bot spawn seam.
struct BballSoloRosterCourt : ModesCtfWorld
{
    walker* hero = nullptr;

    explicit BballSoloRosterCourt(short guy_level)
        : ModesCtfWorld(kBballLevelA)
    {
        spawn_anchor(0, 128, 448);
        spawn_anchor(0, 128, 512);
        spawn_anchor(1, 512, 448);
        spawn_anchor(1, 512, 512);
        // E5: the opponent's fill is a turned wheel now (the stored 0 is
        // NONE and would leave the solo roster unopposed).
        world().ctf_requested_fill[1] = og::sim::kFillFair;
        hero = spawn_leveled_hero(FAMILY_SOLDIER, 0, 128, 96, 1, guy_level);
    }

    bool basketball_active() const
    {
        return var(kBbModeId) == kModeIdBasketball;
    }
};

}  // namespace

// The FAIR-mask == OWN-mask twin (D26/D33(a), one-delta restated by D39):
// FAIR bundles OWN's whole deployment policy — same mask, same fill sites
// — and the ONLY delta is the generated squad, which spawns at matched
// power AND matched headcount: the solo hero's court is a 1v1, not the
// 5v5 the pre-amendment game shape pinned (D34 supersedes D12 here).
TEST_F(ModesBasketball, explicit_fair_matches_a_solo_roster_court)
{
    // FILL: FAIR (B2) solves the solo roster's opponent at matched power
    // AND matched headcount: a 1v1 court, not the old legacy five (D34).
    // E5: the fixture turns that wheel itself — the stored default is NONE
    // and would leave the roster unopposed.
    BballSoloRosterCourt matched(4);
    matched.tick(1);
    ASSERT_TRUE(matched.basketball_active());

    EXPECT_EQ(1, alive_on_team(matched.world(), 1))
        << "the generated squad matches the solo headcount (D34/D39)";
    EXPECT_EQ(1, matched.var(kSlotMatchedSize));

    ASSERT_GT(matched.var(kSlotMatchedTarget), 0);
    const int code = matched_plan_code(matched.var(kSlotMatchedPlan), 1);
    ASSERT_NE(0, code) << "the default solved team 1";
    EXPECT_EQ(0, code % 10) << "n = 1 admits no upgrades (D36)";
    const std::vector<int> matched_levels =
        team_levels_sorted(matched.world(), 1);
    ASSERT_EQ(1u, matched_levels.size());
    EXPECT_EQ(code / 10, matched_levels.front())
        << "the lone bot's level follows the stored plan";
    EXPECT_EQ(1, count_notifications(matched.events, "TEAMS MATCHED"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// I5/E4 for basketball: the wipe watchdog's reset backstop refields a
// wiped MATCHED team at the STORED (L*, k*) AND the stored headcount —
// identical strength and size, never the legacy formula — and stays
// silent mid-match (the solo-roster fixture makes this a 1v1, D34/D39).
TEST_F(ModesBasketball, wipe_watchdog_refields_a_matched_team_at_strength)
{
    BballSoloRosterCourt fx(4);
    arm_matched(fx.world());
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    const std::vector<int> at_init = team_levels_sorted(fx.world(), 1);
    ASSERT_EQ(1u, at_init.size())
        << "the solo roster's matched opponent is a single bot (D34)";
    const int code = matched_plan_code(fx.var(kSlotMatchedPlan), 1);
    ASSERT_NE(0, code);
    ASSERT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));

    for (const auto& uptr : fx.world().oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 1)
            w->set_dead(1);
    }
    fx.tick(2);  // the corpses persist; their revive entries ride the queue
    ASSERT_EQ(0, alive_on_team(fx.world(), 1));
    // The D39 reprovision arm guards the drained-queue edge (entries
    // genuinely lost, bodies swept — a full all-player queue): clear the
    // in-flight revives and their bodies so the watchdog reset refields
    // from the plan.
    fx.world().respawn.respawn_queue.clear();
    retire_dead_guyless(fx.world(), 1);

    fx.tick(598);
    for (int i = 0;
         i < 120 && count_notifications(fx.events, "BALL RESET") == 0; ++i)
        fx.tick(1);
    ASSERT_EQ(1, count_notifications(fx.events, "BALL RESET"))
        << "the wipe watchdog must reset the dead ball";
    EXPECT_EQ(1, alive_on_team(fx.world(), 1))
        << "revive_wiped_teams refields the wiped matched side at the "
           "stored headcount (D39)";
    EXPECT_EQ(at_init, team_levels_sorted(fx.world(), 1))
        << "the replacement squad reproduces the stored (L*, k*)";
    EXPECT_EQ(code, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "the plan is durable across the wipe";
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"))
        << "mid-match reprovision stays silent (§7)";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// Issue #235 — the seeded bot-squad permutation (mode_anchors.lua)
// ===========================================================================

namespace {

// The shared mode_anchors squad-seed header slot (#235): the latched
// squad-order code + 1, drawn once per match by the first bot-squad spawn.
inline constexpr int kSlotSquadSeed = 6;

// C++ twin of mode_match.walker_power under the §4.1 trunc-on-read
// discipline (the test_modes_tdm.cpp measurement idiom), to measure a
// spawned squad against the stored target.
long long trio_walker_f(const walker* w)
{
    const statistics* s = w->stats();
    if (s == nullptr)
        return 0;
    const long long hp = static_cast<long long>(s->max_hitpoints());
    const long long mp = static_cast<long long>(s->max_magicpoints());
    const long long armor = static_cast<long long>(s->armor());
    const long long dmg = static_cast<long long>(w->damage());
    const long long sp = static_cast<long long>(w->stepsize());
    long long ff = static_cast<long long>(w->fire_frequency());
    if (ff < 1)
        ff = 1;
    const long long level = s->level();
    const long long ed = dmg * (level + 3) / 4;
    const long long rate = 120 / ff;
    const long long off = ed * rate + 5 * sp;
    const long long ehp = hp + 4 * armor + mp / 2;
    return ehp * (off + 60) / 60;
}

long long trio_team_f_sum(GameWorld& world, int team)
{
    long long sum = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team))
            continue;
        sum += trio_walker_f(w);
    }
    return sum;
}

// One seeded init on the reference court: THREE leveled humans on team 0
// (the reporter's roster shape), anchors only on team 1, TROOPS: FAIR —
// the matched census latches SIZE = 3, so team 1's generated squad is the
// 3-prefix of the (seeded) squad order.
struct SeededTrio
{
    std::vector<int> families;  // team-1 bots in oblist (spawn) order
    std::int32_t seed_var = 0;
    std::int32_t target = 0;
    std::int32_t announced = 0;
    long long squad_f = 0;
};

SeededTrio run_seeded_trio(std::uint32_t rng_state)
{
    ModesCtfWorld fx(kBballLevelA);
    fx.spawn_anchor(0, 128, 448);
    fx.spawn_anchor(0, 128, 512);
    fx.spawn_anchor(1, 512, 448);
    fx.spawn_anchor(1, 512, 512);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 128, 96, 1, 4);
    fx.spawn_leveled_hero(FAMILY_ARCHER, 0, 160, 96, 2, 4);
    fx.spawn_leveled_hero(FAMILY_ELF, 0, 192, 96, 3, 4);
    arm_matched(fx.world());
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: the fill
    fx.world().rng_.state_ = rng_state;  // pin the match seed
    fx.tick(1);
    EXPECT_TRUE(fx.world().mode.active);

    SeededTrio out;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != 1)
            continue;
        out.families.push_back(static_cast<int>(w->family()));
    }
    out.seed_var = fx.var(kSlotSquadSeed);
    out.target = fx.var(kSlotMatchedTarget);
    out.announced = fx.var(kSlotMatchedAnnounced);
    out.squad_f = trio_team_f_sum(fx.world(), 1);
    return out;
}

// The two match seeds the #235 pins run under (arbitrary but fixed; they
// decode to different squad orders — proven by the exact pins below).
inline constexpr std::uint32_t kTrioSeedA = 1001u;
inline constexpr std::uint32_t kTrioSeedB = 4242u;

}  // namespace

// Issue #235, the reporter's scenario: with three humans the matched
// squad seam truncates the bot table to its first min(SIZE, 5) members,
// so every match against bots opened with the same soldier/archer/elf
// trio. The squad order is now a per-match PERMUTATION drawn once from
// the sim RNG and latched in the shared header var, so the trio varies
// with the match seed and repeats exactly under the same seed.
TEST_F(ModesBasketball, bot_trio_varies_with_the_match_seed_and_repeats_with_it)
{
    const SeededTrio a1 = run_seeded_trio(kTrioSeedA);
    const SeededTrio b = run_seeded_trio(kTrioSeedB);
    const SeededTrio a2 = run_seeded_trio(kTrioSeedA);

    ASSERT_EQ(3u, a1.families.size()) << "SIZE = 3 truncation (D34/D39)";
    ASSERT_EQ(3u, b.families.size());
    EXPECT_EQ(a1.families, a2.families)
        << "the same match seed must reproduce the same trio";
    EXPECT_EQ(a1.seed_var, a2.seed_var)
        << "the latched squad-order code is a pure function of the seed";
    EXPECT_NE(a1.families, b.families)
        << "different match seeds must vary the opposing trio (issue #235)";

    // Exact pins, adjudicated once against the sim post-fix (deterministic
    // forever): seed A decodes order code 72, seed B code 114.
    const std::vector<int> pinned_a = {FAMILY_ARCHER, FAMILY_ELF,
                                       FAMILY_SOLDIER};
    const std::vector<int> pinned_b = {FAMILY_MAGE, FAMILY_ELF, FAMILY_THIEF};
    EXPECT_EQ(pinned_a, a1.families);
    EXPECT_EQ(pinned_b, b.families);
    EXPECT_EQ(72, a1.seed_var);
    EXPECT_EQ(114, b.seed_var);

    // The latched code is order + 1, in [1, 120]; 0 means never drawn.
    EXPECT_GE(a1.seed_var, 1);
    EXPECT_LE(a1.seed_var, 120);
    EXPECT_GE(b.seed_var, 1);
    EXPECT_LE(b.seed_var, 120);

    // Each trio is 3 DISTINCT members of the five-family squad set (a
    // permutation prefix, never a sample-with-replacement).
    for (const SeededTrio* run : {&a1, &b})
    {
        std::vector<int> fams = run->families;
        std::sort(fams.begin(), fams.end());
        EXPECT_EQ(fams.end(), std::unique(fams.begin(), fams.end()))
            << "trio families must be distinct";
        for (int fam : fams)
        {
            EXPECT_TRUE(fam == FAMILY_SOLDIER || fam == FAMILY_ARCHER ||
                        fam == FAMILY_ELF || fam == FAMILY_MAGE ||
                        fam == FAMILY_THIEF)
                << "family byte " << fam << " is not a squad member";
        }
    }
}

// The FAIR guarantee survives the permutation: whatever 3-of-5 subset the
// seed picks, spawn_matched_bots re-measures the ACTUAL squad and solves
// its levels against the SAME stored target, so the seeded trio's measured
// f-sum stays inside the matched accuracy band for every seed.
TEST_F(ModesBasketball, seeded_trio_power_stays_within_the_matched_band)
{
    const SeededTrio a = run_seeded_trio(kTrioSeedA);
    const SeededTrio b = run_seeded_trio(kTrioSeedB);

    ASSERT_NE(0, a.announced) << "TEAMS MATCHED announced at init";
    ASSERT_NE(0, b.announced);
    ASSERT_GT(a.target, 0);
    EXPECT_EQ(a.target, b.target)
        << "the target is the human roster's own f-sum — seed-independent";

    for (const SeededTrio* run : {&a, &b})
    {
        const long long target = run->target;
        const long long miss = run->squad_f > target ? run->squad_f - target
                                                     : target - run->squad_f;
        EXPECT_LE(miss * 100, target * 10)
            << "squad f " << run->squad_f << " vs target " << target
            << " misses the matched band";
    }
}

// ===========================================================================
// §11.2 #48 — D33 body denial: the point-blank face-stuff
// ===========================================================================

TEST_F(ModesBasketball, body_denial_point_blank)
{
    BballCourt fx;
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    auto& vars = fx.world().mode.vars;
    vars[kBbBallState] = kStateShot;
    vars[kBbShotValue] = 2;
    vars[kBbShotHoop1] = 2;
    vars[kBbShotTeam1] = 1;  // red released it
    vars[kBbShotLand] = pos_pack(576, 480);
    vars[kBbFlightTicks] = 20;
    vars[kBbLastToucher] = static_cast<std::int32_t>(fx.red->entity_id());
    vars[kBbLastTouch1] = 1;
    vars[kBbBallPx] = 320 * 256;
    vars[kBbBallPy] = 300 * 256;
    vars[kBbBallPz] = 14 * 256;  // inside the deny band [0, grab_z]
    vars[kBbBallVx] = 0;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = 900;       // the release ascent
    fx.ball()->set_team_num(0);
    // Green parked goal-side on the release lane: center (311, 297) is
    // 12 L1 from the ball's ground center — contact at exactly
    // catch_radius also pins the <= compare.
    fx.green->setxy(303, 289);
    fx.tick(1);

    EXPECT_EQ(kStateRebound, fx.var(kBbBallState)) << "T20 body denial";
    EXPECT_EQ(1, count_notifications(fx.events, "DENIED!"));
    EXPECT_EQ(0, count_notifications(fx.events, "BLOCK!"))
        << "a face-stuff announces the deny, never the weapon swat";
    EXPECT_GE(25u, longest_notification(fx.events)) << "I8 budget";
    // Exact impulse pin: ball - defender = (9, 3), L1-normalized at
    // deny_speed 4 -> vx = 1024*9/12 = 768, vy = 1024*3/12 = 256; the
    // horizontal pair survives the same-tick run_air on open floor.
    EXPECT_EQ(768, fx.var(kBbBallVx))
        << "swat_velocity along ball - defender at the deny_speed floor";
    EXPECT_EQ(256, fx.var(kBbBallVy));
    EXPECT_EQ(kFumblePop - kGravity, fx.var(kBbBallVz))
        << "vz is SET to fumble_pop (a scramble, not the block's +512 "
           "sail), then one tick of gravity";
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()),
              fx.var(kBbLastToucher))
        << "the toucher restamps to the defender";
    EXPECT_EQ(2, fx.var(kBbLastTouch1));
    EXPECT_EQ(1, fx.var(kBbLastTouch2)) << "the shooter demotes to touch2";
    EXPECT_EQ(1, static_cast<int>(fx.ball()->team_num())) << "ball tint";
    EXPECT_EQ(0, fx.var(kBbShotValue)) << "flight facts cleared";
    EXPECT_EQ(0, fx.var(kBbShotTeam1));
    EXPECT_EQ(0, fx.var(kBbFlightTicks));
    EXPECT_EQ(0, fx.var(kBbGraceUntil))
        << "no grace write (D24): denier and shooter may both grab";
    EXPECT_EQ(0, fx.var(kBbGraceTeam1));
    EXPECT_EQ(0, fx.var(kBbGraceEntity));

    // No bar was written, so the denier grabs the carom once it settles.
    const int ran = tick_until_state_leaves(fx, kStateRebound, 400);
    ASSERT_LT(ran, 400) << "the carom must settle";
    ASSERT_EQ(kStateFree, fx.var(kBbBallState));
    fx.green->setxy(static_cast<short>(fx.ball_cx() - 8),
                    static_cast<short>(fx.ball_cy() - 8));
    fx.tick(1);
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()), fx.carrier())
        << "the defender scoops what it earned";
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);

    // Dead-center degenerate: the defender's center exactly under the
    // ball leaves swat_velocity no direction, so the carom follows the
    // defender's FACING — consume_throw's dead-center rule verbatim. The
    // defender wears an entity grace bar: it STILL denies (grace bars
    // bar taking the ball, never contesting it, §3.5) but cannot scoop
    // the carom the same tick, which keeps the impulse observable.
    {
        BballCourt dc;
        dc.tick(1);
        ASSERT_TRUE(dc.basketball_active());
        dc.thaw();
        auto& dvars = dc.world().mode.vars;
        dvars[kBbBallState] = kStateShot;
        dvars[kBbShotValue] = 2;
        dvars[kBbShotHoop1] = 2;
        dvars[kBbShotTeam1] = 1;
        dvars[kBbShotLand] = pos_pack(576, 480);
        dvars[kBbFlightTicks] = 20;
        dvars[kBbLastToucher] =
            static_cast<std::int32_t>(dc.red->entity_id());
        dvars[kBbLastTouch1] = 1;
        dvars[kBbBallPx] = 320 * 256;
        dvars[kBbBallPy] = 300 * 256;
        dvars[kBbBallPz] = 14 * 256;
        dvars[kBbBallVx] = 0;
        dvars[kBbBallVy] = 0;
        dvars[kBbBallVz] = 900;
        dvars[kBbGraceUntil] = 1000;
        dvars[kBbGraceTeam1] = 0;
        dvars[kBbGraceEntity] =
            static_cast<std::int32_t>(dc.green->entity_id());
        dc.green->setxy(312, 292);  // center == ball ground center
        dc.green->set_curdir(FACE_RIGHT);
        dc.green->set_enddir(FACE_RIGHT);  // no act-phase turn-in-place
        dc.tick(1);
        EXPECT_EQ(kStateRebound, dc.var(kBbBallState))
            << "a grace-barred body still contests the shot";
        EXPECT_EQ(1, count_notifications(dc.events, "DENIED!"));
        EXPECT_EQ(4 * 256, dc.var(kBbBallVx))
            << "the carom rides the defender's facing (east), full "
               "deny_speed on one axis";
        EXPECT_EQ(0, dc.var(kBbBallVy));
        EXPECT_EQ(kFumblePop - kGravity, dc.var(kBbBallVz));
        EXPECT_EQ(1000, dc.var(kBbGraceUntil))
            << "the deny never moves a grace bar (D24)";
        EXPECT_EQ(static_cast<std::int32_t>(dc.green->entity_id()),
                  dc.var(kBbGraceEntity));
    }

    // Facing-less denier: core-family specials park curdir at the -1
    // sentinel while their command burst drains, so FACING_X[curdir + 1]
    // indexes slot 0 and answers nil. The dead-center carom must then take
    // the (0, 1) fallback and heave FACE_DOWN deterministically —
    // consume_throw's sentinel rule, applied to the deny.
    {
        BballCourt sc;
        sc.tick(1);
        ASSERT_TRUE(sc.basketball_active());
        sc.thaw();
        auto& svars = sc.world().mode.vars;
        svars[kBbBallState] = kStateShot;
        svars[kBbShotValue] = 2;
        svars[kBbShotHoop1] = 2;
        svars[kBbShotTeam1] = 1;
        svars[kBbShotLand] = pos_pack(576, 480);
        svars[kBbFlightTicks] = 20;
        svars[kBbLastToucher] =
            static_cast<std::int32_t>(sc.red->entity_id());
        svars[kBbLastTouch1] = 1;
        svars[kBbBallPx] = 320 * 256;
        svars[kBbBallPy] = 300 * 256;
        svars[kBbBallPz] = 14 * 256;
        svars[kBbBallVx] = 0;
        svars[kBbBallVy] = 0;
        svars[kBbBallVz] = 900;
        svars[kBbGraceUntil] = 1000;
        svars[kBbGraceTeam1] = 0;
        svars[kBbGraceEntity] =
            static_cast<std::int32_t>(sc.green->entity_id());
        sc.green->setxy(312, 292);  // center == ball ground center
        sc.green->set_curdir(static_cast<signed char>(-1));
        sc.green->set_enddir(static_cast<signed char>(-1));
        sc.tick(1);
        EXPECT_EQ(kStateRebound, sc.var(kBbBallState))
            << "a facing-less body still denies";
        EXPECT_EQ(1, count_notifications(sc.events, "DENIED!"));
        EXPECT_EQ(0, sc.var(kBbBallVx))
            << "nil facing falls back to (0, 1): the carom heaves "
               "FACE_DOWN at full deny_speed";
        EXPECT_EQ(4 * 256, sc.var(kBbBallVy));
        EXPECT_EQ(kFumblePop - kGravity, sc.var(kBbBallVz));
        ASSERT_EQ(0u, og::script::hooks::hook_failures().count)
            << "the nil-guard arm must not raise";
    }
}

// ===========================================================================
// §11.2 #49 — the deny band's z boundary: above grab_z the arc is body-proof
// ===========================================================================

TEST_F(ModesBasketball, denial_z_boundary_sails_over)
{
    // Every contact tick above grab_z: the body sanctuary — no deny, the
    // flight completes. Staged to ASCEND for its whole 10-tick life
    // (vz 900 stays positive through tick 9), so z never re-enters the
    // band while the defender stands in radius.
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
        vars[kBbFlightTicks] = 10;
        vars[kBbLastToucher] = static_cast<std::int32_t>(fx.red->entity_id());
        vars[kBbLastTouch1] = 1;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 21 * 256;  // one px over grab_z
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 900;
        fx.green->setxy(303, 289);   // 12 L1: in contact the whole flight
        fx.tick(1);
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "z 21 > grab_z: a body cannot reach the shot";
        EXPECT_EQ(9, fx.var(kBbFlightTicks)) << "the flight continues";
        fx.tick(9);
        EXPECT_EQ(0, count_notifications(fx.events, "DENIED!"))
            << "no contact tick ever entered the deny band";
        EXPECT_NE(kStateShot, fx.var(kBbBallState))
            << "the flight completed (airball resolution)";
    }
    // The boundary arm: contact staged at exactly z == grab_z denies —
    // the window compare is <=.
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
        vars[kBbLastToucher] = static_cast<std::int32_t>(fx.red->entity_id());
        vars[kBbLastTouch1] = 1;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = kGrabZ * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 900;
        fx.green->setxy(303, 289);
        fx.tick(1);
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState))
            << "z == grab_z is inside the deny band (<=)";
        EXPECT_EQ(1, count_notifications(fx.events, "DENIED!"));
    }
}

// ===========================================================================
// §11.2 #50 — the shooter can never deny its own shot (both prongs)
// ===========================================================================

TEST_F(ModesBasketball, shooter_never_self_denies)
{
    // Release-tick geometry: ball ground == shooter center, deep inside
    // radius and band — the team prong (SHOT_TEAM1) refuses the shooter.
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
        vars[kBbFlightTicks] = 3;
        vars[kBbLastToucher] = static_cast<std::int32_t>(fx.red->entity_id());
        vars[kBbLastTouch1] = 1;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 14 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 900;
        fx.red->setxy(312, 292);  // shooter center == ball ground center
        fx.tick(1);
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "no self deny at the release point (team prong)";
        EXPECT_EQ(2, fx.var(kBbFlightTicks));
        fx.tick(2);
        EXPECT_EQ(0, count_notifications(fx.events, "DENIED!"));
        EXPECT_EQ(kStateRebound, fx.var(kBbBallState))
            << "the flight resolved normally (airball)";
    }
    // The charm arm (edge #34): the shooter swapped onto an ENEMY team
    // mid-flight, standing in its own slow shot. The team prong now
    // passes — only the LAST_TOUCHER id prong refuses it.
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
        vars[kBbFlightTicks] = 3;
        vars[kBbLastToucher] = static_cast<std::int32_t>(fx.red->entity_id());
        vars[kBbLastTouch1] = 1;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 14 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 900;
        fx.red->setxy(312, 292);
        fx.red->set_team_num(1);  // charmed onto the enemy team
        fx.tick(1);
        EXPECT_EQ(kStateShot, fx.var(kBbBallState))
            << "the id prong is charm-proof (edge #34)";
        fx.tick(2);
        EXPECT_EQ(0, count_notifications(fx.events, "DENIED!"));
        EXPECT_NE(kStateShot, fx.var(kBbBallState));
    }
}

// ===========================================================================
// §11.2 #51 — teammate bodies never carom; "enemy" is read live (edge #35)
// ===========================================================================

TEST_F(ModesBasketball, teammate_body_never_caroms)
{
    BballCourt fx;
    walker* mate = fx.spawn_living(FAMILY_SOLDIER, 0, 303, 289, ACT_SIT);
    ASSERT_NE(nullptr, mate);  // center (311, 297): 12 L1 on the lane
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.thaw();
    auto& vars = fx.world().mode.vars;
    vars[kBbBallState] = kStateShot;
    vars[kBbShotValue] = 2;
    vars[kBbShotHoop1] = 2;
    vars[kBbShotTeam1] = 1;  // red's shot; the mate shares team 0
    vars[kBbShotLand] = pos_pack(576, 480);
    vars[kBbFlightTicks] = 20;
    vars[kBbLastToucher] = static_cast<std::int32_t>(fx.red->entity_id());
    vars[kBbLastTouch1] = 1;
    vars[kBbBallPx] = 320 * 256;
    vars[kBbBallPy] = 300 * 256;
    vars[kBbBallPz] = 14 * 256;
    vars[kBbBallVx] = 0;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = 900;
    fx.tick(1);
    EXPECT_EQ(kStateShot, fx.var(kBbBallState))
        << "a teammate body on the lane never caroms the shot (enemies "
           "only, D33)";
    EXPECT_EQ(0, count_notifications(fx.events, "DENIED!"));
    EXPECT_EQ(19, fx.var(kBbFlightTicks));

    // The live-read arm (edge #35): the SAME body charmed onto an enemy
    // team before the next contact tick (z 17, still in radius) gains
    // the deny, and the restamp pays the live team.
    mate->set_team_num(1);
    fx.tick(1);
    EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
    EXPECT_EQ(1, count_notifications(fx.events, "DENIED!"));
    EXPECT_EQ(static_cast<std::int32_t>(mate->entity_id()),
              fx.var(kBbLastToucher));
    EXPECT_EQ(2, fx.var(kBbLastTouch1)) << "paid to the live team";
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// §11.2 #52 — D34 precedence: the body beats a same-tick weapon in the
// shared band, and the weapon's follow-up tip of the carom stays silent
// ===========================================================================

TEST_F(ModesBasketball, deny_beats_weapon_swat_same_tick)
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
    vars[kBbLastToucher] = static_cast<std::int32_t>(fx.red->entity_id());
    vars[kBbLastTouch1] = 1;
    vars[kBbBallPx] = 320 * 256;
    vars[kBbBallPy] = 300 * 256;
    vars[kBbBallPz] = 14 * 256;  // inside BOTH windows (deny 20, block 24)
    vars[kBbBallVx] = 0;
    vars[kBbBallVy] = 0;
    vars[kBbBallVz] = 900;
    fx.green->setxy(303, 289);   // body at 12 L1
    walker* swat = fx.spawn_weapon(fx.green, 317, 297, 4.0f, 0.0f);
    ASSERT_NE(nullptr, swat);
    swat->set_damage(5.0f);      // a 10 px/tick weapon impulse, if it led
    const std::uint32_t swat_id = swat->entity_id();
    fx.tick(1);

    EXPECT_EQ(kStateRebound, fx.var(kBbBallState));
    EXPECT_EQ(1, count_notifications(fx.events, "DENIED!"))
        << "the body owns the shared band (D34: body before weapon)";
    EXPECT_EQ(0, count_notifications(fx.events, "BLOCK!"))
        << "the same-tick weapon tip of the fresh REBOUND is silent "
           "(was_shot false)";
    // The ordering, pinned in the velocities: the deny SET vz to
    // fumble_pop, the silent tip then ADDED its +512 pop and overwrote
    // the horizontal with the weapon impulse (10 px/tick along (4,0));
    // gravity closes the tick. Flipped precedence would read
    // 900 + 512 - 96 here and announce BLOCK!.
    EXPECT_EQ(kFumblePop + 512 - kGravity, fx.var(kBbBallVz));
    EXPECT_EQ(10 * 256, fx.var(kBbBallVx));
    EXPECT_EQ(0, fx.var(kBbBallVy));
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()),
              fx.var(kBbLastToucher));
    EXPECT_EQ(2, fx.var(kBbLastTouch1));
    EXPECT_TRUE(fx.weapon_present(swat_id))
        << "neither the deny nor the tip consumes the weapon";
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// §11.2 #53 — the deny writes no CLOCK_* and draws zero RNG; the next gain
// arms a fresh clock (§3.6 rule 3)
// ===========================================================================

TEST_F(ModesBasketball, deny_clock_state_matches_block)
{
    // Twin runs (sequential — the harness holds one world at a time),
    // identical except the defender: in `a` the green body stands at
    // contact (12 L1, denies); in `b` one px out (13 L1, the shot sails
    // on). Fixture construction is deterministic, so equal pre-tick AND
    // post-tick LCG states prove the deny itself draws nothing.
    auto stage = [](BballCourt& fx, int green_cx) {
        fx.tick(1);
        fx.thaw();
        auto& vars = fx.world().mode.vars;
        vars[kBbBallState] = kStateShot;
        vars[kBbShotValue] = 2;
        vars[kBbShotHoop1] = 2;
        vars[kBbShotTeam1] = 1;
        vars[kBbShotLand] = pos_pack(576, 480);
        vars[kBbFlightTicks] = 20;
        vars[kBbLastToucher] =
            static_cast<std::int32_t>(fx.red->entity_id());
        vars[kBbLastTouch1] = 1;
        vars[kBbBallPx] = 320 * 256;
        vars[kBbBallPy] = 300 * 256;
        vars[kBbBallPz] = 14 * 256;
        vars[kBbBallVx] = 0;
        vars[kBbBallVy] = 0;
        vars[kBbBallVz] = 900;
        fx.green->setxy(green_cx - 8, 289);
    };
    std::uint32_t rng_before_a = 0;
    std::uint32_t rng_after_a = 0;
    {
        BballCourt a;
        stage(a, 311);
        ASSERT_TRUE(a.basketball_active());
        rng_before_a = a.world().rng_.state_;
        a.tick(1);
        rng_after_a = a.world().rng_.state_;
        ASSERT_EQ(kStateRebound, a.var(kBbBallState));
        ASSERT_EQ(1, count_notifications(a.events, "DENIED!"));
        // The release already cleared CLOCK_* and the deny writes
        // neither slot — byte-identical to the weapon-block path.
        EXPECT_EQ(0, a.var(kBbClockUntil));
        EXPECT_EQ(0, a.var(kBbClockTeam1));

        // The next possession gain arms fresh: walk the denier onto the
        // carom until it grabs.
        int grabbed = -1;
        for (int i = 0; i < 80 && grabbed < 0; ++i)
        {
            a.green->setxy(static_cast<short>(a.ball_cx() - 8),
                           static_cast<short>(a.ball_cy() - 8));
            a.tick(1);
            if (a.carrier() ==
                static_cast<std::int32_t>(a.green->entity_id()))
                grabbed = i;
        }
        ASSERT_GE(grabbed, 0) << "the denier must eventually grab the carom";
        EXPECT_EQ(2, a.var(kBbClockTeam1))
            << "fresh clock for the gaining team (§3.6 rule 3)";
        const std::int32_t remaining =
            a.var(kBbClockUntil) -
            static_cast<std::int32_t>(a.world().tick_count_);
        EXPECT_GE(remaining, kShotClock - 2);
        EXPECT_LE(remaining, kShotClock);
    }
    {
        BballCourt b;
        stage(b, 310);
        ASSERT_TRUE(b.basketball_active());
        ASSERT_EQ(rng_before_a, b.world().rng_.state_)
            << "twins must enter the tick in the same RNG state";
        b.tick(1);
        ASSERT_EQ(kStateShot, b.var(kBbBallState))
            << "13 L1 is out of contact: the shot sails on";
        ASSERT_EQ(0, count_notifications(b.events, "DENIED!"));
        EXPECT_EQ(rng_after_a, b.world().rng_.state_)
            << "the deny draws ZERO RNG (D33) — the stream is untouched";
    }
}

// ===========================================================================
// §11.2 #54 — the director's open-shot rung never feeds the deny (D33
// arithmetic: press_count == 0 at release keeps every body out of range)
// ===========================================================================

TEST_F(ModesBasketball, director_open_shot_undenied_regression)
{
    // The rung-1 open release (the §11.2 #31 staging): press_count == 0
    // at the release tick — the nearest enemy stands hundreds of px off
    // the lane, so no body can reach the 2-3-tick low window and bot
    // offense never feeds the deny.
    BballCourt fx;
    walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 492, 472, ACT_SIT);
    ASSERT_NE(nullptr, bot);
    fx.tick(1);
    ASSERT_TRUE(fx.basketball_active());
    fx.give_ball(bot, 500, 480);  // 76 px Euclid <= sweet 88, nobody near
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(kStateShot, fx.var(kBbBallState))
        << "the open bot releases the jumper";
    const int ran = tick_until_state_leaves(fx, kStateShot, 40);
    ASSERT_LT(ran, 40) << "the flight must resolve";
    EXPECT_EQ(0, count_notifications(fx.events, "DENIED!"))
        << "an open release never meets a body in the low window";
    EXPECT_EQ(0, count_notifications(fx.events, "BLOCK!"));
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
}
