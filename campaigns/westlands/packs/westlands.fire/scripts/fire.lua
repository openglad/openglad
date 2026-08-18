-- westlands.fire — THE COMPANY FIRE: the Base Camp itself — camp stanza, the company, the road ahead — over og.register_campaign_hooks (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- The fire is the camp, not a book behind a door: base_camp composes the
-- Gameplay zone every fetch, and the only rows under the roster are the fight
-- at the company's feet and — once that fight is won — the real forward
-- choices out of it. Backtracking lives in the world's own exits, so a back
-- row is never composed. Every state is a pure function of the campaign
-- providers (save cursor, completed set, campaign state, wallet, roster oath
-- tags); the zone session owns all navigation and every cost debit. The four
-- decision keys the sim can read are registered as vars below; delve_sunk
-- is deliberately NOT a var — the river keeps it, and the world never hears
-- of it. The oath tags are the per-hero campaign_tag byte, written by the
-- roster's own assign column, and no camp state is stored anywhere else.

local fronts = og.use("fronts")
local road = og.use("road")
local stanzas = og.use("stanzas")

-- The offer caps and prices, in one place.
local PROVISION_CAP = 3
local PROVISION_PRICE = 300
local WATCH_PRICE = 900
local BREAD_PRICE = 60
local DELVE_GRANT = 800

-- The camp's text band holds three lines; the ledger page holds six. Both
-- compositions are priority ladders that stop at their budget rather than
-- letting the tail fall off the bottom of a screen nobody sized for it.
local CAMP_TEXT_LINES = 3
local LEDGER_LINES = 6

-- Row labels are 24 glyphs, which is the panel's 42-glyph row face minus the
-- " (HOST)" a joiner's copy of a level row wears and minus the engine's own
-- "  [CURRENT]". road.lua and stanzas.lua are authored to it; the current
-- level's row borrows the SHIPPED scenario title, the one label this pack
-- does not write itself, so that one is clipped here.
local ROW_LABEL_MAX = 24

local function push_capped(lines, line, cap)
  if line == nil then
    return
  end
  if #lines >= cap then
    return
  end
  lines[#lines + 1] = line
end

-- ---------------------------------------------------------------------------
-- The offer windows (unchanged from the book: the same one-shot keys)
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

-- ---------------------------------------------------------------------------
-- The camp's state: derived every fetch, stored nowhere
-- ---------------------------------------------------------------------------

local function roads_walked(war_front, burden_front)
  if war_front ~= nil then
    return false
  end
  if burden_front ~= nil then
    return false
  end
  return true
end

local function road_started()
  if fronts.started(fronts.WAR) then
    return true
  end
  return fronts.started(fronts.BURDEN)
end

-- The third act is live from the Falls until both roads are walked.
local function split_is_live(st)
  if not st.past_falls then
    return false
  end
  return not st.roads_done
end

-- The reunion waits for both roads and retires the moment the summit falls.
local function reunion_is_open(st)
  if not st.past_falls then
    return false
  end
  if not st.roads_done then
    return false
  end
  return not og.campaign_level_completed(fronts.SUMMIT)
end

local function camp_state()
  local st = {}
  st.cursor = og.campaign_current_level()
  st.count = fronts.muster(og.campaign_team())
  st.war_front = fronts.front(fronts.WAR)
  st.burden_front = fronts.front(fronts.BURDEN)
  st.past_falls = og.campaign_level_completed(fronts.FALLS)
  st.roads_done = roads_walked(st.war_front, st.burden_front)
  st.split_live = split_is_live(st)
  st.reunion = reunion_is_open(st)
  st.started = road_started()
  st.frozen = fronts.frozen(st.count, st.started)
  return st
end

-- ---------------------------------------------------------------------------
-- The text band: the fire's voice, three lines at most
-- ---------------------------------------------------------------------------

-- The sortie the camp can actually send. Every road row promises a column,
-- but GO takes only the heroes standing in it, and the oath ceremony stands
-- a hero down as it swears him — so the state right after the Falls is "four
-- swords sworn, nobody deployed". That is the one thing on this screen worth
-- more than the fire's own flavour, so it rides high in every ladder.
local function no_march_line(st)
  if st.count.size == 0 then
    return nil
  end
  if st.count.standing > 0 then
    return nil
  end
  return stanzas.WARN.no_march
end

local function camp_line(level)
  local rows = stanzas.CAMP
  for i = 1, #rows do
    local row = rows[i]
    if level >= row.min then
      if level <= row.max then
        return row.line
      end
    end
  end
  return stanzas.CAMP_FALLBACK
end

local function milestone_line()
  local rows = stanzas.MILESTONE
  for i = 1, #rows do
    local row = rows[i]
    if og.campaign_level_completed(row.level) then
      return row.line
    end
  end
  return nil
end

local function loop_line()
  if not og.campaign_level_completed(26) then
    return nil
  end
  return stanzas.LOOP_LINE
end

-- The camp with no forward exits (the act gap, or a save pointing nowhere):
-- the empty docket needs a reason on screen.
local function dark_line(st)
  if road.ROAD[st.cursor] ~= nil then
    return nil
  end
  return stanzas.WARN.dark
end

local function hoard_line()
  if not delve_offer_open() then
    return nil
  end
  return stanzas.WARN.hoard
end

local function escort_here(level)
  local here = road.ROAD[level]
  if here == nil then
    return false
  end
  return here.escort
end

local function bearer_walks_line(st)
  if not escort_here(st.cursor) then
    return nil
  end
  return stanzas.WARN.bearer_walks
end

local function open_road_lines(st)
  local lines = {}
  push_capped(lines, loop_line(), CAMP_TEXT_LINES)
  push_capped(lines, camp_line(st.cursor), CAMP_TEXT_LINES)
  push_capped(lines, no_march_line(st), CAMP_TEXT_LINES)
  push_capped(lines, dark_line(st), CAMP_TEXT_LINES)
  push_capped(lines, hoard_line(), CAMP_TEXT_LINES)
  push_capped(lines, bearer_walks_line(st), CAMP_TEXT_LINES)
  push_capped(lines, milestone_line(), CAMP_TEXT_LINES)
  return lines
end

-- The swearing window's third line: the thin or empty column is warned about
-- in prose BEFORE the first march freezes anything.
local function falls_warning(count)
  if count.unsworn > 0 then
    return stanzas.FALLS.unsworn
  end
  if count.war == 1 then
    return stanzas.FALLS.thin_war
  end
  if count.burden == 1 then
    return stanzas.FALLS.thin_burden
  end
  return nil
end

local function falls_lines(st)
  local lines = {}
  push_capped(lines, stanzas.FALLS.divide, CAMP_TEXT_LINES)
  push_capped(lines, stanzas.FALLS.swear, CAMP_TEXT_LINES)
  push_capped(lines, no_march_line(st), CAMP_TEXT_LINES)
  push_capped(lines, falls_warning(st.count), CAMP_TEXT_LINES)
  return lines
end

local function war_line(st)
  if st.war_front == nil then
    return stanzas.FRONT.WAR_DONE
  end
  return stanzas.FRONT.WAR_AT[st.war_front]
end

local function burden_line(st)
  if st.burden_front == nil then
    return stanzas.FRONT.BURDEN_DONE
  end
  return stanzas.FRONT.BURDEN_AT[st.burden_front]
end

-- A column with nobody in it lifts its own locks: the split is over, and the
-- fire says so instead of soft-locking the road.
local function oath_warning(count)
  if count.war == 0 then
    if count.burden == 0 then
      return stanzas.WARN.no_oath
    end
    return stanzas.WARN.ashes_west
  end
  if count.burden == 0 then
    return stanzas.WARN.ashes_east
  end
  return nil
end

-- The summit is reachable from the war road's own exit, so a company can
-- STAND on the mountain with the Bearer still out east — 17 offers nothing
-- else, so this is the common path, not a corner. The refusal has to be on
-- screen while the cursor rests there, before GO, and not only after the
-- mountain is already behind the company.
local function summit_warning(st)
  if st.cursor == fronts.SUMMIT then
    return stanzas.WARN.not_come
  end
  if not og.campaign_level_completed(fronts.SUMMIT) then
    return nil
  end
  return stanzas.WARN.not_come
end

local function bearer_stake_line(st)
  if fronts.road_of(st.cursor) ~= fronts.BURDEN_TAG then
    return nil
  end
  return stanzas.WARN.bearer_stake
end

local function march_lines(st)
  local lines = {}
  push_capped(lines, oath_warning(st.count), CAMP_TEXT_LINES)
  push_capped(lines, summit_warning(st), CAMP_TEXT_LINES)
  push_capped(lines, no_march_line(st), CAMP_TEXT_LINES)
  push_capped(lines, war_line(st), CAMP_TEXT_LINES)
  push_capped(lines, burden_line(st), CAMP_TEXT_LINES)
  push_capped(lines, bearer_stake_line(st), CAMP_TEXT_LINES)
  return lines
end

local function reunion_lines(st)
  local lines = {}
  push_capped(lines, stanzas.REUNION[1], CAMP_TEXT_LINES)
  push_capped(lines, stanzas.REUNION[2], CAMP_TEXT_LINES)
  push_capped(lines, no_march_line(st), CAMP_TEXT_LINES)
  return lines
end

local function camp_lines(st)
  if st.reunion then
    return reunion_lines(st)
  end
  if not st.split_live then
    return open_road_lines(st)
  end
  if st.started then
    return march_lines(st)
  end
  return falls_lines(st)
end

-- ---------------------------------------------------------------------------
-- The roster: the oath column and the deploy locks it makes mechanical
-- ---------------------------------------------------------------------------

-- The hiring board shuts exactly while the split is frozen: that is the one
-- state where a new blade could neither swear a road nor walk one.
local function hiring_closed(st)
  if not st.split_live then
    return false
  end
  return st.frozen
end

-- A hero whose own road is walked is not on it any more: he waits under the
-- mountain for the other column.
local function away_reason(front, on_the_road)
  if front == nil then
    return stanzas.LOCK.mountain
  end
  return on_the_road
end

-- Locks are derived from the SET level, never from a stored "current front":
-- tonight's road decides who is free to deploy.
local function deploy_locks(st)
  local here = fronts.road_of(st.cursor)
  if here == 0 then
    return nil
  end
  local locks = {}
  if here == fronts.WAR_TAG then
    locks[1] = {
      tag = fronts.BURDEN_TAG,
      reason = away_reason(st.burden_front, stanzas.LOCK.burden),
    }
  else
    locks[1] = {
      tag = fronts.WAR_TAG,
      reason = away_reason(st.war_front, stanzas.LOCK.war),
    }
  end
  locks[2] = { unset = true, reason = stanzas.LOCK.unsworn }
  return locks
end

local function roster_widget(st)
  local widget = { kind = "roster" }
  if not st.split_live then
    return widget
  end
  -- The oath column takes the team-chip cell for the whole third act.
  widget.can_team = false
  widget.assign = { key = "road", labels = { "WAR", "BURDEN" } }
  if not st.frozen then
    return widget
  end
  widget.assign.frozen = stanzas.LOCK.frozen
  widget.locks = deploy_locks(st)
  -- The hiring board shuts with the split. A blade bought after the Falls
  -- carries no oath: the frozen column would refuse to swear him and the
  -- unsworn lock would refuse to deploy him, so the camp would be selling a
  -- hero who can only stand at the waterfall. "None turn back" cuts both
  -- ways, and a wiped column lifts the freeze and the board with it.
  widget.can_hire = not hiring_closed(st)
  return widget
end

-- ---------------------------------------------------------------------------
-- The docket: the roads, the night's offers, the two doors
-- ---------------------------------------------------------------------------

-- The level the company is STANDING ON carries no note, wherever the docket
-- names it. The engine marks that row [CURRENT] on every client, and the
-- marker plus its gap is eleven glyphs out of the panel's forty-two: a note
-- beside it left the row's own name nothing to sit in, and the panel cut the
-- words mid-syllable ("THE QUIET VALE - THE FIGHT AT..", "EAST: THE
-- CROSSROADS - ESCORT.."). The marker says what the note said, so the row
-- says it once and keeps its name whole. Not gating and not decoration: the
-- pack is budgeting its own words against a marker it knows is coming.
local function level_row(st, level, label, note)
  if level == st.cursor then
    note = nil
  end
  return {
    id = "go" .. level,
    kind = "level",
    level = level,
    label = label,
    note = note,
  }
end

-- The march note counts the column AND the blades that wait at the Falls, so
-- a thin muster is legible before the road is chosen.
local function muster_note(word, sworn, waiting)
  if sworn == 0 then
    -- A column nobody holds does not stop the road: the freeze needs both
    -- musters, so an empty one lifts every lock. "unsworn" read as a
    -- refusal beside a text band already explaining that all ride.
    return "all ride"
  end
  if waiting == 0 then
    return word .. " " .. sworn
  end
  return word .. " " .. sworn .. ", " .. waiting .. " wait"
end

local function front_rows(st, rows)
  if st.war_front ~= nil then
    local note = muster_note("party", st.count.war, st.count.unsworn)
    local label = stanzas.FRONT.WAR_LABEL[st.war_front]
    rows[#rows + 1] = level_row(st, st.war_front, label, note)
  end
  if st.burden_front ~= nil then
    local note = muster_note("escort", st.count.burden, st.count.unsworn)
    local label = stanzas.FRONT.BURDEN_LABEL[st.burden_front]
    rows[#rows + 1] = level_row(st, st.burden_front, label, note)
  end
end

-- The level the company is standing on, named the way the campaign names it.
-- The scenario title is what every other surface calls the level (the engine's
-- own "Level set to <title>." toast included), so the fire says the same word;
-- a level whose file the package cannot read has no title to borrow.
-- The row itself carries no note (see level_row): the borrowed title is the
-- longest label the docket ever shows, and the [CURRENT] marker is the only
-- thing that needs the room beside it.
local HERE_UNTITLED = "THE ROAD AT YOUR FEET"

local function here_label(level)
  local title = og.campaign_scenario_title(level)
  if title == "" then
    return HERE_UNTITLED
  end
  return string.upper(string.sub(title, 1, ROW_LABEL_MAX))
end

-- The docket leads with the fight at the company's feet, and NAMES the roads
-- out of it on the night they open — the night that fight is won. A normal
-- night is three rows (the fight, the quartermaster, the ledger), which is
-- exactly the band the camp's docket gets to show at once: a fourth row would
-- hide THE LEDGER behind a pager arrow every night of the campaign, to
-- advertise a road nobody may walk yet. This is composition, not gating: the
-- forward rows are still exactly what the graph authored, and the engine
-- remains the only thing that refuses a road (a second refusal written here
-- could only disagree with it).
local function graph_rows(st, rows)
  local here = road.ROAD[st.cursor]
  if here == nil then
    return
  end
  rows[#rows + 1] = level_row(st, st.cursor, here_label(st.cursor))
  if not og.campaign_level_completed(st.cursor) then
    return
  end
  for i = 1, #here.fwd do
    local exit = here.fwd[i]
    rows[#rows + 1] = level_row(st, exit.level, exit.label, exit.note)
  end
end

local function road_rows(st, rows)
  if st.split_live then
    front_rows(st, rows)
    return
  end
  if st.reunion then
    local label = "THE MOUNTAIN OF FIRE"
    rows[#rows + 1] =
      level_row(st, fronts.SUMMIT, label, "all swords together")
    return
  end
  graph_rows(st, rows)
end

-- The hot one-shot offers live HERE and nowhere else: the moral fork belongs
-- at the fire on the night it matters, and one door means one re-check.
local function offer_rows(rows)
  if delve_offer_open() then
    rows[#rows + 1] = {
      id = "delve_count",
      kind = "action",
      label = "COUNT THE DELVE GOLD",
      note = "+800g, and a debt",
    }
    rows[#rows + 1] = {
      id = "delve_sink",
      kind = "action",
      label = "SINK IT IN THE RIVER",
      note = "no gold, no debt",
    }
  end
  if bread_offer_open() then
    rows[#rows + 1] = {
      id = "sneak_bread",
      kind = "action",
      label = "BREAD FOR SNEAK",
      cost = BREAD_PRICE,
      -- A priced row's face carries the price too ("  60g"), so this note is
      -- one glyph shorter than the note budget: with the comma it was the
      -- panel's own cut ("KINDNESS,..  60G").
      note = "kindness remembered",
    }
  end
end

local function door_rows(rows)
  rows[#rows + 1] = {
    id = "stores",
    kind = "page",
    label = "QUARTERMASTER",
    note = "packs and wages",
  }
  rows[#rows + 1] = {
    id = "ledger",
    kind = "page",
    label = "THE LEDGER",
    note = "what the road cost",
  }
end

local function docket(st)
  local rows = {}
  road_rows(st, rows)
  offer_rows(rows)
  door_rows(rows)
  return rows
end

-- ---------------------------------------------------------------------------
-- The readout: the weight the company carries
-- ---------------------------------------------------------------------------

-- The purse is NOT here: the header GOLD cell is C++-owned on every surface
-- (the panel header and the terminal camp strip both carry it always), and a
-- camp that composed its own GOLD cell would print the same number twice on
-- adjacent lines. What only the fire knows is the pack train.
local function pack_weight()
  local tier = og.campaign_state_get("provisions")
  if tier <= 0 then
    return "none"
  end
  return "tier " .. stanzas.TIER_WORDS[og.min(tier, PROVISION_CAP)]
end

local function readout_widget()
  local items = { { label = "PACKS", value = pack_weight() } }
  return { kind = "readout", items = items }
end

-- The composition: the readout heads the panel, the fire speaks, the company
-- stands, and the road is under them. The text and docket bands take their
-- default shares (<= 2 and <= 3 units), so the roster always keeps rows.
local function base_camp()
  local st = camp_state()
  return {
    widgets = {
      readout_widget(),
      { kind = "text", lines = camp_lines(st) },
      roster_widget(st),
      { kind = "actions", entries = docket(st) },
    },
  }
end

-- ---------------------------------------------------------------------------
-- QUARTERMASTER (the standing trade; the hot offers are the fire's own)
-- ---------------------------------------------------------------------------

local function provisions_row(tier)
  local shown = og.min(tier + 1, PROVISION_CAP)
  local row = {
    id = "provisions",
    kind = "action",
    label = "PROVISION PACKS",
    cost = PROVISION_PRICE,
    note = "tier " .. shown .. " of 3",
  }
  if tier >= PROVISION_CAP then
    row.done = true
  end
  return row
end

local function watch_row()
  if not og.campaign_level_completed(7) then
    return nil
  end
  local row = {
    id = "watch_pay",
    kind = "action",
    label = "THE WATCH'S PAY",
    cost = WATCH_PRICE,
    note = "a debt honored",
  }
  if og.campaign_state_get("watch_paid") ~= 0 then
    row.done = true
  end
  return row
end

-- The shelf's own lines, plus the closed hiring board while the split holds:
-- the trade door is where a player brings gold, so it is where a purchase
-- the camp will not sell has to be explained.
local function stores_lines(st)
  local lines = {}
  for i = 1, #stanzas.STORES do
    lines[i] = stanzas.STORES[i]
  end
  if hiring_closed(st) then
    lines[#lines + 1] = stanzas.STORES_NO_HIRE
  end
  return lines
end

local function stores_page()
  local st = camp_state()
  local entries = {}
  entries[1] = provisions_row(og.campaign_state_get("provisions"))
  local watch = watch_row()
  if watch ~= nil then
    entries[2] = watch
  end
  return {
    title = "QUARTERMASTER",
    lines = stores_lines(st),
    entries = entries,
  }
end

-- ---------------------------------------------------------------------------
-- THE LEDGER (lines only; Back is the way home)
-- ---------------------------------------------------------------------------

local function watch_deed_line()
  if og.campaign_state_get("watch_paid") == 0 then
    return nil
  end
  return "The Watch drinks to your name."
end

local function delve_deed_line()
  if og.campaign_state_get("delve_counted") ~= 0 then
    return "The delve gold rides with us."
  end
  if og.campaign_state_get("delve_sunk") ~= 0 then
    return "The river keeps what you would not."
  end
  return nil
end

local function packs_deed_line()
  local tier = og.campaign_state_get("provisions")
  if tier <= 0 then
    return nil
  end
  local word = stanzas.TIER_WORDS[og.min(tier, PROVISION_CAP)]
  return "Packs at tier " .. word .. ". Heavy, and glad."
end

local function bread_deed_line()
  if og.campaign_state_get("sneak_bread") == 0 then
    return nil
  end
  return "Sneak ate at your fire."
end

-- The three kindnesses the Pilgrim counts: wages paid, bread shared, hoard
-- refused. Two of them are enough for the ledger to hint at the third — but
-- a counted hoard closed the hand, and the ledger never promises what the
-- quay will not deliver.
local function generosity_keys()
  local keys = 0
  if og.campaign_state_get("watch_paid") ~= 0 then
    keys = keys + 1
  end
  if og.campaign_state_get("sneak_bread") ~= 0 then
    keys = keys + 1
  end
  if og.campaign_state_get("delve_sunk") ~= 0 then
    keys = keys + 1
  end
  return keys
end

local function grace_line()
  if og.campaign_state_get("delve_counted") ~= 0 then
    return nil
  end
  if generosity_keys() < 2 then
    return nil
  end
  return "Grace follows the open hand."
end

-- While the roads are apart the ledger keeps both columns' positions; once
-- they are walked it recounts them instead. 24 is reachable from both roads,
-- and a company that walked both hears both.
local function ledger_roads(lines, st)
  if st.split_live then
    -- The ledger records, never foretells: until a road is actually
    -- marched the book holds no front positions (the swearing window
    -- would otherwise read as prophecy).
    if not st.started then
      push_capped(lines, "Nothing is written past the Falls.", LEDGER_LINES)
      return
    end
    push_capped(lines, war_line(st), LEDGER_LINES)
    push_capped(lines, burden_line(st), LEDGER_LINES)
    return
  end
  if og.campaign_level_completed(13) then
    push_capped(lines, "West, we rode to the horns of war.", LEDGER_LINES)
  end
  if og.campaign_level_completed(19) then
    push_capped(lines, "East, we walked the drowned road.", LEDGER_LINES)
  end
end

local function ledger_page()
  local st = camp_state()
  local lines = {}
  push_capped(lines, watch_deed_line(), LEDGER_LINES)
  push_capped(lines, delve_deed_line(), LEDGER_LINES)
  push_capped(lines, packs_deed_line(), LEDGER_LINES)
  push_capped(lines, bread_deed_line(), LEDGER_LINES)
  push_capped(lines, grace_line(), LEDGER_LINES)
  ledger_roads(lines, st)
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
  base_camp = base_camp,
  picker_menu = function(page_id)
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
