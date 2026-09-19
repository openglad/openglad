-- MULTIPLAYER ARENAS — the Base Camp: the cleared tally, the game and arena shortcuts into SETUP, the RANDOM ARENA roll, and the knobs the current game uses (match_knobs) (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Issues #206 (the campaign's own pages), #304 (the SETUP wizard hosts
-- them), #305 (the ball games buy bodies above FAIR) and #306 (the
-- engine's words, not a narrator's), composed onto the Base Camp zone
-- contract (docs/basecamp-zones-design.md, "The four camps"). Pure by
-- contract (docs/campaign-scripting-design.md, "The picker contract"):
-- page id in, page description out, nothing cached between fetches — and
-- the same rule binds base_camp, fetched on the zone's own cadence, and
-- match_knobs, fetched per navigation and per deal. NO campaign state is
-- read or written any more: the retired keys book_signed, card_seq and
-- deck_cut may persist as dead rows in old saves, and nothing reads them
-- (harmless by design).

local levels = og.use("mode_levels")
local lineup = og.use("core:lineup")
local shape = og.use("mode_shape")

-- The seven games, in the campaign.yaml description's own order. Page id =
-- the manifest's mode tag (the v1 page ids, kept: the arena pages ARE
-- those pages); `flavor` is the game's rule line, which heads its page.
local MODES = {
  {
    id = "tdm",
    title = "TEAM DEATHMATCH",
    flavor = "First team to the kill target wins.",
  },
  {
    id = "ctf",
    title = "CAPTURE THE FLAG",
    flavor = "Take their flag home. Keep yours.",
  },
  {
    id = "onslaught",
    title = "ONSLAUGHT",
    flavor = "Last team with engines standing wins.",
  },
  {
    id = "mutant",
    title = "MUTANT",
    flavor = "Every kill changes your form.",
  },
  {
    id = "soccer",
    title = "SOCCER",
    flavor = "Kick the ball into their goal.",
  },
  {
    id = "basketball",
    title = "BASKETBALL",
    flavor = "Shoot or dunk. Most points wins.",
  },
  {
    id = "ffa",
    title = "FREE FOR ALL",
    flavor = "Every fighter for themselves.",
  },
}

-- The full authored id space (the widest band any mode script scans), so a
-- future arena outside 300..899 can never silently vanish from the index.
local FIRST_SCAN_ID = 0
local LAST_SCAN_ID = 1023

-- The camp's grid, from docs/basecamp-zones-design.md ("Bounds
-- arithmetic"): 8 whole-row units, of which the roster keeps its column
-- heading plus two hero rows. What is left is every line and every docket
-- row the camp gets to show at once.
local ZONE_ROW_UNITS = 8
local ROSTER_FLOOR_UNITS = 3

-- The ordered manifest walk: every authored row ascending by id (40 rows,
-- the generator's own hard count). Re-derived per fetch — nothing here
-- keeps state.
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
-- reads as the arena number.
local function stripped_title(id)
  local title = og.campaign_scenario_title(id)
  if title == "" then
    return "ARENA " .. id
  end
  local cut = string.find(title, ": ", 1, true)
  if cut == nil then
    return title
  end
  return string.sub(title, cut + 2)
end

-- The clock the arena will actually run, in ticks: the TIME LIMIT knob
-- when the host turned it, the row's own manifest value while the knob is
-- still MAP (0). Same precedence mode_match.resolve_time_limit applies in
-- the sim (`requested > 0` wins), read through the accessor the camp has —
-- the picker runs outside a world, so og.campaign_match_get is its
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
-- "20m" and not "20 min": "ARENA: DUNGEON OF STARS" spends 23 of those
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

-- How many of a walk's arenas are cleared. One counter serves the whole
-- manifest and a single game's band: band_rows hands back the same
-- { id, row } shape manifest_rows does.
local function cleared_count(rows)
  local cleared = 0
  for i = 1, #rows do
    if og.campaign_level_completed(rows[i].id) then
      cleared = cleared + 1
    end
  end
  return cleared
end

-- One game's progress in the engine's own word. The tally takes the header
-- readout's own "12/40" shape so the camp row, the index row and the
-- CLEARED cell all read alike (and fit the 42-char panel row beside a
-- 22-character game name).
local function cleared_note(band)
  return cleared_count(band) .. "/" .. #band .. " cleared"
end

-- The next uncleared arena scanning forward from the campaign cursor,
-- wrapping past the band end (the 509-to-500 rotation
-- docs/mp-game-modes.md promises). nil when the whole band is cleared.
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

-- The pairing the camp is set for: the game and arena the campaign cursor
-- sits on. A dangling cursor — winning a band's last arena parks the
-- cursor one past it — falls back to the first game still holding an
-- uncleared arena; a wholly cleared campaign falls back to the first arena
-- of the first game. This is the ONE owner of level -> game: the camp's
-- ARENA row, the wizard's ARENA step (match_knobs.arena_page) and the
-- roll's step-on all read it. The camp names the pairing, it does not fix
-- progression.
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

-- GAMES — the index the SETUP wizard's GAME step hosts, and the page the
-- camp's GAME: row opens. It answers the ROOT page id as well as "games":
-- the wizard fetches the root, the docket row keeps the id it always had.
-- Browsing costs nothing on any machine, joiners included.
local function games_page()
  local rows = manifest_rows()
  local cleared = cleared_count(rows)
  local entries = {}
  for i = 1, #MODES do
    local mode = MODES[i]
    local band = band_rows(mode.id)
    entries[#entries + 1] = {
      id = mode.id,
      kind = "page",
      label = mode.title,
      note = cleared_note(band),
    }
  end
  local lines = {
    "Cleared: " .. cleared .. " of " .. #rows .. ".",
  }
  return {
    title = "GAMES",
    lines = lines,
    entries = entries,
  }
end

-- One game's arena page — what the wizard's ARENA step shows: its arenas
-- as selectable rows, the game's rule line, and where to go next.
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
    -- Never "shut": #207 kept every cleared arena replayable, and the rows
    -- below this line are all still live.
    call_line = "Every arena here is cleared."
  else
    call_line = "Next uncleared: " .. stripped_title(call) .. "."
  end
  return {
    title = mode.title,
    lines = { mode.flavor, call_line },
    entries = entries,
  }
end

local function picker_menu(page_id)
  if page_id == "" or page_id == "games" then
    return games_page()
  end
  for i = 1, #MODES do
    if MODES[i].id == page_id then
      return mode_page(MODES[i])
    end
  end
  return nil
end

-- The Base Camp composition: the cleared tally overhead, the current
-- pairing as two one-click shortcuts INTO the SETUP wizard (#304, D28 —
-- the GAME: row lands on its GAME step, the ARENA: row on the page that
-- lists this arena), and the roll beside them.
--
-- The camp's grid is 8 units and the roster's floor takes three of them
-- (its column heading plus two hero rows), so text lines and docket rows
-- share the other FIVE (docs/basecamp-zones-design.md, "Bounds
-- arithmetic"). Neither face spends one on a line: the two rows already
-- name the host's game and arena, and the whole match is stated on the
-- wizard's own steps. A row past the free band does not page politely here
-- — it hides one behind an arrow — so nothing in this function is allowed
-- to grow the docket past it.
--
-- A joiner keeps both rows as its browsable index and loses only the row
-- it could never play: the roll cannot be played by a machine that does
-- not set the level, so it is cut at fetch rather than left to refuse.
-- The unit that frees goes to the roster.
local function base_camp()
  local rows = manifest_rows()
  local cleared = cleared_count(rows)
  local host = og.campaign_is_host()
  local pair = current_pair()
  local entries = {}
  entries[#entries + 1] = {
    id = "games",
    kind = "page",
    label = "GAME: " .. pair.mode.title,
    note = cleared_note(band_rows(pair.mode.id)),
  }
  entries[#entries + 1] = {
    id = pair.mode.id,
    kind = "page",
    label = "ARENA: " .. stripped_title(pair.id),
    note = mode_note(pair.mode.id, pair.row),
  }
  if host then
    -- An ACTION, not a level row: the arena is only known after the roll,
    -- so the row may wear its own name here — the engine's confirmation
    -- still names something playable ("Level set to CENTWHEIT MANOR."),
    -- because a level set through the roll runs the same gated tail and
    -- the same toast as a level row's click.
    entries[#entries + 1] = {
      id = "random_scenario",
      kind = "action",
      label = "RANDOM ARENA",
      note = "any game, any arena",
    }
  end
  -- The band the docket may spend, stated rather than assumed: an actions
  -- widget that does not weigh itself takes THREE units however many rows
  -- it carries, which is how a four-row docket ends up as three rows and
  -- two unlabelled arrows. Weighing it past the band is worse than paging
  -- — an over-budget composition falls back to the default zone and the
  -- camp disappears — so the ask is clamped to what is actually free.
  local docket_units = math.min(#entries,
    ZONE_ROW_UNITS - ROSTER_FLOOR_UNITS)
  local widgets = {
    {
      kind = "readout",
      -- The tally alone. The purse is already inked in the C++ header cell
      -- a few pixels above this band on every surface, and a campaign that
      -- composed its own GOLD would print the same wallet twice — twice
      -- over, since the header cell spells an infinite purse "INF" and a
      -- scripted one can only push the raw number.
      items = {
        { label = "CLEARED", value = cleared .. "/" .. #rows },
      },
    },
  }
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

-- RANDOM ARENA: one roll over the whole ordered manifest, answered as
-- `level` so the ENGINE runs its own gated set tail (host gate, load
-- rollback) and speaks its own confirmation ("Level set to <arena>.").
-- A roll that lands on the arena the camp is already set to steps one row
-- on, wrapping — a button that changes nothing is not a roll. The roll
-- happens HERE, at dispatch, never in a fetch: a random pick computed at
-- fetch time would keep state, and base_camp stays pure.
local function random_scenario()
  local rows = manifest_rows()
  local i = og.campaign_random(#rows)
  if rows[i].id == current_pair().id and #rows > 1 then
    i = og.mod(i, #rows) + 1
  end
  return { level = rows[i].id }
end

-- Which knobs the current game uses, which root row lists its arena, and
-- what a fresh arena deals (docs/match-setup-design.md, match_knobs): ONE
-- table keyed by the mode id; the engine spells no mode fact. Fetched per
-- navigation and per deal, never per frame; pure.
local MODE_KNOBS = {
  tdm = {},
  ctf = {},
  soccer = {},
  basketball = {},
  onslaught = { teams = false, fill = false, score = false },
  mutant = { teams = false, fill = "band", lines = { "FILL sets how strong the bots are." } },
  ffa = { teams = false, fill = "band", lines = { "FILL sets how strong the bots are." } },
}
-- The ball games buy bodies above FAIR (mode_shape.bodies, #305): the line
-- says so, and the arena deals STRONG so the extra body is already there
-- at the shipped default.
local BODIES_LINE = "STRONG adds a fighter, BRUTAL two."

local function match_knobs()
  local mode = current_pair().mode.id
  local knobs = MODE_KNOBS[mode]
  local out = {
    teams = knobs.teams,
    fill = knobs.fill,
    score = knobs.score,
    time = knobs.time,
    lines = knobs.lines,
    arena_page = mode,
  }
  local body = shape.of(mode)
  if body ~= nil and body.bodies then
    out.deal = "strong"
    out.lines = { BODIES_LINE }
  end
  return out
end

local function picker_action(entry_id)
  if entry_id == "random_scenario" then
    return random_scenario()
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
  match_knobs = match_knobs,
  lineup = {
    power = lineup_power,
  },
})
