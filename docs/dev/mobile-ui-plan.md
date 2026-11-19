# Desktop and mobile UI plan

Target platforms are Windows and macOS for Desktop, Android and iOS for Mobile. This is a planning document. Implement each AP and record its validation before marking it complete. Seasonal effects are outside this task.

## Product contract

| Capability | Windows / macOS | Android / iOS |
| --- | --- | --- |
| Main, Assist, Learn, Modes, Visual, UI and supported Dev tools | Desktop layout | Responsive touch layout |
| Classic sidebar material and accent palette | Available | Available with bounded rendering cost |
| Background dimming | Available | Available |
| Sidebar Blur, Background Blur, Liquid Glass | Available | Unavailable |
| Keys tab and keybind controls | Available | Hidden |
| Numeric entry | Right-click or keyboard | Tap the displayed value |
| Open settings | Pause EB button, Geode settings, menu hotkey | Pause EB button and Geode settings |
| Classic Geode settings | Optional fallback | Available as recovery/fallback |

Platform capabilities are explicit. Connecting a keyboard to a phone does not enable the desktop Keys UI. Touch input on desktop may use the touch entry path without changing shader availability. Mobile must not initialize blur/glass programs, framebuffers, captures or animation state. Both interfaces operate on the same gameplay settings and validation rules.

## AP01 — Establish platform capabilities and build boundaries

- [ ] Audit every `EB_DESKTOP_IMGUI` branch and the bundled imgui-cocos integration. Identify which restrictions are product choices and which represent platform dependencies.
- [ ] Introduce one capability interface using Geode's Windows/macOS/Android/iOS build definitions. Define desktop layout, mobile layout, keybind UI, Classic material, backdrop dimming and expensive material support independently.
- [ ] Separate shared settings/widget behavior from platform-specific input and renderer code. Keep Windows headers and native mouse polling inside Windows-only compilation.
- [ ] Compile Blur and Liquid Glass implementations only for Windows/macOS. Mobile material dispatch must reject these effects even if their settings are imported or left over from another platform.
- [ ] Preserve serialized keys and gameplay behavior. Unsupported appearance preferences may remain stored for desktop use, while mobile resolves them to supported materials.
- [ ] Verify all four platform builds and document the available ImGui renderer/input backend per platform before integrating the mobile shell.

Acceptance: capability resolution is testable, and mobile has no reachable expensive-material initialization path.

Implementation record — AP01

- Added `EbUiCapabilities` with explicit Windows, macOS, Android, iOS and Unknown platform resolution.
- CMake defines the target identity and enables the desktop GPU material boundary only on Windows/macOS.
- The current ImGui implementation remains desktop-only. Mobile continues through the existing classic settings fallback while the shared touch shell is developed in later APs.
- The GPU implementation additionally checks desktop platform identity. Future mobile ImGui callers receive an unavailable backend without GL resource initialization.
- Sidebar and background dispatch resolve expensive effects off on unsupported platforms while retaining stored appearance preferences.
- The existing backend provides desktop GL rendering and Cocos touch-to-left-click input. System keyboard integration and a dedicated mobile renderer/input audit remain work for AP03/AP04.
- Existing UI implementations contain 41 desktop-ImGui guard sites. Shared layout extraction and mobile navigation remain future work. Keybind visibility is exposed as a capability, with consumer migration planned in AP05.
- Standalone capability checks passed for all four named platform identities and Unknown, including an erroneously enabled expensive-material flag on mobile.
- Full macOS/Android/iOS builds and device checks remain pending. AP01 cross-platform build acceptance remains open.

## AP02 — Define desktop and mobile navigation

- [ ] Keep the existing sidebar shell on Windows/macOS.
- [ ] Build the mobile shell around the available safe viewport in landscape and supported portrait orientations. Apply safe-area insets around notches, rounded corners and system gesture regions.
- [ ] Use compact category navigation on small screens. A dropdown or compact category strip selects Main, Assist, Learn, Modes, Visual, UI and supported Dev content. Avoid fitting the entire desktop sidebar into a narrow phone viewport.
- [ ] Share the actual panel definitions and setting bindings wherever feasible. Adapt row layout and widths at a small set of documented breakpoints.
- [ ] On narrow screens, place a control below its label when a horizontal row would truncate text or shrink the touch target.
- [ ] Define a consistent minimum touch target, initially 44 logical points, and verify its mapping to actual device scale. Keep readable text independent of raw framebuffer pixels.
- [ ] Preserve scroll position per category and clamp it after viewport, orientation or content changes.
- [ ] Keep navigation, close and recovery controls reachable with every supported UI scale. Add a fit-to-viewport effective size while preserving the user's stored preference.

Acceptance: all settings can be reached on the smallest supported phone layout without overlapping controls or horizontally clipped labels.

Implementation record — AP02 (navigation foundation; acceptance remains open)

- Windows/macOS retain the desktop sidebar and classic category strip.
- Android/iOS now use a compact category title with previous/next controls in the existing Geode settings dialog. Targets are 44 Cocos logical units; actual physical device mapping is still unverified. Navigation skips Keys.
- Category navigation continues to use the same setting nodes and validation. Mobile category switches retain a bounded scroll offset for each category during the popup lifetime.
- The category bar follows changes to the settings list width. Navigation callbacks are deferred until touch dispatch completes.
- This is an incremental navigation foundation in the recovery dialog, not the finished custom mobile shell. Safe-area discovery, portrait layout, responsive stacked setting rows, mobile appearance, and effective UI-scale fitting remain outstanding. Do not mark AP02 complete until those paths and real-device accessibility are verified.

## AP03 — Implement mobile touch input

- [ ] Audit the backend's touch events, coordinate conversions and mouse emulation. Ensure one finger does not generate duplicate activation through multiple input paths.
- [ ] Implement touch scrolling with drag threshold, velocity tracking and bounded inertia. A vertical scroll gesture must cancel a pending button press.
- [ ] Distinguish a horizontal slider drag from a vertical page gesture. Require deliberate horizontal movement before taking slider ownership.
- [ ] Route a gesture to the deepest scrollable region under its starting point. Parent and child must not scroll together.
- [ ] Give dropdowns, color pickers and modal sheets explicit gesture ownership. Prevent a tap through a closing popup from activating gameplay or a lower control.
- [ ] Handle touch cancellation, app backgrounding, lost focus and a second finger during editing or dragging. Clear held UI state on interruption.
- [ ] Verify opening, closing and returning to gameplay with no stuck input or injected gameplay tap.

Acceptance: touch ownership stays stable through scrolling, slider editing and nested popups.

Implementation record — AP03 (existing-dialog integration; acceptance remains open)

- Audited the installed Geode ScrollLayer implementation: it already delegates drag/inertia to CCScrollLayerExt, forwards cancellation, and can cancel child touches after a vertical movement threshold. No additional mouse emulation or duplicate touch delegate was added.
- Mobile settings lists now enable that existing cancellation mechanism with a 10-local-unit threshold converted to world coordinates. Desktop behavior is unchanged.
- Category changes and popup exit cancel an active list drag before replacing content or leaving the scene.
- This does not yet implement direction locking for sliders, deepest-child gesture ownership, multi-finger handling, focus/background interruption, or popup tap-through protection. Geode's built-in cancellation is based on vertical displacement alone; it is not a complete axis-aware gesture router.
- Style audit and the Windows build/package/install passed. Android/iOS compilation and actual touch interaction remain unverified. The custom mobile shell is still outstanding under AP02, so AP03 acceptance remains open as well.

## AP04 — Mobile numeric entry and keyboard handling

- [ ] Make the displayed slider value a clear tap target on mobile. Preserve right-click direct entry on desktop.
- [ ] Connect text focus to the platform keyboard using the supported Geode/Cocos text-input mechanism. Verify the backend actually receives inserted text and editing events.
- [ ] Use suitable integer/decimal keyboard types while retaining negative values where a setting requires them. Support paste and decimal formatting deliberately.
- [ ] Reuse existing range, finite-number and integer validation. Direct entry may use exact values between drag steps.
- [ ] Provide visible Apply and Cancel actions on mobile, including when the keyboard lacks a useful Enter key.
- [ ] Scroll the active field into view when the keyboard appears. Keep it visible after rotation and restore the page position sensibly after dismissal.
- [ ] Cancel editing when its setting becomes disabled or its page closes. Preserve uncommitted text until explicit commit or cancellation within the active editor.
- [ ] Test empty input, overflow, invalid paste, selecting text, backspace, external keyboard events and app interruption.

Acceptance: every numeric setting can be edited with touch and a system keyboard without requiring a keybind.

Implementation record — AP04 (Android test13)

- Mobile sliders reserve a 44-unit header with a 96-unit-wide value activation region. Tapping that region opens a modal numeric editor without first changing the slider value. Desktop right-click editing is retained.
- The editor uses the existing ImGui/Cocos IME bridge, selects the initial value, filters numeric characters and provides explicit Apply/Cancel actions. Parsed values must be finite, integer where required, and are clamped to the existing setting range. Drag-step quantization is not imposed on typed values.
- Modal placement is near the viewport top rather than the keyboard-covered bottom. Closing the shell, changing category or disabling the setting cancels editing. Explicit cancellation retains the stored value.
- Added a null check to the generated Android IME adapter for content requests after text focus disappears. Opening diagnostic notifications have been removed.
- Android native build and style audit passed. The backend currently requests the standard Cocos system keyboard; a dedicated native decimal/integer keyboard layout is not yet selected. Device tests must verify actual keyboard appearance, text insertion/deletion/paste, keyboard dismissal, rotation and app interruption. AP04 acceptance remains open until these pass; iOS integration is still pending.

## AP05 — Desktop/mobile settings visibility

- [ ] Hide the Keys tab, individual hotkey editors and desktop menu hotkey instructions on Android/iOS.
- [ ] Hide Classic Settings Menu's desktop-switch wording on mobile. Present an explicit recovery route to the classic Geode interface.
- [ ] In mobile Sidebar Materials, expose supported Classic controls. Omit Blur and Liquid Glass from Type options.
- [ ] In mobile Game Background, expose Dimmed. Omit Background Blur and its radius, opacity and quality controls.
- [ ] Hide desktop Shader Quality on mobile. If Classic needs a quality control, expose a clearly named mobile control with a real mobile implementation.
- [ ] Make Desktop/Mobile availability explicit in documentation and shared setting descriptions. Use “Windows/macOS” and “Android/iOS” where “desktop” would otherwise be ambiguous.
- [ ] Audit Learn's advanced controls, the timing inspector, Cloud Configs, file actions and Dev diagnostics for touch access. Keep operations disabled with a useful reason when a platform dependency is genuinely unavailable.
- [ ] Verify that no required gameplay operation is accessible only through a hotkey.

Acceptance: mobile presents a coherent set of supported options and each visible control has an effective implementation.

Implementation record — AP05

- New ImGui mobile UI exposes Classic sidebar and Dimmed backdrop only; Blur, Liquid Glass and desktop Shader Quality controls are omitted. Active-material text uses effective platform capabilities rather than reporting imported unsupported effects as active.
- Keys navigation, direct Keys panel drawing and individual keybind-setting drawing are guarded by keybind capability. Programmatic selection of Keys on mobile returns to Main.
- Mobile Interface has an explicit Open Geode Settings recovery action. It closes ImGui and opens the existing Geode popup without changing the saved menu preference. The classic Geode UI remains unchanged as explicitly requested by the user.
- Mobile material resets affect supported values only. Stored desktop Blur/Glass parameters remain available for later export or desktop use. Shared config keys and schema are unchanged.
- Learn, inspector, Cloud and Dev actions retain their shared panel definitions. Config import/export already has non-Windows save-folder paths and explanatory transfer messages; inspector has a nested scrollbar. Touch usability, system file access and external URL actions still require actual-device verification under AP09. No newly unsupported operation was identified that needs disabling here.
- Android compilation/package and style audit passed; desktop build and device UI review are recorded separately. AP05 device acceptance remains pending; iOS is still not integrated into the ImGui build.

## AP06 — Mobile appearance and renderer

- [ ] Keep the shared accent-derived palette, contrast settings, supported brightness controls and background dimming.
- [ ] Implement Classic using a bounded GPU pass where supported. Establish a static accent-gradient fallback for shader failure or low-cost mode.
- [ ] Avoid the existing dense CPU wave sampling as an unrestricted mobile fallback. Bound its geometry or use the static fallback.
- [ ] Use a low-cost mobile default and offer animation reduction independently from gameplay assist.
- [ ] Enforce zero screen captures and zero blur/glass framebuffer allocation on mobile.
- [ ] Pause material animation while hidden, when the application is backgrounded and when the menu closes. Release resources safely on renderer/context recreation.
- [ ] Restore renderer state after custom GPU work. Check interaction with Geometry Dash overlays, pause menus and other Geode interfaces.
- [ ] Verify premultiplied/straight alpha consistency, clipping, corners and text quality on high-density screens.

Acceptance: mobile appearance preserves the visual identity with a bounded rendering path and working failure recovery.

## AP07 — Shared configuration and migration

- [ ] Reuse the existing setting store and config bundle format. Apply platform capabilities when resolving effective settings.
- [ ] Import a desktop config on mobile containing enabled Blur/Glass and desktop scale/keybind values. Verify supported gameplay settings apply while unsupported effects cannot execute.
- [ ] Retain imported desktop appearance preferences for later export, unless the user explicitly resets them. Document effective mobile fallback in the UI where needed.
- [ ] Register new mobile-specific settings in schema, categories, cache/watchers and export/import whitelist.
- [ ] Validate unsupported enum values, malformed material JSON, extreme scales and old settings. Provide safe defaults without rewriting unrelated preferences.
- [ ] Test classic Geode settings and the new mobile shell editing the same values. Verify restart persistence and level changes.

Acceptance: switching interfaces and importing cross-platform settings keeps stored and effective behavior understandable.

## AP08 — Mobile performance gate

- [ ] Name reference devices, OS versions, screen resolutions, refresh rates and Geode versions. Include an older iPhone and an older Samsung/Android device where available.
- [ ] Measure menu closed, static menu, animated Classic, rapid scrolling, dropdowns, color editing, keyboard entry, Learn inspection and Cloud browsing.
- [ ] Compare CPU/GPU frame p50/p95/p99, allocation rate and memory against the classic Geode interface and menu-closed baseline.
- [ ] Run a sustained session to observe thermal behavior. Record gameplay timing around menu close and app resume.
- [ ] Confirm that shader-unavailable mobile settings never trigger capture or unsupported GL calls, including after config import.
- [ ] Verify no repeated resource allocation at stable size and no unbounded widget animation, log or inspection caches.
- [ ] Choose mobile defaults from device measurements. Any adaptive quality change needs hysteresis and must preserve input responsiveness.
- [ ] Report measured results and pending devices explicitly. Treat performance acceptance as open until actual device runs support it.

Acceptance: supported mobile devices meet an agreed frame-time and memory budget with no recurring UI-induced gameplay hitch after closing.

## AP09 — Cross-platform interaction and release gate

- [ ] Run Windows/macOS regressions for mouse drag, wheel scrolling, right-click entry, keybind capture and desktop shaders.
- [ ] Run Android/iOS checks for touch targets, keyboard focus, rotation, safe areas, nested scrolling, app interruption and recovery.
- [ ] Verify every category, disabled control, long label, dropdown, modal and file/config action on the smallest supported viewport.
- [ ] Test desktop scaling at 75/100/125/150% and mobile fit-to-screen behavior.
- [ ] Keep recorded input-planning regression requirements when shared code changes reach gameplay paths.
- [ ] Run C++ formatting/style audit, applicable standalone tests, Geode builds and package validation for Windows, macOS, Android32, Android64 and iOS as applicable to the supported ship line.
- [ ] Retain GD 2.2081 and Geode SDK 5.11.1 requirements until a separately requested compatibility update.
- [ ] Document the platform feature matrix, mobile entry points and fallback behavior. Assign the release version with the user after acceptance.
- [ ] Leave commits, publishing and release upload to the user.

## Completion record

For each AP, record changed classes/settings, tested platforms, build/package identifiers, measured results and remaining checks. A successful desktop build does not close mobile device acceptance.


## Android ImGui test build — corrected integration

The user clarified that the Geode settings UI must remain unchanged. The AP02 compact classic category bar and AP03 classic touch modifications have therefore been reverted. They are not the mobile ImGui implementation.

Android now fetches and links the existing pinned imgui-cocos backend and compiles the shared ImGui panels. The Pause EB entry respects Classic Settings Menu; ordinary Geode settings remain available. iOS is still excluded pending its backend/input integration. Desktop GPU materials remain behind their independent desktop-only build flag.

For this initial Android test the shared shell fits to the viewport, hides Keys, adds a touch-accessible Close button and exposes a draggable content scrollbar. Classic uses a static accent fill to avoid the dense CPU wave fallback. Full gesture scrolling, mobile panel layout, safe-area handling and unsupported appearance-control visibility remain pending. This is a renderer/menu integration test, not AP02/AP03 acceptance.


## Android device feedback — Test05/Test06

- Test05 confirmed actual ImGui rendering on the user's Android device through a scene overlay. Director hooks alone did not reach the draw callback on that device.
- Test05 did not deliver ImGui interactions and leaked touches to the underlying pause menu.
- Test06 replaces upstream touch forwarding on Android with a targeted, swallowing Cocos delegate attached to the overlay. It feeds a single primary touch in framebuffer coordinates, consumes additional fingers, and releases/cancels on touch completion and scene exit. The existing shared scale hook converts queued coordinates to logical UI coordinates.
- Generated Android backend adapters preserve upstream sources and disable their duplicate rendering, touch forwarding and desktop mouse-position polling. Desktop builds continue using the original backend.
- Android rendering and input remain device-test gated. Direction-aware gesture scrolling, focus/background handling, safe areas and mobile layout remain outstanding; this does not complete AP03.


## Android Classic — test09

- Added a GLES2-compatible procedural Classic material through CCGLProgram and a persistent CCRenderTexture (128 x 384 Cocos units; physical texture dimensions depend on content scale). No scene capture, blur pass or dense CPU mesh is used.
- Four fixed wave iterations, cached uniform locations, linear texture filtering and padded-texture UV handling. Existing accent/brightness/speed controls remain shared. Classic enable fades are applied when compositing.
- Stable Still preset output is reused until time, shape or colors change. Rendering only occurs when the material is requested; shader failure retains static fill.
- Android native compilation/package and style audit passed. Runtime shader compilation, device appearance, resume/context recovery and measured frame times remain device-test gated. This does not certify the mobile performance gate.


## Android Classic — test10 visual corrections

Device screenshot from test09 showed opaque glyph rectangles, incomplete material coverage and desktop drag trails. The material pass changed blending to ONE/ZERO during ImGui frame construction without restoring the pre-pass blend state. Added scoped GL-state preservation around initialization and actual offscreen updates, including framebuffer, viewport, scissor, blend factors/enables, clear color, color mask, texture units/bindings, buffers and program. Cocos state cache is invalidated at pass boundaries so later Cocos-backed ImGui rendering rebinds its cached resources. Stable Still frames bypass those state queries.

The source quad now scales by its actual Cocos content dimensions rather than assuming a one-unit sprite, and Android drag trails are disabled. Device visual confirmation remains pending.


## Android Classic — test11 hang correction candidate

Test10 device feedback reports a complete frame freeze on opening. Removed global ccGLInvalidateStateCache calls from the in-visit material pass; restoration now uses targeted Cocos program/texture/blend cache APIs alongside explicit viewport/FBO/scissor restoration. Global invalidation is unsafe to assume is cache-only while Cocos matrix/visit state is active. The exact on-device hang location remains unconfirmed. Test11 requires device validation for both opening and the previous font/coverage defects.
