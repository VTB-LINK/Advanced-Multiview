/*
OBS Advanced Multiview - multiview output layer (issue #11)

Backend-agnostic transmission of the composed multiview frame, independent
of OBS's source/scene system (Approach B). The window renders the grid once
per frame into a shared offscreen target; the manager then fans that single
texture out to every enabled backend. Spout lands first; NDI reuses the exact
same interface later (it just reads the texture back to CPU inside its own
submit_frame).

Copyright (C) 2025 VTB-LINK
License: GPL-2.0-or-later
*/

#pragma once

#include <obs.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

/* One output protocol (Spout / NDI / ...). All methods run on the OBS
 * graphics thread. */
class IMultiviewOutputBackend {
public:
	virtual ~IMultiviewOutputBackend() = default;

	/* Stable identifier for logs, e.g. "spout" / "ndi". */
	virtual const char *kind() const = 0;

	/* Transmit one frame. The backend lazily (re)creates its sender to
	 * match `name` and the texture's dimensions/format, then sends `tex`
	 * (a GS_BGRA texture owned by the manager's texrender). The backend
	 * must not retain `tex` past the call: Spout copies it on the GPU; NDI
	 * will read it back to CPU here. */
	virtual void submit_frame(const std::string &name, gs_texture_t *tex, uint32_t w, uint32_t h) = 0;

	/* Release the sender and all GPU/OS resources. Called on the graphics
	 * thread. Safe to call when never started. */
	virtual void stop() = 0;

	/* True once a sender is live and has transmitted at least one frame. */
	virtual bool is_active() const = 0;
};

/* Owns the shared offscreen render target and the active backends for one
 * multiview instance. */
class MultiviewOutputManager {
public:
	MultiviewOutputManager();
	~MultiviewOutputManager();

	MultiviewOutputManager(const MultiviewOutputManager &) = delete;
	MultiviewOutputManager &operator=(const MultiviewOutputManager &) = delete;

	/* Enable/disable the Spout backend. Returns the resulting enabled
	 * state (false when Spout output is unavailable on this platform).
	 * Idempotent. Enabling only allocates a lightweight backend object —
	 * the actual D3D/Spout sender is created lazily on the first frame
	 * (graphics thread). Disabling tears the sender down via the graphics
	 * thread, so this is safe to call from the UI thread. */
	bool set_spout_enabled(bool enabled);
	bool spout_enabled() const { return spout_ != nullptr; }

	/* True when at least one backend is enabled, i.e. render() should do
	 * the offscreen pass this frame. */
	bool has_backends() const { return !backends_.empty(); }

	/* Graphics-thread: draw the grid once into a (w x h) BGRA target via
	 * `draw` (which should paint the composition mapped to 0,0,w,h), then
	 * dispatch the resulting texture to every backend. Returns that texture
	 * so the caller can also blit it to its on-screen display, or nullptr
	 * on failure / when no backend is enabled. */
	gs_texture_t *render_and_dispatch(const std::string &name, uint32_t w, uint32_t h,
					  const std::function<void()> &draw);

	/* Release the texrender and stop all backends. Wraps obs_enter_graphics
	 * so it is safe to call from the UI thread (window close / destroy). */
	void shutdown_graphics();

	/* Whether Spout output is even possible here. Reuses the existing
	 * Spout platform detection (Windows-only); the D3D11-renderer check is
	 * deferred to the backend on the graphics thread. */
	static bool spout_supported();

private:
	gs_texrender_t *texrender_ = nullptr;
	std::vector<std::unique_ptr<IMultiviewOutputBackend>> backends_;
	IMultiviewOutputBackend *spout_ = nullptr; /* non-owning, points into backends_ */
};
