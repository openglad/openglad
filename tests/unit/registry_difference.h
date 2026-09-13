/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace og::testing {

// The first differing line of two registry dumps, with the [order id]
// header it sits under. A whole-dump EXPECT_EQ on 80 KB of text is
// unreadable; the point is to NAME the field that moved.
inline std::string first_registry_difference(const std::string& want,
                                             const std::string& got)
{
    const auto lines_of = [](const std::string& text) {
        std::vector<std::string> out;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
            out.push_back(line);
        return out;
    };
    const std::vector<std::string> a = lines_of(want);
    const std::vector<std::string> b = lines_of(got);
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    for (; i < n; i++) {
        if (a[i] != b[i])
            break;
    }
    if (i == n && a.size() == b.size())
        return "  (dumps are equal)";
    std::ostringstream out;
    out << "  first difference at line " << (i + 1) << "\n";
    for (std::size_t back = i + 1; back-- > 0;) {
        if (back < a.size() && !a[back].empty() && a[back][0] == '[') {
            out << "  in " << a[back] << "\n";
            break;
        }
    }
    out << "  on entry: " << (i < a.size() ? a[i] : "<end of dump>") << "\n";
    out << "  on exit:  " << (i < b.size() ? b[i] : "<end of dump>");
    return out.str();
}

}  // namespace og::testing
