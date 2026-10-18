# Build and package

[Русский](BUILD.ru.md) · [Home](../README.md)

## Requirements

- Windows x64, PowerShell 5.1 or newer, Git, CMake 3.20 or newer, and Python 3 for OpenXR's CMake configuration.
- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++**, MSVC x86/x64 tools and a Windows 10/11 SDK. The release was built with MSVC 14.44.35207 and SDK 10.0.22621.0.
- A 32-bit build is mandatory because `swkotor.exe` is x86. The script uses the x64-hosted x86 compiler and NMake. Do not configure this project for x64.
- A game/headset is not required to compile or run the math tests. A supported game and working 32-bit OpenXR runtime are needed for live validation.

## Standard build

```powershell
git clone https://github.com/megabars/kotor-vr.git
Set-Location kotor-vr
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
.\bin\kotor-vr-launcher.exe --version
```

The script discovers Visual Studio with `vswhere`, selects an installed Windows SDK, configures `build/`, builds **Release**, and runs CTest. OpenXR SDK and MinHook are vendored; no dependency download or recursive submodule command is needed. `VERSION` supplies the project, header and Windows resource version. The expected release version is `0.0.1`.

Output files in `bin/`:

| File | Role |
| --- | --- |
| `kotor_vr.dll` | Injected game renderer module |
| `kotor-vr-launcher.exe` | EXE verification, runtime selection and DLL loading |
| `kotor-vr-probe.exe` | Independent headset cube test |
| `kotor-vr-math-test.exe` | Headset-free projection and cursor checks |

The OpenXR loader and MinHook are linked statically. The Microsoft C/C++ runtime is dynamic (`/MD`); users need the VC++ **x86** Redistributable. The actual vendor OpenXR runtime remains external.

## Custom tool locations and options

`-ToolchainRoot` must point to the **version directory**, not the Visual Studio installation root. `-WindowsSdkRoot` must contain `Include`, `Lib` and `bin`:

```powershell
.\build.ps1 -ToolchainRoot 'D:\BuildTools\VC\Tools\MSVC\14.44.35207' -WindowsSdkRoot 'D:\Windows Kits\10' -WindowsSdkVersion '10.0.22621.0'
.\build.ps1 -Configuration RelWithDebInfo -BuildDirectory "$PWD\build-debug-info" -OutputDirectory "$PWD\bin-debug-info"
.\build.ps1 -ConfigureOnly
.\build.ps1 -ProbeOnly -BuildDirectory "$PWD\build-probe" -OutputDirectory "$PWD\bin-probe"
```

The process's `PATH`, `INCLUDE`, `LIB` and `VSLANG` are restored after the script. No global environment settings are changed. Build options on an existing CMake cache may conflict after changing compilers/generators; use a new `-BuildDirectory`. `-ProbeOnly` omits the game DLL, so it cannot produce a complete installable package. Custom output folders are not automatically ignored; do not commit generated binaries.

Equivalent configuration from an **x86 MSVC developer environment**:

```powershell
cmake -S . -B build-manual -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_RC_COMPILER=rc
cmake --build build-manual
ctest --test-dir build-manual --output-on-failure
```

## Test and deploy your build

```powershell
ctest --test-dir build --output-on-failure
.\bin\kotor-vr-math-test.exe
.\bin\kotor-vr-launcher.exe --version
.\bin\kotor-vr-launcher.exe --help
.\install.ps1 -GameDirectory 'D:\Games\Star Wars - KotOR'
```

Tests exercise the production stereo projection, head/eye offsets and cursor unprojection. Passing them does not prove headset visibility or game compatibility. For live checks, connect Virtual Desktop, run the probe, then launch the game and verify depth, recentering, UI and cursor clicks in a save. See [installation](INSTALL.md).

## Create a release archive

Build a full Release into the default `bin/`, then run:

```powershell
.\package.ps1
Get-FileHash .\dist\kotor-vr-0.0.1-windows-x86.zip -Algorithm SHA256
```

The script checks the version resource of all four binaries, stages an allowlist of runtime files, documentation, installer and licenses, then writes:

- `dist/kotor-vr-0.0.1-windows-x86.zip`, containing top-level `kotor-vr/`;
- `dist/SHA256SUMS.txt`, containing the archive SHA-256;
- a binary checksum manifest **inside** the archive.

Source dependencies, game files, PDB/ILK files, logs, local INI files, saves and screenshots are excluded. The script refuses to reuse its staging directory or overwrite an existing archive. For another packaging attempt use `-OutputDirectory "$PWD\dist\attempt-2"`. Archive timestamps can differ between builds; hashes identify the actual uploaded archive, not a promise of byte-for-byte reproducible builds.

Commit source and documentation to Git. Publish the runtime ZIP and archive checksum as GitHub Release assets for tag `v0.0.1`. Keep binary files out of Git history.

## Common build failures

| Error | Resolution |
| --- | --- |
| MSVC/vswhere not found | Install the C++ workload or supply the exact `-ToolchainRoot` |
| Missing `rc.exe`, `kernel32.lib`, headers | Install a complete Windows SDK and supply the root/version |
| `KOTOR requires an x86 build` | Use the x86 compiler and a fresh CMake build directory |
| Python not found during SDK configuration | Install Python 3 and make it available in this terminal |
| `cmake` not recognized | Install CMake and reopen the terminal |
| Access denied replacing a DLL | Close the game using that output DLL before rebuilding |
| Debug build fails on another PC | Distribute Release; debug CRT is not a redistributable runtime |
