# Build from source

For people who change code or ship custom packages.

## Needs

- GD **2.2081**
- Geode SDK / CLI **5.11.1+**
- CMake 3.21+
- C++23 toolchain (MSVC on Windows)
- `GEODE_SDK` set to your SDK folder

## Quick build (Windows)

```bash
geode build --config RelWithDebInfo
```

That local package is usually **Windows-only**. Do not put it on Android as-is.

## Android64 (maintainer)

```bash
geode sdk install-binaries -p android64
geode build -p android64 --config RelWithDebInfo
python tools/geode_package_check.py build-android64/audesign.earlybuffer.geode --require android64
```

Full notes live in the repo file `INSTALL.md` (CMake, NDK, merge, iOS).

## Docs site locally

```bash
npm install
npm run docs:dev
```

Open the URL VitePress prints (usually `http://localhost:5173/earlybuffering-gd/`).
