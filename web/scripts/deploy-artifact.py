#!/usr/bin/env python3
"""Prepare and verify the static Web artifact deployed by every provider."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import re
import shutil
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

METADATA_DIRECTORY = "safeparts-build"
METADATA_FILE = "metadata.json"
MANIFEST_FILE = "content-manifest.sha256"
SELF_REFERENTIAL_EVIDENCE = {
    f"{METADATA_DIRECTORY}/{MANIFEST_FILE}",
    f"{METADATA_DIRECTORY}/{METADATA_FILE}",
}
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
DIGEST_PATTERN = re.compile(r"[0-9a-f]{64}")
DIAGNOSTIC_VALUE_LIMIT = 120


class ArtifactError(RuntimeError):
    """An invalid or mismatched deployment artifact."""


@dataclass(frozen=True)
class RemoteBytes:
    content: bytes
    date: str
    digest: str
    length: int
    url: str


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def checked_site(site: Path) -> Path:
    site = site.resolve()
    if not site.is_dir():
        raise ArtifactError(f"site directory does not exist: {site}")
    return site


def content_files(site: Path) -> list[Path]:
    files: list[Path] = []
    for path in site.rglob("*"):
        relative = path.relative_to(site)
        relative_name = relative.as_posix()
        if path.is_symlink():
            raise ArtifactError(f"deployment content must not contain symlinks: {relative_name}")
        if relative_name in SELF_REFERENTIAL_EVIDENCE:
            continue
        if path.is_file():
            files.append(path)
    if not files:
        raise ArtifactError("site directory contains no deployable files")
    return sorted(files, key=lambda path: path.relative_to(site).as_posix())


def make_manifest(site: Path) -> bytes:
    lines = [
        f"{sha256_bytes(path.read_bytes())}  {path.relative_to(site).as_posix()}\n"
        for path in content_files(site)
    ]
    return "".join(lines).encode("utf-8")


def parse_manifest(content: bytes) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    try:
        lines = content.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ArtifactError("content manifest is not UTF-8") from error
    for line in lines:
        match = re.fullmatch(r"([0-9a-f]{64})  ([^\r\n]+)", line)
        if not match:
            raise ArtifactError(f"invalid content manifest line: {line!r}")
        digest, relative = match.groups()
        parts = Path(relative).parts
        if not parts or relative.startswith("/") or ".." in parts:
            raise ArtifactError(f"unsafe content manifest path: {relative!r}")
        entries.append((digest, relative))
    if not entries:
        raise ArtifactError("content manifest is empty")
    if [relative for _, relative in entries] != sorted(relative for _, relative in entries):
        raise ArtifactError("content manifest paths are not sorted")
    return entries


def prepare(args: argparse.Namespace) -> None:
    if not COMMIT_PATTERN.fullmatch(args.source_commit):
        raise ArtifactError("source commit must be a 40-character lowercase hexadecimal SHA")

    site = checked_site(args.site)
    metadata_directory = site / METADATA_DIRECTORY
    if metadata_directory.exists():
        shutil.rmtree(metadata_directory)

    manifest = make_manifest(site)
    content_digest = sha256_bytes(manifest)
    metadata = {
        "schemaVersion": 1,
        "sourceCommit": args.source_commit,
        "artifactDigest": f"sha256:{content_digest}",
        "contentDigest": content_digest,
        "contentManifest": f"/{METADATA_DIRECTORY}/{MANIFEST_FILE}",
        "tools": {
            "bun": args.bun_version,
            "node": args.node_version,
            "rust": args.rust_version,
            "wasmBindgen": args.wasm_bindgen_version,
            "wasmPack": args.wasm_pack_version,
        },
    }
    metadata_content = (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode("utf-8")

    metadata_directory.mkdir(parents=True)
    (metadata_directory / MANIFEST_FILE).write_bytes(manifest)
    (metadata_directory / METADATA_FILE).write_bytes(metadata_content)

    evidence = args.evidence.resolve()
    if evidence.exists():
        shutil.rmtree(evidence)
    evidence.mkdir(parents=True)
    (evidence / MANIFEST_FILE).write_bytes(manifest)
    (evidence / METADATA_FILE).write_bytes(metadata_content)
    (evidence / "artifact-digest.sha256").write_text(
        f"{content_digest}  {args.source_commit}\n", encoding="utf-8"
    )
    print(f"prepared Web artifact for {args.source_commit} ({content_digest})")


def evidence_files(evidence: Path) -> tuple[bytes, bytes]:
    evidence = evidence.resolve()
    try:
        manifest = (evidence / MANIFEST_FILE).read_bytes()
        metadata = (evidence / METADATA_FILE).read_bytes()
        digest_record = (evidence / "artifact-digest.sha256").read_text(encoding="utf-8")
    except FileNotFoundError as error:
        raise ArtifactError(f"deployment evidence is incomplete: {error.filename}") from error
    parse_manifest(manifest)
    try:
        metadata_value = json.loads(metadata)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ArtifactError("artifact metadata is not valid JSON") from error
    expected_digest = sha256_bytes(manifest)
    if metadata_value.get("contentDigest") != expected_digest:
        raise ArtifactError("artifact metadata digest does not match the content manifest")
    if metadata_value.get("artifactDigest") != f"sha256:{expected_digest}":
        raise ArtifactError("artifact metadata does not record the expected SHA-256 digest")
    source_commit = str(metadata_value.get("sourceCommit", ""))
    if not COMMIT_PATTERN.fullmatch(source_commit):
        raise ArtifactError("artifact metadata does not contain a full source commit")
    if digest_record != f"{expected_digest}  {source_commit}\n":
        raise ArtifactError("retained artifact digest does not match the metadata and manifest")
    return manifest, metadata


def verify(args: argparse.Namespace) -> None:
    site = checked_site(args.site)
    manifest, metadata = evidence_files(args.evidence)
    if make_manifest(site) != manifest:
        raise ArtifactError("content manifest does not match the deployment directory")
    metadata_directory = site / METADATA_DIRECTORY
    if (metadata_directory / MANIFEST_FILE).read_bytes() != manifest:
        raise ArtifactError("deployed content manifest differs from retained evidence")
    if (metadata_directory / METADATA_FILE).read_bytes() != metadata:
        raise ArtifactError("deployed metadata differs from retained evidence")
    print(f"verified Web artifact ({sha256_bytes(manifest)})")


def deployed_path(relative: str) -> str:
    if relative == "index.html":
        return "/"
    if relative.endswith("/index.html"):
        return f"/{relative[:-len('index.html')]}"
    return f"/{relative}"


def fetch(base_url: str, path: str) -> RemoteBytes:
    quoted_path = "/".join(urllib.parse.quote(segment) for segment in path.split("/"))
    url = f"{base_url.rstrip('/')}{quoted_path}"
    request = urllib.request.Request(
        url,
        headers={"Accept-Encoding": "identity", "User-Agent": "safeparts-artifact-verifier/1"},
    )
    try:
        response = urllib.request.urlopen(request, timeout=30)
    except urllib.error.HTTPError as error:
        raise ArtifactError(f"remote verification failed for {url}: HTTP {error.code}") from error
    except urllib.error.URLError as error:
        raise ArtifactError(f"remote verification failed for {url}: {error.reason}") from error
    with response:
        encoding = response.headers.get("Content-Encoding", "identity").lower()
        if encoding not in {"", "identity"}:
            raise ArtifactError(
                f"remote verification requested identity bytes but {url} returned {encoding} encoding"
            )
        content = response.read()
        return RemoteBytes(
            content=content,
            date=response.headers.get("Date", "unknown"),
            digest=sha256_bytes(content),
            length=len(content),
            url=url,
        )


def bounded_diagnostic_value(value: str) -> str:
    escaped = value.encode("unicode_escape", "backslashreplace").decode("ascii")
    if len(escaped) > DIAGNOSTIC_VALUE_LIMIT:
        return escaped[:DIAGNOSTIC_VALUE_LIMIT] + "..."
    return escaped


def metadata_identity(content: bytes) -> dict[str, str]:
    try:
        metadata = json.loads(content)
    except (UnicodeDecodeError, json.JSONDecodeError):
        return {
            "sourceCommit": "unparseable",
            "contentDigest": "unparseable",
            "artifactDigest": "unparseable",
        }
    if not isinstance(metadata, dict):
        return {
            "sourceCommit": "unparseable",
            "contentDigest": "unparseable",
            "artifactDigest": "unparseable",
        }

    source_commit = metadata.get("sourceCommit")
    content_digest = metadata.get("contentDigest")
    artifact_digest = metadata.get("artifactDigest")
    return {
        "sourceCommit": source_commit
        if isinstance(source_commit, str) and COMMIT_PATTERN.fullmatch(source_commit)
        else "missing" if source_commit is None else "invalid",
        "contentDigest": content_digest
        if isinstance(content_digest, str) and DIGEST_PATTERN.fullmatch(content_digest)
        else "missing" if content_digest is None else "invalid",
        "artifactDigest": artifact_digest
        if (isinstance(artifact_digest, str)
            and artifact_digest.startswith("sha256:")
            and DIGEST_PATTERN.fullmatch(artifact_digest.removeprefix("sha256:")))
        else "missing" if artifact_digest is None else "invalid",
    }


def mismatch_diagnostics(
    kind: str,
    expected: bytes,
    observed: RemoteBytes,
    provider_deployment_id: str | None,
) -> str:
    expected_identity = metadata_identity(expected) if kind == "metadata" else {}
    observed_identity = metadata_identity(observed.content) if kind == "metadata" else {}
    details = [
        f"served artifact {kind} does not match the retained artifact",
        f"expected sha256={sha256_bytes(expected)} bytes={len(expected)}",
        (
            f"observed sha256={observed.digest} bytes={observed.length} "
            f"date={bounded_diagnostic_value(observed.date)} "
            f"url={bounded_diagnostic_value(observed.url)}"
        ),
    ]
    if expected_identity:
        details.extend(
            f"expected {key}={value}" for key, value in expected_identity.items()
        )
        details.extend(
            f"observed {key}={value}" for key, value in observed_identity.items()
        )
    if provider_deployment_id:
        details.append(f"providerDeploymentId={bounded_diagnostic_value(provider_deployment_id)}")
    return "; ".join(details)


def verify_remote(args: argparse.Namespace) -> None:
    manifest, metadata = evidence_files(args.evidence)
    remote_metadata = fetch(args.base_url, f"/{METADATA_DIRECTORY}/{METADATA_FILE}")
    remote_manifest = fetch(args.base_url, f"/{METADATA_DIRECTORY}/{MANIFEST_FILE}")
    if remote_metadata.content != metadata:
        raise ArtifactError(
            mismatch_diagnostics(
                "metadata", metadata, remote_metadata, args.provider_deployment_id
            )
        )
    if remote_manifest.content != manifest:
        raise ArtifactError(
            mismatch_diagnostics(
                "content manifest", manifest, remote_manifest, args.provider_deployment_id
            )
        )
    for digest, relative in parse_manifest(manifest):
        served = fetch(args.base_url, deployed_path(relative))
        if served.digest != digest:
            raise ArtifactError(f"served content digest does not match: {relative}")
    metadata_value = json.loads(metadata)
    print(
        f"verified {len(parse_manifest(manifest))} served files for "
        f"{metadata_value['sourceCommit']} at {args.base_url}"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    prepare_parser = subparsers.add_parser("prepare", help="add metadata and retain evidence")
    prepare_parser.add_argument("--site", type=Path, required=True)
    prepare_parser.add_argument("--evidence", type=Path, required=True)
    prepare_parser.add_argument("--source-commit", required=True)
    prepare_parser.add_argument("--rust-version", required=True)
    prepare_parser.add_argument("--bun-version", required=True)
    prepare_parser.add_argument("--node-version", required=True)
    prepare_parser.add_argument("--wasm-pack-version", required=True)
    prepare_parser.add_argument("--wasm-bindgen-version", required=True)
    prepare_parser.set_defaults(handler=prepare)

    verify_parser = subparsers.add_parser("verify", help="verify a local deployment directory")
    verify_parser.add_argument("--site", type=Path, required=True)
    verify_parser.add_argument("--evidence", type=Path, required=True)
    verify_parser.set_defaults(handler=verify)

    remote_parser = subparsers.add_parser("verify-remote", help="verify bytes served by a provider")
    remote_parser.add_argument("--base-url", required=True)
    remote_parser.add_argument("--evidence", type=Path, required=True)
    remote_parser.add_argument(
        "--provider-deployment-id",
        help="provider deployment/version identifier to include in mismatch diagnostics",
    )
    remote_parser.set_defaults(handler=verify_remote)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        args.handler(args)
    except (ArtifactError, FileNotFoundError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
