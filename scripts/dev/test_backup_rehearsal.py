"""Execute the bilingual, synthetic-only help rehearsal through the public CLI.

Build with cargo build -p safeparts, then put that binary on PATH before running.
No production inputs, mocks, or new backup-verification interface.
"""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "web/help/src/content/docs"


class BackupRehearsalTest(unittest.TestCase):
    def test_saved_backup_success_and_failed_verification_paths(self):
        for locale in ("", "ar/"):
            with self.subTest(locale=locale or "en"):
                page = DOCS / locale / "it-devops-guide/automation.mdx"
                match = re.search(
                    r"```bash\n(# Synthetic saved-backup rehearsal\n.*?)\n```",
                    page.read_text(),
                    re.DOTALL,
                )
                self.assertIsNotNone(match, f"Missing executable rehearsal in {page}")
                self.assertIsNotNone(shutil.which("safeparts"), "Put built safeparts on PATH")
                with tempfile.TemporaryDirectory() as directory:
                    result = subprocess.run(
                        ["bash", "-c", match[1]],
                        cwd=directory,
                        env={**os.environ, "TMPDIR": directory},
                        capture_output=True,
                        text=True,
                        timeout=120,
                    )
                    self.assertEqual(result.returncode, 0, "Synthetic rehearsal failed")
                    self.assertEqual(result.stderr, "")
                    self.assertEqual(result.stdout.splitlines(), [
                        "PASS: saved shares 1,2,3 verified",
                        "PASS: wrong Passphrase blocked; working copy kept",
                        "PASS: saved-copy mismatch blocked; working copy kept",
                        "PASS: corrected backup reverified before retirement",
                    ])
                    self.assertEqual(list(Path(directory).iterdir()), [], "Temporary files remain")


if __name__ == "__main__":
    unittest.main()
