-- core:ghost_scare — effect behavior hooks transliterated from
-- effect_family_ghost_scare.cpp. Cookbook (docs/lua-classpacks-design.md §3)
-- applies: og.div for integer division, setters narrow like the C++ field
-- types, og.rand preserves draw order and count.
--
-- on_death applies the fright through walker:s_force_fright(iterations,
-- dx, dy) — statistics::force_fright, the scare-MERGE variant of
-- force_command that inspects the command queue front (forced +
-- COMMAND_WALK → refresh count/direction instead of prepending).
-- s_force_command is NOT a substitute: it stacks overlapping scares
-- end-to-end, which is exactly what runaway-specials §3.2 removed.

local C = og.C

-- ghost_scare_on_act: the scare cloud rides its caster for its whole life.
local function on_act(self)
  local owner = self:owner()
  if owner then
    self:center_on(owner)
  end
  return false  -- delegate to effect::act default animate/die path
end

-- ghost_scare_on_death: when the cloud expires, every living foe inside the
-- caster's scare radius is pushed into a forced flee-walk away from the cloud.
local function on_death(self)
  local owner = self:owner()
  -- dead() hands back the C++ int, and 0 is TRUTHY in Lua: the comparison
  -- against 0 is what makes this the `!self->owner()->dead()` test.
  if not owner or owner:dead() ~= 0 then
    return false
  end

  -- Runaway-specials §2.3: radius capped at 250 px (legacy 50 + 10*L goes
  -- off-screen at L27); binds only at L21+, draw-free. og.scare_radius is the
  -- binding for og::combat::scare_radius (core/combat_math.h).
  local foelist, howmany =
    og.find_foes_in_range("ob", og.scare_radius(owner:s_level()), owner)
  if howmany < 1 then
    return false
  end

  for i = 1, #foelist do
    local w = foelist[i]
    if w and w:order() == C.ORDER_LIVING then
      -- xpos/ypos are shorts: the sign collapse is C integer division.
      local tempx = w:xpos() - self:xpos()
      if tempx ~= 0 then
        tempx = og.div(tempx, math.abs(tempx))
      end
      local tempy = w:ypos() - self:ypos()
      if tempy ~= 0 then
        tempy = og.div(tempy, math.abs(tempy))
      end
      -- Runaway-specials §2.2: duration soft-capped (legacy 25*L, knee 325 =
      -- L13, ceiling 375) BEFORE the legacy myguy-only constitution resist
      -- draw — draw order/count identical, and both scare goldens (L1 = 25,
      -- L5 = 125) sit below the knee.
      local generic = og.scare_duration(owner:s_level())
      if w:has_guy() then
        -- rng_.next(0) returns 0 without advancing the stream; og.rand(0)
        -- raises, so the zero bound is guarded.
        local con = w:g_constitution()
        local roll = 0
        if con > 0 then
          roll = og.rand(con)
        end
        generic = generic - roll
      end
      if generic > 0 then
        w:s_force_fright(generic, tempx, tempy)
      end
    end
  end
  return true
end

og.register_hooks("fx", "core:ghost_scare", {
  on_act = on_act,
  on_death = on_death,
})
