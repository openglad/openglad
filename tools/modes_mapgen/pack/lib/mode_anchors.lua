-- modes lib: mode_anchors — anchor-rotation placement, bot-squad seeding and the score-team read shared by the soccer/onslaught impls (cursor slot supplied per mode; cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C

-- Anchor rotation (DECISIONS D1): the caller's mode-var cursor + eat-free
-- probes. Deterministic and zero-RNG on the respawn path; init-time bot
-- placement passes allow_teleport (the one RNG fallback, the CTF idiom).
local function place_at_anchor(w, team, cursor_slot, allow_teleport)
  local final_x = -1
  local final_y = 0
  if team >= 0 then
    if team < C.SCORE_TEAM_COUNT then
      local count = og.respawn_anchor_count(team)
      for _ = 1, count do
        local cursor = og.mode_get(cursor_slot)
        og.mode_set(cursor_slot, cursor + 1)
        local ax, ay = og.respawn_anchor(team, og.mod(cursor, count))
        if og.spawn_spot_clear(w, ax, ay) then
          final_x = ax
          final_y = ay
          break
        end
      end
    end
  end
  if final_x >= 0 then
    w:setxy(final_x, final_y)
    return true
  end
  if allow_teleport then
    return w:teleport()
  end
  return false
end

local BOT_SQUAD = { "core:soldier", "core:archer", "core:elf", "core:mage", "core:thief" }

-- Five-bot squad for an active team that fields no livings (the CTF init
-- idiom; the session difficulty percent picks the level).
local function spawn_bot_squad(team, cursor_slot)
  local level = og.max(1, og.div(og.match_setting("difficulty"), 100) + 1)
  for k = 1, #BOT_SQUAD do
    local fam = og.family_id("living", BOT_SQUAD[k])
    local w = og.add_ob("living", fam)
    if w ~= nil then
      w:set_team_num(team)
      w:set_real_team_num(255)
      w:s_set_level(level)
      w:set_difficulty(level)
      place_at_anchor(w, team, cursor_slot, true)
    end
  end
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
  place_at_anchor = place_at_anchor,
  spawn_bot_squad = spawn_bot_squad,
  score_team = score_team,
}
