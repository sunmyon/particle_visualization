#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "FileIO/snapshot_io_service.h"
#include "app/app_snapshot_load.h"
#include "app/execution/analysis_execution.h"
#include "app/execution/clump_batch_execution.h"
#include "app/execution/profile_histogram_execution.h"
#include "app/execution/projection_execution.h"
#include "app/app_services.h"
#include "app/state/app_state.h"
#include "app/state/snapshot_state_sync.h"
#include "config/config_apply.h"
#include "config/config_data.h"
#include "config/config_io.h"
#include "config/config_validation.h"
#include "data/particle_array.h"
#include "interaction/camera.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_tool_state.h"

#ifdef CLUMP_DATA_READ
#include "FindClumps/find_clumps.h"
#endif

#ifdef GEOMETRICAL_ANALYSIS
#include "GeometricAnalysis/DiskRadius.hpp"
#include "GeometricAnalysis/ellipse_fitter.h"
#endif

using json = nlohmann::json;

namespace {

void CopyCStr(char* dst, std::size_t dstSize, const std::string& src)
{
  if (!dst || dstSize == 0) return;
  std::snprintf(dst, dstSize, "%s", src.c_str());
}

bool ReadJsonFile(const char* path, json& out)
{
  std::ifstream in(path);
  if (!in) {
    std::cerr << "failed to open job file: " << path << "\n";
    return false;
  }
  try {
    in >> out;
  } catch (const std::exception& e) {
    std::cerr << "failed to parse job json: " << e.what() << "\n";
    return false;
  }
  return true;
}

void ApplyJsonVec3(const json& obj, const char* key, float out[3])
{
  if (!obj.contains(key)) return;
  const auto& v = obj.at(key);
  if (!v.is_array() || v.size() != 3) {
    throw std::runtime_error(std::string(key) + " must be an array of 3 numbers");
  }
  for (int i = 0; i < 3; ++i) {
    out[i] = v.at(static_cast<size_t>(i)).get<float>();
  }
}

DataSource ParseDataSource(const std::string& value)
{
  if (value == "gas" || value == "Gas" || value == "GAS") return DataSource::Gas;
  if (value == "dm" || value == "DM") return DataSource::DM;
  if (value == "stars" || value == "Stars" || value == "star") return DataSource::Stars;
  throw std::runtime_error("unknown projection.dataSource: " + value);
}

QuantityId ParseQuantity(const json& value)
{
  if (value.is_number_integer()) {
    const int i = value.get<int>();
    if (i >= 0 && i < kMaxQ) return static_cast<QuantityId>(i);
    throw std::runtime_error("quantity index out of range: " + std::to_string(i));
  }

  const std::string name = value.get<std::string>();
  for (int i = 0; i < kMaxQ; ++i) {
    const QuantityId q = static_cast<QuantityId>(i);
    if (name == QuantityLabel(q)) return q;
  }

  throw std::runtime_error("unknown quantity: " + name);
}

XAxisMode ParseXAxisMode(const json& value)
{
  if (value.is_number_integer()) {
    const int i = value.get<int>();
    if (i >= 0 && i <= static_cast<int>(XAxisMode::EnclosedMass)) {
      return static_cast<XAxisMode>(i);
    }
    throw std::runtime_error("xmode index out of range: " + std::to_string(i));
  }

  const std::string name = value.get<std::string>();
  if (name == "radius" || name == "r") return XAxisMode::Radius;
  if (name == "x" || name == "posx") return XAxisMode::PosX;
  if (name == "y" || name == "posy") return XAxisMode::PosY;
  if (name == "z" || name == "posz") return XAxisMode::PosZ;
  if (name == "enclosed_mass" || name == "mass" || name == "M(<r)") {
    return XAxisMode::EnclosedMass;
  }
  throw std::runtime_error("unknown radial.xmode: " + name);
}

void ApplyProjectionJson(const json& obj, ProjectionMapParams& params)
{
  if (obj.contains("npixel")) params.npixel = obj.at("npixel").get<int>();
  ApplyJsonVec3(obj, "xlen", params.xlen);
  ApplyJsonVec3(obj, "xoffset", params.xoffset);
  ApplyJsonVec3(obj, "tilt", params.tilt);

  if (obj.contains("fileFormat")) {
    CopyCStr(params.fileFormat, sizeof(params.fileFormat), obj.at("fileFormat").get<std::string>());
  }
  if (obj.contains("folderPath")) {
    CopyCStr(params.folderPath, sizeof(params.folderPath), obj.at("folderPath").get<std::string>());
  }
  if (obj.contains("selectedAxis")) params.selectedAxis = obj.at("selectedAxis").get<int>();
  if (obj.contains("selectedType")) params.selectedType = obj.at("selectedType").get<int>();
  if (obj.contains("dataSource")) params.dataSource = ParseDataSource(obj.at("dataSource").get<std::string>());
  if (obj.contains("densityWeight")) params.flagDensityWeight = obj.at("densityWeight").get<bool>();
  if (obj.contains("voronoi")) params.flagVoronoi = obj.at("voronoi").get<bool>();
  if (obj.contains("step_z")) params.step_z = obj.at("step_z").get<int>();
  if (obj.contains("logScale")) params.flagLogScale = obj.at("logScale").get<bool>();
  if (obj.contains("autoRange")) params.autoRange = obj.at("autoRange").get<bool>();
  if (obj.contains("rangeMin")) params.range_min = obj.at("rangeMin").get<float>();
  if (obj.contains("rangeMax")) params.range_max = obj.at("rangeMax").get<float>();
  if (obj.contains("showStarParticles")) {
    params.flagShowStarParticles = obj.at("showStarParticles").get<bool>();
  }
  if (obj.contains("showTimeLabel")) params.flagTimeLabel = obj.at("showTimeLabel").get<bool>();
  if (obj.contains("timeFormat")) {
    CopyCStr(params.timeFormatBuf, sizeof(params.timeFormatBuf), obj.at("timeFormat").get<std::string>());
  }
  if (obj.contains("timeFactor")) {
    params.factorShownTimeInUnitTime = obj.at("timeFactor").get<float>();
  }
}

void ApplySnapshotJson(const json& obj, FileNavigationRuntimeState& fileNav)
{
  auto& nav = fileNav.navigation;
  auto& input = fileNav.input;

  if (obj.contains("initialIndex")) nav.initialIndex = obj.at("initialIndex").get<int>();
  if (obj.contains("currentStep")) nav.currentStep = obj.at("currentStep").get<int>();
  if (obj.contains("skipStep")) nav.skipStep = std::max(1, obj.at("skipStep").get<int>());
  if (obj.contains("batchSize")) nav.batchSize = std::max(1, obj.at("batchSize").get<int>());
  if (obj.contains("fileFormat")) {
    CopyCStr(input.fileFormat, sizeof(input.fileFormat), obj.at("fileFormat").get<std::string>());
  }
  if (obj.contains("folderPath")) {
    CopyCStr(input.folderPath, sizeof(input.folderPath), obj.at("folderPath").get<std::string>());
  }
#ifdef HAVE_HDF5
  if (obj.contains("useHDF5")) input.useHDF5 = obj.at("useHDF5").get<bool>();
#endif
  RecomputeCurrentFileIndex(fileNav);
  RefreshSnapshotFilePath(fileNav);
}

void ApplyCameraJson(const json& obj, CameraContext& camera)
{
  float pos[3] = {camera.cameraPos.x, camera.cameraPos.y, camera.cameraPos.z};
  float target[3] = {camera.cameraTarget.x, camera.cameraTarget.y, camera.cameraTarget.z};
  ApplyJsonVec3(obj, "position", pos);
  ApplyJsonVec3(obj, "target", target);
  camera.cameraPos = glm::vec3(pos[0], pos[1], pos[2]);
  camera.cameraTarget = glm::vec3(target[0], target[1], target[2]);
}

void ApplyRadialProfileJson(const json& obj, RadialProfileParams& params)
{
  if (obj.contains("useOriginal")) params.useOriginal = obj.at("useOriginal").get<bool>();
  if (obj.contains("bins")) params.bins = obj.at("bins").get<int>();
  if (obj.contains("xmode")) params.xmode = ParseXAxisMode(obj.at("xmode"));
  if (obj.contains("isMDot")) params.isMDot = obj.at("isMDot").get<bool>();
  if (obj.contains("quantity")) params.var1 = ParseQuantity(obj.at("quantity"));
  if (obj.contains("var1")) params.var1 = ParseQuantity(obj.at("var1"));
  if (obj.contains("autoRange")) params.autorange = obj.at("autoRange").get<bool>();
  if (obj.contains("absolute")) params.flagAbsolute = obj.at("absolute").get<bool>();
  if (obj.contains("logX")) params.plotXAxisLog = obj.at("logX").get<bool>();
  if (obj.contains("logY")) params.plotYAxisLog = obj.at("logY").get<bool>();
  if (obj.contains("xmin")) params.xmin = obj.at("xmin").get<float>();
  if (obj.contains("xmax")) params.xmax = obj.at("xmax").get<float>();
  if (obj.contains("ymin")) params.ymin = obj.at("ymin").get<float>();
  if (obj.contains("ymax")) params.ymax = obj.at("ymax").get<float>();
  if (obj.contains("rmax")) params.rmax = obj.at("rmax").get<float>();
}

void ApplyHistogram2DJson(const json& obj, Histogram2DParams& params)
{
  if (obj.contains("var1")) params.var1 = ParseQuantity(obj.at("var1"));
  if (obj.contains("var2")) params.var2 = ParseQuantity(obj.at("var2"));
  if (obj.contains("bins1")) params.bins1 = obj.at("bins1").get<int>();
  if (obj.contains("bins2")) params.bins2 = obj.at("bins2").get<int>();
  if (obj.contains("bins")) {
    params.bins1 = obj.at("bins").get<int>();
    params.bins2 = params.bins1;
  }
  if (obj.contains("autoRange")) params.autoRange = obj.at("autoRange").get<bool>();
  if (obj.contains("range1Min")) params.range1_min = obj.at("range1Min").get<float>();
  if (obj.contains("range1Max")) params.range1_max = obj.at("range1Max").get<float>();
  if (obj.contains("range2Min")) params.range2_min = obj.at("range2Min").get<float>();
  if (obj.contains("range2Max")) params.range2_max = obj.at("range2Max").get<float>();
  if (obj.contains("logX")) params.logScaleX = obj.at("logX").get<bool>();
  if (obj.contains("logY")) params.logScaleY = obj.at("logY").get<bool>();
  if (obj.contains("logColor")) params.logScaleColor = obj.at("logColor").get<bool>();
  if (obj.contains("useCameraCenter")) params.useCameraCenter = obj.at("useCameraCenter").get<bool>();
  if (obj.contains("cameraRadius")) params.cameraRadius = obj.at("cameraRadius").get<float>();
#ifdef USE_CONVEX_HULL
  if (obj.contains("useConvexHull")) params.useConvexHull = obj.at("useConvexHull").get<bool>();
#endif
}

#ifdef GEOMETRICAL_ANALYSIS
void ApplyDiskJson(const json& obj, DiskAnalysisRequestState& request)
{
  if (obj.contains("targetParticleId")) {
    request.targetParticleId = obj.at("targetParticleId").get<int>();
  }
  if (obj.contains("rejectTypeZeroTarget")) {
    request.rejectTypeZeroTarget = obj.at("rejectTypeZeroTarget").get<bool>();
  }
  if (obj.contains("diskOpacity")) request.diskOpacity = obj.at("diskOpacity").get<float>();
  if (obj.contains("diskTag")) {
    CopyCStr(request.diskTag, sizeof(request.diskTag), obj.at("diskTag").get<std::string>());
  }
}

void ApplyDiskBatchJson(const json& obj, DiskAnalysisBatchRequestState& request)
{
  if (obj.contains("inputFile")) {
    CopyCStr(request.inputFile, sizeof(request.inputFile), obj.at("inputFile").get<std::string>());
  }
  if (obj.contains("outputFile")) {
    CopyCStr(request.outputFile, sizeof(request.outputFile), obj.at("outputFile").get<std::string>());
  }
}
#endif

#ifdef CLUMP_DATA_READ
void ApplyClumpJson(const json& obj, ClumpRequestState& request)
{
  if (obj.contains("method")) request.method = obj.at("method").get<int>();
  if (obj.contains("outputPath")) {
    CopyCStr(request.outputPath, sizeof(request.outputPath), obj.at("outputPath").get<std::string>());
  }
}

void ApplyClumpBatchJson(const json& obj, ClumpBatchRequestState& request)
{
  if (obj.contains("method")) request.method = obj.at("method").get<int>();
  if (obj.contains("nSnapshots")) request.nSnapshots = obj.at("nSnapshots").get<int>();
  if (obj.contains("outputFileName")) {
    CopyCStr(request.outputFileName, sizeof(request.outputFileName), obj.at("outputFileName").get<std::string>());
  }
  if (obj.contains("outputFolderPath")) {
    CopyCStr(request.outputFolderPath, sizeof(request.outputFolderPath), obj.at("outputFolderPath").get<std::string>());
  }
}
#endif

bool WriteRadialProfileCsv(const std::string& path, const RadialProfileResult& result)
{
  std::ofstream out(path);
  if (!out) return false;
  out << "# x y\n";
  const size_t n = std::min(result.x.size(), result.y.size());
  for (size_t i = 0; i < n; ++i) {
    out << result.x[i] << ' ' << result.y[i] << '\n';
  }
  return true;
}

bool WriteHistogram2DCsv(const std::string& path, const Histogram2DResult& result)
{
  std::ofstream out(path);
  if (!out) return false;
  out << "# x_center y_center value\n";
  for (size_t i = 0; i < result.centers1.size(); ++i) {
    for (size_t j = 0; j < result.centers2.size(); ++j) {
      float value = 0.0f;
      if (i < result.values.size() && j < result.values[i].size()) {
        value = result.values[i][j];
      }
      out << result.centers1[i] << ' ' << result.centers2[j] << ' ' << value << '\n';
    }
  }
  return true;
}

void InitCoreBatchApp(AppState& app)
{
  app.services.snapshotIO = std::make_unique<SnapshotIOService>();
  app.services.projectionMap2D = std::make_unique<ProjectionMapGenerator>();
  app.data.particles = new ParticleArray();

#ifdef GEOMETRICAL_ANALYSIS
  app.services.diskFinder = std::make_unique<DiskRadiusFinder>();
  app.services.ellipsoid = std::make_unique<EllipseFitter>();
#endif

#ifdef CLUMP_DATA_READ
  app.services.clumpFind = std::make_unique<FindClump>();
#endif

  ConfigData config;
  if (LoadConfigFile("config.txt", config)) {
    ConfigValidationIssues issues;
    SanitizeConfigData(config, &issues);
    PrintConfigValidationIssues(issues);
    ApplyConfigData(config,
                    app.runtime.settings.fileNavigation,
                    app.runtime.settings.snapshotFormat,
                    app.runtime.quantity.units,
                    app.runtime.settings.normalization.desiredMax,
                    app.runtime.particleVisual,
                    app.runtime.settings.inputFilter.mask);
  }

  app.runtime.quantity.units.updateDerived();
  app.runtime.quantity.conversion.displaySpace =
    app.runtime.quantity.units.useComovingCoordinate
      ? UnitSpace::Comoving
      : UnitSpace::Physical;
  app.runtime.quantity.rebuildConversion(1.0);
}

void CleanupCoreBatchApp(AppState& app)
{
  delete app.data.particles;
  app.data.particles = nullptr;
}

bool LoadSnapshotStep(AppState& app, int step, SnapshotLoadOwner owner)
{
  RequestSnapshotLoad(app.runtime.snapshotLoad, owner, step, 100);
  for (int i = 0; i < 1000; ++i) {
    ProcessSnapshotLoadQueue(app.data, app.runtime, app.services);
    if (app.runtime.snapshotLoad.result.loadedThisFrame) {
      return true;
    }
    if (app.runtime.snapshotLoad.result.failedThisFrame) {
      std::cerr << "snapshot load failed: "
                << app.runtime.snapshotLoad.result.errorMessage << "\n";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::cerr << "snapshot load timed out\n";
  return false;
}

bool RunProjectionMapJob(AppState& app, CameraContext& camera)
{
  const auto& nav = app.runtime.settings.fileNavigation.navigation;
  if (!LoadSnapshotStep(app, nav.currentStep, SnapshotLoadOwner::UserNavigation)) {
    return false;
  }

  ProjectionFrameExecutionContext frameCtx{
    *app.data.particles,
    *app.services.projectionMap2D,
    app.runtime.quantity.units,
    app.runtime.settings.normalization.toPhysicalScale()
  };

  ProjectionMapRequestState request;
  request.params = app.runtime.analysisTools.projectionMap.params;
  request.paramsChanged = true;
  request.renderRequested = true;

  ProjectionMapExecutionContext execCtx{
    frameCtx,
    camera,
    app.runtime.settings.fileNavigation.current.loadedFileIndex,
    app.runtime.settings.fileNavigation.current.loadedTime,
    nullptr,
    nullptr,
    nullptr
  };

  ProjectionFrameResult result = ExecuteProjectionMapRequests(request, execCtx);
  if (!result.ok) {
    std::cerr << result.error << "\n";
    return false;
  }
  std::cout << "wrote " << result.outputPath << "\n";
  return true;
}

bool RunProjectionMovieJob(AppState& app, CameraContext& camera, const json& job)
{
  auto& request = app.runtime.analysisRequests.projectionMovie;
  if (job.contains("nSnapshots")) request.nSnapshots = job.at("nSnapshots").get<int>();
  if (job.contains("outputFileFormat")) {
    CopyCStr(request.outputFileFormat, sizeof(request.outputFileFormat), job.at("outputFileFormat").get<std::string>());
  }
  if (job.contains("outputFolderPath")) {
    CopyCStr(request.outputFolderPath, sizeof(request.outputFolderPath), job.at("outputFolderPath").get<std::string>());
  }
  if (job.contains("outputMovieName")) {
    CopyCStr(request.outputMovieName, sizeof(request.outputMovieName), job.at("outputMovieName").get<std::string>());
  }
  if (job.contains("followSinkCenter")) request.followSinkCenter = job.at("followSinkCenter").get<bool>();
  if (job.contains("followMostMassiveSink")) {
    request.followMostMassiveSink = job.at("followMostMassiveSink").get<bool>();
  }
  if (job.contains("particleIdCenter")) request.particleIdCenter = job.at("particleIdCenter").get<int>();
  if (job.contains("restoreCameraOnFinish")) {
    request.restoreCameraOnFinish = job.at("restoreCameraOnFinish").get<bool>();
  }
  request.runRequested = true;

  const int maxIterations = std::max(1000, request.nSnapshots * 1000);
  for (int i = 0; i < maxIterations; ++i) {
    ProcessSnapshotLoadQueue(app.data, app.runtime, app.services);

    ProjectionFrameExecutionContext frameCtx{
      *app.data.particles,
      *app.services.projectionMap2D,
      app.runtime.quantity.units,
      app.runtime.settings.normalization.toPhysicalScale()
    };

    ProjectionMovieExecutionContext movieCtx{
      frameCtx,
      app.runtime.settings.tracking,
      app.runtime.settings.fileNavigation,
      app.runtime.analysisTools.projectionMap.params,
      camera,
      app.runtime.analysisRequests.projectionMovie,
      app.runtime.analysisJobs.projectionMovie,
      app.runtime.snapshotLoad,
      app.runtime.snapshotPostprocess,
      app.derived.analysis.projectionMovie
    };
    ExecuteProjectionMovieRequest(movieCtx);

    if (app.derived.analysis.projectionMovie.completed) {
      if (!app.derived.analysis.projectionMovie.success) {
        std::cerr << app.derived.analysis.projectionMovie.errorMessage << "\n";
      } else {
        std::cout << "wrote "
                  << app.derived.analysis.projectionMovie.outputMoviePath << "\n";
      }
      return app.derived.analysis.projectionMovie.success;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cerr << "projection movie job timed out\n";
  return false;
}

bool RunRadialProfileJob(AppState& app, CameraContext& camera, const json& job)
{
  const auto& nav = app.runtime.settings.fileNavigation.navigation;
  if (!LoadSnapshotStep(app, nav.currentStep, SnapshotLoadOwner::UserNavigation)) {
    return false;
  }

  RadialProfileRequestState request;
  ApplyRadialProfileJson(job, request.params);
  request.runRequested = true;

  RadialProfileResultState result;
  ExecuteRadialProfileRequest(request,
                              result,
                              app.data.particles->particleBlock,
                              camera.cameraTarget,
                              app.runtime.settings.normalization,
                              app.runtime.quantity);
  if (!result.computed || !result.result.valid) {
    std::cerr << "radial profile failed\n";
    return false;
  }

  const std::string outputFile = job.value("outputFile", "radial_profile.txt");
  if (!WriteRadialProfileCsv(outputFile, result.result)) {
    std::cerr << "failed to write radial profile: " << outputFile << "\n";
    return false;
  }

  std::cout << "wrote " << outputFile << "\n";
  return true;
}

bool RunHistogram2DJob(AppState& app, CameraContext& camera, const json& job)
{
  const auto& nav = app.runtime.settings.fileNavigation.navigation;
  if (!LoadSnapshotStep(app, nav.currentStep, SnapshotLoadOwner::UserNavigation)) {
    return false;
  }

  Histogram2DRequestState request;
  ApplyHistogram2DJson(job, request.params);
  request.runRequested = true;

  Histogram2DContext ctx;
  ctx.cameraCenter = &camera.cameraTarget;

  Histogram2DResultState result;
  ExecuteHistogram2DRequest(request,
                            result,
                            app.data.particles->particleBlock,
                            ctx);
  if (!result.computed || !result.result.valid) {
    std::cerr << "2D histogram failed\n";
    return false;
  }

  const std::string outputFile = job.value("outputFile", "histogram2d.txt");
  if (!WriteHistogram2DCsv(outputFile, result.result)) {
    std::cerr << "failed to write 2D histogram: " << outputFile << "\n";
    return false;
  }

  std::cout << "wrote " << outputFile << "\n";
  return true;
}

#ifdef GEOMETRICAL_ANALYSIS
bool RunDiskJob(AppState& app, const json& job)
{
  const auto& nav = app.runtime.settings.fileNavigation.navigation;
  if (!LoadSnapshotStep(app, nav.currentStep, SnapshotLoadOwner::UserNavigation)) {
    return false;
  }

  DiskAnalysisRequestState request;
  ApplyDiskJson(job, request);
  request.runRequested = true;

  DiskAnalysisResultState result;
  ExecuteSingleDiskAnalysisRequest(*app.data.particles,
                                   app.runtime.settings.normalization,
                                   *app.services.diskFinder,
                                   request,
                                   result,
                                   app.runtime.quantity.units);
  if (!result.valid) {
    std::cerr << "disk analysis failed for particle "
              << request.targetParticleId << "\n";
    return false;
  }

  const std::string outputFile = job.value("outputFile", "disk_analysis.txt");
  std::ofstream out(outputFile);
  if (!out) {
    std::cerr << "failed to write disk analysis: " << outputFile << "\n";
    return false;
  }
  out << "# particle_id radius center_x center_y center_z\n";
  out << result.targetParticleId << ' '
      << result.radius << ' '
      << result.disk.position.x << ' '
      << result.disk.position.y << ' '
      << result.disk.position.z << '\n';

  std::cout << "wrote " << outputFile << "\n";
  return true;
}

bool RunDiskBatchJob(AppState& app, const json& job)
{
  auto& request = app.runtime.analysisRequests.diskBatch;
  ApplyDiskBatchJson(job, request);
  request.runRequested = true;

  const int maxIterations = 100000;
  for (int i = 0; i < maxIterations; ++i) {
    ProcessSnapshotLoadQueue(app.data, app.runtime, app.services);
    ExecuteDiskBatchRequest(*app.data.particles,
                            app.runtime.settings.normalization,
                            app.runtime.settings.fileNavigation,
                            app.runtime.snapshotLoad,
                            *app.services.diskFinder,
                            app.runtime.render.disks,
                            request,
                            app.runtime.analysisJobs.diskBatch,
                            app.derived.analysis.diskBatch,
                            app.runtime.quantity.units);

    if (app.derived.analysis.diskBatch.completed) {
      if (!app.derived.analysis.diskBatch.success) {
        std::cerr << "disk batch failed\n";
      } else {
        std::cout << "wrote "
                  << app.derived.analysis.diskBatch.lastOutputFile << "\n";
      }
      return app.derived.analysis.diskBatch.success;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cerr << "disk batch job timed out\n";
  return false;
}
#endif

#ifdef CLUMP_DATA_READ
bool RunClumpJob(AppState& app, const json& job)
{
  const auto& nav = app.runtime.settings.fileNavigation.navigation;
  if (!LoadSnapshotStep(app, nav.currentStep, SnapshotLoadOwner::UserNavigation)) {
    return false;
  }

  ClumpRequestState request;
  request.snapshotIndex = app.runtime.settings.fileNavigation.current.loadedFileIndex;
  request.snapshotTime = app.runtime.settings.fileNavigation.current.loadedTime;
  ApplyClumpJson(job, request);
  request.runRequested = true;

  if (!ExecuteClumpRequest(*app.data.particles, *app.services.clumpFind, request)) {
    std::cerr << "clump analysis failed\n";
    return false;
  }

  std::cout << "wrote " << request.outputPath << "\n";
  return true;
}

bool RunClumpBatchJob(AppState& app, const json& job)
{
  auto& request = app.runtime.analysisRequests.clumpBatch;
  ApplyClumpBatchJson(job, request);
  request.runRequested = true;

  const int maxIterations = std::max(1000, request.nSnapshots * 1000);
  for (int i = 0; i < maxIterations; ++i) {
    ProcessSnapshotLoadQueue(app.data, app.runtime, app.services);
    ExecuteClumpBatchRequest(*app.data.particles,
                             app.runtime.settings.fileNavigation,
                             app.runtime.snapshotLoad,
                             *app.services.clumpFind,
                             request,
                             app.runtime.analysisJobs.clumpBatch,
                             app.derived.analysis.clumpBatch);

    if (app.derived.analysis.clumpBatch.completed) {
      if (app.derived.analysis.clumpBatch.errorMessage[0] != '\0') {
        std::cerr << app.derived.analysis.clumpBatch.errorMessage << "\n";
        return false;
      }
      std::cout << "wrote " << app.derived.analysis.clumpBatch.outputPath << "\n";
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cerr << "clump batch job timed out\n";
  return false;
}
#endif

bool RunJob(const json& root)
{
  AppState app;
  CameraContext camera;
  InitCoreBatchApp(app);

  try {
    if (root.contains("snapshot")) {
      ApplySnapshotJson(root.at("snapshot"), app.runtime.settings.fileNavigation);
    }
    if (root.contains("projection")) {
      ApplyProjectionJson(root.at("projection"), app.runtime.analysisTools.projectionMap.params);
    }
    if (root.contains("camera")) {
      ApplyCameraJson(root.at("camera"), camera);
    }

    const std::string type = root.value("type", "projection_map");
    bool ok = false;
    if (type == "projection_map") {
      ok = RunProjectionMapJob(app, camera);
    } else if (type == "projection_movie") {
      ok = RunProjectionMovieJob(app, camera, root.value("movie", json::object()));
    } else if (type == "radial_profile") {
      ok = RunRadialProfileJob(app, camera, root.value("radial", json::object()));
    } else if (type == "histogram2d") {
      ok = RunHistogram2DJob(app, camera, root.value("histogram", json::object()));
    } else if (type == "disk") {
#ifdef GEOMETRICAL_ANALYSIS
      ok = RunDiskJob(app, root.value("disk", json::object()));
#else
      std::cerr << "disk batch support requires GEOMETRICAL_ANALYSIS=ON\n";
      ok = false;
#endif
    } else if (type == "disk_batch") {
#ifdef GEOMETRICAL_ANALYSIS
      ok = RunDiskBatchJob(app, root.value("diskBatch", json::object()));
#else
      std::cerr << "disk batch support requires GEOMETRICAL_ANALYSIS=ON\n";
      ok = false;
#endif
    } else if (type == "clump") {
#ifdef CLUMP_DATA_READ
      ok = RunClumpJob(app, root.value("clump", json::object()));
#else
      std::cerr << "clump batch support requires CLUMP_DATA_READ=ON\n";
      ok = false;
#endif
    } else if (type == "clump_batch") {
#ifdef CLUMP_DATA_READ
      ok = RunClumpBatchJob(app, root.value("clumpBatch", json::object()));
#else
      std::cerr << "clump batch support requires CLUMP_DATA_READ=ON\n";
      ok = false;
#endif
    } else {
      std::cerr << "unknown batch job type: " << type << "\n";
      ok = false;
    }

    CleanupCoreBatchApp(app);
    return ok;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    CleanupCoreBatchApp(app);
    return false;
  }
}

} // namespace

int main(int argc, char** argv)
{
  if (argc != 2) {
    std::cerr << "usage: particle_vis_batch job.json\n";
    return EXIT_FAILURE;
  }

  json job;
  if (!ReadJsonFile(argv[1], job)) {
    return EXIT_FAILURE;
  }

  return RunJob(job) ? EXIT_SUCCESS : EXIT_FAILURE;
}
