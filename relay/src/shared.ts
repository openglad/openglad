import type { RoomInfo, StoredRoomState } from "./types";

export const ROOM_INDEX_PREFIX = "room:";
export const ROOM_INDEX_TTL_SECONDS = 60 * 60;
export const EMPTY_ROOM_GRACE_MS = 30_000;
export const MAX_ROOM_PEERS = 4;
export const MAX_RELAY_PAYLOAD_BYTES = 64 * 1024;

export const RELAY_TARGET_TAG = 1;
export const RELAY_FROM_PEER_TAG = 2;
export const RELAY_BROADCAST_TAG = 3;

const ROOM_CODE_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const ROOM_CODE_PATTERN = /^GLAD-[A-Z0-9]{4}$/;

export function roomIndexKey(code: string): string {
  return `${ROOM_INDEX_PREFIX}${code}`;
}

export function normalizeRoomCode(code: string): string {
  return code.trim().toUpperCase();
}

export function isValidRoomCode(code: string): boolean {
  return ROOM_CODE_PATTERN.test(code);
}

export function generateRoomCode(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(4));
  let suffix = "";
  for (const value of bytes) {
    suffix += ROOM_CODE_ALPHABET[value % ROOM_CODE_ALPHABET.length];
  }
  return `GLAD-${suffix}`;
}

export function makeRoomInfo(init: {
  code: string;
  campaign_hash?: string;
  campaign_name?: string;
  host_name?: string;
  player_count?: number;
  created_at?: number;
}): RoomInfo {
  const campaignHash = init.campaign_hash ?? "";
  return {
    code: init.code,
    campaign_hash: campaignHash,
    campaign_name: init.campaign_name ?? campaignHash,
    host_name: init.host_name ?? "",
    player_count: init.player_count ?? 0,
    created_at: init.created_at ?? Date.now(),
  };
}

export function makeStoredRoomState(room: RoomInfo): StoredRoomState {
  return {
    code: room.code,
    campaign_hash: room.campaign_hash,
    campaign_name: room.campaign_name,
    host_name: room.host_name,
    created_at: room.created_at,
    next_peer_id: 1,
    host_peer_id: null,
  };
}

export function roomInfoFromStoredState(
  state: StoredRoomState,
  playerCount: number,
): RoomInfo {
  return {
    code: state.code,
    campaign_hash: state.campaign_hash,
    campaign_name: state.campaign_name,
    host_name: state.host_name,
    player_count: playerCount,
    created_at: state.created_at,
  };
}

export function jsonResponse(
  data: unknown,
  init: ResponseInit = {},
): Response {
  const headers = new Headers(init.headers);
  headers.set("content-type", "application/json; charset=utf-8");
  applyCorsHeaders(headers);
  return new Response(JSON.stringify(data), {
    ...init,
    headers,
  });
}

export function textResponse(
  body: string,
  init: ResponseInit = {},
): Response {
  const headers = new Headers(init.headers);
  applyCorsHeaders(headers);
  return new Response(body, {
    ...init,
    headers,
  });
}

export function emptyResponse(status = 204, headers?: HeadersInit): Response {
  const responseHeaders = new Headers(headers);
  applyCorsHeaders(responseHeaders);
  return new Response(null, {
    status,
    headers: responseHeaders,
  });
}

export function applyCorsHeaders(headers: Headers): void {
  headers.set("access-control-allow-origin", "*");
  headers.set("access-control-allow-methods", "GET, POST, OPTIONS");
  headers.set("access-control-allow-headers", "content-type");
}

export function parseRoomInfo(raw: string | null): RoomInfo | null {
  if (!raw) {
    return null;
  }

  try {
    const parsed = JSON.parse(raw) as Partial<RoomInfo>;
    if (typeof parsed.code !== "string" || !isValidRoomCode(parsed.code)) {
      return null;
    }

    return makeRoomInfo({
      code: parsed.code,
      campaign_hash:
        typeof parsed.campaign_hash === "string" ? parsed.campaign_hash : "",
      campaign_name:
        typeof parsed.campaign_name === "string" ? parsed.campaign_name : "",
      host_name: typeof parsed.host_name === "string" ? parsed.host_name : "",
      player_count:
        typeof parsed.player_count === "number" ? parsed.player_count : 0,
      created_at:
        typeof parsed.created_at === "number" ? parsed.created_at : Date.now(),
    });
  } catch {
    return null;
  }
}

export function readPeerId(bytes: Uint8Array, offset: number): number {
  return (
    bytes[offset] |
    (bytes[offset + 1] << 8) |
    (bytes[offset + 2] << 16) |
    (bytes[offset + 3] << 24)
  ) >>> 0;
}

export function writePeerId(
  bytes: Uint8Array,
  offset: number,
  peerId: number,
): void {
  bytes[offset] = peerId & 0xff;
  bytes[offset + 1] = (peerId >>> 8) & 0xff;
  bytes[offset + 2] = (peerId >>> 16) & 0xff;
  bytes[offset + 3] = (peerId >>> 24) & 0xff;
}

export function makeRelayFrame(
  fromPeerId: number,
  payload: Uint8Array,
): ArrayBuffer {
  const frame = new Uint8Array(5 + payload.byteLength);
  frame[0] = RELAY_FROM_PEER_TAG;
  writePeerId(frame, 1, fromPeerId);
  frame.set(payload, 5);
  return frame.buffer;
}
