#!/usr/bin/env python3
"""Deterministic .glad (zip) writer for the built-in campaigns.

The campaign archives are BUILD OUTPUTS composed from committed source
trees (campaigns/<id>/ plus, for pack-bearing campaigns, a tools/ pack
dir mapped under packs/<pack-id>/). The container must be byte-for-byte
reproducible: two builds of the same tree produce identical archives on
any machine, so archive diffs are always content diffs.

Determinism rules (all deliberate, all load-bearing):
  * members are STORED, never deflated — no zlib version can vary the bytes
    (the corpus is ~1 MB and dominated by already-compressed PNGs; HTTP
    compression covers the wire for the web build),
  * member order is the bytewise sort of the archive paths,
  * every timestamp is the DOS epoch (1980-01-01 00:00:00),
  * no directory entries — PhysFS and libzip both synthesize directories
    from member paths,
  * fixed unix permissions (0644) and a fixed creator system.

The output file is left untouched (mtime preserved) when the composed
bytes already match, so downstream steps that depend on the archive file
(the emscripten preload link, staged-asset consumers) do not churn.

Usage:
  make_glad.py OUTPUT.glad --root DIR [--root DIR@ARCPREFIX ...]

Each --root contributes every regular file under DIR at its DIR-relative
path, prefixed with ARCPREFIX/ when given. Files named .gitkeep are
git-empty-dir placeholders and are skipped; any other dotfile is an
error (nothing hidden ships silently). Two roots contributing the same
archive path is an error.
"""

from __future__ import annotations

import argparse
import os
import sys
import zipfile

DOS_EPOCH = (1980, 1, 1, 0, 0, 0)


def collect_members(roots: list[tuple[str, str]]) -> dict[str, str]:
    """Map archive path -> filesystem path for every file under the roots."""
    members: dict[str, str] = {}
    for root, prefix in roots:
        if not os.path.isdir(root):
            sys.exit(f"make_glad: root is not a directory: {root}")
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames.sort()
            for name in sorted(filenames):
                if name == ".gitkeep":
                    continue
                if name.startswith("."):
                    sys.exit(
                        f"make_glad: refusing hidden file "
                        f"{os.path.join(dirpath, name)} (only .gitkeep is "
                        f"understood; delete it or rename it)"
                    )
                fs_path = os.path.join(dirpath, name)
                rel = os.path.relpath(fs_path, root)
                arcname = rel.replace(os.sep, "/")
                if prefix:
                    arcname = f"{prefix}/{arcname}"
                if arcname in members:
                    sys.exit(
                        f"make_glad: archive path collision on {arcname}: "
                        f"{members[arcname]} vs {fs_path}"
                    )
                members[arcname] = fs_path
    if not members:
        sys.exit("make_glad: no members collected — empty campaign tree?")
    return members


def compose(members: dict[str, str]) -> bytes:
    """Build the deterministic archive bytes in memory."""
    import io

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", allowZip64=False) as zf:
        for arcname in sorted(members, key=lambda n: n.encode("utf-8")):
            with open(members[arcname], "rb") as fh:
                data = fh.read()
            zi = zipfile.ZipInfo(arcname, date_time=DOS_EPOCH)
            zi.compress_type = zipfile.ZIP_STORED
            zi.create_system = 3  # unix
            zi.external_attr = 0o100644 << 16
            zf.writestr(zi, data)
    return buf.getvalue()


def parse_root(spec: str) -> tuple[str, str]:
    root, sep, prefix = spec.partition("@")
    if sep and not prefix:
        sys.exit(f"make_glad: empty archive prefix in root spec: {spec}")
    return root, prefix.strip("/")


def main() -> None:
    ap = argparse.ArgumentParser(allow_abbrev=False)
    ap.add_argument("output")
    ap.add_argument(
        "--root",
        action="append",
        required=True,
        metavar="DIR[@ARCPREFIX]",
        help="source dir to compose (optionally mapped under ARCPREFIX/)",
    )
    args = ap.parse_args()

    roots = [parse_root(spec) for spec in args.root]
    data = compose(collect_members(roots))

    out = args.output
    try:
        with open(out, "rb") as fh:
            if fh.read() == data:
                return  # byte-identical: keep the existing file's mtime
    except OSError:
        pass

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    tmp = f"{out}.tmp.{os.getpid()}"
    with open(tmp, "wb") as fh:
        fh.write(data)
    os.replace(tmp, out)


if __name__ == "__main__":
    main()
