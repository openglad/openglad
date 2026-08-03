import type { RoomInfo } from "./types";

// ---------------------------------------------------------------------------
// Wire protocol constants. These MUST match the authoritative C++ decoders in
// src/platform/emscripten/net_transport_relay_ws.cpp and
// src/platform/sdl/net_transport_relay_ws.cpp (kRelay*Tag,
// kMaxInboundFrameBytes) and the reference stub tests/e2e/relay_stub.js.
// ---------------------------------------------------------------------------

/** client -> relay: [0x01][target peer u32 LE][body] */
export const RELAY_TARGET_TAG = 1;
/** relay -> client: [0x02][sender peer u32 LE][body] */
export const RELAY_FROM_PEER_TAG = 2;
/** client -> relay: [0x03][body] */
export const RELAY_BROADCAST_TAG = 3;
export const RELAY_PEER_HEADER_SIZE = 5;

/** The C++ transports close the link (1009) on any inbound frame larger than
 *  kMaxInboundFrameBytes = 128 KiB, so the relay both rejects larger frames
 *  from clients and guarantees it never forwards a larger frame to them. */
export const MAX_INBOUND_BINARY_FRAME_BYTES = 128 * 1024;
/** Broadcast frames grow by 4 bytes when re-framed as [0x02][sender u32][body]
 *  ([0x03][body] -> 1+4+body), so cap inbound broadcasts 4 bytes lower to keep
 *  the forwarded frame within MAX_INBOUND_BINARY_FRAME_BYTES. */
export const MAX_BROADCAST_FRAME_BYTES = MAX_INBOUND_BINARY_FRAME_BYTES - 4;
/** The game clients never send TEXT frames; anything longer than this is
 *  hostile and gets a 1009 close. (Relay -> client control messages must stay
 *  under the C++ kMaxInboundTextBytes = 8 KiB; ours are tiny.) */
export const MAX_INBOUND_TEXT_MESSAGE_BYTES = 4 * 1024;

// ---------------------------------------------------------------------------
// Limits and lifecycle tuning.
// ---------------------------------------------------------------------------

// Matches og::sim::kMaxGlobalPlayers: up to 16 seats spread across up to 16
// machines (each relay peer is one machine carrying 1..4 local seats).
export const MAX_ROOM_PEERS = 16;
/** Room creations allowed per client IP per window. */
export const CREATE_RATE_LIMIT_MAX_ROOMS = 10;
export const CREATE_RATE_LIMIT_WINDOW_MS = 60_000;
/** Per-connection message budget (DoS guard, far above game traffic: the host
 *  relays per-peer snapshots every tick). Closes with 1008 when exceeded. */
export const MESSAGE_RATE_LIMIT_MAX_MESSAGES = 2_000;
export const MESSAGE_RATE_LIMIT_MAX_BYTES = 8 * 1024 * 1024;
export const MESSAGE_RATE_LIMIT_WINDOW_MS = 1_000;

export const MAX_CAMPAIGN_HASH_LENGTH = 128;
export const MAX_CAMPAIGN_NAME_LENGTH = 128;
export const MAX_HOST_NAME_LENGTH = 64;

/** How long an empty room (no connected sockets) survives before the durable
 *  object alarm deletes it. Also the create -> first-connect grace window. */
export const DEFAULT_EMPTY_ROOM_TTL_MS = 120_000;
/** A room whose owner never presented the owner token gets closed after this,
 *  even if non-owner joiners are keeping it non-empty. */
export const OWNER_CONNECT_GRACE_MS = 5 * 60_000;
/** Absolute cap on room lifetime, as a cost/leak backstop. */
export const ROOM_MAX_AGE_MS = 12 * 60 * 60 * 1000;
/** Occupied rooms refresh their registry entry at this cadence... */
export const REGISTRY_HEARTBEAT_MS = 5 * 60_000;
/** ...and the registry prunes entries not refreshed within this window, so a
 *  room durable object that vanished without deregistering self-heals out of
 *  the listing. */
export const REGISTRY_ENTRY_TTL_MS = 15 * 60_000;

export const REGISTRY_INSTANCE_NAME = "global";
export const REGISTRY_ROOM_KEY_PREFIX = "room:";

// ---------------------------------------------------------------------------
// Cloud saves (issue #155). One passphrase-derived key = one SaveVault
// durable object holding one hex-encoded GTL blob. The key is the lowercase
// 16-hex fnv1a64 the CLIENT derives; the server never sees the passphrase.
// ---------------------------------------------------------------------------

export const CLOUD_SAVE_KEY_PATTERN = /^[0-9a-f]{16}$/;

export function isValidCloudSaveKey(key: string): boolean {
  return CLOUD_SAVE_KEY_PATTERN.test(key);
}

/** Decoded blob cap. A maxed company is a few KB; 64 KiB is ~10x headroom
 *  and far under the SQLite-DO value limit. Enforced on the hex length. */
export const MAX_CLOUD_SAVE_BYTES = 64 * 1024;
export const MAX_CLOUD_SAVE_HEX_CHARS = MAX_CLOUD_SAVE_BYTES * 2;
export const MAX_CLOUD_SAVE_SLOT_LENGTH = 64;
export const MAX_CLOUD_SAVE_NAME_LENGTH = 64;
/** Inactivity TTL: any GET or successful POST refreshes it (the
 *  EMPTY_ROOM_TTL_MS alarm pattern; CLOUD_SAVE_TTL_MS env override). */
export const DEFAULT_CLOUD_SAVE_TTL_MS = 180 * 24 * 60 * 60 * 1000;
/** Uploads allowed per client IP per window (GETs are unlimited). */
export const SAVE_UPLOAD_RATE_LIMIT_MAX = 10;
export const SAVE_UPLOAD_RATE_LIMIT_WINDOW_MS = 60_000;

// ---------------------------------------------------------------------------
// Room codes. GLAD-XXXX with an alphabet that avoids the confusable
// characters 0/O, 1/I/L. Lookup still accepts the full A-Z0-9 range so a
// mistyped-but-plausible code fails with a clean 404 rather than a 400.
// ---------------------------------------------------------------------------

const ROOM_CODE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
const ROOM_CODE_PATTERN = /^GLAD-[A-Z0-9]{4}$/;

export function normalizeRoomCode(code: string): string {
  return code.trim().toUpperCase();
}

export function isValidRoomCode(code: string): boolean {
  return ROOM_CODE_PATTERN.test(code);
}

export function generateRoomCode(): string {
  let suffix = "";
  while (suffix.length < 4) {
    const bytes = crypto.getRandomValues(new Uint8Array(8));
    for (const value of bytes) {
      // Rejection sampling for an unbiased draw from the 31-char alphabet.
      if (value < 248 && suffix.length < 4) {
        suffix += ROOM_CODE_ALPHABET[value % ROOM_CODE_ALPHABET.length];
      }
    }
  }
  return `GLAD-${suffix}`;
}

export function generateOwnerToken(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  let token = "";
  for (const value of bytes) {
    token += value.toString(16).padStart(2, "0");
  }
  return token;
}

export function registryRoomKey(code: string): string {
  return `${REGISTRY_ROOM_KEY_PREFIX}${code}`;
}

// ---------------------------------------------------------------------------
// HTTP helpers. CORS is permissive exactly like tests/e2e/relay_stub.js: the
// game is served from a different origin (pages.dev / localhost / tunnel).
// ---------------------------------------------------------------------------

export function applyCorsHeaders(headers: Headers): void {
  headers.set("Access-Control-Allow-Origin", "*");
  headers.set("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  headers.set("Access-Control-Allow-Headers", "*");
}

export function jsonResponse(data: unknown, init: ResponseInit = {}): Response {
  const headers = new Headers(init.headers);
  headers.set("Content-Type", "application/json; charset=utf-8");
  applyCorsHeaders(headers);
  return new Response(JSON.stringify(data), { ...init, headers });
}

export function textResponse(body: string, init: ResponseInit = {}): Response {
  const headers = new Headers(init.headers);
  applyCorsHeaders(headers);
  return new Response(body, { ...init, headers });
}

export function emptyResponse(status = 204): Response {
  const headers = new Headers();
  applyCorsHeaders(headers);
  return new Response(null, { status, headers });
}

export function clientIpFromRequest(request: Request): string {
  // Set by the Cloudflare edge in production (client-supplied copies are
  // replaced there); spoofable only in local dev, which is fine for a light
  // rate limit.
  const cfConnectingIp = request.headers.get("cf-connecting-ip")?.trim();
  if (cfConnectingIp) {
    return cfConnectingIp;
  }
  const forwardedFor = request.headers.get("x-forwarded-for");
  if (forwardedFor) {
    const firstForwarded = forwardedFor.split(",")[0]?.trim();
    if (firstForwarded) {
      return firstForwarded;
    }
  }
  return "0.0.0.0";
}

function boundedString(value: string | undefined, maxLength: number): string {
  return (value ?? "").trim().slice(0, maxLength);
}

export function makeRoomInfo(init: {
  code: string;
  campaign_hash?: string;
  campaign_name?: string;
  host_name?: string;
  player_count?: number;
  created_at?: number;
}): RoomInfo {
  const campaignHash = boundedString(init.campaign_hash, MAX_CAMPAIGN_HASH_LENGTH);
  return {
    code: init.code,
    campaign_hash: campaignHash,
    campaign_name: boundedString(
      init.campaign_name ?? campaignHash,
      MAX_CAMPAIGN_NAME_LENGTH,
    ),
    host_name: boundedString(init.host_name, MAX_HOST_NAME_LENGTH),
    player_count: init.player_count ?? 0,
    created_at: init.created_at ?? Date.now(),
  };
}

// ---------------------------------------------------------------------------
// Binary frame helpers.
// ---------------------------------------------------------------------------

export function readPeerId(bytes: Uint8Array, offset: number): number {
  return (
    (bytes[offset] |
      (bytes[offset + 1] << 8) |
      (bytes[offset + 2] << 16) |
      (bytes[offset + 3] << 24)) >>>
    0
  );
}

export function writePeerId(bytes: Uint8Array, offset: number, peerId: number): void {
  bytes[offset] = peerId & 0xff;
  bytes[offset + 1] = (peerId >>> 8) & 0xff;
  bytes[offset + 2] = (peerId >>> 16) & 0xff;
  bytes[offset + 3] = (peerId >>> 24) & 0xff;
}

/** Build the relay->client frame [0x02][sender peer u32 LE][body]. */
export function makeRelayFrame(fromPeerId: number, payload: Uint8Array): ArrayBuffer {
  const frame = new Uint8Array(RELAY_PEER_HEADER_SIZE + payload.byteLength);
  frame[0] = RELAY_FROM_PEER_TAG;
  writePeerId(frame, 1, fromPeerId);
  frame.set(payload, RELAY_PEER_HEADER_SIZE);
  return frame.buffer;
}
