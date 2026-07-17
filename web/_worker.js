// Cloudflare Pages Advanced Mode worker: serves the static game and mounts
// the multiplayer relay under /relay on the SAME origin.
//
// /relay and /relay/* are forwarded over a service binding (RELAY -> the
// `openglad-relay` Worker, which owns the Durable Objects; Pages projects
// cannot host DOs themselves) with the /relay prefix stripped, so
//   https://openglad.pages.dev/relay/api/create
// reaches the relay as /api/create. Service bindings carry WebSocket
// upgrades, so /relay/api/room/<CODE> sockets work through here too.
// Everything else falls through to the static assets.
export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === '/relay' || url.pathname.startsWith('/relay/')) {
      if (!env.RELAY) {
        return new Response(
          'Relay service binding is not configured on this Pages project.',
          { status: 503 },
        );
      }
      const stripped = url.pathname.slice('/relay'.length) || '/';
      const target = new URL(request.url);
      target.pathname = stripped;
      return env.RELAY.fetch(new Request(target, request));
    }

    return env.ASSETS.fetch(request);
  },
};
