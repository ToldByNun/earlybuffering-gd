# Phone install (Android / iOS)

Same mod idea as PC. The important part is: **the `.geode` must include the phone binary**.

## Android

1. Install Geode for GD **2.2081** on the phone.
2. Install a package that contains  
   `audesign.earlybuffer.android64.so`  
   (modern 64-bit phones).
3. Restart the game.

### Classic mistake

Copying a Windows-only build to Android. Geode then says it cannot find:

`audesign.earlybuffer.android64.so`

Fix: use a release / merged package that actually has that file. See [Mobile problems](/troubleshoot/mobile).

Also prefer **v1.6.0+** packages (deflate-only). Older **v1.5.1** zstd zips can fail with `Unable to read entry (code -3)`.

## iOS

1. Geode + GD **2.2081** on device.
2. Package must include `audesign.earlybuffer.ios.dylib` (signed as required by your Geode / sideload path).
3. Restart.

## Files on device

Save-dir I/O matches desktop ideas:

- config / cloud cache
- optional `replay-drop/` for replay bakes

Budgets on phone are tighter — if assist feels “PassBudget-y”, lower Adaptive / tick pressure or use simpler Live settings. See [Configure → Assist](/configure/assist).
