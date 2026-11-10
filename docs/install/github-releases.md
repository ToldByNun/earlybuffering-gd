# Install from GitHub Releases

This is the **normal** install path. EarlyBuffer is **not** on the Geode Index.

1. Open [Releases](https://github.com/ToldByNun/earlybuffering-gd/releases).
2. Download the `.geode` file for that tag.
3. Put it into your Geode mods folder  
   (Geode → open mods folder, or “install from file” if your Geode build has it).
4. Restart GD.
5. If prompted, install **geode.node-ids** from the Index (dependency only).

## Multi-platform packages

A good release zip for everyone should contain binaries for the platforms you care about, for example:

- Windows `.dll`
- Android `audesign.earlybuffer.android64.so`
- iOS `audesign.earlybuffer.ios.dylib`

If you only built on Windows locally, that package is **Windows-only** and will fail on phone. See [Mobile](/install/mobile).

## Verify a package (optional)

Maintainers can check:

```bash
python tools/geode_package_check.py path/to/audesign.earlybuffer.geode --require win --require android64
```

Exit code `0` = required binaries are inside the zip.
