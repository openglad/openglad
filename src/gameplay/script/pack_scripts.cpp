/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/script/pack_scripts.h>

#include <algorithm>

namespace og::script {

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

}  // namespace og::script
