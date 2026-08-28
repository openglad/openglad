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

-- The TEAMS/FILL macros (amendment 5, G1-G3): two rows over the ONE
-- per-team fill array — no second store. The faces DERIVE from the array
-- on every refetch, every write goes through og.campaign_match_set
-- ("fill_N"), and LINEUP keeps per-team authority: a band diverged there
-- reads back here as MIXED, never as a value of the camp's own invention.

local FILL_WORDS = { "WEAK", "FAIR", "STRONG", "BRUTAL" }
local COUNT_WORDS = { "One", "Two", "Three", "Four" }
local FILL_FAIR = 2
local TEAMS_CYCLE = { 2, 3, 4 }
local FILL_CYCLE = { 0, 1, 2, 3, 4 }

-- A stored code's word. Junk reads NONE — E2's ruling, worn here the way
-- lineup_fill_name wears it: the storage default, and a promise of no
-- squad the level will not field.
local function fill_word(code)
  local word = FILL_WORDS[code]
  if word == nil then
    return "NONE"
  end
  return word
end

-- The knob a 0-based team's band answers to: the 1-based team digit of
-- the fill_N vocabulary.
local function fill_key(team)
  return "fill_" .. (team + 1)
end

-- The three opponents in G2's choosing order: ascending team index,
-- skipping the local seat's own team (og.campaign_my_team, the G4
-- binding).
local function opponents()
  local mine = og.campaign_my_team()
  local list = {}
  for team = 0, 3 do
    if team ~= mine then
      list[#list + 1] = team
    end
  end
  return list
end

-- The opponents whose band is ON (fill ~= NONE), each with the code it
-- holds. Opponents only: this list is the SIDES count, and the local
-- team is already at the table.
local function on_opponents()
  local opp = opponents()
  local on = {}
  for i = 1, #opp do
    local code = og.campaign_match_get(fill_key(opp[i]))
    if code ~= 0 then
      on[#on + 1] = { team = opp[i], code = code }
    end
  end
  return on
end

-- Every band the FILL face answers for (amendment 6, H2): the on
-- opponents plus the local seat's own band where it holds a word — one
-- uniform non-NONE rule instead of G's own-band carve-out, so a LINEUP
-- tweak to the own band reads back MIXED here like any other. NONE stays
-- silent: a fresh TEAMS deal leaves the own band NONE and must not read
-- MIXED for it.
local function face_bands(on)
  local bands = {}
  for i = 1, #on do
    bands[#bands + 1] = on[i]
  end
  local mine = og.campaign_my_team()
  local code = og.campaign_match_get(fill_key(mine))
  if code ~= 0 then
    bands[#bands + 1] = { team = mine, code = code }
  end
  return bands
end

-- The FILL face's one number: the common code of the given bands, 0 with
-- none on, nil where LINEUP diverged them (the MIXED face).
local function common_fill(on)
  if #on == 0 then
    return 0
  end
  local code = on[1].code
  for i = 2, #on do
    if on[i].code ~= code then
      return nil
    end
  end
  return code
end

-- What a TEAMS turn deals its chosen opponents (G2, H2): the FILL row's
-- own value where the face names one — own band included, so a BRUTAL
-- own band deals BRUTAL sides — FAIR where the face reads NONE or MIXED.
local function effective_fill(on)
  local code = common_fill(face_bands(on))
  if code == nil or code == 0 then
    return FILL_FAIR
  end
  return code
end

local function teams_face(on)
  return "TEAMS: " .. (1 + #on)
end

local function fill_face(on)
  local code = common_fill(face_bands(on))
  if code == nil then
    return "FILL: MIXED"
  end
  return "FILL: " .. fill_word(code)
end

-- The second half of both macros' sentences: what was dealt, to how many.
local function squads_phrase(count, code)
  if count == 1 then
    return "One squad at " .. fill_word(code) .. "."
  end
  return COUNT_WORDS[count] .. " squads at " .. fill_word(code) .. "."
end

local function teams_said(n, code)
  return COUNT_WORDS[n] .. " sides. " .. squads_phrase(n - 1, code)
end

-- H3: the own band is a KNOB write, not a promised squad (a solo table
-- fields no allies), so it rides as a two-word tail. The wrap cleared it
-- with the rest, and "No squads." covers nothing fielded anywhere.
local function fill_said(code, count)
  if count == 0 then
    return "No squads."
  end
  return squads_phrase(count, code) .. " Yours too."
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

-- The same rules at note length for the MATCH SETUP row — the sides, the
-- fill and the score, now that the page turns all three (amendment 5).
-- Worst case "4-way, brutal, to 50" spends the whole 20-char note budget,
-- which is why the sides wear "-way" and not "sides". The sentence above
-- still stops at the score, and the TIME LIMIT row still wears its own
-- value where the host turns it (#241).
local function rules_digest()
  local on = on_opponents()
  local code = common_fill(face_bands(on))
  local word = "mixed"
  if code ~= nil then
    word = string.lower(fill_word(code))
  end
  local _, short_score = score_words()
  return (1 + #on) .. "-way, " .. word .. ", " .. short_score
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
-- box are their successors. Amendment 5 seats TEAMS and FILL back above
-- these rows as MACROS over that per-team array, not as knobs of their
-- own: they have no store, no key, and no slot in this table.
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

-- One step along a cycle, wrapping at the end. A value that is not on the
-- cycle at all — a match settled from the MATCHUP screen or a lobby, an
-- older save, or a derived face the wheel has no slot for (TEAMS: 1, FILL:
-- MIXED) — rejoins at the head rather than pretending to know where it
-- was.
local function next_value(cycle, current)
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

-- MATCH SETUP: the two macro rows over the knobs, each labelled with what
-- it holds and answering a click by stepping one on. TEAMS and FILL lead
-- (amendment 5): they are the rows a host reaches for first, and their
-- faces derive from the same fill array LINEUP owns band by band. The rows
-- are CUT for a non-host: the line carries the whole value of the page,
-- and a row whose only possible answer is a refusal is a row nobody should
-- be offered.
local function setup_page()
  local lines = { rules_line() }
  local entries = {}
  if og.campaign_is_host() then
    local on = on_opponents()
    entries[#entries + 1] = {
      id = "teams",
      kind = "action",
      label = teams_face(on),
      note = "2, 3, 4",
    }
    entries[#entries + 1] = {
      id = "fill",
      kind = "action",
      label = fill_face(on),
      note = "none to brutal",
    }
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
  local value = next_value(knob.cycle, og.campaign_match_get(knob.key))
  og.campaign_match_set(knob.key, value)
  return { message = knob.said(value) }
end

-- Turning TEAMS (G2): field n sides total — the local seat's team plus
-- n-1 opponents in ascending order — each dealt the FILL row's effective
-- value, the rest turned NONE. The face is derived, so the all-NONE rest
-- reads TEAMS: 1, a value off the wheel, and the first click rejoins at
-- the head: 2.
local function turn_teams()
  if not og.campaign_is_host() then
    return { message = "The host calls the rules." }
  end
  local on = on_opponents()
  local n = next_value(TEAMS_CYCLE, 1 + #on)
  local code = effective_fill(on)
  local opp = opponents()
  for i = 1, #opp do
    local value = 0
    if i <= n - 1 then
      value = code
    end
    og.campaign_match_set(fill_key(opp[i]), value)
  end
  return { message = teams_said(n, code) }
end

-- Turning FILL (G3, H1): one step along the wheel, written to every on
-- opponent AND the local seat's own band — the maintainer's "all selected
-- teams", and the own band is the allies knob; with no opponent on the
-- lowest one turns on at the new value — the TEAMS: 2 shape. NONE
-- included: the wrap clears the own band with the rest. A MIXED face is
-- off the wheel and rejoins at the head, NONE, the same rule every knob
-- applies to a value it cannot place.
local function turn_fill()
  if not og.campaign_is_host() then
    return { message = "The host calls the rules." }
  end
  local on = on_opponents()
  local current = common_fill(face_bands(on))
  if current == nil then
    current = -1
  end
  local code = next_value(FILL_CYCLE, current)
  local targets = {}
  for i = 1, #on do
    targets[#targets + 1] = on[i].team
  end
  if #targets == 0 then
    targets[1] = opponents()[1]
  end
  local count = #targets
  targets[#targets + 1] = og.campaign_my_team()
  for i = 1, #targets do
    og.campaign_match_set(fill_key(targets[i]), code)
  end
  if code == 0 then
    count = 0
  end
  return { message = fill_said(code, count) }
end

local function picker_action(entry_id)
  if entry_id == "random_scenario" then
    return random_scenario()
  end
  if entry_id == "sign" then
    return sign_book()
  end
  if entry_id == "teams" then
    return turn_teams()
  end
  if entry_id == "fill" then
    return turn_fill()
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
