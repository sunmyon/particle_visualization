#pragma once

#include <vector>
#include "app/state/scene_state.h"
#include "app/state/runtime_state.h"
#include "app/state/overlay_state.h"
#include "app/state/analysis_state.h"
#include "app/state/ui_state.h"
#include "app/state/tool_window_state.h"
#include "app/state/window_commands.h"
#include "app/app_render_sync.h"
#include "interaction/camera.h"
#include "interaction/input_event.h"
#include "interaction/interaction_utils.h"
#include "render/render_frame.h"
#include "data/clump_store.h"
#include "data/halo_store.h"

class ParticleArray;
class WindowContext;

#include "app/app_services.h"

struct AppDataState {
  ParticleArray* particles = nullptr;
  ClumpStore clumpStore;
  HaloStore haloStore;
};

struct AppViewState {
  CameraContext camera;
};

struct AppUIState {
  SettingsUIState settings;
  ToolWindowUIState toolWindows;
  WindowCommandQueue windowCommands;
};

struct AppRuntimeState {
  InteractionState interaction;
  InputEventQueue inputEvents;
  SnapshotLoadRuntimeState snapshotLoad;

  RenderRuntimeState render;
  AnalysisRequestState analysisRequests;
  AnalysisViewState analysisView;
  AnalysisToolState analysisTools;
  AnalysisJobRuntimeState analysisJobs;
  ParticleVisualConfig particleVisual;
  QuantityState quantity;
  SnapshotPostprocessState snapshotPostprocess;

  SettingsRuntimeState settings;
};

struct AppDerivedState {
  SceneManagers scene;
  OverlayState overlay;
  AnalysisDerivedState analysis;
};

struct AppState {
  AppServices services;
  AppDataState data;
  AppViewState view;
  AppRuntimeState runtime;
  AppUIState ui;
  AppDerivedState derived;
  RenderFrameInput renderFrameInput;
};

struct CallbackContext {
  AppState* app = nullptr;
  WindowContext* window = nullptr;
};
