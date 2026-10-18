# Release 0.0.1 validation

[Русский](VALIDATION.ru.md) · [Implementation](DEVELOPMENT.md)

Date: 2026-10-18. This records release preparation, not a claim of full-game certification.

- Built all four Windows x86 runtime targets in Release with MSVC 14.44.35207 / Windows SDK 10.0.22621.0. Every binary carries Windows file/product version `0.0.1`; launcher `--version` reports `0.0.1`.
- CTest passed the production stereo matrix, head/eye-offset and cursor projection checks in Release.
- Installer integration ran in a separate temporary game folder using a local supported EXE. It verified copied binary hashes, correct INI section targeting, preservation of non-ASCII bytes, first-backup preservation across reinstall, restoration of original values, removal of an originally absent key, preservation of an unrelated later user setting, and rejection of an unsupported EXE before deployment.
- Scripts were parsed and installer/restoration exercised with Windows PowerShell 5.1. Local test EXE/configurations remain ignored and are not distributed.
- Release ZIP uses an explicit file allowlist, includes dependency licenses and an internal binary checksum manifest, and excludes original game files, personal backups, debug symbols and logs. The separate `SHA256SUMS.txt` identifies the uploaded ZIP.

Earlier live prototype checks on Quest 3S + Virtual Desktop/VDXR confirmed depth, head tracking, HUD and cursor/clicks, as described in [implementation notes](DEVELOPMENT.md#what-was-verified). The release rebuild adds metadata and distribution scripts without intentional renderer behavior changes. A new end-to-end headset session of this exact packaged build has not been claimed.

To repeat installer integration from a source checkout after building:

```powershell
.\tests\install-test.ps1 -SupportedExe 'D:\Games\Star Wars - KotOR\swkotor.exe'
```

The test does not launch or modify that game installation; it copies the EXE locally into ignored `test-output/`. Do not publish that directory.
