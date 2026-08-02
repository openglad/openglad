/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// og_lua_lines — static oracle for BOTH halves of the Lua coverage
// denominator.
//
// Prints, for each Lua source named on the command line, the set of lines
// that can hold a breakpoint AND the (linedefined, lastlinedefined) span of
// every function prototype the file contains, as Lua itself computes them
// (see og::script::coverage::source_facts).
// scripts/coverage/coverage_report.py uses this to build the DENOMINATOR:
// every Lua source the repository ships gets its full line and function
// total whether or not any test ever loaded it, so an untested pack reads as
// 0%, not as absent from the report.
//
// Both totals come from one walk of one prototype tree, which is what keeps
// the two metrics on one grid — and what makes an uncalled local helper or an
// anonymous callback cost exactly as much as a registered hook.
//
// Output, one record per line, tab-separated:
//     <path>\t<n>\t<line>,...\t<m>\t<ld>:<lld>,...   on success
//     <path>\t-1\t<error message>                    when it will not compile
// (n or m may be 0, in which case the corresponding list is empty.)
//
// This binary is deliberately linked against Lua alone — not og_gameplay —
// so that running it during a coverage build cannot write .gcda files and
// inflate the C++ numbers with a report tool's own execution.

#include <openglad/gameplay/script/script_coverage.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool read_file(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

void print_list(const std::vector<int>& values)
{
    for (std::size_t n = 0; n < values.size(); n++) {
        if (n != 0)
            std::cout << ',';
        std::cout << values[n];
    }
}

void print_spans(const std::vector<og::script::coverage::FunctionSpan>& spans)
{
    for (std::size_t n = 0; n < spans.size(); n++) {
        if (n != 0)
            std::cout << ',';
        std::cout << spans[n].line_defined << ':' << spans[n].last_line_defined;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: og_lua_lines <file.lua> [file.lua ...]\n");
        return 2;
    }
    int failures = 0;
    for (int i = 1; i < argc; i++) {
        const std::string path = argv[i];
        std::string source;
        if (!read_file(path, source)) {
            std::cout << path << "\t-1\tcannot read file\n";
            failures++;
            continue;
        }
        const auto result = og::script::coverage::source_facts(source, path);
        if (!result.ok) {
            std::cout << path << "\t-1\t" << result.error << '\n';
            failures++;
            continue;
        }
        std::cout << path << '\t' << result.lines.size() << '\t';
        print_list(result.lines);
        std::cout << '\t' << result.functions.size() << '\t';
        print_spans(result.functions);
        std::cout << '\n';
    }
    std::cout.flush();
    return failures == 0 ? 0 : 1;
}
