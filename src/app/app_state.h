#pragma once

#include "interaction/camera.h"
#include "ui_state.h"
#include "scene_manager.h"
#include "particle_visual_config.h"
#include "overlay_state.h"
#include "analysis_state.h"
#include "render/gizmo_renderer.h"
#include "interaction/interaction_utils.h"

class ParticleArray;
class FileInfo;
class WindowContext;

#include "app_services.h"

struct AppState {
  AppServices services;

  CameraContext camera;
  InteractionState interaction;

  SettingsRuntimeState settings;
  RenderRuntimeState render;
  ParticleVisualConfig particleVisual;

  SceneManagers scene;
  OverlayState overlay;
  AnalysisState analysis;
  
  ParticleArray* particles = nullptr;
  FileInfo* fileInfo = nullptr;
};

struct CallbackContext {
  AppState* app = nullptr;
  WindowContext* window = nullptr;
};
