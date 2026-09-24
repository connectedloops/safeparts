#!/usr/bin/env python3
"""Capture bounded development-host relocation evidence for a packaged app."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

OWNER_MARKER = ".safeparts-desktop-smoke"
EXPECTED_IMAGES = ("/MacOS/Safeparts", "/QtCore", "/QtGui", "/QtWidgets", "/libqcocoa.dylib", "/libqmacstyle.dylib")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare_evidence(repo: Path, evidence: Path) -> None:
    target = (repo / "target").resolve()
    try:
        evidence.relative_to(target)
    except ValueError as error:
        raise RuntimeError("evidence directory must be inside repository target") from error
    marker = evidence / OWNER_MARKER
    if evidence.exists():
        if not marker.is_file() or marker.read_text() != "owned smoke directory\n":
            raise RuntimeError(f"refusing to delete unowned evidence directory: {evidence}")
        shutil.rmtree(evidence)
    evidence.mkdir(parents=True)
    marker.write_text("owned smoke directory\n")


def verify_manifest(package: Path, manifest: dict) -> None:
    app = package / "Safeparts.app"
    for row in manifest["files"]:
        path = app / row["path"]
        if row["type"] == "file":
            if not path.is_file() or sha256(path) != row["sha256"]:
                raise RuntimeError(f"artifact identity mismatch: {row['path']}")
        elif not path.is_symlink() or os.readlink(path) != row["target"]:
            raise RuntimeError(f"artifact symlink mismatch: {row['path']}")


def loaded_paths(lines: list[str]) -> list[str]:
    paths = []
    for line in lines:
        if line.startswith("dyld[") and "> /" in line:
            paths.append(line.split("> ", 1)[1])
    return paths


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    if sys.platform != "darwin" or os.uname().machine != "arm64":
        raise SystemExit("package smoke requires Apple-silicon macOS")
    repo, package, evidence = args.repo_root.resolve(), args.package_dir.resolve(), args.evidence_dir.resolve()
    if subprocess.run(["/usr/bin/git", "status", "--porcelain"], cwd=repo, text=True, capture_output=True, check=True).stdout:
        raise SystemExit("refusing evidence from a dirty source tree")
    manifest_path = package / "manifest.json"
    manifest_hash_before = sha256(manifest_path)
    manifest = json.loads(manifest_path.read_text())
    if manifest["source"] != {"commit": subprocess.check_output(["/usr/bin/git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(), "tree": "clean"}:
        raise SystemExit("package source identity does not match clean checkout")
    verify_manifest(package, manifest)
    prepare_evidence(repo, evidence)
    relocated = evidence / "Safeparts.app"
    subprocess.run(["/usr/bin/ditto", str(package / "Safeparts.app"), str(relocated)], check=True)
    executable = relocated / "Contents/MacOS/Safeparts"
    log_path = evidence / "loaded-images.log"
    env = {key: value for key, value in os.environ.items() if not key.startswith(("DYLD_", "QT_"))}
    env.update({"DYLD_PRINT_LIBRARIES": "1", "LC_ALL": "C", "LANG": "C"})
    rss_samples: list[int] = []
    network = {"status": "not sampled", "note": "bounded observation is not network-silence proof"}
    footprint = {"status": "not sampled", "note": "idle sample is not capacity qualification"}
    with (evidence / "stdout.log").open("wb") as stdout, log_path.open("wb") as stderr:
        process = subprocess.Popen([str(executable)], stdout=stdout, stderr=stderr, env=env, start_new_session=True)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"packaged app exited during startup: {process.returncode}")
                rss = subprocess.run(["/bin/ps", "-o", "rss=", "-p", str(process.pid)], text=True,
                                     capture_output=True, check=True).stdout.strip()
                if rss:
                    rss_samples.append(int(rss) * 1024)
                time.sleep(0.5)
            try:
                result = subprocess.run(["/usr/bin/footprint", "-p", str(process.pid)], text=True,
                                        capture_output=True, timeout=15)
                (evidence / "footprint.txt").write_text(result.stdout + result.stderr)
                footprint = {"status": "captured" if result.returncode == 0 else "unavailable",
                             "exit": result.returncode, "note": "single idle observation; not a peak or capacity gate"}
            except (FileNotFoundError, subprocess.TimeoutExpired) as error:
                footprint = {"status": "unavailable", "note": str(error)}
            try:
                result = subprocess.run(["/usr/bin/nettop", "-P", "-L", "1", "-J", "bytes_in,bytes_out", "-p", str(process.pid)],
                                        text=True, capture_output=True, timeout=10)
                (evidence / "nettop.txt").write_text(result.stdout + result.stderr)
                network = {"status": "captured" if result.returncode == 0 else "unavailable",
                           "exit": result.returncode,
                           "note": "one bounded process-attributed observation; absent rows are not network-silence proof"}
            except (FileNotFoundError, subprocess.TimeoutExpired) as error:
                network = {"status": "unavailable", "note": str(error)}
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
            try:
                exit_code = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                exit_code = process.wait(timeout=5)
                raise RuntimeError("packaged app ignored bounded SIGTERM cleanup")
    if exit_code != -signal.SIGTERM:
        raise RuntimeError(f"expected bounded SIGTERM exit, got {exit_code}")
    lines = log_path.read_text(errors="replace").splitlines()
    paths = loaded_paths(lines)
    relocated_root = str(relocated.resolve())
    external = [path for path in paths if ("/opt/homebrew" in path or "/Qt" in path) and not path.startswith(relocated_root)]
    missing = [name for name in EXPECTED_IMAGES if not any(name in path and path.startswith(relocated_root) for path in paths)]
    if external:
        raise RuntimeError("external Qt/Homebrew images loaded:\n" + "\n".join(external))
    if missing:
        raise RuntimeError("expected deployed images were not loaded: " + ", ".join(missing))
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(relocated)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    verify_manifest(package, manifest)
    if sha256(manifest_path) != manifest_hash_before:
        raise RuntimeError("package manifest changed during evidence capture")
    report = {"scope": "development-host launch/idle relocation smoke; not clean-host, offline, network-silence, or capacity proof",
              "source_commit": manifest["source"]["commit"], "manifest_sha256": manifest_hash_before,
              "expected_exit": "SIGTERM after bounded healthy idle", "exit_code": exit_code,
              "loaded_image_records": len(paths), "in_bundle_image_records": sum(path.startswith(relocated_root) for path in paths),
              "external_qt_or_homebrew_records": len(external), "rss_samples_bytes": rss_samples,
              "rss_observed_max_bytes": max(rss_samples), "private_footprint": footprint, "network": network}
    (evidence / "evidence.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(evidence / "evidence.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
