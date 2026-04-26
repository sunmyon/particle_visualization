#pragma once

#include "core/tracking_vector.h"
#include "scene_manager.h"
#include "app/runtime_state.h"
#include "app/overlay_state.h"
#include "app/analysis_state.h"
#include "app/ui_state.h"
#include "app/tool_window_state.h"
#include "app/window_commands.h"
#include "app/app_render_sync.h"
#include "interaction/camera.h"
#include "interaction/input_event.h"
#include "interaction/interaction_utils.h"
#include "render/render_frame.h"
#include "data/clump_store.h"
#include "data/halo_store.h"

class ParticleArray;
class WindowContext;

#include "app_services.h"

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
