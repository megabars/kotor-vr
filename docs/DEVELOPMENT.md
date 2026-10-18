# Implementation and validation notes

[Русский](DEVELOPMENT.ru.md) · [Home](../README.md)

## Render path

The original x86 process owns gameplay and the desktop OpenGL context. `kotor-vr-launcher.exe` verifies the exact EXE SHA-256, starts it suspended, loads its adjacent DLL using a remote `LoadLibraryW` call, verifies the injected module's full path, and resumes the main thread. The launcher resolves the actual owning module/RVA of the loader function rather than assuming a local address is valid remotely. An early module-enumeration fallback handles incomplete process startup. Failed launch/injection cleans up the suspended child through launcher ownership.

The game DLL uses MinHook at `Camera::RenderScene`, `Scene::Render` and `SwapBuffers`. It chooses a main-scene camera using scene, field-of-view and viewport checks. The normal world/desktop pass still runs. At presentation time the mod acquires OpenXR eye images and invokes the camera renderer separately for the two eyes. At the scene boundary it substitutes each eye's asymmetric projection and tracked camera offset. Gameplay is not advanced an extra simulation tick for those eye passes.

The mod updates frustum planes for the tracked view and restores the relevant camera planes, clip globals, scene render counter and renderer cycle after extra passes. OpenGL matrix, attribute and client state are saved/restored around VR rendering. These measures address observed renderer state problems; they do not establish that every engine subsystem is side-effect-free.

`VrBackend` owns OpenXR instance/system/session, reference space, session-state events, frame timing, eye location, swapchains and OpenGL framebuffer/depth/stencil resources. Projection uses each eye's actual OpenXR field of view. Head translation and rotation are relative to the recentered third-person camera; F10 levels the horizontal heading and captures a new origin. The runtime supplies eye separation. There is no motion-controller action binding in this version.

## HUD and cursor

`HudOverlay` captures the ordinary world before GUI and compares it with the completed normal frame. The resulting difference mask becomes an alpha interface layer in both eye views. F9 uses the complete original frame as a panel instead. Screen-space effects can be mistaken for interface content; the fallback panel is intentional.

`VrCursor` reads `GetCursorInfo`, `GetIconInfo` and `DrawIconEx`. Rendering the cursor against black/white backgrounds reconstructs its texture transparency. The actual Windows pointer/hotspot and hover icon are retained; draw size is 1.5×. A depth sample and the original scene projection unproject the desktop mouse location for stereo world placement. Detected interface pixels select panel placement. The original game still receives all mouse events, so no replacement picking subsystem is introduced. Tests use the same cursor math as the implementation.

## File map

| Source | Responsibility |
| --- | --- |
| `src/launcher.cpp` | Paths, quoting, hash validation, process creation, DLL loading, runtime selection |
| `src/game_mod.cpp` | Engine hooks, camera selection, extra render passes, state restoration, hotkeys and diagnostics |
| `src/vr_backend.cpp`, `.h` | OpenXR/OpenGL integration and shared projection/pose math |
| `src/hud_overlay.cpp`, `.h` | World/final capture and interface extraction |
| `src/vr_cursor.cpp`, `.h`, `cursor_math.h` | Cursor icon capture, plane/depth placement and unprojection |
| `src/vr_probe.cpp` | Independent OpenXR cube test |
| `tests/vr_math_test.cpp` | Projection, eye/head transform and cursor math checks |
| `src/version.h.in`, `version.rc.in` | Generated C++ and Windows version information |

## Reverse-engineering scope

Analysis used Ghidra 12.1.4 through REA, MSVC `dumpbin`, the [KotOR Patch Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) address reference and runtime experiments on the exact supported EXE. These are internal engine labels/references, not an official engine API.

| Item | Address / value |
| --- | --- |
| Expected image base | `0x00400000` |
| `Camera::RenderScene` | `0x0045c540` |
| `Scene::Render` | `0x004512d0` |
| View setup reference | `0x00425ca0` |
| EXE SHA-256 | `9c10e0450a6eeca417e036e3cde7474fed1f0a92aab018446d156944dea91435` |

Local REA evidence identifiers retained for provenance:

```text
Camera render: ev_fa831e344f86956e6e4686cb9f0bd7ff893ff78b8e79e48d69194026ee7a035d
View setup:    ev_c4770d2328a33fcafface8f1d9aa439e0569e21707ff329bf3d8e8207f4ec310
```

These identifiers refer to the development session; they are not hosted public artifacts or prerequisites for building. Some short/virtual procedures were not recognized as functions by the initial Ghidra analysis. The scene-render region was additionally checked with disassembly and runtime behavior. Game binaries, decompilation databases, Ghidra installations, raw session logs and personal configuration were excluded from the public repository.

Porting to another EXE requires independent mapping of all functions, globals, object layouts and calling conventions in `game_mod.cpp`, and validation of the renderer's state restoration. Updating only the allowed hash is unsafe and insufficient. The DLL also refuses an unexpected image base.

## What was verified

- The original prototype submitted stereo cube frames through VirtualDesktopXR on RTX 3070/OpenGL 4.6.
- In-game runtime logs and geometry captures established that separate eye passes ran.
- The player, wearing Quest 3S, explicitly confirmed stereo depth and head movement/rotation, then HUD visibility, then the repaired cursor and accurate scene/UI clicks.
- Release packaging rebuilds the same renderer sources with version resources and a launcher `--version` command. Automated projection/cursor tests run in Release and use explicit failure handling, not assertions compiled away by `NDEBUG`.
- Packaging validation checks installer/restoration behavior separately from the real game installation. Release-specific test results are recorded in [validation notes](VALIDATION.md).

The user's earlier headset confirmations apply to the working prototype before release metadata/packaging changes. They are not a claim of a new headset test after every rebuild. No complete playthrough or broad hardware/runtime compatibility matrix exists.

## Contributing

Include a reproducible problem, exact game hash/runtime, and a small focused change. Run `build.ps1`/CTest. For renderer changes, separately test the probe, stereo scene geometry, original desktop, F9 panel, HUD, cursor hover/clicks, F10 recenter and normal gameplay progression. Keep original game assets, local backups, secrets, logs and generated files out of commits. Do not replace the hash gate with a permissive check.
