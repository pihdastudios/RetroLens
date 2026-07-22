# RetroLens device findings

## Proven on the ILCE-5100 before the native RetroLens build

- The inherited Sony lifecycle baseline opens `CameraEx`, starts normal preview, preserves manual autofocus and still capture, and releases cleanly.
- `CameraSequence` accepted 640×480, rate 30000, format 256, one queued frame, and a 262144-byte JPEG buffer on this same camera.
- Analytical payloads were valid JPEGs of roughly 55–83 KiB and arrived at about 9–10 FPS.
- Every observed `DeviceMemory` was released, and the proven stop order was worker, sequence, native consumer, then `CameraEx`.
- Repeated `takePicture(null,null,null)` calls worked with a guarded `cancelTakePicture()` around 418–428 ms later while analytical preview remained active.

Those results came from the source WaveSnap/PMCADemo baseline. They justify the API and lifecycle choices but do not prove RetroLens rendering or output files.

## RetroLens evidence on 2026-07-22

- Renamed recovery APK installed successfully as package `io.pihda.retrolens`.
- Final native-engine APK SHA-256 `435f93762f90740ece95377bf34b8250c2a6aa396da8803b7afcd89e3a07f9e1` installed successfully and contains only `lib/armeabi/libretrolens.so`.
- Host ASan/UBSan tests pass for all 70 preset graphs, tiny and odd dimensions, deterministic state, JPEG writing, JSON escaping, performance decisions, and AVI finalization.
- No `RETROLENS/LOG.TXT` existed when the camera was reconnected, so physical launch, filtered output, controls, capture derivative, and Retro Clip recording remain unverified after the latest install.
- A fully black display with working physical Sony shutter was observed from the earlier native build. New 5–6.6 MB Sony originals appeared on the card, confirming camera/shutter operation independently of the failed effect display.
- No `RETROLENS/` directory was produced by that run, even though Java session logging precedes native and camera startup. The exact installed/selected package or early failure point therefore remains unresolved.
- Stability build `stability-20260722-a`, final APK SHA-256 `f535468af3867b5139c0a6700b932fc3f1d0dc77c3d684ed0d6595cebb84d95c`, builds and verifies. An earlier stability iteration installed successfully; final-install and physical results are pending. It adds a synchronous surface probe and visible fallback, uses the real 80×60 reduced grid, renders on events, batches logs, shortens teardown bounds, and disables Retro Clip/processed derivatives.
- Physical testing of `stability-20260722-a` still showed a pure black screen. The shutter worked inside RetroLens and the Movie button had no visible effect. After exiting, the next shutter press in Sony's normal camera application remained at `Writing memory` with the red media LED continuously active; battery removal was required. This is a severe lifecycle regression, so that build is retired from hardware testing.
- The card had ample free space when inspected afterward. No `RETROLENS/` directory or log existed, so external app output did not fill the card and cannot explain the wedge.
- Static review found two concrete risks in the retired build: it forced high display color depth despite the proven camera apps using low depth, and it released `CameraEx` even after reporting that the CameraSequence worker had failed to stop. Its opaque second SurfaceView could also cover both normal preview and Java fallback after a successful initial surface probe followed by a later render failure.
- Safety build `safe-preview-20260722-b`, APK SHA-256 `c71655c7f71dff4c107954fc09c1478494f3cc61fd2528b79c74b0015cc14b63`, passed both physical test phases. The user confirmed normal preview/capture and clean post-exit normal-camera operation. Six new originals (`DSC05026.JPG` through `DSC05031.JPG`) were confirmed on the card at regular intervals, and no `RETROLENS/` directory was created. No `Writing memory` wedge, persistent media LED, or battery removal recurred.
- The proven safety APK is preserved as `releases/RetroLens-1.0.0-safe-preview.apk` with its checksum.
- Native display probe `native-probe-20260722-c`, APK SHA-256 `cb7b6cd9658395e6e1d5c7ea98d1225a7a1cdb16e92762bce4ea3c49be99d87f`, showed the expected pattern and `NATIVE DISPLAY OK`. The user reports that all repeated lifecycle/capture phases passed. `DSC05032.JPG` is the only new original independently visible from the latest mounted-card inspection, so the repeated write sequence is user-confirmed rather than fully reconstructed from card timestamps.
- Thread probe `thread-probe-20260722-d`, APK SHA-256 `9bdaddbcea606cf74c0b14b7f4b5aa966a733f2f0f48f9ea9366a578649fc4bb`, adds one fixed 256×144 RGB565 offscreen worker at 8 FPS. The worker owns no surface, camera API, JNI reference, file, or queue; one reusable Java runnable posts its latest raster synchronously. Twenty repeated worker lifecycle tests pass under ASan/UBSan and the full suite passes ThreadSanitizer. Build verification passes; physical results are pending.
- The latest PMCADemo log showed many consecutive Boot/Exit receiver messages while navigating the Sony application menu. PMCADemo and LegacyAlphaRemote receiver behavior is being audited separately and has not been changed.

## Open hardware checks

1. Confirm the probe panel sweep and frame counter animate near 8 FPS while normal Sony preview remains visible and responsive.
2. Open and exit the thread probe five times without capture. After every exit, take one normal-camera photo and confirm writing completes normally.
3. Take one photo inside RetroLens, wait for its media LED to stop, exit, then take one normal-camera photo and confirm writing completes normally.
4. Verify autofocus, still capture, disabled Movie message, Delete/back exit, and application-menu responsiveness.
5. Only after the native thread passes may CameraSequence return, with strict worker quarantine and the same repeated exit test before filters.

No item in this section may be promoted to “working on a5100” without `RETROLENS/LOG.TXT` evidence.
