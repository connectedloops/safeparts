#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import re
import shutil
import sys
import tarfile
import zipfile
from pathlib import Path


def _detect_os() -> str:
    if sys.platform.startswith("linux"):
        return "linux"
    if sys.platform == "darwin":
        return "macos"
    if sys.platform in ("win32", "cygwin"):
        return "windows"
    raise RuntimeError(f"unsupported platform: {sys.platform}")


def _detect_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "x86_64"
    if machine in ("aarch64", "arm64"):
        return "aarch64"
    return machine


def normalized_version(value: str) -> str:
    version = value.removeprefix("refs/tags/").removeprefix("v")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError(f"release version must use vMAJOR.MINOR.PATCH form: {value}")
    return version


def _sha256_hex(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _binary_name(base: str, os_id: str) -> str:
    return f"{base}.exe" if os_id == "windows" else base


def _target_release_dir(repo_root: Path, cargo_target: str | None) -> Path:
    if cargo_target:
        return repo_root / "target" / cargo_target / "release"
    return repo_root / "target" / "release"


def main() -> int:
    parser = argparse.ArgumentParser(description="Package Safeparts release binaries")
    parser.add_argument(
        "--version",
        required=True,
        help="Version string for naming (e.g. 0.1.0 or v0.1.0)",
    )
    parser.add_argument(
        "--target",
        default=None,
        help="Optional cargo --target triple (uses target/<triple>/release)",
    )
    parser.add_argument(
        "--arch",
        default=None,
        help="Override detected architecture for naming",
    )
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Output directory (default: dist/release)",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    os_id = _detect_os()
    arch = args.arch or _detect_arch()

    version = normalized_version(args.version)

    out_dir = Path(args.out_dir) if args.out_dir else repo_root / "dist" / "release"
    out_dir.mkdir(parents=True, exist_ok=True)

    release_dir = _target_release_dir(repo_root, args.target)
    cli_path = release_dir / _binary_name("safeparts", os_id)
    tui_path = release_dir / _binary_name("safeparts-tui", os_id)

    missing = [p for p in (cli_path, tui_path) if not p.exists()]
    if missing:
        missing_list = ", ".join(str(p) for p in missing)
        raise RuntimeError(
            "missing built binaries (run cargo build --release first): " + missing_list
        )

    stage_dir = out_dir / f"safeparts-{version}-{os_id}-{arch}"
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True)

    shutil.copy2(cli_path, stage_dir / cli_path.name)
    shutil.copy2(tui_path, stage_dir / tui_path.name)

    license_path = repo_root / "LICENSE"
    if license_path.exists():
        shutil.copy2(license_path, stage_dir / "LICENSE")

    # Ensure binaries are executable on unix-like systems.
    if os_id != "windows":
        for p in (stage_dir / cli_path.name, stage_dir / tui_path.name):
            mode = p.stat().st_mode
            p.chmod(mode | 0o111)

    base_name = stage_dir.name
    if os_id == "windows":
        archive_path = out_dir / f"{base_name}.zip"
        if archive_path.exists():
            archive_path.unlink()
        with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for file_path in stage_dir.rglob("*"):
                if file_path.is_dir():
                    continue
                z.write(file_path, arcname=str(Path(base_name) / file_path.relative_to(stage_dir)))
    else:
        archive_path = out_dir / f"{base_name}.tar.gz"
        if archive_path.exists():
            archive_path.unlink()
        with tarfile.open(archive_path, "w:gz") as t:
            t.add(stage_dir, arcname=base_name)

    # Local-only checksums for convenience.
    sums_path = out_dir / "SHA256SUMS.txt"
    sha = _sha256_hex(archive_path)
    with sums_path.open("w", encoding="utf-8") as f:
        f.write(f"{sha}  {archive_path.name}{os.linesep}")

    print(str(archive_path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
