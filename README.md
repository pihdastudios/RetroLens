# RetroLens

RetroLens is a photo-only retro-effects camera for the Sony a5100 OpenMemories environment. It keeps Sony's normal preview, autofocus, and original JPEG capture while a bounded native C++ engine processes analytical preview frames, renders a full-screen custom interface, and saves a separate 320x240 RetroLens derivative.

This build deliberately contains no linked video runtime. The Movie button is consumed and reports that video processing is disabled. RetroLens never modifies firmware, calibration data, boot partitions, or Sony originals.

## Verification status

- All 70 preset graphs, the reduced JPEG path, worker lifecycle, persistence, JPEG output, gallery index, and photo transaction pass host ASan/UBSan tests.
- The legacy API-10 `armeabi` APK builds and verifies with the pinned toolchain.
- Earlier staged builds physically proved normal preview/capture, clean CameraSequence shutdown, the 8 FPS native display thread, analytical JPEG delivery, and moving Olive Pocket output on an ILCE-5100.
- The installed photo-runtime candidate displayed moving effects, but only in the deliberately small panel. The user also could not see the processed result in the in-app gallery.
- The `fullscreen-photo-20260722-i` candidate removes the small-window geometry request, fills the actual locked display buffer, and renders the saved processed thumbnail full-screen in the RetroLens gallery. It is host-tested but has not been installed or physically tested.
- The `startup-recovery-20260722-l` candidate removes storage and first-frame work from the startup-overlay gate. It keeps native output hidden until a filtered frame exists, reduces the splash to a compact Sony-preview fallback after 800 ms, and initializes derivative storage on the native photo worker. It passes host sanitizers and the legacy APK suite but is not hardware-tested or installed.

Exact staged evidence and preserved recovery hashes are in `DEVICE_FINDINGS.md`.

## Architecture

```text
CameraEx -> Sony normal preview SurfaceView -> autofocus + Sony original capture
CameraSequence worker -> one reusable direct JPEG buffer -> one bounded native JPEG slot
hidden native process worker -> reduced 80x60 decode -> preset graph + temporal history
first filtered frame -> reveal native surface -> full-screen 240x180 logical UI
native photo worker -> card verification -> one snapshot -> JPEG + JSON + thumbnail + atomic index
```

Java owns Android/Sony lifecycle, camera calls, one analytical polling thread, the direct buffer, physical-key routing, touch forwarding, and the display cadence runnable. Pixel processing, preset state, UI composition, derivative encoding, settings, gallery indexing, and photo file transactions are native.

Every Sony `DeviceMemory` is released in `finally`. Busy frames are dropped rather than queued. The analytical sequence starts after the hidden native runtime exists and does not wait for storage or a display surface. The opaque native surface is revealed only after a filtered frame is ready; until then the Sony preview remains visible. Pause stops CameraSequence polling, joins the analytical consumer, stops the native workers, and only then releases `CameraEx`; a Sony worker that does not stop causes the camera object to be quarantined rather than released beneath a live call.

## Toolchain

- JDK 8; Java source and target 1.6
- Gradle 2.14.1; Android plugin 2.2.3
- compile/min/target SDK 10; Build Tools 25.0.2
- Android NDK r14b; `ndk-build`; `armeabi`; GNU STL static
- NDK platform resolves to Android 9 because r14b has no literal `platforms/android-10`; the application contract remains API 10

OpenMemories framework and compile-only Sony stubs are pinned under `app/libs`; hashes and origins are in `ATTRIBUTION.md`. Stub classes are not packaged in the APK.

## Build, test, package, install

From `RetroLens/`:

```bash
./scripts/format.sh --check
./scripts/smoke-test.sh --host-only
./scripts/build-native.sh
./scripts/build-apk.sh --clean
./scripts/verify-apk.sh
./scripts/package-release.sh --existing
./scripts/install.sh releases/RetroLens-1.0.0-startup-recovery.apk
```

The normal build output is `app/build/outputs/apk/RetroLens-debug-1.0.0.apk`. Installation requires a Sony-PMCA-RE-compatible USB mode.

## Controls

| Camera control | Action |
| --- | --- |
| Left/right or either dial | Previous/next style; adjust the open quick control |
| Up/down | Previous/next category; choose Intensity, Contrast, Grain, or Motion in quick controls |
| Center | Open/close quick controls; confirm gallery deletion |
| Menu | Open/close the style browser |
| Playback | Open/close the RetroLens gallery |
| Half shutter | Sony autofocus |
| Full shutter | Sony original plus queued RetroLens derivative |
| C1 | Hold original/effect split compare |
| Fn | Show/hide diagnostics |
| Delete | Back; from gallery, request delete confirmation; from camera, exit |
| Movie | Display `PHOTO ONLY - VIDEO OFF`; no recorder is opened |

Touch supports horizontal style swipes, tap-to-hide/show controls, and long-press compare. Touch-to-focus is not enabled because a safe private Sony API has not been verified.

## Interface and presets

The native UI uses a dark charcoal base, warm text, restrained mint accent, a five-card style browser, quick controls, compare labels, focus and capture state, hidden diagnostics, and an app-only gallery. Its compact camera bars auto-hide after two seconds and reappear after input; modal drawers and gallery scenes remain visible. The full startup panel has an 800 ms deadline, then becomes a small `EFFECTS STARTING` strip over the usable Sony preview. A successful first filtered surface removes the Java view from layout entirely so it cannot cover native filter names. Its logical 240x180 compositor is center-crop scaled across the actual locked display buffer instead of requesting a small Android window. Surface, sequence, or decode failure hides native output and preserves Sony preview and capture.

All 70 requested presets ship across Handheld, Computer, Console, Analog TV, Tape, Film, Archive, Print, Digital Decay, Game Era, and Experimental categories. This includes Olive Pocket, Soviet Archive 1978, Piss Filter 2007 / Seventh-Gen Amber, Comic Ink, Newsprint, and Thermal False Color. Presets are declarative bounded graphs using shared integer passes. Thermal and infrared modes are simulated color mappings, not sensor measurements.

Each preset retains independent Contrast, Grain, and Motion adjustments; master Intensity, favorites, recent styles, the selected style, and diagnostics state are versioned and persisted atomically. Temporal history is bounded to the previous frame and resets on style changes.

## Still photographs

A full shutter press first invokes the verified Sony `takePicture(null, null, null)` path. The Sony camera owns and saves the full-resolution original. RetroLens never guesses its path and never overwrites it.

When a processed analytical frame and at least 64 MiB of free storage are available, one photo job is queued. The writer saves:

```text
RETROLENS/PHOTOS/<timestamp>_<preset>_preview.jpg
RETROLENS/PHOTOS/<timestamp>_<preset>_preview.json
RETROLENS/THUMBNAILS/<timestamp>_<preset>_preview.rgb
```

The derivative is explicitly preview-resolution at 320x240. Its sidecar records preset, master intensity, per-preset adjustments, camera model, app version, source timestamp, and that the Sony original was preserved but its path is unknown. Scanline, mask, and vignette texture are included in the saved derivative. JPEG, sidecar, thumbnail, settings, and index files use temporary files, flush/sync, and atomic rename; partial save failure removes the transaction's preceding files.

After activity resume, Java resolves the absolute Environment path without touching the card and starts camera/analysis immediately. The existing bounded native photo worker independently creates the output tree, checks available space, and proves write/close/rename/read-back behavior before enabling derivatives. Card initialization therefore cannot hold the splash or filtered preview. While it runs the UI reports `RETRO STORAGE STARTING`; hard permission, read-only, I/O, index, and no-space failures disable only the derivative. The logger buffers normal startup messages and flushes after camera teardown instead of syncing the card on the UI thread. Native diagnostics distinguish initialization, readiness, directory, write-test, low-space, and index states; Sony capture remains available throughout.

Full-resolution post-processing remains disabled until safe discovery of the newly captured Sony JPEG and a memory-bounded tile path are physically proven.

## Gallery and storage

The gallery indexes at most 48 RetroLens-produced photos and loads one fixed 80x60 processed thumbnail at a time. The newest completed save is placed into the gallery slot immediately after the durable transaction, and Playback displays the selected processed image full-screen with its preset and `PROCESSED 320X240` label. It does not scan the full card or register files with Sony's private playback database. Delete requires confirmation and removes only the selected derivative, its sidecar, and its app thumbnail.

Startup removes bounded stale `.tmp` transaction files. If the gallery index is missing, malformed, or points at incomplete entries, it is rebuilt from at most 512 inspected photo names while retaining at most the newest 48 complete JPEG/JSON/thumbnail sets. Orphan and incomplete files are never presented as successful gallery entries.

```text
RETROLENS/
├── CONFIG/settings.cfg
├── PHOTOS/
├── THUMBNAILS/index.txt
└── LOG.TXT
```

There is intentionally no `CLIPS` directory in the active photo runtime. The app requests camera and external-storage write permissions, but no Internet or microphone permission.

## Performance and memory

The analytical source requests at most one Sony frame and uses one 256 KiB direct buffer. Native memory is fixed: compressed input, raw, output, previous, photo snapshot, gallery thumbnail, one 320x240 scaling buffer, and one bounded 512 KiB JPEG buffer. There is one process worker and one photo writer, never a thread per frame and never an unbounded queue. There are no per-frame Java bitmaps or pixel operations.

PicoJPEG reduced decode produces an 80x60 working image, chosen to fit the a5100's measured 9-10 FPS analytical source and constrained CPU/memory budget. Integer processing and RGB565 output are preferable here to adding a larger decoder, OpenCV, FFmpeg, assembly, or speculative SIMD before device profiling identifies a real bottleneck.

## Troubleshooting and limitations

- The earlier small-panel behavior is physically observed. The fullscreen candidate is a new uninstalled build and must not be described as device-proven until its locked-buffer dimensions and complete screen coverage are observed on-camera.
- A frozen moving area with a working Sony preview indicates a decode/cadence issue. Fn opens rate-limited diagnostics.
- If effects are delayed, the full splash must reduce to `EFFECTS STARTING` within 800 ms while Sony preview and shutter remain usable. A permanent full-screen splash is a startup-state regression.
- If `Writing memory` persists after exit, stop testing this candidate and reinstall `releases/RetroLens-1.0.0-safe-preview.apk`; avoid battery removal while the card LED is active unless the camera is irrecoverably wedged.
- If derivative saving fails, confirm free card space and inspect `RETROLENS/LOG.TXT`. A healthy startup records `Storage: status=1`, the canonical root, free bytes, and `PhotoRuntime: storage ready`. The Sony original should still exist independently.
- Use `./scripts/toolchain-info.sh` for missing tools and `./scripts/usb-status.sh` for installation connectivity.

Unresolved Sony questions are full-resolution captured-JPEG discovery, safe full-screen processed output, exact format-256 semantics beyond observed JPEG markers, and private movie control. Video is outside this build and no filtered-video, audio, HD, encoder, or ISP claim is made.

See `LICENSES.md` and `ATTRIBUTION.md`. RetroLens does not vendor OpenCV, FFmpeg, RetroArch, or shader collections and has no network feature.
