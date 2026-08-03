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
#include <openglad/core/text_wrap.h>

namespace og::core {
namespace {

bool is_space(char c)
{
    return c == ' ' || c == '\t';
}

std::string_view rstrip(std::string_view s)
{
    while (!s.empty() && (is_space(s.back()) || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::string_view lstrip(std::string_view s)
{
    while (!s.empty() && is_space(s.front()))
        s.remove_prefix(1);
    return s;
}

std::vector<std::string_view> split_physical_lines(std::string_view input)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= input.size(); ++i)
    {
        if (i == input.size() || input[i] == '\n')
        {
            out.push_back(input.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::vector<std::string_view> split_words(std::string_view s)
{
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && is_space(s[i]))
            ++i;
        const std::size_t start = i;
        while (i < s.size() && !is_space(s[i]))
            ++i;
        if (i > start)
            words.push_back(s.substr(start, i - start));
    }
    return words;
}

// Greedily pack `words` into lines of at most `max_chars` columns, appending
// to `out`. `indent` prefixes every emitted line (dropped if it alone would
// fill the line — degenerate input must still terminate).
void wrap_words(const std::vector<std::string_view>& words,
                std::string_view indent, std::size_t max_chars,
                std::vector<std::string>& out)
{
    std::string prefix(indent);
    if (prefix.size() >= max_chars)
        prefix.clear();
    std::string current;
    for (std::string_view word : words)
    {
        for (;;)
        {
            if (current.empty())
            {
                const std::size_t room = max_chars - prefix.size();
                if (word.size() <= room)
                {
                    current = prefix;
                    current += word;
                    break;
                }
                // A single word longer than a whole line: hard-split it.
                out.push_back(prefix + std::string(word.substr(0, room)));
                word.remove_prefix(room);
                continue;
            }
            if (current.size() + 1 + word.size() <= max_chars)
            {
                current += ' ';
                current += word;
                break;
            }
            out.push_back(current);
            current.clear();
        }
    }
    if (!current.empty())
        out.push_back(current);
}

} // namespace

std::vector<std::string> wrap_text(std::string_view input, int max_chars,
                                   WrapMode mode)
{
    std::vector<std::string> out;
    if (input.empty())
        return out;

    const std::vector<std::string_view> physical = split_physical_lines(input);

    if (max_chars <= 0)
    {
        // Defensive: no sane budget — hand the input back unwrapped.
        for (std::string_view line : physical)
            out.emplace_back(line);
        return out;
    }
    const std::size_t width = static_cast<std::size_t>(max_chars);

    if (mode == WrapMode::HardBreaks)
    {
        for (std::string_view raw : physical)
        {
            const std::string_view line = rstrip(raw);
            if (line.size() <= width)
            {
                // Fitting lines (and blanks) pass through verbatim so ASCII
                // tables keep their interior spacing.
                out.emplace_back(line);
                continue;
            }
            const std::size_t indent_len = line.size() - lstrip(line).size();
            wrap_words(split_words(line), line.substr(0, indent_len), width,
                       out);
        }
        return out;
    }

    // WrapMode::Paragraphs
    bool emitted_any = false;
    std::vector<std::string_view> paragraph;
    const auto flush_paragraph = [&]() {
        if (paragraph.empty())
            return;
        if (emitted_any)
            out.emplace_back();
        wrap_words(paragraph, {}, width, out);
        paragraph.clear();
        emitted_any = true;
    };
    for (std::string_view raw : physical)
    {
        const std::string_view line = rstrip(raw);
        if (line.empty())
        {
            // Any run of blank lines is one paragraph break.
            flush_paragraph();
            continue;
        }
        const std::vector<std::string_view> words = split_words(line);
        paragraph.insert(paragraph.end(), words.begin(), words.end());
    }
    flush_paragraph();
    return out;
}

std::vector<std::string> wrap_lines(const std::list<std::string>& lines,
                                    int max_chars, WrapMode mode)
{
    if (lines.empty())
        return {};
    std::size_t total = lines.size();
    for (const std::string& line : lines)
        total += line.size();
    std::string joined;
    joined.reserve(total);
    bool first = true;
    for (const std::string& line : lines)
    {
        if (!first)
            joined += '\n';
        joined += line;
        first = false;
    }
    return wrap_text(joined, max_chars, mode);
}

} // namespace og::core
