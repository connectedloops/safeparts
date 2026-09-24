#!/usr/bin/env python3
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

MODULE_PATH = Path(__file__).parents[1] / "scripts/package_macos.py"
SPEC = importlib.util.spec_from_file_location("package_macos", MODULE_PATH)
PACKAGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PACKAGE
SPEC.loader.exec_module(PACKAGE)
R = PACKAGE.MachORecord
SMOKE_PATH = Path(__file__).parents[1] / "scripts/smoke_macos_package.py"
SMOKE_SPEC = importlib.util.spec_from_file_location("smoke_macos_package", SMOKE_PATH)
SMOKE = importlib.util.module_from_spec(SMOKE_SPEC)
sys.modules[SMOKE_SPEC.name] = SMOKE
SMOKE_SPEC.loader.exec_module(SMOKE)
LICENSE_PATH = Path(__file__).parents[1] / "scripts/package_license_material.py"
LICENSE_SPEC = importlib.util.spec_from_file_location("package_license_material", LICENSE_PATH)
LICENSE = importlib.util.module_from_spec(LICENSE_SPEC)
sys.modules[LICENSE_SPEC.name] = LICENSE
LICENSE_SPEC.loader.exec_module(LICENSE)
WORKFLOW_PATH = Path(__file__).parents[1] / "scripts/run_package_workflows.py"
WORKFLOW_SPEC = importlib.util.spec_from_file_location("run_package_workflows", WORKFLOW_PATH)
WORKFLOW = importlib.util.module_from_spec(WORKFLOW_SPEC)
sys.modules[WORKFLOW_SPEC.name] = WORKFLOW
WORKFLOW_SPEC.loader.exec_module(WORKFLOW)


class PackageManifestTests(unittest.TestCase):
    def test_tree_hashes_are_sorted_and_content_addressed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "z").write_bytes(b"z")
            (root / "a").write_bytes(b"a")
            (root / "link").symlink_to("a")
            rows = PACKAGE.tree_hashes(root)
            self.assertEqual([row["path"] for row in rows], ["a", "link", "z"])
            self.assertEqual(rows[0]["sha256"], "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb")
            self.assertEqual(rows[1], {"path": "link", "type": "symlink", "target": "a"})

    def fixture(self):
        temporary = tempfile.TemporaryDirectory()
        app = Path(temporary.name) / "Safeparts.app"
        executable = app / "Contents/MacOS/Safeparts"
        library = app / "Contents/Frameworks/libok.dylib"
        plugin = app / "Contents/PlugIns/platforms/libqcocoa.dylib"
        for path in (executable, library, plugin):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")
        records = [
            R("Contents/MacOS/Safeparts", ("arm64",), "15.5", None,
              ("@loader_path/../Frameworks/libok.dylib", "/usr/lib/libSystem.B.dylib"), ()),
            R("Contents/Frameworks/libok.dylib", ("arm64",), "14.0", "@rpath/libok.dylib", (), ()),
            R("Contents/PlugIns/platforms/libqcocoa.dylib", ("arm64",), "15.0", None,
              ("@loader_path/../../Frameworks/libok.dylib",), ()),
        ]
        return temporary, app, records

    def test_valid_relocated_closure_and_deployment_inventory(self):
        temporary, app, records = self.fixture()
        with temporary:
            self.assertEqual(PACKAGE.validate_records(app, records), [])
            self.assertEqual({record.minimum_macos for record in records}, {"14.0", "15.0", "15.5"})

    def test_rejects_escaping_plugin_runpath(self):
        temporary, app, records = self.fixture()
        with temporary:
            records[2] = R(records[2].path, ("arm64",), "15.0", None, (), ("@loader_path/../../../../lib",))
            self.assertTrue(any("runpath escapes bundle" in failure for failure in PACKAGE.validate_records(app, records)))

    def test_rejects_missing_and_unresolved_dependencies(self):
        temporary, app, records = self.fixture()
        with temporary:
            records[0] = R(records[0].path, ("arm64",), "15.5", None,
                           ("@loader_path/../Frameworks/missing.dylib", "@rpath/other.dylib"), ())
            failures = PACKAGE.validate_records(app, records)
            self.assertTrue(any("missing dependency" in failure for failure in failures))
            self.assertTrue(any("unresolved @rpath" in failure for failure in failures))

    def test_rejects_wrong_architecture_and_network_runtime(self):
        temporary, app, records = self.fixture()
        with temporary:
            network = app / "Contents/Frameworks/QtNetwork.framework/Versions/A/QtNetwork"
            network.parent.mkdir(parents=True)
            network.write_bytes(b"fixture")
            records.append(R(network.relative_to(app).as_posix(), ("x86_64",), "15.5", "@rpath/QtNetwork", (), ()))
            failures = PACKAGE.validate_records(app, records)
            self.assertTrue(any("unexpected architectures" in failure for failure in failures))
            self.assertTrue(any("network-capable runtime" in failure for failure in failures))

    def test_rejects_symlink_dependency_escape(self):
        temporary, app, records = self.fixture()
        with temporary, tempfile.TemporaryDirectory() as outside:
            escaped = app / "Contents/Frameworks/escaped.dylib"
            escaped.symlink_to(Path(outside) / "escaped.dylib")
            (Path(outside) / "escaped.dylib").write_bytes(b"fixture")
            records[0] = R(records[0].path, ("arm64",), "15.5", None,
                           ("@loader_path/../Frameworks/escaped.dylib",), ())
            self.assertTrue(any("dependency escapes bundle" in failure for failure in PACKAGE.validate_records(app, records)))

    def test_output_cleanup_requires_owned_target_staging_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            target = repo / "target"
            unowned = target / "package"
            unowned.mkdir(parents=True)
            sentinel = unowned / "keep.txt"
            sentinel.write_text("keep")
            with self.assertRaisesRegex(RuntimeError, "unowned"):
                PACKAGE.prepare_output(repo, unowned.resolve())
            self.assertEqual(sentinel.read_text(), "keep")
            owned = target / "owned"
            PACKAGE.prepare_output(repo, owned.resolve())
            (owned / "stale-object.o").write_text("built from another checkout")
            PACKAGE.prepare_output(repo, owned.resolve())
            self.assertFalse((owned / "stale-object.o").exists())
            self.assertTrue((owned / PACKAGE.OWNER_MARKER).is_file())
            with self.assertRaisesRegex(RuntimeError, "inside repository target"):
                PACKAGE.prepare_output(repo, (repo / "elsewhere").resolve())

    def test_parser_distinguishes_install_id_from_dependencies(self):
        text = """Load command 0
          cmd LC_ID_DYLIB
         name @rpath/libself.dylib (offset 24)
Load command 1
          cmd LC_LOAD_DYLIB
         name @loader_path/libdep.dylib (offset 24)
Load command 2
          cmd LC_BUILD_VERSION
      cmdsize 32
     platform 1
        minos 15.5
"""
        install_id, dependencies, rpaths, minimum = PACKAGE.parse_load_commands(text)
        self.assertEqual(install_id, "@rpath/libself.dylib")
        self.assertEqual(dependencies, ("@loader_path/libdep.dylib",))
        self.assertEqual(rpaths, ())
        self.assertEqual(minimum, "15.5")

    def test_loaded_image_parser_ignores_non_image_dyld_messages(self):
        lines = [
            "dyld[12]: <UUID> /tmp/Safeparts.app/Contents/Frameworks/QtCore",
            "dyld[12]: move loaded to delayed: NetworkExtension",
            "ordinary output",
        ]
        self.assertEqual(SMOKE.loaded_paths(lines), ["/tmp/Safeparts.app/Contents/Frameworks/QtCore"])

    def test_smoke_cleanup_refuses_unowned_or_overlapping_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            package = repo / "target/package"
            package.mkdir(parents=True)
            evidence = repo / "target/evidence"
            evidence.mkdir(parents=True)
            sentinel = evidence / "keep"
            sentinel.write_text("keep")
            with self.assertRaisesRegex(RuntimeError, "unowned"):
                SMOKE.prepare_evidence(repo, evidence.resolve(), (package.resolve(),))
            self.assertTrue(sentinel.exists())
            with self.assertRaisesRegex(RuntimeError, "proper child"):
                SMOKE.prepare_evidence(repo, (repo / "target").resolve())
            with self.assertRaisesRegex(RuntimeError, "overlaps"):
                SMOKE.prepare_evidence(repo, package.resolve(), (package.resolve(),))

    def test_manifest_requires_exact_safe_typed_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            app = package / "Safeparts.app"
            file = app / "Contents/MacOS/Safeparts"
            file.parent.mkdir(parents=True)
            file.write_bytes(b"app")
            manifest = {"files": [{"path": "Contents/MacOS/Safeparts", "type": "file",
                                    "sha256": SMOKE.sha256(file)}]}
            SMOKE.verify_manifest(package, manifest)
            extra = app / "extra"
            extra.write_text("extra")
            with self.assertRaisesRegex(RuntimeError, "extra"):
                SMOKE.verify_manifest(package, manifest)
            extra.unlink()
            file.unlink()
            file.symlink_to("elsewhere")
            with self.assertRaisesRegex(RuntimeError, "changed_type"):
                SMOKE.verify_manifest(package, manifest)
            bad_rows = ["../escape", "/absolute", "Contents/../escape"]
            for path in bad_rows:
                with self.assertRaisesRegex(RuntimeError, "unsafe"):
                    SMOKE.manifest_rows({"files": [{"path": path, "type": "file", "sha256": "x"}]})
            with self.assertRaisesRegex(RuntimeError, "duplicate"):
                SMOKE.manifest_rows({"files": [manifest["files"][0], manifest["files"][0]]})
            with self.assertRaisesRegex(RuntimeError, "malformed"):
                SMOKE.manifest_rows({"files": [{"path": "x", "type": "directory", "target": "y"}]})
            for path in ("Contents//MacOS/Safeparts", "./Contents/MacOS/Safeparts"):
                with self.assertRaisesRegex(RuntimeError, "non-canonical"):
                    SMOKE.manifest_rows({"files": [{"path": path, "type": "file", "sha256": "x"}]})

    def test_license_inventory_maps_only_verified_closure_names(self):
        self.assertEqual(LICENSE.component_for("Contents/Frameworks/QtCore.framework/Versions/A/QtCore"), "qt")
        self.assertEqual(LICENSE.component_for("Contents/PlugIns/styles/libqmacstyle.dylib"), "qt")
        self.assertEqual(LICENSE.component_for("Contents/Frameworks/libglib-2.0.0.dylib"), "glib")
        self.assertEqual(LICENSE.component_for("Contents/MacOS/Safeparts"), "safeparts")
        with self.assertRaisesRegex(RuntimeError, "unverified deployed plugin"):
            LICENSE.component_for("Contents/PlugIns/other/libunknown.dylib")
        with self.assertRaisesRegex(RuntimeError, "unmapped"):
            LICENSE.component_for("Contents/Frameworks/libunknown.dylib")

    def test_license_output_refuses_unowned_root_and_overlapping_inputs_without_deletion(self):
        with tempfile.TemporaryDirectory() as directory:
            repo=Path(directory); unowned=repo/"target/material"; unowned.mkdir(parents=True)
            sentinel=unowned/"keep"; sentinel.write_text("keep")
            with self.assertRaisesRegex(RuntimeError,"unowned"):
                LICENSE.owned_output(repo,unowned.resolve(),())
            self.assertTrue(sentinel.exists())
            with self.assertRaisesRegex(RuntimeError,"proper"):
                LICENSE.owned_output(repo,(repo/"target").resolve(),())
            protected=repo/"target/package"; protected.mkdir()
            marker=protected/LICENSE.OWNER; marker.write_text("owned license material\n")
            sentinel=protected/"package"; sentinel.write_text("keep")
            with self.assertRaisesRegex(RuntimeError,"overlaps"):
                LICENSE.owned_output(repo,protected.resolve(),(protected.resolve(),))
            self.assertTrue(sentinel.exists())

    def test_license_cache_requires_exact_safe_regular_file_set(self):
        with tempfile.TemporaryDirectory() as directory:
            cache=Path(directory); licenses=cache/"qtbase-6.9.1/LICENSES"; licenses.mkdir(parents=True)
            file=licenses/"MIT.txt"; file.write_text("license")
            def write_manifest(lines):
                manifest=cache/"SHA256SUMS"; manifest.write_text("\n".join(lines)+"\n")
                LICENSE.CACHE_MANIFEST_SHA256=LICENSE.sha(manifest)
            row=f"{LICENSE.sha(file)}  qtbase-6.9.1/LICENSES/MIT.txt"
            write_manifest([row]); self.assertEqual(len(LICENSE.cache_rows(cache)),1)
            extra=licenses/"extra"; extra.write_text("extra")
            with self.assertRaisesRegex(RuntimeError,"tree mismatch"): LICENSE.cache_rows(cache)
            extra.unlink(); file.unlink(); file.symlink_to("missing")
            with self.assertRaisesRegex(RuntimeError,"tree mismatch"): LICENSE.cache_rows(cache)
            file.unlink(); file.write_text("license")
            for bad in ("../escape", "/absolute", "qtbase-6.9.1/LICENSES/../x", "qtbase-6.9.1/LICENSES//MIT.txt"):
                write_manifest([f"{'0'*64}  {bad}"])
                with self.assertRaisesRegex(RuntimeError,"unsafe|non-canonical"): LICENSE.cache_rows(cache)
            write_manifest([row,row])
            with self.assertRaisesRegex(RuntimeError,"duplicate"): LICENSE.cache_rows(cache)
            write_manifest(["malformed"])
            with self.assertRaisesRegex(RuntimeError,"malformed"): LICENSE.cache_rows(cache)

    def test_license_package_binding_rejects_stale_source_modified_tree_and_closure(self):
        with tempfile.TemporaryDirectory() as directory:
            package=Path(directory); app=package/"Safeparts.app"; file=app/"Contents/MacOS/Safeparts"
            file.parent.mkdir(parents=True); file.write_bytes(b"app")
            row={"path":"Contents/MacOS/Safeparts","type":"file","sha256":SMOKE.sha256(file)}
            record={"path":"Contents/MacOS/Safeparts","architectures":["arm64"],"minimum_macos":"15.5","install_id":None,"dependencies":[],"rpaths":[]}
            manifest={"source":{"commit":"head","tree":"clean"},"files":[row],"macho":[record]}
            (package/"manifest.json").write_text(json.dumps(manifest))
            with mock.patch.object(LICENSE.PACKAGE,"inspect_bundle",return_value=[record]):
                LICENSE.verified_package(package,"head")
                with self.assertRaisesRegex(RuntimeError,"clean HEAD"): LICENSE.verified_package(package,"other")
                file.write_bytes(b"changed")
                with self.assertRaisesRegex(RuntimeError,"hash mismatch"): LICENSE.verified_package(package,"head")
                file.write_bytes(b"app")
            with mock.patch.object(LICENSE.PACKAGE,"inspect_bundle",return_value=[]):
                with self.assertRaisesRegex(RuntimeError,"closure differs"): LICENSE.verified_package(package,"head")

    def test_rust_inventory_filters_dev_and_separates_host_build_candidates(self):
        packages=[]
        for name,kind in (("root",["lib"]),("runtime",["lib"]),("builder",["lib"]),("macro",["proc-macro"]),("dev",["lib"]),("mixed",["lib"]),("shared",["lib"])):
            packages.append({"id":name,"name":name,"targets":[{"kind":kind}]})
        nodes=[{"id":"root","deps":[
            {"pkg":"runtime","dep_kinds":[{"kind":None,"target":None}]},
            {"pkg":"builder","dep_kinds":[{"kind":"build","target":None}]},
            {"pkg":"macro","dep_kinds":[{"kind":None,"target":None}]},
            {"pkg":"dev","dep_kinds":[{"kind":"dev","target":None}]},
            {"pkg":"mixed","dep_kinds":[{"kind":None,"target":None},{"kind":"build","target":None}]}]},
            {"id":"runtime","deps":[{"pkg":"shared","dep_kinds":[{"kind":None,"target":None}]}]},
            {"id":"builder","deps":[{"pkg":"shared","dep_kinds":[{"kind":None,"target":None}]}]},
            {"id":"macro","deps":[]},{"id":"dev","deps":[]},{"id":"mixed","deps":[]},{"id":"shared","deps":[]}]
        runtime,host=LICENSE.classify_dependencies({"packages":packages,"resolve":{"nodes":nodes}},"root")
        self.assertEqual(runtime,{"root","runtime","mixed","shared"})
        self.assertEqual(host,{"builder","macro","mixed","shared"})

    def test_binary_source_provenance_rejects_wrong_prefix_mismatch_and_missing_mapping(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); package=root/"package"; prefix=root/"qt/6.9.1"; source=prefix/"lib/QtCore"; source.parent.mkdir(parents=True)
            source.write_bytes(b"raw"); (prefix/"INSTALL_RECEIPT.json").write_text("receipt")
            staged=package/"provenance-inputs/Contents/Frameworks/QtCore"; staged.parent.mkdir(parents=True); staged.write_bytes(b"raw")
            final=package/"Safeparts.app/Contents/Frameworks/QtCore"; final.parent.mkdir(parents=True); final.write_bytes(b"final")
            relative="Contents/Frameworks/QtCore"; record={"path":relative}
            evidence={"source_path":str(source),"source_sha256":LICENSE.sha(source),"staged_path":"provenance-inputs/Contents/Frameworks/QtCore",
                "staged_sha256":LICENSE.sha(staged),"deployed_raw_sha256":LICENSE.sha(staged),"final_sha256":LICENSE.sha(final),"formula":"qt","formula_prefix":str(prefix),
                "formula_version":"6.9.1","receipt_sha256":LICENSE.sha(prefix/"INSTALL_RECEIPT.json"),"sbom_sha256":None}
            manifest={"build":{"binary_inputs":{relative:evidence}}}
            LICENSE.verify_binary_inputs(package,[record],manifest)
            with self.assertRaisesRegex(RuntimeError,"incomplete"):
                LICENSE.verify_binary_inputs(package,[record],{"build":{"binary_inputs":{}}})
            evidence["formula_prefix"]=str(root/"other/6.9.1")
            with self.assertRaisesRegex(RuntimeError,"outside recorded"):
                LICENSE.verify_binary_inputs(package,[record],manifest)
            evidence["formula_prefix"]=str(prefix); staged.write_bytes(b"changed")
            with self.assertRaisesRegex(RuntimeError,"staged input"):
                LICENSE.verify_binary_inputs(package,[record],manifest)
            staged.write_bytes(b"raw"); evidence["formula"]="other"
            with self.assertRaisesRegex(RuntimeError,"selected Qt prefix mismatch"):
                LICENSE.verify_binary_inputs(package,[record],manifest)

    def test_verified_snapshots_are_actual_raw_deployment_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            package=Path(directory); app=package/"Safeparts.app"; relative="Contents/MacOS/Safeparts"
            destination=app/relative; destination.parent.mkdir(parents=True); destination.write_bytes(b"scaffold")
            snapshot=package/"provenance-inputs"/relative; snapshot.parent.mkdir(parents=True); snapshot.write_bytes(b"verified-raw")
            digest=PACKAGE.hashlib.sha256(b"verified-raw").hexdigest()
            provenance={relative:{"staged_path":snapshot.relative_to(package).as_posix(),"staged_sha256":digest,"source_sha256":digest,"dependency_edges":{}}}
            with mock.patch.object(PACKAGE,"is_macho",side_effect=lambda path: path==destination):
                PACKAGE.deploy_verified_snapshots(app,package,provenance)
            self.assertEqual(destination.read_bytes(),b"verified-raw")
            self.assertEqual(provenance[relative]["deployed_raw_sha256"],digest)

    def test_raw_deployment_gate_rejects_substitution_corruption_and_set_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            package=Path(directory); app=package/"Safeparts.app"; relative="Contents/MacOS/Safeparts"
            destination=app/relative; destination.parent.mkdir(parents=True); destination.write_bytes(b"scaffold")
            snapshot=package/"provenance-inputs"/relative; snapshot.parent.mkdir(parents=True); snapshot.write_bytes(b"raw")
            digest=PACKAGE.hashlib.sha256(b"raw").hexdigest()
            provenance={relative:{"staged_path":snapshot.relative_to(package).as_posix(),"staged_sha256":digest,"source_sha256":digest,"dependency_edges":{}}}
            original=PACKAGE.shutil.copy2
            def substitute(src,dst):
                value=original(src,dst); Path(dst).write_bytes(b"same-name-substitute"); return value
            with mock.patch.object(PACKAGE,"is_macho",side_effect=lambda path: path==destination), mock.patch.object(PACKAGE.shutil,"copy2",side_effect=substitute):
                with self.assertRaisesRegex(RuntimeError,"differs from verified snapshot"):
                    PACKAGE.deploy_verified_snapshots(app,package,provenance)
            snapshot.write_bytes(b"corrupt")
            with mock.patch.object(PACKAGE,"is_macho",side_effect=lambda path: path==destination):
                with self.assertRaisesRegex(RuntimeError,"snapshot changed"):
                    PACKAGE.deploy_verified_snapshots(app,package,provenance)
            snapshot.write_bytes(b"raw"); extra=app/"Contents/Frameworks/extra"; extra.parent.mkdir(parents=True); extra.write_bytes(b"extra")
            with mock.patch.object(PACKAGE,"is_macho",side_effect=lambda path: path in (destination,extra)):
                with self.assertRaisesRegex(RuntimeError,"extra"):
                    PACKAGE.deploy_verified_snapshots(app,package,provenance)

    def test_source_graph_rejects_destination_collision(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); executable=root/"Safeparts"; first=root/"a/libx.dylib"; second=root/"b/libx.dylib"
            for path in (executable,first,second): path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(b"x")
            def loads(text):
                return (None,("/a/libx.dylib","/b/libx.dylib"),(),"15.5") if text=="root" else (None,(),(),"15.5")
            def fake_run(*args,**kwargs): return "root" if Path(args[-1]).resolve()==executable.resolve() else "child"
            def resolve(binary,dependency,rpaths,app): return first if dependency.startswith("/a/") else second
            with mock.patch.object(PACKAGE,"run",side_effect=fake_run), mock.patch.object(PACKAGE,"parse_load_commands",side_effect=loads), mock.patch.object(PACKAGE,"resolve_source_load",side_effect=resolve):
                with self.assertRaisesRegex(RuntimeError,"ambiguous deployment destination"):
                    PACKAGE.collect_deployment_sources(executable,{})

    def test_recorded_edges_rewrite_absolute_and_rpath_loads(self):
        with tempfile.TemporaryDirectory() as directory:
            app=Path(directory)/"Safeparts.app"; binary=app/"Contents/MacOS/Safeparts"; one=app/"Contents/Frameworks/QtCore"; two=app/"Contents/Frameworks/libx.dylib"
            for path in (binary,one,two): path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(b"x")
            provenance={"Contents/MacOS/Safeparts":{"dependency_edges":{"@rpath/QtCore":"Contents/Frameworks/QtCore","/outside/libx.dylib":"Contents/Frameworks/libx.dylib"}} ,
                        "Contents/Frameworks/QtCore":{"dependency_edges":{}},"Contents/Frameworks/libx.dylib":{"dependency_edges":{}}}
            calls=[]
            def parse(text):
                return (None,("@rpath/QtCore","/outside/libx.dylib"),("@loader_path/../Frameworks",),"15.5") if text=="binary" else (None,(),(),"15.5")
            def fake_run(*args,**kwargs):
                if args[:2]==("/usr/bin/otool","-l"): return "binary" if Path(args[2])==binary else "other"
                calls.append(args); return ""
            with mock.patch.object(PACKAGE,"parse_load_commands",side_effect=parse), mock.patch.object(PACKAGE,"run",side_effect=fake_run):
                PACKAGE.normalize_recorded_load_paths(app,provenance)
            changes=[call for call in calls if "-change" in call]
            self.assertEqual({call[2] for call in changes},{"@rpath/QtCore","/outside/libx.dylib"})
            self.assertTrue(any("-delete_rpath" in call for call in calls))

    def test_workflow_evidence_refuses_unowned_and_overlapping_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            repo=Path(directory); package=repo/"target/package"; package.mkdir(parents=True)
            evidence=repo/"target/evidence"; evidence.mkdir(); sentinel=evidence/"keep"; sentinel.write_text("keep")
            with self.assertRaisesRegex(RuntimeError,"unowned"):
                WORKFLOW.prepare(repo,evidence.resolve(),package.resolve())
            self.assertTrue(sentinel.exists())
            marker=package/WORKFLOW.MARKER; marker.write_text("owned workflow evidence\n")
            with self.assertRaisesRegex(RuntimeError,"unsafe"):
                WORKFLOW.prepare(repo,package.resolve(),package.resolve())

    def test_workflow_copy_requires_transition_and_preserves_exact_bytes(self):
        expected=b"  exact bytes with trailing space  \n"
        sentinel=b"different synthetic sentinel\n"
        clipboard=[b"unread existing clipboard"]; delayed=[]; reads=[]
        def write(value): clipboard[0]=value
        def read():
            value=delayed.pop(0) if delayed else clipboard[0]
            reads.append(value); return value
        def delayed_copy():
            delayed.extend([sentinel,expected]); clipboard[0]=expected
        WORKFLOW.verified_copy(expected,sentinel,delayed_copy,deadline=time.monotonic()+1,write=write,read=read)
        self.assertEqual(reads[0],sentinel)
        self.assertEqual(reads[-1],expected)

    def test_workflow_copy_rejects_noop_and_does_not_read_before_sentinel(self):
        expected=b"expected\n"; sentinel=b"sentinel\n"; clipboard=[expected]; events=[]
        def write(value): events.append("write"); clipboard[0]=value
        def read(): events.append("read"); return clipboard[0]
        with self.assertRaises(TimeoutError):
            WORKFLOW.verified_copy(expected,sentinel,lambda: None,deadline=time.monotonic()+0.02,write=write,read=read)
        self.assertEqual(events[0],"write")

    def test_workflow_failure_wait_handles_delay_noop_and_query_error(self):
        working="Working…".encode(); statuses=iter([working,working,WORKFLOW.FAILURE_STATUS.encode()])
        enabled=iter([False,True])
        WORKFLOW.wait_failure(lambda: next(statuses,WORKFLOW.FAILURE_STATUS.encode()),lambda: next(enabled,True),lambda: True,lambda: b"sentinel",b"sentinel",deadline=time.monotonic()+1)
        with self.assertRaises(TimeoutError):
            WORKFLOW.wait_failure(lambda: working,lambda: True,lambda: True,lambda: b"sentinel",b"sentinel",deadline=time.monotonic()+0.02)
        with self.assertRaisesRegex(RuntimeError,"AX query failed"):
            WORKFLOW.wait_failure(lambda: (_ for _ in ()).throw(RuntimeError("AX query failed")),lambda: True,lambda: True,lambda: b"sentinel",b"sentinel",deadline=time.monotonic()+1)

    def test_workflow_dialog_requires_open_close_and_preserved_state(self):
        state={"dialog":False,"value":"synthetic"}; responsive=[]
        WORKFLOW.verify_dialog_cancellation(lambda: state["value"],lambda: state["dialog"],lambda: state.__setitem__("dialog",True),lambda: state.__setitem__("dialog",False),lambda: responsive.append(True),deadline=time.monotonic()+1)
        self.assertEqual(responsive,[True])
        with self.assertRaises(TimeoutError):
            WORKFLOW.verify_dialog_cancellation(lambda: "same",lambda: False,lambda: None,lambda: None,lambda: None,deadline=time.monotonic()+0.02)
        state={"dialog":False}
        with self.assertRaises(TimeoutError):
            WORKFLOW.verify_dialog_cancellation(lambda: "same",lambda: state["dialog"],lambda: state.__setitem__("dialog",True),lambda: None,lambda: None,deadline=time.monotonic()+0.02)
        state={"dialog":False,"value":"before"}
        with self.assertRaisesRegex(RuntimeError,"changed app state"):
            WORKFLOW.verify_dialog_cancellation(lambda: state["value"],lambda: state["dialog"],lambda: state.__setitem__("dialog",True),lambda: (state.__setitem__("dialog",False),state.__setitem__("value","after")),lambda: None,deadline=time.monotonic()+1)

    def test_workflow_ax_errors_and_timeouts_are_not_absence(self):
        result=subprocess.CompletedProcess([],5,b"",b"failure")
        with mock.patch.object(WORKFLOW.os,"kill"), mock.patch.object(WORKFLOW.subprocess,"run",return_value=result) as invoked:
            with self.assertRaisesRegex(RuntimeError,"AX helper failed"):
                WORKFLOW.ax_call(Path("helper"),123,"get","AXTextArea","Secret",deadline=time.monotonic()+1)
            self.assertLessEqual(invoked.call_args.kwargs["timeout"],1)

    def test_final_hash_is_recorded_after_all_signing_mutations(self):
        with tempfile.TemporaryDirectory() as directory:
            app=Path(directory)/"Safeparts.app"; binary=app/"Contents/MacOS/Safeparts"
            binary.parent.mkdir(parents=True); binary.write_bytes(b"pre-sign")
            provenance={"Contents/MacOS/Safeparts":{"source_sha256":"source","staged_sha256":"source"}}
            binary.write_bytes(b"post-bundle-sign")
            PACKAGE.record_final_hashes(app,provenance)
            self.assertEqual(provenance["Contents/MacOS/Safeparts"]["final_sha256"],PACKAGE.hashlib.sha256(b"post-bundle-sign").hexdigest())
            self.assertEqual(provenance["Contents/MacOS/Safeparts"]["source_sha256"],"source")
            self.assertEqual(provenance["Contents/MacOS/Safeparts"]["staged_sha256"],"source")

    def test_source_staging_detects_copy_mismatch_and_out_of_prefix_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); prefix=root/"qt/6.9.1"; source=prefix/"lib/QtCore"; source.parent.mkdir(parents=True)
            source.write_bytes(b"raw"); (prefix/"INSTALL_RECEIPT.json").write_text("receipt")
            sources={"Contents/Frameworks/QtCore":{"source":source,"dependency_edges":{}}}
            result=PACKAGE.stage_source_provenance(sources,root/"stage",prefix)
            self.assertEqual(result["Contents/Frameworks/QtCore"]["source_sha256"],result["Contents/Frameworks/QtCore"]["staged_sha256"])
            outside=root/"outside/QtCore"; outside.parent.mkdir(); outside.write_bytes(b"raw")
            self.assertIsNone(PACKAGE.source_formula(outside,prefix))
            original=PACKAGE.shutil.copy2
            def corrupt(src,dst):
                value=original(src,dst); Path(dst).write_bytes(b"changed"); return value
            with mock.patch.object(PACKAGE.shutil,"copy2",side_effect=corrupt):
                with self.assertRaisesRegex(RuntimeError,"differs from source"):
                    PACKAGE.stage_source_provenance(sources,root/"bad-stage",prefix)

    def test_cleanup_terminates_descendant_after_leader_exit(self):
        code = "import os,time; p=os.fork(); os._exit(0) if p else time.sleep(60)"
        process = subprocess.Popen([sys.executable, "-c", code], start_new_session=True)
        pgid = process.pid
        process.wait(timeout=5)
        time.sleep(0.1)
        with tempfile.TemporaryDirectory() as directory:
            record = SMOKE.cleanup_group(pgid, Path(directory) / "cleanup.json", timeout=1)
            self.assertTrue(record["term_sent"])
            self.assertFalse(record["kill_sent"])
            self.assertEqual(record["final_members"], [])

    def test_cleanup_force_kills_term_resistant_descendant_and_reports_failure(self):
        code = "import os,signal,time; p=os.fork(); os._exit(0) if p else (signal.signal(signal.SIGTERM, signal.SIG_IGN), time.sleep(60))"
        process = subprocess.Popen([sys.executable, "-c", code], start_new_session=True)
        pgid = process.pid
        process.wait(timeout=5)
        time.sleep(0.1)
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory) / "cleanup.json"
            with self.assertRaisesRegex(RuntimeError, "SIGKILL"):
                SMOKE.cleanup_group(pgid, evidence, timeout=0.2)
            record = json.loads(evidence.read_text())
            self.assertTrue(record["kill_sent"])
            self.assertEqual(record["final_members"], [])

    def test_generated_bridge_defaults_inside_fresh_cmake_tree(self):
        cmake = (Path(__file__).parents[1] / "CMakeLists.txt").read_text()
        self.assertIn('${CMAKE_BINARY_DIR}/generated', cmake)
        self.assertNotIn('${REPO_ROOT}/target/desktop-generated', cmake)


if __name__ == "__main__":
    unittest.main()
