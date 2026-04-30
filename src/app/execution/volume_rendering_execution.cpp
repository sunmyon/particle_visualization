#include "app/execution/analysis_execution.h"

#ifdef VOLUME_RENDERING

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "app/state/analysis_state.h"
#include "app/state/render_runtime_state.h"
#include "app/state/runtime_state.h"
#include "data/particle_array.h"
#include "volume/adaptive_volume_tree.h"

namespace {
struct VolumeValueRange {
  float min = 0.0f;
  float max = 1.0f;
  bool valid = false;
};

VolumeValueRange ComputeVolumeValueRange(const ParticleBlock& block,
                                         QuantityId quantity,
                                         bool positiveOnly)
{
  VolumeValueRange range;
  range.min = std::numeric_limits<float>::max();
  range.max = -std::numeric_limits<float>::max();

  for (std::size_t i = 0; i < block.particles.size(); ++i) {
    const ParticleData& p = block.particles[i];
    const float value = getScalarValue(block, p, static_cast<int>(i), quantity);
    if (!std::isfinite(value)) continue;
    if (positiveOnly && value <= 0.0f) continue;

    range.min = std::min(range.min, value);
    range.max = std::max(range.max, value);
    range.valid = true;
  }

  if (!range.valid) {
    range.min = positiveOnly ? 1.0e-6f : 0.0f;
    range.max = 1.0f;
    return range;
  }

  if (range.max <= range.min) {
    range.max = range.min + std::max(std::abs(range.min) * 1.0e-6f, 1.0e-6f);
  }

  return range;
}

}

void ExecuteVolumeRenderingRequest(ParticleArray& particles,
                                   VolumeRenderingRequestState& request,
                                   VolumeRenderingResultState& result,
                                   VolumeRenderState& volumeRenderState)
{
  if (request.clearRequested) {
    result.tree.clear();
    result.stats = AdaptiveVolumeTreeStats{};
    result.valid = false;
    result.success = true;
    result.message = "Volume tree cleared.";
    result.cpuUpdated = true;
    volumeRenderState.show = false;
    volumeRenderState.cpuUpdated = true;
    request.clearRequested = false;
    return;
  }

  if (!request.buildRequested) {
    return;
  }

  request.buildRequested = false;

  if (particles.particleBlock.particles.empty()) {
    result.tree.clear();
    result.stats = AdaptiveVolumeTreeStats{};
    result.valid = false;
    result.success = false;
    result.message = "No particles are loaded.";
    result.cpuUpdated = true;
    volumeRenderState.show = false;
    volumeRenderState.cpuUpdated = true;
    return;
  }

  request.minParticlesPerLeaf = std::max(request.minParticlesPerLeaf, 1);
  request.maxTreeLevel = std::clamp(request.maxTreeLevel, 1, 24);

  if (request.autoRange) {
    const VolumeValueRange range =
      ComputeVolumeValueRange(particles.particleBlock,
                              request.selectedQuantity,
                              request.logScale);
    request.valueMin = range.min;
    request.valueMax = range.max;
  } else {
    const bool validRange = std::isfinite(request.valueMin) &&
                            std::isfinite(request.valueMax) &&
                            request.valueMax > request.valueMin;
    if (!validRange) {
      const VolumeValueRange range =
        ComputeVolumeValueRange(particles.particleBlock,
                                request.selectedQuantity,
                                request.logScale);
      request.valueMin = range.min;
      request.valueMax = range.max;
    }
  }

  AdaptiveVolumeTreeBuildParams params;
  params.quantity = request.selectedQuantity;
  params.minParticlesPerLeaf =
    static_cast<std::size_t>(request.minParticlesPerLeaf);
  params.maxDepth = static_cast<std::size_t>(request.maxTreeLevel);
  params.balanceTree = request.balanceTree;
  params.emptySigmaEpsilon = 0.0f;
  params.expandBoundsByHsml = true;

  AdaptiveVolumeTreeBuildResult built =
    BuildAdaptiveVolumeTreeFromParticles(particles.particleBlock,
                                         params,
                                         [](float value) {
                                           return std::isfinite(value)
                                             ? std::max(value, 0.0f)
                                             : 0.0f;
                                         });

  result.tree = std::move(built.tree);
  result.stats = built.stats;
  result.valid = result.tree.valid();
  result.success = result.valid;
  result.message = built.warning.empty()
    ? "Volume tree built with raw values."
    : built.warning;
  result.cpuUpdated = true;

  volumeRenderState.show = result.valid;
  volumeRenderState.cpuUpdated = true;
}

#endif
