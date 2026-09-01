-- modes lib: mode_anchors — the soccer/onslaught spelling of the shared anchor toolkit: placement and bot seeding delegate to mode_match's single copy (style S4), plus the score-team read (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local match = og.use("mode_match")

-- The stock five-bot squad (D35, soldier-first) lives in the shared core
-- pack with the rest of the match machinery (docs/lineup-design.md C1);
-- the seeded shuffle below is the modes' own spelling of fielding it.
local BOT_SQUAD = og.use("core:lineup").BOT_SQUAD

-- MUDBOWL and THE CAUSEWAY put standable pitch on opposite sides of open
-- water. Automatic bots must never introduce a teleporter there: the hunt
-- AI beelines after a blink and can grind forever on the intervening shore.
-- Keep the ordinary five-family balance shape, but trade the mage for the
-- non-summoning orc. Human roster mages remain legal and player-controlled.
-- This selection lives at the one squad seam so init fills and wiped-team
-- recovery cannot disagree about the roster.
local WATER_BOT_SQUAD = {
  "core:soldier", "core:archer", "core:elf", "core:orc", "core:thief",
}

local function level_needs_nonteleporting_bots()
  local level = og.level_id()
  return level == 821 or level == 829
end

-- Shared header-band slot (mode_match's MATCHED precedent: slots 0-7 are
-- mode-neutral by convention; MATCHED owns 2-5, this claims 6): the
-- per-match squad order code + 1, latched by the FIRST squad spawn of the
-- match (init backfill, or the first wiped-team revive when every team
-- authored livings). 0 = not yet drawn.
local SQUAD_SEED = 6
local SQUAD_ORDERS = 120 -- 5! orders of either five-family source

local function squad_code()
  local seed = og.mode_get(SQUAD_SEED)
  if seed == 0 then
    seed = og.rand(SQUAD_ORDERS) + 1
    og.mode_set(SQUAD_SEED, seed)
  end
  return seed - 1
end

-- Decode a code in 0..119 into a permutation of the selected source (inside-out
-- swap-remove — a bijection onto the 120 orders, pure integer arithmetic,
-- zero RNG): pick index (code mod n) from the shrinking pool, then swap
-- the pool's last entry into the hole.
local function shuffled_squad(code, source)
  local pool = {}
  for k = 1, #source do
    pool[k] = source[k]
  end
  local squad = {}
  for n = #source, 1, -1 do
    local pick = og.mod(code, n) + 1
    code = og.div(code, n)
    squad[#squad + 1] = pool[pick]
    pool[pick] = pool[n]
  end
  return squad
end

-- Five-bot squad for an active team that fields no livings (the CTF squad
-- families over mode_match's spawner), in the match's seeded order (#235):
-- the order varies match to match, so a small matched roster faces a
-- varying subset instead of the fixed soldier-first prefix, and every
-- wiped-team revive decodes the SAME latched code back to the same squad.
-- cap is the caller's hard shape (basketball's 5v5 — lineup §3.2), handed
-- through to the one squad seam; nil for every other caller.
local function spawn_bot_squad(team, cursor_slot, cap)
  local source = BOT_SQUAD
  if level_needs_nonteleporting_bots() then
    source = WATER_BOT_SQUAD
  end
  match.spawn_bots(team, shuffled_squad(squad_code(), source),
                   cursor_slot, nil, cap)
end

-- The walker's scoring team: the banked pre-charm team when one exists.
local function score_team(w)
  local team = w:real_team_num()
  if team == 255 then
    team = w:team_num()
  end
  return team
end

return {
  place_at_anchor = match.place_at_anchor,
  spawn_bot_squad = spawn_bot_squad,
  score_team = score_team,
}
