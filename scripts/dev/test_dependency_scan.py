"""Controlled fixtures at the dependency-scan CLI and external-tool boundary."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CRATES = ["safeparts_core", "safeparts", "safeparts_tui", "safeparts_wasm"]

# External scanner fixture: discovers every lockfile it is actually given, including
# poison examples. Production orchestration, staging, validation and reporting run unchanged.
TOOL = r'''#!/usr/bin/env python3
import json, os, pathlib, sys
from datetime import datetime, timedelta, timezone
args = sys.argv[1:]
name = pathlib.Path(sys.argv[0]).name
mode = os.environ.get("FIXTURE_MODE", "")
if name == "bun":
    if args == ["--version"]:
        print("1.3.11")
    else:
        print(pathlib.Path(args[-1]).read_text())
    sys.exit(0)
if name == "cargo":
    if mode == "cargo-stale":
        print("Cargo.lock needs to be updated but --locked was passed", file=sys.stderr)
        sys.exit(101)
    crates = ["safeparts_core", "safeparts", "safeparts_tui", "safeparts_wasm"]
    print(json.dumps({"workspace_members": crates, "packages": [
        {"id": c, "name": c, "manifest_path": str(pathlib.Path.cwd() / "crates" / c / "Cargo.toml")}
        for c in crates]}))
    sys.exit(0)
if args[:1] == ["version"]:
    print(json.dumps({"Version": "0.1.0" if mode == "version" else "0.74.0"}))
    sys.exit(0)
cache = pathlib.Path(args[args.index("--cache-dir") + 1])
if "--download-db-only" in args:
    if mode == "refresh":
        sys.exit(9)
    now = datetime.now(timezone.utc)
    db = cache / "db"
    db.mkdir(parents=True, exist_ok=True)
    (db / "trivy.db").write_bytes(b"fixture advisory database")
    (db / "metadata.json").write_text(json.dumps({"Version": 2,
        "UpdatedAt": (now - timedelta(hours=1)).isoformat(),
        "DownloadedAt": now.isoformat(),
        "NextUpdate": (now + timedelta(hours=-1 if mode == "stale" else 12)).isoformat()}))
    sys.exit(0)
if mode == "scanner-error":
    print("network failure", file=sys.stderr)
    sys.exit(8)
if mode == "malformed":
    print("not JSON")
    sys.exit(0)
results = []
for path in sorted(pathlib.Path(args[-1]).rglob("*.lock")):
    text = path.read_text()
    # This sentinel is deliberately a development-only dependency finding.
    vulnerable = "fixture-vulnerable" in text and "--include-dev-deps" in args
    if os.environ.get("TRIVY_SEVERITY") == "CRITICAL":
        vulnerable = False
    ignore = pathlib.Path(args[args.index("--ignorefile") + 1]) if "--ignorefile" in args else pathlib.Path(".trivyignore")
    if ignore.exists() and "GHSA-fixture-dev" in ignore.read_text():
        vulnerable = False
    package = {"ID": "fixture@1.0.0", "Name": "fixture", "Version": "1.0.0", "Dev": True}
    results.append({"Target": str(path.relative_to(args[-1])), "Class": "lang-pkgs",
        "Type": "cargo" if path.name == "Cargo.lock" else "bun", "Packages": [package],
        "Vulnerabilities": [{"VulnerabilityID": "GHSA-fixture-dev", "PkgID": "fixture@1.0.0",
            "PkgName": "fixture", "InstalledVersion": "1.0.0", "Severity": "HIGH"}] if vulnerable else []})
if mode == "missing-result":
    results.pop()
if mode == "extra-result":
    results.append(dict(results[0], Target="node_modules/example/Cargo.lock"))
if mode == "empty-packages":
    results[0]["Packages"] = []
if mode == "wrong-type":
    results[0]["Type"] = "gemspec"
if mode == "bad-vulnerability":
    results[0]["Vulnerabilities"] = [{}]
if mode == "wrong-schema":
    print(json.dumps({"error": "advisory lookup failed"}))
else:
    print(json.dumps({"SchemaVersion": 2, "Results": results}))
sys.exit(0)
'''


class DependencyScanTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        script = self.root / "scripts/dev/dependency_scan.py"
        script.parent.mkdir(parents=True)
        if (ROOT / "scripts/dev/dependency_scan.py").exists():
            shutil.copyfile(ROOT / "scripts/dev/dependency_scan.py", script)
        self.script = script
        tools = self.root / "bin"
        tools.mkdir()
        for name in ("trivy", "bun", "cargo"):
            tool = tools / name
            tool.write_text(TOOL.replace("#!/usr/bin/env python3", f"#!{sys.executable}"))
            tool.chmod(0o755)
        self.env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"])
        self.put("Cargo.toml", '[workspace]\nmembers = ' + json.dumps([f"crates/{c}" for c in CRATES]) +
                 '\nexclude = ["desktop/src-tauri", "crates/safeparts_uniffi"]\n')
        self.put("Cargo.lock", 'version = 4\n[[package]]\nname = "fixture"\nversion = "1.0.0"\n')
        for crate in CRATES:
            self.put(f"crates/{crate}/Cargo.toml", f'[package]\nname = "{crate}"\nversion = "0.1.0"\n')
        for directory in ("web", "web/help", "desktop"):
            self.put(f"{directory}/package.json", '{"name":"fixture", "devDependencies":{"fixture":"1.0.0"}}')
            self.put(f"{directory}/bun.lock", json.dumps({"lockfileVersion": 1,
                "workspaces": {"": {"name": "fixture", "devDependencies": {"fixture": "1.0.0"}}},
                "packages": {"fixture": ["fixture@1.0.0", "", {}, "integrity"]}}))
        self.put("mobile/src-native/Cargo.lock", (self.root / "Cargo.lock").read_text())

    def put(self, path: str, text: str) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def scan(self, scope: str = "supported", mode: str = "") -> tuple[subprocess.CompletedProcess[str], dict]:
        result = subprocess.run([sys.executable, str(self.script), "--scope", scope],
                                cwd=self.root, env=dict(self.env, FIXTURE_MODE=mode),
                                text=True, capture_output=True)
        path = self.root / f"target/security/{scope}/report.json"
        return result, json.loads(path.read_text()) if path.exists() else {}

    def test_supported_scan_retains_dev_findings_without_example_or_retired_inputs(self) -> None:
        path = self.root / "web/help/bun.lock"
        path.write_text(path.read_text().replace('"integrity"', '"fixture-vulnerable"'))
        for poison in ("mobile/src-native/Cargo.lock", "desktop/bun.lock",
                       "web/node_modules/example/Gemfile.lock", "web/help/dist/bun.lock",
                       "web/src/wasm_pkg/Cargo.lock", "target/generated/Cargo.lock",
                       "windows/obj/Cargo.lock", "macos/.build/Cargo.lock",
                       "crates/safeparts_uniffi/Cargo.lock"):
            self.put(poison, "fixture-vulnerable")
        result, report = self.scan()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual([item["path"] for item in report["inventory"]],
                         ["Cargo.lock", "web/bun.lock", "web/help/bun.lock"])
        self.assertEqual(report["finding_count"], 1)
        self.assertEqual(report["results"][2]["Vulnerabilities"][0]["VulnerabilityID"], "GHSA-fixture-dev")
        self.assertIn("web/help/bun.lock: 1", result.stdout)
        self.assertEqual(report["status"], "findings")
        self.assertTrue(report["include_dev_dependencies"])
        self.assertTrue(report["coverage_complete"])

    def test_operational_failures_replace_previous_success_with_error_report(self) -> None:
        result, report = self.scan()
        self.assertEqual(result.returncode, 0, result.stderr)
        for mode in ("version", "refresh", "stale", "scanner-error", "malformed"):
            with self.subTest(mode=mode):
                result, report = self.scan(mode=mode)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(report["status"], "error")
                self.assertTrue(report["errors"])
                self.assertIsNone(report["finding_count"])
                self.assertFalse(report["coverage_complete"])
                self.assertIn("ERROR", (self.root / "target/security/supported/summary.txt").read_text())
        (self.root / "bin/trivy").unlink()
        self.env["PATH"] = str(self.root / "bin")
        result, report = self.scan()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(report["status"], "error")

    def test_retired_report_separates_locked_findings_and_unresolved_surfaces(self) -> None:
        result, report = self.scan("retired")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(report["coverage_complete"])
        self.assertEqual({gap["surface"] for gap in report["coverage_gaps"]},
                         {"desktop/src-tauri", "macos", "windows", "crates/safeparts_uniffi"})
        self.assertIn("INCOMPLETE COVERAGE", result.stdout)
        self.assertNotIn("PASSED", result.stdout)
        self.assertEqual([item["path"] for item in report["inventory"]],
                         ["desktop/bun.lock", "mobile/src-native/Cargo.lock"])
        self.put("mobile/src-native/Cargo.lock", (self.root / "Cargo.lock").read_text() + "# fixture-vulnerable\n")
        self.put("mobile/node_modules/example/Gemfile.lock", "fixture-vulnerable")
        self.put("web/help/bun.lock", "fixture-vulnerable")
        result, report = self.scan("retired")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["finding_count"], 1)
        self.assertIn("mobile/src-native/Cargo.lock: 1", result.stdout)

    def test_missing_or_invalid_scanner_inventory_never_passes(self) -> None:
        for mode in ("missing-result", "extra-result", "empty-packages", "wrong-type", "bad-vulnerability", "wrong-schema"):
            with self.subTest(mode=mode):
                result, report = self.scan(mode=mode)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(report["status"], "error")
                self.assertIsNone(report["finding_count"])

    def test_invalid_inputs_and_manifest_graph_drift_fail_closed(self) -> None:
        cases = {
            "missing-lock": ("web/help/bun.lock", None),
            "invalid-lock": ("web/bun.lock", "not JSONC"),
            "invalid-cargo": ("Cargo.lock", "not TOML"),
            "missing-manifest": ("web/help/package.json", None),
            "manifest-drift": ("web/package.json", '{"name":"fixture","devDependencies":{"fixture":"2.0.0"}}'),
            "workspace-drift": ("Cargo.toml", '[workspace]\nmembers=["desktop/src-tauri"]\n'),
        }
        for name, (path, content) in cases.items():
            with self.subTest(case=name):
                file = self.root / path
                original = file.read_text()
                if content is None:
                    file.unlink()
                else:
                    file.write_text(content)
                result, report = self.scan()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(report["status"], "error")
                file.write_text(original)
        result, report = self.scan(mode="cargo-stale")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("--locked", " ".join(report["errors"]))

    def test_symlink_cannot_substitute_a_retired_graph_for_supported_input(self) -> None:
        lock = self.root / "web/bun.lock"
        lock.unlink()
        lock.symlink_to(self.root / "desktop/bun.lock")
        result, report = self.scan()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("symlink", " ".join(report["errors"]))

    def test_reports_record_reproduction_metadata_and_ignore_ambient_suppressions(self) -> None:
        path = self.root / "web/bun.lock"
        path.write_text(path.read_text().replace('"integrity"', '"fixture-vulnerable"'))
        self.put(".trivyignore", "GHSA-fixture-dev\n")
        self.put("trivy.yaml", "severity: [CRITICAL]\nignore-unfixed: true\n")
        self.env.update(TRIVY_SEVERITY="CRITICAL", TRIVY_SKIP_DB_UPDATE="true",
                        TRIVY_IGNOREFILE=str(self.root / ".trivyignore"))
        result, report = self.scan()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["scanner"]["Version"], "0.74.0")
        self.assertEqual(len(report["database"]["sha256"]), 64)
        self.assertTrue(report["database"]["metadata"]["NextUpdate"])
        self.assertTrue(report["started_at"])
        self.assertTrue(report["finished_at"])
        self.assertEqual(len(report["inventory"][0]["sha256"]), 64)
        self.assertEqual(report["totals"]["by_severity"], {"HIGH": 1})
        self.assertEqual(report["totals"]["dev_findings"], 1)
        self.assertIn("Trivy 0.74.0", result.stdout)
        self.assertIn("HIGH=1", result.stdout)
        self.assertIn("dev=1", result.stdout)
        self.assertTrue((self.root / "target/security/supported/scanner.json").is_file())


if __name__ == "__main__":
    unittest.main()
