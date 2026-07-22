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
- The latest PMCADemo log showed many consecutive Boot/Exit receiver messages while navigating the Sony application menu. PMCADemo and LegacyAlphaRemote receiver behavior is being audited separately and has not been changed.

## Open hardware checks

1. Launch and confirm the native startup surface transitions into processed live view.
2. Record actual 80×60 decode/filter/compose FPS, surface geometry/format, and dropped frames.
3. Exercise touch, dial, directional, center, menu, C1, playback, autofocus, shutter, movie, and delete behavior.
4. Confirm the Sony original plus preview derivative/sidecar.
5. Record and host-validate a Retro Clip; interrupt a second clip during activity pause.
6. Repeat entry/exit five times and check frame acquisition/release balance.
7. Test low storage, malformed settings, lens removal, low battery, and unsupported CameraSequence behavior.
8. Probe Sony private movie control only in a separate diagnostic build.

No item in this section may be promoted to “working on a5100” without `RETROLENS/LOG.TXT` evidence.
