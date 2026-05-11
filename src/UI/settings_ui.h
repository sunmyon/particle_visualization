#pragma once

#include <cstddef>
#include <string>

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
struct PowerSpectrumResultState;
struct IsoContourGeometryState;
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
  bool processMemoryKnown = false;
  size_t processMemoryBytes = 0;
  bool gpuAvailableKnown = false;
  size_t gpuAvailableBytes = 0;
  bool gpuAllocatedKnown = false;
  size_t gpuAllocatedBytes = 0;
  bool gpuBudgetKnown = false;
  size_t gpuBudgetBytes = 0;
  std::string gpuDeviceName;
  RenderBackendTimingInfo timing;
};

struct SettingsCameraView {
  float position[3] = {0.f, 0.f, 0.f};
  float target[3] = {0.f, 0.f, 0.f};
  float originalPosition[3] = {0.f, 0.f, 0.f};
  float originalTarget[3] = {0.f, 0.f, 0.f};
  float renderToWorldScale = 1.0f;
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
#ifdef POWER_SPECTRUM
  const PowerSpectrumResultState* powerSpectrum = nullptr;
#endif
#ifdef ISO_CONTOUR
  const IsoContourGeometryState* isoContour = nullptr;
#endif
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
