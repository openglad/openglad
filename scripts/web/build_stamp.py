#!/usr/bin/env python3
"""Read the WASM build's version stamp for the web workflow.

og_git_hash.h names the commit, its count and whether the commit is on
master's history; only a master build has a version (#508). A pull-request
build counts commits no one else will ever have, so its count names nothing:
it shows its hash alone and gets no version and no v2-<count> alias.

Both "Read the build stamp" steps of .github/workflows/wasm-e2e.yml call
this, so the build and deploy jobs read the stamp one way. It also checks the
stamp against the checkout and against the workflow event, so a deployed page
can never claim a build the main menu disagrees with.

Standard library only - this runs on a bare CI image.
"""

from __future__ import annotations

import argparse
import re
import sys
from typing import Optional

_MINOR_RE = re.compile(r"^#define OPENGLAD_VERSION_MINOR ([0-9]+)$", re.M)
_HASH_RE = re.compile(r'^#define OPENGLAD_GIT_HASH "([^"]*)"$', re.M)
_ON_MASTER_RE = re.compile(r"^#define OPENGLAD_VERSION_ON_MASTER ([01])$", re.M)


class StampError(Exception):
    """The stamp is missing, or disagrees with the checkout or the event."""


def parse_header(text: str) -> dict:
    """The three facts the stamp carries: minor, hash and on_master."""
    minor = _MINOR_RE.search(text)
    commit = _HASH_RE.search(text)
    on_master = _ON_MASTER_RE.search(text)
    if minor is None or commit is None or on_master is None or not commit.group(1):
        raise StampError("no version stamp")
    return {
        "minor": int(minor.group(1)),
        "hash": commit.group(1),
        "on_master": on_master.group(1) == "1",
    }


def stamp_outputs(text: str, expect_on_master: bool, rev_count: int,
                  short_hash: str) -> dict:
    """The workflow outputs for this stamp, after checking it.

    ``version`` and ``alias`` are empty for a build off master: there is no
    version to name and no archive alias to link.
    """
    stamp = parse_header(text)
    if stamp["minor"] != rev_count:
        raise StampError(
            f"stamp 2.{stamp['minor']} != git rev-list --count HEAD "
            f"(shallow checkout?)")
    if stamp["hash"] != short_hash:
        raise StampError("WASM build stamp does not match checkout")
    if stamp["on_master"] != expect_on_master:
        raise StampError(
            f"the stamp says on_master={int(stamp['on_master'])} but the "
            f"workflow event says {int(expect_on_master)}")
    on_master = stamp["on_master"]
    return {
        "minor": str(stamp["minor"]),
        "hash": stamp["hash"],
        "on_master": "1" if on_master else "0",
        "version": f"2.{stamp['minor']}" if on_master else "",
        "alias": f"v2-{stamp['minor']}" if on_master else "",
    }


def _bit(value: str) -> bool:
    if value not in ("0", "1"):
        raise argparse.ArgumentTypeError("expected 0 or 1")
    return value == "1"


def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--header", required=True,
                        help="the build's og_git_hash.h")
    parser.add_argument("--expect-on-master", required=True, type=_bit,
                        help="1 for a master push or dispatch, else 0")
    parser.add_argument("--rev-count", required=True, type=int,
                        help="git rev-list --count HEAD of the checkout")
    parser.add_argument("--short-hash", required=True,
                        help="git rev-parse --short=8 HEAD of the checkout")
    parser.add_argument("--output-file", required=True,
                        help="the file key=value lines are appended to "
                             "($GITHUB_OUTPUT)")
    args = parser.parse_args(argv)
    try:
        with open(args.header, encoding="utf-8") as handle:
            text = handle.read()
        outputs = stamp_outputs(text, args.expect_on_master, args.rev_count,
                                args.short_hash)
    except (OSError, StampError) as error:
        message = str(error)
        if isinstance(error, StampError) and message == "no version stamp":
            message = f"{args.header} has no version stamp"
        print(f"::error::{message}")
        return 1
    with open(args.output_file, "a", encoding="utf-8") as handle:
        for key in ("version", "minor", "hash", "alias", "on_master"):
            handle.write(f"{key}={outputs[key]}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
