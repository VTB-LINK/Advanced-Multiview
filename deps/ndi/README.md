# Vendored NDI SDK headers

`include/` holds the **headers only** from the [NDI SDK](https://ndi.video/for-developers/ndi-sdk/)
(v6), copied verbatim from `NDI 6 SDK/Include/`. They let the project build the
NDI external output **without every developer (or CI) installing the SDK** — the
same approach the DistroAV plugin uses (`lib/ndi/`).

- **Headers only.** No import library and no runtime DLL are vendored or
  redistributed. At runtime the plugin loads `Processing.NDI.Lib.x64.dll`
  dynamically from the installed NDI runtime (see
  [`src/multiview-ndi-runtime.cpp`](../../src/multiview-ndi-runtime.cpp)).
- **Override.** `cmake/ndi-output.cmake` prefers an installed SDK
  (`NDI_SDK_DIR` env var or the default install path) and only falls back to
  these vendored headers when none is found, so a locally installed SDK still
  wins.
- **License.** Use of these headers is governed by `NDI SDK License Agreement.pdf`
  (included here). "NDI" is a registered trademark of Vizrt NV.

To update: copy the contents of a newer `NDI x SDK/Include/` over `include/`.
