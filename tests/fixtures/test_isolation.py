#!/usr/bin/env python3
"""Prove two group processes cannot rewrite each other's installed fixtures."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from run_isolated import run


class NativeWorkspace(unittest.TestCase):
    def test_parallel_processes_have_private_assets_and_preserve_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            assets = root / "assets"
            assets.mkdir()
            # Use a real binary: a symlink would make /proc/self/exe expose the
            # shared installation even if argv[0] looked private.
            binary = assets / "python"
            shutil.copy2(Path("/proc/self/exe").resolve(), binary)
            shutil.copy2(binary, assets / "companion")
            for name in ("cfg", "pix", "sound", "packs", "builtin", "builtin-dev"):
                (assets / name).mkdir()
                (assets / name / "fixture").write_text("original")
            for name in ("campaigns", "scen", "src", "include", "tests",
                         "scripts", "docs", "tools"):
                (source / name).mkdir(parents=True)
                (source / name / "fixture").write_text("original")
            child = r'''
from pathlib import Path
import os
import sys
workspace = Path.cwd()
import subprocess
probe = subprocess.run([str(workspace / 'companion'), '-c',
                        'import os; print(os.readlink("/proc/self/exe"))'],
                       check=True, text=True, capture_output=True)
assert Path(probe.stdout.strip()).parent == workspace
assert Path(os.readlink('/proc/self/exe')).parent == workspace
for name in ('cfg', 'pix', 'sound', 'packs', 'builtin', 'builtin-dev', 'campaigns', 'scen'):
    fixture = workspace / name / 'fixture'
    assert fixture.read_text() == 'original'
    fixture.write_text(sys.argv[1])
for name in ('temp', 'save'):
    (workspace / name / 'same-name').write_text(sys.argv[1])
Path(sys.argv[2]).write_text(str(workspace))
raise SystemExit(37)
'''
            def invoke(index):
                return run(source, binary, ["-c", child, str(index),
                                            str(root / f"path-{index}")])
            with ThreadPoolExecutor(max_workers=2) as pool:
                self.assertEqual([37, 37], list(pool.map(invoke, range(2))))
            paths = [(root / f"path-{i}").read_text() for i in range(2)]
            self.assertNotEqual(paths[0], paths[1])
            for path in paths:
                self.assertFalse(Path(path).exists(), "workspace must be cleaned")
            for fixture in [*assets.glob("*/fixture"), *source.glob("*/fixture")]:
                self.assertEqual("original", fixture.read_text(), str(fixture))


if __name__ == "__main__":
    unittest.main()
