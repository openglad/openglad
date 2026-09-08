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

    // Archived v2-<n> builds each ship the version list as it stood on their
    // own deploy day. They refresh it by fetching production's index.json
    // cross-origin, so that one file — and nothing else — is readable from
    // any origin.
    if (url.pathname === '/versions/index.json') {
      const res = await env.ASSETS.fetch(request);
      const headers = new Headers(res.headers);
      headers.set('Access-Control-Allow-Origin', '*');
      return new Response(res.body, { status: res.status, headers });
    }

    return env.ASSETS.fetch(request);
  },
};
