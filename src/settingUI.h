#pragma once

#include "app/runtime_state.h"
#include "app/analysis_state.h"
#include "app/app_state.h"

struct AppServices;
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
