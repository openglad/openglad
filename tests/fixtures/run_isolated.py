#!/usr/bin/env python3
"""Run a native test beside private writable assets and a private cwd.

get_asset_path resolves /proc/self/exe, so a symlink to the binary would still
resolve shared build assets. A hardlink (copy across filesystems) gives it a
private executable path without adding a product-only-for-tests path override.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


def run(source, executable, arguments):
    source = Path(source).resolve()
    executable = Path(executable).absolute()
    assets = executable.parent
    # Keep the executable cohort on its build filesystem: a system /tmp on
    # another mount would copy every large debug binary for every test entry.
    with tempfile.TemporaryDirectory(prefix=f".{executable.name}-", dir=assets) as directory:
        workspace = Path(directory)
        private_binary = workspace / executable.name
        # Preserve the installed executable cohort: report tests launch
        # og_lua_lines beside themselves, and subprocess tests relaunch their
        # own binary. Every companion must resolve these same private assets.
        for companion in assets.iterdir():
            if not companion.is_file() or not os.access(companion, os.X_OK):
                continue
            destination = workspace / companion.name
            try:
                os.link(companion.resolve(), destination)
            except OSError:
                shutil.copy2(companion, destination)
        # Both direct filesystem writes and asset fallback writes stay private.
        # Copy instead of linking even the small art/pack trees: fixture tests
        # are permitted to exercise missing or damaged installed resources.
        for name in ("cfg", "pix", "sound", "packs", "builtin", "builtin-dev"):
            shutil.copytree(assets / name, workspace / name)
        for name in ("campaigns", "scen"):
            shutil.copytree(source / name, workspace / name)
        for name in ("temp", "save"):
            (workspace / name).mkdir()
        # Source-oracle tests inspect these paths, never mutate them. Explicit
        # names avoid copying unrelated working files or developer credentials.
        for name in ("src", "include", "tests", "scripts", "docs", "tools"):
            (workspace / name).symlink_to(source / name, target_is_directory=True)
        return subprocess.run([str(private_binary), *arguments], cwd=workspace,
                              stdin=subprocess.DEVNULL, check=False).returncode


if __name__ == "__main__":
    code = run(sys.argv[1], sys.argv[2], sys.argv[3:])
    raise SystemExit(code if code >= 0 else 128 - code)
