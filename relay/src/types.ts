export interface Env {
  GAME_ROOM: DurableObjectNamespace;
  ROOM_REGISTRY: DurableObjectNamespace;
  /** Optional override (milliseconds, as a string var) for how long an empty
   *  room survives before the expiry alarm deletes it. Used by smoke.mjs to
   *  exercise the alarm path quickly; production uses the default. */
  EMPTY_ROOM_TTL_MS?: string;
  /** Optional override for the per-connection message budget (messages per
   *  window). The default (2000/s) is far above game traffic but too high for
   *  a loaded test machine to trip inside one window; tests set a small
   *  value so the flood check is timing-independent. */
  MESSAGE_BUDGET_MAX_MESSAGES?: string;
}

/** Public room description, shaped exactly for the C++ room-list parser
 *  (parse_relay_room_list in src/platform/sdl/picker_lobby_network_client.cpp):
 *  code, campaign_hash, campaign_name, host_name, player_count, created_at. */
export interface RoomInfo {
  code: string;
  campaign_hash: string;
  campaign_name: string;
  host_name: string;
  player_count: number;
  /** Milliseconds since epoch; the C++ parser reads it as a non-negative i64. */
  created_at: number;
}

/** Registry entry persisted by the RoomRegistry durable object. */
export interface RegistryEntry extends RoomInfo {
  /** Last refresh time; stale entries are pruned on read. */
  updated_at: number;
}

export interface StoredRoomState {
  code: string;
  campaign_hash: string;
  campaign_name: string;
  host_name: string;
  created_at: number;
  /** Next guest peer id. Guests count up from 2; id 1 is reserved for the
   *  room owner. */
  next_peer_id: number;
  /** The owner's peer id while the owner is connected, null otherwise. This
   *  is what the control messages advertise as "host". */
  host_peer_id: number | null;
  owner_token: string;
  host_ever_connected: boolean;
  /** Set while the room has zero connected sockets; drives empty-room expiry. */
  empty_since: number | null;
}

export interface WebSocketAttachment {
  peerId: number;
  isOwner: boolean;
}
