-- core:ghost_scare — expiring scare cloud frights nearby foes (cookbook: docs/lua-classpacks-design.md §3).
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
  local foes, foe_count =
    og.find_foes_in_range("ob", og.scare_radius(owner.level), owner)
  if foe_count < 1 then
    return false
  end

  for i = 1, #foes do
    local foe = foes[i]
    if foe and foe:order() == C.ORDER_LIVING then
      local flee_dx = og.sign(foe:xpos() - self:xpos())
      local flee_dy = og.sign(foe:ypos() - self:ypos())
      -- Runaway-specials §2.2: duration soft-capped (legacy 25*L, knee 325 =
      -- L13, ceiling 375) BEFORE the legacy myguy-only constitution resist
      -- draw — draw order/count identical, and both scare goldens (L1 = 25,
      -- L5 = 125) sit below the knee.
      local fright = og.scare_duration(owner.level)
      if foe:has_guy() then
        local con_roll = og.rand0(foe:g_constitution())
        fright = fright - con_roll
      end
      if fright > 0 then
        foe:s_force_fright(fright, flee_dx, flee_dy)
      end
    end
  end
  return true
end

og.register_hooks("fx", "core:ghost_scare", {
  on_act = on_act,
  on_death = on_death,
})
