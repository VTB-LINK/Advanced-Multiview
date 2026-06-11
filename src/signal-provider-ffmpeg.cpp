/*
OBS Advanced Multiview - FFmpeg signal provider (Phase 3 / M6.1)

Wraps OBS's built-in `ffmpeg_source` so multiview cells can host network
media (RTMP / HLS / FLV / SRT / file URLs / any FFmpeg-accepted URL)
without the user manually adding the source to a scene. The source is
created as a private OBS source (via `obs_source_create_private`) so it
stays out of `obs_enum_sources` / the OBS Sources dock and only this
plugin can render or release it.

Scope of this milestone:
  - Availability detection (is the host plugin present?).
  - One-shot create-or-update from a SignalConfig.
  - Release via OBSSource RAII; no SDK calls, no extra threads.

Health, reconnect, fallback and SIGNAL LOST overlays land in step 10.

Copyright (C) 2025 VTB-LINK
License: GPL-2.0-or-later
*/

#include "signal-provider.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <cstring>

namespace {

constexpr const char *kFfmpegSourceId = "ffmpeg_source";

/* Settings keys defined by the upstream `ffmpeg_source` plugin (see
 * obs-studio/plugins/obs-ffmpeg/obs-ffmpeg-source.c). We pin the exact
 * spellings here so a typo cannot silently render a non-functional URL
 * source. M6.1 only sets the live-URL essentials; advanced playback
 * settings (looping, speed, hardware decode, color range) are left at
 * source defaults so OBS's own behavior is the reference. */
constexpr const char *kKeyInput = "input";
constexpr const char *kKeyIsLocalFile = "is_local_file";
constexpr const char *kKeyReconnectDelaySec = "reconnect_delay_sec";
constexpr const char *kKeyBufferingMb = "buffering_mb";
constexpr const char *kKeyRestartOnActivate = "restart_on_activate";
constexpr const char *kKeyCloseWhenInactive = "close_when_inactive";
constexpr const char *kKeyClearOnMediaEnd = "clear_on_media_end";
constexpr const char *kKeyLinearAlpha = "linear_alpha";

class FfmpegProvider : public ISignalProvider {
public:
	SignalProviderType type() const override { return SignalProviderType::Ffmpeg; }
	const char *id() const override { return signal_provider_to_string(type()); }
	const char *display_name() const override { return "FFmpeg Media"; }

	bool is_available() const override
	{
		/* Cheapest path: ask OBS whether the source type is registered.
		 * obs_source_get_display_name returns nullptr when no plugin
		 * registered the id, which is the same signal SourcePicker
		 * uses for placeholder vs real tab decisions. ffmpeg_source is
		 * shipped with OBS Studio so this should be true on every
		 * supported install; we still gate on it so the picker shows
		 * a clear reason if the user removed the obs-ffmpeg plugin. */
		return obs_source_get_display_name(kFfmpegSourceId) != nullptr;
	}

	std::string unavailable_reason() const override
	{
		if (is_available())
			return std::string();
		return "OBS built-in FFmpeg media source (ffmpeg_source) is not registered.";
	}

	OBSSource create_private_source(const std::string &desired_name, const SignalConfig &cfg) const override
	{
		if (!is_available()) {
			obs_log(LOG_WARNING, "[signal-provider/ffmpeg] create skipped: ffmpeg_source unavailable");
			return OBSSource();
		}

		/* Pull the user-entered URL out of the provider settings.
		 * SourcePicker writes it to `input`; we treat missing/empty
		 * URL as a soft failure (return null) so the runtime can paint
		 * the cell as MISSING and the user can edit. */
		const char *input_url = nullptr;
		obs_data_t *src_settings = cfg.providerSettings;
		if (src_settings)
			input_url = obs_data_get_string(src_settings, kKeyInput);
		if (!input_url || !*input_url) {
			obs_log(LOG_WARNING, "[signal-provider/ffmpeg] create skipped: empty input URL");
			return OBSSource();
		}

		/* Build OBS source settings from the provider config plus the
		 * M6.1 live-URL defaults documented in the plan. We deliberately
		 * keep the obs_data_t separate from cfg.providerSettings so the
		 * user-facing config never sees our defaults persisted back. */
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, kKeyInput, input_url);
		obs_data_set_bool(settings, kKeyIsLocalFile, false);
		obs_data_set_int(settings, kKeyReconnectDelaySec, 10);
		obs_data_set_int(settings, kKeyBufferingMb, 2);
		obs_data_set_bool(settings, kKeyRestartOnActivate, true);
		obs_data_set_bool(settings, kKeyCloseWhenInactive, false);
		obs_data_set_bool(settings, kKeyClearOnMediaEnd, true);
		obs_data_set_bool(settings, kKeyLinearAlpha, false);

		/* obs_source_create_private returns a +1 strong ref; we hand
		 * it to OBSSource which holds it as a +1 strong ref and will
		 * release on destruction. The intermediate raw release matches
		 * the standard OBS C++ wrapper idiom and ensures no leak if
		 * the OBSSource assignment throws. */
		obs_source_t *raw = obs_source_create_private(kFfmpegSourceId, desired_name.c_str(), settings);
		obs_data_release(settings);
		if (!raw) {
			obs_log(LOG_WARNING, "[signal-provider/ffmpeg] obs_source_create_private failed for '%s'",
				desired_name.c_str());
			return OBSSource();
		}

		obs_log(LOG_INFO, "[signal-provider/ffmpeg] created private source '%s' input='%s'",
			desired_name.c_str(), input_url);

		OBSSource wrapper(raw);
		obs_source_release(raw);
		return wrapper;
	}
};

static FfmpegProvider g_ffmpeg_provider;

} /* anonymous namespace */

/* ========== Module entry point ========== */

/* Called from signal_provider_registry_init via a TU-local hook in
 * signal-provider.cpp. Keeping the registration in this file keeps the
 * FFmpeg specifics out of the registry skeleton. */
void register_ffmpeg_provider()
{
	SignalProviderRegistry::instance().register_provider(&g_ffmpeg_provider);
}
