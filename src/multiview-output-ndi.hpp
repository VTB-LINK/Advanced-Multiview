/*
OBS Advanced Multiview - NDI output backend (issue #11)

Transmits the composed multiview frame as an NDI source. The frame is read
back from the GPU (GS_BGRA texrender -> staging surface -> CPU) and sent as a
BGRA NDI video frame. Independent of OBS's source/scene system (Approach B);
built against the installed NDI SDK headers and the runtime DLL loaded
on demand (see multiview-ndi-runtime).

Copyright (C) 2025 VTB-LINK
License: GPL-2.0-or-later
*/

#pragma once

#ifdef AMV_ENABLE_NDI_OUTPUT

#include "multiview-output.hpp"

#include <memory>

/* Factory: a fresh NDI backend. The runtime + sender + staging surface are
 * acquired lazily on the first submit_frame (graphics thread). */
std::unique_ptr<IMultiviewOutputBackend> create_ndi_output_backend();

#endif /* AMV_ENABLE_NDI_OUTPUT */
