#!/usr/bin/env python3
"""Integrity tests for the repository-wide retrying Nix installer action."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ACTION = ROOT / ".github/actions/install-nix/action.yml"
WRAPPER = ROOT / ".github/actions/install-nix/bin/curl"
WORKFLOWS = ROOT / ".github/workflows"
PIN = "cachix/install-nix-action@75c75c31ebece974e48ebc36b4ad125bd97bd97b"


class InstallNixActionTests(unittest.TestCase):
    def test_every_workflow_uses_the_retrying_local_action(self):
        direct_uses = []
        local_uses = 0
        for workflow in WORKFLOWS.glob("*.yml"):
            text = workflow.read_text()
            if PIN in text:
                direct_uses.append(workflow.name)
            local_uses += text.count("uses: ./.github/actions/install-nix")
        self.assertEqual([], direct_uses)
        self.assertEqual(27, local_uses)
        self.assertEqual(1, ACTION.read_text().count(PIN))

    def test_wrapper_retries_all_transient_curl_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arguments = root / "arguments"
            fake_curl = root / "curl"
            fake_curl.write_text(
                "#!/usr/bin/env bash\nprintf '%s\\n' \"$@\" > \"$ARGUMENTS\"\n")
            fake_curl.chmod(0o755)
            environment = os.environ.copy()
            environment.update(
                OPENGLAD_SYSTEM_CURL=str(fake_curl), ARGUMENTS=str(arguments))
            subprocess.run(
                [str(WRAPPER), "--fail", "https://example.invalid/archive"],
                check=True, env=environment)
            observed = arguments.read_text().splitlines()
        self.assertEqual(
            [
                "--retry", "4", "--retry-all-errors", "--retry-delay", "2",
                "--connect-timeout", "30", "--speed-limit", "1024",
                "--speed-time", "30", "--fail",
                "https://example.invalid/archive",
            ],
            observed,
        )


if __name__ == "__main__":
    unittest.main()
