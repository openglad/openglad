/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The CHANGELOG! page (docs/ability-select-design.md F): shown once, the
// first time the game starts after an update, from the picker's shared
// start (picker_main, and the web build's picker_init), before any lobby
// exists.
//
// The page lists every entry newer than the cfg marker ui/changelog_seen,
// oldest first, then stamps the marker with the newest id, saves cfg and
// flushes it to disk (on the web build that flush is what reaches
// IndexedDB). The marker is the only latch: there is no once-per-process
// flag. Never shown in a demo (non-interactive) session; under TESTING it
// only leaves a trace unless a test forces it.

#include <openglad/interface/ui/menu_screen_spec.h>

#include <cstddef>
#include <span>

struct button;

namespace og::ui {

struct ChangelogEntry {
    int id;                              // monotonic; the marker holds the highest shown
    const char* title;                   // big-font title
    std::span<const char* const> lines;  // at most kChangelogMaxLines lines
    const char* image;                   // under pix/, or nullptr
    const char* web_image;               // the web build's picture, or nullptr
};

// The page's text budget: the lines sit centred in the 4..316 bevel at
// 6 px a glyph, above the picture band.
inline constexpr std::size_t kChangelogMaxGlyphs = 48;
inline constexpr std::size_t kChangelogMaxLines = 5;

// Entry 1: New Ability Select. The picture under the text is the new default
// keyboard (scripts/keyboard_map/make_keyboard_map.py draws it from the key
// tables).
inline constexpr const char* kAbilitySelectLines[] = {
    "One key per ability: SPEC 1, 2, 3 cast at once.",
    "Hold SHIFTER for three more. No more cycling.",
    "TRAIN > ABILITIES puts any ability on any key.",
    "New default keys below; REMAP keeps yours.",
    "Old way: Settings > UI FX > New Ability Select.",
};

inline constexpr ChangelogEntry kChangelog[] = {
    {1, "CHANGELOG!", kAbilitySelectLines, "pix/changelog/keyboard_belt.png",
     "pix/changelog/keyboard_belt_web.png"},
};
inline constexpr int kChangelogLatestId = 1;

namespace changelog_detail {
constexpr std::size_t glyphs(const char* text)
{
    std::size_t n = 0;
    while (text[n] != '\0')
        ++n;
    return n;
}
constexpr bool entries_fit()
{
    int previous_id = 0;
    for (const ChangelogEntry& entry : kChangelog) {
        if (entry.id <= previous_id || entry.lines.size() > kChangelogMaxLines)
            return false;
        previous_id = entry.id;
        for (const char* line : entry.lines)
            if (glyphs(line) > kChangelogMaxGlyphs)
                return false;
    }
    return previous_id == kChangelogLatestId;
}
}  // namespace changelog_detail

static_assert(changelog_detail::entries_fit(),
              "a CHANGELOG! line is wider than the bevel (48 glyphs), an "
              "entry has more than five lines, or the ids are not rising to "
              "kChangelogLatestId");

const MenuScreenSpec& changelog_menu_screen_spec();

// The marker as cfg holds it (0 when absent or not a number).
int changelog_seen_marker();

// The picture path an entry shows on this build (the _web picture on the web
// build, where player 1 attacks with Z).
const char* changelog_entry_image(const ChangelogEntry& entry, bool web);

}  // namespace og::ui

// Show every entry newer than the marker, then stamp, save and sync cfg.
// Called right after the picker's shared menu state is loaded.
void picker_show_pending_changelog();

#ifdef TESTING
// A flag of its own (never the shared real-dialogs flag): with it set the
// page really shows in a test; without it picker_show_pending_changelog only
// traces. reset_integration_ui_state clears it between tests.
void picker_testing_set_force_changelog(bool force);
bool picker_testing_changelog_forced();
#endif

// The D3 accessor pair (buttons() fills what count() reads).
button* picker_changelog_buttons();
int picker_changelog_button_count();
