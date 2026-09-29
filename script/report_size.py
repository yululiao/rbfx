#!/usr/bin/env python3
"""Size report for web/minigame build artifacts.

Prints two tables over one or more directories:

  * per top-level entry (file or directory) - this is the view that maps to a
    minigame package layout, where every top-level directory is a subpackage
    and every top-level file is part of the main package;
  * per category (wasm / js / data / other), so a budget can be expressed
    against the thing that actually has a platform limit.

Budgets are MiB and fail the process (exit 1) when exceeded:

    report_size.py out_douyin --budget package_wasm=24 --budget wasm=24 \
        --budget wasm=24 --budget package_main=4

Every budget name is matched against top-level entry names first and category
names second. This is the gate used by the minigame packaging (main package <
4 MiB) and by CI (wasm growth).

Optional: when `wasm-objdump` (ships with Emscripten/Binaryen) is on PATH, the
section breakdown of each .wasm file is printed as well.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from collections import defaultdict

CATEGORY_BY_EXT = {
    ".wasm": "wasm",
    ".br": "wasm",  # precompressed wasm payloads
    ".gz": "wasm",
    ".js": "js",
    ".data": "data",
    ".pak": "data",
}

MIB = 1024.0 * 1024.0


def human(num_bytes):
    return f"{num_bytes / MIB:8.2f} MiB"


def categorize(file_name):
    _, ext = os.path.splitext(file_name)
    return CATEGORY_BY_EXT.get(ext.lower(), "other")


def scan(paths, exclude_globs):
    """Returns {top_level_entry: {category: size}, ..., '__total__': {...}}."""
    import fnmatch

    result = defaultdict(lambda: defaultdict(int))
    for root in paths:
        if not os.path.exists(root):
            print(f"warning: '{root}' does not exist", file=sys.stderr)
            continue

        if os.path.isfile(root):
            name = os.path.basename(root)
            category = categorize(name)
            size = os.path.getsize(root)
            result[name][category] += size
            result["__total__"][category] += size
            continue

        for dirpath, dirnames, filenames in os.walk(root):
            for filename in filenames:
                full = os.path.join(dirpath, filename)
                rel = os.path.relpath(full, root)
                if any(fnmatch.fnmatch(rel, pattern) or fnmatch.fnmatch(filename, pattern)
                       for pattern in exclude_globs):
                    continue

                top = rel.split(os.sep, 1)[0]
                category = categorize(filename)
                try:
                    size = os.path.getsize(full)
                except OSError:
                    continue
                result[top][category] += size
                result["__total__"][category] += size
    return result


def print_entry_table(result):
    print("Per top-level entry (minigame: one row per subpackage or main-package file):")
    print(f"  {'entry':<40} {'total':>12}   breakdown")
    entries = sorted(
        (name for name in result if name != "__total__"),
        key=lambda name: (-sum(result[name].values()), name))
    for name in entries:
        total = sum(result[name].values())
        breakdown = ", ".join(f"{category}={human(size).strip()}"
                              for category, size in sorted(result[name].items()))
        print(f"  {name:<40} {human(total)}   {breakdown}")


def print_category_table(result):
    print("Per category:")
    print(f"  {'category':<40} {'total':>12}")
    for category, size in sorted(result["__total__"].items(),
                                 key=lambda item: -item[1]):
        print(f"  {category:<40} {human(size)}")


def wasm_objdump_sections(wasm_file):
    objdump = shutil.which("wasm-objdump")
    if objdump is None:
        return
    print(f"  sections of {os.path.basename(wasm_file)}:")
    try:
        output = subprocess.run([objdump, "-h", wasm_file], check=True,
                                capture_output=True, text=True, timeout=120).stdout
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(f"    failed: {error}")
        return
    for line in output.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith(("-", "section", "Sections")):
            print(f"    {stripped}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="directories or files to report")
    parser.add_argument("--exclude", action="append", default=[],
                        help="glob (relative path or file name) to skip; repeatable")
    parser.add_argument("--budget", action="append", default=[],
                        help="NAME=SIZE_IN_MIB, NAME is a top-level entry or category; repeatable")
    parser.add_argument("--json", help="also write the raw report to this file")
    parser.add_argument("--sections", action="store_true",
                        help="print wasm section breakdown when wasm-objdump is available")
    args = parser.parse_args()

    result = scan(args.paths, args.exclude)

    print()
    print_entry_table(result)
    print()
    print_category_table(result)

    if args.sections:
        print()
        for root in args.paths:
            if os.path.isfile(root) and root.endswith(".wasm"):
                wasm_objdump_sections(root)
            elif os.path.isdir(root):
                for dirpath, _, filenames in os.walk(root):
                    for filename in filenames:
                        if filename.endswith(".wasm"):
                            wasm_objdump_sections(os.path.join(dirpath, filename))

    if args.json:
        serializable = {name: dict(categories) for name, categories in result.items()}
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(serializable, handle, indent=2, sort_keys=True)
        print(f"\nraw report written to {args.json}")

    failures = []
    for budget in args.budget:
        try:
            name, size_text = budget.split("=", 1)
            limit_bytes = float(size_text) * MIB
        except ValueError:
            parser.error(f"malformed budget '{budget}', expected NAME=SIZE_IN_MIB")
        if name in result and name != "__total__":
            actual = sum(result[name].values())
        elif name in result["__total__"]:
            actual = result["__total__"][name]
        else:
            print(f"BUDGET FAIL: '{name}' is neither a top-level entry nor a category")
            failures.append(name)
            continue
        status = "PASS" if actual <= limit_bytes else "FAIL"
        print(f"BUDGET {status}: {name} = {human(actual).strip()} (limit {size_text} MiB)")
        if actual > limit_bytes:
            failures.append(name)

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
