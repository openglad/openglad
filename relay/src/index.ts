import { GameRoom } from "./game-room";
import { RoomRegistry } from "./registry";
import { SaveVault } from "./save-vault";
import {
  REGISTRY_INSTANCE_NAME,
  applyCorsHeaders,
  clientIpFromRequest,
  emptyResponse,
  generateOwnerToken,
  generateRoomCode,
  isValidCloudSaveKey,
  isValidRoomCode,
  jsonResponse,
  normalizeRoomCode,
  textResponse,
} from "./shared";
import type { Env } from "./types";

const ROOM_CODE_ALLOCATION_ATTEMPTS = 8;

function registryStub(env: Env): DurableObjectStub {
  return env.ROOM_REGISTRY.get(
    env.ROOM_REGISTRY.idFromName(REGISTRY_INSTANCE_NAME),
  );
}

/**
 * POST /api/create?campaign=<hash>&campaign_name=<name>&host=<host name>
 *   -> 200 {"room_code": "GLAD-XXXX", "code": "GLAD-XXXX", "owner_token": "..."}
 *
 * The C++ parser (parse_relay_room_create_response_impl) accepts either
 * "code" or "room_code"; both are provided. The reference stub uses
 * "room_code".
 */
async function createRoom(env: Env, request: Request): Promise<Response> {
  const url = new URL(request.url);
  const campaignHash = url.searchParams.get("campaign") ?? "";
  const campaignName = url.searchParams.get("campaign_name") ?? campaignHash;
  const hostName = url.searchParams.get("host") ?? "";

  const limitResponse = await registryStub(env).fetch(
    new Request("https://relay.internal/internal/create-limit", {
      method: "POST",
      headers: { "content-type": "application/json; charset=utf-8" },
      body: JSON.stringify({ ip: clientIpFromRequest(request) }),
    }),
  );
  if (limitResponse.status === 429) {
    return textResponse("Rate limit exceeded", { status: 429 });
  }
  if (!limitResponse.ok && limitResponse.status !== 204) {
    return textResponse("Room creation is unavailable", { status: 503 });
  }

  for (let attempt = 0; attempt < ROOM_CODE_ALLOCATION_ATTEMPTS; ++attempt) {
    const code = generateRoomCode();
    const ownerToken = generateOwnerToken();

    const room = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(code));
    const initResponse = await room.fetch(
      new Request("https://relay.internal/internal/initialize", {
        method: "POST",
        headers: { "content-type": "application/json; charset=utf-8" },
        body: JSON.stringify({
          code,
          campaign_hash: campaignHash,
          campaign_name: campaignName,
          host_name: hostName,
          owner_token: ownerToken,
        }),
      }),
    );
    if (initResponse.status === 409) {
      continue; // Code collision with a live room; try another code.
    }
    if (!initResponse.ok) {
      return textResponse("Room creation failed", { status: 502 });
    }

    // Seed the listing entry (player_count 0 keeps it hidden until the owner
    // connects; the room durable object takes over updates from here).
    await registryStub(env).fetch(
      new Request("https://relay.internal/internal/upsert", {
        method: "POST",
        headers: { "content-type": "application/json; charset=utf-8" },
        body: JSON.stringify({
          code,
          campaign_hash: campaignHash.trim().slice(0, 128),
          campaign_name: campaignName.trim().slice(0, 128),
          host_name: hostName.trim().slice(0, 64),
          player_count: 0,
          created_at: Date.now(),
        }),
      }),
    );

    return jsonResponse({
      room_code: code,
      code,
      owner_token: ownerToken,
    });
  }

  return textResponse("Unable to allocate a room code", { status: 503 });
}

/** GET /api/rooms[?campaign=<hash>] -> 200 JSON array of joinable rooms. */
async function listRooms(env: Env, request: Request): Promise<Response> {
  const url = new URL(request.url);
  const campaignFilter = url.searchParams.get("campaign");
  const listUrl = new URL("https://relay.internal/internal/list");
  if (campaignFilter) {
    listUrl.searchParams.set("campaign", campaignFilter);
  }

  const listResponse = await registryStub(env).fetch(new Request(listUrl));
  if (!listResponse.ok) {
    return textResponse("Room listing is unavailable", { status: 503 });
  }
  return jsonResponse(await listResponse.json());
}

/** WebSocket upgrade on /api/room/<CODE>[?owner_token=...]. Unknown rooms are
 *  refused with HTTP 404 (the browser then never fires the WS open event —
 *  same shape as relay_stub.js 'reject'). */
async function connectRoom(
  env: Env,
  request: Request,
  rawCode: string,
): Promise<Response> {
  if (request.method !== "GET") {
    return textResponse("Method not allowed", { status: 405 });
  }

  let decodedCode = rawCode;
  try {
    decodedCode = decodeURIComponent(rawCode);
  } catch {
    return textResponse("Invalid room code", { status: 400 });
  }
  const code = normalizeRoomCode(decodedCode);
  if (!isValidRoomCode(code)) {
    return textResponse("Invalid room code", { status: 400 });
  }

  const url = new URL(request.url);
  const ownerToken = url.searchParams.get("owner_token") ?? "";

  const room = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(code));
  const headers = new Headers(request.headers);
  headers.set("x-openglad-room-code", code);
  headers.set("x-openglad-connecting-owner-token", ownerToken);
  return room.fetch(new Request(request, { headers }));
}

/** Re-wrap a durable object response with the permissive CORS headers every
 *  public /api route carries (the DO responses are internal and carry none). */
function withCors(response: Response): Response {
  const headers = new Headers(response.headers);
  applyCorsHeaders(headers);
  return new Response(response.body, { status: response.status, headers });
}

/** Pre-parse cap for POST /api/save/<KEY>: the JSON body around a maximal
 *  131072-char data_hex stays well under this, so anything larger is
 *  refused before the JSON is even read. */
const MAX_CLOUD_SAVE_REQUEST_BYTES = 300 * 1024;

/**
 * GET/POST /api/save/<KEY> (issue #155): passphrase-keyed cloud saves. The
 * key is validated here, BEFORE any durable object is touched; uploads pass
 * the per-IP budget in the registry first (the createRoom pattern).
 */
async function handleCloudSave(
  env: Env,
  request: Request,
  rawKey: string,
): Promise<Response> {
  let decodedKey = rawKey;
  try {
    decodedKey = decodeURIComponent(rawKey);
  } catch {
    return textResponse("Invalid save key", { status: 400 });
  }
  if (!isValidCloudSaveKey(decodedKey)) {
    return textResponse("Invalid save key", { status: 400 });
  }

  if (request.method !== "GET" && request.method !== "POST") {
    return textResponse("Method not allowed", { status: 405 });
  }

  const vault = env.SAVE_VAULT.get(env.SAVE_VAULT.idFromName(decodedKey));

  if (request.method === "GET") {
    const response = await vault.fetch(
      new Request("https://relay.internal/internal/save"),
    );
    return withCors(response);
  }

  // POST: refuse oversized bodies before any parsing.
  const contentLength = Number(request.headers.get("content-length") ?? "0");
  if (Number.isFinite(contentLength) &&
      contentLength > MAX_CLOUD_SAVE_REQUEST_BYTES) {
    return textResponse("Save too large", { status: 413 });
  }

  const limitResponse = await registryStub(env).fetch(
    new Request("https://relay.internal/internal/save-limit", {
      method: "POST",
      headers: { "content-type": "application/json; charset=utf-8" },
      body: JSON.stringify({ ip: clientIpFromRequest(request) }),
    }),
  );
  if (limitResponse.status === 429) {
    return textResponse("Rate limit exceeded", { status: 429 });
  }
  if (!limitResponse.ok && limitResponse.status !== 204) {
    return textResponse("Cloud saves are unavailable", { status: 503 });
  }

  const body = await request.text();
  if (body.length > MAX_CLOUD_SAVE_REQUEST_BYTES) {
    return textResponse("Save too large", { status: 413 });
  }
  const response = await vault.fetch(
    new Request("https://relay.internal/internal/save", {
      method: "POST",
      headers: { "content-type": "application/json; charset=utf-8" },
      body,
    }),
  );
  return withCors(response);
}

export { GameRoom, RoomRegistry, SaveVault };

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (request.method === "OPTIONS" && url.pathname.startsWith("/api/")) {
      return emptyResponse();
    }

    if (url.pathname === "/api/create") {
      if (request.method !== "POST") {
        return textResponse("Method not allowed", { status: 405 });
      }
      return createRoom(env, request);
    }

    if (url.pathname === "/api/rooms") {
      if (request.method !== "GET") {
        return textResponse("Method not allowed", { status: 405 });
      }
      return listRooms(env, request);
    }

    if (url.pathname.startsWith("/api/room/")) {
      return connectRoom(env, request, url.pathname.slice("/api/room/".length));
    }

    if (url.pathname.startsWith("/api/save/")) {
      return handleCloudSave(
        env,
        request,
        url.pathname.slice("/api/save/".length),
      );
    }

    if (url.pathname === "/") {
      return textResponse("OpenGlad Relay");
    }

    return textResponse("Not found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;
