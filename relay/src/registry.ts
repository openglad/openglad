import { DurableObject } from "cloudflare:workers";

import {
  CREATE_RATE_LIMIT_MAX_ROOMS,
  CREATE_RATE_LIMIT_WINDOW_MS,
  REGISTRY_ENTRY_TTL_MS,
  REGISTRY_ROOM_KEY_PREFIX,
  registryRoomKey,
} from "./shared";
import type { Env, RegistryEntry, RoomInfo } from "./types";

/** Bound on registry keys resolved per /internal/list call, so a public
 *  unauthenticated GET /api/rooms can never fan out unboundedly. */
const LIST_ROOMS_MAX_KEYS = 200;
/** Bound on distinct IPs tracked by the in-memory create rate limiter. */
const RATE_LIMIT_MAX_TRACKED_IPS = 4_096;

interface RateBudget {
  count: number;
  windowStartedAt: number;
}

/**
 * Single-instance durable object (idFromName("global")) that owns:
 *
 *  - the room directory for GET /api/rooms. Room durable objects upsert their
 *    entry on every membership change and on a periodic heartbeat while
 *    occupied; entries not refreshed within REGISTRY_ENTRY_TTL_MS are pruned
 *    on read, so a room DO that vanished without deregistering self-heals out
 *    of the listing. Entries persist in DO storage, so a registry restart
 *    loses nothing.
 *  - the per-IP room-create rate limit. Counters are in-memory only (a light
 *    limit; an eviction resetting the window is acceptable) and, because a
 *    single DO instance is single-threaded, the check is atomic — unlike the
 *    KV read-modify-write it replaces.
 */
export class RoomRegistry extends DurableObject {
  private readonly createBudgets = new Map<string, RateBudget>();

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
  }

  override async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/internal/create-limit" && request.method === "POST") {
      return this.checkCreateLimit(request);
    }
    if (url.pathname === "/internal/upsert" && request.method === "POST") {
      return this.upsertRoom(request);
    }
    if (url.pathname === "/internal/remove" && request.method === "POST") {
      return this.removeRoom(request);
    }
    if (url.pathname === "/internal/list" && request.method === "GET") {
      return this.listRooms(url.searchParams.get("campaign"));
    }

    return new Response("Not found", { status: 404 });
  }

  private async checkCreateLimit(request: Request): Promise<Response> {
    let ip = "";
    try {
      const payload = (await request.json()) as { ip?: string };
      ip = typeof payload.ip === "string" ? payload.ip : "";
    } catch {
      return new Response("Malformed payload", { status: 400 });
    }
    if (!ip) {
      return new Response("Missing ip", { status: 400 });
    }

    const now = Date.now();
    if (this.createBudgets.size >= RATE_LIMIT_MAX_TRACKED_IPS) {
      this.pruneExpiredBudgets(now);
    }

    const budget = this.createBudgets.get(ip);
    if (!budget || now - budget.windowStartedAt >= CREATE_RATE_LIMIT_WINDOW_MS) {
      this.createBudgets.set(ip, { count: 1, windowStartedAt: now });
      return new Response(null, { status: 204 });
    }
    if (budget.count >= CREATE_RATE_LIMIT_MAX_ROOMS) {
      return new Response("Rate limit exceeded", { status: 429 });
    }
    budget.count += 1;
    return new Response(null, { status: 204 });
  }

  private pruneExpiredBudgets(now: number): void {
    for (const [ip, budget] of this.createBudgets) {
      if (now - budget.windowStartedAt >= CREATE_RATE_LIMIT_WINDOW_MS) {
        this.createBudgets.delete(ip);
      }
    }
    // Under active abuse from >4k distinct IPs the map could still be full;
    // dropping the oldest entries keeps memory bounded at the price of
    // slightly loosening the limit for those IPs.
    while (this.createBudgets.size >= RATE_LIMIT_MAX_TRACKED_IPS) {
      const oldest = this.createBudgets.keys().next();
      if (oldest.done) {
        break;
      }
      this.createBudgets.delete(oldest.value);
    }
  }

  private async upsertRoom(request: Request): Promise<Response> {
    let entry: RegistryEntry;
    try {
      const payload = (await request.json()) as Partial<RoomInfo>;
      if (typeof payload.code !== "string" || payload.code.length === 0) {
        return new Response("Missing room code", { status: 400 });
      }
      entry = {
        code: payload.code,
        campaign_hash: typeof payload.campaign_hash === "string" ? payload.campaign_hash : "",
        campaign_name: typeof payload.campaign_name === "string" ? payload.campaign_name : "",
        host_name: typeof payload.host_name === "string" ? payload.host_name : "",
        player_count: typeof payload.player_count === "number" ? payload.player_count : 0,
        created_at: typeof payload.created_at === "number" ? payload.created_at : Date.now(),
        updated_at: Date.now(),
      };
    } catch {
      return new Response("Malformed payload", { status: 400 });
    }

    await this.ctx.storage.put(registryRoomKey(entry.code), entry);
    return new Response(null, { status: 204 });
  }

  private async removeRoom(request: Request): Promise<Response> {
    let code = "";
    try {
      const payload = (await request.json()) as { code?: string };
      code = typeof payload.code === "string" ? payload.code : "";
    } catch {
      return new Response("Malformed payload", { status: 400 });
    }
    if (!code) {
      return new Response("Missing room code", { status: 400 });
    }
    await this.ctx.storage.delete(registryRoomKey(code));
    return new Response(null, { status: 204 });
  }

  private async listRooms(campaignFilter: string | null): Promise<Response> {
    const now = Date.now();
    const stored = await this.ctx.storage.list<RegistryEntry>({
      prefix: REGISTRY_ROOM_KEY_PREFIX,
      limit: LIST_ROOMS_MAX_KEYS,
    });

    const rooms: RoomInfo[] = [];
    const staleKeys: string[] = [];
    for (const [key, entry] of stored) {
      if (
        typeof entry?.code !== "string" ||
        typeof entry.updated_at !== "number" ||
        now - entry.updated_at >= REGISTRY_ENTRY_TTL_MS
      ) {
        staleKeys.push(key);
        continue;
      }
      if (entry.player_count <= 0) {
        continue;
      }
      if (campaignFilter && entry.campaign_hash !== campaignFilter) {
        continue;
      }
      rooms.push({
        code: entry.code,
        campaign_hash: entry.campaign_hash,
        campaign_name: entry.campaign_name,
        host_name: entry.host_name,
        player_count: entry.player_count,
        created_at: entry.created_at,
      });
    }

    if (staleKeys.length > 0) {
      await this.ctx.storage.delete(staleKeys);
    }

    rooms.sort((left, right) => right.created_at - left.created_at);
    return Response.json(rooms);
  }
}
