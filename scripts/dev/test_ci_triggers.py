from __future__ import annotations

import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = REPO_ROOT / ".github" / "workflows"


class CiTriggerPolicyTests(unittest.TestCase):
    def workflow_text(self, name: str) -> str:
        return (WORKFLOWS / name).read_text(encoding="utf-8")

    def assert_push_limited_to_main(self, text: str) -> None:
        self.assertIn("  push:\n    branches: [main]\n    paths:\n", text)

    def assert_pr_keeps_path_filtering(self, text: str) -> None:
        self.assertIn("  pull_request:\n    paths:\n", text)
        self.assertNotIn("  pull_request:\n    branches:", text)

    def assert_paths_include(self, text: str, paths: tuple[str, ...]) -> None:
        for path in paths:
            with self.subTest(path=path):
                self.assertIn(f"      - '{path}'", text)

    def test_rust_and_web_verification_use_prs_for_feature_branch_updates(self) -> None:
        expected_paths = {
            "rust-ci.yml": (
                "Cargo.toml",
                "crates/safeparts_core/**",
                "crates/safeparts_wasm/**",
                "web/package.json",
                ".github/**",
            ),
            "web-ci.yml": (
                "web/**",
                "crates/safeparts_core/**",
                "crates/safeparts_wasm/**",
                "Cargo.toml",
                ".github/workflows/web-ci.yml",
            ),
        }
        for workflow, paths in expected_paths.items():
            with self.subTest(workflow=workflow):
                text = self.workflow_text(workflow)

                self.assert_push_limited_to_main(text)
                self.assert_pr_keeps_path_filtering(text)
                self.assert_paths_include(text, paths)

    def test_web_recovery_triggers_remain_available(self) -> None:
        text = self.workflow_text("web-ci.yml")

        self.assertIn("  schedule:\n    - cron: '0 3 * * *'\n", text)
        self.assertIn("  workflow_dispatch:\n", text)

    def test_rustsec_keeps_all_prs_and_main_pushes(self) -> None:
        text = self.workflow_text("rustsec.yml")

        self.assertIn("  pull_request:\n  push:\n    branches: [main]\n", text)

    def test_rust_ci_owns_lightweight_workflow_validation(self) -> None:
        text = self.workflow_text("rust-ci.yml")

        self.assertIn("  workflow_policy:\n    name: workflow policy and actionlint\n", text)
        self.assertIn("tool: actionlint@1.7.12", text)
        self.assertIn("python3 scripts/dev/test_rust_coverage.py", text)
        self.assertIn("python3 scripts/dev/test_rustsec_audit.py", text)
        self.assertIn(
            "actionlint -config-file .github/actionlint.yaml .github/workflows/*.yml",
            text,
        )

    def test_rust_ci_runs_terminal_behavior_on_supported_hosts(self) -> None:
        text = self.workflow_text("rust-ci.yml")
        job = text.split("  terminal_behavior_hosts:\n", 1)[1].split(
            "\n  rust:\n", 1
        )[0]

        self.assertIn("name: terminal behavior (${{ matrix.id }})", job)
        self.assertIn("os: blacksmith-4vcpu-windows-2025", job)
        self.assertIn("os: blacksmith-6vcpu-macos-15", job)
        self.assertIn("toolchain: '1.93.0'", job)
        self.assertIn("cargo test -p safeparts --all-features", job)
        self.assertIn("cargo test -p safeparts_tui --all-features", job)
        self.assertIn("sensitive_output_files_are_owner_only_even_when_overwritten", job)
        self.assertIn("failed_replace_removes_temporary_output", job)
        self.assertNotIn("cargo fmt", job)
        self.assertNotIn("cargo clippy", job)
        self.assertNotIn("rust_coverage.py", job)
        self.assertNotIn("test-wasm.sh", job)

    def test_rustsec_ci_runs_classifier_regressions_before_audit(self) -> None:
        text = self.workflow_text("rustsec.yml")

        self.assertIn("python3 scripts/dev/test_rustsec_audit.py", text)
        self.assertLess(
            text.index("python3 scripts/dev/test_rustsec_audit.py"),
            text.index("python3 scripts/dev/rustsec_audit.py"),
        )


if __name__ == "__main__":
    unittest.main()
