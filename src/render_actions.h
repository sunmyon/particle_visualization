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

#ifdef VOLUME_RENDERING
class ParticleArray;
namespace lbvh { class MortonBuilder; }

void PrepareVolumeRendering(ParticleArray& part,
                            lbvh::MortonBuilder& bvh,
			    VolumeRenderingRuntime& volume,
                            RenderRuntimeState& render);

void ReloadVolumeRendering(VolumeRenderingRuntime& volume, RenderRuntimeState& render);
#endif

#ifdef ISO_CONTOUR
class ParticleArray;
enum class QuantityId : int;

void BuildIsoContourMesh(ParticleArray& part,
                         QuantityId selectedVar,
                         float isoLevel,
                         int max_treelevel,
			 IsoContourRuntime& iso,
                         RenderRuntimeState& render);
#endif
