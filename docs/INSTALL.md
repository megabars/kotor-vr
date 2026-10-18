# Installation, operation and troubleshooting

[Русский](INSTALL.ru.md) · [Home](../README.md)

## Before installation

You need your own installed KotOR with the [exact supported EXE hash](../README.md#exact-game-compatibility), Windows x64, a GPU/driver capable of the game's OpenGL context and OpenXR OpenGL operation, Meta Quest 3S, and working Virtual Desktop headset/Streamer software. This combination was tested; other combinations are unverified. Install the [Visual C++ x86 runtime](https://aka.ms/vs/17/release/vc_redist.x86.exe). Run the original game once to create `swkotor.ini`, then close it.

The runtime ZIP includes the four compiled files in `bin/`, documentation, installer/restoration scripts and licenses. It contains no game data. Preserve the directory structure when extracting. If you downloaded GitHub's **Source code (zip)**, build the project first or download the Windows x86 release asset instead.

To check archive integrity, compare this result to the accompanying `SHA256SUMS.txt`:

```powershell
Get-FileHash .\kotor-vr-0.0.1-windows-x86.zip -Algorithm SHA256
```

There is also an internal manifest for each runtime binary. Checksums detect differences from the published files; they are not a code-signing certificate. The executables are unsigned.

## Automatic installation

Close the target game, then run from the extracted directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\kotor-vr\install.ps1 -GameDirectory 'D:\Games\Star Wars - KotOR'
```

`-GameDirectory` is the folder containing `swkotor.exe`, not a save folder. The execution policy override applies only to this PowerShell process. You need write permission to the game directory; a directory protected by Windows may require a writable installation location or an elevated terminal.

The installer verifies the EXE before changing files, checks that this installation is not running, and deploys:

```text
Star Wars - KotOR/
  swkotor.exe                  your original game, unchanged
  swkotor.ini                  three video settings adjusted
  KotOR VR.lnk
  kotor-vr/
    bin/
      kotor_vr.dll
      kotor-vr-launcher.exe
      kotor-vr-probe.exe
      kotor-vr-math-test.exe
    backups/swkotor.ini.original
    scripts/Ini.ps1
    install.ps1
    restore-settings.ps1
    README.md, README.ru.md, VERSION, LICENSE, docs/, licenses/...
```

The first INI backup is preserved on reinstall. In `[Graphics Options]`, the installer sets:

```ini
FullScreen=0
AllowWindowedMode=1
V-Sync=0
```

Other INI values and non-ASCII bytes are preserved. Installer options `-SkipVideoSettings` and `-SkipShortcut` omit those steps; configure the video settings yourself if skipping them. Installation does not launch the game or modify saves.

## Manual installation / running without copying

Extract the release's `kotor-vr` directory directly beside `swkotor.exe`, back up `swkotor.ini`, apply the settings above, then launch `kotor-vr\bin\kotor-vr-launcher.exe`. The launcher infers the game directory from that layout. Never copy `kotor_vr.dll` into a system directory or rename it to a proxy DLL.

To keep the mod elsewhere, supply the game path explicitly:

```powershell
& 'C:\Mods\kotor-vr\bin\kotor-vr-launcher.exe' --game-dir 'D:\Games\Star Wars - KotOR'
```

The injected DLL must stay beside the launcher. Original game arguments can follow `--`; the launcher forwards them, but their meaning is the original game's responsibility:

```text
kotor-vr-launcher.exe [--game-dir DIR] [--xr-runtime JSON] [-- GAME_ARGS...]
```

`--version` prints the mod version and exits. `--help` prints usage. Game validation errors stop the launch; do not bypass the hash check for another game build.

## Virtual Desktop and runtime selection

1. Start Virtual Desktop Streamer on the PC and connect from Virtual Desktop on Quest.
2. Wear/wake the headset and leave another active VR application before starting KotOR VR.
3. Start the shortcut. The launcher uses an explicit `--xr-runtime` first, then an inherited `XR_RUNTIME_JSON`. Without either override, it uses the following installed manifest if present:

```text
C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr-32.json
```

4. If the default manifest does not exist, the OpenXR loader falls back to its normal runtime discovery. Use **32-bit** runtime manifests for this x86 game. VDXR does not require SteamVR for the tested route.

For a nonstandard Streamer install:

```powershell
.\bin\kotor-vr-launcher.exe --game-dir 'D:\Games\Star Wars - KotOR' --xr-runtime 'D:\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr-32.json'
```

The override affects the launcher and child game only. It does not register/change the system-wide runtime. If you previously set `XR_RUNTIME_JSON` in this terminal, check it with `$env:XR_RUNTIME_JSON`; remove it from this process using `Remove-Item Env:XR_RUNTIME_JSON` when you want default discovery. Do not point to a 64-bit manifest.

## First session and controls

Load a save normally. The third-person main scene camera enables stereo rendering. Look forward and press F10. Head rotation and translation affect the view around that camera, while normal game controls continue controlling the character. Quest controllers do not replace mouse/keyboard.

F9 toggles the original full-screen image on a VR panel. Use it for menu navigation, dialogues or cutscenes that do not appear correctly through the extracted HUD. Toggle it off to return to the unobstructed stereo view. F11 pauses/resumes VR submissions. F8 writes debug BMP images next to the DLL and can cause a brief pause/disk use; it is not needed for normal play.

The mouse cursor is separately composited in stereo. In the world, it uses depth under the original desktop mouse; over detected UI it is placed on the interface plane. Keep the game focused; a cursor hidden by the original game is not forced visible. Game windows, cursor capture and head rotation can affect what the original game considers under the mouse. Recenter and check the desktop mirror if an unusual scene does not line up.

## Diagnostic tools

With the game closed and Virtual Desktop connected:

```powershell
.\bin\kotor-vr-probe.exe --seconds=15
```

This displays an independent stereo cube, using the same backend. In the probe, **R** recenters and **Escape** exits. `--seconds=N` accepts 1–3600. The probe prints runtime/session details and submitted frame counts. It tests OpenXR/headset operation, not the game's hooks. Do not run it simultaneously with another VR session.

```powershell
.\bin\kotor-vr-math-test.exe
```

This checks production math without a game or headset. A nonzero exit code indicates a failed check.

The game mod writes `bin/kotor-vr.log` next to the injected DLL. It is replaced on launch, so copy a useful log before restarting. Look for the mod version, hook installation, OpenGL/GPU details, OpenXR runtime and `STEREO frame=` progress. F8 captures `smoke-left.bmp` and `smoke-right.bmp` and additional original-frame diagnostics. These images do not prove the headset compositor showed frames.

Developer environment flags: any nonempty `KVR_XR_DISABLED` disables initial VR submission; any nonempty `KVR_SMOKE_TEST` schedules a geometry capture. Remove these process variables for a normal run. They are diagnostics, not performance settings.

## Troubleshooting

| Symptom | Checks / action |
| --- | --- |
| Unsupported `swkotor.exe` | Compare SHA-256; restore the supported legitimate installation. Different addresses require a new port, not a renamed EXE |
| Missing `VCRUNTIME140.dll` / `MSVCP140.dll` | Install Microsoft VC++ Redistributable **x86** |
| DLL missing / wrong architecture | Extract the entire runtime ZIP; keep its x86 DLL next to its launcher |
| `XR_ERROR_FORM_FACTOR_UNAVAILABLE` | Wear/wake headset, reconnect Virtual Desktop, close the other VR app. The mod retries initialization about every five seconds |
| Loader/runtime errors | Verify the 32-bit manifest path and remove stale `XR_RUNTIME_JSON` overrides; test with the probe |
| Desktop works but headset is blank | Check headset/VD connection, log and runtime, press F11 once if submissions were paused, test probe separately |
| Menu or dialogue missing | Press F9 to show the full original panel; keep game focused |
| Cursor/clicks seem wrong | Focus the game, move mouse inside it, try F10 and F9; compare with desktop. Report scene and reproducible steps |
| HUD contains unwanted effects | Known limitation of difference extraction; use F9 and report the effect/scene |
| Poor performance | Reduce game resolution or Virtual Desktop streaming/render settings, close other GPU-heavy apps. Extra render passes impose a real cost |
| Game will not start | Run original `swkotor.exe` directly to distinguish base-game issues; preserve launcher output and mod log |

The tested build has no public configuration file for world scale, IPD, HUD distance or cursor size. Do not invent INI keys; developer changes require rebuilding.

## Restore / remove

Close the game and restore only the three changed video settings:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\Games\Star Wars - KotOR\kotor-vr\restore-settings.ps1' -GameDirectory 'D:\Games\Star Wars - KotOR'
```

The script reads `kotor-vr\backups\swkotor.ini.original`. A key absent in the original is removed; other current settings are retained. Keep this backup until restoration is complete. For manual installation, restore the values from your own backup instead.

Launching `swkotor.exe` directly runs the game without loading the mod. After restoring settings, you can delete only `KotOR VR.lnk` and the installed `kotor-vr` directory. If that directory is also your source checkout, preserve the source and any local changes first. Saves and original game files are outside the mod directory.

## Reporting a problem

Include mod version, EXE SHA-256, Windows/GPU/driver, headset, runtime/Streamer version, other installed mods, steps from a save to reproduce, and whether the standalone probe works. Attach a relevant log and optional F8 pictures after reviewing them for personal paths/screenshots. State whether the problem is visible on the monitor, headset or both. Do not upload the game executable or saves publicly unless you have checked their contents and distribution rights.
