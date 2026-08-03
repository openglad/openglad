// Cloud-save vault tests (issue #155): GET/POST /api/save/<KEY> lifecycle,
// validation, revision conflicts, rate limiting, and the inactivity TTL
// alarm. Runs in workerd via @cloudflare/vitest-pool-workers like
// relay.test.ts.

import { env, runDurableObjectAlarm, runInDurableObject, SELF } from "cloudflare:test";
import { afterEach, describe, expect, it } from "vitest";

import {
  MAX_CLOUD_SAVE_HEX_CHARS,
  MAX_CLOUD_SAVE_NAME_LENGTH,
  MAX_CLOUD_SAVE_SLOT_LENGTH,
  REGISTRY_INSTANCE_NAME,
  SAVE_UPLOAD_RATE_LIMIT_MAX,
} from "../src/shared";
import type { StoredCloudSave } from "../src/types";

// Must match the CLOUD_SAVE_TTL_MS miniflare binding in vitest.config.mts.
const TEST_CLOUD_SAVE_TTL_MS = 60_000;

const VALID_KEY = "73270125791ba273"; // derive("correct horse battery")
const OTHER_KEY = "9a9bba29704ed608"; // derive("gladiator")

interface CloudSaveRecord {
  revision: number;
  uploaded_at: number;
  slot: string;
  save_name: string;
  scen_num: number;
  last_played: number;
  data_hex?: string;
}

function saveUrl(key: string): string {
  return `https://relay.test/api/save/${key}`;
}

function uploadBody(overrides: Record<string, unknown> = {}): string {
  return JSON.stringify({
    expected_revision: 0,
    slot: "save0",
    save_name: "The Iron Band",
    scen_num: 7,
    last_played: 1754190000,
    data_hex: "47544c0e00112233",
    ...overrides,
  });
}

async function postSave(
  key: string,
  body: string,
  clientIp = "192.0.2.77",
): Promise<Response> {
  return SELF.fetch(saveUrl(key), {
    method: "POST",
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cf-connecting-ip": clientIp,
    },
    body,
  });
}

function vaultStub(key: string): DurableObjectStub {
  return env.SAVE_VAULT.get(env.SAVE_VAULT.idFromName(key));
}

async function rewindLastAccess(key: string, byMs: number): Promise<void> {
  await runInDurableObject(vaultStub(key), async (_instance, state) => {
    const stored = await state.storage.get<StoredCloudSave>("save");
    if (!stored) {
      throw new Error("expected a stored save to rewind");
    }
    stored.last_access -= byMs;
    await state.storage.put("save", stored);
  });
}

afterEach(async () => {
  // Wipe both vaults and the registry's in-memory save budget so tests
  // cannot bleed into each other.
  for (const key of [VALID_KEY, OTHER_KEY]) {
    await runInDurableObject(vaultStub(key), async (_instance, state) => {
      await state.storage.deleteAll();
      await state.storage.deleteAlarm();
    });
  }
  const registry = env.ROOM_REGISTRY.get(
    env.ROOM_REGISTRY.idFromName(REGISTRY_INSTANCE_NAME),
  );
  await runInDurableObject(registry, async (instance, state) => {
    (instance as unknown as {
      saveBudgets: Map<string, unknown>;
      createBudgets: Map<string, unknown>;
    }).saveBudgets.clear();
    (instance as unknown as {
      createBudgets: Map<string, unknown>;
    }).createBudgets.clear();
    await state.storage.deleteAll();
  });
});

describe("Cloud save vault", () => {
  it("roundtrips create then GET with byte-identical hex and echoed meta", async () => {
    const before = Date.now();
    const createResponse = await postSave(VALID_KEY, uploadBody());
    expect(createResponse.status).toBe(200);
    expect(createResponse.headers.get("access-control-allow-origin")).toBe("*");
    const created = (await createResponse.json()) as CloudSaveRecord;
    expect(created.revision).toBe(1);

    const getResponse = await SELF.fetch(saveUrl(VALID_KEY));
    expect(getResponse.status).toBe(200);
    expect(getResponse.headers.get("access-control-allow-origin")).toBe("*");
    const fetched = (await getResponse.json()) as CloudSaveRecord;
    expect(fetched.revision).toBe(1);
    expect(fetched.data_hex).toBe("47544c0e00112233");
    expect(fetched.slot).toBe("save0");
    expect(fetched.save_name).toBe("The Iron Band");
    expect(fetched.scen_num).toBe(7);
    expect(fetched.last_played).toBe(1754190000);
    expect(fetched.uploaded_at).toBeGreaterThanOrEqual(before);
    expect(fetched.uploaded_at).toBeLessThanOrEqual(Date.now());
  });

  it("returns 404 for a key never written", async () => {
    const response = await SELF.fetch(saveUrl(OTHER_KEY));
    expect(response.status).toBe(404);
    expect(await response.text()).toBe("No cloud save");
  });

  it("rejects malformed keys with 400 on both GET and POST", async () => {
    const badKeys = [
      "73270125791ba27", // 15 hex
      "73270125791ba2734", // 17 hex
      "73270125791BA273", // uppercase
      "73270125791ba27g", // non-hex
      "%zz%zz%zz%zz%zz%zz%zz%zz", // undecodable junk
      "..%2f..%2fetc", // path-shaped junk
    ];
    for (const key of badKeys) {
      const getResponse = await SELF.fetch(saveUrl(key));
      expect(getResponse.status, `GET ${key}`).toBe(400);
      const postResponse = await postSave(key, uploadBody());
      expect(postResponse.status, `POST ${key}`).toBe(400);
    }
  });

  it("rejects malformed upload payloads with 400", async () => {
    const cases: Array<[string, string]> = [
      ["non-JSON", "this is not json"],
      ["missing data_hex", JSON.stringify({ expected_revision: 0 })],
      ["empty data_hex", uploadBody({ data_hex: "" })],
      ["odd-length hex", uploadBody({ data_hex: "47544" })],
      ["non-hex chars", uploadBody({ data_hex: "47544c0g" })],
      ["uppercase hex", uploadBody({ data_hex: "47544C0E" })],
      ["negative revision", uploadBody({ expected_revision: -1 })],
      ["fractional revision", uploadBody({ expected_revision: 0.5 })],
      ["string revision", uploadBody({ expected_revision: "0" })],
      ["fractional scen_num", uploadBody({ scen_num: 1.5 })],
      ["negative last_played", uploadBody({ last_played: -5 })],
    ];
    // Distinct IPs: the per-IP upload budget is spent BEFORE validation
    // (the createRoom ordering), so 11 rejects from one IP would trip 429.
    let ipSuffix = 100;
    for (const [label, body] of cases) {
      const response = await postSave(VALID_KEY, body, `192.0.2.${ipSuffix++}`);
      expect(response.status, label).toBe(400);
    }
    // Nothing was stored by any of the rejects.
    const getResponse = await SELF.fetch(saveUrl(VALID_KEY));
    expect(getResponse.status).toBe(404);
  });

  it("refuses an oversize blob with 413", async () => {
    const oversize = "ab".repeat(MAX_CLOUD_SAVE_HEX_CHARS / 2) + "cd";
    const response = await postSave(VALID_KEY, uploadBody({ data_hex: oversize }));
    expect(response.status).toBe(413);
    expect(await response.text()).toBe("Save too large");
  });

  it("runs the revision conflict protocol", async () => {
    const create = await postSave(VALID_KEY, uploadBody());
    expect(create.status).toBe(200);

    // Second create attempt (expected 0) conflicts with revision 1 and
    // returns the stored identity WITHOUT the blob.
    const conflict = await postSave(
      VALID_KEY,
      uploadBody({ save_name: "Someone Else" }),
    );
    expect(conflict.status).toBe(409);
    const conflictBody = (await conflict.json()) as CloudSaveRecord;
    expect(conflictBody.revision).toBe(1);
    expect(conflictBody.save_name).toBe("The Iron Band");
    expect(conflictBody.scen_num).toBe(7);
    expect(conflictBody.data_hex).toBeUndefined();

    // Retry with the server's revision: stored, revision increments.
    const retry = await postSave(
      VALID_KEY,
      uploadBody({
        expected_revision: 1,
        save_name: "Someone Else",
        data_hex: "deadbeef",
      }),
    );
    expect(retry.status).toBe(200);
    expect(((await retry.json()) as CloudSaveRecord).revision).toBe(2);

    const fetched = (await (await SELF.fetch(saveUrl(VALID_KEY))).json()) as
      CloudSaveRecord;
    expect(fetched.revision).toBe(2);
    expect(fetched.data_hex).toBe("deadbeef");
    expect(fetched.save_name).toBe("Someone Else");
  });

  it("rate limits uploads per IP but never GETs", async () => {
    for (let i = 0; i < SAVE_UPLOAD_RATE_LIMIT_MAX; ++i) {
      const response = await postSave(
        VALID_KEY,
        uploadBody({ expected_revision: i }),
        "192.0.2.50",
      );
      expect(response.status, `upload ${i + 1}`).toBe(200);
    }
    const over = await postSave(
      VALID_KEY,
      uploadBody({ expected_revision: SAVE_UPLOAD_RATE_LIMIT_MAX }),
      "192.0.2.50",
    );
    expect(over.status).toBe(429);
    expect(await over.text()).toBe("Rate limit exceeded");

    // A different IP still has budget.
    const otherIp = await postSave(OTHER_KEY, uploadBody(), "192.0.2.51");
    expect(otherIp.status).toBe(200);

    // GETs are not rate limited: far more than the upload budget succeeds.
    for (let i = 0; i < SAVE_UPLOAD_RATE_LIMIT_MAX + 5; ++i) {
      const response = await SELF.fetch(saveUrl(VALID_KEY), {
        headers: { "cf-connecting-ip": "192.0.2.50" },
      });
      expect(response.status, `get ${i + 1}`).toBe(200);
    }
  });

  it("expires an untouched save via the TTL alarm and re-arms on access", async () => {
    expect((await postSave(VALID_KEY, uploadBody())).status).toBe(200);

    // Fresh save: the alarm fires but the save survives (accessed recently)
    // and the alarm is re-armed.
    await expect(runDurableObjectAlarm(vaultStub(VALID_KEY))).resolves.toBe(true);
    expect((await SELF.fetch(saveUrl(VALID_KEY))).status).toBe(200);
    await runInDurableObject(vaultStub(VALID_KEY), async (_instance, state) => {
      expect(await state.storage.getAlarm()).not.toBeNull();
    });

    // Age it past the TTL: the alarm deletes it.
    await rewindLastAccess(VALID_KEY, TEST_CLOUD_SAVE_TTL_MS + 1_000);
    await expect(runDurableObjectAlarm(vaultStub(VALID_KEY))).resolves.toBe(true);
    expect((await SELF.fetch(saveUrl(VALID_KEY))).status).toBe(404);
  });

  it("handles OPTIONS preflight and refuses other methods", async () => {
    const preflight = await SELF.fetch(saveUrl(VALID_KEY), {
      method: "OPTIONS",
    });
    expect(preflight.status).toBe(204);
    expect(preflight.headers.get("access-control-allow-origin")).toBe("*");
    expect(preflight.headers.get("access-control-allow-methods")).toContain(
      "POST",
    );

    const del = await SELF.fetch(saveUrl(VALID_KEY), { method: "DELETE" });
    expect(del.status).toBe(405);
    const put = await SELF.fetch(saveUrl(VALID_KEY), { method: "PUT" });
    expect(put.status).toBe(405);
  });

  it("bounds oversized metadata to the documented limits", async () => {
    const response = await postSave(
      VALID_KEY,
      uploadBody({
        slot: "s".repeat(200),
        save_name: "n".repeat(200),
        scen_num: 999999,
      }),
    );
    expect(response.status).toBe(200);
    const fetched = (await (await SELF.fetch(saveUrl(VALID_KEY))).json()) as
      CloudSaveRecord;
    expect(fetched.slot).toBe("s".repeat(MAX_CLOUD_SAVE_SLOT_LENGTH));
    expect(fetched.save_name).toBe("n".repeat(MAX_CLOUD_SAVE_NAME_LENGTH));
    expect(fetched.scen_num).toBe(32767); // clamped to int16
  });

  it("refuses an over-cap request body before parsing", async () => {
    // 300 KiB of padding pushes the raw body over the router's pre-parse
    // cap even though data_hex itself is valid.
    const padded = JSON.stringify({
      expected_revision: 0,
      slot: "save0",
      save_name: "x",
      scen_num: 1,
      last_played: 1,
      data_hex: "47544c0e",
      padding: "p".repeat(320 * 1024),
    });
    const response = await postSave(VALID_KEY, padded);
    expect(response.status).toBe(413);
  });
});
