"""Regress the iOS launcher archive-name contract without an iOS device."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from geode_package_check import check_package


class IosImportFilenameTests(unittest.TestCase):
    def test_launcher_derived_binary_name(self):
        # Mirrors AppDelegate openURL: strip .geode, append .ios.dylib.
        binary = "audesign.earlybuffer.ios.dylib"
        self.assertEqual(Path("audesign.earlybuffer.geode").stem + ".ios.dylib", binary)
        for filename in ("audesign.earlybuffer-v1.6.1.geode",
                         "audesign.earlybuffer (1).geode",
                         "audesign.earlybuffer-ios.geode"):
            self.assertNotEqual(Path(filename).stem + ".ios.dylib", binary)

    def test_gate_rejects_renamed_uploads_with_identical_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            canonical = root / "audesign.earlybuffer.geode"
            with zipfile.ZipFile(canonical, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("mod.json", json.dumps({"id": "audesign.earlybuffer"}))
                archive.writestr("audesign.earlybuffer.dll", b"test binary")
            for filename, expected in ((canonical.name, 0),
                                       ("audesign.earlybuffer-v1.6.1.geode", 1),
                                       ("audesign.earlybuffer (1).geode", 1),
                                       ("audesign.earlybuffer-ios.geode", 1)):
                package = root / filename
                if package != canonical:
                    package.write_bytes(canonical.read_bytes())
                with contextlib.redirect_stdout(io.StringIO()):
                    result = check_package(package, ["win"], None, check_install_name=True)
                self.assertEqual(result, expected, filename)
            # Historical archive inspection remains available without the ship gate.
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(check_package(package, ["win"], None), 0)


if __name__ == "__main__":
    unittest.main()
