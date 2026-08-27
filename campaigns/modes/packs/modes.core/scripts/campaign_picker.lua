-- THE GAMESMASTER'S TABLE — the Multiplayer Modes campaign's Base Camp: the stamp tally overhead, the current pairing as two doors, the RANDOM SCENARIO roll, MATCH SETUP; the seven-game index, the field pages and the signature behind them (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Issues #206 (the book itself) and #212 (the MATCH SETUP knobs),
-- recomposed onto the Base Camp zone contract
-- (docs/basecamp-zones-design.md, "The four camps"). Pure by contract
-- (docs/campaign-scripting-design.md, "The picker contract"): page id in,
-- page description out, nothing cached between fetches — and the same rule
-- binds base_camp, which is fetched on the zone's own cadence and never per
-- frame. The ONE navigation key the book remembers — book_signed — lives
-- in og.campaign_state_get/set only; no campaign vars are registered, no
-- level ever reads it, and no fetch writes it. (Two retired deck keys,
-- card_seq and deck_cut, may persist as dead rows in saves that shuffled
-- TONIGHT'S CARD before the RANDOM SCENARIO roll replaced it — nothing
-- reads them, harmless by design.)

local levels = og.use("mode_levels")
local lineup = og.use("core:lineup")

-- The seven games, in the campaign.yaml description's own order. Page id =
-- the manifest's mode tag (the v1 page ids, kept: the field pages ARE those
-- pages); `flavor` heads the game's field page.
local MODES = {
  {
    id = "tdm",
    title = "TEAM DEATHMATCH",
    flavor = "Four colors go in. One walks out.",
  },
  {
    id = "ctf",
    title = "CAPTURE THE FLAG",
    flavor = "Steal theirs. Keep yours. Simple.",
  },
  {
    id = "onslaught",
    title = "ONSLAUGHT",
    flavor = "Hold the line until there is no line.",
  },
  {
    id = "mutant",
    title = "MUTANT",
    flavor = "New shape every kill. Keep count.",
  },
  {
    id = "soccer",
    title = "SOCCER",
    flavor = "No hands. Blades are fine.",
  },
  {
    id = "basketball",
    title = "BASKETBALL",
    flavor = "The arc is chalk. The rim is law.",
  },
  {
    id = "ffa",
    title = "FREE FOR ALL",
    flavor = "No teams. No excuses. No refunds.",
  },
}

-- The full authored id space (the widest band any mode script scans), so a
-- future arena outside 300..899 can never silently vanish from the book.
local FIRST_SCAN_ID = 0
local LAST_SCAN_ID = 1023

-- The camp's grid, from docs/basecamp-zones-design.md ("Bounds
-- arithmetic"): 8 whole-row units, of which the roster keeps its column
-- heading plus two hero rows. What is left is every line and every docket
-- row the table gets to show at once.
local ZONE_ROW_UNITS = 8
local ROSTER_FLOOR_UNITS = 3

-- The ordered manifest walk: every authored row ascending by id (39 rows,
-- the generator's own hard count). Re-derived per fetch — the book keeps
-- no state.
local function manifest_rows()
  local rows = {}
  for id = FIRST_SCAN_ID, LAST_SCAN_ID do
    local row = levels.levels[id]
    if row ~= nil then
      rows[#rows + 1] = { id = id, row = row }
    end
  end
  return rows
end

-- An arena's display name: the scen title with the "<Mode>: " prefix cut
-- at the first ": ". A prefix-less title reads as itself; a missing one
-- reads as the field number.
local function stripped_title(id)
  local title = og.campaign_scenario_title(id)
  if title == "" then
    return "FIELD " .. id
  end
  local cut = string.find(title, ": ", 1, true)
  if cut == nil then
    return title
  end
  return string.sub(title, cut + 2)
end

-- The clock the field will actually run, in ticks: the TIME LIMIT knob when
-- the host turned it, the row's own manifest value while the knob is still
-- MAP (0). Same precedence mode_match.resolve_time_limit applies in the sim
-- (`requested > 0` wins), read through the accessor the camp has — the
-- picker runs outside a world, so og.campaign_match_get is its
-- og.match_setting. Without this the note advertised a clock the knob had
-- already overridden (#241).
local function clock_ticks(row)
  local requested = og.campaign_match_get("time_limit")
  if requested > 0 then
    return requested
  end
  return row.time_limit
end

-- The facts each mode's rows carry, straight from the manifest row: sides
-- and the arena's own score, minutes for CTF's flag rule, lives for
-- Onslaught's elimination, heads and shifters for the roster games. Every
-- note is budgeted against the camp's 42-char panel row carrying its
-- longest arena name and the door marker, which is why CTF's clock is
-- "20m" and not "20 min": "FIELD: DUNGEON OF STARS" spends 23 of those
-- characters before the note starts. CTF's clock is the one fact the host
-- can override, so it reads the resolved value, not the row's.
local function mode_note(mode, row)
  if mode == "tdm" then
    return row.teams .. " teams, to " .. row.score_limit
  end
  if mode == "ctf" then
    return row.teams .. " sides, " .. og.div(clock_ticks(row), 720) .. "m"
  end
  if mode == "onslaught" then
    return row.teams .. " sides, " .. row.spawn_caps[0] .. " lives"
  end
  if mode == "mutant" then
    return row.fighters .. " shifters, to " .. row.score_limit
  end
  if mode == "soccer" then
    return row.teams .. " sides, " .. row.score_limit .. " goals"
  end
  if mode == "basketball" then
    return row.teams .. " sides, to " .. row.score_limit
  end
  return row.fighters .. " heads, to " .. row.score_limit
end

-- One mode's manifest rows, ascending.
local function band_rows(mode)
  local band = {}
  local rows = manifest_rows()
  for i = 1, #rows do
    if rows[i].row.mode == mode then
      band[#band + 1] = rows[i]
    end
  end
  return band
end

-- How many of a row walk's fields carry a stamp. One counter serves the
-- whole manifest and a single game's band: band_rows hands back the same
-- { id, row } shape manifest_rows does.
local function stamped_count(rows)
  local stamped = 0
  for i = 1, #rows do
    if og.campaign_level_completed(rows[i].id) then
      stamped = stamped + 1
    end
  end
  return stamped
end

-- One game's progress in the book's ONE word for a cleared field. Fields
-- are stamped — never won — on every row that counts them, and the tally
-- takes the header readout's own "12/39" shape so the camp row, the index
-- row and the BOOK cell all read alike (and fit the 42-char panel row
-- beside a 22-character game name).
local function stamp_note(band)
  return stamped_count(band) .. "/" .. #band .. " stamped"
end

-- The ladder call: the first unstamped arena scanning forward from the
-- campaign cursor, wrapping past the band end (the 509-to-500 rotation
-- docs/mp-game-modes.md promises). nil when the whole band is stamped.
local function call_id(band)
  local cursor = og.campaign_current_level()
  for i = 1, #band do
    local id = band[i].id
    if id >= cursor then
      if not og.campaign_level_completed(id) then
        return id
      end
    end
  end
  for i = 1, #band do
    local id = band[i].id
    if id < cursor then
      if not og.campaign_level_completed(id) then
        return id
      end
    end
  end
  return nil
end

-- The pairing the table is set for: the game and field the campaign cursor
-- sits on. A dangling cursor — winning a band's last field parks the cursor
-- one past it — falls back to the first game still holding an unstamped
-- field; a wholly stamped book falls back to the first field of the first
-- game. The camp names the pairing, it does not fix progression.
local function current_pair()
  local cursor = og.campaign_current_level()
  for i = 1, #MODES do
    local band = band_rows(MODES[i].id)
    for j = 1, #band do
      if band[j].id == cursor then
        return { mode = MODES[i], id = band[j].id, row = band[j].row }
      end
    end
  end
  for i = 1, #MODES do
    local band = band_rows(MODES[i].id)
    local call = call_id(band)
    if call ~= nil then
      return { mode = MODES[i], id = call, row = levels.levels[call] }
    end
  end
  local first = band_rows(MODES[1].id)[1]
  return { mode = MODES[1], id = first.id, row = first.row }
end

-- The lead hero's name, uppercased — the signature the book takes. An
-- empty roster leaves nobody to sign as. Saved names are a 12-byte field,
-- so "THE BOOK OF <name>" never outgrows the page title's 24 characters.
local function lead_name()
  local team = og.campaign_team()
  if #team == 0 then
    return ""
  end
  return string.upper(team[1].name)
end

-- The index's cover: the Gamesmaster keeps the book until the signature
-- takes it, and then it keeps the name for good.
local function games_title(signed)
  if signed == 0 then
    return "SEVEN GAMES"
  end
  local name = lead_name()
  if name == "" then
    return "SEVEN GAMES"
  end
  return "THE BOOK OF " .. name
end

-- The rules the table plays (#212), spelled honestly — 0 keeps its MATCHUP
-- meaning (the map's own score), and an off-menu number reads as itself.
-- TEAMS is gone from the table (lineup amendment A1/A3) and TROOPS with it
-- (amendment B5): the sides are the LINEUP page's, band by band, and so is
-- whether each team's map-shipped units are fielded. The camp keeps only
-- what LINEUP has no home for.

-- The score in the two spellings the table needs: the sentence's "map
-- score" and the row note's bare "map".
local function score_words()
  local limit = og.campaign_match_get("score_limit")
  if limit == 0 then
    return "map score", "map"
  end
  local phrase = "to " .. limit
  return phrase, phrase
end

-- A sentence starts with a capital: the score phrase leads now that the
-- sides no longer do, and "map score" / "to 7" are lower-case words.
local function sentence(words)
  return string.upper(string.sub(words, 1, 1)) .. string.sub(words, 2)
end

-- What the table plays right now, as its one sentence. Worst case is
-- "Map score." at 10 of the 38-char line budget.
local function rules_line()
  local long_score = score_words()
  return sentence(long_score .. ".")
end

-- The same rules at note length for the MATCH SETUP row — worst case
-- "to 50" at 5 of the 20-char note budget.
-- Both summaries deliberately stop at the score: the TIME LIMIT row
-- already wears its own value where the host turns it, the sides are
-- LINEUP's to state, and so are the map units since amendment B5.
local function rules_digest()
  local _, short_score = score_words()
  return short_score
end

-- SEVEN GAMES — the index: what else the book holds, and how far each game
-- is stamped. Browsing it costs nothing on any machine, joiners included;
-- the one row that writes anything is the signature, and it is the host's.
-- SIGN THE BOOK lives HERE rather than at the camp because this is the
-- page the signature changes — the cover takes the name — and because the
-- camp's docket rows are the whole grid it can spend beside a roster.
local function games_page()
  local rows = manifest_rows()
  local stamped = stamped_count(rows)
  local signed = og.campaign_state_get("book_signed")
  local entries = {}
  for i = 1, #MODES do
    local mode = MODES[i]
    local band = band_rows(mode.id)
    entries[#entries + 1] = {
      id = mode.id,
      kind = "page",
      label = mode.title,
      note = stamp_note(band),
    }
  end
  local lines = {
    "Every game keeps its own page.",
    "Stamped: " .. stamped .. " of " .. #rows .. ".",
  }
  local can_sign = stamped == #rows
    and signed == 0
  if can_sign and og.campaign_is_host() then
    lines[#lines + 1] = "The book is full. Sign it."
    entries[#entries + 1] = {
      id = "sign",
      kind = "action",
      label = "SIGN THE BOOK",
      note = "your name, for good",
    }
  end
  return {
    title = games_title(signed),
    lines = lines,
    entries = entries,
  }
end

-- One game's field page: its arenas as selectable rows, the flavor line,
-- and the book's call. The call lives HERE and nowhere else — the table
-- upstairs offers no advice, only the roll.
local function mode_page(mode)
  local band = band_rows(mode.id)
  local entries = {}
  for i = 1, #band do
    local arena = band[i]
    entries[#entries + 1] = {
      id = tostring(arena.id),
      kind = "level",
      level = arena.id,
      label = stripped_title(arena.id),
      note = mode_note(mode.id, arena.row),
    }
  end
  local call = call_id(band)
  local call_line
  if call == nil then
    -- Never "shut": #207 kept every cleared field replayable, and the rows
    -- below this line are all still live.
    call_line = "Every field here is stamped."
  else
    call_line = "The book calls: " .. stripped_title(call) .. "."
  end
  return {
    title = mode.title,
    lines = { mode.flavor, call_line },
    entries = entries,
  }
end

-- The face a knob wears: the value it holds RIGHT NOW, in the row's upper
-- case. Every knob's 0 is the MATCHUP sentinel, and each spells it in its
-- own word.
local function score_face(value)
  if value == 0 then
    return "MAP"
  end
  return tostring(value)
end

-- The clock is stored in sim ticks (12/s, the manifest's own unit) and worn
-- in minutes, the way the CTF camp note already spells it. og.div, never
-- `/`: integer division is the determinism contract.
local function time_face(value)
  if value == 0 then
    return "MAP"
  end
  return og.div(value, 720) .. "M"
end

-- What the click just did, in the plainest words the table has. The three
-- zeroes say the same thing because they mean the same thing: whatever the
-- map itself authored.
local function score_said(value)
  if value == 0 then
    return "Score: the map's own."
  end
  return "Score to " .. value .. "."
end

local function time_said(value)
  if value == 0 then
    return "Clock: the map's own."
  end
  return "Clock: " .. og.div(value, 720) .. " minutes."
end

-- The two knobs MATCH SETUP turns, each written straight through
-- og.campaign_match_set: the key it writes, the cycle it steps (the
-- MATCHUP screen's own order — cycle_ctf_capture_limit), the face it wears
-- and the sentence it speaks. The note is the cycle itself: a row that
-- shows only what it holds hides where the next click lands. TEAMS was the
-- fourth until lineup amendment A1 retired it and TROOPS the third until
-- amendment B5 did — the LINEUP band's per-team FILL wheel and MAP UNITS
-- box are their successors, and neither ever had a camp row.
local KNOBS = {
  {
    id = "score",
    key = "score_limit",
    title = "TARGET SCORE",
    note = "map, 1, 3, 5, 10",
    cycle = { 0, 1, 3, 5, 10 },
    face = score_face,
    said = score_said,
  },
  -- The clock the modes actually run on (#241). The cycle holds every
  -- shipped manifest value except basketball's one short court, so a host
  -- can always dial the map's own number back explicitly.
  {
    id = "time",
    key = "time_limit",
    title = "TIME LIMIT",
    note = "map, 5, 10, 15, 20m",
    cycle = { 0, 3600, 7200, 10800, 14400 },
    face = time_face,
    said = time_said,
  },
}

-- One step along a knob's cycle, wrapping at the end. A value that is not
-- on the cycle at all — a match settled from the MATCHUP screen or a lobby,
-- or an older save — rejoins at the head rather than pretending to know
-- where it was.
local function next_value(knob, current)
  local cycle = knob.cycle
  for i = 1, #cycle do
    if cycle[i] == current then
      return cycle[og.mod(i, #cycle) + 1]
    end
  end
  return cycle[1]
end

local function knob_by_id(entry_id)
  for i = 1, #KNOBS do
    if KNOBS[i].id == entry_id then
      return KNOBS[i]
    end
  end
  return nil
end

-- MATCH SETUP: the knobs as rows, each labelled with what it
-- holds and answering a click by stepping one on. The rows are CUT for a
-- non-host: the line carries the whole value of the page, and a row whose
-- only possible answer is a refusal is a row nobody should be offered.
local function setup_page()
  local lines = { rules_line() }
  local entries = {}
  if og.campaign_is_host() then
    for i = 1, #KNOBS do
      local knob = KNOBS[i]
      local value = og.campaign_match_get(knob.key)
      entries[#entries + 1] = {
        id = knob.id,
        kind = "action",
        label = knob.title .. ": " .. knob.face(value),
        note = knob.note,
      }
    end
  else
    lines[#lines + 1] = "The host calls the rules."
  end
  return {
    title = "MATCH SETUP",
    lines = lines,
    entries = entries,
  }
end

local function picker_menu(page_id)
  if page_id == "games" then
    return games_page()
  end
  if page_id == "setup" then
    return setup_page()
  end
  for i = 1, #MODES do
    if MODES[i].id == page_id then
      return mode_page(MODES[i])
    end
  end
  return nil
end

-- THE GAMESMASTER'S TABLE — the Base Camp composition: the stamp tally
-- overhead, the current pairing as two one-deep doors, the RANDOM
-- SCENARIO roll beside them, and the match rules on the row that changes
-- them.
--
-- The camp's grid is 8 units and the roster's floor takes three of them
-- (its column heading plus two hero rows), so text lines and docket rows
-- share the other FIVE (docs/basecamp-zones-design.md, "Bounds
-- arithmetic"). The host spends four on rows and speaks no line: the rules
-- ARE the MATCH SETUP row's note, and the signature lives on the index
-- page whose cover it takes. A row past the free band does not page
-- politely here — it hides one behind an arrow — so nothing in this
-- function is allowed to grow the docket past it.
--
-- A joiner keeps the pairing, the rules and its own book, spends one row
-- unit on the line that says whose call the game is, and loses the row it
-- could never play: the roll cannot be played by a machine that does not
-- set the level, so it is cut at fetch rather than left to refuse.
local function base_camp()
  local rows = manifest_rows()
  local stamped = stamped_count(rows)
  local host = og.campaign_is_host()
  local can_sign = stamped == #rows
    and og.campaign_state_get("book_signed") == 0
  local pair = current_pair()
  local entries = {}
  -- The index door's note is its game's tally, except on the one book that
  -- has nothing left to tally: a full book asks for the signature waiting
  -- behind this very door.
  local game_note = stamp_note(band_rows(pair.mode.id))
  if host and can_sign then
    game_note = "sign the book"
  end
  entries[#entries + 1] = {
    id = "games",
    kind = "page",
    label = "GAME: " .. pair.mode.title,
    note = game_note,
  }
  entries[#entries + 1] = {
    id = pair.mode.id,
    kind = "page",
    label = "FIELD: " .. stripped_title(pair.id),
    note = mode_note(pair.mode.id, pair.row),
  }
  if host then
    -- An ACTION, not a level row: the arena is only known after the roll,
    -- so the ceremony may wear its own name here — the engine's
    -- confirmation still names something playable ("Level set to
    -- CENTWHEIT MANOR."), because a level set through the roll runs the
    -- same gated tail and the same toast as a level row's click.
    entries[#entries + 1] = {
      id = "random_scenario",
      kind = "action",
      label = "RANDOM SCENARIO",
      note = "any game, any field",
    }
  end
  entries[#entries + 1] = {
    id = "setup",
    kind = "page",
    label = "MATCH SETUP",
    note = rules_digest(),
  }
  -- The band the docket may spend, stated rather than assumed: an actions
  -- widget that does not weigh itself takes THREE units however many rows
  -- it carries, which is how a four-row docket ends up as three rows and
  -- two unlabelled arrows. Weighing it past the band is worse than paging
  -- — an over-budget composition falls back to the default zone and the
  -- camp disappears — so the ask is clamped to what is actually free.
  local text_units = 0
  if not host then
    text_units = 1
  end
  local docket_units = math.min(#entries,
    ZONE_ROW_UNITS - ROSTER_FLOOR_UNITS - text_units)
  local widgets = {
    {
      kind = "readout",
      -- BOOK alone. The purse is already inked in the C++ header cell a
      -- few pixels above this band on every surface, and a campaign that
      -- composed its own GOLD would print the same wallet twice — twice
      -- over, since the header cell spells an infinite purse "INF" and a
      -- scripted one can only push the raw number.
      items = {
        { label = "BOOK", value = stamped .. "/" .. #rows },
      },
    },
  }
  if not host then
    widgets[#widgets + 1] = {
      kind = "text",
      lines = { "The host calls the game." },
    }
  end
  widgets[#widgets + 1] = {
    kind = "actions",
    weight = docket_units,
    entries = entries,
  }
  widgets[#widgets + 1] = {
    kind = "roster",
  }
  return { widgets = widgets }
end

-- RANDOM SCENARIO: one roll over the whole ordered manifest, answered as
-- `level` so the ENGINE runs its own gated set tail (host gate, load
-- rollback) and speaks its own confirmation ("Level set to <arena>.").
-- A roll that lands on the field the table is already set to steps one
-- row on, wrapping — a button that changes nothing is not a roll. The
-- roll happens HERE, at dispatch, never in a fetch: a random pick
-- computed at fetch time would be the deck, re-labelled, and base_camp
-- stays pure.
local function random_scenario()
  local rows = manifest_rows()
  local i = og.campaign_random(#rows)
  if rows[i].id == current_pair().id and #rows > 1 then
    i = og.mod(i, #rows) + 1
  end
  return { level = rows[i].id }
end

-- SIGN THE BOOK: the completion flourish — the index retitles for good and
-- the signature row disappears from the table.
local function sign_book()
  og.campaign_state_set("book_signed", 1)
  return { message = "Your name goes in the book." }
end

-- Turning a knob: read what it holds, write the next value on the cycle,
-- and say the new state outright. The write goes through
-- og.campaign_match_set, which is the host's alone — the rows are cut on
-- every other machine, so this refusal is the backstop behind them, not the
-- thing a player is meant to meet.
local function turn_knob(knob)
  if not og.campaign_is_host() then
    return { message = "The host calls the rules." }
  end
  local value = next_value(knob, og.campaign_match_get(knob.key))
  og.campaign_match_set(knob.key, value)
  return { message = knob.said(value) }
end

local function picker_action(entry_id)
  if entry_id == "random_scenario" then
    return random_scenario()
  end
  if entry_id == "sign" then
    return sign_book()
  end
  local knob = knob_by_id(entry_id)
  if knob ~= nil then
    return turn_knob(knob)
  end
  return nil
end

-- The LINEUP hook (docs/lineup-design.md §4): the power function alone
-- since amendment B1 replaced the BOTS preset wheel with the five-value
-- FILL wheel, which names nothing a campaign owns. It is the core pack's
-- own stat_power over the engine's derived-stat row (C1 moved the match
-- machinery there; this registration points straight at the shared lib),
-- so the bands price a fighter with the exact metric the FILL solver
-- measures against. The qualified og.use works here because the campaign
-- VM loads every installed pack's lib modules exactly like a world VM,
-- and stat_power spends nothing but og.div, which the campaign fence
-- leaves open (clock_ticks above already relies on that).
local function lineup_power(row)
  return lineup.stat_power(row.hp, row.mp, row.armor, row.damage,
                           row.stepsize, row.fire_frequency, row.level)
end

og.register_campaign_hooks({
  base_camp = base_camp,
  picker_menu = picker_menu,
  picker_action = picker_action,
  lineup = {
    power = lineup_power,
  },
})
