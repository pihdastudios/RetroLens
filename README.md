# RetroLens

RetroLens is a retro-effects camera project for the Sony a5100 OpenMemories environment. Its effects engine remains implemented in native C++, while the current hardware-recovery build deliberately runs only Sony's normal preview and still-capture path.

The current `native-probe-20260722-c` build keeps CameraSequence, the full native runtime, Retro Clip, processed derivatives, and external-card logging disabled. It adds one small synchronous native display probe above the proven Sony preview without creating a native worker or retaining a native window or frame buffer.

The project never modifies firmware, calibration data, boot partitions, or Sony originals.

## Current verification status

- Host native tests and the API-10 APK build pass.
- The package has installed successfully through Sony-PMCA-RE on an ILCE-5100.
- CameraSequence itself was previously measured on this same camera in the source baseline at roughly 9–10 analytical JPEG frames per second.
- The prior stability build produced an immediate black screen and left the next normal-camera capture stuck at `Writing memory`; battery removal was required. It must not be used for further testing.
- Safety APK SHA-256 `c71655c7f71dff4c107954fc09c1478494f3cc61fd2528b79c74b0015cc14b63` passed both physical lifecycle phases: five capture-free open/exit cycles followed by normal-camera captures, then capture inside RetroLens followed by a normal-camera capture. Six corresponding Sony originals were confirmed.
- The proven recovery APK is preserved as `releases/RetroLens-1.0.0-safe-preview.apk`.
- Native display probe APK SHA-256 `cb7b6cd9658395e6e1d5c7ea98d1225a7a1cdb16e92762bce4ea3c49be99d87f` builds and host-tests successfully but requires the same two-phase physical test before it can be promoted.
- No screenshots have yet been captured from the physical camera.

See `DEVICE_FINDINGS.md` for the exact evidence boundary.

## Architecture

```text
CameraEx -> one Sony-owned normal preview SurfaceView
         -> autofocus and still capture
JNI      -> one synchronous 256x144 native display post
Java View -> probe status and physical-key feedback
```

Java owns the camera and probe lifecycle. The probe locks, paints, posts, and releases its `ANativeWindow` within one UI-thread call using a stack raster. The dormant C++ engine still owns filters, bounded temporal state, persistence, MJPEG/AVI support, and native UI for later hardware-gated builds.

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
| Navigation controls | Reserved while effects are disabled |
| Half shutter | Sony autofocus |
| Full shutter | Sony original capture |
| Movie | Show `VIDEO DISABLED - SAFETY BUILD` |
| Delete | Back/exit |

Effects touch gestures and touch-to-focus are disabled in the safety baseline.

## Interface

The interface keeps the Sony preview full-screen, adds a transparent Java status overlay, and places a 256×144 native diagnostic panel at the right center. The panel shows color bars, `NATIVE DISPLAY OK`, actual surface geometry/format, build ID, and confirmation that CameraSequence is inactive.

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

Processed derivative output is disabled in the safety build. The captured-JPEG listener is not registered, avoiding an additional multi-megabyte Java byte array after shutter. When the feature is re-enabled after validation, it will write a separate preview-resolution image and sidecar without modifying the Sony original:

```text
RETROLENS/PHOTOS/<date>_<preset>_preview.jpg
RETROLENS/PHOTOS/<date>_<preset>_preview.json
```

This is explicitly a preview-resolution derivative. Full-resolution post-processing is disabled until safe access to the newly captured original and memory-bounded encoding are physically proven. RetroLens never guesses the Sony file path or overwrites the original.

## Video modes

### Retro Clip

Retro Clip is runtime-disabled in `native-probe-20260722-c`. Its bounded MJPEG/AVI implementation remains compiled and host-tested, but the full native runtime is not created and the movie key cannot start an output file.

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

The probe build performs no analytical JPEG decoding, filter processing, recording, or app-owned card I/O. Its only native work is one bounded synchronous diagnostic raster/post per surface creation or change.

The performance controller monitors decode, filter, render, and dropped-frame measurements and controls accepted cadence. The renderer sleeps between new frames and UI animation deadlines. SIMD, handwritten assembly, OpenGL ES, and larger JPEG libraries remain deferred until device measurements prove the optimized integer path insufficient and confirm CPU capabilities.

## Storage and configuration

External logging and all `RETROLENS/` writes are disabled in the probe build so card activity cannot contaminate lifecycle testing. The APK does not request external-storage write permission. The dormant engine retains this layout for later builds:

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

- A black or absent probe panel with working Sony preview is a native display-probe failure, not a camera failure. Report the visible `E` code; the panel hides itself after a failed post.
- A fully black screen is unexpected because the probe occupies only 256×144. Stop testing and reinstall the preserved safety APK.
- `Writing memory` persisting after exit: do not launch RetroLens again. Allow a normal power-off if possible and avoid removing the battery while the card LED is active unless the camera is irrecoverably wedged.
- Build cannot find tools: run `./scripts/toolchain-info.sh`; this project expects the workspace `toolchain/` directory and JDK 8.
- Install cannot find camera: set USB Connection to Mass Storage or leave it in app-install mode and run `./scripts/usb-status.sh`.
- The probe build deliberately produces no `RETROLENS/LOG.TXT`; validation uses its visible geometry/status text and observed capture/exit behavior.

## Dependencies, licenses, and unresolved questions

See `LICENSES.md` and `ATTRIBUTION.md`. The app does not vendor OpenCV, FFmpeg, RetroArch, or shader collections and does not request Internet access.

Unresolved device questions include current native-surface compatibility, exact format-256 semantics, full-resolution capture discovery, safe alternative preview modes, Sony movie output selection, thermal/storage observability, and MJPEG throughput. These remain unclaimed until physical logs answer them.
