#!/usr/bin/env python3
"""Validate immutable inputs and least-privilege rules for release CI."""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "release.yml"
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
VERSION_COMMENT_PATTERN = re.compile(
    r"^(?:v\d+\.\d+\.\d+|stable \(\d{4}-\d{2}-\d{2}\))$"
)
REQUIRED_RUST_JOBS = {"test", "build"}
RETIRED_RELEASE_PATTERN = re.compile(
    r"desktop|native[-_](?:macos|windows)|safeparts_uniffi|tauri|"
    r"(?:macos|windows)/|setup-dotnet|setup-xcode|"
    r"\.(?:appimage|deb|rpm|dmg|msi|exe)\b",
    re.IGNORECASE,
)
REQUIRED_PUBLISH_CONDITION = (
    "github.event_name == 'push' && startsWith(github.ref, 'refs/tags/')"
)


@dataclass(frozen=True)
class ActionUse:
    identifier: str
    reference: str
    comment: str | None
    line_number: int
    job: str | None
    inputs: dict[str, str]


def _unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
        return value[1:-1]
    return value


def _job_at_line(job_starts: list[tuple[int, str]], line_number: int) -> str | None:
    current = None
    for start, job in job_starts:
        if start > line_number:
            break
        current = job
    return current


def _inline_permissions(value: str) -> dict[str, str]:
    value = _unquote(value)
    if value == "write-all":
        return {"*": "write"}
    if value == "read-all":
        return {"*": "read"}
    if value.startswith("{") and value.endswith("}"):
        permissions: dict[str, str] = {}
        for entry in value[1:-1].split(","):
            if not entry.strip():
                continue
            match = re.fullmatch(
                r"\s*([A-Za-z0-9_-]+)\s*:\s*([A-Za-z]+)\s*", entry
            )
            if match:
                permissions[match.group(1)] = match.group(2)
        return permissions
    return {}


def _step_inputs(lines: list[str], action_index: int, action_indent: int) -> dict[str, str]:
    inputs: dict[str, str] = {}
    in_with = False
    for line in lines[action_index + 1 :]:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip())
        if re.match(r"^\s*-\s+", line) and indent <= action_indent:
            break
        if indent < action_indent:
            break
        if line.strip() == "with:":
            in_with = True
            continue
        if not in_with:
            continue
        if indent <= action_indent:
            break
        match = re.match(r"^\s+([A-Za-z0-9_-]+):\s*([^#]*?)\s*(?:#.*)?$", line)
        if match:
            inputs[match.group(1)] = _unquote(match.group(2))
    return inputs


def parse_workflow(
    text: str,
) -> tuple[
    list[str],
    list[ActionUse],
    dict[str, dict[str, str]],
    dict[str, str],
]:
    """Parse the policy-relevant subset of a GitHub Actions workflow."""
    lines = text.splitlines()
    job_starts = [
        (index + 1, match.group(1))
        for index, line in enumerate(lines)
        if (match := re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line))
        and any(previous.strip() == "jobs:" for previous in lines[:index])
    ]

    actions: list[ActionUse] = []
    uses_pattern = re.compile(r"^(\s*)(?:-\s+)?uses:\s*(.*?)\s*$")
    for index, line in enumerate(lines):
        match = uses_pattern.match(line)
        if not match:
            continue
        indent = len(match.group(1))
        scalar = match.group(2)
        comment_match = re.match(r"^(.*?)\s+#\s*(.*?)$", scalar)
        if comment_match:
            scalar = comment_match.group(1)
            comment = comment_match.group(2)
        else:
            comment = None
        action_value = _unquote(scalar)
        if action_value.startswith("./"):
            identifier, reference = action_value, ""
        elif "@" in action_value:
            identifier, reference = action_value.rsplit("@", 1)
        else:
            identifier, reference = action_value, ""
        actions.append(
            ActionUse(
                identifier=identifier,
                reference=reference,
                comment=comment,
                line_number=index + 1,
                job=_job_at_line(job_starts, index + 1),
                inputs=_step_inputs(lines, index, indent),
            )
        )

    permissions: dict[str, dict[str, str]] = {"workflow": {}}
    job_conditions: dict[str, str] = {}
    current_job: str | None = None
    in_jobs = False
    permission_scope: str | None = None
    permission_indent = -1
    for line in lines:
        if line.strip() == "jobs:" and not line.startswith(" "):
            in_jobs = True
            current_job = None
        job_match = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
        if in_jobs and job_match:
            current_job = job_match.group(1)
        condition_match = re.match(r"^    if:\s*(.*?)\s*$", line)
        if current_job is not None and condition_match:
            job_conditions[current_job] = _unquote(condition_match.group(1))
        indent = len(line) - len(line.lstrip())
        permission_match = re.match(
            r"^\s*permissions:\s*([^#]*?)\s*(?:#.*)?$", line
        )
        if permission_match:
            permission_scope = current_job if indent == 4 else "workflow"
            permission_indent = indent
            scalar = permission_match.group(1)
            if scalar:
                permissions[permission_scope] = _inline_permissions(scalar)
                permission_scope = None
            else:
                permissions.setdefault(permission_scope, {})
            continue
        if permission_scope is None or not line.strip() or line.lstrip().startswith("#"):
            continue
        if indent <= permission_indent:
            permission_scope = None
            continue
        permission = re.match(r"^\s+([A-Za-z0-9_-]+):\s*([A-Za-z]+)\s*$", line)
        if permission:
            permissions[permission_scope][permission.group(1)] = permission.group(2)

    return lines, actions, permissions, job_conditions


def repository_versions(repo_root: Path) -> tuple[str, str]:
    mise = tomllib.loads((repo_root / "mise.toml").read_text(encoding="utf-8"))
    rust_config = mise["tools"]["rust"]
    rust_version = rust_config["version"] if isinstance(rust_config, dict) else rust_config
    bun_version = mise["tools"]["bun"]
    return str(rust_version), str(bun_version)


def validate_workflow(text: str, repo_root: Path = REPO_ROOT) -> list[str]:
    lines, actions, permissions, job_conditions = parse_workflow(text)
    rust_version, bun_version = repository_versions(repo_root)
    errors: list[str] = []

    for action in actions:
        location = f"line {action.line_number}"
        if action.identifier.startswith("./"):
            continue
        if not SHA_PATTERN.fullmatch(action.reference):
            action_reference = (
                f"{action.identifier}@{action.reference}"
                if action.reference
                else action.identifier
            )
            errors.append(f"{location}: mutable action reference {action_reference}")
        if action.comment is None or not VERSION_COMMENT_PATTERN.fullmatch(action.comment):
            errors.append(
                f"{location}: action pin is missing a version comment for {action.identifier}"
            )

        expected_input: tuple[str, str, str] | None = None
        if action.identifier == "dtolnay/rust-toolchain":
            expected_input = (
                "toolchain",
                rust_version,
                f"Rust toolchain must match mise.toml ({rust_version})",
            )
        elif action.identifier == "oven-sh/setup-bun":
            expected_input = (
                "bun-version",
                bun_version,
                f"Bun version must match mise.toml ({bun_version})",
            )
        if expected_input is not None:
            key, expected, message = expected_input
            if action.inputs.get(key) != expected:
                errors.append(f"{location}: {message}")

    actions_by_job: dict[str, list[ActionUse]] = {}
    for action in actions:
        if action.job is not None:
            actions_by_job.setdefault(action.job, []).append(action)
    for job in sorted(REQUIRED_RUST_JOBS):
        if not any(
            action.identifier == "dtolnay/rust-toolchain"
            for action in actions_by_job.get(job, [])
        ):
            errors.append(f"job {job} must install the repository Rust toolchain")

    for line_number, line in enumerate(lines, 1):
        if not line.lstrip().startswith("#") and RETIRED_RELEASE_PATTERN.search(line):
            errors.append(f"line {line_number}: retired release workload or artifact")
        moving_runner = re.search(r"\b(?:ubuntu|windows|macos)-latest\b", line)
        if moving_runner:
            errors.append(
                f"line {line_number}: moving runner label {moving_runner.group(0)}"
            )

    if permissions.get("workflow") != {"contents": "read"}:
        errors.append("workflow permissions must be exactly contents: read")
    if permissions.get("publish") != {"contents": "write"}:
        errors.append("publish permissions must be exactly contents: write")
    if job_conditions.get("publish") != REQUIRED_PUBLISH_CONDITION:
        errors.append("publish job must be restricted to tag pushes")
    for scope, grants in permissions.items():
        if scope in {"workflow", "publish"}:
            continue
        for permission, access in grants.items():
            if access == "write":
                grant = "write-all" if permission == "*" else f"{permission}: write"
                errors.append(f"job {scope} has unexpected write permission {grant}")

    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check release workflow action, toolchain, and permission policy"
    )
    parser.add_argument(
        "workflow",
        nargs="?",
        type=Path,
        default=DEFAULT_WORKFLOW,
        help="workflow to check (default: .github/workflows/release.yml)",
    )
    args = parser.parse_args(argv)

    try:
        text = args.workflow.read_text(encoding="utf-8")
        errors = validate_workflow(text)
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(f"error: could not check workflow policy: {error}", file=sys.stderr)
        return 2

    if errors:
        print(f"workflow policy failed for {args.workflow}:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(f"workflow policy passed: {args.workflow}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
