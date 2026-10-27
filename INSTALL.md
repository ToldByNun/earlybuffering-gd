# frirecracker - build / install

## Requirements

- Geometry Dash 2.2081
- Geode 5.11.1+
- CMake + C++20 toolchain (MSVC on Windows)

## Build (Windows)

```
geode build --config RelWithDebInfo
```

Or configure/build with CMake into `build/`, then package with Geode CLI.

## Install

Copy the produced `.geode` into your Geode mods folder, or use `geode run` / Geode install from the CLI.

## Hardening notes

- Assist is disabled in platformer.
- Bake is main-thread only (BakeScheduler); cancel from pause UI.
- Profiles/bakes key off LevelContentKey (id + content revision hash).
- On load, PlanGoldCorpus + shadow self-check run once (console).