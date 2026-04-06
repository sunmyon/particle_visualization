#pragma once

#include "interaction/camera.h"
#include "ui_state.h"
#include "scene_manager.h"
#include "particle_visual_config.h"
#include "particle_label_overlay.h"
#include "render/gizmo_renderer.h"
#include "interaction/interaction_utils.h"

class ParticleArray;
class FileInfo;
class WindowContext;

#include "app_services.h"

#ifdef USE_CONVEX_HULL
#include "FindClumps/create_convex_hull.h"
#endif

struct AppState {
  AppServices services;

  CameraContext camera;
  SettingsRuntimeState settings;
  RenderRuntimeState render;
  ParticleVisualConfig particleVisual;

  SceneManagers scene;

  ParticleArray* particles = nullptr;
  FileInfo* fileInfo = nullptr;

  InteractionState interaction;
  ParticleLabelOverlay particleLabels;

#ifdef USE_CONVEX_HULL
  ConvexHullGenerator* convexHullGenerator = nullptr;
#endif
};

struct CallbackContext {
  AppState* app = nullptr;
  WindowContext* window = nullptr;
};
