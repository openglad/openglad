#!/usr/bin/env python3
"""Compile-cache workers must cover one exact, mergeable object inventory."""

import argparse
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import compile_cache


class CompileCacheTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()

    def write_database(self, sizes=(80, 40, 30, 10)):
        records = []
        for index, size in enumerate(sizes):
            source = self.root / f"source-{index}.cpp"
            source.write_text("x" * size)
            output = self.build / "objects" / f"source-{index}.cpp.o"
            records.append({
                "directory": str(self.build),
                "file": str(source),
                "output": str(output),
                "command": f"c++ -c {source} -o {output}",
            })
        (self.build / "compile_commands.json").write_text(json.dumps(records))

    def test_plan_is_complete_disjoint_and_balanced_by_source_weight(self):
        self.write_database()
        plan = compile_cache.compile_plan(self.build, 2)
        outputs = [item for shard in plan["assignments"] for item in shard]
        self.assertEqual(4, plan["record_count"])
        self.assertEqual(4, len(set(outputs)))
        self.assertEqual(80, plan["weights"][0])
        self.assertEqual(80, plan["weights"][1])

    def test_plan_identity_ignores_the_ephemeral_nix_cache_directory(self):
        self.write_database((10,))
        database_path = self.build / "compile_commands.json"
        database = json.loads(database_path.read_text())
        database[0]["command"] = "env SCCACHE_DIR=/nix/build/one sccache " + database[0]["command"]
        database_path.write_text(json.dumps(database))
        first = compile_cache.compile_plan(self.build, 1)["fingerprint"]
        database[0]["command"] = database[0]["command"].replace("/nix/build/one", "/nix/build/two")
        database_path.write_text(json.dumps(database))
        self.assertEqual(first, compile_cache.compile_plan(self.build, 1)["fingerprint"])

    def test_plan_resolves_relative_sources_from_the_compile_directory(self):
        self.write_database((10,))
        database_path = self.build / "compile_commands.json"
        database = json.loads(database_path.read_text())
        database[0]["directory"] = str(self.root)
        database[0]["file"] = "source-0.cpp"
        database_path.write_text(json.dumps(database))
        plan = compile_cache.compile_plan(self.build, 1)
        self.assertEqual(1, plan["record_count"])

    def test_warm_invokes_only_its_planned_ninja_objects(self):
        self.write_database((30, 20, 10))
        manifest = self.root / "shard.json"
        args = argparse.Namespace(build=self.build, lane="asan", revision="a" * 40,
                                  shards=2, shard=1, jobs=4, out=manifest)
        with patch("compile_cache.subprocess.run") as execute:
            compile_cache.warm(args)
        command = execute.call_args.args[0]
        plan = compile_cache.compile_plan(self.build, 2)
        self.assertEqual(plan["assignments"][1], command[5:])
        self.assertEqual(1, json.loads(manifest.read_text())["shard"])

    def test_coverage_warm_requires_one_compiler_note_per_object(self):
        self.write_database((10,))
        database_path = self.build / "compile_commands.json"
        database = json.loads(database_path.read_text())
        database[0]["command"] += " --coverage"
        database_path.write_text(json.dumps(database))
        output = self.build / "objects/source-0.cpp.o"

        def materialize(_command, check):
            self.assertTrue(check)
            output.parent.mkdir(parents=True)
            (self.build / "objects/source-0.cpp.gcno").write_bytes(b"notes")

        manifest = self.root / "coverage-shard.json"
        args = argparse.Namespace(build=self.build, lane="coverage", revision="rev",
                                  shards=1, shard=0, jobs=1, out=manifest)
        with patch("compile_cache.subprocess.run", side_effect=materialize):
            compile_cache.warm(args)
        self.assertEqual(1, json.loads(manifest.read_text())["coverage_notes"])

        (self.build / "objects/source-0.cpp.gcno").unlink()
        with patch("compile_cache.subprocess.run"):
            with self.assertRaisesRegex(ValueError, "materialize notes"):
                compile_cache.warm(args)

    def test_coverage_warm_does_not_demand_notes_from_uninstrumented_dependencies(self):
        self.write_database((10,))
        manifest = self.root / "coverage-external-shard.json"
        args = argparse.Namespace(build=self.build, lane="coverage", revision="rev",
                                  shards=1, shard=0, jobs=1, out=manifest)
        with patch("compile_cache.subprocess.run"):
            compile_cache.warm(args)
        self.assertEqual(0, json.loads(manifest.read_text())["coverage_notes"])

    def cache_root(self, name, entries):
        root = self.root / name
        (root / "cache").mkdir(parents=True)
        for relative, content in entries.items():
            path = root / "cache" / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        return root

    def publish_shard(self, name, shard, entries, seed):
        live = self.cache_root(name + "-live", entries)
        manifest = self.root / f"{name}-plan.json"
        manifest.write_text(json.dumps({
            "format": 1, "lane": "asan", "revision": "rev", "fingerprint": "plan",
            "record_count": 4, "shard_count": 2, "shard": shard,
            "assigned_count": 2, "assigned_weight": 2,
        }))
        output = self.root / name
        compile_cache.publish(argparse.Namespace(
            cache=live / "cache", seed=seed, output=output, lane="asan",
            revision="rev", shard_manifest=manifest, max_bytes=None))
        return output

    def test_full_publication_enforces_its_cache_budget_with_newest_files(self):
        live = self.cache_root("live", {"old": b"old", "new": b"newer"})
        (live / "cache/old").touch()
        (live / "cache/new").touch()
        # Give the intended survivor a strictly newer timestamp.
        old = (live / "cache/old").stat().st_mtime_ns
        new = live / "cache/new"
        os.utime(new, ns=(old + 1_000_000, old + 1_000_000))
        output = self.root / "bounded"
        compile_cache.publish(argparse.Namespace(
            cache=live / "cache", seed=None, output=output, lane="asan",
            revision="rev", shard_manifest=None, max_bytes=5))
        self.assertFalse((output / "cache/old").exists())
        self.assertEqual(b"newer", (output / "cache/new").read_bytes())

    def test_shards_publish_only_deltas_and_complete_merge_restores_seed(self):
        seed = self.cache_root("seed", {"a/seed": b"old"})
        first = self.publish_shard("first", 0, {"a/seed": b"old", "b/one": b"1"}, seed)
        second = self.publish_shard("second", 1, {"a/seed": b"old", "c/two": b"2"}, seed)
        self.assertFalse((first / "cache/a/seed").exists())
        merged = self.root / "merged"
        compile_cache.merge(argparse.Namespace(
            seed=seed, input=[first, second], output=merged, lane="asan",
            revision="rev", shards=2))
        self.assertEqual(
            {"a/seed", "b/one", "c/two"},
            {path.relative_to(merged / "cache").as_posix()
             for path in (merged / "cache").rglob("*") if path.is_file()},
        )

    def test_merge_accepts_a_fully_cached_shard_whose_empty_directory_was_not_uploaded(self):
        seed = self.cache_root("seed", {"a/seed": b"old"})
        first = self.publish_shard("first", 0, {"a/seed": b"old"}, seed)
        second = self.publish_shard("second", 1, {"a/seed": b"old"}, seed)
        (first / "cache").rmdir()
        (second / "cache").rmdir()
        merged = self.root / "merged"
        compile_cache.merge(argparse.Namespace(
            seed=seed, input=[first, second], output=merged, lane="asan",
            revision="rev", shards=2))
        self.assertEqual(b"old", (merged / "cache/a/seed").read_bytes())

    def test_merge_rejects_missing_shard(self):
        seed = self.cache_root("seed", {})
        first = self.publish_shard("first", 0, {"b/one": b"1"}, seed)
        with self.assertRaisesRegex(ValueError, "expected 2"):
            compile_cache.merge(argparse.Namespace(
                seed=seed, input=[first], output=self.root / "merged", lane="asan",
                revision="rev", shards=2))

    def test_merge_rejects_an_incomplete_assignment_count(self):
        seed = self.cache_root("seed", {})
        first = self.publish_shard("first", 0, {"b/one": b"1"}, seed)
        second = self.publish_shard("second", 1, {"c/two": b"2"}, seed)
        manifest_path = second / "shard.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["assigned_count"] = 1
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "assign 3 objects, expected 4"):
            compile_cache.merge(argparse.Namespace(
                seed=seed, input=[first, second], output=self.root / "merged",
                lane="asan", revision="rev", shards=2))


if __name__ == "__main__":
    unittest.main()
