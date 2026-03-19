import { GameRoom } from "./game-room";
import {
  CREATE_RATE_LIMIT_MAX_ROOMS,
  CREATE_RATE_LIMIT_WINDOW_SECONDS,
  ROOM_INDEX_PREFIX,
  ROOM_INDEX_TTL_SECONDS,
  clientIpFromRequest,
  createRateLimitKey,
  emptyResponse,
  generateRoomCode,
  isValidRoomCode,
  jsonResponse,
  makeRoomInfo,
  normalizeRoomCode,
  parseRoomInfo,
  roomIndexKey,
  textResponse,
} from "./shared";
import type { Env, RoomInfo } from "./types";

interface CreateRateLimitState {
  count: number;
  window_started_at: number;
}

function parseCreateRateLimitState(raw: string | null): CreateRateLimitState | null {
  if (!raw) {
    return null;
  }

  try {
    const parsed = JSON.parse(raw) as Partial<CreateRateLimitState>;
    if (
      typeof parsed.count !== "number" ||
      !Number.isFinite(parsed.count) ||
      typeof parsed.window_started_at !== "number" ||
      !Number.isFinite(parsed.window_started_at)
    ) {
      return null;
    }

    return {
      count: parsed.count,
      window_started_at: parsed.window_started_at,
    };
  } catch {
    return null;
  }
}

async function enforceCreateRoomRateLimit(
  env: Env,
  request: Request,
): Promise<Response | null> {
  const clientIp = clientIpFromRequest(request);
  const key = createRateLimitKey(clientIp);
  const now = Date.now();
  const existing = parseCreateRateLimitState(await env.ROOM_INDEX.get(key));

  let count = existing?.count ?? 0;
  let windowStartedAt = existing?.window_started_at ?? now;
  if (now - windowStartedAt >= CREATE_RATE_LIMIT_WINDOW_SECONDS * 1_000) {
    count = 0;
    windowStartedAt = now;
  }

  if (count >= CREATE_RATE_LIMIT_MAX_ROOMS) {
    return textResponse("Rate limit exceeded", { status: 429 });
  }

  await env.ROOM_INDEX.put(
    key,
    JSON.stringify({
      count: count + 1,
      window_started_at: windowStartedAt,
    } satisfies CreateRateLimitState),
    {
      expirationTtl: CREATE_RATE_LIMIT_WINDOW_SECONDS,
    },
  );

  return null;
}

async function createRoom(env: Env, request: Request): Promise<Response> {
  const url = new URL(request.url);
  const campaignHash = url.searchParams.get("campaign") ?? "";
  const campaignName = url.searchParams.get("campaign_name") ?? campaignHash;
  const hostName = url.searchParams.get("host") ?? "";

  const rateLimitResponse = await enforceCreateRoomRateLimit(env, request);
  if (rateLimitResponse) {
    return rateLimitResponse;
  }

  for (let attempt = 0; attempt < 10; ++attempt) {
    const code = generateRoomCode();
    const key = roomIndexKey(code);
    if (await env.ROOM_INDEX.get(key)) {
      continue;
    }

    const roomInfo = makeRoomInfo({
      code,
      campaign_hash: campaignHash,
      campaign_name: campaignName,
      host_name: hostName,
      player_count: 0,
    });

    await env.ROOM_INDEX.put(key, JSON.stringify(roomInfo), {
      expirationTtl: ROOM_INDEX_TTL_SECONDS,
    });

    return jsonResponse({
      code,
      room: roomInfo,
    });
  }

  return textResponse("Unable to allocate a room code", {
    status: 503,
  });
}

async function listRooms(env: Env, request: Request): Promise<Response> {
  const url = new URL(request.url);
  const campaignFilter = url.searchParams.get("campaign");

  const listResult = await env.ROOM_INDEX.list({ prefix: ROOM_INDEX_PREFIX });
  const rooms = (
    await Promise.all(
      listResult.keys.map(async ({ name }) => {
        const parsed = parseRoomInfo(await env.ROOM_INDEX.get(name));
        if (!parsed) {
          return null;
        }
        if (parsed.player_count <= 0) {
          return null;
        }
        if (campaignFilter && parsed.campaign_hash !== campaignFilter) {
          return null;
        }
        return parsed;
      }),
    )
  )
    .filter((room): room is RoomInfo => room !== null)
    .sort((left, right) => right.created_at - left.created_at);

  return jsonResponse(rooms);
}

async function connectRoom(env: Env, request: Request, code: string): Promise<Response> {
  if (request.method !== "GET") {
    return textResponse("Method not allowed", { status: 405 });
  }
  const normalizedCode = normalizeRoomCode(code);
  if (!isValidRoomCode(normalizedCode)) {
    return textResponse("Invalid room code", { status: 400 });
  }

  const roomInfo = parseRoomInfo(
    await env.ROOM_INDEX.get(roomIndexKey(normalizedCode)),
  );
  if (!roomInfo) {
    return textResponse("Room not found", { status: 404 });
  }

  const id = env.GAME_ROOM.idFromName(normalizedCode);
  const room = env.GAME_ROOM.get(id);
  const headers = new Headers(request.headers);
  headers.set("x-openglad-room-code", roomInfo.code);
  headers.set("x-openglad-campaign-hash", roomInfo.campaign_hash);
  headers.set("x-openglad-campaign-name", roomInfo.campaign_name);
  headers.set("x-openglad-host-name", roomInfo.host_name);
  headers.set("x-openglad-created-at", roomInfo.created_at.toString());

  return room.fetch(new Request(request, { headers }));
}

export { GameRoom };

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

    return textResponse("OpenGlad Relay");
  },
} satisfies ExportedHandler<Env>;
