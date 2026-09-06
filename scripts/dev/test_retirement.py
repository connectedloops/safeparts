"""Regression tests at the supported workload and build-input boundaries."""
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ACTIVE_CRATES = {
    "crates/safeparts_core", "crates/safeparts", "crates/safeparts_tui",
    "crates/safeparts_wasm",
}
RETIRED = ("desktop", "macos/", "windows/", "safeparts_uniffi", "tauri", "uniffi")


class RetirementTests(unittest.TestCase):
    def test_workspace_and_default_tasks_require_only_supported_surfaces(self) -> None:
        workspace = tomllib.loads((ROOT / "Cargo.toml").read_text())["workspace"]
        self.assertEqual(set(workspace["members"]), ACTIVE_CRATES)
        self.assertTrue({"desktop/src-tauri", "crates/safeparts_uniffi"} <= set(workspace["exclude"]))
        tasks = tomllib.loads((ROOT / "mise.toml").read_text())["tasks"]
        for name, task in tasks.items():
            with self.subTest(task=name):
                for retired in RETIRED:
                    self.assertNotIn(retired, name + str(task))
        self.assertEqual(set(tasks["verify"]["depends"]), {
            "verify:rust", "web:build:site", "dx:verify", "workflow:check",
        })
        self.assertEqual(set(tasks["verify:rust"]["depends"]), {"fmt-check", "lint", "test"})
        self.assertIn("cargo clippy --all-targets --all-features -- -D warnings", tasks["lint"]["run"])
        self.assertEqual(tasks["test"]["run"], "cargo test --all-features")
        self.assertIn("test_retirement.py", str(tasks["workflow:policy"]["run"]))

    def test_ci_runs_supported_checks_without_retired_workloads(self) -> None:
        for path in (ROOT / ".github/workflows").glob("*.yml"):
            text = path.read_text()
            with self.subTest(workflow=path.name):
                for retired in RETIRED:
                    self.assertNotIn(retired, text.lower())
        rust = (ROOT / ".github/workflows/rust-ci.yml").read_text()
        for command in ("cargo fmt --all -- --check", "cargo clippy --all-targets --all-features -- -D warnings",
                        "cargo test --all-features", "bash web/scripts/test-wasm.sh", "scripts/dev/rust_coverage.py",
                        "scripts/dev/test_retirement.py"):
            self.assertIn(command, rust)
        web = (ROOT / ".github/workflows/web-ci.yml").read_text()
        for command in ("bun run test:wasm", "bun run typecheck", "bun run test:e2e:full",
                        "PLAYWRIGHT_BASE_URL=http://127.0.0.1:4173", "python3 -m http.server"):
            self.assertIn(command, web)

    def test_release_preserves_cli_tui_hosts_and_checksum_safety(self) -> None:
        release = (ROOT / ".github/workflows/release.yml").read_text()
        build = release.split("  build:\n", 1)[1].split("  assemble:\n", 1)[0]
        self.assertEqual(set(re.findall(r"^            id: (.+)$", build, re.MULTILINE)), {"linux", "windows", "macos-x86_64", "macos-aarch64"})
        self.assertIn("-p safeparts -p safeparts_tui", build)
        self.assertIn("target: x86_64-apple-darwin", build)
        self.assertIn("target: aarch64-apple-darwin", build)
        publish = release.split("  publish:\n", 1)[1]
        globs = re.findall(r"^            (dist/release/\S+)$", publish, re.MULTILINE)
        self.assertEqual(set(globs), {"dist/release/**/*.tar.gz", "dist/release/**/*.zip", "dist/release/SHA256SUMS.txt"})
        code = release.split("          python - <<'PY'\n", 1)[1].split("          PY\n", 1)[0]
        code = "\n".join(line[10:] for line in code.splitlines())
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = root / "dist/release"
            assets.mkdir(parents=True)
            (assets / "synthetic.tar.gz").write_bytes(b"abc")
            result = subprocess.run([sys.executable, "-c", code], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((assets / "SHA256SUMS.txt").read_text(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  synthetic.tar.gz\n")
            (assets / "duplicate").mkdir()
            (assets / "duplicate/synthetic.tar.gz").write_bytes(b"different")
            result = subprocess.run([sys.executable, "-c", code], cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("asset names must be unique", result.stderr)

    def test_release_version_validation_ignores_dormant_manifests(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checker = root / "scripts/release/check-version.py"
            checker.parent.mkdir(parents=True)
            shutil.copyfile(ROOT / "scripts/release/check-version.py", checker)
            for crate in ACTIVE_CRATES:
                manifest = root / crate / "Cargo.toml"
                manifest.parent.mkdir(parents=True)
                manifest.write_text('[package]\nversion = "9.8.7"\n')
            for dormant in ("desktop/src-tauri/Cargo.toml", "crates/safeparts_uniffi/Cargo.toml"):
                manifest = root / dormant
                manifest.parent.mkdir(parents=True)
                manifest.write_text('not a valid active manifest')
            result = subprocess.run([sys.executable, str(checker), "v9.8.7"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            (root / "crates/safeparts/Cargo.toml").write_text('[package]\nversion = "1.0.0"\n')
            result = subprocess.run([sys.executable, str(checker), "v9.8.7"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("release version mismatch", result.stderr)

    def test_docker_build_inputs_resolve_without_retired_source(self) -> None:
        dockerfile = (ROOT / "web/Dockerfile").read_text()
        ignore = (ROOT / ".dockerignore").read_text().splitlines()
        for retired in ("desktop/", "macos/", "windows/", "crates/safeparts_uniffi/"):
            self.assertIn(retired, ignore)
        # Rehearse the real Cargo COPY boundary without requiring a Docker daemon.
        stage = dockerfile.split("FROM rust-tools AS wasm-builder\n", 1)[1].split("FROM ${BUN_IMAGE}", 1)[0]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for source, destination in re.findall(r"^COPY (\S+) (\S+)$", stage, re.MULTILINE):
                src, dst = ROOT / source, root / destination
                for retired in RETIRED:
                    self.assertNotIn(retired, source)
                dst.parent.mkdir(parents=True, exist_ok=True)
                if src.is_dir():
                    shutil.copytree(src, dst)
                else:
                    shutil.copyfile(src, dst)
            result = subprocess.run(["cargo", "metadata", "--offline", "--locked", "--format-version", "1", "--no-deps"], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            metadata = json.loads(result.stdout)
            self.assertEqual({package["name"] for package in metadata["packages"]}, {"safeparts_core", "safeparts_wasm"})


if __name__ == "__main__":
    unittest.main()
