#!/usr/bin/env python3
"""Stage and inspect local, relocatable Apple-silicon application material."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import shutil
import stat
import subprocess
import sys

FORBIDDEN_TEXT = ("/opt/homebrew", "/usr/local", "/target/desktop-", "QtNetwork.framework")
NETWORK_NAMES = ("libqnetwork", "bearer", "networkinformation", "tls/")
FORMULA_BY_BINARY_PREFIX = {
    "libb2": "libb2", "libdbus": "dbus", "libdouble-conversion": "double-conversion",
    "libfreetype": "freetype", "libglib": "glib", "libgthread": "glib", "libgraphite2": "graphite2",
    "libharfbuzz": "harfbuzz", "libicu": "icu4c@77", "libintl": "gettext", "libmd4c": "md4c",
    "libpcre2": "pcre2", "libpng": "libpng", "libzstd": "zstd",
}
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


def derive_selected_qt_prefix(sources: dict[str, dict[str, object]], qt_version: str) -> Path:
    qt_sources = [record["source"] for destination, record in sources.items()
                  if destination.startswith("Contents/Frameworks/Qt") or destination.startswith("Contents/PlugIns/")]
    prefixes = set()
    for source in qt_sources:
        parts = source.resolve().parts
        try:
            index = parts.index("Cellar")
        except ValueError as error:
            raise RuntimeError(f"selected Qt source is outside a formula prefix: {source}") from error
        if len(parts) <= index + 2 or parts[index + 1] != "qt":
            raise RuntimeError(f"selected Qt source is not from the Qt formula: {source}")
        prefixes.add(Path(*parts[:index + 3]))
    if len(prefixes) != 1:
        raise RuntimeError(f"selected Qt sources span formula prefixes: {sorted(map(str, prefixes))}")
    prefix = prefixes.pop()
    if prefix.name != qt_version:
        raise RuntimeError(f"selected Qt source version {prefix.name} does not match Qt tools {qt_version}")
    return prefix


def deploy_verified_snapshots(app: Path, package_root: Path, provenance: dict[str, dict[str, object]]) -> None:
    expected = set(provenance)
    existing = {path.relative_to(app).as_posix() for path in app.rglob("*") if is_macho(path)}
    if existing != expected:
        raise RuntimeError(f"scaffold Mach-O set differs from snapshots: missing={sorted(expected-existing)}, extra={sorted(existing-expected)}")
    app_real = app.resolve()
    for relative, entry in provenance.items():
        destination = app / relative
        if destination.is_symlink() or not destination.is_file():
            raise RuntimeError(f"snapshot destination is not a regular file: {relative}")
        try:
            destination.resolve().relative_to(app_real)
        except ValueError as error:
            raise RuntimeError(f"snapshot destination escapes bundle: {relative}") from error
        snapshot = package_root / entry["staged_path"]
        if snapshot.is_symlink() or not snapshot.is_file():
            raise RuntimeError(f"snapshot is not a regular file: {relative}")
        if hashlib.sha256(snapshot.read_bytes()).hexdigest() != entry["staged_sha256"]:
            raise RuntimeError(f"snapshot changed before deployment: {relative}")
        destination.chmod(destination.stat().st_mode | stat.S_IWUSR)
        shutil.copy2(snapshot, destination)
    deployed = {path.relative_to(app).as_posix() for path in app.rglob("*") if is_macho(path)}
    if deployed != expected:
        raise RuntimeError(f"raw deployed Mach-O set differs from snapshots: missing={sorted(expected-deployed)}, extra={sorted(deployed-expected)}")
    for relative, entry in provenance.items():
        digest = hashlib.sha256((app / relative).read_bytes()).hexdigest()
        if digest != entry["source_sha256"] or digest != entry["staged_sha256"]:
            raise RuntimeError(f"raw deployed binary differs from verified snapshot: {relative}")
        entry["deployed_raw_sha256"] = digest


def normalize_recorded_load_paths(app: Path, provenance: dict[str, dict[str, object]]) -> None:
    for relative, entry in provenance.items():
        binary = app / relative
        _, dependencies, rpaths, _ = parse_load_commands(run("/usr/bin/otool", "-l", str(binary)))
        non_system = {dependency for dependency in dependencies if not dependency.startswith(("/System/Library/", "/usr/lib/"))}
        edges = entry["dependency_edges"]
        if non_system != set(edges):
            raise RuntimeError(f"recorded dependency edges differ from raw binary: {relative}")
        for dependency, target_relative in edges.items():
            target = app / target_relative
            if target_relative not in provenance or not target.is_file():
                raise RuntimeError(f"recorded dependency target is absent: {relative}: {target_relative}")
            replacement = "@loader_path/" + os.path.relpath(target, binary.parent)
            run("/usr/bin/install_name_tool", "-change", dependency, replacement, str(binary))
        for rpath in rpaths:
            run("/usr/bin/install_name_tool", "-delete_rpath", rpath, str(binary))


def resolve_source_load(binary: Path, dependency: str, rpaths: tuple[str, ...], executable: Path) -> Path:
    candidates = []
    if dependency.startswith("@rpath/"):
        suffix = dependency.removeprefix("@rpath/")
        for rpath in rpaths:
            base = rpath.replace("@loader_path", str(binary.parent)).replace("@executable_path", str(executable.parent))
            candidates.append(Path(base) / suffix)
    elif dependency.startswith("@loader_path/"):
        candidates.append(binary.parent / dependency.removeprefix("@loader_path/"))
    elif dependency.startswith("@executable_path/"):
        candidates.append(executable.parent / dependency.removeprefix("@executable_path/"))
    elif dependency.startswith("/"):
        candidates.append(Path(dependency))
    resolved = {candidate.resolve() for candidate in candidates if candidate.exists()}
    if len(resolved) != 1:
        raise RuntimeError(f"dependency does not resolve to one source: {binary}: {dependency}: {sorted(map(str, resolved))}")
    return resolved.pop()


def deployment_destination(source: Path, app_source: Path, plugins: dict[Path, str]) -> str:
    if source == app_source.resolve():
        return "Contents/MacOS/Safeparts"
    if source in plugins:
        return "Contents/PlugIns/" + plugins[source]
    framework_parts = list(source.parts)
    framework_index = next((index for index, part in enumerate(framework_parts) if part.endswith(".framework")), None)
    if framework_index is not None:
        return "Contents/Frameworks/" + "/".join(framework_parts[framework_index:])
    return "Contents/Frameworks/" + source.name


def dependency_destination(dependency: str) -> str:
    relative = dependency
    for prefix in ("@rpath/", "@loader_path/", "@executable_path/"):
        if relative.startswith(prefix):
            relative = relative.removeprefix(prefix)
            break
    parts = PurePosixPath(relative).parts
    framework_index = next((index for index, part in enumerate(parts) if part.endswith(".framework")), None)
    if framework_index is not None:
        return "Contents/Frameworks/" + "/".join(parts[framework_index:])
    return "Contents/Frameworks/" + PurePosixPath(relative).name


def collect_deployment_sources(app_source: Path, plugin_sources: dict[Path, str]) -> dict[str, dict[str, object]]:
    executable = app_source.resolve()
    plugins = {path.resolve(): relative for path, relative in plugin_sources.items()}
    pending = [(executable, "Contents/MacOS/Safeparts")]
    pending.extend((source, "Contents/PlugIns/" + relative) for source, relative in plugins.items())
    by_destination: dict[str, dict[str, object]] = {}
    visited = set()
    while pending:
        source, destination = pending.pop()
        source = source.resolve()
        identity = (source, destination)
        if identity in visited:
            continue
        visited.add(identity)
        previous = by_destination.get(destination)
        if previous is not None and previous["source"] != source:
            raise RuntimeError(f"ambiguous deployment destination {destination}: {previous['source']} and {source}")
        record = by_destination.setdefault(destination, {"source": source, "dependency_edges": {}})
        _, dependencies, rpaths, _ = parse_load_commands(run("/usr/bin/otool", "-l", str(source)))
        for dependency in dependencies:
            if dependency.startswith(("/System/Library/", "/usr/lib/")):
                continue
            resolved = resolve_source_load(source, dependency, rpaths, executable)
            target_destination = dependency_destination(dependency)
            record["dependency_edges"][dependency] = target_destination
            pending.append((resolved, target_destination))
    return by_destination


def source_formula(source: Path, selected_qt_prefix: Path) -> tuple[str, Path] | None:
    source = source.resolve()
    selected_qt_prefix = selected_qt_prefix.resolve()
    try:
        source.relative_to(selected_qt_prefix)
        return "qt", selected_qt_prefix
    except ValueError:
        pass
    cellar = Path("/opt/homebrew/Cellar")
    try:
        relative = source.relative_to(cellar)
    except ValueError:
        return None
    if len(relative.parts) < 3:
        return None
    prefix = cellar / relative.parts[0] / relative.parts[1]
    return relative.parts[0], prefix


def record_final_hashes(app: Path, provenance: dict[str, dict[str, str | None]]) -> None:
    for relative, entry in provenance.items():
        entry["final_sha256"] = hashlib.sha256((app / relative).read_bytes()).hexdigest()


def stage_source_provenance(sources: dict[str, dict[str, object]], stage: Path, selected_qt_prefix: Path) -> dict[str, dict[str, object]]:
    result = {}
    for destination, source_record in sorted(sources.items()):
        source = source_record["source"]
        staged = stage / destination
        staged.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, staged)
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        staged_hash = hashlib.sha256(staged.read_bytes()).hexdigest()
        if source_hash != staged_hash:
            raise RuntimeError(f"unmodified staged input differs from source: {source}")
        formula = source_formula(source, selected_qt_prefix)
        entry = {"source_path": str(source), "source_sha256": source_hash,
                 "staged_path": staged.relative_to(stage.parent).as_posix(), "staged_sha256": staged_hash,
                 "dependency_edges": source_record["dependency_edges"],
                 "formula": None, "formula_prefix": None, "formula_version": None,
                 "receipt_sha256": None, "sbom_sha256": None}
        if formula:
            name, prefix = formula
            receipt = prefix / "INSTALL_RECEIPT.json"
            sbom = prefix / "sbom.spdx.json"
            if not receipt.is_file():
                raise RuntimeError(f"source formula receipt is unavailable: {source}")
            entry.update({"formula": name, "formula_prefix": str(prefix), "formula_version": prefix.name,
                          "receipt_sha256": hashlib.sha256(receipt.read_bytes()).hexdigest(),
                          "sbom_sha256": hashlib.sha256(sbom.read_bytes()).hexdigest() if sbom.is_file() else None})
        result[destination] = entry
    return result


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
    generated_dir = build_dir / "generated"
    run(cmake, "-S", str(repo / "apps/desktop"), "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON", "-DCMAKE_OSX_ARCHITECTURES=arm64",
        f"-DDESKTOP_RUST_TARGET_DIR={rust_target}", f"-DDESKTOP_GENERATED_DIR={generated_dir}")
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
    qt_version = run(str(qtpaths), "--qt-version")
    plugin_sources = {qt_plugins / relative: relative for relative in ("platforms/libqcocoa.dylib", "styles/libqmacstyle.dylib")}
    deployment_sources = collect_deployment_sources(build_dir / "Safeparts.app/Contents/MacOS/Safeparts", plugin_sources)
    selected_qt_prefix = derive_selected_qt_prefix(deployment_sources, qt_version)
    binary_provenance = stage_source_provenance(deployment_sources, output_dir / "provenance-inputs", selected_qt_prefix)
    run(str(macdeployqt), str(app), "-always-overwrite", "-no-plugins", "-verbose=1")
    frameworks = app / "Contents/Frameworks"
    qt_dbus = frameworks / "QtDBus.framework"
    if not qt_dbus.exists():
        shutil.copytree(qt_libs / "QtDBus.framework", qt_dbus, symlinks=True)
    dbus_relative = next(relative for relative in binary_provenance if relative.startswith("Contents/Frameworks/libdbus-"))
    dbus_target = app / dbus_relative
    shutil.copy2(output_dir / binary_provenance[dbus_relative]["staged_path"], dbus_target)
    plugins = app / "Contents/PlugIns"
    for relative in ("platforms/libqcocoa.dylib", "styles/libqmacstyle.dylib"):
        destination = plugins / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(output_dir / binary_provenance[f"Contents/PlugIns/{relative}"]["staged_path"], destination)
    deploy_verified_snapshots(app, output_dir, binary_provenance)
    normalize_recorded_load_paths(app, binary_provenance)
    for binary in sorted(app.rglob("*")):
        if is_macho(binary):
            run("/usr/bin/codesign", "--force", "--sign", "-", str(binary))
    resources = app / "Contents/Resources"
    license_dir = resources / "licenses"
    license_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(repo / "LICENSE", license_dir / "Safeparts-MIT.txt")
    (resources / "qt.conf").write_text("[Paths]\nPrefix = ..\nPlugins = PlugIns\n")
    macho = inspect_bundle(app)
    final_paths = {record["path"] for record in macho}
    if final_paths != set(binary_provenance):
        raise RuntimeError(f"deployed closure differs from proven source closure: missing={sorted(set(binary_provenance) - final_paths)}, unproven={sorted(final_paths - set(binary_provenance))}")
    for relative, provenance in binary_provenance.items():
        source = Path(provenance["source_path"])
        staged = output_dir / provenance["staged_path"]
        if hashlib.sha256(source.read_bytes()).hexdigest() != provenance["source_sha256"]:
            raise RuntimeError(f"deployment source changed during packaging: {source}")
        if hashlib.sha256(staged.read_bytes()).hexdigest() != provenance["staged_sha256"]:
            raise RuntimeError(f"staged deployment input changed during packaging: {staged}")
    run("/usr/bin/codesign", "--force", "--sign", "-", str(app))
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app))
    record_final_hashes(app, binary_provenance)
    with (app / "Contents/Info.plist").open("rb") as handle:
        plist = plistlib.load(handle)
    source_commit = run("/usr/bin/git", "rev-parse", "HEAD", cwd=repo)
    manifest = {
        "artifact": "local engineering material; not a release or clean-host qualification",
        "source": {"commit": source_commit, "tree": "clean"},
        "build": {"provenance": "package-owned CMake, Cargo, and generated-CXX directories recreated before configuration",
                  "cmake_cache_sha256": hashlib.sha256(cache_path.read_bytes()).hexdigest(),
                  "installed_executable_sha256": hashlib.sha256((app / "Contents/MacOS/Safeparts").read_bytes()).hexdigest(),
                  "binary_inputs": binary_provenance},
        "bundle_identifier": plist["CFBundleIdentifier"], "deployment_target": plist["LSMinimumSystemVersion"],
        "toolchain": {"rust": run("mise", "exec", "--", "rustc", "--version", cwd=repo),
                      "cmake": run(cmake, "--version").splitlines()[0],
                      "xcode": run("/usr/bin/xcodebuild", "-version").replace("\n", "; "),
                      "sdk": run("/usr/bin/xcrun", "--show-sdk-version"),
                      "qt": qt_version, "cxx": "1.0.195 (Cargo.lock and generated bridge command)"},
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
