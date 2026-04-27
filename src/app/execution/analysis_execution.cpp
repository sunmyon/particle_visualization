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
#include "object.h"

#ifdef USE_CONVEX_HULL
#include "FindClumps/find_clumps.h"
#include "geometry/convex_hull_generator.h"
#include "app/state/convex_hull_state.h"
#endif

#ifdef GEOMETRICAL_ANALYSIS
#include "GeometricAnalysis/DiskRadius.hpp"
#include "GeometricAnalysis/ellipse_fitter.h"

void ExecuteSingleDiskAnalysisRequest(ParticleArray& particles,
				      NormalizationContext& normalization,
				      DiskRadiusFinder& diskFinder,
				      DiskAnalysisRequestState& request,
				      DiskAnalysisResultState& result,
				      const UnitSystem& units)
{
  if (request.clearRequested) {
    result = DiskAnalysisResultState{};
    request.clearRequested = false;
    return;
  }

  if (!request.runRequested) {
    return;
  }

  request.runRequested = false;
  result = DiskAnalysisResultState{};
  result.targetParticleId = request.targetParticleId;

  DiskRadiusFinder::Params param{};
  bool found = false;
  for (const auto& p : particles.particleBlock.particles) {
    if (p.ID != request.targetParticleId) {
      continue;
    }
    if (request.rejectTypeZeroTarget && p.type == 0) {
      result.valid = false;
      result.cpuUpdated = true;
      return;
    }

    param.mass = p.mass;
    for (int k = 0; k < 3; ++k) {
      param.center[k] = p.pos[k];
      param.v_center[k] = p.vel[k];
    }
    found = true;
    break;
  }

  if (!found) {
    result.valid = false;
    result.cpuUpdated = true;
    return;
  }

  param.G = units.grav_const_internal;
  param.max_shell = 100;
  param.scale_fac = normalization.toPhysicalScale();

  DiskObject disk;
  disk.color = glm::vec3(1.0f);
  disk.opacity = request.diskOpacity;
  disk.tag = request.diskTag;

  if (diskFinder.compute(particles.particleBlock.particles, param, disk)) {
    result.valid  = true;
    result.radius = disk.radius;
    result.disk   = std::move(disk);
  } else {
    result.valid = false;
  }

  result.cpuUpdated = true;
}

static bool LoadDiskBatchTargets(const char* inputFile,
	                                 std::vector<DiskAnalysisBatchTargetRow>& outRows)
{
  outRows.clear();
  std::ifstream fin(inputFile);
  if (!fin) {
    return false;
  }

  std::string line;
  DiskAnalysisBatchTargetRow row;
  while (std::getline(fin, line)) {
    if (line.empty() || line[0] == '#') continue;

    std::istringstream iss(line);
    if (iss >> row.idx >> row.idA >> row.idB >> row.snap) {
      outRows.push_back(row);
    } else {
      std::cerr << "parse error: " << line << '\n';
    }
  }
  return true;
}

static void ResetDiskBatchRowState(DiskAnalysisBatchRuntimeState& runtime)
{
  runtime.scanOffset = 0;
  runtime.firstEvolution = true;
  runtime.snapDisk = -1;
  runtime.snapNotDisk = -1;
  runtime.timeDisk = -1.0;
  runtime.timeNotDisk = -1.0;
  runtime.distDisk = 0.0;
  runtime.rDisk1 = 0.0;
  runtime.rDisk2 = 0.0;
  runtime.evolutionOutputFile[0] = '\0';
}

static bool AppendDiskBatchSummary(const DiskAnalysisBatchRequestState& params,
                                   DiskAnalysisBatchRuntimeState& runtime,
	                                   const DiskAnalysisBatchTargetRow& row)
{
  FILE* fp = std::fopen(params.outputFile, runtime.firstOutput ? "w" : "a");
  if (!fp) return false;

  if (runtime.firstOutput) {
    std::fprintf(fp, "#index idA idB t_disk snap_disk dist_disk r_disk1 r_disk2 t_not_disk snap_not_disk\n");
    runtime.firstOutput = false;
  }

  std::fprintf(fp, "%d %d %d %g %d %g %g %g %g %d\n",
	               row.idx, row.idA, row.idB,
	               runtime.timeDisk,
	               runtime.snapDisk,
	               runtime.distDisk,
	               runtime.rDisk1,
	               runtime.rDisk2,
	               runtime.timeNotDisk,
	               runtime.snapNotDisk);
  std::fclose(fp);
  return true;
}

static bool AppendDiskBatchEvolution(DiskAnalysisBatchRuntimeState& runtime,
	                                     int snapshotFileIndex,
                                     double time,
                                     double dist,
                                     double r1,
                                     double r2,
                                     bool isDisk)
{
  FILE* fp = std::fopen(runtime.evolutionOutputFile, runtime.firstEvolution ? "w" : "a");
  if (!fp) return false;

  if (runtime.firstEvolution) {
    runtime.firstEvolution = false;
    runtime.timeNotDisk = time;
    runtime.snapNotDisk = snapshotFileIndex;
  }

  std::fprintf(fp, "%d %g %g %g %g %d\n",
               snapshotFileIndex,
               time,
               dist,
               r1,
               r2,
               static_cast<int>(isDisk));
  std::fclose(fp);
  return true;
}

struct DiskBatchFrameSpec {
  const DiskAnalysisBatchTargetRow* row = nullptr;
  int snapInit = 0;
  int targetFileIndex = 0;
  int targetStep = 0;
};

static bool PrepareDiskBatchExecution(FileNavigationRuntimeState& fileNav,
                                      SnapshotLoadRuntimeState& snapshotLoad,
                                      DiskAnalysisBatchRequestState& request,
                                      DiskAnalysisBatchRuntimeState& runtime,
                                      DiskAnalysisBatchResultState& result,
                                      DiskBatchFrameSpec& spec)
{
  auto& nav = fileNav.navigation;
  auto& job = runtime.job;

  if (request.cancelRequested) {
    job.cancelRequested = true;
    request.cancelRequested = false;
  }

  if (request.runRequested && job.status != JobStatus::Running) {
    runtime.params = request;
    runtime.params.runRequested = false;
    runtime.params.cancelRequested = false;
    request.runRequested = false;
    result = DiskAnalysisBatchResultState{};
    result.running = true;
    result.lastInputFile = runtime.params.inputFile;
    result.lastOutputFile = runtime.params.outputFile;

    if (nav.skipStep <= 0) {
      std::cerr << "invalid skipStep for disk batch: " << nav.skipStep << '\n';
      result.running = false;
      result.completed = true;
      result.success = false;
      return false;
    }

    runtime.rows.clear();
    if (!LoadDiskBatchTargets(runtime.params.inputFile, runtime.rows)) {
      std::cerr << "cannot open " << runtime.params.inputFile << '\n';
      result.running = false;
      result.completed = true;
      result.success = false;
      return false;
    }

    job = SnapshotJobRuntimeState{};
    job.status = JobStatus::Running;
    job.savedCurrentStep = nav.currentStep;
    runtime.rowCursor = 0;
    runtime.firstOutput = true;
    ResetDiskBatchRowState(runtime);
  }

  if (job.status != JobStatus::Running) return false;

  if (job.cancelRequested) {
    job.status = JobStatus::Cancelled;
    return false;
  }

  while (runtime.rowCursor < static_cast<int>(runtime.rows.size())) {
    const auto& row = runtime.rows[runtime.rowCursor];
    if (row.snap >= 0) break;
    ++runtime.rowCursor;
  }

  if (runtime.rowCursor >= static_cast<int>(runtime.rows.size())) {
    job.status = JobStatus::Completed;
    return false;
  }

  spec.row = &runtime.rows[runtime.rowCursor];
  spec.snapInit = static_cast<int>(spec.row->snap / nav.skipStep) * nav.skipStep;
  spec.targetFileIndex = spec.snapInit + nav.skipStep * runtime.scanOffset;
  spec.targetStep = (nav.skipStep != 0)
                  ? ((spec.targetFileIndex - nav.initialIndex) / nav.skipStep)
                  : nav.currentStep;

  if (runtime.scanOffset == 0) {
    std::snprintf(runtime.evolutionOutputFile,
                  sizeof(runtime.evolutionOutputFile),
                  "binary_evolution_%d.txt",
                  spec.row->idx);
    runtime.snapNotDisk = spec.snapInit;
  }

  const SnapshotSequenceLoadState loadState =
    EnsureSnapshotSequenceStepLoaded(snapshotLoad,
                                     SnapshotLoadOwner::DiskBatch,
                                     spec.targetStep,
                                     20,
                                     nullptr,
                                     0);
  if (loadState == SnapshotSequenceLoadState::Failed) {
    std::cerr << snapshotLoad.result.errorMessage << '\n';
    job.status = JobStatus::Error;
    return false;
  }
  if (loadState == SnapshotSequenceLoadState::Waiting) {
    return false;
  }
  return true;
}

static void ApplyDiskBatchFrameResult(const DiskBatchFrameSpec& spec,
                                      const DiskAnalysisResultState& disk1,
                                      const DiskAnalysisResultState& disk2,
                                      double physicalScale,
                                      FileNavigationRuntimeState& fileNav,
                                      SnapshotLoadRuntimeState& snapshotLoad,
                                      DiskAnalysisBatchRuntimeState& runtime,
                                      DiskAnalysisBatchResultState& result)
{
  auto& nav = fileNav.navigation;
  auto& current = fileNav.current;
  auto& job = runtime.job;

  bool rowDone = false;
  if (disk1.valid && disk2.valid) {
    const double dist =
      glm::length(disk1.disk.position - disk2.disk.position) * physicalScale;
    const double rDisk1 = disk1.disk.radius * physicalScale;
    const double rDisk2 = disk2.disk.radius * physicalScale;
    const bool flagDisk = (dist < rDisk1 + rDisk2);

    if (!AppendDiskBatchEvolution(runtime,
                                  spec.targetFileIndex,
                                  current.loadedTime,
                                  dist,
                                  rDisk1,
                                  rDisk2,
                                  flagDisk)) {
      std::cerr << "cannot open " << runtime.evolutionOutputFile << '\n';
      job.status = JobStatus::Error;
    } else if (flagDisk) {
      runtime.timeDisk = current.loadedTime;
      runtime.snapDisk = spec.targetFileIndex;
      runtime.distDisk = dist;
      runtime.rDisk1 = rDisk1;
      runtime.rDisk2 = rDisk2;
      rowDone = true;
    } else {
      runtime.timeNotDisk = current.loadedTime;
      runtime.snapNotDisk = spec.targetFileIndex;
    }
  }

  if (!rowDone) {
    ++runtime.scanOffset;
    if (runtime.scanOffset < 100) {
      const int nextFileIndex = spec.snapInit + nav.skipStep * runtime.scanOffset;
      const int nextStep = (nav.skipStep != 0)
                         ? ((nextFileIndex - nav.initialIndex) / nav.skipStep)
                         : nav.currentStep;
      RequestSnapshotLoad(snapshotLoad,
                          SnapshotLoadOwner::DiskBatch,
                          nextStep,
                          20);
      return;
    }
    rowDone = true;
  }

  if (rowDone) {
    if (!AppendDiskBatchSummary(runtime.params, runtime, *spec.row)) {
      std::cerr << "cannot open " << runtime.params.outputFile << '\n';
      job.status = JobStatus::Error;
    } else {
      ++result.processedRows;
      job.processed = result.processedRows;
      ++runtime.rowCursor;
      ResetDiskBatchRowState(runtime);
    }
  }
}

static void FinishDiskBatchExecution(FileNavigationRuntimeState& fileNav,
                                     SnapshotLoadRuntimeState& snapshotLoad,
                                     DiskAnalysisBatchRuntimeState& runtime,
                                     DiskAnalysisBatchResultState& result)
{
  auto& job = runtime.job;

  if (job.status == JobStatus::Cancelled ||
      job.status == JobStatus::Error ||
      job.status == JobStatus::Completed) {
    RestoreSnapshotSequenceNavigation(fileNav, snapshotLoad, job, 50);

    result.running = false;
    result.completed = true;
    result.success = (job.status == JobStatus::Completed);

    runtime.rows.clear();
    runtime.rowCursor = 0;
    ResetDiskBatchRowState(runtime);
    runtime.firstOutput = true;
    runtime.job = SnapshotJobRuntimeState{};
  }
}

void ExecuteDiskBatchRequest(ParticleArray& particles,
			     NormalizationContext& normalization,
			     FileNavigationRuntimeState& fileNav,
			     SnapshotLoadRuntimeState& snapshotLoad,
				     DiskRadiusFinder& diskFinder,
				     const RenderLayerState& diskRenderState,
				     DiskAnalysisBatchRequestState& request,
                                     DiskAnalysisBatchRuntimeState& runtime,
				     DiskAnalysisBatchResultState& result,
				     const UnitSystem& units)
{
  DiskBatchFrameSpec spec;
  const bool canExecuteFrame =
    PrepareDiskBatchExecution(fileNav, snapshotLoad, request, runtime, result, spec);

  if (canExecuteFrame) {
    const auto& row = *spec.row;

    DiskAnalysisRequestState diskRequest1;
    diskRequest1.targetParticleId = row.idA;
    diskRequest1.rejectTypeZeroTarget = true;
    diskRequest1.diskOpacity = diskRenderState.opacity;
    diskRequest1.runRequested = true;
    DiskAnalysisResultState diskResult1;
    ExecuteSingleDiskAnalysisRequest(particles,
                                     normalization,
                                     diskFinder,
                                     diskRequest1,
                                     diskResult1,
                                     units);

    DiskAnalysisRequestState diskRequest2;
    diskRequest2.targetParticleId = row.idB;
    diskRequest2.rejectTypeZeroTarget = true;
    diskRequest2.diskOpacity = diskRenderState.opacity;
    diskRequest2.runRequested = true;
    DiskAnalysisResultState diskResult2;
    ExecuteSingleDiskAnalysisRequest(particles,
                                     normalization,
                                     diskFinder,
                                     diskRequest2,
                                     diskResult2,
                                     units);

    ApplyDiskBatchFrameResult(spec,
                              diskResult1,
                              diskResult2,
                              normalization.toPhysicalScale(),
                              fileNav,
                              snapshotLoad,
                              runtime,
                              result);
  }

  FinishDiskBatchExecution(fileNav, snapshotLoad, runtime, result);
}

void ExecuteSingleEllipsoidAnalysisRequest(ParticleArray& particles,
                                           EllipseFitter& ellipsoidFitter,
                                           EllipsoidAnalysisRequestState& request,
                                           EllipsoidAnalysisResultState& result)
{
  if (request.clearRequested) {
    result = EllipsoidAnalysisResultState{};
    request.clearRequested = false;
    return;
  }

  if (!request.runRequested) {
    return;
  }

  request.runRequested = false;
  result = EllipsoidAnalysisResultState{};

  EllipsoidObject obj;
  if (ellipsoidFitter.computeEllipse(particles.particleBlock.particles,
                                     request.particleId1,
                                     request.particleId2,
                                     obj)) {
    obj.color = glm::vec3(1.0f);
    obj.tag = "analysis_ellipsoid";
    obj.renderMode = EllipsoidRenderMode::Solid;

    result.valid = true;
    result.ellipsoid = std::move(obj);
  } else {
    result.valid = false;
  }

  result.cpuUpdated = true;
}

static bool AppendEllipsoidBatchResult(const char* outputFile,
                                       bool& firstOutput,
                                       const EllipsoidAnalysisBatchTargetRow& row,
                                       double densityThreshold,
                                       const EllipsoidObject& ellipsoid)
{
  FILE* fp = std::fopen(outputFile, firstOutput ? "w" : "a");
  if (!fp) {
    return false;
  }

  if (firstOutput) {
    std::fprintf(fp, "index ID1 ID2 snap n a b c\n");
    firstOutput = false;
  }
  std::fprintf(fp, "%d %d %d %d %g %g %g %g\n",
               row.idx,
               row.idA,
               row.idB,
               row.snap,
               densityThreshold,
               ellipsoid.radii.x,
               ellipsoid.radii.y,
               ellipsoid.radii.z);
  std::fclose(fp);
  return true;
}

static bool PrepareEllipsoidBatchExecution(FileNavigationRuntimeState& fileNav,
                                           SnapshotLoadRuntimeState& snapshotLoad,
                                           EllipsoidAnalysisBatchRequestState& request,
                                           EllipsoidAnalysisBatchRuntimeState& runtime,
                                           EllipsoidAnalysisBatchResultState& result)
{
  auto& nav = fileNav.navigation;
  auto& job = runtime.job;

  if (request.cancelRequested) {
    job.cancelRequested = true;
    request.cancelRequested = false;
  }

  if (request.runRequested && job.status != JobStatus::Running) {
    runtime.params = request;
    runtime.params.runRequested = false;
    runtime.params.cancelRequested = false;
    request.runRequested = false;
    result = EllipsoidAnalysisBatchResultState{};
    result.lastInputFile = runtime.params.inputFile;
    result.lastOutputFile = runtime.params.outputFile;

    if (nav.skipStep <= 0) {
      std::cerr << "invalid skipStep for ellipsoid batch: " << nav.skipStep << '\n';
      result.completed = true;
      result.success = false;
      return false;
    }

    runtime.rows.clear();
    std::ifstream fin(runtime.params.inputFile);
    if (!fin) {
      std::cerr << "cannot open " << runtime.params.inputFile << '\n';
      result.completed = true;
      result.success = false;
      return false;
    }

    std::string line;
    EllipsoidAnalysisBatchTargetRow row;
    while (std::getline(fin, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream iss(line);
      if (iss >> row.idx >> row.idA >> row.idB >> row.snap) {
        runtime.rows.push_back(row);
      } else {
        std::cerr << "parse error: " << line << '\n';
      }
    }

    runtime.rowCursor = 0;
    runtime.firstOutput = true;
    job = SnapshotJobRuntimeState{};
    job.status = JobStatus::Running;
    job.savedCurrentStep = nav.currentStep;
  }

  if (job.status != JobStatus::Running) return false;

  if (job.cancelRequested) {
    job.status = JobStatus::Cancelled;
    return false;
  }

  while (runtime.rowCursor < static_cast<int>(runtime.rows.size())) {
    const auto& row = runtime.rows[runtime.rowCursor];
    if (row.snap >= 0) break;
    ++runtime.rowCursor;
  }

  if (runtime.rowCursor >= static_cast<int>(runtime.rows.size())) {
    job.status = JobStatus::Completed;
    return false;
  }

  const auto& row = runtime.rows[runtime.rowCursor];
  const int targetStep = (nav.skipStep != 0)
                       ? ((row.snap - nav.initialIndex) / nav.skipStep)
                       : nav.currentStep;
  const SnapshotSequenceLoadState loadState =
    EnsureSnapshotSequenceStepLoaded(snapshotLoad,
                                     SnapshotLoadOwner::EllipsoidBatch,
                                     targetStep,
                                     20,
                                     nullptr,
                                     0);
  if (loadState == SnapshotSequenceLoadState::Failed) {
    std::cerr << snapshotLoad.result.errorMessage << '\n';
    job.status = JobStatus::Error;
    return false;
  }
  if (loadState == SnapshotSequenceLoadState::Waiting) {
    return false;
  }
  return true;
}

static void FinishEllipsoidBatchExecution(FileNavigationRuntimeState& fileNav,
                                          SnapshotLoadRuntimeState& snapshotLoad,
                                          EllipsoidAnalysisBatchRuntimeState& runtime,
                                          EllipsoidAnalysisBatchResultState& result)
{
  auto& job = runtime.job;

  if (job.status == JobStatus::Cancelled ||
      job.status == JobStatus::Error ||
      job.status == JobStatus::Completed) {
    RestoreSnapshotSequenceNavigation(fileNav, snapshotLoad, job, 50);

    result.completed = true;
    result.success = (job.status == JobStatus::Completed);

    runtime.rows.clear();
    runtime.rowCursor = 0;
    runtime.firstOutput = true;
    runtime.job = SnapshotJobRuntimeState{};
  }
}

void ExecuteEllipsoidBatchRequest(ParticleArray& particles,
				  FileNavigationRuntimeState& fileNav,
					  SnapshotLoadRuntimeState& snapshotLoad,
					  EllipseFitter& ellipsoidFitter,
					  EllipsoidAnalysisBatchRequestState& request,
                                          EllipsoidAnalysisBatchRuntimeState& runtime,
					  EllipsoidAnalysisBatchResultState& result)
{
  const bool canExecuteFrame =
    PrepareEllipsoidBatchExecution(fileNav, snapshotLoad, request, runtime, result);

  if (canExecuteFrame) {
    const auto& row = runtime.rows[runtime.rowCursor];
    EllipsoidAnalysisRequestState frameRequest;
    frameRequest.particleId1 = row.idA;
    frameRequest.particleId2 = row.idB;
    frameRequest.runRequested = true;
    EllipsoidAnalysisResultState frameResult;
    ExecuteSingleEllipsoidAnalysisRequest(particles,
                                          ellipsoidFitter,
                                          frameRequest,
                                          frameResult);

    if (frameResult.valid) {
      if (!AppendEllipsoidBatchResult(runtime.params.outputFile,
                                      runtime.firstOutput,
                                      row,
                                      ellipsoidFitter.getDensityThreshold(),
                                      frameResult.ellipsoid)) {
        std::cerr << "cannot open " << runtime.params.outputFile << '\n';
        runtime.job.status = JobStatus::Error;
      } else {
        ++result.processedRows;
        runtime.job.processed = result.processedRows;
      }
    }

    if (runtime.job.status == JobStatus::Running) {
      ++runtime.rowCursor;
    }
  }

  FinishEllipsoidBatchExecution(fileNav, snapshotLoad, runtime, result);
}
#endif


#ifdef STREAM_LINE
#include "StreamLine/stream_line_new.h"

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

  streamLine.setRegionFromParticleData(particles.particleBlock.particles);
  streamLine.setStreamRegionFromParticleData(particles.particleBlock.particles);

  if (request.limitRegion) {
    if (request.regionSize[0] > 0.f &&
        request.regionSize[1] > 0.f &&
        request.regionSize[2] > 0.f) {
      streamLine.setStreamRegionByHand(request.regionCenter, request.regionSize);
    } else {
      streamLine.disableStreamRegion();
    }
  } else {
    streamLine.disableStreamRegion();
  }

  streamLine.setSeeds(particles.particleBlock.particles, request.nSeeds);

  auto builtLines = streamLine.build(particles.particleBlock, 10.f);

  result.lines.clear();
  for (auto& line : builtLines) {
    line.color   = glm::vec3(1.0f);
    line.opacity = 1.0f;
    line.tag     = "streamline";
    result.lines.push_back(std::move(line));
  }

  result.cpuUpdated = true;
}
#endif

#ifdef ISO_CONTOUR
#include "IsoSurface/iso_contour_build.h"
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
  
  BuildIsoContourGeometry(particles,
                          request.selectedQuantity,
                          request.isoLevel,
                          request.maxTreeLevel,
                          geometry);

  isoContourRenderState.show = true;
  isoContourRenderState.cpuUpdated = true;
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

#ifdef USE_CONVEX_HULL
void ExecuteConvexHullRequests(ParticleArray& particles,
                               FindClump& clumpFind,
                               ConvexHullGenerator& convexHull,
                               ConvexHullRuntimeState& convexState,
                               RenderLayerState& polyhedraState)
{
  if (!clumpFind.checkClumpComputation()) {
    return;
  }
  if (!clumpFind.isDirty()) {
    return;
  }

  convexState.resetGroup("convex_hull");

  const int nclumps = clumpFind.get_nclumps();
  for (int i = 0; i < nclumps; ++i) {
    if (!clumpFind.flagShowHull(i)) {
      continue;
    }

    TrackingVector<ParticleData> pts =
      clumpFind.get_particle_indices(i, particles.particleBlock.particles);

    std::vector<glm::vec3> points;
    points.reserve(pts.size());
    for (const auto& p : pts) {
      points.emplace_back(p.pos[0], p.pos[1], p.pos[2]);
    }

    ConvexHullEntry entry;
    entry.hull = convexHull.buildHull(points);
    entry.tag = "convex_hull";
    entry.sourceId = i;
    entry.visible = static_cast<bool>(entry.hull);
    entry.lineVertices = convexHull.buildLineVertices(points);

    convexState.entries.push_back(std::move(entry));
  }

  clumpFind.clearDirtyFlag();
  polyhedraState.show = !convexState.entries.empty();
  polyhedraState.cpuUpdated = true;
}
#endif

void ExecuteFileNavigationRequests(FileNavigationRuntimeState& rt,
                                   SnapshotLoadRuntimeState& snapshotLoad)
{
  auto& req = rt.request;
  auto& nav = rt.navigation;

  auto enqueueUserNavLoad = [&](int step) {
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::UserNavigation,
                        step,
                        100); // User操作を最優先
  };

  if (req.applySkipStepRequested) {
    if (rt.tempSkipStep > 0) {
      nav.currentStep = std::max(
        0,
        static_cast<int>(std::round(
          (nav.currentFileIndex - nav.initialIndex) /
          static_cast<float>(rt.tempSkipStep)))
      );
      nav.skipStep = rt.tempSkipStep;
      RecomputeCurrentFileIndex(rt);
      enqueueUserNavLoad(nav.currentStep);
    }
    req.applySkipStepRequested = false;
  }

  if (req.loadSelectedSnapshotRequested) {
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.loadSelectedSnapshotRequested = false;
  }

  if (req.loadPreviousRequested) {
    if (nav.currentStep > 0) {
      --nav.currentStep;
    }
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.loadPreviousRequested = false;
  }

  if (req.loadNextRequested) {
    ++nav.currentStep;
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.loadNextRequested = false;
  }

  if (req.loadBatchRequested) {
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.loadBatchRequested = false;
  }

  if (req.reloadRequested) {
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.reloadRequested = false;
  }

  if (req.generateTestDataRequested) {
    RecomputeCurrentFileIndex(rt);
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::UserNavigation,
                        nav.currentStep,
                        1,
                        SnapshotLoadKind::GenerateTestData);
    req.generateTestDataRequested = false;
  }
}

void ExecuteSettingsActionRequests(ParticleArray& particles,
                                   QuantityState& quantity,
                                   ParticleVisualConfig& particleVisual,
                                   RenderRuntimeState& render,
                                   SettingsRuntimeState& settings,
                                   SnapshotPostprocessState& post)
{
  auto& req = settings.request;

  if (req.applyParticleVisualRequested) {
    particleVisual = req.particleVisualDraft;
    particles.particlesDirty = true;
    req.applyParticleVisualRequested = false;
    req.particleVisualDraftDirty = false;
    req.particleRenderDirtyRequested = false;
  }

  if (req.applyRenderRequested) {
    render.particleLabels = req.renderDraft.particleLabels;
    render.velocity = req.renderDraft.velocity;
    render.disks.opacity = req.renderDraft.diskOpacity;
    render.ellipsoids.opacity = req.renderDraft.ellipsoidOpacity;
    render.isocontour.opacity = req.renderDraft.isoContourOpacity;
    render.crossGizmo.size = req.renderDraft.crossGizmoSize;
    req.applyRenderRequested = false;
    req.renderDraftDirty = false;
  }

  if (req.applyUnitsRequested) {
    quantity.units = req.unitsDraft;
    quantity.units.updateDerived();
    req.unitConversionRebuildRequested = true;
    req.applyUnitsRequested = false;
    req.unitsDraftDirty = false;
  }

  if (req.normalizeRequested) {
    settings.normalization.originalMax = quantity.range.originalMax;
    NormalizeParticlePositions(particles, settings.normalization);
    post.refreshTree = true;
    post.refreshCulling = true;
    post.refreshTopParticles = true;
    req.normalizeRequested = false;
  }

  if (req.particleRenderDirtyRequested) {
    particles.particlesDirty = true;
    req.particleRenderDirtyRequested = false;
  }

  if (req.velocityRenderDirtyRequested) {
    particles.velocityDirty = true;
    req.velocityRenderDirtyRequested = false;
  }

  if (req.unitConversionRebuildRequested) {
    auto& current = settings.fileNavigation.current;
    current.useComovingCoordinates = quantity.units.useComovingCoordinate;
    if (current.useComovingCoordinates) {
      double a = current.loadedTime;
      if (!std::isfinite(a) || a <= 0.0) {
        a = 1.0;
      }
      current.loadedScaleFactor = a;
      current.loadedRedshift = (1.0 / a) - 1.0;
    } else {
      current.loadedScaleFactor = 1.0;
      current.loadedRedshift = 0.0;
    }

    quantity.conversion.displaySpace =
      quantity.units.useComovingCoordinate
      ? UnitSpace::Comoving
      : UnitSpace::Physical;
    quantity.rebuildConversion(settings.fileNavigation.current.loadedScaleFactor);
    req.unitConversionRebuildRequested = false;
  }
}

void ExecuteCameraPlacementRequests(ParticleArray& particles,
				    const NormalizationContext& normalization,
				    ViewFilterConfig& viewFilter,
				    CameraContext& camCtx,
				    SettingsRuntimeState& rt,
				    SnapshotPostprocessState &post)
{
  auto& req = rt.cameraPlacement;

  if (req.setCenterRequested) {
    float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);

    if (req.inputIsOriginal) {
      float scale = normalization.toPhysicalScale();
      camCtx.cameraTarget =
        glm::vec3(req.centerInput[0], req.centerInput[1], req.centerInput[2]) * scale;
    } else {
      camCtx.cameraTarget =
        glm::vec3(req.centerInput[0], req.centerInput[1], req.centerInput[2]);
    }

    camCtx.cameraPos = camCtx.cameraTarget - direction * distance;
    req.setCenterRequested = false;
  }

  if (req.setProjectionRequested) {
    float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);

    switch (req.currentView) {
    case 0:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(distance, 0.0f, 0.0f);
      camCtx.cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 1:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(-distance, 0.0f, 0.0f);
      camCtx.cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 2:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, distance, 0.0f);
      camCtx.cameraUp  = glm::vec3(0.0f, 0.0f, -1.0f);
      break;
    case 3:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, -distance, 0.0f);
      camCtx.cameraUp  = glm::vec3(0.0f, 0.0f, 1.0f);
      break;
    case 4:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, 0.0f, distance);
      camCtx.cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 5:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, 0.0f, -distance);
      camCtx.cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    }

    glm::vec3 viewDir = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
    glm::quat rollQuat = glm::angleAxis(glm::radians(req.rollAngle), viewDir);
    camCtx.cameraUp = rollQuat * camCtx.cameraUp;

    glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
    camCtx.cameraOrientation = glm::quat_cast(glm::inverse(view));

    req.setProjectionRequested = false;
  }

  if (req.applyCullingRequested) {
    ApplyCullingSphere(particles, normalization, viewFilter);
    viewFilter.enabled = true;
    req.applyCullingRequested = false;
  }

  if (req.clearCullingRequested) {
    ClearVisibilityMask(particles);
    viewFilter.enabled = false;
    req.clearCullingRequested = false;
  }

  if(post.refreshCulling){
    if(viewFilter.enabled)
      ApplyCullingSphere(particles, normalization, viewFilter);
    post.refreshCulling = false;
  }
}

static void RecenterCameraKeepView(CameraContext& camCtx, const glm::vec3& target)
{
  float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
  if (dist < 1e-6f) {
    dist = (camCtx.distance > 1e-6f) ? camCtx.distance : 1.0f;
  }

  glm::vec3 dir = camCtx.cameraTarget - camCtx.cameraPos;
  if (glm::dot(dir, dir) < 1e-12f) {
    dir = glm::vec3(0.0f, 0.0f, -1.0f);
  } else {
    dir = glm::normalize(dir);
  }

  camCtx.cameraTarget = target;
  camCtx.cameraPos = target - dir * dist;
  camCtx.distance = dist;

  glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
  camCtx.cameraOrientation = glm::quat_cast(glm::inverse(view));
}


static glm::vec3 StabilizeAxisSign(glm::vec3 axis, TrackingTargetState& track)
{
  if (!track.amKeepSignContinuity) return axis;

  if (track.amHasLastAxis) {
    glm::vec3 last(track.amLastAxis[0], track.amLastAxis[1], track.amLastAxis[2]);
    if (glm::dot(axis, last) < 0.0f) {
      axis = -axis;
    }
  }

  track.amLastAxis[0] = axis.x;
  track.amLastAxis[1] = axis.y;
  track.amLastAxis[2] = axis.z;
  track.amHasLastAxis = true;

  return axis;
}

static void ApplyCameraAlignmentFromAxis(CameraContext& camCtx,
                                         const glm::vec3& center,
                                         const glm::vec3& axisIn,
                                         AngularMomentumViewMode mode)
{
  glm::vec3 axis = glm::normalize(axisIn);

  float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
  if (dist < 1e-6f) {
    dist = (camCtx.distance > 1e-6f) ? camCtx.distance : 1.0f;
  }

  glm::vec3 prevF = camCtx.cameraTarget - camCtx.cameraPos;
  if (glm::dot(prevF, prevF) < 1e-12f) prevF = glm::vec3(0.0f, 0.0f, -1.0f);
  prevF = glm::normalize(prevF);

  glm::vec3 forward(0.0f);

  if (mode == AngularMomentumViewMode::FaceOn) {
    glm::vec3 f1 = -axis;
    glm::vec3 f2 =  axis;
    forward = (glm::dot(f1, prevF) >= glm::dot(f2, prevF)) ? f1 : f2;
  } else {
    glm::vec3 proj = prevF - glm::dot(prevF, axis) * axis;
    if (glm::dot(proj, proj) < 1e-12f) {
      glm::vec3 c1 = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
      if (glm::dot(c1, c1) < 1e-12f) {
        c1 = glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f));
      }
      c1 = glm::normalize(c1);
      glm::vec3 c2 = -c1;
      forward = (glm::dot(c1, prevF) >= glm::dot(c2, prevF)) ? c1 : c2;
    } else {
      forward = glm::normalize(proj);
    }
  }

  glm::vec3 upHint = (mode == AngularMomentumViewMode::EdgeOn) ? axis : camCtx.cameraUp;
  if (glm::dot(upHint, upHint) < 1e-12f) upHint = glm::vec3(0.0f, 1.0f, 0.0f);
  upHint = glm::normalize(upHint);

  if (std::abs(glm::dot(upHint, forward)) > 0.95f) {
    upHint = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(upHint, forward)) > 0.95f) {
      upHint = glm::vec3(1.0f, 0.0f, 0.0f);
    }
  }

  glm::vec3 right = glm::cross(forward, upHint);
  if (glm::dot(right, right) < 1e-12f) {
    upHint = glm::vec3(1.0f, 0.0f, 0.0f);
    right = glm::cross(forward, upHint);
  }
  right = glm::normalize(right);

  glm::vec3 up = glm::normalize(glm::cross(right, forward));

  camCtx.cameraTarget = center;
  camCtx.cameraPos = center - forward * dist;
  camCtx.cameraUp = up;
  camCtx.distance = dist;

  glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
  camCtx.cameraOrientation = glm::quat_cast(glm::inverse(view));
}

static bool ResolveTrackingCenter(ParticleArray& particles,
                                  ClumpStore& clumpStore,
                                  const NormalizationContext& normalization,
                                  TrackingTargetState& track,
                                  int currentFileIndex,
                                  glm::vec3& outCenter)
{
#ifdef CLUMP_DATA_READ
  if (track.followClump) {
    if (clumpStore.filePath().empty() || clumpStore.empty()) {
      track.followClump = false;
    } else {
      int idx = clumpStore.findIndexByClumpID(track.targetClumpID);
      if (idx < 0 && track.targetClumpID >= 0 && track.targetClumpID < static_cast<int>(clumpStore.size())) {
        idx = track.targetClumpID;
      }

      if (idx < 0 || idx >= static_cast<int>(clumpStore.size())) {
        track.followClump = false;
      } else {
        ClumpData targetClump = clumpStore.clump(idx);

        TrackingVector<ClumpData> newClumps =
          loadClumpData(clumpStore.filePath().c_str(),
                        currentFileIndex,
                        normalization.toNormalizedScale());

        if (newClumps.empty()) {
          track.followClump = false;
        } else {
          clumpStore.setClumps(std::move(newClumps));

          float nextPos[3] = {0.f, 0.f, 0.f};
          targetClump.get_next_clump_position(clumpStore.clumps(), nextPos);
          outCenter = glm::vec3(nextPos[0], nextPos[1], nextPos[2]);
          return true;
        }
      }
    }
  }
#endif

  if (track.followParticle) {
    float pos[3] = {0.f, 0.f, 0.f};
    if (!particles.findParticleID(track.targetParticleID, pos)) {
      track.followParticle = false;
    } else {
      outCenter = glm::vec3(pos[0], pos[1], pos[2]);
      return true;
    }
  }

  if (track.followSinkParticle) {
    float targetPos[3] = {0.f, 0.f, 0.f};
    bool found = false;

    if (!track.followSinkParticleMostMassive) {
      found = particles.findParticleID(track.targetSinkParticleID, targetPos);
    }

    if (track.followSinkParticleMostMassive || !found) {
      double massMax = -1.0;
      for (const auto& p : particles.particleBlock.particles) {
        if (p.type < 3) continue;
        if (p.mass > massMax) {
          targetPos[0] = p.pos[0];
          targetPos[1] = p.pos[1];
          targetPos[2] = p.pos[2];
          massMax = p.mass;
          found = true;
        }
      }
    }

    if (!found) {
      track.followSinkParticle = false;
    } else {
      if (track.useMassCenter) {
        double dPos[3] = {0.0, 0.0, 0.0};
        double weight = 0.0;
        const double r2max = (track.massCenterRadius > 0.0f)
                           ? static_cast<double>(track.massCenterRadius) * static_cast<double>(track.massCenterRadius)
                           : -1.0;

        for (const auto& p : particles.particleBlock.particles) {
          if (p.type == 1 || p.type == 2) continue;
          if (p.type == 0 && p.density < track.massCenterMinDensity) continue;

          const double dx = static_cast<double>(targetPos[0]) - static_cast<double>(p.pos[0]);
          const double dy = static_cast<double>(targetPos[1]) - static_cast<double>(p.pos[1]);
          const double dz = static_cast<double>(targetPos[2]) - static_cast<double>(p.pos[2]);
          const double dist2 = dx * dx + dy * dy + dz * dz;
          if (r2max > 0.0 && dist2 > r2max) continue;

          dPos[0] += static_cast<double>(p.mass) * dx;
          dPos[1] += static_cast<double>(p.mass) * dy;
          dPos[2] += static_cast<double>(p.mass) * dz;
          weight += static_cast<double>(p.mass);
        }

        if (weight > 0.0) {
          targetPos[0] += static_cast<float>(dPos[0] / weight);
          targetPos[1] += static_cast<float>(dPos[1] / weight);
          targetPos[2] += static_cast<float>(dPos[2] / weight);
        }
      }

      outCenter = glm::vec3(targetPos[0], targetPos[1], targetPos[2]);
      return true;
    }
  }

  return false;
}


void ExecutePostSnapshotLoadActions(ParticleArray& particles,
                                    ClumpStore& clumpStore,
                                    NormalizationContext& normalization,
                                    TrackingTargetState& track,
                                    CameraContext& camCtx,
                                    SnapshotPostprocessState& post,
                                    int currentFileIndex)
{
  if (!post.applyTrackingToCamera && !track.renewAfterSnapshot) {
    return;
  }

  glm::vec3 center = camCtx.cameraTarget;
  const bool foundCenter =
    ResolveTrackingCenter(particles,
                          clumpStore,
                          normalization,
                          track,
                          currentFileIndex,
                          center);

  if (foundCenter) {
    if (track.alignToAngularMomentum) {
      ParticleSelectionOption op;
      op.center = center;
      op.radius = track.amRadius;
      op.useType = track.amUseType;
      op.flagSubtractBulkVelocity = track.amSubtractBulkVelocity;
      
      glm::vec3 axis(0.0f, 0.0f, 1.0f);
      if (particles.particleBlock.ComputeAngularMomentumAxis(op, axis)) {
        axis = StabilizeAxisSign(axis, track);
        ApplyCameraAlignmentFromAxis(camCtx, center, axis, track.amViewMode);
      } else {
        RecenterCameraKeepView(camCtx, center);
      }
    } else {
      RecenterCameraKeepView(camCtx, center);
    }
  }

  post.applyTrackingToCamera = false;
  track.renewAfterSnapshot = false;
}
