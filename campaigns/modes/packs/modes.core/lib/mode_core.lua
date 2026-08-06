-- modes shared toolkit — slot/header conventions, team masks + the activation clamp, packed positions, announce/win/HUD/scrub helpers, manifest registration (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C

-- Well-known mode-var header (slots 0-7; 8+ are mode-private — each mode's
-- map lives with its impl module). MODE_ID doubles as the activation latch:
-- every impl writes it LAST in on_mode_init, so family hooks (flag on_eat)
-- can gate on it exactly like the C++ hooks gated on ctf.active.
local SLOT = {
  MODE_ID = 0,
  PHASE = 1,
}

-- SLOT.MODE_ID values (0 = none).
local MODE = {
  TDM = 1,
  CTF = 2,
  ONSLAUGHT = 3,
  SOCCER = 4,
  MUTANT = 5,
}

-- Packed pixel positions: one int32 mode var carries (x, y), each within
-- [0, 4095] (a 255-tile grid tops out at 4080px).
local function pos_pack(x, y)
  return x * 4096 + y
end

local function pos_x(v)
  return og.div(v, 4096)
end

local function pos_y(v)
  return og.mod(v, 4096)
end

-- Team bitmask helpers over the 4 score teams (integer arithmetic only —
-- no float exponentiation, no bitwise operators).
local TEAM_BIT = { 1, 2, 4, 8 }

local function mask_has(mask, team)
  return og.mod(og.div(mask, TEAM_BIT[team + 1]), 2) == 1
end

local function mask_add(mask, team)
  if mask_has(mask, team) then
    return mask
  end
  return mask + TEAM_BIT[team + 1]
end

local function mask_count(mask)
  local count = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if mask_has(mask, team) then
      count = count + 1
    end
  end
  return count
end

-- The team-activation clamp (og.effective_team_mask's rule applied to an
-- arbitrary authored domain — CTF activates flag teams, not marker teams):
-- requested <= 0 takes every authored team; otherwise the first
-- clamp(requested, 2, 4) authored teams in index order.
local function activate_teams(authored_mask, requested)
  if requested <= 0 then
    return authored_mask
  end
  local remaining = og.clamp(requested, 2, C.SCORE_TEAM_COUNT)
  local mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if remaining > 0 then
      if mask_has(authored_mask, team) then
        mask = mask + TEAM_BIT[team + 1]
        remaining = remaining - 1
      end
    end
  end
  return mask
end

-- Notification + sound pair (every mode announcement is both).
local function announce(text, sound)
  og.emit_notification(text)
  og.emit_sound(sound)
end

-- Match decided: the "{COLOR} TEAM WINS!" announcement (17 chars worst
-- case, inside the 25-char budget) then the engine win latch, which
-- flush-revives, computes winner_is_player and re-asserts every tick.
local function declare_team_win(team)
  announce(og.team_color_name(team) .. " TEAM WINS!", C.SOUND_CHARGE)
  og.declare_winner(team)
end

-- Kill the fresh stain/life-gem drops under a corpse that will never
-- respawn (og.respawn_schedule scrubs for scheduled corpses already;
-- TDM/Onslaught call this for permanent bodies to keep cleric resurrects
-- and score farming away from them).
local function scrub_corpse(corpse)
  og.scrub_corpse_stain(corpse:xpos(), corpse:ypos(), corpse:floor())
end

-- One HUD score row per team: "RED 2/3" (max "YELLOW 999/999" = 14 chars,
-- inside the 25-char row budget), tinted with the team ramp.
local function hud_score_line(slot, team, score, limit)
  og.set_hud_line(slot, og.team_color_name(team) .. " " .. score .. "/" .. limit, team)
end

-- The D9 level-id band the manifest may populate (mirrors mode_match).
local FIRST_LEVEL_ID = 300
local LAST_LEVEL_ID = 899

-- Register one mode's hook table for every manifest row carrying its name.
-- Accepts either a row ARRAY ({ id = level_id, mode = "ctf", ... } entries)
-- or the og.use("mode_levels") module itself — an id-keyed map with holes,
-- scanned over the D9 band because the sandbox has no pairs.
local function register_mode(levels, mode_name, hooks)
  local rows = levels
  if levels.levels ~= nil then
    rows = {}
    for id = FIRST_LEVEL_ID, LAST_LEVEL_ID do
      local row = levels.levels[id]
      if row ~= nil then
        rows[#rows + 1] = { id = id, mode = row.mode }
      end
    end
  end
  for i = 1, #rows do
    local row = rows[i]
    if row.mode == mode_name then
      og.register_level_hooks(row.id, hooks)
    end
  end
end

return {
  SLOT = SLOT,
  MODE = MODE,
  TEAM_BIT = TEAM_BIT,
  pos_pack = pos_pack,
  pos_x = pos_x,
  pos_y = pos_y,
  mask_has = mask_has,
  mask_add = mask_add,
  mask_count = mask_count,
  activate_teams = activate_teams,
  announce = announce,
  declare_team_win = declare_team_win,
  scrub_corpse = scrub_corpse,
  hud_score_line = hud_score_line,
  register_mode = register_mode,
}
