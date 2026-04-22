#pragma once

#include "app/app_services.h"

class CubeManager;
struct RenderRuntimeState;

#ifdef STREAM_LINE
class StreamlineComputer;
void UpdateSeedRegionPreview(StreamlineComputer& streamLine,
			     CubeManager& cubeManager,
                             RenderRuntimeState& render,
                             float seed_center[3],
                             float seed_len[3],
                             float seed_opacity);
#endif
