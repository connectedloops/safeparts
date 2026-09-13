from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPO_ROOT / "scripts" / "dev" / "workflow_policy.py"

PINNED_CHECKOUT = "actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4.4.0"
PINNED_RUST = (
    "dtolnay/rust-toolchain@4360b52568e2003a75bf9bc1d59f33a8e3fc893c "
    "# stable (2026-08-05)"
)
PINNED_BUN = "oven-sh/setup-bun@0c5077e51419868618aeaa5fe8019c62421857d6 # v2.2.0"


def valid_workflow() -> str:
    return f"""name: release
on: workflow_dispatch
permissions:
  contents: read
jobs:
  test:
    runs-on: ubuntu-24.04
    steps:
      - uses: {PINNED_CHECKOUT}
      - uses: {PINNED_RUST}
        with:
          toolchain: '1.93.0'
      - name: Validate release version
        run: python3 scripts/release/check-version.py "$RELEASE_VERSION"
      - name: Test
        run: cargo test --all-features --locked
  build:
    runs-on: windows-2025
    steps:
      - uses: {PINNED_RUST}
        with:
          toolchain: '1.93.0'
      - uses: {PINNED_BUN}
        with:
          bun-version: '1.3.11'
      - name: Build release binaries
        run: cargo build --release --locked -p safeparts -p safeparts_tui
      - name: Package
        run: python scripts/release/package.py --version "$RELEASE_VERSION" --arch x86_64
  publish:
    if: github.event_name == 'push' && startsWith(github.ref, 'refs/tags/')
    runs-on: ubuntu-24.04
    permissions:
      contents: write
    steps:
      - uses: {PINNED_CHECKOUT}
"""


class WorkflowPolicyTests(unittest.TestCase):
    def run_policy(self, workflow: str) -> subprocess.CompletedProcess[str]:
        with tempfile.NamedTemporaryFile("w", suffix=".yml", encoding="utf-8") as file:
            file.write(workflow)
            file.flush()
            return subprocess.run(
                [sys.executable, str(CHECKER), file.name],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

    def test_repository_release_workflow_passes(self) -> None:
        result = subprocess.run(
            [sys.executable, str(CHECKER)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_supported_workflow_needs_no_native_sdks(self) -> None:
        result = self.run_policy(valid_workflow())
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_retired_jobs_commands_and_installer_globs_are_rejected(self) -> None:
        for addition in (
            "  desktop:\n    runs-on: ubuntu-24.04\n",
            "  native-macos:\n    runs-on: macos-15\n",
            "  native-windows:\n    runs-on: windows-2025\n",
            "      - run: cargo build -p safeparts_uniffi\n",
            "      - run: bun run tauri:build\n",
            "      - run: python windows/scripts/package-release.py\n",
            "      - run: macos/scripts/package-release.sh\n",
            "          files: dist/release/**/*.dmg\n",
            "          files: dist/release/safeparts-native-windows-*.zip\n",
            "          files: dist/release/**/*.msi\n",
            "          files: dist/release/**/*.AppImage\n",
        ):
            with self.subTest(addition=addition):
                result = self.run_policy(valid_workflow() + addition)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("retired release workload or artifact", result.stderr)

    def test_cli_host_runner_pins_remain_required(self) -> None:
        for host in ("ubuntu", "windows", "macos"):
            with self.subTest(host=host):
                result = self.run_policy(valid_workflow().replace("runs-on: windows-2025", f"runs-on: {host}-latest"))
                self.assertEqual(result.returncode, 1)
                self.assertIn(f"moving runner label {host}-latest", result.stderr)

    def test_mutable_action_reference_is_rejected(self) -> None:
        workflow = valid_workflow().replace(
            PINNED_CHECKOUT, "actions/checkout@v4 # v4"
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn("mutable action reference actions/checkout@v4", result.stderr)

    def test_container_action_without_a_commit_sha_is_rejected(self) -> None:
        workflow = valid_workflow().replace(
            PINNED_CHECKOUT, "docker://alpine:latest # v3.22.1"
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn("mutable action reference docker://alpine:latest", result.stderr)

    def test_moving_toolchains_and_runner_are_rejected(self) -> None:
        workflow = (
            valid_workflow()
            .replace("toolchain: '1.93.0'", "toolchain: stable", 1)
            .replace("runs-on: ubuntu-24.04", "runs-on: ubuntu-latest", 1)
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn("Rust toolchain must match mise.toml (1.93.0)", result.stderr)
        self.assertIn("moving runner label ubuntu-latest", result.stderr)

    def test_repository_rust_ci_uses_repository_rust_toolchain(self) -> None:
        rust_ci = REPO_ROOT / ".github" / "workflows" / "rust-ci.yml"
        text = rust_ci.read_text(encoding="utf-8")
        self.assertIn("components: rustfmt, clippy, llvm-tools-preview", text)
        self.assertIn("targets: wasm32-unknown-unknown", text)

        result = subprocess.run(
            [sys.executable, str(CHECKER), str(rust_ci)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ordinary_rust_ci_rejects_toolchain_drift(self) -> None:
        rust_ci = (REPO_ROOT / ".github" / "workflows" / "rust-ci.yml").read_text(
            encoding="utf-8"
        )
        with tempfile.NamedTemporaryFile("w", suffix=".yml", encoding="utf-8") as file:
            file.write(rust_ci.replace("toolchain: '1.93.0'", "toolchain: stable"))
            file.flush()
            result = subprocess.run(
                [sys.executable, str(CHECKER), file.name],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

        self.assertEqual(result.returncode, 1)
        self.assertIn("Rust toolchain must match mise.toml (1.93.0)", result.stderr)

    def test_moving_bun_versions_are_rejected(self) -> None:
        workflow = (
            valid_workflow()
            .replace("bun-version: '1.3.11'", "bun-version: latest")
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn("Bun version must match mise.toml (1.3.11)", result.stderr)


    def test_write_permission_outside_publish_is_rejected(self) -> None:
        workflow = valid_workflow().replace(
            "  test:\n    runs-on:",
            "  test:\n    permissions:\n      contents: write\n    runs-on:",
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "job test has unexpected write permission contents: write", result.stderr
        )

    def test_scalar_write_all_permission_outside_publish_is_rejected(self) -> None:
        workflow = valid_workflow().replace(
            "  test:\n    runs-on:",
            "  test:\n    permissions: write-all\n    runs-on:",
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "job test has unexpected write permission write-all", result.stderr
        )

    def test_inline_write_permission_outside_publish_is_rejected(self) -> None:
        workflow = valid_workflow().replace(
            "  test:\n    runs-on:",
            "  test:\n    permissions: {contents: write}\n    runs-on:",
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "job test has unexpected write permission contents: write", result.stderr
        )

    def test_publish_without_tag_only_condition_is_rejected(self) -> None:
        workflow = valid_workflow().replace(
            "    if: github.event_name == 'push' && startsWith(github.ref, 'refs/tags/')\n",
            "",
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn("publish job must be restricted to tag pushes", result.stderr)

    def test_publish_condition_cannot_be_weakened_to_any_push(self) -> None:
        workflow = valid_workflow().replace(
            "github.event_name == 'push' && startsWith(github.ref, 'refs/tags/')",
            "github.event_name == 'push'",
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn("publish job must be restricted to tag pushes", result.stderr)

    def test_action_pin_requires_a_maintenance_comment(self) -> None:
        workflow = valid_workflow().replace(" # v4.4.0", "", 1)

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn("action pin is missing a version comment", result.stderr)

    def test_release_cargo_commands_must_use_locked_resolution(self) -> None:
        for command in ("cargo test --all-features", "cargo build --release"):
            with self.subTest(command=command):
                workflow = valid_workflow().replace(command + " --locked", command)

                result = self.run_policy(workflow)

                self.assertEqual(result.returncode, 1)
                self.assertIn("release Cargo commands must use --locked", result.stderr)

    def test_release_version_must_not_be_interpolated_into_shell_text(self) -> None:
        workflow = valid_workflow().replace(
            '"$RELEASE_VERSION"', '"${{ env.RELEASE_VERSION }}"', 1
        )

        result = self.run_policy(workflow)

        self.assertEqual(result.returncode, 1)
        self.assertIn("release version must be read from shell environment data", result.stderr)


if __name__ == "__main__":
    unittest.main()
