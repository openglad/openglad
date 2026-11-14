"""A failed parser or lost evidence must never become a successful fuzz phase."""
import contextlib
import gzip
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

from run import instrumentation, run_phase


class ProductionInstrumentation(unittest.TestCase):
    def test_rejects_harness_only_instrumentation_and_testing_variants(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            commands = []
            for name in ("level_file_io.cpp", "save_data.cpp"):
                source = root / "src/resources" / name
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text("production source\n")
                commands.append(dict(file=str(source), arguments=["clang++", "-c", str(source),
                    "-fsanitize=fuzzer-no-link,address,undefined,float-cast-overflow"]))
            database = root / "compile_commands.json"
            database.write_text(json.dumps(commands))
            self.assertEqual(set(instrumentation(root)), {
                "src/resources/level_file_io.cpp", "src/resources/save_data.cpp"})
            commands[0]["arguments"].pop()
            database.write_text(json.dumps(commands))
            with self.assertRaisesRegex(RuntimeError, "not fully fuzz-instrumented"):
                instrumentation(root)
            commands[0]["arguments"].extend([
                "-fsanitize=fuzzer-no-link,address,undefined,float-cast-overflow", "-DTESTING"])
            database.write_text(json.dumps(commands))
            with self.assertRaisesRegex(RuntimeError, "TESTING implementation"):
                instrumentation(root)
            database.write_text("[]")
            with self.assertRaisesRegex(RuntimeError, "records are missing"):
                instrumentation(root)


class PhaseEvidence(unittest.TestCase):
    def phase(self, script, timeout=5):
        root = Path(self.directory.name)
        with contextlib.redirect_stdout(io.StringIO()):
            return run_phase(Path(sys.executable), ["-c", script], root, "probe", timeout)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)

    def test_nonzero_exit_preserves_failure_and_full_log(self):
        with self.assertRaises(RuntimeError):
            self.phase("import sys; print('sanitizer repro'); sys.exit(23)")
        root = Path(self.directory.name)
        self.assertEqual(json.loads((root / "probe.json").read_text())["exit_code"], 23)
        self.assertIn(b"sanitizer repro", gzip.decompress((root / "probe.log.gz").read_bytes()))

    def test_hung_process_is_killed_and_is_a_failure(self):
        with self.assertRaises(RuntimeError):
            self.phase("import time; time.sleep(30)", timeout=1)
        result = json.loads((Path(self.directory.name) / "probe.json").read_text())
        self.assertTrue(result["timeout"])
        self.assertNotEqual(result["exit_code"], 0)

    def test_reports_initial_and_final_instrumented_coverage(self):
        result = self.phase("print('#4 INITED cov: 100 ft: 110'); print('#80 DONE cov: 150 ft: 220')")
        self.assertEqual(result["first"]["edges"], 100)
        self.assertEqual(result["last"], dict(executions=80, event="DONE", edges=150, features=220))


if __name__ == "__main__":
    unittest.main()
