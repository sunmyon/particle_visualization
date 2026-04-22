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
#include "app/runtime_state.h"
#include "app/normalization_config.h"
#include "app/tracking_view_state.h"
#include "app/app_visibility_actions.h"
#include "data/particle_array.h"
#include "data/clump_loader.h"
#include "data/clump_store.h"
#include "data/halo_store.h"
#include "FileIO/file_io.h"
#include "object.h"
#include "make_2D_projection_map.h"
#include "FindClumps/find_clumps_helpers.h"

#ifdef GEOMETRICAL_ANALYSIS
#include "GeometricAnalysis/DiskRadius.hpp"
#include "GeometricAnalysis/ellipse_fitter.h"

void ExecuteSingleDiskAnalysisRequest(ParticleArray& particles,
				      NormalizationContext& normalization,
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

  param.G         = particles.units.grav_const_internal;
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

void ExecuteDiskBatchRequest(ParticleArray& particles,
			     NormalizationContext& normalization,
			     const InputFilterConfig& filter,
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

    auto& src = fileInfo.editSource();
    int snap_init = static_cast<int>(r.snap / src.skipStep) * src.skipStep;
    int snap_disk = -1, snap_not_disk = snap_init;

    for (int istep = 0; istep < 100; ++istep) {
      int snap = snap_init + src.skipStep * istep;
      fileInfo.loadNewSnapshot(snap, &particles, normalization, filter);

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

        param.G         = particles.units.grav_const_internal;
        param.max_shell = 100;
        param.scale_fac = normalization.toPhysicalScale();

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
      double scale_fac = normalization.toPhysicalScale();

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
				  NormalizationContext& normalization,
				  const InputFilterConfig& filter,
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

    fileInfo.loadNewSnapshot(r.snap, &particles, normalization, filter);
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
				  const NormalizationContext& normalization,
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

  particles.computeStellarDensity(sel, request.overwriteHsml, normalization);
  particles.particlesDirty = true;
}

#ifdef CLUMP_DATA_READ
#include "FindClumps/find_clumps.h" 
void ExecuteClumpBatchRequest(ParticleArray& particles,
			      NormalizationContext& normalization,
			      const InputFilterConfig& filter,
                              FileInfo& fileInfo,
                              FindClump& clumpFind,
                              ClumpBatchRequestState& request,
                              ClumpBatchResultState& result)
{
  if (!request.runRequested) {
    return;
  }
  request.runRequested = false;

  result = ClumpBatchResultState{};

  char filename[512];
  std::snprintf(filename, sizeof(filename), "%s/%s",
                request.outputFolderPath,
                request.outputFileName);

  std::snprintf(result.outputPath, sizeof(result.outputPath), "%s", filename);

  auto& src = fileInfo.editSource();
  const int savedStep = src.currentStep;

  clumpFind.initialize_prev_nodes();

  for (int i = 0; i < request.nSnapshots; ++i) {
    src.currentStep = savedStep + i;

    const int newFileIndex =
      src.initialIndex + src.currentStep * src.skipStep;

    fileInfo.loadNewSnapshot(newFileIndex, &particles, normalization, filter);

    if (particles.particleBlock.particles.empty()) {
      continue;
    }

    clumpFind.do_FOF_and_output_clump_data(request.method,
                                           particles.particleBlock.particles,
                                           particles.particleBlock.header,
                                           filename,
                                           newFileIndex);

    result.processedSnapshots++;
  }

  src.currentStep = savedStep;
  src.currentFileIndex =
    src.initialIndex + src.currentStep * src.skipStep;

  const int initstep = src.currentFileIndex;
  const int dstep = src.skipStep;
  give_stellar_id_to_clumps(initstep,
			    request.nSnapshots,
			    dstep,
			    std::string(filename));

  result.completed = true;
}
#endif

void ExecuteProjectionMovieRequest(ParticleArray& particles,
				   NormalizationContext& normalization,
				   const InputFilterConfig& filter,
				   TrackingTargetState& track,
                                   FileInfo& fileInfo,
                                   ProjectionMapGenerator& projectionMap,
                                   const CameraContext& camera,
                                   ProjectionMovieRequestState& request,
                                   ProjectionMovieResultState& result)
{
  if (!request.runRequested) {
    return;
  }
  request.runRequested = false;

  result = ProjectionMovieResultState{};

  namespace fs = std::filesystem;
  const fs::path dir = "ffmpeg_frames";

  try {
    auto ensure_dir = [](const fs::path& p) {
      if (fs::exists(p)) {
        if (!fs::is_directory(p)) {
          throw fs::filesystem_error("Path exists but is not a directory", p,
                                     std::make_error_code(std::errc::not_a_directory));
        }
      } else {
        fs::create_directories(p);
      }
    };

    ensure_dir(dir);
    ensure_dir(request.outputFolderPath);

    auto& src = fileInfo.editSource();
    const int savedStep = src.currentStep;
    int count_i = 0;

    for (int i = 0; i < request.nSnapshots; ++i) {
      src.currentStep = savedStep + i;

      const int newFileIndex =
        src.initialIndex + src.currentStep * src.skipStep;

      fileInfo.loadNewSnapshot(newFileIndex, &particles, normalization, filter);
      if (particles.particleBlock.particles.empty()) {
        continue;
      }

      char filename_format[512];
      std::snprintf(filename_format, sizeof(filename_format), "%s/%s",
                    request.outputFolderPath,
                    request.outputFileFormat);

      char filename[512];
      std::snprintf(filename, sizeof(filename), filename_format, newFileIndex);

      int flag_use_amvector = (i == 0 && request.faceOn) ? 1 : 0;

      int flag_center = 0;
      float pos_center[3] = {
        camera.cameraTarget[0],
        camera.cameraTarget[1],
        camera.cameraTarget[2]
      };

#ifdef CLUMP_DATA_READ
      if (track.followClump)
        flag_center = 1;
#endif
      if (track.followParticle)
        flag_center = 1;

      if (request.followSinkCenter) {
        double pos_init[3] = {0., 0., 0.};
        bool found = false;

        if (!request.followMostMassiveSink) {
          for (const auto& p : particles.particleBlock.particles) {
            if (p.ID == request.particleIdCenter) {
              pos_init[0] = p.pos[0];
              pos_init[1] = p.pos[1];
              pos_init[2] = p.pos[2];
              found = true;
              break;
            }
          }
        }

        if (request.followMostMassiveSink || !found) {
          double mass_max = 0.0;
          for (const auto& p : particles.particleBlock.particles) {
            if (p.type < 3) continue;
            if (p.mass > mass_max) {
              pos_init[0] = p.pos[0];
              pos_init[1] = p.pos[1];
              pos_init[2] = p.pos[2];
              mass_max = p.mass;
              found = true;
            }
          }
        }

        if (found) {
          pos_center[0] = pos_init[0];
          pos_center[1] = pos_init[1];
          pos_center[2] = pos_init[2];
          flag_center = 1;
        }

        if (found && request.useMassCenter) {
          double pos_temp[3] = {0., 0., 0.};
          double weight = 0.0;

          for (const auto& p : particles.particleBlock.particles) {
            if (p.type == 1 || p.type == 2) continue;
            if (p.type == 0 && p.density < request.massCenterMinDensity) continue;

            const double dx = pos_init[0] - p.pos[0];
            const double dy = pos_init[1] - p.pos[1];
            const double dz = pos_init[2] - p.pos[2];
            const double dist2 = dx*dx + dy*dy + dz*dz;

            if (dist2 > request.massCenterRadius * request.massCenterRadius) continue;

            pos_temp[0] += p.mass * p.pos[0];
            pos_temp[1] += p.mass * p.pos[1];
            pos_temp[2] += p.mass * p.pos[2];
            weight += p.mass;
          }

          if (weight > 0.0) {
            pos_center[0] = pos_temp[0] / weight;
            pos_center[1] = pos_temp[1] / weight;
            pos_center[2] = pos_temp[2] / weight;
            flag_center = 1;
          }
        }
      }

      projectionMap.set_projection_parameters(particles.particleBlock.particles,
                                              flag_use_amvector,
                                              flag_center ? pos_center : nullptr,
                                              -1.0f,
                                              std::numeric_limits<float>::quiet_NaN(),
                                              std::numeric_limits<float>::quiet_NaN(),
                                              -1, -1, "");

      projectionMap.make_density_map(&particles, filename);

      char linkname[512];
      std::snprintf(linkname, sizeof(linkname), "ffmpeg_frames/frame_%04d.png", count_i++);
      fs::remove(linkname);
      fs::create_symlink(fs::absolute(filename), linkname);

      result.processedSnapshots++;
    }

    std::string ffmpegCommand =
      "ffmpeg -y -framerate 30 -i ffmpeg_frames/frame_%04d.png "
      "-vf \"scale=ceil(iw/2)*2:ceil(ih/2)*2\" "
      "-c:v libx264 -pix_fmt yuv420p " +
      std::string(request.outputFolderPath) + "/" + std::string(request.outputMovieName);

    std::system(ffmpegCommand.c_str());
    fs::remove_all("ffmpeg_frames");

    src.currentStep = savedStep;
    src.currentFileIndex =
      src.initialIndex + src.currentStep * src.skipStep;

    std::snprintf(result.outputMoviePath, sizeof(result.outputMoviePath),
                  "%s/%s", request.outputFolderPath, request.outputMovieName);

    result.completed = true;
  }
  catch (const std::exception& e) {
    std::snprintf(result.errorMessage, sizeof(result.errorMessage), "%s", e.what());
  }
}

void ExecuteFileNavigationRequests(FileInfo& fileInfo,
				   ParticleArray& particles,
				   NormalizationContext& normalization,
				   const InputFilterConfig& filter,
				   FileNavigationRuntimeState& rt)
{
  auto& req = rt.request;
  auto& src = fileInfo.editSource();
  
  if (req.applySkipStepRequested) {
    if (rt.tempSkipStep > 0) {
      int newStep = std::round(
        (src.currentFileIndex - src.initialIndex) /
        static_cast<float>(rt.tempSkipStep)
      );
      src.currentStep = std::max(0, newStep);
      src.skipStep = rt.tempSkipStep;

      int newFileIndex =
        src.initialIndex + src.currentStep * src.skipStep;

      if (!fileInfo.isLoading()) {
	fileInfo.loadNewSnapshot(newFileIndex, &particles, normalization, filter);
      }

      src.currentFileIndex = newFileIndex;
    }

    req.applySkipStepRequested = false;
  }

  if (req.loadSelectedSnapshotRequested) {
    int newFileIndex =
      src.initialIndex + src.currentStep * src.skipStep;
    fileInfo.loadNewSnapshot(newFileIndex, &particles, normalization, filter);
    req.loadSelectedSnapshotRequested = false;
  }

  if (req.loadPreviousRequested) {
    if (src.currentStep > 0) {
      src.currentStep--;
      int newFileIndex =
        src.initialIndex + src.currentStep * src.skipStep;
      fileInfo.loadNewSnapshot(newFileIndex, &particles, normalization, filter);
    }
    req.loadPreviousRequested = false;
  }

  if (req.loadNextRequested) {
    src.currentStep++;
    int newFileIndex =
      src.initialIndex + src.currentStep * src.skipStep;
    fileInfo.loadNewSnapshot(newFileIndex, &particles, normalization, filter);
    req.loadNextRequested = false;
  }

  if (req.loadBatchRequested) {
    if (!fileInfo.isLoading()) {
      int newFileIndex =
        src.initialIndex + src.currentStep * src.skipStep;
      fileInfo.loadNewSnapshot(newFileIndex, &particles, normalization, filter);
      src.currentFileIndex = newFileIndex;
    }
    req.loadBatchRequested = false;
  }

  if (req.reloadRequested) {
    if (!fileInfo.isLoading()) {
      int newFileIndex =
        src.initialIndex + src.currentStep * src.skipStep;
      fileInfo.loadNewSnapshot(newFileIndex, &particles, normalization, filter);
      src.currentFileIndex = newFileIndex;
    }
    req.reloadRequested = false;
  }

  if (req.generateTestDataRequested) {
    fileInfo.generateTestData(&particles, normalization);
    req.generateTestDataRequested = false;
  }
}

void ExecuteCameraPlacementRequests(ParticleArray& particles,
				    const NormalizationContext& normalization,
				    ViewFilterConfig& viewFilter,
				    CameraContext& camCtx,
				    SettingsRuntimeState& rt)
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
}


void ExecutePostSnapshotLoadActions(ParticleArray& particles,
				    ClumpStore& clumpStore,
				    NormalizationContext& normalization,
				    TrackingTargetState& track,
				    CameraContext& camCtx,
				    int currentFileIndex)
{
#ifdef CLUMP_DATA_READ
  if (track.followClump) {    
    ClumpData targetClump = clumpStore.clump(track.targetClumpID);

    TrackingVector<ClumpData> newClumps =
      loadClumpData(clumpStore.filePath().c_str(),
		    currentFileIndex,
		    normalization.toNormalizedScale());
    
    if (newClumps.empty()) {
      track.followClump = false;
    } else {
      clumpStore.setClumps(std::move(newClumps));

      float target_pos_new[3];
      targetClump.get_next_clump_position(clumpStore.clumps(), target_pos_new);

      float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
      glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
      camCtx.cameraTarget = glm::vec3(target_pos_new[0], target_pos_new[1], target_pos_new[2]);
      camCtx.cameraPos = camCtx.cameraTarget - direction * dist;
    }
  }
#endif

  if (track.followParticle) {
    float target_pos_new[3];
    bool found = particles.findParticleID(track.targetParticleID, target_pos_new);

    if (!found) {
      track.followParticle = false;
    } else {
      float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
      glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
      camCtx.cameraTarget = glm::vec3(target_pos_new[0], target_pos_new[1], target_pos_new[2]);
      camCtx.cameraPos = camCtx.cameraTarget - direction * dist;
    }
  }
}

void ExecuteHaloesUIRequests(HaloesUIState& state,
                             HaloStore& haloes,
                             ParticleArray& particles)
{
  if (state.requestRecomputePositions) {
    haloes.recomputeHaloPositionsFromParticles(
      particles.particleBlock.particles,
      state.recomputeUseMassWeight,
      state.recomputeUseOriginalPos
    );
    state.requestRecomputePositions = false;
  }

  if (state.stressSelectionDirty) {
    for (auto& p : particles.particleBlock.particles) {
      p.flag_stress = 0;
    }

    if (haloes.idsLoaded()) {
      const int n = static_cast<int>(std::min(haloes.size(), state.selectedForStress.size()));
      for (int i = 0; i < n; ++i) {
        if (!state.selectedForStress[i]) continue;
        particles.ApplyIDStress(haloes.ids(i));
      }
    }

    particles.particlesDirty = true;
    state.stressSelectionDirty = false;
  }
}
