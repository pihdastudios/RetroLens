# RetroLens

RetroLens is a live retro-effects camera application for the Sony a5100 OpenMemories environment. It acquires Sony analytical-preview JPEGs with `CameraSequence`, decodes and processes them in native C++, composites a camera-oriented interface, preserves normal Sony still capture, writes a preview-resolution derivative, and can record the processed preview as a silent MJPEG AVI Retro Clip.

The project never modifies firmware, calibration data, boot partitions, or Sony originals.

## Current verification status

- Host native tests and the API-10 APK build pass.
- The package has installed successfully through Sony-PMCA-RE on an ILCE-5100.
- CameraSequence itself was previously measured on this same camera in the source baseline at roughly 9–10 analytical JPEG frames per second.
- The current RetroLens native surface, filters, derivative saving, and Retro Clip recording still require post-install physical operation and log review before they can be called hardware-tested.
- No screenshots have yet been captured from the physical camera.

See `DEVICE_FINDINGS.md` for the exact evidence boundary.

## Architecture

```text
CameraEx + normal Sony preview
CameraSequence worker -> one direct ByteBuffer -> bounded JNI copy
                                             |
                                             v
                         native process/render thread
                         PicoJPEG -> 320x240 RGB -> preset graph
                                      |              |
                                      |              +-> native RGB565 surface + UI
                                      +-> one-slot recorder thread
                                           +-> preview JPEG derivative
                                           +-> silent MJPEG AVI
```

Java owns the Activity, Sony API calls, surfaces, physical keys, touch forwarding, one CameraSequence worker, and deterministic teardown. C++ owns JPEG decoding/encoding, filtering, temporal history, UI scenes, settings, snapshots, frame pacing, logging, and recording.

The Java frame callback never allocates a bitmap or processes pixels. It reuses one 256 KiB direct buffer. JNI copies into one native slot and drops a frame when that slot is busy. Native memory is allocated once at startup.

## Toolchain

- JDK 8, Java source/target 1.6
- Gradle 2.14.1, Android plugin 2.2.3
- compile/min/target SDK 10, Build Tools 25.0.2
- Android NDK r14b, `ndk-build`, `armeabi`, GNU STL static
- Effective NDK platform resolves to Android 9 because r14b has no literal `platforms/android-10`; the application SDK contract remains API 10.

The OpenMemories framework and compile-only Sony stubs are pinned under `app/libs`; hashes are in `ATTRIBUTION.md`. Sony stub classes are not packaged into the APK.

## Build, test, install

From `RetroLens/`:

```bash
./scripts/format.sh --check
./scripts/smoke-test.sh --host-only
./scripts/build-native.sh
./scripts/build-apk.sh --clean
./scripts/verify-apk.sh
./scripts/install.sh
```

The normal output is:

```text
app/build/outputs/apk/RetroLens-debug-1.0.0.apk
```

Installation requires the camera in a Sony-PMCA-RE compatible USB mode. `./scripts/package-release.sh` creates a checked APK plus SHA-256 file under `releases/`.

## Controls

| Control | Action |
| --- | --- |
| Left/right or upper dial | Previous/next style |
| Up/down or lower dial | Category navigation or intensity in quick controls |
| Center | Open/close quick controls |
| Menu | Style browser |
| Playback | RetroLens gallery scene |
| Half shutter | Sony autofocus |
| Full shutter | Sony capture, then processed preview derivative |
| Movie | Start/stop Retro Clip |
| C1 | Hold original/effect compare |
| Delete | Back/exit |

Touch gestures: swipe horizontally for styles, tap the preview to hide/show controls, tap the center card for quick controls, drag across the quick-control panel to change intensity, double-tap to favorite, and long-press for comparison. Touch-to-focus is not enabled because no safe Sony API path has been proven.

## Interface

The native renderer provides the startup wake, processed preview, status bar, three-card carousel, favorites, quick-control drawer, style browser, compare label, capture flash, recording state, gallery scene, and a polished compatibility card. Routine animation is short and is redrawn independently of analytical frame arrival.

The UI exposes descriptive controls. Internally each preset has a stable ID, category, description, tier, control mask, tuned grade, and a graph assembled from bounded shared passes.

## Presets

All 70 requested presets ship:

- Handheld: Olive Pocket, Silver Pocket, Virtual Red, Dot Matrix Toy, Calculator Vision, Pocket Color.
- Computer: One-Bit Desktop, CGA Shock, EGA Sixteen, VGA Sixteen, Amber Terminal, Green Terminal, Teletext, ANSI Camera, ASCII Mono, ASCII Color, Braille Mosaic.
- Console: Eight-Bit Summer, Sixteen-Bit Arcade, Arcade Cabinet, Composite Console, RF Channel Three.
- Analog TV: Consumer CRT, Aperture Grille, Shadow Mask, Late-Night Broadcast, NTSC Composite, PAL Ghost, SECAM State TV.
- Tape: VHS Rental, Damaged VHS, Video8 Family, Hi8 Vacation, MiniDV 2002, Early Webcam, CCTV 1996, Night Security.
- Film and Archive: Super Eight, Sixteen-Millimeter News, Soviet Archive 1978, State Archive Color, Cosmonaut Tape, Expired Slide, Bleach Bypass, Cross Process, Orthochromatic, Cyanotype.
- Print: Newsprint, CMYK Misprint, Risograph, Manga Screen, Photocopier, Thermal Receipt, Pencil Draft, Comic Ink.
- Digital Decay: JPEG Hell, Databent Preview, Frame Echo, Signal Tear, Pixel Sort.
- Game Era: Piss Filter 2007 / Seventh-Gen Amber, Brown Apocalypse, Teal-Orange Blockbuster, Green Code, Blue Sci-Fi 2004, Bloom Shooter.
- Experimental: Thermal False Color, Infrared False Color, Edge Scanner, Blueprint.

Thermal and infrared modes are simulated color mappings, not sensor measurements. Tier 3 means reduced-detail preview or capture-quality intent; it never permits an unbounded or blocking graph.

## Still photographs

A full shutter press calls the verified Sony `takePicture(null,null,null)` path and releases it after the established guarded delay. The Sony original remains owned and saved by the camera.

After a `CameraEx.JpegListener` callback, RetroLens writes the most recent filtered 320×240 analytical frame and a JSON sidecar:

```text
RETROLENS/PHOTOS/<date>_<preset>_preview.jpg
RETROLENS/PHOTOS/<date>_<preset>_preview.json
```

This is explicitly a preview-resolution derivative. Full-resolution post-processing is disabled until safe access to the newly captured original and memory-bounded encoding are physically proven. RetroLens never guesses the Sony file path or overwrites the original.

## Video modes

### Retro Clip

Retro Clip records the processed analytical preview at 320×240, nominal 10 FPS, MJPEG in AVI 1.0, with no audio. It uses one recorder slot, one fixed encoded buffer, a fixed 9000-frame index, and a file-size/duration bound. Frames are dropped rather than queued when encoding is busy.

Files are written to `.tmp`, finalized with `idx1`, patched, flushed, and atomically renamed. Activity interruption produces a finalized filename ending in `.incomplete` and an `interrupted: true` sidecar rather than pretending the clip is complete.

### Sony Movie

Sony Movie control is not enabled. The private `com.sony.scalar.media.MediaRecorder` output-media semantics have not been verified on this a5100. If later enabled, the UI must continue to state:

```text
SONY MOVIE
Effect preview only
Saved video remains clean
```

RetroLens makes no full-HD baked-filter, H.264, audio, or encoder/ISP interception claim.

## Performance and memory

The source CameraSequence ceiling is approximately 10 FPS. Performance mode processes a 320×240 working image reconstructed from reduced JPEG block samples. The renderer scales to the 640×480 display. Balanced 480×360 and full 640×480 processing remain benchmark-gated because the proven full PicoJPEG decode was substantially slower.

The performance controller monitors decode, filter, render, and dropped-frame measurements. It reduces animation/detail and target cadence before allowing interface stalls, without changing the selected preset.

## Storage and configuration

```text
RETROLENS/
├── CONFIG/settings.cfg
├── PRESETS/
├── PHOTOS/
├── CLIPS/
├── THUMBNAILS/
└── LOG.TXT
```

Settings are versioned and atomically replaced. Invalid values fall back to safe defaults. RetroLens only indexes files produced inside its own directory.

## Troubleshooting

- `Preview unavailable`: normal Sony capture should remain available; reconnect and inspect `RETROLENS/LOG.TXT`.
- No frames: verify the CameraSequence probe remains accepted at 640×480, format 256, one queued frame, and a 262144-byte JPEG maximum.
- Build cannot find tools: run `./scripts/toolchain-info.sh`; this project expects the workspace `toolchain/` directory and JDK 8.
- Install cannot find camera: set USB Connection to Mass Storage or leave it in app-install mode and run `./scripts/usb-status.sh`.
- Analyze a collected log with `./scripts/analyze-log.sh <log>`.

## Dependencies, licenses, and unresolved questions

See `LICENSES.md` and `ATTRIBUTION.md`. The app does not vendor OpenCV, FFmpeg, RetroArch, or shader collections and does not request Internet access.

Unresolved device questions include current native-surface compatibility, exact format-256 semantics, full-resolution capture discovery, safe alternative preview modes, Sony movie output selection, thermal/storage observability, and MJPEG throughput. These remain unclaimed until physical logs answer them.
