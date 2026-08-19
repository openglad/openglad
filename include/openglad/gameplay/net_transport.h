#pragma once

#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/lobby_state.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace og::sim {

struct SimEventBatch;
struct WorldSnapshot;

using PeerId = std::uint32_t;

enum class NetMessageType : std::uint8_t {
    Hello = 1,
    InputMessage = 2,
    SnapshotMessage = 3,
    DeltaSnapshotMessage = 4,
    SimEventBatchMessage = 5,
    GameFlowEventBatchMessage = 6,
    LobbyMessage = 7,
    LobbyStateMessage = 8,
    InitialSetup = 9,
    ClientReady = 10,
    KeyframeRequest = 11,
    Heartbeat = 12,
    ExitPromptBroadcast = 13,
    ExitPromptResponse = 14,
    PauseBroadcast = 15,
    PauseResponse = 16,
    ControlChange = 17,
    SnapshotHashCheck = 18,
    PackManifest = 19,
    PackRequest = 20,
    PackFileChunk = 21,
    PackTransferDone = 22,
    StagedMatchSetup = 23,
    StagedMatchKeyframe = 24,
};

constexpr std::uint8_t net_message_type_value(NetMessageType message_type) noexcept
{
    return static_cast<std::uint8_t>(message_type);
}

// v2: lobby settings carry the three CTF fields (team count, capture limit,
// respawn ticks) and the snapshot format moved to v4 alongside them.
// v3: lobby settings carry the CTF scenario-troops strip flag and the
// snapshot format moved to v5 alongside it.
// v4: entities carry Z-axis fields (worldz/vz/sizez/floor); snapshot format
// moved to v6 and replay to v7 alongside it. See docs/z-axis-design.md.
// v5: WorldSnapshot carries the per-level weather kind (render-only sim
// state, rolled by the authoritative side); snapshot format moved to v7 and
// replay to v8 alongside it.
// v6: difficulty submenu — lobby settings carry respawn_mode/generator_rate/
// keep_fallen_heroes, InitialSetup carries respawn_mode/generator_rate,
// WorldSnapshot carries the respawn_mode/generator_rate world scalars (after
// the CTF block), entities carry the spawn-point fields (spawn_x/spawn_y/
// spawn_floor), and CTF respawn entries carry x/y/floor; snapshot format
// moved to v8 and replay to v9 alongside them.
// v7: multiple local players per peer ("seats"). LobbyMessage/Join appends a
// u8 extra-seat count plus that many serialized LobbyPlayers (seats 1..N-1;
// seat 0 stays in the existing player field), InitialSetup's trailing
// controlled-entity-id block becomes u8-count-prefixed (sender writes
// kMaxGlobalPlayers u32 ids), and LobbyPlayerBinding carries the per-peer
// local_slot. Global player indices may now exceed MAX_PLAYERS (per-machine
// cap) up to kMaxGlobalPlayers. Snapshot format stays v8, replay stays v9,
// and the InputMessage keeps exactly MAX_PLAYERS slots (per-machine cap).
// v8: company & base camp (docs/company-basecamp-design.md §4.1).
// LobbyCharacterSlot gains a slot_flags byte after slot_index (bit0 =
// deployed, bits 1-7 reserved-zero; the serialized guy payload is unchanged),
// LobbyPlayer gains a company display-name string after name (reader clamps
// to 40 chars), lobby settings gain cross_control as the 11th i16, and
// LobbyState gains a u8 last_start_denial echo (StartDenialReason) after
// host_player_id. WorldSnapshot carries the control_policy byte and the
// u8[16] player_machine map (low 7 bits machine id, bit7 machine-deployed,
// 0xff none) after the respawn_mode/generator_rate scalars; snapshot format
// moved to v9 and replay to v10 alongside them.
// v9: stable lobby-seat and machine identity. LobbyPlayer gains server-issued
// u32 seat_id and u32 machine_id after player_index; TeamChange carries the
// seat token after its retained player_index so a queued request cannot alias
// another seat when dense P# ordinals change after a disconnect. LobbyState
// appends a recipient-specific u8 count plus u32 local_seat_ids, providing
// authoritative ownership without trusting display names. LobbySettings gains
// an authored CTF-team-mask u8. StartGame carries a u32 request_id echoed by
// its confirmation or LobbyState's u32 last_start_request_id on denial. Join
// likewise carries a u32 request_id plus a resume-after-level flag, and
// LobbyState appends the recipient's u32 last_join_request_id and host-peer
// flag after local_seat_ids. The resume flag is the only Join form retained
// while a level has locked the lobby, preventing a seat mutation that lost a
// StartGame race from appearing silently next round; recipient host authority
// remains visible even when that machine has zero active seats. RemoveSeat
// carries the retained player_index plus the server-issued seat_id, allowing
// an owned middle seat to leave without retargeting its siblings by dense
// ordinal.
// Snapshot protocol follows the network version, so replay format moves to v11.
// v10: automatic multiplayer class-pack transfer (docs/lua-classpacks-design.md
// §8). Four new message types: PackManifest (19, host→client: pack_index/
// pack_count session set, pack id + version, FNV-1a-hashed file table),
// PackRequest (20, client→host: pack id), PackFileChunk (21, host→client:
// ≤32 KiB sequential file bytes), PackTransferDone (22, host→client: end of a
// pack's stream). Manifests are sent by the LobbyServer during the lobby
// handshake, alongside the initial LobbyState. Existing payload layouts are
// unchanged; replay format moves to v12 with the envelope byte.
// v11: LobbySettings gains a twelfth i16, infinite_gold (the DIFFICULTY
// screen's host-only free-purchase setting), appended after cross_control in
// append/read_lobby_settings. No snapshot payload change — the setting only
// gates the picker's hire/train purchases and never reaches the sim — but the
// snapshot/input protocol byte follows the network version, so replay format
// moves to v13.
// v12: snapshot format v10 — the flattened CTF world block (flags, control
// points, captures) is replaced by two generic blocks: the RespawnState
// engine block (respawn_ticks/serial, team anchors, respawn queue) and the
// ModeState block (64 vars, HUD lines, beacons, mode name, win latch) that
// every scripted mode replicates through. LobbySettings gains a thirteenth
// i16, shared_teams (host sets it from campaign_matchup(id) == "versus";
// sanitized to {0,1}), appended after infinite_gold — the lobby's
// shared-teams rule now rides the wire instead of comparing campaign ids.
// Replay format moves to v14.
// v13: staged lobby (#218). Two new message types deliver the lobby OWNER's
// staged world to joiners during the lobby phase: StagedMatchSetup (23,
// u32 stage_generation + a length-prefixed complete InitialSetup message)
// and StagedMatchKeyframe (24, u32 stage_generation + a length-prefixed
// complete keyframe SnapshotMessage of the staged tick-0 world). Pairs are
// generation-stamped; a keyframe whose generation differs from the last
// received setup's is dropped by the client. The serializers REFUSE (empty
// result, no throw) when the inner message would push the wrapper past the
// u16 payload cap. StartDenialReason gains StageFailed (4) — the owner's
// start gate denies GO while its stage is Failed. Reader-side hardening
// rides along: read_serialized_guy clamps guy names to the 11-char disk
// width and read_lobby_player clamps player names to the 40-char company
// cap. Snapshot format stays v10 and replay stays v15 (no world-state or
// recording layout change; the v14→v15 replay bump already broke the
// versions-move-in-lockstep convention).
inline constexpr std::uint8_t kNetworkProtocolVersion = 13;

// Global networked player-index cap (seats across ALL peers). Distinct from
// MAX_PLAYERS, which stays 4 and caps the seats of ONE machine (input slots,
// viewscreens, replay slots, lobby team range). 16 keeps every player index
// u8-safe (0xff stays the "none" sentinel) and within the walker user tag's
// signed-char range.
inline constexpr std::size_t kMaxGlobalPlayers = 16;
using ControlledEntityIds = std::array<std::uint32_t, kMaxGlobalPlayers>;
inline constexpr std::size_t kTransportHeaderSize = 4;
inline constexpr std::size_t kSessionTokenSize = 16;
using SessionToken = std::array<std::uint8_t, kSessionTokenSize>;
inline constexpr SessionToken kZeroSessionToken = {};
inline bool is_zero_session_token(const SessionToken& token) noexcept
{
    return token == kZeroSessionToken;
}
inline constexpr std::size_t kSerializedHelloPayloadSize =
    3 + kSessionTokenSize + sizeof(std::uint32_t);
inline constexpr std::size_t kSerializedHelloMessageSize =
    kTransportHeaderSize + kSerializedHelloPayloadSize;
inline constexpr std::uint8_t kHelloMessageType =
    net_message_type_value(NetMessageType::Hello);
inline constexpr std::uint8_t kInputMessageType =
    net_message_type_value(NetMessageType::InputMessage);
inline constexpr std::uint8_t kInputStateMessageType = kInputMessageType;
inline constexpr std::uint8_t kSnapshotMessageType =
    net_message_type_value(NetMessageType::SnapshotMessage);
inline constexpr std::uint8_t kDeltaSnapshotMessageType =
    net_message_type_value(NetMessageType::DeltaSnapshotMessage);
inline constexpr std::uint8_t kSimEventBatchMessageType =
    net_message_type_value(NetMessageType::SimEventBatchMessage);
inline constexpr std::uint8_t kGameFlowEventBatchMessageType =
    net_message_type_value(NetMessageType::GameFlowEventBatchMessage);
inline constexpr std::uint8_t kLobbyMessageType =
    net_message_type_value(NetMessageType::LobbyMessage);
inline constexpr std::uint8_t kLobbyStateMessageType =
    net_message_type_value(NetMessageType::LobbyStateMessage);
inline constexpr std::uint8_t kInitialSetupMessageType =
    net_message_type_value(NetMessageType::InitialSetup);
inline constexpr std::uint8_t kClientReadyMessageType =
    net_message_type_value(NetMessageType::ClientReady);
inline constexpr std::uint8_t kKeyframeRequestMessageType =
    net_message_type_value(NetMessageType::KeyframeRequest);
inline constexpr std::uint8_t kHeartbeatMessageType =
    net_message_type_value(NetMessageType::Heartbeat);
inline constexpr std::uint8_t kExitPromptBroadcastMessageType =
    net_message_type_value(NetMessageType::ExitPromptBroadcast);
inline constexpr std::uint8_t kExitPromptResponseMessageType =
    net_message_type_value(NetMessageType::ExitPromptResponse);
inline constexpr std::uint8_t kPauseBroadcastMessageType =
    net_message_type_value(NetMessageType::PauseBroadcast);
inline constexpr std::uint8_t kPauseResponseMessageType =
    net_message_type_value(NetMessageType::PauseResponse);
inline constexpr std::uint8_t kControlChangeMessageType =
    net_message_type_value(NetMessageType::ControlChange);
inline constexpr std::uint8_t kSnapshotHashCheckMessageType =
    net_message_type_value(NetMessageType::SnapshotHashCheck);
inline constexpr std::uint8_t kPackManifestMessageType =
    net_message_type_value(NetMessageType::PackManifest);
inline constexpr std::uint8_t kPackRequestMessageType =
    net_message_type_value(NetMessageType::PackRequest);
inline constexpr std::uint8_t kPackFileChunkMessageType =
    net_message_type_value(NetMessageType::PackFileChunk);
inline constexpr std::uint8_t kPackTransferDoneMessageType =
    net_message_type_value(NetMessageType::PackTransferDone);
inline constexpr std::uint8_t kStagedMatchSetupMessageType =
    net_message_type_value(NetMessageType::StagedMatchSetup);
inline constexpr std::uint8_t kStagedMatchKeyframeMessageType =
    net_message_type_value(NetMessageType::StagedMatchKeyframe);

// Hello payload wire format:
// - byte 0: current protocol version
// - byte 1: minimum supported protocol version
// - byte 2: snapshot format version
// - bytes 3-18: session token
// - bytes 19-22: little-endian campaign content hash
struct HelloMessage {
    std::uint8_t protocol_version = kNetworkProtocolVersion;
    std::uint8_t min_protocol_version = kNetworkProtocolVersion;
    std::uint8_t snapshot_format_version = 0;
    SessionToken session_token = {};
    std::uint32_t campaign_content_hash = 0;

    bool operator==(const HelloMessage&) const = default;
};

struct InitialSetupGuyData {
    std::int32_t guy_id = 0;
    std::string name;
    std::int8_t family = 0;
    std::int16_t strength = 0;
    std::int16_t dexterity = 0;
    std::int16_t constitution = 0;
    std::int16_t intelligence = 0;
    std::int16_t armor = 0;
    std::uint32_t exp = 0;
    std::int16_t kills = 0;
    std::int32_t level_kills = 0;
    std::int32_t total_damage = 0;
    std::int32_t total_hits = 0;
    std::int32_t total_shots = 0;
    std::int16_t teamnum = 0;
    float scen_damage = 0.0f;
    std::int16_t scen_kills = 0;
    float scen_damage_taken = 0.0f;
    float scen_min_hp = 0.0f;
    std::int16_t scen_shots = 0;
    std::int16_t scen_hits = 0;
    std::int16_t level = 0;

    bool operator==(const InitialSetupGuyData&) const = default;
};

struct InitialSetupMessage {
    std::int32_t level_id = 0;
    std::string level_title;
    std::int8_t level_type = 0;
    std::int16_t par_value = 0;
    std::int16_t time_bonus_limit = 0;
    std::int16_t difficulty = 0;
    std::int32_t pixmaxx = 0;
    std::int32_t pixmaxy = 0;
    std::int16_t my_team = 0;
    std::int16_t allied_mode = 0;
    std::int16_t current_scenario = 0;
    std::int16_t respawn_mode = 0;
    std::int16_t generator_rate = 0;
    std::vector<InitialSetupGuyData> guys;
    std::vector<std::int32_t> completed_levels;
    // Keyed by GLOBAL player index (u8-count-prefixed on the wire).
    ControlledEntityIds controlled_entity_ids = {};

    bool operator==(const InitialSetupMessage&) const = default;
};

struct ClientReadyMessage {
    std::uint32_t last_applied_tick = 0;

    bool operator==(const ClientReadyMessage&) const = default;
};

struct KeyframeRequestMessage {
    std::uint32_t last_seen_tick = 0;

    bool operator==(const KeyframeRequestMessage&) const = default;
};

struct HeartbeatMessage {
    bool operator==(const HeartbeatMessage&) const = default;
};

struct ExitPromptBroadcastMessage {
    std::int16_t destination_level = -1;
    bool withdraw_prompt = false;
    std::string prompt_text;

    bool operator==(const ExitPromptBroadcastMessage&) const = default;
};

struct ExitPromptResponseMessage {
    bool accepted = false;
    // Client -> server unilateral "quit this mission" request (no pending
    // prompt). The server withdraws ALL players to the current level so every
    // peer returns to the team-build menu together — instead of this one client
    // leaving and its character being converted to AI.
    bool abort_request = false;

    bool operator==(const ExitPromptResponseMessage&) const = default;
};

struct PauseBroadcastMessage {
    std::uint8_t player_index = 0xff;
    std::string player_name;

    bool operator==(const PauseBroadcastMessage&) const = default;
};

struct PauseResponseMessage {
    bool resume = true;

    bool operator==(const PauseResponseMessage&) const = default;
};

struct ControlChangeMessage {
    std::uint8_t player_index = 0xff;
    std::uint32_t entity_id = 0;

    bool operator==(const ControlChangeMessage&) const = default;
};

struct SnapshotHashCheckMessage {
    std::uint32_t tick = 0;
    std::uint32_t snapshot_hash = 0;

    bool operator==(const SnapshotHashCheckMessage&) const = default;
};

// --- Class-pack transfer messages (protocol v10) ---------------------------
// Class packs are directories under the virtual path packs/<pack_id>/. The
// host offers its non-core mounted packs; a client that lacks a pack (or
// whose local content differs) requests it and receives the files over the
// reliable transport, before play. Path validation and the transfer state
// machines live in <openglad/gameplay/pack_transfer.h>; the wire-level caps
// below are enforced by the deserializers themselves.

// Chunk payload cap. Keeps every PackFileChunk comfortably inside the u16
// transport payload limit (32 KiB data + id/indices < 64 KiB).
inline constexpr std::size_t kPackFileChunkMaxBytes = 32u * 1024u;
// Manifest shape caps (wire-structural; the byte-volume caps kMaxPackBytes/
// kMaxSessionPackBytes are semantic and live in pack_transfer.h).
inline constexpr std::size_t kMaxPackManifestFiles = 512;
inline constexpr std::size_t kMaxPackIdLength = 64;
inline constexpr std::size_t kMaxPackVersionLength = 32;
inline constexpr std::size_t kMaxPackRelativePathLength = 160;
inline constexpr std::size_t kMaxPacksPerSession = 16;

// One file of a pack manifest. `path` is relative to the pack root
// (forward-slash separated, validated by is_safe_pack_relative_path);
// `hash64` is FNV-1a over the file bytes (integrity/versioning only — see
// <openglad/core/fnv1a.h>).
struct PackManifestFileEntry {
    std::string path;
    std::uint32_t size_bytes = 0;
    std::uint64_t hash64 = 0;

    bool operator==(const PackManifestFileEntry&) const = default;
};

// Host → client. The session's pack set is announced as pack_count messages
// with pack_index 0..pack_count-1; pack_count == 0 is the explicit "no packs
// needed" announcement (empty id/version/files). A manifest with
// pack_index == 0 starts a fresh announcement generation on the client.
struct PackManifestMessage {
    std::uint8_t pack_index = 0;
    std::uint8_t pack_count = 0;
    std::string pack_id;
    std::string version;
    std::vector<PackManifestFileEntry> files;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept
    {
        std::uint64_t total = 0;
        for (const PackManifestFileEntry& file : files)
            total += file.size_bytes;
        return total;
    }

    bool operator==(const PackManifestMessage&) const = default;
};

// Client → host: send me this pack's files.
struct PackRequestMessage {
    std::string pack_id;

    bool operator==(const PackRequestMessage&) const = default;
};

// Host → client: sequential slice of manifest file `file_index`, starting at
// byte `offset`. Payload capped at kPackFileChunkMaxBytes (32 KiB).
struct PackFileChunkMessage {
    std::string pack_id;
    std::uint32_t file_index = 0;
    std::uint32_t offset = 0;
    std::vector<std::uint8_t> data;

    bool operator==(const PackFileChunkMessage&) const = default;
};

// Host → client: every chunk of the pack has been sent; verify and mount.
struct PackTransferDoneMessage {
    std::string pack_id;

    bool operator==(const PackTransferDoneMessage&) const = default;
};

// --- Staged-lobby messages (protocol v13, #218) -----------------------------
// The lobby OWNER broadcasts its staged world to joiners after every
// completed restage: a StagedMatchSetup followed by a StagedMatchKeyframe,
// both stamped with the stage generation so a client can pair them (a
// keyframe whose generation differs from the last received setup's is
// dropped; restage races resolve to the newest pair). The inner bytes are
// COMPLETE existing wire messages — an InitialSetup and a keyframe
// SnapshotMessage respectively — so receivers reuse the existing
// deserializers verbatim and the payloads never fork from the launch wire.

// The wrapper payload is u32 generation + u32 inner length + inner bytes;
// the inner message may occupy at most the u16 transport cap minus those
// eight bytes. The staged serializers REFUSE (empty result) beyond this so
// an oversize staged world can never throw out of the owner's poll loop —
// the owner reports StageStatus::Failed instead.
inline constexpr std::size_t kMaxStagedInnerMessageBytes = 65527;

// Owner → client: the staged world's level identity/metadata (the fields a
// snapshot cannot carry). `setup_bytes` is a complete serialized
// InitialSetupMessage (deserialize_initial_setup_message applies).
struct StagedMatchSetupMessage {
    std::uint32_t stage_generation = 0;
    std::vector<std::uint8_t> setup_bytes;

    bool operator==(const StagedMatchSetupMessage&) const = default;
};

// Owner → client: the staged world itself. `snapshot_bytes` is a complete
// serialized keyframe SnapshotMessage of the dormant tick-0 staged world
// (deserialize_snapshot applies).
struct StagedMatchKeyframeMessage {
    std::uint32_t stage_generation = 0;
    std::vector<std::uint8_t> snapshot_bytes;

    bool operator==(const StagedMatchKeyframeMessage&) const = default;
};

struct ReceivedMessage {
    PeerId peer_id = 0;
    std::vector<std::uint8_t> data;
};

enum class TypedReceivedMessageKind : std::uint8_t {
    Snapshot,
    DeltaSnapshot,
    Input,
    SimEventBatch,
    GameFlowEventBatch,
    LobbyMessage,
    LobbyState,
    InitialSetup,
    Hello,
    ClientReady,
    KeyframeRequest,
    Heartbeat,
    ExitPromptBroadcast,
    ExitPromptResponse,
    PauseBroadcast,
    PauseResponse,
    ControlChange,
    SnapshotHashCheck,
    PackManifest,
    PackRequest,
    PackFileChunk,
    PackTransferDone,
    StagedMatchSetup,
    StagedMatchKeyframe,
    Malformed,
};

struct TypedReceivedMessage {
    PeerId peer_id = 0;
    TypedReceivedMessageKind kind = TypedReceivedMessageKind::Snapshot;
    std::shared_ptr<WorldSnapshot> snapshot;
    std::shared_ptr<InputState> input;
    std::shared_ptr<SimEventBatch> event_batch;
    std::shared_ptr<LobbyMessage> lobby_message;
    std::shared_ptr<LobbyState> lobby_state;
    std::shared_ptr<InitialSetupMessage> initial_setup;
    std::shared_ptr<HelloMessage> hello;
    std::shared_ptr<ClientReadyMessage> client_ready;
    std::shared_ptr<KeyframeRequestMessage> keyframe_request;
    std::shared_ptr<HeartbeatMessage> heartbeat;
    std::shared_ptr<ExitPromptBroadcastMessage> exit_prompt_broadcast;
    std::shared_ptr<ExitPromptResponseMessage> exit_prompt_response;
    std::shared_ptr<PauseBroadcastMessage> pause_broadcast;
    std::shared_ptr<PauseResponseMessage> pause_response;
    std::shared_ptr<ControlChangeMessage> control_change;
    std::shared_ptr<SnapshotHashCheckMessage> snapshot_hash_check;
    std::shared_ptr<PackManifestMessage> pack_manifest;
    std::shared_ptr<PackRequestMessage> pack_request;
    std::shared_ptr<PackFileChunkMessage> pack_file_chunk;
    std::shared_ptr<PackTransferDoneMessage> pack_transfer_done;
    std::shared_ptr<StagedMatchSetupMessage> staged_match_setup;
    std::shared_ptr<StagedMatchKeyframeMessage> staged_match_keyframe;
    std::uint32_t tick = 0;
};

std::vector<std::uint8_t> serialize_initial_setup_message(
    const InitialSetupMessage& message);
std::optional<InitialSetupMessage> deserialize_initial_setup_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_lobby_message(const LobbyMessage& message);
std::optional<LobbyMessage> deserialize_lobby_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_lobby_state_message(
    const LobbyState& state);
std::optional<LobbyState> deserialize_lobby_state_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_client_ready_message(
    const ClientReadyMessage& message);
std::optional<ClientReadyMessage> deserialize_client_ready_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_heartbeat_message(
    const HeartbeatMessage& message);
std::optional<HeartbeatMessage> deserialize_heartbeat_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_keyframe_request_message(
    const KeyframeRequestMessage& message);
std::optional<KeyframeRequestMessage> deserialize_keyframe_request_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_exit_prompt_broadcast_message(
    const ExitPromptBroadcastMessage& message);
std::optional<ExitPromptBroadcastMessage>
deserialize_exit_prompt_broadcast_message(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_exit_prompt_response_message(
    const ExitPromptResponseMessage& message);
std::optional<ExitPromptResponseMessage>
deserialize_exit_prompt_response_message(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_pause_broadcast_message(
    const PauseBroadcastMessage& message);
std::optional<PauseBroadcastMessage> deserialize_pause_broadcast_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_pause_response_message(
    const PauseResponseMessage& message);
std::optional<PauseResponseMessage> deserialize_pause_response_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_control_change_message(
    const ControlChangeMessage& message);
std::optional<ControlChangeMessage> deserialize_control_change_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_snapshot_hash_check_message(
    const SnapshotHashCheckMessage& message);
std::optional<SnapshotHashCheckMessage> deserialize_snapshot_hash_check_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_pack_manifest_message(
    const PackManifestMessage& message);
std::optional<PackManifestMessage> deserialize_pack_manifest_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_pack_request_message(
    const PackRequestMessage& message);
std::optional<PackRequestMessage> deserialize_pack_request_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_pack_file_chunk_message(
    const PackFileChunkMessage& message);
std::optional<PackFileChunkMessage> deserialize_pack_file_chunk_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_pack_transfer_done_message(
    const PackTransferDoneMessage& message);
std::optional<PackTransferDoneMessage> deserialize_pack_transfer_done_message(
    std::span<const std::uint8_t> bytes);
// Staged serializers refuse oversize (empty result — see
// kMaxStagedInnerMessageBytes) instead of throwing.
std::vector<std::uint8_t> serialize_staged_match_setup_message(
    const StagedMatchSetupMessage& message);
std::optional<StagedMatchSetupMessage> deserialize_staged_match_setup_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_staged_match_keyframe_message(
    const StagedMatchKeyframeMessage& message);
std::optional<StagedMatchKeyframeMessage>
deserialize_staged_match_keyframe_message(std::span<const std::uint8_t> bytes);
TypedReceivedMessage decode_received_message(const ReceivedMessage& message);

// Coarse health of a transport's upstream link, for client-side status UI.
// Implementations derive it on the poll() thread from their existing
// connect/disconnect bookkeeping; transports without a single upstream socket
// (in-process pairs, server listeners) simply report Connected.
enum class TransportLinkState : std::uint8_t {
    Connecting, // link started (or not yet started), never connected so far
    Connected,  // link is up
    Failed,     // link closed or errored before ever connecting
    Lost,       // link closed after having been connected
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual void send(PeerId peer_id,
                      const std::uint8_t* data,
                      std::size_t len) = 0;
    virtual void broadcast(const std::uint8_t* data, std::size_t len);

    void send(PeerId peer_id, std::span<const std::uint8_t> bytes)
    {
        send(peer_id, bytes.data(), bytes.size());
    }

    void broadcast(std::span<const std::uint8_t> bytes)
    {
        broadcast(bytes.data(), bytes.size());
    }

    [[nodiscard]] virtual bool supports_typed_messages() const noexcept;
    virtual void send_snapshot(PeerId peer_id,
                               std::shared_ptr<WorldSnapshot> snapshot);
    virtual void send_delta_snapshot(PeerId peer_id,
                                     std::shared_ptr<WorldSnapshot> snapshot);
    virtual void send_input(PeerId peer_id,
                            std::shared_ptr<InputState> input,
                            std::uint32_t tick);
    void send_input(PeerId peer_id, std::shared_ptr<InputState> input)
    {
        send_input(peer_id, std::move(input), 0);
    }
    virtual void send_sim_event_batch(PeerId peer_id,
                                      std::shared_ptr<SimEventBatch> batch);
    virtual void send_game_flow_event_batch(PeerId peer_id,
                                            std::shared_ptr<SimEventBatch> batch);
    virtual void send_lobby_message(PeerId peer_id,
                                    std::shared_ptr<LobbyMessage> message);
    virtual void send_lobby_state(PeerId peer_id,
                                  std::shared_ptr<LobbyState> state);
    virtual void send_initial_setup(PeerId peer_id,
                                    std::shared_ptr<InitialSetupMessage> message);
    virtual void send_hello(PeerId peer_id,
                            std::shared_ptr<HelloMessage> message);
    virtual void send_client_ready(PeerId peer_id,
                                   std::shared_ptr<ClientReadyMessage> message);
    virtual void send_keyframe_request(
        PeerId peer_id,
        std::shared_ptr<KeyframeRequestMessage> message);
    virtual void send_heartbeat(
        PeerId peer_id,
        std::shared_ptr<HeartbeatMessage> message);
    virtual void send_exit_prompt_broadcast(
        PeerId peer_id,
        std::shared_ptr<ExitPromptBroadcastMessage> message);
    virtual void send_exit_prompt_response(
        PeerId peer_id,
        std::shared_ptr<ExitPromptResponseMessage> message);
    virtual void send_pause_broadcast(
        PeerId peer_id,
        std::shared_ptr<PauseBroadcastMessage> message);
    virtual void send_pause_response(
        PeerId peer_id,
        std::shared_ptr<PauseResponseMessage> message);
    virtual void send_control_change(PeerId peer_id,
                                     std::shared_ptr<ControlChangeMessage> message);
    virtual void send_snapshot_hash_check(
        PeerId peer_id,
        std::shared_ptr<SnapshotHashCheckMessage> message);
    // Pack-transfer sends (v10) intentionally have no typed fast path: they
    // always serialize and go through send(), so every transport (including
    // the in-process pair) exercises the real wire encoding.
    virtual void send_pack_manifest(PeerId peer_id,
                                    std::shared_ptr<PackManifestMessage> message);
    virtual void send_pack_request(PeerId peer_id,
                                   std::shared_ptr<PackRequestMessage> message);
    virtual void send_pack_file_chunk(
        PeerId peer_id,
        std::shared_ptr<PackFileChunkMessage> message);
    virtual void send_pack_transfer_done(
        PeerId peer_id,
        std::shared_ptr<PackTransferDoneMessage> message);
    // Staged-lobby sends (v13): like the pack-transfer sends, deliberately no
    // typed fast path — they always serialize and go through send(), so every
    // transport (including the in-process pair) exercises the real wire
    // encoding. An oversize inner message serializes to nothing and is
    // silently skipped here; the OWNER detects that shape before sending
    // (stage status Failed), never mid-broadcast.
    virtual void send_staged_match_setup(
        PeerId peer_id,
        std::shared_ptr<StagedMatchSetupMessage> message);
    virtual void send_staged_match_keyframe(
        PeerId peer_id,
        std::shared_ptr<StagedMatchKeyframeMessage> message);

    [[nodiscard]] virtual std::vector<ReceivedMessage> poll() = 0;
    [[nodiscard]] virtual std::vector<TypedReceivedMessage> poll_typed();
    virtual void accept_connections() = 0;
    virtual void disconnect(PeerId peer_id) = 0;
    [[nodiscard]] virtual std::vector<PeerId> connected_peers() const = 0;

    // Must be queried from the poll() thread; see TransportLinkState.
    [[nodiscard]] virtual TransportLinkState link_state() const noexcept
    {
        return TransportLinkState::Connected;
    }
};

struct TransportEnvelope {
    std::uint8_t protocol_version = kNetworkProtocolVersion;
    std::uint8_t message_type = 0;
    std::uint16_t payload_length = 0;
};

inline void append_transport_header(std::vector<std::uint8_t>& bytes,
                                    std::uint8_t message_type,
                                    std::uint16_t payload_length)
{
    bytes.push_back(kNetworkProtocolVersion);
    bytes.push_back(message_type);
    bytes.push_back(static_cast<std::uint8_t>(payload_length & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((payload_length >> 8) & 0xffu));
}

template <std::size_t N>
inline void write_transport_header(std::array<std::uint8_t, N>& bytes,
                                   std::uint8_t message_type,
                                   std::uint16_t payload_length)
{
    static_assert(N >= kTransportHeaderSize);
    bytes[0] = kNetworkProtocolVersion;
    bytes[1] = message_type;
    bytes[2] = static_cast<std::uint8_t>(payload_length & 0xffu);
    bytes[3] = static_cast<std::uint8_t>((payload_length >> 8) & 0xffu);
}

inline bool decode_transport_envelope(std::span<const std::uint8_t> bytes,
                                      TransportEnvelope& envelope) noexcept
{
    if (bytes.size() < kTransportHeaderSize ||
        bytes[0] != kNetworkProtocolVersion)
    {
        return false;
    }

    envelope.protocol_version = bytes[0];
    envelope.message_type = bytes[1];
    const std::uint16_t payload_lo = static_cast<std::uint16_t>(bytes[2]);
    const std::uint16_t payload_hi =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[3]) << 8);
    envelope.payload_length =
        static_cast<std::uint16_t>(payload_lo | payload_hi);
    return true;
}

inline std::array<std::uint8_t, kSerializedHelloMessageSize>
serialize_hello(const HelloMessage& message)
{
    std::array<std::uint8_t, kSerializedHelloMessageSize> bytes{};
    write_transport_header(bytes, kHelloMessageType,
                           static_cast<std::uint16_t>(kSerializedHelloPayloadSize));

    bytes[kTransportHeaderSize + 0] = message.protocol_version;
    bytes[kTransportHeaderSize + 1] = message.min_protocol_version;
    bytes[kTransportHeaderSize + 2] = message.snapshot_format_version;

    for (std::size_t i = 0; i < kSessionTokenSize; ++i)
    {
        bytes[kTransportHeaderSize + 3 + i] = message.session_token[i];
    }

    const std::size_t hash_offset = kTransportHeaderSize + 3 + kSessionTokenSize;
    bytes[hash_offset + 0] =
        static_cast<std::uint8_t>(message.campaign_content_hash & 0xffu);
    bytes[hash_offset + 1] = static_cast<std::uint8_t>(
        (message.campaign_content_hash >> 8) & 0xffu);
    bytes[hash_offset + 2] = static_cast<std::uint8_t>(
        (message.campaign_content_hash >> 16) & 0xffu);
    bytes[hash_offset + 3] = static_cast<std::uint8_t>(
        (message.campaign_content_hash >> 24) & 0xffu);

    return bytes;
}

inline std::optional<HelloMessage> deserialize_hello_message(
    std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != kSerializedHelloMessageSize)
    {
        return std::nullopt;
    }

    TransportEnvelope envelope;
    if (!decode_transport_envelope(bytes, envelope) ||
        envelope.message_type != kHelloMessageType ||
        envelope.payload_length != kSerializedHelloPayloadSize)
    {
        return std::nullopt;
    }

    HelloMessage message;
    message.protocol_version = bytes[kTransportHeaderSize + 0];
    message.min_protocol_version = bytes[kTransportHeaderSize + 1];
    message.snapshot_format_version = bytes[kTransportHeaderSize + 2];

    if (message.protocol_version != envelope.protocol_version ||
        message.min_protocol_version > message.protocol_version)
    {
        return std::nullopt;
    }

    for (std::size_t i = 0; i < kSessionTokenSize; ++i)
    {
        message.session_token[i] = bytes[kTransportHeaderSize + 3 + i];
    }

    const std::size_t hash_offset = kTransportHeaderSize + 3 + kSessionTokenSize;
    message.campaign_content_hash =
        static_cast<std::uint32_t>(bytes[hash_offset + 0]) |
        (static_cast<std::uint32_t>(bytes[hash_offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[hash_offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[hash_offset + 3]) << 24);
    return message;
}

} // namespace og::sim
