-- westlands.fire — THE COMPANY FIRE: the scripted camp between levels — road, quartermaster, ledger — over og.register_campaign_hooks (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Every page is a pure function of the campaign providers (save cursor,
-- completed set, campaign state, wallet); the picker session owns all
-- navigation and every cost debit. The four decision keys the sim can
-- read are registered as vars below; delve_sunk is deliberately NOT a
-- var — the river keeps it, and the world never hears of it.

local road = og.use("road")
local stanzas = og.use("stanzas")

-- The offer caps and prices, in one place.
local PROVISION_CAP = 3
local PROVISION_PRICE = 300
local WATCH_PRICE = 900
local BREAD_PRICE = 60
local DELVE_GRANT = 800

-- ---------------------------------------------------------------------------
-- THE COMPANY FIRE (the root page)
-- ---------------------------------------------------------------------------

local function camp_line(level)
  local rows = stanzas.CAMP
  for i = 1, #rows do
    local row = rows[i]
    if level >= row.min and level <= row.max then
      return row.line
    end
  end
  return stanzas.CAMP_FALLBACK
end

local function milestone_line()
  if og.campaign_level_completed(24) then
    return "The mountain is behind you."
  end
  if og.campaign_level_completed(12) then
    return "The falls are behind; no road back."
  end
  if og.campaign_level_completed(6) then
    return "The mountain gate shut behind you."
  end
  if og.campaign_level_completed(3) then
    return "The ford is held and crossed."
  end
  return nil
end

local function root_page()
  local title = "THE COMPANY FIRE"
  local lines = {}
  if og.campaign_level_completed(26) then
    title = stanzas.LOOP_TITLE
    lines[#lines + 1] = stanzas.LOOP_LINE
  end
  lines[#lines + 1] = camp_line(og.campaign_current_level())
  local milestone = milestone_line()
  if milestone ~= nil then
    lines[#lines + 1] = milestone
  end
  return {
    title = title,
    lines = lines,
    entries = {
      { id = "road", label = "TAKE THE ROAD", kind = "page" },
      { id = "stores", label = "QUARTERMASTER", kind = "page" },
      { id = "ledger", label = "THE LEDGER", kind = "page" },
    },
  }
end

-- ---------------------------------------------------------------------------
-- THE ROAD (level select in the fiction's words)
-- ---------------------------------------------------------------------------

local function road_page()
  local here = road.ROAD[og.campaign_current_level()]
  if here == nil then
    return { title = "THE ROAD", lines = { "The road is dark." } }
  end
  local entries = {}
  for i = 1, #here.fwd do
    local exit = here.fwd[i]
    entries[#entries + 1] = {
      id = "go" .. exit.level,
      label = exit.label,
      kind = "level",
      level = exit.level,
      note = exit.note,
    }
  end
  for i = 1, #here.back do
    local exit = here.back[i]
    entries[#entries + 1] = {
      id = "back" .. exit.level,
      label = exit.label,
      kind = "level",
      level = exit.level,
    }
  end
  return {
    title = "THE ROAD",
    lines = { "You stand at " .. here.here .. "." },
    entries = entries,
  }
end

-- ---------------------------------------------------------------------------
-- QUARTERMASTER (the shop: every offer one-shot or capped, gate-narrated)
-- ---------------------------------------------------------------------------

-- The delve pair's shared window: after the vault (9), before the river
-- is crossed (11), and only while neither choice has been made.
local function delve_offer_open()
  if not og.campaign_level_completed(9) then
    return false
  end
  if og.campaign_level_completed(11) then
    return false
  end
  if og.campaign_state_get("delve_counted") ~= 0 then
    return false
  end
  if og.campaign_state_get("delve_sunk") ~= 0 then
    return false
  end
  return true
end

-- The bread window: the marsh nights when Sneak guides the company.
local function bread_offer_open()
  if not og.campaign_level_completed(19) then
    return false
  end
  if og.campaign_level_completed(21) then
    return false
  end
  if og.campaign_state_get("sneak_bread") ~= 0 then
    return false
  end
  return true
end

local function stores_page()
  local entries = {}
  local tier = og.campaign_state_get("provisions")
  if tier < PROVISION_CAP then
    entries[#entries + 1] = {
      id = "provisions",
      label = "PROVISION PACKS",
      kind = "action",
      cost = PROVISION_PRICE,
      note = "tier " .. (tier + 1) .. " of 3",
    }
  end
  local watch_owed = og.campaign_level_completed(7)
  if og.campaign_state_get("watch_paid") ~= 0 then
    watch_owed = false
  end
  if watch_owed then
    entries[#entries + 1] = {
      id = "watch_pay",
      label = "THE WATCH'S PAY",
      kind = "action",
      cost = WATCH_PRICE,
      note = "wages owed north",
    }
  end
  if delve_offer_open() then
    entries[#entries + 1] = {
      id = "delve_count",
      label = "COUNT THE DELVE GOLD",
      kind = "action",
      cost = 0,
      note = "a debt, maybe",
    }
    entries[#entries + 1] = {
      id = "delve_sink",
      label = "SINK IT IN THE RIVER",
      kind = "action",
      cost = 0,
      note = "the river keeps it",
    }
  end
  if bread_offer_open() then
    entries[#entries + 1] = {
      id = "sneak_bread",
      label = "BREAD FOR SNEAK",
      kind = "action",
      cost = BREAD_PRICE,
      note = "a small kindness",
    }
  end
  return {
    title = "QUARTERMASTER",
    lines = { "He deals in wages, bread, and weight." },
    entries = entries,
  }
end

-- ---------------------------------------------------------------------------
-- THE LEDGER (lines only; Back is the way home)
-- ---------------------------------------------------------------------------

local function ledger_page()
  local lines = {}
  if og.campaign_state_get("watch_paid") ~= 0 then
    lines[#lines + 1] = "The Watch drinks to your name."
  end
  if og.campaign_state_get("delve_counted") ~= 0 then
    lines[#lines + 1] = "The delve gold rides with us."
  end
  if og.campaign_state_get("delve_sunk") ~= 0 then
    lines[#lines + 1] = "The river keeps what you would not."
  end
  local tier = og.campaign_state_get("provisions")
  if tier > 0 then
    local word = stanzas.TIER_WORDS[og.min(tier, PROVISION_CAP)]
    lines[#lines + 1] = "Packs at tier " .. word .. ". Heavy, and glad."
  end
  if og.campaign_state_get("sneak_bread") ~= 0 then
    lines[#lines + 1] = "Sneak ate at your fire."
  end
  -- The two roads are recounted independently: 24 is reachable from both,
  -- and a company that walked both hears both.
  if og.campaign_level_completed(13) then
    lines[#lines + 1] = "West, we rode to the horns of war."
  end
  if og.campaign_level_completed(19) then
    lines[#lines + 1] = "East, we walked the drowned road."
  end
  if #lines == 0 then
    lines[1] = "The ledger lies open, and blank."
  end
  return { title = "THE LEDGER", lines = lines }
end

-- ---------------------------------------------------------------------------
-- Actions (the session owns every cost debit before these dispatch; each
-- action re-checks its own one-shot state so a direct dispatch can never
-- double-set a key or double-grant the delve gold)
-- ---------------------------------------------------------------------------

local function buy_provisions()
  local tier = og.campaign_state_get("provisions")
  if tier >= PROVISION_CAP then
    return { message = "The packs are full." }
  end
  og.campaign_state_set("provisions", tier + 1)
  local word = stanzas.TIER_WORDS[tier + 1]
  return { message = "Packs at tier " .. word .. ". Heavy, and glad." }
end

local function pay_watch()
  if og.campaign_state_get("watch_paid") ~= 0 then
    return { message = "The chest is gone north already." }
  end
  og.campaign_state_set("watch_paid", 1)
  return { message = "The chest goes north. Paid in full." }
end

local function count_delve()
  if not delve_offer_open() then
    return { message = "The counting is done with." }
  end
  og.campaign_grant_gold(DELVE_GRANT)
  og.campaign_state_set("delve_counted", 1)
  return { message = "Counted. Eight hundred, and a debt." }
end

local function sink_delve()
  if not delve_offer_open() then
    return { message = "The river is past." }
  end
  og.campaign_state_set("delve_sunk", 1)
  return { message = "The river takes it, and tells no one." }
end

local function buy_bread()
  if og.campaign_state_get("sneak_bread") ~= 0 then
    return { message = "He has eaten." }
  end
  og.campaign_state_set("sneak_bread", 1)
  return { message = "He eats in silence, and says nothing." }
end

-- ---------------------------------------------------------------------------
-- Registration (the one og.register_campaign_hooks of this campaign)
-- ---------------------------------------------------------------------------

og.register_campaign_hooks({
  vars = { "watch_paid", "delve_counted", "provisions", "sneak_bread" },
  picker_menu = function(page_id)
    if page_id == "" then
      return root_page()
    end
    if page_id == "road" then
      return road_page()
    end
    if page_id == "stores" then
      return stores_page()
    end
    if page_id == "ledger" then
      return ledger_page()
    end
    return nil
  end,
  picker_action = function(entry_id)
    if entry_id == "provisions" then
      return buy_provisions()
    end
    if entry_id == "watch_pay" then
      return pay_watch()
    end
    if entry_id == "delve_count" then
      return count_delve()
    end
    if entry_id == "delve_sink" then
      return sink_delve()
    end
    if entry_id == "sneak_bread" then
      return buy_bread()
    end
    return nil
  end,
})
