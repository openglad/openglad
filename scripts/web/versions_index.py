#!/usr/bin/env python3
"""Generate the ``/versions`` index shipped with the Openglad web build.

Openglad versions are ``2.<commit count>``: every push to master is deployed to
Cloudflare Pages twice, once at the stable archive alias
``https://v2-<count>.openglad.pages.dev`` and once at production
``https://openglad.pages.dev``. Nothing is ever rebuilt to keep an old version
playable, so the only authority on "which versions are still up" is the
Cloudflare Pages deployment list itself. This script reads that list, resolves
each deployment's commit back to a version number with ``git rev-list --count``,
and writes ``index.json`` + ``index.html`` into the deploy directory.

The generator runs on EVERY wasm-e2e run: ``--live`` on the master production
gate, ``--offline`` everywhere else, which emits a page describing only the
build that produced it. A page therefore always exists, and pull-request
previews get a ``/versions/`` that honestly says what they are.

Failure to reach the API is never fatal: the script warns, falls back to the
current-only page, and exits 0. A broken index must not red a deploy.

Standard library only (no jq, no requests) - this runs on a bare CI image.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import html
import json
import os
import re
import subprocess
import sys
import urllib.request
from typing import Any, Iterable, Optional

API_BASE = "https://api.cloudflare.com/client/v4"
REPO_URL = "https://github.com/openglad/openglad"
PRODUCTION_URL = "https://openglad.pages.dev"
LIVE_INDEX_URL = "https://openglad.pages.dev/versions/index.json"

# Cloudflare aliases the synthetic branch v2-<count> to this host. Only these
# and production deployments belong in the index; pr-<n> previews are transient
# and are deliberately excluded.
ALIAS_RE = re.compile(r"^https://v2-(\d+)\.openglad\.pages\.dev$")

HTTP_TIMEOUT_SECONDS = 20
PER_PAGE = 25
MAX_PAGES = 60


# --------------------------------------------------------------------------
# Deployment listing
# --------------------------------------------------------------------------


def fetch_deployments(project: str, account_id: str, token: str) -> list[dict[str, Any]]:
    """Return every Pages deployment for ``project``, newest first.

    Pages returns pages of ``result`` entries; an empty page ends the listing.
    The page cap bounds a runaway/looping API rather than trusting the server.
    """
    deployments: list[dict[str, Any]] = []
    for page in range(1, MAX_PAGES + 1):
        url = (
            f"{API_BASE}/accounts/{account_id}/pages/projects/{project}"
            f"/deployments?page={page}&per_page={PER_PAGE}"
        )
        request = urllib.request.Request(
            url,
            headers={
                "Authorization": f"Bearer {token}",
                "Accept": "application/json",
            },
        )
        with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
            payload = json.loads(response.read().decode("utf-8"))
        if not payload.get("success", True):
            raise RuntimeError("Cloudflare API reported failure listing deployments")
        batch = payload.get("result") or []
        if not batch:
            break
        deployments.extend(batch)
    return deployments


def load_deployments(path: str) -> list[dict[str, Any]]:
    """Read a saved API listing (``{"result": [...]}`` or a bare list)."""
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
    if isinstance(payload, list):
        return payload
    return payload.get("result") or []


# --------------------------------------------------------------------------
# Row construction
# --------------------------------------------------------------------------


def alias_url_of(deployment: dict[str, Any]) -> Optional[str]:
    """The v2-<count> archive alias of a deployment, if it has one."""
    for alias in deployment.get("aliases") or []:
        if isinstance(alias, str) and ALIAS_RE.match(alias):
            return alias
    return None


def succeeded(deployment: dict[str, Any]) -> bool:
    """False when the API says this deployment's last stage did not succeed.

    Cloudflare lists failed and cancelled deployments alongside the good ones,
    and their ``url`` serves nothing - indexing them would put dead "archived
    build" links on the page. A listing with no ``latest_stage.status`` at all
    is trusted (older API shapes), so a missing field never empties the index.
    """
    stage = deployment.get("latest_stage") or {}
    status = stage.get("status")
    if not isinstance(status, str) or not status:
        return True
    return status == "success"


def is_indexable(deployment: dict[str, Any]) -> bool:
    """Production deployments and archive aliases only - pr-<n> previews out."""
    if not succeeded(deployment):
        return False
    if deployment.get("environment") == "production":
        return True
    return alias_url_of(deployment) is not None


def commit_hash_of(deployment: dict[str, Any]) -> Optional[str]:
    trigger = deployment.get("deployment_trigger") or {}
    metadata = trigger.get("metadata") or {}
    commit = metadata.get("commit_hash")
    if isinstance(commit, str) and commit.strip():
        return commit.strip()
    return None


def created_date(deployment: dict[str, Any]) -> Optional[str]:
    """``created_on`` as ``YYYY-MM-DD`` (UTC); ``None`` when unparseable."""
    raw = deployment.get("created_on")
    if not isinstance(raw, str) or not raw:
        return None
    text = raw.replace("Z", "+00:00")
    try:
        stamp = _dt.datetime.fromisoformat(text)
    except ValueError:
        return raw[:10] if len(raw) >= 10 else None
    if stamp.tzinfo is not None:
        stamp = stamp.astimezone(_dt.timezone.utc)
    return stamp.date().isoformat()


class CommitCounter:
    """Resolves a commit hash to its ``git rev-list --count``.

    ``--no-git`` swaps the git call for a committed JSON map so the unit tests
    never depend on the checkout's own history.
    """

    def __init__(self, repo_root: str, use_git: bool = True,
                 counts: Optional[dict[str, int]] = None) -> None:
        self.repo_root = repo_root
        self.use_git = use_git
        self.counts = counts or {}
        self._cache: dict[str, Optional[int]] = {}

    def count(self, commit: str) -> Optional[int]:
        if commit in self._cache:
            return self._cache[commit]
        result: Optional[int] = None
        for key, value in self.counts.items():
            if key.startswith(commit) or commit.startswith(key):
                result = int(value)
                break
        if result is None and self.use_git:
            result = self._git_count(commit)
        self._cache[commit] = result
        return result

    def _git_count(self, commit: str) -> Optional[int]:
        try:
            completed = subprocess.run(
                ["git", "-C", self.repo_root, "rev-list", "--count", commit],
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            return None
        if completed.returncode != 0:
            return None
        text = completed.stdout.strip()
        return int(text) if text.isdigit() else None


def make_row(commit: Optional[str], minor: Optional[int], date: Optional[str],
             alias_url: Optional[str], immutable_url: Optional[str],
             current: bool = False) -> dict[str, Any]:
    return {
        "version": None if minor is None else f"2.{minor}",
        "minor": minor,
        "commit": commit,
        "short_commit": commit[:8] if commit else None,
        "commit_url": f"{REPO_URL}/commit/{commit}" if commit else None,
        "date": date,
        "alias_url": alias_url,
        "immutable_url": immutable_url,
        "current": current,
    }


def group_deployments(deployments: Iterable[dict[str, Any]],
                      counter: CommitCounter) -> list[dict[str, Any]]:
    """Collapse the deployment list into one row per commit.

    A master push produces two deployments of the identical ``dist/`` (the
    archive alias and production), so the commit - not the deployment - is the
    unit a player cares about. Deployments with no commit metadata keep a row of
    their own with a null version, rather than vanishing from the index.
    """
    groups: dict[str, dict[str, Any]] = {}
    order: list[str] = []
    for deployment in deployments:
        if not is_indexable(deployment):
            continue
        commit = commit_hash_of(deployment)
        key = commit if commit else f"deployment:{deployment.get('id')}"
        group = groups.get(key)
        if group is None:
            group = {
                "commit": commit,
                "date": None,
                "alias_url": None,
                "production_url": None,
                "any_url": None,
            }
            groups[key] = group
            order.append(key)
        date = created_date(deployment)
        if date and (group["date"] is None or date < group["date"]):
            group["date"] = date
        alias = alias_url_of(deployment)
        if alias and not group["alias_url"]:
            group["alias_url"] = alias
        url = deployment.get("url")
        if isinstance(url, str) and url:
            if deployment.get("environment") == "production" and not group["production_url"]:
                group["production_url"] = url
            if not group["any_url"]:
                group["any_url"] = url

    rows: list[dict[str, Any]] = []
    for key in order:
        group = groups[key]
        commit = group["commit"]
        minor = counter.count(commit) if commit else None
        rows.append(
            make_row(
                commit=commit,
                minor=minor,
                date=group["date"],
                alias_url=group["alias_url"],
                immutable_url=group["production_url"] or group["any_url"],
            )
        )
    return rows


def minor_of_version(version: str) -> Optional[int]:
    match = re.match(r"^\d+\.(\d+)$", version.strip())
    return int(match.group(1)) if match else None


def build_rows(deployments: list[dict[str, Any]], counter: CommitCounter,
               current_version: str, current_commit: str,
               current_alias: str) -> list[dict[str, Any]]:
    """Current build first, then numbered versions newest-first, unknowns last."""
    rows = group_deployments(deployments, counter)

    current_immutable: Optional[str] = None
    current_date: Optional[str] = None
    remaining: list[dict[str, Any]] = []
    for row in rows:
        if row["commit"] and current_commit and (
            row["commit"].startswith(current_commit) or current_commit.startswith(row["commit"])
        ):
            current_immutable = current_immutable or row["immutable_url"]
            current_date = current_date or row["date"]
            continue
        remaining.append(row)

    current_row = make_row(
        commit=current_commit or None,
        minor=minor_of_version(current_version),
        date=current_date or _dt.datetime.now(_dt.timezone.utc).date().isoformat(),
        alias_url=f"https://{current_alias}.openglad.pages.dev" if current_alias else None,
        immutable_url=current_immutable,
        current=True,
    )
    if current_row["version"] is None and current_version:
        current_row["version"] = current_version

    numbered = [row for row in remaining if row["minor"] is not None]
    unknown = [row for row in remaining if row["minor"] is None]
    numbered.sort(key=lambda row: row["minor"], reverse=True)
    unknown.sort(key=lambda row: row["date"] or "", reverse=True)
    return [current_row] + numbered + unknown


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------

# The page borrows web/index.html's palette and typography so an archived
# version list looks like part of the site rather than a CI artifact.
PAGE_CSS = """
        * { margin: 0; padding: 0; box-sizing: border-box; }

        html, body {
            width: 100%;
            background-color: #000;
            color: #fff;
            font-family: Arial, sans-serif;
        }

        body {
            display: flex;
            flex-direction: column;
            min-height: 100vh;
            background: linear-gradient(180deg, #4a5f45 0%, #34452f 50%, #24331f 100%);
        }

        main {
            flex: 1;
            width: 100%;
            max-width: 52rem;
            margin: 0 auto;
            padding: 2.5rem 1.25rem;
        }

        h1 {
            font-size: 2.25rem;
            margin-bottom: 1rem;
            text-transform: uppercase;
            letter-spacing: 0.1em;
        }

        p { line-height: 1.6; margin-bottom: 1.25rem; }

        a { color: #f5e6c4; }
        a:hover { color: #fff; }

        table {
            width: 100%;
            border-collapse: collapse;
            background: rgba(0, 0, 0, 0.35);
            font-size: 0.95rem;
        }

        th, td {
            padding: 0.55rem 0.75rem;
            text-align: left;
            border-bottom: 1px solid rgba(245, 230, 196, 0.2);
        }

        th {
            text-transform: uppercase;
            letter-spacing: 0.08em;
            font-size: 0.75rem;
            color: #d9a441;
        }

        tr.current td { background: rgba(217, 164, 65, 0.12); }

        code { font-family: "Courier New", monospace; }

        .badge {
            margin-left: 0.4rem;
            padding: 0.05rem 0.4rem;
            font-size: 0.7rem;
            text-transform: uppercase;
            letter-spacing: 0.06em;
            color: #000;
            background: #d9a441;
            border-radius: 2px;
        }

        .note {
            margin-top: 1.25rem;
            padding: 1rem 1.25rem;
            font-size: 0.9rem;
            color: #f5e6c4;
            background: rgba(0, 0, 0, 0.35);
            border: 1px solid #d9a441;
            border-left-width: 4px;
            border-radius: 4px;
        }

        footer {
            padding: 1.5rem;
            text-align: center;
            font-size: 0.85rem;
            color: #888;
            line-height: 1.6;
        }

        footer a { color: #aaa; }
        footer a:hover { color: #fff; }
"""

LIVE_REFRESH_JS = """
    (function () {
      var LIVE_INDEX = '__LIVE_INDEX_URL__';
      function esc(value) {
        return String(value).replace(/[&<>"']/g, function (c) {
          return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
        });
      }
      function cells(row) {
        var version = row.version ? esc(row.version) : 'unknown';
        if (row.current) { version += ' <span class="badge">current</span>'; }
        var commit = row.commit_url
          ? '<a href="' + esc(row.commit_url) + '"><code>' + esc(row.short_commit || row.commit) + '</code></a>'
          : '&mdash;';
        var play = '&mdash;';
        if (row.alias_url) {
          play = '<a href="' + esc(row.alias_url) + '">play</a>';
        } else if (row.immutable_url) {
          play = '<a href="' + esc(row.immutable_url) + '">archived build</a>';
        }
        return '<td>' + version + '</td><td>' + commit + '</td><td>' +
               esc(row.date || '\\u2014') + '</td><td>' + play + '</td>';
      }
      function render(data) {
        var body = document.getElementById('versions-body');
        if (!body || !data || !Array.isArray(data.versions)) { return; }
        body.innerHTML = data.versions.map(function (row) {
          return '<tr' + (row.current ? ' class="current"' : '') + '>' + cells(row) + '</tr>';
        }).join('');
        var live = document.getElementById('live-note');
        if (live) { live.hidden = false; }
      }
      // An archived build ships the list as it stood on its own deploy day.
      // When the production index is reachable, show the live list instead.
      if (location.host === 'openglad.pages.dev') { return; }
      (async function () {
        try {
          var response = await fetch(LIVE_INDEX, { mode: 'cors' });
          if (response.ok) { render(await response.json()); }
        } catch (error) { /* keep the embedded snapshot */ }
      })();
    })();
"""


def render_row_html(row: dict[str, Any]) -> str:
    version = html.escape(row["version"]) if row["version"] else "unknown"
    if row["current"]:
        version += ' <span class="badge">current</span>'
    if row["commit_url"]:
        short = html.escape(row["short_commit"] or row["commit"])
        commit = f'<a href="{html.escape(row["commit_url"])}"><code>{short}</code></a>'
    else:
        commit = "&mdash;"
    if row["alias_url"]:
        play = f'<a href="{html.escape(row["alias_url"])}">play</a>'
    elif row["immutable_url"]:
        play = f'<a href="{html.escape(row["immutable_url"])}">archived build</a>'
    else:
        play = "&mdash;"
    date = html.escape(row["date"]) if row["date"] else "&mdash;"
    classes = ' class="current"' if row["current"] else ""
    return (
        f"          <tr{classes}><td>{version}</td><td>{commit}</td>"
        f"<td>{date}</td><td>{play}</td></tr>"
    )


def render_html(document: dict[str, Any]) -> str:
    generated = document["generated_from"]
    version = html.escape(str(generated.get("version") or "unknown"))
    commit = html.escape(str(generated.get("commit") or "")[:8] or "unknown")
    rows = "\n".join(render_row_html(row) for row in document["versions"])
    script = LIVE_REFRESH_JS.replace("__LIVE_INDEX_URL__", LIVE_INDEX_URL)
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Openglad versions</title>
    <style>{PAGE_CSS}    </style>
</head>
<body>
    <main>
        <h1>Openglad versions</h1>
        <p>
            Every push to master is deployed to its own permanent address, so any
            build that ever shipped stays playable for comparison.
            This page was generated by build {version} ({commit}) &mdash;
            <a href="{PRODUCTION_URL}">play the latest</a>.
        </p>
        <table>
            <thead>
                <tr><th>Version</th><th>Commit</th><th>Deployed</th><th>Play</th></tr>
            </thead>
            <tbody id="versions-body">
{rows}
            </tbody>
        </table>
        <p class="note">
            Older builds are Cloudflare preview deployments: multiplayer on them
            needs the preview environment's relay binding
            (<a href="{REPO_URL}/blob/master/docs/INSTALL.md">docs/INSTALL.md</a>).
            This list was generated by the build above; when reachable, the live
            list from openglad.pages.dev replaces it.
        </p>
        <p class="note" id="live-note" hidden>Showing the live list from openglad.pages.dev.</p>
    </main>
    <footer>
        <a href="{REPO_URL}">Source code</a> &middot;
        <a href="../help.html">Help</a> &middot;
        <a href="{PRODUCTION_URL}">Play the latest</a>
    </footer>
    <script>
{script}    </script>
</body>
</html>
"""


def write_output(out_dir: str, document: dict[str, Any]) -> None:
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=2, sort_keys=False)
        handle.write("\n")
    with open(os.path.join(out_dir, "index.html"), "w", encoding="utf-8") as handle:
        handle.write(render_html(document))


def make_document(rows: list[dict[str, Any]], version: str, commit: str,
                  alias: str) -> dict[str, Any]:
    return {
        "generated_from": {"version": version, "commit": commit, "alias": alias},
        "generated_at": _dt.datetime.now(_dt.timezone.utc).replace(microsecond=0).isoformat(),
        "versions": rows,
    }


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate the Openglad /versions index (index.json + index.html).",
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--live", action="store_true",
        help="List every deployed version, read from the Cloudflare Pages API "
             "(CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID in the environment).",
    )
    mode.add_argument(
        "--offline", action="store_true",
        help="Skip the API and describe only the build that runs this script.",
    )
    parser.add_argument("--project", default="openglad",
                        help="Cloudflare Pages project name (default: openglad).")
    parser.add_argument("--repo-root", default=".",
                        help="Git checkout used to turn a commit into a version number.")
    parser.add_argument("--current-version", required=True,
                        help="Version of the build being deployed, e.g. 2.1073.")
    parser.add_argument("--current-commit", required=True,
                        help="Full commit sha of the build being deployed.")
    parser.add_argument("--current-alias", default="",
                        help="Archive alias branch of the build, e.g. v2-1073.")
    parser.add_argument("--out", required=True,
                        help="Directory to write index.json and index.html into.")
    parser.add_argument("--deployments-json", default=None,
                        help="Read the deployment listing from this file instead of the API.")
    parser.add_argument("--no-git", action="store_true",
                        help="Never shell out to git; resolve versions from --commit-counts only.")
    parser.add_argument("--commit-counts", default=None,
                        help="JSON map of commit sha to commit count, consulted before git.")
    return parser.parse_args(argv)


def main(argv: Optional[list[str]] = None) -> int:
    args = parse_args(argv)

    counts: dict[str, int] = {}
    if args.commit_counts:
        with open(args.commit_counts, "r", encoding="utf-8") as handle:
            counts = {str(k): int(v) for k, v in json.load(handle).items()}
    counter = CommitCounter(args.repo_root, use_git=not args.no_git, counts=counts)

    deployments: list[dict[str, Any]] = []
    if args.live:
        try:
            if args.deployments_json:
                deployments = load_deployments(args.deployments_json)
            else:
                token = os.environ.get("CLOUDFLARE_API_TOKEN", "")
                account = os.environ.get("CLOUDFLARE_ACCOUNT_ID", "")
                if not token or not account:
                    raise RuntimeError(
                        "CLOUDFLARE_API_TOKEN / CLOUDFLARE_ACCOUNT_ID are not set"
                    )
                deployments = fetch_deployments(args.project, account, token)
        except Exception as error:  # noqa: BLE001 - a bad index must never red a deploy
            # Never echo the token or the URL that carries it.
            print(
                f"::warning::Could not list Cloudflare Pages deployments "
                f"({type(error).__name__}); writing a current-only versions page.",
                file=sys.stderr,
            )
            deployments = []

    rows = build_rows(
        deployments, counter, args.current_version, args.current_commit, args.current_alias
    )
    document = make_document(rows, args.current_version, args.current_commit, args.current_alias)
    write_output(args.out, document)
    print(f"Wrote {len(rows)} version row(s) to {os.path.join(args.out, 'index.json')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
