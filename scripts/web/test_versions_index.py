#!/usr/bin/env python3
"""Unit tests for scripts/web/versions_index.py.

These run in wasm-e2e.yml before the WASM build, so a broken generator is
caught in seconds rather than after a 20-minute Emscripten build. Everything
here works off the committed fixture with --no-git: the tests must not depend
on the checkout's own history (a shallow CI clone would change the answers).
"""

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import versions_index  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "testdata", "deployments_sample.json")

COMMIT_A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1"  # v2-1075 + production
COMMIT_B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb2"  # v2-1074 alias only
COMMIT_C = "ccccccccccccccccccccccccccccccccccccccc3"  # pre-alias production
COMMIT_D = "ddddddddddddddddddddddddddddddddddddddd4"  # not in the counts map
COMMIT_PR = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee5"  # the pr-42 preview
COMMIT_FAILED = "fffffffffffffffffffffffffffffffffffffff7"  # production, build failed
COMMIT_CURRENT = "f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f6"

COUNTS = {COMMIT_A: 1075, COMMIT_B: 1074, COMMIT_C: 1050}


def rows_for(current_commit: str = COMMIT_CURRENT,
             current_version: str = "2.1076",
             current_alias: str = "v2-1076") -> list:
    deployments = versions_index.load_deployments(FIXTURE)
    counter = versions_index.CommitCounter(HERE, use_git=False, counts=COUNTS)
    return versions_index.build_rows(
        deployments, counter, current_version, current_commit, current_alias
    )


class GroupingTests(unittest.TestCase):
    def test_pr_preview_deployments_are_excluded(self):
        """A pr-<n> preview is transient; it must never appear in the index."""
        rows = rows_for()
        self.assertNotIn(COMMIT_PR, [row["commit"] for row in rows])
        self.assertFalse(
            any(row["alias_url"] and "pr-42" in row["alias_url"] for row in rows)
        )

    def test_failed_deployments_are_excluded(self):
        """A failed production deployment serves nothing: no dead link on the page."""
        rows = rows_for()
        self.assertNotIn(COMMIT_FAILED, [row["commit"] for row in rows])
        self.assertFalse(
            any(row["immutable_url"] and "f00dcafe" in row["immutable_url"]
                for row in rows)
        )
        # A listing with no latest_stage at all is still trusted - the other
        # fixture rows carry none and they are all indexed.
        self.assertTrue(versions_index.succeeded({"environment": "production"}))

    def test_current_row_is_first_and_flagged(self):
        rows = rows_for()
        self.assertTrue(rows[0]["current"])
        self.assertEqual(rows[0]["commit"], COMMIT_CURRENT)
        self.assertEqual(rows[0]["version"], "2.1076")
        self.assertEqual(rows[0]["alias_url"], "https://v2-1076.openglad.pages.dev")
        self.assertFalse(any(row["current"] for row in rows[1:]))

    def test_rows_are_ordered_current_then_version_desc_then_unknown(self):
        rows = rows_for()
        self.assertEqual(
            [row["version"] for row in rows],
            ["2.1076", "2.1075", "2.1074", "2.1050", None, None],
        )
        # Unknown-version rows fall back to newest-deploy-first.
        self.assertEqual([row["date"] for row in rows[-2:]], ["2026-08-15", "2026-08-10"])

    def test_alias_and_immutable_urls_resolve_per_commit(self):
        """Alias + production of one commit collapse into a single row."""
        rows = {row["commit"]: row for row in rows_for()}
        a = rows[COMMIT_A]
        self.assertEqual(a["alias_url"], "https://v2-1075.openglad.pages.dev")
        self.assertEqual(a["immutable_url"], "https://e5f6a7b8.openglad.pages.dev")
        self.assertEqual(a["date"], "2026-09-05")
        b = rows[COMMIT_B]
        self.assertEqual(b["alias_url"], "https://v2-1074.openglad.pages.dev")
        # No production deploy for this commit: the alias deployment's own URL.
        self.assertEqual(b["immutable_url"], "https://c9d8e7f6.openglad.pages.dev")
        c = rows[COMMIT_C]
        self.assertIsNone(c["alias_url"])
        self.assertEqual(c["immutable_url"], "https://1a2b3c4d.openglad.pages.dev")

    def test_deployment_without_commit_hash_is_kept_with_a_null_version(self):
        rows = rows_for()
        anonymous = [row for row in rows if row["commit"] is None]
        self.assertEqual(len(anonymous), 1)
        self.assertIsNone(anonymous[0]["version"])
        self.assertIsNone(anonymous[0]["commit_url"])
        self.assertEqual(anonymous[0]["immutable_url"], "https://5e6f7a8b.openglad.pages.dev")

    def test_commit_outside_the_counted_history_gets_a_null_version(self):
        rows = {row["commit"]: row for row in rows_for()}
        self.assertIn(COMMIT_D, rows)
        self.assertIsNone(rows[COMMIT_D]["version"])
        self.assertIsNone(rows[COMMIT_D]["minor"])
        self.assertEqual(rows[COMMIT_D]["immutable_url"], "https://9c8b7a6d.openglad.pages.dev")

    def test_current_commit_absorbs_its_own_api_rows(self):
        """Re-deploying an already-listed commit must not duplicate its row."""
        rows = rows_for(current_commit=COMMIT_A, current_version="2.1075",
                        current_alias="v2-1075")
        self.assertEqual(len([row for row in rows if row["commit"] == COMMIT_A]), 1)
        self.assertTrue(rows[0]["current"])
        self.assertEqual(rows[0]["immutable_url"], "https://e5f6a7b8.openglad.pages.dev")


class RenderingTests(unittest.TestCase):
    def test_html_contains_every_play_link_and_no_plain_http(self):
        rows = rows_for()
        document = versions_index.make_document(rows, "2.1076", COMMIT_CURRENT, "v2-1076")
        page = versions_index.render_html(document)
        for row in rows:
            target = row["alias_url"] or row["immutable_url"]
            if target:
                self.assertIn(f'href="{target}"', page)
        self.assertNotIn("http://", page)
        self.assertIn("<title>Openglad versions</title>", page)
        self.assertIn("openglad.pages.dev/versions/index.json", page)

    def test_html_escapes_api_supplied_strings(self):
        hostile = versions_index.make_row(
            commit='<script>"x"', minor=None, date="2026-01-01",
            alias_url='https://evil"onload=alert(1)', immutable_url=None,
        )
        document = versions_index.make_document([hostile], "2.0", "", "")
        page = versions_index.render_html(document)
        self.assertNotIn("<script>\"x\"", page)
        self.assertIn("&lt;script&gt;", page)
        self.assertIn("&quot;onload=alert(1)", page)


class CliTests(unittest.TestCase):
    def test_offline_writes_a_current_only_page(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "versions")
            exit_code = versions_index.main([
                "--offline", "--project", "openglad", "--repo-root", HERE,
                "--no-git", "--current-version", "2.1076",
                "--current-commit", COMMIT_CURRENT, "--current-alias", "v2-1076",
                "--out", out,
            ])
            self.assertEqual(exit_code, 0)
            with open(os.path.join(out, "index.json"), encoding="utf-8") as handle:
                document = json.load(handle)
            self.assertEqual(len(document["versions"]), 1)
            self.assertTrue(document["versions"][0]["current"])
            self.assertEqual(document["generated_from"]["alias"], "v2-1076")
            with open(os.path.join(out, "index.html"), encoding="utf-8") as handle:
                page = handle.read()
            self.assertIn("https://v2-1076.openglad.pages.dev", page)
            self.assertIn("current", page)

    def test_live_with_a_saved_listing_writes_every_row(self):
        with tempfile.TemporaryDirectory() as tmp:
            counts_path = os.path.join(tmp, "counts.json")
            with open(counts_path, "w", encoding="utf-8") as handle:
                json.dump(COUNTS, handle)
            out = os.path.join(tmp, "versions")
            exit_code = versions_index.main([
                "--live", "--project", "openglad", "--repo-root", HERE,
                "--no-git", "--commit-counts", counts_path,
                "--deployments-json", FIXTURE,
                "--current-version", "2.1076",
                "--current-commit", COMMIT_CURRENT, "--current-alias", "v2-1076",
                "--out", out,
            ])
            self.assertEqual(exit_code, 0)
            with open(os.path.join(out, "index.json"), encoding="utf-8") as handle:
                document = json.load(handle)
            self.assertEqual(len(document["versions"]), 6)

    def test_live_api_failure_degrades_to_the_current_build_and_exits_zero(self):
        """API trouble warns and ships a current-only page; it never reds a deploy."""
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "versions")
            env_backup = {key: os.environ.pop(key, None)
                          for key in ("CLOUDFLARE_API_TOKEN", "CLOUDFLARE_ACCOUNT_ID")}
            try:
                exit_code = versions_index.main([
                    "--live", "--project", "openglad", "--repo-root", HERE,
                    "--no-git", "--current-version", "2.1076",
                    "--current-commit", COMMIT_CURRENT, "--current-alias", "v2-1076",
                    "--out", out,
                ])
            finally:
                for key, value in env_backup.items():
                    if value is not None:
                        os.environ[key] = value
            self.assertEqual(exit_code, 0)
            with open(os.path.join(out, "index.json"), encoding="utf-8") as handle:
                document = json.load(handle)
            self.assertEqual(len(document["versions"]), 1)


if __name__ == "__main__":
    unittest.main()
