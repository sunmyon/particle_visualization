#include "app/volume_gpu_batch.h"

#ifdef VOLUME_RENDERING

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "app/app_lifecycle.h"
#include "app/app_snapshot_load.h"
#include "app/execution/analysis_execution.h"
#include "app/state/app_state.h"
#include "app/state/snapshot_state_sync.h"
#include "config/config_apply.h"
#include "config/config_data.h"
#include "data/sample_coordinates.h"
#include "config/config_io.h"
#include "config/config_validation.h"
#include "core/quantity.h"
#include "data/simulation_dataset.h"
#include "data/simulation_block.h"
#include "platform/imgui_context.h"
#include "platform/platform_session.h"
#include "render/render_frame.h"
#include "render/render_system.h"
#include "render/render_viewport.h"

using json = nlohmann::json;

namespace {

void SetEnv(const char* name, const std::string& value)
{
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

bool ReadJsonFile(const char* path, json& out)
{
  std::ifstream in(path);
  if (!in) {
    std::cerr << "failed to open volume gpu batch job: " << path << "\n";
    return false;
  }
  try {
    in >> out;
  } catch (const std::exception& e) {
    std::cerr << "failed to parse volume gpu batch json: " << e.what() << "\n";
    return false;
  }
  return true;
}

void CopyCStr(char* dst, std::size_t dstSize, const std::string& src)
{
  if (!dst || dstSize == 0) return;
  std::snprintf(dst, dstSize, "%s", src.c_str());
}

QuantityId ParseQuantity(const json& value)
{
  if (value.is_number_integer()) {
    const int i = value.get<int>();
    if (i >= 0 && i < kMaxQ) return static_cast<QuantityId>(i);
    throw std::runtime_error("quantity index out of range: " + std::to_string(i));
  }

  const std::string name = value.get<std::string>();
  std::string lowered = name;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  for (int i = 0; i < kMaxQ; ++i) {
    const QuantityId q = static_cast<QuantityId>(i);
    std::string label = QuantityLabel(q);
    std::transform(label.begin(), label.end(), label.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    if (lowered == label) return q;
  }
  throw std::runtime_error("unknown quantity: " + name);
}

void ApplyJsonVec3(const json& obj, const char* key, glm::vec3& out)
{
  if (!obj.contains(key)) return;
  const auto& v = obj.at(key);
  if (!v.is_array() || v.size() != 3) {
    throw std::runtime_error(std::string(key) + " must be an array of 3 numbers");
  }
  out = glm::vec3(v.at(0).get<float>(),
                  v.at(1).get<float>(),
                  v.at(2).get<float>());
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

void ApplyVolumeJson(const json& obj,
                     VolumeRenderingRequestState& request,
                     VolumeRenderState& render)
{
  if (obj.contains("quantity")) request.selectedQuantity = ParseQuantity(obj.at("quantity"));
  if (obj.contains("minParticlesPerLeaf")) {
    request.minParticlesPerLeaf = obj.at("minParticlesPerLeaf").get<int>();
  }
  if (obj.contains("maxTreeLevel")) request.maxTreeLevel = obj.at("maxTreeLevel").get<int>();
  if (obj.contains("logScale")) request.logScale = obj.at("logScale").get<bool>();
  if (obj.contains("autoRange")) request.autoRange = obj.at("autoRange").get<bool>();
  if (obj.contains("valueMin")) request.valueMin = obj.at("valueMin").get<float>();
  if (obj.contains("valueMax")) request.valueMax = obj.at("valueMax").get<float>();
  request.cornerReconstructionMode =
    obj.value("cornerReconstructionMode", request.cornerReconstructionMode);

  render.show = true;
  render.pixelThreshold = obj.value("pixelThreshold", 2.0f);
  render.tauMax = obj.value("tauMax", 1.0f);
  render.stepBias = obj.value("stepBias", 0.0f);
  render.maxSamplesPerCell = obj.value("maxSamplesPerCell", 32);
  render.skipEpsilon = obj.value("skipEpsilon", 1.0e-4f);
  render.debugMode = obj.value("debugMode", 0);
  render.colorMode = obj.value("colorMode", 0);
  render.colormapIndex = obj.value("colormapIndex", render.colormapIndex);
  render.tfSigmaScale = 1.0f;
  render.tfLogScale = request.logScale;
}

void ConfigureTransferFunction(const json& obj,
                               const VolumeRenderingRequestState& request,
                               VolumeRenderState& render)
{
  render.tfValueMin = request.valueMin;
  render.tfValueMax = request.valueMax;
  render.tfComponents.clear();
  render.tfMaxSigma = 0.0f;

  if (obj.contains("baseColor")) {
    glm::vec3 color = render.baseColor;
    ApplyJsonVec3(obj, "baseColor", color);
    render.baseColor = color;
  }

  if (obj.contains("tfComponents")) {
    const auto& comps = obj.at("tfComponents");
    if (!comps.is_array()) {
      throw std::runtime_error("volume.tfComponents must be an array");
    }
    for (const auto& src : comps) {
      if (render.tfComponents.size() >= kMaxVolumeTransferComponents) break;
      VolumeTransferFunctionComponent comp;
      comp.type = src.value("type", 0);
      comp.center = src.value("center", 1.0f);
      comp.width = src.value("width", 1.0f);
      comp.amplitude = src.value("amplitude", 1.0f);
      comp.logDomain = src.value("logDomain", request.logScale);
      render.tfComponents.push_back(comp);
      render.tfMaxSigma =
        std::max(render.tfMaxSigma, std::max(comp.amplitude, 0.0f));
    }
  }

  if (render.tfComponents.empty()) {
    VolumeTransferFunctionComponent comp;
    comp.type = 0;
    comp.center = obj.value("gaussianCenter", 100.0f);
    comp.width = obj.value("gaussianLogWidth", 2.0f);
    comp.logDomain = obj.value("gaussianLogDomain", true);
    const float sigmaAtDensity1 =
      obj.value("sigmaAtDensity1", obj.value("opacity", 1.0f));
    const float dx =
      comp.logDomain
        ? (0.0f - std::log10(std::max(comp.center, 1.0e-30f))) /
            std::max(comp.width, 1.0e-12f)
        : (1.0f - comp.center) / std::max(comp.width, 1.0e-12f);
    const float weightAtDensity1 = std::exp(-0.5f * dx * dx);
    comp.amplitude = obj.value(
      "gaussianAmplitude",
      sigmaAtDensity1 / std::max(weightAtDensity1, 1.0e-30f));
    render.tfComponents.push_back(comp);
    render.tfMaxSigma = std::max(comp.amplitude, 0.0f);
  }
}

bool FindMostMassiveParticleTarget(const SimulationBlock& block,
                                   int type,
                                   glm::vec3& outTarget)
{
  bool found = false;
  float bestMass = -1.0f;
  for (const SimulationElement& p : block.particles) {
    if (static_cast<int>(p.type) != type) {
      continue;
    }
    if (!found || p.mass > bestMass) {
      bestMass = p.mass;
      outTarget = renderPosition(p, block.worldToRenderScale);
      found = true;
    }
  }
  return found;
}

void ApplyBatchCameraJson(const json& root,
                          const SimulationBlock& block,
                          CameraContext& camera)
{
  camera = CameraContext{};
  glm::vec3 direction =
    glm::normalize(camera.cameraPos - camera.cameraTarget);
  float distance = camera.distance;

  if (!root.contains("camera")) {
    return;
  }

  const json& cameraJob = root.at("camera");
  if (cameraJob.contains("targetMostMassiveType")) {
    const int type = cameraJob.at("targetMostMassiveType").get<int>();
    glm::vec3 target;
    if (!FindMostMassiveParticleTarget(block, type, target)) {
      throw std::runtime_error("no particle found for targetMostMassiveType=" +
                               std::to_string(type));
    }
    camera.cameraTarget = target;
  }
  ApplyJsonVec3(cameraJob, "target", camera.cameraTarget);
  ApplyJsonVec3(cameraJob, "up", camera.cameraUp);

  if (cameraJob.contains("position")) {
    ApplyJsonVec3(cameraJob, "position", camera.cameraPos);
    camera.distance = glm::length(camera.cameraPos - camera.cameraTarget);
    return;
  }

  if (cameraJob.contains("distance")) {
    distance = cameraJob.at("distance").get<float>();
  }
  camera.distance = distance;
  camera.cameraPos = camera.cameraTarget + direction * distance;
}

bool LoadSnapshotForBatch(AppState& app, const json& root)
{
  const bool synthetic = root.value("syntheticTestData", true);
  if (root.contains("snapshot")) {
    ApplySnapshotJson(root.at("snapshot"), app.runtime.settings.fileNavigation);
  }

  const SnapshotLoadKind kind =
    synthetic ? SnapshotLoadKind::GenerateTestData : SnapshotLoadKind::FileStep;
  const int step = app.runtime.settings.fileNavigation.navigation.currentStep;
  RequestSnapshotLoad(app.runtime.snapshotLoad,
                      SnapshotLoadOwner::UserNavigation,
                      step,
                      100,
                      kind);

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
  }
  std::cerr << "snapshot load timed out\n";
  return false;
}

void ApplyConfigForBatch(AppState& app, const json& root)
{
  const std::string configFile = root.value("configFile", "");
  if (configFile.empty()) {
    return;
  }

  ConfigData config;
  if (!LoadConfigFile(configFile, config)) {
    std::cerr << "volume gpu batch: config not loaded: "
              << configFile << "\n";
    return;
  }

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
  app.runtime.quantity.units.updateDerived();
  app.runtime.quantity.conversion.displaySpace =
    app.runtime.quantity.units.useComovingCoordinate
      ? UnitSpace::Comoving
      : UnitSpace::Physical;
  app.runtime.quantity.rebuildConversion(1.0);
}

RenderViewport MakeBatchViewport(WindowContext& window)
{
  RenderViewport viewport;
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = window.framebufferWidth();
  viewport.height = window.framebufferHeight();
  viewport.framebufferWidth = window.framebufferWidth();
  viewport.framebufferHeight = window.framebufferHeight();
  viewport.framebufferScaleX = 1.0f;
  viewport.framebufferScaleY = 1.0f;
  return viewport;
}

void WriteResultJson(const std::string& path,
                     const std::string& backend,
                     const VolumeRenderingRequestState& request,
                     const VolumeRenderState& render,
                     const CameraContext& camera,
                     const VolumeRenderingResultState& volume,
                     const RenderBackendTimingInfo& timing,
                     const RenderBackendVolumeStats& stats,
                     double treeBuildSeconds,
                     int framesRun)
{
  json out;
  out["backend"] = backend;
  out["framesRun"] = framesRun;
  out["treeBuildSeconds"] = treeBuildSeconds;
  out["volume"] = {
    {"valid", volume.valid},
    {"message", volume.message},
    {"nodeCount", volume.stats.nodeCount},
    {"leafCount", volume.stats.leafCount},
    {"emptyNodesDropped", volume.stats.emptyNodesDropped},
    {"particleCount", volume.stats.particleCount},
    {"maxDepth", volume.stats.maxDepth},
    {"sigmaMax", volume.stats.sigmaMax}
  };
  out["request"] = {
    {"quantity", QuantityLabel(request.selectedQuantity)},
    {"minParticlesPerLeaf", request.minParticlesPerLeaf},
    {"maxTreeLevel", request.maxTreeLevel},
    {"cornerReconstructionMode", request.cornerReconstructionMode},
    {"valueMin", request.valueMin},
    {"valueMax", request.valueMax},
    {"logScale", request.logScale}
  };
  out["camera"] = {
    {"position", {camera.cameraPos.x, camera.cameraPos.y, camera.cameraPos.z}},
    {"target", {camera.cameraTarget.x, camera.cameraTarget.y, camera.cameraTarget.z}},
    {"distance", camera.distance}
  };
  if (!render.tfComponents.empty()) {
    const VolumeTransferFunctionComponent& comp = render.tfComponents.front();
    out["transferFunction"] = {
      {"componentCount", render.tfComponents.size()},
      {"firstType", comp.type},
      {"firstCenter", comp.center},
      {"firstWidth", comp.width},
      {"firstAmplitude", comp.amplitude},
      {"firstLogDomain", comp.logDomain},
      {"maxSigma", render.tfMaxSigma}
    };
  }
  out["timing"] = {
    {"volumeGpuTimeKnown", timing.volumeGpuTimeKnown},
    {"volumeGpuMs", timing.volumeGpuMs},
    {"volumeWallLatencyKnown", timing.volumeWallLatencyKnown},
    {"volumeWallLatencyMs", timing.volumeWallLatencyMs},
    {"volumeCacheUsed", timing.volumeCacheUsed},
    {"volumeCacheUpdated", timing.volumeCacheUpdated},
    {"volumeCacheHit", timing.volumeCacheHit},
    {"volumeCacheScale", timing.volumeCacheScale}
  };
  out["gpuTraversalStats"] = {
    {"known", stats.known},
    {"width", stats.width},
    {"height", stats.height},
    {"sampleStep", stats.sampleStep},
    {"sampledRays", stats.sampledRays},
    {"rootHitFraction", stats.rootHitFraction},
    {"avgNodeVisitsPerRay", stats.avgNodeVisitsPerRay},
    {"avgChildHitsPerRay", stats.avgChildHitsPerRay},
    {"avgLeafStopsPerRay", stats.avgLeafStopsPerRay},
    {"nodeVisits", stats.nodeVisits},
    {"childHits", stats.childHits},
    {"leafStops", stats.leafStops}
  };
  std::ofstream file(path);
  if (!file) {
    throw std::runtime_error("failed to write output json: " + path);
  }
  file << out.dump(2) << '\n';
}

} // namespace

int RunVolumeGpuBatchFromJson(const char* path)
{
  json root;
  if (!ReadJsonFile(path, root)) {
    return EXIT_FAILURE;
  }

  try {
    const std::string backend = root.value("backend", "opengl");
    const int width = std::max(1, root.value("width", 640));
    const int height = std::max(1, root.value("height", 480));
    const int frames = std::max(1, root.value("frames", 4));
    const std::string outputFile =
      root.value("outputFile", "volume_gpu_batch_result.json");

    SetEnv("PARTICLE_VIS_RENDER_BACKEND", backend);
    SetEnv("PARTICLE_VIS_HIDDEN_WINDOW", "1");
    SetEnv("PARTICLE_VIS_WINDOW_WIDTH", std::to_string(width));
    SetEnv("PARTICLE_VIS_WINDOW_HEIGHT", std::to_string(height));

    PlatformSession platform;
    AppState app;
    RenderSystem render;
    CallbackContext callbackCtx;

    if (!platform.init(app, callbackCtx)) {
      std::cerr << "failed to initialize platform for GPU volume batch\n";
      return EXIT_FAILURE;
    }

    if (std::unique_ptr<RenderBackend> platformBackend =
          platform.createRenderBackend()) {
      render.backend = std::move(platformBackend);
    }
    InitApplication(app, render);
    ApplyConfigForBatch(app, root);

    bool ok = false;
    int framesRun = 0;
    double treeBuildSeconds = 0.0;
    try {
      ok = LoadSnapshotForBatch(app, root);
      if (!ok) {
        throw std::runtime_error("failed to load batch snapshot");
      }

      VolumeRenderingRequestState request;
      VolumeRenderState& volumeRender = app.runtime.render.volume;
      const json volumeJob = root.value("volume", json::object());
      ApplyVolumeJson(volumeJob, request, volumeRender);
      request.buildRequested = true;

      const auto treeT0 = std::chrono::steady_clock::now();
      ExecuteVolumeRenderingRequest(*app.data.particles,
                                    request,
                                    app.derived.analysis.volume,
                                    volumeRender);
      const auto treeT1 = std::chrono::steady_clock::now();
      treeBuildSeconds =
        std::chrono::duration<double>(treeT1 - treeT0).count();
      if (!app.derived.analysis.volume.valid) {
        throw std::runtime_error("volume tree build failed: " +
                                 app.derived.analysis.volume.message);
      }
      ConfigureTransferFunction(volumeJob, request, volumeRender);

      render.scene.volume = app.derived.analysis.volume.tree;
      ++render.scene.volumeVersion;

      ApplyBatchCameraJson(root,
                           app.data.particles->simulationBlock,
                           app.view.camera);

      const RenderViewport batchViewport = MakeBatchViewport(platform.window());

      app.runtime.render.scheduling.cacheVolumeFrames =
        root.value("cacheVolumeFrames", true);
      app.runtime.render.scheduling.volumeFrameCacheScale =
        root.value("volumeFrameCacheScale", 1.0f);

      if (backend == "vulkan" || backend == "vk") {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      for (int i = 0; i < frames; ++i) {
        std::cerr << "volume gpu batch frame " << (i + 1)
                  << "/" << frames << "\n";
        platform.window().pollEvents();
        if (!BeginImGuiFrame(platform.window().framebufferWidth(),
                             platform.window().framebufferHeight())) {
          continue;
        }

        UpdateRenderFrameInput(app.renderFrameInput,
                               batchViewport,
                               app.view.camera,
                               app.runtime.particleVisual,
                               app.runtime.render,
                               app.derived.overlay);
        PrepareRenderFrame(app.renderFrameInput, render);
        RenderScene(render);
        platform.presenter().present();
        if (render.backend) {
          render.backend->waitIdle();
        }
        ++framesRun;
      }

      const RenderBackendTimingInfo timing =
        render.backend ? render.backend->queryTimingInfo()
                       : RenderBackendTimingInfo{};
      const int volumeStatsSampleStep =
        std::max(1, root.value("volumeStatsSampleStep", 8));
      const RenderBackendVolumeStats volumeStats =
        render.backend ? render.backend->queryVolumeStats(volumeStatsSampleStep)
                       : RenderBackendVolumeStats{};
      WriteResultJson(outputFile,
                      backend,
                      request,
                      volumeRender,
                      app.view.camera,
                      app.derived.analysis.volume,
                      timing,
                      volumeStats,
                      treeBuildSeconds,
                      framesRun);
      std::cout << "wrote " << outputFile << "\n";
      ok = true;
    } catch (const std::exception& e) {
      std::cerr << e.what() << "\n";
      ok = false;
    }

    DestroyRenderSystem(render);
    delete app.data.particles;
    app.data.particles = nullptr;
    platform.shutdown();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }
}

#else

#include <cstdlib>
#include <iostream>

int RunVolumeGpuBatchFromJson(const char* path)
{
  (void)path;
  std::cerr << "volume GPU batch requires VOLUME_RENDERING=ON\n";
  return EXIT_FAILURE;
}

#endif
