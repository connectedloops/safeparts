#!/usr/bin/env python3
"""Scan explicit dependency graphs; keep installed and generated files out of scope."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import quote

TRIVY_VERSION = "0.74.0"
BUN_VERSION = "1.3.11"
ACTIVE_CRATES = {"crates/safeparts_core", "crates/safeparts", "crates/safeparts_tui", "crates/safeparts_wasm"}
SUPPORTED = {"Cargo.lock": "cargo", "web/bun.lock": "bun", "web/help/bun.lock": "bun"}
RETIRED = {"desktop/bun.lock": "bun", "mobile/src-native/Cargo.lock": "cargo"}
RETIRED_GAPS = {
    "desktop/src-tauri": ["desktop/src-tauri/Cargo.toml"],
    "macos": ["macos/Package.swift"],
    "windows": [f"windows/{name}/{name}.csproj" for name in (
        "Safeparts.App", "Safeparts.AppModel", "Safeparts.Native", "Safeparts.AppModel.Tests",
        "Safeparts.InteropSmoke", "Safeparts.UiAutomation.Tests")],
    "crates/safeparts_uniffi": ["crates/safeparts_uniffi/Cargo.toml"],
}


def sha256(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def run(command: list[str], cwd: Path, report: dict[str, Any]) -> str:
    # Ambient Trivy filters, servers and config must not change the local gate.
    env = {key: value for key, value in os.environ.items() if not key.startswith("TRIVY_")}
    result = subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True, timeout=600)
    report["commands"].append({"argv": command, "exit_code": result.returncode, "stderr": result.stderr})
    if result.returncode != 0:
        raise ValueError(f"{command[0]} {command[1]} failed (exit {result.returncode}): {result.stderr.strip()}")
    return result.stdout


def timestamp(value: str) -> datetime:
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("database timestamps must include a timezone")
    return parsed


def input_file(repo: Path, relative: str) -> Path:
    path = repo / relative
    if any(parent.is_symlink() for parent in [path, *path.parents] if parent != repo):
        raise ValueError(f"scan input cannot be a symlink: {relative}")
    if not path.is_file():
        raise ValueError(f"missing scan input: {relative}")
    return path


def unique_object(pairs: list[tuple[str, Any]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def strict_jsonc(text: str) -> dict:
    # Preserve strings while removing JSONC comments, then trailing commas.
    strings = r'"(?:[^"\\]|\\.)*"'
    text = re.sub(strings + r'|//[^\n]*|/\*.*?\*/',
                  lambda m: m[0] if m[0].startswith('"') else ' ', text, flags=re.S)
    text = re.sub(strings + r'|,\s*(?=[}\]])',
                  lambda m: m[0] if m[0].startswith('"') else '', text)
    return json.loads(text, object_pairs_hook=unique_object)


def validate_inputs(repo: Path, work: Path, graphs: dict[str, str], report: dict[str, Any]) -> dict[str, dict]:
    manifests = []
    bun_inventories = {}
    if report["scope"] == "supported":
        workspace = tomllib.loads(input_file(repo, "Cargo.toml").read_text())["workspace"]
        if set(workspace["members"]) != ACTIVE_CRATES or not {"desktop/src-tauri", "crates/safeparts_uniffi"} <= set(workspace.get("exclude", [])):
            raise ValueError("Cargo workspace differs from the supported scan boundary")
        manifests = ["Cargo.toml", *[f"{crate}/Cargo.toml" for crate in sorted(ACTIVE_CRATES)]]
        for manifest in manifests:
            input_file(repo, manifest)
        metadata = json.loads(run(["cargo", "metadata", "--locked", "--all-features", "--format-version", "1"], repo, report))
        members = {package["name"] for package in metadata["packages"] if package["id"] in metadata["workspace_members"]}
        if members != {Path(crate).name for crate in ACTIVE_CRATES}:
            raise ValueError("Cargo metadata differs from the supported workspace")
    bun_version = run(["bun", "--version"], work, report).strip()
    if bun_version != BUN_VERSION:
        raise ValueError(f"Bun {BUN_VERSION} required to parse Bun lockfiles; run mise install")
    report["bun_version"] = bun_version
    for relative, kind in graphs.items():
        path = input_file(repo, relative)
        if kind == "cargo":
            lock = tomllib.loads(path.read_text())
            if not isinstance(lock.get("package"), list) or not lock["package"]:
                raise ValueError(f"empty Cargo graph: {relative}")
            continue
        manifest_path = str(Path(relative).with_name("package.json"))
        manifest = json.loads(input_file(repo, manifest_path).read_text())
        manifests.append(manifest_path)
        lock = json.loads(run(["bun", "-e", "console.log(JSON.stringify(Bun.JSONC.parse(await Bun.file(process.argv[1]).text())))", str(path)], work, report))
        if lock != strict_jsonc(path.read_text()):
            raise ValueError(f"Bun JSONC parse disagrees with strict inventory: {relative}")
        if lock.get("lockfileVersion") != 1 or set(lock["workspaces"]) != {""} or not lock.get("packages"):
            raise ValueError(f"unsupported or empty Bun graph: {relative}")
        root = lock["workspaces"][""]
        for key in ("name", "dependencies", "devDependencies", "optionalDependencies", "peerDependencies"):
            if manifest.get(key, {}) != root.get(key, {}):
                raise ValueError(f"{manifest_path} {key} differs from {relative}")
        if manifest.get("workspaces") or manifest.get("overrides") or manifest.get("resolutions"):
            raise ValueError(f"{manifest_path}: workspace or override policy requires explicit scan review")
        bun_inventories[relative] = bun_inventory(lock, relative)
    report["manifests"] = [{"path": path, "sha256": sha256(input_file(repo, path))} for path in manifests]
    return bun_inventories


def bun_inventory(lock: dict, relative: str) -> dict:
    # Bun keys are resolution paths, not npm names. The tuple's first field is
    # canonical even for nested/scoped packages and npm aliases.
    identities: dict[tuple[str, str], list[str]] = {}
    number = r"(?:0|[1-9][0-9]*)"
    prerelease = r"(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)"
    version_pattern = rf"{number}\.{number}\.{number}(?:-{prerelease}(?:\.{prerelease})*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    if not isinstance(lock["packages"], dict):
        raise ValueError(f"invalid Bun packages: {relative}")
    for key, record in lock["packages"].items():
        if not isinstance(key, str) or not key or not isinstance(record, list) or not record or not isinstance(record[0], str):
            raise ValueError(f"invalid Bun package: {relative}: {key}")
        match = re.fullmatch(r"(@[a-z0-9._~-]+/[a-z0-9._~-]+|[a-z0-9._~-]+)@"
                             rf"({version_pattern})", record[0])
        if not match:
            raise ValueError(f"unsupported Bun package identity: {relative}: {key}: {record[0]}")
        identities.setdefault((match[1], match[2]), []).append(key)
    return {"path": relative, "lock_records": len(lock["packages"]),
            "identities": [{"name": name, "version": version, "lock_keys": sorted(keys)}
                           for (name, version), keys in sorted(identities.items())]}


def bun_sbom(inventory: dict) -> dict:
    components = []
    for identity in inventory["identities"]:
        name, version = identity["name"], identity["version"]
        purl = f"pkg:npm/{quote(name, safe='/')}@{quote(version, safe='')}"
        components.append({"type": "library", "name": name, "version": version,
                           "purl": purl, "bom-ref": purl})
    return {"bomFormat": "CycloneDX", "specVersion": "1.5", "version": 1, "components": components}


def reconcile_bun_inventory(result: dict, inventory: dict) -> None:
    expected = {(p["name"], p["version"]): p["lock_keys"] for p in inventory["identities"]}
    actual = {(p["Name"], p["Version"]) for p in result["Packages"]}
    missing, unexpected = set(expected) - actual, actual - set(expected)
    inventory.update(scanned_identities=len(actual), missing=sorted(missing), unexpected=sorted(unexpected))
    if missing or unexpected:
        raise ValueError(f"Bun advisory inventory mismatch: {inventory['path']}: missing={sorted(missing)}, unexpected={sorted(unexpected)}")
    for package in result["Packages"]:
        package["BunLockKeys"] = expected[(package["Name"], package["Version"])]
    for finding in result.get("Vulnerabilities", []):
        identity = (finding["PkgName"], finding["InstalledVersion"])
        if identity not in actual:
            raise ValueError(f"finding outside scanned inventory: {inventory['path']}: {identity}")
        finding["BunLockKeys"] = expected[identity]


def validate_results(raw: dict[str, Any], graphs: dict[str, str]) -> list[dict[str, Any]]:
    if raw.get("SchemaVersion") != 2 or not isinstance(raw.get("Results"), list):
        raise ValueError("scanner returned an invalid report schema")
    results = raw["Results"]
    if len(results) != len(graphs) or {item["Target"] for item in results} != set(graphs):
        raise ValueError("scanner inventory does not match the explicit scan scope")
    for item in results:
        if item["Type"] != graphs[item["Target"]] or item["Class"] != "lang-pkgs":
            raise ValueError(f"wrong scanner type for {item['Target']}")
        packages = item.get("Packages")
        if not isinstance(packages, list) or not packages:
            raise ValueError(f"scanner returned no package inventory for {item['Target']}")
        for package in packages:
            if not all(isinstance(package.get(key), str) and package[key] for key in ("Name", "Version")):
                raise ValueError("scanner returned an invalid package")
        vulnerabilities = item.get("Vulnerabilities", [])
        if not isinstance(vulnerabilities, list):
            raise ValueError("scanner returned an invalid vulnerability list")
        for finding in vulnerabilities:
            if not all(isinstance(finding.get(key), str) and finding[key]
                       for key in ("VulnerabilityID", "PkgName", "InstalledVersion", "Severity")):
                raise ValueError("scanner returned an invalid vulnerability")
            if finding["Severity"] not in {"UNKNOWN", "LOW", "MEDIUM", "HIGH", "CRITICAL"}:
                raise ValueError("scanner returned an unknown severity")
    return sorted(results, key=lambda item: item["Target"])


def scan(repo: Path, output: Path, report: dict[str, Any]) -> None:
    with tempfile.TemporaryDirectory(prefix="safeparts-scan-") as temporary:
        work = Path(temporary)
        stage = work / "inputs"
        stage.mkdir()
        graphs = SUPPORTED if report["scope"] == "supported" else RETIRED
        if report["scope"] == "retired":
            for surface, paths in RETIRED_GAPS.items():
                report["coverage_gaps"].append({"surface": surface,
                    "reason": "No usable locked graph in the scan contract; dependencies are unresolved and were not restored.",
                    "manifests": [{"path": path, "present": (repo / path).is_file(),
                                   "sha256": sha256(repo / path) if (repo / path).is_file() else None} for path in paths]})
        bun_inventories = validate_inputs(repo, work, graphs, report)
        report["bun_inventory_comparison"] = list(bun_inventories.values())
        for path, kind in graphs.items():
            destination = stage / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            if kind == "cargo":
                shutil.copyfile(input_file(repo, path), destination)
            report["inventory"].append({"path": path, "sha256": sha256(input_file(repo, path))})
        (work / "trivy.yaml").write_text("{}\n")
        (work / "empty.ignore").write_text("")
        version = json.loads(run(["trivy", "version", "--format", "json"], work, report))
        if not isinstance(version, dict):
            raise ValueError("Trivy returned invalid version metadata")
        report["scanner"] = {"name": "Trivy", "Version": version.get("Version")}
        if version.get("Version") != TRIVY_VERSION:
            raise ValueError(f"Trivy {TRIVY_VERSION} required; run mise install")
        cache = output / "cache"
        base = ["trivy", "fs", "--config", str(work / "trivy.yaml"), "--cache-dir", str(cache),
                "--disable-telemetry", "--skip-version-check", "--no-progress"]
        run(base + ["--download-db-only"], work, report)
        database = json.loads((cache / "db/metadata.json").read_text())
        now = datetime.now(timezone.utc)
        if database["Version"] != 2 or not (timestamp(database["UpdatedAt"]) <= now < timestamp(database["NextUpdate"])):
            raise ValueError("database is stale, future-dated or has an unsupported schema")
        if timestamp(database["DownloadedAt"]) > now:
            raise ValueError("database download timestamp is in the future")
        report["database"] = {"metadata": database, "sha256": sha256(cache / "db/trivy.db"),
                              "freshness_checked_at": now.isoformat()}
        command = base + ["--skip-db-update", "--scanners", "vuln", "--pkg-types", "library",
                          "--include-dev-deps", "--list-all-pkgs", "--severity", "UNKNOWN,LOW,MEDIUM,HIGH,CRITICAL",
                          "--ignorefile", str(work / "empty.ignore"), "--exit-code", "0", "--format", "json", str(stage)]
        raw_text = run(command, work, report)
        (output / "scanner.json").write_text(raw_text)
        raw = json.loads(raw_text)
        report["results"] = validate_results(raw, {path: kind for path, kind in graphs.items() if kind == "cargo"})
        for path, inventory in bun_inventories.items():
            stem = path.replace("/", "-")
            sbom_path = output / f"{stem}.cdx.json"
            sbom_path.write_text(json.dumps(bun_sbom(inventory), indent=2) + "\n")
            inventory["sbom"] = {"path": sbom_path.name, "sha256": sha256(sbom_path)}
            # Scan every identity, including optional/platform/dev-only records.
            # SBOM has no reachability classification; do not infer Dev from names.
            sbom_command = ["trivy", "sbom", *base[2:], "--skip-db-update", "--scanners", "vuln",
                            "--pkg-types", "library", "--list-all-pkgs", "--severity", "UNKNOWN,LOW,MEDIUM,HIGH,CRITICAL",
                            "--ignorefile", str(work / "empty.ignore"), "--exit-code", "0", "--format", "json", str(sbom_path)]
            raw_text = run(sbom_command, work, report)
            (output / f"scanner-{stem}.json").write_text(raw_text)
            result = validate_results(json.loads(raw_text), {"Node.js": "node-pkg"})[0]
            reconcile_bun_inventory(result, inventory)
            result["Target"] = path
            report["results"].append(result)
        report["results"].sort(key=lambda item: item["Target"])
    severities: Counter[str] = Counter()
    dev_findings = 0
    for item in report["results"]:
        findings = item.get("Vulnerabilities", [])
        packages = {package.get("ID"): package for package in item["Packages"]}
        counts = Counter(finding["Severity"] for finding in findings)
        dev = sum(packages.get(finding.get("PkgID"), {}).get("Dev") is True for finding in findings)
        report["summaries"].append({"path": item["Target"], "packages": len(item["Packages"]),
                                    "findings": len(findings), "by_severity": dict(counts),
                                    "dev_findings": dev, "other_or_unknown_findings": len(findings) - dev})
        severities.update(counts)
        dev_findings += dev
    report["finding_count"] = sum(severities.values())
    report["totals"] = {"by_severity": dict(severities), "dev_findings": dev_findings,
                        "other_or_unknown_findings": report["finding_count"] - dev_findings}
    report["status"] = "findings" if report["finding_count"] else "no-findings"
    report["coverage_complete"] = report["scope"] == "supported"


def write_report(output: Path, report: dict[str, Any]) -> None:
    lines = [f"Dependency scan ({report['scope']}): {report['status'].upper()}"]
    if report["scope"] == "retired":
        lines.append("INCOMPLETE COVERAGE: only the listed locked graphs were scanned; retired sources remain unsupported.")
        lines.extend(f"Coverage gap: {gap['surface']}: {gap['reason']}" for gap in report["coverage_gaps"])
    if report.get("scanner"):
        lines.append(f"Trivy {report['scanner'].get('Version')}; development dependencies included; all severities")
    if report.get("database"):
        database = report["database"]
        lines.append(f"Database updated {database['metadata']['UpdatedAt']}; next update {database['metadata']['NextUpdate']}")
        lines.append(f"Database SHA-256: {database['sha256']}")
    for item in report.get("bun_inventory_comparison", []):
        lines.append(f"{item['path']} inventory: {item['lock_records']} lock records; "
                     f"{len(item['identities'])} expected identities; {item.get('scanned_identities', 'not scanned')} scanned")
    for item in report["summaries"]:
        severity = ", ".join(f"{key}={value}" for key, value in sorted(item["by_severity"].items())) or "none"
        lines.append(f"{item['path']}: {item['findings']} findings; {item['packages']} packages; {severity}; "
                     f"dev={item['dev_findings']}, other/unknown={item['other_or_unknown_findings']}")
    if report["finding_count"] is not None:
        lines.append(f"Total: {report['finding_count']} package/advisory findings (not globally deduplicated)")
    lines.extend(f"ERROR: {error}" for error in report["errors"])
    lines.append(f"Report: {output / 'report.json'}")
    summary = "\n".join(lines) + "\n"
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "summary.txt").write_text(summary)
    print(summary, end="")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", choices=["supported", "retired"], default="supported")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = repo / "target/security" / args.scope
    report: dict[str, Any] = {"schema": 2, "scope": args.scope, "started_at": datetime.now(timezone.utc).isoformat(),
                              "inventory": [], "include_dev_dependencies": True, "results": [], "summaries": [],
                              "finding_count": None, "status": "error", "errors": [], "commands": [],
                              "coverage_complete": False, "coverage_gaps": []}
    try:
        output.mkdir(parents=True, exist_ok=True)
        for pattern in ("scanner*.json", "*.cdx.json"):
            for artifact in output.glob(pattern):
                artifact.unlink()
        # Invalidate an older success before running external tools.
        (output / "report.json").write_text(json.dumps(report) + "\n")
        (output / "summary.txt").write_text("Dependency scan: INCOMPLETE\n")
        scan(repo, output, report)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.SubprocessError) as error:
        report["errors"].append(str(error))
        report["status"] = "error"
        report["finding_count"] = None
        report["coverage_complete"] = False
    report["finished_at"] = datetime.now(timezone.utc).isoformat()
    try:
        write_report(output, report)
    except OSError as error:
        print(f"ERROR: cannot write scan report: {error}", file=sys.stderr)
        return 2
    return {"no-findings": 0, "findings": 1, "error": 2}[report["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
