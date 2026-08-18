-- longseason.ledger: campaign_book — the open ledger: Kettle's book IS the Base Camp (the JOBS/DEBT/COINS readout, the season stanza, the work docket, the warm-coin ritual) with Kettle's Stores as its one room (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local D = og.use("ledger_data")

-- The whole camp is state-derived: every widget is a pure function of the
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

-- Zone row units on the 14px grid. The band holds eight; the roster keeps
-- three rows plus its own column heading, so the stanza and the action rows
-- share a fixed four.
--
-- A widget's unit count IS the number of rows it shows: everything past it
-- goes behind a pager. So an ordinary week spends ONE unit on the stanza and
-- THREE on the docket, and the job, the book's money row and the shop door
-- are all on the screen at once — only an optional contract ever pages. When
-- the book has more to say than the season (a coin waiting, a debt
-- outstanding, the settlement arithmetic) the stanza takes a second unit and
-- the docket pays for it, which is why the money row rides directly under
-- the job rather than under the shop door.
local STANZA_UNITS = 1
local STANZA_STACKED_UNITS = 2
local DOCKET_UNITS = 3
local DOCKET_TIGHT_UNITS = 2
local COIN_UNITS = 2

-- The page's line budget. A composed "<prefix><job title>." line clips the
-- TITLE to what is left of it, so a long job name is cut by the book
-- rather than overrunning the panel edge.
local LINE_MAX = 38

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

-- What the wagon is already carrying, by crate kind (provisions mod 8).
local WAGON_LINE = {
  "On the wagon: 4 meals.",
  "On the wagon: 8 meals.",
  "On the wagon: 8 meals, 2 silver.",
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

-- Jobs done, off the same mask (the settlement pays by the job).
local function completed_count(mask)
  local count = 0
  for lvl = 1, D.LEVEL_COUNT do
    if D.has_bit(mask, lvl) then
      count = count + 1
    end
  end
  return count
end

-- Capitalized count word for a sentence start ("Eighteen jobs done").
local function cap_word(n)
  local w = D.count_word(n)
  return string.upper(string.sub(w, 1, 1)) .. string.sub(w, 2)
end

-- The job the camp is pointed at. That is the cursor, except on a settled
-- Settlement Day: the book is closed, and the only work it still offers is
-- next spring.
local function job_level(cursor, settled)
  if cursor ~= D.SETTLEMENT_LEVEL then
    return cursor
  end
  if settled == 0 then
    return cursor
  end
  return D.FIRST_LEVEL
end

-- "<prefix><job title>." for a level the campaign carries, nil for one it
-- does not (an absent title must not compose "This job: .").
local function job_line(prefix, lvl)
  local title = og.campaign_scenario_title(lvl)
  if title == "" then
    return nil
  end
  local budget = LINE_MAX - #prefix - 1
  return prefix .. string.sub(title, 1, budget) .. "."
end

-- What a docket row costs you if it goes wrong, stated ON the row.
-- Precedence: the year's turn, the collectors riding the toll road, the
-- escort that must survive, an optional contract, then the level's own
-- kind of work.
local function stake_note(lvl, new_season, debt)
  if new_season then
    if lvl == D.FIRST_LEVEL then
      return "the year turns"
    end
  end
  if lvl == D.TOLL_LEVEL then
    if debt ~= 0 then
      return "the collectors ride"
    end
  end
  if D.PROTECTED_NOTE[lvl] ~= nil then
    return D.PROTECTED_NOTE[lvl]
  end
  if D.is_optional(lvl) then
    return "optional, pays extra"
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
-- The zone: the ledger, open on the table
-- ---------------------------------------------------------------------------

-- The header cells: three facts the book keeps that the screen does not
-- already carry. The purse is NOT one of them — the panel's own GOLD cell
-- and the terminals' COMPANY strip are engine-owned and always present, and
-- a second word for the same money on the same screen only asks the player
-- whether these are two pots. So the book states what it alone counts: jobs
-- done (the settlement pays 100g each), what the book is owed, and the
-- tally the warm-coin ritual writes.
local function readout_items(debt, kept_count, done)
  local debt_value = "none"
  if debt ~= 0 then
    debt_value = debt .. "g"
  end
  local coin_value = "none"
  if kept_count > 0 then
    coin_value = kept_count .. " kept"
  end
  local items = {}
  table.insert(items, { label = "JOBS", value = done .. " done" })
  table.insert(items, { label = "DEBT", value = debt_value })
  table.insert(items, { label = "COINS", value = coin_value })
  return items
end

-- The ritual's stanza: the coin's own flavor, why passing it pays over the
-- odds, and WHERE a kept coin is redeemed. The last line is the one the
-- trade is unreadable without — "1 ally per 4 kept" is a promise with no
-- place until the mint is named, and the mint is thirteen jobs away from
-- the first coin. At the mint itself, with the payoff banked, it says what
-- is about to happen instead.
local function coin_lines(pending, cursor, kept_count)
  local lines = {}
  table.insert(lines, D.COIN_LINE[pending])
  table.insert(lines, "It spends high. Nobody asks why.")
  if cursor == D.MINT_LEVEL and kept_count >= D.CARRIED_PER_KEPT then
    table.insert(lines, "At the mint, carried bones stand up.")
  else
    table.insert(lines, "Kept coins stand up at the mint.")
  end
  return lines
end

-- The settlement arithmetic, under the day's own line and before the button
-- that does it. Three lines is the whole band, so the last one goes to the
-- fact that moves money: a debt comes off the draw, and only a square book
-- has room to say what a kept coin buys at the door.
local function settlement_lines(mask, debt, kept_count)
  local done = completed_count(mask)
  local tail = " jobs done, 100g the job."
  if done == 1 then
    tail = " job done, 100g the job."
  end
  local counted = cap_word(done) .. tail
  if done == 0 then
    counted = "No" .. tail
  end
  local lines = {}
  table.insert(lines, season_line(D.SETTLEMENT_LEVEL))
  table.insert(lines, counted)
  if debt ~= 0 then
    table.insert(lines, "Debt comes off the draw: " .. debt .. "g.")
  elseif kept_count > 0 then
    table.insert(lines, "One kept coin is nailed over the door.")
  end
  return lines
end

-- The book's money row is open until the collectors ride. Before The Long
-- Toll an advance can still be taken and still be squared; once the Toll has
-- been fought, 900g to dodge a 900g dock is a wash, AND an advance would
-- quote a deadline that has already passed to write a debt with no exit —
-- the settlement pays out once and latches. So the row shuts, both halves
-- together, and an outstanding debt rides to the settlement to be docked.
local function money_row_open()
  return not og.campaign_level_completed(D.TOLL_LEVEL)
end

-- The ordinary stanza: where the season is, plus the one consequence no row
-- on the screen can state. While the money row is open the debt states
-- itself — the DEBT cell counts it, and SETTLE THE BOOK names its price,
-- what it buys and where — so the stanza stays one line and the docket buys
-- the other unit with it. Once the collectors have ridden there is no settle
-- row left to say anything, and the debt's last consequence goes here.
local function season_lines(cursor, settled, debt, job)
  local lines = {}
  if settled ~= 0 then
    if cursor == D.SETTLEMENT_LEVEL then
      table.insert(lines, "New season. Same book.")
    end
  end
  table.insert(lines, season_line(job))
  if debt ~= 0 then
    if settled == 0 then
      if not money_row_open() then
        table.insert(lines, "Unpaid debt docks the settlement.")
      end
    end
  end
  return lines
end

-- The trade, in numbers, on the rows. Once four kept coins per ally has
-- banked the cap, another kept coin buys nothing but the door coin — and
-- the row says so instead of quoting a rate that no longer pays.
local function coin_rows(kept_count)
  local keep_note = "1 ally per 4 kept"
  if kept_count >= D.CARRIED_PER_KEPT * D.CARRIED_CAP then
    keep_note = "a coin for the door"
  end
  local rows = {}
  table.insert(rows, { id = "coin_keep", label = "KEEP THIS COIN",
                       kind = "action", note = keep_note })
  table.insert(rows, { id = "coin_pass", label = "PASS IT ON",
                       kind = "action", note = "150g now, none later" })
  return rows
end

-- The payout, netted at fetch time: the note is the number, not a slogan.
local function draw_pay_note(mask, debt)
  local pay = og.max(completed_count(mask) * PAY_PER_LEVEL - debt, 0)
  return "pays " .. pay .. "g, once"
end

-- The docket, in the order the band shows it: the job in front of you, the
-- book's money row, the door into the stores, and then every optional
-- contract still open. The first three are the camp's decisions and they are
-- ON the screen in every state; contracts are extra work, so they are the
-- only rows that ever pay the pager.
--
-- The advance's note states BOTH sides. A row that advertises only what it
-- costs is a row nobody signs on purpose, and the grant is the whole reason
-- to sign: 700g is one hire in a spring that cannot afford one. Settle's
-- note is short for a reason: the panel draws "<label> - <note>  <cost>g"
-- into 42 characters, and a note past 18 pushes the price off the end of
-- the row it is the price of.
local function docket_rows(cursor, settled, debt, mask, new_season)
  local job = job_level(cursor, settled)
  local rows = {}
  table.insert(rows, { id = "" .. job, kind = "level", level = job,
                       note = stake_note(job, new_season, debt) })
  if money_row_open() then
    if debt == 0 then
      table.insert(rows, { id = "take_advance", label = "TAKE AN ADVANCE",
                           kind = "action", note = "700 now, 900 at Toll" })
    else
      table.insert(rows, { id = "settle_book", label = "SETTLE THE BOOK",
                           kind = "action", cost = ADVANCE_OWED,
                           note = "no Toll collectors" })
    end
  end
  table.insert(rows, { id = "stores", label = "KETTLE'S STORES",
                       kind = "page", note = "crates for this job" })
  local open = D.accessible_levels(mask, cursor)
  for i = 1, #open do
    local lvl = open[i]
    if lvl ~= job then
      if D.is_optional(lvl) then
        if not og.campaign_level_completed(lvl) then
          table.insert(rows, { id = "" .. lvl, kind = "level", level = lvl,
                               note = "optional, pays extra" })
        end
      end
    end
  end
  return rows
end

local function base_camp()
  local cursor = og.campaign_current_level()
  local settled = og.campaign_state_get("settled")
  local debt = og.campaign_state_get("advance_debt")
  local kept = og.campaign_state_get("coin_kept")
  local spent = og.campaign_state_get("coin_spent")
  local mask = completed_mask()
  local kept_count = D.popcount_coins(kept)
  local pending = D.oldest_unresolved(kept, spent, mask)
  local new_season = false
  local settlement = false
  if cursor == D.SETTLEMENT_LEVEL then
    if settled == 0 then
      settlement = true
    else
      new_season = true
    end
  end

  local lines = nil
  if pending ~= 0 then
    lines = coin_lines(pending, cursor, kept_count)
  elseif settlement then
    lines = settlement_lines(mask, debt, kept_count)
  else
    lines = season_lines(cursor, settled, debt, job_level(cursor, settled))
  end
  local stanza_units = STANZA_UNITS
  if #lines > 1 then
    stanza_units = STANZA_STACKED_UNITS
  end

  local widgets = {}
  table.insert(widgets, { kind = "readout",
                          items = readout_items(debt, kept_count,
                                                completed_count(mask)) })
  table.insert(widgets, { kind = "text", weight = stanza_units,
                          lines = lines })
  if pending ~= 0 then
    -- The ritual IS the camp while a coin waits. It is the one composition
    -- that interrupts, and it interrupts properly: the docket steps aside
    -- until the coin is written, so both sides of the trade fit on the
    -- screen with the mint named over them, and one click hands the docket
    -- straight back. GO is untouched — the camp asks for a decision, it
    -- does not hold the company hostage.
    table.insert(widgets, { kind = "actions", weight = COIN_UNITS,
                            entries = coin_rows(kept_count) })
  else
    local docket = docket_rows(cursor, settled, debt, mask, new_season)
    if settlement then
      table.insert(docket, 1, { id = "draw_pay", label = "DRAW YOUR PAY",
                                kind = "action",
                                note = draw_pay_note(mask, debt) })
    end
    local docket_units = DOCKET_UNITS
    if stanza_units > STANZA_UNITS then
      docket_units = DOCKET_TIGHT_UNITS
    end
    table.insert(widgets, { kind = "actions", weight = docket_units,
                            entries = docket })
  end
  -- The company keeps every default capability: The Long Season's texture
  -- is money and consequence, never a hero the camp will not muster.
  table.insert(widgets, { kind = "roster" })
  return { widgets = widgets }
end

-- ---------------------------------------------------------------------------
-- The one room: Kettle's Stores. Priced goods need a page of their own —
-- destination, contents and the re-address rule have to be readable on the
-- same screen as the price.
-- ---------------------------------------------------------------------------

local function stores_page()
  local cursor = og.campaign_current_level()
  local settled = og.campaign_state_get("settled")
  local job = job_level(cursor, settled)
  local provisions = og.campaign_state_get("provisions")
  local fair_open = false
  if og.campaign_state_get("fair_round") == 0 then
    if not og.campaign_level_completed(D.FAIR_LEVEL) then
      fair_open = true
    end
  end

  local lines = {}
  table.insert(lines, "Crates land at the current job's camp.")
  local destination = job_line("This job: ", job)
  if destination ~= nil then
    table.insert(lines, destination)
  end
  local wagon = WAGON_LINE[og.mod(provisions, 8)]
  if wagon ~= nil then
    table.insert(lines, wagon)
    local addressed = og.div(provisions, 8)
    if addressed == job then
      table.insert(lines, "Buying again re-addresses the wagon.")
    else
      local elsewhere = job_line("Addressed to ", addressed)
      if elsewhere == nil then
        elsewhere = "Addressed to another job."
      end
      table.insert(lines, elsewhere)
    end
  end
  if fair_open then
    table.insert(lines, "A round buys hands on the fair door.")
  end
  table.insert(lines, "The kettle is not for sale.")

  local entries = {}
  table.insert(entries, { id = "buy_meal", label = "MEAL FOR THE ROAD",
                          kind = "action", cost = MEAL_COST,
                          note = "4 meals at the job" })
  table.insert(entries, { id = "buy_good", label = "THE GOOD CRATE",
                          kind = "action", cost = GOOD_CRATE_COST,
                          note = "8 meals at the job" })
  table.insert(entries, { id = "buy_strong", label = "THE STRONG CRATE",
                          kind = "action", cost = STRONG_CRATE_COST,
                          note = "8 meals + 2 silver" })
  if fair_open then
    table.insert(entries, { id = "buy_round",
                            label = "STAND THE CREW A ROUND",
                            kind = "action", cost = FAIR_ROUND_COST,
                            note = "guards at the fair" })
  end
  -- The gag lives here, free, as the shop's last row: it is not a decision,
  -- so it cannot cost the camp a row of its own.
  table.insert(entries, { id = "ask", label = "ASK ABOUT THE KETTLE",
                          kind = "action",
                          note = kettle_note(og.campaign_state_get(
                              "kettle_asked")) })
  return { title = "KETTLE'S STORES", lines = lines, entries = entries }
end

local function picker_menu(page_id)
  if page_id == "stores" then
    return stores_page()
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

-- The advance re-derives its own window, like the draw does. A page that
-- went stale must not be able to write a debt the book could never square,
-- nor a second advance over an outstanding one.
local function act_take_advance()
  if not money_row_open() then
    return { message = "The book takes no more advances." }
  end
  if og.campaign_state_get("advance_debt") ~= 0 then
    return { message = "One advance at a time." }
  end
  og.campaign_grant_gold(ADVANCE_GRANT)
  og.campaign_state_set("advance_debt", ADVANCE_OWED)
  return { message = "Seven hundred, counted warm." }
end

local function act_buy_crate(kind, message)
  local cursor = og.campaign_current_level()
  local settled = og.campaign_state_get("settled")
  og.campaign_state_set("provisions", kind + 8 * job_level(cursor, settled))
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
    return act_take_advance()
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
  base_camp = base_camp,
  picker_menu = picker_menu,
  picker_action = picker_action,
})
