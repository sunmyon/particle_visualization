#include <algorithm>
#include <utility>
#include <array>
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
#include "data/simulation_dataset.h"
#include "data/particle_selection.h"
#include "data/clump_loader.h"
#include "data/clump_store.h"
#include "data/halo_store.h"
#include "render/scene_objects.h"
#ifdef STREAM_LINE
#include "analysis/streamline/streamline.h"

namespace {
std::array<LineObject, 3> MakeManualSeedMarker(const std::array<float, 3>& seed)
{
  const glm::vec3 c(seed[0], seed[1], seed[2]);
  constexpr float r = 1.0f;

  std::array<LineObject, 3> markers;
  const glm::vec3 color(1.0f, 0.15f, 0.05f);
  const glm::vec3 axes[3] = {
    glm::vec3(r, 0.0f, 0.0f),
    glm::vec3(0.0f, r, 0.0f),
    glm::vec3(0.0f, 0.0f, r)
  };

  for (int i = 0; i < 3; ++i) {
    markers[i].color = color;
    markers[i].opacity = 1.0f;
    markers[i].tag = "streamline";
    markers[i].points = {c - axes[i], c + axes[i]};
  }
  return markers;
}
}

void ExecuteStreamlinePreviewRequest(const SimulationDataset& particles,
                                     StreamlinePreviewRequestState& request,
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
  result.cpuUpdated = true;

  if (!request.showSeedBox) {
    return;
  }

  const float worldToRender =
    particles.simulationBlock.worldToRenderScale > 0.0f
      ? particles.simulationBlock.worldToRenderScale
      : 1.0f;

  CubeObject cube;
  cube.center  = worldToRender * glm::vec3(request.seedCenter[0],
                                                  request.seedCenter[1],
                                                  request.seedCenter[2]);
  cube.halfSize = 0.5f * worldToRender * glm::vec3(request.seedSize[0],
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

void ExecuteStreamlineBuildRequest(SimulationDataset& particles,
                                   StreamlineComputer& streamLine,
                                   StreamlineBuildRequestState& request,
                                   StreamlineBuildResultState& result)
{
  if (request.clearRequested) {
    result.lines.clear();
    result.success = false;
    result.message.clear();
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
  spec.fieldSource = request.fieldSource == 1
    ? StreamlineBuildSpec::FieldSource::BField
    : StreamlineBuildSpec::FieldSource::Velocity;
  spec.maxSteps = request.maxSteps;
  spec.stepScale = request.stepScale;
  spec.thetaMaxDegrees = request.thetaMaxDegrees;
  spec.useManualSeed = request.useManualSeed;
  const float worldToRender =
    particles.simulationBlock.worldToRenderScale > 0.0f
      ? particles.simulationBlock.worldToRenderScale
      : 1.0f;
  const float renderToWorld =
    particles.simulationBlock.worldToRenderScale > 0.0f
      ? 1.0f / particles.simulationBlock.worldToRenderScale
      : 1.0f;

  spec.manualSeeds = request.manualSeeds;
  for (auto& seed : spec.manualSeeds) {
    for (float& v : seed) {
      v *= worldToRender;
    }
  }

  if (request.seedSize[0] > 0.f &&
      request.seedSize[1] > 0.f &&
      request.seedSize[2] > 0.f) {
    spec.seedRegion.enabled = true;
    for (int i = 0; i < 3; ++i) {
      spec.seedRegion.center[i] = request.seedCenter[i] * worldToRender;
      spec.seedRegion.size[i] = request.seedSize[i] * worldToRender;
    }
  }

  if (request.limitRegion &&
      request.regionSize[0] > 0.f &&
      request.regionSize[1] > 0.f &&
      request.regionSize[2] > 0.f) {
    spec.fieldRegion.enabled = true;
    for (int i = 0; i < 3; ++i) {
      spec.fieldRegion.center[i] = request.regionCenter[i] * worldToRender;
      spec.fieldRegion.size[i] = request.regionSize[i] * worldToRender;
    }
  }

  auto built = streamLine.build(particles.simulationBlock, spec);

  result.lines.clear();
  result.success = built.ok;
  result.message = built.message;
  result.seedCount = built.seedCount;
  result.lineCount = built.lineCount;
  result.stopCounts = built.stopCounts;
  result.seedReports.clear();
  result.seedReports.reserve(built.seedReports.size());
  for (const auto& src : built.seedReports) {
    StreamlineBuildResultState::SeedReport dst;
    dst.seedIndex = src.seedIndex;
    dst.position[0] = src.position[0] * renderToWorld;
    dst.position[1] = src.position[1] * renderToWorld;
    dst.position[2] = src.position[2] * renderToWorld;
    dst.stopReason = static_cast<int>(src.stopReason);
    dst.pointCount = src.pointCount;
    dst.length = src.length * renderToWorld;
    result.seedReports.push_back(dst);
  }

  if (request.useManualSeed) {
    for (const auto& seed : spec.manualSeeds) {
      for (auto& marker : MakeManualSeedMarker(seed)) {
        result.lines.push_back(std::move(marker));
      }
    }
  }

  for (const auto& linePoints : built.lines) {
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

void ExecuteStellarDensityRequest(SimulationDataset& particles,
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
void ExecuteIsoContourRequest(SimulationDataset& particles,
                              IsoContourRequestState& request,
                              IsoContourGeometryState& geometry,
                              RenderLayerState& isoContourRenderState)
{
  if (request.clearRequested) {
    geometry.clear();
    isoContourRenderState.show = false;
    isoContourRenderState.cpuUpdated = true;
    request.clearRequested = false;
    return;
  }

  if (!request.runRequested && !request.applyRequested) {
    return;
  }

  const bool rebuildTreeRequested = request.runRequested;
  const bool applyThresholdRequested = request.applyRequested;
  request.runRequested = false;
  request.applyRequested = false;

  IsoContourBuildParams params;
  params.selectedQuantity = request.selectedQuantity;
  params.isoLevel = request.isoLevel;
  params.maxTreeLevel = request.maxTreeLevel;
  params.minParticles =
    static_cast<std::size_t>(std::max(1, request.minParticlesPerLeaf));
  params.cornerReconstructionMode =
    std::clamp(request.cornerReconstructionMode, 0, 2);

  std::string treeMessage;
  const bool cacheMatches =
    geometry.cacheMatches(params.selectedQuantity,
                          params.maxTreeLevel,
                          static_cast<int>(params.minParticles),
                          params.cornerReconstructionMode);
  if (rebuildTreeRequested || !cacheMatches) {
    IsoContourTreeBuildResult tree =
      BuildAdaptiveIsoContourTree(particles.simulationBlock, params);
    geometry.adaptiveTree = std::move(tree.tree);
    geometry.adaptiveTreeValid = geometry.adaptiveTree.valid();
    geometry.cachedQuantity = params.selectedQuantity;
    geometry.cachedMaxTreeLevel = params.maxTreeLevel;
    geometry.cachedMinParticlesPerLeaf =
      static_cast<int>(params.minParticles);
    geometry.cachedCornerReconstructionMode =
      params.cornerReconstructionMode;
    treeMessage = std::move(tree.message);
  } else if (applyThresholdRequested) {
    treeMessage = "Adaptive tree cache reused.";
  }

  IsoContourBuildResult built =
    ExtractAdaptiveIsoContourMesh(geometry.adaptiveTree, params.isoLevel);
  geometry.verts = std::move(built.mesh.vertices);
  geometry.inds = std::move(built.mesh.indices);
  geometry.message = treeMessage.empty()
    ? std::move(built.message)
    : treeMessage + "; " + built.message;

  isoContourRenderState.show = true;
  isoContourRenderState.cpuUpdated = true;
}
#endif
