#!/usr/bin/env python3
"""Stage and inspect a local, relocatable Apple-silicon application bundle."""

from __future__ import annotations

import argparse
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
    if path.is_symlink() or not path.is_file():
        return False
    return "Mach-O" in run("/usr/bin/file", "-b", str(path))


def inspect_bundle(app: Path) -> list[dict[str, object]]:
    failures: list[str] = []
    inventory: list[dict[str, object]] = []
    app_real = app.resolve()
    for path in sorted(app.rglob("*")):
        lowered = path.relative_to(app).as_posix().lower()
        if any(name in lowered for name in NETWORK_NAMES):
            failures.append(f"network-capable runtime is not allowed: {path.relative_to(app)}")
        if not is_macho(path):
            continue
        architectures = run("/usr/bin/lipo", "-archs", str(path)).split()
        if architectures != ["arm64"]:
            failures.append(f"unexpected architectures for {path}: {' '.join(architectures)}")
        loads = run("/usr/bin/otool", "-L", str(path)).splitlines()[1:]
        dependencies = [line.strip().split(" (", 1)[0] for line in loads]
        rpaths: list[str] = []
        lines = run("/usr/bin/otool", "-l", str(path)).splitlines()
        for index, line in enumerate(lines):
            if line.strip() == "cmd LC_RPATH" and index + 2 < len(lines):
                rpaths.append(lines[index + 2].strip().split(" (", 1)[0].removeprefix("path "))
        minos = None
        for index, line in enumerate(lines):
            if line.strip() == "cmd LC_BUILD_VERSION":
                for detail in lines[index:index + 8]:
                    if detail.strip().startswith("minos "):
                        minos = detail.strip().split()[1]
        text = "\n".join(dependencies + rpaths)
        for forbidden in FORBIDDEN_TEXT:
            if forbidden in text:
                failures.append(f"forbidden runtime path in {path}: {forbidden}")
        for dependency in dependencies:
            if dependency.startswith(("/System/Library/", "/usr/lib/")):
                continue
            if dependency.startswith("@rpath/"):
                candidate = app / "Contents/Frameworks" / dependency.removeprefix("@rpath/")
            elif dependency.startswith("@loader_path/"):
                candidate = path.parent / dependency.removeprefix("@loader_path/")
            elif dependency.startswith("@executable_path/"):
                candidate = app / "Contents/MacOS" / dependency.removeprefix("@executable_path/")
            else:
                candidate = Path(dependency)
            try:
                candidate.resolve().relative_to(app_real)
            except (ValueError, FileNotFoundError):
                failures.append(f"dependency escapes bundle for {path}: {dependency}")
                continue
            if not candidate.exists():
                failures.append(f"missing dependency for {path}: {dependency}")
        inventory.append({"path": path.relative_to(app).as_posix(), "architectures": architectures,
                          "minimum_macos": minos, "dependencies": dependencies, "rpaths": rpaths})
    if not inventory:
        failures.append("bundle contains no Mach-O files")
    if failures:
        raise RuntimeError("\n".join(failures))
    return inventory


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    if sys.platform != "darwin" or run("/usr/bin/uname", "-m") != "arm64":
        raise SystemExit("local package requires Apple-silicon macOS")
    build_dir, output_dir, repo = args.build_dir.resolve(), args.output_dir.resolve(), args.repo_root.resolve()
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)
    cmake = shutil.which("cmake")
    if not cmake:
        raise SystemExit("cmake is unavailable")
    run(cmake, "--install", str(build_dir), "--prefix", str(output_dir), "--component", "desktop")
    app = output_dir / "Safeparts.app"
    if not app.is_dir():
        raise SystemExit(f"install did not produce {app}")
    cache = run(cmake, "-LA", "-N", str(build_dir))
    if "Qt6_DIR:PATH=" not in cache:
        raise SystemExit("configured build does not record Qt6_DIR")
    qt_dir = Path(cache.split("Qt6_DIR:PATH=", 1)[1].splitlines()[0])
    macdeployqt = qt_dir.parents[2] / "bin/macdeployqt"
    if not macdeployqt.is_file():
        raise SystemExit(f"selected Qt does not provide macdeployqt: {macdeployqt}")
    qt_prefix = qt_dir.parents[2]
    qtpaths = qt_prefix / "bin/qtpaths"
    qt_libs = Path(run(str(qtpaths), "--query", "QT_INSTALL_LIBS"))
    qt_plugins = Path(run(str(qtpaths), "--query", "QT_INSTALL_PLUGINS"))
    run(str(macdeployqt), str(app), "-always-overwrite", "-no-plugins", "-verbose=1")
    # Qt 6.9's macdeployqt omits QtGui's QtDBus dependency and otherwise copies
    # unrelated image/input plugins. Stage the narrow Widgets runtime explicitly.
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
    dbus_loads = run("/usr/bin/otool", "-l", str(dbus_target)).splitlines()
    for index, line in enumerate(dbus_loads):
        if line.strip() == "cmd LC_RPATH":
            run("/usr/bin/install_name_tool", "-delete_rpath", dbus_loads[index + 2].strip().split(" (", 1)[0].removeprefix("path "), str(dbus_target))
    plugins = app / "Contents/PlugIns"
    for relative in ("platforms/libqcocoa.dylib", "styles/libqmacstyle.dylib"):
        source = qt_plugins / relative
        destination = plugins / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    for binary in sorted(app.rglob("*")):
        if is_macho(binary):
            run("/usr/bin/codesign", "--force", "--sign", "-", str(binary))
    license_dir = app / "Contents/Resources/licenses"
    license_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(repo / "LICENSE", license_dir / "Safeparts-MIT.txt")
    macho = inspect_bundle(app)
    run("/usr/bin/codesign", "--force", "--sign", "-", str(app))
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app))
    with (app / "Contents/Info.plist").open("rb") as handle:
        plist = plistlib.load(handle)
    manifest = {
        "artifact": "local engineering material; not a release or clean-host qualification",
        "source_commit": run("/usr/bin/git", "rev-parse", "HEAD", cwd=repo),
        "bundle_identifier": plist["CFBundleIdentifier"],
        "deployment_target": plist["LSMinimumSystemVersion"],
        "toolchain": {
            "rust": run("mise", "exec", "--", "rustc", "--version", cwd=repo),
            "cmake": run(cmake, "--version").splitlines()[0],
            "xcode": run("/usr/bin/xcodebuild", "-version").replace("\n", "; "),
            "sdk": run("/usr/bin/xcrun", "--show-sdk-version"),
            "qt": run(str(qtpaths), "--qt-version"),
            "cxx": "1.0.195 (Cargo.lock and generated bridge command)",
        },
        "macho": macho,
        "plugins": sorted(p.relative_to(app).as_posix() for p in (app / "Contents/PlugIns").rglob("*") if p.is_file()),
        "licensing": {
            "safeparts_mit_notice": "bundled",
            "qt_lgpl_and_third_party_notices": "BLOCKED: not supplied by the installed development Qt prefix",
            "corresponding_qt_source_and_build_configuration": "BLOCKED: acquire and pin before distribution",
            "replacement_relinking_procedure": "BLOCKED: document and review with final signing/channel policy",
            "qt_source_reference": f"https://download.qt.io/official_releases/qt/{run(str(qtpaths), '--qt-version').rsplit('.', 1)[0]}/{run(str(qtpaths), '--qt-version')}/",
        },
        "qualification": {
            "relocatable_closure": "passed static inspection on development host",
            "offline_clean_host": "untested",
            "gatekeeper_notarization": "untested; bundle is ad-hoc signed",
            "minimum_macos": "untested; configured value is not a supported floor",
            "printing": "deferred and not passed",
            "clipboard_pre_materialization": "blocked by complete AppKit NSData acquisition",
        },
    }
    manifest["files"] = tree_hashes(app)
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    (output_dir / "SHA256SUMS").write_text(
        f"{hashlib.sha256(manifest_path.read_bytes()).hexdigest()}  manifest.json\n")
    print(output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
