#include <utility>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <cmath>
#include <cstdlib>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "app/execution/analysis_execution.h"
#include "app/state/app_state.h"
#include "app/state/analysis_state.h"
#include "app/state/runtime_state.h"
#include "app/state/normalization_config.h"
#include "app/state/tracking_view_state.h"
#include "app/state/snapshot_state_sync.h"
#include "app/execution/snapshot_sequence_job.h"
#include "app/app_visibility_actions.h"
#include "app/app_data_actions.h"
#include "data/particle_array.h"
#include "data/particle_selection.h"
#include "data/clump_loader.h"
#include "data/clump_store.h"
#include "data/halo_store.h"
#include "render/scene_objects.h"
#ifdef STREAM_LINE
#include "analysis/streamline/streamline.h"

void ExecuteStreamlinePreviewRequest(StreamlinePreviewRequestState& request,
                                     StreamlinePreviewResultState& result)
{
  if (request.clearRequested) {
    result = StreamlinePreviewResultState{};
    request.clearRequested = false;
    return;
  }

  if (!request.updateRequested) {
    return;
  }

  request.updateRequested = false;

  result = StreamlinePreviewResultState{};

  CubeObject cube;
  cube.center  = glm::vec3(request.seedCenter[0],
                           request.seedCenter[1],
                           request.seedCenter[2]);
  cube.halfSize = 0.5f * glm::vec3(request.seedSize[0],
                                   request.seedSize[1],
                                   request.seedSize[2]);
  
  cube.orientation = glm::quat{1, 0, 0, 0};
  cube.color   = glm::vec3(1.0f);
  cube.opacity = request.opacity;
  cube.tag     = "streamline_seed_region";

  result.valid = true;
  result.cube = std::move(cube);
  result.cpuUpdated = true;
}

void ExecuteStreamlineBuildRequest(ParticleArray& particles,
                                   StreamlineComputer& streamLine,
                                   StreamlineBuildRequestState& request,
                                   StreamlineBuildResultState& result)
{
  if (request.clearRequested) {
    result.lines.clear();
    result.cpuUpdated = true;
    request.clearRequested = false;
    return;
  }

  if (!request.runRequested) {
    return;
  }

  request.runRequested = false;

  StreamlineBuildSpec spec;
  spec.nSeeds = request.nSeeds;
  spec.thetaMaxDegrees = request.thetaMaxDegrees;
  spec.useManualSeed = request.useManualSeed;
  for (int i = 0; i < 3; ++i) {
    spec.manualSeed[i] = request.manualSeed[i];
  }

  if (request.seedSize[0] > 0.f &&
      request.seedSize[1] > 0.f &&
      request.seedSize[2] > 0.f) {
    spec.seedRegion.enabled = true;
    for (int i = 0; i < 3; ++i) {
      spec.seedRegion.center[i] = request.seedCenter[i];
      spec.seedRegion.size[i] = request.seedSize[i];
    }
  }

  if (request.limitRegion &&
      request.regionSize[0] > 0.f &&
      request.regionSize[1] > 0.f &&
      request.regionSize[2] > 0.f) {
    spec.fieldRegion.enabled = true;
    for (int i = 0; i < 3; ++i) {
      spec.fieldRegion.center[i] = request.regionCenter[i];
      spec.fieldRegion.size[i] = request.regionSize[i];
    }
  }

  auto builtLines = streamLine.build(particles.particleBlock, spec);

  result.lines.clear();
  for (const auto& linePoints : builtLines) {
    if (linePoints.empty()) continue;

    LineObject line;
    line.points.reserve(linePoints.size());
    for (const auto& p : linePoints) {
      line.points.emplace_back(p.x, p.y, p.z);
    }

    line.color   = glm::vec3(1.0f);
    line.opacity = 1.0f;
    line.tag     = "streamline";
    result.lines.push_back(std::move(line));
  }

  result.cpuUpdated = true;
}
#endif

void ExecuteStellarDensityRequest(ParticleArray& particles,
				  const UnitSystem& units,
				  const NormalizationContext& normalization,
                                  StellarDensityRequestState& request,
				  double time)
{
  if (!request.runRequested) {
    return;
  }

  request.runRequested = false;

  std::array<bool, 6> sel{};
  for (int i = 0; i < 6; ++i) {
    sel[i] = request.selectedTypes[i];
  }

  particles.computeStellarDensity(sel,
                                  request.overwriteHsml,
                                  normalization,
                                  time,
                                  units);
  particles.particlesDirty = true;
}

#ifdef ISO_CONTOUR
#include "analysis/isosurface/iso_contour_build.h"
#include "analysis/isosurface/mesh_data.h"
void ExecuteIsoContourRequest(ParticleArray& particles,
                              IsoContourRequestState& request,
                              IsoContourGeometryState& geometry,
                              RenderLayerState& isoContourRenderState)
{
  if (request.clearRequested) {
    geometry.verts.clear();
    geometry.inds.clear();
    isoContourRenderState.show = false;
    isoContourRenderState.cpuUpdated = true;
    request.clearRequested = false;
    return;
  }

  if (!request.runRequested) {
    return;
  }

  request.runRequested = false;

  IsoContourBuildParams params;
  params.selectedQuantity = request.selectedQuantity;
  params.isoLevel = request.isoLevel;
  params.maxTreeLevel = request.maxTreeLevel;

  Mesh mesh = BuildIsoContourMesh(particles.particleBlock, params);
  geometry.verts = std::move(mesh.vertices);
  geometry.inds = std::move(mesh.indices);

  isoContourRenderState.show = true;
  isoContourRenderState.cpuUpdated = true;
}
#endif
