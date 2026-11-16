#!/usr/bin/env python3
"""Seed tables of the campaign calibration suites: run them, compare them.

The calibration suites of Westlands, The Long Season, Imaginations and the
Tower, and the classic campaigns' suite, each have a measure mode that plays
the stand-in crew of eight for 600 ticks on every seed of
OG_CALIBRATION_SEEDS and prints, per level, the crew members standing and
the crew's damage taken on each seed (tests/unit/westlands_sim_fixture.h:
calibration_seeds() and print_calibn_lines()). This script runs those modes
and compares two runs.

  run       plays one suite in measure mode for a seed list and a value of
            the Gameplay Fixes setting, and writes its lines to a file.
  verdict   compares two such files level by level, by the rule below.
  playtest  compares two sweeps of the playtest scripts
            (scripts/westlands_playtest.sh and its two siblings).
  --self-test  checks the formulas on fixed lists (a CTest entry).

The rule, for one level over n seeds, x the survivors before and y after on
each seed (both runs play the same seeds, so the comparison is paired):
d = y - x per seed, m = the mean of d, se = sqrt(sample variance of d / n),
with the variance divided by n - 1. One word per level:

  floor     the mean before is under 1.0: the count sits on its floor and
            cannot judge the level;
  fell      m < 0 and m <= -3 se;
  fell<3    m < 0 and m <= -2 se: a fall the measure cannot prove;
  too far   m > 0 and m >= 2 se;
  back      m >= -se (with se 0: m >= 0);
  not back  anything else.

--rule two-lists judges list A and list B apart at two errors: "fell" when
both lists fell, "fell(A)" when only list A did; "too far" and "back" are
judged on list A. Every comparison is made in exact fractions, so a level
that sits exactly on a boundary reads the same on every machine.

Seed lists: A is 42, 1337, 2025 and 1 to 17 (the twenty of the docs), B is
101 to 120, AB is A then B.

The script starts no build and takes no lock: run it from the repository's
build environment (`nix develop`), under whatever lock the caller holds.
Python 3.9 or newer, standard library only.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from fractions import Fraction

LIST_A = [42, 1337, 2025] + list(range(1, 18))
LIST_B = list(range(101, 121))

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# suite -> (test binary under build/ci-test, gtest filter, its measure switch)
SUITES = {
    "westlands": ("og_unit_sim", "WestlandsCampaignTest.curve_levels_*",
                  "WESTLANDS_CALIBRATION_MEASURE"),
    "longseason": ("og_unit_sim", "LongSeasonCalibration.curve_levels_*",
                   "LONGSEASON_CALIBRATION_MEASURE"),
    "imaginations": ("og_unit_sim", "ImaginationsCalibration.curve_levels_*",
                     "IMAGINATIONS_CALIBRATION_MEASURE"),
    "tower": ("og_unit_tower_calibration", "TowerCalibration.*",
              "TOWER_CALIBRATION_MEASURE"),
    "classic": ("og_unit_sim", "ClassicCampaignsCalibration.*",
                "CLASSIC_CALIBRATION_MEASURE"),
}

# The two line forms print_calibn_lines() writes, with or without "TOWER ".
LINE_RE = re.compile(
    r"^(?P<tower>TOWER )?CALIBN(?P<dash>-TAKEN)? "
    r"(?P<keys>(?:[a-z_]+=\S+ )+)"
    r"(?P<what>survivors|taken)@600:(?P<values>(?: -?[0-9]+)*)[ \t]*$")

HEADER_PREFIX = "# seed_table "


class TableError(Exception):
    """A file or an output that is not what this script reads."""


# ---------------------------------------------------------------------------
# Seed lists and table files
# ---------------------------------------------------------------------------

def resolve_seeds(spec):
    """A, B, AB, or a comma-separated list of numbers."""
    if spec == "A":
        return list(LIST_A)
    if spec == "B":
        return list(LIST_B)
    if spec == "AB":
        return LIST_A + LIST_B
    items = spec.split(",")
    if not items or any(not item.isdigit() for item in items):
        raise TableError(f"--seeds {spec!r} is not A, B, AB or a comma-separated list of numbers")
    seeds = [int(item) for item in items]
    if any(seed > 0xFFFFFFFF for seed in seeds):
        raise TableError(f"--seeds {spec!r}: a seed is wider than 32 bits")
    return seeds


def parse_line(line):
    """(row key, "alive" or "taken", values) of a CALIBN line, else None."""
    match = LINE_RE.match(line)
    if match is None:
        return None
    kind = "taken" if match.group("dash") else "alive"
    if (kind == "taken") != (match.group("what") == "taken"):
        return None
    key = (match.group("tower") or "") + match.group("keys").strip()
    values = [int(v) for v in match.group("values").split()]
    return key, kind, values


def key_fields(key):
    fields = {}
    for token in key.split():
        name, sep, value = token.partition("=")
        if sep:
            fields[name] = value
    return fields


def row_sort_key(key):
    fields = key_fields(key)
    number = fields.get("level", fields.get("floor", "0"))
    return (key.startswith("TOWER "), fields.get("campaign", ""),
            int(number) if number.lstrip("-").isdigit() else 0, key)


class Table:
    """The rows of one measuring run: key -> {"alive": [...], "taken": [...]}."""

    def __init__(self, header=None, seeds=None):
        self.header = dict(header or {})
        self.seeds = list(seeds or [])
        self.rows = {}

    def add(self, key, kind, values, where):
        row = self.rows.setdefault(key, {})
        if kind in row:
            raise TableError(f"{where}: a second {kind} line for {key!r}")
        row[kind] = values

    def check(self, where, need_taken):
        if not self.rows:
            raise TableError(f"{where}: no CALIBN line")
        for key, row in self.rows.items():
            if "alive" not in row:
                raise TableError(f"{where}: {key!r} has a damage line and no survivor line")
            if need_taken and "taken" not in row:
                raise TableError(f"{where}: {key!r} has no CALIBN-TAKEN line")
            if any(v < 0 for v in row["alive"]):
                raise TableError(f"{where}: {key!r} holds a negative survivor count: "
                                 "the level did not load or the crew did not deploy")
            for kind, values in row.items():
                if len(values) != len(self.seeds):
                    raise TableError(
                        f"{where}: {key!r} has {len(values)} {kind} values for "
                        f"{len(self.seeds)} seeds")

    def sorted_keys(self):
        return sorted(self.rows, key=row_sort_key)


def extract_lines(output, where):
    """The CALIBN lines of a test binary's output, in the order printed. A
    line that names CALIBN in any other form is an error, never skipped."""
    kept = []
    for line in output.splitlines():
        if "CALIBN" not in line:
            continue
        if parse_line(line) is None:
            raise TableError(f"{where}: unreadable CALIBN line: {line!r}")
        kept.append(line)
    return kept


def table_from_lines(lines, seeds, header, where, need_taken):
    table = Table(header, seeds)
    for line in lines:
        key, kind, values = parse_line(line)
        table.add(key, kind, values, where)
    table.check(where, need_taken)
    return table


def header_line(fields):
    return HEADER_PREFIX + " ".join(f"{k}={v}" for k, v in fields.items())


def read_table(path):
    header = None
    lines = []
    with open(path, encoding="utf-8") as handle:
        for number, raw in enumerate(handle, 1):
            line = raw.rstrip("\n")
            if not line.strip():
                continue
            if line.startswith(HEADER_PREFIX):
                if header is not None:
                    raise TableError(f"{path}:{number}: a second header line")
                header = key_fields(line[len(HEADER_PREFIX):])
                continue
            if line.startswith("#"):
                continue
            if parse_line(line) is None:
                raise TableError(f"{path}:{number}: not a CALIBN line: {line!r}")
            lines.append(line)
    if header is None or "seeds" not in header:
        raise TableError(f"{path}: no '{HEADER_PREFIX}... seeds=...' header line")
    seeds = resolve_seeds(header["seeds"])
    return table_from_lines(lines, seeds, header, path, need_taken=False)


# ---------------------------------------------------------------------------
# The statistics (exact fractions)
# ---------------------------------------------------------------------------

def sample_variance(values):
    """The variance of a sample, divided by n - 1."""
    n = len(values)
    mean = Fraction(sum(values), n)
    return sum((Fraction(v) - mean) ** 2 for v in values) / (n - 1)


class Paired:
    """Before x and after y on the same seeds."""

    def __init__(self, xs, ys):
        if len(xs) != len(ys):
            raise TableError("paired lists of different lengths")
        if len(xs) < 2:
            raise TableError("an error needs at least two seeds")
        n = len(xs)
        diffs = [y - x for x, y in zip(xs, ys)]
        self.n = n
        self.mean_x = Fraction(sum(xs), n)
        self.mean_y = Fraction(sum(ys), n)
        self.se2_x = sample_variance(xs) / n
        self.se2_y = sample_variance(ys) / n
        self.m = Fraction(sum(diffs), n)
        self.se2 = sample_variance(diffs) / n

    @property
    def se(self):
        return math.sqrt(self.se2)

    def ratio(self):
        """m / se, or None when se is 0."""
        if self.se2 == 0:
            return None
        return float(self.m) / self.se


def falls_by(m, se2, k):
    """m < 0 and m <= -k se, in exact arithmetic."""
    return m < 0 and m * m >= k * k * se2


def rises_by(m, se2, k):
    """m > 0 and m >= k se."""
    return m > 0 and m * m >= k * k * se2


def is_back(m, se2):
    """m >= -se; with se 0, m >= 0."""
    return m >= 0 or m * m <= se2


def pooled_word(p):
    if p.mean_x < 1:
        return "floor"
    if falls_by(p.m, p.se2, 3):
        return "fell"
    if falls_by(p.m, p.se2, 2):
        return "fell<3"
    if rises_by(p.m, p.se2, 2):
        return "too far"
    if is_back(p.m, p.se2):
        return "back"
    return "not back"


def two_lists_word(pa, pb):
    if pa.mean_x < 1:
        return "floor"
    fell_a = falls_by(pa.m, pa.se2, 2)
    fell_b = falls_by(pb.m, pb.se2, 2)
    if fell_a and fell_b:
        return "fell"
    if fell_a:
        return "fell(A)"
    if rises_by(pa.m, pa.se2, 2):
        return "too far"
    if is_back(pa.m, pa.se2):
        return "back"
    return "not back"


def subset(values, seeds, wanted):
    picked = set(wanted)
    return [v for v, s in zip(values, seeds) if s in picked]


def fmt(value, digits=2):
    return f"{float(value):.{digits}f}"


def fmt_ratio(p):
    r = p.ratio()
    return "-" if r is None else f"{r:.1f}"


def mean_text(values):
    return "-" if not values else fmt(Fraction(sum(values), len(values)))


# ---------------------------------------------------------------------------
# verdict
# ---------------------------------------------------------------------------

def compare_tables(before, after):
    if before.seeds != after.seeds:
        raise TableError("the two files hold different seed lists")
    missing = sorted(set(before.rows) ^ set(after.rows), key=row_sort_key)
    if missing:
        raise TableError("the two files hold different levels: " + "; ".join(missing[:6]))


def group_of(key, suite):
    return key_fields(key).get("campaign", suite)


def verdict_lines(before, after, rule):
    compare_tables(before, after)
    seeds = before.seeds
    a_seeds = [s for s in seeds if s in set(LIST_A)]
    b_seeds = [s for s in seeds if s in set(LIST_B)]
    out = []
    for name, table in (("before", before), ("after", after)):
        out.append(f"{name}: " + " ".join(f"{k}={v}" for k, v in table.header.items()))
    suite = before.header.get("suite", "suite")
    if rule == "two-lists":
        if len(a_seeds) < 2 or len(b_seeds) < 2:
            raise TableError("--rule two-lists needs at least two seeds of list A and two of list B")
        out.append(f"{'row':<46} {'nA':>3} {'A before':>8} {'A after':>8} {'mA':>6} {'seA':>5} "
                   f"{'nB':>3} {'mB':>6} {'seB':>5}  word")
    else:
        out.append(f"{'row':<46} {'n':>3} {'before (se)':>13} {'after (se)':>13} {'m':>6} "
                   f"{'se':>5} {'m/se':>6} {'A bef':>6} {'A aft':>6} {'taken bef':>9} "
                   f"{'taken aft':>9}  word")
    for key in before.sorted_keys():
        xs = before.rows[key]["alive"]
        ys = after.rows[key]["alive"]
        if rule == "two-lists":
            pa = Paired(subset(xs, seeds, LIST_A), subset(ys, seeds, LIST_A))
            pb = Paired(subset(xs, seeds, LIST_B), subset(ys, seeds, LIST_B))
            out.append(f"{key:<46} {pa.n:>3} {fmt(pa.mean_x):>8} {fmt(pa.mean_y):>8} "
                       f"{fmt(pa.m):>6} {fmt(pa.se):>5} {pb.n:>3} {fmt(pb.m):>6} "
                       f"{fmt(pb.se):>5}  {two_lists_word(pa, pb)}")
            continue
        p = Paired(xs, ys)
        taken_x = before.rows[key].get("taken", [])
        taken_y = after.rows[key].get("taken", [])
        out.append(
            f"{key:<46} {p.n:>3} {fmt(p.mean_x):>6} ({fmt(math.sqrt(p.se2_x))}) "
            f"{fmt(p.mean_y):>6} ({fmt(math.sqrt(p.se2_y))}) {fmt(p.m):>6} {fmt(p.se):>5} "
            f"{fmt_ratio(p):>6} {mean_text(subset(xs, seeds, LIST_A)):>6} "
            f"{mean_text(subset(ys, seeds, LIST_A)):>6} {mean_text(taken_x):>9} "
            f"{mean_text(taken_y):>9}  {pooled_word(p)}")
    groups = []
    for key in before.sorted_keys():
        group = group_of(key, suite)
        if group not in groups:
            groups.append(group)
    for group in groups:
        keys = [k for k in before.sorted_keys() if group_of(k, suite) == group]
        totals_x = [sum(before.rows[k]["alive"][i] for k in keys) for i in range(len(seeds))]
        totals_y = [sum(after.rows[k]["alive"][i] for k in keys) for i in range(len(seeds))]
        line = f"SUM {group} rows={len(keys)} " + sum_text(totals_x, totals_y)
        if len(a_seeds) >= 2 and len(a_seeds) < len(seeds):
            line += " | A: " + sum_text(subset(totals_x, seeds, LIST_A),
                                        subset(totals_y, seeds, LIST_A))
        out.append(line)
    return out


def sum_text(xs, ys):
    p = Paired(xs, ys)
    return (f"n={p.n} before={fmt(p.mean_x)} ({fmt(math.sqrt(p.se2_x))}) "
            f"after={fmt(p.mean_y)} ({fmt(math.sqrt(p.se2_y))}) "
            f"m={fmt(p.m)} se={fmt(p.se)} m/se={fmt_ratio(p)}")


# ---------------------------------------------------------------------------
# playtest
# ---------------------------------------------------------------------------

def read_playtest(path):
    runs = {}
    with open(path, encoding="utf-8") as handle:
        for number, raw in enumerate(handle, 1):
            raw = raw.strip()
            if not raw:
                continue
            try:
                record = json.loads(raw)
                key = (int(record["level"]), int(record["seed"]))
            except (ValueError, KeyError, TypeError) as error:
                raise TableError(f"{path}:{number}: not a playtest summary line ({error})")
            if key in runs:
                raise TableError(f"{path}:{number}: a second run of level {key[0]} seed {key[1]}")
            runs[key] = record
    if not runs:
        raise TableError(f"{path}: no playtest summary line")
    return runs


def playtest_tally(runs):
    """level -> runs, foes cleared (level_done == 1), crew alive at the end
    (summed), a protected hero lost (save_all_loss), runs that failed."""
    per = {}
    for (level, _seed), record in sorted(runs.items()):
        tally = per.setdefault(level, {"runs": 0, "done": 0, "alive": 0, "loss": 0, "errors": 0})
        tally["runs"] += 1
        if "error" in record:
            tally["errors"] += 1
            continue
        if record.get("level_done") == 1:
            tally["done"] += 1
        tally["alive"] += int(record.get("crew_alive") or 0)
        if record.get("save_all_loss") is True:
            tally["loss"] += 1
    return per


def playtest_lines(before_runs, after_runs):
    if set(before_runs) != set(after_runs):
        odd = sorted(set(before_runs) ^ set(after_runs))
        raise TableError("the two sweeps ran different levels or seeds: " +
                         ", ".join(f"level {l} seed {s}" for l, s in odd[:8]))
    before = playtest_tally(before_runs)
    after = playtest_tally(after_runs)
    out = [f"{'level':>5} {'runs':>4}  {'cleared':>9}  {'crew alive':>10}  "
           f"{'hero lost':>9}  {'failed':>7}"]
    differ = 0
    totals = {"done": [0, 0], "alive": [0, 0], "loss": [0, 0], "errors": [0, 0]}
    for level in sorted(before):
        b, a = before[level], after[level]
        for name in totals:
            totals[name][0] += b[name]
            totals[name][1] += a[name]
        if all(b[name] == a[name] for name in totals):
            continue
        differ += 1
        out.append(f"{level:>5} {b['runs']:>4}  {b['done']:>4}->{a['done']:<4}  "
                   f"{b['alive']:>5}->{a['alive']:<4}  {b['loss']:>4}->{a['loss']:<4}  "
                   f"{b['errors']:>3}->{a['errors']:<3}")
    out.append(f"playtest: {len(before)} levels, {differ} differ; totals: cleared "
               f"{totals['done'][0]}->{totals['done'][1]}, crew alive "
               f"{totals['alive'][0]}->{totals['alive'][1]}, hero lost "
               f"{totals['loss'][0]}->{totals['loss'][1]}, failed runs "
               f"{totals['errors'][0]}->{totals['errors'][1]}")
    return out


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------

def git_text(*args):
    try:
        return subprocess.run(["git", *args], cwd=ROOT, stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, check=True).stdout.decode()
    except (OSError, subprocess.CalledProcessError):
        return None


def cmd_run(args):
    seeds = resolve_seeds(args.seeds)
    binary, gtest_filter, switch = SUITES[args.suite]
    executable = os.path.join("build", "ci-test", binary)
    if not os.path.exists(os.path.join(ROOT, executable)):
        raise TableError(f"{executable} is not built")
    out_path = os.path.abspath(args.out)
    env = dict(os.environ)
    for _binary, _filter, other in SUITES.values():
        env.pop(other, None)
    env.pop("CLASSIC_CALIBRATION_FIT", None)
    env["OG_CALIBRATION_SEEDS"] = ",".join(str(s) for s in seeds)
    env["OG_CALIBRATION_GAMEPLAY_FIXES"] = str(args.fixes)
    env[switch] = "1"
    new_specials = "0" if env.get("OG_CALIBRATION_NEW_SPECIALS", "").startswith("0") else "1"
    command = [sys.executable, os.path.join("tests", "fixtures", "run_isolated.py"), ".",
               executable, f"--gtest_filter={gtest_filter}"]
    proc = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = proc.stdout.decode("utf-8", errors="replace")
    out_dir = os.path.dirname(out_path)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(out_path + ".log", "w", encoding="utf-8") as log:
        log.write(output)
    if proc.returncode != 0:
        raise TableError(f"{binary} exited {proc.returncode} (its output: {out_path}.log)")
    head = (git_text("rev-parse", "--short", "HEAD") or "?").strip()
    status = git_text("status", "--short")
    dirty = "?" if status is None else str(len([l for l in status.splitlines() if l.strip()]))
    header = {"suite": args.suite, "seeds": ",".join(str(s) for s in seeds),
              "fixes": str(args.fixes), "new_specials": new_specials, "head": head,
              "dirty": dirty}
    lines = extract_lines(output, f"{out_path}.log")
    table_from_lines(lines, seeds, header, f"{out_path}.log", need_taken=True)
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write(header_line(header) + "\n")
        for line in lines:
            handle.write(line + "\n")
    print(f"wrote {len(lines)} lines ({len(lines) // 2} rows, {len(seeds)} seeds) to {out_path}")
    return 0


# ---------------------------------------------------------------------------
# --self-test
# ---------------------------------------------------------------------------

class Checks:
    def __init__(self):
        self.count = 0
        self.failed = 0

    def equal(self, name, expected, got):
        self.count += 1
        if expected != got:
            self.failed += 1
            print(f"FAIL {name}: expected {expected!r}, got {got!r}")

    def near(self, name, expected, got, tolerance=5e-5):
        self.count += 1
        if got is None or abs(expected - got) > tolerance:
            self.failed += 1
            print(f"FAIL {name}: expected {expected}, got {got}")

    def raises(self, name, call):
        self.count += 1
        try:
            call()
        except TableError:
            return
        except Exception as error:  # a crash is not a refusal
            self.failed += 1
            print(f"FAIL {name}: expected a refusal, got {type(error).__name__}: {error}")
            return
        self.failed += 1
        print(f"FAIL {name}: expected a refusal, got none")


def self_test():
    c = Checks()

    # Seed lists.
    c.equal("list A", 20, len(LIST_A))
    c.equal("list AB", LIST_A + LIST_B, resolve_seeds("AB"))
    c.equal("comma list", [7, 1, 101], resolve_seeds("7,1,101"))
    c.raises("a word is not a seed list", lambda: resolve_seeds("7,x"))
    c.raises("an empty item is not a seed", lambda: resolve_seeds("7,,1"))

    # All equal: no change, no error, back.
    p = Paired([5] * 40, [5] * 40)
    c.equal("all equal: m", Fraction(0), p.m)
    c.equal("all equal: se^2", Fraction(0), p.se2)
    c.equal("all equal: word", "back", pooled_word(p))

    # One seed of forty lower by 8: d is 0 on 39 seeds and -8 on one.
    # m = -8/40 = -0.2; squared deviations 39 * 0.04 + 7.8^2 = 62.4;
    # variance 62.4 / 39 = 1.6; se^2 = 1.6 / 40 = 0.04, se = 0.2: m sits on
    # -se exactly, which still reads back.
    p = Paired([8] * 40, [8] * 39 + [0])
    c.equal("one seed lower by 8: m", Fraction(-1, 5), p.m)
    c.equal("one seed lower by 8: se^2", Fraction(1, 25), p.se2)
    c.equal("one seed lower by 8: word", "back", pooled_word(p))

    # The hand-worked case. x = 6 5 4 7 8, y = 4 4 4 4 4, so d = -2 -1 0 -3 -4.
    # m = -10 / 5 = -2. Squared deviations from m: 0 1 4 1 4, sum 10.
    # Sample variance 10 / (5 - 1) = 2.5; se = sqrt(2.5 / 5) = sqrt(0.5)
    # = 0.70711; m / se = -2.83: past two errors, short of three.
    p = Paired([6, 5, 4, 7, 8], [4, 4, 4, 4, 4])
    c.near("hand-worked m", -2.0, float(p.m))
    c.near("hand-worked se", 0.70711, p.se)
    c.near("hand-worked m/se", -2.82843, p.ratio())
    c.near("hand-worked se before", 0.70711, math.sqrt(p.se2_x))
    c.equal("hand-worked word", "fell<3", pooled_word(p))

    # The other words.
    c.equal("floor (mean before 0.6)", "floor",
            pooled_word(Paired([2, 0, 1, 0, 0, 1, 0, 0, 2, 0], [0] * 10)))
    c.equal("fell (every seed one lower, se 0)", "fell",
            pooled_word(Paired([5] * 4, [4] * 4)))
    c.equal("too far (every seed three higher)", "too far",
            pooled_word(Paired([2, 3, 2, 3], [5, 6, 5, 6])))
    # d = -2 -1 0 0 -1 0 1 -1: m = -0.5, squared deviations sum 6, variance
    # 6/7, se^2 = 3/28 (se 0.327): -2 se < m < -se.
    c.equal("not back", "not back",
            pooled_word(Paired([4] * 8, [2, 3, 4, 4, 3, 4, 5, 3])))

    # The floor is the mean BEFORE, never the mean after. Every seed falls
    # from 5 to 0: m = -5, se 0, a fall, although the mean after is under 1.0.
    c.equal("a fall to under 1.0 reads fell, not floor", "fell",
            pooled_word(Paired([5] * 4, [0] * 4)))
    # The other way: 0.5 before, 3 on every seed after. The level sat on its
    # floor before, so the count cannot judge it, whatever it reads after.
    c.equal("a rise from under 1.0 reads floor", "floor",
            pooled_word(Paired([0, 1, 0, 1], [3] * 4)))

    # The words on their boundaries, each exact.
    # d = -1 on five seeds of ten, 0 on five: m = -1/2; squared deviations
    # 10 * 1/4 = 5/2; variance 5/2 / 9 = 5/18; se^2 = 5/18 / 10 = 1/36,
    # se = 1/6: m is exactly -3 se, which is a fall.
    p = Paired([5] * 10, [4] * 5 + [5] * 5)
    c.equal("exactly -3 se: se^2", Fraction(1, 36), p.se2)
    c.equal("exactly -3 se reads fell", "fell", pooled_word(p))
    # d = -1 on three seeds of nine, 0 on six: m = -1/3; squared deviations
    # 3 * 4/9 + 6 * 1/9 = 2; variance 2 / 8 = 1/4; se^2 = 1/4 / 9 = 1/36,
    # se = 1/6: m is exactly -2 se.
    p = Paired([5] * 9, [4] * 3 + [5] * 6)
    c.equal("exactly -2 se: se^2", Fraction(1, 36), p.se2)
    c.equal("exactly -2 se reads fell<3", "fell<3", pooled_word(p))
    # The mirror: d = +1 on three seeds of nine: m = 1/3, se = 1/6, exactly
    # +2 se, which is too far.
    c.equal("exactly +2 se reads too far", "too far",
            pooled_word(Paired([5] * 9, [6] * 3 + [5] * 6)))
    # d = +1 on two seeds of nine, 0 on seven: m = 2/9; squared deviations
    # 2 * 49/81 + 7 * 4/81 = 126/81 = 14/9; variance 14/9 / 8 = 7/36;
    # se^2 = 7/36 / 9 = 7/324 (se 0.147): m / se = 1.51, a rise of less than
    # two errors, which is back.
    p = Paired([5] * 9, [6] * 2 + [5] * 7)
    c.equal("+1.5 se: se^2", Fraction(7, 324), p.se2)
    c.equal("+1.5 se reads back", "back", pooled_word(p))
    # A mean before of exactly 1.0 is not the floor (the floor is under 1.0).
    c.equal("mean before exactly 1.0 reads back", "back",
            pooled_word(Paired([1] * 4, [1] * 4)))

    # Two lists that disagree: list A loses one survivor on ten of its
    # twenty seeds, list B changes nothing. Pooled over the forty:
    # m = -0.25, se^2 = (7.5 / 39) / 40 = 1/208, m/se = -3.6: fell. List A
    # alone: m = -0.5, se^2 = (5 / 19) / 20 = 1/76: past two errors; list B:
    # m = 0. The two-lists rule wants both: fell(A).
    seeds = LIST_A + LIST_B
    xs = [5] * 40
    ys = [4] * 10 + [5] * 10 + [5] * 20
    c.equal("pooled over disagreeing lists", "fell", pooled_word(Paired(xs, ys)))
    pa = Paired(subset(xs, seeds, LIST_A), subset(ys, seeds, LIST_A))
    pb = Paired(subset(xs, seeds, LIST_B), subset(ys, seeds, LIST_B))
    c.equal("list A se^2", Fraction(1, 76), pa.se2)
    c.equal("two lists that disagree", "fell(A)", two_lists_word(pa, pb))
    ys_both = [4] * 10 + [5] * 10 + [4] * 10 + [5] * 10
    pb_both = Paired(subset(xs, seeds, LIST_B), subset(ys_both, seeds, LIST_B))
    c.equal("two lists that agree", "fell", two_lists_word(pa, pb_both))

    def two_lists(xs, ys):
        return two_lists_word(Paired(subset(xs, seeds, LIST_A), subset(ys, seeds, LIST_A)),
                              Paired(subset(xs, seeds, LIST_B), subset(ys, seeds, LIST_B)))

    # Each list of twenty loses one survivor on four seeds: on each list
    # m = -4/20 = -1/5; squared deviations 4 * 16/25 + 16 * 1/25 = 16/5;
    # variance 16/5 / 19 = 16/95; se^2 = 16/95 / 20 = 4/475 (se 0.092):
    # m / se = -2.18 on each list, past two errors on both, so fell (it
    # would be no fall at three errors).
    ys_four = ([4] * 4 + [5] * 16) * 2
    c.equal("two lists, four seeds lower on each: list A se^2", Fraction(4, 475),
            Paired(subset(xs, seeds, LIST_A), subset(ys_four, seeds, LIST_A)).se2)
    c.equal("two lists, 2.18 errors on each list reads fell", "fell",
            two_lists(xs, ys_four))
    # The mirror on list A only (list B unchanged): +2.18 errors, too far.
    c.equal("two lists, list A 2.18 errors higher reads too far", "too far",
            two_lists(xs, [6] * 4 + [5] * 16 + [5] * 20))
    # List A loses one on two seeds: m = -1/10; squared deviations
    # 2 * 81/100 + 18 * 1/100 = 9/5; variance 9/5 / 19 = 9/95; se^2 = 9/1900
    # (se 0.069): m / se = -1.45, between one and two errors: not back.
    c.equal("two lists, list A 1.45 errors lower reads not back", "not back",
            two_lists(xs, [4] * 2 + [5] * 18 + [5] * 20))
    c.equal("two lists, no change reads back", "back", two_lists(xs, list(xs)))
    # The floor is list A's mean before: list A averages 0.5 and list B 2.0,
    # so the forty average 1.25 and the pooled rule judges the level (back),
    # while the two-lists rule reads floor.
    xs_low_a = [1, 0] * 10 + [2] * 20
    c.equal("pooled, list A under 1.0 but forty at 1.25, reads back", "back",
            pooled_word(Paired(xs_low_a, list(xs_low_a))))
    c.equal("two lists, list A under 1.0 reads floor", "floor",
            two_lists(xs_low_a, list(xs_low_a)))

    # The two-lists floor is list A's mean BEFORE as well.
    c.equal("two lists, a fall to under 1.0 on both lists reads fell", "fell",
            two_lists(xs, [0] * 40))
    c.equal("two lists, list A under 1.0 before and 3 after reads floor", "floor",
            two_lists(xs_low_a, [3] * 40))

    # What verdict prints under --rule two-lists, on the forty seeds, token
    # by token. One survivor lost on ten seeds of a list of twenty: m = -1/2,
    # se^2 = 1/76 (se 0.11), past two errors (the case above).
    #   level=1: list A falls, list B does not: fell(A).
    #   level=2: list B falls, list A does not: judged on list A, back.
    #   level=3: both lists fall: fell.
    tbefore = Table({"suite": "x"}, seeds)
    tafter = Table({"suite": "x"}, seeds)
    for level, after_alive in ((1, [4] * 10 + [5] * 30),
                               (2, [5] * 20 + [4] * 10 + [5] * 10),
                               (3, ([4] * 10 + [5] * 10) * 2)):
        tbefore.rows[f"level={level} cl=3"] = {"alive": [5] * 40, "taken": [0] * 40}
        tafter.rows[f"level={level} cl=3"] = {"alive": after_alive, "taken": [0] * 40}
    printed = [line.split() for line in verdict_lines(tbefore, tafter, "two-lists")]
    c.equal("verdict, two lists: list A alone fell",
            "level=1 cl=3 20 5.00 4.50 -0.50 0.11 20 0.00 0.00 fell(A)".split(), printed[3])
    c.equal("verdict, two lists: list B alone fell",
            "level=2 cl=3 20 5.00 5.00 0.00 0.00 20 -0.50 0.11 back".split(), printed[4])
    c.equal("verdict, two lists: both lists fell",
            "level=3 cl=3 20 5.00 4.50 -0.50 0.11 20 -0.50 0.11 fell".split(), printed[5])

    # The lines a run writes, read back; the old three-seed line is not ours.
    output = "\n".join([
        "[ RUN      ] WestlandsCampaignTest.curve_levels_x",
        "CALIBN level=7 cl=4 survivors@600: 3 4",
        "CALIBN-TAKEN level=7 cl=4 taken@600: 1200 980",
        "TOWER CALIBN floor=5 cl=2 survivors@600: 1 0",
        "TOWER CALIBN-TAKEN floor=5 cl=2 taken@600: 2000 2100",
        "CALIBN campaign=gladiator level=3 cl=2 markers=6 survivors@600: 8 7",
        "CALIBN-TAKEN campaign=gladiator level=3 cl=2 markers=6 taken@600: 100 250",
        "CALIB level=1 cl=1 survivors@600: 8 8 8  [seed42 alive=8 host=9 taken=338 dealt=1162]",
    ])
    lines = extract_lines(output, "sample")
    c.equal("lines kept", 6, len(lines))
    table = table_from_lines(lines, [1, 2], {"suite": "x", "seeds": "1,2"}, "sample", True)
    c.equal("rows read", ["level=7 cl=4", "campaign=gladiator level=3 cl=2 markers=6",
                          "TOWER floor=5 cl=2"], table.sorted_keys())
    c.equal("damage read", [100, 250],
            table.rows["campaign=gladiator level=3 cl=2 markers=6"]["taken"])
    c.raises("a mangled CALIBN line is refused",
             lambda: extract_lines("CALIBN level=7 survivors@600: 3 x", "sample"))
    c.raises("a row short of a seed is refused",
             lambda: table_from_lines(lines, [1, 2, 3], {}, "sample", True))
    c.raises("a row without its damage line is refused when a run needs it",
             lambda: table_from_lines(lines[:1], [1, 2], {}, "sample", True))
    other = Table({}, [1, 3])
    other.rows = dict(table.rows)
    c.raises("two files of different seeds are refused",
             lambda: verdict_lines(table, other, "pooled"))

    # A table file read back: the header's seed order is the columns' order,
    # and a file without the header line is refused.
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "table.txt")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(header_line({"suite": "x", "seeds": "42,101,1", "fixes": "1"}) + "\n")
            handle.write("CALIBN level=7 cl=4 survivors@600: 3 4 5\n")
            handle.write("CALIBN-TAKEN level=7 cl=4 taken@600: 10 20 30\n")
        read_back = read_table(path)
        bare = os.path.join(tmp, "bare.txt")
        with open(bare, "w", encoding="utf-8") as handle:
            handle.write("CALIBN level=7 cl=4 survivors@600: 3 4 5\n")
        c.raises("a table file without its header line is refused", lambda: read_table(bare))
    c.equal("a table file read back: seeds in the header's order", [42, 101, 1], read_back.seeds)
    c.equal("a table file read back: its row",
            {"alive": [3, 4, 5], "taken": [10, 20, 30]}, read_back.rows["level=7 cl=4"])
    c.equal("a table file read back: its header", "1", read_back.header["fixes"])
    c.raises("a row of minus one survivors is refused: that battle did not happen",
             lambda: table_from_lines(["CALIBN level=7 cl=4 survivors@600: 3 -1"], [1, 2], {},
                                      "sample", False))

    # What verdict prints, on two three-seed files. Seeds 42 and 1 are of
    # list A, 101 is not, so the list-A columns and the SUM's "| A:" part
    # read seeds 42 and 1 only. Compared token by token (the column widths
    # are not the rule).
    #   level=1: survivors 8 8 8 -> 7 7 7, d = -1 on every seed: m = -1,
    #     se 0, so fell; damage 10 20 30 -> 40 50 60, means 20 and 50.
    #   level=2: 3 5 4 -> 4 5 4, d = 1 0 0: m = 1/3; squared deviations
    #     4/9 + 1/9 + 1/9 = 2/3, variance 1/3, se^2 = 1/9, se = 1/3, m/se 1.0:
    #     back. Before: mean 4, deviations 1 1 0, variance 1, se^2 1/3 (se
    #     0.58); after: mean 13/3 = 4.33, deviations 1/9 4/9 1/9, variance
    #     1/3, se^2 1/9 (se 0.33). List A: 3 and 4 -> 4 and 4, 3.50 -> 4.00.
    #   SUM x (the two rows): per seed 11 13 12 -> 11 12 11, mean 12 (se
    #     0.58) -> 34/3 = 11.33 (se 0.33); d = 0 -1 -1, m = -2/3, squared
    #     deviations 4/9 + 1/9 + 1/9 = 2/3, variance 1/3, se 1/3, m/se -2.0.
    #     List A: 11 12 -> 11 11, means 11.50 (se 0.50) and 11.00 (0.00);
    #     d = 0 -1, m = -1/2, variance 1/2, se^2 1/4, se 0.50, m/se -1.0.
    #   SUM gladiator (its own campaign, one row of 2 2 2 both times).
    vseeds = [42, 101, 1]
    vbefore = Table({"suite": "x", "seeds": "42,101,1"}, vseeds)
    vafter = Table({"suite": "x", "seeds": "42,101,1"}, vseeds)
    vbefore.rows = {"level=1 cl=3": {"alive": [8, 8, 8], "taken": [10, 20, 30]},
                    "level=2 cl=3": {"alive": [3, 5, 4], "taken": [6, 0, 0]},
                    "campaign=gladiator level=3 cl=2 markers=8":
                        {"alive": [2, 2, 2], "taken": [0, 0, 0]}}
    vafter.rows = {"level=1 cl=3": {"alive": [7, 7, 7], "taken": [40, 50, 60]},
                   "level=2 cl=3": {"alive": [4, 5, 4], "taken": [30, 0, 0]},
                   "campaign=gladiator level=3 cl=2 markers=8":
                       {"alive": [2, 2, 2], "taken": [0, 0, 0]}}
    printed = [line.split() for line in verdict_lines(vbefore, vafter, "pooled")]
    c.equal("verdict: level 1 row",
            "level=1 cl=3 3 8.00 (0.00) 7.00 (0.00) -1.00 0.00 - 8.00 7.00 "
            "20.00 50.00 fell".split(), printed[3])
    c.equal("verdict: level 2 row",
            "level=2 cl=3 3 4.00 (0.58) 4.33 (0.33) 0.33 0.33 1.0 3.50 4.00 "
            "2.00 10.00 back".split(), printed[4])
    c.equal("verdict: the campaign's own row",
            "campaign=gladiator level=3 cl=2 markers=8 3 2.00 (0.00) 2.00 (0.00) "
            "0.00 0.00 - 2.00 2.00 0.00 0.00 back".split(), printed[5])
    c.equal("verdict: SUM of the rows without a campaign",
            "SUM x rows=2 n=3 before=12.00 (0.58) after=11.33 (0.33) m=-0.67 "
            "se=0.33 m/se=-2.0 | A: n=2 before=11.50 (0.50) after=11.00 (0.00) "
            "m=-0.50 se=0.50 m/se=-1.0".split(), printed[6])
    c.equal("verdict: SUM of the campaign",
            "SUM gladiator rows=1 n=3 before=2.00 (0.00) after=2.00 (0.00) m=0.00 "
            "se=0.00 m/se=- | A: n=2 before=2.00 (0.00) after=2.00 (0.00) "
            "m=0.00 se=0.00 m/se=-".split(), printed[7])
    c.equal("verdict: nothing after the SUM lines", 8, len(printed))
    fewer = Table(vafter.header, vseeds)
    fewer.rows = {k: v for k, v in vafter.rows.items() if k != "level=2 cl=3"}
    c.raises("two files of different levels are refused",
             lambda: verdict_lines(vbefore, fewer, "pooled"))
    renamed = Table(vafter.header, vseeds)
    renamed.rows = dict(vafter.rows)
    renamed.rows["level=2 cl=4"] = renamed.rows.pop("level=2 cl=3")
    c.raises("a row of another crew level is another row",
             lambda: verdict_lines(vbefore, renamed, "pooled"))
    more = Table(vafter.header, vseeds)
    more.rows = dict(vafter.rows)
    more.rows["level=9 cl=1"] = {"alive": [1, 1, 1], "taken": [0, 0, 0]}
    c.raises("an after file with a level the before file lacks is refused",
             lambda: verdict_lines(vbefore, more, "pooled"))

    # The playtest reader on a three-line file: on level 1 the second seed
    # turns from a lost hero and a wiped crew into a clear with two alive.
    with tempfile.TemporaryDirectory() as tmp:
        before_path = os.path.join(tmp, "before.jsonl")
        after_path = os.path.join(tmp, "after.jsonl")
        with open(before_path, "w", encoding="utf-8") as handle:
            handle.write('{"level":1,"seed":42,"level_done":1,"crew_alive":4,"save_all_loss":false}\n'
                         '{"level":1,"seed":1337,"level_done":0,"crew_alive":0,"save_all_loss":true}\n'
                         '{"level":2,"seed":42,"level_done":1,"crew_alive":8,"save_all_loss":false}\n')
        with open(after_path, "w", encoding="utf-8") as handle:
            handle.write('{"level":1,"seed":42,"level_done":1,"crew_alive":4,"save_all_loss":false}\n'
                         '{"level":1,"seed":1337,"level_done":1,"crew_alive":2,"save_all_loss":false}\n'
                         '{"level":2,"seed":42,"level_done":1,"crew_alive":8,"save_all_loss":false}\n')
        before_runs = read_playtest(before_path)
        after_runs = read_playtest(after_path)
    c.equal("playtest tally before", {
        1: {"runs": 2, "done": 1, "alive": 4, "loss": 1, "errors": 0},
        2: {"runs": 1, "done": 1, "alive": 8, "loss": 0, "errors": 0}},
        playtest_tally(before_runs))
    report = playtest_lines(before_runs, after_runs)
    c.equal("playtest: only level 1 differs", ["    1"], [line[:5] for line in report[1:-1]])
    c.equal("playtest totals",
            "playtest: 2 levels, 1 differ; totals: cleared 2->3, crew alive 12->14, "
            "hero lost 1->0, failed runs 0->0", report[-1])

    # Each count alone: on level 3 only the clear changes, on level 4 only
    # the crew alive, on level 5 only the lost hero, on level 7 only a run
    # that failed; level 6 changes nothing and is not printed.
    def runs_of(text):
        runs = {}
        for line in text:
            record = json.loads(line)
            runs[(record["level"], record["seed"])] = record
        return runs

    alone_before = runs_of([
        '{"level":3,"seed":42,"level_done":0,"crew_alive":0,"save_all_loss":false}',
        '{"level":4,"seed":42,"level_done":1,"crew_alive":3,"save_all_loss":false}',
        '{"level":5,"seed":42,"level_done":1,"crew_alive":2,"save_all_loss":false}',
        '{"level":6,"seed":42,"level_done":1,"crew_alive":4,"save_all_loss":false}',
        '{"level":7,"seed":42,"level_done":0,"crew_alive":0,"save_all_loss":false}'])
    alone_after = runs_of([
        '{"level":3,"seed":42,"level_done":1,"crew_alive":0,"save_all_loss":false}',
        '{"level":4,"seed":42,"level_done":1,"crew_alive":1,"save_all_loss":false}',
        '{"level":5,"seed":42,"level_done":1,"crew_alive":2,"save_all_loss":true}',
        '{"level":6,"seed":42,"level_done":1,"crew_alive":4,"save_all_loss":false}',
        '{"level":7,"seed":42,"crew_level":1,"error":"run failed or timed out"}'])
    report = [line.split() for line in playtest_lines(alone_before, alone_after)]
    c.equal("playtest, one count at a time: the rows printed", [
        "3 1 0->1 0->0 0->0 0->0".split(),
        "4 1 1->1 3->1 0->0 0->0".split(),
        "5 1 1->1 2->2 0->1 0->0".split(),
        "7 1 0->0 0->0 0->0 0->1".split()], report[1:-1])
    c.equal("playtest, one count at a time: totals",
            "playtest: 5 levels, 4 differ; totals: cleared 3->4, crew alive 9->7, "
            "hero lost 0->1, failed runs 0->1".split(), report[-1])
    c.raises("two sweeps of different runs are refused",
             lambda: playtest_lines(alone_before,
                                    {k: v for k, v in alone_after.items() if k[0] != 6}))

    if c.failed:
        print(f"seed_table self-test: {c.failed} of {c.count} checks failed")
        return 1
    print(f"seed_table self-test: OK ({c.count} checks)")
    return 0


# ---------------------------------------------------------------------------

def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    parser = argparse.ArgumentParser(
        description="Seed tables of the campaign calibration suites (see the module text).")
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run", help="play one suite's measure mode and write its lines")
    run.add_argument("--suite", required=True, choices=sorted(SUITES))
    run.add_argument("--seeds", required=True, help="A, B, AB or a comma-separated list")
    run.add_argument("--fixes", required=True, type=int, choices=(0, 1))
    run.add_argument("--out", required=True)
    verdict = sub.add_parser("verdict", help="judge AFTER against BEFORE, level by level")
    verdict.add_argument("before")
    verdict.add_argument("after")
    verdict.add_argument("--rule", choices=("pooled", "two-lists"), default="pooled")
    playtest = sub.add_parser("playtest", help="compare two playtest sweeps")
    playtest.add_argument("before")
    playtest.add_argument("after")
    args = parser.parse_args(argv)
    try:
        if args.command == "run":
            return cmd_run(args)
        if args.command == "verdict":
            for line in verdict_lines(read_table(args.before), read_table(args.after), args.rule):
                print(line)
            return 0
        for line in playtest_lines(read_playtest(args.before), read_playtest(args.after)):
            print(line)
        return 0
    except (TableError, OSError) as error:
        print(f"seed_table: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
