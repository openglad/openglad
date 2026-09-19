/* Multiplayer Arenas campaign generator — the briefing theme lint.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Standard library only: tests include this header by relative path so the
// lint that guards the generator's briefings is the same code the pins
// exercise. Keep it free of engine includes.
#include <string>
#include <vector>

namespace modes_mapgen {

// The words the untheme retired from the briefings. Case-sensitive is
// enough because a briefing is drawn in the 4x6 UPPER-CASE font and any
// lower-case letter is itself a violation, so the list is complete by
// construction.
inline const std::vector<std::string>& briefing_retired_words()
{
    static const std::vector<std::string> words = {
        "GAMESMASTER", "THE BOOK", "LEDGER",  "CONTENDERS",
        "PURSE",       "TALLY",    "PAGE OF", "TONIGHT",
    };
    return words;
}

// Empty string = clean; otherwise one sentence naming the first violation.
//
// Rules, in order: a briefing must have at least one line; no line may
// begin with the retired sign-off's "-- " lead; no line may name a retired
// word; no line may hold a lower-case letter.
inline std::string briefing_theme_violation(const std::vector<std::string>& lines)
{
    if (lines.empty())
        return "briefing is empty";

    for (const std::string& line : lines)
        if (line.rfind("-- ", 0) == 0)
            return "briefing line '" + line +
                   "' begins '-- ' (the retired sign-off)";

    for (const std::string& line : lines)
        for (const std::string& word : briefing_retired_words())
            if (line.find(word) != std::string::npos)
                return "briefing line '" + line + "' names the retired word '" +
                       word + "'";

    for (const std::string& line : lines)
        for (const char c : line)
            if (static_cast<unsigned char>(c) >= 'a' &&
                static_cast<unsigned char>(c) <= 'z')
                return "briefing line '" + line +
                       "' holds a lower-case letter '" + std::string(1, c) +
                       "' (the briefing font is upper case)";

    return "";
}

} // namespace modes_mapgen
