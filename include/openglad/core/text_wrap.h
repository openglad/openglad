/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#pragma once

#include <list>
#include <string>
#include <string_view>
#include <vector>

namespace og::core {

// How the newlines already present in the input are treated.
enum class WrapMode {
    // Every '\n' is a hard break. Lines that already fit are emitted
    // verbatim (interior spacing preserved — ASCII tables survive); only
    // over-long lines are re-wrapped, keeping their leading indentation.
    HardBreaks,
    // A blank line is a paragraph break (emitted as exactly one empty
    // output line). A single '\n' is soft: it joins with a space and the
    // whole paragraph is re-flowed. Use for prose (class descriptions,
    // campaign blurbs) that was hand-wrapped to some other column.
    Paragraphs,
};

// Greedy word wrap of `input` to at most `max_chars` columns per line.
//
// Contract (pinned by tests/unit/test_text_wrap.cpp):
// - breaks on ASCII spaces/tabs, BEFORE the word that would overflow;
// - trailing whitespace (and '\r') is stripped from every emitted line;
// - a single word longer than max_chars is hard-split at max_chars — it is
//   never dropped and no emitted line ever exceeds max_chars;
// - empty input yields an empty vector; a blank line inside the input
//   survives as one empty output line;
// - max_chars <= 0 returns the input as-is, one entry per source line.
std::vector<std::string> wrap_text(std::string_view input, int max_chars,
                                   WrapMode mode = WrapMode::HardBreaks);

// Same, over a pre-split line list (levels and campaigns store their
// descriptions as line lists). Equivalent to joining with '\n' and calling
// wrap_text.
std::vector<std::string> wrap_lines(const std::list<std::string>& lines,
                                    int max_chars,
                                    WrapMode mode = WrapMode::HardBreaks);

} // namespace og::core
