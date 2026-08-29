/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Authored voxel art — the .voxtxt reader (docs/voxel-render-design.md §16).
//
// Every error carries file:line, because these files are written by hand and
// a silently-dropped row is a figure with a hole in it that nobody can find.

#include <openglad/interface/render/voxel_art.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace og::render {

namespace {

struct Parser
{
    const std::string& source;
    std::string& error;
    int line = 0;

    bool fail(const std::string& what)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), ":%d: ", line);
        error = source + buf + what;
        return false;
    }
};

// Everything after '#' is a comment; trailing blanks never matter.
std::string strip(const std::string& raw)
{
    const std::size_t hash = raw.find('#');
    std::string s = (hash == std::string::npos) ? raw : raw.substr(0, hash);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::string ltrim(const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    return s.substr(i);
}

} // namespace

bool voxel_art_parse(const std::string& text, const std::string& source,
                     VoxelModel& out, std::string& error)
{
    Parser p{source, error, 0};
    out = VoxelModel{};
    out.cell = 1.0f;
    out.cube_faces = true;

    // Legend: index by character, so a lookup is a subscript and an unknown
    // character is a diagnosable miss rather than a silent empty voxel.
    std::array<int, 256> legend{};
    legend.fill(-1);

    int w = 0, d = 0;
    bool have_size = false;
    bool have_anchor = false;
    std::vector<std::vector<unsigned char>> layers; // per layer, w*d chars
    std::vector<std::vector<unsigned char>> layer_occ;

    std::istringstream in(text);
    std::string raw;
    int expect_rows = 0; // rows still owed to the layer being read
    while (std::getline(in, raw))
    {
        ++p.line;
        const std::string s = strip(raw);
        if (expect_rows > 0)
        {
            // Inside a layer: the row is data, so it is NOT trimmed and a
            // blank line is a missing row, not filler.
            if (static_cast<int>(s.size()) != w)
            {
                char buf[96];
                std::snprintf(buf, sizeof(buf),
                              "layer row is %d characters, expected %d",
                              static_cast<int>(s.size()), w);
                return p.fail(buf);
            }
            const int j = d - expect_rows;
            for (int i = 0; i < w; ++i)
            {
                const unsigned char ch = static_cast<unsigned char>(s[
                    static_cast<std::size_t>(i)]);
                if (ch == '.')
                    continue;
                const int idx = legend[ch];
                if (idx < 0)
                {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf),
                                  "character '%c' is not in the legend", ch);
                    return p.fail(buf);
                }
                const std::size_t off =
                    static_cast<std::size_t>(j) * static_cast<std::size_t>(w) +
                    static_cast<std::size_t>(i);
                layers.back()[off] = static_cast<unsigned char>(idx);
                layer_occ.back()[off] = 1u;
            }
            --expect_rows;
            continue;
        }

        const std::string t = ltrim(s);
        if (t.empty())
            continue;
        std::istringstream ls(t);
        std::string key;
        ls >> key;
        if (key == "name")
        {
            std::string nm;
            ls >> nm;
            if (nm.empty())
                return p.fail("name needs a value");
        }
        else if (key == "size")
        {
            if (!(ls >> w >> d))
                return p.fail("size needs width and depth");
            if (w <= 0 || d <= 0 || w > 64 || d > 64)
                return p.fail("size must be 1..64 in each axis");
            have_size = true;
        }
        else if (key == "cell")
        {
            float c = 0.0f;
            if (!(ls >> c) || !(c > 0.0f))
                return p.fail("cell needs a positive number");
            out.cell = c;
        }
        else if (key == "anchor")
        {
            float ax = 0.0f, ay = 0.0f;
            if (!(ls >> ax >> ay))
                return p.fail("anchor needs x and y");
            out.anchor_x = ax;
            out.anchor_y = ay;
            have_anchor = true;
        }
        else if (key == "color")
        {
            std::string ch;
            int idx = -1;
            if (!(ls >> ch >> idx))
                return p.fail("color needs a character and a palette index");
            if (ch.size() != 1)
                return p.fail("color takes exactly one character");
            if (ch[0] == '.')
                return p.fail("'.' is reserved for empty");
            if (idx < 0 || idx > 255)
                return p.fail("palette index must be 0..255");
            legend[static_cast<unsigned char>(ch[0])] = idx;
        }
        else if (key == "layer")
        {
            if (!have_size)
                return p.fail("layer before size");
            int k = -1;
            if (!(ls >> k))
                return p.fail("layer needs its index");
            if (k != static_cast<int>(layers.size()))
            {
                char buf[80];
                std::snprintf(buf, sizeof(buf),
                              "layer %d out of order, expected %d", k,
                              static_cast<int>(layers.size()));
                return p.fail(buf);
            }
            layers.emplace_back(static_cast<std::size_t>(w) *
                                    static_cast<std::size_t>(d),
                                0u);
            layer_occ.emplace_back(static_cast<std::size_t>(w) *
                                       static_cast<std::size_t>(d),
                                   0u);
            expect_rows = d;
        }
        else
        {
            return p.fail("unknown key '" + key + "'");
        }
    }
    if (expect_rows > 0)
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "file ends %d row(s) into a layer",
                      d - expect_rows);
        return p.fail(buf);
    }
    if (!have_size)
        return p.fail("no size");
    if (layers.empty())
        return p.fail("no layers");

    out.w = w;
    out.d = d;
    out.z = static_cast<int>(layers.size());
    const std::size_t n = static_cast<std::size_t>(w) *
                          static_cast<std::size_t>(d) *
                          static_cast<std::size_t>(out.z);
    out.occ.assign(n, 0u);
    out.index.assign(n, 0u);
    out.lit.assign(n, 0u);
    out.shade.clear(); // authored art carries its own reading; no AO
    for (std::size_t k = 0; k < layers.size(); ++k)
    {
        const std::size_t base = k * static_cast<std::size_t>(w) *
                                 static_cast<std::size_t>(d);
        for (std::size_t o = 0; o < layers[k].size(); ++o)
        {
            out.occ[base + o] = layer_occ[k][o];
            out.index[base + o] = layers[k][o];
        }
    }
    for (int k = 0; k < out.z; ++k)
        for (int j = 0; j < d; ++j)
            for (int i = 0; i < w; ++i)
                if (out.occ[out.at(i, j, k)] != 0)
                    out.lit[out.at(i, j, k)] =
                        out.solid(i, j, k + 1) ? 0u : 1u;
    if (!have_anchor)
    {
        out.anchor_x = static_cast<float>(w) * out.cell * 0.5f;
        out.anchor_y = static_cast<float>(d) * out.cell * 0.5f;
    }
    bool any = false;
    for (unsigned char o : out.occ)
        if (o != 0)
            any = true;
    if (!any)
        return p.fail("every layer is empty");
    error.clear();
    return true;
}

bool voxel_art_load_file(const std::string& path, VoxelModel& out,
                         std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    return voxel_art_parse(buf.str(), path, out, error);
}

} // namespace og::render
