import { env, SELF } from "cloudflare:test";
import { afterEach, describe, expect, it } from "vitest";

const openSockets: WebSocket[] = [];

function websocketRequest(path: string): Request {
  return new Request(`https://relay.test${path}`, {
    headers: {
      Upgrade: "websocket",
    },
  });
}

async function openRoomSocket(path: string): Promise<WebSocket> {
  const response = await SELF.fetch(websocketRequest(path));
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

function decodeFrame(data: ArrayBuffer): number[] {
  return [...new Uint8Array(data)];
}

async function createRoom(campaign = "org.openglad.gladiator"): Promise<string> {
  const response = await SELF.fetch(
    `https://relay.test/api/create?campaign=${encodeURIComponent(campaign)}`,
    {
      method: "POST",
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
  const list = await env.ROOM_INDEX.list({ prefix: "room:" });
  await Promise.all(
    list.keys.map(({ name }: { name: string }) => env.ROOM_INDEX.delete(name)),
  );
});

describe("OpenGlad relay worker", () => {
  it("supports room listing, frame relay, host migration, and room limits", async () => {
    const alphaRoom = await createRoom("campaign.alpha");
    const betaRoom = await createRoom("campaign.beta");

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

    const roomCode = await createRoom("campaign.capacity");
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
    sockets[0].send(new Uint8Array(64 * 1024 + 6).buffer);
    const closeEvent = await closePromise;
    expect(closeEvent.code).toBe(1009);

    for (const socket of sockets.slice(1)) {
      socket.close(1000, "done");
    }
  });
});
