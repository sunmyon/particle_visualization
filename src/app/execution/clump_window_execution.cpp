#include "app/execution/clump_window_execution.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "analysis/clump/clump_chain.h"
#include "app/state/clump_window_state.h"
#include "analysis/clump/find_clumps.h"
#include "app/state/loaded_clump_tool.h"
#include "app/app_snapshot_load.h"
#include "app/state/normalization_config.h"
#include "app/state/runtime_state.h"
#include "app/state/tracking_view_state.h"
#include "core/quantity.h"
#include "data/clump_loader.h"
#include "data/clump_store.h"
#include "data/simulation_dataset.h"
#include "data/simulation_element.h"
#include "image/image_io.h"
#include "image/rgb_image.h"
#include "interaction/camera.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_geometry.h"
#include "projection/projection_map_context.h"

static void RecenterCameraPreservingDistance(CameraContext& cam,
                                             const glm::vec3& newTarget);

static float WorldToRenderScaleOrOne(float worldToRenderScale)
{
  return worldToRenderScale > 0.0f ? worldToRenderScale : 1.0f;
}

static float RenderToWorldScaleOrOne(float worldToRenderScale)
{
  return worldToRenderScale > 0.0f ? 1.0f / worldToRenderScale : 1.0f;
}

static glm::vec3 WorldToRender(const glm::vec3& p, float worldToRenderScale)
{
  return p * WorldToRenderScaleOrOne(worldToRenderScale);
}

static glm::vec3 RenderToWorld(const glm::vec3& p, float worldToRenderScale)
{
  return p * RenderToWorldScaleOrOne(worldToRenderScale);
}

void ExecuteClumpFinderWindowRequests(ClumpFinderWindowState& ui,
                                             FindClump& clumpFind,
                                             SimulationDataset& simulationData,
                                             CameraContext& camera,
                                             double snapshotTime,
                                             const SnapshotInputState& input,
                                             const SnapshotCurrentState& current,
                                             const UnitSystem& units)
{
  static constexpr const char* kQuantities[] = {
    "Density", "Temperature", "Hsml"
  };
  static constexpr int kNumQuantities =
    static_cast<int>(sizeof(kQuantities) / sizeof(kQuantities[0]));

  int selected = ui.selectedVar;
  if (selected < 0 || selected >= kNumQuantities) {
    selected = 0;
  }
  const std::string var = kQuantities[selected];

  auto syncViewFromService = [&]() {
    ui.rows.clear();
    ui.rows.reserve(clumpFind.nodes().size());
    ui.showHull.assign(clumpFind.nodes().size(), false);

    QuantityUnitConverter converter;
    converter.rebuild(units, 1.0);
    const double massToMsun =
      converter.factor(QuantityId::Mass, UnitSpace::Physical);

    std::unordered_map<const StructureNode*, int> sourceIndexByNode;
    sourceIndexByNode.reserve(clumpFind.nodes().size());
    for (size_t i = 0; i < clumpFind.nodes().size(); ++i) {
      sourceIndexByNode[clumpFind.node(static_cast<int>(i))] =
        static_cast<int>(i);
    }

    for (size_t i = 0; i < clumpFind.nodes().size(); ++i) {
      const StructureNode* node = clumpFind.node(static_cast<int>(i));
      if (!node) {
        continue;
      }

      ClumpFinderRowView row;
      row.sourceIndex = static_cast<int>(i);
      row.count = node->count;
      row.mass = node->totalMass * massToMsun;
      const glm::vec3 originalPos =
        RenderToWorld(glm::vec3(static_cast<float>(node->pos_cm[0]),
                                       static_cast<float>(node->pos_cm[1]),
                                       static_cast<float>(node->pos_cm[2])),
                             simulationData.simulationBlock.worldToRenderScale);
      row.pos[0] = originalPos.x;
      row.pos[1] = originalPos.y;
      row.pos[2] = originalPos.z;
      row.vpeak = static_cast<float>(node->vpeak);
      row.isLeaf = node->isLeaf();
      row.isTrunk = node->parent == nullptr;
      row.childCount = static_cast<int>(node->children.size());
      row.depth = 0;
      for (const StructureNode* p = node->parent; p != nullptr; p = p->parent) {
        ++row.depth;
      }
      if (node->parent) {
        auto it = sourceIndexByNode.find(node->parent);
        if (it != sourceIndexByNode.end()) {
          row.parentSourceIndex = it->second;
        }
      }
      ui.rows.push_back(row);

#ifdef USE_CONVEX_HULL
      ui.showHull[i] = clumpFind.showHull(static_cast<int>(i));
#endif
    }

    ui.clumpsComputed = clumpFind.computed();
    ui.massHistogramValues = clumpFind.massHistogramValues();
    ui.histogramComputed = clumpFind.histogramComputed();
  };

  if (ui.requestRunFOF) {
    auto& p = clumpFind.params();
    p.densityThreshold = ui.densityThreshold;
    p.minParticles = ui.minParticles;
    p.minDensityContrastRatio = ui.minDensityContrastRatio;
    p.useHsml = ui.useHsml;
    p.linkingLength = ui.linkingLength;
    p.linkingLength_over_cell_size = ui.linkingLengthOverCellSize;
    clumpFind.runFOF(simulationData.simulationBlock, var);
    ui.requestRunFOF = false;
    syncViewFromService();
  }

  if (ui.requestRunDendrogram) {
    auto& p = clumpFind.params();
    p.densityThreshold = ui.densityThreshold;
    p.minParticles = ui.minParticles;
    p.minDensityContrastRatio = ui.minDensityContrastRatio;
    p.useHsml = ui.useHsml;
    p.linkingLength = ui.linkingLength;
    p.linkingLength_over_cell_size = ui.linkingLengthOverCellSize;
    clumpFind.runDendrogram(simulationData.simulationBlock, var);
    ui.requestRunDendrogram = false;
    syncViewFromService();
  }

  if (ui.requestSortByMass) {
    clumpFind.sortNodesByMass();
    ui.requestSortByMass = false;
    syncViewFromService();
  }

  if (ui.requestSortByHierarchy) {
    clumpFind.sortNodesByHierarchy();
    ui.requestSortByHierarchy = false;
    syncViewFromService();
  }

#ifdef CLUMP_DATA_READ
  if (ui.requestOutputHdf5) {
    char fullPath[512];
    std::snprintf(fullPath,
                  sizeof(fullPath),
                  "%s/%s",
                  input.folderPath,
                  ui.outputFileName);
    int snapshotIndex = current.loadedFileIndex;
    if (snapshotIndex < 0) {
      snapshotIndex = 0;
    }
    clumpFind.writeFOFtoHDF5(simulationData.simulationBlock,
                             snapshotTime,
                             fullPath,
                             snapshotIndex);
    ui.requestOutputHdf5 = false;
  }
#endif

  if (ui.requestComputeHistogram) {
    float massMin = 0.0f;
    float massMax = 1.0f;
    clumpFind.buildMassHistogram(ui.histogramLogScaleX, massMin, massMax);
    if (ui.histogramAutoRange && clumpFind.histogramComputed()) {
      ui.histogramRangeMin = massMin;
      ui.histogramRangeMax = massMax;
    }
    ui.requestComputeHistogram = false;
    syncViewFromService();
    if (ui.histogramComputed) {
      ++ui.histogramVersion;
    }
  }

  if (ui.requestFocusRow) {
    if (ui.focusRowIndex >= 0 &&
        ui.focusRowIndex < static_cast<int>(ui.rows.size())) {
      const auto& row = ui.rows[static_cast<size_t>(ui.focusRowIndex)];
      RecenterCameraPreservingDistance(camera,
                                       WorldToRender(glm::vec3(row.pos[0],
                                                                      row.pos[1],
                                                                      row.pos[2]),
                                                            simulationData.simulationBlock.worldToRenderScale));
    }
    ui.requestFocusRow = false;
    ui.focusRowIndex = -1;
  }

#ifdef USE_CONVEX_HULL
  if (ui.requestApplyHullSelection) {
    for (size_t i = 0; i < ui.showHull.size(); ++i) {
      clumpFind.setShowHull(static_cast<int>(i), ui.showHull[i]);
    }
    clumpFind.applyHullSelectionToStressFlags(
      simulationData.flag_stress,
      simulationData.simulationBlock.particles.size());
    simulationData.particlesDirty = true;
    ui.requestApplyHullSelection = false;
    syncViewFromService();
  }
#endif

  if (ui.rows.empty() && clumpFind.computed()) {
    syncViewFromService();
  }

  if (!clumpFind.computed() && (ui.clumpsComputed || !ui.rows.empty())) {
    ui.clumpsComputed = false;
    ui.rows.clear();
    ui.showHull.clear();
    ui.massHistogramValues.clear();
    ui.histogramComputed = false;
  }
}

void ExecuteLoadedClumpWindowRequests(LoadedClumpWindowState& ui,
                                             LoadedClumpTool& clumpTool,
                                             ClumpStore& clumpStore,
                                             TrackingTargetState& tracking,
                                             CameraContext& camera,
                                             int currentFileIndex,
                                             const SnapshotInputState& input,
                                             const NormalizationContext& normalization)
{
  auto syncRowsFromStore = [&]() {
    ui.rows.clear();
    ui.rows.reserve(clumpStore.size());
    for (size_t i = 0; i < clumpStore.size(); ++i) {
      const auto& cp = clumpStore.clump(i);
      LoadedClumpRowView row;
      row.sourceIndex = static_cast<int>(i);
      row.clumpID = cp.clumpID;
      row.count = cp.count;
      row.mass = static_cast<float>(cp.mass);
      row.density = static_cast<float>(cp.density);
      row.pos[0] = cp.originalPos[0];
      row.pos[1] = cp.originalPos[1];
      row.pos[2] = cp.originalPos[2];
      row.stellarCount = cp.stellar_count;
      row.stellarMass = static_cast<float>(cp.stellar_mass);
      row.stellarID = cp.stellar_id;
      ui.rows.push_back(row);
    }
  };

  if (ui.requestUseInputPath) {
    std::strncpy(ui.clumpListPath, input.folderPath, sizeof(ui.clumpListPath));
    ui.clumpListPath[sizeof(ui.clumpListPath) - 1] = '\0';
    ui.requestUseInputPath = false;
  }

  if (tracking.renewAfterSnapshot) {
    if (tracking.followClump) {
      ui.selectedClumpID = clumpStore.findIndexByClumpID(tracking.targetClumpID);
    }
    tracking.renewAfterSnapshot = false;
  }

  if (ui.requestReload) {
    clumpStore.setFilePath(ui.clumpListPath);
#ifdef CLUMP_DATA_READ
    auto clumps = loadClumpData(clumpStore.filePath().c_str(),
                                currentFileIndex,
                                normalization.toNormalizedScale());

    if (!clumps.empty()) {
      clumpStore.setClumps(std::move(clumps));
      ui.showEvolve.resize(clumpStore.size(), false);
      syncRowsFromStore();
    }
#else
    (void)currentFileIndex;
    (void)normalization;
    clumpStore.clear();
    ui.rows.clear();
    ui.showEvolve.clear();
#endif

    clumpTool.clearEvolutionCache();
    ui.evolutionCache.clear();
    ui.showEvolutionPlot = false;
    ui.requestReload = false;
  }

  if (ui.requestFollowSelected) {
    if (ui.selectedClumpID >= 0 &&
        ui.selectedClumpID < static_cast<int>(clumpStore.size())) {
      tracking.targetClumpID = clumpStore.clump(ui.selectedClumpID).clumpID;
      tracking.followClump = true;
      tracking.followParticle = false;
    }
    ui.requestFollowSelected = false;
  }

  if (ui.requestFocusSelected) {
    if (ui.focusClumpIndex >= 0 &&
        ui.focusClumpIndex < static_cast<int>(ui.rows.size())) {
      const auto& row = ui.rows[static_cast<size_t>(ui.focusClumpIndex)];
      const float scale = normalization.toNormalizedScale();
      RecenterCameraPreservingDistance(camera,
                                       glm::vec3(row.pos[0] * scale,
                                                 row.pos[1] * scale,
                                                 row.pos[2] * scale));
    }
    ui.requestFocusSelected = false;
    ui.focusClumpIndex = -1;
  }

  if (ui.requestUpdateEvolutionCache) {
    clumpTool.requestEvolutionPlotUpdate();
    ui.requestUpdateEvolutionCache = false;
  }

  if (ui.rows.empty() && clumpStore.loaded()) {
    syncRowsFromStore();
  }

  if (!clumpTool.needCacheUpdate()) {
    return;
  }

  float tMin = 0.0f, tMax = 1.0f;
  float valMin = 0.0f, valMax = 1.0f;
  clumpTool.rebuildEvolutionCache(ui, clumpStore, currentFileIndex,
                                  tMin, tMax, valMin, valMax);

  if (ui.autoRangeX) {
    ui.tMinInput = tMin;
    ui.tMaxInput = tMax;
  }
  if (ui.autoRangeY) {
    ui.valMinInput = valMin;
    ui.valMaxInput = valMax;
  }

  ui.evolutionCache.clear();
  ui.evolutionCache.reserve(clumpTool.evolutionCache().size());
  for (const auto& cache : clumpTool.evolutionCache()) {
    LoadedClumpEvolutionCacheView item;
    item.index = cache.index;
    if (cache.index >= 0 && cache.index < static_cast<int>(clumpStore.size())) {
      item.clumpID = clumpStore.clump(cache.index).clumpID;
    }
    item.timeFloats = cache.timeFloats;
    item.valueFloats = cache.valueFloats;
    ui.evolutionCache.push_back(std::move(item));
  }
  ++ui.evolutionVersion;
}

static void RecenterCameraPreservingDistance(CameraContext& cam,
                                             const glm::vec3& newTarget)
{
  const float dist = glm::length(cam.cameraPos - cam.cameraTarget);
  const glm::vec3 direction = cam.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
  cam.cameraTarget = newTarget;
  cam.cameraPos = cam.cameraTarget - direction * dist;
}

void ExecuteClumpChainWindowRequests(ClumpChainWindowState& ui,
                                            ClumpChain& chain,
                                            SimulationDataset& particles,
                                            const UnitSystem& units,
                                            ProjectionMapGenerator& projectionMap,
                                            const ProjectionMapParams& baseParams,
                                            const SnapshotCurrentState& current,
                                            SnapshotLoadRuntimeState& snapshotLoad,
                                            CameraContext& cam,
                                            NormalizationContext& normalization)
{
  auto syncViewFromService = [&]() {
    ui.series.clear();
    ui.computed = chain.computed();
    if (!ui.computed) {
      return;
    }

    const auto& props = chain.props();
    const auto& chains = chain.chains();
    ui.series.reserve(props.size());
    for (size_t i = 0; i < props.size() && i < chains.size(); ++i) {
      const auto& p = props[i];
      ClumpChainSeriesView s;
      s.firstSnapshot = p.first_snapshot;
      s.lastSnapshot = p.last_snapshot;
      s.globalId = p.global_id;
      s.stellarId = p.stellar_id;
      s.nstar = p.nstar;
      s.mstar = p.mstar;
      s.mstarMaximum = p.mstar_maximum;
      s.massMaximum = p.mass_maximum;
      s.temperature_d = p.temperature_d;

      const auto& srcChain = chains[i];
      s.snapshots.reserve(srcChain.size());
      for (const auto* snap : srcChain) {
        if (!snap) continue;
        ClumpChainSnapshotView v;
        v.time = snap->time;
        v.pos[0] = snap->pos[0];
        v.pos[1] = snap->pos[1];
        v.pos[2] = snap->pos[2];
        v.density = snap->density;
        v.temperature = snap->temperature;
        v.mass = snap->mass;
        v.stellarMass = snap->stellar_mass;
        s.snapshots.push_back(v);
      }
      ui.series.push_back(std::move(s));
    }
  };

  if (ui.requestBuild) {
    chain.build(ui.clumpChainInitFileIndex,
                ui.clumpChainNsnapshots,
                ui.clumpChainDFileIndex,
                ui.clumpChainFileName,
                units,
                current.loadedScaleFactor);
    ui.requestBuild = false;
    syncViewFromService();
    if (ui.computed) {
      ++ui.evolutionVersion;
    }
  } else if (ui.series.empty() && chain.computed()) {
    syncViewFromService();
    if (ui.computed) {
      ++ui.evolutionVersion;
    }
  }

  if (ui.navigationLoadPending &&
      IsSnapshotLoadedFor(snapshotLoad,
                          SnapshotLoadOwner::UserNavigation,
                          ui.navigationPendingStep)) {
    RecenterCameraPreservingDistance(cam,
                                     glm::vec3(ui.navigationPendingCenter[0],
                                               ui.navigationPendingCenter[1],
                                               ui.navigationPendingCenter[2]));
    ui.navigationLoadPending = false;
  }

  if (ui.requestFixedView) {
    cam.cameraPos = cam.cameraTarget + glm::vec3(0.0f, 0.0f, -1.0f);

    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 forward = glm::normalize(cam.cameraTarget - cam.cameraPos);
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    glm::mat4 viewMatrix = glm::lookAt(cam.cameraPos, cam.cameraTarget, up);
    glm::mat3 rotationMatrix = glm::mat3(viewMatrix);
    cam.distance = glm::length(cam.cameraPos - cam.cameraTarget);
    cam.cameraOrientation = glm::quat_cast(rotationMatrix);

    ui.requestFixedView = false;
  }

  if (ui.selectedChainIndex >= 0 &&
      ui.selectedChainIndex < static_cast<int>(ui.series.size())) {
    const auto& selectedSeries = ui.series[static_cast<size_t>(ui.selectedChainIndex)];

    bool triggerLoad = false;
    if (ui.requestLoadSelected) {
      ui.currentSnapshotIndex = 0;
      ui.flagFileLoaded = true;
      triggerLoad = true;
    }

    if (ui.requestPrev) {
      if (ui.flagFileLoaded && ui.currentSnapshotIndex > 0) {
        ui.currentSnapshotIndex--;
        triggerLoad = true;
      }
    }

    if (ui.requestNext) {
      if (ui.flagFileLoaded &&
          ui.currentSnapshotIndex < static_cast<int>(selectedSeries.snapshots.size()) - 1) {
        ui.currentSnapshotIndex++;
        triggerLoad = true;
      }
    }

    if (triggerLoad && !selectedSeries.snapshots.empty()) {
      const int targetStep = selectedSeries.firstSnapshot + ui.currentSnapshotIndex;
      const float scaleFromPhys = normalization.toNormalizedScale();
      const auto& snap = selectedSeries.snapshots[static_cast<size_t>(ui.currentSnapshotIndex)];

      ui.navigationPendingCenter[0] = snap.pos[0] * scaleFromPhys;
      ui.navigationPendingCenter[1] = snap.pos[1] * scaleFromPhys;
      ui.navigationPendingCenter[2] = snap.pos[2] * scaleFromPhys;
      ui.navigationPendingStep = targetStep;
      ui.navigationLoadPending = true;

      RequestSnapshotLoad(snapshotLoad,
                          SnapshotLoadOwner::UserNavigation,
                          targetStep,
                          100);
    }
  }

  ui.requestLoadSelected = false;
  ui.requestPrev = false;
  ui.requestNext = false;

  if (ui.requestMakeProjectionMaps && !ui.projectionBatchRunning) {
    if (ui.selectedChainIndex >= 0 &&
        ui.selectedChainIndex < static_cast<int>(ui.series.size())) {
      ui.projectionBatchRunning = true;
      ui.projectionBatchCursor = 0;
      ui.projectionBatchChainIndex = ui.selectedChainIndex;
    }
  }
  ui.requestMakeProjectionMaps = false;

  if (!ui.projectionBatchRunning) {
    return;
  }

  if (ui.projectionBatchChainIndex < 0 ||
      ui.projectionBatchChainIndex >= static_cast<int>(ui.series.size())) {
    ui.projectionBatchRunning = false;
    return;
  }

  const auto& selectedSeries = ui.series[static_cast<size_t>(ui.projectionBatchChainIndex)];
  const auto& clumpSnapshots = selectedSeries.snapshots;
  if (ui.projectionBatchCursor >= static_cast<int>(clumpSnapshots.size())) {
    ui.projectionBatchRunning = false;
    return;
  }

  const int targetStep = selectedSeries.firstSnapshot + ui.projectionBatchCursor;
  if (!IsSnapshotLoadedFor(snapshotLoad,
                           SnapshotLoadOwner::ClumpChainProjectionBatch,
                           targetStep)) {
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::ClumpChainProjectionBatch,
                        targetStep,
                        20);
    return;
  }

  const QuantityId projectionVars[] = {
    QuantityId::Density,
    QuantityId::Temperature,
    QuantityId::Val,
    QuantityId::Val2,
    QuantityId::Hsml,
    QuantityId::Mass
  };
  const int nProjectionVars =
    static_cast<int>(sizeof(projectionVars) / sizeof(projectionVars[0]));
  int qIndex = ui.selectedProjectionVar;
  if (qIndex < 0 || qIndex >= nProjectionVars) {
    qIndex = 0;
  }

  const bool useAngularMomentumAxis = (ui.projectionBatchCursor == 0);

  float pos_center[3];
  const float scale_from_phys = normalization.toNormalizedScale();
  const auto& snap = clumpSnapshots[static_cast<size_t>(ui.projectionBatchCursor)];
  pos_center[0] = snap.pos[0] * scale_from_phys;
  pos_center[1] = snap.pos[1] * scale_from_phys;
  pos_center[2] = snap.pos[2] * scale_from_phys;

  char fname_output[512];
  std::snprintf(fname_output, sizeof(fname_output),
                "%s/image_clump%d_%04d.png",
                ui.mapOutputDir,
                ui.projectionBatchChainIndex,
                ui.projectionBatchCursor);

  ProjectionMapParams frameParams = baseParams;
  frameParams.dataSource = DataSource::Gas;
  frameParams.selectedType = 0;
  frameParams.selectedVarGas = projectionVars[qIndex];
  frameParams.var = QuantityLabel(frameParams.selectedVarGas);
  frameParams.xoffset[0] = pos_center[0];
  frameParams.xoffset[1] = pos_center[1];
  frameParams.xoffset[2] = pos_center[2];
  frameParams.xlen[0] = ui.mapLen * scale_from_phys;
  frameParams.xlen[1] = ui.mapLen * scale_from_phys;
  frameParams.xlen[2] = ui.mapLen * scale_from_phys;
  frameParams.range_min = ui.mapValMin;
  frameParams.range_max = ui.mapValMax;
  frameParams.autoRange = false;
  frameParams.npixel = ui.mapNpixel;
  frameParams.step_z = ui.mapNslices;
  frameParams.flagVoronoi = (ui.mapNslices > 1);

  ProjectionMapContext context =
    BuildProjectionMapContext(frameParams,
                              normalization.toPhysicalScale(),
                              current.loadedTime);

  if (useAngularMomentumAxis) {
    auto frame = ComputeAngularMomentumFrame(particles.simulationBlock.particles,
                                             particles.simulationBlock.worldToRenderScale,
                                             context.center,
                                             frameParams.xlen);
    if (frame.valid) {
      context.center = frame.center;
      context.planeNormal = frame.axis;
      context.cuboidTransform = BuildRotationFromZAxisTo(frame.axis);
    }
  }

  RgbImage image = projectionMap.makeDensityMapImage(particles,
                                                     units,
                                                     frameParams,
                                                     context);
  if (!WritePngRgb(fname_output, image.width, image.height, image.rgb)) {
    std::fprintf(stderr, "Failed to write projection map: %s\n", fname_output);
  }

  ++ui.projectionBatchCursor;
  if (ui.projectionBatchCursor >= static_cast<int>(clumpSnapshots.size())) {
    ui.projectionBatchRunning = false;
  } else {
    const int nextStep = selectedSeries.firstSnapshot + ui.projectionBatchCursor;
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::ClumpChainProjectionBatch,
                        nextStep,
                        20);
  }
}
