from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPO_ROOT / "scripts" / "dev" / "merge_gate.py"


def check_run(name: str, conclusion: str | None = "success", status: str = "completed", started_at: str = "2026-01-01T00:00:00Z") -> dict[str, str | None]:
    return {
        "name": name,
        "status": status,
        "conclusion": conclusion,
        "started_at": started_at,
    }


BASE_CHECKS = [
    check_run("workflow policy and actionlint"),
    check_run("audit"),
]
RUST_CHECKS = [
    check_run("rust"),
    check_run("terminal behavior (windows)"),
    check_run("terminal behavior (macos-native)"),
]
WEB_CHECKS = [
    check_run("container smoke"),
    check_run("build, test, and package Web artifact"),
]


class MergeGateTests(unittest.TestCase):
    def run_gate(self, paths: list[str], checks: list[dict[str, str | None]]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            paths_file = root / "paths.txt"
            checks_file = root / "checks.json"
            paths_file.write_text("\n".join(paths) + "\n", encoding="utf-8")
            checks_file.write_text(json.dumps({"check_runs": checks}), encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(CHECKER), "--changed-files", str(paths_file), "--checks-json", str(checks_file)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

    def test_relevant_rust_and_web_changes_require_applicable_checks(self) -> None:
        result = self.run_gate(
            ["crates/safeparts_core/src/lib.rs", "web/src/App.tsx"],
            BASE_CHECKS + RUST_CHECKS + WEB_CHECKS,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("merge gate passed", result.stdout)

    def test_irrelevant_docs_change_does_not_wait_for_filtered_workloads(self) -> None:
        result = self.run_gate(["docs/dev/verification.md"], BASE_CHECKS)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("rust", result.stderr)
        self.assertNotIn("build, test, and package Web artifact", result.stderr)

    def test_failed_cancelled_missing_and_unexpectedly_skipped_checks_fail(self) -> None:
        cases = {
            "failed": BASE_CHECKS + [check_run("rust", "failure")] + RUST_CHECKS[1:],
            "cancelled": BASE_CHECKS + [check_run("rust", "cancelled")] + RUST_CHECKS[1:],
            "missing": BASE_CHECKS + RUST_CHECKS[1:],
            "skipped": BASE_CHECKS + [check_run("rust", "skipped")] + RUST_CHECKS[1:],
            "pending": BASE_CHECKS + [check_run("rust", None, "in_progress")] + RUST_CHECKS[1:],
        }
        for case, checks in cases.items():
            with self.subTest(case=case):
                result = self.run_gate(["Cargo.toml"], checks)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("merge gate failed", result.stderr)
                self.assertIn("rust", result.stderr)

    def test_expected_skipped_deployments_do_not_block_pr_gate(self) -> None:
        result = self.run_gate(
            ["web/src/App.tsx"],
            BASE_CHECKS
            + WEB_CHECKS
            + [
                check_run("deploy tested artifact to Netlify", "skipped"),
                check_run("deploy tested artifact to Cloudflare Workers", "skipped"),
            ],
        )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_latest_check_run_for_a_required_name_controls_the_gate(self) -> None:
        result = self.run_gate(
            ["Cargo.toml"],
            BASE_CHECKS
            + RUST_CHECKS[1:]
            + [
                check_run("rust", "success", started_at="2026-01-01T00:00:00Z"),
                check_run("rust", "failure", started_at="2026-01-01T00:01:00Z"),
            ],
        )

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("rust: completed/failure", result.stderr)


if __name__ == "__main__":
    unittest.main()
