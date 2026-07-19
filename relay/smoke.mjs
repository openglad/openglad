#!/usr/bin/env node
// Smoke test for the OpenGlad relay worker against a real `wrangler dev`
// instance (adapting the approach of tests/e2e/relay_stub.js, but exercising
// the actual Worker + Durable Objects code in workerd).
//
// Usage:
//   node smoke.mjs                 # spawns `npx wrangler dev --local` itself
//   node smoke.mjs --url http://127.0.0.1:8787 [--skip-expiry]
//                                  # targets an already-running relay
//
// When self-spawning, the empty-room TTL is overridden to 4 seconds
// (--var EMPTY_ROOM_TTL_MS:4000) so the durable-object expiry alarm can be
// observed end-to-end. Requires Node >= 22 (built-in WebSocket client).

import { spawn } from "node:child_process";
import net from "node:net";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const RELAY_DIR = path.dirname(fileURLToPath(import.meta.url));
const SMOKE_EMPTY_ROOM_TTL_MS = 4_000;
const ROOM_CODE_PATTERN = /^GLAD-[2-9A-HJKMNP-Z]{4}$/;
const OWNER_TOKEN_PATTERN = /^[0-9a-f]{32}$/;

const args = process.argv.slice(2);
const urlFlagIndex = args.indexOf("--url");
const externalUrl = urlFlagIndex >= 0 ? args[urlFlagIndex + 1] : null;
const skipExpiry = args.includes("--skip-expiry");

let failures = 0;
function check(label, condition, detail = "") {
  if (condition) {
    console.log(`PASS ${label}`);
  } else {
    failures += 1;
    console.error(`FAIL ${label}${detail ? ` -- ${detail}` : ""}`);
  }
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function findFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const { port } = server.address();
      server.close(() => resolve(port));
    });
  });
}

/** Wraps the built-in WebSocket with queued receive + close observation. */
class SocketProbe {
  constructor(url) {
    this.messages = [];
    this.waiters = [];
    this.closeEvent = null;
    this.closeWaiters = [];
    this.openError = null;
    this.socket = new WebSocket(url);
    this.socket.binaryType = "arraybuffer";
    this.opened = new Promise((resolve) => {
      this.socket.addEventListener("open", () => resolve(true), { once: true });
      this.socket.addEventListener(
        "close",
        () => resolve(false),
        { once: true },
      );
      this.socket.addEventListener(
        "error",
        () => resolve(false),
        { once: true },
      );
    });
    this.socket.addEventListener("message", (event) => {
      const waiter = this.waiters.shift();
      if (waiter) {
        waiter(event.data);
      } else {
        this.messages.push(event.data);
      }
    });
    this.socket.addEventListener("close", (event) => {
      this.closeEvent = { code: event.code, reason: event.reason };
      for (const waiter of this.closeWaiters.splice(0)) {
        waiter(this.closeEvent);
      }
    });
    this.socket.addEventListener("error", () => {});
  }

  nextMessage(label, timeoutMs = 3_000) {
    if (this.messages.length > 0) {
      return Promise.resolve(this.messages.shift());
    }
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error(`timed out waiting for message: ${label}`));
      }, timeoutMs);
      this.waiters.push((data) => {
        clearTimeout(timeout);
        resolve(data);
      });
    });
  }

  async nextJson(label, timeoutMs = 3_000) {
    return JSON.parse(await this.nextMessage(label, timeoutMs));
  }

  waitForClose(label, timeoutMs = 3_000) {
    if (this.closeEvent) {
      return Promise.resolve(this.closeEvent);
    }
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error(`timed out waiting for close: ${label}`));
      }, timeoutMs);
      this.closeWaiters.push((event) => {
        clearTimeout(timeout);
        resolve(event);
      });
    });
  }

  send(data) {
    this.socket.send(data);
  }

  close() {
    try {
      this.socket.close(1000, "smoke done");
    } catch {
      // Already closed.
    }
  }
}

function targetedFrame(targetPeerId, body) {
  const frame = new Uint8Array(5 + body.length);
  frame[0] = 1;
  new DataView(frame.buffer).setUint32(1, targetPeerId, true);
  frame.set(body, 5);
  return frame.buffer;
}

function broadcastFrame(body) {
  const frame = new Uint8Array(1 + body.length);
  frame[0] = 3;
  frame.set(body, 1);
  return frame.buffer;
}

function frameBytes(data) {
  return [...new Uint8Array(data)];
}

function sameBytes(data, expected) {
  const actual = frameBytes(data);
  return (
    actual.length === expected.length &&
    actual.every((value, index) => value === expected[index])
  );
}

async function waitForRelayReady(baseUrl, deadlineMs) {
  const deadline = Date.now() + deadlineMs;
  let lastError = "no response";
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`${baseUrl}/`, {
        signal: AbortSignal.timeout(2_000),
      });
      if (response.ok && (await response.text()) === "OpenGlad Relay") {
        return;
      }
      lastError = `status ${response.status}`;
    } catch (error) {
      lastError = String(error?.message ?? error);
    }
    await sleep(500);
  }
  throw new Error(`relay never became ready: ${lastError}`);
}

async function createRoom(baseUrl, params = {}) {
  const url = new URL(`${baseUrl}/api/create`);
  for (const [key, value] of Object.entries(params)) {
    url.searchParams.set(key, value);
  }
  return fetch(url, { method: "POST" });
}

function wsUrl(baseUrl, code, ownerToken = "") {
  const base = baseUrl.replace(/^http/, "ws");
  const suffix = ownerToken
    ? `?owner_token=${encodeURIComponent(ownerToken)}`
    : "";
  return `${base}/api/room/${code}${suffix}`;
}

async function runSmoke(baseUrl) {
  // --- CORS preflight -----------------------------------------------------
  const preflight = await fetch(`${baseUrl}/api/create`, { method: "OPTIONS" });
  check(
    "OPTIONS preflight returns 204 with permissive CORS",
    preflight.status === 204 &&
      preflight.headers.get("access-control-allow-origin") === "*",
    `status=${preflight.status}`,
  );

  // --- Room creation ------------------------------------------------------
  const createResponse = await createRoom(baseUrl, {
    campaign: "smokehash",
    campaign_name: "Smoke Campaign",
    host: "Smokey",
  });
  check(
    "POST /api/create returns 200 with CORS",
    createResponse.status === 200 &&
      createResponse.headers.get("access-control-allow-origin") === "*",
    `status=${createResponse.status}`,
  );
  const created = await createResponse.json();
  check(
    "room_code is GLAD-XXXX without confusable characters",
    ROOM_CODE_PATTERN.test(created.room_code) && created.code === created.room_code,
    `room_code=${created.room_code}`,
  );
  check(
    "owner_token is a 32-char hex token",
    OWNER_TOKEN_PATTERN.test(created.owner_token),
    `owner_token=${created.owner_token}`,
  );

  // --- Listing hides empty rooms ------------------------------------------
  let rooms = await (await fetch(`${baseUrl}/api/rooms`)).json();
  check(
    "unoccupied room is not listed",
    Array.isArray(rooms) && !rooms.some((room) => room.code === created.room_code),
    JSON.stringify(rooms),
  );

  // --- Owner control handshake --------------------------------------------
  const owner = new SocketProbe(wsUrl(baseUrl, created.room_code, created.owner_token));
  check("owner websocket opens", await owner.opened);
  const ownerJoined = await owner.nextJson("owner joined");
  const ownerPeerList = await owner.nextJson("owner peer_list");
  check(
    "owner handshake: joined peer_id=1 host=1",
    ownerJoined.type === "joined" && ownerJoined.peer_id === 1 && ownerJoined.host === 1,
    JSON.stringify(ownerJoined),
  );
  check(
    "owner handshake: peer_list [1] host=1",
    ownerPeerList.type === "peer_list" &&
      JSON.stringify(ownerPeerList.peers) === "[1]" &&
      ownerPeerList.host === 1,
    JSON.stringify(ownerPeerList),
  );

  // --- Listing shows the occupied room ------------------------------------
  rooms = await (await fetch(`${baseUrl}/api/rooms?campaign=smokehash`)).json();
  const listed = rooms.find((room) => room.code === created.room_code);
  check(
    "occupied room is listed with player count and metadata",
    Boolean(listed) &&
      listed.player_count === 1 &&
      listed.campaign_hash === "smokehash" &&
      listed.campaign_name === "Smoke Campaign" &&
      listed.host_name === "Smokey" &&
      typeof listed.created_at === "number",
    JSON.stringify(rooms),
  );
  const filteredOut = await (
    await fetch(`${baseUrl}/api/rooms?campaign=some-other-campaign`)
  ).json();
  check(
    "campaign filter excludes the room",
    Array.isArray(filteredOut) &&
      !filteredOut.some((room) => room.code === created.room_code),
    JSON.stringify(filteredOut),
  );

  // --- Guest join ----------------------------------------------------------
  const guest = new SocketProbe(wsUrl(baseUrl, created.room_code));
  check("guest websocket opens", await guest.opened);
  const guestJoined = await guest.nextJson("guest joined");
  const guestPeerList = await guest.nextJson("guest peer_list");
  check(
    "guest handshake: joined peer_id=2 host=1",
    guestJoined.type === "joined" && guestJoined.peer_id === 2 && guestJoined.host === 1,
    JSON.stringify(guestJoined),
  );
  check(
    "guest handshake: peer_list [1,2] host=1",
    JSON.stringify(guestPeerList.peers) === "[1,2]" && guestPeerList.host === 1,
    JSON.stringify(guestPeerList),
  );
  const ownerSawGuest = await owner.nextJson("owner peer_joined");
  check(
    "owner notified: peer_joined peer_id=2 is_host=false",
    ownerSawGuest.type === "peer_joined" &&
      ownerSawGuest.peer_id === 2 &&
      ownerSawGuest.is_host === false,
    JSON.stringify(ownerSawGuest),
  );

  // --- Binary relay both directions ---------------------------------------
  guest.send(targetedFrame(1, [10, 20, 30]));
  const ownerFrame = await owner.nextMessage("owner targeted frame");
  check(
    "targeted frame guest->owner re-framed as [0x02][sender=2][body]",
    sameBytes(ownerFrame, [2, 2, 0, 0, 0, 10, 20, 30]),
    frameBytes(ownerFrame).join(","),
  );
  owner.send(broadcastFrame([40, 50]));
  const guestFrame = await guest.nextMessage("guest broadcast frame");
  check(
    "broadcast frame owner->guest re-framed as [0x02][sender=1][body]",
    sameBytes(guestFrame, [2, 1, 0, 0, 0, 40, 50]),
    frameBytes(guestFrame).join(","),
  );

  // --- Second guest join/leave notifications -------------------------------
  const guest2 = new SocketProbe(wsUrl(baseUrl, created.room_code));
  check("second guest websocket opens", await guest2.opened);
  await guest2.nextJson("guest2 joined");
  await guest2.nextJson("guest2 peer_list");
  const ownerSawGuest2 = await owner.nextJson("owner peer_joined guest2");
  const guestSawGuest2 = await guest.nextJson("guest peer_joined guest2");
  check(
    "both peers notified of guest2 (peer_id=3)",
    ownerSawGuest2.peer_id === 3 && guestSawGuest2.peer_id === 3,
    JSON.stringify([ownerSawGuest2, guestSawGuest2]),
  );
  guest2.close();
  const ownerSawLeave = await owner.nextJson("owner peer_left guest2");
  const guestSawLeave = await guest.nextJson("guest peer_left guest2");
  check(
    "peer_left broadcast on guest disconnect",
    ownerSawLeave.type === "peer_left" &&
      ownerSawLeave.peer_id === 3 &&
      guestSawLeave.peer_id === 3,
    JSON.stringify([ownerSawLeave, guestSawLeave]),
  );

  // --- Oversized frame rejection (1009) ------------------------------------
  const flooder = new SocketProbe(wsUrl(baseUrl, created.room_code));
  check("oversize-test guest opens", await flooder.opened);
  await flooder.nextJson("flooder joined");
  await flooder.nextJson("flooder peer_list");
  await owner.nextJson("owner peer_joined flooder");
  await guest.nextJson("guest peer_joined flooder");
  const oversized = new Uint8Array(128 * 1024 + 1);
  oversized[0] = 1; // targeted tag; target bytes stay zero
  flooder.send(oversized.buffer);
  const flooderClose = await flooder.waitForClose("oversize close");
  check(
    "oversized frame closes the connection with 1009",
    flooderClose.code === 1009,
    `code=${flooderClose.code}`,
  );
  await owner.nextJson("owner peer_left flooder");
  await guest.nextJson("guest peer_left flooder");

  // --- Owner disconnect closes the room ------------------------------------
  owner.close();
  const guestPeerLeftOwner = await guest.nextJson("guest peer_left owner");
  check(
    "guest receives peer_left for the owner",
    guestPeerLeftOwner.type === "peer_left" && guestPeerLeftOwner.peer_id === 1,
    JSON.stringify(guestPeerLeftOwner),
  );
  const guestClose = await guest.waitForClose("room close");
  check(
    "guest socket closed (1001) after owner leaves",
    guestClose.code === 1001,
    `code=${guestClose.code}`,
  );
  rooms = await (await fetch(`${baseUrl}/api/rooms`)).json();
  check(
    "closed room no longer listed",
    !rooms.some((room) => room.code === created.room_code),
    JSON.stringify(rooms),
  );
  const rejoin = new SocketProbe(wsUrl(baseUrl, created.room_code));
  check("closed room refuses the upgrade (no open event)", !(await rejoin.opened));

  // --- Unknown room refusal ------------------------------------------------
  const unknown = new SocketProbe(wsUrl(baseUrl, "GLAD-ZZZZ"));
  check("unknown room refuses the upgrade (no open event)", !(await unknown.opened));

  // --- Empty-room expiry via the durable object alarm ----------------------
  if (skipExpiry) {
    console.log("SKIP empty-room expiry (external relay; TTL not overridden)");
  } else {
    const expiryCreate = await createRoom(baseUrl, { campaign: "smoke-expiry" });
    const expiryRoom = await expiryCreate.json();
    const expiryOwner = new SocketProbe(
      wsUrl(baseUrl, expiryRoom.room_code, expiryRoom.owner_token),
    );
    check("expiry room owner opens", await expiryOwner.opened);
    await expiryOwner.nextJson("expiry owner joined");
    await expiryOwner.nextJson("expiry owner peer_list");
    expiryOwner.close();
    await sleep(1_000);

    // Inside the grace window the room is still joinable (owner reconnect).
    const graceOwner = new SocketProbe(
      wsUrl(baseUrl, expiryRoom.room_code, expiryRoom.owner_token),
    );
    check("empty room reconnectable inside grace window", await graceOwner.opened);
    const graceJoined = await graceOwner.nextJson("grace owner joined");
    check(
      "grace reconnect restores owner peer_id=1 host=1",
      graceJoined.peer_id === 1 && graceJoined.host === 1,
      JSON.stringify(graceJoined),
    );
    graceOwner.close();

    // After the (overridden 4s) TTL the alarm deletes the room.
    await sleep(SMOKE_EMPTY_ROOM_TTL_MS + 3_000);
    const expired = new SocketProbe(wsUrl(baseUrl, expiryRoom.room_code));
    check("empty room expired by alarm (upgrade refused)", !(await expired.opened));
  }

  // --- Create rate limit (last: it exhausts the shared local IP budget) ----
  let sawRateLimit = false;
  let successes = 0;
  for (let attempt = 0; attempt < 20; ++attempt) {
    const response = await createRoom(baseUrl, { campaign: "smoke-limit" });
    if (response.status === 429) {
      sawRateLimit = true;
      break;
    }
    if (response.status === 200) {
      successes += 1;
      await response.json();
    }
  }
  check(
    "per-ip create rate limit returns 429",
    sawRateLimit && successes <= 10,
    `successes=${successes} sawRateLimit=${sawRateLimit}`,
  );
}

async function main() {
  let wrangler = null;
  let baseUrl = externalUrl;

  if (!baseUrl) {
    const port = await findFreePort();
    const inspectorPort = await findFreePort();
    baseUrl = `http://127.0.0.1:${port}`;
    console.log(`Starting wrangler dev --local on port ${port} ...`);
    wrangler = spawn(
      "npx",
      [
        "wrangler",
        "dev",
        "--local",
        "--port",
        String(port),
        "--inspector-port",
        String(inspectorPort),
        "--var",
        `EMPTY_ROOM_TTL_MS:${SMOKE_EMPTY_ROOM_TTL_MS}`,
      ],
      {
        cwd: RELAY_DIR,
        stdio: ["ignore", "pipe", "pipe"],
        detached: true,
        env: { ...process.env, CI: "1", WRANGLER_SEND_METRICS: "false" },
      },
    );
    wrangler.stdout.on("data", () => {});
    wrangler.stderr.on("data", () => {});
    wrangler.on("exit", (code) => {
      if (failures === 0 && code !== null && code !== 0) {
        console.error(`wrangler dev exited early with code ${code}`);
      }
    });
  }

  try {
    await waitForRelayReady(baseUrl, 120_000);
    console.log(`Relay ready at ${baseUrl}`);
    await runSmoke(baseUrl);
  } finally {
    if (wrangler && wrangler.pid) {
      try {
        process.kill(-wrangler.pid, "SIGTERM");
      } catch {
        // Already gone.
      }
      await sleep(1_000);
      try {
        process.kill(-wrangler.pid, "SIGKILL");
      } catch {
        // Already gone.
      }
    }
  }

  if (failures > 0) {
    console.error(`\n${failures} smoke check(s) FAILED`);
    process.exit(1);
  }
  console.log("\nAll smoke checks passed");
}

main().catch((error) => {
  console.error(`Smoke run aborted: ${error?.stack ?? error}`);
  process.exit(1);
});
