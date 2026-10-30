# Build & install

## Requirements

- Geometry Dash **2.2081** (this mod line does **not** support other GD versions)
- [Geode](https://geode-sdk.org/) **5.11.1+** (SDK + CLI)
- CMake 3.21+ and a **C++23** toolchain (MSVC on Windows)

Set `GEODE_SDK` to your Geode SDK checkout.

## Quick build (recommended)

```bash
geode build --config RelWithDebInfo
```

The CLI packages and can install the `.geode` into your mods folder.

**Windows-only warning:** A local `geode build` on Windows produces a package that contains **`audesign.earlybuffer.dll` only**. Installing that `.geode` on Android fails with:

> Unable to find platform binary under the name 'audesign.earlybuffer.android64.so'

Always verify Android / iOS packages with the package check (below) before copying them to a device.

## CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

Then package with the Geode CLI, or use `setup_geode_mod` output from the build tree.

## Platforms

Local day-to-day development is primarily **Windows**. Official multi-platform binaries can come from GitHub Actions (Windows, macOS, iOS, Android32/64), **or** from a local Android NDK build + merge (this section). You do not need GitHub to ship Android — you need a package that actually contains the Android binary.

## Android (local maintainer path)

Target Geometry Dash **2.2081** + Geode on device. Modern phones need **Android64** (`arm64-v8a`). Older 32-bit devices need **Android32** as well.

### Prerequisites

1. Install the [Android NDK](https://developer.android.com/ndk) (Geode currently pins an NDK **r29** tree) and set **`ANDROID_NDK_ROOT`** permanently to that folder (User or Machine env). A working local path looks like:
   `%LOCALAPPDATA%\Android\Sdk\ndk\29.0.14206865`
   Session-only env vars are lost when the terminal closes — set the User variable or Android builds will fail to configure.
2. On **Windows**, also install **Ninja** (e.g. `scoop install ninja`).
3. Install Geode Android loader binaries for the SDK:

```bash
geode sdk install-binaries -p android64
# Only if you still support 32-bit phones:
geode sdk install-binaries -p android32
```

### Build Android64

```bash
geode build -p android64 --config RelWithDebInfo
# Equivalent:
# geode build --platform android64 --config RelWithDebInfo
```

Default output directory: **`build-android64/`** (Geode CLI). The packaged file is typically:

`build-android64/audesign.earlybuffer.geode`

Inside that zip Geode expects this exact binary name:

**`audesign.earlybuffer.android64.so`**

(mod id + `.android64.so` — do not rename).

Android32 (optional):

```bash
geode build -p android32 --config RelWithDebInfo
# → build-android32/… containing audesign.earlybuffer.android32.so
```

### Package check (required before phone install)

Never copy a Windows-only `build/audesign.earlybuffer.geode` to Android. Run:

```bash
python tools/geode_package_check.py build-android64/audesign.earlybuffer.geode --require android64
```

Exit code **0** = binary present. Exit code **1** = missing (same error Geode shows on device). Exit code **2** = bad/missing zip.

For a multi-platform ship zip (Windows + Android64), require both after merge:

```bash
python tools/geode_package_check.py path/to/merged.geode --require win --require android64
```

Include iOS when the ship zip has the iOS binary:

```bash
python tools/geode_package_check.py path/to/merged.geode --require win --require android64 --require ios
```

### Merge with Windows (required for dual-platform ships)

A phone-only Android64 `.geode` is enough to test on device. A release that also supports Windows needs both binaries in **one** `.geode`.

`geode package merge` **mutates its first argument**. Prefer the wrapper (copies inputs, merges into `--output`, then runs the package check):

```bash
# 1) Windows package
geode build --config RelWithDebInfo
# → build/audesign.earlybuffer.geode  (dll only)

# 2) Android64 package
geode build -p android64 --config RelWithDebInfo
# → build-android64/audesign.earlybuffer.geode  (android64.so)

# 3) Merge + gate (fails if android64.so missing)
python tools/merge_geode_packages.py ^
  --output releases/audesign.earlybuffer.geode ^
  build/audesign.earlybuffer.geode ^
  build-android64/audesign.earlybuffer.geode
```

Phone-only (no Windows `.dll` required):

```bash
python tools/merge_geode_packages.py ^
  --output releases/audesign.earlybuffer-android64.geode ^
  --allow-missing-win ^
  --require android64 ^
  build-android64/audesign.earlybuffer.geode
```

Manual equivalent (easy to get wrong — wrapper preferred):

```bash
copy build\audesign.earlybuffer.geode releases\audesign.earlybuffer.geode
geode package merge releases\audesign.earlybuffer.geode build-android64\audesign.earlybuffer.geode
python tools/geode_package_check.py releases\audesign.earlybuffer.geode --require win --require android64
```

**Never** install or publish until the package check exits **0**. A Windows-only `build/audesign.earlybuffer.geode` will fail `--require android64` on purpose.

### Install on device

Copy the verified `.geode` into the Geode mods folder. On stock Geode Android Launcher that is commonly:

```text
/storage/emulated/0/Android/media/com.geode.launcher/game/geode/mods/
```

Some launchers or forks use a different root — if unsure, use the **Geode mods folder** shown in the launcher / Geode docs for your install. Enable **EarlyBuffer** (`audesign.earlybuffer`) and dependency **geode.node-ids**.

### Edge cases

| Case | What to do |
|------|------------|
| Windows-only `.geode` on phone | Will show the Unknown / missing `android64.so` error — rebuild Android64 or merge |
| 32-bit phone | Also build/merge `android32` (`audesign.earlybuffer.android32.so`) |
| Wrong GD version | This line is **2.2081 only** |
| Package check fails after merge | Do not install; re-merge / re-build the missing platform |
| NDK / Ninja missing | Configure fails before any `.so` is produced — fix env, do not ship the Windows zip as a workaround |

## After install

1. Enable **EarlyBuffer** (`audesign.earlybuffer`) in Geode.
2. Ensure **geode.node-ids** is installed.
3. Prefer **Safe Mode** on for online / rated play.
4. Platformer levels: assist stays off by design.

## Android device smoke checklist

Use a **merged** or android64-checked `.geode` only. Tick each item on a 64-bit phone (GD **2.2081** + Geode):

1. **Package gate (PC)** — `python tools/geode_package_check.py <ship.geode> --require android64` exits 0. For dual ships also `--require win`.
2. **Load** — copy into Geode mods folder; enable EarlyBuffer; **no** `Unable to find platform binary under the name 'audesign.earlybuffer.android64.so'`.
3. **Live assist (touch)** — short classic level (not platformer); Enabled on; Safe Mode on; Assist Mode Live; early jump → Delay or Pass; overlay updates (proves touch → `handleButton`).
4. **Settings without keyboard** — Pause → EB → toggle Enabled / Preset (hotkeys optional).
5. **Config paths** — Export Config → alert shows save-dir path; Import after placing/editing `earlybuffer-config.json` there.
6. **Replay drop** — place one `.gdr` (or `.gdr2` / `.mhr` / `.json` / `.slc`) in `replay-drop/` under the mod save dir → Import Replay succeeds or clear error (not “Windows-only”).
7. **Cold start** — force-stop the app; reopen; mod still loads; PlanGold/load stays WARN-only (no crash). Dev Lab unused stays quiet.
8. **Optional Dev Lab** — if Dev Record Enable is on: Start Recording / Eval Last / Copy Report writes `earlybuffer-dev-report.txt` under the save dir.

If step 2 fails, you installed a Windows-only package — re-merge. If step 3 fails with no overlay and no delay, capture Geode crashlog before changing planner code.

## iOS (local / CI)

Target Geometry Dash **2.2081** + Geode on device. Binary name inside the `.geode`:

**`audesign.earlybuffer.ios.dylib`**

(mod id + `.ios.dylib` — do not rename).

### Build (macOS host)

```bash
geode sdk install-binaries -p ios
geode build -p ios --config RelWithDebInfo
# → typically build-ios/audesign.earlybuffer.geode containing audesign.earlybuffer.ios.dylib
```

CI also builds iOS via [`.github/workflows/multi-platform.yml`](.github/workflows/multi-platform.yml) (macOS runner, `target: iOS`). iOS GeodeBindings needs an FMOD force-include (see `CMakeLists.txt`) because codegen emits FMOD stubs without including `fmod.h`.

### Package check

```bash
python tools/geode_package_check.py build-ios/audesign.earlybuffer.geode --require ios
```

Merged multi-platform ship (example):

```bash
python tools/merge_geode_packages.py ^
  --output releases/audesign.earlybuffer.geode ^
  --require android64 --require ios ^
  build/audesign.earlybuffer.geode ^
  build-android64/audesign.earlybuffer.geode ^
  build-ios/audesign.earlybuffer.geode
```

(`merge_geode_packages.py` still defaults to requiring `android64`; pass `--require ios` explicitly when iOS is in the ship. Use `--allow-missing-win` only for device-only zips.)

### Mobile policy (shared with Android)

iOS uses the same **mobile I/O** path as Android (`SaveDropPaths` under the Geode mod save dir — no Win32 file dialogs) and the same **`PlatformLimits::isConstrainedDevice()`** ceilings (sim ≤2000µs, tick ≤2000, taller overlay inset, no disk PlanGold on load). Alerts show the full path and copy it to the clipboard; use Files / Share / Finder-via-device (see pause Export/Import).

### iOS device smoke checklist

Use a package that passes `--require ios` (and any other platforms you ship). Tick on device (GD **2.2081** + Geode):

1. **Package gate (Mac/PC)** — `python tools/geode_package_check.py <ship.geode> --require ios` exits 0.
2. **Load** — install into Geode mods; enable EarlyBuffer; **no** missing `audesign.earlybuffer.ios.dylib`.
3. **Live assist (touch)** — short classic level (not platformer); Enabled on; Safe Mode on; Assist Mode Live; early jump → Delay or Pass; overlay updates.
4. **Settings without keyboard** — Pause → EB → toggle Enabled / Preset.
5. **Config paths** — Export Config → alert shows full save-dir path + clipboard copy; Import after placing `earlybuffer-config.json` via Files / Share / Finder.
6. **Replay drop** — place one `.gdr` (or `.gdr2` / `.mhr` / `.json` / `.slc`) in `replay-drop/` under the mod save dir → Import Replay succeeds or clear error.
7. **Constrained budgets** — with default sim/tick settings, Hybrid near a bake should not soft-lock; Lag Probe `HYBRID eb.hybrid.*` lines stay under a few ms when on.
8. **Cold start** — force-quit; reopen; mod still loads; no crash on PlanGold/load.

If step 2 fails, the `.geode` lacks the iOS binary — rebuild/merge. If step 5 fails with no path in the alert, capture a screenshot before changing I/O code.

## CI note

Multi-platform CI builds Windows, macOS, iOS, and Android. Local Android NDK builds above do not require running CI; local iOS builds need a macOS host + Geode iOS binaries.
