#!/usr/bin/env python3
"""Assemble offline license/provenance material for the deployed macOS closure."""
from __future__ import annotations
import argparse, hashlib, importlib.util, json, os, sys
from pathlib import Path, PurePosixPath
import shutil, subprocess

CACHE_MANIFEST_SHA256 = "851cfcc05fd2eff53c5f7b5758cf7ea8f6633c3d99259ac31e7458d5fc8befde"
OWNER = ".safeparts-desktop-license-material"
COMPONENTS = {
    "libb2": "libb2", "libdbus": "dbus", "libdouble-conversion": "double-conversion",
    "libfreetype": "freetype", "libglib": "glib", "libgthread": "glib", "libgraphite2": "graphite2",
    "libharfbuzz": "harfbuzz", "libicu": "icu4c@77", "libintl": "gettext", "libmd4c": "md4c",
    "libpcre2": "pcre2", "libpng": "libpng", "libzstd": "zstd",
}
EXACT_QT_PLUGINS = {"Contents/PlugIns/platforms/libqcocoa.dylib", "Contents/PlugIns/styles/libqmacstyle.dylib"}


def load_sibling(name: str):
    path=Path(__file__).with_name(name+".py"); spec=importlib.util.spec_from_file_location(name,path); module=importlib.util.module_from_spec(spec)
    sys.modules[spec.name]=module; spec.loader.exec_module(module); return module
SMOKE=load_sibling("smoke_macos_package"); PACKAGE=load_sibling("package_macos")

def sha(path: Path) -> str: return hashlib.sha256(path.read_bytes()).hexdigest()
def run(*args: str, cwd: Path | None = None) -> str:
    return subprocess.run(args,cwd=cwd,check=True,text=True,stdout=subprocess.PIPE).stdout.strip()

def owned_output(repo: Path, output: Path, protected: tuple[Path,...]) -> None:
    target=(repo/"target").resolve()
    try: rel=output.relative_to(target)
    except ValueError as error: raise RuntimeError("output must be inside repository target") from error
    if not rel.parts: raise RuntimeError("output must be a proper target child")
    for path in protected:
        if SMOKE.overlaps(output,path.resolve()): raise RuntimeError(f"output overlaps protected input: {path}")
    marker=output/OWNER
    if output.exists():
        if not marker.is_file() or marker.read_text()!="owned license material\n": raise RuntimeError(f"refusing to delete unowned output: {output}")
        shutil.rmtree(output)
    output.mkdir(parents=True); marker.write_text("owned license material\n")

def cache_rows(cache: Path) -> dict[str,str]:
    manifest=cache/"SHA256SUMS"
    if sha(manifest)!=CACHE_MANIFEST_SHA256: raise RuntimeError("license cache manifest identity mismatch")
    rows={}
    for line in manifest.read_text().splitlines():
        if line.count("  ")!=1: raise RuntimeError("malformed license cache row")
        digest,original=line.split("  ",1)
        if len(digest)!=64 or any(c not in "0123456789abcdef" for c in digest): raise RuntimeError("malformed license cache digest")
        if not original.startswith("qtbase-6.9.1/LICENSES/") or original.startswith("/"):
            raise RuntimeError(f"unsafe license cache path: {original}")
        pure=PurePosixPath(original)
        if pure.as_posix()!=original or any(part in (".","..") for part in pure.parts): raise RuntimeError(f"non-canonical license cache path: {original}")
        if original in rows: raise RuntimeError(f"duplicate license cache path: {original}")
        rows[original]=digest
    actual={p.relative_to(cache).as_posix():"symlink" if p.is_symlink() else "file" for p in cache.rglob("*") if p.name!="SHA256SUMS" and (p.is_file() or p.is_symlink())}
    expected={path:"file" for path in rows}
    if actual!=expected: raise RuntimeError(f"license cache tree mismatch: expected={sorted(expected)}, actual={sorted(actual)}")
    for relative,digest in rows.items():
        if sha(cache/relative)!=digest: raise RuntimeError(f"license cache file mismatch: {relative}")
    return rows

def component_for(path: str) -> str:
    name=PurePosixPath(path).name
    if path.startswith("Contents/Frameworks/Qt") or path in EXACT_QT_PLUGINS: return "qt"
    if path.startswith("Contents/PlugIns/"): raise RuntimeError(f"unverified deployed plugin: {path}")
    if path=="Contents/MacOS/Safeparts": return "safeparts"
    matches={component for prefix,component in COMPONENTS.items() if name.startswith(prefix)}
    if len(matches)!=1: raise RuntimeError(f"unmapped deployed binary: {path}")
    return matches.pop()

def verified_package(package: Path, head: str) -> tuple[dict, str, list[dict[str, object]]]:
    manifest_path=package/"manifest.json"; manifest_hash=sha(manifest_path); manifest=json.loads(manifest_path.read_text())
    if manifest.get("source")!={"commit":head,"tree":"clean"}: raise RuntimeError("package source does not match clean HEAD")
    SMOKE.verify_manifest(package,manifest)
    fresh=PACKAGE.inspect_bundle(package/"Safeparts.app")
    if fresh!=manifest.get("macho"): raise RuntimeError("package Mach-O closure differs from manifest")
    return manifest,manifest_hash,fresh


def classify_dependencies(metadata: dict, root: str) -> tuple[set[str],set[str]]:
    packages={p["id"]:p for p in metadata["packages"]}; nodes={n["id"]:n for n in metadata["resolve"]["nodes"]}
    runtime=set(); host=set(); todo=[(root,"runtime")]
    while todo:
        item,scope=todo.pop()
        bucket=host if scope=="host" else runtime
        if item in bucket: continue
        bucket.add(item)
        for dep in nodes.get(item,{}).get("deps",[]):
            kinds={entry.get("kind") or "normal" for entry in dep.get("dep_kinds",[])}
            if kinds=={"dev"} or not kinds: continue
            next_scope="host" if scope=="host" or "build" in kinds or any(t.get("kind")==["proc-macro"] for t in packages[dep["pkg"]].get("targets",[])) else "runtime"
            if "normal" in kinds or "build" in kinds: todo.append((dep["pkg"],next_scope))
    runtime-=host
    return runtime,host

def main() -> int:
    parser=argparse.ArgumentParser(); parser.add_argument("--package-dir",type=Path,required=True); parser.add_argument("--cache-dir",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,required=True); parser.add_argument("--repo-root",type=Path,default=Path(__file__).resolve().parents[3]); args=parser.parse_args()
    repo=args.repo_root.resolve(); package=args.package_dir.resolve(); cache=args.cache_dir.resolve(); output=args.output_dir.resolve()
    if run("/usr/bin/git","status","--porcelain",cwd=repo): raise SystemExit("refusing material from dirty source tree")
    head=run("/usr/bin/git","rev-parse","HEAD",cwd=repo); protected=(package,cache,repo/"target/desktop-package-work",repo/"apps/desktop",repo/"crates")
    cache_rows(cache)
    manifest_path=package/"manifest.json"; manifest,manifest_hash,fresh=verified_package(package,head)
    build_inputs=manifest.get("build",{}).get("homebrew_inputs",{})
    for component in sorted({component_for(binary["path"]) for binary in fresh}-{ "safeparts" }):
        evidence=build_inputs.get(component)
        if not evidence: raise SystemExit(f"package lacks build-time provenance for {component}")
        prefix=Path(evidence["prefix"]); receipt=prefix/"INSTALL_RECEIPT.json"; sbom=prefix/"sbom.spdx.json"
        if prefix.name!=evidence["version"] or not receipt.is_file() or sha(receipt)!=evidence["receipt_sha256"]:
            raise SystemExit(f"installed receipt no longer matches packaged {component}")
        if evidence["sbom_sha256"] and (not sbom.is_file() or sha(sbom)!=evidence["sbom_sha256"]):
            raise SystemExit(f"installed SBOM no longer matches packaged {component}")
    owned_output(repo,output,protected)
    notices=output/"notices"; notices.mkdir(); shutil.copytree(cache/"qtbase-6.9.1/LICENSES",notices/"qtbase-LICENSES"); shutil.copy2(repo/"LICENSE",notices/"Safeparts-MIT.txt")
    receipts=output/"receipts"; receipts.mkdir()
    deployed=[]; components={}
    for binary in fresh:
        component=component_for(binary["path"]); deployed.append({"path":binary["path"],"component":component})
        if component=="safeparts" or component in components: continue
        evidence=build_inputs.get(component)
        if not evidence: raise SystemExit(f"package lacks build-time provenance for {component}")
        prefix=Path(evidence["prefix"])
        if prefix.name!=evidence["version"]: raise SystemExit(f"invalid package provenance version for {component}")
        receipt=prefix/"INSTALL_RECEIPT.json"; sbom=prefix/"sbom.spdx.json"
        if not receipt.is_file() or sha(receipt)!=evidence["receipt_sha256"]: raise SystemExit(f"installed receipt no longer matches packaged {component}")
        if evidence["sbom_sha256"] and (not sbom.is_file() or sha(sbom)!=evidence["sbom_sha256"]): raise SystemExit(f"installed SBOM no longer matches packaged {component}")
        components[component]={"version":evidence["version"],"origin":"package-recorded Homebrew build input","receipt_sha256":evidence["receipt_sha256"],"sbom_sha256":evidence["sbom_sha256"],"license_text_status":"Qtbase license set verified" if component=="qt" else "exact upstream text not yet cached"}
        shutil.copy2(receipt,receipts/f"{component}-INSTALL_RECEIPT.json")
        if sbom.is_file(): shutil.copy2(sbom,receipts/f"{component}-sbom.spdx.json")
        if component=="qt":
            for name,source in (("qtbase-6.9.1.spdx",prefix/"share/qt/sbom/qtbase-6.9.1.spdx"),("qt.rb",prefix/".brew/qt.rb")): shutil.copy2(source,receipts/name)
    cxx=next((Path(os.environ.get("CARGO_HOME",Path.home()/".cargo"))/"registry/src").glob("*/cxx-1.0.195"))
    for name in ("LICENSE-APACHE","LICENSE-MIT"): shutil.copy2(cxx/name,notices/f"cxx-1.0.195-{name}.txt")
    components["safeparts"]={"version":head,"license":"MIT","license_text_status":"included"}
    metadata=json.loads(run("mise","exec","--","cargo","metadata","--locked","--offline","--filter-platform","aarch64-apple-darwin","--format-version","1",cwd=repo))
    packages={p["id"]:p for p in metadata["packages"]}; root=next(p["id"] for p in metadata["packages"] if p["name"]=="safeparts_desktop_bridge")
    runtime_ids,host_ids=classify_dependencies(metadata,root)
    describe=lambda ids:[{"name":packages[i]["name"],"version":packages[i]["version"],"license":packages[i]["license"],"source":packages[i]["source"]} for i in sorted(ids)]
    inventory={"scope":"verified deployed Mach-O closure plus conservative target-filtered Cargo candidates; no Rust link-map proof","source_commit":head,"package_manifest_sha256":manifest_hash,
      "deployed":deployed,"components":dict(sorted(components.items())),"rust_runtime_candidates":describe(runtime_ids),"rust_host_build_candidates":describe(host_ids),
      "rust_inventory_limit":"Cargo metadata reachability is not evidence that every candidate was statically linked into the executable.",
      "material_status":{"distribution_ready":False,"present":["Safeparts MIT text","CXX 1.0.195 MIT/Apache texts","Qtbase 6.9.1 license set","package-bound installed receipts and SPDX"],
      "missing":["exact license texts/notices for non-Qt Homebrew dylibs","durable corresponding Qt 6.9.1 source archive","complete build configuration and modifications","reviewed distribution replacement/relinking procedure"]}}
    (output/"inventory.json").write_text(json.dumps(inventory,indent=2,sort_keys=True)+"\n")
    SMOKE.verify_manifest(package,manifest)
    if sha(manifest_path)!=manifest_hash or PACKAGE.inspect_bundle(package/"Safeparts.app")!=fresh: raise SystemExit("package identity changed during collection")
    files=[f"{sha(p)}  {p.relative_to(output).as_posix()}" for p in sorted(output.rglob("*")) if p.is_file() and p.name!="SHA256SUMS"]
    (output/"SHA256SUMS").write_text("\n".join(files)+"\n"); print(output); return 0
if __name__=="__main__": raise SystemExit(main())
