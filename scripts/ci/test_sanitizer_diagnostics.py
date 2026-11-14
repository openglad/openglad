#!/usr/bin/env python3
"""A successful process or the wrong diagnostic must never satisfy the guard."""
import unittest

import sanitizer_diagnostics as diagnostics


class DiagnosticTests(unittest.TestCase):
    def test_expected_failure_requires_the_right_diagnostic_and_exact_line(self):
        output = "ERROR: AddressSanitizer: heap-buffer-overflow at fixture.cpp:42:9\n"
        diagnostics.verify_diagnostic(output, 1, "heap-buffer-overflow", "fixture.cpp", 42)
        for text, status in ((output, 0), (output.replace("heap", "global"), 1),
                             (output.replace(":42:", ":43:"), 1), (output.replace(":42:", ":420:"), 1)):
            with self.subTest(text=text, status=status), self.assertRaises(ValueError):
                diagnostics.verify_diagnostic(text, status, "heap-buffer-overflow", "fixture.cpp", 42)

    def test_each_lane_keeps_its_instrumentation_classes(self):
        self.assertEqual(set(diagnostics.diagnostic_cases("asan")), {"global", "heap", "integer", "float"})
        self.assertEqual(diagnostics.diagnostic_cases("fuzz"), diagnostics.diagnostic_cases("asan"))
        self.assertEqual(set(diagnostics.diagnostic_cases("tsan")), {"race"})


if __name__ == "__main__":
    unittest.main()
