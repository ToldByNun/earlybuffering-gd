# Mobile package errors

## “Unable to find … android64.so”

Your `.geode` is almost certainly a **Windows-only** build.

Fix:

1. Install from a [Release](https://github.com/ToldByNun/earlybuffering-gd/releases) that lists Android support, **or**
2. Use a merged package that contains `audesign.earlybuffer.android64.so`

Check (on PC):

```bash
python tools/geode_package_check.py path/to/file.geode --require android64 --forbid-zstd
```

## “Unable to read entry (code -3)” / DOWNLOAD FAILED

The zip entry failed to decompress (`MZ_DATA_ERROR`). EarlyBuffer **v1.5.1** release packages used **zstd** (ZIP method 93); many Android Geode builds choke on that.

Fix: install **v1.6.0+** (deflate-only ship). On PC, reject zstd packages with `--forbid-zstd` as above.

## iOS missing dylib

Same idea: package needs `audesign.earlybuffer.ios.dylib` and whatever signing / sideload steps your Geode path requires.

## Mod installs but assist feels empty

Phone budgets are tighter. Try:

- Adaptive off
- Smaller windows
- Orbs off
- Trigger World only if you need it

See [Mobile install](/install/mobile).
