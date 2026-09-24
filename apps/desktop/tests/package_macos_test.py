#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

MODULE_PATH = Path(__file__).parents[1] / "scripts/package_macos.py"
SPEC = importlib.util.spec_from_file_location("package_macos", MODULE_PATH)
PACKAGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PACKAGE
SPEC.loader.exec_module(PACKAGE)
R = PACKAGE.MachORecord


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
            (owned / "old").write_text("old")
            PACKAGE.prepare_output(repo, owned.resolve())
            self.assertFalse((owned / "old").exists())
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


if __name__ == "__main__":
    unittest.main()
