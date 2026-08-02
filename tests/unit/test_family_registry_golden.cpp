/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The installed family registry, held against a golden file.
//
// The golden text was CAPTURED from the classpack.yaml reader, before
// packs/core moved onto Lua `og.family` declarations, to check the one
// claim that move rested on: the front end changes and the data does not.
// It has not been recaptured since. The reader is gone and the file still
// matches character for character, which is the proof, kept where it can
// keep proving itself.
//
// From here on it earns its place as a plain regression pin. Every wire id,
// stat, special cost, sprite name, animation row and tuning value the
// shipped pack installs is in this file, so an accidental edit to
// packs/core shows up as a named diff instead of as a subtly different
// game. The two tests under it are the self-consistency half: the dumper
// reaches all five orders, and two dumps of one install are the same text.
//
// REGENERATING. The golden is not sacred, it is a record of a decision.
// When packs/core changes on purpose:
//
//     OPENGLAD_FAMILY_GOLDEN_WRITE=1 ./build/ci-test/og_unit_data
//         --gtest_filter='FamilyRegistryGolden.*'
//
// then READ THE DIFF before committing it. A migration commit that rewrites
// this file has, by definition, changed the game.

#include <gtest/gtest.h>

#include "family_registry_dump.h"

#include <openglad/gameplay/families/family_registries.h>
#include <openglad/resources/packs.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kGolden{"tests/golden/family_registry.txt"};

// Reinstall every mounted pack from scratch, so the dump is a function of
// the pack tree and not of whatever the previous test in this binary left
// in the registries.
std::string fresh_install_dump()
{
    init_all_registries();
    og::resources::refresh_pack_scripts();
    return og::testing::dump_installed_families();
}

std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        out.push_back(line);
    return out;
}

// The first differing line, with context — a whole-file EXPECT_EQ on a
// hundred kilobytes of text is unreadable, and the point of the golden is
// that a failure NAMES the field that moved.
std::string first_difference(const std::string& want, const std::string& got)
{
    const std::vector<std::string> a = lines_of(want);
    const std::vector<std::string> b = lines_of(got);
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    for (; i < n; i++) {
        if (a[i] != b[i])
            break;
    }
    std::ostringstream out;
    out << "first difference at line " << (i + 1) << " (golden " << a.size()
        << " lines, installed " << b.size() << ")\n";
    // The enclosing [order id] header, so the reader knows whose field it is.
    for (std::size_t back = i + 1; back-- > 0;) {
        if (!a[back].empty() && a[back][0] == '[') {
            out << "  in " << a[back] << "\n";
            break;
        }
    }
    out << "  golden:    " << (i < a.size() ? a[i] : "<end of file>") << "\n";
    out << "  installed: " << (i < b.size() ? b[i] : "<end of file>") << "\n";
    return out.str();
}

}  // namespace

TEST(FamilyRegistryGolden, installed_registry_matches_the_captured_golden)
{
    const std::string got = fresh_install_dump();
    ASSERT_FALSE(got.empty()) << "no families installed at all — the shipped "
                                 "packs/ tree is not mounted";

    if (const char* write = std::getenv("OPENGLAD_FAMILY_GOLDEN_WRITE");
        write != nullptr && write[0] != '\0' && write[0] != '0') {
        std::error_code ec;
        std::filesystem::create_directories(kGolden.parent_path(), ec);
        std::ofstream out(kGolden, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << "cannot write " << kGolden;
        out << got;
        ASSERT_TRUE(out.good()) << "short write to " << kGolden;
        out.close();
        GTEST_SKIP() << "golden REWRITTEN (" << kGolden
                     << ") — read the diff before committing it";
    }

    std::ifstream in(kGolden, std::ios::binary);
    ASSERT_TRUE(in.is_open())
        << "cannot read " << kGolden
        << " (run from the repo root; OPENGLAD_FAMILY_GOLDEN_WRITE=1 "
           "recaptures)";
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string want = buffer.str();

    EXPECT_EQ(want, got) << first_difference(want, got);
}

// The dumper is only worth trusting if it actually reaches everything. A
// core pack that installs 21 livings and four other registries must show up
// as five populated orders — a dumper that silently skipped an order would
// make the golden above agree with anything.
TEST(FamilyRegistryGolden, dump_covers_every_order)
{
    const std::string got = fresh_install_dump();
    for (const char* order :
         {"[living ", "[weapon ", "[effect ", "[treasure ", "[generator "}) {
        EXPECT_NE(std::string::npos, got.find(order))
            << "no " << order << "block in the dump";
    }
    // Field coverage, spot-checked on the parts a front-end swap is most
    // likely to drop: the long strings, the frame tables, the tuning maps.
    EXPECT_NE(std::string::npos, got.find("\n  description = \""));
    EXPECT_NE(std::string::npos, got.find("\n  name_pool[0] = \""));
    EXPECT_NE(std::string::npos, got.find("\n  tuning["));
    EXPECT_NE(std::string::npos, got.find("\n  special[1].id = "));
    // No shipped core family carries a pack-supplied frame table (they all
    // name a built-in animation), so the dumper's row rendering is proved
    // against a pack that does — see LuaPackInstall.the_dump_renders_a_pack
    // _shipped_frame_table.
    EXPECT_NE(std::string::npos, got.find("\n  anim_table = ~"));
}

// Two dumps of one install are the same text. Trivial to state and the
// whole file rests on it: a dump that varied with iteration order or with
// an address would make every future failure a coin flip.
TEST(FamilyRegistryGolden, the_dump_is_stable_across_reinstalls)
{
    const std::string first = fresh_install_dump();
    EXPECT_EQ(first, og::testing::dump_installed_families())
        << "the dump changed without a reinstall";
    EXPECT_EQ(first, fresh_install_dump())
        << "reinstalling the same pack tree produced a different registry";
}
