# Install from GitHub Releases

This is the **normal** install path. EarlyBuffer is **not** on the Geode Index.

1. Open [Releases](https://github.com/ToldByNun/earlybuffering-gd/releases).
2. Download **`audesign.earlybuffer.geode`** for that tag. Keep this exact filename.
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
python tools/geode_package_check.py path/to/audesign.earlybuffer.geode --require win --require android64 --forbid-zstd
```

Exit code `0` = required binaries are inside the zip, and (with `--forbid-zstd`) no ZIP method 93. Prefer **v1.6.0+** release packages on Android.

## iOS file import: keep the canonical filename

The iOS launcher derives the binary name from the imported archive filename.
`audesign.earlybuffer-v1.6.2.geode` makes it look for
`audesign.earlybuffer-v1.6.2.ios.dylib`, while our binary is
`audesign.earlybuffer.ios.dylib`. The package can contain the correct signed
binary and still fail during launcher import.

Publish the asset as **`audesign.earlybuffer.geode`**; put the version in the
release tag and title. Do not append a version, `-ios`, or a download suffix
such as ` (1)` to the install filename.

If already affected, fully close GD, remove the old EarlyBuffer installation
through Geode, and import the canonical file through the iOS launcher, then
complete its signing/rebuild process and launch again. Keep saved settings.
If it still fails, collect the launcher log and Geode log, launcher/Geode
versions, installation mode (JIT, JIT-less, TrollStore), and the installed
package's SHA256; the missing-binary message alone cannot prove a cache fault.

Maintainer release gate (run on the final upload file, after any renaming):

```bash
python tools/geode_package_check.py path/to/audesign.earlybuffer.geode --require win --require android32 --require android64 --require mac --require ios --forbid-zstd --check-install-name
```

Source: [iOS launcher file import](https://github.com/geode-sdk/ios-launcher/blob/main/src/AppDelegate.m)
and [launcher extraction/signing preparation](https://github.com/geode-sdk/ios-launcher/blob/main/src/Patcher.mm).
