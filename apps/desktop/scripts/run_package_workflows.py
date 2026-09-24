#!/usr/bin/env python3
"""Drive bounded synthetic workflows through the relocated production macOS app."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from collections.abc import Callable


def load(name: str):
    path = Path(__file__).with_name(name + ".py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


SMOKE = load("smoke_macos_package")
MARKER = ".safeparts-desktop-workflows"
FAILURE_STATUS = "The passphrase may be incorrect or the Recovery shares may be damaged. No output was shown."


class ElementNotFound(RuntimeError):
    pass


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_text(*args: str, timeout: float = 15) -> str:
    return subprocess.run(args, check=True, text=True, capture_output=True, timeout=timeout).stdout.strip()


def remaining(deadline: float, maximum: float = 2) -> float:
    value = min(maximum, deadline - time.monotonic())
    if value <= 0:
        raise TimeoutError("operation deadline expired")
    return value


def ax_call(helper: Path, pid: int, command: str, role: str, title: str,
            occurrence: int = 0, value: str | None = None, *, deadline: float) -> bytes:
    try:
        os.kill(pid, 0)
    except ProcessLookupError as error:
        raise RuntimeError("target app exited") from error
    args = [str(helper), command, str(pid), role, title, str(occurrence)]
    if value is not None:
        args.append(value)
    result = subprocess.run(args, capture_output=True, timeout=remaining(deadline))
    if result.returncode == 0:
        return result.stdout
    if result.returncode == 4:
        raise ElementNotFound(f"AX element not found: {role} {title}")
    if result.returncode == 3:
        raise RuntimeError("Accessibility permission became unavailable")
    raise RuntimeError(f"AX helper failed for {command} {role} {title}: exit {result.returncode}")


def poll(probe: Callable[[], object], accept: Callable[[object], bool], *, deadline: float,
         description: str) -> object:
    last: object = None
    while time.monotonic() < deadline:
        last = probe()
        if accept(last):
            return last
        time.sleep(min(0.05, max(0, deadline - time.monotonic())))
    raise TimeoutError(f"timed out waiting for {description}; last state {last!r}")


def ax_value(helper: Path, pid: int, role: str, title: str, *, deadline: float) -> bytes:
    return ax_call(helper, pid, "get", role, title, deadline=deadline)


def ax_enabled(helper: Path, pid: int, role: str, title: str, *, deadline: float) -> bool:
    value = ax_call(helper, pid, "enabled", role, title, deadline=deadline).strip()
    if value not in (b"0", b"1", b"false", b"true"):
        raise RuntimeError(f"invalid AX enabled value for {role} {title}")
    return value in (b"1", b"true")


def ax_exists(helper: Path, pid: int, role: str, title: str, *, deadline: float) -> bool:
    try:
        ax_call(helper, pid, "get", role, title, deadline=deadline)
        return True
    except ElementNotFound:
        return False


def wait_value(helper: Path, pid: int, role: str, title: str, *, deadline: float) -> bytes:
    def probe():
        try:
            return ax_value(helper, pid, role, title, deadline=deadline)
        except ElementNotFound:
            return None
    return poll(probe, lambda value: isinstance(value, bytes) and bool(value), deadline=deadline,
                description=f"nonempty {role} {title}")


def write_clipboard(data: bytes, *, deadline: float) -> None:
    subprocess.run(["/usr/bin/pbcopy"], input=data, check=True, capture_output=True,
                   timeout=remaining(deadline))


def read_clipboard(*, deadline: float) -> bytes:
    return subprocess.run(["/usr/bin/pbpaste"], check=True, capture_output=True,
                          timeout=remaining(deadline)).stdout


def verified_copy(expected: bytes, sentinel: bytes, invoke: Callable[[], None], *, deadline: float,
                  write: Callable[[bytes], None], read: Callable[[], bytes]) -> None:
    if sentinel == expected:
        raise ValueError("clipboard sentinel must differ from expected bytes")
    write(sentinel)
    if read() != sentinel:
        raise RuntimeError("synthetic clipboard sentinel verification failed")
    invoke()
    poll(lambda: read(), lambda value: value == expected, deadline=deadline,
         description="exact clipboard Copy handoff")


def wait_failure(status: Callable[[], bytes], enabled: Callable[[], bool], absent: Callable[[], bool],
                 clipboard: Callable[[], bytes], sentinel: bytes, *, deadline: float) -> None:
    poll(status, lambda value: value == FAILURE_STATUS.encode(), deadline=deadline,
         description="wrong-passphrase failure status")
    poll(enabled, lambda value: value is True, deadline=deadline,
         description="resolved wrong-passphrase busy state")
    if not absent():
        raise RuntimeError("wrong passphrase exposed recovered output")
    if clipboard() != sentinel:
        raise RuntimeError("wrong-passphrase attempt changed synthetic clipboard")


def verify_dialog_cancellation(snapshot: Callable[[], object], present: Callable[[], bool],
                               open_dialog: Callable[[], None], dismiss: Callable[[], None],
                               responsive: Callable[[], None], *, deadline: float) -> None:
    before = snapshot()
    if present():
        raise RuntimeError("native dialog existed before open action")
    open_dialog()
    poll(lambda: present(), bool, deadline=deadline, description="native dialog appearance")
    dismiss()
    poll(lambda: present(), lambda value: not value, deadline=deadline, description="native dialog dismissal")
    if snapshot() != before:
        raise RuntimeError("native dialog cancellation changed app state")
    responsive()


def prepare(repo: Path, evidence: Path, package: Path) -> None:
    target = (repo / "target").resolve()
    evidence.relative_to(target)
    if evidence == target or SMOKE.overlaps(evidence, package):
        raise RuntimeError("unsafe workflow evidence path")
    marker = evidence / MARKER
    if evidence.exists():
        if not marker.is_file() or marker.read_text() != "owned workflow evidence\n":
            raise RuntimeError("refusing unowned workflow evidence")
        shutil.rmtree(evidence)
    evidence.mkdir(parents=True)
    marker.write_text("owned workflow evidence\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    repo, package, evidence = args.repo_root.resolve(), args.package_dir.resolve(), args.evidence_dir.resolve()
    if run_text("/usr/bin/git", "status", "--porcelain"):
        raise SystemExit("dirty source tree")
    manifest_bytes = (package / "manifest.json").read_bytes()
    manifest_digest = sha(manifest_bytes)
    manifest = json.loads(manifest_bytes)
    head = run_text("/usr/bin/git", "rev-parse", "HEAD")
    if manifest["source"] != {"commit": head, "tree": "clean"}:
        raise SystemExit("package source mismatch")
    SMOKE.verify_manifest(package, manifest)
    prepare(repo, evidence, package)
    app = evidence / "Safeparts.app"
    subprocess.run(["/usr/bin/ditto", str(package / "Safeparts.app"), str(app)], check=True)
    SMOKE.verify_manifest(evidence, manifest)
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)], check=True, capture_output=True)
    helper = evidence / "ax-harness"
    subprocess.run(["/usr/bin/xcrun", "swiftc", str(repo / "apps/desktop/tests/ax_harness.swift"), "-o", str(helper), "-framework", "ApplicationServices"], check=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith(("DYLD_", "QT_"))}
    env["DYLD_PRINT_LIBRARIES"] = "1"
    stdout = (evidence / "stdout.log").open("wb")
    stderr = (evidence / "loaded-images.log").open("wb")
    process = subprocess.Popen([str(app / "Contents/MacOS/Safeparts")], env=env, stdout=stdout, stderr=stderr, start_new_session=True)
    results: list[dict[str, object]] = []
    try:
        pid = process.pid
        ready_deadline = time.monotonic() + 15
        poll(lambda: ax_exists(helper, pid, "AXWindow", "Safeparts", deadline=ready_deadline), bool,
             deadline=ready_deadline, description="target app window")
        secret = "  Synthetic café مرحبا 👩🏽‍🔬\nline two  \n"
        expected = secret.encode()
        ax_call(helper, pid, "type", "AXTextArea", "Secret", value=secret, deadline=time.monotonic() + 5)
        ax_call(helper, pid, "press", "AXButton", "Split", deadline=time.monotonic() + 5)
        shares = [wait_value(helper, pid, "AXTextArea", f"Recovery share {index}", deadline=time.monotonic() + 30) for index in (1, 2)]
        copy_sentinel = b"  synthetic clipboard sentinel: generated share  \n"
        verified_copy(shares[0], copy_sentinel,
                      lambda: ax_call(helper, pid, "press", "AXButton", "Copy Recovery share 1", deadline=time.monotonic() + 2),
                      deadline=time.monotonic() + 10,
                      write=lambda data: write_clipboard(data, deadline=time.monotonic() + 2),
                      read=lambda: read_clipboard(deadline=time.monotonic() + 2))
        ax_call(helper, pid, "press", "AXRadioButton", "Combine", deadline=time.monotonic() + 5)
        for index, share in enumerate(shares, 1):
            ax_call(helper, pid, "type", "AXTextArea", f"Recovery share {index}", value=share.decode(), deadline=time.monotonic() + 5)
        combine_deadline = time.monotonic() + 20
        poll(lambda: ax_enabled(helper, pid, "AXButton", "Combine", deadline=combine_deadline), bool,
             deadline=combine_deadline, description="unprotected Combine readiness")
        ax_call(helper, pid, "press", "AXButton", "Combine", deadline=time.monotonic() + 5)
        recovered = wait_value(helper, pid, "AXTextArea", "Recovered secret", deadline=time.monotonic() + 30)
        if recovered != expected:
            raise RuntimeError("unprotected UTF-8 recovery mismatch")
        recovered_sentinel = b"  synthetic clipboard sentinel: recovered secret  \n"
        verified_copy(expected, recovered_sentinel,
                      lambda: ax_call(helper, pid, "press", "AXButton", "Copy recovered Secret", deadline=time.monotonic() + 2),
                      deadline=time.monotonic() + 10,
                      write=lambda data: write_clipboard(data, deadline=time.monotonic() + 2),
                      read=lambda: read_clipboard(deadline=time.monotonic() + 2))
        results.append({"case": "words-unprotected-utf8-copy", "status": "passed", "secret_sha256": sha(expected), "share_count_used": 2, "copy_exact_bytes": True})

        ax_call(helper, pid, "press", "AXRadioButton", "Split", deadline=time.monotonic() + 5)
        ax_call(helper, pid, "press", "AXButton", "Start over", deadline=time.monotonic() + 5)
        ax_call(helper, pid, "type", "AXTextArea", "Secret", value="protected synthetic", deadline=time.monotonic() + 5)
        ax_call(helper, pid, "press", "AXCheckBox", "Protect with passphrase", deadline=time.monotonic() + 5)
        ax_call(helper, pid, "type", "AXTextArea", "Passphrase input (contents hidden)", value="synthetic-pass", deadline=time.monotonic() + 5)
        ax_call(helper, pid, "type", "AXTextArea", "Passphrase confirmation (contents hidden)", value="synthetic-pass", deadline=time.monotonic() + 5)
        split_deadline = time.monotonic() + 10
        poll(lambda: ax_enabled(helper, pid, "AXButton", "Split", deadline=split_deadline), bool, deadline=split_deadline, description="protected Split readiness")
        ax_call(helper, pid, "press", "AXButton", "Split", deadline=time.monotonic() + 5)
        protected_shares = [wait_value(helper, pid, "AXTextArea", f"Recovery share {index}", deadline=time.monotonic() + 30) for index in (1, 2)]
        ax_call(helper, pid, "press", "AXRadioButton", "Combine", deadline=time.monotonic() + 5)
        for index, share in enumerate(protected_shares, 1):
            ax_call(helper, pid, "type", "AXTextArea", f"Recovery share {index}", value=share.decode(), deadline=time.monotonic() + 5)
        protected_ready = time.monotonic() + 20
        poll(lambda: ax_exists(helper, pid, "AXTextArea", "Passphrase input (contents hidden)", deadline=protected_ready), bool,
             deadline=protected_ready, description="protected recovery passphrase editor")
        ax_call(helper, pid, "type", "AXTextArea", "Passphrase input (contents hidden)", value="wrong-synthetic", deadline=time.monotonic() + 5)
        failure_clipboard = b"  synthetic clipboard sentinel: wrong passphrase  \n"
        write_clipboard(failure_clipboard, deadline=time.monotonic() + 2)
        if read_clipboard(deadline=time.monotonic() + 2) != failure_clipboard:
            raise RuntimeError("wrong-passphrase clipboard sentinel verification failed")
        wrong_ready = time.monotonic() + 20
        poll(lambda: ax_enabled(helper, pid, "AXButton", "Combine", deadline=wrong_ready), bool, deadline=wrong_ready, description="wrong-passphrase Combine readiness")
        if ax_exists(helper, pid, "AXStaticText", FAILURE_STATUS, deadline=time.monotonic() + 2):
            raise RuntimeError("wrong-passphrase failure status existed before attempt")
        ax_call(helper, pid, "press", "AXButton", "Combine", deadline=time.monotonic() + 5)
        wait_failure(
            lambda: FAILURE_STATUS.encode() if ax_exists(helper, pid, "AXStaticText", FAILURE_STATUS, deadline=time.monotonic() + 2) else b"",
            lambda: ax_enabled(helper, pid, "AXButton", "Combine", deadline=time.monotonic() + 2),
            lambda: not ax_exists(helper, pid, "AXTextArea", "Recovered secret", deadline=time.monotonic() + 2),
            lambda: read_clipboard(deadline=time.monotonic() + 2), failure_clipboard, deadline=time.monotonic() + 40)
        ax_call(helper, pid, "type", "AXTextArea", "Passphrase input (contents hidden)", value="synthetic-pass", deadline=time.monotonic() + 5)
        retry_ready = time.monotonic() + 20
        poll(lambda: ax_enabled(helper, pid, "AXButton", "Combine", deadline=retry_ready), bool, deadline=retry_ready, description="corrected-passphrase Combine readiness")
        ax_call(helper, pid, "press", "AXButton", "Combine", deadline=time.monotonic() + 5)
        protected = wait_value(helper, pid, "AXTextArea", "Recovered secret", deadline=time.monotonic() + 40)
        if protected != b"protected synthetic":
            raise RuntimeError("protected retry mismatch")
        results.append({"case": "words-protected-wrong-passphrase-retry", "status": "passed", "secret_sha256": sha(protected), "failure_status_observed": True, "failure_clipboard_unchanged": True})

        ax_call(helper, pid, "press", "AXRadioButton", "Split", deadline=time.monotonic() + 5)
        ax_call(helper, pid, "press", "AXButton", "Start over", deadline=time.monotonic() + 5)
        dialog_secret = "synthetic cancellation state"
        ax_call(helper, pid, "type", "AXTextArea", "Secret", value=dialog_secret, deadline=time.monotonic() + 5)
        dialog_ready = time.monotonic() + 10
        poll(lambda: ax_enabled(helper, pid, "AXButton", "Split", deadline=dialog_ready), bool, deadline=dialog_ready, description="pre-dialog Split readiness")
        def dialog_snapshot():
            return {"secret": ax_value(helper, pid, "AXTextArea", "Secret", deadline=time.monotonic() + 2), "split_enabled": ax_enabled(helper, pid, "AXButton", "Split", deadline=time.monotonic() + 2), "choose_enabled": ax_enabled(helper, pid, "AXButton", "Choose file…", deadline=time.monotonic() + 2)}
        verify_dialog_cancellation(
            dialog_snapshot,
            lambda: ax_exists(helper, pid, "AXWindow", "Choose Secret file", deadline=time.monotonic() + 2),
            lambda: ax_call(helper, pid, "press", "AXButton", "Choose file…", deadline=time.monotonic() + 5),
            lambda: ax_call(helper, pid, "press", "AXButton", "Cancel", deadline=time.monotonic() + 5),
            lambda: ax_call(helper, pid, "press", "AXRadioButton", "Split", deadline=time.monotonic() + 5),
            deadline=time.monotonic() + 10)
        results.append({"case": "cancel-open-dialog", "status": "passed", "dialog_observed": True, "state_preserved": True, "controls_responsive": True})
    finally:
        cleanup_error = None
        try:
            cleanup = SMOKE.cleanup_group(process.pid, evidence / "cleanup.json")
        except BaseException as error:
            cleanup_error = error
        process.wait(timeout=5)
        stdout.close()
        stderr.close()
        if cleanup_error:
            raise cleanup_error

    SMOKE.verify_manifest(evidence, manifest)
    SMOKE.verify_manifest(package, manifest)
    if sha((package / "manifest.json").read_bytes()) != manifest_digest:
        raise RuntimeError("package manifest changed during workflow")
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)], check=True, capture_output=True)
    paths = SMOKE.loaded_paths((evidence / "loaded-images.log").read_text(errors="replace").splitlines())
    external = [path for path in paths if ("/opt/homebrew" in path or "/Qt" in path) and not SMOKE.contained(path, app)]
    if external:
        raise RuntimeError("external Qt/Homebrew image loaded")
    report = {
        "scope": "packaged production UI through external macOS Accessibility; synthetic inputs only",
        "source_commit": head,
        "manifest_sha256_before_after": manifest_digest,
        "relocated_codesign_deep_strict_pre_post": True,
        "cases": results,
        "loaded_images": len(paths),
        "external_qt_or_homebrew": 0,
        "cleanup": cleanup,
        "clipboard": "owned synthetic sentinels established before reads; final content is synthetic",
        "limitations": ["Words encoding only", "text workflows only; arbitrary binary file Save remains pending", "no maximum workload or network/storage qualification"],
    }
    (evidence / "evidence.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(evidence / "evidence.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
