# Build & install

## Requirements

- Geometry Dash **2.2081**
- [Geode](https://geode-sdk.org/) **5.11.1+** (SDK + CLI)
- CMake 3.21+ and a **C++23** toolchain (MSVC on Windows)

Set `GEODE_SDK` to your Geode SDK checkout.

## Quick build (recommended)

```bash
geode build --config RelWithDebInfo
```

The CLI packages and can install the `.geode` into your mods folder.

## CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

Then package with the Geode CLI, or use `setup_geode_mod` output from the build tree.

## Platforms

Local development is primarily **Windows**. Multi-platform binaries are produced by GitHub Actions (Windows, macOS, iOS, Android32/64).

## After install

1. Enable the mod in Geode.
2. Ensure **geode.node-ids** is installed.
3. Prefer **Safe Mode** on for online / rated play.
4. Platformer levels: assist stays off by design.

## CI note

GitHub Actions builds Windows, macOS, and Android. The iOS job is currently **continue-on-error** because GeodeBindings fails to compile FMOD stubs under the runner's Xcode 26 toolchain (upstream, not EarlyBuffer source). Re-enable hard-fail when Geode/bindings fix lands.
