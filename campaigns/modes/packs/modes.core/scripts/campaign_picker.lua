-- THE GAMESMASTER'S TABLE — the Multiplayer Modes campaign's Base Camp: the stamp tally overhead, the current pairing as two doors, tonight's card and the shuffle, MATCH SETUP; the seven-game index, the field pages and the signature behind them (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Issues #206 (the book itself) and #212 (the MATCH SETUP presets),
-- recomposed onto the Base Camp zone contract
-- (docs/basecamp-zones-design.md, "The four camps"). Pure by contract
-- (docs/campaign-scripting-design.md, "The picker contract"): page id in,
-- page description out, nothing cached between fetches — and the same rule
-- binds base_camp, which is fetched on the zone's own cadence and never per
-- frame. The three navigation keys the book remembers — card_seq, deck_cut,
-- book_signed — live in og.campaign_state_get/set only; no campaign vars
-- are registered, no level ever reads them, and no fetch writes them (the
-- deck cut latches only inside the SHUFFLE action, so renders before the
-- first shuffle use the constant default cut of 1).

local levels = og.use("mode_levels")

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

-- The deck: 39 cards, stride 7 — coprime, so every card is dealt before
-- any repeat.
local DECK_SIZE = 39
local DECK_STRIDE = 7

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

-- The facts each mode's rows post, straight from the manifest row: sides
-- and the posted score, minutes for CTF's flag rule, lives for
-- Onslaught's elimination, heads and shifters for the roster games. Every
-- note is budgeted against the camp's 42-char panel row carrying its
-- longest arena name and the door marker, which is why CTF's clock is
-- "20m" and not "20 min": "FIELD: DUNGEON OF STARS" spends 23 of those
-- characters before the note starts.
local function mode_note(mode, row)
  if mode == "tdm" then
    return row.teams .. " teams, to " .. row.score_limit
  end
  if mode == "ctf" then
    return row.teams .. " sides, " .. og.div(row.time_limit, 720) .. "m"
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
-- A card that deals the field the table is ALREADY set to is not a draw —
-- it is a green button that changes nothing — so the deal steps one card
-- on. The stride is coprime with the deck, so one step is always a
-- different card, and the campaign's own first level would otherwise be
-- dealt to every new company on its first sight of the camp.
local function drawn_row(avoid_id)
  local rows = manifest_rows()
  local seq = og.campaign_state_get("card_seq")
  local cut = deck_cut() - 1
  local idx = og.mod(cut + seq * DECK_STRIDE, DECK_SIZE)
  if rows[idx + 1].id == avoid_id then
    idx = og.mod(cut + (seq + 1) * DECK_STRIDE, DECK_SIZE)
  end
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

-- The posted rules (#212), spelled honestly — 0 keeps its MATCHUP meaning
-- (Auto teams, the map's own score, all troops), and an off-menu number
-- reads as itself.
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

-- The posted score in the two spellings the table needs: the sentence's
-- "map score" and the row note's bare "map".
local function score_words()
  local limit = og.campaign_match_get("score_limit")
  if limit == 0 then
    return "map score", "map"
  end
  local phrase = "to " .. limit
  return phrase, phrase
end

local function troops_word()
  local strip = og.campaign_match_get("strip_troops")
  local word = TROOPS_WORDS[strip]
  if word == nil then
    return tostring(strip)
  end
  return word
end

-- What is posted right now, as the camp's one sentence. Worst case is
-- "Posted: Auto sides, map score, fair." at 36 of the 38-char line budget.
local function posted_line()
  local long_score = score_words()
  local head = "Posted: " .. teams_word() .. " sides, "
  return head .. long_score .. ", " .. troops_word() .. "."
end

-- The same rules at note length for the MATCH SETUP row — worst case
-- "Auto, to 50, fair" at 17 of the 20-char note budget.
local function posted_digest()
  local _, short_score = score_words()
  local head = teams_word() .. ", " .. short_score
  return head .. ", " .. troops_word()
end

-- SEVEN GAMES — the index: what else the book holds, and how far each game
-- is stamped. Browsing it costs nothing on any machine, joiners included;
-- the one row that writes anything is the signature, and it is the host's.
-- SIGN THE BOOK lives HERE rather than at the camp because this is the
-- page the signature changes — the cover takes the name — and because the
-- camp's five rows are the whole grid it can spend beside a roster.
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
-- upstairs has one advisor, and it is the card.
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

-- The #212 presets: the id the action dispatches, the row that offers it,
-- the note that states its exact writes, and the writes themselves.
local PRESETS = {
  {
    id = "two_sides",
    label = "TWO SIDES, FAIR BOTS",
    note = "sides 2, troops fair",
    writes = {
      { "team_count", 2 },
      { "strip_troops", 3 },
    },
  },
  {
    id = "four_sides",
    label = "FOUR SIDES, FAIR BOTS",
    note = "sides 4, troops fair",
    writes = {
      { "team_count", 4 },
      { "strip_troops", 3 },
    },
  },
  {
    id = "classic_rules",
    label = "CLASSIC RULES",
    note = "auto, map score, all",
    writes = {
      { "team_count", 0 },
      { "strip_troops", 0 },
      { "score_limit", 0 },
    },
  },
  {
    id = "short_match",
    label = "SHORT MATCH",
    note = "score to 5",
    writes = {
      { "score_limit", 5 },
    },
  },
}

-- MATCH SETUP: what is posted now, what each preset would post, and — on a
-- machine that cannot post — who does. The preset rows are CUT for a
-- non-host: the lines carry the whole value of the page, and a row whose
-- only possible answer is a refusal is a row nobody should be offered.
local function setup_page()
  local lines = { posted_line() }
  local entries = {}
  if og.campaign_is_host() then
    for i = 1, #PRESETS do
      entries[#entries + 1] = {
        id = PRESETS[i].id,
        kind = "action",
        label = PRESETS[i].label,
        note = PRESETS[i].note,
      }
    end
  else
    lines[#lines + 1] = "The host posts the rules."
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
-- overhead, the current pairing as two one-deep doors, tonight's card and
-- the shuffle beside them, and the posted rules on the row that changes
-- them.
--
-- The camp's grid is 8 units and the roster's floor takes three of them
-- (its column heading plus two hero rows), so text lines and docket rows
-- share the other FIVE (docs/basecamp-zones-design.md, "Bounds
-- arithmetic"). The host therefore spends all five on rows and speaks no
-- line: the posted rules ARE the MATCH SETUP row's note, and the signature
-- lives on the index page whose cover it takes. A sixth row does not page
-- politely here — it hides a row behind an arrow — so nothing in this
-- function is allowed to grow one.
--
-- A joiner keeps the pairing, the rules and its own book, spends one of
-- the five on the line that says whose call the game is, and loses the
-- rows it could never play: the card cannot be dealt by a machine that
-- does not set the level, so it is cut at fetch rather than left to
-- refuse.
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
    -- The draw wears the ARENA as its label, not the ceremony: the engine
    -- confirms a level set by naming the row it set ("Level set to
    -- CENTWHEIT MANOR."), and a row labelled TONIGHT'S CARD would be the
    -- one level row on the screen whose confirmation names nothing you
    -- could play.
    local draw = drawn_row(pair.id)
    entries[#entries + 1] = {
      id = "card_play",
      kind = "level",
      level = draw.id,
      label = stripped_title(draw.id),
      note = "the card",
    }
    entries[#entries + 1] = {
      id = "shuffle",
      kind = "action",
      label = "SHUFFLE THE DECK",
      note = "deal a new card",
    }
  end
  entries[#entries + 1] = {
    id = "setup",
    kind = "page",
    label = "MATCH SETUP",
    note = posted_digest(),
  }
  -- The band the docket may spend, stated rather than assumed: an actions
  -- widget that does not weigh itself takes THREE units however many rows
  -- it carries, which is how a five-row docket ends up as three rows and
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

-- SIGN THE BOOK: the completion flourish — the index retitles for good and
-- the signature row disappears from the table.
local function sign_book()
  og.campaign_state_set("book_signed", 1)
  return { message = "Your name goes in the book." }
end

local function preset_by_id(entry_id)
  for i = 1, #PRESETS do
    if PRESETS[i].id == entry_id then
      return PRESETS[i]
    end
  end
  return nil
end

-- The preset writes go through og.campaign_match_set only in the host's
-- hand. The rows are cut on every other machine, so this refusal is the
-- backstop behind them, not the thing a player is meant to meet.
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
  local preset = preset_by_id(entry_id)
  if preset ~= nil then
    return post_rules(preset.writes)
  end
  return nil
end

og.register_campaign_hooks({
  base_camp = base_camp,
  picker_menu = picker_menu,
  picker_action = picker_action,
})
