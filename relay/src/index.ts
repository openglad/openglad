import { GameRoom } from "./game-room";
import {
  CREATE_RATE_LIMIT_MAX_ROOMS,
  CREATE_RATE_LIMIT_WINDOW_SECONDS,
  ROOM_INDEX_PREFIX,
  ROOM_INDEX_TTL_SECONDS,
  clientIpFromRequest,
  createRateLimitKey,
  emptyResponse,
  generateOwnerToken,
  generateRoomCode,
  isValidRoomCode,
  jsonResponse,
  makeRoomInfo,
  normalizeRoomCode,
  parseRoomInfo,
  roomIndexKey,
  roomOwnerKey,
  textResponse,
} from "./shared";
import type { Env, RoomInfo } from "./types";

interface CreateRateLimitState {
  count: number;
  window_started_at: number;
}

interface InitializeRoomPayload {
  code: string;
  campaign_hash: string;
  campaign_name: string;
  host_name: string;
  created_at: number;
  owner_token: string;
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
    const ownerToken = generateOwnerToken();

    const roomInfo = makeRoomInfo({
      code,
      campaign_hash: campaignHash,
      campaign_name: campaignName,
      host_name: hostName,
      player_count: 0,
    });

    const initialized = await initializeRoomDurableObject(
      env,
      {
        code: roomInfo.code,
        campaign_hash: roomInfo.campaign_hash,
        campaign_name: roomInfo.campaign_name,
        host_name: roomInfo.host_name,
        created_at: roomInfo.created_at,
        owner_token: ownerToken,
      },
    );
    if (!initialized) {
      continue;
    }

    await env.ROOM_INDEX.put(key, JSON.stringify(roomInfo), {
      expirationTtl: ROOM_INDEX_TTL_SECONDS,
    });
    await env.ROOM_INDEX.put(roomOwnerKey(code), ownerToken, {
      expirationTtl: ROOM_INDEX_TTL_SECONDS,
    });

    return jsonResponse({
      code,
      owner_token: ownerToken,
      room: roomInfo,
    });
  }

  return textResponse("Unable to allocate a room code", {
    status: 503,
  });
}

async function initializeRoomDurableObject(
  env: Env,
  payload: InitializeRoomPayload,
): Promise<boolean> {
  const id = env.GAME_ROOM.idFromName(payload.code);
  const room = env.GAME_ROOM.get(id);
  const response = await room.fetch(
    new Request("https://relay.internal/internal/initialize", {
      method: "POST",
      headers: {
        "content-type": "application/json; charset=utf-8",
      },
      body: JSON.stringify(payload),
    }),
  );

  if (response.ok) {
    return true;
  }
  if (response.status === 409) {
    return false;
  }

  throw new Error(
    `Failed to initialize room ${payload.code} (${response.status})`,
  );
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
  const url = new URL(request.url);
  const normalizedCode = normalizeRoomCode(code);
  if (!isValidRoomCode(normalizedCode)) {
    return textResponse("Invalid room code", { status: 400 });
  }

  const requestOwnerToken = url.searchParams.get("owner_token") ?? "";

  const id = env.GAME_ROOM.idFromName(normalizedCode);
  const room = env.GAME_ROOM.get(id);
  const headers = new Headers(request.headers);
  headers.set("x-openglad-room-code", normalizedCode);
  headers.set("x-openglad-connecting-owner-token", requestOwnerToken);

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
