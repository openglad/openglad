/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// File-system side of the multiplayer class-pack transfer (protocol v10,
// docs/lua-classpacks-design.md §8). The gameplay layer owns the protocol
// state machines (openglad/gameplay/pack_transfer.h); this module supplies
// their I/O: building the host's transferable set from the mounted virtual
// packs/ tree, comparing manifests against local content, persisting
// received packs under <user_path>/packs_cache/<pack_id>@<hash>/, mounting
// them at packs/<pack_id>/, and refreshing the pack-script registry.

#include <openglad/gameplay/pack_transfer.h>

#include <cstdint>
#include <string>
#include <vector>

namespace og::resources {

// Host side: one HostedPack per mounted non-core pack directory under the
// virtual path packs/. File hashes are FNV-1a; entries with unsafe names
// are skipped (they could never be re-created on a client).
std::vector<og::sim::HostedPack> build_transferable_packs();

// True when the mounted packs/<pack_id>/ tree is content-identical to the
// manifest (same file set, sizes, and hashes — extra local files count as a
// difference because pack scripts feed the deterministic sim).
bool mounted_pack_matches_manifest(const og::sim::PackManifestMessage& manifest);

// True when a previously received copy under packs_cache/ matches the
// manifest; mounts it (and refreshes pack scripts) before returning.
bool try_mount_cached_pack(const og::sim::PackManifestMessage& manifest);

// Client availability probe for PackTransferClient: mounted match first,
// then the cache.
bool pack_locally_available(const og::sim::PackManifestMessage& manifest);

// Persist a verified received pack to packs_cache/<pack_id>@<hash>/, mount
// it at packs/<pack_id>/ and refresh the pack-script registry. `files` is
// parallel to manifest.files. Session-scoped: re-installing the manifest
// already mounted for this pack id is a no-op; a different manifest for the
// same id replaces the previous session mount.
bool install_received_pack(
    const og::sim::PackManifestMessage& manifest,
    const std::vector<std::vector<std::uint8_t>>& files);

// Unmount every pack mounted by try_mount_cached_pack/install_received_pack
// this session and refresh the script registry (session teardown, tests).
void unmount_session_packs();

// Ready-made PackTransferClient callbacks over the three entry points above.
og::sim::PackTransferClient::Callbacks make_pack_transfer_client_callbacks();

} // namespace og::resources
