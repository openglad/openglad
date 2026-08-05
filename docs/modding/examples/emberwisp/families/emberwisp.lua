-- example:emberwisp — ember pool, flare gate, stun-burst special 1 (cookbook: docs/lua-classpacks-design.md §3).
-- Ember Wisp — a worked example of a class pack that ships its OWN art and
-- its OWN motion, so nothing about it is borrowed from the core families.
--
-- This pack is deliberately NOT under packs/, because everything under
-- packs/ is mounted at startup and would push a new family into the picker,
-- the registries and the wire ids of the shipped game. Mount it explicitly
-- to try it:
--
--   og::resources::mount("docs/modding/examples/emberwisp",
--                        "packs/emberwisp/", 1);
--   og::resources::refresh_pack_scripts();
--
-- The mount point fixes the pack id ("emberwisp", the directory name under
-- packs/) and it is what makes the `sprite:` path below resolve — sprite
-- paths are virtual-filesystem paths, not pack-relative ones, so they start
-- at packs/<pack id>/.

og.pack{
  id = "emberwisp",
  version = "1",
  title = "Ember Wisp",
  authors = { "the OpenGlad project" },
}

-- Pack-defined animation tables. A row is looked up as
-- `anim_table[ani_type * 8 + curdir]`, i.e. 8 facings per ani_type; the
-- frames in a row are indices into the sprite's frame stack and playback
-- ends at the end of the row. `rows: 16` gives this family two ani_types:
-- 0 = ANI_WALK, 1 = ANI_ATTACK.
--
-- The wisp has no directional art, so all eight facings of an ani_type
-- share the same frame list — but each row is still written out, because
-- rows are expanded CYCLICALLY when fewer are declared than `rows` asks
-- for (two declared rows over `rows: 16` would alternate walk/attack per
-- facing, not fill eight of each).
og.anims("emberwisp_motion", {
  rows = 16,
  frames = {
    { 0, 1, 2, 3, 2, 1 },  -- ani_type 0 (walk), facing 0
    { 0, 1, 2, 3, 2, 1 },  --                    facing 1
    { 0, 1, 2, 3, 2, 1 },  --                    facing 2
    { 0, 1, 2, 3, 2, 1 },  --                    facing 3
    { 0, 1, 2, 3, 2, 1 },  --                    facing 4
    { 0, 1, 2, 3, 2, 1 },  --                    facing 5
    { 0, 1, 2, 3, 2, 1 },  --                    facing 6
    { 0, 1, 2, 3, 2, 1 },  --                    facing 7
    { 4, 5, 6, 7 },        -- ani_type 1 (attack), facing 0
    { 4, 5, 6, 7 },        --                      facing 1
    { 4, 5, 6, 7 },        --                      facing 2
    { 4, 5, 6, 7 },        --                      facing 3
    { 4, 5, 6, 7 },        --                      facing 4
    { 4, 5, 6, 7 },        --                      facing 5
    { 4, 5, 6, 7 },        --                      facing 6
    { 4, 5, 6, 7 },        --                      facing 7
  },
})

local C = og.C

-- Each wisp starts with a random slice of its ember already spent.
-- Exactly one draw, unconditional, so the RNG stream advances identically
-- on every peer (R4). The bound is a positive literal, so plain og.rand
-- is right — its error on n <= 0 is a tripwire worth keeping; og.rand0 is
-- for bounds that can legitimately reach zero (see flare_burst).
local function on_create(self)
  local spent = og.rand(16)
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fsub(self.max_magicpoints, spent)
  self.ani_type = C.ANI_WALK
end

-- Returning false blocks the shot: a wisp below the flare cost cannot
-- fire. One that can flares into ani_type 1 — rows 8..15 of this pack's
-- own animation table.
local function on_fire_weapon(self)
  if self.magicpoints < og.tuning(self).flare_cost then
    return false
  end
  self.ani_type = C.ANI_ATTACK
  self:set_cycle(0)
  return true
end

-- Special 1, FLARE BURST: vent every point of ember above the burn floor
-- as a stunning flash. Answering false means "did not fire" — the engine
-- then skips the special's descriptor MP cost.
local function flare_burst(self)
  local t = og.tuning(self)
  local ember = og.trunc(self.magicpoints) - t.burn_floor
  if ember <= 0 then
    return false
  end
  -- Tuning is modder data: clamp it into a sane sim window before use.
  local range = og.clamp(t.burst_range, C.GRID_SIZE, 320)
  local foes = og.foes_in_range(self, range)
  for i = 1, #foes do
    -- One stun roll per foe, in list order (R4). The bound is tuning-
    -- driven and may be zero: og.rand0 answers 0 then WITHOUT advancing
    -- the stream (IRandom::next(0) semantics).
    local roll = og.rand0(self.level * t.stun_per_level)
    -- add_frozen_stun is combat_math.stun_total fused with the setter;
    -- the thaw-immunity discard and the 150 cap are its policy, not ours.
    foes[i]:add_frozen_stun(t.stun_base + roll)
  end
  -- magicpoints and busy are C++ floats: per-op rounding.
  self.magicpoints = og.fsub(self.magicpoints, ember)
  self.busy = og.fadd(self:busy(), 4.0)
  og.emit_positional_sound(self, C.SOUND_EXPLODE)
  return true
end

og.family("living", {
  id = "example:emberwisp",
  -- `auto` takes the next free id above the core pins (>= 21) in a
  -- deterministic order, so every peer agrees on the wire byte.
  wire_id = "auto",
  name = "EMBERWISP",
  short_name = "WISP",
  -- Attribute scores a fresh recruit starts with. The picker prices
  -- hiring and training as deltas from these, at the per-point prices
  -- in costs.train below. Every member is required: there is no honest
  -- default for armor, so leaving it out is a parse error rather than
  -- a silently 0-armor class.
  stats = {
    strength = 8,
    dexterity = 14,
    constitution = 7,
    intelligence = 16,
    armor = 4,
    level = 1,  -- starting level; leave it at 1
  },
  -- What a spawned wisp is in the field. The loader stamps these
  -- straight onto the walker.
  combat = {
    hp = 70,  -- base hitpoints
    melee_damage = 11,
    stepsize = 5,  -- px per step (movement speed)
    fire_delay = 9,  -- busy ticks added AFTER each attack, so lower
                     -- is faster (the Lua accessor kept its older
                     -- name: self:fire_frequency())
    fire_mp_cost = 2,  -- MAGIC POINTS per ranged shot; a wisp below
                       -- this cannot fire at all. Gold lives in
                       -- costs: — nothing in combat: is money.
  },
  -- Gold, picker-side, and nothing else. train: is the price of one
  -- point on each stats axis.
  costs = {
    hire = 400,
    train = {
      strength = 30,
      dexterity = 8,
      constitution = 30,
      intelligence = 8,
      armor = 60,
      level = 200,  -- vestigial: levels are priced by the exp
                    -- curve and nothing reads this. New packs may
                    -- omit it; an omitted axis is 0.
    },
  },
  -- Up to five, in slot order: the first entry is slot 1, usable at
  -- level (N-1)*3+1. `cast` is this slot's handler, written into the same
  -- entry as the price it charges, and the engine resolves the `id` to a
  -- slot number once — so reordering this list can never silently re-bind
  -- a handler, a misspelling on either side is a load error naming the ids
  -- that do exist, and a bare slot number is refused. `default_cast`
  -- (absent here) catches every other slot; a declared slot with neither
  -- is a pack error unless it says `cast = false` and means it. The engine
  -- refuses the cast below mp_cost and deducts it only after the handler
  -- answers true. All of this replaces a hand-written current_special()
  -- ladder.
  specials = {
    { id = "flare_burst", name = "FLARE BURST", mp_cost = 5, cast = flare_burst },
  },
  default_weapon = "core:fireball",
  flags = { "FLYING", "FORESTWALK" },
  init_ani_type = 0,
  -- The wisp's ember pool. This is the live max-MP knob; the usual
  -- maximum is 10 + INT*3, and there is no combat: member for it.
  init_max_magicpoints = 40,
  leaves_bloodspot = false,
  magic_damage_modifier = 0.5,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "EMBERWISP GUTTERS OUT",
  -- A virtual-filesystem path under this pack's mount point. Core art
  -- keeps using bare names ("footman.png"), which resolve under pix/.
  sprite = "packs/emberwisp/sprites/emberwisp.png",
  -- Names the set declared above instead of one of the seven built-in
  -- tables (standard|mage|skeleton|giant_skeleton|slime|small_slime|
  -- static). Built-in names stay reserved and win, so a pack cannot
  -- shadow them by accident.
  animation = "emberwisp_motion",
  ai_line_of_sight = 9,
  description = "A knot of live coal that never learned to go out.\n" ..
                "Drifts over anything, burns what it touches, and\n" ..
                "keeps almost nothing in reserve.\n",
  names = { "Cinder", "Sparkfall", "Emberling", "Guttering", "Coalheart" },
  playable = true,
  playable_order = 90,
  -- Presentation for the character-cell clients and the radar.
  glyph = "✦",
  glyph_ascii = "*",
  glyph_color = "yellow",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 228,
  radar_jitter = 3,

  -- Balance constants the hooks above read back with og.tuning(self) —
  -- a frozen, read-only table. Integers arrive as Lua integers, decimals
  -- as floats, strings as strings. They sit a screen from the code they
  -- tune, which is the whole point of a family being one file.
  tuning = {
    -- on_fire_weapon: shots below this ember reserve are blocked.
    flare_cost = 6.0,
    -- FLARE BURST (special 1): vents ember above burn_floor, stunning
    -- foes within burst_range px for stun_base + rand0(level *
    -- stun_per_level) each.
    burn_floor = 10,
    burst_range = 80,
    stun_base = 4,
    stun_per_level = 8,
  },

  on_create = on_create,
  on_fire_weapon = on_fire_weapon,
})
