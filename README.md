# KotOR VR 0.0.1

[Русская документация](README.ru.md) · [Build from source](docs/BUILD.md) · [Install and troubleshoot](docs/INSTALL.md) · [Implementation](docs/DEVELOPMENT.md)

An experimental third-person VR mod for **Star Wars: Knights of the Old Republic, GOG 1.03, Windows x86**. The game world is rendered separately for each eye using OpenXR, with headset position and rotation applied to the camera. You control the original game with mouse and keyboard. There are no motion-controller combat or lightsaber gestures.

The prototype was tested with **Meta Quest 3S, Virtual Desktop/VDXR and an NVIDIA RTX 3070**. The player confirmed stereoscopic depth, head tracking, visible HUD, visible mouse cursor and working clicks on scene objects and interface controls. A complete playthrough, every cinematic and every graphics effect have **not** been tested. This is an early prototype, not a finished VR port.

## Download and start

1. Download `kotor-vr-0.0.1-windows-x86.zip` from [Release v0.0.1](https://github.com/megabars/kotor-vr/releases/tag/v0.0.1). GitHub's automatic source archives do not contain compiled DLL/EXE files.
2. Extract the whole ZIP. Install the [Microsoft Visual C++ x86 Redistributable](https://aka.ms/vs/17/release/vc_redist.x86.exe) if needed. The **x86** runtime is required even on 64-bit Windows.
3. Close KotOR. Open PowerShell and run the following command, replacing both paths:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File 'C:\Downloads\kotor-vr\install.ps1' -GameDirectory 'D:\Games\Star Wars - KotOR'
```

4. Wear the headset and connect to this PC in Virtual Desktop. Close another application's active VR session. VDXR must be installed with Virtual Desktop Streamer.
5. Open `KotOR VR.lnk` in the game directory. Load a save using the normal game controls. Press **F10** while looking forward to recenter.
6. Use **F9** if you need the full original screen for menus, dialogues or an interface element that the automatic HUD mask misses.

The installer copies the mod to `<game>\kotor-vr`, creates a shortcut, backs up `swkotor.ini`, and sets `FullScreen=0`, `AllowWindowedMode=1`, `V-Sync=0` in `[Graphics Options]`. It does not patch the game executable or overwrite saves. See the [detailed installation guide](docs/INSTALL.md) for manual setup and rollback.

## Exact game compatibility

Both installer and launcher require this SHA-256 of `swkotor.exe`:

```text
9c10e0450a6eeca417e036e3cde7474fed1f0a92aab018446d156944dea91435
```

Check your copy:

```powershell
Get-FileHash 'D:\Games\Star Wars - KotOR\swkotor.exe' -Algorithm SHA256
```

The storefront/version label alone is insufficient: a different hash is unsupported, including unverified Steam, retail or modified executables. The hash gate protects address-dependent hooks. The repository and release contain no original game executable or game assets; you need your own game installation.

## Controls

| Input | Action |
| --- | --- |
| Original mouse and keyboard controls | Move, select targets, interact, use the interface |
| Headset movement/rotation | Change the VR view from the third-person camera |
| F9 | Toggle the full original screen panel |
| F10 | Recenter headset position and horizontal heading |
| F11 | Pause/resume VR frame submission |
| F8 | Write diagnostic eye views and desktop image to BMP files beside the mod DLL |

The original desktop image remains available. The HUD is extracted from the normal frame and drawn over the stereo world. The Windows cursor, including changing hover icons, is drawn separately in both eyes at 1.5× size. Scene cursor placement uses depth under the original mouse position; interface placement follows the UI plane. Keep the game window focused and the mouse inside it when clicking.

## Project contents

| Location | Contents |
| --- | --- |
| `src/` | Launcher, game hooks, OpenXR backend, HUD, cursor and version resources |
| `deps/` | Vendored OpenXR SDK and MinHook sources with upstream licenses |
| `tests/` | Production matrix/eye-offset/cursor-projection checks |
| `build.ps1` | MSVC x86 configuration, compilation and CTest |
| `install.ps1`, `restore-settings.ps1` | Deployment and restoration of the three video settings |
| `package.ps1` | Versioned runtime ZIP and SHA-256 manifest |
| `docs/` | Detailed English and Russian build, installation and development guides |
| `licenses/` | Third-party license texts included with binary distributions |

`bin/`, `build/`, `dist/`, private configuration backups and diagnostic files are excluded from Git. **Compiled binaries are distributed in GitHub Releases.** You can build offline once the repository and required development tools are available; dependencies are already included.

## Known limitations and diagnostics

- Only the exact executable above is supported. Other OpenXR runtimes/headsets are not verified.
- The renderer runs an ordinary desktop pass and separate geometry passes for each eye. This costs more GPU/CPU time than the original game; no universal framerate is promised.
- Menus, movies, dialogues and some effects may need F9. The difference-based HUD mask can also pick up screen effects.
- Quest controllers do not provide game input. Existing compatible game mods may still affect rendering and are not broadly tested.
- Head tracking changes the camera; ordinary game mouse clicks keep the original game input path. Cursor alignment was confirmed in tested scenes, not every possible camera/effect.
- The launcher chooses the installed 32-bit Virtual Desktop runtime for itself and the game when no runtime override is supplied. It does not change the system-wide OpenXR runtime.

Log: `<game>\kotor-vr\bin\kotor-vr.log` (replaced on each launch). Standalone headset test: `bin\kotor-vr-probe.exe --seconds=15`. Headset-free checks: `bin\kotor-vr-math-test.exe`. See [troubleshooting](docs/INSTALL.md#troubleshooting) before reporting an issue. Remove private paths or screenshots before publishing diagnostics.

## License and acknowledgements

Original mod code is under the [MIT license](LICENSE). Dependencies retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md). This is an unofficial fan project, unaffiliated with the game's rights holders, Meta or Virtual Desktop.

Thanks to [KotOR Patch Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) for executable/address references, [Ghidra](https://github.com/NationalSecurityAgency/ghidra) for analysis, [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) and [MinHook](https://github.com/TsudaKageyu/minhook).
