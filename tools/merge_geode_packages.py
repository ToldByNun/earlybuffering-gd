#!/usr/bin/env python3
"""Merge Geode .geode packages and refuse to ship without required binaries.

`geode package merge` mutates its FIRST argument. This wrapper:
  1. Copies inputs aside (never destroys the only Windows zip by accident)
  2. Runs `geode package merge` into the output path
  3. Runs tools/geode_package_check.py (default: require android64)

Example (Windows + Android64 ship):

  python tools/merge_geode_packages.py ^
    --output releases/audesign.earlybuffer.geode ^
    build/audesign.earlybuffer.geode ^
    build-android64/audesign.earlybuffer.geode

Phone-only Android64 test package:

  python tools/merge_geode_packages.py ^
    --output releases/audesign.earlybuffer-android64.geode ^
    --allow-missing-win ^
    --require android64 ^
    build-android64/audesign.earlybuffer.geode
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
CHECK_SCRIPT = SCRIPT_DIR / "geode_package_check.py"


def die(msg: str, code: int = 2) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    raise SystemExit(code)


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(cmd))
    try:
        subprocess.run(cmd, check=True, cwd=cwd)
    except FileNotFoundError:
        die(f"command not found: {cmd[0]}")
    except subprocess.CalledProcessError as exc:
        die(f"command failed ({exc.returncode}): {' '.join(cmd)}", code=exc.returncode or 1)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=(
            "Merge .geode packages via `geode package merge`, then gate with "
            "geode_package_check.py (android64 required by default)."
        )
    )
    p.add_argument(
        "packages",
        nargs="+",
        type=Path,
        help="Input .geode files (first is the merge base; a copy is used)",
    )
    p.add_argument(
        "--output",
        "-o",
        type=Path,
        required=True,
        help="Output .geode path (created/overwritten)",
    )
    p.add_argument(
        "--require",
        action="append",
        default=[],
        metavar="PLATFORM",
        help="Platform required after merge (repeatable). Default: android64",
    )
    p.add_argument(
        "--allow-missing-win",
        action="store_true",
        help="Do not require the Windows .dll (phone-only / android-only ships)",
    )
    p.add_argument(
        "--zstd",
        action="store_true",
        help="Pass --zstd to geode package merge",
    )
    p.add_argument(
        "--optimize-pngs",
        action="store_true",
        help="Pass --optimize-pngs to geode package merge",
    )
    p.add_argument(
        "--geode",
        default="geode",
        help="Geode CLI executable (default: geode)",
    )
    p.add_argument(
        "--skip-check",
        action="store_true",
        help="Skip package binary check (not recommended)",
    )
    return p.parse_args(argv)


def resolve_requires(args: argparse.Namespace) -> list[str]:
    require = [r.lower().strip() for r in args.require] if args.require else ["android64"]
    if not args.allow_missing_win and "win" not in require and "windows" not in require:
        if len(args.packages) >= 2:
            require = ["win", *require]
    seen: set[str] = set()
    out: list[str] = []
    for r in require:
        if r not in seen:
            seen.add(r)
            out.append(r)
    return out


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    packages = [p.resolve() for p in args.packages]
    output = args.output.resolve()

    if not CHECK_SCRIPT.is_file():
        die(f"missing checker: {CHECK_SCRIPT}")

    for pkg in packages:
        if not pkg.is_file():
            die(f"package not found: {pkg}")

    require = resolve_requires(args)
    print(f"output:   {output}")
    print(f"require:  {', '.join(require)}")
    print("inputs:")
    for pkg in packages:
        print(f"  {pkg}")

    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="eb-geode-merge-") as tmp:
        tmp_path = Path(tmp)
        work_pkgs: list[Path] = []
        for i, pkg in enumerate(packages):
            dest = tmp_path / f"pkg{i}-{pkg.name}"
            shutil.copy2(pkg, dest)
            work_pkgs.append(dest)

        if len(work_pkgs) == 1:
            merged = work_pkgs[0]
            print("single package — skip geode package merge")
        else:
            merged = work_pkgs[0]
            cmd = [args.geode, "package", "merge"]
            if args.zstd:
                cmd.append("--zstd")
            if args.optimize_pngs:
                cmd.append("--optimize-pngs")
            cmd.extend(str(p) for p in work_pkgs)
            run(cmd)

        staging = output.with_suffix(output.suffix + ".tmp")
        if staging.exists():
            staging.unlink()
        shutil.copy2(merged, staging)
        staging.replace(output)

    print(f"wrote: {output}")

    if args.skip_check:
        print("WARN: --skip-check set; not verifying binaries")
        return 0

    check_cmd = [
        sys.executable,
        str(CHECK_SCRIPT),
        str(output),
    ]
    for plat in require:
        check_cmd.extend(["--require", plat])
    print("+", " ".join(check_cmd))
    try:
        subprocess.run(check_cmd, check=True)
    except subprocess.CalledProcessError as exc:
        print(
            "FAIL: merged package missing required binaries — "
            "do not install or publish this file.",
            file=sys.stderr,
        )
        return exc.returncode or 1

    print("OK: merge + package check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
