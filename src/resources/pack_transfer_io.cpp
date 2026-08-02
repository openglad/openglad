/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/resources/pack_transfer_io.h>

#include <openglad/core/fnv1a.h>
#include <openglad/core/util.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/filesystem_sync.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/physfs_api.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace og::resources {

namespace {

namespace fs = std::filesystem;

// Session-scoped mounts: pack_id -> mounted real directory. Return-to-lobby
// re-announcements find the pack already mounted (and matching) instead of
// mounting a duplicate search-path entry.
std::unordered_map<std::string, std::string>& session_pack_mounts()
{
    static std::unordered_map<std::string, std::string> mounts;
    return mounts;
}

// Depth-first walk of the virtual directory packs/<pack_id>/, collecting
// relative file paths in deterministic (sorted, prefix) order. Entries whose
// relative path fails the transfer validator are skipped: they could never
// be re-created on a client. Collection stops well past the manifest cap so
// the caller's validation can reject oversized packs explicitly.
void walk_virtual_pack_dir(const std::string& virtual_dir,
                           const std::string& relative_prefix,
                           std::vector<std::string>& out)
{
    if (out.size() > 2 * og::sim::kMaxPackManifestFiles)
        return;
    for (const std::string& name :
         og::io::physfs_enumerate_files_sorted(virtual_dir))
    {
        const std::string relative =
            relative_prefix.empty() ? name : relative_prefix + "/" + name;
        const std::string virtual_path = virtual_dir + "/" + name;
        if (og::io::physfs_is_directory(virtual_path))
        {
            walk_virtual_pack_dir(virtual_path, relative, out);
            continue;
        }
        if (!og::sim::is_safe_pack_relative_path(relative))
        {
            LogWarn("pack transfer: skipping unsafe pack entry {}\n",
                    virtual_path);
            continue;
        }
        out.push_back(relative);
        if (out.size() > 2 * og::sim::kMaxPackManifestFiles)
            return;
    }
}

std::string pack_cache_relative_dir(const og::sim::PackManifestMessage& manifest)
{
    return "packs_cache/" + manifest.pack_id + "@" +
           og::sim::pack_manifest_content_hash_hex(manifest);
}

std::optional<std::vector<std::uint8_t>>
read_disk_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good())
        return std::nullopt;
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
        return std::nullopt;
    return bytes;
}

bool write_disk_file(const fs::path& path,
                     const std::vector<std::uint8_t>& bytes)
{
    std::error_code ec;
    if (path.has_parent_path())
        fs::create_directories(path.parent_path(), ec);

    // tmp + rename, mirroring copy_user_file: an interrupted transfer can
    // never leave a torn cache file that a later hash check would trust.
    const fs::path tmp = fs::path(path.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good())
            return false;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out.good())
        {
            out.close();
            std::error_code cleanup_ec;
            fs::remove(tmp, cleanup_ec);
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec)
    {
        std::error_code cleanup_ec;
        fs::remove(tmp, cleanup_ec);
        return false;
    }
    return true;
}

// Mount `real_dir` at packs/<pack_id>/ and refresh the script registry. A
// previous session mount for the same pack id is replaced. When a foreign
// (asset/user/campaign) copy of the pack already shadows the mountpoint we
// prepend so the received content actually wins the PhysFS search order —
// that shadow is exactly the "content differs" case that triggered the
// transfer. Otherwise append, keeping the received pack behind everything
// the user installed deliberately.
bool mount_pack_dir(const std::string& pack_id, const std::string& real_dir)
{
    auto& mounts = session_pack_mounts();
    const auto it = mounts.find(pack_id);
    if (it != mounts.end())
    {
        if (it->second == real_dir)
            return true; // Already mounted this session.
        (void)og::resources::unmount(it->second.c_str());
        mounts.erase(it);
    }

    const std::string mountpoint = "packs/" + pack_id + "/";
    const bool shadowed = og::io::physfs_is_directory("packs/" + pack_id);
    if (!og::resources::mount(real_dir.c_str(), mountpoint.c_str(),
                              shadowed ? 0 : 1))
    {
        LogWarn("pack transfer: mount failed for {}: {}\n", real_dir,
                og::resources::filesystem_last_error());
        return false;
    }
    mounts.emplace(pack_id, real_dir);
    (void)og::resources::refresh_pack_scripts();
    return true;
}

} // namespace

std::vector<og::sim::HostedPack> build_transferable_packs()
{
    std::vector<og::sim::HostedPack> packs;
    for (const std::string& pack_id :
         og::io::physfs_enumerate_files_sorted("packs"))
    {
        if (pack_id == og::sim::kBuiltinPackId)
            continue; // Every install ships the core pack.
        if (!og::sim::is_safe_pack_id(pack_id))
        {
            LogWarn("pack transfer: skipping unsafely named pack '{}'\n",
                    pack_id);
            continue;
        }
        const std::string pack_root = "packs/" + pack_id;
        if (!og::io::physfs_is_directory(pack_root))
            continue;

        og::sim::HostedPack pack;
        pack.manifest.pack_id = pack_id;

        std::vector<std::string> relative_paths;
        walk_virtual_pack_dir(pack_root, "", relative_paths);
        bool readable = true;
        for (const std::string& relative : relative_paths)
        {
            const std::string virtual_path = pack_root + "/" + relative;
            std::vector<std::uint8_t> bytes = read_file(virtual_path.c_str());
            if (bytes.empty() && !og::resources::exists(virtual_path.c_str()))
            {
                LogWarn("pack transfer: unreadable pack entry {}\n",
                        virtual_path);
                readable = false;
                break;
            }
            og::sim::PackManifestFileEntry entry;
            entry.path = relative;
            entry.size_bytes = static_cast<std::uint32_t>(bytes.size());
            entry.hash64 = og::core::fnv1a64(bytes.data(), bytes.size());
            pack.manifest.files.push_back(std::move(entry));
            pack.file_contents.push_back(std::move(bytes));
        }
        if (!readable || pack.manifest.files.empty())
            continue;
        packs.push_back(std::move(pack));
    }
    return packs;
}

bool mounted_pack_matches_manifest(const og::sim::PackManifestMessage& manifest)
{
    if (manifest.pack_id.empty())
        return false;
    const std::string pack_root = "packs/" + manifest.pack_id;
    if (!og::io::physfs_is_directory(pack_root))
        return false;

    // Same file SET, not just a superset: extra local files (for example a
    // stale script) would still feed the deterministic sim and diverge.
    std::vector<std::string> local_paths;
    walk_virtual_pack_dir(pack_root, "", local_paths);
    if (local_paths.size() != manifest.files.size())
        return false;

    for (const og::sim::PackManifestFileEntry& file : manifest.files)
    {
        const std::string virtual_path = pack_root + "/" + file.path;
        if (!og::resources::exists(virtual_path.c_str()))
            return false;
        const std::vector<std::uint8_t> bytes =
            read_file(virtual_path.c_str());
        if (bytes.size() != file.size_bytes ||
            og::core::fnv1a64(bytes.data(), bytes.size()) != file.hash64)
        {
            return false;
        }
    }
    return true;
}

bool try_mount_cached_pack(const og::sim::PackManifestMessage& manifest)
{
    if (manifest.pack_id.empty() ||
        !og::sim::is_safe_pack_id(manifest.pack_id))
    {
        return false;
    }
    const fs::path cache_dir =
        fs::path(get_user_path()) / pack_cache_relative_dir(manifest);
    std::error_code ec;
    if (!fs::is_directory(cache_dir, ec))
        return false;

    for (const og::sim::PackManifestFileEntry& file : manifest.files)
    {
        const std::optional<std::vector<std::uint8_t>> bytes =
            read_disk_file(cache_dir / file.path);
        if (!bytes.has_value() || bytes->size() != file.size_bytes ||
            og::core::fnv1a64(bytes->data(), bytes->size()) != file.hash64)
        {
            return false;
        }
    }
    return mount_pack_dir(manifest.pack_id, cache_dir.string());
}

bool pack_locally_available(const og::sim::PackManifestMessage& manifest)
{
    return mounted_pack_matches_manifest(manifest) ||
           try_mount_cached_pack(manifest);
}

bool install_received_pack(const og::sim::PackManifestMessage& manifest,
                           const std::vector<std::vector<std::uint8_t>>& files)
{
    // The gameplay client verified counts/paths/hashes, but this function
    // writes to disk, so re-validate independently (defense in depth).
    if (files.size() != manifest.files.size())
        return false;
    if (validate_pack_manifest(manifest).has_value() ||
        !og::sim::is_safe_pack_id(manifest.pack_id))
    {
        return false;
    }

    const fs::path cache_dir =
        fs::path(get_user_path()) / pack_cache_relative_dir(manifest);
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        if (files[i].size() != manifest.files[i].size_bytes)
            return false;
        if (!write_disk_file(cache_dir / manifest.files[i].path, files[i]))
        {
            LogWarn("pack transfer: failed writing {}\n",
                    (cache_dir / manifest.files[i].path).string());
            return false;
        }
    }
    // Emscripten: persist the cache to IndexedDB (no-op elsewhere).
    sync_filesystem();

    return mount_pack_dir(manifest.pack_id, cache_dir.string());
}

void unmount_session_packs()
{
    auto& mounts = session_pack_mounts();
    if (mounts.empty())
        return;
    for (const auto& [pack_id, real_dir] : mounts)
    {
        (void)pack_id;
        (void)og::resources::unmount(real_dir.c_str());
    }
    mounts.clear();
    (void)og::resources::refresh_pack_scripts();
}

og::sim::PackTransferClient::Callbacks make_pack_transfer_client_callbacks()
{
    og::sim::PackTransferClient::Callbacks callbacks;
    callbacks.pack_locally_available = [](const og::sim::PackManifestMessage&
                                              manifest) {
        return pack_locally_available(manifest);
    };
    callbacks.install_pack =
        [](const og::sim::PackManifestMessage& manifest,
           const std::vector<std::vector<std::uint8_t>>& files) {
            return install_received_pack(manifest, files);
        };
    return callbacks;
}

} // namespace og::resources
