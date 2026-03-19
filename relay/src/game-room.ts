import { DurableObject } from "cloudflare:workers";

import {
  EMPTY_ROOM_GRACE_MS,
  MAX_RELAY_PAYLOAD_BYTES,
  MAX_ROOM_PEERS,
  MESSAGE_RATE_LIMIT_MAX_MESSAGES,
  MESSAGE_RATE_LIMIT_WINDOW_MS,
  ROOM_INDEX_TTL_SECONDS,
  RELAY_BROADCAST_TAG,
  RELAY_TARGET_TAG,
  clientIpFromRequest,
  isValidRoomCode,
  makeRelayFrame,
  makeRoomInfo,
  readPeerId,
  roomIndexKey,
  roomInfoFromStoredState,
} from "./shared";
import type { Env, StoredRoomState, WebSocketAttachment } from "./types";

const ROOM_STATE_KEY = "room_state";

export class GameRoom extends DurableObject {
  private readonly peers = new Map<number, WebSocket>();
  private readonly messageRateLimits = new Map<
    number,
    { count: number; windowStartedAt: number }
  >();
  private readonly appEnv: Env;
  private stateData: StoredRoomState | null = null;

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.appEnv = env;

    this.ctx.blockConcurrencyWhile(async () => {
      this.stateData =
        (await this.ctx.storage.get<StoredRoomState>(ROOM_STATE_KEY)) ?? null;
      this.restoreHibernatedSockets();
    });
  }

  override async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/internal/metadata") {
      if (!this.stateData) {
        return new Response("Room not initialized", { status: 404 });
      }
      return Response.json(
        roomInfoFromStoredState(this.stateData, this.peers.size),
      );
    }

    if (request.headers.get("upgrade")?.toLowerCase() !== "websocket") {
      return new Response("WebSocket upgrade required", { status: 426 });
    }

    const roomCode = request.headers.get("x-openglad-room-code") ?? "";
    if (!isValidRoomCode(roomCode)) {
      return new Response("Unknown room", { status: 404 });
    }

    if (!this.stateData) {
      const seededRoom = makeRoomInfo({
        code: roomCode,
        campaign_hash:
          request.headers.get("x-openglad-campaign-hash") ?? "",
        campaign_name:
          request.headers.get("x-openglad-campaign-name") ?? "",
        host_name: request.headers.get("x-openglad-host-name") ?? "",
        created_at: Number(request.headers.get("x-openglad-created-at")) || Date.now(),
        player_count: 0,
      });
      this.stateData = {
        code: seededRoom.code,
        campaign_hash: seededRoom.campaign_hash,
        campaign_name: seededRoom.campaign_name,
        host_name: seededRoom.host_name,
        created_at: seededRoom.created_at,
        next_peer_id: 1,
        host_peer_id: null,
      };
      await this.persistState();
    }

    if (this.peers.size >= MAX_ROOM_PEERS) {
      return new Response("Room is full", { status: 409 });
    }

    await this.ctx.storage.deleteAlarm();

    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    const peerId = this.stateData.next_peer_id++;
    const clientIp = clientIpFromRequest(request);

    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({ peerId, clientIp } satisfies WebSocketAttachment);
    this.peers.set(peerId, server);

    if (this.stateData.host_peer_id === null) {
      this.stateData.host_peer_id = peerId;
    }

    await this.persistState();
    await this.persistRoomIndex();

    this.sendJson(server, {
      type: "joined",
      peer_id: peerId,
      host: this.stateData.host_peer_id,
    });
    this.sendJson(server, {
      type: "peer_list",
      peers: [...this.peers.keys()].sort((left, right) => left - right),
      host: this.stateData.host_peer_id,
    });
    await this.broadcastJson(
      {
        type: "peer_joined",
        peer_id: peerId,
        is_host: peerId === this.stateData.host_peer_id,
      },
      peerId,
    );

    return new Response(null, {
      status: 101,
      webSocket: client,
    });
  }

  override async webSocketMessage(
    ws: WebSocket,
    message: string | ArrayBuffer,
  ): Promise<void> {
    const attachment = ws.deserializeAttachment() as
      | WebSocketAttachment
      | undefined;
    const peerId = attachment?.peerId;
    if (!peerId) {
      ws.close(1011, "Missing peer metadata");
      return;
    }

    if (!this.consumeMessageRateBudget(peerId)) {
      ws.close(1008, "Rate limit exceeded");
      return;
    }

    if (typeof message === "string") {
      await this.handleJsonMessage(peerId, ws, message);
      return;
    }

    const bytes = new Uint8Array(message);
    if (bytes.byteLength === 0) {
      return;
    }
    switch (bytes[0]) {
      case RELAY_BROADCAST_TAG:
        if (bytes.byteLength - 1 > MAX_RELAY_PAYLOAD_BYTES) {
          ws.close(1009, "Relay payload too large");
          return;
        }
        await this.forwardPayload(peerId, bytes.subarray(1));
        return;

      case RELAY_TARGET_TAG:
        if (bytes.byteLength < 5) {
          ws.close(1003, "Malformed relay frame");
          return;
        }
        if (bytes.byteLength - 5 > MAX_RELAY_PAYLOAD_BYTES) {
          ws.close(1009, "Relay payload too large");
          return;
        }

        await this.forwardPayload(peerId, bytes.subarray(5), readPeerId(bytes, 1));
        return;

      default:
        ws.close(1003, "Unsupported relay frame");
    }
  }

  override async webSocketClose(ws: WebSocket): Promise<void> {
    const attachment = ws.deserializeAttachment() as
      | WebSocketAttachment
      | undefined;
    const peerId = attachment?.peerId;
    if (!peerId) {
      return;
    }

    await this.removePeer(peerId);
  }

  override async webSocketError(ws: WebSocket): Promise<void> {
    await this.webSocketClose(ws);
  }

  override async alarm(): Promise<void> {
    if (this.peers.size === 0 && this.stateData) {
      await this.appEnv.ROOM_INDEX.delete(roomIndexKey(this.stateData.code));
      await this.ctx.storage.delete(ROOM_STATE_KEY);
      this.stateData = null;
    }
  }

  private restoreHibernatedSockets(): void {
    for (const socket of this.ctx.getWebSockets()) {
      const attachment = socket.deserializeAttachment() as
        | WebSocketAttachment
        | undefined;
      if (!attachment?.peerId) {
        continue;
      }
      this.peers.set(attachment.peerId, socket);
    }
  }

  private async handleJsonMessage(
    peerId: number,
    ws: WebSocket,
    rawMessage: string,
  ): Promise<void> {
    let parsed: { type?: string } | null = null;
    try {
      parsed = JSON.parse(rawMessage) as { type?: string };
    } catch {
      ws.close(1003, "Malformed JSON");
      return;
    }

    switch (parsed?.type) {
      case "leave_room":
        ws.close(1000, "Leaving room");
        await this.removePeer(peerId);
        break;

      case "list_peers":
        this.sendJson(ws, {
          type: "peer_list",
          peers: [...this.peers.keys()].sort((left, right) => left - right),
          host: this.stateData?.host_peer_id ?? null,
        });
        break;

      default:
        break;
    }
  }

  private async forwardPayload(
    fromPeerId: number,
    payload: Uint8Array,
    targetPeerId?: number,
  ): Promise<void> {
    const frame = makeRelayFrame(fromPeerId, payload);
    const failedPeerIds: number[] = [];

    if (targetPeerId !== undefined) {
      const socket = this.peers.get(targetPeerId);
      if (socket && !this.trySend(socket, frame)) {
        failedPeerIds.push(targetPeerId);
      }
      await this.removeFailedPeers(failedPeerIds);
      return;
    }

    for (const [peerId, socket] of this.peers) {
      if (peerId === fromPeerId) {
        continue;
      }
      if (!this.trySend(socket, frame)) {
        failedPeerIds.push(peerId);
      }
    }

    await this.removeFailedPeers(failedPeerIds);
  }

  private async removePeer(peerId: number): Promise<void> {
    if (!this.peers.delete(peerId) || !this.stateData) {
      return;
    }
    this.messageRateLimits.delete(peerId);

    await this.broadcastJson({
      type: "peer_left",
      peer_id: peerId,
    });

    if (this.stateData.host_peer_id === peerId) {
      const nextHostPeerId =
        [...this.peers.keys()].sort((left, right) => left - right)[0] ?? null;
      this.stateData.host_peer_id = nextHostPeerId;
      if (nextHostPeerId !== null) {
        await this.broadcastJson({
          type: "host_changed",
          new_host: nextHostPeerId,
        });
      }
    }

    await this.persistState();
    await this.persistRoomIndex();

    if (this.peers.size === 0) {
      await this.ctx.storage.setAlarm(Date.now() + EMPTY_ROOM_GRACE_MS);
    }
  }

  private async broadcastJson(
    payload: unknown,
    excludedPeerId?: number,
  ): Promise<void> {
    const encoded = JSON.stringify(payload);
    const failedPeerIds: number[] = [];

    for (const [peerId, socket] of this.peers) {
      if (peerId === excludedPeerId) {
        continue;
      }
      if (!this.trySend(socket, encoded)) {
        failedPeerIds.push(peerId);
      }
    }

    await this.removeFailedPeers(failedPeerIds);
  }

  private sendJson(socket: WebSocket, payload: unknown): void {
    this.trySend(socket, JSON.stringify(payload));
  }

  private trySend(socket: WebSocket, payload: string | ArrayBuffer): boolean {
    try {
      socket.send(payload);
      return true;
    } catch {
      return false;
    }
  }

  private async removeFailedPeers(peerIds: Iterable<number>): Promise<void> {
    const uniquePeerIds = [...new Set(peerIds)];
    for (const peerId of uniquePeerIds) {
      await this.removePeer(peerId);
    }
  }

  private async persistState(): Promise<void> {
    if (!this.stateData) {
      return;
    }
    await this.ctx.storage.put(ROOM_STATE_KEY, this.stateData);
  }

  private async persistRoomIndex(): Promise<void> {
    if (!this.stateData) {
      return;
    }

    const roomInfo = roomInfoFromStoredState(this.stateData, this.peers.size);
    await this.appEnv.ROOM_INDEX.put(
      roomIndexKey(roomInfo.code),
      JSON.stringify(roomInfo),
      {
        expirationTtl: ROOM_INDEX_TTL_SECONDS,
      },
    );
  }

  private consumeMessageRateBudget(peerId: number): boolean {
    const now = Date.now();
    const budget = this.messageRateLimits.get(peerId);
    if (!budget || now - budget.windowStartedAt >= MESSAGE_RATE_LIMIT_WINDOW_MS) {
      this.messageRateLimits.set(peerId, {
        count: 1,
        windowStartedAt: now,
      });
      return true;
    }

    if (budget.count >= MESSAGE_RATE_LIMIT_MAX_MESSAGES) {
      return false;
    }

    budget.count += 1;
    return true;
  }
}
