/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// libzip wrapper implementation.

#include <openglad/io/zip_api.h>

#include <openglad/platform/io_common.h> // ArchiveIoError values

#include "zip.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace og::io {

namespace fs = std::filesystem;

static std::vector<fs::path> list_relative_paths_recursively(const fs::path& base)
{
    std::vector<fs::path> out;
    std::error_code ec;

    if (!fs::exists(base, ec))
        return out;

    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec)
            break;
        const fs::path& p = it->path();
        fs::path rel = fs::relative(p, base, ec);
        if (ec)
            continue;
        out.push_back(rel);
    }
    return out;
}

static bool path_has_prefix(const fs::path& path, const fs::path& prefix)
{
    auto pit = path.begin();
    auto pend = path.end();
    auto qit = prefix.begin();
    auto qend = prefix.end();
    for (; qit != qend; ++qit, ++pit)
    {
        if (pit == pend || *pit != *qit)
            return false;
    }
    return true;
}

static bool resolve_safe_unzip_destination(const fs::path& outdir_canonical,
    const std::string& archive_entry_name, fs::path* out_destination)
{
    if (archive_entry_name.empty() || archive_entry_name.find('\0') != std::string::npos)
        return false;

    std::string normalized = archive_entry_name;
    for (char& c : normalized)
    {
        if (c == '\\')
            c = '/';
    }
    while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();
    if (normalized.empty())
        return false;

    const fs::path rel = fs::path(normalized);
    if (rel.empty() || rel.is_absolute() || rel.has_root_directory() || rel.has_root_name())
        return false;

    for (const fs::path& part : rel)
    {
        const std::string token = part.generic_string();
        if (token.empty() || token == "." || token == "..")
            return false;
    }

    std::error_code ec;
    fs::path candidate = fs::weakly_canonical(outdir_canonical / rel, ec);
    if (ec)
        candidate = (outdir_canonical / rel).lexically_normal();

    if (!path_has_prefix(candidate, outdir_canonical))
        return false;

    *out_destination = candidate;
    return true;
}

ArchiveIoError zip_contents_with_error(const std::string& indirectory, const std::string& outfile)
{
    fs::path base = fs::path(indirectory);
    std::error_code ec;
    base = fs::weakly_canonical(base, ec);
    if (ec)
        base = fs::path(indirectory);
    fs::path out_path = fs::path(outfile);
    std::error_code out_ec;
    out_path = fs::weakly_canonical(out_path, out_ec);
    if (out_ec)
        out_path = fs::path(outfile);

    int err = 0;
    zip* archive = zip_open(outfile.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (archive == nullptr)
        return ArchiveIoError::OpenArchiveFailed;

    bool add_failed = false;
    for (const fs::path& rel : list_relative_paths_recursively(base))
    {
        const fs::path src = base / rel;
        const std::string dest_name = rel.generic_string();
        if (dest_name.empty())
            continue;
        std::error_code same_ec;
        if (fs::equivalent(src, out_path, same_ec))
            continue;

        if (fs::is_directory(src, ec))
        {
            // Preserve empty directories.
            if (zip_dir_add(archive, (dest_name + "/").c_str(), ZIP_FL_ENC_GUESS) < 0)
                add_failed = true;
            continue;
        }

        if (!fs::is_regular_file(src, ec))
            continue;

        zip_source* s = zip_source_file(archive, src.string().c_str(), 0, -1);
        if (s == nullptr)
        {
            add_failed = true;
            continue;
        }
        if (zip_file_add(archive, dest_name.c_str(), s, ZIP_FL_OVERWRITE | ZIP_FL_ENC_GUESS) < 0)
        {
            zip_source_free(s);
            add_failed = true;
            continue;
        }
    }

    if (zip_close(archive) < 0)
        return ArchiveIoError::CloseArchiveFailed;

    return add_failed ? ArchiveIoError::AddEntryFailed : ArchiveIoError::None;
}

ArchiveIoError unzip_into_with_error(const std::string& infile, const std::string& outdirectory)
{
    fs::path outdir = fs::path(outdirectory);
    std::error_code ec;
    fs::create_directories(outdir, ec);

    fs::path outdir_canonical = fs::weakly_canonical(outdir, ec);
    if (ec)
    {
        outdir_canonical = fs::absolute(outdir, ec);
        if (ec)
            outdir_canonical = outdir.lexically_normal();
    }

    int err = 0;
    zip* archive = zip_open(infile.c_str(), 0, &err);
    if (archive == nullptr)
        return ArchiveIoError::OpenArchiveFailed;

    ArchiveIoError result = ArchiveIoError::None;
    zip_stat_t st;
    zip_stat_init(&st);

    constexpr size_t buf_size = 4096;
    std::array<char, buf_size> buf{};

    const zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < num_entries; i++)
    {
        if (zip_stat_index(archive, i, 0, &st) != 0 || st.name == nullptr)
        {
            result = ArchiveIoError::OpenEntryFailed;
            continue;
        }

        const std::string name(st.name);
        if (name.empty())
            continue;

        const bool is_dir = !name.empty() && name.back() == '/';
        fs::path dest;
        if (!resolve_safe_unzip_destination(outdir_canonical, name, &dest))
        {
            result = ArchiveIoError::OpenEntryFailed;
            continue;
        }

        if (is_dir)
        {
            fs::create_directories(dest, ec);
            continue;
        }

        fs::create_directories(dest.parent_path(), ec);

        zip_file_t* zf = zip_fopen_index(archive, i, 0);
        if (zf == nullptr)
        {
            result = ArchiveIoError::OpenEntryFailed;
            continue;
        }

        FILE* out = std::fopen(dest.string().c_str(), "wb");
        if (out == nullptr)
        {
            zip_fclose(zf);
            result = ArchiveIoError::OpenOutputFailed;
            continue;
        }

        zip_uint64_t written = 0;
        while (written < st.size)
        {
            zip_int64_t n = zip_fread(zf, buf.data(), static_cast<zip_uint64_t>(buf.size()));
            if (n < 0)
            {
                result = ArchiveIoError::ReadEntryFailed;
                break;
            }
            if (n == 0)
            {
                // Avoid infinite loops if size is inaccurate.
                if (written < st.size)
                    result = ArchiveIoError::ReadEntryFailed;
                break;
            }
            std::fwrite(buf.data(), 1, static_cast<size_t>(n), out);
            written += static_cast<zip_uint64_t>(n);
        }

        std::fclose(out);
        zip_fclose(zf);
    }

    if (zip_close(archive) < 0)
        return ArchiveIoError::CloseArchiveFailed;

    return result;
}

} // namespace og::io
