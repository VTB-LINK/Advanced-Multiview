/*
OBS Advanced Multiview - NDI runtime loader (issue #11)

Copyright (C) 2025 VTB-LINK
License: GPL-2.0-or-later
*/

#ifdef AMV_ENABLE_NDI_OUTPUT

#include "multiview-ndi-runtime.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <mutex>
#include <string>

namespace {

/* NDI 5 uses its own runtime-dir env var; the v6 header only defines the v6
 * one (NDILIB_REDIST_FOLDER). Try v6 first, then v5, then the bare DLL name so
 * the OS loader resolves it from PATH / the NDI install dir on the search path. */
constexpr const char *kRuntimeDirEnvV6 = NDILIB_REDIST_FOLDER; /* "NDI_RUNTIME_DIR_V6" */
constexpr const char *kRuntimeDirEnvV5 = "NDI_RUNTIME_DIR_V5";

std::string env_var(const char *name)
{
	/* Win32 over getenv to dodge MSVC's C4996 under /WX. */
	DWORD len = GetEnvironmentVariableA(name, nullptr, 0);
	if (len == 0)
		return {};
	std::string value(len, '\0');
	DWORD written = GetEnvironmentVariableA(name, value.data(), len);
	value.resize(written);
	return value;
}

/* Locate and LoadLibrary the NDI runtime DLL. Returns nullptr if not found.
 * Does NOT resolve the table or initialize — callers do that. */
HMODULE load_runtime_dll()
{
	for (const char *envName : {kRuntimeDirEnvV6, kRuntimeDirEnvV5}) {
		const std::string dir = env_var(envName);
		if (dir.empty())
			continue;
		std::string path = dir;
		if (path.back() != '\\' && path.back() != '/')
			path += '\\';
		path += NDILIB_LIBRARY_NAME;
		if (HMODULE h = LoadLibraryA(path.c_str()))
			return h;
	}
	/* Fall back to the OS search path (PATH, app dir, system dirs). */
	return LoadLibraryA(NDILIB_LIBRARY_NAME);
}

} /* anonymous namespace */

std::shared_ptr<NdiRuntime> NdiRuntime::acquire()
{
	static std::mutex mtx;
	static std::weak_ptr<NdiRuntime> cached;
	static bool warned_load_failed = false;

	std::lock_guard<std::mutex> lock(mtx);

	if (auto sp = cached.lock())
		return sp;

	HMODULE h = load_runtime_dll();
	if (!h) {
		if (!warned_load_failed) {
			obs_log(LOG_WARNING,
				"[multiview-output/ndi] NDI runtime not found ('%s'). "
				"Install the NDI runtime from %s to enable NDI output.",
				NDILIB_LIBRARY_NAME, NDILIB_REDIST_URL);
			warned_load_failed = true;
		}
		return nullptr;
	}

	/* v5 table for v5+v6 runtime compatibility (see header). */
	using load_fn_t = const NDIlib_v5 *(*)(void);
	auto load_fn = reinterpret_cast<load_fn_t>(GetProcAddress(h, "NDIlib_v5_load"));
	const NDIlib_v5 *lib = load_fn ? load_fn() : nullptr;
	if (!lib) {
		obs_log(LOG_WARNING, "[multiview-output/ndi] NDIlib_v5_load missing or returned null");
		FreeLibrary(h);
		return nullptr;
	}

	if (!lib->initialize()) {
		/* Almost always an unsupported (too old) CPU. */
		obs_log(LOG_WARNING, "[multiview-output/ndi] NDIlib initialize failed (unsupported CPU?)");
		FreeLibrary(h);
		return nullptr;
	}

	warned_load_failed = false;
	obs_log(LOG_INFO, "[multiview-output/ndi] NDI runtime loaded ('%s')", lib->version());

	auto sp = std::shared_ptr<NdiRuntime>(new NdiRuntime(h, lib));
	cached = sp;
	return sp;
}

bool NdiRuntime::available()
{
	/* If a backend already holds the runtime, it's obviously available. */
	if (HMODULE h = GetModuleHandleA(NDILIB_LIBRARY_NAME))
		return GetProcAddress(h, "NDIlib_v5_load") != nullptr;

	/* Otherwise probe a load without initializing or retaining anything — a
	 * lightweight "is the NDI runtime installed?" check for the settings UI. */
	HMODULE probe = load_runtime_dll();
	if (!probe)
		return false;
	const bool ok = GetProcAddress(probe, "NDIlib_v5_load") != nullptr;
	FreeLibrary(probe);
	return ok;
}

NdiRuntime::~NdiRuntime()
{
	/* All senders created from this table must already be destroyed (backends
	 * release their shared_ptr only after send_destroy). */
	if (lib_)
		lib_->destroy();
	if (module_)
		FreeLibrary(static_cast<HMODULE>(module_));
}

#endif /* AMV_ENABLE_NDI_OUTPUT */
