#!/usr/bin/env python3
"""Run the single bounded issue-151 native-window/AX comparison."""
from __future__ import annotations
import argparse, ctypes, hashlib, json, os, signal, subprocess, sys, time, uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import smoke_macos_package as smoke

EXPECTED_MANIFEST = "97a2aafd844c2eada2cdf8bd1e9a59c84f62f9da58900e3d023124d3d064f534"
EXPECTED_SOURCE = "1dde4cc1a91469543c1c193d4d052b8b7c4239b4"

def sha(path: Path) -> str: return hashlib.sha256(path.read_bytes()).hexdigest()
def executable_for_pid(pid: int) -> str:
    libproc = ctypes.CDLL("/usr/lib/libproc.dylib")
    buffer = ctypes.create_string_buffer(4096)
    size = libproc.proc_pidpath(pid, buffer, len(buffer))
    if size <= 0: raise RuntimeError("proc_pidpath failed")
    return os.path.realpath(buffer.value.decode())

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--package-dir", type=Path, required=True)
    args = parser.parse_args()
    repo, package = args.repo_root.resolve(), args.package_dir.resolve()
    if sys.platform != "darwin" or os.uname().machine != "arm64": raise SystemExit("requires Apple-silicon macOS")
    if subprocess.check_output(["/usr/bin/git", "status", "--porcelain"], cwd=repo, text=True): raise SystemExit("dirty source tree")
    manifest_path = package / "manifest.json"; manifest_hash = sha(manifest_path)
    if manifest_hash != EXPECTED_MANIFEST: raise SystemExit("unexpected package manifest identity")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("source") != {"commit": EXPECTED_SOURCE, "tree": "clean"}: raise SystemExit("unexpected package source identity")
    smoke.verify_manifest(package, manifest)
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(package / "Safeparts.app")], check=True, capture_output=True)
    root = (repo / "target/desktop-package-workflows").resolve(); attempts = root / "attempts"
    if not attempts.is_dir() or attempts.is_symlink(): raise SystemExit("invalid attempts root")
    attempt = attempts / f"diagnostic-{EXPECTED_SOURCE[:12]}-{manifest_hash[:12]}-{uuid.uuid4().hex[:12]}"
    attempt.mkdir(); (attempt / ".safeparts-desktop-workflows").write_text("owned workflow evidence\n")
    relocated = attempt / "Safeparts.app"
    subprocess.run(["/usr/bin/ditto", str(package / "Safeparts.app"), str(relocated)], check=True)
    smoke.verify_manifest(attempt, manifest)
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(relocated)], check=True, capture_output=True)
    helper_source = repo / "apps/desktop/tests/ax_readonly_diagnostic.swift"; helper = attempt / "ax-readonly-diagnostic"
    subprocess.run(["/usr/bin/xcrun", "swiftc", str(helper_source), "-o", str(helper), "-framework", "ApplicationServices", "-framework", "CoreGraphics"], check=True, timeout=30)
    head = subprocess.check_output(["/usr/bin/git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    metadata = {"package_source_commit": EXPECTED_SOURCE, "probe_source_commit": head, "manifest_sha256": manifest_hash,
                "helper_source_sha256": sha(helper_source), "helper_binary_sha256": sha(helper), "launch_count": 0}
    (attempt / "attempt.json").write_text(json.dumps(metadata, indent=2, sort_keys=True)+"\n")
    executable = relocated / "Contents/MacOS/Safeparts"; expected_executable = str(executable.resolve())
    env = {k:v for k,v in os.environ.items() if not k.startswith(("QT_", "DYLD_"))}; env.update({"LC_ALL":"C", "LANG":"C"})
    process = None; runtime_error = None
    with (attempt/"stdout.log").open("wb") as out, (attempt/"stderr.log").open("wb") as err:
      try:
        process = subprocess.Popen([str(executable)], stdout=out, stderr=err, env=env, start_new_session=True)
        metadata["launch_count"] = 1; metadata["pid"] = process.pid; metadata["launch_monotonic_ns"] = time.monotonic_ns()
        (attempt / "attempt.json").write_text(json.dumps(metadata, indent=2, sort_keys=True)+"\n")
        time.sleep(.15)
        if process.poll() is not None: raise RuntimeError("application exited before identity verification")
        actual = executable_for_pid(process.pid); metadata["canonical_executable"] = actual
        if actual != expected_executable: raise RuntimeError("PID executable identity mismatch")
        started = time.monotonic()
        result = subprocess.run([str(helper), str(process.pid)], capture_output=True, timeout=30)
        metadata["helper_elapsed_seconds"] = time.monotonic()-started
        (attempt/"ax-window-comparison.json").write_bytes(result.stdout)
        (attempt/"helper.stderr").write_bytes(result.stderr)
        if result.returncode != 0: raise RuntimeError(f"helper exited {result.returncode}")
        json.loads(result.stdout)
      except BaseException as error: runtime_error = error
      finally:
        if process is not None:
          cleanup_error = None
          try: cleanup = smoke.cleanup_group(process.pid, attempt/"cleanup.json")
          except BaseException as error: cleanup_error = error
          process.wait(timeout=5)
          if cleanup_error: runtime_error = runtime_error or cleanup_error
    smoke.verify_manifest(attempt, manifest); smoke.verify_manifest(package, manifest)
    if sha(manifest_path) != manifest_hash: raise RuntimeError("manifest changed")
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(relocated)], check=True, capture_output=True)
    metadata["post_checks"] = {"source_and_copy_manifest_trees": True, "deep_strict_codesign": True, "manifest_unchanged": True}
    (attempt/"attempt.json").write_text(json.dumps(metadata, indent=2, sort_keys=True)+"\n")
    status = {"status": "failed" if runtime_error else "completed", "error_type": type(runtime_error).__name__ if runtime_error else None,
              "cleanup": json.loads((attempt/"cleanup.json").read_text())}
    (attempt/"result.json").write_text(json.dumps(status, indent=2, sort_keys=True)+"\n")
    print(attempt)
    if runtime_error: raise runtime_error
    return 0
if __name__ == "__main__": raise SystemExit(main())
