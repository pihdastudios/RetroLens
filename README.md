# RetroLens

RetroLens is a photo-only retro-effects camera for the Sony a5100 OpenMemories environment. It keeps Sony's normal preview, autofocus, and original JPEG capture while a bounded native C++ engine processes analytical preview frames, renders a compact custom interface, and saves a separate 320x240 RetroLens derivative.

This build deliberately contains no linked video runtime. The Movie button is consumed and reports that video processing is disabled. RetroLens never modifies firmware, calibration data, boot partitions, or Sony originals.

## Verification status

- All 70 preset graphs, the reduced JPEG path, worker lifecycle, persistence, JPEG output, gallery index, and photo transaction pass host ASan/UBSan tests.
- The legacy API-10 `armeabi` APK builds and verifies with the pinned toolchain.
- Earlier staged builds physically proved normal preview/capture, clean CameraSequence shutdown, the 8 FPS native display thread, analytical JPEG delivery, and moving Olive Pocket output on an ILCE-5100.
- The user reported the latest style panel still updates only in its top-left area, approximately one quarter of the camera display, while the rest remains static black or olive. This geometry defect is intentionally not fixed in this photo-completion build.
- The new 70-preset UI, settings writes, gallery, and processed derivative have not yet been physically verified. Build or installation success is not hardware proof.

Exact staged evidence and preserved recovery hashes are in `DEVICE_FINDINGS.md`.

## Architecture

```text
CameraEx -> Sony normal preview SurfaceView -> autofocus + Sony original capture
CameraSequence worker -> one reusable direct JPEG buffer -> one bounded native JPEG slot
native process worker -> reduced 80x60 decode -> preset graph + temporal history -> custom UI
native photo worker -> one 80x60 snapshot -> 320x240 JPEG + JSON + thumbnail + atomic index
```

Java owns Android/Sony lifecycle, camera calls, one analytical polling thread, the direct buffer, physical-key routing, touch forwarding, and the display cadence runnable. Pixel processing, preset state, UI composition, derivative encoding, settings, gallery indexing, and photo file transactions are native.

Every Sony `DeviceMemory` is released in `finally`. Busy frames are dropped rather than queued. Pause stops CameraSequence polling, joins the analytical consumer, stops the native workers, and only then releases `CameraEx`; a Sony worker that does not stop causes the camera object to be quarantined rather than released beneath a live call.

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
./scripts/install.sh releases/RetroLens-1.0.0-photo-runtime.apk
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

The native UI uses a dark charcoal base, warm text, restrained mint accent, a five-card style browser, quick controls, compare labels, focus and capture state, hidden diagnostics, and an app-only thumbnail gallery. The essential UI is kept inside the known visible top-left 120x90 region while the unresolved physical panel geometry is left unchanged.

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

Full-resolution post-processing remains disabled until safe discovery of the newly captured Sony JPEG and a memory-bounded tile path are physically proven.

## Gallery and storage

The gallery indexes at most 48 RetroLens-produced photos and loads one fixed 80x60 thumbnail at a time. It does not scan the full card. Delete requires confirmation and removes only the selected derivative, its sidecar, and its app thumbnail.

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

- Known: the processed panel updates only in the top-left portion on the physical camera. This build does not attempt another geometry change.
- A frozen moving area with a working Sony preview indicates a decode/cadence issue. Fn opens rate-limited diagnostics.
- If `Writing memory` persists after exit, stop testing this candidate and reinstall `releases/RetroLens-1.0.0-safe-preview.apk`; avoid battery removal while the card LED is active unless the camera is irrecoverably wedged.
- If derivative saving fails, confirm free card space and inspect `RETROLENS/LOG.TXT`. The Sony original should still exist independently.
- Use `./scripts/toolchain-info.sh` for missing tools and `./scripts/usb-status.sh` for installation connectivity.

Unresolved Sony questions are full-resolution captured-JPEG discovery, safe full-screen processed output, exact format-256 semantics beyond observed JPEG markers, and private movie control. Video is outside this build and no filtered-video, audio, HD, encoder, or ISP claim is made.

See `LICENSES.md` and `ATTRIBUTION.md`. RetroLens does not vendor OpenCV, FFmpeg, RetroArch, or shader collections and has no network feature.
