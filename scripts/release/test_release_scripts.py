from __future__ import annotations

import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CHECK_VERSION = REPO_ROOT / "scripts" / "release" / "check-version.py"
PACKAGE = REPO_ROOT / "scripts" / "release" / "package.py"


class ReleaseScriptTests(unittest.TestCase):
    def test_invalid_version_probe_is_rejected_without_shell_execution(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            probe = Path(temp_dir) / "probe-ran"
            invalid_version = f"v0.3.1$(touch {probe})"

            for script, args in (
                (CHECK_VERSION, [invalid_version]),
                (PACKAGE, ["--version", invalid_version, "--out-dir", temp_dir]),
            ):
                with self.subTest(script=script.name):
                    result = subprocess.run(
                        [sys.executable, str(script), *args],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                        check=False,
                    )

                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(
                        "release version must use vMAJOR.MINOR.PATCH form",
                        result.stderr,
                    )
                    self.assertFalse(probe.exists())

    def test_package_accepts_tag_prefixed_valid_version_for_naming(self) -> None:
        import importlib.util

        spec = importlib.util.spec_from_file_location("package", PACKAGE)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        self.assertEqual(module.normalized_version("refs/tags/v0.3.1"), "0.3.1")
        self.assertEqual(module.normalized_version("v0.3.1"), "0.3.1")
        self.assertEqual(module.normalized_version("0.3.1"), "0.3.1")

    def test_locked_cargo_command_rejects_stale_lockfile_fixture(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "src").mkdir()
            (root / "src" / "lib.rs").write_text(
                "pub fn ok() -> bool { true }\n", encoding="utf-8"
            )
            (root / "Cargo.toml").write_text(
                textwrap.dedent(
                    """
                    [package]
                    name = "stale-lock-fixture"
                    version = "0.1.0"
                    edition = "2024"
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            subprocess.run(
                ["cargo", "generate-lockfile", "--offline"],
                cwd=root,
                check=True,
                capture_output=True,
            )

            dep = root / "dep"
            (dep / "src").mkdir(parents=True)
            (dep / "src" / "lib.rs").write_text("pub fn value() -> u8 { 1 }\n", encoding="utf-8")
            (dep / "Cargo.toml").write_text(
                textwrap.dedent(
                    """
                    [package]
                    name = "stale-lock-dep"
                    version = "0.1.0"
                    edition = "2024"
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            with (root / "Cargo.toml").open("a", encoding="utf-8") as manifest:
                manifest.write('\n[dependencies]\nstale-lock-dep = { path = "dep" }\n')

            result = subprocess.run(
                ["cargo", "test", "--locked", "--offline"],
                cwd=root,
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("the lock file", result.stderr)
            self.assertIn("--locked was passed", result.stderr)


if __name__ == "__main__":
    unittest.main()
