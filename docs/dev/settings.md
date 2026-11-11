# Settings & mod.json

## Add a user-facing setting

1. Add under `settings` in `mod.json` (`bool` / `int` / `float` / `string` + `one-of`)
2. Cache in `StageConfig::refreshCache()` + getter
3. `listenForSettingChanges<…>` in the module entry
4. Document in [Assist reference](/assist_reference) + `changelog.md` if players see it
5. Wire the smallest owning core/hook file

Preserve **serialized setting keys** when renaming C++ identifiers.

## Categories (UI tabs)

Filter map lives under `src/ui/settings/` (`title-*` keys → Main / Scope / Assist / Bake / Modes / Keys / Dev).

## Fingerprint

Dev sessions store a settings fingerprint (stage, mode, pick, engine, ticks, safe, budgets, TW flags, …). Useful when comparing traces across builds.

## Cloud packs

Cloud install goes through ConfigBundle, skips keybinds + developer settings, refreshes cache/UI. See [Cloud configs](/cloud_configs).
