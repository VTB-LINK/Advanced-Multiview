/*
OBS Advanced Multiview - Spout output backend (issue #11)

Windows/DirectX-only. Transmits the composed multiview frame as a Spout
sender by sharing the OBS render-target texture directly on the GPU (no CPU
readback). Implemented against the vendored SpoutDX library (deps/Spout2),
independent of the obs-spout2 plugin used for Spout *input* cells.

Copyright (C) 2025 VTB-LINK
License: GPL-2.0-or-later
*/

#pragma once

#ifdef AMV_ENABLE_SPOUT_OUTPUT

#include "multiview-output.hpp"

#include <memory>

/* Factory: a fresh, not-yet-opened Spout backend. The D3D11 device + sender
 * are acquired lazily on the first submit_frame (graphics thread). */
std::unique_ptr<IMultiviewOutputBackend> create_spout_output_backend();

#endif /* AMV_ENABLE_SPOUT_OUTPUT */
