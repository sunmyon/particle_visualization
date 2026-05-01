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
#include "data/simulation_dataset.h"
#include "data/sample_coordinates.h"
#include "data/particle_selection.h"
#include "data/clump_loader.h"
#include "data/clump_store.h"
#include "data/halo_store.h"
#include "render/scene_objects.h"
#ifdef GEOMETRICAL_ANALYSIS
#include "analysis/disk_radius.h"
#include "analysis/ellipse_fitter.h"

void ExecuteSingleDiskAnalysisRequest(SimulationDataset& particles,
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
  for (size_t i = 0; i < particles.simulationBlock.particles.size(); ++i) {
    const auto& p = particles.simulationBlock.particles[i];
    if (particles.simulationBlock.particleIdSigned(i) != request.targetParticleId) {
      continue;
    }
    if (request.rejectTypeZeroTarget && p.type == 0) {
      result.valid = false;
      result.cpuUpdated = true;
      return;
    }

    param.mass = p.mass;
    for (int k = 0; k < 3; ++k) {
      const glm::vec3 pos =
        renderPosition(p, particles.simulationBlock.worldToRenderScale);
      param.center[k] = pos[k];
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

  if (diskFinder.compute(particles.simulationBlock.particles,
                         particles.simulationBlock.worldToRenderScale,
                         param,
                         disk)) {
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

  std::fprintf(fp, "%d %lld %lld %g %d %g %g %g %g %d\n",
	               row.idx,
                   static_cast<long long>(row.idA),
                   static_cast<long long>(row.idB),
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

void ExecuteDiskBatchRequest(SimulationDataset& particles,
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

void ExecuteSingleEllipsoidAnalysisRequest(SimulationDataset& particles,
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
  if (ellipsoidFitter.computeEllipse(particles.simulationBlock,
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
  std::fprintf(fp, "%d %lld %lld %d %g %g %g %g\n",
               row.idx,
               static_cast<long long>(row.idA),
               static_cast<long long>(row.idB),
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

void ExecuteEllipsoidBatchRequest(SimulationDataset& particles,
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
