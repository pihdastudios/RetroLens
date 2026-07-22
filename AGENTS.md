# RetroLens Sony a5100 Engineering Guide

RetroLens is an API-10 native effects camera. Read the workspace root guide and `.codex/skills/sony-camera-app-workflow/SKILL.md` first.

- Preserve package/application ID `io.pihda.retrolens`, activity `RetroLensActivity`, and library `libretrolens.so`.
- Preserve the pinned legacy toolchain, local framework/stubs jars, Java 6 syntax, `armeabi`, and separate `ndk-build` packaging.
- Preserve the proven CameraSequence options and unconditional `DeviceMemory.release()`.
- Java owns Sony APIs on the UI thread. Native code owns pixels, UI, files, and recording.
- Keep one pending compressed frame and one recorder frame. Drop stale work; never add an unbounded queue or per-frame allocation.
- Stop frame polling before CameraSequence, native runtime, and CameraEx release.
- Never overwrite Sony originals or describe preview effects as baked into Sony movies.
- Run `./scripts/smoke-test.sh` before installation. After device interaction, collect and analyze `RETROLENS/LOG.TXT` and update `DEVICE_FINDINGS.md` with evidence only.
