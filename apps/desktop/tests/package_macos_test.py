#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE_PATH = Path(__file__).parents[1] / "scripts/package_macos.py"
SPEC = importlib.util.spec_from_file_location("package_macos", MODULE_PATH)
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


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


if __name__ == "__main__":
    unittest.main()
