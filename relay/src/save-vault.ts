import { DurableObject } from "cloudflare:workers";

import {
  DEFAULT_CLOUD_SAVE_TTL_MS,
  MAX_CLOUD_SAVE_HEX_CHARS,
  MAX_CLOUD_SAVE_NAME_LENGTH,
  MAX_CLOUD_SAVE_SLOT_LENGTH,
} from "./shared";
import type { Env, StoredCloudSave } from "./types";

/** Single storage key: one vault instance holds exactly one save. */
const SAVE_STORAGE_KEY = "save";

const CLOUD_SAVE_HEX_PATTERN = /^[0-9a-f]+$/;

interface CloudSaveUploadPayload {
  expected_revision?: unknown;
  slot?: unknown;
  save_name?: unknown;
  scen_num?: unknown;
  last_played?: unknown;
  data_hex?: unknown;
}

function boundedMetaString(value: unknown, maxLength: number): string {
  return typeof value === "string" ? value.trim().slice(0, maxLength) : "";
}

/**
 * One passphrase-keyed cloud save (issue #155). One instance per derived key
 * via env.SAVE_VAULT.idFromName(key); the durable object is single-threaded,
 * so the read-compare-increment revision check is atomic per key.
 *
 * Internal routes (the worker router owns key validation):
 *   GET  /internal/save -> stored record (refreshes last_access) or 404
 *   POST /internal/save -> revision-checked store (body = client JSON)
 *
 * Retention: an inactivity alarm (the GameRoom EMPTY_ROOM_TTL_MS pattern)
 * deletes saves untouched for CLOUD_SAVE_TTL_MS (default 180 days); every
 * GET and successful POST refreshes last_access and re-arms it.
 */
export class SaveVault extends DurableObject {
  private readonly appEnv: Env;

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.appEnv = env;
  }

  override async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    if (url.pathname !== "/internal/save") {
      return new Response("Not found", { status: 404 });
    }
    if (request.method === "GET") {
      return this.getSave();
    }
    if (request.method === "POST") {
      return this.storeSave(request);
    }
    return new Response("Method not allowed", { status: 405 });
  }

  override async alarm(): Promise<void> {
    const stored = await this.ctx.storage.get<StoredCloudSave>(SAVE_STORAGE_KEY);
    if (!stored) {
      await this.ctx.storage.deleteAlarm();
      return;
    }
    const now = Date.now();
    if (now - stored.last_access >= this.ttlMs()) {
      await this.ctx.storage.deleteAll();
      await this.ctx.storage.deleteAlarm();
      return;
    }
    // Accessed since the alarm was armed: re-arm at the fresh deadline (the
    // GameRoom pattern).
    await this.ctx.storage.setAlarm(stored.last_access + this.ttlMs());
  }

  // -------------------------------------------------------------------------

  private async getSave(): Promise<Response> {
    const stored = await this.ctx.storage.get<StoredCloudSave>(SAVE_STORAGE_KEY);
    if (!stored) {
      return new Response("No cloud save", { status: 404 });
    }
    await this.touch(stored);
    return Response.json({
      revision: stored.revision,
      uploaded_at: stored.uploaded_at,
      slot: stored.slot,
      save_name: stored.save_name,
      scen_num: stored.scen_num,
      last_played: stored.last_played,
      data_hex: stored.data_hex,
    });
  }

  private async storeSave(request: Request): Promise<Response> {
    let payload: CloudSaveUploadPayload;
    try {
      payload = (await request.json()) as CloudSaveUploadPayload;
    } catch {
      return new Response("Malformed cloud save payload", { status: 400 });
    }

    const expectedRevision = payload.expected_revision;
    if (
      typeof expectedRevision !== "number" ||
      !Number.isSafeInteger(expectedRevision) ||
      expectedRevision < 0
    ) {
      return new Response("Invalid expected_revision", { status: 400 });
    }

    const dataHex = payload.data_hex;
    if (typeof dataHex !== "string" || dataHex.length === 0) {
      return new Response("Missing data_hex", { status: 400 });
    }
    if (dataHex.length > MAX_CLOUD_SAVE_HEX_CHARS) {
      return new Response("Save too large", { status: 413 });
    }
    if (dataHex.length % 2 !== 0 || !CLOUD_SAVE_HEX_PATTERN.test(dataHex)) {
      return new Response("Invalid data_hex", { status: 400 });
    }

    // Metadata echo: bounded, never parsed. The CLIENT validates slot safety
    // on download; the server only bounds sizes.
    const slot = boundedMetaString(payload.slot, MAX_CLOUD_SAVE_SLOT_LENGTH);
    const saveName = boundedMetaString(
      payload.save_name,
      MAX_CLOUD_SAVE_NAME_LENGTH,
    );
    const rawScenNum = payload.scen_num;
    if (
      rawScenNum !== undefined &&
      (typeof rawScenNum !== "number" || !Number.isSafeInteger(rawScenNum))
    ) {
      return new Response("Invalid scen_num", { status: 400 });
    }
    const scenNum = Math.max(-32768, Math.min(32767, rawScenNum ?? 0));
    const rawLastPlayed = payload.last_played;
    if (
      rawLastPlayed !== undefined &&
      (typeof rawLastPlayed !== "number" ||
        !Number.isSafeInteger(rawLastPlayed) ||
        rawLastPlayed < 0)
    ) {
      return new Response("Invalid last_played", { status: 400 });
    }
    const lastPlayed = rawLastPlayed ?? 0;

    const stored = await this.ctx.storage.get<StoredCloudSave>(SAVE_STORAGE_KEY);
    const currentRevision = stored?.revision ?? 0;
    if (expectedRevision !== currentRevision) {
      // Conflict: current revision + the stored identity (metadata only, NO
      // data_hex) so the client can show its overwrite prompt.
      return Response.json(
        {
          revision: currentRevision,
          uploaded_at: stored?.uploaded_at ?? 0,
          slot: stored?.slot ?? "",
          save_name: stored?.save_name ?? "",
          scen_num: stored?.scen_num ?? 0,
          last_played: stored?.last_played ?? 0,
        },
        { status: 409 },
      );
    }

    const now = Date.now();
    const record: StoredCloudSave = {
      revision: currentRevision + 1,
      uploaded_at: now,
      last_access: now,
      slot,
      save_name: saveName,
      scen_num: scenNum,
      last_played: lastPlayed,
      data_hex: dataHex,
    };
    await this.ctx.storage.put(SAVE_STORAGE_KEY, record);
    await this.ctx.storage.setAlarm(now + this.ttlMs());
    return Response.json({ revision: record.revision });
  }

  private async touch(stored: StoredCloudSave): Promise<void> {
    stored.last_access = Date.now();
    await this.ctx.storage.put(SAVE_STORAGE_KEY, stored);
    await this.ctx.storage.setAlarm(stored.last_access + this.ttlMs());
  }

  private ttlMs(): number {
    const configured = Number(this.appEnv.CLOUD_SAVE_TTL_MS);
    if (Number.isFinite(configured) && configured > 0) {
      return configured;
    }
    return DEFAULT_CLOUD_SAVE_TTL_MS;
  }
}
