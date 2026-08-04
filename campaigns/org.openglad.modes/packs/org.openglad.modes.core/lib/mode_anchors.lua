-- modes lib: mode_anchors — the soccer/onslaught spelling of the shared anchor toolkit: placement and bot seeding delegate to mode_match's single copy (style S4), plus the score-team read (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local match = og.use("mode_match")

local BOT_SQUAD = { "core:soldier", "core:archer", "core:elf", "core:mage", "core:thief" }

-- Five-bot squad for an active team that fields no livings (the CTF squad
-- families over mode_match's spawner).
local function spawn_bot_squad(team, cursor_slot)
  match.spawn_bots(team, BOT_SQUAD, cursor_slot)
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
