#!/usr/bin/env python3
"""Unit tests for scripts/web/build_stamp.py (#508).

Run by wasm-e2e.yml's `python3 -m unittest discover -s scripts/web -p
'test_*.py'` step, before the WASM build.
"""

import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_stamp  # noqa: E402


def header(minor=1080, commit="7e7f4079", on_master=1, with_flag=True):
    lines = [
        "#pragma once",
        f'#define OPENGLAD_GIT_HASH "{commit}"',
        "#define OPENGLAD_VERSION_MAJOR 2",
        f"#define OPENGLAD_VERSION_MINOR {minor}",
        f'#define OPENGLAD_VERSION_STRING "2.{minor}"',
    ]
    if with_flag:
        lines.append(f"#define OPENGLAD_VERSION_ON_MASTER {on_master}")
    return "\n".join(lines) + "\n"


class StampOutputTests(unittest.TestCase):
    def test_a_master_build_has_its_version_and_alias(self):
        self.assertEqual(
            build_stamp.stamp_outputs(header(on_master=1), True, 1080,
                                      "7e7f4079"),
            {"minor": "1080", "hash": "7e7f4079", "on_master": "1",
             "version": "2.1080", "alias": "v2-1080"})

    def test_a_pull_request_build_has_no_version(self):
        self.assertEqual(
            build_stamp.stamp_outputs(header(on_master=0), False, 1080,
                                      "7e7f4079"),
            {"minor": "1080", "hash": "7e7f4079", "on_master": "0",
             "version": "", "alias": ""})

    def test_a_stamp_that_disagrees_with_the_event_is_refused(self):
        with self.assertRaisesRegex(
                build_stamp.StampError,
                "^the stamp says on_master=0 but the workflow event says 1$"):
            build_stamp.stamp_outputs(header(on_master=0), True, 1080,
                                      "7e7f4079")
        with self.assertRaisesRegex(
                build_stamp.StampError,
                "^the stamp says on_master=1 but the workflow event says 0$"):
            build_stamp.stamp_outputs(header(on_master=1), False, 1080,
                                      "7e7f4079")

    def test_a_count_or_hash_off_the_checkout_is_refused(self):
        with self.assertRaisesRegex(
                build_stamp.StampError,
                r"^stamp 2\.1080 != git rev-list --count HEAD "
                r"\(shallow checkout\?\)$"):
            build_stamp.stamp_outputs(header(), True, 1081, "7e7f4079")
        with self.assertRaisesRegex(
                build_stamp.StampError,
                "^WASM build stamp does not match checkout$"):
            build_stamp.stamp_outputs(header(commit="7e7f4079+"), True, 1080,
                                      "7e7f4079")

    def test_a_header_without_the_flag_is_refused(self):
        with self.assertRaisesRegex(build_stamp.StampError,
                                    "^no version stamp$"):
            build_stamp.parse_header(header(with_flag=False))
        with self.assertRaisesRegex(build_stamp.StampError,
                                    "^no version stamp$"):
            build_stamp.parse_header("#pragma once\n")


class CliTests(unittest.TestCase):
    def test_the_cli_appends_the_outputs_a_pull_request_build_gets(self):
        with tempfile.TemporaryDirectory() as tmp:
            stamp_path = os.path.join(tmp, "og_git_hash.h")
            out_path = os.path.join(tmp, "out")
            with open(stamp_path, "w", encoding="utf-8") as handle:
                handle.write(header(on_master=0))
            with open(out_path, "w", encoding="utf-8") as handle:
                handle.write("earlier=kept\n")
            printed = io.StringIO()
            with contextlib.redirect_stdout(printed):
                code = build_stamp.main([
                    "--header", stamp_path, "--expect-on-master", "0",
                    "--rev-count", "1080", "--short-hash", "7e7f4079",
                    "--output-file", out_path])
            self.assertEqual(0, code)
            self.assertEqual("", printed.getvalue())
            with open(out_path, encoding="utf-8") as handle:
                self.assertEqual(
                    "earlier=kept\nversion=\nminor=1080\nhash=7e7f4079\n"
                    "alias=\non_master=0\n", handle.read())

    def test_the_cli_refuses_a_missing_stamp_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            stamp_path = os.path.join(tmp, "og_git_hash.h")
            out_path = os.path.join(tmp, "out")
            with open(stamp_path, "w", encoding="utf-8") as handle:
                handle.write("#pragma once\n")
            # The refusal is a GitHub workflow command; captured here so a
            # green test run prints no error annotation of its own.
            printed = io.StringIO()
            with contextlib.redirect_stdout(printed):
                code = build_stamp.main([
                    "--header", stamp_path, "--expect-on-master", "1",
                    "--rev-count", "1080", "--short-hash", "7e7f4079",
                    "--output-file", out_path])
            self.assertEqual(1, code)
            self.assertEqual(f"::error::{stamp_path} has no version stamp\n",
                             printed.getvalue())
            self.assertFalse(os.path.exists(out_path))


if __name__ == "__main__":
    unittest.main()
