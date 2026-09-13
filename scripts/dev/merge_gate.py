#!/usr/bin/env python3
"""Evaluate the stable PR merge gate from public check-run results."""
from __future__ import annotations

import argparse
import fnmatch
import json
import subprocess
import sys
from collections.abc import Iterable
from pathlib import Path

BASE_REQUIRED = ("workflow policy and actionlint", "audit")
RUST_REQUIRED = ("rust", "terminal behavior (windows)", "terminal behavior (macos-native)")
WEB_REQUIRED = ("container smoke", "build, test, and package Web artifact")

RUST_PATHS = (
    "Cargo.toml",
    "Cargo.lock",
    "crates/safeparts/**",
    "crates/safeparts_core/**",
    "crates/safeparts_tui/**",
    "crates/safeparts_wasm/**",
    "scripts/dev/**",
    "scripts/release/**",
    "mise.toml",
    "web/package.json",
    "web/scripts/test-wasm.sh",
    ".github/**",
)
WEB_PATHS = (
    "web/**",
    "crates/safeparts_core/**",
    "crates/safeparts_wasm/**",
    "Cargo.toml",
    "Cargo.lock",
    ".dockerignore",
    "scripts/dev/test_retirement.py",
    "scripts/dev/test_web_deploy.py",
    "scripts/dev/*changelog*.py",
    ".github/workflows/changelog.yml",
    "mise.toml",
    "netlify.toml",
    "wrangler.jsonc",
    ".github/workflows/cloudflare-workers.yml",
    ".github/workflows/web-ci.yml",
)


def matches_any(path: str, patterns: Iterable[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def applicable_checks(paths: list[str]) -> list[str]:
    required = list(BASE_REQUIRED)
    if any(matches_any(path, RUST_PATHS) for path in paths):
        required.extend(RUST_REQUIRED)
    if any(matches_any(path, WEB_PATHS) for path in paths):
        required.extend(WEB_REQUIRED)
    return required


def check_runs_from_payload(payload: object) -> list[dict[str, object]]:
    if isinstance(payload, dict) and isinstance(payload.get("check_runs"), list):
        return [run for run in payload["check_runs"] if isinstance(run, dict)]
    if isinstance(payload, list):
        runs: list[dict[str, object]] = []
        for page in payload:
            if isinstance(page, dict) and isinstance(page.get("check_runs"), list):
                runs.extend(run for run in page["check_runs"] if isinstance(run, dict))
        return runs
    raise ValueError("checks JSON must contain check_runs or paginated check_runs")


def normalized_name(name: object) -> str:
    text = str(name or "").strip()
    if " / " in text:
        return text.rsplit(" / ", 1)[1].strip()
    return text


def latest_runs(runs: Iterable[dict[str, object]]) -> dict[str, dict[str, object]]:
    latest: dict[str, dict[str, object]] = {}
    for run in runs:
        name = normalized_name(run.get("name"))
        if not name:
            continue
        started_at = str(run.get("started_at") or run.get("completed_at") or "")
        current = latest.get(name)
        current_started_at = str(current.get("started_at") or current.get("completed_at") or "") if current else ""
        if current is None or started_at >= current_started_at:
            latest[name] = run
    return latest


def evaluate(paths: list[str], runs: list[dict[str, object]]) -> list[str]:
    latest = latest_runs(runs)
    failures: list[str] = []
    for name in applicable_checks(paths):
        run = latest.get(name)
        if run is None:
            failures.append(f"{name}: missing")
            continue
        status = str(run.get("status") or "")
        conclusion = str(run.get("conclusion") or "")
        if status != "completed" or conclusion != "success":
            failures.append(f"{name}: {status or 'unknown'}/{conclusion or 'pending'}")
    return failures


def read_paths(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def read_checks(path: Path) -> list[dict[str, object]]:
    return check_runs_from_payload(json.loads(path.read_text(encoding="utf-8")))


def fetch_github_checks(repository: str, sha: str, output: Path) -> None:
    with output.open("w", encoding="utf-8") as file:
        result = subprocess.run(
            ["gh", "api", f"/repos/{repository}/commits/{sha}/check-runs?per_page=100", "--paginate", "--slurp"],
            text=True,
            stdout=file,
            stderr=subprocess.PIPE,
            check=False,
        )
    if result.returncode != 0:
        raise RuntimeError(f"gh check-run query failed: {result.stderr.strip()}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--changed-files", required=True, type=Path)
    parser.add_argument("--checks-json", type=Path)
    parser.add_argument("--fetch-checks-for", metavar="REPOSITORY@SHA")
    args = parser.parse_args()

    checks_json = args.checks_json
    if args.fetch_checks_for:
        if checks_json is None:
            parser.error("--fetch-checks-for requires --checks-json output path")
        repository, _, sha = args.fetch_checks_for.partition("@")
        if not repository or not sha:
            parser.error("--fetch-checks-for must be REPOSITORY@SHA")
        fetch_github_checks(repository, sha, checks_json)

    if checks_json is None:
        parser.error("--checks-json is required unless fetching to a path")

    failures = evaluate(read_paths(args.changed_files), read_checks(checks_json))
    if failures:
        print("merge gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print("merge gate passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
