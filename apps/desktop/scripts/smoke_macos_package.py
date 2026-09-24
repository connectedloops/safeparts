#!/usr/bin/env python3
"""Capture bounded development-host relocation evidence for a packaged app."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import signal
import subprocess
import sys
import time

OWNER_MARKER = ".safeparts-desktop-smoke"
EXPECTED_IMAGES = ("/MacOS/Safeparts", "/QtCore", "/QtGui", "/QtWidgets", "/libqcocoa.dylib", "/libqmacstyle.dylib")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def overlaps(left: Path, right: Path) -> bool:
    return left == right or left in right.parents or right in left.parents


def prepare_evidence(repo: Path, evidence: Path, protected: tuple[Path, ...] = ()) -> None:
    target = (repo / "target").resolve()
    try:
        relative = evidence.relative_to(target)
    except ValueError as error:
        raise RuntimeError("evidence directory must be inside repository target") from error
    if not relative.parts or evidence == target:
        raise RuntimeError("evidence directory must be a proper child of repository target")
    for path in protected:
        if overlaps(evidence, path.resolve()):
            raise RuntimeError(f"evidence directory overlaps protected input: {path}")
    marker = evidence / OWNER_MARKER
    if evidence.exists():
        if not marker.is_file() or marker.read_text() != "owned smoke directory\n":
            raise RuntimeError(f"refusing to delete unowned evidence directory: {evidence}")
        shutil.rmtree(evidence)
    evidence.mkdir(parents=True)
    marker.write_text("owned smoke directory\n")


def manifest_rows(manifest: dict) -> dict[str, dict]:
    rows = manifest.get("files")
    if not isinstance(rows, list):
        raise RuntimeError("manifest files must be a list")
    result: dict[str, dict] = {}
    for row in rows:
        if not isinstance(row, dict) or row.get("type") not in ("file", "symlink") or not isinstance(row.get("path"), str):
            raise RuntimeError("malformed manifest file row")
        pure = PurePosixPath(row["path"])
        if pure.is_absolute() or not pure.parts or any(part in ("", ".", "..") for part in pure.parts):
            raise RuntimeError(f"unsafe manifest path: {row['path']}")
        normalized = pure.as_posix()
        if row["path"] != normalized:
            raise RuntimeError(f"non-canonical manifest path: {row['path']}")
        if normalized in result:
            raise RuntimeError(f"duplicate manifest path: {normalized}")
        expected_keys = {"path", "type", "sha256" if row["type"] == "file" else "target"}
        if set(row) != expected_keys or not isinstance(row[expected_keys.difference({"path", "type"}).pop()], str):
            raise RuntimeError(f"malformed manifest file row: {normalized}")
        result[normalized] = row
    return result


def verify_manifest(package: Path, manifest: dict) -> None:
    app = package / "Safeparts.app"
    expected = manifest_rows(manifest)
    actual: dict[str, str] = {}
    for path in app.rglob("*"):
        if path.is_symlink():
            actual[path.relative_to(app).as_posix()] = "symlink"
        elif path.is_file():
            actual[path.relative_to(app).as_posix()] = "file"
    expected_types = {path: row["type"] for path, row in expected.items()}
    if actual != expected_types:
        missing = sorted(set(expected_types) - set(actual))
        extra = sorted(set(actual) - set(expected_types))
        changed = sorted(path for path in set(actual) & set(expected_types) if actual[path] != expected_types[path])
        raise RuntimeError(f"artifact tree mismatch; missing={missing}, extra={extra}, changed_type={changed}")
    for relative, row in expected.items():
        path = app / relative
        if row["type"] == "file" and sha256(path) != row["sha256"]:
            raise RuntimeError(f"artifact hash mismatch: {relative}")
        if row["type"] == "symlink" and os.readlink(path) != row["target"]:
            raise RuntimeError(f"artifact symlink mismatch: {relative}")


def loaded_paths(lines: list[str]) -> list[str]:
    return [line.split("> ", 1)[1] for line in lines if line.startswith("dyld[") and "> /" in line]


def contained(path: str, root: Path) -> bool:
    try:
        Path(path).resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def group_members(pgid: int) -> list[int]:
    # macOS ps exposes `sess` as a kernel session pointer rather than a POSIX
    # session ID. The child was created with start_new_session=True, so its
    # exact new PGID is the stable identity shared by surviving descendants.
    result = subprocess.run(["/bin/ps", "-ax", "-o", "pid=", "-o", "pgid=", "-o", "state="],
                            text=True, capture_output=True, check=True)
    members = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) == 3 and int(fields[1]) == pgid and not fields[2].startswith("Z"):
            members.append(int(fields[0]))
    return members


def cleanup_group(pgid: int, evidence_path: Path, timeout: float = 5.0) -> dict:
    record: dict[str, object] = {"pgid": pgid, "initial_members": group_members(pgid), "term_sent": False,
                                 "kill_sent": False, "final_members": []}
    try:
        if record["initial_members"]:
            os.killpg(pgid, signal.SIGTERM)
            record["term_sent"] = True
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline and group_members(pgid):
                time.sleep(0.05)
        remaining = group_members(pgid)
        if remaining:
            os.killpg(pgid, signal.SIGKILL)
            record["kill_sent"] = True
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline and group_members(pgid):
                time.sleep(0.05)
        record["final_members"] = group_members(pgid)
    finally:
        evidence_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    if record["final_members"]:
        raise RuntimeError(f"process group cleanup could not verify disappearance: {record['final_members']}")
    if record["kill_sent"]:
        raise RuntimeError("process group required forced SIGKILL cleanup")
    return record


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
    prepare_evidence(repo, evidence, (package, repo / "target/desktop-package-work", repo / "apps/desktop"))
    manifest_path = package / "manifest.json"
    manifest_hash_before = sha256(manifest_path)
    manifest = json.loads(manifest_path.read_text())
    if manifest["source"] != {"commit": subprocess.check_output(["/usr/bin/git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(), "tree": "clean"}:
        raise SystemExit("package source identity does not match clean checkout")
    verify_manifest(package, manifest)
    relocated = evidence / "Safeparts.app"
    subprocess.run(["/usr/bin/ditto", str(package / "Safeparts.app"), str(relocated)], check=True)
    verify_manifest(evidence, manifest)
    executable = relocated / "Contents/MacOS/Safeparts"
    log_path = evidence / "loaded-images.log"
    env = {key: value for key, value in os.environ.items() if not key.startswith(("DYLD_", "QT_"))}
    env.update({"DYLD_PRINT_LIBRARIES": "1", "LC_ALL": "C", "LANG": "C"})
    rss_samples: list[int] = []
    network = {"status": "not sampled", "note": "bounded observation is not network-silence proof"}
    footprint = {"status": "not sampled", "note": "idle sample is not capacity qualification"}
    process = None
    runtime_error = None
    exit_code = None
    with (evidence / "stdout.log").open("wb") as stdout, log_path.open("wb") as stderr:
        process = subprocess.Popen([str(executable)], stdout=stdout, stderr=stderr, env=env, start_new_session=True)
        pgid = process.pid
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"packaged app exited during startup: {process.returncode}")
                rss = subprocess.run(["/bin/ps", "-o", "rss=", "-p", str(process.pid)], text=True, capture_output=True, check=True).stdout.strip()
                if rss:
                    rss_samples.append(int(rss) * 1024)
                time.sleep(0.5)
            try:
                result = subprocess.run(["/usr/bin/footprint", "-p", str(process.pid)], text=True, capture_output=True, timeout=15)
                (evidence / "footprint.txt").write_text(result.stdout + result.stderr)
                footprint = {"status": "captured" if result.returncode == 0 else "unavailable", "exit": result.returncode,
                             "note": "single idle observation; not a peak or capacity gate"}
            except (FileNotFoundError, subprocess.TimeoutExpired) as error:
                footprint = {"status": "unavailable", "note": str(error)}
            try:
                result = subprocess.run(["/usr/bin/nettop", "-P", "-L", "1", "-J", "bytes_in,bytes_out", "-p", str(process.pid)], text=True, capture_output=True, timeout=10)
                (evidence / "nettop.txt").write_text(result.stdout + result.stderr)
                network = {"status": "captured" if result.returncode == 0 else "unavailable", "exit": result.returncode,
                           "note": "one bounded process-attributed observation; absent rows are not network-silence proof"}
            except (FileNotFoundError, subprocess.TimeoutExpired) as error:
                network = {"status": "unavailable", "note": str(error)}
        except BaseException as error:
            runtime_error = error
        finally:
            cleanup_error = None
            try:
                cleanup_group(pgid, evidence / "cleanup.json")
            except BaseException as error:
                cleanup_error = error
            exit_code = process.wait(timeout=5)
            if cleanup_error:
                raise cleanup_error
    if runtime_error:
        raise runtime_error
    if exit_code != -signal.SIGTERM:
        raise RuntimeError(f"expected bounded SIGTERM exit, got {exit_code}")
    paths = loaded_paths(log_path.read_text(errors="replace").splitlines())
    external = [path for path in paths if ("/opt/homebrew" in path or "/Qt" in path) and not contained(path, relocated)]
    missing = [name for name in EXPECTED_IMAGES if not any(name in path and contained(path, relocated) for path in paths)]
    if external:
        raise RuntimeError("external Qt/Homebrew images loaded:\n" + "\n".join(external))
    if missing:
        raise RuntimeError("expected deployed images were not loaded: " + ", ".join(missing))
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(relocated)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    verify_manifest(evidence, manifest)
    verify_manifest(package, manifest)
    if sha256(manifest_path) != manifest_hash_before:
        raise RuntimeError("package manifest changed during evidence capture")
    cleanup = json.loads((evidence / "cleanup.json").read_text())
    report = {"scope": "development-host launch/idle relocation smoke; not clean-host, offline, network-silence, or capacity proof",
              "source_commit": manifest["source"]["commit"], "manifest_sha256": manifest_hash_before,
              "expected_exit": "SIGTERM after bounded healthy idle", "exit_code": exit_code, "cleanup": cleanup,
              "loaded_image_records": len(paths), "in_bundle_image_records": sum(contained(path, relocated) for path in paths),
              "external_qt_or_homebrew_records": len(external), "rss_samples_bytes": rss_samples,
              "rss_observed_max_bytes": max(rss_samples), "private_footprint": footprint, "network": network}
    (evidence / "evidence.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(evidence / "evidence.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
