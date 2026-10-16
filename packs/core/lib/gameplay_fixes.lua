-- core lib: gameplay_fixes — the Gameplay Fixes setting and the few helpers the fixed branches of the classic specials share (cookbook: docs/lua-classpacks-design.md §3).

local M = {}

-- The setting as this session plays it. A pure read: it draws nothing and
-- has no side effect, so a gate may ask it anywhere. 0 = the classic game,
-- bugs included (docs/gameplay-fixes.md).
function M.on()
  return og.match_setting("gameplay_fixes") ~= 0
end

return M
