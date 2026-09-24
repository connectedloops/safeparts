#!/usr/bin/env python3
"""Assemble offline license/provenance material for the deployed macOS closure."""
from __future__ import annotations
import argparse, hashlib, json, os
from pathlib import Path, PurePosixPath
import shutil, subprocess

CACHE_MANIFEST_SHA256 = "95ba11ba6e8fdb85487cebfdf17252c6fb623d4233c6c1c705f8b91d6442f376"
OWNER = ".safeparts-desktop-license-material"
COMPONENTS = {
    "Qt": "qt", "libb2": "libb2", "libdbus": "dbus", "libdouble-conversion": "double-conversion",
    "libfreetype": "freetype", "libglib": "glib", "libgthread": "glib", "libgraphite2": "graphite2",
    "libharfbuzz": "harfbuzz", "libicu": "icu4c@77", "libintl": "gettext", "libmd4c": "md4c",
    "libpcre2": "pcre2", "libpng": "libpng", "libzstd": "zstd",
}


def sha(path: Path) -> str: return hashlib.sha256(path.read_bytes()).hexdigest()
def run(*args: str, cwd: Path | None = None) -> str:
    return subprocess.run(args, cwd=cwd, check=True, text=True, stdout=subprocess.PIPE).stdout.strip()


def owned_output(repo: Path, output: Path) -> None:
    target = (repo / "target").resolve()
    try: rel = output.relative_to(target)
    except ValueError as error: raise RuntimeError("output must be inside repository target") from error
    if not rel.parts: raise RuntimeError("output must be a proper target child")
    marker = output / OWNER
    if output.exists():
        if not marker.is_file() or marker.read_text() != "owned license material\n":
            raise RuntimeError(f"refusing to delete unowned output: {output}")
        shutil.rmtree(output)
    output.mkdir(parents=True); marker.write_text("owned license material\n")


def component_for(path: str) -> str:
    name = PurePosixPath(path).name
    if path.startswith("Contents/Frameworks/Qt") or path.startswith("Contents/PlugIns/"): return "qt"
    if path == "Contents/MacOS/Safeparts": return "safeparts"
    for prefix, component in COMPONENTS.items():
        if name.startswith(prefix): return component
    raise RuntimeError(f"unmapped deployed binary: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True); parser.add_argument("--cache-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True); parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args(); repo=args.repo_root.resolve(); package=args.package_dir.resolve(); cache=args.cache_dir.resolve(); output=args.output_dir.resolve()
    if run("/usr/bin/git", "status", "--porcelain", cwd=repo): raise SystemExit("refusing material from dirty source tree")
    if sha(cache / "SHA256SUMS") != CACHE_MANIFEST_SHA256: raise SystemExit("license cache manifest identity mismatch")
    for line in (cache / "SHA256SUMS").read_text().splitlines():
        digest, relative = line.split("  ", 1); path = cache / relative.removeprefix("./")
        if not path.is_file() or sha(path) != digest: raise SystemExit(f"license cache file mismatch: {relative}")
    manifest=json.loads((package/"manifest.json").read_text()); owned_output(repo, output)
    notices=output/"notices"; notices.mkdir(); shutil.copytree(cache/"qtbase-6.9.1/LICENSES", notices/"qtbase-LICENSES")
    shutil.copy2(repo/"LICENSE", notices/"Safeparts-MIT.txt")
    qt_prefix=Path(run("brew","--prefix","qt")); receipts=output/"receipts"; receipts.mkdir()
    for name, source in (("qt-INSTALL_RECEIPT.json",qt_prefix/"INSTALL_RECEIPT.json"),("qt-homebrew-sbom.spdx.json",qt_prefix/"sbom.spdx.json"),("qtbase-6.9.1.spdx",qt_prefix/"share/qt/sbom/qtbase-6.9.1.spdx"),("qt.rb",qt_prefix/"qt.rb")):
        shutil.copy2(source, receipts/name)
    cxx=next((Path(os.environ.get("CARGO_HOME",Path.home()/".cargo"))/"registry/src").glob("*/cxx-1.0.195"))
    for name in ("LICENSE-APACHE","LICENSE-MIT"): shutil.copy2(cxx/name, notices/f"cxx-1.0.195-{name}.txt")
    deployed=[]; components={}
    for binary in manifest["macho"]:
        component=component_for(binary["path"]); deployed.append({"path":binary["path"],"component":component})
        if component in ("safeparts","qt") or component in components: continue
        prefix=Path(run("brew","--prefix",component)).resolve(); receipt=prefix/"INSTALL_RECEIPT.json"; sbom=prefix/"sbom.spdx.json"
        version=prefix.name; components[component]={"version":version,"origin":"Homebrew installed prefix","receipt_sha256":sha(receipt),
            "sbom_sha256":sha(sbom) if sbom.exists() else None,"license_text_status":"missing from installed prefix; exact upstream text not yet cached"}
        shutil.copy2(receipt,receipts/f"{component}-INSTALL_RECEIPT.json")
        if sbom.exists(): shutil.copy2(sbom,receipts/f"{component}-sbom.spdx.json")
    components["qt"]={"version":"6.9.1","origin":"Homebrew bottle and installed Qt SPDX","license_text_status":"Qtbase license set cached and verified; component copyright/third-party inventory in qtbase SPDX"}
    components["safeparts"]={"version":manifest["source"]["commit"],"license":"MIT","license_text_status":"included"}
    metadata=json.loads(run("mise","exec","--","cargo","metadata","--locked","--offline","--format-version","1",cwd=repo))
    packages={p["id"]:p for p in metadata["packages"]}; root=next(p["id"] for p in metadata["packages"] if p["name"]=="safeparts_desktop_bridge")
    edges={n["id"]:[d["pkg"] for d in n["deps"]] for n in metadata["resolve"]["nodes"]}; seen=set(); todo=[root]
    while todo:
        item=todo.pop()
        if item in seen: continue
        seen.add(item); todo.extend(edges.get(item,()))
    rust=[{"name":packages[i]["name"],"version":packages[i]["version"],"license":packages[i]["license"],"source":packages[i]["source"]} for i in sorted(seen)]
    inventory={"scope":"actual local deployed Mach-O closure plus statically linked locked Rust graph","source_commit":manifest["source"]["commit"],
      "package_manifest_sha256":sha(package/"manifest.json"),"deployed":deployed,"components":dict(sorted(components.items())),"rust":rust,
      "material_status":{"distribution_ready":False,"present":["Safeparts MIT text","CXX 1.0.195 MIT/Apache texts","Qtbase 6.9.1 license set","installed build receipts and SPDX"],
      "missing":["exact license texts/notices for non-Qt Homebrew dylibs","durable corresponding Qt 6.9.1 source archive","complete build configuration and modifications","reviewed distribution replacement/relinking procedure"]}}
    (output/"inventory.json").write_text(json.dumps(inventory,indent=2,sort_keys=True)+"\n")
    files=[]
    for path in sorted(output.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS": files.append(f"{sha(path)}  {path.relative_to(output).as_posix()}")
    (output/"SHA256SUMS").write_text("\n".join(files)+"\n"); print(output)
    return 0
if __name__=="__main__": raise SystemExit(main())
