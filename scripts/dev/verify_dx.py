#!/usr/bin/env python3
"""Repository DX consistency checks."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

REQUIRED_FEATURE_MATRIX_COLUMNS = [
    "Feature",
    "Core",
    "CLI",
    "TUI",
    "WASM",
    "Web",
    "Help docs",
    "Tests",
    "Update when changed",
]

NON_BUN_LOCKS = [
    "web/package-lock.json",
    "web/pnpm-lock.yaml",
    "web/yarn.lock",
    "web/help/package-lock.json",
    "web/help/pnpm-lock.yaml",
    "web/help/yarn.lock",
]

GENERATED_PREFIXES = [
    "web/src/wasm_pkg/",
    "web/dist/",
    "web/help/dist/",
    "web/help/.astro/",
    "target/",
    "dist/",
]


class CheckResult:
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


def run(args: list[str]) -> tuple[int, str]:
    proc = subprocess.run(
        args,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return proc.returncode, proc.stdout.strip()


def is_git_ignored(path: Path) -> bool:
    rel = path.relative_to(REPO_ROOT).as_posix()
    code, _ = run(["git", "check-ignore", "-q", rel])
    return code == 0


def list_agents_files() -> list[Path]:
    return sorted(
        (path for path in REPO_ROOT.rglob("AGENTS.md") if not is_git_ignored(path)),
        key=lambda p: p.as_posix(),
    )


def child_index_lines(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    out: list[str] = []
    in_section = False
    for line in lines:
        if line.strip() == "## Child DOX Index":
            in_section = True
            continue
        if in_section and line.startswith("## "):
            break
        if in_section:
            out.append(line)
    return out


def extract_index_paths(lines: list[str]) -> list[str]:
    paths: list[str] = []
    for line in lines:
        stripped = line.strip()
        if not stripped.startswith("-"):
            continue
        match = re.search(r"`([^`]+)`", stripped)
        if not match:
            continue
        candidate = match.group(1).strip()
        if candidate and not candidate.startswith("http"):
            paths.append(candidate)
    return paths


def check_agents_indexes(result: CheckResult) -> None:
    agents_files = list_agents_files()
    if not agents_files:
        result.error("no AGENTS.md files found")
        return

    for agents in agents_files:
        rel_agents = agents.relative_to(REPO_ROOT)
        lines = child_index_lines(agents)
        if not lines:
            result.error(f"{rel_agents} missing Child DOX Index section")
            continue

        for child in extract_index_paths(lines):
            resolved = (agents.parent / child).resolve()
            try:
                resolved.relative_to(REPO_ROOT)
            except ValueError:
                result.error(f"{rel_agents} child path escapes repo: {child}")
                continue
            if not resolved.exists():
                result.error(f"{rel_agents} child path does not exist: {child}")

    result.ok(f"checked {len(agents_files)} AGENTS.md files")


def check_feature_matrix(result: CheckResult) -> None:
    path = REPO_ROOT / "docs" / "dev" / "feature-matrix.md"
    if not path.exists():
        result.error("missing docs/dev/feature-matrix.md")
        return

    text = path.read_text(encoding="utf-8")
    header_line = next((line for line in text.splitlines() if line.startswith("| Feature |")), "")
    for column in REQUIRED_FEATURE_MATRIX_COLUMNS:
        if f"| {column} " not in header_line and not header_line.endswith(f"| {column} |"):
            result.error(f"feature matrix missing column: {column}")
    result.ok("feature matrix columns checked")


def check_surface_guides(result: CheckResult) -> None:
    required = [
        "core.md",
        "cli.md",
        "tui.md",
        "wasm.md",
        "web.md",
        "help-docs.md",
        "release.md",
    ]
    base = REPO_ROOT / "docs" / "dev" / "surfaces"
    for name in required:
        path = base / name
        if path.exists():
            result.ok(f"surface guide present: docs/dev/surfaces/{name}")
        else:
            result.error(f"missing surface guide: docs/dev/surfaces/{name}")


def check_developer_manuals(result: CheckResult) -> None:
    required = [
        "README.md",
        "rust-library.md",
        "cli-automation.md",
    ]
    base = REPO_ROOT / "docs" / "dev" / "manuals"
    for name in required:
        path = base / name
        if path.exists():
            result.ok(f"developer manual present: docs/dev/manuals/{name}")
        else:
            result.error(f"missing developer manual: docs/dev/manuals/{name}")


def check_lockfiles(result: CheckResult) -> None:
    for rel in ["web/bun.lock", "web/help/bun.lock"]:
        if (REPO_ROOT / rel).exists():
            result.ok(f"Bun lockfile present: {rel}")
        else:
            result.error(f"missing Bun lockfile: {rel}")

    for rel in NON_BUN_LOCKS:
        if (REPO_ROOT / rel).exists():
            result.error(f"unexpected non-Bun lockfile: {rel}")


def check_generated_status(result: CheckResult) -> None:
    code, out = run(["git", "status", "--short"])
    if code != 0:
        result.warn("could not read git status for generated artifact check")
        return

    for line in out.splitlines():
        if not line:
            continue
        rel = line[3:] if len(line) > 3 else ""
        if rel.startswith('"') and rel.endswith('"'):
            rel = rel[1:-1]
        if any(rel.startswith(prefix) for prefix in GENERATED_PREFIXES):
            result.warn(f"generated artifact present in working tree: {line}")


def check_web_deploy_policy(result: CheckResult) -> None:
    code, out = run([sys.executable, "scripts/dev/test_web_deploy.py"])
    if code == 0:
        result.ok("Web deployment artifact and workflow policy checked")
        return
    if out:
        for line in out.splitlines():
            result.error(f"Web deployment policy: {line}")
    else:
        result.error("Web deployment artifact and workflow policy check failed")


def main() -> int:
    os.chdir(REPO_ROOT)
    result = CheckResult()

    check_agents_indexes(result)
    check_feature_matrix(result)
    check_surface_guides(result)
    check_developer_manuals(result)
    check_lockfiles(result)
    check_generated_status(result)
    check_web_deploy_policy(result)

    print()
    if result.errors:
        print(f"dx verification failed with {len(result.errors)} error(s) and {len(result.warnings)} warning(s)")
        return 1

    print(f"dx verification passed with {len(result.warnings)} warning(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
