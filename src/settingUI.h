#pragma once

struct CameraContext;
struct ParticleVisualConfig;
struct RenderRuntimeState;
struct AnalysisDerivedState;
struct ToolWindowUIState;
struct SettingsUIContext;
struct AppRuntimeState;
struct QuantityState;

struct SettingsUIContext {
  QuantityState* quantity = nullptr;
  CameraContext* camCtx = nullptr;
  ParticleVisualConfig* particleVisual = nullptr;
  RenderRuntimeState* render = nullptr;
  AnalysisDerivedState* analysis = nullptr;
  ToolWindowUIState* windows = nullptr;
};

void ShowSettingsUI(SettingsUIContext& ctx, AppRuntimeState& rt);
