#pragma once

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
