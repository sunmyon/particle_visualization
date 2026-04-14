#include <utility>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <iostream>
#include <cmath>

#include <glm/vec3.hpp>

#include "app/app_state.h"
#include "app/analysis_state.h"
#include "data/particle_array.h"
#include "FileIO/file_io.h"
#include "ui_state.h"
#include "object.h"

#ifdef GEOMETRICAL_ANALYSIS
#include "GeometricAnalysis/DiskRadius.hpp"
#include "GeometricAnalysis/ellipse_fitter.h"

void ExecuteSingleDiskAnalysisRequest(ParticleArray& particles,
				      DiskRadiusFinder& diskFinder,
				      DiskAnalysisRequestState& request,
				      DiskAnalysisResultState& result)
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

  param.G         = particles.GravConst_internal;
  param.max_shell = 100;
  param.scale_fac = particles.originalMax / particles.desiredMax;

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

void ExecuteDiskBatchRequest(ParticleArray& particles,
			     FileInfo& fileInfo,
			     DiskRadiusFinder& diskFinder,
			     const RenderLayerState& diskRenderState,
			     DiskAnalysisBatchRequestState& request,
			     DiskAnalysisBatchResultState& result)
{
  if (!request.runRequested) {
    return;
  }

  request.runRequested = false;

  result = DiskAnalysisBatchResultState{};
  result.running = true;
  result.lastInputFile  = request.inputFile;
  result.lastOutputFile = request.outputFile;

  struct Row { int idx, idA, idB, snap; };
  std::vector<Row> rows;

  {
    std::ifstream fin(request.inputFile);
    if (!fin) {
      std::cerr << "cannot open " << request.inputFile << '\n';
      result.running = false;
      result.completed = true;
      result.success = false;
      return;
    }

    std::string line;
    Row r;
    while (std::getline(fin, line)) {
      if (line.empty() || line[0] == '#')
        continue;

      std::istringstream iss(line);
      if (iss >> r.idx >> r.idA >> r.idB >> r.snap) {
        rows.push_back(r);
      } else {
        std::cerr << "parse error: " << line << '\n';
      }
    }
  }

  bool firstOutput = true;
  int processed = 0;

  for (auto& r : rows) {
    if (r.snap < 0) {
      continue;
    }

    FILE* fp_out = nullptr;
    if (firstOutput) {
      fp_out = std::fopen(request.outputFile, "w");
      if (fp_out) {
        std::fprintf(fp_out, "#index idA idB t_disk\n");
      }
      firstOutput = false;
    } else {
      fp_out = std::fopen(request.outputFile, "a");
    }

    if (!fp_out) {
      std::cerr << "cannot open " << request.outputFile << '\n';
      result.running = false;
      result.completed = true;
      result.success = false;
      result.processedRows = processed;
      return;
    }

    double time_disk = -1.0, time_not_disk = -1.0;
    char fname_evolution[255];
    std::snprintf(fname_evolution, sizeof(fname_evolution),
                  "binary_evolution_%d.txt", r.idx);

    bool firstEvolution = true;
    double dist_disk = 0.0, r_disk1 = 0.0, r_disk2 = 0.0;

    int snap_init = static_cast<int>(r.snap / fileInfo.skipStep) * fileInfo.skipStep;
    int snap_disk = -1, snap_not_disk = snap_init;

    for (int istep = 0; istep < 100; ++istep) {
      int snap = snap_init + fileInfo.skipStep * istep;
      fileInfo.loadNewSnapshot(snap, &particles);

      if (particles.particleBlock.particles.empty()) {
        continue;
      }

      double r1 = 0.0, r2 = 0.0;
      float pos1[3] = {0}, pos2[3] = {0};
      bool found_binary = true;

      for (int ibin = 0; ibin < 2; ++ibin) {
        int id = (ibin == 0) ? r.idA : r.idB;
        float* pos = (ibin == 0) ? pos1 : pos2;
        double* r_disk = (ibin == 0) ? &r1 : &r2;

        DiskRadiusFinder::Params param{};
        bool found = false;

        for (const auto& p : particles.particleBlock.particles) {
          if (p.ID != id) {
            continue;
          }

          if (p.type == 0) {
            break;
          }

          param.mass = p.mass;
          for (int k = 0; k < 3; ++k) {
            param.center[k]   = pos[k] = p.pos[k];
            param.v_center[k] = p.vel[k];
          }
          found = true;
          break;
        }

        if (!found) {
          found_binary = false;
          break;
        }

        param.G         = particles.GravConst_internal;
        param.max_shell = 100;
        param.scale_fac = particles.originalMax / particles.desiredMax;

        DiskObject disk;
        disk.color   = glm::vec3(1.0f);
        disk.opacity = diskRenderState.opacity;
        disk.tag     = "main_disk";

        if (diskFinder.compute(particles.particleBlock.particles, param, disk)) {
          *r_disk = disk.radius;
        } else {
          found_binary = false;
          break;
        }
      }

      if (!found_binary) {
        continue;
      }

      FILE* fp_evo = nullptr;
      if (firstEvolution) {
        fp_evo = std::fopen(fname_evolution, "w");
        firstEvolution = false;
        time_not_disk = particles.particleBlock.header.time;
        snap_not_disk = snap;
      } else {
        fp_evo = std::fopen(fname_evolution, "a");
      }

      if (!fp_evo) {
        std::fclose(fp_out);
        std::cerr << "cannot open " << fname_evolution << '\n';
        result.running = false;
        result.completed = true;
        result.success = false;
        result.processedRows = processed;
        return;
      }

      double dist2 =
          (pos1[0] - pos2[0]) * (pos1[0] - pos2[0]) +
          (pos1[1] - pos2[1]) * (pos1[1] - pos2[1]) +
          (pos1[2] - pos2[2]) * (pos1[2] - pos2[2]);

      bool flag_disk = (std::sqrt(dist2) < r1 + r2);
      double scale_fac = particles.originalMax / particles.desiredMax;

      std::fprintf(fp_evo, "%d %g %g %g %g %d\n",
                   snap,
                   particles.particleBlock.header.time,
                   std::sqrt(dist2) * scale_fac,
                   r1 * scale_fac,
                   r2 * scale_fac,
                   static_cast<int>(flag_disk));
      std::fclose(fp_evo);

      if (flag_disk) {
        time_disk = particles.particleBlock.header.time;
        dist_disk = std::sqrt(dist2) * scale_fac;
        snap_disk = snap;
        r_disk1 = r1 * scale_fac;
        r_disk2 = r2 * scale_fac;
        break;
      } else {
        time_not_disk = particles.particleBlock.header.time;
        snap_not_disk = snap;
      }
    }

    std::fprintf(fp_out, "%d %d %d %g %d %g %g %g %g %d\n",
                 r.idx, r.idA, r.idB,
                 time_disk, snap_disk,
                 dist_disk, r_disk1, r_disk2,
                 time_not_disk, snap_not_disk);
    std::fclose(fp_out);

    ++processed;
  }

  result.running = false;
  result.completed = true;
  result.success = true;
  result.processedRows = processed;
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
                                  FileInfo& fileInfo,
                                  EllipseFitter& ellipsoidFitter,
                                  EllipsoidAnalysisBatchRequestState& request,
                                  EllipsoidAnalysisBatchResultState& result)
{
  if (!request.runRequested) {
    return;
  }

  request.runRequested = false;

  result = EllipsoidAnalysisBatchResultState{};
  result.lastInputFile = request.inputFile;
  result.lastOutputFile = request.outputFile;

  struct Row {
    int idx, idA, idB, snap;
  };

  std::vector<Row> rows;
  {
    std::ifstream fin(request.inputFile);
    if (!fin) {
      std::cerr << "cannot open " << request.inputFile << '\n';
      result.completed = true;
      result.success = false;
      return;
    }

    std::string line;
    Row r;
    while (std::getline(fin, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }

      std::istringstream iss(line);
      if (iss >> r.idx >> r.idA >> r.idB >> r.snap) {
        rows.push_back(r);
      } else {
        std::cerr << "parse error: " << line << '\n';
      }
    }
  }

  bool first = true;
  int processed = 0;

  for (const auto& r : rows) {
    if (r.snap < 0) {
      continue;
    }

    fileInfo.loadNewSnapshot(r.snap, &particles);
    if (particles.particleBlock.particles.empty()) {
      continue;
    }

    EllipsoidObject obj;
    bool ok = ellipsoidFitter.computeEllipse(particles.particleBlock.particles,
                                             r.idA,
                                             r.idB,
                                             obj);
    if (!ok) {
      continue;
    }

    FILE* fp_out = std::fopen(request.outputFile, first ? "w" : "a");
    if (!fp_out) {
      std::cerr << "cannot open " << request.outputFile << '\n';
      result.completed = true;
      result.success = false;
      result.processedRows = processed;
      return;
    }

    if (first) {
      std::fprintf(fp_out, "index ID1 ID2 snap n a b c\n");
      first = false;
    }

    double a = obj.radii.x;
    double b = obj.radii.y;
    double c = obj.radii.z;
    double n = ellipsoidFitter.getDensityThreshold();

    std::fprintf(fp_out, "%d %d %d %d %g %g %g %g\n",
                 r.idx, r.idA, r.idB, r.snap, n, a, b, c);
    std::fclose(fp_out);

    ++processed;
  }

  result.completed = true;
  result.success = true;
  result.processedRows = processed;
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
                                  StellarDensityRequestState& request)
{
  if (!request.runRequested) {
    return;
  }

  request.runRequested = false;

  std::array<bool, 6> sel{};
  for (int i = 0; i < 6; ++i) {
    sel[i] = request.selectedTypes[i];
  }

  particles.computeStellarDensity(sel, request.overwriteHsml);
  particles.particlesDirty = true;
}
