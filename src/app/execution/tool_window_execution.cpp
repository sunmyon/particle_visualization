#include "app/execution/tool_window_execution.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <string>
#include <unordered_set>

#include <glm/glm.hpp>

#include "app/state/analysis_state.h"
#include "app/state/normalization_config.h"
#include "app/state/runtime_state.h"
#include "app/state/tracking_view_state.h"
#include "app/state/tool_window_state.h"
#include "app/execution/profile_histogram_execution.h"
#include "analysis/histogram2d.h"
#include "analysis/radial_profile.h"
#include "data/particle_array.h"
#include "data/particle_block.h"
#include "data/particle_data.h"
#include "data/halo_store.h"
#include "interaction/camera.h"
#include "projection/make_2D_projection_map.h"

namespace {

ParticleData* FindParticleById(ParticleArray& particles, int particleId)
{
  const size_t index =
    particles.particleBlock.findIndexByID(static_cast<uint64_t>(static_cast<int64_t>(particleId)));
  if (index == static_cast<size_t>(-1)) {
    return nullptr;
  }
  return &particles.particleBlock.particles[index];
}

} // namespace

void ExecuteTopParticlesWindowRequests(TopParticlesUIState& ui,
                                       TopParticlesRequestState& req,
                                       TopParticlesResultState& result,
                                       ParticleArray& particles,
                                       CameraContext& camera,
                                       TrackingTargetState& tracking,
                                       SnapshotPostprocessState& post,
                                       const QuantityState& quantity)
{
  static constexpr int kHistorySizeMax = 10;
  static constexpr const char* kQuantities[] = {
    "x", "y", "z", "r", "Density", "Temperature", "Hsml", "Mass"
  };

  if (post.refreshTopParticles) {
    req.refreshHistoryRequested = true;
    req.refreshFilteredRequested = true;
    post.refreshTopParticles = false;
  }

  auto centerCameraOnParticle = [&](int particleId) {
    const ParticleData* p = FindParticleById(particles, particleId);
    if (!p) return;

    const float distance = glm::length(camera.cameraPos - camera.cameraTarget);
    const glm::vec3 direction =
      camera.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
    camera.cameraTarget = glm::vec3(p->pos[0], p->pos[1], p->pos[2]);
    camera.cameraPos = camera.cameraTarget - direction * distance;
  };

  if (req.queryParticleRequested) {
    result.hasFound = false;
    result.queryFailed = false;

    if (const ParticleData* p = FindParticleById(particles, req.queryParticleId)) {
      result.foundParticle = *p;
      result.hasFound = true;
      result.historyData.push_front(*p);
      if (static_cast<int>(result.historyData.size()) > kHistorySizeMax) {
        result.historyData.pop_back();
      }
      ui.historySel = 0;
    } else {
      result.queryFailed = true;
    }

    req.queryParticleRequested = false;
  }

  if (req.refreshHistoryRequested) {
    const int prevID =
      (ui.historySel >= 0 &&
       ui.historySel < static_cast<int>(result.historyData.size()))
        ? result.historyData[ui.historySel].ID
        : -1;

    std::deque<ParticleData> newHistory;
    std::unordered_set<int> seen;

    for (const auto& oldP : result.historyData) {
      if (seen.find(oldP.ID) != seen.end()) continue;

      if (const ParticleData* p = FindParticleById(particles, oldP.ID)) {
        newHistory.push_back(*p);
        seen.insert(oldP.ID);
      }
    }

    result.historyData.swap(newHistory);
    ui.historySel = -1;
    if (prevID >= 0) {
      for (int i = 0; i < static_cast<int>(result.historyData.size()); ++i) {
        if (result.historyData[i].ID == prevID) {
          ui.historySel = i;
          break;
        }
      }
    }
    if (ui.historySel == -1 && !result.historyData.empty()) {
      ui.historySel = 0;
    }

    req.refreshHistoryRequested = false;
  }

  if (req.clearHistoryRequested) {
    result.historyData.clear();
    result.hasFound = false;
    result.queryFailed = false;
    ui.historySel = -1;
    req.clearHistoryRequested = false;
  }

  if (req.refreshFilteredRequested) {
    result.filtered.clear();
    for (size_t i = 0; i < particles.particleBlock.particles.size(); ++i) {
      const ParticleData& p = particles.particleBlock.particles[i];
      if (i < particles.flag_mask.size() && particles.flag_mask[i] != 0) {
        continue;
      }
      if (p.type >= 0 && p.type < 6 && req.selectedTypes[p.type]) {
        result.filtered.push_back(p);
      }
    }

    std::sort(result.filtered.begin(), result.filtered.end(),
              [](const ParticleData& a, const ParticleData& b) {
                return a.mass > b.mass;
              });

    req.refreshFilteredRequested = false;
  }

  if (req.centerParticleRequested) {
    centerCameraOnParticle(req.centerParticleId);
    req.centerParticleRequested = false;
    req.centerParticleId = -1;
  }

  if (req.followParticleRequested) {
    if (const ParticleData* p = FindParticleById(particles, req.centerParticleId)) {
      centerCameraOnParticle(p->ID);
      tracking.followParticle = true;
      tracking.targetParticleID = p->ID;
      tracking.followClump = false;
    }
    req.followParticleRequested = false;
    req.centerParticleId = -1;
  }

  if (req.disableFollowParticleRequested) {
    tracking.followParticle = false;
    req.disableFollowParticleRequested = false;
  }

  if (!req.computeHistogramRequested) {
    return;
  }

  const int quantityCount = static_cast<int>(sizeof(kQuantities) / sizeof(kQuantities[0]));
  int selectedVar = req.histogramSelectedVar;
  if (selectedVar < 0 || selectedVar >= quantityCount) {
    selectedVar = 0;
  }
  const std::string var = kQuantities[selectedVar];
  const int bins = std::max(1, req.histogramBins);

  std::function<bool(const ParticleData&)> includeParticle =
    [](const ParticleData&) { return true; };

  if (req.histogramUseCameraCenter) {
    const glm::vec3 camCenter = camera.cameraTarget;
    const float radius = req.histogramCameraRadius;
    includeParticle = [camCenter, radius](const ParticleData& p) {
      glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
      return glm::length(pos - camCenter) <= radius;
    };
  }

  auto particleValue = [&](const ParticleData& p) {
    float value = p.getValue(var);
    if (var == "Mass") {
      value = static_cast<float>(quantity.toDisplay(QuantityId::Mass, value));
    }
    return value;
  };

  float valueMin = std::numeric_limits<float>::max();
  float valueMax = std::numeric_limits<float>::lowest();
  bool hasValue = false;

  for (const auto& p : result.filtered) {
    if (!includeParticle(p)) continue;

    float value = particleValue(p);
    if (value == 0.0f) continue;
    if (req.histogramLogScaleX) {
      if (value <= 0.0f) continue;
      value = std::log10(value);
    }

    valueMin = std::min(valueMin, value);
    valueMax = std::max(valueMax, value);
    hasValue = true;
  }

  if (!hasValue) {
    result.histogramComputed = false;
    result.histBins.clear();
    result.binCenters.clear();
    req.computeHistogramRequested = false;
    return;
  }

  if (valueMin == valueMax) {
    valueMax = valueMin + 1.0f;
  }

  if (req.histogramAutoRange) {
    req.histogramRange1Min = valueMin;
    req.histogramRange1Max = valueMax;
  }

  TrackingVector<int> binCounts(bins, 0);
  result.binSize = (req.histogramRange1Max - req.histogramRange1Min) / bins;
  if (result.binSize <= 0.0f) {
    result.binSize = 1.0f;
  }

  for (const auto& p : result.filtered) {
    if (!includeParticle(p)) continue;

    float value = particleValue(p);
    if (value == 0.0f) continue;
    if (req.histogramLogScaleX) {
      if (value <= 0.0f) continue;
      value = std::log10(value);
    }

    int bin = static_cast<int>((value - req.histogramRange1Min) / result.binSize);
    if (bin < 0) bin = 0;
    if (bin >= bins) bin = bins - 1;
    binCounts[bin]++;
  }

  result.histBins.resize(bins);
  result.binCenters.resize(bins);
  result.vmin = std::numeric_limits<float>::max();
  result.vmax = std::numeric_limits<float>::lowest();

  for (int i = 0; i < bins; i++) {
    result.histBins[i] = static_cast<float>(binCounts[i]);

    float value = result.histBins[i];
    if (req.histogramLogScaleY) {
      if (value == 0.0f) continue;
      value = std::log10(value);
    }

    result.vmin = std::min(result.vmin, value);
    result.vmax = std::max(result.vmax, value);
  }

  if (result.vmin == std::numeric_limits<float>::max()) {
    result.vmin = 0.0f;
    result.vmax = 1.0f;
  } else if (req.histogramLogScaleY) {
    result.vmin = std::floor(result.vmin);
    result.vmax = std::ceil(result.vmax);
    result.vmin = 0.8f * std::pow(10.0f, result.vmin);
    result.vmax = std::pow(10.0f, result.vmax);
  } else {
    result.vmin = 0.0f;
    int digits = (result.vmax > 0.0f) ? static_cast<int>(std::log10(result.vmax)) : 0;
    double scale = std::pow(10.0, digits);
    result.vmax = std::ceil(result.vmax / scale) * scale;
  }

  if (req.histogramAutoRange) {
    req.histogramRange2Min = result.vmin;
    req.histogramRange2Max = result.vmax;
  }

  ui.range1_min = req.histogramRange1Min;
  ui.range1_max = req.histogramRange1Max;
  ui.range2_min = req.histogramRange2Min;
  ui.range2_max = req.histogramRange2Max;

  for (int i = 0; i < bins; i++) {
    result.binCenters[i] = req.histogramRange1Min + (i + 0.5f) * result.binSize;
  }

  result.histogramComputed = true;
  ++result.histogramVersion;
  req.computeHistogramRequested = false;
}

void ExecuteRadialProfileWindowRequests(RadialProfileUIState& ui,
                                        RadialProfileRequestState& request,
                                        RadialProfileResultState& result,
                                        const ParticleBlock& partblock,
                                        const glm::vec3& camCenter,
                                        NormalizationContext& normalization,
                                        QuantityState& quantity)
{
  if (!request.runRequested) {
    return;
  }

  ExecuteRadialProfileRequest(request,
                              result,
                              partblock,
                              camCenter,
                              normalization,
                              quantity);

  if (request.params.autorange && result.result.valid) {
    ui.draftParams = request.params;
  }
}

void ExecuteHistogram2DWindowRequests(Histogram2DUIState& ui,
                                      Histogram2DRequestState& request,
                                      Histogram2DResultState& result,
                                      const ParticleBlock& partblock,
                                      const Histogram2DContext& ctx)
{
  if (!request.runRequested) {
    return;
  }

  ExecuteHistogram2DRequest(request,
                            result,
                            partblock,
                            ctx);

  if (result.result.valid) {
    ui.draftParams = request.params;
  }
}

void ExecuteProjectionFontSelectionRequests(ProjectionMapUIState& ui,
                                            ProjectionFontSelectionRequestState& request,
                                            ProjectionMapGenerator& generator)
{
  if (ui.fontListRefreshRequested || ui.availableFontPaths.empty()) {
    ui.availableFontPaths.clear();
    const int count = generator.getFontCount();
    ui.availableFontPaths.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      ui.availableFontPaths.push_back(generator.getFontPath(i));
    }

    if (count <= 0) {
      ui.currentFontIndex = 0;
      ui.appliedFontIndex = -1;
    } else {
      if (ui.currentFontIndex < 0 || ui.currentFontIndex >= count) {
        ui.currentFontIndex = 0;
      }
      if (ui.appliedFontIndex >= count) {
        ui.appliedFontIndex = -1;
      }
    }

    ui.fontListRefreshRequested = false;
  }

  if (!request.applySelectedFontRequested) {
    return;
  }

  const int count = static_cast<int>(ui.availableFontPaths.size());
  if (count > 0 &&
      ui.currentFontIndex >= 0 &&
      ui.currentFontIndex < count &&
      generator.selectFontFileByIndex(ui.currentFontIndex)) {
    ui.appliedFontIndex = ui.currentFontIndex;
  }

  request.applySelectedFontRequested = false;
}

void ExecuteMaskWindowRequests(MaskUIState& ui,
                               MaskRequestState& request)
{
  if (!request.applyRequested) {
    return;
  }

  ui.revision++;
  request.applyRequested = false;
}

void ExecuteHaloesWindowRequests(HaloesUIState& ui,
                                 HaloesRequestState& request,
                                 HaloStore& haloes,
                                 ParticleArray& particles,
                                 CameraContext& camera,
                                 const NormalizationContext& normalization)
{
#ifdef HAVE_HDF5
  auto syncRowsFromStore = [&]() {
    ui.rows.clear();
    ui.rows.reserve(haloes.size());
    for (size_t i = 0; i < haloes.size(); ++i) {
      const HaloData& h = haloes.halo(i);
      HaloRowView row;
      row.sourceIndex = static_cast<int>(i);
      row.groupLen = h.GroupLen;
      row.groupMass = h.GroupMass;
      for (int k = 0; k < 6; ++k) {
        row.groupMassType[k] = h.GroupMassType[k];
      }
      for (int k = 0; k < 3; ++k) {
        row.groupPos[k] = h.GroupPos[k];
        row.groupVel[k] = h.GroupVel[k];
      }
      row.groupMetallicity[0] = h.GroupMetallicity[0];
      row.groupMetallicity[1] = h.GroupMetallicity[1];
      ui.rows.push_back(row);
    }
    ui.loaded = !haloes.empty();
    ui.idsLoaded = haloes.idsLoaded();
  };

  if (request.loadWithoutIdsRequested || request.loadWithIdsRequested) {
    const bool loadIds = request.loadWithIdsRequested;
    haloes.loadFromHDF5(request.filename, loadIds);
    ui.selectedForStress.assign(haloes.size(), 0);
    request.selectedForStress = ui.selectedForStress;
    ui.histogramComputed = false;
    syncRowsFromStore();
    request.loadWithoutIdsRequested = false;
    request.loadWithIdsRequested = false;
  }

  if (request.clearIdsRequested) {
    haloes.clearHaloIDs();
    syncRowsFromStore();
    request.clearIdsRequested = false;
  }

  if (request.resetSelectionRequested) {
    std::fill(ui.selectedForStress.begin(), ui.selectedForStress.end(), 0);
    request.selectedForStress = ui.selectedForStress;
    request.stressSelectionChanged = true;
    request.resetSelectionRequested = false;
  }

  if (request.recomputePositionsRequested) {
    haloes.recomputeHaloPositionsFromParticles(
      particles.particleBlock.particles,
      request.recomputeUseMassWeight,
      request.recomputeUseOriginalPos
    );
    syncRowsFromStore();
    request.recomputePositionsRequested = false;
  }

  if (request.stressSelectionChanged) {
    for (auto& p : particles.particleBlock.particles) {
      p.flag_stress = 0;
    }

    if (haloes.idsLoaded()) {
      const int n = static_cast<int>(std::min(haloes.size(), request.selectedForStress.size()));
      for (int i = 0; i < n; ++i) {
        if (!request.selectedForStress[i]) continue;
        particles.ApplyIDStress(haloes.ids(i));
      }
    }

    ui.selectedForStress = request.selectedForStress;
    particles.particlesDirty = true;
    request.stressSelectionChanged = false;
  }

  if (request.focusHaloRequested) {
    if (request.focusHaloIndex >= 0 &&
        request.focusHaloIndex < static_cast<int>(haloes.size())) {
      const HaloData& h = haloes.halo(request.focusHaloIndex);
      const float distance = glm::length(camera.cameraPos - camera.cameraTarget);
      const glm::vec3 direction =
        camera.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);

      const float scale = normalization.toPhysicalScale();
      camera.cameraTarget = glm::vec3(h.GroupPos[0] * scale,
                                      h.GroupPos[1] * scale,
                                      h.GroupPos[2] * scale);
      camera.cameraPos = camera.cameraTarget - direction * distance;
    }
    request.focusHaloRequested = false;
    request.focusHaloIndex = -1;
  }

  if (ui.loaded != !haloes.empty() ||
      ui.idsLoaded != haloes.idsLoaded() ||
      ui.rows.size() != haloes.size()) {
    syncRowsFromStore();
  }

  if (!request.computeHistogramRequested) {
    return;
  }

  static constexpr const char* kQuantities[] = {
    "Mass", "GasMass", "StellarMass", "GasMetallicity", "StellarMetallicity"
  };
  int selected = request.histogramSelectedVar;
  const int nQuantities = static_cast<int>(sizeof(kQuantities) / sizeof(kQuantities[0]));
  if (selected < 0 || selected >= nQuantities) {
    selected = 0;
  }
  const std::string var = kQuantities[selected];
  const int bins = std::max(1, request.histogramBins);

  float valueMin = std::numeric_limits<float>::max();
  float valueMax = std::numeric_limits<float>::lowest();
  bool hasValue = false;

  for (const auto& h : haloes.allHaloes()) {
    float value = h.getHaloValue(var);
    if (request.histogramLogScaleX) {
      if (value <= 0.0f) continue;
      value = std::log10(value);
    }

    valueMin = std::min(valueMin, value);
    valueMax = std::max(valueMax, value);
    hasValue = true;
  }

  if (!hasValue) {
    ui.histogramComputed = false;
    ui.histBins.clear();
    ui.binCenters.clear();
    request.computeHistogramRequested = false;
    return;
  }

  if (valueMin == valueMax) {
    valueMax = valueMin + 1.0f;
  }

  if (request.histogramAutoRange) {
    request.histogramRange1Min = valueMin;
    request.histogramRange1Max = valueMax;
  }

  TrackingVector<int> binCounts(bins, 0);
  ui.binSize = (request.histogramRange1Max - request.histogramRange1Min) / bins;
  if (ui.binSize <= 0.0f) {
    ui.binSize = 1.0f;
  }

  for (const auto& h : haloes.allHaloes()) {
    float value = h.getHaloValue(var);
    if (request.histogramLogScaleX) {
      if (value <= 0.0f) continue;
      value = std::log10(value);
    }

    int bin = static_cast<int>((value - request.histogramRange1Min) / ui.binSize);
    if (bin < 0) bin = 0;
    if (bin >= bins) bin = bins - 1;
    binCounts[bin]++;
  }

  ui.vmin = std::numeric_limits<float>::max();
  ui.vmax = std::numeric_limits<float>::lowest();
  ui.histBins.resize(bins);
  ui.binCenters.resize(bins);

  for (int i = 0; i < bins; i++) {
    ui.histBins[i] = static_cast<float>(binCounts[i]);

    float value = ui.histBins[i];
    if (request.histogramLogScaleY) {
      if (value == 0.0f) continue;
      value = std::log10(value);
    }

    ui.vmin = std::min(ui.vmin, value);
    ui.vmax = std::max(ui.vmax, value);
  }

  if (ui.vmin == std::numeric_limits<float>::max()) {
    ui.vmin = 0.0f;
    ui.vmax = 1.0f;
  } else if (request.histogramLogScaleY) {
    ui.vmin = std::floor(ui.vmin);
    ui.vmax = std::ceil(ui.vmax);
    ui.vmin = 0.8f * std::pow(10.0f, ui.vmin);
    ui.vmax = std::pow(10.0f, ui.vmax);
  } else {
    ui.vmin = 0.0f;
    int digits = (ui.vmax > 0.0f) ? static_cast<int>(std::log10(ui.vmax)) : 0;
    double scale = std::pow(10.0, digits);
    ui.vmax = std::ceil(ui.vmax / scale) * scale;
  }

  if (request.histogramAutoRange) {
    request.histogramRange2Min = ui.vmin;
    request.histogramRange2Max = ui.vmax;
  }

  ui.range1_min = request.histogramRange1Min;
  ui.range1_max = request.histogramRange1Max;
  ui.range2_min = request.histogramRange2Min;
  ui.range2_max = request.histogramRange2Max;

  for (int i = 0; i < bins; i++) {
    ui.binCenters[i] = request.histogramRange1Min + (i + 0.5f) * ui.binSize;
  }

  ui.histogramComputed = true;
  ++ui.histogramVersion;
  request.computeHistogramRequested = false;
#else
  (void)ui;
  (void)request;
  (void)haloes;
  (void)particles;
  (void)camera;
  (void)normalization;
#endif
}
