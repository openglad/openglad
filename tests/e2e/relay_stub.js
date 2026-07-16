// @ts-check
'use strict';

// Minimal in-process relay stub for the browser networking e2e tests.
//
// It implements the protocol subset that the web HOST/JOIN flows exercise
// (see src/platform/sdl/picker_lobby_network_client.cpp create_relay_room /
// build_relay_room_create_url / build_relay_room_websocket_url and
// src/platform/emscripten/net_transport_relay_ws.cpp):
//
//   1. POST /api/create?campaign=...&campaign_name=...&host=...
//        -> 200 JSON {"room_code": "...", "owner_token": "..."}
//      (CORS headers required: the page origin is the http-server on :8089.)
//   2. WebSocket upgrade on /api/room/<CODE>[?owner_token=...]
//        -> RFC6455 accept handshake (hand-rolled; no ws dependency in
//           node_modules) for the single known room. Each accepted socket is
//           assigned an incrementing peer id; the first socket presenting the
//           correct owner_token becomes the room host. Control TEXT frames
//           match what RelayWebSocketTransport::process_control_message
//           parses (field name is peer_id, and a numeric host of 0 means
//           "no host yet" and is ignored by the client):
//              to the new socket:
//                {"type":"joined","peer_id":N,"host":H}
//                {"type":"peer_list","peers":[...],"host":H}
//              to everyone else:  {"type":"peer_joined","peer_id":N,"is_host":B}
//              on close:          {"type":"peer_left","peer_id":N}
//      Unknown room codes are, per `unknownRoomBehavior`:
//        'reject' (default) -> refuse the upgrade with HTTP 404, like a real
//                              relay: the browser never fires the WS open
//                              event, so the transport fails before EVER
//                              connecting.
//        'hold'             -> never answer the upgrade: the browser WebSocket
//                              stays in CONNECTING (a stalled relay).
//   3. BINARY data frames are decoded per the kRelay*Tag framing and FORWARDED
//      between peers:
//        client->relay [0x01][target peer u32 LE][body]  (send-to-peer)
//        client->relay [0x03][body]                      (broadcast)
//        relay->client [0x02][sender peer u32 LE][body]  (receive-from-peer)
//      Every forwarded frame is recorded in `forwardedFrames` so the tests
//      can assert that lobby traffic really flowed host<->joiner.
//   4. GET /api/rooms -> 200 [] (defensive; the JOIN room-list flow).
//
// Known limitations (fine for these tests): a single fixed room, no
// WebSocket fragmentation/continuation support, and no permessage-deflate.

const crypto = require('crypto');
const http = require('http');

const WEBSOCKET_ACCEPT_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const OPCODE_TEXT = 0x1;
const OPCODE_BINARY = 0x2;
const OPCODE_CLOSE = 0x8;
const OPCODE_PING = 0x9;
const OPCODE_PONG = 0xa;

// Relay data-frame tag bytes; must match the kRelay*Tag constants in
// src/platform/emscripten/net_transport_relay_ws.cpp (and its native twin).
const RELAY_SEND_TO_PEER_TAG = 1;
const RELAY_RECEIVE_FROM_PEER_TAG = 2;
const RELAY_BROADCAST_TAG = 3;
const RELAY_PEER_HEADER_SIZE = 5;

const CORS_HEADERS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': '*',
};

function encodeFrame(opcode, payload) {
  let header;
  if (payload.length < 126) {
    header = Buffer.from([0x80 | opcode, payload.length]);
  } else if (payload.length < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | opcode;
    header[1] = 126;
    header.writeUInt16BE(payload.length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(payload.length), 2);
  }
  return Buffer.concat([header, payload]);
}

function encodeTextFrame(text) {
  return encodeFrame(OPCODE_TEXT, Buffer.from(text, 'utf8'));
}

function encodeCloseFrame() {
  const payload = Buffer.alloc(2);
  payload.writeUInt16BE(1000, 0);
  return encodeFrame(OPCODE_CLOSE, payload);
}

function encodeU32LE(value) {
  const buffer = Buffer.alloc(4);
  buffer.writeUInt32LE(value >>> 0, 0);
  return buffer;
}

// Decode one client frame from `buffer`. Returns null while incomplete.
function decodeFrame(buffer) {
  if (buffer.length < 2) {
    return null;
  }
  const opcode = buffer[0] & 0x0f;
  const masked = (buffer[1] & 0x80) !== 0;
  let payloadLength = buffer[1] & 0x7f;
  let offset = 2;
  if (payloadLength === 126) {
    if (buffer.length < 4) {
      return null;
    }
    payloadLength = buffer.readUInt16BE(2);
    offset = 4;
  } else if (payloadLength === 127) {
    if (buffer.length < 10) {
      return null;
    }
    const bigLength = buffer.readBigUInt64BE(2);
    if (bigLength > BigInt(Number.MAX_SAFE_INTEGER)) {
      throw new Error(`relay stub: unreasonable frame length ${bigLength}`);
    }
    payloadLength = Number(bigLength);
    offset = 10;
  }
  let maskKey = null;
  if (masked) {
    if (buffer.length < offset + 4) {
      return null;
    }
    maskKey = buffer.subarray(offset, offset + 4);
    offset += 4;
  }
  if (buffer.length < offset + payloadLength) {
    return null;
  }
  const payload = Buffer.from(buffer.subarray(offset, offset + payloadLength));
  if (maskKey) {
    for (let i = 0; i < payload.length; ++i) {
      payload[i] ^= maskKey[i % 4];
    }
  }
  return { opcode, payload, rest: buffer.subarray(offset + payloadLength) };
}

class RelayStub {
  constructor(options = {}) {
    this.roomCode = options.roomCode || 'GLAD-E2E1';
    this.ownerToken = options.ownerToken || 'tok-e2e-owner';
    /** Unknown-room upgrade handling: 'reject' (HTTP 404) or 'hold' (never
     *  answer). Mutable at runtime so a test can exercise both shapes. */
    this.unknownRoomBehavior = options.unknownRoomBehavior || 'reject';
    this.port = 0;

    /** HTTP room-create requests, as {pathname, params}. */
    this.createRequests = [];
    /** Accepted room WebSocket connections, as
     *  {roomCode, ownerToken, peerId, closed, receivedFrames, sentControls,
     *  close()}. `sentControls` records every control TEXT message the stub
     *  successfully wrote to that peer (e.g. the owner's peer_joined
     *  notification), so tests can assert protocol facts without pixels. */
    this.roomConnections = [];
    /** Upgrades refused with HTTP 404 (unknown room), as {roomCode}. */
    this.rejectedUpgrades = [];
    /** Upgrades deliberately left unanswered (unknown room), as {roomCode}. */
    this.heldUpgrades = [];
    /** Data frames forwarded between peers, as
     *  {from, to, kind: 'send'|'broadcast', byteLength}. */
    this.forwardedFrames = [];
    /** Binary frames that could not be routed, as {from, to?, reason}. */
    this.droppedFrames = [];

    /** @type {Map<number, {socket: import('net').Socket, connection: any}>} */
    this._peers = new Map();
    this._nextPeerId = 1;
    this._hostPeerId = null;

    this._sockets = new Set();
    this._server = http.createServer((req, res) => this._handleRequest(req, res));
    this._server.on('upgrade', (req, socket) => this._handleUpgrade(req, socket));
  }

  get baseUrl() {
    return `http://127.0.0.1:${this.port}`;
  }

  /** Count of data frames forwarded from one peer to another. */
  forwardedFrameCount(fromPeerId, toPeerId) {
    return this.forwardedFrames.filter(
      (frame) => frame.from === fromPeerId && frame.to === toPeerId,
    ).length;
  }

  async start() {
    await new Promise((resolve, reject) => {
      this._server.once('error', reject);
      this._server.listen(0, '127.0.0.1', () => {
        this._server.removeListener('error', reject);
        resolve(undefined);
      });
    });
    const address = this._server.address();
    if (!address || typeof address === 'string') {
      throw new Error('relay stub: server address is unavailable');
    }
    this.port = address.port;
    return this.port;
  }

  async stop() {
    for (const socket of this._sockets) {
      socket.destroy();
    }
    this._sockets.clear();
    this._peers.clear();
    if (typeof this._server.closeAllConnections === 'function') {
      this._server.closeAllConnections();
    }
    await new Promise((resolve) => this._server.close(() => resolve(undefined)));
  }

  _handleRequest(req, res) {
    const url = new URL(req.url || '/', this.baseUrl);
    if (req.method === 'OPTIONS') {
      res.writeHead(204, CORS_HEADERS);
      res.end();
      return;
    }
    if (req.method === 'POST' && url.pathname === '/api/create') {
      this.createRequests.push({
        pathname: url.pathname,
        params: Object.fromEntries(url.searchParams),
      });
      res.writeHead(200, { ...CORS_HEADERS, 'Content-Type': 'application/json' });
      res.end(
        JSON.stringify({ room_code: this.roomCode, owner_token: this.ownerToken }),
      );
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/rooms') {
      res.writeHead(200, { ...CORS_HEADERS, 'Content-Type': 'application/json' });
      res.end('[]');
      return;
    }
    res.writeHead(404, CORS_HEADERS);
    res.end('relay stub: not found');
  }

  _handleUpgrade(req, socket) {
    const url = new URL(req.url || '/', this.baseUrl);
    const roomMatch = url.pathname.match(/^\/api\/room\/([^/]+)$/);
    const key = req.headers['sec-websocket-key'];
    if (!roomMatch || typeof key !== 'string') {
      socket.write('HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n');
      socket.destroy();
      return;
    }

    const roomCode = decodeURIComponent(roomMatch[1]);
    if (roomCode !== this.roomCode) {
      if (this.unknownRoomBehavior === 'hold') {
        // A stalled relay: never answer the upgrade, so the browser-side
        // WebSocket stays in CONNECTING until the client tears it down.
        this.heldUpgrades.push({ roomCode });
        this._sockets.add(socket);
        const forget = () => this._sockets.delete(socket);
        socket.on('close', forget);
        socket.on('error', forget);
        socket.on('data', () => {});
        return;
      }
      // A real relay rejects unknown rooms: refuse the upgrade with an HTTP
      // error, so the browser never fires the WebSocket open event and the
      // transport fails before EVER connecting.
      this.rejectedUpgrades.push({ roomCode });
      socket.write('HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n');
      socket.destroy();
      return;
    }

    const accept = crypto
      .createHash('sha1')
      .update(key + WEBSOCKET_ACCEPT_GUID)
      .digest('base64');
    socket.write(
      'HTTP/1.1 101 Switching Protocols\r\n' +
        'Upgrade: websocket\r\n' +
        'Connection: Upgrade\r\n' +
        `Sec-WebSocket-Accept: ${accept}\r\n` +
        '\r\n',
    );

    const peerId = this._nextPeerId++;
    const ownerToken = url.searchParams.get('owner_token');
    if (ownerToken === this.ownerToken && this._hostPeerId === null) {
      this._hostPeerId = peerId;
    }

    const connection = {
      roomCode,
      ownerToken,
      peerId,
      closed: false,
      receivedFrames: [],
      sentControls: [],
      // Server-initiated close, used to simulate a relay dropping a peer.
      close: () => {
        if (connection.closed || socket.destroyed) {
          return;
        }
        this._writeRaw(socket, encodeCloseFrame());
        socket.end();
      },
    };
    this.roomConnections.push(connection);
    this._sockets.add(socket);
    this._peers.set(peerId, { socket, connection });

    const teardown = () => {
      if (connection.closed) {
        return;
      }
      connection.closed = true;
      this._sockets.delete(socket);
      const registered = this._peers.get(peerId);
      if (registered && registered.socket === socket) {
        this._peers.delete(peerId);
        this._sendControlToOthers(peerId, {
          type: 'peer_left',
          peer_id: peerId,
        });
      }
    };
    socket.on('close', teardown);
    socket.on('error', teardown);

    // Initial control handshake: identify the new socket and hand it the
    // full peer list (which includes itself; the transport filters that
    // out). RelayWebSocketTransport flips `connected` on the socket open and
    // fills local/host peer ids from these two messages.
    this._sendControl(socket, connection, {
      type: 'joined',
      peer_id: peerId,
      host: this._hostPeerId || 0,
    });
    this._sendControl(socket, connection, {
      type: 'peer_list',
      peers: [...this._peers.keys()],
      host: this._hostPeerId || 0,
    });
    this._sendControlToOthers(peerId, {
      type: 'peer_joined',
      peer_id: peerId,
      is_host: peerId === this._hostPeerId,
    });

    let inbound = Buffer.alloc(0);
    socket.on('data', (chunk) => {
      inbound = Buffer.concat([inbound, chunk]);
      try {
        for (;;) {
          const frame = decodeFrame(inbound);
          if (!frame) {
            break;
          }
          inbound = frame.rest;
          connection.receivedFrames.push({
            opcode: frame.opcode,
            byteLength: frame.payload.length,
          });
          if (frame.opcode === OPCODE_CLOSE) {
            this._writeRaw(socket, encodeCloseFrame());
            socket.end();
            teardown();
            return;
          }
          if (frame.opcode === OPCODE_PING) {
            this._writeRaw(socket, encodeFrame(OPCODE_PONG, frame.payload));
            continue;
          }
          if (frame.opcode === OPCODE_BINARY) {
            this._routeBinaryFrame(peerId, frame.payload);
          }
          // TEXT frames from clients carry no relay semantics; they are
          // recorded above and otherwise ignored.
        }
      } catch (error) {
        socket.destroy();
        teardown();
      }
    });
  }

  _routeBinaryFrame(senderPeerId, payload) {
    if (payload.length === 0) {
      this.droppedFrames.push({ from: senderPeerId, reason: 'empty-frame' });
      return;
    }
    const tag = payload[0];
    if (tag === RELAY_SEND_TO_PEER_TAG) {
      if (payload.length < RELAY_PEER_HEADER_SIZE) {
        this.droppedFrames.push({
          from: senderPeerId,
          reason: 'short-send-header',
        });
        return;
      }
      const targetPeerId = payload.readUInt32LE(1);
      this._forwardTo(
        senderPeerId,
        targetPeerId,
        payload.subarray(RELAY_PEER_HEADER_SIZE),
        'send',
      );
      return;
    }
    if (tag === RELAY_BROADCAST_TAG) {
      const body = payload.subarray(1);
      for (const peerId of this._peers.keys()) {
        if (peerId !== senderPeerId) {
          this._forwardTo(senderPeerId, peerId, body, 'broadcast');
        }
      }
      return;
    }
    this.droppedFrames.push({
      from: senderPeerId,
      reason: `unknown-tag-${tag}`,
    });
  }

  _forwardTo(fromPeerId, toPeerId, body, kind) {
    const target = this._peers.get(toPeerId);
    if (!target || toPeerId === fromPeerId) {
      this.droppedFrames.push({
        from: fromPeerId,
        to: toPeerId,
        kind,
        reason: 'no-such-peer',
      });
      return;
    }
    const relayed = Buffer.concat([
      Buffer.from([RELAY_RECEIVE_FROM_PEER_TAG]),
      encodeU32LE(fromPeerId),
      body,
    ]);
    if (this._writeRaw(target.socket, encodeFrame(OPCODE_BINARY, relayed))) {
      this.forwardedFrames.push({
        from: fromPeerId,
        to: toPeerId,
        kind,
        byteLength: body.length,
      });
    }
  }

  _sendControl(socket, connection, message) {
    const written = this._writeRaw(socket, encodeTextFrame(JSON.stringify(message)));
    if (written && connection) {
      connection.sentControls.push(message);
    }
    return written;
  }

  _sendControlToOthers(exceptPeerId, message) {
    for (const [peerId, peer] of this._peers) {
      if (peerId !== exceptPeerId) {
        this._sendControl(peer.socket, peer.connection, message);
      }
    }
  }

  _writeRaw(socket, frameBuffer) {
    if (socket.destroyed || socket.writableEnded) {
      return false;
    }
    try {
      socket.write(frameBuffer);
      return true;
    } catch (error) {
      return false;
    }
  }
}

module.exports = { RelayStub };
