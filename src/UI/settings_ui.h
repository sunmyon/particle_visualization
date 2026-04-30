#pragma once

#include <cstddef>

#include "render/render_backend.h"

struct CameraContext;
struct WindowCommandQueue;
struct SettingsUIState;
struct SettingsRuntimeState;
struct AnalysisJobRuntimeState;
struct RenderRuntimeState;
struct ParticleVisualConfig;
struct QuantityState;
struct ClumpBatchResultState;
struct DiskAnalysisResultState;
struct DiskAnalysisBatchResultState;
struct EllipsoidAnalysisResultState;
struct EllipsoidAnalysisBatchResultState;
struct ProjectionMovieResultState;
struct StreamlineBuildResultState;
struct VolumeRenderingResultState;
struct PythonBridgeViewState;
struct RenderSceneData;
struct RenderViewport;

struct SettingsMemoryView {
  size_t particleCount = 0;
  size_t renderParticleCount = 0;
  size_t particleLodProxyCount = 0;
  size_t particleLodNodeCount = 0;
  size_t volumeNodeCount = 0;
  size_t cpuParticleBytes = 0;
  size_t cpuRenderSceneBytes = 0;
  size_t gpuParticleBufferBytes = 0;
  size_t cpuParticleLodTreeBytes = 0;
  size_t gpuParticleCacheBytes = 0;
  size_t gpuVolumeTreeBytes = 0;
  size_t gpuVolumeCacheBytes = 0;
  bool systemAvailableKnown = false;
  size_t systemAvailableBytes = 0;
  bool gpuAvailableKnown = false;
  size_t gpuAvailableBytes = 0;
  RenderBackendTimingInfo timing;
};

struct SettingsCameraView {
  float position[3] = {0.f, 0.f, 0.f};
  float target[3] = {0.f, 0.f, 0.f};
};

struct SettingsAnalysisResultView {
#ifdef CLUMP_DATA_READ
  const ClumpBatchResultState* clumpBatch = nullptr;
#endif
  const DiskAnalysisResultState* disk = nullptr;
  const DiskAnalysisBatchResultState* diskBatch = nullptr;
  const EllipsoidAnalysisResultState* ellipsoid = nullptr;
  const EllipsoidAnalysisBatchResultState* ellipsoidBatch = nullptr;
  const StreamlineBuildResultState* streamlineBuild = nullptr;
#ifdef VOLUME_RENDERING
  const VolumeRenderingResultState* volume = nullptr;
#endif
  const ProjectionMovieResultState* projectionMovie = nullptr;
};

struct SettingsViewContext {
  SettingsCameraView camera;
  SettingsAnalysisResultView analysis;
  SettingsMemoryView memory;
  RenderBackendCapabilities backend;
#ifdef PYTHON_BRIDGE
  const PythonBridgeViewState* pythonBridge = nullptr;
#endif
  bool snapshotLoading = false;
};

void ShowSettingsUI(SettingsUIState& ui,
                    SettingsRuntimeState& settings,
                    const AnalysisJobRuntimeState& analysisJobs,
                    const RenderRuntimeState& render,
                    const ParticleVisualConfig& particleVisual,
                    const QuantityState& quantity,
                    const SettingsViewContext& view,
                    WindowCommandQueue& windowCommands);
