/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Adversarial half of the class-pack transfer (protocol v10).
//
// tests/unit/test_pack_transfer.cpp covers the wire round trip and the
// cooperative flows. This file covers what happens when the other end is
// NOT cooperative — which matters more than usual here, because a received
// pack becomes executable Lua that drives the deterministic sim, and the
// bytes arrive from a stranger's machine over a public relay.
//
// Three families of hostile input:
//   * resource exhaustion — more packs, more files or more bytes than the
//     session caps allow, and a peer re-requesting a pack to make the host
//     amplify its bandwidth;
//   * protocol confusion — manifests out of order, generations that
//     contradict each other, a pack announced twice, a chunk pointing at a
//     file that does not exist;
//   * filesystem hostility — pack ids and entry names that would escape
//     the cache directory, and a cache path the process cannot write.
//
// The contract is the same everywhere: refuse, latch the failure, and
// leave nothing half-written.

#include <openglad/core/fnv1a.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/pack_transfer.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/pack_transfer_io.h>
#include <openglad/resources/packs.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

class RecordingTransport final : public og::sim::ITransport
{
public:
    using og::sim::ITransport::broadcast;

    void send(og::sim::PeerId peer_id, const std::uint8_t* data,
              std::size_t len) override
    {
        sent_.push_back({peer_id, std::vector<std::uint8_t>(data, data + len)});
    }

    std::vector<og::sim::ReceivedMessage> poll() override { return {}; }
    void accept_connections() override {}
    void disconnect(og::sim::PeerId) override {}

    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return {};
    }

    const std::vector<og::sim::ReceivedMessage>& sent() const noexcept
    {
        return sent_;
    }

    void clear() { sent_.clear(); }

private:
    std::vector<og::sim::ReceivedMessage> sent_;
};

og::sim::HostedPack make_pack(const std::string& pack_id,
                              std::vector<std::pair<std::string, std::string>>
                                  files)
{
    og::sim::HostedPack pack;
    pack.manifest.pack_id = pack_id;
    for (auto& [path, content] : files)
    {
        og::sim::PackManifestFileEntry entry;
        entry.path = path;
        entry.size_bytes = static_cast<std::uint32_t>(content.size());
        entry.hash64 = og::core::fnv1a64(
            reinterpret_cast<const std::uint8_t*>(content.data()),
            content.size());
        pack.manifest.files.push_back(std::move(entry));
        pack.file_contents.emplace_back(content.begin(), content.end());
    }
    return pack;
}

// A pack of `bytes` filler in one file. Used for the byte-volume caps,
// where only the totals matter.
og::sim::HostedPack make_bulk_pack(const std::string& pack_id,
                                   std::size_t bytes)
{
    og::sim::HostedPack pack;
    pack.manifest.pack_id = pack_id;
    std::vector<std::uint8_t> content(bytes, 0x5au);
    og::sim::PackManifestFileEntry entry;
    entry.path = "data/bulk.bin";
    entry.size_bytes = static_cast<std::uint32_t>(bytes);
    entry.hash64 = og::core::fnv1a64(content.data(), content.size());
    pack.manifest.files.push_back(std::move(entry));
    pack.file_contents.push_back(std::move(content));
    return pack;
}

struct ClientHarness {
    RecordingTransport transport;
    std::vector<std::string> log;
    std::size_t install_calls = 0;
    bool locally_available = false;
    bool install_result = true;
    std::unique_ptr<og::sim::PackTransferClient> client;

    ClientHarness()
    {
        og::sim::PackTransferClient::Callbacks callbacks;
        callbacks.pack_locally_available =
            [this](const og::sim::PackManifestMessage&) {
                return locally_available;
            };
        callbacks.install_pack =
            [this](const og::sim::PackManifestMessage&,
                   const std::vector<std::vector<std::uint8_t>>&) {
                install_calls++;
                return install_result;
            };
        callbacks.log_status = [this](const std::string& text) {
            log.push_back(text);
        };
        client = std::make_unique<og::sim::PackTransferClient>(
            std::move(callbacks));
    }

    bool feed(const std::vector<std::uint8_t>& bytes)
    {
        return client->handle_message(
            transport, 1u,
            og::sim::decode_received_message({.peer_id = 1u, .data = bytes}));
    }

    bool feed_manifest(const og::sim::PackManifestMessage& m)
    {
        return feed(og::sim::serialize_pack_manifest_message(m));
    }

    bool failed() const { return client->failed(); }
    const std::string& reason() const { return client->failure_reason(); }
};

og::sim::PackManifestMessage one_file_manifest(const char* pack_id,
                                               std::uint8_t index,
                                               std::uint8_t count,
                                               const char* content)
{
    og::sim::PackManifestMessage m;
    m.pack_index = index;
    m.pack_count = count;
    m.pack_id = pack_id;
    m.version = "1";
    og::sim::PackManifestFileEntry entry;
    entry.path = "scripts/a.lua";
    entry.size_bytes = static_cast<std::uint32_t>(std::string(content).size());
    entry.hash64 = og::core::fnv1a64(
        reinterpret_cast<const std::uint8_t*>(content),
        std::string(content).size());
    m.files.push_back(std::move(entry));
    return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// Resource exhaustion
// ---------------------------------------------------------------------------

TEST(PackTransferLimits, a_manifest_over_the_file_cap_is_rejected)
{
    og::sim::PackManifestMessage manifest;
    manifest.pack_index = 0;
    manifest.pack_count = 1;
    manifest.pack_id = "org.test.many";
    for (std::size_t i = 0; i <= og::sim::kMaxPackManifestFiles; i++)
    {
        og::sim::PackManifestFileEntry entry;
        entry.path = "scripts/f" + std::to_string(i) + ".lua";
        entry.size_bytes = 1;
        entry.hash64 = i;
        manifest.files.push_back(std::move(entry));
    }
    const auto reason = og::sim::validate_pack_manifest(manifest);
    ASSERT_TRUE(reason.has_value());
    EXPECT_NE(reason->find("too many files"), std::string::npos) << *reason;
}

TEST(PackTransferLimits, set_packs_stops_at_the_session_pack_count_cap)
{
    og::sim::PackTransferHost host;
    std::vector<og::sim::HostedPack> offer;
    for (std::size_t i = 0; i < og::sim::kMaxPacksPerSession + 4; i++)
    {
        offer.push_back(make_pack("org.test.p" + std::to_string(i),
                                  {{"scripts/a.lua", "-- a\n"}}));
    }
    EXPECT_EQ(og::sim::kMaxPacksPerSession, host.set_packs(std::move(offer)));
    ASSERT_EQ(og::sim::kMaxPacksPerSession, host.packs().size());
    // The surviving packs are the FIRST ones offered, and the announcement
    // shape is restamped to the truncated count so a client is not left
    // waiting for a manifest that will never be sent.
    EXPECT_EQ("org.test.p0", host.packs().front().manifest.pack_id);
    EXPECT_EQ(static_cast<std::uint8_t>(og::sim::kMaxPacksPerSession),
              host.packs().front().manifest.pack_count);
}

TEST(PackTransferLimits, set_packs_stops_at_the_session_byte_cap)
{
    // Five 15 MiB packs is 75 MiB against a 64 MiB session cap: four fit.
    constexpr std::size_t kPackBytes = 15ull * 1024u * 1024u;
    og::sim::PackTransferHost host;
    std::vector<og::sim::HostedPack> offer;
    for (int i = 0; i < 5; i++)
        offer.push_back(make_bulk_pack("org.test.b" + std::to_string(i),
                                       kPackBytes));

    EXPECT_EQ(4u, host.set_packs(std::move(offer)));
    std::uint64_t total = 0;
    for (const auto& pack : host.packs())
        total += pack.manifest.total_bytes();
    EXPECT_LE(total, og::sim::kMaxSessionPackBytes);
}

TEST(PackTransferLimits, a_peer_cannot_re_request_past_the_transfer_budget)
{
    // Bandwidth amplification: an honest client asks once. Asking again and
    // again must stop costing the host bandwidth once the peer has been
    // served a session's worth.
    constexpr std::size_t kPackBytes = 16ull * 1024u * 1024u;
    og::sim::PackTransferHost host;
    std::vector<og::sim::HostedPack> offer;
    offer.push_back(make_bulk_pack("org.test.big", kPackBytes));
    ASSERT_EQ(1u, host.set_packs(std::move(offer)));

    RecordingTransport transport;
    const int allowed = static_cast<int>(og::sim::kMaxSessionPackBytes /
                                         kPackBytes);
    for (int i = 0; i < allowed; i++)
    {
        transport.clear();
        host.handle_request(transport, 7u, {.pack_id = "org.test.big"});
        EXPECT_FALSE(transport.sent().empty()) << "request " << i;
    }
    transport.clear();
    host.handle_request(transport, 7u, {.pack_id = "org.test.big"});
    EXPECT_TRUE(transport.sent().empty())
        << "the request past the budget must send nothing at all";

    // The budget is per peer: a different peer still gets served.
    transport.clear();
    host.handle_request(transport, 8u, {.pack_id = "org.test.big"});
    EXPECT_FALSE(transport.sent().empty());
}

// ---------------------------------------------------------------------------
// Protocol confusion
// ---------------------------------------------------------------------------

TEST(PackTransferClientErrors, a_manifest_before_the_generation_head_fails)
{
    ClientHarness harness;
    // pack_index 1 with nothing before it: there is no generation open.
    ASSERT_TRUE(harness.feed_manifest(one_file_manifest("org.test.a", 1, 2,
                                                        "-- a\n")));
    EXPECT_TRUE(harness.failed());
    EXPECT_NE(harness.reason().find("out of order"), std::string::npos)
        << harness.reason();
    EXPECT_FALSE(harness.log.empty()) << "the failure must reach the UI";
}

TEST(PackTransferClientErrors, a_generation_that_changes_count_fails)
{
    ClientHarness harness;
    ASSERT_TRUE(harness.feed_manifest(
        one_file_manifest("org.test.a", 0, 2, "-- a\n")));
    EXPECT_FALSE(harness.failed());
    // Same generation, different total.
    ASSERT_TRUE(harness.feed_manifest(
        one_file_manifest("org.test.b", 1, 3, "-- b\n")));
    EXPECT_TRUE(harness.failed());
    EXPECT_NE(harness.reason().find("inconsistent"), std::string::npos)
        << harness.reason();
}

TEST(PackTransferClientErrors, a_repeated_index_must_be_byte_identical)
{
    ClientHarness harness;
    harness.locally_available = true;  // keeps the flow off the wire
    ASSERT_TRUE(harness.feed_manifest(
        one_file_manifest("org.test.a", 0, 2, "-- a\n")));
    const og::sim::PackManifestMessage second =
        one_file_manifest("org.test.b", 1, 2, "-- b\n");
    ASSERT_TRUE(harness.feed_manifest(second));
    EXPECT_FALSE(harness.failed());

    // Idempotent re-send of exactly the same manifest: tolerated.
    ASSERT_TRUE(harness.feed_manifest(second));
    EXPECT_FALSE(harness.failed()) << "an identical re-send is not an error";

    // Same index, different content: a contradiction.
    og::sim::PackManifestMessage contradiction = second;
    contradiction.files[0].hash64 ^= 0xffull;
    ASSERT_TRUE(harness.feed_manifest(contradiction));
    EXPECT_TRUE(harness.failed());
    EXPECT_NE(harness.reason().find("inconsistent"), std::string::npos)
        << harness.reason();
}

TEST(PackTransferClientErrors, a_skipped_index_fails)
{
    ClientHarness harness;
    harness.locally_available = true;
    ASSERT_TRUE(harness.feed_manifest(
        one_file_manifest("org.test.a", 0, 3, "-- a\n")));
    // Index 2 with only index 0 held: a gap.
    ASSERT_TRUE(harness.feed_manifest(
        one_file_manifest("org.test.c", 2, 3, "-- c\n")));
    EXPECT_TRUE(harness.failed());
    EXPECT_NE(harness.reason().find("out of order"), std::string::npos)
        << harness.reason();
}

TEST(PackTransferClientErrors, the_same_pack_id_announced_twice_fails)
{
    ClientHarness harness;
    harness.locally_available = true;
    ASSERT_TRUE(harness.feed_manifest(
        one_file_manifest("org.test.dup", 0, 2, "-- a\n")));
    ASSERT_TRUE(harness.feed_manifest(
        one_file_manifest("org.test.dup", 1, 2, "-- b\n")));
    EXPECT_TRUE(harness.failed());
    EXPECT_NE(harness.reason().find("announced twice"), std::string::npos)
        << harness.reason();
}

TEST(PackTransferClientErrors, a_chunk_naming_an_unknown_file_fails)
{
    ClientHarness harness;
    ASSERT_TRUE(harness.feed_manifest(
        one_file_manifest("org.test.a", 0, 1, "-- a\n")));
    EXPECT_FALSE(harness.failed());

    og::sim::PackFileChunkMessage chunk;
    chunk.pack_id = "org.test.a";
    chunk.file_index = 9;  // the manifest declares exactly one file
    chunk.offset = 0;
    chunk.data = {'x'};
    ASSERT_TRUE(harness.feed(og::sim::serialize_pack_file_chunk_message(chunk)));
    EXPECT_TRUE(harness.failed());
    EXPECT_NE(harness.reason().find("unknown file"), std::string::npos)
        << harness.reason();
}

TEST(PackTransferClientErrors, an_install_that_refuses_fails_the_transfer)
{
    ClientHarness harness;
    harness.install_result = false;

    const char* content = "-- a\n";
    const og::sim::PackManifestMessage manifest =
        one_file_manifest("org.test.a", 0, 1, content);
    ASSERT_TRUE(harness.feed_manifest(manifest));

    og::sim::PackFileChunkMessage chunk;
    chunk.pack_id = "org.test.a";
    chunk.file_index = 0;
    chunk.offset = 0;
    chunk.data.assign(content, content + std::string(content).size());
    ASSERT_TRUE(harness.feed(og::sim::serialize_pack_file_chunk_message(chunk)));

    og::sim::PackTransferDoneMessage done;
    done.pack_id = "org.test.a";
    ASSERT_TRUE(
        harness.feed(og::sim::serialize_pack_transfer_done_message(done)));

    EXPECT_EQ(1u, harness.install_calls);
    EXPECT_TRUE(harness.failed());
    EXPECT_NE(harness.reason().find("could not be installed"),
              std::string::npos)
        << harness.reason();
    EXPECT_FALSE(harness.client->busy())
        << "a failed transfer must not keep the lobby waiting";
}

// ---------------------------------------------------------------------------
// Filesystem hostility
// ---------------------------------------------------------------------------

namespace {

class PackTransferIoErrorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root_ = fs::path(get_user_path()) / "pack_io_error_stage";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
        ASSERT_FALSE(ec) << ec.message();
    }

    void TearDown() override
    {
        og::resources::unmount_session_packs();
        for (const std::string& mounted : mounted_)
            (void)og::resources::unmount(mounted.c_str());
        (void)og::resources::refresh_pack_scripts();
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::remove_all(fs::path(get_user_path()) / "packs_cache", ec);
    }

    fs::path stage_pack(const std::string& dir_name,
                        const std::vector<std::pair<std::string, std::string>>&
                            files)
    {
        const fs::path dir = root_ / dir_name;
        std::error_code ec;
        for (const auto& [rel, content] : files)
        {
            const fs::path path = dir / rel;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary);
            out << content;
        }
        return dir;
    }

    void mount_pack(const fs::path& real_dir, const std::string& pack_id)
    {
        const std::string mountpoint = "packs/" + pack_id + "/";
        ASSERT_TRUE(og::resources::mount(real_dir.string().c_str(),
                                         mountpoint.c_str(), 1))
            << og::resources::filesystem_last_error();
        mounted_.push_back(real_dir.string());
    }

    fs::path root_;
    std::vector<std::string> mounted_;
};

}  // namespace

// A file whose name cannot survive the trip (a space is outside the path
// alphabet) must be dropped from the offer, not shipped and then rejected
// on the far side. A pack that is ENTIRELY unshippable must not be offered
// at all — an empty manifest would look like "you already have it".
TEST_F(PackTransferIoErrorTest, unshippable_entries_and_packs_are_not_offered)
{
    const fs::path mixed =
        stage_pack("mixed", {{"classpack.yaml", "pack: org.test.mixed\n"},
                             {"scripts/bad name.lua", "-- unshippable\n"}});
    const fs::path allbad =
        stage_pack("allbad", {{"bad name.lua", "-- unshippable\n"}});
    mount_pack(mixed, "org.test.mixed");
    mount_pack(allbad, "org.test.allbad");

    const std::vector<og::sim::HostedPack> offer =
        og::resources::build_transferable_packs();

    const auto found = std::find_if(
        offer.begin(), offer.end(), [](const og::sim::HostedPack& p) {
            return p.manifest.pack_id == "org.test.mixed";
        });
    ASSERT_NE(offer.end(), found);
    ASSERT_EQ(1u, found->manifest.files.size())
        << "the unshippable entry must be dropped";
    EXPECT_EQ("classpack.yaml", found->manifest.files[0].path);

    EXPECT_TRUE(std::none_of(offer.begin(), offer.end(),
                             [](const og::sim::HostedPack& p) {
                                 return p.manifest.pack_id ==
                                        "org.test.allbad";
                             }))
        << "a pack with nothing shippable must not be announced";
}

// A pack DIRECTORY whose name is not a legal pack id can exist locally (a
// user can make one), but it can never be named on the wire, so it must be
// skipped rather than announced under a name the client would refuse.
TEST_F(PackTransferIoErrorTest, an_unsafely_named_pack_directory_is_skipped)
{
    const fs::path dir =
        stage_pack("weird", {{"classpack.yaml", "pack: weird\n"}});
    mount_pack(dir, "bad id");

    const std::vector<og::sim::HostedPack> offer =
        og::resources::build_transferable_packs();
    EXPECT_TRUE(std::none_of(offer.begin(), offer.end(),
                             [](const og::sim::HostedPack& p) {
                                 return p.manifest.pack_id == "bad id";
                             }));
}

// The local-availability probe must compare CONTENT, not just presence: a
// mounted pack whose bytes drifted from the manifest has to be re-fetched,
// otherwise two peers run different Lua and the sim desyncs.
TEST_F(PackTransferIoErrorTest, a_content_mismatch_is_not_locally_available)
{
    const fs::path dir = stage_pack(
        "drift", {{"classpack.yaml", "pack: org.test.drift\n"},
                  {"scripts/a.lua", "-- local\n"}});
    mount_pack(dir, "org.test.drift");

    const std::vector<og::sim::HostedPack> offer =
        og::resources::build_transferable_packs();
    const auto found = std::find_if(
        offer.begin(), offer.end(), [](const og::sim::HostedPack& p) {
            return p.manifest.pack_id == "org.test.drift";
        });
    ASSERT_NE(offer.end(), found);

    og::sim::PackManifestMessage exact = found->manifest;
    EXPECT_TRUE(og::resources::mounted_pack_matches_manifest(exact));

    og::sim::PackManifestMessage tampered = exact;
    tampered.files[0].hash64 ^= 1ull;
    EXPECT_FALSE(og::resources::mounted_pack_matches_manifest(tampered))
        << "same size, different bytes must not pass";

    og::sim::PackManifestMessage shorter = exact;
    shorter.files.pop_back();
    EXPECT_FALSE(og::resources::mounted_pack_matches_manifest(shorter))
        << "an extra local file is a difference too";
}

// Pack ids reach the filesystem as one path component of the cache
// directory. Anything that could escape it must be refused before a byte
// is written.
TEST_F(PackTransferIoErrorTest, unsafe_pack_ids_never_reach_the_cache)
{
    for (const char* pack_id : {"", "..", ".", "../escape", "a/b", "has space"})
    {
        og::sim::PackManifestMessage manifest;
        manifest.pack_index = 0;
        manifest.pack_count = 1;
        manifest.pack_id = pack_id;
        og::sim::PackManifestFileEntry entry;
        entry.path = "scripts/a.lua";
        entry.size_bytes = 1;
        entry.hash64 = og::core::fnv1a64(
            reinterpret_cast<const std::uint8_t*>("x"), 1u);
        manifest.files.push_back(std::move(entry));

        EXPECT_FALSE(og::resources::try_mount_cached_pack(manifest))
            << "cache probe accepted pack id '" << pack_id << "'";
        EXPECT_FALSE(og::resources::install_received_pack(
            manifest, {std::vector<std::uint8_t>{'x'}}))
            << "install accepted pack id '" << pack_id << "'";
    }

    std::error_code ec;
    EXPECT_FALSE(fs::exists(fs::path(get_user_path()) / "packs_cache", ec))
        << "a refused install must not create the cache tree";
}

// A cache path the process cannot write (here: the destination name is
// already a non-empty directory, so the atomic rename cannot land) must
// fail the install cleanly and leave no torn .tmp behind — a half-written
// cache entry that a later hash check trusted would be the worst outcome.
TEST_F(PackTransferIoErrorTest, an_unwritable_cache_path_fails_the_install)
{
    const std::string content = "-- a\n";
    og::sim::PackManifestMessage manifest;
    manifest.pack_index = 0;
    manifest.pack_count = 1;
    manifest.pack_id = "org.test.unwritable";
    og::sim::PackManifestFileEntry entry;
    entry.path = "scripts/a.lua";
    entry.size_bytes = static_cast<std::uint32_t>(content.size());
    entry.hash64 = og::core::fnv1a64(
        reinterpret_cast<const std::uint8_t*>(content.data()), content.size());
    manifest.files.push_back(std::move(entry));

    // Occupy the destination with a non-empty directory.
    const fs::path cache_dir =
        fs::path(get_user_path()) / "packs_cache" /
        (manifest.pack_id + "@" +
         og::sim::pack_manifest_content_hash_hex(manifest));
    const fs::path blocked = cache_dir / "scripts" / "a.lua";
    std::error_code ec;
    fs::create_directories(blocked, ec);
    ASSERT_FALSE(ec) << ec.message();
    { std::ofstream occupant(blocked / "occupant"); occupant << "x"; }

    EXPECT_FALSE(og::resources::install_received_pack(
        manifest,
        {std::vector<std::uint8_t>(content.begin(), content.end())}));
    EXPECT_FALSE(fs::exists(fs::path(blocked.string() + ".tmp"), ec))
        << "a failed write must not leave a temporary behind";
}

// Two different generations of the same pack id in one session: the second
// install must replace the first mount rather than stack a second search
// path entry, or PhysFS would keep answering with the stale scripts.
TEST_F(PackTransferIoErrorTest, reinstalling_a_pack_id_replaces_its_mount)
{
    auto manifest_for = [](const std::string& content) {
        og::sim::PackManifestMessage m;
        m.pack_index = 0;
        m.pack_count = 1;
        m.pack_id = "org.test.regen";
        og::sim::PackManifestFileEntry entry;
        entry.path = "scripts/a.lua";
        entry.size_bytes = static_cast<std::uint32_t>(content.size());
        entry.hash64 = og::core::fnv1a64(
            reinterpret_cast<const std::uint8_t*>(content.data()),
            content.size());
        m.files.push_back(std::move(entry));
        return m;
    };

    const std::string first = "-- generation one\n";
    const std::string second = "-- generation two, longer\n";
    const og::sim::PackManifestMessage m1 = manifest_for(first);
    const og::sim::PackManifestMessage m2 = manifest_for(second);

    ASSERT_TRUE(og::resources::install_received_pack(
        m1, {std::vector<std::uint8_t>(first.begin(), first.end())}));
    EXPECT_TRUE(og::resources::mounted_pack_matches_manifest(m1));

    // Same manifest again: idempotent, still mounted, still matching.
    ASSERT_TRUE(og::resources::install_received_pack(
        m1, {std::vector<std::uint8_t>(first.begin(), first.end())}));
    EXPECT_TRUE(og::resources::mounted_pack_matches_manifest(m1));

    ASSERT_TRUE(og::resources::install_received_pack(
        m2, {std::vector<std::uint8_t>(second.begin(), second.end())}));
    EXPECT_TRUE(og::resources::mounted_pack_matches_manifest(m2));
    EXPECT_FALSE(og::resources::mounted_pack_matches_manifest(m1))
        << "the superseded generation must not still be answering";
}
