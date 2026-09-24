#!/usr/bin/env python3
"""Drive representative synthetic workflows through the packaged macOS UI."""
from __future__ import annotations
import argparse, hashlib, importlib.util, json, os
from pathlib import Path
import shutil, subprocess, sys, time


def load(name: str):
    path=Path(__file__).with_name(name+".py"); spec=importlib.util.spec_from_file_location(name,path); module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module);return module
SMOKE=load("smoke_macos_package")
MARKER=".safeparts-desktop-workflows"

def sha(data: bytes)->str: return hashlib.sha256(data).hexdigest()
def run(*args: str, timeout: float=15)->str: return subprocess.run(args,check=True,text=True,capture_output=True,timeout=timeout).stdout.strip()
def wait_get(helper: Path,pid:int,role:str,title:str,occurrence:int=0,timeout:float=15)->str:
    deadline=time.monotonic()+timeout; last=""
    while time.monotonic()<deadline:
        result=subprocess.run([str(helper),"get",str(pid),role,title,str(occurrence)],text=True,capture_output=True)
        if result.returncode==0 and result.stdout: return result.stdout
        last=result.stderr; time.sleep(.1)
    raise RuntimeError(f"timed out waiting for {role} {title}: {last}")
def action(helper:Path,pid:int,command:str,role:str,title:str,occurrence:int=0,value:str|None=None)->str:
    args=[str(helper),command,str(pid),role,title,str(occurrence)]
    if value is not None: args.append(value)
    return run(*args)
def prepare(repo:Path,evidence:Path,package:Path):
    target=(repo/"target").resolve(); evidence.relative_to(target)
    if evidence==target or SMOKE.overlaps(evidence,package): raise RuntimeError("unsafe workflow evidence path")
    marker=evidence/MARKER
    if evidence.exists():
        if not marker.is_file() or marker.read_text()!="owned workflow evidence\n": raise RuntimeError("refusing unowned workflow evidence")
        shutil.rmtree(evidence)
    evidence.mkdir(parents=True); marker.write_text("owned workflow evidence\n")

def main()->int:
    p=argparse.ArgumentParser();p.add_argument("--package-dir",type=Path,required=True);p.add_argument("--evidence-dir",type=Path,required=True);p.add_argument("--repo-root",type=Path,default=Path(__file__).resolve().parents[3]);a=p.parse_args()
    repo=a.repo_root.resolve();package=a.package_dir.resolve();evidence=a.evidence_dir.resolve()
    if run("/usr/bin/git","status","--porcelain"): raise SystemExit("dirty source tree")
    manifest=json.loads((package/"manifest.json").read_text());head=run("/usr/bin/git","rev-parse","HEAD")
    if manifest["source"]!={"commit":head,"tree":"clean"}: raise SystemExit("package source mismatch")
    SMOKE.verify_manifest(package,manifest);prepare(repo,evidence,package)
    app=evidence/"Safeparts.app";subprocess.run(["/usr/bin/ditto",str(package/"Safeparts.app"),str(app)],check=True);SMOKE.verify_manifest(evidence,manifest)
    helper=evidence/"ax-harness"; subprocess.run(["/usr/bin/xcrun","swiftc",str(repo/"apps/desktop/tests/ax_harness.swift"),"-o",str(helper),"-framework","ApplicationServices"],check=True)
    trust=subprocess.run([str(helper),"count","0","AXApplication","Safeparts","0"],capture_output=True)
    if trust.returncode==3: raise SystemExit("Accessibility permission is unavailable for the repository AX helper")
    env={k:v for k,v in os.environ.items() if not k.startswith(("DYLD_","QT_"))};env["DYLD_PRINT_LIBRARIES"]="1"
    stderr=(evidence/"loaded-images.log").open("wb");stdout=(evidence/"stdout.log").open("wb");process=subprocess.Popen([str(app/"Contents/MacOS/Safeparts")],env=env,stdout=stdout,stderr=stderr,start_new_session=True)
    results=[]
    try:
        time.sleep(2); pid=process.pid
        secret="Synthetic café مرحبا 👩🏽‍🔬\nline two"; expected=secret.encode()
        action(helper,pid,"type","AXTextArea","Secret",value=secret);action(helper,pid,"press","AXButton","Split")
        shares=[wait_get(helper,pid,"AXTextArea",f"Recovery share {i}") for i in (1,2)]
        action(helper,pid,"press","AXButton","Copy Recovery share 1"); time.sleep(.2); clipboard=run("/usr/bin/pbpaste").encode()
        if clipboard!=shares[0].encode(): raise RuntimeError("authoritative share Copy mismatch")
        action(helper,pid,"press","AXRadioButton","Combine");time.sleep(.3)
        for i,share in enumerate(shares,1): action(helper,pid,"type","AXTextArea",f"Recovery share {i}",value=share); time.sleep(.3)
        time.sleep(.5); action(helper,pid,"press","AXButton","Combine"); recovered=wait_get(helper,pid,"AXTextArea","Recovered secret")
        if recovered.encode()!=expected: raise RuntimeError("unprotected UTF-8 recovery mismatch")
        action(helper,pid,"press","AXButton","Copy recovered Secret"); time.sleep(.2); copied=run("/usr/bin/pbpaste").encode()
        if copied!=expected: raise RuntimeError("authoritative recovered Copy mismatch")
        results.append({"case":"words-unprotected-utf8-copy","status":"passed","secret_sha256":sha(expected),"share_count_used":2})
        action(helper,pid,"press","AXRadioButton","Split");action(helper,pid,"press","AXButton","Start over");time.sleep(.2)
        action(helper,pid,"type","AXTextArea","Secret",value="protected synthetic")
        action(helper,pid,"press","AXCheckBox","Protect with passphrase");time.sleep(.2)
        action(helper,pid,"type","AXTextArea","Passphrase input (contents hidden)",value="synthetic-pass")
        action(helper,pid,"type","AXTextArea","Passphrase confirmation (contents hidden)",value="synthetic-pass")
        action(helper,pid,"press","AXButton","Split");pshares=[wait_get(helper,pid,"AXTextArea",f"Recovery share {i}") for i in (1,2)]
        action(helper,pid,"press","AXRadioButton","Combine");time.sleep(.2)
        for i,share in enumerate(pshares,1): action(helper,pid,"type","AXTextArea",f"Recovery share {i}",value=share); time.sleep(.3)
        action(helper,pid,"type","AXTextArea","Passphrase input (contents hidden)",value="wrong-synthetic")
        action(helper,pid,"press","AXButton","Combine");time.sleep(3)
        if subprocess.run([str(helper),"count",str(pid),"AXTextArea","Recovered secret","0"],capture_output=True).returncode==0: raise RuntimeError("wrong passphrase exposed output")
        action(helper,pid,"type","AXTextArea","Passphrase input (contents hidden)",value="synthetic-pass")
        action(helper,pid,"press","AXButton","Combine");protected=wait_get(helper,pid,"AXTextArea","Recovered secret")
        if protected!="protected synthetic": raise RuntimeError("protected retry mismatch")
        results.append({"case":"words-protected-wrong-passphrase-retry","status":"passed","secret_sha256":sha(protected.encode())})
        action(helper,pid,"press","AXRadioButton","Split");action(helper,pid,"press","AXButton","Start over");time.sleep(.2)
        action(helper,pid,"press","AXButton","Choose file…");time.sleep(.5);action(helper,pid,"escape","AXApplication","Safeparts");time.sleep(.5)
        results.append({"case":"cancel-open-dialog","status":"passed"})
    finally:
        cleanup_error=None
        try: cleanup=SMOKE.cleanup_group(process.pid,evidence/"cleanup.json")
        except BaseException as error: cleanup_error=error
        process.wait(timeout=5);stdout.close();stderr.close()
        if cleanup_error: raise cleanup_error
    SMOKE.verify_manifest(evidence,manifest);SMOKE.verify_manifest(package,manifest)
    paths=SMOKE.loaded_paths((evidence/"loaded-images.log").read_text(errors="replace").splitlines());external=[x for x in paths if ("/opt/homebrew" in x or "/Qt" in x) and not SMOKE.contained(x,app)]
    if external: raise RuntimeError("external Qt/Homebrew image loaded")
    report={"scope":"packaged production UI through external macOS Accessibility; synthetic inputs only","source_commit":head,"manifest_sha256":sha((package/"manifest.json").read_bytes()),"cases":results,"loaded_images":len(paths),"external_qt_or_homebrew":0,"cleanup":cleanup,"clipboard":"replaced with synthetic recovered text; original clipboard was not read or saved","limitations":["Words encoding only","text workflows only; arbitrary binary file Save remains pending","no maximum workload or network/storage qualification"]}
    (evidence/"evidence.json").write_text(json.dumps(report,indent=2,sort_keys=True)+"\n");print(evidence/"evidence.json");return 0
if __name__=="__main__":raise SystemExit(main())
