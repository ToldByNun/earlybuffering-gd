#!/usr/bin/env python3
"""Verify a Geode .geode package contains required platform binaries.

Geode loads {mod_id}{platform_suffix} from the package zip. Installing a
Windows-only package on Android fails with:

  Unable to find platform binary under the name 'audesign.earlybuffer.android64.so'

This tool fails loudly before that install step.
"""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

# Geode platform binary suffixes (ModMetadataImpl: id + GEODE_PLATFORM_EXTENSION).
PLATFORM_SUFFIX: dict[str, str] = {
    "win": ".dll",
    "windows": ".dll",
    "android64": ".android64.so",
    "android32": ".android32.so",
    "mac": ".dylib",
    "macos": ".dylib",
    "ios": ".ios.dylib",
}

DEFAULT_MOD_ID = "audesign.earlybuffer"

# Mach-O (little-endian thin + fat) — used to detect LC_CODE_SIGNATURE on iOS.
MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE
FAT_MAGIC = 0xCAFEBABE
FAT_CIGAM = 0xBEBAFECA
LC_CODE_SIGNATURE = 0x1D


def normalize_entry(name: str) -> str:
    """Zip entries may use forward or back slashes; compare basenames only."""
    return name.replace("\\", "/").split("/")[-1]


def read_mod_id_from_package(zf: zipfile.ZipFile) -> str | None:
    names = {normalize_entry(n): n for n in zf.namelist()}
    key = names.get("mod.json")
    if key is None:
        return None
    try:
        data = json.loads(zf.read(key).decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError, KeyError):
        return None
    mid = data.get("id")
    return mid if isinstance(mid, str) and mid else None


def expected_binary(mod_id: str, platform: str) -> str:
    plat = platform.lower().strip()
    if plat not in PLATFORM_SUFFIX:
        raise ValueError(
            f"unknown platform '{platform}' "
            f"(known: {', '.join(sorted(PLATFORM_SUFFIX))})"
        )
    return mod_id + PLATFORM_SUFFIX[plat]


def list_binaries(zf: zipfile.ZipFile, mod_id: str) -> list[str]:
    """Basenames that look like this mod's platform binaries."""
    found: list[str] = []
    prefixes = (mod_id + ".",)
    # Windows: {id}.dll — no extra platform token between id and extension.
    win_name = mod_id + ".dll"
    for raw in zf.namelist():
        base = normalize_entry(raw)
        if not base or base.endswith("/"):
            continue
        if base == win_name or base.startswith(prefixes):
            # Accept Geode binary extensions only (ignore e.g. id.pdb noise).
            lower = base.lower()
            if (
                lower.endswith(".dll")
                or lower.endswith(".so")
                or lower.endswith(".dylib")
            ):
                found.append(base)
    return sorted(set(found))


def _u32(data: bytes, off: int, *, big_endian: bool) -> int:
    return int.from_bytes(data[off : off + 4], "big" if big_endian else "little")


def mach_o_slice_has_code_signature(data: bytes, offset: int = 0) -> bool:
    """True if a thin Mach-O at offset has LC_CODE_SIGNATURE."""
    if len(data) < offset + 32:
        return False
    magic = _u32(data, offset, big_endian=False)
    if magic == MH_MAGIC_64:
        be = False
    elif magic == MH_CIGAM_64:
        be = True
    else:
        return False
    ncmds = _u32(data, offset + 16, big_endian=be)
    sizeofcmds = _u32(data, offset + 20, big_endian=be)
    cmd_off = offset + 32
    end = cmd_off + sizeofcmds
    if end > len(data) or ncmds > 512:
        return False
    for _ in range(ncmds):
        if cmd_off + 8 > end:
            return False
        cmd = _u32(data, cmd_off, big_endian=be)
        cmdsize = _u32(data, cmd_off + 4, big_endian=be)
        if cmdsize < 8:
            return False
        if cmd == LC_CODE_SIGNATURE:
            return True
        cmd_off += cmdsize
    return False


def macho_has_code_signature(data: bytes) -> bool:
    """True if thin or fat Mach-O has LC_CODE_SIGNATURE on every slice."""
    if len(data) < 8:
        return False
    magic = _u32(data, 0, big_endian=False)
    if magic in (MH_MAGIC_64, MH_CIGAM_64):
        return mach_o_slice_has_code_signature(data, 0)
    if magic == FAT_MAGIC:
        be = True
    elif magic == FAT_CIGAM:
        be = False
    else:
        return False
    nfat = _u32(data, 4, big_endian=be)
    if nfat == 0 or nfat > 16 or len(data) < 8 + nfat * 20:
        return False
    for i in range(nfat):
        # fat_arch: cputype(4) cpusubtype(4) offset(4) size(4) align(4)
        base = 8 + i * 20
        slice_off = _u32(data, base + 8, big_endian=be)
        if not mach_o_slice_has_code_signature(data, slice_off):
            return False
    return True


def find_entry(zf: zipfile.ZipFile, basename: str) -> str | None:
    for raw in zf.namelist():
        if normalize_entry(raw).lower() == basename.lower():
            return raw
    return None


def check_package(
    path: Path,
    require: list[str],
    mod_id: str | None,
    check_ios_sign: bool = False,
) -> int:
    if not path.is_file():
        print(f"ERROR: package not found: {path}", file=sys.stderr)
        return 2

    try:
        with zipfile.ZipFile(path, "r") as zf:
            bad = zf.testzip()
            if bad is not None:
                print(f"ERROR: corrupt zip entry: {bad}", file=sys.stderr)
                return 2

            entries = zf.namelist()
            if not entries:
                print("ERROR: package zip is empty", file=sys.stderr)
                return 2

            pkg_id = read_mod_id_from_package(zf)
            resolved_id = mod_id or pkg_id or DEFAULT_MOD_ID
            if mod_id and pkg_id and mod_id != pkg_id:
                print(
                    f"ERROR: --mod-id '{mod_id}' does not match "
                    f"mod.json id '{pkg_id}'",
                    file=sys.stderr,
                )
                return 2
            if pkg_id is None:
                print(
                    "WARN: mod.json missing or unreadable; "
                    f"using mod id '{resolved_id}'",
                    file=sys.stderr,
                )

            present = list_binaries(zf, resolved_id)
            print(f"package: {path}")
            print(f"mod id:  {resolved_id}")
            print("binaries found:")
            if present:
                for b in present:
                    print(f"  {b}")
            else:
                print("  (none)")

            missing: list[str] = []
            for plat in require:
                name = expected_binary(resolved_id, plat)
                if name not in present:
                    # Also accept basename match case-insensitively on Windows.
                    if not any(p.lower() == name.lower() for p in present):
                        missing.append(name)

            if missing:
                print("MISSING required platform binaries:")
                for name in missing:
                    print(f"  {name}")
                    # Match Geode loader wording so logs are searchable.
                    print(
                        "  Unable to find platform binary under the name "
                        f"'{name}'"
                    )
                print(
                    "FAIL: do not install this .geode on a platform that "
                    "needs a missing binary (e.g. Android64 / iOS)."
                )
                sys.stdout.flush()
                return 1

            # iOS: Geode dlopen fails with "missing code signature" (dyld may
            # also print "no such file" as the first tried path) when the
            # .ios.dylib lacks LC_CODE_SIGNATURE. Check on any host.
            want_ios_sign = check_ios_sign or ("ios" in require)
            if want_ios_sign:
                ios_name = expected_binary(resolved_id, "ios")
                entry = find_entry(zf, ios_name)
                if entry is None:
                    if check_ios_sign:
                        print(
                            f"FAIL: --check-ios-sign requires {ios_name} "
                            "in the package"
                        )
                        return 1
                else:
                    blob = zf.read(entry)
                    if len(blob) < 64:
                        print(
                            f"FAIL: {ios_name} is empty/truncated "
                            f"({len(blob)} bytes)"
                        )
                        return 1
                    if not macho_has_code_signature(blob):
                        print(
                            f"FAIL: {ios_name} has no LC_CODE_SIGNATURE — "
                            "iOS Geode will dlopen-fail with "
                            "'missing code signature' / 'Unable to load the DYLIB'."
                        )
                        print(
                            "  Rebuild on macOS/CI (ldid/codesign POST_BUILD) "
                            "or re-sign: ldid -S <dylib> / codesign -f -s - <dylib>"
                        )
                        return 1
                    print(f"OK: {ios_name} has LC_CODE_SIGNATURE")

            if require:
                print("OK: all required binaries present")
            return 0
    except zipfile.BadZipFile as exc:
        print(f"ERROR: not a valid zip/.geode: {exc}", file=sys.stderr)
        return 2
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=(
            "Check that a .geode zip contains required Geode platform binaries "
            "(prevents Android 'Unable to find platform binary' installs)."
        )
    )
    p.add_argument(
        "package",
        type=Path,
        help="Path to a .geode file (zip)",
    )
    p.add_argument(
        "--require",
        action="append",
        default=[],
        metavar="PLATFORM",
        help=(
            "Required platform (repeatable): android64, android32, win, mac, ios. "
            "Default if omitted: android64"
        ),
    )
    p.add_argument(
        "--mod-id",
        default=None,
        help=f"Mod id (default: from mod.json or {DEFAULT_MOD_ID})",
    )
    p.add_argument(
        "--list-only",
        action="store_true",
        help="List binaries and exit 0 even if requirements are missing",
    )
    p.add_argument(
        "--check-ios-sign",
        action="store_true",
        help=(
            "Require audesign.earlybuffer.ios.dylib to carry LC_CODE_SIGNATURE "
            "(also implied by --require ios)"
        ),
    )
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    require = list(args.require) if args.require else ["android64"]
    # Deduplicate while preserving order.
    seen: set[str] = set()
    require_unique: list[str] = []
    for r in require:
        key = r.lower().strip()
        if key not in seen:
            seen.add(key)
            require_unique.append(key)

    if args.list_only:
        # Open and list only; empty require → never fail on missing platforms.
        code = check_package(args.package, [], args.mod_id, False)
        return 0 if code in (0, 1) else code

    return check_package(
        args.package, require_unique, args.mod_id, args.check_ios_sign
    )


if __name__ == "__main__":
    sys.exit(main())
