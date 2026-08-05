#!/usr/bin/env bash
#
# Keep the local web preview (fx-review site + playable wasm build) current.
#
# What it does, idempotently:
#   1. Rebuilds the Emscripten play build from the current tree and copies it
#      into the served site directory (build/fx-review/site/play/).
#   2. With --full, also regenerates the whole fx-review site first
#      (scripts/fx_review/generate.sh — capture scenes + cards; slow).
#   3. Ensures the local HTTP server (port ${OG_PREVIEW_PORT:-8790}) and the
#      Cloudflare quick tunnel are running, and prints the public URL.
#
# Toolchain: uses $EMSDK if set, else ~/emsdk. The nix dev shell provides
# cmake/ninja; this script re-execs itself inside `nix develop` when cmake
# is not already on PATH, so it works from a bare shell and from git hooks.
#
# Staleness guard: scripts/build_web.sh stamps dist/; the post-commit hook
# (installed by --install-hook) runs this script detached on every commit so
# the preview tracks the last commit without blocking the committer.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SITE_DIR="$REPO_ROOT/build/fx-review/site"
PORT="${OG_PREVIEW_PORT:-8790}"
LOG_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/openglad-preview"
mkdir -p "$LOG_DIR"

if [[ "${1:-}" == "--install-hook" ]]; then
    HOOK="$REPO_ROOT/.git/hooks/post-commit"
    cat > "$HOOK" << 'HOOKEOF'
#!/usr/bin/env bash
# Refresh the local web preview in the background after every commit.
# Log: ~/.local/state/openglad-preview/refresh.log
#
# The log directory is created HERE, not in refresh_web_preview.sh: the shell
# evaluates the redirect below before the script runs, so the script's own
# mkdir is too late to create its own log file's parent. Without this, every
# commit prints "No such file or directory" and the preview never refreshes.
LOG_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/openglad-preview"
mkdir -p "$LOG_DIR"
nohup "$(git rev-parse --show-toplevel)/scripts/refresh_web_preview.sh" \
    >> "$LOG_DIR/refresh.log" 2>&1 &
HOOKEOF
    chmod +x "$HOOK"
    echo "post-commit hook installed: $HOOK"
    exit 0
fi

# Re-exec inside the nix dev shell if cmake is missing (hook/bare-shell path).
if ! command -v cmake > /dev/null 2>&1; then
    exec nix develop "$REPO_ROOT" -c "$0" "$@"
fi

echo "=== refresh_web_preview $(date -u '+%F %T') @ $(git -C "$REPO_ROOT" rev-parse --short HEAD) ==="

# Emscripten toolchain (durable copy; see docs).
if ! command -v emcmake > /dev/null 2>&1; then
    EMSDK_DIR="${EMSDK:-$HOME/emsdk}"
    if [[ ! -f "$EMSDK_DIR/emsdk_env.sh" ]]; then
        echo "ERROR: no emsdk at $EMSDK_DIR (set \$EMSDK)" >&2
        exit 1
    fi
    # shellcheck disable=SC1091
    source "$EMSDK_DIR/emsdk_env.sh" > /dev/null 2>&1
fi

if [[ "${1:-}" == "--full" ]]; then
    "$SCRIPT_DIR/fx_review/generate.sh"
fi

"$SCRIPT_DIR/build_web.sh"
mkdir -p "$SITE_DIR/play"
cp "$REPO_ROOT"/dist/play.html "$REPO_ROOT"/dist/play.js \
   "$REPO_ROOT"/dist/play.wasm "$REPO_ROOT"/dist/play.data "$SITE_DIR/play/"

# Cache buster, injected at DEPLOY time so the committed shell stays clean:
# version the play.js include AND give Emscripten a locateFile so the
# play.wasm/play.data fetches carry the same ?v= (busting play.js alone
# leaves the wasm cached). Idempotent: the cp above always starts from the
# pristine dist/ copy.
SHA="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"
sed -i "s|src=\"play.js\"|src=\"play.js?v=$SHA\"|" "$SITE_DIR/play/play.html"
sed -i "s|var Module = {|var Module = { locateFile: function(path, prefix) { return prefix + path + '?v=$SHA'; },|" \
    "$SITE_DIR/play/play.html"

# Ensure the HTTP server is up — with Cache-Control: no-store, which also
# keeps the Cloudflare tunnel edge from caching stale assets.
SERVE_PY="$LOG_DIR/serve_nocache.py"
cat > "$SERVE_PY" << 'PYEOF'
import functools, http.server, sys
class NoStore(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, must-revalidate')
        super().end_headers()
    def log_message(self, *a):
        pass
port, root = int(sys.argv[1]), sys.argv[2]
http.server.ThreadingHTTPServer(
    ('127.0.0.1', port),
    functools.partial(NoStore, directory=root)).serve_forever()
PYEOF
if ! curl -sf -o /dev/null -w '%{http_code}' "http://localhost:$PORT/index.html" \
   || ! curl -sfI "http://localhost:$PORT/index.html" | grep -qi 'no-store'; then
    pkill -f "http.server.*$PORT" 2>/dev/null || true
    pkill -f "serve_nocache.py $PORT" 2>/dev/null || true
    sleep 1
    setsid nohup python3 "$SERVE_PY" "$PORT" "$SITE_DIR" \
        >> "$LOG_DIR/http.log" 2>&1 < /dev/null &
    disown || true
    sleep 1
fi

# Ensure the tunnel is up; reuse the recorded URL when alive. url.txt is the
# durable record (grep on a missing/foreign log must never kill the script —
# pipefail + set -e made exactly that mistake once).
TUNNEL_LOG="$LOG_DIR/tunnel.log"
URL_FILE="$LOG_DIR/url.txt"
tunnel_url() {
    { grep -oE 'https://[a-z0-9-]+\.trycloudflare\.com' "$TUNNEL_LOG" 2>/dev/null || true; } | tail -1
}
if ! pgrep -f 'cloudflared.*tunnel' > /dev/null 2>&1; then
    : > "$TUNNEL_LOG"
    rm -f "$URL_FILE"
    setsid nohup nix run nixpkgs#cloudflared -- tunnel --url "http://localhost:$PORT" \
        >> "$TUNNEL_LOG" 2>&1 < /dev/null &
    disown || true
    for _ in $(seq 1 30); do
        [[ -n "$(tunnel_url)" ]] && break
        sleep 2
    done
    [[ -n "$(tunnel_url)" ]] && tunnel_url > "$URL_FILE"
fi

URL="$(cat "$URL_FILE" 2>/dev/null || true)"
[[ -z "$URL" ]] && URL="$(tunnel_url)"
echo "preview refreshed: http://localhost:$PORT/  ${URL:+| $URL}"
