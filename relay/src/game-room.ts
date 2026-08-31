import { DurableObject } from "cloudflare:workers";

import {
  DEFAULT_EMPTY_ROOM_TTL_MS,
  MAX_BROADCAST_FRAME_BYTES,
  MAX_INBOUND_BINARY_FRAME_BYTES,
  MAX_INBOUND_TEXT_MESSAGE_BYTES,
  MAX_ROOM_PEERS,
  MESSAGE_RATE_LIMIT_MAX_BYTES,
  MESSAGE_RATE_LIMIT_MAX_MESSAGES,
  MESSAGE_RATE_LIMIT_WINDOW_MS,
  OWNER_CONNECT_GRACE_MS,
  REGISTRY_HEARTBEAT_MS,
  REGISTRY_INSTANCE_NAME,
  RELAY_BROADCAST_TAG,
  RELAY_PEER_HEADER_SIZE,
  RELAY_TARGET_TAG,
  ROOM_MAX_AGE_MS,
  isValidRoomCode,
  makeRelayFrame,
  makeRoomInfo,
  readPeerId,
} from "./shared";
import type { Env, StoredRoomState, WebSocketAttachment } from "./types";

const ROOM_STATE_KEY = "room_state";
/** Peer id reserved for the room owner (the C++ clients do not rely on this —
 *  they read the "host" control field — but "owner is 1" keeps ids familiar). */
const OWNER_PEER_ID = 1;

interface InitializeRoomPayload {
  code?: string;
  campaign_hash?: string;
  campaign_name?: string;
  host_name?: string;
  owner_token?: string;
}

interface MessageBudget {
  count: number;
  bytes: number;
  windowStartedAt: number;
}

/**
 * One durable object per room. Uses the WebSocket Hibernation API
 * (acceptWebSocket + serializeAttachment + webSocketMessage/webSocketClose),
 * so an idle lobby holds no running isolate; on wake the constructor rebuilds
 * the peer map from the hibernated sockets' attachments.
 *
 * Lifecycle:
 *  - initialized via /internal/initialize from the worker's POST /api/create.
 *  - joinable from creation (the owner's create -> connect gap is covered by
 *    the empty-room grace window).
 *  - the owner (correct owner_token) always gets peer id 1 and is the host.
 *    An owner reconnect supersedes the previous owner socket (closed 1012).
 *  - owner disconnect with guests present: remaining peers receive
 *    {"type":"peer_left","peer_id":1} and are then closed (1001) — the C++
 *    transport resets host_peer_id on the host's peer_left and surfaces the
 *    closed link as TransportLinkState::Lost ("connection lost"). The game
 *    server lives inside the host's client, so the room cannot outlive it.
 *  - owner disconnect while alone: the room stays reconnectable for the
 *    empty-room grace window, then the alarm deletes it.
 *  - alarms also enforce: owner-never-connected grace, absolute max room age,
 *    and the periodic registry heartbeat while occupied.
 */
export class GameRoom extends DurableObject {
  private readonly peers = new Map<number, WebSocket>();
  private readonly messageBudgets = new Map<number, MessageBudget>();
  private readonly appEnv: Env;
  private stateData: StoredRoomState | null = null;

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.appEnv = env;

    this.ctx.blockConcurrencyWhile(async () => {
      this.stateData =
        (await this.ctx.storage.get<StoredRoomState>(ROOM_STATE_KEY)) ?? null;
      for (const socket of this.ctx.getWebSockets()) {
        const attachment = socket.deserializeAttachment() as
          | WebSocketAttachment
          | undefined;
        if (attachment && typeof attachment.peerId === "number") {
          this.peers.set(attachment.peerId, socket);
        }
      }
    });
  }

  override async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/internal/initialize") {
      return this.initializeRoom(request);
    }

    if (!this.stateData) {
      return new Response("Room not found", { status: 404 });
    }
    if (request.headers.get("upgrade")?.toLowerCase() !== "websocket") {
      return new Response("WebSocket upgrade required", { status: 426 });
    }
    if (Date.now() >= this.stateData.created_at + ROOM_MAX_AGE_MS) {
      await this.closeRoom();
      return new Response("Room not found", { status: 404 });
    }

    const requestOwnerToken =
      request.headers.get("x-openglad-connecting-owner-token") ?? "";
    const isOwner =
      this.stateData.owner_token.length > 0 &&
      requestOwnerToken === this.stateData.owner_token;

    let supersededOwnerSocket = false;
    if (isOwner) {
      const existing = this.peers.get(OWNER_PEER_ID);
      if (existing) {
        this.peers.delete(OWNER_PEER_ID);
        this.messageBudgets.delete(OWNER_PEER_ID);
        this.tryClose(existing, 1012, "Superseded by owner reconnect");
        supersededOwnerSocket = true;
      }
    } else if (this.guestCount() >= MAX_ROOM_PEERS - 1) {
      // Guests may fill every slot except the one reserved for the owner.
      return new Response("Room is full", { status: 409 });
    }

    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];

    const peerId = isOwner ? OWNER_PEER_ID : this.stateData.next_peer_id++;
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({ peerId, isOwner } satisfies WebSocketAttachment);
    this.peers.set(peerId, server);

    if (isOwner) {
      this.stateData.host_peer_id = OWNER_PEER_ID;
      this.stateData.host_ever_connected = true;
    }
    this.stateData.empty_since = null;
    await this.persistState();
    await this.updateRegistryEntry();
    await this.scheduleAlarm();

    const hostPeerId = this.stateData.host_peer_id ?? 0;
    this.sendJson(server, {
      type: "joined",
      peer_id: peerId,
      host: hostPeerId,
    });
    this.sendJson(server, {
      type: "peer_list",
      peers: [...this.peers.keys()].sort((left, right) => left - right),
      host: hostPeerId,
    });
    if (!supersededOwnerSocket) {
      await this.broadcastJson(
        {
          type: "peer_joined",
          peer_id: peerId,
          is_host: peerId === this.stateData.host_peer_id,
        },
        peerId,
      );
    }

    return new Response(null, { status: 101, webSocket: client });
  }

  override async webSocketMessage(
    ws: WebSocket,
    message: string | ArrayBuffer,
  ): Promise<void> {
    const peerId = this.peerIdForSocket(ws);
    if (peerId === null) {
      this.tryClose(ws, 1000, "Superseded connection");
      return;
    }

    const messageBytes =
      typeof message === "string" ? message.length : message.byteLength;
    if (!this.consumeMessageBudget(peerId, messageBytes)) {
      ws.close(1008, "Message rate limit exceeded");
      return;
    }

    if (typeof message === "string") {
      if (message.length > MAX_INBOUND_TEXT_MESSAGE_BYTES) {
        ws.close(1009, "Control message too large");
        return;
      }
      await this.handleTextMessage(peerId, ws, message);
      return;
    }

    if (message.byteLength === 0) {
      return;
    }
    if (message.byteLength > MAX_INBOUND_BINARY_FRAME_BYTES) {
      ws.close(1009, "Relay frame too large");
      return;
    }

    const bytes = new Uint8Array(message);
    switch (bytes[0]) {
      case RELAY_TARGET_TAG: {
        if (bytes.byteLength < RELAY_PEER_HEADER_SIZE) {
          ws.close(1003, "Malformed relay frame");
          return;
        }
        const targetPeerId = readPeerId(bytes, 1);
        await this.forwardPayload(
          peerId,
          bytes.subarray(RELAY_PEER_HEADER_SIZE),
          targetPeerId,
        );
        return;
      }

      case RELAY_BROADCAST_TAG:
        if (bytes.byteLength > MAX_BROADCAST_FRAME_BYTES) {
          // Re-framing would push the forwarded frame past the receivers'
          // 128 KiB inbound limit.
          ws.close(1009, "Relay frame too large");
          return;
        }
        await this.forwardPayload(peerId, bytes.subarray(1));
        return;

      default:
        // Unknown tags are dropped, mirroring the tolerant reference stub.
        return;
    }
  }

  override async webSocketClose(ws: WebSocket): Promise<void> {
    const peerId = this.peerIdForSocket(ws);
    if (peerId !== null) {
      await this.removePeer(peerId);
    }
  }

  override async webSocketError(ws: WebSocket): Promise<void> {
    await this.webSocketClose(ws);
  }

  override async alarm(): Promise<void> {
    if (!this.stateData) {
      return;
    }

    const now = Date.now();
    if (now >= this.stateData.created_at + ROOM_MAX_AGE_MS) {
      await this.closeRoom();
      return;
    }
    if (
      this.peers.size === 0 &&
      this.stateData.empty_since !== null &&
      now >= this.stateData.empty_since + this.emptyRoomTtlMs()
    ) {
      await this.closeRoom();
      return;
    }
    if (
      !this.stateData.host_ever_connected &&
      now >= this.stateData.created_at + OWNER_CONNECT_GRACE_MS
    ) {
      await this.closeRoom();
      return;
    }

    // Registry heartbeat: keep the listing entry fresh while occupied.
    if (this.peers.size > 0) {
      await this.updateRegistryEntry();
    }
    await this.scheduleAlarm(now);
  }

  // -------------------------------------------------------------------------

  private async initializeRoom(request: Request): Promise<Response> {
    if (request.method !== "POST") {
      return new Response("Method not allowed", { status: 405 });
    }
    if (this.stateData) {
      return new Response("Room already initialized", { status: 409 });
    }

    let payload: InitializeRoomPayload;
    try {
      payload = (await request.json()) as InitializeRoomPayload;
    } catch {
      return new Response("Malformed room initialization payload", { status: 400 });
    }

    const roomCode = typeof payload.code === "string" ? payload.code : "";
    if (!isValidRoomCode(roomCode)) {
      return new Response("Invalid room code", { status: 400 });
    }
    const ownerToken =
      typeof payload.owner_token === "string" ? payload.owner_token : "";
    if (!ownerToken) {
      return new Response("Missing owner token", { status: 400 });
    }

    const now = Date.now();
    const seeded = makeRoomInfo({
      code: roomCode,
      campaign_hash:
        typeof payload.campaign_hash === "string" ? payload.campaign_hash : "",
      campaign_name:
        typeof payload.campaign_name === "string" ? payload.campaign_name : "",
      host_name: typeof payload.host_name === "string" ? payload.host_name : "",
      created_at: now,
    });

    this.stateData = {
      code: seeded.code,
      campaign_hash: seeded.campaign_hash,
      campaign_name: seeded.campaign_name,
      host_name: seeded.host_name,
      created_at: now,
      next_peer_id: OWNER_PEER_ID + 1,
      host_peer_id: null,
      owner_token: ownerToken,
      host_ever_connected: false,
      empty_since: now,
    };
    await this.persistState();
    await this.scheduleAlarm(now);
    return Response.json({ code: seeded.code });
  }

  private peerIdForSocket(ws: WebSocket): number | null {
    const attachment = ws.deserializeAttachment() as WebSocketAttachment | undefined;
    const peerId = attachment?.peerId;
    if (typeof peerId !== "number" || this.peers.get(peerId) !== ws) {
      return null;
    }
    return peerId;
  }

  private guestCount(): number {
    let count = 0;
    for (const peerId of this.peers.keys()) {
      if (peerId !== OWNER_PEER_ID) {
        ++count;
      }
    }
    return count;
  }

  private async handleTextMessage(
    peerId: number,
    ws: WebSocket,
    rawMessage: string,
  ): Promise<void> {
    // The C++ clients never send TEXT frames; this handling exists for
    // debugging tools and forward compatibility.
    let parsed: { type?: string };
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
        return;

      case "list_peers":
        this.sendJson(ws, {
          type: "peer_list",
          peers: [...this.peers.keys()].sort((left, right) => left - right),
          host: this.stateData?.host_peer_id ?? 0,
        });
        return;

      default:
        return;
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
      if (targetPeerId === fromPeerId) {
        return;
      }
      const socket = this.peers.get(targetPeerId);
      if (socket && !this.trySend(socket, frame)) {
        failedPeerIds.push(targetPeerId);
      }
    } else {
      for (const [peerId, socket] of this.peers) {
        if (peerId === fromPeerId) {
          continue;
        }
        if (!this.trySend(socket, frame)) {
          failedPeerIds.push(peerId);
        }
      }
    }

    for (const failedPeerId of failedPeerIds) {
      await this.removePeer(failedPeerId);
    }
  }

  private async removePeer(peerId: number): Promise<void> {
    if (!this.peers.delete(peerId) || !this.stateData) {
      return;
    }
    this.messageBudgets.delete(peerId);

    if (peerId === this.stateData.host_peer_id) {
      this.stateData.host_peer_id = null;
      if (this.peers.size > 0) {
        // The game server lives in the owner's client; without it the lobby
        // is dead. Tell the remaining peers, then close the room.
        await this.closeRoom(peerId);
        return;
      }
      // Owner left an empty room: leave it reconnectable for the grace
      // window (covers navigation hiccups between create and lobby fill).
      this.stateData.empty_since = Date.now();
      await this.persistState();
      await this.updateRegistryEntry();
      await this.scheduleAlarm();
      return;
    }

    if (this.peers.size === 0) {
      this.stateData.empty_since = Date.now();
    }
    await this.persistState();
    await this.updateRegistryEntry();
    await this.scheduleAlarm();

    // Treat peer_left as the externally observable commit signal: once a
    // remaining peer receives it, the registry must already expose the new
    // membership count.
    await this.broadcastJson({ type: "peer_left", peer_id: peerId });
  }

  /** Notify remaining peers (peer_left for `leavingPeerId`, when given), close
   *  every socket, and delete all room state — the room code 404s afterwards. */
  private async closeRoom(leavingPeerId?: number): Promise<void> {
    const remaining = [...this.peers.values()];
    this.peers.clear();
    this.messageBudgets.clear();

    for (const socket of remaining) {
      if (leavingPeerId !== undefined) {
        this.trySend(
          socket,
          JSON.stringify({ type: "peer_left", peer_id: leavingPeerId }),
        );
      }
      this.tryClose(socket, 1001, "Room closed");
    }

    if (this.stateData) {
      await this.registryFetch("/internal/remove", { code: this.stateData.code });
    }
    this.stateData = null;
    await this.ctx.storage.deleteAll();
    await this.ctx.storage.deleteAlarm();
  }

  private async broadcastJson(payload: unknown, excludedPeerId?: number): Promise<void> {
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
    for (const failedPeerId of failedPeerIds) {
      await this.removePeer(failedPeerId);
    }
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

  private tryClose(socket: WebSocket, code: number, reason: string): void {
    try {
      socket.close(code, reason);
    } catch {
      // Ignore sockets that are already closing or closed.
    }
  }

  private consumeMessageBudget(peerId: number, messageBytes: number): boolean {
    const now = Date.now();
    const budget = this.messageBudgets.get(peerId);
    if (!budget || now - budget.windowStartedAt >= MESSAGE_RATE_LIMIT_WINDOW_MS) {
      this.messageBudgets.set(peerId, {
        count: 1,
        bytes: messageBytes,
        windowStartedAt: now,
      });
      return true;
    }
    if (
      budget.count >= this.messageBudgetMaxMessages() ||
      budget.bytes + messageBytes > MESSAGE_RATE_LIMIT_MAX_BYTES
    ) {
      return false;
    }
    budget.count += 1;
    budget.bytes += messageBytes;
    return true;
  }

  private emptyRoomTtlMs(): number {
    const configured = Number(this.appEnv.EMPTY_ROOM_TTL_MS);
    if (Number.isFinite(configured) && configured > 0) {
      return configured;
    }
    return DEFAULT_EMPTY_ROOM_TTL_MS;
  }

  private messageBudgetMaxMessages(): number {
    const configured = Number(this.appEnv.MESSAGE_BUDGET_MAX_MESSAGES);
    if (Number.isFinite(configured) && configured > 0) {
      return configured;
    }
    return MESSAGE_RATE_LIMIT_MAX_MESSAGES;
  }

  private async persistState(): Promise<void> {
    if (this.stateData) {
      await this.ctx.storage.put(ROOM_STATE_KEY, this.stateData);
    }
  }

  private async updateRegistryEntry(): Promise<void> {
    if (!this.stateData) {
      return;
    }
    await this.registryFetch("/internal/upsert", {
      code: this.stateData.code,
      campaign_hash: this.stateData.campaign_hash,
      campaign_name: this.stateData.campaign_name,
      host_name: this.stateData.host_name,
      player_count: this.peers.size,
      created_at: this.stateData.created_at,
    });
  }

  private async registryFetch(path: string, payload: unknown): Promise<void> {
    const registry = this.appEnv.ROOM_REGISTRY.get(
      this.appEnv.ROOM_REGISTRY.idFromName(REGISTRY_INSTANCE_NAME),
    );
    await registry.fetch(
      new Request(`https://relay.internal${path}`, {
        method: "POST",
        headers: { "content-type": "application/json; charset=utf-8" },
        body: JSON.stringify(payload),
      }),
    );
  }

  private async scheduleAlarm(now = Date.now()): Promise<void> {
    if (!this.stateData) {
      return;
    }

    const deadlines = [this.stateData.created_at + ROOM_MAX_AGE_MS];
    if (this.peers.size === 0 && this.stateData.empty_since !== null) {
      deadlines.push(this.stateData.empty_since + this.emptyRoomTtlMs());
    }
    if (!this.stateData.host_ever_connected) {
      deadlines.push(this.stateData.created_at + OWNER_CONNECT_GRACE_MS);
    }
    if (this.peers.size > 0) {
      deadlines.push(now + REGISTRY_HEARTBEAT_MS);
    }

    await this.ctx.storage.setAlarm(Math.max(now, Math.min(...deadlines)));
  }
}
