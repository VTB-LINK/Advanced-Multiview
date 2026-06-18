# NDI SDK integration for the multiview NDI output feature.
#
# Unlike Spout (which we statically link from a vendored submodule), NDI is
# introduced as a *build-time header-only* dependency: we compile against the
# installed NDI SDK headers but never link its import library and never bundle
# its runtime DLL. The plugin locates and loads Processing.NDI.Lib.x64.dll
# dynamically at runtime (see src/multiview-ndi-runtime.cpp), so the only thing
# this module needs is the SDK include directory.
#
# Detection is non-fatal: if the SDK is not installed (e.g. on CI runners),
# we print a STATUS message and turn ENABLE_NDI_OUTPUT OFF so the NDI source
# files compile to empty translation units (they are guarded by
# AMV_ENABLE_NDI_OUTPUT). This keeps CI and third-party builds green without
# the SDK; building or testing the NDI backend simply requires installing it.
#
# Windows-only for now (matches the Spout output gate). Set the NDI_SDK_DIR
# environment variable to override the default install-path search.

# `Processing.NDI.Lib.h` is the umbrella header; finding it pins the Include dir.
# Search order: explicit NDI_SDK_DIR override, then the default v6 and v5
# installer locations. Both runtimes are supported at load time, so either SDK
# version's headers suffice (we only use the ABI-frozen v5 function subset).
find_path(
  NDI_SDK_INCLUDE_DIR
  NAMES Processing.NDI.Lib.h
  HINTS "$ENV{NDI_SDK_DIR}" "$ENV{NDI_SDK_DIR}/Include"
  PATHS
    "C:/Program Files/NDI/NDI 6 SDK/Include"
    "C:/Program Files/NDI/NDI 5 SDK/Include"
    "C:/Program Files/NewTek/NDI 5 SDK/Include"
  NO_CACHE
)

if(NDI_SDK_INCLUDE_DIR)
  message(STATUS "NDI SDK found: ${NDI_SDK_INCLUDE_DIR} (NDI output enabled)")
else()
  message(
    STATUS
    "NDI SDK not found - multiview NDI output disabled. "
    "Install the NDI SDK (or set NDI_SDK_DIR) to build/test the NDI backend."
  )
  set(ENABLE_NDI_OUTPUT OFF)
endif()
