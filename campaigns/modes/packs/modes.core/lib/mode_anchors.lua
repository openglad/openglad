-- modes lib: mode_anchors — the soccer/onslaught spelling of the shared anchor toolkit: placement and bot seeding delegate to mode_match's single copy (style S4), plus the score-team read (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local match = og.use("mode_match")

local BOT_SQUAD = { "core:soldier", "core:archer", "core:elf", "core:mage", "core:thief" }

-- Shared header-band slot (mode_match's MATCHED precedent: slots 0-7 are
-- mode-neutral by convention; MATCHED owns 2-5, this claims 6): the
-- per-match squad order code + 1, latched by the FIRST squad spawn of the
-- match (init backfill, or the first wiped-team revive when every team
-- authored livings). 0 = not yet drawn.
local SQUAD_SEED = 6
local SQUAD_ORDERS = 120 -- 5! orders of BOT_SQUAD

local function squad_code()
  local seed = og.mode_get(SQUAD_SEED)
  if seed == 0 then
    seed = og.rand(SQUAD_ORDERS) + 1
    og.mode_set(SQUAD_SEED, seed)
  end
  return seed - 1
end

-- Decode a code in 0..119 into a permutation of BOT_SQUAD (inside-out
-- swap-remove — a bijection onto the 120 orders, pure integer arithmetic,
-- zero RNG): pick index (code mod n) from the shrinking pool, then swap
-- the pool's last entry into the hole.
local function shuffled_squad(code)
  local pool = {}
  for k = 1, #BOT_SQUAD do
    pool[k] = BOT_SQUAD[k]
  end
  local squad = {}
  for n = #BOT_SQUAD, 1, -1 do
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
local function spawn_bot_squad(team, cursor_slot)
  match.spawn_bots(team, shuffled_squad(squad_code()), cursor_slot)
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
