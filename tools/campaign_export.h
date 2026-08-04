/* Shared final step for the campaign authoring tools: publish a
 * self-checked staging tree as the committed campaign SOURCE TREE under
 * campaigns/<id>/. The shipped .glad archive is not written here — the
 * build composes it from the committed tree (scripts/make_glad.py via the
 * og_builtin_campaigns target), so a regeneration reviews as plain file
 * diffs.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace og::toolexport {

// The top-level entries of campaigns/<id>/ that are HAND-AUTHORED and
// therefore never owned by a generator: the campaign's embedded pack tree
// (Lua + art, edited in place) and the provenance README. Everything else
// under campaigns/<id>/ is generator output, wholesale.
inline constexpr const char* kHandAuthoredTopLevel[] = {"packs", "README.md"};

inline bool is_hand_authored_top_level(std::string_view name)
{
    for (const char* keep : kHandAuthoredTopLevel)
        if (name == keep)
            return true;
    return false;
}

// Mirrors `staging` into `dest`: every generator-owned top-level entry of
// dest is removed and rebuilt, so a file that left the staging tree
// becomes a git deletion in the committed campaign source instead of
// silently surviving. The hand-authored entries (kHandAuthoredTopLevel:
// packs/, README.md) are preserved in dest and never copied from staging —
// a regeneration must not clobber them, and the self-check staging copy of
// packs/ (stage_pack_tree) must not export back over its own source.
// Returns false (with the reason on stderr) on any filesystem error.
inline bool export_campaign_tree(
    const std::filesystem::path& staging,
    const std::filesystem::path& dest)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists(dest, ec))
    {
        for (const auto& entry : fs::directory_iterator(dest, ec))
        {
            const std::string name = entry.path().filename().string();
            if (is_hand_authored_top_level(name))
                continue;
            fs::remove_all(entry.path(), ec);
            if (ec)
            {
                std::fprintf(stderr,
                             "campaign export: cannot clear %s: %s\n",
                             entry.path().string().c_str(),
                             ec.message().c_str());
                return false;
            }
        }
        if (ec)
        {
            std::fprintf(stderr, "campaign export: cannot read %s: %s\n",
                         dest.string().c_str(), ec.message().c_str());
            return false;
        }
    }
    fs::create_directories(dest, ec);
    if (ec)
    {
        std::fprintf(stderr, "campaign export: cannot create %s: %s\n",
                     dest.string().c_str(), ec.message().c_str());
        return false;
    }

    for (const auto& entry : fs::directory_iterator(staging, ec))
    {
        const std::string name = entry.path().filename().string();
        if (is_hand_authored_top_level(name))
            continue;
        fs::copy(entry.path(), dest / name,
                 fs::copy_options::recursive |
                     fs::copy_options::overwrite_existing,
                 ec);
        if (ec)
        {
            std::fprintf(stderr, "campaign export: cannot copy %s: %s\n",
                         entry.path().string().c_str(),
                         ec.message().c_str());
            return false;
        }
    }
    if (ec)
    {
        std::fprintf(stderr, "campaign export: cannot read %s: %s\n",
                     staging.string().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

// Byte-copies the campaign's hand-authored pack tree
// (campaigns/<id>/packs/<pack-id>/**) into the staging dir under
// packs/<pack_id>/ so the generator's self-checks run against the pack
// exactly as the build will ship it. Git placeholders (dotfiles such as
// .gitkeep) are skipped. Returns false (with the reason on stderr) on any
// filesystem error.
inline bool stage_pack_tree(const std::filesystem::path& pack_src,
                            const std::filesystem::path& staging_root,
                            std::string_view pack_id)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dst = staging_root / "packs" / std::string(pack_id);
    for (const auto& entry :
         fs::recursive_directory_iterator(pack_src, ec))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (!name.empty() && name.front() == '.')
            continue;
        const fs::path rel = fs::relative(entry.path(), pack_src);
        fs::create_directories((dst / rel).parent_path(), ec);
        fs::copy_file(entry.path(), dst / rel,
                      fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            std::fprintf(stderr,
                         "campaign export: cannot stage pack file %s: %s\n",
                         rel.string().c_str(), ec.message().c_str());
            return false;
        }
    }
    if (ec)
    {
        std::fprintf(stderr, "campaign export: cannot read %s: %s\n",
                     pack_src.string().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

} // namespace og::toolexport
