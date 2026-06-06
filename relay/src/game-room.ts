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
  roomOwnerKey,
  roomInfoFromStoredState,
} from "./shared";
import type { Env, StoredRoomState, WebSocketAttachment } from "./types";

const ROOM_STATE_KEY = "room_state";
const ROOM_EXPIRATION_MS = ROOM_INDEX_TTL_SECONDS * 1_000;

interface InitializeRoomPayload {
  code?: string;
  campaign_hash?: string;
  campaign_name?: string;
  host_name?: string;
  created_at?: number;
  owner_token?: string;
}

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
      const stored =
        (await this.ctx.storage.get<Partial<StoredRoomState>>(ROOM_STATE_KEY)) ?? null;
      this.stateData = stored
        ? {
            code: stored.code ?? "",
            campaign_hash: stored.campaign_hash ?? "",
            campaign_name: stored.campaign_name ?? "",
            host_name: stored.host_name ?? "",
            created_at: stored.created_at ?? Date.now(),
            next_peer_id: stored.next_peer_id ?? 1,
            host_peer_id:
              typeof stored.host_peer_id === "number" ? stored.host_peer_id : null,
            owner_peer_id:
              typeof stored.owner_peer_id === "number"
                ? stored.owner_peer_id
                : typeof stored.host_owner_token === "string" &&
                    stored.host_owner_token.length > 0
                  ? 1
                  : typeof stored.host_peer_id === "number"
                    ? stored.host_peer_id
                    : null,
            host_owner_token:
              typeof stored.host_owner_token === "string" ? stored.host_owner_token : "",
          }
        : null;
      this.restoreHibernatedSockets();
    });
  }

  override async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/internal/initialize") {
      return this.initializeRoom(request);
    }

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
      return new Response("Room not found", { status: 404 });
    }

    const isHostOwner = this.isHostOwnerRequest(request);
    const replacedOwnerConnection = this.replaceExistingOwnerConnectionIfNeeded(isHostOwner);

    if (this.isRoomExpired()) {
      return new Response("Room not found", { status: 404 });
    }

    if (this.peers.size >= MAX_ROOM_PEERS) {
      return new Response("Room is full", { status: 409 });
    }

    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    const peerId = this.assignPeerId(isHostOwner);
    const clientIp = clientIpFromRequest(request);

    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({
      peerId,
      clientIp,
      isHostOwner,
    } satisfies WebSocketAttachment);
    this.peers.set(peerId, server);

    await this.persistState();
    await this.persistRoomIndex();
    await this.scheduleNextAlarm();

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
    if (!replacedOwnerConnection) {
      await this.broadcastJson(
        {
          type: "peer_joined",
          peer_id: peerId,
          is_host: peerId === this.stateData.host_peer_id,
        },
        peerId,
      );
    }

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
    if (this.peers.get(peerId) !== ws) {
      this.tryClose(ws, 1000, "Superseded connection");
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
    if (this.peers.get(peerId) !== ws) {
      return;
    }

    await this.removePeer(peerId);
  }

  override async webSocketError(ws: WebSocket): Promise<void> {
    await this.webSocketClose(ws);
  }

  override async alarm(): Promise<void> {
    if (!this.stateData) {
      return;
    }

    if (this.isRoomExpired()) {
      if (this.peers.size === 0) {
        await this.deleteRoomState();
      }
      return;
    }

    if (this.peers.size === 0) {
      await this.deleteRoomState();
      return;
    }

    await this.scheduleNextAlarm();
  }

  private async initializeRoom(request: Request): Promise<Response> {
    if (request.method !== "POST") {
      return new Response("Method not allowed", { status: 405 });
    }
    if (this.stateData) {
      return new Response("Room already initialized", { status: 409 });
    }

    let payload: InitializeRoomPayload | null = null;
    try {
      payload = (await request.json()) as InitializeRoomPayload;
    } catch {
      return new Response("Malformed room initialization payload", { status: 400 });
    }

    const roomCode = typeof payload?.code === "string" ? payload.code : "";
    if (!isValidRoomCode(roomCode)) {
      return new Response("Invalid room code", { status: 400 });
    }

    const seededRoom = makeRoomInfo({
      code: roomCode,
      campaign_hash:
        typeof payload?.campaign_hash === "string" ? payload.campaign_hash : "",
      campaign_name:
        typeof payload?.campaign_name === "string" ? payload.campaign_name : "",
      host_name:
        typeof payload?.host_name === "string" ? payload.host_name : "",
      created_at:
        typeof payload?.created_at === "number" ? payload.created_at : Date.now(),
      player_count: 0,
    });
    const ownerToken =
      typeof payload?.owner_token === "string" ? payload.owner_token : "";

    this.stateData = {
      code: seededRoom.code,
      campaign_hash: seededRoom.campaign_hash,
      campaign_name: seededRoom.campaign_name,
      host_name: seededRoom.host_name,
      created_at: seededRoom.created_at,
      next_peer_id: ownerToken ? 2 : 1,
      host_peer_id: null,
      owner_peer_id: ownerToken ? 1 : null,
      host_owner_token: ownerToken,
    };
    await this.persistState();
    await this.scheduleNextAlarm();
    return Response.json(roomInfoFromStoredState(this.stateData, 0));
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
    await this.scheduleNextAlarm();
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

  private tryClose(
    socket: WebSocket,
    code: number,
    reason: string,
  ): void {
    try {
      socket.close(code, reason);
    } catch {
      // Ignore sockets that are already closing or closed.
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

    if (this.isRoomExpired()) {
      await this.appEnv.ROOM_INDEX.delete(roomIndexKey(this.stateData.code));
      await this.appEnv.ROOM_INDEX.delete(roomOwnerKey(this.stateData.code));
      return;
    }

    const roomInfo = roomInfoFromStoredState(this.stateData, this.peers.size);
    await this.appEnv.ROOM_INDEX.put(
      roomIndexKey(roomInfo.code),
      JSON.stringify(roomInfo),
      {
        expiration: this.roomKvExpiration(),
      },
    );
  }

  private async deleteRoomState(): Promise<void> {
    if (!this.stateData) {
      return;
    }
    await this.appEnv.ROOM_INDEX.delete(roomIndexKey(this.stateData.code));
    await this.appEnv.ROOM_INDEX.delete(roomOwnerKey(this.stateData.code));
    await this.ctx.storage.delete(ROOM_STATE_KEY);
    await this.ctx.storage.deleteAlarm();
    this.stateData = null;
  }

  private roomExpiresAt(): number | null {
    if (!this.stateData) {
      return null;
    }
    return this.stateData.created_at + ROOM_EXPIRATION_MS;
  }

  private roomKvExpiration(): number {
    if (!this.stateData) {
      throw new Error("Room state must exist before computing KV expiration");
    }
    return Math.ceil(this.stateData.created_at / 1_000) + ROOM_INDEX_TTL_SECONDS;
  }

  private isRoomExpired(now = Date.now()): boolean {
    const expiresAt = this.roomExpiresAt();
    return expiresAt !== null && now >= expiresAt;
  }

  private async scheduleNextAlarm(now = Date.now()): Promise<void> {
    if (!this.stateData) {
      return;
    }

    let nextAlarmAt: number | null = null;
    const expiresAt = this.roomExpiresAt();
    if (expiresAt !== null && expiresAt > now) {
      nextAlarmAt = expiresAt;
    } else if (this.peers.size === 0) {
      nextAlarmAt = now;
    }

    if (this.peers.size === 0) {
      const emptyRoomDeadline = now + EMPTY_ROOM_GRACE_MS;
      nextAlarmAt =
        nextAlarmAt === null ? emptyRoomDeadline : Math.min(nextAlarmAt, emptyRoomDeadline);
    }

    if (nextAlarmAt === null) {
      await this.ctx.storage.deleteAlarm();
      return;
    }

    await this.ctx.storage.setAlarm(nextAlarmAt);
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

  private replaceExistingOwnerConnectionIfNeeded(isHostOwner: boolean): boolean {
    if (!isHostOwner || !this.stateData || this.stateData.owner_peer_id === null) {
      return false;
    }

    const existingSocket = this.peers.get(this.stateData.owner_peer_id);
    if (!existingSocket) {
      return false;
    }

    this.peers.delete(this.stateData.owner_peer_id);
    this.messageRateLimits.delete(this.stateData.owner_peer_id);
    this.tryClose(existingSocket, 1012, "Superseded by reconnect");
    return true;
  }

  private assignPeerId(isHostOwner: boolean): number {
    if (!this.stateData) {
      throw new Error("Room state must exist before assigning a peer id");
    }

    if (
      isHostOwner &&
      this.stateData.owner_peer_id !== null &&
      !this.peers.has(this.stateData.owner_peer_id)
    ) {
      const peerId = this.stateData.owner_peer_id;
      if (peerId >= this.stateData.next_peer_id) {
        this.stateData.next_peer_id = peerId + 1;
      }
      if (this.stateData.host_peer_id === null) {
        this.stateData.host_peer_id = peerId;
      }
      return peerId;
    }

    while (
      this.peers.has(this.stateData.next_peer_id) ||
      this.stateData.next_peer_id === this.stateData.owner_peer_id
    ) {
      this.stateData.next_peer_id += 1;
    }

    const peerId = this.stateData.next_peer_id;
    this.stateData.next_peer_id += 1;

    if (this.stateData.owner_peer_id === null && isHostOwner) {
      this.stateData.owner_peer_id = peerId;
    }

    if (this.stateData.host_peer_id === null) {
      this.stateData.host_peer_id = peerId;
    }

    return peerId;
  }

  private isHostOwnerRequest(request: Request): boolean {
    if (!this.stateData) {
      return false;
    }

    if (!this.stateData.host_owner_token) {
      return this.stateData.owner_peer_id === null;
    }

    const connectingOwnerToken =
      request.headers.get("x-openglad-connecting-owner-token") ?? "";
    return (
      connectingOwnerToken.length > 0 &&
      connectingOwnerToken === this.stateData.host_owner_token
    );
  }
}
