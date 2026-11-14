#!/usr/bin/env python3
"""The compile fan-out decision: a wrong "skip" may only ever cost time."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import compile_fanout as fanout  # noqa: E402

PREFIX = "native-sccache-v2-Linux-coverage-lock-"


class ClassificationTests(unittest.TestCase):
    def test_what_no_compiler_reads_costs_nothing(self):
        for path in (".github/workflows/test.yml", "docs/ARCHITECTURE.md", "CLAUDE.md",
                     "packs/core/families/living-05-cleric.lua", "campaigns/tower/scen/scen1.fss",
                     "tests/e2e/wasm-jitter.spec.js", "relay/src/index.ts", "pix/footman.png",
                     "scripts/ci/test_timings.json", "scripts/ci/test_test_matrix.py",
                     "scripts/ci/nix_bundle_test.py", "scripts/parity/run_mutation_canary.sh",
                     "tests/parity/golden/DRIFT_LEDGER.md"):
            with self.subTest(path=path):
                self.assertEqual(0, fanout.compilations(path))

    def test_one_source_is_a_known_number_of_objects(self):
        self.assertEqual(3, fanout.compilations("src/gameplay/walker.cpp"))
        self.assertEqual(1, fanout.compilations("tests/integration/test_uxshots_probe.cpp"))
        self.assertEqual(1, fanout.compilations("tests/unit/test_coverage_counters.cpp"))

    def test_everything_that_can_reach_many_objects_keeps_the_fan_out(self):
        for path in ("include/openglad/gameplay/walker.h", "src/interface/ui/picker_sdl_defs.h",
                     "tests/test_interact.h", "tests/coverage_internal/help_internal.inc",
                     "tests/unit/unit_main.cpp", "tests/integration/integration_main.cpp",
                     "tests/unit/family_registry_dump.cpp", "CMakeLists.txt",
                     "cmake/OpenGladTests.cmake", "CMakePresets.json", "flake.nix", "flake.lock",
                     "nix/native-ci.nix", "scripts/ci/nix_bundle.py",
                     "scripts/ci/compile_cache.py", "scripts/ci/sccache_output_key.sh",
                     "tools/mapgen/tower_mapgen.cpp", "a_new_top_level_file"):
            with self.subTest(path=path):
                self.assertIsNone(fanout.compilations(path))
                self.assertEqual(True, fanout.decide([path])[0])

    def test_paths_outside_the_repository_are_refused(self):
        for path in ("/etc/passwd", "../other/src/a.cpp", "src/../../a.cpp"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                fanout.compilations(path)


class DecisionTests(unittest.TestCase):
    def test_nothing_changed_skips_the_fan_out(self):
        self.assertEqual((False, "about 0 objects changed, within the producer's budget of 24"),
                         fanout.decide([]))

    def test_budget_is_inclusive_and_counts_each_path_once(self):
        tests = [f"tests/unit/test_{index}.cpp" for index in range(24)]
        self.assertEqual(False, fanout.decide(tests)[0])
        self.assertEqual(False, fanout.decide(tests + tests)[0])
        warm, reason = fanout.decide(tests + ["tests/unit/test_one_more.cpp"])
        self.assertEqual(True, warm)
        self.assertIn("about 25 objects", reason)
        game = [f"src/gameplay/file_{index}.cpp" for index in range(8)]
        self.assertEqual(False, fanout.decide(game)[0])
        self.assertEqual(True, fanout.decide(game + ["src/core/one_more.cpp"])[0])

    def test_one_wide_path_outweighs_any_number_of_narrow_ones(self):
        warm, reason = fanout.decide(["docs/a.md", "tests/unit/test_a.cpp",
                                      "include/openglad/core/util.h"])
        self.assertEqual(True, warm)
        self.assertIn("include/openglad/core/util.h", reason)

    def test_seed_revision_requires_this_lanes_prefix_and_a_full_commit_id(self):
        revision = "0123456789abcdef0123456789abcdef01234567"
        self.assertEqual(revision, fanout.seed_revision(PREFIX, PREFIX + revision))
        for key in ("", PREFIX, PREFIX + revision[:39], PREFIX + revision + "0",
                    PREFIX + revision.upper(), "native-sccache-v2-Linux-asan-lock-" + revision):
            with self.subTest(key=key):
                self.assertIsNone(fanout.seed_revision(PREFIX, key))
        self.assertIsNone(fanout.seed_revision("", revision))


class RepositoryTests(unittest.TestCase):
    """The decision against real commits, in a throwaway repository."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.git("init", "--quiet")
        self.commit({"src/core/a.cpp": "1", "include/openglad/core/a.h": "1",
                     "docs/notes.md": "1", "tests/unit/test_a.cpp": "1"})
        self.seed = self.git("rev-parse", "HEAD")
        previous = os.getcwd()
        os.chdir(self.root)
        self.addCleanup(os.chdir, previous)

    def git(self, *arguments):
        return subprocess.check_output(
            ["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid",
             "-c", "commit.gpgsign=false", *arguments], cwd=self.root, text=True).strip()

    def commit(self, files):
        for name, text in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        self.git("add", "--all")
        self.git("commit", "--quiet", "--message", "change")

    def plan(self):
        return fanout.plan(PREFIX, PREFIX + self.seed)

    def test_the_same_tree_and_a_test_only_change_skip_the_fan_out(self):
        self.assertEqual(False, self.plan()[0])
        self.commit({"tests/unit/test_a.cpp": "2", "docs/notes.md": "2"})
        warm, reason = self.plan()
        self.assertEqual(False, warm)
        self.assertIn("about 1 objects", reason)
        self.assertIn(self.seed[:8], reason)

    def test_a_header_change_keeps_the_fan_out_even_when_it_is_reverted_later(self):
        self.commit({"include/openglad/core/a.h": "2"})
        self.assertEqual(True, self.plan()[0])
        # The decision reads the two trees, not the commits between them.
        self.commit({"include/openglad/core/a.h": "1"})
        self.assertEqual(False, self.plan()[0])

    def test_a_deleted_and_a_renamed_source_are_both_counted(self):
        self.git("mv", "src/core/a.cpp", "src/core/b.cpp")
        self.git("rm", "--quiet", "tests/unit/test_a.cpp")
        self.git("commit", "--quiet", "--message", "move")
        warm, reason = self.plan()
        self.assertEqual(False, warm)
        self.assertIn("about 7 objects", reason)

    def test_a_seed_from_an_unknown_commit_or_no_seed_keeps_the_fan_out(self):
        unknown = "f" * 40
        self.assertEqual((True, f"the seed's commit {unknown[:8]} is not in this checkout"),
                         fanout.plan(PREFIX, PREFIX + unknown))
        self.assertEqual((True, "no compiler-cache seed from a known commit"),
                         fanout.plan(PREFIX, ""))

    def test_command_line_writes_the_verdict_for_the_workflow(self):
        output = self.root / "output"
        summary = self.root / "summary"
        script = Path(fanout.__file__).resolve()
        environment = dict(os.environ, GITHUB_OUTPUT=str(output), GITHUB_STEP_SUMMARY=str(summary))
        subprocess.run([sys.executable, str(script), "--prefix", PREFIX,
                        "--matched-key", PREFIX + self.seed],
                       check=True, cwd=self.root, env=environment, stdout=subprocess.DEVNULL)
        lines = output.read_text().splitlines()
        self.assertEqual("warm=false", lines[0])
        self.assertTrue(lines[1].startswith("reason=about 0 objects changed"))
        self.assertIn("skipped", summary.read_text())
        output.unlink()
        subprocess.run([sys.executable, str(script), "--prefix", PREFIX],
                       check=True, cwd=self.root, env=environment, stdout=subprocess.DEVNULL)
        self.assertEqual("warm=true", output.read_text().splitlines()[0])


if __name__ == "__main__":
    unittest.main()
