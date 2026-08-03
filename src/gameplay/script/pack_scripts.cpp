/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/script/pack_scripts.h>

#include <openglad/gameplay/script/script_coverage.h>

#include <algorithm>
#include <string_view>

namespace og::script {

// The prefix the coverage report reads as "this chunk is pack content"
// (scripts/coverage/coverage_report.py PACK_CHUNK_PREFIX). See the header
// for why the declaration lives at registration.
namespace detail {

void declare_registered_chunk(const std::string& chunk_name,
                              const std::string& source,
                              const std::string& origin)
{
    // Guarded rather than relying on declare_pack_source's own early-out:
    // with the recorder off a registration costs one bool load and never
    // touches the string. (One line, so the guard cannot read as an
    // uncovered line in a run where it is never taken.)
    if (!coverage::enabled()) return;
    if (!std::string_view(chunk_name).starts_with("packs/"))
        return;
    coverage::declare_pack_source(chunk_name, source, origin);
}

}  // namespace detail

namespace {
std::vector<PackScript>& storage()
{
    static std::vector<PackScript> s;
    return s;
}
unsigned& generation()
{
    static unsigned g = 0;
    return g;
}
}  // namespace

void register_pack_script(PackScript script)
{
    detail::declare_registered_chunk(script.chunk_name, script.source,
                                     script.origin);
    auto& v = storage();
    auto it = std::find_if(v.begin(), v.end(), [&](const PackScript& p) {
        return p.pack_id == script.pack_id &&
               p.chunk_name == script.chunk_name;
    });
    if (it != v.end())
        *it = std::move(script);
    else
        v.push_back(std::move(script));
    generation()++;
}

void unregister_pack_scripts(const std::string& pack_id)
{
    auto& v = storage();
    v.erase(std::remove_if(
                v.begin(), v.end(),
                [&](const PackScript& p) { return p.pack_id == pack_id; }),
            v.end());
    generation()++;
}

const std::vector<PackScript>& pack_scripts()
{
    return storage();
}

void clear_pack_scripts()
{
    storage().clear();
    generation()++;
}

unsigned pack_scripts_generation()
{
    return generation();
}

// --- family chunks (packs/<id>/families/*.lua) --------------------------
//
// Same registry shape, separate storage: the declaration pass replays these
// and ONLY these, so a behavior script can never contribute descriptor data
// (og.family outside families/ is a load error, and this split is what
// makes that rule mechanical rather than advisory).

namespace {
std::vector<PackScript>& family_storage()
{
    static std::vector<PackScript> s;
    return s;
}
unsigned& family_generation_counter()
{
    static unsigned g = 0;
    return g;
}
}  // namespace

void register_pack_family_chunk(PackScript chunk)
{
    detail::declare_registered_chunk(chunk.chunk_name, chunk.source,
                                     chunk.origin);
    auto& v = family_storage();
    auto it = std::find_if(v.begin(), v.end(), [&](const PackScript& p) {
        return p.pack_id == chunk.pack_id &&
               p.chunk_name == chunk.chunk_name;
    });
    if (it != v.end())
        *it = std::move(chunk);
    else
        v.push_back(std::move(chunk));
    family_generation_counter()++;
}

void unregister_pack_family_chunks(const std::string& pack_id)
{
    auto& v = family_storage();
    const auto removed = std::remove_if(
        v.begin(), v.end(),
        [&](const PackScript& p) { return p.pack_id == pack_id; });
    if (removed == v.end())
        return;
    v.erase(removed, v.end());
    family_generation_counter()++;
}

const std::vector<PackScript>& pack_family_chunks()
{
    return family_storage();
}

void clear_pack_family_chunks()
{
    family_storage().clear();
    family_generation_counter()++;
}

unsigned pack_family_generation()
{
    return family_generation_counter();
}

}  // namespace og::script
