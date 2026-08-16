-- The Gamesmaster's Book — the Multiplayer Modes campaign's scripted picker: mode pages with live tallies, TONIGHT'S CARD, MATCH SETUP presets, SIGN THE BOOK (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Issues #206 (the book itself) and #212 (the MATCH SETUP page). Pure by
-- contract (docs/campaign-scripting-design.md, "The picker contract"):
-- page id in, page description out, nothing cached between fetches. The
-- three navigation keys the book remembers — card_seq, deck_cut,
-- book_signed — live in og.campaign_state_get/set only; no campaign vars
-- are registered, no level ever reads them, and no page fetch writes them
-- (the deck cut latches only inside the SHUFFLE action, so renders before
-- the first shuffle use the constant default cut of 1).

local levels = og.use("mode_levels")

-- The seven games, in the campaign.yaml description's own order. Page id =
-- the manifest's mode tag; `flavor` opens the mode page.
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

-- The card page speaks the long game name in its note.
local LONG_NAMES = {
  tdm = "team deathmatch",
  ctf = "capture the flag",
  onslaught = "onslaught",
  mutant = "mutant",
  soccer = "soccer",
  basketball = "basketball",
  ffa = "free for all",
}

-- The full authored id space (the widest band any mode script scans), so a
-- future arena outside 300..899 can never silently vanish from the book.
local FIRST_SCAN_ID = 0
local LAST_SCAN_ID = 1023

-- The deck: 39 cards, stride 7 — coprime, so every card is dealt before
-- any repeat.
local DECK_SIZE = 39
local DECK_STRIDE = 7

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

-- The facts each mode's rows post, straight from the manifest row: sides
-- and the posted score, minutes for CTF's flag rule, lives for
-- Onslaught's elimination, heads and shifters for the roster games.
local function mode_note(mode, row)
  if mode == "tdm" then
    return row.teams .. " teams, to " .. row.score_limit
  end
  if mode == "ctf" then
    return row.teams .. " sides, " .. og.div(row.time_limit, 720) .. " min"
  end
  if mode == "onslaught" then
    return row.teams .. " sides, " .. row.spawn_caps[0] .. " lives"
  end
  if mode == "mutant" then
    return row.fighters .. " shifters, to " .. row.score_limit
  end
  if mode == "soccer" then
    return row.teams .. " sides, first to " .. row.score_limit
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

local function band_won(band)
  local won = 0
  for i = 1, #band do
    if og.campaign_level_completed(band[i].id) then
      won = won + 1
    end
  end
  return won
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

-- The deck cut, defaulting to the constant 1 until the first shuffle
-- latches the company's own cut (0 = never cut; the latched range is
-- 1..39, so the sentinel is unambiguous).
local function deck_cut()
  local cut = og.campaign_state_get("deck_cut")
  if cut == 0 then
    return 1
  end
  return cut
end

-- Tonight's draw: the ordered-manifest row the cut and the deal count
-- select. Stride 7 over 39 cards walks the whole deck before any repeat.
local function drawn_row()
  local rows = manifest_rows()
  local seq = og.campaign_state_get("card_seq")
  local idx = og.mod((deck_cut() - 1) + seq * DECK_STRIDE, DECK_SIZE)
  return rows[idx + 1]
end

-- The company's cut of the deck, computed at the FIRST shuffle only: the
-- sum of every hero's level, exp and name bytes, folded og.mod once at
-- the end into 1..39. An empty roster salts 0 and cuts 1.
local function roster_cut()
  local team = og.campaign_team()
  local salt = 0
  for i = 1, #team do
    local member = team[i]
    salt = salt + member.level + member.exp
    local name = member.name
    for j = 1, #name do
      salt = salt + string.byte(name, j)
    end
  end
  return og.mod(salt, DECK_SIZE) + 1
end

-- The root title: the Gamesmaster keeps the book until the signature
-- takes it. An empty roster leaves nobody to sign as, so the old title
-- stands.
local function root_title(signed)
  if signed == 0 then
    return "THE GAMESMASTER'S BOOK"
  end
  local team = og.campaign_team()
  if #team == 0 then
    return "THE GAMESMASTER'S BOOK"
  end
  return "THE BOOK OF " .. string.upper(team[1].name)
end

-- THE GAMESMASTER'S BOOK — the index page: seven games, the card, the
-- posted rules, and (at 39 of 39, unsigned) the signature line.
local function root_page()
  local rows = manifest_rows()
  local total_won = 0
  for i = 1, #rows do
    if og.campaign_level_completed(rows[i].id) then
      total_won = total_won + 1
    end
  end
  local entries = {}
  for i = 1, #MODES do
    local mode = MODES[i]
    local band = band_rows(mode.id)
    entries[#entries + 1] = {
      id = mode.id,
      kind = "page",
      label = mode.title,
      note = #band .. " arenas, " .. band_won(band) .. " won",
    }
  end
  entries[#entries + 1] = {
    id = "card",
    kind = "page",
    label = "TONIGHT'S CARD",
    note = stripped_title(drawn_row().id),
  }
  entries[#entries + 1] = {
    id = "setup",
    kind = "page",
    label = "MATCH SETUP",
    note = "the posted rules",
  }
  local signed = og.campaign_state_get("book_signed")
  local lines = {
    "The Gamesmaster opens the book.",
    "Seven games. Thirty-nine fields.",
    "Stamped: " .. total_won .. " of " .. #rows .. ".",
  }
  if total_won == #rows then
    if signed == 0 then
      lines[#lines + 1] = "The book is full. Sign it."
      entries[#entries + 1] = {
        id = "sign",
        kind = "action",
        label = "SIGN THE BOOK",
      }
    end
  end
  return {
    title = root_title(signed),
    lines = lines,
    entries = entries,
  }
end

-- One game's page: its arenas as selectable rows, the flavor line, and
-- the book's call.
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
    call_line = "This page is stamped shut."
  else
    call_line = "The book calls: " .. stripped_title(call) .. "."
  end
  return {
    title = mode.title,
    lines = { mode.flavor, call_line },
    entries = entries,
  }
end

-- TONIGHT'S CARD: one dealt arena and the shuffle.
local function card_page()
  local draw = drawn_row()
  return {
    title = "TONIGHT'S CARD",
    lines = {
      "The Gamesmaster cuts the deck once.",
      "Every card is dealt before any repeat.",
    },
    entries = {
      {
        id = tostring(draw.id),
        kind = "level",
        level = draw.id,
        label = stripped_title(draw.id),
        note = LONG_NAMES[draw.row.mode],
      },
      {
        id = "shuffle",
        kind = "action",
        label = "SHUFFLE THE DECK",
      },
    },
  }
end

-- MATCH SETUP (#212): the posted rules, spelled honestly — 0 keeps its
-- MATCHUP meaning (Auto teams, the map's own score, all troops), and an
-- off-menu number reads as itself.
local TROOPS_WORDS = {
  [0] = "all",
  [2] = "own",
  [3] = "fair",
}

local function teams_word()
  local teams = og.campaign_match_get("team_count")
  if teams == 0 then
    return "Auto"
  end
  return tostring(teams)
end

local function score_word()
  local limit = og.campaign_match_get("score_limit")
  if limit == 0 then
    return "map"
  end
  return tostring(limit)
end

local function troops_word()
  local strip = og.campaign_match_get("strip_troops")
  local word = TROOPS_WORDS[strip]
  if word == nil then
    return tostring(strip)
  end
  return word
end

local function setup_page()
  return {
    title = "MATCH SETUP",
    lines = {
      "Teams: " .. teams_word() .. ". Score: " .. score_word() .. ".",
      "Troops: " .. troops_word() .. ".",
    },
    entries = {
      {
        id = "two_sides",
        kind = "action",
        label = "TWO SIDES, FAIR BOTS",
      },
      {
        id = "four_sides",
        kind = "action",
        label = "FOUR SIDES, FAIR BOTS",
      },
      {
        id = "classic_rules",
        kind = "action",
        label = "CLASSIC RULES",
      },
      {
        id = "short_match",
        kind = "action",
        label = "SHORT MATCH",
      },
    },
  }
end

local function picker_menu(page_id)
  if page_id == "" then
    return root_page()
  end
  if page_id == "card" then
    return card_page()
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

-- SHUFFLE THE DECK: advance the deal (stored mod 39, so the int never
-- grows), and let the FIRST shuffle latch the company's cut.
local function shuffle_deck()
  local seq = og.campaign_state_get("card_seq")
  og.campaign_state_set("card_seq", og.mod(seq + 1, DECK_SIZE))
  if og.campaign_state_get("deck_cut") == 0 then
    og.campaign_state_set("deck_cut", roster_cut())
  end
  return { message = "The Gamesmaster deals again." }
end

-- SIGN THE BOOK: the completion flourish — the root page retitles for
-- good and the signature row disappears.
local function sign_book()
  og.campaign_state_set("book_signed", 1)
  return { message = "Your name goes in the book." }
end

-- The #212 presets. Writes go through og.campaign_match_set only in the
-- host's hand; every other machine is told whose book the rules live in.
local PRESETS = {
  two_sides = {
    { "team_count", 2 },
    { "strip_troops", 3 },
  },
  four_sides = {
    { "team_count", 4 },
    { "strip_troops", 3 },
  },
  classic_rules = {
    { "team_count", 0 },
    { "strip_troops", 0 },
    { "score_limit", 0 },
  },
  short_match = {
    { "score_limit", 5 },
  },
}

local function post_rules(writes)
  if not og.campaign_is_host() then
    return { message = "The host posts the rules." }
  end
  for i = 1, #writes do
    og.campaign_match_set(writes[i][1], writes[i][2])
  end
  return { message = "The Gamesmaster posts it." }
end

local function picker_action(entry_id)
  if entry_id == "shuffle" then
    return shuffle_deck()
  end
  if entry_id == "sign" then
    return sign_book()
  end
  local preset = PRESETS[entry_id]
  if preset ~= nil then
    return post_rules(preset)
  end
  return nil
end

og.register_campaign_hooks({
  picker_menu = picker_menu,
  picker_action = picker_action,
})
