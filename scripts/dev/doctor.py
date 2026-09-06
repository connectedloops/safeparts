#!/usr/bin/env python3
"""Read-only local environment diagnostics for Safeparts developers."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def ok(self, message: str) -> None:
        print(f"ok: {message}")

    def warn(self, message: str) -> None:
        self.warnings.append(message)
        print(f"warn: {message}")

    def error(self, message: str) -> None:
        self.errors.append(message)
        print(f"error: {message}")


def run(args: list[str], cwd: Path = REPO_ROOT) -> tuple[int, str]:
    try:
        proc = subprocess.run(
            args,
            cwd=cwd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except OSError as exc:
        return 127, str(exc)
    return proc.returncode, proc.stdout.strip()


def check_command(
    report: Report,
    name: str,
    version_args: list[str],
    expected: str | None = None,
) -> bool:
    path = shutil.which(name)
    if path is None:
        report.error(f"missing `{name}` on PATH")
        return False

    code, out = run(version_args)
    if code == 0 and out:
        first_line = out.splitlines()[0]
        if expected and expected not in first_line:
            report.warn(f"{name}: {first_line} (expected {expected}; run `mise install` or the documented build command)")
        else:
            report.ok(f"{name}: {first_line}")
    else:
        report.warn(f"found `{name}` at {path}, but version check failed")
    return True


def check_rust(report: Report) -> None:
    has_rustup = check_command(report, "rustup", ["rustup", "--version"])
    check_command(report, "rustc", ["rustc", "--version"], expected="1.93.0")
    check_command(report, "cargo", ["cargo", "--version"], expected="1.93.0")
    check_command(report, "rustfmt", ["rustfmt", "--version"])

    if shutil.which("cargo") is not None:
        code, out = run(["cargo", "clippy", "--version"])
        if code == 0:
            report.ok(f"clippy: {out.splitlines()[0] if out else 'installed'}")
        else:
            report.error("missing clippy; run `rustup component add clippy`")

    if has_rustup:
        code, out = run(["rustup", "target", "list", "--installed"])
        if code == 0 and "wasm32-unknown-unknown" in out.splitlines():
            report.ok("Rust target wasm32-unknown-unknown installed")
        else:
            report.error("missing Rust target wasm32-unknown-unknown; run `rustup target add wasm32-unknown-unknown`")


def check_js(report: Report) -> None:
    check_command(report, "bun", ["bun", "--version"], expected="1.3.11")

    expected = [
        REPO_ROOT / "web" / "bun.lock",
        REPO_ROOT / "web" / "help" / "bun.lock",
    ]
    for path in expected:
        if path.exists():
            report.ok(f"lockfile present: {path.relative_to(REPO_ROOT)}")
        else:
            report.error(f"missing Bun lockfile: {path.relative_to(REPO_ROOT)}")

    ambiguous = [
        REPO_ROOT / "web" / "package-lock.json",
        REPO_ROOT / "web" / "pnpm-lock.yaml",
        REPO_ROOT / "web" / "yarn.lock",
        REPO_ROOT / "web" / "help" / "package-lock.json",
        REPO_ROOT / "web" / "help" / "pnpm-lock.yaml",
        REPO_ROOT / "web" / "help" / "yarn.lock",
    ]
    for path in ambiguous:
        if path.exists():
            report.error(f"unexpected non-Bun lockfile: {path.relative_to(REPO_ROOT)}")


def check_wasm_tools(report: Report) -> None:
    check_command(report, "wasm-pack", ["wasm-pack", "--version"], expected="0.15.0")
    check_command(report, "wasm-bindgen", ["wasm-bindgen", "--version"], expected="0.2.108")

    wasm_pkg = REPO_ROOT / "web" / "src" / "wasm_pkg" / "safeparts_wasm.js"
    if wasm_pkg.exists():
        report.ok("generated WASM package present")
    else:
        report.warn("generated WASM package missing; run `cd web && bun run build:wasm`")


def main() -> int:
    os.chdir(REPO_ROOT)
    report = Report()
    print(f"Safeparts doctor: {REPO_ROOT}")

    check_rust(report)
    check_js(report)
    check_wasm_tools(report)

    print()
    if report.errors:
        print(f"doctor failed with {len(report.errors)} error(s) and {len(report.warnings)} warning(s)")
        print("Try: `mise install && mise run setup`")
        return 1

    print(f"doctor passed with {len(report.warnings)} warning(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
