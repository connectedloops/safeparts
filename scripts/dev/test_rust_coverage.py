import tempfile
import unittest
from pathlib import Path

import rust_coverage


class RustCoverageTests(unittest.TestCase):
    def test_inline_tests_and_launch_shims_are_excluded(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            core = repo / "crates/safeparts_core/src/lib.rs"
            core.parent.mkdir(parents=True)
            core.write_text(
                "fn covered() {}\nfn missed() {}\n#[cfg(test)]\nmod tests {}\n",
                encoding="utf-8",
            )
            launch = repo / "crates/safeparts_tui/src/main.rs"
            launch.parent.mkdir(parents=True)
            launch.write_text("fn main() {}\n", encoding="utf-8")
            lcov = repo / "coverage.lcov"
            lcov.write_text(
                f"SF:{core}\nDA:1,1\nDA:2,0\nDA:4,1\nend_of_record\n"
                f"SF:{launch}\nDA:1,0\nend_of_record\n",
                encoding="utf-8",
            )

            overall, components = rust_coverage.calculate_metrics(lcov, repo)

            self.assertEqual(components["core"], rust_coverage.LineMetric(2, 1))
            self.assertEqual(components["tui"], rust_coverage.LineMetric(0, 0))
            self.assertEqual(overall, rust_coverage.LineMetric(2, 1))

    def test_retired_source_cannot_change_supported_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            lcov = repo / "coverage.lcov"
            records = []
            for relative in ("crates/safeparts_core/src/lib.rs", "crates/safeparts_uniffi/src/lib.rs", "desktop/src-tauri/src/lib.rs"):
                source = repo / relative
                source.parent.mkdir(parents=True)
                source.write_text("fn synthetic() {}\n")
                count = 1 if "safeparts_core" in relative else 0
                records.append(f"SF:{source}\nDA:1,{count}\nend_of_record\n")
            lcov.write_text("".join(records))
            overall, components = rust_coverage.calculate_metrics(lcov, repo)
            self.assertEqual(overall, rust_coverage.LineMetric(1, 1))
            self.assertEqual(set(components), {"core", "cli", "tui", "wasm"})
            self.assertEqual(rust_coverage.FLOORS, {"overall": 70.0, "core": 90.0, "cli": 75.0, "tui": 50.0})

    def test_floor_failures_name_each_low_component(self) -> None:
        passing = {
            name: rust_coverage.LineMetric(100, 100)
            for name in rust_coverage.COMPONENT_PREFIXES
        }
        passing["cli"] = rust_coverage.LineMetric(100, 70)
        passing["tui"] = rust_coverage.LineMetric(100, 40)

        failures = rust_coverage.failing_floors(
            rust_coverage.LineMetric(600, 500), passing
        )

        self.assertEqual([name for name, _, _ in failures], ["cli", "tui"])


if __name__ == "__main__":
    unittest.main()
