"""Exercise final archive validation and refusal to publish mixed CI builds."""
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile
from bundle_geode import GeodeBundler, PLATFORMS
from geode_package_check import expected_binary


class BundleTests(unittest.TestCase):
    def packages(self, root):
        manifest = {"id": "audesign.earlybuffer", "version": "v1.6.2"}
        inputs = []
        for platform in PLATFORMS:
            package = root / platform / "audesign.earlybuffer.geode"
            package.parent.mkdir()
            data = platform.encode()
            if platform == "ios":
                # Synthetic Mach-O marker for package-gate tests, not a valid signature.
                data = struct.pack("<8I", 0xFEEDFACF, 0x100000C, 0, 6, 1, 16, 0, 0)
                data += struct.pack("<4I", 0x1D, 16, 0, 0) + bytes(64)
            with zipfile.ZipFile(package, "w") as archive:
                archive.writestr("mod.json", json.dumps(manifest))
                archive.writestr(expected_binary(manifest["id"], platform), data)
                archive.writestr("resources/shared.txt", b"identical resource\r\n" if platform == "win" else b"identical resource\n")
            inputs.append(package)
        return manifest, inputs

    def merge(self, inputs, output, manifest):
        with contextlib.redirect_stdout(io.StringIO()):
            GeodeBundler.merge(inputs, output, manifest)

    def test_complete_bundle_preserves_binaries_and_uses_deflate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, inputs = self.packages(root)
            output = root / "out" / "audesign.earlybuffer.geode"
            self.merge(inputs, output, manifest)
            with zipfile.ZipFile(output) as merged:
                self.assertTrue(all(i.compress_type == zipfile.ZIP_DEFLATED for i in merged.infolist()))
                for platform, source in zip(PLATFORMS, inputs):
                    binary = expected_binary(manifest["id"], platform)
                    with zipfile.ZipFile(source) as original:
                        self.assertEqual(merged.read(binary), original.read(binary))
            self.assertTrue(output.with_suffix(".sha256.txt").is_file())

    def test_missing_platform_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, inputs = self.packages(root)
            output = root / "audesign.earlybuffer.geode"
            output.write_bytes(b"existing release")
            with self.assertRaises(ValueError):
                self.merge(inputs[:-1], output, manifest)
            self.assertEqual(output.read_bytes(), b"existing release")

    def test_mixed_version_and_renamed_output_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, inputs = self.packages(root)
            with self.assertRaises(ValueError):
                self.merge(inputs, root / "versioned-v1.6.2.geode", manifest)
            manifest["version"] = "v1.6.3"
            with self.assertRaises(ValueError):
                self.merge(inputs, root / "out" / "audesign.earlybuffer.geode", manifest)

    def test_duplicate_platform_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, inputs = self.packages(root)
            with self.assertRaises(ValueError):
                self.merge([inputs[0]] * 5, root / "out" / "audesign.earlybuffer.geode", manifest)

    def test_conflicting_shared_resource_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, inputs = self.packages(root)
            source = inputs[-1]
            with zipfile.ZipFile(source) as archive:
                files = {name: archive.read(name) for name in archive.namelist()}
            files["resources/shared.txt"] = b"different commit resource"
            with zipfile.ZipFile(source, "w") as archive:
                for name, data in files.items():
                    archive.writestr(name, data)
            with self.assertRaises(ValueError):
                self.merge(inputs, root / "out" / "audesign.earlybuffer.geode", manifest)


if __name__ == "__main__":
    unittest.main()
