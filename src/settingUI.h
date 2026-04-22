#pragma once

class ParticleArray;
class FileInfo;
struct CameraContext;
struct ParticleVisualConfig;
struct RenderRuntimeState;
struct AnalysisDerivedState;
struct ToolWindowUIState;
struct SettingsUIContext;
struct AppRuntimeState;

struct SettingsUIContext {
  ParticleArray* P = nullptr;
  FileInfo* fileInfo = nullptr;
  CameraContext* camCtx = nullptr;
  ParticleVisualConfig* particleVisual = nullptr;
  RenderRuntimeState* render = nullptr;
  AnalysisDerivedState* analysis = nullptr;
  ToolWindowUIState* windows = nullptr;
};

void ShowSettingsUI(SettingsUIContext& ctx, AppRuntimeState& rt);
