import { env, runDurableObjectAlarm, runInDurableObject, SELF } from "cloudflare:test";
import { afterEach, describe, expect, it } from "vitest";

import {
  CREATE_RATE_LIMIT_MAX_ROOMS,
  DEFAULT_EMPTY_ROOM_TTL_MS,
  MAX_BROADCAST_FRAME_BYTES,
  MAX_CAMPAIGN_HASH_LENGTH,
  MAX_CAMPAIGN_NAME_LENGTH,
  MAX_HOST_NAME_LENGTH,
  MAX_INBOUND_BINARY_FRAME_BYTES,
  MAX_INBOUND_TEXT_MESSAGE_BYTES,
  MAX_ROOM_PEERS,
  OWNER_CONNECT_GRACE_MS,
  REGISTRY_ENTRY_TTL_MS,
  REGISTRY_INSTANCE_NAME,
  RELAY_BROADCAST_TAG,
  RELAY_TARGET_TAG,
  ROOM_MAX_AGE_MS,
  registryRoomKey,
} from "../src/shared";

const openSockets: WebSocket[] = [];

interface CreateRoomOptions {
  campaign?: string;
  campaignName?: string;
  hostName?: string;
  clientIp?: string;
}

interface CreatedRoom {
  code: string;
  ownerToken: string;
}

function websocketRequest(path: string, headers?: HeadersInit): Request {
  const requestHeaders = new Headers(headers);
  requestHeaders.set("Upgrade", "websocket");
  return new Request(`https://relay.test${path}`, {
    headers: requestHeaders,
  });
}

function roomSocketPath(room: CreatedRoom, asOwner = false): string {
  if (!asOwner || !room.ownerToken) {
    return `/api/room/${room.code}`;
  }
  return `/api/room/${room.code}?owner_token=${encodeURIComponent(room.ownerToken)}`;
}

async function openRoomSocket(path: string, headers?: HeadersInit): Promise<WebSocket> {
  const response = await SELF.fetch(websocketRequest(path, headers));
  expect(response.status).toBe(101);
  const socket = response.webSocket;
  if (!socket) {
    throw new Error("Expected a WebSocket upgrade response");
  }
  socket.accept();
  // Recent compatibility dates default client sockets to the spec's "blob"
  // binaryType; the game clients pin arraybuffer (emscripten does so
  // explicitly, ixwebsocket is raw RFC6455), so the tests do too.
  (socket as unknown as { binaryType: string }).binaryType = "arraybuffer";
  openSockets.push(socket);
  return socket;
}

function waitForMessage(
  socket: WebSocket,
  label: string,
): Promise<string | ArrayBuffer> {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      reject(new Error(`Timed out waiting for WebSocket message: ${label}`));
    }, 1_000);

    socket.addEventListener(
      "message",
      (event) => {
        clearTimeout(timeout);
        resolve(event.data as string | ArrayBuffer);
      },
      { once: true },
    );
    socket.addEventListener(
      "close",
      () => {
        clearTimeout(timeout);
        reject(new Error(`WebSocket closed before a message arrived: ${label}`));
      },
      { once: true },
    );
  });
}

async function waitForJson(socket: WebSocket, label: string): Promise<unknown> {
  return JSON.parse((await waitForMessage(socket, label)) as string) as unknown;
}

/** Consume the joined + peer_list handshake sent to every new socket. */
async function readHandshake(
  socket: WebSocket,
  label: string,
): Promise<{
  joined: { peer_id: number; host: number };
  peerList: { peers: number[]; host: number };
}> {
  const joined = (await waitForJson(socket, `${label} joined`)) as {
    peer_id: number;
    host: number;
  };
  const peerList = (await waitForJson(socket, `${label} peer list`)) as {
    peers: number[];
    host: number;
  };
  return { joined, peerList };
}

function waitForClose(socket: WebSocket): Promise<CloseEvent> {
  return new Promise((resolve) => {
    socket.addEventListener("close", (event) => resolve(event), { once: true });
  });
}

function waitForSilence(
  socket: WebSocket,
  label: string,
  timeoutMs = 100,
): Promise<void> {
  return new Promise((resolve, reject) => {
    const onMessage = (event: MessageEvent) => {
      cleanup();
      reject(
        new Error(
          `Unexpected WebSocket message while waiting for silence (${label}): ${String(event.data)}`,
        ),
      );
    };
    const onClose = () => {
      cleanup();
      reject(new Error(`WebSocket closed while waiting for silence: ${label}`));
    };
    const cleanup = () => {
      clearTimeout(timeout);
      socket.removeEventListener("message", onMessage);
      socket.removeEventListener("close", onClose);
    };
    const timeout = setTimeout(() => {
      cleanup();
      resolve();
    }, timeoutMs);

    socket.addEventListener("message", onMessage);
    socket.addEventListener("close", onClose, { once: true });
  });
}

function decodeFrame(data: ArrayBuffer): number[] {
  return [...new Uint8Array(data)];
}

function targetedFrame(targetPeerId: number, body: number[]): ArrayBuffer {
  const frame = new Uint8Array(5 + body.length);
  frame[0] = RELAY_TARGET_TAG;
  frame[1] = targetPeerId & 0xff;
  frame[2] = (targetPeerId >>> 8) & 0xff;
  frame[3] = (targetPeerId >>> 16) & 0xff;
  frame[4] = (targetPeerId >>> 24) & 0xff;
  frame.set(body, 5);
  return frame.buffer;
}

function roomStubFor(room: CreatedRoom): DurableObjectStub {
  return env.GAME_ROOM.get(env.GAME_ROOM.idFromName(room.code));
}

function registryStub(): DurableObjectStub {
  return env.ROOM_REGISTRY.get(
    env.ROOM_REGISTRY.idFromName(REGISTRY_INSTANCE_NAME),
  );
}

async function listRooms(campaign?: string): Promise<
  Array<{
    code: string;
    campaign_hash: string;
    campaign_name: string;
    host_name: string;
    player_count: number;
    created_at: number;
  }>
> {
  const url = campaign
    ? `https://relay.test/api/rooms?campaign=${encodeURIComponent(campaign)}`
    : "https://relay.test/api/rooms";
  const response = await SELF.fetch(url);
  expect(response.status).toBe(200);
  return (await response.json()) as Array<{
    code: string;
    campaign_hash: string;
    campaign_name: string;
    host_name: string;
    player_count: number;
    created_at: number;
  }>;
}

interface MutableRoomInternals {
  stateData: {
    created_at: number;
    empty_since: number | null;
    host_ever_connected: boolean;
  } | null;
  persistState(): Promise<void>;
}

async function mutateRoomState(
  stub: DurableObjectStub,
  mutate: (state: NonNullable<MutableRoomInternals["stateData"]>) => void,
): Promise<void> {
  await runInDurableObject(stub, async (instance) => {
    const room = instance as unknown as MutableRoomInternals;
    if (!room.stateData) {
      throw new Error("Room state is not initialized");
    }
    mutate(room.stateData);
    await room.persistState();
  });
}

async function createRoom(options: CreateRoomOptions = {}): Promise<CreatedRoom> {
  const params = new URLSearchParams();
  params.set("campaign", options.campaign ?? "gladiator");
  if (options.campaignName) {
    params.set("campaign_name", options.campaignName);
  }
  if (options.hostName) {
    params.set("host", options.hostName);
  }

  const headers = new Headers();
  headers.set("cf-connecting-ip", options.clientIp ?? "192.0.2.1");

  const response = await SELF.fetch(
    `https://relay.test/api/create?${params.toString()}`,
    { method: "POST", headers },
  );
  expect(response.status).toBe(200);
  expect(response.headers.get("access-control-allow-origin")).toBe("*");
  const payload = (await response.json()) as {
    room_code?: string;
    code?: string;
    owner_token?: string;
  };
  // Codes avoid the confusable characters 0/O and 1/I/L.
  expect(payload.room_code).toMatch(/^GLAD-[2-9A-HJKMNP-Z]{4}$/);
  expect(payload.code).toBe(payload.room_code);
  expect(payload.owner_token).toMatch(/^[0-9a-f]{32}$/);
  return {
    code: payload.room_code ?? "",
    ownerToken: payload.owner_token ?? "",
  };
}

afterEach(async () => {
  for (const socket of openSockets.splice(0)) {
    try {
      socket.close(1000, "cleanup");
    } catch {
      // Ignore sockets that are already closed.
    }
  }
  await new Promise((resolve) => setTimeout(resolve, 25));
  await runInDurableObject(registryStub(), async (instance, state) => {
    // Reset both the room directory and the in-memory create rate counters so
    // tests cannot bleed into each other through the shared registry.
    (instance as unknown as { createBudgets: Map<string, unknown> }).createBudgets.clear();
    await state.storage.deleteAll();
  });
});

describe("OpenGlad relay worker", () => {
  it("creates rooms, lists occupied rooms, relays frames, and closes the room when the owner leaves", async () => {
    const alphaRoom = await createRoom({
      campaign: "campaign.alpha",
      campaignName: "Alpha Campaign",
      hostName: "Host Alpha",
    });
    await createRoom({ campaign: "campaign.beta" });

    // Rooms stay hidden until the owner connects.
    await expect(listRooms()).resolves.toEqual([]);

    const ownerSocket = await openRoomSocket(roomSocketPath(alphaRoom, true));
    const ownerHandshake = await readHandshake(ownerSocket, "owner");
    expect(ownerHandshake.joined).toEqual(
      expect.objectContaining({ peer_id: 1, host: 1 }),
    );
    expect(ownerHandshake.peerList).toEqual(
      expect.objectContaining({ peers: [1], host: 1 }),
    );

    await expect(listRooms("campaign.alpha")).resolves.toEqual([
      expect.objectContaining({
        code: alphaRoom.code,
        campaign_hash: "campaign.alpha",
        campaign_name: "Alpha Campaign",
        host_name: "Host Alpha",
        player_count: 1,
      }),
    ]);
    await expect(listRooms("campaign.does-not-exist")).resolves.toEqual([]);

    const ownerPeerJoinedPromise = waitForJson(ownerSocket, "owner peer joined");
    const guestSocket = await openRoomSocket(roomSocketPath(alphaRoom));
    const guestHandshake = await readHandshake(guestSocket, "guest");
    expect(guestHandshake.joined).toEqual(
      expect.objectContaining({ peer_id: 2, host: 1 }),
    );
    expect(guestHandshake.peerList).toEqual(
      expect.objectContaining({ peers: [1, 2], host: 1 }),
    );
    await expect(ownerPeerJoinedPromise).resolves.toEqual({
      type: "peer_joined",
      peer_id: 2,
      is_host: false,
    });

    // client->relay [0x01][target u32 LE][body] becomes
    // relay->client [0x02][sender u32 LE][body].
    const ownerTargetedPromise = waitForMessage(ownerSocket, "owner targeted relay");
    guestSocket.send(targetedFrame(1, [7, 8, 9]));
    expect(decodeFrame((await ownerTargetedPromise) as ArrayBuffer)).toEqual([
      2, 2, 0, 0, 0, 7, 8, 9,
    ]);

    // client->relay [0x03][body] broadcast.
    const guestBroadcastPromise = waitForMessage(guestSocket, "guest broadcast relay");
    ownerSocket.send(new Uint8Array([RELAY_BROADCAST_TAG, 4, 5, 6]).buffer);
    expect(decodeFrame((await guestBroadcastPromise) as ArrayBuffer)).toEqual([
      2, 1, 0, 0, 0, 4, 5, 6,
    ]);

    // Owner leaves with a guest present: peer_left, then the room closes.
    const guestPeerLeftPromise = waitForJson(guestSocket, "guest peer left");
    const guestClosePromise = waitForClose(guestSocket);
    ownerSocket.close(1000, "owner left");
    await expect(guestPeerLeftPromise).resolves.toEqual({
      type: "peer_left",
      peer_id: 1,
    });
    const guestClose = await guestClosePromise;
    expect(guestClose.code).toBe(1001);

    await expect(listRooms("campaign.alpha")).resolves.toEqual([]);
    const rejoin = await SELF.fetch(websocketRequest(roomSocketPath(alphaRoom)));
    expect(rejoin.status).toBe(404);
  });

  it("keeps the room open when a guest leaves and notifies the others", async () => {
    const room = await createRoom({ campaign: "campaign.guest-leave" });

    const ownerSocket = await openRoomSocket(roomSocketPath(room, true));
    await readHandshake(ownerSocket, "owner");

    const ownerSawGuest1 = waitForJson(ownerSocket, "owner saw guest1");
    const guest1Socket = await openRoomSocket(roomSocketPath(room));
    await readHandshake(guest1Socket, "guest1");
    await ownerSawGuest1;

    const ownerSawGuest2 = waitForJson(ownerSocket, "owner saw guest2");
    const guest1SawGuest2 = waitForJson(guest1Socket, "guest1 saw guest2");
    const guest2Socket = await openRoomSocket(roomSocketPath(room));
    const guest2Handshake = await readHandshake(guest2Socket, "guest2");
    expect(guest2Handshake.joined).toEqual(
      expect.objectContaining({ peer_id: 3, host: 1 }),
    );
    await ownerSawGuest2;
    await guest1SawGuest2;

    const ownerPeerLeft = waitForJson(ownerSocket, "owner guest2 left");
    const guest1PeerLeft = waitForJson(guest1Socket, "guest1 guest2 left");
    guest2Socket.close(1000, "guest2 done");
    await expect(ownerPeerLeft).resolves.toEqual({ type: "peer_left", peer_id: 3 });
    await expect(guest1PeerLeft).resolves.toEqual({ type: "peer_left", peer_id: 3 });

    await expect(listRooms("campaign.guest-leave")).resolves.toEqual([
      expect.objectContaining({ code: room.code, player_count: 2 }),
    ]);

    const ownerRelayPromise = waitForMessage(ownerSocket, "owner relay after leave");
    guest1Socket.send(targetedFrame(1, [42]));
    expect(decodeFrame((await ownerRelayPromise) as ArrayBuffer)).toEqual([
      2, 2, 0, 0, 0, 42,
    ]);
  });

  it("supersedes the previous owner connection when the owner reconnects", async () => {
    const room = await createRoom({ campaign: "campaign.supersede" });

    const ownerSocket = await openRoomSocket(roomSocketPath(room, true));
    await readHandshake(ownerSocket, "owner");

    const ownerSawGuest = waitForJson(ownerSocket, "owner saw guest");
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    await readHandshake(guestSocket, "guest");
    await ownerSawGuest;

    const oldOwnerClosePromise = waitForClose(ownerSocket);
    const replacementSocket = await openRoomSocket(roomSocketPath(room, true));
    const replacementHandshake = await readHandshake(replacementSocket, "replacement");
    expect(replacementHandshake.joined).toEqual(
      expect.objectContaining({ peer_id: 1, host: 1 }),
    );
    expect(replacementHandshake.peerList).toEqual(
      expect.objectContaining({ peers: [1, 2], host: 1 }),
    );

    const oldOwnerClose = await oldOwnerClosePromise;
    expect(oldOwnerClose.code).toBe(1012);

    // The guest never learns about the swap: no peer_joined/peer_left churn.
    await waitForSilence(guestSocket, "guest during owner supersede");

    const replacementRelayPromise = waitForMessage(
      replacementSocket,
      "relay to replacement owner",
    );
    guestSocket.send(targetedFrame(1, [4, 3, 2, 1]));
    expect(decodeFrame((await replacementRelayPromise) as ArrayBuffer)).toEqual([
      2, 2, 0, 0, 0, 4, 3, 2, 1,
    ]);
  });

  it("keeps an empty room reconnectable through the grace window and expires it via the alarm", async () => {
    const room = await createRoom({ campaign: "campaign.grace" });
    const roomStub = roomStubFor(room);

    const ownerSocket = await openRoomSocket(roomSocketPath(room, true));
    await readHandshake(ownerSocket, "owner");
    ownerSocket.close(1000, "owner left alone");
    await new Promise((resolve) => setTimeout(resolve, 25));

    // Owner alone -> no room close; reconnect within the grace window works.
    const reconnectSocket = await openRoomSocket(roomSocketPath(room, true));
    const reconnectHandshake = await readHandshake(reconnectSocket, "reconnect");
    expect(reconnectHandshake.joined).toEqual(
      expect.objectContaining({ peer_id: 1, host: 1 }),
    );
    reconnectSocket.close(1000, "left again");
    await new Promise((resolve) => setTimeout(resolve, 25));

    // Age the emptiness past the TTL, then fire the alarm: the room dies.
    await mutateRoomState(roomStub, (state) => {
      state.empty_since = Date.now() - (DEFAULT_EMPTY_ROOM_TTL_MS + 1);
    });
    await expect(runDurableObjectAlarm(roomStub)).resolves.toBe(true);

    const expired = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(expired.status).toBe(404);
    await expect(listRooms("campaign.grace")).resolves.toEqual([]);
  });

  it("expires rooms whose owner never presented the owner token", async () => {
    const room = await createRoom({ campaign: "campaign.no-owner" });
    const roomStub = roomStubFor(room);

    // A guest keeps the room non-empty, but the owner never connects.
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    const guestHandshake = await readHandshake(guestSocket, "guest");
    expect(guestHandshake.joined).toEqual(
      expect.objectContaining({ peer_id: 2, host: 0 }),
    );

    await mutateRoomState(roomStub, (state) => {
      state.created_at = Date.now() - (OWNER_CONNECT_GRACE_MS + 1);
    });
    const guestClosePromise = waitForClose(guestSocket);
    await expect(runDurableObjectAlarm(roomStub)).resolves.toBe(true);
    const guestClose = await guestClosePromise;
    expect(guestClose.code).toBe(1001);

    const expired = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(expired.status).toBe(404);
  });

  it("closes rooms that exceed the absolute maximum age", async () => {
    const room = await createRoom({ campaign: "campaign.max-age" });
    const roomStub = roomStubFor(room);

    const ownerSocket = await openRoomSocket(roomSocketPath(room, true));
    await readHandshake(ownerSocket, "owner");

    await mutateRoomState(roomStub, (state) => {
      state.created_at = Date.now() - (ROOM_MAX_AGE_MS + 1);
    });
    const ownerClosePromise = waitForClose(ownerSocket);
    await expect(runDurableObjectAlarm(roomStub)).resolves.toBe(true);
    const ownerClose = await ownerClosePromise;
    expect(ownerClose.code).toBe(1001);

    const expired = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(expired.status).toBe(404);
  });

  it("enforces room capacity and reserves a slot for the owner", async () => {
    const room = await createRoom({ campaign: "campaign.capacity" });

    // Guests may fill every slot except the owner's reserved one.
    const guestSockets: WebSocket[] = [];
    for (let index = 0; index < MAX_ROOM_PEERS - 1; ++index) {
      const socket = await openRoomSocket(roomSocketPath(room));
      await readHandshake(socket, `capacity guest ${index}`);
      guestSockets.push(socket);
    }

    const guestOverflow = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(guestOverflow.status).toBe(409);

    // The owner still gets in (MAX_ROOM_PEERS total) and is announced as host.
    const firstGuestSawOwner = waitForJson(guestSockets[0], "guest saw owner");
    const ownerSocket = await openRoomSocket(roomSocketPath(room, true));
    const ownerHandshake = await readHandshake(ownerSocket, "late owner");
    expect(ownerHandshake.joined).toEqual(
      expect.objectContaining({ peer_id: 1, host: 1 }),
    );
    expect(ownerHandshake.peerList).toEqual(
      expect.objectContaining({
        peers: Array.from({ length: MAX_ROOM_PEERS }, (_, index) => index + 1),
        host: 1,
      }),
    );
    await expect(firstGuestSawOwner).resolves.toEqual({
      type: "peer_joined",
      peer_id: 1,
      is_host: true,
    });

    const fullOverflow = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(fullOverflow.status).toBe(409);
  });

  it("relays frames up to the 128 KiB limit and closes larger ones with 1009", async () => {
    const room = await createRoom({ campaign: "campaign.frame-size" });

    const ownerSocket = await openRoomSocket(roomSocketPath(room, true));
    await readHandshake(ownerSocket, "owner");
    const ownerSawGuest = waitForJson(ownerSocket, "owner saw guest");
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    await readHandshake(guestSocket, "guest");
    await ownerSawGuest;

    // Boundary: a maximum-size targeted frame forwards intact (the relayed
    // frame is the same size, still within the C++ inbound limit).
    const maxTargeted = new Uint8Array(MAX_INBOUND_BINARY_FRAME_BYTES);
    maxTargeted[0] = RELAY_TARGET_TAG;
    maxTargeted[1] = 1; // target peer 1, little-endian
    maxTargeted[MAX_INBOUND_BINARY_FRAME_BYTES - 1] = 0xaa;
    const ownerMaxFramePromise = waitForMessage(ownerSocket, "owner max frame");
    guestSocket.send(maxTargeted.buffer);
    const ownerMaxFrame = (await ownerMaxFramePromise) as ArrayBuffer;
    expect(ownerMaxFrame.byteLength).toBe(MAX_INBOUND_BINARY_FRAME_BYTES);
    expect(new Uint8Array(ownerMaxFrame)[0]).toBe(2);
    expect(new Uint8Array(ownerMaxFrame)[MAX_INBOUND_BINARY_FRAME_BYTES - 1]).toBe(0xaa);

    // Boundary: a maximum-size broadcast (4 bytes smaller, because re-framing
    // adds the sender header) forwards intact.
    const maxBroadcast = new Uint8Array(MAX_BROADCAST_FRAME_BYTES);
    maxBroadcast[0] = RELAY_BROADCAST_TAG;
    const ownerMaxBroadcastPromise = waitForMessage(ownerSocket, "owner max broadcast");
    guestSocket.send(maxBroadcast.buffer);
    expect(((await ownerMaxBroadcastPromise) as ArrayBuffer).byteLength).toBe(
      MAX_INBOUND_BINARY_FRAME_BYTES,
    );

    // One byte over the broadcast bound: 1009, and the owner sees peer_left.
    const guestClosePromise = waitForClose(guestSocket);
    const ownerPeerLeftPromise = waitForJson(ownerSocket, "owner guest left");
    const oversizedBroadcast = new Uint8Array(MAX_BROADCAST_FRAME_BYTES + 1);
    oversizedBroadcast[0] = RELAY_BROADCAST_TAG;
    guestSocket.send(oversizedBroadcast.buffer);
    expect((await guestClosePromise).code).toBe(1009);
    await expect(ownerPeerLeftPromise).resolves.toEqual({
      type: "peer_left",
      peer_id: 2,
    });

    // Oversized targeted frame from a fresh guest: also 1009.
    const ownerSawGuest2 = waitForJson(ownerSocket, "owner saw guest2");
    const guest2Socket = await openRoomSocket(roomSocketPath(room));
    await readHandshake(guest2Socket, "guest2");
    await ownerSawGuest2;
    const guest2ClosePromise = waitForClose(guest2Socket);
    const oversizedTargeted = new Uint8Array(MAX_INBOUND_BINARY_FRAME_BYTES + 1);
    oversizedTargeted[0] = RELAY_TARGET_TAG;
    oversizedTargeted[1] = 1;
    guest2Socket.send(oversizedTargeted.buffer);
    expect((await guest2ClosePromise).code).toBe(1009);
  });

  it("enforces the per-ip create rate limit", async () => {
    const limitedIp = "203.0.113.10";
    for (let index = 0; index < CREATE_RATE_LIMIT_MAX_ROOMS; ++index) {
      await createRoom({ campaign: "campaign.limit", clientIp: limitedIp });
    }

    const limitedResponse = await SELF.fetch(
      "https://relay.test/api/create?campaign=campaign.limit",
      {
        method: "POST",
        headers: { "cf-connecting-ip": limitedIp },
      },
    );
    expect(limitedResponse.status).toBe(429);

    // A different IP is unaffected.
    await createRoom({ campaign: "campaign.limit", clientIp: "203.0.113.11" });
  });

  it("closes connections that exceed the per-connection message budget", async () => {
    const room = await createRoom({ campaign: "campaign.flood" });

    const ownerSocket = await openRoomSocket(roomSocketPath(room, true));
    await readHandshake(ownerSocket, "owner");

    // Unknown-tag frames are dropped silently but still consume budget. The
    // test environment lowers the budget to TEST_MESSAGE_BUDGET (see
    // vitest.config.mts) so the flood reliably trips the limit inside one
    // budget window regardless of machine load.
    const TEST_MESSAGE_BUDGET = 50;
    const closePromise = waitForClose(ownerSocket);
    const junkFrame = new Uint8Array([9]).buffer;
    for (let index = 0; index < TEST_MESSAGE_BUDGET + 10; ++index) {
      try {
        ownerSocket.send(junkFrame);
      } catch {
        break;
      }
    }
    expect((await closePromise).code).toBe(1008);
  });

  it("bounds room metadata and handles text control messages", async () => {
    const longCampaign = `campaign.${"x".repeat(MAX_CAMPAIGN_HASH_LENGTH + 64)}`;
    const room = await createRoom({
      campaign: longCampaign,
      campaignName: "Campaign ".repeat(40),
      hostName: "Host ".repeat(40),
    });

    const ownerSocket = await openRoomSocket(roomSocketPath(room, true));
    await readHandshake(ownerSocket, "owner");

    const rooms = await listRooms();
    const listed = rooms.find((entry) => entry.code === room.code);
    expect(listed).toBeTruthy();
    expect(listed?.campaign_hash).toHaveLength(MAX_CAMPAIGN_HASH_LENGTH);
    expect(listed?.campaign_name).toHaveLength(MAX_CAMPAIGN_NAME_LENGTH);
    expect(listed?.host_name).toHaveLength(MAX_HOST_NAME_LENGTH);

    // list_peers control query.
    const peerListPromise = waitForJson(ownerSocket, "list_peers reply");
    ownerSocket.send(JSON.stringify({ type: "list_peers" }));
    await expect(peerListPromise).resolves.toEqual({
      type: "peer_list",
      peers: [1],
      host: 1,
    });

    // leave_room from a guest: clean close plus peer_left for the others.
    const ownerSawGuest = waitForJson(ownerSocket, "owner saw guest");
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    await readHandshake(guestSocket, "guest");
    await ownerSawGuest;
    const guestClosePromise = waitForClose(guestSocket);
    const ownerPeerLeftPromise = waitForJson(ownerSocket, "owner guest left");
    guestSocket.send(JSON.stringify({ type: "leave_room" }));
    expect((await guestClosePromise).code).toBe(1000);
    await expect(ownerPeerLeftPromise).resolves.toEqual({
      type: "peer_left",
      peer_id: 2,
    });

    // Malformed JSON text: 1003.
    const ownerSawGuest2 = waitForJson(ownerSocket, "owner saw guest2");
    const guest2Socket = await openRoomSocket(roomSocketPath(room));
    await readHandshake(guest2Socket, "guest2");
    await ownerSawGuest2;
    const guest2ClosePromise = waitForClose(guest2Socket);
    guest2Socket.send("this is not json");
    expect((await guest2ClosePromise).code).toBe(1003);

    // Oversized text: 1009.
    const ownerClosePromise = waitForClose(ownerSocket);
    ownerSocket.send("x".repeat(MAX_INBOUND_TEXT_MESSAGE_BYTES + 1));
    expect((await ownerClosePromise).code).toBe(1009);
  });

  it("prunes stale registry entries from the listing", async () => {
    await runInDurableObject(registryStub(), async (_instance, state) => {
      await state.storage.put(registryRoomKey("GLAD-STAL"), {
        code: "GLAD-STAL",
        campaign_hash: "campaign.stale",
        campaign_name: "Stale",
        host_name: "Ghost",
        player_count: 2,
        created_at: Date.now() - REGISTRY_ENTRY_TTL_MS - 1_000,
        updated_at: Date.now() - REGISTRY_ENTRY_TTL_MS - 1_000,
      });
    });

    await expect(listRooms("campaign.stale")).resolves.toEqual([]);
    await runInDurableObject(registryStub(), async (_instance, state) => {
      expect(await state.storage.get(registryRoomKey("GLAD-STAL"))).toBeUndefined();
    });
  });

  it("rejects unknown rooms, malformed codes, and wrong methods", async () => {
    // Unknown but well-formed room: HTTP 404 upgrade refusal.
    const unknown = await SELF.fetch(websocketRequest("/api/room/GLAD-ZZZZ"));
    expect(unknown.status).toBe(404);

    // Lower-case input is normalized before lookup.
    const room = await createRoom({ campaign: "campaign.routes" });
    const ownerSocket = await openRoomSocket(
      `/api/room/${room.code.toLowerCase()}?owner_token=${encodeURIComponent(room.ownerToken)}`,
    );
    await readHandshake(ownerSocket, "case-insensitive owner");

    // Malformed code: 400.
    const malformed = await SELF.fetch(websocketRequest("/api/room/NOPE"));
    expect(malformed.status).toBe(400);

    // Plain GET without an upgrade on a live room: 426.
    const noUpgrade = await SELF.fetch(`https://relay.test/api/room/${room.code}`);
    expect(noUpgrade.status).toBe(426);

    // Method checks and CORS preflight.
    const wrongCreate = await SELF.fetch("https://relay.test/api/create");
    expect(wrongCreate.status).toBe(405);
    const wrongRooms = await SELF.fetch("https://relay.test/api/rooms", {
      method: "POST",
    });
    expect(wrongRooms.status).toBe(405);
    const preflight = await SELF.fetch("https://relay.test/api/create", {
      method: "OPTIONS",
    });
    expect(preflight.status).toBe(204);
    expect(preflight.headers.get("access-control-allow-origin")).toBe("*");

    const banner = await SELF.fetch("https://relay.test/");
    expect(banner.status).toBe(200);
    expect(await banner.text()).toBe("OpenGlad Relay");
  });
});
