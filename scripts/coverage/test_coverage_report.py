#!/usr/bin/env python3
"""Contract tests for production coverage scope and one-pass collection."""

import pathlib
import tempfile
import types
import unittest
from unittest import mock

import coverage_report as report


class CppScopeTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path("/checkout")
        self.source = report.FileCoverage(
            "src/core/sample.cpp", {4: 3}, {"source()": (4, 3)})
        self.header = report.FileCoverage(
            "include/openglad/core/sample.h", {8: 2, 12: 0},
            {"inline_hit()": (8, 2), "inline_miss()": (12, 0)})

    def test_line_floor_is_strict_and_function_floor_requires_every_body(self):
        self.assertEqual((False, True), report.bar_verdicts(report.Totals(98, 100, 7, 7), 98, 100))
        self.assertEqual((True, True), report.bar_verdicts(report.Totals(99, 100, 7, 7), 98, 100))
        self.assertEqual((True, False), report.bar_verdicts(report.Totals(99, 100, 6, 7), 98, 100))

    def test_valid_64bit_heat_is_preserved_but_negative_counts_remain_errors(self):
        from gcovr.formats.gcov.parser.common import check_hits, NegativeHits, SuspiciousHits
        state = {"location": ("src/gameplay/script/script_coverage.cpp", 84)}
        self.assertEqual(53249716800, check_hits(53249716800, "rotr32", set(),
                         report.GCOVR_SUSPICIOUS_HITS_THRESHOLD, state))
        with self.assertRaises(NegativeHits):
            check_hits(-1, "rotr32", set(), report.GCOVR_SUSPICIOUS_HITS_THRESHOLD, state)
        with self.assertRaises(SuspiciousHits):
            check_hits((1 << 63) - 1, "rotr32", set(),
                       report.GCOVR_SUSPICIOUS_HITS_THRESHOLD, state)

    def test_header_misses_stay_in_the_function_denominator(self):
        errors = []
        files = [self.source, self.header]
        roots = report.check_cpp_scope(files, self.root, errors)
        self.assertEqual([], errors)
        self.assertEqual(report.Totals(1, 2, 1, 2), roots["include/openglad/"])
        self.assertEqual(report.Totals(2, 3, 2, 3), report.totals_of(files))
        self.assertEqual((False, False), report.bar_verdicts(report.totals_of(files), 98, 100))

    def test_a_legacy_source_only_tracefile_is_rejected(self):
        errors = []
        report.check_cpp_scope([self.source], self.root, errors)
        self.assertEqual([
            "C++ tracefile has no compiler-emitted public-header functions: "
            "include/openglad/ must be collected together with src/"
        ], errors)

    def test_test_code_cannot_pad_imported_production_totals(self):
        errors = []
        helper = report.FileCoverage("tests/coverage_internal/sample.inc", {1: 100},
                                     {"test_helper()": (1, 100)})
        roots = report.check_cpp_scope([self.source, self.header, helper], self.root, errors)
        self.assertEqual([
            "C++ tracefile includes records outside the production scope "
            "src/ + include/openglad/: tests/coverage_internal/sample.inc"
        ], errors)
        self.assertEqual(report.Totals(1, 1, 1, 1), roots["src/"])

    def test_missing_tracked_header_is_an_error_even_without_strict_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            errors, warnings = [], []
            kept = report.drop_deleted_sources(
                [self.header], root, {self.header.path}, False, errors, warnings)
            self.assertEqual([], kept)
            self.assertEqual([], warnings)
            self.assertEqual(1, len(errors))
            self.assertIn("git-TRACKED", errors[0])
            self.assertIn(self.header.path, errors[0])


class GcovrCollectionTests(unittest.TestCase):
    def test_all_outputs_share_one_wide_scope_collection(self):
        calls = []

        def fake_run(command, **_kwargs):
            calls.append(command)
            if command[-1] == "--help":
                return types.SimpleNamespace(returncode=0,
                    stdout="--lcov-format-version {1.x,2.0}", stderr="")
            pathlib.Path(command[command.index("--json") + 1]).write_text(
                report.json.dumps({"gcovr/format_version": "0.14", "files": []}))
            for option in ("--lcov", "--html-details", "--txt"):
                if option in command:
                    pathlib.Path(command[command.index(option) + 1]).write_text(option)
            return types.SimpleNamespace(returncode=0)

        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            with mock.patch.object(report.shutil, "which", return_value="/bin/gcovr"), \
                    mock.patch.object(report.subprocess, "run", side_effect=fake_run):
                report.gcovr_tracefile(root / "build", root, root / "cpp.info",
                                      json_output=root / "cpp.json",
                                      html_output=root / "coverage.html")
            self.assertEqual(3, len(calls))
            collection, help_call, formatting = calls
            self.assertEqual(["/bin/gcovr", "--help"], help_call)
            self.assertEqual("(?:src/|include/openglad/)", collection[collection.index("--filter") + 1])
            self.assertEqual(str(root / "build"), collection[-1])
            self.assertNotIn("--exclude", collection)
            self.assertEqual(str((1 << 63) - 1),
                collection[collection.index("--gcov-suspicious-hits-threshold") + 1])
            self.assertFalse(any(option.startswith("--gcov-ignore-parse-errors") for option in collection))
            self.assertIn("--add-tracefile", formatting)
            self.assertNotIn(str(root / "build"), formatting)
            self.assertEqual([], report.json.loads((root / "cpp.json").read_text())["files"])
            self.assertEqual("--lcov", (root / "cpp.info").read_text())
            self.assertEqual("--html-details", (root / "coverage.html").read_text())
            self.assertEqual("--txt", (root / "coverage-summary.txt").read_text())

    def test_missing_requested_output_cannot_reuse_a_stale_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "cpp.json").write_text(report.json.dumps({"gcovr/format_version": "0.14", "files": []}))
            with mock.patch.object(report.shutil, "which", return_value="/bin/gcovr"), \
                    mock.patch.object(report.subprocess, "run",
                                      return_value=types.SimpleNamespace(returncode=0, stdout="--lcov-format-1.x", stderr="")), \
                    self.assertRaisesRegex(SystemExit, "gcovr failed"):
                report.gcovr_tracefile(root / "build", root, root / "cpp.info",
                                      json_output=root / "cpp.json")
            self.assertFalse((root / "cpp.json").exists())


class CppAliasTests(unittest.TestCase):
    def test_alias_merging_preserves_distinct_function_instantiations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            original = root / "include/openglad/sample.h"
            original.parent.mkdir(parents=True)
            original.write_text("inline int sample() { return 1; }\n")
            alias = root / "build/ci-coverage/component_includes/core/openglad/sample.h"
            alias.parent.mkdir(parents=True)
            alias.symlink_to(original)
            files = [report.FileCoverage(str(original), {1: 3}, {"sample<int>()": (1, 3)}),
                     report.FileCoverage(str(alias), {1: 0},
                                         {"sample<int>()": (1, 0), "sample<long>()": (1, 0)})]
            merged = report.canonicalize_cpp_records(files, root)
            self.assertEqual(1, len(merged))
            self.assertEqual("include/openglad/sample.h", merged[0].path)
            self.assertEqual(report.Totals(1, 1, 1, 2), report.totals_of(merged))
            self.assertEqual((1, 3), merged[0].functions["sample<int>()"])
            self.assertEqual((1, 0), merged[0].functions["sample<long>()"])

    def test_json_alias_merge_keeps_mangled_instantiations_and_zero_hits(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            original = root / "include/openglad/sample.h"
            original.parent.mkdir(parents=True)
            original.write_text("template<class T> int sample() { return 1; }\n")
            alias = root / "build/ci-coverage/component_includes/core/openglad/sample.h"
            alias.parent.mkdir(parents=True)
            alias.symlink_to(original)
            def record(path, count, names):
                return {"file": str(path), "lines": [{"line_number": 1, "count": count, "branches": []}],
                        "functions": [{"name": name, "demangled_name": name, "lineno": 1,
                                       "execution_count": hits, "blocks_percent": 100.0 if hits else 0.0}
                                      for name, hits in names]}
            data = root / "cpp.json"
            data.write_text(report.json.dumps({"gcovr/format_version": "0.14", "files": [
                record(original, 3, [("_Z6sampleIiEiv", 3)]),
                record(alias, 0, [("_Z6sampleIiEiv", 0), ("_Z6sampleIlEiv", 0)])]}))
            report.canonicalize_cpp_json(data, root)
            files = report.json.loads(data.read_text())["files"]
            self.assertEqual(1, len(files))
            self.assertEqual("include/openglad/sample.h", files[0]["file"])
            self.assertEqual({"_Z6sampleIiEiv": 3, "_Z6sampleIlEiv": 0},
                             {fn["name"]: fn["execution_count"] for fn in files[0]["functions"]})

            # A worker scans its object directories as separate partitions:
            # merging those saved traces must add their counts and keep each
            # one's zero-hit bodies, leaving the partition files untouched.
            parts = []
            for index, (path, count, names) in enumerate((
                    (original, 3, [("_Z6sampleIiEiv", 3)]),
                    (alias, 2, [("_Z6sampleIiEiv", 2), ("_Z6sampleIlEiv", 0)]))):
                part = root / f"part-{index}.json"
                part.write_text(report.json.dumps({"gcovr/format_version": "0.14",
                                                   "files": [record(path, count, names)]}))
                parts.append(part)
            before = [part.read_text() for part in parts]
            combined = root / "combined.json"
            report.canonicalize_cpp_json(combined, root, sources=parts)
            files = report.json.loads(combined.read_text())["files"]
            self.assertEqual(["include/openglad/sample.h"], [item["file"] for item in files])
            self.assertEqual([5], [line["count"] for line in files[0]["lines"]])
            self.assertEqual({"_Z6sampleIiEiv": 5, "_Z6sampleIlEiv": 0},
                             {fn["name"]: fn["execution_count"] for fn in files[0]["functions"]})
            self.assertEqual(before, [part.read_text() for part in parts])
            with self.assertRaisesRegex(ValueError, "no C\\+\\+ coverage JSON"):
                report.canonicalize_cpp_json(root / "empty.json", root, sources=[])

    def test_copied_component_headers_must_match_source_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            original = root / "include/openglad/sample.h"
            original.parent.mkdir(parents=True)
            original.write_text("current body\n")
            alias = root / "build/ci-coverage/component_includes/core/openglad/sample.h"
            alias.parent.mkdir(parents=True)
            alias.write_text("different body\n")
            with self.assertRaisesRegex(ValueError, "differs"):
                report.canonical_cpp_source(str(alias), root)
            alias.write_text(original.read_text())
            self.assertEqual("include/openglad/sample.h", report.canonical_cpp_source(str(alias), root))


class ProductionVariantTests(unittest.TestCase):
    def test_only_testing_object_directories_are_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            records = [
                {"file": "src/game.cpp", "arguments": ["c++", "-DTESTING", "-o", "CMakeFiles/game_test.dir/src/game.cpp.o"]},
                {"file": "src/game.cpp", "arguments": ["c++", "-DTESTING_SUPPORT", "-o", "CMakeFiles/game.dir/src/game.cpp.o"]},
                {"file": "tests/test.cpp", "command": "c++ -DTESTING=0 -o CMakeFiles/test.dir/tests/test.cpp.o"},
            ]
            (root / "compile_commands.json").write_text(report.json.dumps(records))
            patterns = report.production_variant_exclusions(root)
            self.assertEqual(2, len(patterns))
            self.assertTrue(any(report.re.search(pattern, "/checkout/build/CMakeFiles/game_test.dir/src/game.cpp.gcno") for pattern in patterns))
            self.assertFalse(any(report.re.search(pattern, "/checkout/build/CMakeFiles/game.dir/src/game.cpp.gcno") for pattern in patterns))
            self.assertFalse(any(report.re.search(pattern, "/checkout/build/CMakeFiles/game_test.dir_extra/src/game.cpp.gcno") for pattern in patterns))

    def test_production_collection_requires_the_variant_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            with self.assertRaisesRegex(ValueError, "compilation records"):
                report.production_variant_exclusions(root)
            (root / "compile_commands.json").write_text("[]")
            with self.assertRaisesRegex(ValueError, "no TESTING"):
                report.production_variant_exclusions(root)

    def test_production_misses_are_not_hidden_by_the_union(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "src").mkdir()
            (root / "src/game.cpp").write_text("#if OTHER\nint a();\n#elif defined(TESTING)\nint b();\n#else\nint f() { return 1; }\n#endif\n")
            production = report.FileCoverage("src/game.cpp", {6: 0}, {"f()": (6, 0)})
            aggregate = report.FileCoverage("src/game.cpp", {6: 8}, {"f()": (6, 8)})
            summary = report.production_variant_summary([production], [aggregate], root)
            self.assertEqual(0, summary["totals"]["functions_hit"])
            self.assertFalse(summary["gated"])
            self.assertEqual([{"path": "src/game.cpp", "line": 6, "name": "f()", "covered_in_union": True}], summary["uncovered_functions"])
            self.assertEqual([{"path": "src/game.cpp", "line": 6, "covered_in_union": True, "source_text": "int f() { return 1; }"}], summary["uncovered_testing_conditional_lines"])
            self.assertIn(2, report.testing_conditional_lines(root / "src/game.cpp"))


if __name__ == "__main__":
    unittest.main()
