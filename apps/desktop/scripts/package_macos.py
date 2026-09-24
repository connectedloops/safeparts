#!/usr/bin/env python3
"""Stage and inspect local, relocatable Apple-silicon application material."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

FORBIDDEN_TEXT = ("/opt/homebrew", "/usr/local", "/target/desktop-", "QtNetwork.framework")
NETWORK_NAMES = ("libqnetwork", "bearer", "networkinformation", "tls/")
OWNER_MARKER = ".safeparts-desktop-package"
LOAD_COMMANDS = {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB"}


@dataclass(frozen=True)
class MachORecord:
    path: str
    architectures: tuple[str, ...]
    minimum_macos: str | None
    install_id: str | None
    dependencies: tuple[str, ...]
    rpaths: tuple[str, ...]


def run(*args: str, cwd: Path | None = None) -> str:
    result = subprocess.run(args, cwd=cwd, check=True, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    return result.stdout.strip()


def tree_hashes(root: Path) -> list[dict[str, str]]:
    rows = []
    for path in sorted(root.rglob("*"), key=lambda p: p.relative_to(root).as_posix()):
        rel = path.relative_to(root).as_posix()
        if path.is_symlink():
            rows.append({"path": rel, "type": "symlink", "target": os.readlink(path)})
        elif path.is_file():
            rows.append({"path": rel, "type": "file", "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    return rows


def is_macho(path: Path) -> bool:
    return not path.is_symlink() and path.is_file() and "Mach-O" in run("/usr/bin/file", "-b", str(path))


def parse_load_commands(text: str) -> tuple[str | None, tuple[str, ...], tuple[str, ...], str | None]:
    lines = text.splitlines()
    install_id = None
    dependencies: list[str] = []
    rpaths: list[str] = []
    minimum = None
    def field_after(index: int, prefix: str) -> str:
        for detail in lines[index + 1:index + 6]:
            stripped = detail.strip()
            if stripped.startswith(prefix + " "):
                return stripped.split(" (", 1)[0].removeprefix(prefix + " ")
        raise RuntimeError(f"malformed otool output: missing {prefix}")

    for index, line in enumerate(lines):
        command = line.strip().removeprefix("cmd ")
        if command == "LC_ID_DYLIB":
            install_id = field_after(index, "name")
        elif command in LOAD_COMMANDS:
            dependencies.append(field_after(index, "name"))
        elif command == "LC_RPATH":
            rpaths.append(field_after(index, "path"))
        elif command == "LC_BUILD_VERSION":
            for detail in lines[index:index + 8]:
                if detail.strip().startswith("minos "):
                    minimum = detail.strip().split()[1]
                    break
    return install_id, tuple(dependencies), tuple(rpaths), minimum


def collect_record(app: Path, path: Path) -> MachORecord:
    install_id, dependencies, rpaths, minimum = parse_load_commands(run("/usr/bin/otool", "-l", str(path)))
    return MachORecord(path.relative_to(app).as_posix(), tuple(run("/usr/bin/lipo", "-archs", str(path)).split()),
                       minimum, install_id, dependencies, rpaths)


def resolve_token(app: Path, binary: Path, value: str) -> Path | None:
    if value.startswith("@loader_path/"):
        return binary.parent / value.removeprefix("@loader_path/")
    if value.startswith("@executable_path/"):
        return app / "Contents/MacOS" / value.removeprefix("@executable_path/")
    if value.startswith("/"):
        return Path(value)
    return None


def validate_records(app: Path, records: list[MachORecord]) -> list[str]:
    failures: list[str] = []
    app_real = app.resolve()
    for record in records:
        path = app / record.path
        lowered = record.path.lower()
        if "qtnetwork" in lowered or any(name in lowered for name in NETWORK_NAMES):
            failures.append(f"network-capable runtime is not allowed: {record.path}")
        if record.architectures != ("arm64",):
            failures.append(f"unexpected architectures for {record.path}: {' '.join(record.architectures)}")
        if not record.minimum_macos:
            failures.append(f"missing deployment target for {record.path}")
        for value in (*record.dependencies, *record.rpaths):
            if any(forbidden in value for forbidden in FORBIDDEN_TEXT):
                failures.append(f"forbidden runtime path in {record.path}: {value}")
        for rpath in record.rpaths:
            candidate = resolve_token(app, path, rpath)
            if candidate is None:
                failures.append(f"unresolved runpath for {record.path}: {rpath}")
                continue
            try:
                candidate.resolve().relative_to(app_real)
            except ValueError:
                failures.append(f"runpath escapes bundle for {record.path}: {rpath}")
        for dependency in record.dependencies:
            if dependency.startswith(("/System/Library/", "/usr/lib/")):
                continue
            if dependency.startswith("@rpath/"):
                failures.append(f"unresolved @rpath dependency for {record.path}: {dependency}")
                continue
            candidate = resolve_token(app, path, dependency)
            if candidate is None:
                failures.append(f"unresolved dependency for {record.path}: {dependency}")
                continue
            try:
                candidate.resolve(strict=True).relative_to(app_real)
            except FileNotFoundError:
                failures.append(f"missing dependency for {record.path}: {dependency}")
            except ValueError:
                failures.append(f"dependency escapes bundle for {record.path}: {dependency}")
    return failures


def inspect_bundle(app: Path) -> list[dict[str, object]]:
    records = [collect_record(app, path) for path in sorted(app.rglob("*")) if is_macho(path)]
    failures = validate_records(app, records)
    if not records:
        failures.append("bundle contains no Mach-O files")
    if failures:
        raise RuntimeError("\n".join(failures))
    return [{"path": r.path, "architectures": list(r.architectures), "minimum_macos": r.minimum_macos,
             "install_id": r.install_id, "dependencies": list(r.dependencies), "rpaths": list(r.rpaths)} for r in records]


def prepare_output(repo: Path, output: Path) -> None:
    target = (repo / "target").resolve()
    try:
        output.relative_to(target)
    except ValueError as error:
        raise RuntimeError(f"output must be inside repository target directory: {output}") from error
    if output == target:
        raise RuntimeError("output cannot be the repository target directory")
    marker = output / OWNER_MARKER
    if output.exists():
        if not marker.is_file() or marker.read_text() != "owned staging directory\n":
            raise RuntimeError(f"refusing to delete unowned output directory: {output}")
        shutil.rmtree(output)
    output.mkdir(parents=True)
    (output / OWNER_MARKER).write_text("owned staging directory\n")


def normalize_load_paths(app: Path) -> None:
    frameworks = app / "Contents/Frameworks"
    for binary in sorted(app.rglob("*")):
        if not is_macho(binary):
            continue
        install_id, dependencies, rpaths, _ = parse_load_commands(run("/usr/bin/otool", "-l", str(binary)))
        del install_id
        for rpath in rpaths:
            run("/usr/bin/install_name_tool", "-delete_rpath", rpath, str(binary))
        for dependency in dependencies:
            if not dependency.startswith("@rpath/"):
                continue
            target = frameworks / dependency.removeprefix("@rpath/")
            replacement = "@loader_path/" + os.path.relpath(target, binary.parent)
            run("/usr/bin/install_name_tool", "-change", dependency, replacement, str(binary))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    if sys.platform != "darwin" or run("/usr/bin/uname", "-m") != "arm64":
        raise SystemExit("local package requires Apple-silicon macOS")
    build_dir, output_dir, repo = args.build_dir.resolve(), args.output_dir.resolve(), args.repo_root.resolve()
    dirty = run("/usr/bin/git", "status", "--porcelain", cwd=repo)
    if dirty:
        raise SystemExit("refusing package provenance from a dirty source tree")
    if build_dir == output_dir or build_dir in output_dir.parents or output_dir in build_dir.parents:
        raise SystemExit("build and output staging directories must be separate")
    # Both directories are package-owned and recreated before configuration. This
    # prevents any object, Cargo fingerprint, or generated source from a prior
    # checkout from entering the installed artifact.
    prepare_output(repo, build_dir)
    prepare_output(repo, output_dir)
    cmake = shutil.which("cmake")
    if not cmake:
        raise SystemExit("cmake is unavailable")
    rust_target = build_dir / "rust-target"
    run(cmake, "-S", str(repo / "apps/desktop"), "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON", "-DCMAKE_OSX_ARCHITECTURES=arm64",
        f"-DDESKTOP_RUST_TARGET_DIR={rust_target}")
    run(cmake, "--build", str(build_dir), "-j2")
    cache_path = build_dir / "CMakeCache.txt"
    cache_text = cache_path.read_text(errors="replace")
    if f"CMAKE_HOME_DIRECTORY:INTERNAL={repo / 'apps/desktop'}" not in cache_text:
        raise SystemExit("fresh build cache belongs to another source tree")
    run(cmake, "--install", str(build_dir), "--prefix", str(output_dir), "--component", "desktop")
    app = output_dir / "Safeparts.app"
    if not app.is_dir():
        raise SystemExit(f"install did not produce {app}")
    if "Qt6_DIR:PATH=" not in cache_text:
        raise SystemExit("configured build does not record Qt6_DIR")
    qt_dir = Path(cache_text.split("Qt6_DIR:PATH=", 1)[1].splitlines()[0])
    qt_prefix = qt_dir.parents[2]
    macdeployqt, qtpaths = qt_prefix / "bin/macdeployqt", qt_prefix / "bin/qtpaths"
    if not macdeployqt.is_file():
        raise SystemExit(f"selected Qt does not provide macdeployqt: {macdeployqt}")
    qt_libs = Path(run(str(qtpaths), "--query", "QT_INSTALL_LIBS"))
    qt_plugins = Path(run(str(qtpaths), "--query", "QT_INSTALL_PLUGINS"))
    run(str(macdeployqt), str(app), "-always-overwrite", "-no-plugins", "-verbose=1")
    frameworks = app / "Contents/Frameworks"
    qt_dbus = frameworks / "QtDBus.framework"
    if not qt_dbus.exists():
        shutil.copytree(qt_libs / "QtDBus.framework", qt_dbus, symlinks=True)
    qt_dbus_binary = qt_dbus / "Versions/A/QtDBus"
    dbus_source = next(Path(line.strip().split(" (", 1)[0]) for line in run("/usr/bin/otool", "-L", str(qt_dbus_binary)).splitlines()[1:] if "libdbus-1" in line)
    dbus_target = frameworks / dbus_source.name
    shutil.copy2(dbus_source, dbus_target)
    run("/usr/bin/install_name_tool", "-id", "@rpath/QtDBus.framework/Versions/A/QtDBus", str(qt_dbus_binary))
    run("/usr/bin/install_name_tool", "-change", str(dbus_source), f"@rpath/{dbus_target.name}", str(qt_dbus_binary))
    qt_core_source = next(line.strip().split(" (", 1)[0] for line in run("/usr/bin/otool", "-L", str(qt_dbus_binary)).splitlines()[1:] if "QtCore.framework" in line)
    run("/usr/bin/install_name_tool", "-change", qt_core_source, "@rpath/QtCore.framework/Versions/A/QtCore", str(qt_dbus_binary))
    run("/usr/bin/install_name_tool", "-id", f"@rpath/{dbus_target.name}", str(dbus_target))
    plugins = app / "Contents/PlugIns"
    for relative in ("platforms/libqcocoa.dylib", "styles/libqmacstyle.dylib"):
        destination = plugins / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(qt_plugins / relative, destination)
    normalize_load_paths(app)
    for binary in sorted(app.rglob("*")):
        if is_macho(binary):
            run("/usr/bin/codesign", "--force", "--sign", "-", str(binary))
    resources = app / "Contents/Resources"
    license_dir = resources / "licenses"
    license_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(repo / "LICENSE", license_dir / "Safeparts-MIT.txt")
    (resources / "qt.conf").write_text("[Paths]\nPrefix = ..\nPlugins = PlugIns\n")
    macho = inspect_bundle(app)
    run("/usr/bin/codesign", "--force", "--sign", "-", str(app))
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app))
    with (app / "Contents/Info.plist").open("rb") as handle:
        plist = plistlib.load(handle)
    source_commit = run("/usr/bin/git", "rev-parse", "HEAD", cwd=repo)
    manifest = {
        "artifact": "local engineering material; not a release or clean-host qualification",
        "source": {"commit": source_commit, "tree": "clean"},
        "build": {"provenance": "package-owned build and Cargo directories recreated before configuration",
                  "cmake_cache_sha256": hashlib.sha256(cache_path.read_bytes()).hexdigest(),
                  "installed_executable_sha256": hashlib.sha256((app / "Contents/MacOS/Safeparts").read_bytes()).hexdigest()},
        "bundle_identifier": plist["CFBundleIdentifier"], "deployment_target": plist["LSMinimumSystemVersion"],
        "toolchain": {"rust": run("mise", "exec", "--", "rustc", "--version", cwd=repo),
                      "cmake": run(cmake, "--version").splitlines()[0],
                      "xcode": run("/usr/bin/xcodebuild", "-version").replace("\n", "; "),
                      "sdk": run("/usr/bin/xcrun", "--show-sdk-version"),
                      "qt": run(str(qtpaths), "--qt-version"), "cxx": "1.0.195 (Cargo.lock and generated bridge command)"},
        "macho": macho,
        "plugins": sorted(p.relative_to(app).as_posix() for p in plugins.rglob("*") if p.is_file()),
        "licensing": {"safeparts_mit_notice": "bundled",
                      "qt_lgpl_and_third_party_notices": "BLOCKED: not supplied by the installed development Qt prefix",
                      "corresponding_qt_source_and_build_configuration": "BLOCKED: acquire and pin before distribution",
                      "replacement_relinking_procedure": "BLOCKED: document and review with final signing/channel policy",
                      "qt_source_reference": f"https://download.qt.io/official_releases/qt/{run(str(qtpaths), '--qt-version').rsplit('.', 1)[0]}/{run(str(qtpaths), '--qt-version')}/"},
        "qualification": {"relocatable_closure": "passed static inspection on development host",
                          "offline_clean_host": "untested", "gatekeeper_notarization": "untested; bundle is ad-hoc signed",
                          "minimum_macos": "untested; configured value is not a supported floor",
                          "printing": "deferred and not passed",
                          "clipboard_pre_materialization": "blocked by complete AppKit NSData acquisition"}}
    manifest["files"] = tree_hashes(app)
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    (output_dir / "SHA256SUMS").write_text(f"{hashlib.sha256(manifest_path.read_bytes()).hexdigest()}  manifest.json\n")
    print(output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
