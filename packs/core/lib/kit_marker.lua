-- core lib: kit_marker — the invisible helper entity the New Specials kits hang timers and windows on (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local FX_KIT_MARKER = assert(og.family_id("fx", "core:kit_marker"))

local M = {}

-- The roles a marker can play, stored in its ani_type byte. The engine never
-- animates an effect whose on_act answers true, so the byte is free, and it
-- rides every snapshot. The numbers live in one place, the engine's
-- kit_marker_role.h, because the HUD's countdown labels a marker by its role.
M.BURROW = C.MARKER_BURROW
M.LEGION = C.MARKER_LEGION
M.IMMOLATION = C.MARKER_IMMOLATION
M.PHASE_VEIL = C.MARKER_PHASE_VEIL
M.METEOR_RAIN = C.MARKER_METEOR_RAIN
M.WARD = C.MARKER_WARD
M.REST = C.MARKER_REST

-- role -> handler(marker). A dispatch table of functions, filled only while
-- the kit libs load (each kit lib registers its own handlers), never written
-- from a hook, so after loading it is a constant like a resolved family id.
M.roles = {}

-- Called by a kit lib at load time. A role registered twice is a load error:
-- two kits answering one role would make the winner depend on load order.
function M.register(role, handler)
  assert(M.roles[role] == nil, "kit_marker: role registered twice")
  M.roles[role] = handler
end

-- A fresh marker on the owner's floor and spot, owned by it, on its team.
-- lifetime 0 means the role's handler never counts down. The spawn tick is
-- stamped in lineofsight (unused by a marker) so age() works with or without
-- a countdown. Returns the marker, or nil when the world is full.
function M.spawn(owner, role, lifetime)
  local marker = og.summon(owner, "fx", FX_KIT_MARKER)
  if not marker then
    return nil
  end
  marker.ani_type = role
  marker:set_ignore(1)
  marker.lifetime = lifetime
  marker:set_lineofsight(og.world_tick())
  return marker
end

-- core:kit_marker on_act. A marker whose role has no handler (a kit that is
-- not loaded) dies at once; a handler decides everything else, including
-- killing the marker when its owner is gone.
function M.on_act(self)
  local handler = M.roles[self:ani_type()]
  if handler then
    handler(self)
    return true
  end
  M.finish(self)
  return true
end

-- A marker's end: every kit's role handlers end their markers through here.
function M.finish(marker)
  marker.dead = 1
  marker:death()
end

-- The owner's live marker playing `role`, or nil; with `level`, only one
-- whose level is that (a rest's slot). Markers are summoned, and summon puts
-- every non-weapon in the oblist, so that is the list to scan.
function M.find(owner, role, level)
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_FX
        and ob:family() == FX_KIT_MARKER
        and ob:dead() == 0
        and ob:ani_type() == role
        and ob:owner() == owner then
      if level == nil then
        return ob
      end
      if ob.level == level then
        return ob
      end
    end
  end
  return nil
end

-- Ticks since the marker was spawned (the second-press latch reads this).
function M.age(marker)
  return og.world_tick() - marker:lineofsight()
end

-- ------------------------------------------------------------------ rests

-- Ticks left of the owner's rest on `slot`, 0 when it has none.
function M.rest_left(owner, slot)
  local marker = M.find(owner, M.REST, slot)
  if marker then
    return marker:lifetime()
  end
  return 0
end

-- Starts the owner's rest on `slot`, `ticks` long. A full world leaves the
-- special unrested rather than refusing the cast that already happened.
function M.start_rest(owner, slot, ticks)
  local marker = M.spawn(owner, M.REST, ticks)
  if marker then
    marker.level = slot
  end
end

-- A rest counts down while it acts, which is every tick, a frozen caster
-- and stopped time included (only enemy livings and generators are held),
-- and ends with its last tick or with its caster.
local function rest_act(marker)
  local owner = marker:owner()
  if not owner or owner:dead() ~= 0 then
    M.finish(marker)
    return
  end
  marker.lifetime = marker:lifetime() - 1
  if marker:lifetime() <= 0 then
    M.finish(marker)
  end
end

M.register(M.REST, rest_act)

-- The declarations that reference this module (packs/core/families/):
--   core:kit_marker  on_act = kit_marker.on_act
return M
