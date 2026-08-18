-- The dream log (issues #207, #206) — the Imaginations campaign's book AND
-- the face of its Base Camp, shipped INSIDE builtin/imaginations.glad as
-- part of the embedded pack imaginations.dreams, so the log exists exactly
-- while the campaign is mounted.
--
-- The kids asked to go back into dreams they had already had. So the camp
-- IS the log: the docket lists every dream, oldest first, each one
-- selectable, and choosing a row carries the engine's SET LEVEL
-- consequence — with `replay = true` on every row, so a dream already had
-- comes back WHOLE (the engine arms its replay excursion: the authored
-- census loads instead of the cleared-level purge, and winning it again
-- returns the campaign cursor to where the player left it). A dream not
-- yet had ignores the mark and is a plain set. The affordance sits on the
-- screen the player lives on instead of behind a door.
--
-- The camp is the ONLY face a kid reaches today. This pack composes no page
-- row, and a page row is the only door into a book, so nothing opens the
-- book page while the camp composes. The page stays as the log's spare
-- face: when a camp fails to compose — malformed, or over its row budget —
-- the engine builds a door onto the book's root page instead, and the log
-- stays reachable on every client. Both faces come off one row builder, so
-- the spare can never drift from the one in use.
--
-- Pure by contract (docs/campaign-scripting-design.md "The picker
-- contract"; docs/basecamp-zones-design.md "The widget contract"): page id
-- or nothing in, a description out, no state kept between fetches. The list
-- of dreams and the note on every row are both re-derived from the save
-- each time either surface asks.

-- Which dreams the log holds is asked, not remembered: a scenario title
-- comes back "" for an id the mounted campaign does not ship. The README's
-- growth rule appends scens 2, 3, ... as new ideas arrive, and a scan means
-- a new dream shows up the day its level does.
--
-- The two faces have different room, and both bounds are the engine's own.
-- A book page holds 24 entries and CLIPS past them. The camp zone holds 16
-- action rows in TOTAL and REFUSES a composition that overruns them. So
-- each face scans exactly as far as it can hold, and the day the docket
-- overruns its bound the refused camp falls back to the book door — the
-- spare face lists further than the camp could, which is the only reason
-- the two numbers differ.
--
-- Sixteen dreams is not a plan, though: it is the point where the camp
-- stops being able to grow, and the camp is the face a kid uses. The
-- seventeenth dream needs a decision (page the docket, or let the camp
-- carry the newest dreams and leave the rest to the book), and
-- tests/unit/test_imaginations_dream_log.cpp goes red the moment the two
-- faces stop listing the same dreams, so the decision cannot be skipped.
local BOOK_DREAMS = 24
local CAMP_DREAMS = 16

-- The three states of a dream, in the log's own words (the surfaces budget
-- a note at about twenty characters; these are short on purpose).
local NOTE_DREAMED = "dreamed"
local NOTE_TONIGHT = "tonight?"
local NOTE_NOT_YET = "not yet"

-- The line the camp leads with — the promise the whole log is for, and the
-- book page's opening line, so the camp and the book greet a kid the same
-- way. The book's legend line ("tonight? means that dream is next.") stays
-- on the book: a camp row carries the engine's own [CURRENT] and [CLEARED]
-- stamps beside the note, so on the camp the legend would explain a word
-- the row has already spelled out twice.
local CAMP_LINE = "Every dream can be dreamed again."

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

-- One row per shipped dream, in id order, scanning ids 1..`most`. `label`
-- is deliberately absent: C++ fills it from the scenario title, so a row
-- always reads as the level's own name and can never drift away from it.
-- One builder feeds both surfaces — the camp docket and the book page are
-- the same list, seen from two rooms. Every row carries `replay = true`
-- (#207): a cleared dream is dreamed again whole — the engine restores the
-- authored island and brings the cursor home after — and on an uncleared
-- row the mark is inert, so tonight's dream stays a plain set.
local function dream_rows(most)
  local rows = {}
  for id = 1, most do
    local title = og.campaign_scenario_title(id)
    if title ~= "" then
      rows[#rows + 1] = {
        id = tostring(id),
        kind = "level",
        level = id,
        note = dream_note(id),
        replay = true,
      }
    end
  end
  return rows
end

-- The camp: the log's line, the docket, and the company. No weights — the
-- engine sizes a one-line text block and a growing docket on its own (the
-- docket pages in place once it outgrows its band) and hands the roster
-- every row it did not spend. The roster is the plain one: this campaign
-- hires, trains, musters and teams like any other, and nothing here is
-- locked or sworn — a dream log has no oaths in it.
local function base_camp()
  return {
    widgets = {
      { kind = "text", lines = { CAMP_LINE } },
      { kind = "actions", entries = dream_rows(CAMP_DREAMS) },
      { kind = "roster" },
    },
  }
end

-- The spare face (see the head note — nothing opens it while the camp
-- composes). One root page: no subpages, no actions, no gold, no campaign
-- state. The book is the list of dreams and nothing else, and it carries
-- the legend the camp does not need.
local function picker_menu(page_id)
  return {
    title = "THE DREAM LOG",
    lines = {
      CAMP_LINE,
      "Pick a night and we go back in.",
      "tonight? means that dream is next.",
    },
    entries = dream_rows(BOOK_DREAMS),
  }
end

og.register_campaign_hooks({
  picker_menu = picker_menu,
  base_camp = base_camp,
})
