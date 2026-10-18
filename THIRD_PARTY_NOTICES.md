# Third-party notices / Лицензии зависимостей

The root MIT license covers original KotOR VR code. Vendored dependencies keep their original per-file licenses and copyright notices. Корневая MIT относится к собственному коду мода; зависимости сохраняют исходные лицензии и авторство.

| Component | Source / license location |
| --- | --- |
| Khronos OpenXR SDK, loader and headers | https://github.com/KhronosGroup/OpenXR-SDK — `deps/OpenXR-SDK/LICENSE`, `COPYING.adoc`, `LICENSES/`, per-file notices. Primarily Apache-2.0, with Apache-2.0 OR MIT for applicable loader/header files; other files retain their annotated terms |
| MinHook, including bundled disassembler notices | https://github.com/TsudaKageyu/minhook — `deps/minhook/LICENSE.txt`, BSD-style terms and included third-party notices |
| JsonCpp, vendored by OpenXR SDK | `deps/OpenXR-SDK/src/external/jsoncpp/LICENSE`, MIT/public-domain terms as stated upstream |
| jnipp, vendored by OpenXR SDK | `deps/OpenXR-SDK/src/external/jnipp/LICENSE` |
| sanitizers-cmake, vendored by OpenXR SDK | `deps/OpenXR-SDK/src/external/sanitizers-cmake/LICENSE` |

Runtime archives include copies in `licenses/`. OpenXR license guidance and its complete `LICENSES/` directory are included there as well. Consult upstream per-file notices for authoritative coverage. The SDK's Android/helper dependencies are retained in the source tree even when not used by the Windows runtime build.

KotOR Patch Manager was an address/reference resource during analysis; its source/database is not embedded in the runtime or vendored here. Ghidra/REA were development tools and are not packaged. Microsoft C++ runtime and Virtual Desktop/VDXR are separately installed third-party products and are not redistributed in this archive.

STAR WARS and Knights of the Old Republic names identify the target game. No original game executable, artwork, audio, save data or other game assets are distributed by this project.
