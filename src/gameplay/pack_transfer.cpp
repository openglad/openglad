/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/pack_transfer.h>

#include <openglad/core/fnv1a.h>
#include <openglad/core/util.h>

#include <algorithm>
#include <format>
#include <memory>
#include <unordered_set>
#include <utility>

namespace og::sim {

namespace {

bool is_safe_pack_name_char(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

bool is_safe_pack_path_component(std::string_view component) noexcept
{
    if (component.empty() || component == "." || component == "..")
        return false;
    return std::all_of(component.begin(), component.end(),
                       is_safe_pack_name_char);
}

} // namespace

bool is_safe_pack_relative_path(std::string_view path) noexcept
{
    if (path.empty() || path.size() > kMaxPackRelativePathLength)
        return false;
    // Leading '/' (absolute), trailing '/' and "//" all produce an empty
    // component below; backslashes and control bytes fail the alphabet.
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t sep = path.find('/', start);
        const std::string_view component = sep == std::string_view::npos
            ? path.substr(start)
            : path.substr(start, sep - start);
        if (!is_safe_pack_path_component(component))
            return false;
        if (sep == std::string_view::npos)
            break;
        start = sep + 1;
    }
    return true;
}

bool is_safe_pack_id(std::string_view pack_id) noexcept
{
    if (!is_safe_pack_path_component(pack_id) ||
        pack_id.size() > kMaxPackIdLength)
    {
        return false;
    }
    // A dots-only id ("...") is a valid component per the rules above but a
    // pathological directory name; require at least one non-dot character.
    return std::any_of(pack_id.begin(), pack_id.end(),
                       [](char c) { return c != '.'; });
}

std::optional<std::string>
validate_pack_manifest(const PackManifestMessage& manifest)
{
    if (manifest.pack_count == 0)
        return std::nullopt; // Explicit empty announcement.

    if (!is_safe_pack_id(manifest.pack_id))
        return std::format("unsafe pack id '{}'", manifest.pack_id);
    if (manifest.files.size() > kMaxPackManifestFiles)
    {
        return std::format("pack '{}' lists too many files ({})",
                           manifest.pack_id, manifest.files.size());
    }

    std::unordered_set<std::string_view> seen_paths;
    seen_paths.reserve(manifest.files.size());
    std::uint64_t total = 0;
    for (const PackManifestFileEntry& file : manifest.files)
    {
        if (!is_safe_pack_relative_path(file.path))
        {
            return std::format("pack '{}' has an unsafe file path '{}'",
                               manifest.pack_id, file.path);
        }
        if (!seen_paths.insert(file.path).second)
        {
            return std::format("pack '{}' lists '{}' twice",
                               manifest.pack_id, file.path);
        }
        total += file.size_bytes;
        if (file.size_bytes > kMaxPackBytes || total > kMaxPackBytes)
        {
            return std::format("pack '{}' exceeds the {} MiB pack cap",
                               manifest.pack_id,
                               kMaxPackBytes / (1024u * 1024u));
        }
    }
    return std::nullopt;
}

std::uint64_t
pack_manifest_content_hash(const PackManifestMessage& manifest) noexcept
{
    std::uint64_t state = og::core::kFnv1a64OffsetBasis;
    const auto fold_le = [&state](std::uint64_t value, int bytes) {
        for (int shift = 0; shift < bytes * 8; shift += 8)
        {
            const std::uint8_t byte =
                static_cast<std::uint8_t>((value >> shift) & 0xffu);
            state = og::core::fnv1a64_append(state, &byte, 1);
        }
    };
    for (const PackManifestFileEntry& file : manifest.files)
    {
        state = og::core::fnv1a64_append(
            state,
            reinterpret_cast<const std::uint8_t*>(file.path.data()),
            file.path.size());
        const std::uint8_t separator = 0;
        state = og::core::fnv1a64_append(state, &separator, 1);
        fold_le(file.size_bytes, 4);
        fold_le(file.hash64, 8);
    }
    return state;
}

std::string pack_manifest_content_hash_hex(const PackManifestMessage& manifest)
{
    return std::format("{:016x}", pack_manifest_content_hash(manifest));
}

// --- PackTransferHost -------------------------------------------------------

std::size_t PackTransferHost::set_packs(std::vector<HostedPack> packs)
{
    packs_.clear();
    served_bytes_.clear();

    std::uint64_t session_total = 0;
    for (HostedPack& pack : packs)
    {
        if (packs_.size() >= kMaxPacksPerSession)
        {
            LogWarn("pack transfer: session pack-count cap reached; "
                    "dropping '{}'\n", pack.manifest.pack_id);
            continue;
        }
        // Stamp a provisional nonzero shape so validate sees a real pack.
        pack.manifest.pack_index = 0;
        pack.manifest.pack_count = 1;
        if (pack.manifest.pack_id == kBuiltinPackId)
        {
            LogWarn("pack transfer: built-in pack is never offered\n");
            continue;
        }
        if (const std::optional<std::string> reason =
                validate_pack_manifest(pack.manifest);
            reason.has_value())
        {
            LogWarn("pack transfer: dropping offered pack: {}\n", *reason);
            continue;
        }
        if (pack.file_contents.size() != pack.manifest.files.size())
        {
            LogWarn("pack transfer: dropping '{}': manifest/content "
                    "mismatch\n", pack.manifest.pack_id);
            continue;
        }
        bool sizes_match = true;
        for (std::size_t i = 0; i < pack.file_contents.size(); ++i)
        {
            if (pack.file_contents[i].size() !=
                pack.manifest.files[i].size_bytes)
            {
                sizes_match = false;
                break;
            }
        }
        if (!sizes_match)
        {
            LogWarn("pack transfer: dropping '{}': file size drifted from "
                    "manifest\n", pack.manifest.pack_id);
            continue;
        }
        const std::uint64_t pack_total = pack.manifest.total_bytes();
        if (session_total + pack_total > kMaxSessionPackBytes)
        {
            LogWarn("pack transfer: session byte cap reached; dropping "
                    "'{}'\n", pack.manifest.pack_id);
            continue;
        }
        session_total += pack_total;
        packs_.push_back(std::move(pack));
    }

    // Stamp the final announcement shape.
    for (std::size_t index = 0; index < packs_.size(); ++index)
    {
        packs_[index].manifest.pack_index = static_cast<std::uint8_t>(index);
        packs_[index].manifest.pack_count =
            static_cast<std::uint8_t>(packs_.size());
    }
    return packs_.size();
}

void PackTransferHost::announce_to(ITransport& transport, PeerId peer_id) const
{
    if (packs_.empty())
    {
        // Explicit "no packs needed": clients must not wait on a manifest
        // that never comes.
        transport.send_pack_manifest(peer_id,
                                     std::make_shared<PackManifestMessage>());
        return;
    }
    for (const HostedPack& pack : packs_)
    {
        transport.send_pack_manifest(
            peer_id, std::make_shared<PackManifestMessage>(pack.manifest));
    }
}

void PackTransferHost::handle_request(ITransport& transport,
                                      PeerId peer_id,
                                      const PackRequestMessage& request)
{
    const auto it = std::find_if(
        packs_.begin(), packs_.end(),
        [&request](const HostedPack& pack) {
            return pack.manifest.pack_id == request.pack_id;
        });
    if (it == packs_.end())
    {
        LogWarn("pack transfer: peer {} requested unknown pack '{}'\n",
                peer_id, request.pack_id);
        return;
    }

    const std::uint64_t pack_total = it->manifest.total_bytes();
    std::uint64_t& served = served_bytes_[peer_id];
    if (served + pack_total > kMaxSessionPackBytes)
    {
        // Bandwidth-amplification guard: a well-behaved client requests each
        // pack at most once per connection, so the honest ceiling is the
        // session cap already enforced by set_packs.
        LogWarn("pack transfer: peer {} exceeded the session transfer "
                "budget; refusing '{}'\n", peer_id, request.pack_id);
        return;
    }
    served += pack_total;

    for (std::size_t file_index = 0; file_index < it->file_contents.size();
         ++file_index)
    {
        const std::vector<std::uint8_t>& content =
            it->file_contents[file_index];
        for (std::size_t offset = 0; offset < content.size();
             offset += kPackFileChunkMaxBytes)
        {
            const std::size_t chunk_size =
                std::min(kPackFileChunkMaxBytes, content.size() - offset);
            auto chunk = std::make_shared<PackFileChunkMessage>();
            chunk->pack_id = it->manifest.pack_id;
            chunk->file_index = static_cast<std::uint32_t>(file_index);
            chunk->offset = static_cast<std::uint32_t>(offset);
            chunk->data.assign(
                content.begin() + static_cast<std::ptrdiff_t>(offset),
                content.begin() +
                    static_cast<std::ptrdiff_t>(offset + chunk_size));
            transport.send_pack_file_chunk(peer_id, std::move(chunk));
        }
    }
    transport.send_pack_transfer_done(
        peer_id,
        std::make_shared<PackTransferDoneMessage>(
            PackTransferDoneMessage{.pack_id = it->manifest.pack_id}));
    Log("pack transfer: served '{}' ({} bytes) to peer {}\n",
        it->manifest.pack_id, pack_total, peer_id);
}

void PackTransferHost::forget_peer(PeerId peer_id)
{
    served_bytes_.erase(peer_id);
}

// --- PackTransferClient -----------------------------------------------------

PackTransferClient::PackTransferClient(Callbacks callbacks)
    : callbacks_(std::move(callbacks))
{
}

bool PackTransferClient::handle_message(ITransport& transport,
                                        PeerId server_peer_id,
                                        const TypedReceivedMessage& message)
{
    switch (message.kind)
    {
    case TypedReceivedMessageKind::PackManifest:
        if (message.pack_manifest)
            handle_manifest(transport, server_peer_id, *message.pack_manifest);
        return true;
    case TypedReceivedMessageKind::PackFileChunk:
        if (message.pack_file_chunk)
            handle_chunk(*message.pack_file_chunk);
        return true;
    case TypedReceivedMessageKind::PackTransferDone:
        if (message.pack_transfer_done)
            handle_done(*message.pack_transfer_done);
        return true;
    case TypedReceivedMessageKind::PackRequest:
        // Host-bound; a client receiving one simply eats it.
        return true;
    default:
        return false;
    }
}

bool PackTransferClient::busy() const noexcept
{
    if (expected_count_.has_value() &&
        packs_.size() < static_cast<std::size_t>(*expected_count_))
    {
        return true; // Mid-announcement: manifests still due.
    }
    return std::any_of(packs_.begin(), packs_.end(),
                       [](const PendingPack& pack) {
                           return pack.state == PackState::Requested;
                       });
}

std::string PackTransferClient::status_text() const
{
    if (failed_)
        return std::format("Pack transfer failed: {}", failure_reason_);
    for (const PendingPack& pack : packs_)
    {
        if (pack.state != PackState::Requested)
            continue;
        const std::uint64_t expected = std::max<std::uint64_t>(
            pack.expected_bytes, 1);
        const int percent = static_cast<int>(
            (pack.received_bytes * 100u) / expected);
        return std::format("Receiving pack {} ({}%)",
                           pack.manifest.pack_id, percent);
    }
    return {};
}

void PackTransferClient::reset()
{
    expected_count_.reset();
    packs_.clear();
    session_bytes_accepted_ = 0;
    failed_ = false;
    failure_reason_.clear();
}

void PackTransferClient::fail(const std::string& reason)
{
    failed_ = true;
    failure_reason_ = reason;
    for (PendingPack& pack : packs_)
    {
        if (pack.state == PackState::Requested)
        {
            pack.state = PackState::Failed;
            pack.files.clear();
            pack.file_received.clear();
        }
    }
    LogWarn("pack transfer: {}\n", reason);
    log(std::format("Pack transfer failed: {}", reason));
}

void PackTransferClient::log(const std::string& text) const
{
    if (callbacks_.log_status)
        callbacks_.log_status(text);
}

void PackTransferClient::handle_manifest(ITransport& transport,
                                         PeerId server_peer_id,
                                         const PackManifestMessage& manifest)
{
    if (manifest.pack_index == 0)
    {
        // A fresh announcement generation replaces everything, including a
        // latched failure: the host's pack set may have changed to one this
        // client can accept.
        reset();
        expected_count_ = manifest.pack_count;
    }
    if (failed_)
        return;
    if (!expected_count_.has_value())
    {
        fail("pack manifest arrived out of order");
        return;
    }
    if (manifest.pack_count != *expected_count_)
    {
        fail("pack manifest generation is inconsistent");
        return;
    }
    if (manifest.pack_count == 0)
        return; // Nothing to transfer this session.

    if (static_cast<std::size_t>(manifest.pack_index) < packs_.size())
    {
        // Idempotent re-send of a manifest we already hold is fine;
        // anything else is a contradiction.
        if (packs_[manifest.pack_index].manifest != manifest)
            fail("pack manifest generation is inconsistent");
        return;
    }
    if (static_cast<std::size_t>(manifest.pack_index) != packs_.size())
    {
        fail("pack manifest arrived out of order");
        return;
    }
    if (const std::optional<std::string> reason =
            validate_pack_manifest(manifest);
        reason.has_value())
    {
        fail(*reason);
        return;
    }
    for (const PendingPack& existing : packs_)
    {
        if (existing.manifest.pack_id == manifest.pack_id)
        {
            fail(std::format("pack '{}' announced twice", manifest.pack_id));
            return;
        }
    }

    PendingPack pack;
    pack.manifest = manifest;
    pack.expected_bytes = manifest.total_bytes();
    if (session_bytes_accepted_ + pack.expected_bytes > kMaxSessionPackBytes)
    {
        fail(std::format("session pack volume exceeds the {} MiB cap",
                         kMaxSessionPackBytes / (1024u * 1024u)));
        return;
    }
    session_bytes_accepted_ += pack.expected_bytes;

    const bool available = callbacks_.pack_locally_available &&
                           callbacks_.pack_locally_available(manifest);
    if (available)
    {
        pack.state = PackState::Skipped;
        packs_.push_back(std::move(pack));
        return;
    }

    pack.state = PackState::Requested;
    pack.files.resize(manifest.files.size());
    pack.file_received.assign(manifest.files.size(), 0);
    for (std::size_t i = 0; i < manifest.files.size(); ++i)
        pack.files[i].resize(manifest.files[i].size_bytes);
    packs_.push_back(std::move(pack));

    transport.send_pack_request(
        server_peer_id,
        std::make_shared<PackRequestMessage>(
            PackRequestMessage{.pack_id = manifest.pack_id}));
    log(std::format("Receiving pack {} (0%)", manifest.pack_id));
}

void PackTransferClient::handle_chunk(const PackFileChunkMessage& chunk)
{
    if (failed_)
        return;
    const auto it = std::find_if(
        packs_.begin(), packs_.end(),
        [&chunk](const PendingPack& pack) {
            return pack.manifest.pack_id == chunk.pack_id;
        });
    if (it == packs_.end() || it->state != PackState::Requested)
        return; // Stale/unsolicited chunk; drop quietly.

    PendingPack& pack = *it;
    if (chunk.file_index >= pack.files.size())
    {
        fail(std::format("pack '{}' chunk indexes an unknown file",
                         chunk.pack_id));
        return;
    }
    std::vector<std::uint8_t>& file = pack.files[chunk.file_index];
    std::uint32_t& received = pack.file_received[chunk.file_index];
    if (chunk.offset != received ||
        chunk.data.size() > file.size() - received)
    {
        fail(std::format("pack '{}' chunk stream is out of sequence",
                         chunk.pack_id));
        return;
    }
    std::copy(chunk.data.begin(), chunk.data.end(),
              file.begin() + static_cast<std::ptrdiff_t>(received));
    received += static_cast<std::uint32_t>(chunk.data.size());
    pack.received_bytes += chunk.data.size();
}

void PackTransferClient::handle_done(const PackTransferDoneMessage& done)
{
    if (failed_)
        return;
    const auto it = std::find_if(
        packs_.begin(), packs_.end(),
        [&done](const PendingPack& pack) {
            return pack.manifest.pack_id == done.pack_id;
        });
    if (it == packs_.end() || it->state != PackState::Requested)
        return;

    PendingPack& pack = *it;
    for (std::size_t i = 0; i < pack.files.size(); ++i)
    {
        if (pack.file_received[i] != pack.manifest.files[i].size_bytes)
        {
            fail(std::format("pack '{}' transfer ended before '{}' was "
                             "complete",
                             done.pack_id, pack.manifest.files[i].path));
            return;
        }
        if (og::core::fnv1a64(pack.files[i].data(), pack.files[i].size()) !=
            pack.manifest.files[i].hash64)
        {
            fail(std::format("pack '{}' failed verification at '{}'",
                             done.pack_id, pack.manifest.files[i].path));
            return;
        }
    }

    if (!callbacks_.install_pack ||
        !callbacks_.install_pack(pack.manifest, pack.files))
    {
        fail(std::format("pack '{}' could not be installed", done.pack_id));
        return;
    }
    pack.state = PackState::Installed;
    pack.files.clear();
    pack.file_received.clear();
    log(std::format("Pack {} installed", done.pack_id));
    Log("pack transfer: installed '{}' ({} bytes)\n",
        done.pack_id, pack.received_bytes);
}

} // namespace og::sim
