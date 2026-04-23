#pragma once

#include "core/tracking_vector.h"
#include "core/quantity.h"
#include "scene_manager.h"
#include "particle_visual_config.h"
#include "app/runtime_state.h"
#include "app/overlay_state.h"
#include "app/analysis_state.h"
#include "app/ui_state.h"
#include "app/render_runtime_state.h"
#include "interaction/camera.h"
#include "interaction/interaction_utils.h"
#include "data/clump_store.h"
#include "data/halo_store.h"
#include "data/header_info.h"

class ParticleArray;
class FileInfo;
class WindowContext;

#include "app_services.h"

struct AppDataState {
  ParticleArray* particles = nullptr;
  ClumpStore clumpStore;
  HaloStore haloStore;
  FileInfo* fileInfo = nullptr;

  HeaderInfo header;
  QuantityCatalogState quantity;
};

struct AppViewState {
  CameraContext camera;
  ParticleVisualConfig particleVisual;
};

struct AppUIState {
  ToolWindowUIState toolWindows;
};

struct AppRuntimeState {
  InteractionState interaction;
  SettingsRuntimeState settings;
  RenderRuntimeState render;
  AnalysisRequestRuntimeState analysis;
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
};

struct CallbackContext {
  AppState* app = nullptr;
  WindowContext* window = nullptr;
};
