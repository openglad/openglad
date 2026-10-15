#!/usr/bin/env python3
"""The New Specials setting, seen by the headless text client.

Runs openglad_text --protocol on the same level, crew and seed twice with
--new-specials 0 and twice with --new-specials 1, 600 ticks each, and reads
the census line. The crew is four level-10 faeries: with the setting off
they have no specials, with it on the bots blink, glimmer, hasten and wish.

  1. each setting gives the same census line on both of its runs (the run
     is deterministic, so a difference below can only come from the
     setting);
  2. the census lists the four faeries in both;
  3. the census line with the setting on differs from the one with it off.

Usage: test_text_client_new_specials.py <path to openglad_text>
Exit code: 0 on success, 1 on any failure.
"""

import json
import os
import subprocess
import sys
import tempfile

LEVEL = "1"
CREW = "7,7,7,7"  # FAMILY_FAERIE x 4
CREW_FAMILY = 7
TEAM_LEVEL = "10"
SEED = "42"
TICKS = 600


def census_line(text_bin, new_specials, home, timeout):
    """One protocol session; returns the census line as printed."""
    env = dict(os.environ)
    # A fresh home and config dir: a headless run must never write the
    # checkout's own cfg/openglad.yaml.
    env["HOME"] = home
    env["OPENGLAD_CONFIG_DIR"] = os.path.join(home, "config") + os.sep
    args = [
        text_bin, "--protocol",
        "--campaign", "gladiator",
        "--level", LEVEL,
        "--team", CREW,
        "--team-level", TEAM_LEVEL,
        "--seed", SEED,
        "--new-specials", str(new_specials),
    ]
    proc = subprocess.run(
        args,
        input="tick %d\ncensus\nquit\n" % TICKS,
        capture_output=True,
        text=True,
        timeout=timeout,
        env=env,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError("openglad_text --new-specials %d exited %d:\n%s"
                           % (new_specials, proc.returncode, proc.stderr))
    lines = [line for line in proc.stdout.splitlines()
             if '"cmd":"census"' in line]
    if len(lines) != 1:
        raise RuntimeError("openglad_text --new-specials %d printed %d census "
                           "lines, expected 1:\n%s"
                           % (new_specials, len(lines), proc.stdout))
    return lines[0]


def faeries_in(line):
    census = json.loads(line)
    return sum(1 for member in census.get("crew", [])
               if member.get("family") == CREW_FAMILY)


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 1
    text_bin = argv[1]
    if not os.access(text_bin, os.X_OK):
        print("FAIL: cannot run %s" % text_bin, file=sys.stderr)
        return 1
    timeout = int(os.environ.get("OPENGLAD_TEXT_TIMEOUT", "55"))

    failures = []
    with tempfile.TemporaryDirectory() as home:
        lines = {}
        for new_specials in (0, 1):
            first = census_line(text_bin, new_specials, home, timeout)
            second = census_line(text_bin, new_specials, home, timeout)
            print("new_specials=%d: %s" % (new_specials, first))
            if first != second:
                failures.append(
                    "--new-specials %d is not deterministic:\n  %s\n  %s"
                    % (new_specials, first, second))
            if faeries_in(first) != 4:
                failures.append(
                    "--new-specials %d: the census lists %d faeries, "
                    "expected 4" % (new_specials, faeries_in(first)))
            lines[new_specials] = first
        if lines[0] == lines[1]:
            failures.append(
                "the census is the same with New Specials off and on: the "
                "faerie bots cast nothing new")

    for failure in failures:
        print("FAIL: " + failure, file=sys.stderr)
    if failures:
        return 1
    print("PASS: the faerie crew's census differs with New Specials on")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
