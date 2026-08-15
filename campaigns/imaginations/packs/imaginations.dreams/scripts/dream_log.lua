-- The dream log (issue #207) — the Imaginations campaign's mission book,
-- shipped INSIDE builtin/imaginations.glad as part of the embedded pack
-- imaginations.dreams, so the book exists exactly while the campaign is
-- mounted.
--
-- The kids asked to go back into dreams they had already had. So the book
-- is one page listing every dream in the log, oldest first, each one
-- selectable: choosing a row carries the engine's SET LEVEL consequence,
-- and that is the whole replay affordance.
--
-- Pure by contract (docs/campaign-scripting-design.md, "The picker
-- contract"): page id in, page description out, no state kept between
-- fetches. The list of dreams and the note on every row are both
-- re-derived from the save each time the page is asked for.

-- Which dreams the log holds is asked, not remembered: a scenario title
-- comes back "" for an id the mounted campaign does not ship. The README's
-- growth rule appends scens 2, 3, ... as new ideas arrive, and a scan
-- means a new dream shows up in the book the day its level does. The bound
-- is the engine's own page cap of 24 entries.
local MAX_DREAMS = 24

-- The three states of a dream, in the log's own words (the surfaces budget
-- a note at about twenty characters; these are short on purpose).
local NOTE_DREAMED = "dreamed"
local NOTE_TONIGHT = "tonight?"
local NOTE_NOT_YET = "not yet"

-- A dream already had reads "dreamed" even when the campaign cursor is
-- parked on it: that is the row a kid is hunting for, and the engine still
-- stamps its own CURRENT/CLEARED markers over the row text.
local function dream_note(id)
  if og.campaign_level_completed(id) then
    return NOTE_DREAMED
  end
  if og.campaign_current_level() == id then
    return NOTE_TONIGHT
  end
  return NOTE_NOT_YET
end

-- One row per shipped dream, in id order. `label` is deliberately absent:
-- C++ fills it from the scenario title, so a row always reads as the
-- level's own name and can never drift away from it.
local function dream_rows()
  local rows = {}
  for id = 1, MAX_DREAMS do
    local title = og.campaign_scenario_title(id)
    if title ~= "" then
      rows[#rows + 1] = {
        id = tostring(id),
        kind = "level",
        level = id,
        note = dream_note(id),
      }
    end
  end
  return rows
end

-- One root page: no subpages, no actions, no gold, no campaign state. The
-- book is the list of dreams and nothing else.
local function picker_menu(page_id)
  return {
    title = "THE DREAM LOG",
    lines = {
      "Every dream can be dreamed again.",
      "Pick a night and we go back in.",
      "tonight? means that dream is next.",
    },
    entries = dream_rows(),
  }
end

og.register_campaign_hooks({
  picker_menu = picker_menu,
})
