#!/usr/bin/env python3
"""Drive bounded synthetic workflows through the relocated production macOS app."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid
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
TARGET_EXECUTABLES: dict[int, str] = {}
CURRENT_ATTEMPT: Path | None = None


class ElementNotFound(RuntimeError):
    pass


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_text(*args: str, timeout: float = 15, cwd: Path | None = None) -> str:
    return subprocess.run(args, check=True, text=True, capture_output=True, timeout=timeout, cwd=cwd).stdout.strip()


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
    expected_executable = TARGET_EXECUTABLES.get(pid)
    if expected_executable is not None:
        identity = subprocess.run(["/bin/ps", "-p", str(pid), "-o", "command="], capture_output=True,
                                  timeout=remaining(deadline)).stdout.decode().strip()
        if identity != expected_executable:
            raise RuntimeError("target PID identity changed or app exited")
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
    if result.returncode in (8, 9, 10, 11):
        diagnostic = result.stderr.decode(errors="replace").strip()
        raise RuntimeError(f"AX helper diagnostic exit {result.returncode}: {diagnostic}")
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


def navigate_dialog_rows(components: list[str], select: Callable[[str], None],
                         submit: Callable[[], None], visible: Callable[[str], bool], *, deadline: float) -> None:
    if not components:
        raise ValueError("dialog path needs at least one component")
    for index, component in enumerate(components):
        poll(lambda component=component: visible(component), bool, deadline=deadline,
             description=f"native dialog row {index + 1}")
        select(component)
        submit()


def verify_file_result(read: Callable[[], bytes | None], expected: bytes, *, canceled: bool,
                       deadline: float) -> None:
    if canceled:
        if read() is not None:
            raise RuntimeError("canceled Save created a destination")
        return
    value = poll(lambda: read(), lambda result: result is not None, deadline=deadline,
                 description="saved file creation")
    if value != expected:
        raise RuntimeError("saved file bytes mismatch")


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


def prepare(repo: Path, evidence_root: Path, package: Path, source_commit: str,
            manifest_digest: str, attempt_id: str | None = None) -> Path:
    target = (repo / "target").resolve()
    evidence_root.relative_to(target)
    if evidence_root == target or SMOKE.overlaps(evidence_root, package):
        raise RuntimeError("unsafe workflow evidence path")
    if evidence_root.is_symlink():
        raise RuntimeError("refusing symlink workflow evidence root")
    marker = evidence_root / MARKER
    if evidence_root.exists():
        if not marker.is_file() or marker.read_text() != "owned workflow evidence\n":
            raise RuntimeError("refusing unowned workflow evidence")
    else:
        evidence_root.mkdir(parents=True)
        marker.write_text("owned workflow evidence\n")
    attempts = evidence_root / "attempts"
    if attempts.is_symlink():
        raise RuntimeError("refusing symlink attempts directory")
    attempts.mkdir(exist_ok=True)
    identifier = attempt_id or f"{source_commit[:12]}-{manifest_digest[:12]}-{uuid.uuid4().hex[:12]}"
    attempt = attempts / identifier
    attempt.mkdir()
    (attempt / MARKER).write_text("owned workflow evidence\n")
    metadata = {
        "attempt_id": identifier,
        "source_commit": source_commit,
        "manifest_sha256": manifest_digest,
        "package_dir": str(package),
        "started_unix_ns": time.time_ns(),
    }
    (attempt / "attempt.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    return attempt


def record_failed_attempt(attempt: Path, error: BaseException) -> None:
    result = {"status": "failed", "error_type": type(error).__name__}
    cleanup = attempt / "cleanup.json"
    if cleanup.is_file():
        result["cleanup"] = json.loads(cleanup.read_text())
    (attempt / "result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    global CURRENT_ATTEMPT
    repo, package = args.repo_root.resolve(), args.package_dir.resolve()
    evidence_root = args.evidence_dir.absolute()
    if run_text("/usr/bin/git", "status", "--porcelain", cwd=repo):
        raise SystemExit("dirty source tree")
    manifest_bytes = (package / "manifest.json").read_bytes()
    manifest_digest = sha(manifest_bytes)
    manifest = json.loads(manifest_bytes)
    head = run_text("/usr/bin/git", "rev-parse", "HEAD", cwd=repo)
    if manifest["source"] != {"commit": head, "tree": "clean"}:
        raise SystemExit("package source mismatch")
    SMOKE.verify_manifest(package, manifest)
    evidence = prepare(repo, evidence_root, package, head, manifest_digest)
    CURRENT_ATTEMPT = evidence
    app = evidence / "Safeparts.app"
    subprocess.run(["/usr/bin/ditto", str(package / "Safeparts.app"), str(app)], check=True)
    SMOKE.verify_manifest(evidence, manifest)
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)], check=True, capture_output=True)
    io_directory = evidence / "io"
    io_directory.mkdir()
    binary_fixture = bytes.fromhex("00fffe0d0a42696e00807f")
    fixture_path = io_directory / "fixture.bin"
    fixture_path.write_bytes(binary_fixture)
    helper = evidence / "ax-harness"
    helper_source = repo / "apps/desktop/tests/ax_harness.swift"
    subprocess.run(["/usr/bin/xcrun", "swiftc", str(helper_source), "-o", str(helper), "-framework", "ApplicationServices"], check=True)
    helper_metadata = {
        "source_sha256": sha(helper_source.read_bytes()),
        "binary_sha256": sha(helper.read_bytes()),
        "permission_identity": "runtime process authorization; helper filename is not treated as identity",
    }
    (evidence / "helper.json").write_text(json.dumps(helper_metadata, indent=2, sort_keys=True) + "\n")
    env = {key: value for key, value in os.environ.items() if not key.startswith(("DYLD_", "QT_"))}
    env["DYLD_PRINT_LIBRARIES"] = "1"
    stdout = (evidence / "stdout.log").open("wb")
    stderr = (evidence / "loaded-images.log").open("wb")
    executable = str(app / "Contents/MacOS/Safeparts")
    process = subprocess.Popen([executable], env=env, stdout=stdout, stderr=stderr, start_new_session=True)
    TARGET_EXECUTABLES[process.pid] = executable
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
            inspected = time.monotonic() + 20
            count_status = f"{index} of 2 Recovery shares entered"
            poll(lambda: ax_exists(helper, pid, "AXStaticText", count_status, deadline=inspected), bool,
                 deadline=inspected, description=f"inspection of Recovery share {index}")
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
            inspected = time.monotonic() + 20
            count_status = f"{index} of 2 Recovery shares entered"
            poll(lambda: ax_exists(helper, pid, "AXStaticText", count_status, deadline=inspected), bool,
                 deadline=inspected, description=f"inspection of protected Recovery share {index}")
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
            return {
                "secret": ax_value(helper, pid, "AXTextArea", "Secret", deadline=time.monotonic() + 2),
                "split_mode": ax_value(helper, pid, "AXRadioButton", "Split", deadline=time.monotonic() + 2),
                "split_enabled": ax_enabled(helper, pid, "AXButton", "Split", deadline=time.monotonic() + 2),
                "choose_enabled": ax_enabled(helper, pid, "AXButton", "Choose file…", deadline=time.monotonic() + 2),
                "file_selected": ax_exists(helper, pid, "AXButton", "Use text instead", deadline=time.monotonic() + 2),
            }
        verify_dialog_cancellation(
            dialog_snapshot,
            lambda: ax_exists(helper, pid, "AXWindow", "Choose Secret file", deadline=time.monotonic() + 2),
            lambda: ax_call(helper, pid, "press", "AXButton", "Choose file…", deadline=time.monotonic() + 5),
            lambda: ax_call(helper, pid, "press", "AXButton", "Cancel", deadline=time.monotonic() + 5),
            lambda: ax_call(helper, pid, "press", "AXRadioButton", "Split", deadline=time.monotonic() + 5),
            deadline=time.monotonic() + 10)
        results.append({"case": "cancel-open-dialog", "status": "passed", "dialog_observed": True, "state_preserved": True, "controls_responsive": True})

        def wait_dialog(title: str) -> None:
            deadline = time.monotonic() + 10
            poll(lambda: ax_exists(helper, pid, "AXWindow", title, deadline=deadline), bool,
                 deadline=deadline, description=f"{title} appearance")

        def open_single_file(button: str, components: list[str]) -> None:
            ax_call(helper, pid, "press", "AXButton", button, deadline=time.monotonic() + 5)
            wait_dialog("Choose Secret file")
            deadline = time.monotonic() + 20
            path_components = [components[-1]] if ax_exists(helper, pid, "AXTextField", components[-1], deadline=deadline) else components
            navigate_dialog_rows(
                path_components,
                lambda name: ax_call(helper, pid, "dialog-selectrow", "AXWindow", "Choose Secret file", value=name, deadline=deadline),
                lambda: ax_call(helper, pid, "press", "AXButton", "Open", deadline=deadline),
                lambda name: ax_exists(helper, pid, "AXTextField", name, deadline=deadline),
                deadline=deadline)
            poll(lambda: ax_exists(helper, pid, "AXWindow", "Choose Secret file", deadline=deadline),
                 lambda value: not value, deadline=deadline, description="Secret file dialog dismissal")

        def save_result(button_occurrence: int, destination: Path, expected: bytes, *, cancel: bool = False) -> None:
            if destination.exists():
                raise RuntimeError("refusing existing workflow Save destination")
            ax_call(helper, pid, "press", "AXButton", "Save…", occurrence=button_occurrence, deadline=time.monotonic() + 5)
            wait_dialog("Save exact bytes")
            ax_call(helper, pid, "dialog-set-save-name", "AXWindow", "Save exact bytes", value=destination.name, deadline=time.monotonic() + 5)
            control = "Cancel" if cancel else "Save"
            ax_call(helper, pid, "press", "AXButton", control, deadline=time.monotonic() + 5)
            deadline = time.monotonic() + 15
            poll(lambda: ax_exists(helper, pid, "AXWindow", "Save exact bytes", deadline=deadline),
                 lambda value: not value, deadline=deadline, description="Save dialog dismissal")
            verify_file_result(lambda: destination.read_bytes() if destination.exists() else None,
                               expected, canceled=cancel, deadline=deadline)

        def load_share_files(names: list[str]) -> None:
            ax_call(helper, pid, "press", "AXButton", "Load share files…", deadline=time.monotonic() + 5)
            wait_dialog("Load Recovery share files")
            deadline = time.monotonic() + 20
            for name in names:
                poll(lambda name=name: ax_exists(helper, pid, "AXTextField", name, deadline=deadline), bool,
                     deadline=deadline, description=f"share file row {name}")
            ax_call(helper, pid, "dialog-selectrows", "AXWindow", "Load Recovery share files", value="\n".join(names), deadline=deadline)
            ax_call(helper, pid, "press", "AXButton", "Open", deadline=deadline)
            poll(lambda: ax_exists(helper, pid, "AXWindow", "Load Recovery share files", deadline=deadline),
                 lambda value: not value, deadline=deadline, description="share file dialog dismissal")

        def binary_round_trip(protected: bool) -> None:
            ax_call(helper, pid, "press", "AXRadioButton", "Split", deadline=time.monotonic() + 5)
            ax_call(helper, pid, "press", "AXButton", "Start over", deadline=time.monotonic() + 5)
            open_single_file("Choose file…", ["target", evidence.name, "io", fixture_path.name])
            metadata = f"{fixture_path.name} · {len(binary_fixture)} bytes"
            if not ax_exists(helper, pid, "AXStaticText", metadata, deadline=time.monotonic() + 5):
                raise RuntimeError("selected binary fixture metadata was not shown")
            if protected:
                ax_call(helper, pid, "press", "AXCheckBox", "Protect with passphrase", deadline=time.monotonic() + 5)
                ax_call(helper, pid, "type", "AXTextArea", "Passphrase input (contents hidden)", value="binary-synthetic-pass", deadline=time.monotonic() + 5)
                ax_call(helper, pid, "type", "AXTextArea", "Passphrase confirmation (contents hidden)", value="binary-synthetic-pass", deadline=time.monotonic() + 5)
            split_ready = time.monotonic() + 10
            poll(lambda: ax_enabled(helper, pid, "AXButton", "Split", deadline=split_ready), bool,
                 deadline=split_ready, description="binary Split readiness")
            ax_call(helper, pid, "press", "AXButton", "Split", deadline=time.monotonic() + 5)
            binary_shares = [wait_value(helper, pid, "AXTextArea", f"Recovery share {index}", deadline=time.monotonic() + 30) for index in (1, 2)]
            prefix = "protected-" if protected else "unprotected-"
            share_paths = [io_directory / f"{prefix}share-{index}.txt" for index in (1, 2)]
            save_result(0, share_paths[0], binary_shares[0], cancel=True)
            save_result(0, share_paths[0], binary_shares[0])
            save_result(1, share_paths[1], binary_shares[1])
            ax_call(helper, pid, "press", "AXRadioButton", "Combine", deadline=time.monotonic() + 5)
            load_share_files([path.name for path in share_paths])
            entered = time.monotonic() + 30
            poll(lambda: ax_exists(helper, pid, "AXStaticText", "2 of 2 Recovery shares entered", deadline=entered), bool,
                 deadline=entered, description="loaded Recovery shares inspection")
            if protected:
                passphrase_ready = time.monotonic() + 20
                poll(lambda: ax_exists(helper, pid, "AXTextArea", "Passphrase input (contents hidden)", deadline=passphrase_ready), bool,
                     deadline=passphrase_ready, description="binary recovery passphrase editor")
                ax_call(helper, pid, "type", "AXTextArea", "Passphrase input (contents hidden)", value="binary-synthetic-pass", deadline=time.monotonic() + 5)
            combine_ready = time.monotonic() + 20
            poll(lambda: ax_enabled(helper, pid, "AXButton", "Combine", deadline=combine_ready), bool,
                 deadline=combine_ready, description="binary Combine readiness")
            ax_call(helper, pid, "press", "AXButton", "Combine", deadline=time.monotonic() + 5)
            binary_status = "Recovered binary Secret. Save exact bytes."
            recovered_ready = time.monotonic() + 40
            poll(lambda: ax_exists(helper, pid, "AXStaticText", binary_status, deadline=recovered_ready), bool,
                 deadline=recovered_ready, description="binary recovery status")
            recovered_path = io_directory / f"{prefix}recovered.bin"
            save_result(0, recovered_path, binary_fixture)
            recovered = recovered_path.read_bytes()
            if recovered != binary_fixture:
                raise RuntimeError("recovered binary file bytes mismatch")
            results.append({
                "case": f"words-binary-{'protected' if protected else 'unprotected'}-file-round-trip",
                "status": "passed",
                "encoding": "Words",
                "fixture_sha256": sha(binary_fixture),
                "fixture_size": len(binary_fixture),
                "share_files_verified": 2,
                "save_cancel_retry": True,
                "recovered_sha256": sha(recovered),
                "recovered_size": len(recovered),
            })

        binary_round_trip(False)
        binary_round_trip(True)
    finally:
        cleanup_error = None
        try:
            cleanup = SMOKE.cleanup_group(process.pid, evidence / "cleanup.json")
        except BaseException as error:
            cleanup_error = error
        process.wait(timeout=5)
        TARGET_EXECUTABLES.pop(process.pid, None)
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
        "limitations": (["Base64url, Base58Check, and BIP-39 packaged selection not executed"]
                        + ([] if any(str(case.get("case", "")).startswith("words-binary-") for case in results)
                           else ["arbitrary binary file Save not executed successfully"])
                        + ["no maximum workload or network/storage qualification"]),
    }
    (evidence / "evidence.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    (evidence / "result.json").write_text(json.dumps({"status": "passed", "cases": [case["case"] for case in results]}, indent=2, sort_keys=True) + "\n")
    latest = evidence_root / "latest-success.json"
    temporary_latest = evidence_root / ".latest-success.json.tmp"
    temporary_latest.write_text(json.dumps({"attempt": evidence.name, "source_commit": head, "manifest_sha256": manifest_digest}, indent=2, sort_keys=True) + "\n")
    temporary_latest.replace(latest)
    CURRENT_ATTEMPT = None
    print(evidence / "evidence.json")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BaseException as error:
        if CURRENT_ATTEMPT is not None:
            record_failed_attempt(CURRENT_ATTEMPT, error)
        raise
