/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Automatic multiplayer class-pack transfer (protocol v10, design §8).
//
// Host side: the platform layer builds one HostedPack per non-core mounted
// pack (see og::resources::build_transferable_packs) and hands the set to the
// LobbyServer, which announces manifests during the lobby handshake and
// streams chunks on request via PackTransferHost.
//
// Client side: PackTransferClient consumes the Pack* typed messages from the
// client's lobby poll loop, compares manifests against local content through
// callbacks (the resources layer implements them; gameplay stays free of
// file I/O), requests missing/mismatched packs, reassembles and verifies the
// files, and hands complete packs to the install callback. All hashing is
// FNV-1a (openglad/core/fnv1a.h): integrity/versioning, not tamper-proofing.

#include <openglad/gameplay/net_transport.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace og::sim {

// Semantic byte-volume caps (wire-structural caps live in net_transport.h).
inline constexpr std::uint64_t kMaxPackBytes = 16ull * 1024u * 1024u;
inline constexpr std::uint64_t kMaxSessionPackBytes = 64ull * 1024u * 1024u;

// The built-in pack every install ships; never offered for transfer.
inline constexpr std::string_view kBuiltinPackId = "core";

// Focused validator for manifest file paths. Accepts only relative
// '/'-separated paths whose components are non-empty, are not "." or "..",
// and use [A-Za-z0-9._-] with no leading '.'-free requirement beyond the
// dot-component ban. Rejects absolute paths, backslashes, control bytes,
// empty/oversized paths, and trailing separators.
[[nodiscard]] bool is_safe_pack_relative_path(std::string_view path) noexcept;

// Pack ids become one virtual ("packs/<id>/") and one on-disk
// ("packs_cache/<id>@<hash>/") path component: same alphabet as path
// components, non-empty, length-capped, and never "." / ".." / "core"-style
// reserved dot components.
[[nodiscard]] bool is_safe_pack_id(std::string_view pack_id) noexcept;

// Full semantic validation of one manifest (id, per-file paths incl.
// duplicates, per-file and per-pack byte caps). Returns std::nullopt when
// valid, else a short human-readable reason.
[[nodiscard]] std::optional<std::string>
validate_pack_manifest(const PackManifestMessage& manifest);

// Deterministic content identity of a manifest's file table (path bytes +
// NUL + LE size + LE hash per file, in wire order). Names the client cache
// directory: packs_cache/<pack_id>@<hex hash>/.
[[nodiscard]] std::uint64_t
pack_manifest_content_hash(const PackManifestMessage& manifest) noexcept;
[[nodiscard]] std::string
pack_manifest_content_hash_hex(const PackManifestMessage& manifest);

// One transferable pack held by the host: the manifest (pack_index/
// pack_count are stamped by the announcer) plus file bytes parallel to
// manifest.files.
struct HostedPack {
    PackManifestMessage manifest;
    std::vector<std::vector<std::uint8_t>> file_contents;
};

// Host-side responder. Owns no transport reference; the owner (LobbyServer)
// forwards PackRequest messages and supplies the transport per call.
class PackTransferHost
{
public:
    // Validates and adopts the offer set. Packs failing validation (bad ids/
    // paths, byte caps, count caps, content/manifest length mismatches) are
    // dropped with a log line. Returns the number of packs kept.
    std::size_t set_packs(std::vector<HostedPack> packs);

    [[nodiscard]] const std::vector<HostedPack>& packs() const noexcept
    {
        return packs_;
    }

    // Send the full manifest announcement (pack_count messages, or one empty
    // "no packs" manifest) to one peer.
    void announce_to(ITransport& transport, PeerId peer_id) const;

    // Serve one PackRequest: stream every chunk plus PackTransferDone.
    // Unknown pack ids are ignored; a peer whose served-byte total would
    // exceed kMaxSessionPackBytes is refused (bandwidth-amplification guard).
    void handle_request(ITransport& transport,
                        PeerId peer_id,
                        const PackRequestMessage& request);

    // Reset the per-peer served-bytes budget (peer disconnected).
    void forget_peer(PeerId peer_id);

private:
    std::vector<HostedPack> packs_;
    std::unordered_map<PeerId, std::uint64_t> served_bytes_;
};

// Client-side collector. Feed every Pack* typed message; drive callbacks.
class PackTransferClient
{
public:
    struct Callbacks {
        // True when a local pack with this id already matches the manifest
        // (mounted or mountable from cache) — the transfer is skipped.
        std::function<bool(const PackManifestMessage&)> pack_locally_available;
        // Verified files arrived (parallel to manifest.files): persist and
        // mount. Returning false fails the transfer.
        std::function<bool(const PackManifestMessage&,
                           const std::vector<std::vector<std::uint8_t>>&)>
            install_pack;
        // Optional status sink ("Receiving pack X (N%)", failure reasons).
        std::function<void(const std::string&)> log_status;
    };

    explicit PackTransferClient(Callbacks callbacks);

    // Returns true when the message was a pack-transfer message (consumed),
    // false for every other kind.
    bool handle_message(ITransport& transport,
                        PeerId server_peer_id,
                        const TypedReceivedMessage& message);

    // A transfer is in flight (requested/receiving). Ready-up gates on this.
    [[nodiscard]] bool busy() const noexcept;
    // A transfer or validation failed; the reason is latched until the host
    // announces a fresh manifest generation (pack_index == 0).
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const std::string& failure_reason() const noexcept
    {
        return failure_reason_;
    }
    // "Receiving pack <id> (N%)" while busy, "Pack transfer failed: ..."
    // after a failure, empty when idle.
    [[nodiscard]] std::string status_text() const;

    // Drop all transfer state (connection lost / client shutdown).
    void reset();

private:
    enum class PackState : std::uint8_t {
        AwaitingDecision,
        Skipped,
        Requested,
        Installed,
        Failed,
    };

    struct PendingPack {
        PackManifestMessage manifest;
        PackState state = PackState::AwaitingDecision;
        std::vector<std::vector<std::uint8_t>> files;
        // Per-file sequential write cursor (chunks must arrive in order).
        std::vector<std::uint32_t> file_received;
        std::uint64_t received_bytes = 0;
        std::uint64_t expected_bytes = 0;
    };

    void fail(const std::string& reason);
    void handle_manifest(ITransport& transport,
                         PeerId server_peer_id,
                         const PackManifestMessage& manifest);
    void handle_chunk(const PackFileChunkMessage& chunk);
    void handle_done(const PackTransferDoneMessage& done);
    void log(const std::string& text) const;

    Callbacks callbacks_;
    // Announcement generation state. expected_count_ is unset until the
    // first manifest of a generation (pack_index == 0) arrives.
    std::optional<std::uint8_t> expected_count_;
    std::vector<PendingPack> packs_;
    std::uint64_t session_bytes_accepted_ = 0;
    bool failed_ = false;
    std::string failure_reason_;
};

} // namespace og::sim
