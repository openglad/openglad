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
#include <optional>
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

// The one seam a NETWORKED SESSION ends through. Packs a joiner downloaded
// from its host are session-scoped: left mounted, the next campaign the
// player opens registers a second campaign book and the dispatch answers
// "one campaign, one book: no scripted picker will be served" for the rest
// of the process. Call it where the session truly ends — a join client
// disconnecting, the picker replacing a networked client with a local one,
// the lobby shutting down, the terminal client returning to its picker.
// NEVER between levels: a resume rebuilds the lobby through the same
// shutdown() the session end runs, and the pack the next level needs is
// still the one that has to stay mounted.
void end_pack_transfer_session();

// Host side: the transferable pack set describes the campaign this machine
// has MOUNTED, and hosting stages the lobby's campaign on its first poll —
// which remounts under an announcement built at construction time. A set
// snapshotted once therefore advertises the wrong campaign's packs, and
// joiners download a pack for a campaign nobody is playing.
//
// refresh() answers with a fresh set on its first call and whenever the
// mounted campaign changed since; nullopt otherwise, because rebuilding it
// hashes every file of every mounted pack. One memo shared by every host
// (SDL picker, terminal client, dedicated server) instead of one per role.
class HostedPackSync
{
public:
    [[nodiscard]] std::optional<std::vector<og::sim::HostedPack>> refresh();

    // Forget the memo: the next refresh() announces unconditionally (a new
    // LobbyServer has nothing announced to it yet).
    void reset() noexcept;

private:
    std::string mounted_memo_;
    bool announced_ = false;
};

// Ready-made PackTransferClient callbacks over the three entry points above.
og::sim::PackTransferClient::Callbacks make_pack_transfer_client_callbacks();

} // namespace og::resources
