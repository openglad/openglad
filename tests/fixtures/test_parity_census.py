#!/usr/bin/env python3
"""A census fixture cannot reuse prior observations or erase unrelated files."""
from pathlib import Path
import tempfile
import unittest

from parity_census import fixture


class CensusFixtureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.directory = self.root / "census"

    def test_setup_replaces_old_observations_with_a_fresh_identity(self):
        fixture("setup", self.directory)
        original = (self.directory / "nonce").read_text()
        (self.directory / "partition-0.txt").write_text("old observations")
        fixture("setup", self.directory)
        self.assertNotEqual(original, (self.directory / "nonce").read_text())
        self.assertFalse((self.directory / "partition-0.txt").exists())
        fixture("cleanup", self.directory)
        self.assertFalse(self.directory.exists())

    def test_unowned_directories_and_symlinks_are_untouched(self):
        self.directory.mkdir()
        sentinel = self.directory / "unrelated"
        sentinel.write_text("retain")
        for action in ("setup", "cleanup"):
            with self.assertRaises(ValueError):
                fixture(action, self.directory)
            self.assertEqual("retain", sentinel.read_text())
        alias = self.root / "alias"
        alias.symlink_to(self.directory, target_is_directory=True)
        with self.assertRaises(ValueError):
            fixture("setup", alias)
        self.assertEqual("retain", sentinel.read_text())

    def test_invalid_action_does_not_delete_a_valid_fixture(self):
        fixture("setup", self.directory)
        original = (self.directory / "nonce").read_text()
        with self.assertRaises(ValueError):
            fixture("invalid", self.directory)
        self.assertEqual(original, (self.directory / "nonce").read_text())


if __name__ == "__main__":
    unittest.main()
