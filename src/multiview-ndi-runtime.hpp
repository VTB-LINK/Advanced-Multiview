/*
OBS Advanced Multiview - NDI runtime loader (issue #11)

Process-wide, lazily-loaded handle to the NDI runtime DLL. We never link the
NDI import library nor bundle its DLL; instead we locate and load
Processing.NDI.Lib.x64.dll at runtime and resolve the versioned function table.

We deliberately load the *v5* table (NDIlib_v5_load): an NDI 6 runtime still
exports it and a stand-alone NDI 5 runtime exports it natively, so a single
build runs against either runtime. NDIlib_v5 is an ABI-frozen typedef of the
full v6 struct, and we only read the v5-era send/video fields, so reading the
table returned by an NDI 5 runtime never strays past its bounds.

Copyright (C) 2025 VTB-LINK
License: GPL-2.0-or-later
*/

#pragma once

#ifdef AMV_ENABLE_NDI_OUTPUT

/* The NDI headers use NULL in default-argument values without including a
 * header that defines it; pull in <cstddef> first so they compile under
 * clang/gcc (MSVC happens to have NULL in scope already). */
#include <cstddef>

#include <Processing.NDI.Lib.h>

#include <memory>

/* Ref-counted (via shared_ptr) handle to the loaded NDI runtime. The DLL is
 * loaded + NDIlib_initialize()'d on first acquire() and NDIlib_destroy()'d +
 * unloaded when the last holder drops — so every NDI backend must keep its
 * shared_ptr alive for as long as it owns a sender, and destroy that sender
 * BEFORE releasing the handle (NDIlib_destroy requires all senders gone). */
class NdiRuntime {
public:
	~NdiRuntime();

	NdiRuntime(const NdiRuntime &) = delete;
	NdiRuntime &operator=(const NdiRuntime &) = delete;

	/* The loaded v5 function table; valid for this handle's lifetime. */
	const NDIlib_v5 *lib() const { return lib_; }

	/* Acquire a shared handle to the NDI runtime, loading it on first use.
	 * Returns nullptr if the runtime can't be located/loaded/initialized
	 * (logged once); callers stay dormant in that case. */
	static std::shared_ptr<NdiRuntime> acquire();

	/* Whether the runtime can be loaded right now, without retaining a handle.
	 * Used by the settings UI to enable/disable the NDI tab. */
	static bool available();

private:
	NdiRuntime(void *module, const NDIlib_v5 *lib) : module_(module), lib_(lib) {}

	void *module_ = nullptr; /* QLibrary* handle to the NDI runtime (Qt, cross-platform) */
	const NDIlib_v5 *lib_ = nullptr;
};

#endif /* AMV_ENABLE_NDI_OUTPUT */
