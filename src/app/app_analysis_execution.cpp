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

#include <glm/vec3.hpp>

#include "app/app_state.h"
#include "app/analysis_state.h"
#include "app/runtime_state.h"
#include "app/normalization_config.h"
#include "app/tracking_view_state.h"
#include "app/snapshot_state_sync.h"
#include "app/app_visibility_actions.h"
#include "app/app_data_actions.h"
#include "data/particle_array.h"
#include "data/particle_selection.h"
#include "data/clump_loader.h"
#include "data/clump_store.h"
#include "data/halo_store.h"
#include "object.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_context.h"
#include "FindClumps/find_clumps_helpers.h"

#ifdef USE_CONVEX_HULL
#include "FindClumps/find_clumps.h"
#include "geometry/convex_hull_generator.h"
#include "app/convex_hull_state.h"
#endif

#include "image/image_io.h"

static bool IsSafeIndexFormat(const char* format)
{
  if (!format || format[0] == '\0') {
    return false;
  }

  bool hasIndexSpecifier = false;
  const char* p = format;
  while (*p) {
    if (*p != '%') {
      ++p;
      continue;
    }

    ++p;
    if (*p == '\0') {
      return false;
    }
    if (*p == '%') {
      ++p;
      continue;
    }

    while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') ++p;
    while (*p >= '0' && *p <= '9') ++p;
    if (*p == '.') {
      ++p;
      while (*p >= '0' && *p <= '9') ++p;
    }
    if (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't') {
      const char m = *p++;
      if ((m == 'h' && *p == 'h') || (m == 'l' && *p == 'l')) {
        ++p;
      }
    }

    if (*p == '\0') {
      return false;
    }

    if (*p == 'd' || *p == 'i' || *p == 'u') {
      hasIndexSpecifier = true;
      ++p;
      continue;
    }

    return false;
  }

  return hasIndexSpecifier;
}

static std::string ShellQuote(const std::string& value)
{
  std::string quoted;
  quoted.reserve(value.size() + 2);
  quoted.push_back('\'');
  for (char c : value) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(c);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

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

    param.mass = p.mass;
    for (int k = 0; k < 3; ++k) {
      param.center[k]   = p.pos[k];
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

  param.G         = units.grav_const_internal;
  param.max_shell = 100;
  param.scale_fac = normalization.toPhysicalScale();

  DiskObject disk;
  disk.color   = glm::vec3(1.0f);
  disk.tag     = "main_disk";

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
  auto& nav = fileNav.navigation;
  auto& current = fileNav.current;
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
      return;
    }

    runtime.rows.clear();
    if (!LoadDiskBatchTargets(runtime.params.inputFile, runtime.rows)) {
      std::cerr << "cannot open " << runtime.params.inputFile << '\n';
      result.running = false;
      result.completed = true;
      result.success = false;
      return;
    }

    job = SnapshotJobRuntimeState{};
    job.status = JobStatus::Running;
    job.savedCurrentStep = nav.currentStep;
    runtime.rowCursor = 0;
    runtime.firstOutput = true;
    ResetDiskBatchRowState(runtime);
  }

  if (job.status != JobStatus::Running) return;

  if (job.cancelRequested) {
    job.status = JobStatus::Cancelled;
  }

  if (job.status == JobStatus::Running) {
    while (runtime.rowCursor < static_cast<int>(runtime.rows.size())) {
      const auto& row = runtime.rows[runtime.rowCursor];
      if (row.snap >= 0) break;
      ++runtime.rowCursor;
    }

    if (runtime.rowCursor >= static_cast<int>(runtime.rows.size())) {
      job.status = JobStatus::Completed;
    } else {
      const auto& row = runtime.rows[runtime.rowCursor];
      const int snapInit = static_cast<int>(row.snap / nav.skipStep) * nav.skipStep;
      const int targetFileIndex = snapInit + nav.skipStep * runtime.scanOffset;
      const int targetStep = (nav.skipStep != 0)
                           ? ((targetFileIndex - nav.initialIndex) / nav.skipStep)
                           : nav.currentStep;

      if (runtime.scanOffset == 0) {
        std::snprintf(runtime.evolutionOutputFile,
                      sizeof(runtime.evolutionOutputFile),
                      "binary_evolution_%d.txt",
                      row.idx);
        runtime.snapNotDisk = snapInit;
      }

      if (!IsSnapshotLoadedFor(snapshotLoad, SnapshotLoadOwner::DiskBatch, targetStep)) {
        if (IsSnapshotLoadFailedFor(snapshotLoad, SnapshotLoadOwner::DiskBatch, targetStep)) {
          job.status = JobStatus::Error;
          goto finish_disk_batch;
        }
        RequestSnapshotLoad(snapshotLoad,
                            SnapshotLoadOwner::DiskBatch,
                            targetStep,
                            20);
        return;
      }

      bool rowDone = false;
      if (!particles.particleBlock.particles.empty()) {
        double r1 = 0.0;
        double r2 = 0.0;
        float pos1[3] = {0.f, 0.f, 0.f};
        float pos2[3] = {0.f, 0.f, 0.f};
        bool foundBinary = true;

        for (int ibin = 0; ibin < 2; ++ibin) {
          const int id = (ibin == 0) ? row.idA : row.idB;
          float* pos = (ibin == 0) ? pos1 : pos2;
          double* rDisk = (ibin == 0) ? &r1 : &r2;

          DiskRadiusFinder::Params param{};
          bool found = false;
          for (const auto& p : particles.particleBlock.particles) {
            if (p.ID != id) continue;
            if (p.type == 0) break;

            param.mass = p.mass;
            for (int k = 0; k < 3; ++k) {
              param.center[k] = pos[k] = p.pos[k];
              param.v_center[k] = p.vel[k];
            }
            found = true;
            break;
          }
          if (!found) {
            foundBinary = false;
            break;
          }

          param.G = units.grav_const_internal;
          param.max_shell = 100;
          param.scale_fac = normalization.toPhysicalScale();

          DiskObject disk;
          disk.color = glm::vec3(1.0f);
          disk.opacity = diskRenderState.opacity;
          disk.tag = "main_disk";
          if (!diskFinder.compute(particles.particleBlock.particles, param, disk)) {
            foundBinary = false;
            break;
          }
          *rDisk = disk.radius;
        }

        if (foundBinary) {
          const double dist2 =
            (pos1[0] - pos2[0]) * (pos1[0] - pos2[0]) +
            (pos1[1] - pos2[1]) * (pos1[1] - pos2[1]) +
            (pos1[2] - pos2[2]) * (pos1[2] - pos2[2]);

          const bool flagDisk = (std::sqrt(dist2) < r1 + r2);
          const double scale = normalization.toPhysicalScale();
          const double dist = std::sqrt(dist2) * scale;
          const double r1Scaled = r1 * scale;
          const double r2Scaled = r2 * scale;

          if (!AppendDiskBatchEvolution(runtime,
                                        targetFileIndex,
                                        current.loadedTime,
                                        dist,
                                        r1Scaled,
                                        r2Scaled,
                                        flagDisk)) {
            std::cerr << "cannot open " << runtime.evolutionOutputFile << '\n';
            job.status = JobStatus::Error;
          } else if (flagDisk) {
            runtime.timeDisk = current.loadedTime;
            runtime.snapDisk = targetFileIndex;
            runtime.distDisk = dist;
            runtime.rDisk1 = r1Scaled;
            runtime.rDisk2 = r2Scaled;
            rowDone = true;
          } else {
            runtime.timeNotDisk = current.loadedTime;
            runtime.snapNotDisk = targetFileIndex;
          }
        }
      }

      if (!rowDone) {
        ++runtime.scanOffset;
        if (runtime.scanOffset < 100) {
          const int nextFileIndex = snapInit + nav.skipStep * runtime.scanOffset;
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
        if (!AppendDiskBatchSummary(runtime.params, runtime, row)) {
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
  }

finish_disk_batch:
  if (job.status == JobStatus::Cancelled ||
      job.status == JobStatus::Error ||
      job.status == JobStatus::Completed) {
    nav.currentStep = job.savedCurrentStep;
    RecomputeCurrentFileIndex(fileNav);
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::UserNavigation,
                        nav.currentStep,
                        50);

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

void ExecuteEllipsoidBatchRequest(ParticleArray& particles,
				  FileNavigationRuntimeState& fileNav,
					  SnapshotLoadRuntimeState& snapshotLoad,
					  EllipseFitter& ellipsoidFitter,
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
      return;
    }

    runtime.rows.clear();
    std::ifstream fin(runtime.params.inputFile);
    if (!fin) {
      std::cerr << "cannot open " << runtime.params.inputFile << '\n';
      result.completed = true;
      result.success = false;
      return;
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

  if (job.status != JobStatus::Running) return;
  if (job.cancelRequested) {
    job.status = JobStatus::Cancelled;
  }

  if (job.status == JobStatus::Running) {
    while (runtime.rowCursor < static_cast<int>(runtime.rows.size())) {
      const auto& row = runtime.rows[runtime.rowCursor];
      if (row.snap >= 0) break;
      ++runtime.rowCursor;
    }

    if (runtime.rowCursor >= static_cast<int>(runtime.rows.size())) {
      job.status = JobStatus::Completed;
    } else {
      const auto& row = runtime.rows[runtime.rowCursor];
      const int targetStep = (nav.skipStep != 0)
                           ? ((row.snap - nav.initialIndex) / nav.skipStep)
                           : nav.currentStep;

      if (!IsSnapshotLoadedFor(snapshotLoad, SnapshotLoadOwner::EllipsoidBatch, targetStep)) {
        if (IsSnapshotLoadFailedFor(snapshotLoad, SnapshotLoadOwner::EllipsoidBatch, targetStep)) {
          job.status = JobStatus::Error;
          goto finish_ellipsoid_batch;
        }
        RequestSnapshotLoad(snapshotLoad,
                            SnapshotLoadOwner::EllipsoidBatch,
                            targetStep,
                            20);
        return;
      }

      if (!particles.particleBlock.particles.empty()) {
        EllipsoidObject obj;
        const bool ok = ellipsoidFitter.computeEllipse(particles.particleBlock.particles,
                                                       row.idA,
                                                       row.idB,
                                                       obj);
        if (ok) {
          FILE* fp = std::fopen(runtime.params.outputFile, runtime.firstOutput ? "w" : "a");
          if (!fp) {
            std::cerr << "cannot open " << runtime.params.outputFile << '\n';
            job.status = JobStatus::Error;
          } else {
            if (runtime.firstOutput) {
              std::fprintf(fp, "index ID1 ID2 snap n a b c\n");
              runtime.firstOutput = false;
            }
            const double n = ellipsoidFitter.getDensityThreshold();
            std::fprintf(fp, "%d %d %d %d %g %g %g %g\n",
                         row.idx, row.idA, row.idB, row.snap,
                         n, obj.radii.x, obj.radii.y, obj.radii.z);
            std::fclose(fp);
            ++result.processedRows;
            job.processed = result.processedRows;
          }
        }
      }

      if (job.status == JobStatus::Running) {
        ++runtime.rowCursor;
      }
    }
  }

finish_ellipsoid_batch:
  if (job.status == JobStatus::Cancelled ||
      job.status == JobStatus::Error ||
      job.status == JobStatus::Completed) {
    nav.currentStep = job.savedCurrentStep;
    RecomputeCurrentFileIndex(fileNav);
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::UserNavigation,
                        nav.currentStep,
                        50);

    result.completed = true;
    result.success = (job.status == JobStatus::Completed);

    runtime.rows.clear();
    runtime.rowCursor = 0;
    runtime.firstOutput = true;
    runtime.job = SnapshotJobRuntimeState{};
  }
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

#ifdef CLUMP_DATA_READ
void ExecuteClumpBatchRequest(ParticleArray& particles,
			      FileNavigationRuntimeState& fileNav,
				      SnapshotLoadRuntimeState& snapshotLoad,
				      FindClump& clumpFind,
				      ClumpBatchRequestState& request,
                                      ClumpBatchRuntimeState& runtime,
				      ClumpBatchResultState& result)
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
    result = ClumpBatchResultState{};

    char filename[512];
    std::snprintf(filename, sizeof(filename), "%s/%s",
                  runtime.params.outputFolderPath,
                  runtime.params.outputFileName);
    std::snprintf(result.outputPath, sizeof(result.outputPath), "%s", filename);

    if (runtime.params.nSnapshots <= 0) {
      std::snprintf(result.errorMessage, sizeof(result.errorMessage), "nSnapshots must be > 0");
      result.completed = true;
      return;
    }

    if (nav.skipStep <= 0) {
      std::snprintf(result.errorMessage, sizeof(result.errorMessage),
                    "invalid skipStep for clump batch: %d", nav.skipStep);
      result.completed = true;
      return;
    }

    clumpFind.initialize_prev_nodes();
    job = SnapshotJobRuntimeState{};
    job.status = JobStatus::Running;
    job.savedCurrentStep = nav.currentStep;
    job.beginStep = nav.currentStep;
    job.endStep = nav.currentStep + runtime.params.nSnapshots - 1;
    job.nextStep = job.beginStep;
    job.stepStride = 1;
  }

  if (job.status != JobStatus::Running) return;

  if (job.cancelRequested) {
    job.status = JobStatus::Cancelled;
  }

  if (job.status == JobStatus::Running) {
    const int targetStep = job.nextStep;
    if (!IsSnapshotLoadedFor(snapshotLoad, SnapshotLoadOwner::ClumpBatch, targetStep)) {
      if (IsSnapshotLoadFailedFor(snapshotLoad, SnapshotLoadOwner::ClumpBatch, targetStep)) {
        std::snprintf(result.errorMessage,
                      sizeof(result.errorMessage),
                      "%s",
                      snapshotLoad.result.errorMessage);
        job.status = JobStatus::Error;
      } else {
        RequestSnapshotLoad(snapshotLoad,
                            SnapshotLoadOwner::ClumpBatch,
                            targetStep,
                            20);
        return;
      }
    }

    if (job.status == JobStatus::Running) {
      const int newFileIndex = nav.initialIndex + targetStep * nav.skipStep;
      if (!particles.particleBlock.particles.empty()) {
        clumpFind.do_FOF_and_output_clump_data(runtime.params.method,
                                               particles.particleBlock.particles,
                                               fileNav.current.loadedTime,
                                               result.outputPath,
                                               newFileIndex);
        result.processedSnapshots++;
        job.processed = result.processedSnapshots;
      }

      job.nextStep += job.stepStride;
      if (job.nextStep > job.endStep) {
        job.status = JobStatus::Completed;
      } else {
        RequestSnapshotLoad(snapshotLoad,
                            SnapshotLoadOwner::ClumpBatch,
                            job.nextStep,
                            20);
        return;
      }
    }
  }

  if (job.status == JobStatus::Cancelled ||
      job.status == JobStatus::Error ||
      job.status == JobStatus::Completed) {
    nav.currentStep = job.savedCurrentStep;
    RecomputeCurrentFileIndex(fileNav);

    if (job.status == JobStatus::Completed) {
      const int initstep = nav.currentFileIndex;
      const int dstep = nav.skipStep;
      give_stellar_id_to_clumps(initstep,
                                runtime.params.nSnapshots,
                                dstep,
                                std::string(result.outputPath));
      result.completed = true;
    } else {
      if (job.status == JobStatus::Cancelled) {
        std::snprintf(result.errorMessage, sizeof(result.errorMessage), "Cancelled by user");
      }
      result.completed = true;
    }

    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::UserNavigation,
                        nav.currentStep,
                        50);

    runtime.job = SnapshotJobRuntimeState{};
  }
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

static void SaveCameraToJob(const CameraContext& camera, SnapshotJobRuntimeState& job)
{
  job.savedCameraValid = true;
  job.savedCameraPos = {
    camera.cameraPos.x, camera.cameraPos.y, camera.cameraPos.z
  };
  job.savedCameraTarget = {
    camera.cameraTarget.x, camera.cameraTarget.y, camera.cameraTarget.z
  };
  job.savedCameraUp = {
    camera.cameraUp.x, camera.cameraUp.y, camera.cameraUp.z
  };
#ifdef ROTATE_QUATERNION
  job.savedCameraOrientation = {
    camera.cameraOrientation.w,
    camera.cameraOrientation.x,
    camera.cameraOrientation.y,
    camera.cameraOrientation.z
  };
#endif
  job.savedCameraDistance = camera.distance;
}

static void RestoreCameraFromJob(CameraContext& camera, const SnapshotJobRuntimeState& job)
{
  if (!job.savedCameraValid) return;

  camera.cameraPos = glm::vec3(job.savedCameraPos[0], job.savedCameraPos[1], job.savedCameraPos[2]);
  camera.cameraTarget = glm::vec3(job.savedCameraTarget[0], job.savedCameraTarget[1], job.savedCameraTarget[2]);
  camera.cameraUp = glm::vec3(job.savedCameraUp[0], job.savedCameraUp[1], job.savedCameraUp[2]);
  camera.distance = job.savedCameraDistance;
#ifdef ROTATE_QUATERNION
  camera.cameraOrientation = glm::quat(job.savedCameraOrientation[0],
                                       job.savedCameraOrientation[1],
                                       job.savedCameraOrientation[2],
                                       job.savedCameraOrientation[3]);
#endif
}

static void ApplyMovieRequestToTracking(const ProjectionMovieRequestState& request,
                                        TrackingTargetState& track)
{
  track.followParticle = false;
  track.followClump = false;
  track.followSinkParticle = request.followSinkCenter;
  track.followSinkParticleMostMassive = request.followMostMassiveSink;
  track.targetSinkParticleID = request.particleIdCenter;
  track.useMassCenter = request.useMassCenter;
  track.massCenterRadius = request.massCenterRadius;
  track.massCenterMinDensity = request.massCenterMinDensity;

  track.alignToAngularMomentum = (request.alignToAngularMomentum || request.faceOn);
  track.amViewMode = request.faceOn ? AngularMomentumViewMode::FaceOn : request.amViewMode;
  track.amRadius = request.amRadius;
  track.amSubtractBulkVelocity = request.amSubtractBulkVelocity;
  track.amUseType = request.amUseType;
  track.amKeepSignContinuity = request.amKeepSignContinuity;
  track.amHasLastAxis = false;
  track.renewAfterSnapshot = true;
}

static bool RenderProjectionMovieFrame(ParticleArray& particles,				       
				       const UnitSystem& units,
                                       NormalizationContext& normalization,
                                       ProjectionMapGenerator& projectionMap,
                                       const ProjectionMapParams& baseParams,
                                       const CameraContext& camera,
                                       const ProjectionMovieRequestState& request,
                                       int frameSerial,
                                       int newFileIndex,
                                       double snapshotTime,
                                       ProjectionMovieResultState& result)
{
  namespace fs = std::filesystem;

  char filename_format[512];
  std::snprintf(filename_format, sizeof(filename_format), "%s/%s",
                request.outputFolderPath,
                request.outputFileFormat);

  char filename[512];
  if (IsSafeIndexFormat(request.outputFileFormat)) {
    std::snprintf(filename, sizeof(filename), filename_format, newFileIndex);
  } else {
    if (std::strchr(request.outputFileFormat, '%') != nullptr) {
      std::snprintf(result.errorMessage, sizeof(result.errorMessage),
                    "Unsafe outputFileFormat for movie: %s",
                    request.outputFileFormat);
      return false;
    }
    std::snprintf(filename, sizeof(filename), "%s/%s",
                  request.outputFolderPath,
                  request.outputFileFormat);
  }

  ProjectionMapParams frameParams = baseParams;
  frameParams.xoffset[0] = camera.cameraTarget[0];
  frameParams.xoffset[1] = camera.cameraTarget[1];
  frameParams.xoffset[2] = camera.cameraTarget[2];  

  ProjectionMapContext context =
    BuildProjectionMapContext(frameParams,
                              normalization.toPhysicalScale(),
                              snapshotTime);

  RgbImage image =
    projectionMap.makeDensityMapImage(particles, units, frameParams, context);

  if (!WritePngRgb(filename, image.width, image.height, image.rgb)) {
    std::snprintf(result.errorMessage, sizeof(result.errorMessage),
                  "Failed to write projection frame: %s", filename);
    return false;
  }

  char linkname[512];
  std::snprintf(linkname, sizeof(linkname), "ffmpeg_frames/frame_%04d.png", frameSerial);
  fs::remove(linkname);
  fs::create_symlink(fs::absolute(filename), linkname);

  return true;
}

void ExecuteProjectionMovieRequest(ParticleArray& particles,
				   const UnitSystem& units,
                                   NormalizationContext& normalization,
                                   TrackingTargetState& track,
                                   FileNavigationRuntimeState& fileNav,
	                                   ProjectionMapGenerator& projectionMap,
	                                   const ProjectionMapParams& baseParams,
	                                   CameraContext& camera,
	                                   ProjectionMovieRequestState& request,
	                                   ProjectionMovieRuntimeState& runtime,
	                                   SnapshotLoadRuntimeState& snapshotLoad,
	                                   SnapshotPostprocessState& post,
	                                   ProjectionMovieResultState& result)
{
  namespace fs = std::filesystem;
  const fs::path framesDir = "ffmpeg_frames";

  auto& job = runtime.job;
  auto& nav = fileNav.navigation;

  if (request.cancelRequested) {
    job.cancelRequested = true;
    request.cancelRequested = false;
  }

  if (request.runRequested && job.status != JobStatus::Running) {
    runtime.params = request;
    runtime.params.runRequested = false;
    runtime.params.cancelRequested = false;
    runtime.projectionParams = baseParams;
    request.runRequested = false;
    result = ProjectionMovieResultState{};
    job = SnapshotJobRuntimeState{};

    if (runtime.params.nSnapshots <= 0) {
      std::snprintf(result.errorMessage, sizeof(result.errorMessage), "nSnapshots must be > 0");
      result.success = false;
      result.completed = true;
      return;
    }

    try {
      fs::remove_all(framesDir);
      fs::create_directories(framesDir);
      fs::create_directories(runtime.params.outputFolderPath);
    } catch (const std::exception& e) {
      std::snprintf(result.errorMessage, sizeof(result.errorMessage), "%s", e.what());
      result.success = false;
      result.completed = true;
      return;
    }

    if (runtime.params.restoreCameraOnFinish) {
      SaveCameraToJob(camera, job);
    }
    job.savedTracking = track;
    job.savedTrackingValid = true;
    ApplyMovieRequestToTracking(runtime.params, track);
    post.applyTrackingToCamera = true;

    job.status = JobStatus::Running;
    job.savedCurrentStep = nav.currentStep;
    job.beginStep = nav.currentStep;
    job.endStep = nav.currentStep + runtime.params.nSnapshots - 1;
    job.nextStep = job.beginStep;
    job.stepStride = 1;
    job.processed = 0;
  }

  if (job.status != JobStatus::Running) {
    return;
  }

  if (job.cancelRequested) {
    job.status = JobStatus::Cancelled;
  }

  if (job.status == JobStatus::Running) {
    const int targetStep = job.nextStep;

    if (!IsSnapshotLoadedFor(snapshotLoad, SnapshotLoadOwner::ProjectionMovie, targetStep)) {
      if (IsSnapshotLoadFailedFor(snapshotLoad, SnapshotLoadOwner::ProjectionMovie, targetStep)) {
        std::snprintf(result.errorMessage,
                      sizeof(result.errorMessage),
                      "%s",
                      snapshotLoad.result.errorMessage);
        job.status = JobStatus::Error;
      } else {
        RequestSnapshotLoad(snapshotLoad,
                            SnapshotLoadOwner::ProjectionMovie,
                            targetStep,
                            10);
        return;
      }
    }

    if (job.status == JobStatus::Running) {
      const int newFileIndex = nav.initialIndex + targetStep * nav.skipStep;

      try {
        if (!RenderProjectionMovieFrame(particles,
                                        units,
                                        normalization,
                                        projectionMap,
                                        runtime.projectionParams,
                                        camera,
                                        runtime.params,
                                        job.processed,
                                        newFileIndex,
                                        fileNav.current.loadedTime,
                                        result)) {
          job.status = JobStatus::Error;
        } else {
          result.processedSnapshots++;
          job.processed++;
          job.nextStep += job.stepStride;

          if (job.nextStep > job.endStep) {
            job.status = JobStatus::Completed;
          } else {
            RequestSnapshotLoad(snapshotLoad,
                                SnapshotLoadOwner::ProjectionMovie,
                                job.nextStep,
                                10);
          }
        }
      } catch (const std::exception& e) {
        std::snprintf(result.errorMessage, sizeof(result.errorMessage), "%s", e.what());
        job.status = JobStatus::Error;
      }
    }
  }

  if (job.status == JobStatus::Completed) {
    if (result.processedSnapshots <= 0) {
      std::snprintf(result.errorMessage, sizeof(result.errorMessage), "No frames were generated");
      job.status = JobStatus::Error;
    } else {
      const std::string outputMoviePath =
        std::string(runtime.params.outputFolderPath) + "/" + std::string(runtime.params.outputMovieName);
      std::string ffmpegCommand =
        "ffmpeg -y -framerate 30 -i " +
        ShellQuote("ffmpeg_frames/frame_%04d.png") +
        " -vf " +
        ShellQuote("scale=ceil(iw/2)*2:ceil(ih/2)*2") +
        " -c:v libx264 -pix_fmt yuv420p " +
        ShellQuote(outputMoviePath);

      const int ffmpegExit = std::system(ffmpegCommand.c_str());
      if (ffmpegExit != 0) {
        std::snprintf(result.errorMessage, sizeof(result.errorMessage),
                      "ffmpeg failed with exit code %d", ffmpegExit);
        job.status = JobStatus::Error;
      } else {
        std::snprintf(result.outputMoviePath, sizeof(result.outputMoviePath),
                      "%s/%s", runtime.params.outputFolderPath, runtime.params.outputMovieName);
        result.success = true;
        result.completed = true;
      }
    }
  }

  if (job.status == JobStatus::Cancelled ||
      job.status == JobStatus::Error ||
      job.status == JobStatus::Completed) {
    fs::remove_all(framesDir);

    if (job.savedTrackingValid) {
      track = job.savedTracking;
    }

    if (runtime.params.restoreCameraOnFinish) {
      RestoreCameraFromJob(camera, job);
    }

    nav.currentStep = job.savedCurrentStep;
    RecomputeCurrentFileIndex(fileNav);
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::UserNavigation,
                        nav.currentStep,
                        50);

    if (job.status == JobStatus::Cancelled) {
      if (result.errorMessage[0] == '\0') {
        std::snprintf(result.errorMessage, sizeof(result.errorMessage), "Cancelled by user");
      }
      result.success = false;
      result.completed = true;
    } else if (job.status == JobStatus::Error) {
      result.success = false;
      result.completed = true;
    }

    job = SnapshotJobRuntimeState{};
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
