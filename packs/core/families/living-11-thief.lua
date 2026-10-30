-- core:thief — bomb, cloak, taunt/charm, poison cloud (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The shareware-era source marked Drop Bomb "unregistered", while Cloak
-- and Taunt were "Registered."

local C = og.C
local lc = og.use("living_common")
local FX_BOMB = assert(og.family_id("fx", "core:bomb"))
local FX_CLOUD = assert(og.family_id("fx", "core:cloud"))
local kt = og.use("kit_thief")
local FX_MINE = assert(og.family_id("fx", "core:mine"))
local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local function check_special_ai(self)
  if self:current_special() == 1 then
    if gf.on() or og.match_setting("new_specials") ~= 0 then
      local reach = G.blast_reach(self.level)
      local clear = G.blast_clear(self, self, reach, false)
      if og.match_setting("new_specials") ~= 0 then
        -- New Specials: a MINE pays where a foe will walk: a foe past the
        -- blast and within 130 px that is hunting the thief, fewer than
        -- three mines down, no ally in the blast, and the 70 in hand.
        -- Never the coin.
        local hunter = G.hunted_by(self, 130)
        local mines = G.owned(self, C.ORDER_FX, FX_MINE)
        if clear and hunter
            and self:distance_to_ob(hunter) > reach
            and mines < 3
            and G.can_pay(self, 1, true) then
          self:set_shifter_down(1)
          return true
        end
        self:set_shifter_down(0)
      end
      if gf.on() then
        -- Gameplay Fixes: a bomb pays with a foe inside its blast and no
        -- ally in it; the Shift is 0 whenever the setting is on (a MINE
        -- not in play is cast as the bomb anyway).
        self:set_shifter_down(0)
        return G.foes_within(self, 1, reach, true) and clear
      end
    end
    -- Drop-bomb AI uses this fixed per-tick range.
    local foe = self:foe()
    if foe then
      local distance = self:distance_to_ob(foe)
      -- about 6 squares max, 2 min
      if distance < 130 and distance > 35 then
        return false
      end
    else
      local _, foe_count = og.find_foes_in_range("ob", 110, self)
      return foe_count >= 3
    end
    return true -- fallthrough for foe case when distance is acceptable
  elseif self:current_special() == 3 then
    local t = og.tuning(self)
    if gf.on() then
      -- TAUNT and CHARM refuse a busy thief (the casts' guard).
      if lc.is_busy(self) then
        return false
      end
      -- Gameplay Fixes: CHARM only with a foe the thief can take inside
      -- charm reach, else TAUNT; the coin is drawn either way.
      local reach = t.charm_range_base + t.charm_range_per_level * self.level
      local foes = og.find_foes_in_range("ob", reach, self)
      if gf.nearest_charmable(self, foes, self.level) then
        self:set_shifter_down(1)
      else
        self:set_shifter_down(0)
        -- Gameplay Fixes: TAUNT wants a living foe in its reach and an ally
        -- there too: foes pulled onto a thief nobody backs is the thief's
        -- own danger, bought for 100. A tent cannot follow the thief.
        reach = t.taunt_range_base + t.taunt_range_per_level * self.level
        if not G.foes_within(self, 1, reach, false) then
          return false
        end
        return G.allies_within(self, 1, reach)
      end
    end
    local range
    if self:shifter_down() == 0 then
      range = t.taunt_range_base + t.taunt_range_per_level * self.level
    else
      range = t.charm_range_base + t.charm_range_per_level * self.level
    end
    local _, foe_count = og.find_foes_in_range("ob", range, self)
    return foe_count >= 1
  elseif gf.on() then
    if self:current_special() == 2 then
      -- Gameplay Fixes: CLOAK wants a foe hunting the thief and no cloak
      -- left.
      if self:invisibility_left() ~= 0 then
        return false
      end
      return G.hunted_by(self, 130) ~= nil
    end
    -- Gameplay Fixes: POISON CLOUD wants two living foes at blows and no
    -- cloud of the thief's alive, and the thief not busy (the cast's guard).
    if lc.is_busy(self) or G.owned(self, C.ORDER_FX, FX_CLOUD) > 0 then
      return false
    end
    return #G.foes_at_blows(self) >= 2
  end
  return true -- default: go for it
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 12, 4, 8, 1)
end

local function drop_bomb(self)
  local bomb = og.add_ob("fx", FX_BOMB)
  if not bomb then
    return false, "COULD NOT CREATE BOMB"
  end
  bomb.ani_type = C.ANI_BOMB
  if self:has_guy() then
    self:g_set_total_shots(self:g_total_shots() + 1)
    self:g_set_scen_shots(self:g_scen_shots() + 1)
  end
  bomb.damage = og.combat.bomb_damage(self.level)
  bomb:set_floor(self:floor()) -- bomb is armed on the thief's floor
  bomb:setxy(self:xpos() + self:sizex() // 2
               - bomb:sizex() // 2,
             self:ypos() + self:sizey() // 2
               - bomb:sizey() // 2)
  bomb:set_owner(self)
  -- An AI thief flees its own armed bomb.
  if self:user() == -1 then
    local flee_dx = og.rand(3) - 1
    local flee_dy = og.rand(3) - 1
    if flee_dx == 0 and flee_dy == 0 then
      flee_dx = 1
    end
    self:s_force_command(C.COMMAND_WALK, 20, flee_dx, flee_dy)
  end
  return true
end

local function cloak(self)
  local t = og.tuning(self)
  local cur = self:invisibility_left()
  local gain = t.cloak_base + og.rand(t.cloak_roll_span) * self.level
  self:set_invisibility_left(og.combat.cloak_total(cur, gain))
  return true
end

local function taunt_or_charm(self)
  local t = og.tuning(self)
  if self:shifter_down() == 0 then
    -- normal taunt
    if lc.is_busy(self) then
      return false, "SPECIAL BUSY"
    end
    local foes = og.foes_in_range(
      self, t.taunt_range_base + t.taunt_range_per_level * self.level)
    if gf.on() and #foes == 0 then
      -- Gameplay Fixes: a taunt nobody hears costs nothing.
      return false, "NO FOE IN RANGE"
    end
    for i = 1, #foes do
      local foe = foes[i]
      -- Two RNG draws in one C++ expression; drawn left (self) first.
      -- (Bounds lean on the engine invariant level >= 1; the bare og.rand
      -- is the loud tripwire if that ever breaks.)
      local my_roll = og.rand(self.level)
      local foe_roll = og.rand(foe.level)
      if my_roll >= foe_roll then
        foe:set_foe(self)
        -- "A hack, yeah": taunted foes also follow the thief as leader.
        foe:set_leader(self)
        if foe:act_type() ~= C.ACT_CONTROL then
          foe:s_force_command(C.COMMAND_FOLLOW,
                              10 + og.rand(self.level), 0, 0)
        end
      end
    end
    og.emit_notification(og.entity_display_name(self, "THIEF")
                           .. ": 'Nyah Nyah!'")
    -- shim kept: busy is a C++ float: per-op float rounding.
    self:set_busy(og.fadd(self:busy(), 2.0))
    return true
  end
  -- charm opponent
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local foes, foe_count = og.find_foes_in_range(
    "ob", t.charm_range_base + t.charm_range_per_level * self.level, self)
  if foe_count < 1 then
    return false, "NO FOE IN RANGE"
  end
  local fixes = gf.on()
  local candidates = foes
  if fixes then
    -- Gameplay Fixes: the nearest foe the thief can take, not the first
    -- in list order.
    candidates = {}
    local nearest = gf.nearest_charmable(self, foes, nil)
    if nearest then
      candidates[1] = nearest
    end
  end
  local handled = 0
  local resisted = 0
  for i = 1, #candidates do
    if handled ~= 0 then
      break
    end
    local target = candidates[i]
    -- The old charm_left <= 10 gate was disabled; real_team_num is the
    -- remaining charm-history gate.
    if gf.charmable(target, fixes) then
      local level_diff = self.level - target.level
      if level_diff < 0 or og.rand(20) == 0 then
        -- A resisted charm gives the enemy a free attack.
        target:set_foe(self)
        target:attack(self)
        resisted = 1
      else
        target:set_real_team_num(target.team)
        target.team = self.team
        if self:foe() == target then
          target:set_foe(nil)
        else
          target:set_foe(self:foe())
        end
        -- The soft-knee formula is identity through level diff 12. Its
        -- knee and ceiling are engine combat policy, not pack tuning.
        target:set_charm_left(og.soften(
          t.charm_duration_base + level_diff * t.charm_duration_per_diff,
          375, 490))
        resisted = 0
      end
      handled = handled + 1
    end
  end
  if handled == 0 then
    return false, "NO VALID TARGET"
  end
  local message
  if resisted ~= 0 then
    message = og.entity_display_name(self, "Thief")
                .. " failed to charm!"
  else
    message = og.entity_display_name(self, "Thief")
                .. " charmed an opponent!"
  end
  og.emit_notification(message)
  -- shim kept: busy is a C++ float: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), 10.0))
  return true
end

local function poison_cloud(self)
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  local cloud = og.summon(self, "fx", FX_CLOUD)
  if not cloud then
    return false, "COULD NOT CREATE CLOUD"
  end
  -- shim kept: busy is a C++ float: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), 5.0))
  cloud:set_ignore(1)
  cloud.lifetime =
    t.cloud_lifetime_base + t.cloud_lifetime_per_level * self.level
  cloud:set_invisibility_left(t.cloud_invisibility)
  cloud.ani_type = C.ANI_SPIN
  cloud.damage = self.level
  return true
end

og.family("living", {
  id = "core:thief",
  wire_id = 11,
  name = "THIEF",
  short_name = og.NIL,
  stats  = { strength = 9, dexterity = 12, constitution = 12,
             intelligence = 10, armor = 5, level = 1 },
  combat = { hp = 75, melee_damage = 12, stepsize = 5,
             fire_delay = 5, fire_mp_cost = 1 },
  costs  = { hire = 400,
             train = { strength = 15, dexterity = 6, constitution = 9,
                       intelligence = 10, armor = 50, level = 200 } },
  specials = {
    { id = "drop_bomb",    name = "DROP BOMB",    mp_cost = 35,  alternate = { name = "MINE", mp_cost = 70, new_kit = true, detail = "Waits unseen for a foe.", icon = "icons/mine.png" }, cast = kt.bomb_or_mine(drop_bomb), detail = "Leave a burning bomb to explode and hurt the unwary, friend or foe!", icon = "icons/drop_bomb.png" },
    { id = "cloak",        name = "CLOAK",        mp_cost = 125, cast = cloak, detail = "Cloak yourself in the shadows, slipping past your enemies.", icon = "icons/cloak.png" },
    { id = "taunt_enemy",  name = "TAUNT ENEMY",  mp_cost = 100, alternate = { name = "CHARM OPPONENT", detail = "One foe fights for you.", icon = "icons/charm_opponent.png" }, cast = taunt_or_charm, detail = "Beckon your enemies to you with jeers, and confuse their attack.", icon = "icons/taunt_enemy.png" },
    { id = "poison_cloud", name = "POISON CLOUD", mp_cost = 150, detail = "Release a cloud of poisonous gas to roam at will and sicken your foes.", icon = "icons/poison_cloud.png" },
    default_cast = poison_cloud,  -- case 4 and every unmapped slot
  },
  default_weapon = "core:knife",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "THIEF KILLED",
  sprite = "thief.png",
  animation = "standard",
  ai_line_of_sight = 10,
  description = "Thieves are fast, though not so potent as the soldier. Thieves can throw small blades rapidly and damage whole groups of enemies with their bombs.",
  names = { "Shinobi", "Dismas", "Shadow", "Stabby", "Swiftstrike", "Scourge",
            "Rogue" },
  playable = true,
  playable_order = 7,
  glyph = "t",
  glyph_ascii = "t",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Taunt / charm reach (px): base + per-level
    taunt_range_base = 80,
    taunt_range_per_level = 4,
    charm_range_base = 16,
    charm_range_per_level = 4,
    -- Cloak: invisibility gain = base + rand(roll_span)*level
    cloak_base = 20,
    cloak_roll_span = 20,
    -- Charm duration fed to the engine soft-knee: base + per level-diff
    charm_duration_base = 75,
    charm_duration_per_diff = 25,
    -- Poison cloud: lifetime base + per-level, starting invisibility
    cloud_lifetime_base = 40,
    cloud_lifetime_per_level = 3,
    cloud_invisibility = 10,
  },

  check_special_ai = check_special_ai,
  level_up = level_up,
})
