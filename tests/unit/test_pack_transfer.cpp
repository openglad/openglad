// Automatic multiplayer class-pack transfer (protocol v10): wire round-trips
// and fuzz guards for the four Pack* messages, the path validator, the
// host/client state machines, and a loopback host+join end-to-end transfer
// through LobbyServer + the resources install path.

#include <openglad/core/fnv1a.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/pack_transfer.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/pack_transfer_io.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void write_u32_le(std::vector<std::uint8_t>& bytes,
                  std::size_t offset,
                  std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

class RecordingTransport final : public og::sim::ITransport
{
public:
    using og::sim::ITransport::broadcast;

    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        sent_messages_.push_back(
            {peer_id, std::vector<std::uint8_t>(data, data + len)});
    }

    std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> drained =
            std::move(received_messages_);
        received_messages_.clear();
        return drained;
    }

    void accept_connections() override {}
    void disconnect(og::sim::PeerId) override {}

    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return connected_peers_;
    }

    void set_connected_peers(std::vector<og::sim::PeerId> peers)
    {
        connected_peers_ = std::move(peers);
    }

    const std::vector<og::sim::ReceivedMessage>& sent_messages() const noexcept
    {
        return sent_messages_;
    }

    void clear_sent_messages() { sent_messages_.clear(); }

private:
    std::vector<og::sim::PeerId> connected_peers_;
    std::vector<og::sim::ReceivedMessage> received_messages_;
    std::vector<og::sim::ReceivedMessage> sent_messages_;
};

og::sim::PackManifestMessage make_test_manifest()
{
    og::sim::PackManifestMessage manifest;
    manifest.pack_index = 0;
    manifest.pack_count = 1;
    manifest.pack_id = "org.test.mypack";
    manifest.version = "3";
    manifest.files = {
        {.path = "classpack.yaml", .size_bytes = 24, .hash64 = 0x1122334455667788ull},
        {.path = "scripts/warlock.lua", .size_bytes = 900, .hash64 = 0xdeadbeefcafef00dull},
        {.path = "sprites/warlock.png", .size_bytes = 4096, .hash64 = 7},
    };
    return manifest;
}

} // namespace

// --- wire round-trips -------------------------------------------------------

TEST(PackTransferWire, manifest_roundtrip_preserves_all_fields)
{
    const og::sim::PackManifestMessage expected = make_test_manifest();
    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_pack_manifest_message(expected);
    const std::optional<og::sim::PackManifestMessage> decoded =
        og::sim::deserialize_pack_manifest_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(expected, *decoded);
    EXPECT_EQ(24u + 900u + 4096u, decoded->total_bytes());
}

TEST(PackTransferWire, empty_manifest_roundtrip_announces_no_packs)
{
    const og::sim::PackManifestMessage expected; // pack_count == 0
    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_pack_manifest_message(expected);
    const std::optional<og::sim::PackManifestMessage> decoded =
        og::sim::deserialize_pack_manifest_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(expected, *decoded);
    EXPECT_EQ(0u, decoded->pack_count);
    EXPECT_TRUE(decoded->files.empty());
}

TEST(PackTransferWire, request_and_done_roundtrip)
{
    const og::sim::PackRequestMessage request{.pack_id = "org.test.mypack"};
    const auto request_bytes = og::sim::serialize_pack_request_message(request);
    const auto decoded_request =
        og::sim::deserialize_pack_request_message(request_bytes);
    ASSERT_TRUE(decoded_request.has_value());
    EXPECT_EQ(request, *decoded_request);

    const og::sim::PackTransferDoneMessage done{.pack_id = "org.test.mypack"};
    const auto done_bytes = og::sim::serialize_pack_transfer_done_message(done);
    const auto decoded_done =
        og::sim::deserialize_pack_transfer_done_message(done_bytes);
    ASSERT_TRUE(decoded_done.has_value());
    EXPECT_EQ(done, *decoded_done);
}

TEST(PackTransferWire, chunk_roundtrip_preserves_payload)
{
    og::sim::PackFileChunkMessage chunk;
    chunk.pack_id = "org.test.mypack";
    chunk.file_index = 2;
    chunk.offset = 65536;
    chunk.data.resize(og::sim::kPackFileChunkMaxBytes);
    for (std::size_t i = 0; i < chunk.data.size(); ++i)
        chunk.data[i] = static_cast<std::uint8_t>(i * 31u);

    const auto bytes = og::sim::serialize_pack_file_chunk_message(chunk);
    const auto decoded = og::sim::deserialize_pack_file_chunk_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(chunk, *decoded);
}

TEST(PackTransferWire, decode_received_message_produces_pack_kinds)
{
    const og::sim::TypedReceivedMessage manifest =
        og::sim::decode_received_message(
            {.peer_id = 3u,
             .data = og::sim::serialize_pack_manifest_message(
                 make_test_manifest())});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::PackManifest, manifest.kind);
    ASSERT_TRUE(manifest.pack_manifest != nullptr);
    EXPECT_EQ("org.test.mypack", manifest.pack_manifest->pack_id);

    const og::sim::TypedReceivedMessage request =
        og::sim::decode_received_message(
            {.peer_id = 3u,
             .data = og::sim::serialize_pack_request_message(
                 {.pack_id = "p"})});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::PackRequest, request.kind);

    const og::sim::TypedReceivedMessage chunk =
        og::sim::decode_received_message(
            {.peer_id = 3u,
             .data = og::sim::serialize_pack_file_chunk_message(
                 {.pack_id = "p", .file_index = 0, .offset = 0,
                  .data = {1, 2, 3}})});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::PackFileChunk, chunk.kind);

    const og::sim::TypedReceivedMessage done =
        og::sim::decode_received_message(
            {.peer_id = 3u,
             .data = og::sim::serialize_pack_transfer_done_message(
                 {.pack_id = "p"})});
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::PackTransferDone, done.kind);
}

// --- wire fuzz guards -------------------------------------------------------

TEST(PackTransferWire, manifest_rejects_oversized_file_count)
{
    auto bytes = og::sim::serialize_pack_manifest_message(make_test_manifest());
    // Payload layout: header(4) + index(1) + count(1) + id(4+15) + ver(4+1)
    // puts the file count at offset 30.
    const std::size_t count_offset = 4 + 1 + 1 + 4 + 15 + 4 + 1;
    write_u32_le(bytes, count_offset, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_pack_manifest_message(bytes).has_value());

    // A count above the manifest cap is rejected even when each entry would
    // fit in the remaining bytes.
    auto padded = og::sim::serialize_pack_manifest_message(make_test_manifest());
    write_u32_le(padded, count_offset,
                 static_cast<std::uint32_t>(og::sim::kMaxPackManifestFiles + 1));
    EXPECT_FALSE(
        og::sim::deserialize_pack_manifest_message(padded).has_value());
}

TEST(PackTransferWire, manifest_rejects_inconsistent_shapes)
{
    og::sim::PackManifestMessage bad_index = make_test_manifest();
    bad_index.pack_index = 1; // == pack_count
    EXPECT_FALSE(og::sim::deserialize_pack_manifest_message(
                     og::sim::serialize_pack_manifest_message(bad_index))
                     .has_value());

    og::sim::PackManifestMessage bad_count = make_test_manifest();
    bad_count.pack_count = static_cast<std::uint8_t>(
        og::sim::kMaxPacksPerSession + 1);
    EXPECT_FALSE(og::sim::deserialize_pack_manifest_message(
                     og::sim::serialize_pack_manifest_message(bad_count))
                     .has_value());

    og::sim::PackManifestMessage empty_id = make_test_manifest();
    empty_id.pack_id.clear();
    EXPECT_FALSE(og::sim::deserialize_pack_manifest_message(
                     og::sim::serialize_pack_manifest_message(empty_id))
                     .has_value());

    // pack_count == 0 must carry nothing else.
    og::sim::PackManifestMessage nonempty_empty;
    nonempty_empty.pack_count = 0;
    nonempty_empty.pack_id = "sneaky";
    EXPECT_FALSE(og::sim::deserialize_pack_manifest_message(
                     og::sim::serialize_pack_manifest_message(nonempty_empty))
                     .has_value());
}

TEST(PackTransferWire, manifest_rejects_truncated_payload)
{
    auto bytes = og::sim::serialize_pack_manifest_message(make_test_manifest());
    bytes.pop_back();
    const std::uint16_t shorter = static_cast<std::uint16_t>(
        bytes.size() - og::sim::kTransportHeaderSize);
    bytes[2] = static_cast<std::uint8_t>(shorter & 0xffu);
    bytes[3] = static_cast<std::uint8_t>((shorter >> 8) & 0xffu);
    EXPECT_FALSE(
        og::sim::deserialize_pack_manifest_message(bytes).has_value());
}

TEST(PackTransferWire, chunk_rejects_data_over_cap)
{
    og::sim::PackFileChunkMessage chunk;
    chunk.pack_id = "p";
    chunk.data.resize(og::sim::kPackFileChunkMaxBytes + 1, 0xaa);
    // The serializer emits it (payload still < 64 KiB); the decoder is the
    // enforcement point.
    const auto bytes = og::sim::serialize_pack_file_chunk_message(chunk);
    EXPECT_FALSE(
        og::sim::deserialize_pack_file_chunk_message(bytes).has_value());
}

TEST(PackTransferWire, request_rejects_empty_and_oversized_ids)
{
    EXPECT_FALSE(og::sim::deserialize_pack_request_message(
                     og::sim::serialize_pack_request_message({.pack_id = ""}))
                     .has_value());
    EXPECT_FALSE(og::sim::deserialize_pack_transfer_done_message(
                     og::sim::serialize_pack_transfer_done_message(
                         {.pack_id = std::string(
                              og::sim::kMaxPackIdLength + 1, 'a')}))
                     .has_value());
}

// --- path validator ---------------------------------------------------------

TEST(PackTransferValidation, safe_relative_path_rules)
{
    EXPECT_TRUE(og::sim::is_safe_pack_relative_path("classpack.yaml"));
    EXPECT_TRUE(og::sim::is_safe_pack_relative_path("scripts/warlock.lua"));
    EXPECT_TRUE(og::sim::is_safe_pack_relative_path("a/b/c/d.PNG"));
    EXPECT_TRUE(og::sim::is_safe_pack_relative_path("under_score-dash.v2"));

    EXPECT_FALSE(og::sim::is_safe_pack_relative_path(""));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path("/etc/passwd"));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path("../escape.lua"));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path("scripts/../../x"));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path("a/./b"));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path("a//b"));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path("trailing/"));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path("back\\slash"));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path("sp ace.lua"));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path(
        std::string_view("null\0byte", 9)));
    EXPECT_FALSE(og::sim::is_safe_pack_relative_path(
        std::string(og::sim::kMaxPackRelativePathLength + 1, 'a')));

    EXPECT_TRUE(og::sim::is_safe_pack_id("org.test.mypack"));
    EXPECT_TRUE(og::sim::is_safe_pack_id("core"));
    EXPECT_FALSE(og::sim::is_safe_pack_id(""));
    EXPECT_FALSE(og::sim::is_safe_pack_id(".."));
    EXPECT_FALSE(og::sim::is_safe_pack_id("..."));
    EXPECT_FALSE(og::sim::is_safe_pack_id("has/slash"));
    EXPECT_FALSE(og::sim::is_safe_pack_id(
        std::string(og::sim::kMaxPackIdLength + 1, 'a')));
}

TEST(PackTransferValidation, manifest_semantic_validation)
{
    EXPECT_FALSE(validate_pack_manifest(make_test_manifest()).has_value());
    EXPECT_FALSE(
        validate_pack_manifest(og::sim::PackManifestMessage{}).has_value());

    og::sim::PackManifestMessage traversal = make_test_manifest();
    traversal.files[0].path = "scripts/../../evil.lua";
    EXPECT_TRUE(validate_pack_manifest(traversal).has_value());

    og::sim::PackManifestMessage duplicate = make_test_manifest();
    duplicate.files[1].path = duplicate.files[0].path;
    EXPECT_TRUE(validate_pack_manifest(duplicate).has_value());

    og::sim::PackManifestMessage oversized = make_test_manifest();
    oversized.files[0].size_bytes =
        static_cast<std::uint32_t>(og::sim::kMaxPackBytes + 1);
    EXPECT_TRUE(validate_pack_manifest(oversized).has_value());

    // Two files summing past the cap trip the running total.
    og::sim::PackManifestMessage sum = make_test_manifest();
    sum.files[0].size_bytes =
        static_cast<std::uint32_t>(og::sim::kMaxPackBytes / 2 + 1);
    sum.files[1].size_bytes =
        static_cast<std::uint32_t>(og::sim::kMaxPackBytes / 2 + 1);
    EXPECT_TRUE(validate_pack_manifest(sum).has_value());

    og::sim::PackManifestMessage bad_id = make_test_manifest();
    bad_id.pack_id = "no/slashes";
    EXPECT_TRUE(validate_pack_manifest(bad_id).has_value());
}

TEST(PackTransferValidation, manifest_content_hash_tracks_file_table)
{
    const og::sim::PackManifestMessage manifest = make_test_manifest();
    const std::uint64_t base = og::sim::pack_manifest_content_hash(manifest);
    EXPECT_EQ(base, og::sim::pack_manifest_content_hash(manifest));

    og::sim::PackManifestMessage changed = manifest;
    changed.files[1].hash64 ^= 1;
    EXPECT_NE(base, og::sim::pack_manifest_content_hash(changed));

    og::sim::PackManifestMessage reordered = manifest;
    std::swap(reordered.files[0], reordered.files[1]);
    EXPECT_NE(base, og::sim::pack_manifest_content_hash(reordered));

    const std::string hex = og::sim::pack_manifest_content_hash_hex(manifest);
    EXPECT_EQ(16u, hex.size());
}

// --- host state machine -----------------------------------------------------

namespace {

og::sim::HostedPack make_hosted_pack(const std::string& pack_id,
                                     std::vector<std::pair<std::string,
                                                           std::string>> files)
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

} // namespace

TEST(PackTransferHost, set_packs_validates_and_stamps_announcement_shape)
{
    og::sim::PackTransferHost host;

    std::vector<og::sim::HostedPack> offer;
    offer.push_back(make_hosted_pack("bpack", {{"scripts/b.lua", "b"}}));
    offer.push_back(make_hosted_pack("apack", {{"scripts/a.lua", "a"}}));
    // Dropped: the built-in pack is never offered.
    offer.push_back(make_hosted_pack("core", {{"scripts/x.lua", "x"}}));
    // Dropped: traversal path.
    offer.push_back(make_hosted_pack("evil", {{"../up.lua", "u"}}));
    // Dropped: manifest/content length mismatch.
    og::sim::HostedPack mismatched =
        make_hosted_pack("mismatch", {{"scripts/m.lua", "m"}});
    mismatched.file_contents.clear();
    offer.push_back(std::move(mismatched));
    // Dropped: content drifted from the manifest size.
    og::sim::HostedPack drifted =
        make_hosted_pack("drift", {{"scripts/d.lua", "d"}});
    drifted.file_contents[0].push_back(0);
    offer.push_back(std::move(drifted));

    ASSERT_EQ(2u, host.set_packs(std::move(offer)));
    ASSERT_EQ(2u, host.packs().size());
    EXPECT_EQ(0, host.packs()[0].manifest.pack_index);
    EXPECT_EQ(2, host.packs()[0].manifest.pack_count);
    EXPECT_EQ(1, host.packs()[1].manifest.pack_index);
    EXPECT_EQ(2, host.packs()[1].manifest.pack_count);
}

TEST(PackTransferHost, announce_sends_empty_manifest_when_no_packs)
{
    og::sim::PackTransferHost host;
    RecordingTransport transport;
    host.announce_to(transport, 5u);
    ASSERT_EQ(1u, transport.sent_messages().size());
    const auto decoded = og::sim::deserialize_pack_manifest_message(
        transport.sent_messages()[0].data);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(0u, decoded->pack_count);
}

TEST(PackTransferHost, request_streams_sequential_chunks_and_done)
{
    og::sim::PackTransferHost host;
    // 70 KB file forces three chunks; the empty file forces none.
    std::string big(70000, 'x');
    for (std::size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<char>(i % 251);
    std::vector<og::sim::HostedPack> offer;
    offer.push_back(make_hosted_pack(
        "streampack", {{"data/big.bin", big}, {"data/empty.bin", ""}}));
    ASSERT_EQ(1u, host.set_packs(std::move(offer)));

    RecordingTransport transport;
    host.handle_request(transport, 9u, {.pack_id = "streampack"});

    const auto& sent = transport.sent_messages();
    ASSERT_EQ(4u, sent.size()); // 3 chunks + done
    std::vector<std::uint8_t> reassembled;
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto chunk =
            og::sim::deserialize_pack_file_chunk_message(sent[i].data);
        ASSERT_TRUE(chunk.has_value()) << "chunk " << i;
        EXPECT_EQ("streampack", chunk->pack_id);
        EXPECT_EQ(0u, chunk->file_index);
        EXPECT_EQ(reassembled.size(), chunk->offset);
        EXPECT_LE(chunk->data.size(), og::sim::kPackFileChunkMaxBytes);
        reassembled.insert(reassembled.end(), chunk->data.begin(),
                           chunk->data.end());
    }
    EXPECT_EQ(big.size(), reassembled.size());
    EXPECT_TRUE(std::equal(reassembled.begin(), reassembled.end(),
                           big.begin(),
                           [](std::uint8_t lhs, char rhs) {
                               return lhs == static_cast<std::uint8_t>(rhs);
                           }));
    const auto done =
        og::sim::deserialize_pack_transfer_done_message(sent[3].data);
    ASSERT_TRUE(done.has_value());
    EXPECT_EQ("streampack", done->pack_id);

    // Unknown ids are ignored outright.
    transport.clear_sent_messages();
    host.handle_request(transport, 9u, {.pack_id = "who"});
    EXPECT_TRUE(transport.sent_messages().empty());
}

// --- client state machine ---------------------------------------------------

namespace {

struct ClientHarness {
    RecordingTransport transport;
    std::vector<std::string> log;
    std::vector<std::pair<og::sim::PackManifestMessage,
                          std::vector<std::vector<std::uint8_t>>>> installed;
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
            [this](const og::sim::PackManifestMessage& manifest,
                   const std::vector<std::vector<std::uint8_t>>& files) {
                installed.emplace_back(manifest, files);
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
};

} // namespace

TEST(PackTransferClient, requests_receives_verifies_and_installs)
{
    ClientHarness harness;
    const std::string script = "og.log('hello')\n";
    og::sim::HostedPack pack =
        make_hosted_pack("clientpack", {{"scripts/hello.lua", script}});
    pack.manifest.pack_index = 0;
    pack.manifest.pack_count = 1;

    EXPECT_FALSE(harness.client->busy());
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(pack.manifest)));
    EXPECT_TRUE(harness.client->busy());
    EXPECT_NE(std::string::npos,
              harness.client->status_text().find("clientpack"));

    // The request went out to the server peer.
    ASSERT_EQ(1u, harness.transport.sent_messages().size());
    const auto request = og::sim::deserialize_pack_request_message(
        harness.transport.sent_messages()[0].data);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ("clientpack", request->pack_id);

    og::sim::PackFileChunkMessage chunk;
    chunk.pack_id = "clientpack";
    chunk.file_index = 0;
    chunk.offset = 0;
    chunk.data.assign(script.begin(), script.end());
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_file_chunk_message(chunk)));
    ASSERT_TRUE(harness.feed(og::sim::serialize_pack_transfer_done_message(
        {.pack_id = "clientpack"})));

    EXPECT_FALSE(harness.client->busy());
    EXPECT_FALSE(harness.client->failed());
    ASSERT_EQ(1u, harness.installed.size());
    EXPECT_EQ("clientpack", harness.installed[0].first.pack_id);
    ASSERT_EQ(1u, harness.installed[0].second.size());
    EXPECT_EQ(std::vector<std::uint8_t>(script.begin(), script.end()),
              harness.installed[0].second[0]);
    EXPECT_TRUE(harness.client->status_text().empty());
}

TEST(PackTransferClient, skips_locally_available_packs)
{
    ClientHarness harness;
    harness.locally_available = true;
    og::sim::PackManifestMessage manifest = make_test_manifest();
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(manifest)));
    EXPECT_FALSE(harness.client->busy());
    EXPECT_TRUE(harness.transport.sent_messages().empty());
    EXPECT_TRUE(harness.installed.empty());
}

TEST(PackTransferClient, hash_mismatch_fails_transfer)
{
    ClientHarness harness;
    og::sim::HostedPack pack =
        make_hosted_pack("hashpack", {{"scripts/x.lua", "content"}});
    pack.manifest.pack_index = 0;
    pack.manifest.pack_count = 1;
    pack.manifest.files[0].hash64 ^= 1; // Claim a different hash.
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(pack.manifest)));

    og::sim::PackFileChunkMessage chunk;
    chunk.pack_id = "hashpack";
    chunk.data = pack.file_contents[0];
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_file_chunk_message(chunk)));
    ASSERT_TRUE(harness.feed(og::sim::serialize_pack_transfer_done_message(
        {.pack_id = "hashpack"})));

    EXPECT_TRUE(harness.client->failed());
    EXPECT_TRUE(harness.installed.empty());
    EXPECT_NE(std::string::npos,
              harness.client->failure_reason().find("verification"));
    EXPECT_NE(std::string::npos,
              harness.client->status_text().find("failed"));
}

TEST(PackTransferClient, out_of_sequence_chunk_fails_transfer)
{
    ClientHarness harness;
    og::sim::HostedPack pack =
        make_hosted_pack("seqpack", {{"scripts/x.lua", "0123456789"}});
    pack.manifest.pack_index = 0;
    pack.manifest.pack_count = 1;
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(pack.manifest)));

    og::sim::PackFileChunkMessage chunk;
    chunk.pack_id = "seqpack";
    chunk.offset = 5; // Stream must start at 0.
    chunk.data = {1, 2, 3};
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_file_chunk_message(chunk)));
    EXPECT_TRUE(harness.client->failed());
}

TEST(PackTransferClient, incomplete_transfer_at_done_fails)
{
    ClientHarness harness;
    og::sim::HostedPack pack =
        make_hosted_pack("shortpack", {{"scripts/x.lua", "0123456789"}});
    pack.manifest.pack_index = 0;
    pack.manifest.pack_count = 1;
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(pack.manifest)));
    ASSERT_TRUE(harness.feed(og::sim::serialize_pack_transfer_done_message(
        {.pack_id = "shortpack"})));
    EXPECT_TRUE(harness.client->failed());
    EXPECT_NE(std::string::npos,
              harness.client->failure_reason().find("before"));
}

TEST(PackTransferClient, unsafe_manifest_path_refuses_transfer)
{
    ClientHarness harness;
    og::sim::PackManifestMessage manifest = make_test_manifest();
    manifest.files[0].path = "a/../../escape.lua";
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(manifest)));
    EXPECT_TRUE(harness.client->failed());
    EXPECT_TRUE(harness.transport.sent_messages().empty());
}

TEST(PackTransferClient, session_volume_cap_refuses_join)
{
    ClientHarness harness;
    harness.locally_available = true; // No buffers allocated for this test.

    // Five 16 MiB packs claim 80 MiB > the 64 MiB session cap; the fifth
    // manifest trips the refusal even though every pack was skippable.
    const std::uint8_t count = 5;
    for (std::uint8_t index = 0; index < count; ++index)
    {
        og::sim::PackManifestMessage manifest;
        manifest.pack_index = index;
        manifest.pack_count = count;
        manifest.pack_id = std::string("bigpack") +
                           static_cast<char>('a' + index);
        manifest.files = {{.path = "blob.bin",
                           .size_bytes = static_cast<std::uint32_t>(
                               og::sim::kMaxPackBytes),
                           .hash64 = 1}};
        ASSERT_TRUE(harness.feed(
            og::sim::serialize_pack_manifest_message(manifest)));
    }
    EXPECT_TRUE(harness.client->failed());
    EXPECT_NE(std::string::npos,
              harness.client->failure_reason().find("session"));

    // A fresh generation (pack_index 0) clears the latched failure.
    og::sim::PackManifestMessage retry;
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(retry)));
    EXPECT_FALSE(harness.client->failed());
    EXPECT_FALSE(harness.client->busy());
}

TEST(PackTransferClient, mid_announcement_counts_as_busy)
{
    ClientHarness harness;
    harness.locally_available = true;
    og::sim::PackManifestMessage first = make_test_manifest();
    first.pack_index = 0;
    first.pack_count = 2;
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(first)));
    EXPECT_TRUE(harness.client->busy()) << "second manifest still due";

    og::sim::PackManifestMessage second = make_test_manifest();
    second.pack_index = 1;
    second.pack_count = 2;
    second.pack_id = "org.test.otherpack";
    ASSERT_TRUE(harness.feed(
        og::sim::serialize_pack_manifest_message(second)));
    EXPECT_FALSE(harness.client->busy());
}

// --- loopback end-to-end ----------------------------------------------------

namespace {

namespace fs = std::filesystem;

constexpr const char* kE2ePackId = "org.test.e2epack";
constexpr const char* kE2eScript = "og.log('transferred pack loaded')\n";
constexpr const char* kE2eYaml = "pack: org.test.e2epack\nversion: 1\n";
// Split-layout descriptor file: families/*.yaml is ordinary pack content,
// so it must ride the manifest and install — with
// its tuning — on the receiving side exactly like a monolithic
// classpack.yaml.
constexpr const char* kE2eFamilyYaml =
    "families:\n"
    "  living:\n"
    "    - id: e2epack:warrior\n"
    "      wire_id: auto\n"
    "      name: \"E2E WARRIOR\"\n"
    "      tuning: {transferred_key: 42}\n";

bool pack_script_registered(const char* pack_id)
{
    for (const auto& script : og::script::pack_scripts())
    {
        if (script.pack_id == pack_id)
            return true;
    }
    return false;
}

class PackTransferE2ETest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        staging_ = fs::path(get_user_path()) / "pack_e2e_stage" / kE2ePackId;
        std::error_code ec;
        fs::create_directories(staging_ / "scripts", ec);
        ASSERT_FALSE(ec) << ec.message();
        fs::create_directories(staging_ / "families", ec);
        ASSERT_FALSE(ec) << ec.message();
        write_text(staging_ / "classpack.yaml", kE2eYaml);
        write_text(staging_ / "families" / "warrior.yaml", kE2eFamilyYaml);
        write_text(staging_ / "scripts" / "hello.lua", kE2eScript);
        // Binary-ish third file exercises non-text content.
        std::ofstream png(staging_ / "sprite.bin", std::ios::binary);
        for (int i = 0; i < 300; ++i)
            png.put(static_cast<char>(i % 256));
        png.close();
    }

    void TearDown() override
    {
        og::resources::unmount_session_packs();
        (void)og::resources::unmount(staging_.string().c_str());
        (void)og::resources::refresh_pack_scripts();
        std::error_code ec;
        fs::remove_all(fs::path(get_user_path()) / "pack_e2e_stage", ec);
        fs::remove_all(fs::path(get_user_path()) / "packs_cache", ec);
    }

    static void write_text(const fs::path& path, const char* text)
    {
        std::ofstream out(path, std::ios::binary);
        out << text;
        ASSERT_TRUE(out.good()) << path;
    }

    fs::path staging_;
};

} // namespace

TEST_F(PackTransferE2ETest, host_offers_and_join_client_installs_and_registers)
{
    // --- host side: mount the synthetic pack and build the offer.
    ASSERT_TRUE(og::resources::mount(staging_.string().c_str(),
                                     ("packs/" + std::string(kE2ePackId) + "/")
                                         .c_str(),
                                     1));
    std::vector<og::sim::HostedPack> offer =
        og::resources::build_transferable_packs();
    const auto offered = std::find_if(
        offer.begin(), offer.end(), [](const og::sim::HostedPack& pack) {
            return pack.manifest.pack_id == kE2ePackId;
        });
    ASSERT_NE(offer.end(), offered) << "mounted pack must be offered";
    ASSERT_EQ(4u, offered->manifest.files.size())
        << "families/warrior.yaml must ride the manifest walk";
    const og::sim::PackManifestMessage host_manifest = offered->manifest;

    // The joining machine does NOT have the pack: drop the host-side mount
    // (the offer holds the bytes) so the shared-process filesystem looks
    // like a vanilla client.
    ASSERT_TRUE(og::resources::unmount(staging_.string().c_str()));
    (void)og::resources::refresh_pack_scripts();
    ASSERT_FALSE(pack_script_registered(kE2ePackId));
    ASSERT_FALSE(og::resources::mounted_pack_matches_manifest(host_manifest));

    // --- transports + lobby: in-process pair, LobbyServer owns the host end.
    auto server_transport = og::sim::InProcessTransport::create_server();
    server_transport->accept_connections();
    og::sim::LobbyServer lobby(*server_transport);
    lobby.set_hosted_packs(std::move(offer));

    auto client_transport = server_transport->create_client_transport();
    const og::sim::PeerId server_peer_id = client_transport->local_peer_id();

    std::vector<std::string> status_log;
    og::sim::PackTransferClient::Callbacks callbacks =
        og::resources::make_pack_transfer_client_callbacks();
    callbacks.log_status = [&status_log](const std::string& text) {
        status_log.push_back(text);
    };
    og::sim::PackTransferClient pack_client(std::move(callbacks));

    const auto drain_client = [&] {
        for (const og::sim::TypedReceivedMessage& message :
             client_transport->poll_typed())
        {
            pack_client.handle_message(*client_transport, server_peer_id,
                                       message);
        }
    };

    // Connect: the lobby sends state + the pack manifest; the client
    // requests; the lobby serves; the client verifies, installs and mounts.
    lobby.poll_incoming_messages();
    drain_client();
    EXPECT_FALSE(pack_client.failed()) << pack_client.failure_reason();
    lobby.poll_incoming_messages();
    drain_client();

    ASSERT_FALSE(pack_client.failed()) << pack_client.failure_reason();
    EXPECT_FALSE(pack_client.busy());

    // Files landed in the content-addressed cache with matching hashes.
    const fs::path cache_dir =
        fs::path(get_user_path()) /
        ("packs_cache/" + std::string(kE2ePackId) + "@" +
         og::sim::pack_manifest_content_hash_hex(host_manifest));
    for (const og::sim::PackManifestFileEntry& file : host_manifest.files)
    {
        const fs::path on_disk = cache_dir / file.path;
        ASSERT_TRUE(fs::exists(on_disk)) << on_disk;
        std::ifstream in(on_disk, std::ios::binary);
        const std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        EXPECT_EQ(file.size_bytes, bytes.size()) << file.path;
        EXPECT_EQ(file.hash64,
                  og::core::fnv1a64(bytes.data(), bytes.size()))
            << file.path;
    }

    // The cache dir is mounted at packs/<id>/ and the script registry
    // picked the transferred script up.
    EXPECT_TRUE(og::resources::mounted_pack_matches_manifest(host_manifest));
    EXPECT_TRUE(pack_script_registered(kE2ePackId))
        << "transferred scripts/hello.lua must reach the script registry";
    const std::vector<std::uint8_t> via_vfs = og::resources::read_file(
        ("packs/" + std::string(kE2ePackId) + "/scripts/hello.lua").c_str());
    EXPECT_EQ(std::string(kE2eScript),
              std::string(via_vfs.begin(), via_vfs.end()));

    // The transferred families/ descriptor installed on the receiving side,
    // tuning included.
    const int warrior_id = og::families::resolve_family_string_id(
        Order::Living, "e2epack:warrior");
    ASSERT_GE(warrior_id, 0)
        << "families/warrior.yaml must install from the mounted cache";
    const og::script::TuningMap* warrior_tuning =
        og::script::family_tuning(Order::Living, warrior_id);
    ASSERT_NE(warrior_tuning, nullptr);
    ASSERT_EQ(warrior_tuning->size(), 1u);
    EXPECT_EQ((*warrior_tuning)[0].key, "transferred_key");
    EXPECT_EQ((*warrior_tuning)[0].value.integer, 42);

    // Progress surfaced through the status sink.
    EXPECT_FALSE(status_log.empty());
    EXPECT_NE(std::string::npos, status_log.front().find("Receiving pack"));

    // --- second announcement (return-to-lobby shape): already mounted, so
    // the client skips the transfer instead of re-downloading.
    const std::size_t installs_before = status_log.size();
    std::vector<og::sim::HostedPack> second_offer =
        og::resources::build_transferable_packs();
    const auto second = std::find_if(
        second_offer.begin(), second_offer.end(),
        [](const og::sim::HostedPack& pack) {
            return pack.manifest.pack_id == kE2ePackId;
        });
    ASSERT_NE(second_offer.end(), second)
        << "session-mounted pack re-offers on the next round";
    lobby.set_hosted_packs(std::move(second_offer));
    drain_client();
    EXPECT_FALSE(pack_client.busy());
    EXPECT_FALSE(pack_client.failed()) << pack_client.failure_reason();
    // No new transfer began: no fresh "Receiving pack" line was logged.
    for (std::size_t i = installs_before; i < status_log.size(); ++i)
        EXPECT_EQ(std::string::npos, status_log[i].find("Receiving pack"));
}

// --- content-addressed cache reuse ------------------------------------------

namespace {

// A pack that only ever exists in the received-pack cache: it is never
// mounted from the asset tree, so `pack_locally_available` can only answer
// true by finding and verifying the cached copy on disk.
class PackCacheReuseTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        manifest_.pack_id = kCachedPackId;
        manifest_.pack_index = 0;
        manifest_.pack_count = 1;
        add_file("classpack.yaml", "pack: org.test.cachedpack\nversion: 1\n");
        add_file("scripts/hello.lua", "og.log('cached pack loaded')\n");
        cache_dir_ = fs::path(get_user_path()) /
                     ("packs_cache/" + std::string(kCachedPackId) + "@" +
                      og::sim::pack_manifest_content_hash_hex(manifest_));
    }

    void TearDown() override
    {
        og::resources::unmount_session_packs();
        (void)og::resources::refresh_pack_scripts();
        std::error_code ec;
        fs::remove_all(fs::path(get_user_path()) / "packs_cache", ec);
    }

    void add_file(const char* path, std::string_view text)
    {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        og::sim::PackManifestFileEntry entry;
        entry.path = path;
        entry.size_bytes = static_cast<std::uint32_t>(bytes.size());
        entry.hash64 = og::core::fnv1a64(bytes.data(), bytes.size());
        manifest_.files.push_back(std::move(entry));
        contents_.push_back(std::move(bytes));
    }

    static constexpr const char* kCachedPackId = "org.test.cachedpack";
    og::sim::PackManifestMessage manifest_;
    std::vector<std::vector<std::uint8_t>> contents_;
    fs::path cache_dir_;
};

} // namespace

TEST_F(PackCacheReuseTest, a_cached_pack_is_remounted_without_a_second_transfer)
{
    // First session: the bytes arrive over the wire and land in the cache.
    ASSERT_TRUE(og::resources::install_received_pack(manifest_, contents_));
    ASSERT_TRUE(og::resources::mounted_pack_matches_manifest(manifest_));

    // Second session (return to lobby, or a fresh process): the mount is
    // gone but the content-addressed cache is not.
    og::resources::unmount_session_packs();
    ASSERT_FALSE(og::resources::mounted_pack_matches_manifest(manifest_))
        << "the session mount must really be gone for this to prove anything";

    EXPECT_TRUE(og::resources::pack_locally_available(manifest_))
        << "a verified cache hit must satisfy the join without a transfer";
    EXPECT_TRUE(og::resources::mounted_pack_matches_manifest(manifest_))
        << "the cache hit must also mount the pack";
}

TEST_F(PackCacheReuseTest, a_corrupted_cache_entry_forces_a_fresh_transfer)
{
    ASSERT_TRUE(og::resources::install_received_pack(manifest_, contents_));
    og::resources::unmount_session_packs();

    // Same byte count, different bytes: only the hash check can catch this,
    // and it must, or a torn/edited cache would feed the deterministic sim.
    const fs::path victim = cache_dir_ / manifest_.files[1].path;
    {
        std::ofstream out(victim, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good()) << victim;
        out << std::string(manifest_.files[1].size_bytes, 'x');
    }

    EXPECT_FALSE(og::resources::pack_locally_available(manifest_))
        << "a hash mismatch in the cache must not satisfy the join";
    EXPECT_FALSE(og::resources::mounted_pack_matches_manifest(manifest_));
}

TEST_F(PackCacheReuseTest, a_missing_cache_entry_forces_a_fresh_transfer)
{
    ASSERT_TRUE(og::resources::install_received_pack(manifest_, contents_));
    og::resources::unmount_session_packs();

    std::error_code ec;
    fs::remove(cache_dir_ / manifest_.files[0].path, ec);
    ASSERT_FALSE(ec) << ec.message();

    EXPECT_FALSE(og::resources::pack_locally_available(manifest_))
        << "a partially-deleted cache must not satisfy the join";
}

TEST_F(PackCacheReuseTest, install_re_validates_the_manifest_it_is_handed)
{
    // install_received_pack writes to disk, so it re-checks independently of
    // the gameplay client that already validated the stream.
    std::vector<std::vector<std::uint8_t>> short_payload = contents_;
    short_payload.pop_back();
    EXPECT_FALSE(
        og::resources::install_received_pack(manifest_, short_payload))
        << "file count must match the manifest";

    std::vector<std::vector<std::uint8_t>> drifted = contents_;
    drifted[0].push_back('!');
    EXPECT_FALSE(og::resources::install_received_pack(manifest_, drifted))
        << "a file whose size drifted from the manifest must be refused";

    og::sim::PackManifestMessage unsafe = manifest_;
    unsafe.pack_id = "../escape";
    EXPECT_FALSE(og::resources::install_received_pack(unsafe, contents_))
        << "an unsafe pack id must never reach the filesystem";

    og::sim::PackManifestMessage traversal = manifest_;
    traversal.files[0].path = "../escape.txt";
    EXPECT_FALSE(og::resources::install_received_pack(traversal, contents_))
        << "a traversing file path must never reach the filesystem";

    EXPECT_FALSE(fs::exists(cache_dir_))
        << "no rejected install may leave anything behind";
}
