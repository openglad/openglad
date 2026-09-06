-- Basketball rules + AI director — possession ball game over the soccer machinery with a fake vertical axis (x256 fixed-point z + gravity in mode vars), consumed-throw shots/passes, rim/backboard rebounds, dunks, shot clock, goaltending and grace bars (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local anchors = og.use("mode_anchors")
local caps = og.use("mode_caps")
local items = og.use("mode_items")
local match = og.use("mode_match")
local strip = og.use("mode_strip")
local surface = og.use("mode_ball_surface")

-- Mode-private slot map (8+; header 0-7 is mode_core.SLOT). Ball ground
-- position, velocity AND height z are x256 fixed point ("fp"): 256 fp =
-- 1 px, tick rate 12/s (design §2.1-2.2, decisions D6/D19/D21/D23/D24).
-- The private band is now FULL: #225 spent slot 63 (the R4 last spare,
-- after the slot review R4 asked for) on the item clock and had to reach
-- into the shared header band for its second slot. A further claim means
-- repacking DUNK_OK/GRACE_TEAM1/SHOT_VALUE into one bitfield var.
local S = {
  SCORE_LIMIT = 8, -- points to win (post-clamp), 1..255
  RESPAWN_TICKS = 9, -- resolved respawn delay
  TEAM_COUNT = 10, -- active team count 2..4
  TEAM_MASK = 11, -- core.TEAM_BIT mask of active teams
  TIME_LIMIT = 12, -- level ticks; timeout verdict (buzzer rule §3.7)
  ANCHOR_CURSOR = 13, -- match.place_at_anchor rotation cursor
  BALL_ENTITY = 14, -- entity id of modes:bball (og.add_ob)
  SHADOW_ENTITY = 15, -- entity id of modes:bshadow (og.add_fx_ob)
  BALL_PX = 16, -- ball GROUND center x, fp
  BALL_PY = 17, -- ball GROUND center y, fp
  BALL_PZ = 18, -- height above ground, fp, >= 0
  BALL_VX = 19, -- px/tick fp
  BALL_VY = 20, -- px/tick fp
  BALL_VZ = 21, -- px/tick fp, signed
  BALL_STATE = 22, -- 0 FREE, 1 CARRIED, 2 SHOT, 3 PASS, 4 REBOUND
  CARRIER = 23, -- entity id, 0 = none (nonzero iff BALL_STATE == 1)
  CLOCK_UNTIL = 24, -- absolute tick the shot clock expires; 0 = disarmed
  CLOCK_TEAM1 = 25, -- score team + 1 owning the armed clock; 0 = none
  LAST_TOUCH1 = 26, -- last-touching score team + 1; 0 = untouched
  LAST_TOUCH2 = 27, -- last DISTINCT other score team + 1 (own-basket credit)
  LAST_TOUCHER = 28, -- entity id of the last toucher, 0 = none
  GRACE_UNTIL = 29, -- absolute tick a pickup bar lifts; 0 = no bar
  GRACE_TEAM1 = 30, -- 0 = bar is entity-scoped on GRACE_ENTITY (self grace,
  -- D24 — never a live LAST_TOUCHER deref); else team + 1
  -- barred from pickup (turnover grace)
  SHOT_VALUE = 31, -- 0 none / 2 / 3, classified at RELEASE (§3.1)
  SHOT_HOOP1 = 32, -- defending team + 1 of the target hoop; 0 = none
  SHOT_TEAM1 = 33, -- thrower's score team + 1 at every release; passes keep
  -- it as write-through bookkeeping only (the §9 #22 charm
  -- interception is the CLOCK_TEAM1 compare in
  -- gain_possession); its one real read is the goaltend
  -- side test of §3.5
  SHOT_LAND = 34, -- pos_pack pixel landing target (scatter already applied)
  FLIGHT_TICKS = 35, -- SHOT/PASS ticks remaining; resolution fires at 0
  PASS_TARGET = 36, -- intended receiver entity id; 0 = flat throw
  FUMBLE_TICK = 37, -- absolute tick a qualifying hit landed on the carrier
  -- (written by the on_damage hook); 0 = none
  JUMP_UNTIL = 38, -- absolute tick the jump-ball freeze ends
  JUMP_POS = 39, -- packed pixel center-court spot (row.jump_ball)
  STALL_SINCE = 40, -- reset-watchdog start tick (dead-ball stall, and the
  -- D25 wipe watchdog in any state); 0 = running play
  BALL_SPIN = 41, -- spin phase 0..spin_cycle-1 (soccer run_spin shape)
  POINTS = 42, -- 42..45, +team: the mode metric (may go negative, forfeit)
  HOOP_POS = 46, -- 46..49, +team: packed pixel HOOP CENTER team t DEFENDS
  -- (banked for active teams only; 0 = no hoop)
  ARC_RADIUS = 50, -- three-point release distance, px (row.arc_radius)
  SPAWN_CAP = 51, -- 51..58, +team byte 0-7 (caps.bank_caps; -1 = uncapped)
  GRACE_ENTITY = 59, -- entity id the entity-scoped grace bars (D24);
  -- written at every grace ARM, cleared with GRACE_UNTIL
  THROW_WATERMARK = 60, -- highest weaplist entity id at possession gain
  -- (D19); only carrier weapons above it are consumable
  POSSESS_SINCE = 61, -- tick the current possession began (D21 fumble grace)
  DUNK_OK = 62, -- 1 = presence dunk armed; 0 after a catch inside an
  -- enemy box until exit + re-entry (D23)
  ITEM_LAST = 63, -- world tick of the last item spawn, seeded at init
  -- (#225 — the former R4 spare)
  ITEM_CURSOR = 7, -- lib/mode_items pad rotation cursor. In the SHARED
  -- header band (slots 0-7 are mode-neutral by
  -- convention; mode_match's MATCHED owns 2-5 and
  -- mode_anchors' squad seed owns 6) because the
  -- private band 8..62 was already spent — the same
  -- squeeze, and the same escape, mode_match documents
}

-- PHASE stays 1 for the whole match; BALL_STATE is the real machine.
local STATE_FREE = 0
local STATE_CARRIED = 1
local STATE_SHOT = 2
local STATE_PASS = 3
local STATE_REBOUND = 4

-- Authoritative tuning (design §2.3; all integers). Every distance compare
-- names its metric (I7): contact radii and director geometry are L1; arc
-- classification, shot range, shot_sweet and the scatter distance are
-- Euclidean (d² compares; the scatter takes isqrt so accuracy never
-- depends on approach angle, D20); the dunk zone is a Chebyshev box;
-- flight-time distances stay L1 (pacing only, D4).
local T = {
  -- Vertical physics (D15). Integration per tick: z += vz; vz -= gravity.
  gravity = 96, -- fp px/tick^2 = 0.375

  -- Height ladder (px). Below block_ceiling is contestable; (block_ceiling,
  -- rim_z] ascending is the untouchable arc; (rim_z, goaltend_ceiling] near
  -- the target hoop, descending, is the goaltend window.
  carry_z = 12, -- carried/release height; pass catch height
  head_z = 20, -- above this walkers cannot touch the ball
  wall_top = 24, -- above this the ball clears wall tiles (D12)
  block_ceiling = 24, -- weapon swats connect at or below
  rim_z = 32, -- rim plane; SHOT solves to land at this height
  goaltend_ceiling = 48,
  goaltend_radius = 24, -- L1 from the target hoop for the goaltend arm

  -- Contact radii (L1; soccer kick/hit_radius = 12 parity; pickup 12 ==
  -- GOTO arrival distance 12, the anti-milling identity).
  pickup_radius = 12, -- FREE/REBOUND ground grab
  catch_radius = 12, -- interception contact (any non-receiver)
  receiver_catch_radius = 20, -- INTENDED receiver only (D22: moving
  -- receivers must be able to complete; steals stay at 12)
  swat_radius = 12, -- weapon-to-ball swat contact
  grab_z = 20, -- = head_z: max z_px for pickup/interception
  catch_z = 24, -- intended receiver may catch slightly higher

  -- Rim (D14). At SHOT resolution, d = L1(ground, target hoop):
  -- d <= rim_r basket; rim_r < d <= rim_r + rim_lip clang; beyond, airball.
  rim_r = 12,
  rim_lip = 6,

  -- Throw classification (§3.1). Aim cone |cross| * aim_den <= dot * aim_num,
  -- tan(half-angle) = 1/2 (~26.6 degrees either side).
  aim_num = 1,
  aim_den = 2,
  -- Shot range is PER COURT: euclid2(carrier, hoop) <= (ARC_RADIUS + 64)² (D4).
  shot_range_pad = 64,
  chest_range = 96, -- L1; pass at or below = chest, above = lob
  pass_range_max = 192, -- L1; beyond, no pass target is recognized

  -- Flight time Tf (ticks) from D = L1 px release->target:
  --   SHOT  Tf = clamp(div(D, arc_speed), shot_tf_min, shot_tf_max)
  --   CHEST Tf = clamp(div(D, chest_speed), chest_tf_min, chest_tf_max)
  --   LOB   Tf = clamp(div(D, lob_speed), lob_tf_min, lob_tf_max)
  --   FLAT  Tf = flat_tf fixed, range flat_speed * flat_tf = 96 px
  arc_speed = 6,
  shot_tf_min = 10,
  shot_tf_max = 30,
  chest_speed = 8,
  chest_tf_min = 4,
  chest_tf_max = 12,
  lob_speed = 4,
  lob_tf_min = 16,
  lob_tf_max = 36,
  flat_speed = 8,
  flat_tf = 12,

  -- Shot scatter (D20), drawn ONCE at release, folded into SHOT_LAND.
  -- D = isqrt(euclid2(release, target)) (Euclidean, I7):
  --   base  = min(scatter_cap, div(max(0, D - scatter_free), scatter_div))
  --   press = pressure_scatter * (enemy Livings within press_radius L1 of
  --           the shooter at release)
  --   E     = min(scatter_cap_total, base + press)
  --   off = og.rand(2 * E + 1) - E per axis (bound >= 1 always; lint rule 7
  --   needs no rand0 guard).
  -- Open curve: 100% at D <= 57 (E <= 6 keeps |ox|+|oy| <= rim_r), ~77% at
  -- 72, ~50% at 88, ~37% at 100, 28.7% at/past the arc (cap) — EV(3) ~0.86
  -- vs EV(2)@100 ~0.74; pressure degrades everything, the drive does not.
  scatter_free = 16,
  scatter_div = 6,
  scatter_cap = 16,
  pressure_scatter = 6,
  scatter_cap_total = 24,

  -- Rim clang: pop + push away from the hoop + per-axis random deflection
  -- v_axis = dir8_component * rim_speed + (og.rand(2*rim_scatter+1) - rim_scatter).
  rim_pop = 512, -- 2 px/tick up
  rim_speed = 768, -- 3 px/tick away
  rim_scatter = 512,

  -- Ground bounce (REBOUND at z <= 0 with vz < 0); settle to FREE when the
  -- bounced |vz| < settle_vz.
  floor_rest_z = 128, -- keep half the vertical speed
  floor_rest_xy = 192, -- keep three quarters horizontal
  water_rest_z = 32, -- wet landing keeps one eighth vertical speed
  settle_vz = 256,

  -- Grounded roll (FREE): shared surface damping and soccer's substep bound.
  substep_px = 8,

  -- Spin frames: soccer constants for the roll; airborne states advance a
  -- constant air_spin per tick signed by the dominant axis (backspin look).
  spin_step = 256,
  spin_cycle = 2048,
  spin_divisor = 8,
  air_spin = 192,

  -- Loose-ball pops. fumble_pop apexes ~8 px over carry_z (top of the
  -- scramble stays under head_z: grabbable); horizontal scatter per axis in
  -- whole px/tick: v = (og.rand(2 * fumble_scatter + 1) - fumble_scatter) * 256.
  fumble_pop = 640,
  fumble_scatter = 3,
  toss_pop = 768, -- jump-ball toss (apex ~12 px)
  deny_speed = 4, -- body-denial carom speed (D33) = the run_swat impulse
  -- clamp floor (a body is the weakest legal swat); the deny vz is SET to
  -- fumble_pop — a scramble, not a sail

  -- Clocks (D7, D25). 420 = 35 s, sized so the worst defensive-stop
  -- advance (828's ~430 px backcourt at the 1 px/tick slow-family floor)
  -- clears with one completed upcourt pass in the chain. Countdown suffix
  -- at <= clock_hud remaining; one-shot announce at exactly clock_warn
  -- remaining. The deadline clears ONLY on SHOT release / turnover /
  -- center reset and persists across every same-team regain (§3.6).
  shot_clock = 420,
  clock_hud = 120,
  clock_warn = 36,
  turnover_grace = 120, -- team barred after a shot-clock turnover (D25)
  self_grace = 12, -- thrower barred from re-grabbing its own release;
  -- the T5 fumble bar on the ex-carrier reuses it (D24)
  possession_grace = 12, -- ticks after gaining possession before damage
  -- can arm FUMBLE (D21)
  fumble_min_damage = 2, -- on_damage amount below this never fumbles (D21;
  -- the engine floor makes every landed hit >= 1)
  jump_freeze = 36, -- soccer kickoff_freeze parity
  dead_ball_ticks = 600, -- the same 600 drives the wipe watchdog (D25)
  dead_ball_radius = 160, -- soccer parity

  -- Scoring (D3, D9, D10).
  dunk_half = 24, -- Chebyshev half-extent of the dunk box (3x3 carpet)
  score_limit = 21,
  point_score = 100, -- og.award_score delta per point (200/2pt, 300/3pt)
  time_limit_ticks = 7200,
  respawn_ticks = 60,
  squad_cap = 5, -- the court's hard shape: five on five (lineup §3.2)

  -- Respawning pickups (lib/mode_items): fallback interval when the
  -- manifest row carries none. mode_items refills ONE pad per interval
  -- whatever the pad count, so the interval tracks mouths fed: a court
  -- fields 10-20 livings (five-a-side over 2-4 teams), TDM's band at 300,
  -- but ball contact chips everyone all match long, so the trickle runs
  -- faster (#225).
  item_interval = 240,

  -- AI director (§4; ranges re-derived from the D20 curve per D23).
  ai_cadence = 15,
  dunk_drive_range = 80, -- L1: pressed bot carrier inside this drives
  shot_sweet = 88, -- Euclidean px: open bot shot range (P ~50%+)
  press_radius = 24, -- L1: an enemy this close = contested (also the
  -- pressure-scatter and open-cutter radius)
  clock_panic = 36, -- clock remaining at/below forces the shot
  pass_gain = 32, -- cutter must be this much closer to earn a pass
  cutter_perp = 32,
  cutter_standoff = 40,
  rim_protect_standoff = 32, -- retuned for the 24 px dunk box (D3)
  rebound_boxout = 12,
  drive_offset = 8,
  drive_cross = 24,
  drive_cross_hold = 32,
  drive_reach = 10, -- soccer chaser_drives constants, read via ai.* (D17)
  water_recover_speed = 256, -- <= 1 px/tick L1: shoot a bogged ball loose
  oob_ring_max = 8, -- landing-legality ring scan bound (tiles)
  shadow_band = 12, -- shadow frame = clamp(div(z_px, 12), 0, 3)

  -- Hoop furniture animation (D29/D31): the hoop's cycle field counts
  -- ticks left in a frame program, hoop_frame_ticks per sprite frame —
  -- swish = frames 1-3 over 12 ticks (1.0 s), clang = frames 4-5 over
  -- 8 ticks (0.67 s at 12 tps).
  hoop_frame_ticks = 4,
  hoop_swish_ticks = 12,
  hoop_clang_ticks = 8,
}

-- Solver sanity (§2.6 derivations): a Tf=20 shot rises
-- vz0 = div(20*256, 20) + div(96*19, 2) = 1168 fp and apexes ~40 px —
-- above wall_top/block_ceiling, inside goaltend_ceiling; a Tf=24 lob
-- apexes ~37 px (over head_z for the middle of its flight); a Tf=8 chest
-- pass apexes ~14 px (under head_z the whole way — interceptable).

-- The shared scalar helpers, bound to locals at load time: basketball's
-- geometry calls them on every walker, every tick.
local iabs = core.iabs
local walker_center = core.walker_center

-- Lateral fan (px) for the HELP/chaser seams, by support index mod 3 —
-- the FIRST seam holder takes no stagger and holds the midpoint itself
-- (§4.3's {0, +16, -16}; soccer spends the 0 slot on its striker, a role
-- basketball's seam does not have).
local STAGGER = { 0, 16, -16 }

-- The hoop family byte for the D31 fx-list scans, bound once per VM. In
-- the declare pass og.family_id answers an inert truthy placeholder
-- instead — harmless, since no hook (so no compare) ever runs there; in a
-- world VM a missing family fails this assert loudly at chunk load.
local HOOP_FAMILY = assert(og.family_id("effect", "modes:hoop"))

local function fget(base, i)
  return og.mode_get(base + i)
end

local function fset(base, i, v)
  og.mode_set(base + i, v)
end

local function bball_active()
  return og.mode_get(core.SLOT.MODE_ID) == core.MODE.BASKETBALL
end

-- The ball's GROUND center in whole pixels (the fp truth divided down).
local function ball_ground()
  local gx = og.div(og.mode_get(S.BALL_PX), 256)
  local gy = og.div(og.mode_get(S.BALL_PY), 256)
  return gx, gy
end

-- L1 distance between two pixel points — the contact/director metric (I7).
local function dist_l1(ax, ay, bx, by)
  return iabs(ax - bx) + iabs(ay - by)
end

-- Squared Euclidean distance — the arc/range/sweet metric (I7). Courts top
-- out near 752 px so d² stays far inside integer range.
local function euclid2(ax, ay, bx, by)
  local dx = bx - ax
  local dy = by - ay
  return dx * dx + dy * dy
end

-- Integer floor square root (Newton's method over og.div — deterministic,
-- no floats). The D20 scatter distance is isqrt(euclid2) so shot accuracy
-- is rotation-invariant; converges in a handful of steps at court scale.
local function isqrt(n)
  if n <= 0 then
    return 0
  end
  local x = n
  local y = og.div(n + 1, 2)
  while y < x do
    x = y
    y = og.div(og.div(n, x) + x, 2)
  end
  return x
end

-- Team t's banked hoop center, or nil when t defends none (inactive teams
-- on a 4-hoop court bank 0 — they neither score nor count, §6.3).
local function hoop_center(team)
  local pos = fget(S.HOOP_POS, team)
  if pos == 0 then
    return nil
  end
  return core.pos_x(pos), core.pos_y(pos)
end

-- The dunk zone is a Chebyshev box (D3) matching the painted 3x3 carpet.
local function in_dunk_box(cx, cy, hx, hy)
  if iabs(cx - hx) > T.dunk_half then
    return false
  end
  return iabs(cy - hy) <= T.dunk_half
end

-- The ACTIVE enemy team whose dunk box contains (cx, cy), or -1. Dunking
-- one's own hoop is impossible by the t ~= team predicate (§3.4).
local function enemy_box_at(team, cx, cy)
  local mask = og.mode_get(S.TEAM_MASK)
  for t = 0, C.SCORE_TEAM_COUNT - 1 do
    local wanted = t ~= team
    if wanted then
      wanted = core.mask_has(mask, t)
    end
    if wanted then
      local hx, hy = hoop_center(t)
      if hx ~= nil then
        if in_dunk_box(cx, cy, hx, hy) then
          return t
        end
      end
    end
  end
  return -1
end

-- Enemy Livings within press_radius L1 of a point: the D20 pressure term,
-- the "contested" test of the handler ladder and the open-cutter predicate
-- (D23d) are all this one count. Only SCORE teams press — a neutral or
-- wildlife living is not an "enemy" (D20's definition).
local function press_count(livings, team, x, y)
  local count = 0
  for k = 1, #livings do
    local w = livings[k]
    local wt = w:team_num()
    if wt ~= team then
      if wt < C.SCORE_TEAM_COUNT then
        local wx, wy = walker_center(w)
        if dist_l1(x, y, wx, wy) <= T.press_radius then
          count = count + 1
        end
      end
    end
  end
  return count
end

-- Owner-chain root of a weapon, when that root is a Living — swat credit
-- and the goaltend side test read it (§3.5). Bounded walk; owner chains
-- are shallow and acyclic, the bound is a tripwire.
local function owner_root(w)
  local node = w
  for _ = 1, 8 do
    local parent = node:owner()
    if parent == nil then
      break
    end
    node = parent
  end
  if node:order() == C.ORDER_LIVING then
    return node
  end
  return nil
end

-- Touch bookkeeping (soccer's stamp_toucher, §3.3): the ball wears the
-- toucher's team color; only score teams can be credited. A touch by a
-- DIFFERENT score team demotes the outgoing toucher into LAST_TOUCH2 —
-- the side an own basket credits on 3-4 team courts. An uncredited swat
-- (a weapon with no living owner) clears LAST_TOUCH1 only. Restamps never
-- move a grace bar: the entity bar is pinned to GRACE_ENTITY (D24).
local function stamp_toucher(ball, toucher_id, team)
  og.mode_set(S.LAST_TOUCHER, toucher_id)
  if team >= C.SCORE_TEAM_COUNT then
    og.mode_set(S.LAST_TOUCH1, 0)
    return
  end
  local outgoing = og.mode_get(S.LAST_TOUCH1)
  if outgoing > 0 then
    if outgoing - 1 ~= team then
      og.mode_set(S.LAST_TOUCH2, outgoing)
    end
  end
  og.mode_set(S.LAST_TOUCH1, team + 1)
  ball:set_team_num(team)
end

-- Is this walker barred from taking the ball right now? GRACE_TEAM1 == 0
-- means the bar is entity-scoped on GRACE_ENTITY (self grace after a
-- release or fumble, D24 — never a LAST_TOUCHER deref, so a blocker's
-- restamp cannot migrate the bar); nonzero bars that whole team (the
-- 120-tick turnover lockout, D25).
local function grace_barred(w)
  if og.world_tick() >= og.mode_get(S.GRACE_UNTIL) then
    return false
  end
  local team1 = og.mode_get(S.GRACE_TEAM1)
  if team1 == 0 then
    return og.entity_id(w) == og.mode_get(S.GRACE_ENTITY)
  end
  return w:team_num() == team1 - 1
end

-- Arm a grace bar. The trio is always written together (D24): the newest
-- event owns the bar — a turnover grace overwrites a pending self grace.
local function arm_grace(entity, team1, ticks)
  og.mode_set(S.GRACE_UNTIL, og.world_tick() + ticks)
  og.mode_set(S.GRACE_TEAM1, team1)
  og.mode_set(S.GRACE_ENTITY, entity)
end

-- The D19 provenance watermark: the highest entity id alive on the weapon
-- list at possession gain. Entity ids are monotonic, so only weapons fired
-- AFTER this moment can ever be consumed as a throw — a stale in-flight
-- arrow or a returning boomerang flies on untouched (§9 #21).
local function weapon_watermark()
  local weapons = og.weaplist()
  local top = 0
  for k = 1, #weapons do
    local id = og.entity_id(weapons[k])
    if id > top then
      top = id
    end
  end
  return top
end

-- Scrub every in-flight fact. Runs at catches, conversions to REBOUND and
-- inside every reset, so no stale SHOT_*/PASS_TARGET can leak into the
-- next play.
local function clear_flight()
  og.mode_set(S.SHOT_VALUE, 0)
  og.mode_set(S.SHOT_HOOP1, 0)
  og.mode_set(S.SHOT_TEAM1, 0)
  og.mode_set(S.SHOT_LAND, 0)
  og.mode_set(S.FLIGHT_TICKS, 0)
  og.mode_set(S.PASS_TARGET, 0)
end

-- Center reset — the jump ball (§3.8). Everything transient clears
-- (attribution, graces, flight, clock, watermark), DUNK_OK re-arms, the
-- ball goes neutral to the tip spot and the freeze begins. Every reset
-- also runs the wiped-team revive backstop: respawn Off cannot strand a
-- team out of a ball game.
local function center_reset(ball)
  og.mode_set(S.CARRIER, 0)
  og.mode_set(S.BALL_STATE, STATE_FREE)
  og.mode_set(S.BALL_VX, 0)
  og.mode_set(S.BALL_VY, 0)
  og.mode_set(S.BALL_VZ, 0)
  og.mode_set(S.BALL_PZ, 0)
  og.mode_set(S.LAST_TOUCH1, 0)
  og.mode_set(S.LAST_TOUCH2, 0)
  og.mode_set(S.LAST_TOUCHER, 0)
  og.mode_set(S.GRACE_UNTIL, 0)
  og.mode_set(S.GRACE_TEAM1, 0)
  og.mode_set(S.GRACE_ENTITY, 0)
  og.mode_set(S.FUMBLE_TICK, 0)
  og.mode_set(S.CLOCK_UNTIL, 0)
  og.mode_set(S.CLOCK_TEAM1, 0)
  og.mode_set(S.THROW_WATERMARK, 0)
  og.mode_set(S.POSSESS_SINCE, 0)
  og.mode_set(S.DUNK_OK, 1)
  og.mode_set(S.STALL_SINCE, 0)
  og.mode_set(S.BALL_SPIN, 0)
  clear_flight()
  ball:set_frame(0)
  ball:set_team_num(C.SCORE_TEAM_COUNT)
  local jpos = og.mode_get(S.JUMP_POS)
  local jx = core.pos_x(jpos)
  local jy = core.pos_y(jpos)
  og.mode_set(S.BALL_PX, jx * 256)
  og.mode_set(S.BALL_PY, jy * 256)
  ball:setxy(jx - og.div(ball:sizex(), 2), jy - og.div(ball:sizey(), 2))
  og.mode_set(S.JUMP_UNTIL, og.world_tick() + T.jump_freeze)
  match.revive_wiped_teams(anchors, og.mode_get(S.TEAM_MASK), og.mode_get(S.RESPAWN_TICKS), S.ANCHOR_CURSOR, T.squad_cap)
end

-- Park the ball as a FREE dead ball at a pixel center: the landing-
-- legality re-spot (motion zeroed, attribution kept, no grace — a neutral
-- scramble, §2.7).
local function respot_at(cx, cy)
  og.mode_set(S.BALL_PX, cx * 256)
  og.mode_set(S.BALL_PY, cy * 256)
  og.mode_set(S.BALL_PZ, 0)
  og.mode_set(S.BALL_VX, 0)
  og.mode_set(S.BALL_VY, 0)
  og.mode_set(S.BALL_VZ, 0)
  og.mode_set(S.BALL_STATE, STATE_FREE)
  clear_flight()
end

-- Landing legality (§2.7): whenever an airborne ball converts to REBOUND
-- or touches ground, its 12x12 rect must sit on passable terrain. If not:
-- deterministic re-spot — L1 tile rings r = 1..oob_ring_max around the
-- ball's tile, each walked clockwise from due north, first tile whose ball
-- rect passes (probe point tile*16 + 2) takes it. No candidate within 8
-- rings (pathological) → full center reset. Grid-only probes: no RNG.
-- Returns true when the ball was moved (callers stop processing).
local function landing_legal(ball)
  local gx, gy = ball_ground()
  local half_x = og.div(ball:sizex(), 2)
  local half_y = og.div(ball:sizey(), 2)
  if og.query_grid_passable(gx - half_x, gy - half_y, ball) then
    return false
  end
  local tile_x = og.div(gx, 16)
  local tile_y = og.div(gy, 16)
  for r = 1, T.oob_ring_max do
    -- The L1 ring of radius r as four diagonal edges of r tiles each,
    -- clockwise from due north: N->E, E->S, S->W, W->N.
    for i = 0, 4 * r - 1 do
      local edge = og.div(i, r)
      local k = og.mod(i, r)
      local dx
      local dy
      if edge == 0 then
        dx = k
        dy = k - r
      elseif edge == 1 then
        dx = r - k
        dy = k
      elseif edge == 2 then
        dx = -k
        dy = r - k
      else
        dx = k - r
        dy = -k
      end
      local tx = tile_x + dx
      local ty = tile_y + dy
      if tx >= 0 then
        if ty >= 0 then
          if og.query_grid_passable(tx * 16 + 2, ty * 16 + 2, ball) then
            respot_at(tx * 16 + 8, ty * 16 + 8)
            return true
          end
        end
      end
    end
  end
  center_reset(ball)
  return true
end

local function points_of(team)
  return fget(S.POINTS, team)
end

-- ---------------------------------------------------------------------------
-- Hoop furniture (D29-D31). One team-tinted modes:hoop fx entity marks each
-- ACTIVE team's rim. Fx entities never act and never collide; the sim only
-- ever writes their frame/cycle and reads their cycle back inside the
-- driver below — the I4 carve-out. Their existence feeds no other branch.
-- ---------------------------------------------------------------------------

-- The slot-free lookup (D31): one fx-list scan for the hoop family member
-- wearing this team's stamp. team_num is written exactly once, at spawn —
-- stamp_toucher touches only the ball and nothing else restamps fx — so it
-- is an exact key; the list is the shadow + up to four hoops + transient
-- engine fx, O(<=10) compares. Answers nil for a team with no hoop.
local function find_hoop(team)
  local fxlist = og.fxlist()
  for k = 1, #fxlist do
    local e = fxlist[k]
    if e:family() == HOOP_FAMILY then
      if e:team_num() == team then
        return e
      end
    end
  end
  return nil
end

-- Arm a frame program on team's hoop: positive ticks = swish, negative =
-- clang. The newest event overwrites a running program; a missing hoop (an
-- inactive team, or the -1 of an unbanked SHOT_HOOP1) is a silent no-op.
-- The same tick's driver draws the first frame.
local function arm_hoop_anim(team, program)
  local hoop = find_hoop(team)
  if hoop ~= nil then
    hoop:set_cycle(program)
  end
end

-- The frame driver, unconditional in the tick — deliberately outside the
-- ball-handle gate, so a vanished ball (§9 #13) cannot freeze the nets.
-- Anim state is the hoop's replicated cycle field, provably exclusive: fx
-- never act and set_frame never touches cycle. Zero idles (idempotent,
-- self-healing), positive counts a swish down through frames 1-3, negative
-- a clang through 4-5; mirrors replay the per-tick set_frame results and
-- never animate on their own (I4).
local function run_hoop_anims()
  local fxlist = og.fxlist()
  for k = 1, #fxlist do
    local e = fxlist[k]
    if e:family() == HOOP_FAMILY then
      local c = e:cycle()
      if c == 0 then
        e:set_frame(0)
      elseif c > 0 then
        e:set_frame(1 + og.div(T.hoop_swish_ticks - c, T.hoop_frame_ticks))
        e:set_cycle(c - 1)
      else
        local n = -c
        e:set_frame(4 + og.div(T.hoop_clang_ticks - n, T.hoop_frame_ticks))
        e:set_cycle(c + 1)
      end
    end
  end
end

-- Every scored basket funnels through here (§2.5): points on the mode
-- metric, the match score (always a POSITIVE delta — og.award_score is
-- unsigned, I3), the announce, the scored-on hoop's net ripple (armed
-- BEFORE the reset so it plays through the jump freeze, D31), and the
-- center reset that restarts play.
local function score_basket(ball, team, value, label, hoop_team)
  fset(S.POINTS, team, points_of(team) + value)
  og.award_score(team, value * T.point_score)
  core.announce(label, C.SOUND_MONEY)
  arm_hoop_anim(hoop_team, T.hoop_swish_ticks)
  center_reset(ball)
end

-- The one other active team in a two-team match; -1 when the mask holds
-- nobody else (the own-basket beneficiary needs it).
local function lone_rival(mask, team)
  for t = 0, C.SCORE_TEAM_COUNT - 1 do
    if t ~= team then
      if core.mask_has(mask, t) then
        return t
      end
    end
  end
  return -1
end

-- Own basket (§3.3): the crossing team defends this hoop. Two active
-- teams credit the single opponent unconditionally; 3-4 teams credit
-- LAST_TOUCH2 — the side that forced the play — when it is valid, active
-- and different. Nobody with a claim → forfeit: the POINTS metric takes
-- the -value, the match score does NOT (og.award_score is unsigned; a
-- negative would wrap to ~4.29e9 and hand this team the timeout tiebreak
-- it just lost points for, I3). Either way the ball returns to center.
local function own_basket(ball, hoop_team, value)
  local mask = og.mode_get(S.TEAM_MASK)
  local credited = -1
  if og.mode_get(S.TEAM_COUNT) == 2 then
    credited = lone_rival(mask, hoop_team)
  else
    local other = og.mode_get(S.LAST_TOUCH2) - 1
    if other >= 0 then
      if other ~= hoop_team then
        if core.mask_has(mask, other) then
          credited = other
        end
      end
    end
  end
  if credited >= 0 then
    fset(S.POINTS, credited, points_of(credited) + value)
    og.award_score(credited, value * T.point_score)
    core.announce("OWN BASKET! " .. og.team_color_name(credited) .. " +" .. value, C.SOUND_MONEY)
  else
    fset(S.POINTS, hoop_team, points_of(hoop_team) - value)
    core.announce("OWN BASKET! " .. og.team_color_name(hoop_team) .. " -" .. value, C.SOUND_YO)
  end
  -- The ball went in regardless of credit: the crossed hoop ripples (D31).
  arm_hoop_anim(hoop_team, T.hoop_swish_ticks)
  center_reset(ball)
end

-- T15/T16: a descending rim-plane crossing inside an ACTIVE hoop's rim_r
-- scores 2 for LAST_TOUCH1 — the tip-in and the bank shot. An untouched
-- ball scores nothing and plays on (no attribution, no beneficiary).
-- Returns true when a basket ended the play.
local function run_crossing(ball)
  local gx, gy = ball_ground()
  local mask = og.mode_get(S.TEAM_MASK)
  for t = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, t) then
      local hx, hy = hoop_center(t)
      if hx ~= nil then
        if dist_l1(gx, gy, hx, hy) <= T.rim_r then
          local touch1 = og.mode_get(S.LAST_TOUCH1)
          if touch1 == 0 then
            return false
          end
          local scorer = touch1 - 1
          if scorer == t then
            own_basket(ball, t, 2)
            return true
          end
          score_basket(ball, scorer, 2, "BASKET! " .. og.team_color_name(scorer) .. " +2", t)
          return true
        end
      end
    end
  end
  return false
end

-- Loose-ball pop shared by fumble (T5), carrier death (T6) and the
-- turnover (T7): drop in place with fumble_pop and per-axis whole-pixel
-- scatter. The shot clock is deliberately NOT touched here — no loose-
-- ball transition clears it (D25); T7's own clearing happens in turnover.
-- Flight facts DO clear here: T5/T6 enter from CARRIED (already clean),
-- but T7 can fire from a mid-catch late regain (gain_possession rule 1)
-- with PASS_TARGET/FLIGHT_TICKS still live — a REBOUND must carry no pass
-- residue (§2.2's slot semantics; run_loose reads PASS_TARGET).
local function drop_ball()
  og.mode_set(S.CARRIER, 0)
  og.mode_set(S.FUMBLE_TICK, 0)
  clear_flight()
  og.mode_set(S.BALL_STATE, STATE_REBOUND)
  og.mode_set(S.BALL_VZ, T.fumble_pop)
  og.mode_set(S.BALL_VX, (og.rand(2 * T.fumble_scatter + 1) - T.fumble_scatter) * 256)
  og.mode_set(S.BALL_VY, (og.rand(2 * T.fumble_scatter + 1) - T.fumble_scatter) * 256)
end

-- T7, the shot-clock turnover — fired in CARRIED at expiry or at a
-- clock-team regain past the deadline (§3.6 rule 1). CLOCK_* clears like
-- every other clock exit (leaving it armed re-fired T7 on every regrab —
-- announce spam and an unwinnable poison loop, D25); the 120-tick TEAM
-- grace is the lockout.
local function turnover(ball, team)
  og.mode_set(S.CLOCK_UNTIL, 0)
  og.mode_set(S.CLOCK_TEAM1, 0)
  arm_grace(0, team + 1, T.turnover_grace)
  drop_ball()
  core.announce("TURNOVER!", C.SOUND_YO)
end

-- Every possession gain — ground pickup (T1/T2) or catch (T13) — runs
-- this, in §3.6's order: (1) the late-regain turnover check BEFORE any
-- re-arm; (2) same-team persistence — an uncaught own pass, a rolled-dead
-- flat throw, a teammate's fumble scoop never refresh the deadline (the
-- anti-launder rule); (3) a different team, or a disarmed clock, arms
-- fresh. Then the D19 watermark, the D21 grace stamp and the D23b DUNK_OK
-- arming: ground gains always arm (the rebound put-back survives); a
-- catch inside an enemy box disarms until exit + re-entry.
local function gain_possession(ball, w, is_ground)
  local now = og.world_tick()
  local team = w:team_num()
  local deadline = og.mode_get(S.CLOCK_UNTIL)
  local held = false
  if deadline ~= 0 then
    held = og.mode_get(S.CLOCK_TEAM1) == team + 1
  end
  if held then
    if now >= deadline then
      turnover(ball, team)
      return true
    end
  end
  local id = og.entity_id(w)
  local cx, cy = walker_center(w)
  og.mode_set(S.CARRIER, id)
  og.mode_set(S.BALL_STATE, STATE_CARRIED)
  og.mode_set(S.FUMBLE_TICK, 0)
  clear_flight()
  og.mode_set(S.BALL_PX, cx * 256)
  og.mode_set(S.BALL_PY, cy * 256)
  og.mode_set(S.BALL_PZ, T.carry_z * 256)
  og.mode_set(S.BALL_VX, 0)
  og.mode_set(S.BALL_VY, 0)
  og.mode_set(S.BALL_VZ, 0)
  stamp_toucher(ball, id, team)
  og.mode_set(S.THROW_WATERMARK, weapon_watermark())
  og.mode_set(S.POSSESS_SINCE, now)
  if not held then
    og.mode_set(S.CLOCK_TEAM1, team + 1)
    og.mode_set(S.CLOCK_UNTIL, now + T.shot_clock)
  end
  local armed = 1
  if not is_ground then
    if enemy_box_at(team, cx, cy) >= 0 then
      armed = 0
    end
  end
  og.mode_set(S.DUNK_OK, armed)
  return true
end

-- The ballistic release solver (§2.6), shared by every throw class. With
-- z += vz; vz -= gravity per tick, after exactly Tf ticks z lands on
-- hz ± Tf/2 fp (truncation < 0.06 px, absorbed by the rim bands).
local function solve_flight(tx, ty, hz, tf)
  local px = og.mode_get(S.BALL_PX)
  local py = og.mode_get(S.BALL_PY)
  og.mode_set(S.BALL_VX, og.div(tx * 256 - px, tf))
  og.mode_set(S.BALL_VY, og.div(ty * 256 - py, tf))
  og.mode_set(S.BALL_VZ, og.div((hz - T.carry_z) * 256, tf) + og.div(T.gravity * (tf - 1), 2))
  og.mode_set(S.FLIGHT_TICKS, tf)
end

-- Throw classification + release (§3.1, D23d) — ONE body, TWO entrances:
-- the consumed-weapon path and the director's script-invoked release for
-- bot carriers. Builds the JOINT candidate set — hoops of other active
-- teams inside shot range (Euclid d², D4) AND live teammates inside
-- pass_range_max L1 — and picks the single best-aligned candidate under
-- the aim cone by cross-multiplied compare (no division). Hoops are
-- considered first in team order, teammates in oblist order, and only a
-- STRICTLY better alignment replaces the incumbent — exactly the tie
-- ladder: hoop over teammate, lower team index, earlier oblist slot.
-- Returns the landing point (the director leads the receiver onto it, D22).
local function release_throw(ball, livings, carrier, ax, ay)
  local ccx, ccy = walker_center(carrier)
  -- Re-pin the fp ball origin to the carrier's CURRENT center: run_carry
  -- pinned it at the END of the previous mode tick, one act-phase step
  -- behind where this release classifies, scatters and draws from —
  -- classification and the solved flight must share one origin (§2.6).
  og.mode_set(S.BALL_PX, ccx * 256)
  og.mode_set(S.BALL_PY, ccy * 256)
  local team = carrier:team_num()
  local arc = og.mode_get(S.ARC_RADIUS)
  local range = arc + T.shot_range_pad
  local mask = og.mode_get(S.TEAM_MASK)
  -- Candidate rows: { kind (0 hoop / 1 teammate), hoop team, walker or
  -- false, target x, target y }.
  local cands = {}
  for t = 0, C.SCORE_TEAM_COUNT - 1 do
    local wanted = t ~= team
    if wanted then
      wanted = core.mask_has(mask, t)
    end
    if wanted then
      local hx, hy = hoop_center(t)
      if hx ~= nil then
        if euclid2(ccx, ccy, hx, hy) <= range * range then
          cands[#cands + 1] = { 0, t, false, hx, hy }
        end
      end
    end
  end
  for k = 1, #livings do
    local mate = livings[k]
    local ok = mate:team_num() == team
    if ok then
      ok = og.entity_id(mate) ~= og.entity_id(carrier)
    end
    if ok then
      local mx, my = walker_center(mate)
      if dist_l1(ccx, ccy, mx, my) <= T.pass_range_max then
        cands[#cands + 1] = { 1, -1, mate, mx, my }
      end
    end
  end
  local best = nil
  local best_cross = 0
  local best_dot = 0
  for k = 1, #cands do
    local c = cands[k]
    local dot = ax * (c[4] - ccx) + ay * (c[5] - ccy)
    if dot > 0 then
      local across = iabs(ax * (c[5] - ccy) - ay * (c[4] - ccx))
      if across * T.aim_den <= dot * T.aim_num then
        local better = best == nil
        if not better then
          better = across * best_dot < best_cross * dot
        end
        if better then
          best = c
          best_cross = across
          best_dot = dot
        end
      end
    end
  end
  og.mode_set(S.SHOT_TEAM1, team + 1)
  local tx
  local ty
  if best == nil then
    -- FLAT THROW: an uncatchable-by-design pass along the 8-dir snap of
    -- the aim, ground-height target, fixed Tf — anyone may take it low.
    -- Silent at release (D26).
    local sx, sy = ai.dir8(ax, ay)
    tx = og.clamp(ccx + sx * T.flat_speed * T.flat_tf, 0, 4095)
    ty = og.clamp(ccy + sy * T.flat_speed * T.flat_tf, 0, 4095)
    solve_flight(tx, ty, 0, T.flat_tf)
    og.mode_set(S.PASS_TARGET, 0)
    og.mode_set(S.BALL_STATE, STATE_PASS)
    arm_grace(og.entity_id(carrier), 0, T.self_grace)
  elseif best[1] == 0 then
    -- ARC SHOT: value fixed at release — 3 beyond the arc's d² (D4) —
    -- scatter drawn ONCE (D20: Euclidean distance + pressure, capped),
    -- folded into SHOT_LAND, solved to the rim plane. A genuine attempt
    -- satisfies the shot clock (D25: this is one of its three exits).
    local hx = best[4]
    local hy = best[5]
    local d2 = euclid2(ccx, ccy, hx, hy)
    local value = 2
    if d2 > arc * arc then
      value = 3
    end
    local spread = og.min(T.scatter_cap, og.div(og.max(0, isqrt(d2) - T.scatter_free), T.scatter_div))
    spread = spread + T.pressure_scatter * press_count(livings, team, ccx, ccy)
    spread = og.min(T.scatter_cap_total, spread)
    tx = og.clamp(hx + og.rand(2 * spread + 1) - spread, 0, 4095)
    ty = og.clamp(hy + og.rand(2 * spread + 1) - spread, 0, 4095)
    og.mode_set(S.SHOT_VALUE, value)
    og.mode_set(S.SHOT_HOOP1, best[2] + 1)
    og.mode_set(S.SHOT_LAND, core.pos_pack(tx, ty))
    solve_flight(tx, ty, T.rim_z, og.clamp(og.div(dist_l1(ccx, ccy, tx, ty), T.arc_speed), T.shot_tf_min, T.shot_tf_max))
    og.mode_set(S.BALL_STATE, STATE_SHOT)
    og.mode_set(S.CLOCK_UNTIL, 0)
    og.mode_set(S.CLOCK_TEAM1, 0)
    -- Release readability (D26): the bolt says SHOT, the announce says
    -- what it is worth; the rim speaks for the rest.
    og.emit_positional_sound(carrier, C.SOUND_BOLT)
    if value == 3 then
      og.emit_notification("THREE UP!")
    end
  else
    -- PASS: chest at or under chest_range L1 (fast, flat, interceptable),
    -- lob beyond (slow, apex over head_z — safe in the middle, contested
    -- at the ends; positional SOUND_YO, D26). The clock keeps running
    -- (§3.6) and the thrower wears the entity self grace (D24).
    local rec = best[3]
    tx = best[4]
    ty = best[5]
    local d = dist_l1(ccx, ccy, tx, ty)
    if d <= T.chest_range then
      solve_flight(tx, ty, T.carry_z, og.clamp(og.div(d, T.chest_speed), T.chest_tf_min, T.chest_tf_max))
    else
      solve_flight(tx, ty, T.carry_z, og.clamp(og.div(d, T.lob_speed), T.lob_tf_min, T.lob_tf_max))
      og.emit_positional_sound(carrier, C.SOUND_YO)
    end
    og.mode_set(S.PASS_TARGET, og.entity_id(rec))
    og.mode_set(S.BALL_STATE, STATE_PASS)
    arm_grace(og.entity_id(carrier), 0, T.self_grace)
  end
  stamp_toucher(ball, og.entity_id(carrier), team)
  og.mode_set(S.CARRIER, 0)
  return tx, ty
end

-- Consumed-throw scan (§3.1): the FIRST QUALIFYING weapon in weaplist
-- order owned by the carrier AND above the possession watermark (D19)
-- becomes the throw. Qualifying (D27): ALIVE (the original rule); dead by
-- its OWN act this tick (death_called() == 1 — the point-blank weapon
-- that walked into a wall or adjacent enemy; still on the list until the
-- post-mode dead sweep, owner and aim step intact, contact damage
-- stands); or dead under a PLAYER-controlled carrier (ACT_CONTROL — the
-- fire()-time pad-blocked spawn dies via bare set_dead(1), per-weapon
-- indistinguishable from a walker::fire_check ray probe, but ACT_CONTROL
-- walkers never execute fire_check, so no probe can exist there). A dead
-- death_called() == 0 weapon under an AI carrier is exactly as likely a
-- costless scratch probe and is NEVER consumed — consuming it released
-- phantom throws and minted the D28 refund (see D27 and edge #29); such
-- a weapon does not stop the scan. set_dead(1) (a no-op on an
-- already-dead weapon), the carrier's weapon_cost refunds to its
-- magicpoints clamped at max. Returning-weapon families also recover the
-- launch credit that their consumed weapon can no longer return through its
-- normal death effect (D28: throwing is launch-resource-neutral; every
-- qualifying arm implies fire() paid the cost this tick),
-- and its per-tick step is the aim vector — a (0,0) step aims along the
-- carrier's facing (soccer's dead-center rule). One weapon per
-- possession (D27): the release clears CARRIER, so a later same-tick
-- weapon flies on normally and never qualifies again.
local function consume_throw(ball, livings, carrier)
  local watermark = og.mode_get(S.THROW_WATERMARK)
  local carrier_id = og.mode_get(S.CARRIER)
  local weapons = og.weaplist()
  for k = 1, #weapons do
    local shot = weapons[k]
    if og.entity_id(shot) > watermark then
      local shooter = shot:owner()
      if shooter ~= nil then
        if og.entity_id(shooter) == carrier_id then
          local qualifies = shot:dead() == 0
          if not qualifies then
            qualifies = shot:death_called() == 1
          end
          if not qualifies then
            qualifies = carrier:act_type() == C.ACT_CONTROL
          end
          if qualifies then
            local ax = og.trunc(shot:lastx())
            local ay = og.trunc(shot:lasty())
            shot:set_dead(1)
            if shot:death_called() == 0 and
                og.family_flag("living", carrier:family(), "has_returning_weapon") then
              carrier.weapons_left = carrier:weapons_left() + 1
            end
            local cost = carrier:s_weapon_cost()
            if cost > 0 then
              carrier.magicpoints = og.min(og.fadd(carrier.magicpoints, cost), carrier.max_magicpoints)
            end
            if ax == 0 then
              if ay == 0 then
                -- Dead-center step: aim along the carrier's facing.
                -- Core-family specials park curdir at the -1 sentinel
                -- while their command burst drains; a facing-less
                -- carrier heaves FACE_DOWN deterministically.
                local fdir = carrier:curdir() + 1
                ax = ai.FACING_X[fdir]
                ay = ai.FACING_Y[fdir]
                if ax == nil then
                  ax = 0
                  ay = 1
                end
              end
            end
            release_throw(ball, livings, carrier, ax, ay)
            return
          end
        end
      end
    end
  end
end

-- Is the ball inside the goaltend window right now (§3.5)? State SHOT,
-- descending, in the (rim_z, goaltend_ceiling] band, within
-- goaltend_radius L1 of the TARGET hoop.
local function in_goaltend_window(z_px, gx, gy)
  if og.mode_get(S.BALL_STATE) ~= STATE_SHOT then
    return false
  end
  if og.mode_get(S.BALL_VZ) >= 0 then
    return false
  end
  if z_px <= T.rim_z then
    return false
  end
  if z_px > T.goaltend_ceiling then
    return false
  end
  local hx, hy = hoop_center(og.mode_get(S.SHOT_HOOP1) - 1)
  if hx == nil then
    return false
  end
  return dist_l1(gx, gy, hx, hy) <= T.goaltend_radius
end

-- L1-normalized swat impulse: |vx| + |vy| sums to the commanded speed.
local function swat_velocity(dx, dy, speed_px)
  local total = iabs(dx) + iabs(dy)
  local scaled = speed_px * 256
  og.mode_set(S.BALL_VX, og.div(scaled * dx, total))
  og.mode_set(S.BALL_VY, og.div(scaled * dy, total))
end

-- Body-denial scan (D33/D34) — state SHOT only, and only in the low
-- window z_px <= grab_z: physically the release ascent alone (a solved
-- shot clears 20 px within 2-3 ticks and its descent terminates AT
-- rim_z > grab_z, so no body goaltending exists and a deny can never
-- collide with a score tick). The first live ENEMY score-team Living in
-- oblist order within catch_radius L1 of the ball GROUND center caroms
-- the shot off its body into a live REBOUND — a face-stuff scramble,
-- deliberately not a catch (passes keep try_catch's clean interception).
-- Exclusions, two prongs: the team prong (enemy score teams only, read
-- LIVE at the contact tick — press_count's definition; charm moves the
-- read, edge #35) and the id prong (LAST_TOUCHER names the releasing
-- entity for the whole flight, so even a CHARMED shooter walking into
-- its own slow shot cannot self-deny, edge #34). Grace bars do NOT gate
-- the deny: they bar TAKING the ball, not contesting it. Impulse, zero
-- RNG: swat_velocity along ball − defender at deny_speed (the run_swat
-- clamp floor — a body is the weakest legal swat), dead-center contact
-- falls back to the defender's facing (consume_throw's rule), and vz is
-- SET to fumble_pop — the T5 scramble pop, never the block's +512 sail.
-- Then the block arm's bookkeeping verbatim, minus any grace write
-- (denier and shooter may both grab the carom, D24). Precedence: runs
-- BEFORE run_swat (D34 — in the shared z band the body is the physical
-- surface at the contact point; the same tick's weapon scan may still
-- tip the fresh REBOUND, silently). The JUMP_UNTIL guard is a tripwire:
-- no SHOT can exist during the freeze (§9 #5).
local function run_deny(ball, livings)
  if og.mode_get(S.BALL_STATE) ~= STATE_SHOT then
    return
  end
  if og.world_tick() < og.mode_get(S.JUMP_UNTIL) then
    return
  end
  local z_px = og.div(og.mode_get(S.BALL_PZ), 256)
  if z_px > T.grab_z then
    return
  end
  local shooter_team = og.mode_get(S.SHOT_TEAM1) - 1
  local shooter_id = og.mode_get(S.LAST_TOUCHER)
  local gx, gy = ball_ground()
  for k = 1, #livings do
    local w = livings[k]
    local wt = w:team_num()
    local denies = wt < C.SCORE_TEAM_COUNT
    if denies then
      denies = wt ~= shooter_team
    end
    if denies then
      denies = og.entity_id(w) ~= shooter_id
    end
    if denies then
      local wx, wy = walker_center(w)
      if dist_l1(gx, gy, wx, wy) <= T.catch_radius then
        local dx = gx - wx
        local dy = gy - wy
        if dx == 0 then
          if dy == 0 then
            -- Dead-center contact: carom along the defender's facing
            -- (consume_throw's dead-center rule; the -1 curdir sentinel
            -- deflects FACE_DOWN deterministically).
            local fdir = w:curdir() + 1
            dx = ai.FACING_X[fdir]
            dy = ai.FACING_Y[fdir]
            if dx == nil then
              dx = 0
              dy = 1
            end
          end
        end
        swat_velocity(dx, dy, T.deny_speed)
        og.mode_set(S.BALL_VZ, T.fumble_pop)
        og.mode_set(S.BALL_STATE, STATE_REBOUND)
        clear_flight()
        stamp_toucher(ball, og.entity_id(w), wt)
        core.announce("DENIED!", C.SOUND_ROAR)
        landing_legal(ball)
        return
      end
    end
  end
end

-- Weapon swat scan (§3.5) — airborne states, after throw consumption and
-- the body-denial scan, before movement; jump freeze guards it. First live
-- weapon in weaplist
-- order within swat_radius L1 of the GROUND center that CAN swat; weapon
-- z is ignored (weapons fly flat; their reach is the abstraction). A
-- weapon whose per-tick step is (0, 0) — a boomerang at turnaround — has
-- no impulse direction, cannot swat, and is passed over so a later
-- in-radius weapon with a real step still contests the ball (§3.5's
-- zero-step ruling).
--   Goaltend window: a swat by a team other than the shooter's is
--   GOALTENDING — the basket counts. The shooter's own team tips it: a
--   live rebound the crossing rule may still convert — silently ("BLOCK!"
--   belongs to the block window only, §3.5).
--   Block window (z <= block_ceiling, any airborne state): legal contest —
--   REBOUND with the weapon-step impulse (soccer shot-impulse constants
--   clamp(trunc(damage)*2, 4, 12) px/tick) and a +512 fp pop; restamp to
--   the owner root (unowned clears LAST_TOUCH1 only); "BLOCK!" when the
--   victim was a SHOT. Between the windows the arc is untouchable — the
--   apex sanctuary. The swatting weapon is NOT consumed, and a restamp
--   never moves a grace bar (D24).
local function run_swat(ball)
  local st = og.mode_get(S.BALL_STATE)
  if st < STATE_SHOT then
    local water_free = false
    if st == STATE_FREE then
      water_free = surface.touches_water(
        ball, og.mode_get(S.BALL_PX), og.mode_get(S.BALL_PY))
    end
    if not water_free then
      return
    end
  end
  if og.world_tick() < og.mode_get(S.JUMP_UNTIL) then
    return
  end
  local gx, gy = ball_ground()
  local z_px = og.div(og.mode_get(S.BALL_PZ), 256)
  local weapons = og.weaplist()
  for k = 1, #weapons do
    local swat = weapons[k]
    if swat:dead() == 0 then
      local sx, sy = walker_center(swat)
      if dist_l1(gx, gy, sx, sy) <= T.swat_radius then
        local root = owner_root(swat)
        local windowed = z_px <= T.block_ceiling
        local tipped = false
        if not windowed then
          if in_goaltend_window(z_px, gx, gy) then
            local shooter_team = og.mode_get(S.SHOT_TEAM1) - 1
            local swat_team = -1
            if root ~= nil then
              swat_team = root:team_num()
            end
            local defensive = false
            if swat_team >= 0 then
              if swat_team < C.SCORE_TEAM_COUNT then
                defensive = swat_team ~= shooter_team
              end
            end
            if defensive then
              -- T12 GOALTENDING: the frozen outcome pays out in full.
              -- No step needed — the interference is the offense. The
              -- basket counts, so the target hoop swishes (D31).
              local label = "GOALTEND! " .. og.team_color_name(shooter_team) .. " +" .. og.mode_get(S.SHOT_VALUE)
              score_basket(ball, shooter_team, og.mode_get(S.SHOT_VALUE), label, og.mode_get(S.SHOT_HOOP1) - 1)
              return
            end
            -- Offensive (or unowned) tip in the window: fall through to
            -- the rebound conversion below (T11's tip arm), silently.
            windowed = true
            tipped = true
          end
        end
        if not windowed then
          -- The apex sanctuary: the ball's height bars EVERY weapon this
          -- tick, so the scan is done.
          return
        end
        local mx = og.trunc(swat:lastx())
        local my = og.trunc(swat:lasty())
        if mx ~= 0 or my ~= 0 then
          local was_shot = st == STATE_SHOT
          swat_velocity(mx, my, og.clamp(og.trunc(swat:damage()) * 2, 4, 12))
          og.mode_set(S.BALL_VZ, og.mode_get(S.BALL_VZ) + 512)
          og.mode_set(S.BALL_STATE, STATE_REBOUND)
          clear_flight()
          if root ~= nil then
            stamp_toucher(ball, og.entity_id(root), root:team_num())
          else
            stamp_toucher(ball, 0, C.SCORE_TEAM_COUNT)
          end
          if was_shot then
            if not tipped then
              core.announce("BLOCK!", C.SOUND_BOLT)
            end
          end
          landing_legal(ball)
          return
        end
        -- Zero-step weapon: skip it and keep scanning (§3.5).
      end
    end
  end
end

-- Shot resolution at FLIGHT_TICKS == 0 (§3.2). The outcome was decided at
-- release — the flight was presentation plus the two interaction windows.
-- Only the TARGET hoop is tested; the §5.5 hoop-separation authoring rule
-- makes cross-rim landings impossible.
local function resolve_shot(ball)
  -- Read the target hoop's team BEFORE the miss arm's clear_flight scrubs
  -- SHOT_HOOP1: the T9 rim flash needs it after the scrub (D31).
  local hoop_team = og.mode_get(S.SHOT_HOOP1) - 1
  local hx, hy = hoop_center(hoop_team)
  local gx, gy = ball_ground()
  local shooter = og.mode_get(S.SHOT_TEAM1) - 1
  local value = og.mode_get(S.SHOT_VALUE)
  local d = T.rim_r + T.rim_lip + 1
  if hx ~= nil then
    d = dist_l1(gx, gy, hx, hy)
  end
  if d <= T.rim_r then
    -- T8 basket.
    local label = "BASKET! " .. og.team_color_name(shooter) .. " +2"
    if value == 3 then
      label = "THREE! " .. og.team_color_name(shooter) .. " +3"
    end
    score_basket(ball, shooter, value, label, hoop_team)
    return
  end
  og.mode_set(S.BALL_STATE, STATE_REBOUND)
  clear_flight()
  if d <= T.rim_r + T.rim_lip then
    -- T9 rim clang: pop, push away from the hoop, per-axis deflection —
    -- and the rim flash rides the clang sound (D31). The scrum is live;
    -- LAST_TOUCH stays the shooter until someone touches — a rattled-in
    -- re-crossing is that shooter's tip-in.
    og.emit_positional_sound(ball, C.SOUND_CLANG)
    arm_hoop_anim(hoop_team, -T.hoop_clang_ticks)
    local sx, sy = ai.dir8(gx - hx, gy - hy)
    og.mode_set(S.BALL_VZ, T.rim_pop)
    og.mode_set(S.BALL_VX, sx * T.rim_speed + og.rand(2 * T.rim_scatter + 1) - T.rim_scatter)
    og.mode_set(S.BALL_VY, sy * T.rim_speed + og.rand(2 * T.rim_scatter + 1) - T.rim_scatter)
  end
  -- T10 airball: the velocity simply carries on and gravity finishes it.
  landing_legal(ball)
end

-- One tick of sub-stepped horizontal motion with axis-separated wall
-- reflection (soccer's substep bound). `reflect` is false above wall_top
-- (D12): shots and lobs clear the walls that chest passes and flat throws
-- bank off — the BANKHOUSE mechanism. Returns position and the possibly
-- flipped velocity.
local function move_substeps(ball, px, py, vx, vy, reflect)
  local half_x = og.div(ball:sizex(), 2)
  local half_y = og.div(ball:sizey(), 2)
  local steps = og.div(og.max(iabs(vx), iabs(vy)), T.substep_px * 256) + 1
  local wet = surface.touches_water(ball, px, py)
  for _ = 1, steps do
    local nx = px + og.div(vx, steps)
    if reflect then
      if not og.query_grid_passable(og.div(nx, 256) - half_x, og.div(py, 256) - half_y, ball) then
        vx = -vx
        nx = px + og.div(vx, steps)
      end
    end
    px = nx
    if surface.touches_water(ball, px, py) then
      wet = true
    end
    local ny = py + og.div(vy, steps)
    if reflect then
      if not og.query_grid_passable(og.div(px, 256) - half_x, og.div(ny, 256) - half_y, ball) then
        vy = -vy
        ny = py + og.div(vy, steps)
      end
    end
    py = ny
    if surface.touches_water(ball, px, py) then
      wet = true
    end
  end
  return px, py, vx, vy, wet
end

-- PASS contact (D23a): gather this tick's contenders — the intended
-- receiver at receiver_catch_radius up to catch_z (checked against its
-- CURRENT position), every other live Living at catch_radius up to grab_z
-- with the grace bars applied — and let oblist order among them decide. A
-- defender standing on the receiver has a real, deterministic chance; the
-- receiver keeps exclusive right only in the (grab_z, catch_z] band where
-- interceptors cannot reach. A charmed receiver's catch lands as an
-- interception with no extra code: gain_possession re-arms the clock
-- whenever the catching team differs from the clock team (§9 #22).
local function try_catch(ball, livings, z_px)
  if z_px > T.catch_z then
    return false
  end
  local gx, gy = ball_ground()
  local target_id = og.mode_get(S.PASS_TARGET)
  for k = 1, #livings do
    local w = livings[k]
    local wx, wy = walker_center(w)
    local d = dist_l1(gx, gy, wx, wy)
    local takes = false
    if og.entity_id(w) == target_id then
      takes = d <= T.receiver_catch_radius
      if takes then
        -- Only score teams may hold the ball — the mirror of the
        -- interceptor/pickup guards (the receiver keeps its D24 grace
        -- exemption; a poisoned team_num here would corrupt CLOCK_TEAM1
        -- and the POINTS bank downstream).
        takes = w:team_num() < C.SCORE_TEAM_COUNT
      end
    elseif z_px <= T.grab_z then
      if d <= T.catch_radius then
        if w:team_num() < C.SCORE_TEAM_COUNT then
          takes = not grace_barred(w)
        end
      end
    end
    if takes then
      return gain_possession(ball, w, false)
    end
  end
  return false
end

-- REBOUND ground touch: landing legality first (an illegal spot re-spots
-- and ends the bounce), then the energy split — half the vertical, three
-- quarters the horizontal — and the T17 settle to FREE once the bounce
-- falls under settle_vz (the horizontal speed carries into the roll; the
-- shot clock stays armed, D25).
local function run_ground_touch(ball)
  if landing_legal(ball) then
    return
  end
  local wet = surface.touches_water(
    ball, og.mode_get(S.BALL_PX), og.mode_get(S.BALL_PY))
  local rest_z = T.floor_rest_z
  if wet then
    rest_z = T.water_rest_z
  end
  local bounced = og.div(iabs(og.mode_get(S.BALL_VZ)) * rest_z, 256)
  og.mode_set(S.BALL_PZ, 0)
  local vx = og.mode_get(S.BALL_VX)
  local vy = og.mode_get(S.BALL_VY)
  if wet then
    vx, vy = surface.damp(vx, vy, true)
  else
    vx = og.div(vx * T.floor_rest_xy, 256)
    vy = og.div(vy * T.floor_rest_xy, 256)
  end
  og.mode_set(S.BALL_VX, vx)
  og.mode_set(S.BALL_VY, vy)
  if bounced < T.settle_vz then
    og.mode_set(S.BALL_VZ, 0)
    og.mode_set(S.BALL_STATE, STATE_FREE)
    return
  end
  og.mode_set(S.BALL_VZ, bounced)
end

-- Airborne integration for SHOT/PASS/REBOUND (§2.4): substeps with
-- z-gated reflection, gravity, then the per-state interactions — rim-plane
-- crossings (T15/T16; never for a SHOT, whose outcome is frozen), the
-- PASS catch race, the REBOUND ground bounce, and the SHOT/PASS flight
-- countdown into resolution or the T14 conversion.
local function run_air(ball, livings)
  local st = og.mode_get(S.BALL_STATE)
  local pz = og.mode_get(S.BALL_PZ)
  local vz = og.mode_get(S.BALL_VZ)
  local prev_z_px = og.div(pz, 256)
  local falling = vz < 0
  local px, py, vx, vy, swept_wet = move_substeps(
    ball, og.mode_get(S.BALL_PX), og.mode_get(S.BALL_PY),
    og.mode_get(S.BALL_VX), og.mode_get(S.BALL_VY),
    prev_z_px < T.wall_top)
  if pz <= 0 then
    if not falling then
      if swept_wet then
        vx, vy = surface.damp(vx, vy, true)
      end
    end
  end
  pz = pz + vz
  vz = vz - T.gravity
  og.mode_set(S.BALL_PX, px)
  og.mode_set(S.BALL_PY, py)
  og.mode_set(S.BALL_PZ, pz)
  og.mode_set(S.BALL_VX, vx)
  og.mode_set(S.BALL_VY, vy)
  og.mode_set(S.BALL_VZ, vz)
  local z_px = og.div(pz, 256)
  if st ~= STATE_SHOT then
    if falling then
      if prev_z_px > T.rim_z then
        if z_px <= T.rim_z then
          if run_crossing(ball) then
            return
          end
        end
      end
    end
  end
  if st == STATE_PASS then
    if try_catch(ball, livings, z_px) then
      return
    end
  end
  if st == STATE_REBOUND then
    if pz <= 0 then
      if falling then
        run_ground_touch(ball)
      end
    end
    return
  end
  local left = og.mode_get(S.FLIGHT_TICKS) - 1
  og.mode_set(S.FLIGHT_TICKS, left)
  if left > 0 then
    return
  end
  if st == STATE_SHOT then
    resolve_shot(ball)
    return
  end
  -- T14: uncaught pass — convert to a live rebound with the current
  -- velocity; the shot-clock deadline survives the conversion (D25).
  og.mode_set(S.BALL_STATE, STATE_REBOUND)
  clear_flight()
  landing_legal(ball)
end

-- FREE-state tick: frozen during the jump freeze, the one-shot T19 toss
-- at the boundary tick exactly (straight up; the grab race is live the
-- same tick — the pickup scan runs later in the pipeline), then soccer's
-- roll — substeps, wall reflection, shared dry/wet damping. Basketball never
-- calls attack: zero combat RNG from the ball.
local function run_free(ball)
  local now = og.world_tick()
  local jump_until = og.mode_get(S.JUMP_UNTIL)
  if now < jump_until then
    return
  end
  if now == jump_until then
    og.mode_set(S.BALL_STATE, STATE_REBOUND)
    og.mode_set(S.BALL_VZ, T.toss_pop)
    core.announce("JUMP BALL!", C.SOUND_YO)
    return
  end
  local vx = og.mode_get(S.BALL_VX)
  local vy = og.mode_get(S.BALL_VY)
  local speed = iabs(vx) + iabs(vy)
  if speed == 0 then
    return
  end
  local px, py, swept_wet
  px, py, vx, vy, swept_wet = move_substeps(
    ball, og.mode_get(S.BALL_PX), og.mode_get(S.BALL_PY), vx, vy, true)
  vx, vy = surface.damp(vx, vy, swept_wet)
  og.mode_set(S.BALL_PX, px)
  og.mode_set(S.BALL_PY, py)
  og.mode_set(S.BALL_VX, vx)
  og.mode_set(S.BALL_VY, vy)
end

-- CARRIED-state tick: the mode pins the ball to the carrier's center at
-- carry height with zero velocity — the carrier IS the ball's motion.
local function run_carry(carrier)
  local cx, cy = walker_center(carrier)
  og.mode_set(S.BALL_PX, cx * 256)
  og.mode_set(S.BALL_PY, cy * 256)
  og.mode_set(S.BALL_PZ, T.carry_z * 256)
  og.mode_set(S.BALL_VX, 0)
  og.mode_set(S.BALL_VY, 0)
  og.mode_set(S.BALL_VZ, 0)
end

-- Pickup scan (T1/T2): FREE always, REBOUND only at or under grab_z (the
-- air grab). Jump freeze bars everyone; grace bars apply; the FIRST
-- eligible live Living in oblist order wins contention (players sit in
-- earlier slots and win ties — accepted, §9 #4).
local function run_pickup(ball, livings)
  local st = og.mode_get(S.BALL_STATE)
  local grabbable = st == STATE_FREE
  if st == STATE_REBOUND then
    grabbable = og.div(og.mode_get(S.BALL_PZ), 256) <= T.grab_z
  end
  if not grabbable then
    return
  end
  if og.world_tick() < og.mode_get(S.JUMP_UNTIL) then
    return
  end
  local gx, gy = ball_ground()
  for k = 1, #livings do
    local w = livings[k]
    local wx, wy = walker_center(w)
    if dist_l1(gx, gy, wx, wy) <= T.pickup_radius then
      if w:team_num() < C.SCORE_TEAM_COUNT then
        if not grace_barred(w) then
          gain_possession(ball, w, true)
          return
        end
      end
    end
  end
end

-- Dunk detection (§3.4) — runs while CARRIED, after fumble/death
-- resolution and BEFORE throw consumption (a dunk beats a same-tick
-- throw, §9 #16). Outside every enemy box the presence dunk re-arms
-- (D23b); inside one with DUNK_OK it scores 2 — the carrier walked the
-- ball through the defense, and that is the whole gate.
local function run_dunk(ball, carrier)
  local cx, cy = walker_center(carrier)
  local team = carrier:team_num()
  local boxed = enemy_box_at(team, cx, cy)
  if boxed < 0 then
    og.mode_set(S.DUNK_OK, 1)
    return
  end
  if og.mode_get(S.DUNK_OK) == 1 then
    score_basket(ball, team, 2, "DUNK! " .. og.team_color_name(team) .. " +2", boxed)
  end
end

-- Shot-clock upkeep (§3.6): the one-shot warning at exactly clock_warn
-- remaining (equality against a fixed deadline fires once — no flag
-- slot), the charm re-arm when the carrier's live team stops matching the
-- clock team, and the T7 expiry while CARRIED. Loose-ball expiry fires at
-- the next clock-team regain instead (gain_possession rule 1).
local function run_shot_clock(ball, carrier)
  local deadline = og.mode_get(S.CLOCK_UNTIL)
  if deadline == 0 then
    return
  end
  local now = og.world_tick()
  if deadline - now == T.clock_warn then
    core.announce("SHOT CLOCK!", C.SOUND_YO)
  end
  if og.mode_get(S.BALL_STATE) ~= STATE_CARRIED then
    return
  end
  if carrier == nil then
    return
  end
  local team = carrier:team_num()
  if og.mode_get(S.CLOCK_TEAM1) ~= team + 1 then
    og.mode_set(S.CLOCK_TEAM1, team + 1)
    og.mode_set(S.CLOCK_UNTIL, now + T.shot_clock)
    return
  end
  if now >= deadline then
    turnover(ball, team)
  end
end

-- Reset watchdog (§3.9, D25) on the one STALL_SINCE slot. While any
-- ACTIVE team is wiped — zero live Livings AND zero pending revives — the
-- countdown runs in EVERY ball state and ignores attendance: a winner
-- cannot babysit a free ball, ride the turnover cycle or keep the ball
-- carried to deny the revive; at dead_ball_ticks the forced center reset's
-- revive backstop restores the team. With nobody wiped it is the plain
-- dead ball: FREE only, motionless, unattended for dead_ball_ticks.
local function run_reset_watchdog(ball, livings, live_count)
  local mask = og.mode_get(S.TEAM_MASK)
  local wiped = false
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if live_count[team + 1] == 0 then
        if og.respawn_pending_count(team) == 0 then
          wiped = true
        end
      end
    end
  end
  if not wiped then
    if og.mode_get(S.BALL_STATE) ~= STATE_FREE then
      og.mode_set(S.STALL_SINCE, 0)
      return
    end
    if iabs(og.mode_get(S.BALL_VX)) + iabs(og.mode_get(S.BALL_VY)) > 0 then
      og.mode_set(S.STALL_SINCE, 0)
      return
    end
    local waterlogged = surface.touches_water(
      ball, og.mode_get(S.BALL_PX), og.mode_get(S.BALL_PY))
    if not waterlogged then
      local gx, gy = ball_ground()
      for k = 1, #livings do
        local wx, wy = walker_center(livings[k])
        if dist_l1(gx, gy, wx, wy) <= T.dead_ball_radius then
          og.mode_set(S.STALL_SINCE, 0)
          return
        end
      end
    end
  end
  local now = og.world_tick()
  local since = og.mode_get(S.STALL_SINCE)
  if since == 0 then
    og.mode_set(S.STALL_SINCE, now)
    return
  end
  if now - since >= T.dead_ball_ticks then
    core.announce("BALL RESET", C.SOUND_YO)
    center_reset(ball)
  end
end

-- Score limit first, in team order; the time limit runs the shared
-- three-rung ladder (POINTS, then match score, then lowest team byte) —
-- but a buzzer beater defers it: a SHOT in the air resolves first
-- (bounded by shot_tf_max; a rim clang ends the deferral, so tips after
-- the buzzer do not count, §3.7). The deferral rides the same handles
-- success that ran ball logic this tick — FLIGHT_TICKS only counts down
-- while ball logic runs, so a vanished ball handle (§9 #13) must not
-- freeze the buzzer forever.
local function run_win_check(level_tick, handles)
  local mask = og.mode_get(S.TEAM_MASK)
  local limit = og.mode_get(S.SCORE_LIMIT)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if points_of(team) >= limit then
        core.declare_team_win(team)
        return
      end
    end
  end
  if level_tick < og.mode_get(S.TIME_LIMIT) then
    return
  end
  if handles then
    if og.mode_get(S.BALL_STATE) == STATE_SHOT then
      return
    end
  end
  local winner = match.timeout_leader(mask, points_of)
  if winner >= 0 then
    core.declare_team_win(winner)
  end
end

-- One score row per active team (4-team courts fill all four slots — no
-- spare line exists for a dedicated clock row). While the clock is armed
-- and inside clock_hud, the POSSESSING team's own row carries the
-- countdown suffix " !N" in seconds, rounded up so a live clock never
-- reads 0 — a dead-even deadline (reachable for one tick while the ball
-- is loose) shows no suffix at all (worst case "YELLOW 999/999 !10" =
-- 18 <= 25 bytes, I8).
local function update_hud()
  local mask = og.mode_get(S.TEAM_MASK)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local suffix_team = -1
  local seconds = 0
  local deadline = og.mode_get(S.CLOCK_UNTIL)
  if deadline ~= 0 then
    local left = deadline - og.world_tick()
    if left > 0 then
      if left <= T.clock_hud then
        suffix_team = og.mode_get(S.CLOCK_TEAM1) - 1
        seconds = og.div(left + 11, 12)
      end
    end
  end
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if team == suffix_team then
        og.set_hud_line(team, og.team_color_name(team) .. " " .. points_of(team) .. "/" .. limit .. " !" .. seconds, team)
      else
        core.hud_score_line(team, team, points_of(team), limit)
      end
    end
  end
end

-- Spin frames off the 8-frame rotation strip. The roll is soccer's shape
-- (phase advances by speed, signed by the travel direction); airborne
-- states advance a constant air_spin signed the same way — the backspin
-- look; a held ball does not spin. Phase lives in a mode var and the
-- resolved frame replicates, so mirrors draw it without re-deriving (I4).
local function run_spin(ball)
  local st = og.mode_get(S.BALL_STATE)
  if st == STATE_CARRIED then
    return
  end
  local vx = og.mode_get(S.BALL_VX)
  local vy = og.mode_get(S.BALL_VY)
  local step
  if st == STATE_FREE then
    local speed = iabs(vx) + iabs(vy)
    if speed == 0 then
      return
    end
    step = og.div(speed, T.spin_divisor)
  else
    step = T.air_spin
  end
  local dir = og.sign(vx)
  if iabs(vy) > iabs(vx) then
    dir = og.sign(vy)
  end
  if dir == 0 then
    dir = 1
  end
  -- og.mod is the C remainder (sign of the dividend), so a leftward roll
  -- lands negative and folds back into 0..spin_cycle-1.
  local phase = og.mod(og.mode_get(S.BALL_SPIN) + dir * step, T.spin_cycle)
  if phase < 0 then
    phase = phase + T.spin_cycle
  end
  og.mode_set(S.BALL_SPIN, phase)
  ball:set_frame(og.div(phase, T.spin_step))
end

-- Presentation pinning (pipeline step 14, I4): the ball entity draws at
-- the ground spot LIFTED by z (mirrors interpolate the whole arc off the
-- replicated x/y); the shadow fx marks the true ground spot and shrinks
-- with altitude. Beacon 0 is the SHADOW — ground truth for radar and
-- off-screen arrows, since the ball entity draws up to ~40 px high at the
-- apex — and beacon 1 tracks the carrier, team-tinted, or clears.
local function sync_render(ball, shadow)
  local gx, gy = ball_ground()
  local z_px = og.div(og.mode_get(S.BALL_PZ), 256)
  ball:setxy(gx - og.div(ball:sizex(), 2), gy - z_px - og.div(ball:sizey(), 2))
  shadow:setxy(gx - og.div(shadow:sizex(), 2), gy - og.div(shadow:sizey(), 2))
  shadow:set_frame(og.clamp(og.div(z_px, T.shadow_band), 0, 3))
  run_spin(ball)
  og.set_beacon(0, shadow)
  local carrier = nil
  if og.mode_get(S.BALL_STATE) == STATE_CARRIED then
    carrier = og.find_by_id(og.mode_get(S.CARRIER))
  end
  if carrier ~= nil then
    og.set_beacon(1, carrier, carrier:team_num())
  else
    og.set_beacon(1, nil)
  end
end

-- ---------------------------------------------------------------------------
-- AI director (§4). Cadence 15, active teams in team order, members in
-- oblist order, humans excluded by ai.is_directable, zero RNG. The
-- anti-milling doctrine: GOTO's arrival distance (12) equals the contact
-- radius (12), so every contact-seeking command targets a point ON or
-- THROUGH the objective with the half-body top-left correction.
-- ---------------------------------------------------------------------------

-- The nearest ACTIVE enemy hoop to a point — the handler ladder's target
-- and the §4.4 threatened-team selector.
local function nearest_enemy_hoop(team, x, y)
  local mask = og.mode_get(S.TEAM_MASK)
  local best = -1
  local best_x = 0
  local best_y = 0
  local best_d = 0
  for t = 0, C.SCORE_TEAM_COUNT - 1 do
    local wanted = t ~= team
    if wanted then
      wanted = core.mask_has(mask, t)
    end
    if wanted then
      local hx, hy = hoop_center(t)
      if hx ~= nil then
        local d = dist_l1(x, y, hx, hy)
        local better = best < 0
        if not better then
          better = d < best_d
        end
        if better then
          best = t
          best_x = hx
          best_y = hy --[[@as integer]] -- hoop_center's nil arm returns no second value; inside the hx guard the pair is the banked integers
          best_d = d
        end
      end
    end
  end
  return best, best_x, best_y
end

-- Non-combat scheme command: foe cleared (suppresses the tick auto-foe
-- backstop so brawls stay brief), leader on the ball, GOTO the CENTER
-- point with the half-body correction.
local function send_to(w, ball, cx, cy)
  w:set_foe(nil)
  w:set_leader(ball)
  ai.issue_front(w, C.COMMAND_GOTO, 30, cx - og.div(w:sizex(), 2), cy - og.div(w:sizey(), 2))
end

-- The team's directable members in oblist order, minus one excluded
-- entity (the carrier playing its own ladder, or the D22 receiver that
-- must keep its landing-point command).
local function directable_members(livings, team, exclude_id)
  local members = {}
  for k = 1, #livings do
    local w = livings[k]
    if ai.is_directable(w, team) then
      if og.entity_id(w) ~= exclude_id then
        members[#members + 1] = w
      end
    end
  end
  return members
end

local function fresh_flags(members)
  local flags = {}
  for i = 1, #members do
    flags[i] = false
  end
  return flags
end

-- The HELP shape (§4.3/§4.5): every unassigned member holds the midpoint
-- of two points with the lateral STAGGER fan — except that ONE member
-- already holding an un-preemptable combat command may keep it (the
-- single permitted peel, §4.1); everyone else is pulled into the scheme.
local function hold_seam(ball, members, assigned, ax, ay, bx, by)
  local midx = og.div(ax + bx, 2)
  local midy = og.div(ay + by, 2)
  local sx, sy = ai.dir8(bx - ax, by - ay)
  local peeled = false
  local support = 0
  for i = 1, #members do
    if not assigned[i] then
      local w = members[i]
      local keep = false
      if not ai.may_preempt(w:s_front_command()) then
        keep = not peeled
        peeled = true
      end
      if not keep then
        local lat = STAGGER[og.mod(support, 3) + 1]
        support = support + 1
        send_to(w, ball, midx - sy * lat, midy + sx * lat)
      end
    end
  end
end

-- Bot handler decision ladder (§4.2, ordered for the D20 curve per D23c);
-- first match wins. Returns true when the ball was released — the
-- script-invoked release_throw IS the mode's throw function, so bots need
-- no input path and both entrances share one classification body.
local function run_handler(ball, livings, carrier, team, ccx, ccy, hx, hy)
  local pressed = press_count(livings, team, ccx, ccy) > 0
  -- 1. Open shot inside the sweet range (Euclid, ~50%+ to fall).
  if not pressed then
    if euclid2(ccx, ccy, hx, hy) <= T.shot_sweet * T.shot_sweet then
      release_throw(ball, livings, carrier, hx - ccx, hy - ccy)
      return true
    end
  end
  -- 2. Drive: GOTO through the hoop — the 24 px dunk box outreaches the
  -- 12 px arrival, so the dunk triggers en route. Contact on the way
  -- risks the post-grace fumble (D21) — the intended tension, and under
  -- pressure the dunk is the only scatter-free 2.
  if dist_l1(ccx, ccy, hx, hy) <= T.dunk_drive_range then
    local sx, sy = ai.dir8(hx - ccx, hy - ccy)
    send_to(carrier, ball, hx + sx * T.drive_offset, hy + sy * T.drive_offset)
    return false
  end
  -- 3. Clock panic: shoot regardless of pressure (a heave from beyond
  -- shot range classifies as a flat throw — accepted).
  local deadline = og.mode_get(S.CLOCK_UNTIL)
  if deadline ~= 0 then
    if deadline - og.world_tick() <= T.clock_panic then
      release_throw(ball, livings, carrier, hx - ccx, hy - ccy)
      return true
    end
  end
  -- 4. Open cutter meaningfully closer to the hoop AND inside pass range
  -- (D23d "open": nobody pressing the cutter AND nobody pressing the
  -- carrier-cutter midpoint). The pass_range_max gate mirrors the joint
  -- classifier's candidate bound (§3.1) — beyond it the "pass" would
  -- silently classify as a 96 px flat heave, a wasted possession. With
  -- the cutter in range the exact aim vector resolves as the PASS unless
  -- the carrier, cutter and an in-range hoop are exactly collinear —
  -- §3.1's tie rule then hands the release to the hoop as a SHOT. On a
  -- genuine PASS the director leads the ACTUAL receiver onto the landing
  -- point (D22) and the PASS-state arm skips re-commanding it while the
  -- ball flies; on the tie-SHOT nobody is commanded — the returned
  -- landing point is then the private SHOT_LAND the director must never
  -- act on (D26), and the next cadence's shot-flight boxout takes over.
  local carrier_hoop = dist_l1(ccx, ccy, hx, hy)
  for k = 1, #livings do
    local mate = livings[k]
    local ok = mate:team_num() == team
    if ok then
      ok = og.entity_id(mate) ~= og.entity_id(carrier)
    end
    if ok then
      local mx, my = walker_center(mate)
      if dist_l1(ccx, ccy, mx, my) <= T.pass_range_max then
        if dist_l1(mx, my, hx, hy) + T.pass_gain <= carrier_hoop then
          if press_count(livings, team, mx, my) == 0 then
            if press_count(livings, team, og.div(ccx + mx, 2), og.div(ccy + my, 2)) == 0 then
              local tx, ty = release_throw(ball, livings, carrier, mx - ccx, my - ccy)
              if og.mode_get(S.BALL_STATE) == STATE_PASS then
                local rec = og.find_by_id(og.mode_get(S.PASS_TARGET))
                if rec ~= nil then
                  if ai.is_directable(rec, team) then
                    send_to(rec, ball, tx, ty)
                  end
                end
              end
              return true
            end
          end
        end
      end
    end
  end
  -- 5. Advance: the drive-through corridor test against the target hoop;
  -- through it when the geometry passes, an approach point short of it
  -- when it does not (rung 2 takes over inside dunk_drive_range).
  local sx, sy = ai.dir8(hx - ccx, hy - ccy)
  if ai.chaser_drives(carrier, hx, hy, sx, sy, T) then
    send_to(carrier, ball, hx + sx * T.drive_offset, hy + sy * T.drive_offset)
  else
    send_to(carrier, ball, hx - sx * T.cutter_standoff, hy - sy * T.cutter_standoff)
  end
  return false
end

-- Offense minus the handler (§4.2): the two members nearest the target
-- hoop take the flank posts — court-side of the hoop, one per side by
-- assignment order; they are the pass menu. Everyone else falls back to
-- the seam between own hoop and center court, one peel permitted.
local function run_support(ball, livings, team, carrier_id, hx, hy)
  local members = directable_members(livings, team, carrier_id)
  if #members == 0 then
    return
  end
  local assigned = fresh_flags(members)
  local jpos = og.mode_get(S.JUMP_POS)
  local jx = core.pos_x(jpos)
  local jy = core.pos_y(jpos)
  local sx, sy = ai.dir8(jx - hx, jy - hy)
  local side = 1
  for _ = 1, 2 do
    local idx = ai.nearest_unassigned(members, assigned, hx, hy)
    if idx ~= nil then
      assigned[idx] = true
      send_to(members[idx], ball, hx + sx * T.cutter_standoff - sy * T.cutter_perp * side, hy + sy * T.cutter_standoff + sx * T.cutter_perp * side)
      side = -side
    end
  end
  local own_x, own_y = hoop_center(team)
  hold_seam(ball, members, assigned, own_x, own_y, jx, jy)
end

-- Team in possession (§4.2): a human carrier is untouched and the squad
-- plays around it; a bot carrier walks the ladder first — a release ends
-- the cadence for this team (the receiver keeps its landing command).
local function run_offense(ball, livings, team, carrier)
  local ccx, ccy = walker_center(carrier)
  local ht, hx, hy = nearest_enemy_hoop(team, ccx, ccy)
  if ht < 0 then
    return
  end
  if ai.is_directable(carrier, team) then
    if run_handler(ball, livings, carrier, team, ccx, ccy, hx, hy) then
      return
    end
  end
  run_support(ball, livings, team, og.entity_id(carrier), hx, hy)
end

-- Defending (§4.3/§4.4). The THREATENED team — defender of the hoop the
-- handler ladder attacks — plays the full scheme: on-ball defender (foe
-- set; any landed hit forces the fumble — this is the steal), rim
-- protector in the dunk lane, HELP on the carrier/own-hoop seam. Every
-- OTHER defending team sends exactly ONE fumble vulture and keeps the
-- rest home — bounded mobbing that reads correctly.
local function run_defense(ball, livings, team, carrier, threatened)
  local members = directable_members(livings, team, 0)
  if #members == 0 then
    return
  end
  local ccx, ccy = walker_center(carrier)
  local own_x, own_y = hoop_center(team)
  local assigned = fresh_flags(members)
  local idx = ai.nearest_unassigned(members, assigned, ccx, ccy)
  if idx ~= nil then
    assigned[idx] = true
    local onball = members[idx]
    onball:set_foe(carrier)
    ai.issue_front(onball, C.COMMAND_GOTO, 30, ccx - og.div(onball:sizex(), 2), ccy - og.div(onball:sizey(), 2))
  end
  if threatened then
    local ridx = ai.nearest_unassigned(members, assigned, own_x, own_y)
    if ridx ~= nil then
      assigned[ridx] = true
      local sx, sy = ai.dir8(ccx - own_x, ccy - own_y)
      send_to(members[ridx], ball, own_x + sx * T.rim_protect_standoff, own_y + sy * T.rim_protect_standoff)
    end
    hold_seam(ball, members, assigned, ccx, ccy, own_x, own_y)
    return
  end
  -- §4.4: a NON-threatened team keeps everyone but the vulture HOME — the
  -- HELP fan centered ON its own hoop, oriented across the hoop-carrier
  -- axis (mirroring the carrier through the hoop keeps hold_seam's
  -- midpoint exactly at the hoop; dir8 is scale-invariant).
  hold_seam(ball, members, assigned, 2 * own_x - ccx, 2 * own_y - ccy, ccx, ccy)
end

-- Loose ball (§4.5): per team the nearest TWO members race — commanded
-- ONTO the ground center, so arriving IS picking up — and the rest hold
-- the seam between own hoop and the ball. During a PASS the intended
-- receiver is exempt (D22): it keeps its landing-point command.
local function run_loose(ball, livings, team, exclude_id)
  local members = directable_members(livings, team, exclude_id)
  if #members == 0 then
    return
  end
  local gx, gy = ball_ground()
  local assigned = fresh_flags(members)
  local racers = 2
  local slow = iabs(og.mode_get(S.BALL_VX)) +
    iabs(og.mode_get(S.BALL_VY)) <= T.water_recover_speed
  local water_recoverable = og.mode_get(S.BALL_STATE) == STATE_FREE
  if water_recoverable then
    water_recoverable = og.mode_get(S.BALL_PZ) == 0
  end
  if slow and water_recoverable then
    if surface.touches_water(
        ball, og.mode_get(S.BALL_PX), og.mode_get(S.BALL_PY)) then
      local recovery =
        ai.nearest_hostile_ranged_unassigned(
          members, assigned, ball, gx, gy)
      if recovery ~= nil then
        assigned[recovery] = true
        racers = 1
        ai.attack_objective(members[recovery], ball)
      end
    end
  end
  for _ = 1, racers do
    local idx = ai.nearest_unassigned(members, assigned, gx, gy)
    if idx ~= nil then
      assigned[idx] = true
      send_to(members[idx], ball, gx, gy)
    end
  end
  local own_x, own_y = hoop_center(team)
  hold_seam(ball, members, assigned, own_x, own_y, gx, gy)
end

-- Shot in the air (§4.5): the nearest member of every active team boxes
-- out at the TARGET hoop (public, replicated geometry — never SHOT_LAND,
-- whose scatter outcome stays sim-internal, D26; the actual landing is
-- within scatter_cap_total + rim_r + rim_lip = 42 px of the rim
-- regardless). Everyone else holds their posts.
local function run_shot_flight(ball, livings, team)
  local hx, hy = hoop_center(og.mode_get(S.SHOT_HOOP1) - 1)
  if hx == nil then
    return
  end
  local members = directable_members(livings, team, 0)
  if #members == 0 then
    return
  end
  local assigned = fresh_flags(members)
  local idx = ai.nearest_unassigned(members, assigned, hx, hy)
  if idx ~= nil then
    local own_x, own_y = hoop_center(team)
    local sx, sy = ai.dir8(own_x - hx, own_y - hy)
    send_to(members[idx], ball, hx + sx * T.rebound_boxout, hy + sy * T.rebound_boxout)
  end
end

-- Jump freeze (§4.5): a face-off ring — every directable member posts 24
-- px own-hoop-side of the tip spot; at the toss the race is on.
local function run_faceoff(ball, livings, team)
  local members = directable_members(livings, team, 0)
  local jpos = og.mode_get(S.JUMP_POS)
  local jx = core.pos_x(jpos)
  local jy = core.pos_y(jpos)
  local own_x, own_y = hoop_center(team)
  local sx, sy = ai.dir8(own_x - jx, own_y - jy)
  for i = 1, #members do
    send_to(members[i], ball, jx + sx * 24, jy + sy * 24)
  end
end

-- Per-team dispatch. State and carrier are re-read per team so a handler
-- release earlier in the same cadence flows straight into the loose/pass
-- schemes for the teams directed after it.
local function run_team_director(ball, livings, team)
  local st = og.mode_get(S.BALL_STATE)
  if st == STATE_CARRIED then
    local carrier = og.find_by_id(og.mode_get(S.CARRIER))
    if carrier ~= nil then
      local cteam = carrier:team_num()
      if cteam == team then
        run_offense(ball, livings, team, carrier)
        return
      end
      local ccx, ccy = walker_center(carrier)
      local threatened = nearest_enemy_hoop(cteam, ccx, ccy) == team
      run_defense(ball, livings, team, carrier, threatened)
    end
    return
  end
  if st == STATE_SHOT then
    run_shot_flight(ball, livings, team)
    return
  end
  if st == STATE_FREE then
    if og.world_tick() < og.mode_get(S.JUMP_UNTIL) then
      run_faceoff(ball, livings, team)
      return
    end
  end
  run_loose(ball, livings, team, og.mode_get(S.PASS_TARGET))
end

local function run_director(ball, livings)
  local mask = og.mode_get(S.TEAM_MASK)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      run_team_director(ball, livings, team)
    end
  end
end

-- The per-tick ball pipeline (§2.8 steps 3-11), in decided order: carrier
-- liveness (T6), fumble resolution (T5), dunk (beats the throw), throw
-- consumption, the body-denial scan then the weapon swat scan (both
-- before movement; body before weapon, D34), physics by state, the
-- pickup scan, the reset watchdog and the shot clock. The on_damage hook
-- already ran in the act phase, strictly before all of this — a hit
-- carrier's fired weapon is NOT consumed and flies as a plain weapon.
local function run_ball_logic(ball, livings, live_count)
  local carrier = nil
  if og.mode_get(S.BALL_STATE) == STATE_CARRIED then
    carrier = og.find_by_id(og.mode_get(S.CARRIER))
    local gone = true
    if carrier ~= nil then
      gone = carrier:dead() ~= 0
    end
    if gone then
      -- T6: carrier death or vanished handle — drop in place, no grace,
      -- the deadline stays armed (D25); LAST_TOUCH stays the dead
      -- carrier's team (a rolling FREE ball cannot score, §9 #1).
      drop_ball()
      carrier = nil
    elseif og.mode_get(S.FUMBLE_TICK) ~= 0 then
      -- T5 fumble: the ex-carrier wears the entity self grace (D24) so
      -- the defender who forced it can scoop what it earned.
      arm_grace(og.mode_get(S.CARRIER), 0, T.self_grace)
      drop_ball()
      carrier = nil
    else
      run_dunk(ball, carrier)
      if og.mode_get(S.BALL_STATE) == STATE_CARRIED then
        consume_throw(ball, livings, carrier)
      end
    end
  end
  run_deny(ball, livings)
  run_swat(ball)
  local st = og.mode_get(S.BALL_STATE)
  if st == STATE_FREE then
    run_free(ball)
  elseif st == STATE_CARRIED then
    run_carry(carrier)
  else
    run_air(ball, livings)
  end
  run_pickup(ball, livings)
  run_reset_watchdog(ball, livings, live_count)
  run_shot_clock(ball, carrier)
end

-- The fumble arm (D21). Gates on the latch, the carrier, the damage floor
-- (chip at or under 1 never strips the ball) and the possession grace.
-- Always returns nil — authored damage is untouched, so the return-0-is-
-- not-a-cancel trap never arises; sub-threshold and in-grace hits still
-- hurt the carrier, they just do not force the drop.
local function on_damage(target, attacker, amount)
  if not bball_active() then
    return nil
  end
  local carrier_id = og.mode_get(S.CARRIER)
  if carrier_id == 0 then
    return nil
  end
  if og.entity_id(target) ~= carrier_id then
    return nil
  end
  if amount < T.fumble_min_damage then
    return nil
  end
  if og.world_tick() < og.mode_get(S.POSSESS_SINCE) + T.possession_grace then
    return nil
  end
  og.mode_set(S.FUMBLE_TICK, og.world_tick())
  return nil
end

local function on_respawn(ent)
  if not bball_active() then
    return
  end
  anchors.place_at_anchor(ent, ent:team_num(), S.ANCHOR_CURSOR, false)
end

-- One rim sprite per ACTIVE team's banked hoop (D30): inactive-team hoops
-- on a 2-3 team FOUR HOOPS court get no sprite, matching which goals score
-- — the bare carpet is the dead-goal tell (edge #30). set_team_num is the
-- tint (the sprite's 248-255 band remaps to the defending team's color)
-- AND the find_hoop key. Spawned after the shadow so the ball/shadow
-- entity ids stay put; center resets never touch these.
local function spawn_hoops()
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local hx, hy = hoop_center(team)
    if hx ~= nil then
      local hoop = og.add_fx_ob("effect", HOOP_FAMILY)
      if hoop == nil then
        error("basketball: cannot spawn the hoop for team " .. team)
      end
      hoop:set_team_num(team)
      -- The 3/4-view sprite hangs its net BELOW the rim: the rim tube's
      -- center sits 10 rows into the 26-row frame (make_hoop_frames'
      -- rim_cy), so anchoring THAT row on the manifest hoop center puts
      -- the ball's landing point visually through the ring, with the
      -- net swishing beneath it. A plain sizey/2 center would land the
      -- ball in the middle of the net instead.
      hoop:setxy(hx - og.div(hoop:sizex(), 2), hy - 10)
      hoop:set_frame(0)
    end
  end
end

-- The decide fold (#218 staged lobby — the old on_mode_plan body,
-- verbatim, now a local consumed by on_mode_init directly): authored
-- domain (anchor-marker teams — the versus authoring contract, read from
-- the engine anchor census), activation, per-team fills and the resolved
-- score limit. Pure over the census inputs; only the inputs decide.
local function decide(level, inputs, row)
  if row == nil then
    error("basketball: no manifest row for level " .. level)
  end
  local authored_mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if inputs.teams[team + 1].anchors > 0 then
      authored_mask = core.mask_add(authored_mask, team)
    end
  end
  -- The shared activation rule (lineup B1-B4): the manifest row.teams is
  -- the map's own value, plus every occupied team; the fills rows below
  -- drop any team the knobs leave with nothing.
  local mask, starts, matched, matched_size =
      match.activation(inputs, authored_mask, row.teams or 0)
  local limit = match.resolve_limit(row, "score_limit", inputs.score_limit,
                                    T.score_limit)
  local teams, seeded, lineup_mask = match.fills(inputs, mask, {
    matched = matched,
    matched_size = matched_size,
    -- The court's hard shape (review R2): five on five, so a squad
    -- fills only the room the roster leaves. The same cap rides every
    -- spawn call below, so the preview's count IS the spawned count.
    squad_cap = T.squad_cap,
  })
  -- The NONE knob can empty a backfilled team outright (lineup §3.2):
  -- the fills' narrowed mask is the decision's, and starts recounts it.
  mask = lineup_mask
  if starts then
    starts = core.mask_count(mask) >= 2
  end
  local reason = nil
  if not starts then
    reason = "basketball: fewer than two anchor teams"
  end
  return {
    mode = "BASKETBALL",
    starts = starts,
    reason = reason,
    authored_mask = authored_mask,
    active_mask = mask,
    matched = matched,
    matched_size = matched_size,
    score_limit = og.clamp(limit, 1, 255),
    seeded_squads = seeded,
    teams = teams,
  }
end

-- Init (runs once, at staging): the census + the decide fold first, then
-- the world writes, strips and spawns. Raising reports the failed-init
-- shape: the engine latches inactive and classic rules own the level (the
-- CTF discipline).
local function on_mode_init(level, row)
  if row == nil then
    error("basketball: no manifest row for level " .. level)
  end
  local inputs = match.census_inputs()
  local decision = decide(level, inputs, row)
  if not decision.starts then
    error(decision.reason)
  end
  local mask = decision.active_mask
  local active_count = core.mask_count(mask)
  if row.hoops == nil then
    error("basketball: manifest row has no hoops")
  end
  if row.arc_radius == nil then
    error("basketball: manifest row has no arc_radius")
  end
  if row.jump_ball == nil then
    error("basketball: manifest row has no jump_ball")
  end
  -- Hoops bank for ACTIVE teams only (inactive hoops read 0 and neither
  -- score nor count — the 3-team FOUR HOOPS shape, §6.3).
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      local hoop = row.hoops[team]
      if hoop == nil then
        error("basketball: no hoop for team " .. team)
      end
      fset(S.HOOP_POS, team, core.pos_pack(hoop.x, hoop.y))
    else
      fset(S.HOOP_POS, team, 0)
    end
  end
  og.mode_set(S.ARC_RADIUS, row.arc_radius)
  og.mode_set(S.JUMP_POS, core.pos_pack(row.jump_ball.x, row.jump_ball.y))
  og.mode_set(S.TEAM_MASK, mask)
  og.mode_set(S.TEAM_COUNT, active_count)
  -- Score limit is the decision's (request > manifest row > default,
  -- clamped); the respawn/time knobs resolve here.
  og.mode_set(S.SCORE_LIMIT, decision.score_limit)
  local respawn_ticks = og.match_setting("respawn_ticks")
  if respawn_ticks <= 0 then
    respawn_ticks = T.respawn_ticks
  end
  og.mode_set(S.RESPAWN_TICKS, respawn_ticks)
  og.mode_set(S.TIME_LIMIT, match.resolve_time_limit(row, T.time_limit_ticks))
  caps.bank_caps(row, S.SPAWN_CAP)
  -- Consume the active teams' start markers (anchors are already banked
  -- engine-side), strip inactive score teams, then roster-only armies on
  -- request — all BEFORE the plan's squad fills so the spawns land in the
  -- final world.
  local obs = og.oblist()
  -- FAIR banking (the fold decided matched-ness and the headcount; the
  -- power TARGET is measured here, D24) before any squad spawn reads it.
  match.bank_match_target(decision, obs)
  match.consume_markers(obs, mask)
  match.strip_inactive_teams(obs, mask)
  strip.strip_authored_troops()
  -- FILL squads where the decision said so (the empty active teams,
  -- plus any company row with an allies gap — amendment B2/B3), capped
  -- at the court's five (the decide fold's squad_cap twinned).
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if match.wants_squad(decision.teams[team + 1]) then
      anchors.spawn_bot_squad(team, S.ANCHOR_CURSOR, T.squad_cap)
    end
  end
  -- og.add_ob for the ball, not og.add_fx_ob: the fx list never acts,
  -- while an acting ball dispatches the family on_act whose true return
  -- keeps the engine from moving, animating or expiring it. The shadow is
  -- the opposite call on purpose: fx-list, hookless, renders under
  -- everything, replicates (D11/I4).
  local ball_family = og.family_id("effect", "modes:bball") --[[@as integer]] -- the pack declares modes:bball; a nil would be a load bug and add_ob erroring loudly is the right failure
  local ball = og.add_ob("effect", ball_family)
  if ball == nil then
    error("basketball: cannot spawn the ball")
  end
  local shadow_family = og.family_id("effect", "modes:bshadow") --[[@as integer]] -- same contract as the ball family lookup above
  local shadow = og.add_fx_ob("effect", shadow_family)
  if shadow == nil then
    error("basketball: cannot spawn the shadow")
  end
  og.mode_set(S.BALL_ENTITY, og.entity_id(ball))
  og.mode_set(S.SHADOW_ENTITY, og.entity_id(shadow))
  -- The camera follows the SHADOW, not the ball: the ball draws lifted by
  -- up to ~40px of fake Z, so a ball camera would bob through every shot
  -- arc. The shadow is the ground truth on the floor — the same reason
  -- beacon 0 is the shadow. Set once; never re-asserted per tick.
  og.set_camera_view(0, shadow)
  spawn_hoops()
  center_reset(ball)
  og.set_mode_name("BASKETBALL")
  og.mode_set(S.ITEM_LAST, og.world_tick())
  og.mode_set(core.SLOT.PHASE, 1)
  -- The activation latch: written LAST, so hooks observe a fully
  -- initialized match or nothing.
  og.mode_set(core.SLOT.MODE_ID, core.MODE.BASKETBALL)
  core.announce("BASKETBALL! FIRST TO " .. og.mode_get(S.SCORE_LIMIT), C.SOUND_CHARGE)
end

-- Per-tick phases (post-act; the engine already ran the respawn timers
-- and owns the win-latch short-circuit). One oblist walk feeds every
-- consumer; a ball or shadow handle that fails to resolve skips all ball
-- logic this tick (§9 #13). row is the static manifest row make_hooks
-- closed over — the item respawner reads its authored pads (#225).
local function on_mode_tick(level, tick, row)
  local obs = og.oblist()
  match.schedule_dead(obs, og.mode_get(S.TEAM_MASK), og.mode_get(S.RESPAWN_TICKS))
  local livings = {}
  local gens = {}
  local spawn_count = { 0, 0, 0, 0, 0, 0, 0, 0 }
  local live_count = { 0, 0, 0, 0 }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      local order = w:order()
      if order == C.ORDER_LIVING then
        livings[#livings + 1] = w
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          live_count[team + 1] = live_count[team + 1] + 1
        end
        if caps.is_marked_spawn(w) then
          if team < 8 then
            spawn_count[team + 1] = spawn_count[team + 1] + 1
          end
        end
      elseif order == C.ORDER_GENERATOR then
        gens[#gens + 1] = w
      end
    end
  end
  local ball = og.find_by_id(og.mode_get(S.BALL_ENTITY))
  local shadow = og.find_by_id(og.mode_get(S.SHADOW_ENTITY))
  local handles = ball ~= nil
  if handles then
    handles = shadow ~= nil
  end
  if handles then
    run_ball_logic(ball, livings, live_count)
  end
  if og.mod(og.world_tick(), T.ai_cadence) == 0 then
    caps.apply_caps(gens, spawn_count, S.SPAWN_CAP)
    if handles then
      run_director(ball, livings)
    end
  end
  -- Post-death-scan, pre-HUD (the mode_items contract). The row arrives
  -- through the make_hooks closure, not mode_levels: unit fixtures
  -- register synthetic rows that never enter the manifest module.
  items.run(row, S.ITEM_CURSOR, S.ITEM_LAST, T.item_interval)
  run_win_check(tick, handles)
  update_hud()
  run_hoop_anims()
  if handles then
    sync_render(ball, shadow)
  end
end

-- One hook table per level, closing over its manifest row (static
-- authored data, read-only — not the mutable upvalue state R6 bans).
local function make_hooks(row)
  return {
    on_mode_init = function(level)
      on_mode_init(level, row)
    end,
    on_mode_tick = function(level, tick)
      on_mode_tick(level, tick, row)
    end,
    on_respawn = on_respawn,
    on_damage = on_damage,
  }
end

return {
  S = S,
  T = T,
  make_hooks = make_hooks,
}
