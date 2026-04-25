#!/usr/bin/env python3
"""Parse Catch2 XML benchmark output and fail when a gated benchmark
exceeds its threshold.

Catch2 v3 emits each benchmark result as a `BenchmarkResults` element
under the parent `TestCase`. The `mean` child element carries the mean
in nanoseconds. We treat the mean as the gate metric.

Usage:
  check_benchmark_threshold.py <xml_path> [<benchmark_name> <threshold_ms> ...]

Each (name, threshold_ms) pair fails the script when
`mean(name) > threshold_ms`. Pairs are matched on a case-insensitive
substring of the benchmark name.

Always prints the parsed results table for transparency, regardless of
pass/fail.
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET


def parse_results(xml_path: str) -> list[tuple[str, float]]:
    """Return [(benchmark_name, mean_ms), ...] from a Catch2 XML report."""
    tree = ET.parse(xml_path)
    root = tree.getroot()
    results: list[tuple[str, float]] = []
    for br in root.iter("BenchmarkResults"):
        name = br.attrib.get("name", "<unnamed>")
        mean_el = br.find("mean")
        if mean_el is None or "value" not in mean_el.attrib:
            continue
        mean_ns = float(mean_el.attrib["value"])
        results.append((name, mean_ns / 1_000_000.0))
    return results


def main(argv: list[str]) -> int:
    if len(argv) < 2 or (len(argv) - 2) % 2 != 0:
        print(__doc__, file=sys.stderr)
        return 2
    xml_path = argv[1]
    gates: list[tuple[str, float]] = []
    for i in range(2, len(argv), 2):
        gates.append((argv[i].lower(), float(argv[i + 1])))

    results = parse_results(xml_path)
    if not results:
        print(f"No BenchmarkResults found in {xml_path}", file=sys.stderr)
        return 2

    print("Benchmark results (mean):")
    for name, mean_ms in results:
        print(f"  {name}: {mean_ms:.3f} ms")
    print()

    failed = False
    for needle, threshold_ms in gates:
        matches = [(n, m) for n, m in results if needle in n.lower()]
        if not matches:
            print(f"FAIL: no benchmark matched '{needle}'", file=sys.stderr)
            failed = True
            continue
        for name, mean_ms in matches:
            if mean_ms > threshold_ms:
                print(
                    f"FAIL: '{name}' mean {mean_ms:.3f} ms > threshold {threshold_ms:.3f} ms",
                    file=sys.stderr,
                )
                failed = True
            else:
                print(
                    f"OK:   '{name}' mean {mean_ms:.3f} ms <= threshold {threshold_ms:.3f} ms"
                )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
