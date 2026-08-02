/* The Ninefold Court's embedded showcase pack (scen 605).
 *
 * The concept campaign carries a class pack INSIDE its .glad
 * (packs/org.openglad.concept.showcase/scripts/court.lua). Campaign zips
 * may embed packs/ trees; they mount and unmount with the campaign
 * (tests/unit/test_campaign_packs.cpp proves the mechanism), so the court's
 * fight logic ships with the package and touches nothing else.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <string>

namespace conceptgen {

// The embedded pack's id ("org.openglad.concept.showcase"); loads after
// "core" (pack replay order is pack-id lexicographic).
const char* showcase_pack_id();

// The court.lua source (the level script for scen 605).
const char* showcase_court_lua();

// Writes packs/<id>/scripts/court.lua under the campaign staging directory
// so zip_contents_with_error carries it into the .glad. Returns false on
// any filesystem error.
bool write_showcase_pack(const std::string& staging_dir);

} // namespace conceptgen
