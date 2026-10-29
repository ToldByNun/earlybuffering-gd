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

Always verify Android packages with the package check (below) before copying them to a phone.

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

1. Install the [Android NDK](https://developer.android.com/ndk) and set **`ANDROID_NDK_ROOT`** to its extract path.
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

### Merge with Windows (pointer)

A phone-only Android64 `.geode` is enough to test on device. A release that also supports Windows needs both binaries in **one** `.geode`.

High-level steps (full merge helper lands with the merge tooling commit):

1. Build Windows: `geode build --config RelWithDebInfo` → `build/audesign.earlybuffer.geode`
2. Build Android64 as above → `build-android64/audesign.earlybuffer.geode`
3. Merge with Geode CLI (`geode package merge …` — **mutates the first argument**; copy the Windows zip first if you need to keep it)
4. Run `tools/geode_package_check.py` with `--require android64` (and `--require win` for dual ships)
5. Only then install or publish

Until the merge helper script exists, prefer a green CI combine artifact **or** run `geode package merge` manually and always run the package check.

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

## CI note

Multi-platform CI builds Windows, macOS, iOS, and Android. iOS GeodeBindings needs an FMOD force-include (see `CMakeLists.txt`) because codegen emits FMOD stubs without including `fmod.h`. Local Android builds above do not require running CI.
