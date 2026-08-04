/* The build's contract for the shipped campaign archives.
 *
 * Every build/<preset>/builtin/<id>.glad is COMPOSED by the build
 * (scripts/make_glad.py via og_builtin_campaigns) from ONE committed
 * source tree per campaign, campaigns/<id>/ — the tree mirrors the
 * archive 1:1, pack-bearing campaigns' packs/<pack-id>/ subtrees
 * included; the per-campaign README.md (repo documentation) is the one
 * file excluded. This test pins that contract from the consumer side:
 * the staged archive next to the test binary holds EXACTLY the members
 * the source tree produces, byte for byte, in both directions. A member
 * with no source file, a source file with no member, or a single byte of
 * drift between them fails with the offending path named.
 *
 * This is the residue of the old
 * ModesLevels.every_pack_member_matches_its_committed_source test, which
 * compared the modes pack members against the pack source tree back
 * when the committed archive could silently drift from its sources.
 * Composition made that comparison tautological for the producer — but
 * the STAGED archive can still go stale against a fresh checkout's
 * sources (an un-rebuilt build tree), and the recipes themselves
 * (.gitkeep/README.md exclusion, the one-root-per-campaign rule) can
 * regress; this test, now covering all seven campaigns, is what fails
 * when they do.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/resources/filesystem.h>
#include <openglad/resources/physfs_api.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

std::string get_asset_path();

namespace {

namespace fs = std::filesystem;

using MemberMap = std::map<std::string, std::vector<std::uint8_t>>;

// Mirrors scripts/make_glad.py's collection rules: every regular file at
// its root-relative path; .gitkeep (and dotfiles) skipped, and the
// campaign README.md — repo documentation, not campaign content — never
// ships.
void collect_source_members(const fs::path& dir, MemberMap* out)
{
    ASSERT_TRUE(fs::is_directory(dir))
        << dir << " missing — campaign source tree not in the checkout?";
    for (const auto& entry : fs::recursive_directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (!name.empty() && name.front() == '.')
            continue; // .gitkeep and friends never ship
        std::string arcname =
            fs::relative(entry.path(), dir).generic_string();
        if (arcname == "README.md")
            continue; // the provenance note never ships

        std::ifstream in(entry.path(), std::ios::binary);
        ASSERT_TRUE(in) << "cannot read " << entry.path();
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        ASSERT_TRUE(out->emplace(arcname, std::move(bytes)).second)
            << "source tree yields duplicate archive path " << arcname;
    }
}

// Walks the mounted archive; `vdir` is the virtual dir under the probe
// mountpoint, `rel` the archive-relative prefix ("" at the root).
void collect_archive_members(const std::string& vdir, const std::string& rel,
                             MemberMap* out)
{
    for (const std::string& name : og::resources::list_files(vdir.c_str()))
    {
        const std::string vpath = vdir + "/" + name;
        const std::string arcname = rel.empty() ? name : rel + "/" + name;
        if (og::io::physfs_is_directory(vpath))
        {
            collect_archive_members(vpath, arcname, out);
            continue;
        }
        (*out)[arcname] = og::resources::read_file(vpath.c_str());
    }
}

TEST(BuiltinArchives, every_member_matches_its_committed_source)
{
    const std::vector<std::string> campaign_ids = {
        "org.openglad.concept",   "org.openglad.gladiator",
        "org.openglad.longseason", "org.openglad.modes",
        "org.openglad.tower",     "org.openglad.tryxian",
        "org.openglad.westlands",
    };

    for (const std::string& id : campaign_ids)
    {
        SCOPED_TRACE(id);

        MemberMap expected;
        collect_source_members(fs::path(OG_CAMPAIGNS_SOURCE_DIR) / id,
                               &expected);
        if (::testing::Test::HasFatalFailure())
            return;
        ASSERT_FALSE(expected.empty());

        const fs::path archive =
            fs::path(get_asset_path()) / "builtin" / (id + ".glad");
        ASSERT_TRUE(fs::exists(archive))
            << archive << " missing — the build did not compose it "
            << "(og_builtin_campaigns)";

        const std::string mountpoint =
            std::string("builtin_src_check_") + id;
        ASSERT_TRUE(og::resources::mount(archive.string().c_str(),
                                         mountpoint.c_str(), 1))
            << og::resources::filesystem_last_error();

        MemberMap actual;
        collect_archive_members(mountpoint, "", &actual);

        for (const auto& [arcname, bytes] : expected)
        {
            const auto it = actual.find(arcname);
            if (it == actual.end())
            {
                ADD_FAILURE() << "source file " << arcname
                              << " has no member in the staged archive — "
                              << "stale build tree? rebuild "
                              << "og_builtin_campaigns";
                continue;
            }
            EXPECT_EQ(bytes, it->second)
                << arcname << " differs between the staged archive and its "
                << "committed source";
        }
        for (const auto& [arcname, bytes] : actual)
        {
            EXPECT_TRUE(expected.count(arcname) != 0)
                << "archive member " << arcname
                << " has no committed source file";
        }
        EXPECT_EQ(expected.size(), actual.size());

        EXPECT_TRUE(og::resources::unmount(archive.string().c_str()));
    }
}

} // namespace
