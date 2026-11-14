"""Bundle CI platform packages without relying on the upstream combine action."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
import zipfile

from geode_package_check import check_package, expected_binary

PLATFORMS = ("win", "mac", "ios", "android32", "android64")


class GeodeBundler:
    @staticmethod
    def merge(inputs, output, manifest):
        mod_id = manifest["id"]
        version = manifest["version"]
        if output.name != mod_id + ".geode":
            raise ValueError("Output must use the canonical mod-id.geode filename")
        if len(inputs) != len(PLATFORMS):
            raise ValueError("Exactly five platform packages are required")
        contents = {}
        seen_platforms = set()
        for source in inputs:
            if source.resolve() == output.resolve():
                raise ValueError("Output cannot replace an input package")
            with zipfile.ZipFile(source) as archive:
                metadata = json.loads(archive.read("mod.json"))
                if metadata.get("id") != mod_id or metadata.get("version") != version:
                    raise ValueError(f"Mixed mod ID/version in {source}")
                platforms = [p for p in PLATFORMS if expected_binary(mod_id, p) in archive.namelist()]
                if len(platforms) != 1 or platforms[0] in seen_platforms:
                    raise ValueError(f"Expected one unique platform package: {source}")
                seen_platforms.add(platforms[0])
                seen_entries = set()
                for entry in archive.infolist():
                    name = entry.filename
                    path = PurePosixPath(name)
                    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
                        raise ValueError(f"Unsafe archive path: {name}")
                    if name in seen_entries:
                        raise ValueError(f"Duplicate archive entry: {name}")
                    seen_entries.add(name)
                    if entry.is_dir():
                        continue
                    data = archive.read(entry)
                    if path.suffix.lower() in (".md", ".txt", ".json"):
                        # Windows checkout uses CRLF; other runners use LF.
                        # Normalize text only, keeping native binaries untouched.
                        data = data.replace(b"\r\n", b"\n")
                    if name == "mod.json":
                        # Whitespace may differ, but metadata must agree completely.
                        if metadata != manifest:
                            raise ValueError(f"Manifest differs from release source: {source}")
                        data = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode()
                    if name in contents and contents[name] != data:
                        raise ValueError(f"Conflicting shared file: {name}")
                    contents[name] = data
        if seen_platforms != set(PLATFORMS):
            raise ValueError("Missing platform packages")
        output.parent.mkdir(parents=True, exist_ok=True)
        # Validate before replacing an existing release artifact.
        with tempfile.TemporaryDirectory(dir=output.parent) as staging:
            candidate = Path(staging) / output.name
            with zipfile.ZipFile(candidate, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
                for name, data in sorted(contents.items()):
                    entry = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
                    entry.create_system = 3
                    entry.external_attr = 0o100644 << 16
                    archive.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
            if check_package(candidate, list(PLATFORMS), mod_id, True, True, True):
                raise ValueError("Final bundle failed release validation")
            candidate.replace(output)
        digest = hashlib.sha256(output.read_bytes()).hexdigest()
        output.with_suffix(".sha256.txt").write_text(f"{digest}  {output.name}\n", encoding="utf-8")
        print(f"Bundled {version}: {output} SHA256={digest}")

    @staticmethod
    def main():
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--input-dir", type=Path, required=True)
        parser.add_argument("--output", type=Path, required=True)
        parser.add_argument("--manifest", type=Path, default=Path("mod.json"))
        args = parser.parse_args()
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        inputs = sorted(args.input_dir.rglob(manifest["id"] + ".geode"))
        GeodeBundler.merge(inputs, args.output, manifest)


if __name__ == "__main__":
    GeodeBundler.main()
