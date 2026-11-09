#!/usr/bin/env python3
"""Give one parity census a fresh identity and remove its observation files."""
from pathlib import Path
import shutil
import sys
import uuid


def fixture(action: str, directory: Path) -> None:
    if action not in ("setup", "cleanup"):
        raise ValueError("expected setup or cleanup")
    if directory.is_symlink():
        raise ValueError("census directory must not be a symlink")
    marker = directory / ".parity-census-owned"
    if directory.exists():
        if not marker.is_file() or marker.read_text() != "openglad parity census\n":
            raise ValueError(f"refusing to replace an unrelated directory: {directory}")
        shutil.rmtree(directory)
    if action == "setup":
        directory.mkdir(parents=True)
        marker.write_text("openglad parity census\n")
        (directory / "nonce").write_text(uuid.uuid4().hex + "\n")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: parity_census.py setup|cleanup DIRECTORY")
    fixture(sys.argv[1], Path(sys.argv[2]))
