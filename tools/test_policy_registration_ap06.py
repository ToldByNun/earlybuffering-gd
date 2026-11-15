"""Check AP06 policy registration across settings, config bundles and categories."""
import json
import re
from pathlib import Path


class PolicyRegistrationCheck:
    @staticmethod
    def run():
        settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]
        bundle = Path("src/core/bundle/config_bundle_keys.cpp").read_text(encoding="utf-8")
        categories = Path("src/ui/settings/categories/settings_category_map.cpp").read_text(encoding="utf-8")
        keys = ["orb-chain-verify", "ball-ground-check", "level-assist-policies"]
        modes = ("cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing")
        keys += [mode + "-orb-chain-verify" for mode in modes]
        for key in keys:
            assert key in settings, key
            assert '{"' + key + '", EbConfigBundleKeys::Kind::String}' in bundle, key
            assert '{"' + key + '", SettingsCategory::Modes}' in categories, key
        for mode in modes:
            setting = settings[mode + "-orb-chain-verify"]
            assert setting["default"] == "Inherit"
            assert setting["one-of"] == ["Inherit", "Auto", "On", "Off"]
        for key in ("orb-chain-verify", "ball-ground-check"):
            assert settings[key]["default"] == "Auto"
        assert json.loads(settings["level-assist-policies"]["default"]) == {}
        entries = re.findall(r'\{"[^"]+", EbConfigBundleKeys::Kind::', bundle)
        assert len(entries) == 183, len(entries)
        print("AP06 registration: 11 policy keys, all modes and bundle entries verified")


if __name__ == "__main__":
    PolicyRegistrationCheck.run()
