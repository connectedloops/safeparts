#!/usr/bin/env python3
"""Generate and enforce production-only Rust line coverage."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

COMPONENT_PREFIXES = {
    "core": "crates/safeparts_core/src/",
    "cli": "crates/safeparts/src/",
    "tui": "crates/safeparts_tui/src/",
    "wasm": "crates/safeparts_wasm/src/",
}

FLOORS = {
    "overall": 70.0,
    "core": 90.0,
    "cli": 75.0,
    "tui": 50.0,
}

EXCLUDED_FILES = {
    "crates/safeparts_tui/src/main.rs",
}


@dataclass(frozen=True)
class LineMetric:
    total: int
    covered: int

    @property
    def percent(self) -> float:
        return 100.0 if self.total == 0 else self.covered * 100.0 / self.total

    def as_json(self) -> dict[str, int | float]:
        return {
            "total": self.total,
            "covered": self.covered,
            "percent": round(self.percent, 2),
        }


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def parse_lcov(path: Path, repo: Path) -> dict[str, dict[int, int]]:
    repo = repo.resolve()
    records: dict[str, dict[int, int]] = {}
    current: str | None = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if raw_line.startswith("SF:"):
            source = Path(raw_line[3:])
            if not source.is_absolute():
                source = repo / source
            try:
                current = source.resolve().relative_to(repo).as_posix()
            except ValueError:
                current = None
            if current is not None:
                records.setdefault(current, {})
        elif raw_line.startswith("DA:") and current is not None:
            line_number, count, *_ = raw_line[3:].split(",")
            line_number_int = int(line_number)
            count_int = int(count)
            records[current][line_number_int] = max(
                count_int, records[current].get(line_number_int, 0)
            )

    return records


def production_cutoff(source: Path) -> int:
    lines = source.read_text(encoding="utf-8").splitlines()
    return next(
        (index for index, line in enumerate(lines, 1) if line.strip() == "#[cfg(test)]"),
        len(lines) + 1,
    )


def calculate_metrics(lcov: Path, repo: Path) -> tuple[LineMetric, dict[str, LineMetric]]:
    repo = repo.resolve()
    records = parse_lcov(lcov, repo)
    totals = {name: [0, 0] for name in COMPONENT_PREFIXES}

    for relative_path, lines in records.items():
        if relative_path in EXCLUDED_FILES:
            continue

        component = next(
            (
                name
                for name, prefix in COMPONENT_PREFIXES.items()
                if relative_path.startswith(prefix)
            ),
            None,
        )
        if component is None:
            continue

        source = repo / relative_path
        cutoff = production_cutoff(source)
        production_lines = {
            line_number: count
            for line_number, count in lines.items()
            if line_number < cutoff
        }
        totals[component][0] += len(production_lines)
        totals[component][1] += sum(count > 0 for count in production_lines.values())

    components = {
        name: LineMetric(total=values[0], covered=values[1])
        for name, values in totals.items()
    }
    overall = LineMetric(
        total=sum(metric.total for metric in components.values()),
        covered=sum(metric.covered for metric in components.values()),
    )
    return overall, components


def failing_floors(
    overall: LineMetric, components: dict[str, LineMetric]
) -> list[tuple[str, float, float]]:
    failures = []
    for name, floor in FLOORS.items():
        metric = overall if name == "overall" else components[name]
        actual = 0.0 if metric.total == 0 else metric.percent
        if actual + 1e-9 < floor:
            failures.append((name, actual, floor))
    return failures


def write_summary(
    output: Path, overall: LineMetric, components: dict[str, LineMetric]
) -> None:
    summary = {
        "schema": 1,
        "metric": "LLVM line coverage before each source file's first #[cfg(test)] section",
        "overall": {**overall.as_json(), "floor": FLOORS["overall"]},
        "components": {
            name: {
                **metric.as_json(),
                "floor": FLOORS.get(name),
                "floor_note": (
                    "informational: browser-target tests run separately without a stable LLVM line profile"
                    if name == "wasm"
                    else None
                ),
            }
            for name, metric in components.items()
        },
        "excluded_files": sorted(EXCLUDED_FILES),
    }
    output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


def print_summary(overall: LineMetric, components: dict[str, LineMetric]) -> None:
    print("\nProduction-only Rust line coverage")
    print("component  covered/total  percent  floor")
    rows = [("overall", overall), *components.items()]
    for name, metric in rows:
        floor = FLOORS.get(name)
        floor_text = f"{floor:.0f}%" if floor is not None else "info"
        print(
            f"{name:<10} {metric.covered:>5}/{metric.total:<5} "
            f"{metric.percent:>7.2f}%  {floor_text}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("target/coverage"),
        help="artifact directory (default: target/coverage)",
    )
    parser.add_argument(
        "--from-lcov",
        type=Path,
        help="check an existing LCOV file instead of running cargo llvm-cov",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[2]
    output_dir = args.output_dir
    if not output_dir.is_absolute():
        output_dir = repo / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.from_lcov is None:
        lcov = output_dir / "rust.lcov"
        run(["cargo", "llvm-cov", "clean", "--workspace"], repo)
        run(
            [
                "cargo",
                "llvm-cov",
                "--workspace",
                "--all-features",
                "--lcov",
                "--output-path",
                str(lcov),
            ],
            repo,
        )
        run(
            [
                "cargo",
                "llvm-cov",
                "report",
                "--html",
                "--output-dir",
                str(output_dir),
            ],
            repo,
        )
    else:
        lcov = args.from_lcov
        if not lcov.is_absolute():
            lcov = repo / lcov

    overall, components = calculate_metrics(lcov, repo)
    write_summary(output_dir / "production-summary.json", overall, components)
    print_summary(overall, components)

    failures = failing_floors(overall, components)
    if failures:
        print("\nCoverage floor failures:", file=sys.stderr)
        for name, actual, floor in failures:
            print(
                f"- {name}: {actual:.2f}% is below the {floor:.0f}% floor",
                file=sys.stderr,
            )
        return 1

    print(f"\nCoverage artifacts: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
