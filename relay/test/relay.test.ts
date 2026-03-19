import { env, runDurableObjectAlarm, runInDurableObject, SELF } from "cloudflare:test";
import { afterEach, describe, expect, it } from "vitest";

import {
  MAX_RELAY_PAYLOAD_BYTES,
  RELAY_BROADCAST_TAG,
} from "../src/shared";

const openSockets: WebSocket[] = [];

interface CreateRoomOptions {
  campaign?: string;
  campaignName?: string;
  hostName?: string;
  clientIp?: string;
}

function websocketRequest(path: string, headers?: HeadersInit): Request {
  const requestHeaders = new Headers(headers);
  requestHeaders.set("Upgrade", "websocket");
  return new Request(`https://relay.test${path}`, {
    headers: requestHeaders,
  });
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

function decodeFrame(data: ArrayBuffer): number[] {
  return [...new Uint8Array(data)];
}

async function createRoom(options: CreateRoomOptions = {}): Promise<string> {
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
  const payload = (await response.json()) as { code: string };
  expect(payload.code).toMatch(/^GLAD-[A-Z0-9]{4}$/);
  return payload.code;
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
  it("supports room listing, frame relay, host migration, and room limits", async () => {
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

    const hostSocket = await openRoomSocket(`/api/room/${alphaRoom}`);
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
        code: alphaRoom,
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
    const guestSocket = await openRoomSocket(`/api/room/${alphaRoom}`);
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
        code: alphaRoom,
        player_count: 1,
      }),
    ]);

    guestSocket.close(1000, "done");

    const roomCode = await createRoom({ campaign: "campaign.capacity" });
    const sockets: WebSocket[] = [];
    for (let index = 0; index < 4; ++index) {
      const socket = await openRoomSocket(`/api/room/${roomCode}`);
      sockets.push(socket);
      await waitForMessage(socket, "capacity joined");
      await waitForMessage(socket, "capacity peer list");
    }

    const overCapacity = await SELF.fetch(websocketRequest(`/api/room/${roomCode}`));
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
    const roomCode = await createRoom({ campaign: "campaign.reconnect" });
    const roomStub = env.GAME_ROOM.get(env.GAME_ROOM.idFromName(roomCode));

    const hostSocket = await openRoomSocket(`/api/room/${roomCode}`);
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

    const reconnectSocket = await openRoomSocket(`/api/room/${roomCode}`);
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
      peer_id: 2,
      host: 2,
    }));
    expect(reconnectPeerList).toEqual(expect.objectContaining({
      peers: [2],
      host: 2,
    }));

    reconnectSocket.close(1000, "grace reconnect left");
    await waitForAlarmScheduled(roomStub);

    await expect(runDurableObjectAlarm(roomStub)).resolves.toBe(true);

    const expiredResponse = await SELF.fetch(websocketRequest(`/api/room/${roomCode}`));
    expect(expiredResponse.status).toBe(404);
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

    const roomCode = await createRoom({
      campaign: "campaign.limit",
      clientIp: "203.0.113.11",
    });

    const floodSocket = await openRoomSocket(`/api/room/${roomCode}`, {
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
    const roomCode = await createRoom({
      campaign: "campaign.nat",
      clientIp: "203.0.113.12",
    });
    const sharedIpHeaders = {
      "cf-connecting-ip": "198.51.100.77",
    };

    const hostSocket = await openRoomSocket(`/api/room/${roomCode}`, sharedIpHeaders);
    await waitForMessage(hostSocket, "nat host joined");
    await waitForMessage(hostSocket, "nat host peer list");

    const hostPeerJoinedPromise = waitForMessages(hostSocket, 1, "nat host peer joined");
    const guestSocket = await openRoomSocket(`/api/room/${roomCode}`, sharedIpHeaders);
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
});
