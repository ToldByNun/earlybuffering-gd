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

## CI note

Multi-platform CI builds Windows, macOS, iOS, and Android. iOS GeodeBindings needs an FMOD force-include (see `CMakeLists.txt`) because codegen emits FMOD stubs without including `fmod.h`. Local Android builds above do not require running CI.
