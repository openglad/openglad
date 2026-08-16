-- longseason.ledger: campaign_book — Kettle's Book, the scripted picker: the ledger root, the season docket, the warm-coin ritual, stores and advances (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local D = og.use("ledger_data")

-- The whole book is state-derived: every page is a pure function of the
-- campaign store, the wallet and the completion ledger, fetched through
-- the campaign-dispatch providers. Nothing is remembered between fetches.

-- Menu-only store keys (not in the registered vars list, so the sim never
-- sees them): coin_spent, kettle_asked, settled.

local ADVANCE_GRANT = 700
local ADVANCE_OWED = 900
local PASS_ON_PAY = 150
local PAY_PER_LEVEL = 100
local MEAL_COST = 150
local GOOD_CRATE_COST = 400
local STRONG_CRATE_COST = 600
local FAIR_ROUND_COST = 200

-- The season under the cursor, in ledger voice. Indexed by the LAST level
-- of each season (spring 1-4, summer 5-9, autumn 10-13, winter 14-16,
-- reckoning 17-18, the settlement epilogue 19).
local SEASON_LAST_LEVEL = { 4, 9, 13, 16, 18, 19 }
local SEASON_LINE = {
  "Spring. The mud pays first.",
  "Summer. The coin is common now.",
  "Autumn. The seams run warm.",
  "Winter. The toll road is quiet.",
  "Reckoning. Collect at the gate.",
  "Settlement Day. Square the book.",
}

local function season_line(cursor)
  for i = 1, #SEASON_LAST_LEVEL do
    if cursor <= SEASON_LAST_LEVEL[i] then
      return SEASON_LINE[i]
    end
  end
  return SEASON_LINE[#SEASON_LINE]
end

-- Completed-level bitmask over 1..19, the shape the lib helpers take.
local function completed_mask()
  local mask = 0
  for lvl = 1, D.LEVEL_COUNT do
    if og.campaign_level_completed(lvl) then
      mask = mask + D.BIT[lvl]
    end
  end
  return mask
end

-- "taken" / "open" for the contracts line.
local function contract_word(lvl)
  if og.campaign_level_completed(lvl) then
    return "taken"
  end
  return "open"
end

-- Capitalized count word for a sentence start ("None waiting.").
local function cap_word(n)
  local w = D.count_word(n)
  return string.upper(string.sub(w, 1, 1)) .. string.sub(w, 2)
end

-- The docket note for one work row. Precedence: the year's turn, then
-- declining continued work (a backtrack is a COMPLETED level behind the
-- cursor — an unfinished optional contract stays on offer), then the
-- assessor, then optional contracts, then the level's own kind of work.
local function docket_note(lvl, cursor, cleared)
  if cursor == D.SETTLEMENT_LEVEL and lvl == 1 then
    return "the year turns"
  end
  if lvl < cursor and cleared then
    return "decline, go back"
  end
  if lvl == 4 then
    return "the assessor rides"
  end
  if D.is_optional(lvl) then
    return "optional work"
  end
  return D.TYPE_NOTE[lvl]
end

-- The kettle gag's entry note escalates with the ask count.
local function kettle_note(asked)
  if asked == 0 then
    return ""
  end
  if asked == 1 then
    return "he did not look up"
  end
  if asked == 2 then
    return "he looked up"
  end
  return "not for sale"
end

-- ---------------------------------------------------------------------------
-- Pages
-- ---------------------------------------------------------------------------

local function root_page()
  local kept = og.campaign_state_get("coin_kept")
  local spent = og.campaign_state_get("coin_spent")
  local debt = og.campaign_state_get("advance_debt")
  local settled = og.campaign_state_get("settled")
  local cursor = og.campaign_current_level()
  local mask = completed_mask()
  local waiting = D.backlog_count(kept, spent, mask)
  local kept_count = D.popcount_coins(kept)

  local lines = {}
  if settled ~= 0 then
    table.insert(lines, "New season. Same book.")
  end
  local debt_word = "none."
  if debt ~= 0 then
    debt_word = debt .. "g."
  end
  table.insert(lines, "Wages banked: " .. og.campaign_gold() ..
                      "g. Debt: " .. debt_word)
  table.insert(lines, "Coins kept: " .. D.count_word(kept_count) .. ". " ..
                      cap_word(waiting) .. " waiting.")
  table.insert(lines, "Bell " .. contract_word(3) .. ". Tolls " ..
                      contract_word(7) .. ". Vault " .. contract_word(12) ..
                      ".")
  table.insert(lines, season_line(cursor))

  local entries = {}
  local coin_note = ""
  if waiting > 0 then
    coin_note = cap_word(waiting) .. " waiting"
    table.insert(entries, { id = "coin", label = "THE WARM COIN",
                            kind = "page", note = coin_note })
  end
  table.insert(entries, { id = "work", label = "THE SEASON'S WORK",
                          kind = "page" })
  table.insert(entries, { id = "stores", label = "KETTLE'S STORES",
                          kind = "page" })
  local debt_note = ""
  if debt ~= 0 then
    debt_note = "owed: nine hundred"
  end
  table.insert(entries, { id = "debts", label = "ADVANCES & DEBTS",
                          kind = "page", note = debt_note })
  if waiting == 0 then
    table.insert(entries, { id = "coin", label = "THE WARM COIN",
                            kind = "page" })
  end
  if cursor == D.SETTLEMENT_LEVEL and settled == 0 then
    table.insert(entries, { id = "draw_pay", label = "DRAW YOUR PAY",
                            kind = "action", note = "squares the book" })
  end
  table.insert(entries, { id = "ask", label = "ASK ABOUT THE KETTLE",
                          kind = "action",
                          note = kettle_note(og.campaign_state_get(
                              "kettle_asked")) })
  return { title = "KETTLE'S BOOK", lines = lines, entries = entries }
end

local function work_page()
  local cursor = og.campaign_current_level()
  local mask = completed_mask()
  local open = D.accessible_levels(mask, cursor)
  local optional_open = false
  local entries = {}
  for i = 1, #open do
    local lvl = open[i]
    local cleared = og.campaign_level_completed(lvl)
    if D.is_optional(lvl) and not cleared then
      optional_open = true
    end
    table.insert(entries, { id = "" .. lvl, kind = "level", level = lvl,
                            note = docket_note(lvl, cursor, cleared) })
  end
  local docket_line = "The season's work, as the book has it."
  if optional_open then
    docket_line = "Optional work pays. It also costs."
  end
  return { title = "THE SEASON'S WORK", lines = { docket_line },
           entries = entries }
end

local function coin_page()
  local kept = og.campaign_state_get("coin_kept")
  local spent = og.campaign_state_get("coin_spent")
  local mask = completed_mask()
  local pending = D.oldest_unresolved(kept, spent, mask)
  if pending == 0 then
    local kept_count = D.popcount_coins(kept)
    local spent_count = D.popcount_coins(spent)
    return { title = "THE WARM COIN",
             lines = { "No coin waits in the book.",
                       "Kept: " .. D.count_word(kept_count) ..
                       ". Passed on: " .. D.count_word(spent_count) ..
                       "." } }
  end
  local waiting = D.backlog_count(kept, spent, mask)
  local lines = {}
  table.insert(lines, D.COIN_LINE[pending])
  table.insert(lines, "It spends high. Nobody asks why.")
  local cursor = og.campaign_current_level()
  local teaser = cursor == D.MINT_LEVEL
  if D.popcount_coins(kept) < D.CARRIED_PER_KEPT then
    teaser = false
  end
  if teaser then
    table.insert(lines, "You carried bones all year. At the")
    table.insert(lines, "mint, carried bones stand up.")
  else
    table.insert(lines, "Kept coins earn nothing. They stay")
    table.insert(lines, "warm in the pocket, like a hand.")
  end
  table.insert(lines, "Coins waiting: " .. D.count_word(waiting) .. ".")
  return { title = "THE WARM COIN", lines = lines,
           entries = {
             { id = "coin_keep", label = "KEEP THIS COIN", kind = "action",
               note = "earns nothing" },
             { id = "coin_pass", label = "PASS IT ON", kind = "action",
               note = "pays one-fifty" },
           } }
end

local function stores_page()
  local lines = {
    "All of it weighed. None of it free.",
    "Crates go where they are addressed,",
    "not where you are standing.",
    "The kettle is not for sale.",
  }
  if og.campaign_state_get("provisions") ~= 0 then
    table.insert(lines, "One crate rides the wagon already.")
  end
  local entries = {
    { id = "buy_meal", label = "MEAL FOR THE ROAD", kind = "action",
      cost = MEAL_COST, note = "four hot meals" },
    { id = "buy_good", label = "THE GOOD CRATE", kind = "action",
      cost = GOOD_CRATE_COST, note = "eight meals, packed" },
    { id = "buy_strong", label = "THE STRONG CRATE", kind = "action",
      cost = STRONG_CRATE_COST, note = "two of everything" },
  }
  local fair_open = not og.campaign_level_completed(D.FAIR_LEVEL)
  if og.campaign_state_get("fair_round") ~= 0 then
    fair_open = false
  end
  if fair_open then
    table.insert(entries, { id = "buy_round", label = "STAND THE CREW A ROUND",
                            kind = "action", cost = FAIR_ROUND_COST,
                            note = "for the fair" })
  end
  return { title = "KETTLE'S STORES", lines = lines, entries = entries }
end

local function debts_page()
  local debt = og.campaign_state_get("advance_debt")
  local cursor = og.campaign_current_level()
  local lines = {
    "The company charges no interest.",
    "The season does.",
    "Seven hundred now; nine hundred owed.",
  }
  if debt ~= 0 then
    table.insert(lines, "Owed: nine hundred. The season knows.")
    if cursor >= 14 then
      table.insert(lines, "Collectors walk the toll road.")
    end
  end
  local entries = {}
  if debt == 0 then
    table.insert(entries, { id = "take_advance", label = "TAKE AN ADVANCE",
                            kind = "action", note = "700 now, 900 owed" })
  else
    table.insert(entries, { id = "settle_book", label = "SETTLE THE BOOK",
                            kind = "action", cost = ADVANCE_OWED,
                            note = "clears the debt" })
  end
  return { title = "ADVANCES & DEBTS", lines = lines, entries = entries }
end

local function picker_menu(page_id)
  if page_id == "" then
    return root_page()
  end
  if page_id == "work" then
    return work_page()
  end
  if page_id == "coin" then
    return coin_page()
  end
  if page_id == "stores" then
    return stores_page()
  end
  if page_id == "debts" then
    return debts_page()
  end
  return nil
end

-- ---------------------------------------------------------------------------
-- Actions. Fixed `cost` debits are engine-owned and land BEFORE dispatch;
-- every action re-derives its state (a stale page cannot be trusted).
-- ---------------------------------------------------------------------------

local function act_ask()
  local asked = og.campaign_state_get("kettle_asked")
  if asked < 3 then
    og.campaign_state_set("kettle_asked", asked + 1)
  end
  if asked == 0 then
    return { message = "Kettle did not look up." }
  end
  if asked == 1 then
    return { message = "Kettle looked up. Briefly." }
  end
  return { message = "It is not for sale. It is the company." }
end

local function act_coin(keep)
  local kept = og.campaign_state_get("coin_kept")
  local spent = og.campaign_state_get("coin_spent")
  local pending = D.oldest_unresolved(kept, spent, completed_mask())
  if pending == 0 then
    return { message = "No coin waits." }
  end
  if keep then
    og.campaign_state_set("coin_kept", kept + D.BIT[pending])
    return { message = "Written. One coin, kept, warm." }
  end
  og.campaign_state_set("coin_spent", spent + D.BIT[pending])
  og.campaign_grant_gold(PASS_ON_PAY)
  return { message = "Written. It spent high, as they do." }
end

local function act_draw_pay()
  if og.campaign_state_get("settled") ~= 0 then
    return { message = "The book is closed." }
  end
  if og.campaign_current_level() ~= D.SETTLEMENT_LEVEL then
    return { message = "The book is closed." }
  end
  local done = 0
  for lvl = 1, D.LEVEL_COUNT do
    if og.campaign_level_completed(lvl) then
      done = done + 1
    end
  end
  local debt = og.campaign_state_get("advance_debt")
  local pay = og.max(done * PAY_PER_LEVEL - debt, 0)
  og.campaign_grant_gold(pay)
  og.campaign_state_set("advance_debt", 0)
  og.campaign_state_set("settled", 1)
  return { message = "The book closes. The book keeps." }
end

local function act_buy_crate(kind, message)
  local cursor = og.campaign_current_level()
  og.campaign_state_set("provisions", kind + 8 * cursor)
  return { message = message }
end

local function picker_action(entry_id)
  if entry_id == "ask" then
    return act_ask()
  end
  if entry_id == "coin_keep" then
    return act_coin(true)
  end
  if entry_id == "coin_pass" then
    return act_coin(false)
  end
  if entry_id == "buy_meal" then
    return act_buy_crate(1, "Four meals, crated and addressed.")
  end
  if entry_id == "buy_good" then
    return act_buy_crate(2, "Eight meals, packed and addressed.")
  end
  if entry_id == "buy_strong" then
    return act_buy_crate(3, "Two of everything. Weighed twice.")
  end
  if entry_id == "buy_round" then
    og.campaign_state_set("fair_round", 1)
    return { message = "The crew drinks to the fair." }
  end
  if entry_id == "take_advance" then
    og.campaign_grant_gold(ADVANCE_GRANT)
    og.campaign_state_set("advance_debt", ADVANCE_OWED)
    return { message = "Seven hundred, counted warm." }
  end
  if entry_id == "settle_book" then
    og.campaign_state_set("advance_debt", 0)
    return { message = "The page is squared." }
  end
  if entry_id == "draw_pay" then
    return act_draw_pay()
  end
  return nil
end

og.register_campaign_hooks({
  vars = { "coin_kept", "advance_debt", "provisions", "fair_round" },
  picker_menu = picker_menu,
  picker_action = picker_action,
})
