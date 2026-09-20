-- MULTIPLAYER ARENAS — the Base Camp's one SETUP row into the wizard, the GAME and ARENA pages the wizard hosts, the two RANDOM rows over one roll, and the knobs the current game uses (match_knobs) (cookbook: docs/lua-classpacks-design.md §3).
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

-- How big a game is, which is all the index row has to say about it. A
-- count is a fact; the tally it replaced was a score, and this campaign
-- keeps no score.
local function arenas_note(band)
  if #band == 1 then
    return "1 arena"
  end
  return #band .. " arenas"
end

-- The pairing the camp is set for: the game and arena the campaign cursor
-- sits on. A dangling cursor — winning a band's last arena parks the
-- cursor one past it — falls back to the arena with the GREATEST id below
-- it, which is the one just played, so the camp stays on that game; a
-- cursor below every arena falls back to the first arena of the first
-- game. Pure ordering, no completion read. This is the ONE owner of
-- level -> game: the camp's SETUP row, the wizard's ARENA step
-- (match_knobs.arena_page) and the roll's step-on all read it. The camp
-- names the pairing, it does not fix progression.
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
  local below = nil
  for i = 1, #MODES do
    local band = band_rows(MODES[i].id)
    for j = 1, #band do
      if band[j].id < cursor then
        if below == nil or band[j].id > below.id then
          below = { mode = MODES[i], id = band[j].id, row = band[j].row }
        end
      end
    end
  end
  if below ~= nil then
    return below
  end
  local first = band_rows(MODES[1].id)[1]
  return { mode = MODES[1], id = first.id, row = first.row }
end

-- The roll behind BOTH RANDOM rows: one implementation over whatever list
-- of manifest rows the caller hands it — the whole manifest for the GAME
-- step's row, one band for an arena page's. A roll that lands on the arena
-- the campaign is already set to steps one row on, wrapping: a button that
-- changes nothing is not a roll. The answer is a `level`, so the ENGINE
-- runs its own gated set tail (host gate, load rollback) and speaks its own
-- confirmation ("Level set to <arena>."). The roll happens HERE, at
-- dispatch, never in a fetch: a random pick computed at fetch time would
-- keep state, and every page stays pure.
local function roll(rows)
  if #rows == 0 then
    return nil
  end
  local i = og.campaign_random(#rows)
  if rows[i].id == current_pair().id and #rows > 1 then
    i = og.mod(i, #rows) + 1
  end
  return { level = rows[i].id }
end

-- GAMES — the index the SETUP wizard's GAME step hosts, and the page the
-- camp's SETUP row opens. It answers the ROOT page id as well as "games":
-- the wizard fetches the root, the docket row keeps the id it always had.
-- Browsing costs nothing on any machine, joiners included.
local function games_page()
  local entries = {}
  for i = 1, #MODES do
    local mode = MODES[i]
    entries[#entries + 1] = {
      id = mode.id,
      kind = "page",
      label = mode.title,
      note = arenas_note(band_rows(mode.id)),
    }
  end
  -- Appended LAST so every game keeps the ordinal it already had, and cut
  -- at FETCH for a joiner: the SDL "(HOST)" face is level-rows-only, so an
  -- ungated action row would be a dead button on a machine that cannot set
  -- the level.
  if og.campaign_is_host() then
    entries[#entries + 1] = {
      id = "random",
      kind = "action",
      label = "RANDOM",
      note = "any game, any arena",
    }
  end
  return {
    title = "GAMES",
    entries = entries,
  }
end

-- One game's arena page — what the wizard's ARENA step shows: its arenas
-- as selectable rows under the game's rule line, and the band's own roll.
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
  -- The band's own roll, last row and host only, exactly as on the index.
  -- The row id carries the mode tag, so the dispatch rolls THIS page's
  -- band without reading the cursor.
  if og.campaign_is_host() then
    entries[#entries + 1] = {
      id = "random_" .. mode.id,
      kind = "action",
      label = "RANDOM ARENA",
      note = "any arena of this game",
    }
  end
  return {
    title = mode.title,
    lines = { mode.flavor },
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

-- The Base Camp composition: ONE docket row, which states the match the
-- campaign is set to and opens the SETUP wizard on its GAME step (#304,
-- D28 — the id "games" names no ROOT page row, so the wizard stays there).
-- One door, on this client and on every other.
--
-- The roster LEADS. That is not cosmetic: a composition whose roster is not
-- the first widget spends a unit on its own column heading and leaves the
-- classic y=33 header band empty, which reads as a broken screen (the trap
-- campaign_picker_session.cpp:420-425 names). Leading, the roster keeps the
-- heading outside the grid and every unit the one-row docket does not take.
--
-- The row's note is the scenario's OWN title, upper-cased — the same bytes
-- the panel's second header line and the wizard's MATCH step already print,
-- so the three agree. string.upper because the generated titles are mixed
-- case. A joiner browses the identical row: there is nothing on this docket
-- that a machine which does not set the level cannot read.
local function base_camp()
  local pair = current_pair()
  return {
    widgets = {
      {
        kind = "roster",
      },
      {
        kind = "actions",
        weight = 1,
        entries = {
          {
            id = "games",
            kind = "page",
            label = "SETUP",
            note = string.upper(og.campaign_scenario_title(pair.id)),
          },
        },
      },
    },
  }
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

-- The two RANDOM rows, one roll: the index's row covers the whole ordered
-- manifest, an arena page's row covers the band whose tag its id carries.
-- Any other entry id is a served no-op — the retired "random_scenario"
-- among them, so an old surface pressing it sets nothing.
local function picker_action(entry_id)
  if entry_id == "random" then
    return roll(manifest_rows())
  end
  if string.sub(entry_id, 1, 7) == "random_" then
    local tag = string.sub(entry_id, 8)
    for i = 1, #MODES do
      if MODES[i].id == tag then
        return roll(band_rows(tag))
      end
    end
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
