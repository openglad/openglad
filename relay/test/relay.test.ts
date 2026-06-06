import { env, runDurableObjectAlarm, runInDurableObject, SELF } from "cloudflare:test";
import { afterEach, describe, expect, it } from "vitest";

import {
  MAX_RELAY_PAYLOAD_BYTES,
  ROOM_INDEX_TTL_SECONDS,
  RELAY_BROADCAST_TAG,
  roomIndexKey,
  roomOwnerKey,
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
        reject(new Error("WebSocket closed before a message arrived"));
      },
      { once: true },
    );
  });
}

function waitForMessages(
  socket: WebSocket,
  count: number,
  label: string,
): Promise<(string | ArrayBuffer)[]> {
  return new Promise((resolve, reject) => {
    const messages: (string | ArrayBuffer)[] = [];
    const timeout = setTimeout(() => {
      reject(new Error(`Timed out waiting for WebSocket messages: ${label}`));
    }, 1_000);

    const onMessage = (event: MessageEvent) => {
      messages.push(event.data as string | ArrayBuffer);
      if (messages.length >= count) {
        clearTimeout(timeout);
        socket.removeEventListener("message", onMessage);
        socket.removeEventListener("close", onClose);
        resolve(messages);
      }
    };
    const onClose = () => {
      clearTimeout(timeout);
      socket.removeEventListener("message", onMessage);
      socket.removeEventListener("close", onClose);
      reject(new Error(`WebSocket closed before enough messages arrived: ${label}`));
    };

    socket.addEventListener("message", onMessage);
    socket.addEventListener("close", onClose, { once: true });
  });
}

function waitForClose(socket: WebSocket): Promise<CloseEvent> {
  return new Promise((resolve) => {
    socket.addEventListener(
      "close",
      (event) => resolve(event),
      { once: true },
    );
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

async function waitForCondition(
  label: string,
  predicate: () => Promise<boolean>,
): Promise<void> {
  const deadline = Date.now() + 1_000;
  while (Date.now() < deadline) {
    if (await predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  throw new Error(`Timed out waiting for condition: ${label}`);
}

async function waitForAlarmScheduled(stub: DurableObjectStub): Promise<void> {
  await waitForCondition("durable object alarm", async () => {
    return (
      (await runInDurableObject(stub, (_instance, state) => state.storage.getAlarm())) !==
      null
    );
  });
}

async function replacePeerSocketWithThrowingSend(
  stub: DurableObjectStub,
  peerId: number,
): Promise<void> {
  await runInDurableObject(stub, (instance) => {
    const room = instance as unknown as {
      peers: Map<number, WebSocket>;
    };
    const original = room.peers.get(peerId);
    if (!original) {
      throw new Error(`Missing peer ${peerId}`);
    }

    const attachment = original.deserializeAttachment();
    room.peers.set(peerId, {
      send() {
        throw new Error("simulated send failure");
      },
      close() {},
      deserializeAttachment() {
        return attachment;
      },
    } as unknown as WebSocket);
  });
}

async function ageRoomPastExpiry(stub: DurableObjectStub): Promise<void> {
  await runInDurableObject(stub, async (instance, state) => {
    const room = instance as unknown as {
      stateData: { created_at: number } | null;
      persistState(): Promise<void>;
    };
    if (!room.stateData) {
      throw new Error("Room state is not initialized");
    }

    room.stateData.created_at =
      Date.now() - (ROOM_INDEX_TTL_SECONDS * 1_000 + 1);
    await room.persistState();
    await state.storage.setAlarm(Date.now());
  });
}

async function readRoomPeerState(stub: DurableObjectStub): Promise<{
  hostPeerId: number | null;
  ownerPeerId: number | null;
  peerIds: number[];
}> {
  return runInDurableObject(stub, (instance) => {
    const room = instance as unknown as {
      peers: Map<number, WebSocket>;
      stateData: {
        host_peer_id: number | null;
        owner_peer_id: number | null;
      } | null;
    };
    return {
      hostPeerId: room.stateData?.host_peer_id ?? null,
      ownerPeerId: room.stateData?.owner_peer_id ?? null,
      peerIds: [...room.peers.keys()].sort((left, right) => left - right),
    };
  });
}

function decodeFrame(data: ArrayBuffer): number[] {
  return [...new Uint8Array(data)];
}

async function createRoom(options: CreateRoomOptions = {}): Promise<CreatedRoom> {
  const params = new URLSearchParams();
  params.set("campaign", options.campaign ?? "org.openglad.gladiator");
  if (options.campaignName) {
    params.set("campaign_name", options.campaignName);
  }
  if (options.hostName) {
    params.set("host", options.hostName);
  }

  const headers = new Headers();
  if (options.clientIp) {
    headers.set("cf-connecting-ip", options.clientIp);
  }

  const response = await SELF.fetch(
    `https://relay.test/api/create?${params.toString()}`,
    {
      method: "POST",
      headers,
    },
  );
  expect(response.status).toBe(200);
  const payload = (await response.json()) as { code: string; owner_token?: string };
  expect(payload.code).toMatch(/^GLAD-[A-Z0-9]{4}$/);
  expect(typeof payload.owner_token).toBe("string");
  expect(payload.owner_token).toMatch(/^[0-9a-f]{32}$/);
  return {
    code: payload.code,
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
  const list = await env.ROOM_INDEX.list();
  await Promise.all(
    list.keys.map(({ name }: { name: string }) => env.ROOM_INDEX.delete(name)),
  );
});

describe("OpenGlad relay worker", () => {
  it("supports room listing, frame relay, host migration, owner reclaim, and room limits", async () => {
    const alphaRoom = await createRoom({
      campaign: "campaign.alpha",
      campaignName: "Alpha Campaign",
      hostName: "Host Alpha",
    });
    const betaRoom = await createRoom({
      campaign: "campaign.beta",
      campaignName: "Beta Campaign",
      hostName: "Host Beta",
    });

    let response = await SELF.fetch("https://relay.test/api/rooms");
    expect(response.status).toBe(200);
    expect(await response.json()).toEqual([]);

    const hostSocket = await openRoomSocket(roomSocketPath(alphaRoom, true));
    const hostJoined = JSON.parse((await waitForMessage(hostSocket, "host joined")) as string) as {
      peer_id: number;
      host: number;
    };
    const hostPeerList = JSON.parse((await waitForMessage(hostSocket, "host peer list")) as string) as {
      peers: number[];
      host: number;
    };
    expect(hostJoined.peer_id).toBe(1);
    expect(hostJoined.host).toBe(1);
    expect(hostPeerList.peers).toEqual([1]);

    response = await SELF.fetch(
      "https://relay.test/api/rooms?campaign=campaign.alpha",
    );
    expect(response.status).toBe(200);
    await expect(response.json()).resolves.toEqual([
      expect.objectContaining({
        code: alphaRoom.code,
        campaign_hash: "campaign.alpha",
        campaign_name: "Alpha Campaign",
        host_name: "Host Alpha",
        player_count: 1,
      }),
    ]);
    response = await SELF.fetch(
      "https://relay.test/api/rooms?campaign=campaign.beta",
    );
    await expect(response.json()).resolves.toEqual([]);

    const hostPeerJoinedPromise = waitForMessages(hostSocket, 1, "host peer joined");
    const guestSocket = await openRoomSocket(roomSocketPath(alphaRoom));
    const guestJoined = JSON.parse((await waitForMessage(guestSocket, "guest joined")) as string) as {
      peer_id: number;
      host: number;
    };
    const guestPeerList = JSON.parse((await waitForMessage(guestSocket, "guest peer list")) as string) as {
      peers: number[];
      host: number;
    };
    const [hostPeerJoinedRaw] = await hostPeerJoinedPromise;
    const hostPeerJoined = JSON.parse(hostPeerJoinedRaw as string) as {
      type: string;
      peer_id: number;
      is_host: boolean;
    };

    expect(guestJoined.peer_id).toBe(2);
    expect(guestJoined.host).toBe(1);
    expect(guestPeerList.peers).toEqual([1, 2]);
    expect(hostPeerJoined).toEqual({
      type: "peer_joined",
      peer_id: 2,
      is_host: false,
    });

    const hostTargetedRelayPromise = waitForMessage(hostSocket, "host targeted relay");
    guestSocket.send(
      new Uint8Array([1, 1, 0, 0, 0, 7, 8, 9]).buffer,
    );
    expect(decodeFrame((await hostTargetedRelayPromise) as ArrayBuffer)).toEqual([
      2,
      2,
      0,
      0,
      0,
      7,
      8,
      9,
    ]);

    const guestBroadcastRelayPromise = waitForMessage(guestSocket, "guest broadcast relay");
    hostSocket.send(new Uint8Array([3, 4, 5, 6]).buffer);
    expect(decodeFrame((await guestBroadcastRelayPromise) as ArrayBuffer)).toEqual([
      2,
      1,
      0,
      0,
      0,
      4,
      5,
      6,
    ]);

    const guestCloseMessagesPromise = waitForMessages(
      guestSocket,
      2,
      "guest close updates",
    );
    hostSocket.close(1000, "host left");

    const guestCloseMessages = await guestCloseMessagesPromise;
    expect(JSON.parse(guestCloseMessages[0] as string)).toEqual({
      type: "peer_left",
      peer_id: 1,
    });
    expect(JSON.parse(guestCloseMessages[1] as string)).toEqual({
      type: "host_changed",
      new_host: 2,
    });

    const roomListResponse = await SELF.fetch("https://relay.test/api/rooms");
    await expect(roomListResponse.json()).resolves.toEqual([
      expect.objectContaining({
        code: alphaRoom.code,
        player_count: 1,
      }),
    ]);

    await expect(
      readRoomPeerState(env.GAME_ROOM.get(env.GAME_ROOM.idFromName(alphaRoom.code))),
    ).resolves.toEqual({
      hostPeerId: 2,
      ownerPeerId: 1,
      peerIds: [2],
    });

    const guestHostReturnPromise = waitForMessage(
      guestSocket,
      "guest host return",
    );
    const reconnectedHostSocket = await openRoomSocket(roomSocketPath(alphaRoom, true));
    const reconnectedHostJoined = JSON.parse(
      (await waitForMessage(reconnectedHostSocket, "reconnected host joined")) as string,
    ) as {
      peer_id: number;
      host: number;
    };
    const reconnectedHostPeerList = JSON.parse(
      (await waitForMessage(reconnectedHostSocket, "reconnected host peer list")) as string,
    ) as {
      peers: number[];
      host: number;
    };
    expect(reconnectedHostJoined).toEqual(expect.objectContaining({
      peer_id: 1,
      host: 2,
    }));
    expect(reconnectedHostPeerList).toEqual(expect.objectContaining({
      peers: [1, 2],
      host: 2,
    }));
    expect(JSON.parse((await guestHostReturnPromise) as string)).toEqual({
      type: "peer_joined",
      peer_id: 1,
      is_host: false,
    });

    const hostRelayAgainPromise = waitForMessage(
      reconnectedHostSocket,
      "host relay after reconnect",
    );
    guestSocket.send(
      new Uint8Array([1, 1, 0, 0, 0, 3, 2, 1]).buffer,
    );
    expect(decodeFrame((await hostRelayAgainPromise) as ArrayBuffer)).toEqual([
      2,
      2,
      0,
      0,
      0,
      3,
      2,
      1,
    ]);

    guestSocket.close(1000, "done");
    reconnectedHostSocket.close(1000, "done");

    const room = await createRoom({ campaign: "campaign.capacity" });
    const sockets: WebSocket[] = [];
    for (let index = 0; index < 4; ++index) {
      const socket = await openRoomSocket(
        index === 0 ? roomSocketPath(room, true) : roomSocketPath(room),
      );
      sockets.push(socket);
      await waitForMessage(socket, "capacity joined");
      await waitForMessage(socket, "capacity peer list");
    }

    const overCapacity = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(overCapacity.status).toBe(409);

    const closePromise = waitForClose(sockets[0]);
    const oversizedBroadcast = new Uint8Array(MAX_RELAY_PAYLOAD_BYTES + 2);
    oversizedBroadcast[0] = RELAY_BROADCAST_TAG;
    sockets[0].send(oversizedBroadcast.buffer);
    const closeEvent = await closePromise;
    expect(closeEvent.code).toBe(1009);

    for (const socket of sockets.slice(1)) {
      socket.close(1000, "done");
    }
  });

  it("keeps empty rooms reconnectable during the grace window and expires them after the alarm", async () => {
    const room = await createRoom({ campaign: "campaign.reconnect" });
    const roomStub = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(room.code));

    const hostSocket = await openRoomSocket(roomSocketPath(room, true));
    const hostJoined = JSON.parse((await waitForMessage(hostSocket, "host joined")) as string) as {
      peer_id: number;
      host: number;
    };
    const hostPeerList = JSON.parse((await waitForMessage(hostSocket, "host peer list")) as string) as {
      peers: number[];
      host: number;
    };
    expect(hostJoined).toEqual(expect.objectContaining({
      peer_id: 1,
      host: 1,
    }));
    expect(hostPeerList).toEqual(expect.objectContaining({
      peers: [1],
      host: 1,
    }));

    hostSocket.close(1000, "host left");
    await waitForAlarmScheduled(roomStub);

    const hiddenDuringGrace = await SELF.fetch("https://relay.test/api/rooms");
    await expect(hiddenDuringGrace.json()).resolves.toEqual([]);

    const reconnectSocket = await openRoomSocket(roomSocketPath(room, true));
    const reconnectJoined = JSON.parse(
      (await waitForMessage(reconnectSocket, "reconnect joined")) as string,
    ) as {
      peer_id: number;
      host: number;
    };
    const reconnectPeerList = JSON.parse(
      (await waitForMessage(reconnectSocket, "reconnect peer list")) as string,
    ) as {
      peers: number[];
      host: number;
    };
    expect(reconnectJoined).toEqual(expect.objectContaining({
      peer_id: 1,
      host: 1,
    }));
    expect(reconnectPeerList).toEqual(expect.objectContaining({
      peers: [1],
      host: 1,
    }));

    reconnectSocket.close(1000, "grace reconnect left");
    await waitForAlarmScheduled(roomStub);

    await expect(runDurableObjectAlarm(roomStub)).resolves.toBe(true);

    const expiredResponse = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(expiredResponse.status).toBe(404);
  });

  it("connects newly created rooms even if the KV index is not yet visible", async () => {
    const room = await createRoom({ campaign: "campaign.kv-lag" });

    await env.ROOM_INDEX.delete(roomIndexKey(room.code));
    await env.ROOM_INDEX.delete(roomOwnerKey(room.code));

    const hostSocket = await openRoomSocket(roomSocketPath(room, true));
    const hostJoined = JSON.parse((await waitForMessage(hostSocket, "kv-lag host joined")) as string) as {
      peer_id: number;
      host: number;
    };
    const hostPeerList = JSON.parse((await waitForMessage(hostSocket, "kv-lag host peer list")) as string) as {
      peers: number[];
      host: number;
    };
    expect(hostJoined).toEqual(expect.objectContaining({
      peer_id: 1,
      host: 1,
    }));
    expect(hostPeerList).toEqual(expect.objectContaining({
      peers: [1],
      host: 1,
    }));

    const hostPeerJoinedPromise = waitForMessages(
      hostSocket,
      1,
      "kv-lag host peer joined",
    );
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    const guestJoined = JSON.parse((await waitForMessage(guestSocket, "kv-lag guest joined")) as string) as {
      peer_id: number;
      host: number;
    };
    const guestPeerList = JSON.parse((await waitForMessage(guestSocket, "kv-lag guest peer list")) as string) as {
      peers: number[];
      host: number;
    };
    const [hostPeerJoinedRaw] = await hostPeerJoinedPromise;

    expect(guestJoined).toEqual(expect.objectContaining({
      peer_id: 2,
      host: 1,
    }));
    expect(guestPeerList).toEqual(expect.objectContaining({
      peers: [1, 2],
      host: 1,
    }));
    expect(JSON.parse(hostPeerJoinedRaw as string)).toEqual({
      type: "peer_joined",
      peer_id: 2,
      is_host: false,
    });
  });

  it("expires abandoned rooms after the one-hour code lifetime", async () => {
    const room = await createRoom({ campaign: "campaign.expire" });
    const roomStub = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(room.code));

    await ageRoomPastExpiry(roomStub);
    await runDurableObjectAlarm(roomStub);
    await waitForCondition("expired room cleanup", async () => {
      return (
        (await env.ROOM_INDEX.get(roomIndexKey(room.code))) === null &&
        (await env.ROOM_INDEX.get(roomOwnerKey(room.code))) === null
      );
    });

    const expiredResponse = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(expiredResponse.status).toBe(404);
    await expect(env.ROOM_INDEX.get(roomIndexKey(room.code))).resolves.toBeNull();
    await expect(env.ROOM_INDEX.get(roomOwnerKey(room.code))).resolves.toBeNull();
  });

  it("stops advertising expired rooms after later join or leave updates", async () => {
    const room = await createRoom({ campaign: "campaign.expire-listing" });
    const roomStub = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(room.code));

    const hostSocket = await openRoomSocket(roomSocketPath(room, true));
    await waitForMessage(hostSocket, "expire-listing host joined");
    await waitForMessage(hostSocket, "expire-listing host peer list");

    const hostPeerJoinedPromise = waitForMessages(
      hostSocket,
      1,
      "expire-listing host peer joined",
    );
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    await waitForMessage(guestSocket, "expire-listing guest joined");
    await waitForMessage(guestSocket, "expire-listing guest peer list");
    await hostPeerJoinedPromise;

    await ageRoomPastExpiry(roomStub);

    const guestCloseUpdates = waitForMessages(
      guestSocket,
      2,
      "expire-listing close updates",
    );
    hostSocket.close(1000, "host left");
    await guestCloseUpdates;

    await waitForCondition("expired room removed from listing", async () => {
      const response = await SELF.fetch("https://relay.test/api/rooms");
      const rooms = (await response.json()) as Array<{ code: string }>;
      return !rooms.some((entry) => entry.code === room.code);
    });

    const expiredJoin = await SELF.fetch(websocketRequest(roomSocketPath(room)));
    expect(expiredJoin.status).toBe(404);
  });

  it("reclaims the host peer id when the owner reconnects before the old socket closes", async () => {
    const room = await createRoom({ campaign: "campaign.overlap-reconnect" });
    const roomStub = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(room.code));

    const hostSocket = await openRoomSocket(roomSocketPath(room, true));
    await waitForMessage(hostSocket, "overlap host joined");
    await waitForMessage(hostSocket, "overlap host peer list");

    const hostPeerJoinedPromise = waitForMessages(
      hostSocket,
      1,
      "overlap host peer joined",
    );
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    await waitForMessage(guestSocket, "overlap guest joined");
    await waitForMessage(guestSocket, "overlap guest peer list");
    await hostPeerJoinedPromise;

    const hostClosePromise = waitForClose(hostSocket);
    const replacementHostSocket = await openRoomSocket(roomSocketPath(room, true));
    const replacementJoined = JSON.parse(
      (await waitForMessage(replacementHostSocket, "replacement host joined")) as string,
    ) as {
      peer_id: number;
      host: number;
    };
    const replacementPeerList = JSON.parse(
      (await waitForMessage(replacementHostSocket, "replacement host peer list")) as string,
    ) as {
      peers: number[];
      host: number;
    };

    expect(replacementJoined).toEqual(expect.objectContaining({
      peer_id: 1,
      host: 1,
    }));
    expect(replacementPeerList).toEqual(expect.objectContaining({
      peers: [1, 2],
      host: 1,
    }));

    const hostCloseEvent = await hostClosePromise;
    expect(hostCloseEvent.code).toBe(1012);

    await waitForSilence(guestSocket, "owner reconnect replacement");

    const hostRelayPromise = waitForMessage(
      replacementHostSocket,
      "overlap relay after replacement",
    );
    guestSocket.send(
      new Uint8Array([1, 1, 0, 0, 0, 4, 3, 2, 1]).buffer,
    );
    expect(decodeFrame((await hostRelayPromise) as ArrayBuffer)).toEqual([
      2,
      2,
      0,
      0,
      0,
      4,
      3,
      2,
      1,
    ]);

    await expect(readRoomPeerState(roomStub)).resolves.toEqual({
      hostPeerId: 1,
      ownerPeerId: 1,
      peerIds: [1, 2],
    });
  });

  it("enforces per-ip create limits and per-connection websocket message limits", async () => {
    const limitedIp = "203.0.113.10";
    for (let index = 0; index < 10; ++index) {
      await createRoom({
        campaign: "campaign.limit",
        clientIp: limitedIp,
      });
    }

    const limitedCreateResponse = await SELF.fetch(
      "https://relay.test/api/create?campaign=campaign.limit",
      {
        method: "POST",
        headers: {
          "cf-connecting-ip": limitedIp,
        },
      },
    );
    expect(limitedCreateResponse.status).toBe(429);

    const room = await createRoom({
      campaign: "campaign.limit",
      clientIp: "203.0.113.11",
    });

    const floodSocket = await openRoomSocket(roomSocketPath(room, true), {
      "cf-connecting-ip": "198.51.100.42",
    });
    await waitForMessage(floodSocket, "flood joined");
    await waitForMessage(floodSocket, "flood peer list");

    const closePromise = waitForClose(floodSocket);
    for (let index = 0; index < 110; ++index) {
      try {
        floodSocket.send(new Uint8Array([3, 1, 2, 3]).buffer);
      } catch {
        break;
      }
    }

    const closeEvent = await closePromise;
    expect(closeEvent.code).toBe(1008);
  });

  it("does not share websocket rate limits across peers behind the same nat", async () => {
    const room = await createRoom({
      campaign: "campaign.nat",
      clientIp: "203.0.113.12",
    });
    const sharedIpHeaders = {
      "cf-connecting-ip": "198.51.100.77",
    };

    const hostSocket = await openRoomSocket(roomSocketPath(room, true), sharedIpHeaders);
    await waitForMessage(hostSocket, "nat host joined");
    await waitForMessage(hostSocket, "nat host peer list");

    const hostPeerJoinedPromise = waitForMessages(hostSocket, 1, "nat host peer joined");
    const guestSocket = await openRoomSocket(roomSocketPath(room), sharedIpHeaders);
    await waitForMessage(guestSocket, "nat guest joined");
    await waitForMessage(guestSocket, "nat guest peer list");
    await hostPeerJoinedPromise;

    for (let index = 0; index < 100; ++index) {
      hostSocket.send(new Uint8Array([1, 99, 0, 0, 0, 7]).buffer);
    }

    const hostRelayPromise = waitForMessage(hostSocket, "nat guest relay");
    guestSocket.send(new Uint8Array([1, 1, 0, 0, 0, 9]).buffer);
    expect(decodeFrame((await hostRelayPromise) as ArrayBuffer)).toEqual([
      2,
      2,
      0,
      0,
      0,
      9,
    ]);

    const hostClosePromise = waitForClose(hostSocket);
    hostSocket.send(new Uint8Array([1, 99, 0, 0, 0, 7]).buffer);
    const hostCloseEvent = await hostClosePromise;
    expect(hostCloseEvent.code).toBe(1008);
  });

  it("cleans up relay send failures and migrates the room host", async () => {
    const room = await createRoom({ campaign: "campaign.send-failure" });
    const roomStub = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(room.code));

    const hostSocket = await openRoomSocket(roomSocketPath(room, true));
    await waitForMessage(hostSocket, "send failure host joined");
    await waitForMessage(hostSocket, "send failure host peer list");

    const hostPeerJoinedPromise = waitForMessages(
      hostSocket,
      1,
      "send failure host peer joined",
    );
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    await waitForMessage(guestSocket, "send failure guest joined");
    await waitForMessage(guestSocket, "send failure guest peer list");
    await hostPeerJoinedPromise;

    await replacePeerSocketWithThrowingSend(roomStub, 1);

    const guestCleanupPromise = waitForMessages(
      guestSocket,
      2,
      "send failure cleanup updates",
    );
    guestSocket.send(new Uint8Array([RELAY_BROADCAST_TAG, 7, 8, 9]).buffer);

    const guestCleanupMessages = await guestCleanupPromise;
    expect(JSON.parse(guestCleanupMessages[0] as string)).toEqual({
      type: "peer_left",
      peer_id: 1,
    });
    expect(JSON.parse(guestCleanupMessages[1] as string)).toEqual({
      type: "host_changed",
      new_host: 2,
    });

    await waitForCondition("room index after relay send failure", async () => {
      const response = await SELF.fetch("https://relay.test/api/rooms");
      const rooms = (await response.json()) as Array<{
        code: string;
        player_count: number;
      }>;
      return rooms.some((entry) => entry.code === room.code && entry.player_count === 1);
    });

    await expect(readRoomPeerState(roomStub)).resolves.toEqual({
      hostPeerId: 2,
      ownerPeerId: 1,
      peerIds: [2],
    });
  });

  it("cleans up notification send failures through normal peer removal", async () => {
    const room = await createRoom({ campaign: "campaign.broadcast-failure" });
    const roomStub = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(room.code));

    const hostSocket = await openRoomSocket(roomSocketPath(room, true));
    await waitForMessage(hostSocket, "broadcast failure host joined");
    await waitForMessage(hostSocket, "broadcast failure host peer list");

    const hostPeerJoinedPromise = waitForMessages(
      hostSocket,
      1,
      "broadcast failure host peer joined",
    );
    const guestSocket = await openRoomSocket(roomSocketPath(room));
    await waitForMessage(guestSocket, "broadcast failure guest joined");
    await waitForMessage(guestSocket, "broadcast failure guest peer list");
    await hostPeerJoinedPromise;

    await replacePeerSocketWithThrowingSend(roomStub, 2);

    hostSocket.close(1000, "host left");
    await waitForAlarmScheduled(roomStub);

    const hiddenRoomList = await SELF.fetch("https://relay.test/api/rooms");
    await expect(hiddenRoomList.json()).resolves.toEqual([]);
    await expect(readRoomPeerState(roomStub)).resolves.toEqual({
      hostPeerId: null,
      ownerPeerId: 1,
      peerIds: [],
    });
  });
});
