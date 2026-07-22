# RetroLens

RetroLens is a retro-effects camera project for the Sony a5100 OpenMemories environment. Its effects engine remains implemented in native C++, while the current staged probe limits work to Sony normal preview, still capture, and one small filtered analytical-preview panel.

The current `style-panel-probe-20260722-g` build enables CameraSequence only after the proven normal preview and native panel are ready. It reuses one Java direct buffer, synchronously copies accepted JPEGs into one fixed native slot, decodes a reduced 80×60 grid, applies one of ten selected presets, and expands it exactly 3× into a 240×180 panel. Full-screen output, Retro Clip, processed derivatives, settings writes, and all external-card output remain disabled.

The project never modifies firmware, calibration data, boot partitions, or Sony originals.

## Current verification status

- Host native tests and the API-10 APK build pass.
- The package has installed successfully through Sony-PMCA-RE on an ILCE-5100.
- CameraSequence itself was previously measured on this same camera in the source baseline at roughly 9–10 analytical JPEG frames per second.
- The prior stability build produced an immediate black screen and left the next normal-camera capture stuck at `Writing memory`; battery removal was required. It must not be used for further testing.
- Safety APK SHA-256 `c71655c7f71dff4c107954fc09c1478494f3cc61fd2528b79c74b0015cc14b63` passed both physical lifecycle phases: five capture-free open/exit cycles followed by normal-camera captures, then capture inside RetroLens followed by a normal-camera capture. Six corresponding Sony originals were confirmed.
- The proven recovery APK is preserved as `releases/RetroLens-1.0.0-safe-preview.apk`.
- Static native display probe APK SHA-256 `cb7b6cd9658395e6e1d5c7ea98d1225a7a1cdb16e92762bce4ea3c49be99d87f` displayed its pattern successfully and passed both lifecycle phases by user report. `DSC05032.JPG` is the only new file independently visible from that session, so the findings retain that evidence boundary.
- Thread-probe APK SHA-256 `9bdaddbcea606cf74c0b14b7f4b5aa966a733f2f0f48f9ea9366a578649fc4bb` passes ASan/UBSan, ThreadSanitizer, API-10 build, and APK verification. The user confirmed its 8 FPS panel advanced from frame 10 to frame 26 with the rest of the camera behavior working; `DSC05033.JPG` independently confirms a new Sony original from that session.
- Sequence-probe APK SHA-256 `000bc1d559b99c32768b598df19a469d4217ff76b5fdbdc03d6764e7ac8d3802` passes host tests, the legacy build, installation, and the complete physical gate. The user confirmed 30-second metrics, repeated exits, capture inside RetroLens, and normal-camera captures afterward without a persistent media LED or `Writing memory` wedge.
- Filter-panel probe APK SHA-256 `ed67240f922358cda6ad848d1453e49d6bdedd9b58f53a4c9209cedb34541e04` passes host sanitizer tests, the legacy API-10/armeabi build, APK verification, and installation on the ILCE-5100. The user observed a moving Olive Pocket image; its 4:3 image occupied only 75% of the old 16:9 panel, which identified a presentation-geometry defect rather than a stalled decoder.
- Style-panel probe APK SHA-256 `75945f5c7d256fd68ab2a2eaa28b7d452c8e539d61313592039e48045f952b7b` passes ASan/UBSan, ThreadSanitizer, the legacy API-10/armeabi build, APK verification, and physical installation on the ILCE-5100. Physical launch and lifecycle results are pending.
- No screenshots have yet been captured from the physical camera.

See `DEVICE_FINDINGS.md` for the exact evidence boundary.

## Architecture

```text
CameraEx -> one Sony-owned normal preview SurfaceView -> autofocus/still capture
         -> CameraSequence worker -> one direct JPEG buffer -> bounded native copy
native worker -> reduced JPEG decode -> selected 80x60 filter -> exact 3x -> 240x180 panel
Java cadence -> synchronous native display post every 125 ms
```

Java owns the camera, the single analytical polling worker, one reusable direct buffer, one reusable cadence runnable, and surface posting. JNI retains no Java or Sony buffer reference. The native worker owns one fixed compressed slot plus fixed raw, filtered, temporal, and RGB565 buffers; busy frames are dropped. Pause stops analytical polling, joins the decode/display worker, and only then releases `CameraEx`. Failed analytical shutdown quarantines `CameraEx` instead of releasing it underneath a live Sony call.

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
| Left/right | Previous/next probe style |
| Either dial | Previous/next probe style by direction |
| Half shutter | Sony autofocus |
| Full shutter | Sony original capture |
| Movie | Show `VIDEO DISABLED - STYLE PANEL PROBE` |
| Delete | Back/exit |

The style probe exposes Olive Pocket, CGA Shock, One-Bit Desktop, Consumer CRT, VHS Rental, Soviet Archive 1978, Newsprint, Comic Ink, Piss Filter 2007, and Thermal False Color. Effects touch gestures and touch-to-focus remain disabled.

## Interface

The interface keeps the Sony preview full-screen and places a 240×180 native panel at the right center. Before the first frame it shows sequence diagnostics. After decode, the complete 4:3 processed frame fills the panel with no letterbox; shadowed style and metric glyphs are drawn directly over the moving image. `OUT` must remain 0 between frames or briefly reach 1.

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

Retro Clip is runtime-disabled in `style-panel-probe-20260722-g`. Its implementation remains compiled and host-tested, but the full native runtime is not created and the movie key cannot start an output file.

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

The style-panel probe polls at a nominal 90 ms interval with at most one Sony frame requested. It validates markers, copies only when its single native slot is free, and immediately releases the Sony buffer. PicoJPEG reduced mode produces 80×60 unique samples; the selected style runs at that resolution before exact 3× nearest-neighbor expansion. A style change reprocesses the last raw frame, resets temporal history, and does not add another JPEG buffer. There is no per-frame allocation or app-owned card I/O.

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

- A frozen panel with working Sony preview is a worker/cadence failure. A black or absent panel is a native post failure. Report the visible `E` code or last visible frame number.
- A fully black screen is unexpected because the probe occupies only 240×180. Stop testing and reinstall the preserved safety APK.
- `Writing memory` persisting after exit: do not launch RetroLens again. Allow a normal power-off if possible and avoid removing the battery while the card LED is active unless the camera is irrecoverably wedged.
- Build cannot find tools: run `./scripts/toolchain-info.sh`; this project expects the workspace `toolchain/` directory and JDK 8.
- Install cannot find camera: set USB Connection to Mass Storage or leave it in app-install mode and run `./scripts/usb-status.sh`.
- The style-panel probe deliberately produces no `RETROLENS/LOG.TXT`; validation uses visible counters and observed capture/exit behavior. `RX - REL` outside 0–1 is rendered as `BUFFER IMBALANCE`.

## Dependencies, licenses, and unresolved questions

See `LICENSES.md` and `ATTRIBUTION.md`. The app does not vendor OpenCV, FFmpeg, RetroArch, or shader collections and does not request Internet access.

Unresolved device questions include reduced PicoJPEG/filter throughput in this isolated panel, safe full-screen processed output, exact format-256 semantics beyond observed JPEG markers, full-resolution capture discovery, Sony movie output selection, and MJPEG throughput. These remain unclaimed until physical testing answers them.
