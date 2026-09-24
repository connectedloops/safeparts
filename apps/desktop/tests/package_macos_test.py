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
        for name,kind in (("root",["lib"]),("runtime",["lib"]),("builder",["lib"]),("macro",["proc-macro"]),("dev",["lib"])):
            packages.append({"id":name,"name":name,"targets":[{"kind":kind}]})
        nodes=[{"id":"root","deps":[
            {"pkg":"runtime","dep_kinds":[{"kind":None,"target":None}]},
            {"pkg":"builder","dep_kinds":[{"kind":"build","target":None}]},
            {"pkg":"macro","dep_kinds":[{"kind":None,"target":None}]},
            {"pkg":"dev","dep_kinds":[{"kind":"dev","target":None}]}]},
            {"id":"runtime","deps":[]},{"id":"builder","deps":[]},{"id":"macro","deps":[]},{"id":"dev","deps":[]}]
        runtime,host=LICENSE.classify_dependencies({"packages":packages,"resolve":{"nodes":nodes}},"root")
        self.assertEqual(runtime,{"root","runtime"})
        self.assertEqual(host,{"builder","macro"})

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
