export interface Env {
  GAME_ROOM: DurableObjectNamespace;
  ROOM_INDEX: KVNamespace;
}

export interface RoomInfo {
  code: string;
  campaign_hash: string;
  campaign_name: string;
  host_name: string;
  player_count: number;
  created_at: number;
}

export interface StoredRoomState {
  code: string;
  campaign_hash: string;
  campaign_name: string;
  host_name: string;
  created_at: number;
  next_peer_id: number;
  host_peer_id: number | null;
}

export interface WebSocketAttachment {
  peerId: number;
}
