# Build & package

## Requirements

- GD **2.2081**
- Geode SDK / CLI **5.11.1+**
- `GEODE_SDK` set
- CMake 3.21+, C++23 (MSVC on Windows)

## Day-to-day (Windows)

```bash
geode build --config RelWithDebInfo
```

Local Windows packages usually contain **only** the Windows binary. Do not ship that zip to Android/iOS as-is.

## Android64

```bash
geode sdk install-binaries -p android64
geode build -p android64 --config RelWithDebInfo
python tools/geode_package_check.py build-android64/audesign.earlybuffer.geode --require android64
```

## Package honesty

```bash
python tools/geode_package_check.py path/to/merged.geode --require win --require android64 --forbid-zstd
# add --require ios when the dylib is present
```

Exit `0` = required binaries inside the zip, and (with `--forbid-zstd`) no ZIP method 93.

**Android `Unable to read entry (code -3)`:** usually zstd-compressed `.geode` entries from `geode package merge --zstd` / CI combine default. Ship with deflate only (`zstd: false` / no `--zstd`).

## CI

GitHub Actions: `.github/workflows/multi-platform.yml` builds platforms for releases.

Docs site: `.github/workflows/docs.yml` → GitHub Pages.

## Full maintainer notes

Repo root `INSTALL.md` has NDK, merge, iOS, and smoke checklists.
